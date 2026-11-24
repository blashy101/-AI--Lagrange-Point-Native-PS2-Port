/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "kernel.h"
#include "libmc.h"
#include "io_common.h"
void *_gp;
static sem_t sem;
static void (*entry)(void *);
static int result;
static char open_name[4096];
int CreateSema(ee_sema_t *s){return sem_init(&sem,0,s->init_count)?-1:1;}
int DeleteSema(int s){(void)s;return sem_destroy(&sem);}
int SignalSema(int s){(void)s;return sem_post(&sem);}
int WaitSema(int s){(void)s;return sem_wait(&sem);}
int CreateThread(ee_thread_t *t){entry=t->func;return 1;}
static void *worker(void *a){entry(a);return NULL;}
int StartThread(int t,void *a){(void)t;pthread_t id;return pthread_create(&id,NULL,worker,a)?-1:pthread_detach(id);}
int DeleteThread(int t){(void)t;return 0;}
static int submit(int r){result=r;return 0;}
static void path(char *out,size_t n,const char *name){const char *b=getenv("CARD_ROOT");assert(b);snprintf(out,n,"%s%s",b,name);}
int mcInit(int t){(void)t;return 0;}
int mcGetInfo(int p,int s,int *t,int *f,int *fmt){
 assert(!p&&!s);*t=getenv("CARD_ABSENT")?0:2;*f=getenv("CARD_FULL")?0:8000;*fmt=getenv("CARD_UNFORMATTED")?0:1;
 return submit(!*t?-12:!*fmt?-2:-1);
}
int mcOpen(int p,int s,const char *name,int mode){
 assert(!p&&!s);if(getenv("CARD_ABSENT"))return submit(-12);
 path(open_name,sizeof(open_name),name);
 int flags=mode&FIO_O_WRONLY?O_WRONLY:O_RDONLY;
 if(mode&FIO_O_CREAT)flags|=O_CREAT;
 if(mode&FIO_O_TRUNC)flags|=O_TRUNC;
 if((mode&FIO_O_WRONLY)&&getenv("CARD_FULL")&&strstr(name,".DAT"))return submit(-3);
 int fd=open(open_name,flags,0600);return submit(fd<0?errno==ENOENT?-4:-5:fd);
}
int mcClose(int fd){return submit(close(fd));}
int mcRead(int fd,void *b,int n){return submit(read(fd,b,n));}
int mcWrite(int fd,const void *b,int n){
 if((getenv("CARD_SHORT_WRITE")&&strstr(open_name,".DAT"))||
    (getenv("CARD_SHORT_ICON")&&strstr(open_name,"COVER.ICN")))n/=2;
 return submit(write(fd,b,n));
}
int mcFlush(int fd){return submit(fsync(fd));}
int mcGetDir(int p,int s,const char *name,unsigned mode,int count,sceMcTblGetDir *d){
 assert(!p&&!s&&!mode&&count==1);char file[4096];path(file,sizeof(file),name);struct stat st;
 if(stat(file,&st))return submit(errno==ENOENT?0:-5);
 d->FileSizeByte=st.st_size;d->AttrFile=S_ISDIR(st.st_mode)?MC_ATTR_SUBDIR:0x10;return submit(1);
}
int mcMkDir(int p,int s,const char *name){assert(!p&&!s);char file[4096];path(file,sizeof(file),name);return submit(mkdir(file,0700)?-5:0);}
int mcSync(int mode,int *cmd,int *out){assert(mode==MC_WAIT);if(cmd)*cmd=1;*out=result;return 1;}
