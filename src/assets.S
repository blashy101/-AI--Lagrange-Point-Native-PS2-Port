.section .rodata

.macro lp_asset name, path
.balign 64
.global \name
.global \name\()_end
\name:
.incbin "\path"
\name\()_end:
.endm

lp_asset lp_english, "build/generated/en.nes"
lp_asset lp_japanese, "build/generated/ja.nes"
lp_asset lp_audsrv, AUDSRV_IRX
lp_asset lp_libsd, LIBSD_IRX
.balign 64

lp_asset lp_mcman, MCMAN_IRX
lp_asset lp_mcserv, MCSERV_IRX
lp_asset lp_sio2man, SIO2MAN_IRX
lp_asset lp_padman, PADMAN_IRX
lp_asset lp_save_icon, "assets/save.icn"
.balign 64
