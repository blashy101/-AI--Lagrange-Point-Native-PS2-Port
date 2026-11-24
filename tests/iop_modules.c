/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "iop_modules.h"
#include <assert.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <sifcmd.h>
#include <sifdma.h>
#include <smod.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *scenario;
static bool loaded[6];
static unsigned rom_loads,buffer_loads,patches,audio_binds,rpc_searches;
static uint64_t ticks;
static unsigned resets, exits, flags, cache_invalidations;
int _iop_reboot_count;
void SifExitRpc(void){++exits;}
unsigned sceSifGetReg(unsigned reg){assert(reg==SIF_SYSREG_SUBADDR);return !strcmp(scenario,"reset-request-timeout")?0:0x123400;}
void sceSifSetReg(unsigned reg,unsigned value){
 if(reg==SIF_REG_SMFLAG){assert(value==SIF_STAT_BOOTEND||value==SIF_STAT_SIFINIT||value==SIF_STAT_CMDINIT);++flags;}
 else {assert(reg==SIF_SYSREG_RPCINIT||reg==SIF_SYSREG_SUBADDR);assert(!value);++cache_invalidations;}
}
void sceSifStopDma(void){}
void sceSifWriteBackDCache(void *data,int size){assert(!((uintptr_t)data&63)&&size==128);}
int sceSifSetDma(SifDmaTransfer_t *t,int count){
 assert(count==2 && t[0].src==t[1].src && t[0].dest==t[1].dest);
 assert(t[0].size==sizeof(SifCmdHeader_t)&&t[0].attr==0);
 assert(t[1].size==sizeof(SifCmdResetData_t)&&t[1].attr==(SIF_DMA_ERT|SIF_DMA_INT_O));
 SifCmdResetData_t *p=t[1].src;assert(p->header.psize==sizeof(*p)&&p->header.cid==SIF_CMD_RESET_CMD);
 assert(!p->arglen&&!p->mode&&!p->arg[0]);
 if(!strcmp(scenario,"reset-dma-timeout"))return 0;
 ++resets;memset(loaded,0,sizeof(loaded));return 1;
}
int SifIopSync(void){return strcmp(scenario,"reset-sync-timeout")!=0;}
void SifInitRpc(int mode){assert(mode==0);}
uint64_t GetTimerSystemTime(void){return ++ticks;}
static int driver(const char *s){
 const char *names[]={"SIO2MAN","PADMAN","MCMAN","MCSERV","LIBSD","AUDSRV"};
 for(unsigned i=0;i<6;++i)if(strstr(s,names[i]))return i;
 return -1;
}
static bool imports_ready(unsigned id){
 return !((id==1||id==2)&&!loaded[0])&&!(id==3&&!loaded[2])&&!(id==5&&!loaded[4]);
}
static int lookup(const char *name){
 if(!strcmp(name,"mx4sio") && ((!strcmp(scenario,"mx4sio")&&!resets)||!strcmp(scenario,"resident-mx4sio")))return 99;
 if(!strcmp(name,"usbmass_bd")&&!strcmp(scenario,"usb")&&!resets)return 98;
 const char *names[]={"sio2man","padman","mcman","mcserv","libsd","audsrv"};
 if(!strcmp(name,"freesio2")&&!strcmp(scenario,"opl")&&loaded[0])return 10;
 for(unsigned i=0;i<6;++i)if(!strcmp(name,names[i])&&loaded[i]){
  if(i==0&&!strcmp(scenario,"opl"))return -1;
  return 10+i;
 }
 return -1;
}
int SifSearchModuleByName(const char *name){
 ++rpc_searches;
 if(!strcmp(scenario,"legacy-search-zero"))return 0;
 if(!strcmp(scenario,"legacy-search-positive"))return 0x100;
 return lookup(name);
}
int smod_get_mod_by_name(const char *name,smod_mod_info_t *info){
 if(!strcmp(scenario,"opaque-list"))return 0;
 int id=lookup(name);if(id<0)return 0;info->id=id;return id;
}
int SifLoadStartModule(const char *path,int size,const char *args,int *result){
 assert(!size&&!args);assert(!strncmp(path,"rom0:",5)); /* No CD, USB or SD reads. */
 ++rom_loads;int id=driver(path);assert(id>=0);if(loaded[id])return -203;
 if(!imports_ready(id))return -201;
 if(!strcmp(scenario,"embedded")||!strcmp(scenario,"native-lmb")||!strcmp(scenario,"patch-failure")||
    (!strcmp(scenario,"missing-pad")&&id==1)||(!strcmp(scenario,"missing-sio2")&&id==0))return -1;
 loaded[id]=true;*result=!strcmp(scenario,"residency-flags")?0x100:0;return id+1;
}
int sbv_patch_enable_lmb(void){++patches;return !strcmp(scenario,"patch-failure")||!strcmp(scenario,"native-lmb")?-1:0;}
int SifExecModuleBuffer(void *data,int size,int arg_size,const char *args,int *result){
 assert(size==1&&!arg_size&&!args);unsigned id=*(const uint8_t *)data;assert(id<6);++buffer_loads;
 if(loaded[id])return -203;
 if(!imports_ready(id))return -201;
 if(!strcmp(scenario,"patch-failure")||(!strcmp(scenario,"missing-pad")&&id==1)||(!strcmp(scenario,"missing-sio2")&&id==0))return -1;
 loaded[id]=true;*result=!strcmp(scenario,"residency-flags")?0x102:2;return id+1;
}
int SifBindRpc(SifRpcClientData_t *client,unsigned id,int mode){
 assert(mode==SIF_RPC_M_NOWAIT);client->id=id;client->server=NULL;
 bool old=!strcmp(scenario,"rom");
 if(((id==0x80000100||id==0x80000101)&&!old&&loaded[1])||
    ((id==0x8000010f||id==0x8000011f)&&old&&loaded[1])||
    (id==0x80000400&&loaded[3]))client->server=(void *)1;
 if(id==0x0870884e){++audio_binds;if(loaded[5]&&strcmp(scenario,"missing-audio-rpc")&&strcmp(scenario,"hung-audio-rpc"))client->server=(void *)1;}
 return 0;
}
int SifCheckStatRpc(SifRpcClientData_t *client){return client->id==0x0870884e&&!strcmp(scenario,"hung-audio-rpc");}
int main(int argc,char **argv){
 assert(argc==2);scenario=argv[1];
 if(!strcmp(scenario,"opl")||!strcmp(scenario,"opaque-list")||strstr(scenario,"audio-rpc"))for(unsigned i=0;i<6;++i)loaded[i]=true;
 struct lp_iop_services ready=lp_iop_modules_init();
 if(strstr(scenario,"timeout")||!strcmp(scenario,"resident-mx4sio")){
  assert(ready.error&&!ready.pad&&!ready.card&&!ready.audio);
  assert(!rom_loads&&!buffer_loads);printf("%s: %s\n",scenario,ready.error);return 0;
 }
 assert(!ready.error&&resets==1&&exits==1&&flags==3&&cache_invalidations==2&&_iop_reboot_count==1);
 if(!strcmp(scenario,"patch-failure")){assert(!ready.pad&&!ready.card&&!ready.audio&&patches==1);}
 else if(!strcmp(scenario,"missing-sio2")){assert(!ready.pad&&!ready.card&&ready.audio);}
 else if(!strcmp(scenario,"missing-pad")){assert(!ready.pad&&ready.card&&ready.audio);}
 else if(strstr(scenario,"audio-rpc")){
  assert(ready.pad&&ready.card&&!ready.audio);assert(ticks<3000);
  if(!strcmp(scenario,"hung-audio-rpc"))assert(audio_binds==1);
 }else assert(ready.pad&&ready.card&&ready.audio);
 if(!strcmp(scenario,"opl")||!strcmp(scenario,"mx4sio")||!strcmp(scenario,"usb"))assert(rom_loads==5&&buffer_loads==1&&patches==1);
 if(!strcmp(scenario,"rom"))assert(rom_loads==5&&buffer_loads==1);
 if(!strcmp(scenario,"embedded")||!strcmp(scenario,"native-lmb"))assert(buffer_loads==6&&patches==1);
 if(strstr(scenario,"legacy-search")||!strcmp(scenario,"residency-flags"))assert(rom_loads==5&&buffer_loads==1);
 if(strstr(scenario,"legacy-search"))assert(rpc_searches==0);
 printf("%s: pad=%d card=%d audio=%d ROM=%u embedded=%u\n",scenario,ready.pad,ready.card,ready.audio,rom_loads,buffer_loads);
 return 0;
}
