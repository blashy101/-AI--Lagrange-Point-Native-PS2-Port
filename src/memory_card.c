/* SPDX-License-Identifier: GPL-2.0-or-later */
#define NEWLIB_PORT_AWARE
#include "memory_card.h"
#include "save_data.h"
#include <kernel.h>
#include <libmc.h>
#include <io_common.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#define SAVE_DIR "/BILPNT-00100"
extern const uint8_t lp_save_icon[],lp_save_icon_end[];
enum card_result { CARD_OK,CARD_MISSING,CARD_UNFORMATTED,CARD_WRONG_TYPE,
 CARD_FULL,CARD_IO_ERROR,CARD_CORRUPT,CARD_EMPTY,CARD_LOADED,CARD_BACKUP,CARD_ICON_UPDATED,CARD_ICON_CURRENT };
static const char *const messages[]={
 "SAVED TO MEMORY CARD","NO MEMORY CARD IN SLOT 1","MEMORY CARD NEEDS FORMATTING",
 "USE A PS2 MEMORY CARD","MEMORY CARD IS FULL","MEMORY CARD READ OR WRITE FAILED",
 "SAVE DATA COULD NOT BE READ","MEMORY CARD READY","MEMORY CARD SAVE LOADED","BACKUP SAVE LOADED","SAVE ICON UPDATED","MEMORY CARD SAVE LOADED"};
