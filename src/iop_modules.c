/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "iop_modules.h"
#include <kernel.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <sifcmd.h>
#include <sifdma.h>
#include <sifrpc.h>
#include <smod.h>
#include <stdint.h>
#include <string.h>
#include <timer.h>

extern const uint8_t lp_sio2man[], lp_sio2man_end[], lp_padman[], lp_padman_end[];
extern const uint8_t lp_mcman[], lp_mcman_end[], lp_mcserv[], lp_mcserv_end[];
extern const uint8_t lp_libsd[], lp_libsd_end[], lp_audsrv[], lp_audsrv_end[];

static int buffer_loading;
static SifRpcClientData_t probes[6] __attribute__((aligned(64)));

static bool request_clean_reset(void)
{
    /* The executable, both games and every IRX are already in EE RAM. Send
     * the SDK reset command without asking OPL to restore boot-device drivers.
     * Its reset hook recognizes a complete reset packet in the first DMA
     * entry; a header-only first entry leaves this two-entry DMA to the SDK.
     * This also detaches MX4SIO from SIO2 before pad/card drivers start. */
    static union {
        SifCmdResetData_t command;
        uint8_t padded[128];
    } packet __attribute__((aligned(64)));
    void *destination = (void *)(uintptr_t)sceSifGetReg(SIF_SYSREG_SUBADDR);
    if (!destination) return false;
    packet.command.header.psize = sizeof(packet.command);
    packet.command.header.cid = SIF_CMD_RESET_CMD;
    SifDmaTransfer_t transfers[] = {
        {&packet.command, destination, sizeof(SifCmdHeader_t), 0},
        {&packet.command, destination, sizeof(packet.command), SIF_DMA_ERT | SIF_DMA_INT_O},
    };
    sceSifStopDma();
    sceSifWriteBackDCache(&packet, sizeof(packet));
    sceSifSetReg(SIF_REG_SMFLAG, SIF_STAT_BOOTEND);
    if (!sceSifSetDma(transfers, 2)) return false;
    extern int _iop_reboot_count;
    ++_iop_reboot_count;
    sceSifSetReg(SIF_REG_SMFLAG, SIF_STAT_SIFINIT);
    sceSifSetReg(SIF_REG_SMFLAG, SIF_STAT_CMDINIT);
    sceSifSetReg(SIF_SYSREG_RPCINIT, 0);
    sceSifSetReg(SIF_SYSREG_SUBADDR, 0);
    return true;
}

static bool resident(const char *const *names)
{
    smod_mod_info_t info;
    for (; *names; ++names)
        /* LOADFILE's search RPC is absent on older ROM versions. Inspect the
         * public IOP module list instead, as the SDK's own loader patches do. */
        if (smod_get_mod_by_name(*names, &info) > 0 && info.id != 0)
            return true;
    return false;
}

static const char *reset_iop(void)
{
    SifExitRpc();
    uint64_t deadline = GetTimerSystemTime() + MSec2TimerBusClock(3000);
    while (!request_clean_reset())
        if (GetTimerSystemTime() >= deadline) return "IOP RESET REQUEST TIMED OUT";
    deadline = GetTimerSystemTime() + MSec2TimerBusClock(3000);
    while (!SifIopSync())
        if (GetTimerSystemTime() >= deadline) return "IOP REBOOT TIMED OUT";
    SifInitRpc(0);
    static const char *const storage[] = {"mx4sio", "usbmass_bd", "USB_mass", NULL};
    if (resident(storage)) return "BOOT DEVICE DRIVER STILL ACTIVE";
    return NULL;
}

static bool module(const char *const *names, const char *rom,
                   const uint8_t *begin, const uint8_t *end)
{
    if (resident(names))
        return true;
    int result = -1;
    int id = rom ? SifLoadStartModule(rom, 0, NULL, &result) : -1;
    if (id >= 0 && ((result & 3) == 0 || (result & 3) == 2))
        return true;
    if (!buffer_loading) {
        /* Already-patched loaders and OpenPS2BIOS can support buffer loading
         * even when the optional Sony LOADFILE patch pattern is absent. */
        (void)sbv_patch_enable_lmb();
        buffer_loading = 1;
    }
    result = -1;
    id = SifExecModuleBuffer((void *)begin, (uintptr_t)end - (uintptr_t)begin, 0, NULL, &result);
    return id >= 0 && ((result & 3) == 0 || (result & 3) == 2);
}

/* SDK clients otherwise bind indefinitely if a driver failed to register.
 * Keep timed-out descriptors alive: a late IOP reply must not target the stack. */
static bool service(unsigned first, const unsigned *ids, unsigned count)
{
    uint64_t deadline = GetTimerSystemTime() + MSec2TimerBusClock(2000);
    uint64_t next_attempt[4] = {0};
    bool pending[4] = {0};
    unsigned found = 0;
    while (GetTimerSystemTime() < deadline) {
        for (unsigned i = 0; i < count; ++i) {
            SifRpcClientData_t *probe = &probes[first + i];
            if (found & (1u << i)) continue;
            if (!pending[i] && GetTimerSystemTime() >= next_attempt[i]) {
                memset(probe, 0, sizeof(*probe));
                pending[i] = SifBindRpc(probe, ids[i], SIF_RPC_M_NOWAIT) >= 0;
                next_attempt[i] = GetTimerSystemTime() + MSec2TimerBusClock(2);
            }
            if (pending[i] && !SifCheckStatRpc(probe)) {
                if (probe->server) found |= 1u << i;
                pending[i] = false;
            }
        }
        if (count == 1 ? found != 0 : (found & 3) == 3 || (found & 12) == 12)
            return true;
    }
    return false;
}

struct lp_iop_services lp_iop_modules_init(void)
{
    static const char *const sio2_names[] = {"sio2man", "sio2_manager", "freesio2", "rsio2man", NULL};
    static const char *const pad_names[] = {"padman", "padman_old", "freepad", "rpadman", NULL};
    static const char *const mcman_names[] = {"mcman", "mcman_sony", NULL};
    static const char *const mcserv_names[] = {"mcserv", NULL};
    static const char *const sd_names[] = {"libsd", "sd_driver", NULL};
    static const char *const audio_names[] = {"audsrv", NULL};
    struct lp_iop_services ready = {0};
    SifInitRpc(0);
    ready.error = reset_iop();
    if (ready.error) return ready;
    module(sio2_names, "rom0:SIO2MAN", lp_sio2man, lp_sio2man_end);
    module(pad_names, "rom0:PADMAN", lp_padman, lp_padman_end);
    module(mcman_names, "rom0:MCMAN", lp_mcman, lp_mcman_end);
    module(mcserv_names, "rom0:MCSERV", lp_mcserv, lp_mcserv_end);
    module(sd_names, "rom0:LIBSD", lp_libsd, lp_libsd_end);
    module(audio_names, NULL, lp_audsrv, lp_audsrv_end);
    /* A loader may already provide a service even if its module has an
     * unfamiliar name or a duplicate load was rejected. Probe the service
     * itself before deciding whether the SDK client can initialize. */
    {
        /* New and old PADMAN each expose two endpoints. */
        static const unsigned ids[] = {0x80000100, 0x80000101, 0x8000010f, 0x8000011f};
        ready.pad = service(0, ids, 4);
    }
    const unsigned card_id = 0x80000400, audio_id = 0x0870884e;
    ready.card = service(4, &card_id, 1);
    ready.audio = service(5, &audio_id, 1);
    return ready;
}