static bool ready,busy,metadata_pending,metadata_job;
static unsigned edition,retry_frames,notice_frames;
static const char *message="MEMORY CARD UNAVAILABLE";
static struct lp_save_watch watch;
static uint8_t job_payload[LP_SAVE_PAYLOAD_BYTES] __attribute__((aligned(64)));
static uint8_t record[LP_SAVE_RECORD_BYTES] __attribute__((aligned(64)));
static uint8_t incoming[LP_SAVE_RECORD_BYTES+1] __attribute__((aligned(64)));
static uint8_t loaded[LP_SAVE_PAYLOAD_BYTES] __attribute__((aligned(64)));
static uint8_t file_check[4096] __attribute__((aligned(64)));
static uint8_t worker_stack[32768] __attribute__((aligned(16)));
static mcIcon icon __attribute__((aligned(64)));
static sceMcTblGetDir entry __attribute__((aligned(64)));
static int semaphore,mc_type,mc_free,mc_formatted;
static enum card_result job_result;
static atomic_int job_done;
static int wait_mc(int submitted) {
 int result;if(submitted<0||mcSync(MC_WAIT,NULL,&result)!=1)return -1000;return result;
}
static void record_path(char *p,unsigned index) { snprintf(p,64,SAVE_DIR "/%s%u.DAT",edition?"JA":"EN",index); }
static enum card_result card_info(void) {
 mc_type=mc_free=mc_formatted=0;
 int r=wait_mc(mcGetInfo(0,0,&mc_type,&mc_free,&mc_formatted));
 if(mc_type==MC_TYPE_NONE)return CARD_MISSING;
 if(mc_type!=MC_TYPE_PS2)return CARD_WRONG_TYPE;
 if(r==sceMcResNoFormat||!mc_formatted)return CARD_UNFORMATTED;
 return r>=sceMcResChangedCard?CARD_OK:CARD_IO_ERROR;
}
static int read_record(unsigned index) {
 char p[64];record_path(p,index);int fd=wait_mc(mcOpen(0,0,p,FIO_O_RDONLY));if(fd<0)return fd;
 int n=wait_mc(mcRead(fd,incoming,sizeof(incoming))),closed=wait_mc(mcClose(fd));
 return closed<0?-1000:n;
}
/* Rescan on every write so the other generation also survives a card swap. */
static enum card_result scan_records(int *newest,uint32_t *sequence) {
 bool damaged=false;*newest=-1;*sequence=0;
 for(unsigned i=0;i<2;++i) {
  int n=read_record(i);uint32_t seq;
  if(n==sceMcResNoEntry)continue;
  if(n<0)return CARD_IO_ERROR;
  if(!lp_save_decode(incoming,n,edition,NULL,&seq)){damaged=true;continue;}
  if(*newest<0||lp_save_newer(seq,*sequence)) {
   *newest=i;*sequence=seq;memcpy(loaded,incoming+32,LP_SAVE_PAYLOAD_BYTES);
  }
 }
 return *newest>=0?(damaged?CARD_BACKUP:CARD_LOADED):(damaged?CARD_CORRUPT:CARD_EMPTY);
}
static int file_size(const char *path) {
 memset(&entry,0,sizeof(entry));int n=wait_mc(mcGetDir(0,0,path,0,1,&entry));
 if(n==0||n==sceMcResNoEntry)return -1;
 return n<0?-2:(int)entry.FileSizeByte;
}
static enum card_result write_file(const char *path,const void *data,unsigned size) {
 int fd=wait_mc(mcOpen(0,0,path,FIO_O_WRONLY|FIO_O_CREAT|FIO_O_TRUNC));
 if(fd<0)return fd==sceMcResFullDevice?CARD_FULL:CARD_IO_ERROR;
 int n=wait_mc(mcWrite(fd,data,size));int flush=n==(int)size?wait_mc(mcFlush(fd)):-1;
 int closed=wait_mc(mcClose(fd));
 if(n==(int)size&&flush>=0&&closed>=0)return CARD_OK;
 return n==sceMcResFullDevice?CARD_FULL:CARD_IO_ERROR;
}
static bool file_matches(const char *path,const void *data,unsigned size) {
 if(file_size(path)!=(int)size)return false;
 int fd=wait_mc(mcOpen(0,0,path,FIO_O_RDONLY));if(fd<0)return false;
 const uint8_t *p=data;bool same=true;
 for(unsigned offset=0;offset<size;) {
  unsigned count=size-offset;if(count>sizeof(file_check))count=sizeof(file_check);
  int n=wait_mc(mcRead(fd,file_check,count));
  if(n!=(int)count||memcmp(p+offset,file_check,count)){same=false;break;}
  offset+=count;
 }
 return wait_mc(mcClose(fd))>=0&&same;
}
static enum card_result ensure_file(const char *path,const void *data,unsigned size) {
 if(file_matches(path,data,size))return CARD_OK;
 enum card_result r=write_file(path,data,size);
 return r!=CARD_OK?r:file_matches(path,data,size)?CARD_OK:CARD_IO_ERROR;
}
static void prepare_icon(void) {
 memset(&icon,0,sizeof(icon));memcpy(icon.head,"PS2D",4);icon.trans=0x60;
 const char *title="LAGRANGE POINT";uint8_t *sjis=(uint8_t *)icon.title;
 for(unsigned i=0;title[i];++i){sjis[2*i]=title[i]==' '?0x81:0x82;sjis[2*i+1]=title[i]==' '?0x40:0x60+title[i]-'A';}
 for(unsigned i=0;i<4;++i){icon.bgCol[i][0]=10;icon.bgCol[i][1]=22;icon.bgCol[i][2]=36;}
 /* Neutral ambient light preserves the cover's blue/gold colors. */
 for(unsigned i=0;i<3;++i)icon.lightAmbient[i]=1.f;
 strcpy((char *)icon.view,"COVER.ICN");strcpy((char *)icon.copy,"COVER.ICN");strcpy((char *)icon.del,"COVER.ICN");
}
static enum card_result update_icon(void) {
 /* Write the new model first. Interrupted upgrades leave the old POINT.ICN
  * referenced and intact until the descriptor can be replaced. */
 enum card_result r=ensure_file(SAVE_DIR "/COVER.ICN",lp_save_icon,lp_save_icon_end-lp_save_icon);
 if(r!=CARD_OK)return r;
 return ensure_file(SAVE_DIR "/icon.sys",&icon,sizeof(icon));
}
static enum card_result write_job(void) {
 enum card_result r=card_info();if(r!=CARD_OK)return r;
 int newest;uint32_t seq;r=scan_records(&newest,&seq);if(r==CARD_IO_ERROR)return r;
 if(metadata_job) {
  if(newest<0)return CARD_EMPTY;
  if(file_matches(SAVE_DIR "/COVER.ICN",lp_save_icon,lp_save_icon_end-lp_save_icon)&&
     file_matches(SAVE_DIR "/icon.sys",&icon,sizeof(icon)))return CARD_ICON_CURRENT;
  r=update_icon();return r==CARD_OK?CARD_ICON_UPDATED:r;
 }
 memset(&entry,0,sizeof(entry));int n=wait_mc(mcGetDir(0,0,SAVE_DIR,0,1,&entry));
 if(n==0||n==sceMcResNoEntry) {
  if(mc_free<(int)((lp_save_icon_end-lp_save_icon+1023)/1024+12))return CARD_FULL;
  int made=wait_mc(mcMkDir(0,0,SAVE_DIR));if(made<0)return made==sceMcResFullDevice?CARD_FULL:CARD_IO_ERROR;
 }else if(n<0||!(entry.AttrFile&MC_ATTR_SUBDIR))return CARD_IO_ERROR;
 r=update_icon();if(r!=CARD_OK)return r;
 unsigned target=newest<0?1u:(unsigned)newest^1u;lp_save_encode(record,job_payload,edition,seq+1);
 char path[64];record_path(path,target);r=write_file(path,record,sizeof(record));if(r!=CARD_OK)return r;
 int read=read_record(target);
 if(read!=LP_SAVE_RECORD_BYTES||memcmp(record,incoming,sizeof(record))||!lp_save_decode(incoming,read,edition,NULL,NULL))return CARD_IO_ERROR;
 return CARD_OK;
}
static void card_worker(void *arg) {
 (void)arg;for(;;){WaitSema(semaphore);(void)atomic_load_explicit(&job_done,memory_order_acquire);
  job_result=write_job();atomic_store_explicit(&job_done,1,memory_order_release);}
}
void lp_card_init(bool drivers_ready) {
 if(!drivers_ready||mcInit(MC_TYPE_MC)<0)return;
 prepare_icon();ee_sema_t sem={.init_count=0,.max_count=1};semaphore=CreateSema(&sem);if(semaphore<0)return;
 ee_thread_t t={.func=card_worker,.stack=worker_stack,.stack_size=sizeof(worker_stack),.gp_reg=&_gp,.initial_priority=80};
 int id=CreateThread(&t);
 if(id<0||StartThread(id,NULL)<0){if(id>=0)DeleteThread(id);DeleteSema(semaphore);return;}
 ready=true;message=messages[CARD_EMPTY];
}
void lp_card_load(unsigned language,uint8_t *cart) {
 edition=language;
 if(ready){enum card_result r=card_info();if(r==CARD_OK){int newest;uint32_t seq;r=scan_records(&newest,&seq);
   if(newest>=0&&r!=CARD_IO_ERROR){lp_save_restore(cart,loaded);metadata_pending=true;}}
  message=messages[r];notice_frames=r==CARD_EMPTY?0:240;}
 lp_save_watch_init(&watch,cart);
}
void lp_card_poll(const uint8_t *cart) {
 if(!cart)return;
 if(notice_frames)--notice_frames;
 if(busy){if(!atomic_load_explicit(&job_done,memory_order_acquire))return;
  busy=false;bool success=job_result==CARD_OK||job_result==CARD_ICON_UPDATED||job_result==CARD_ICON_CURRENT||job_result==CARD_EMPTY;
  if(job_result==CARD_OK)lp_save_watch_commit(&watch,job_payload);
  if(success)metadata_pending=false;
  message=messages[job_result];notice_frames=job_result==CARD_ICON_CURRENT?0:success?180:600;retry_frames=success?0:180;return;}
 if(retry_frames){--retry_frames;return;}
 if(!ready)return;
 bool save=lp_save_watch_poll(&watch,cart);
 if(!save&&(!metadata_pending||memcmp(watch.candidate,watch.committed,sizeof(watch.candidate))))return;
 metadata_job=!save;
 if(save)memcpy(job_payload,watch.candidate,sizeof(job_payload));
 busy=true;atomic_store_explicit(&job_done,0,memory_order_release);
 if(SignalSema(semaphore)<0){busy=false;retry_frames=180;message=messages[CARD_IO_ERROR];notice_frames=600;}
}
bool lp_card_busy(void){return busy;}
const char *lp_card_status(void){return ready?message:"MEMORY CARD UNAVAILABLE";}
const char *lp_card_notice(void){return busy?(metadata_job?"UPDATING SAVE ICON":"SAVING TO MEMORY CARD"):notice_frames?message:NULL;}
