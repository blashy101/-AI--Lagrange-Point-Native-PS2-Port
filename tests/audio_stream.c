/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include "audio_stream.h"
#include <assert.h>
#include <kernel.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void *_gp;
static sem_t wake;
static pthread_t audio_thread;
static void (*entry)(void *);
static atomic_bool hold, waiting, fail_play;
static atomic_uint count, muted, unmuted, waits, plays;
static atomic_bool full_copies;
static int16_t received[20000];
static void delay(void) { struct timespec t={0,1000000};nanosleep(&t,NULL); }
int CreateSema(ee_sema_t *s){return sem_init(&wake,0,s->init_count)?-1:1;}
int DeleteSema(int s){(void)s;return sem_destroy(&wake);}
int SignalSema(int s){(void)s;return sem_post(&wake);}
int WaitSema(int s){(void)s;return sem_wait(&wake);}
int CreateThread(ee_thread_t *t){entry=t->func;assert(t->initial_priority>0&&t->initial_priority<64);return 1;}
static void *run(void *arg){entry(arg);return NULL;}
int StartThread(int id,void *arg){(void)id;return pthread_create(&audio_thread,NULL,run,arg)?-1:0;}
int DeleteThread(int id){(void)id;return 0;}
int audsrv_wait_audio(int bytes){
 assert(pthread_equal(pthread_self(),audio_thread));assert(bytes>0&&bytes<=4096);
 atomic_fetch_add(&waits,1);atomic_store(&waiting,true);while(atomic_load(&hold))delay();return 0;
}
int audsrv_play_audio(const char *pcm,int bytes){
 assert(pthread_equal(pthread_self(),audio_thread));
 atomic_fetch_add(&plays,1);
 if(atomic_exchange(&fail_play,false))return -1;
 if(atomic_load(&hold))return 0; /* A full ring accepts no samples. */
 if(!atomic_load(&full_copies)&&bytes>20)bytes=20; /* Force short transfers without losing their tail. */
 unsigned n=atomic_load(&count);assert(n+(unsigned)bytes/2<=sizeof(received)/sizeof(*received));
 memcpy(received+n,pcm,bytes);atomic_store(&count,n+bytes/2);return bytes;
}
int audsrv_get_error(void){return 0;}
int audsrv_stop_audio(void){assert(!"Stopping playback would freeze the IOP read head");return -1;}
int audsrv_set_volume(int volume){
 assert(pthread_equal(pthread_self(),audio_thread));assert(volume==0||volume==100);
 if(volume==0)atomic_fetch_add(&muted,1);else atomic_fetch_add(&unmuted,1);return 0;
}
static void push(unsigned tag){
 int16_t pcm[32];for(unsigned i=0;i<32;++i)pcm[i]=tag;
 lp_audio_stream_push(pcm,16);memset(pcm,0xff,sizeof(pcm));
}
static void sent(unsigned n){for(unsigned i=0;i<2000&&lp_audio_stream_stats().sent<n;++i)delay();assert(lp_audio_stream_stats().sent==n);}
int main(void){
 alarm(15);atomic_store(&hold,true);assert(lp_audio_stream_init());
 push(0);while(!atomic_load(&waiting))delay();
 push(1);push(2);push(3);push(99);
 struct lp_audio_stats stats=lp_audio_stream_stats();assert(stats.submitted==64&&stats.dropped==16);
 /* Every producer call above returned while the IOP side was held blocked. */
 atomic_store(&hold,false);sent(64);assert(atomic_load(&count)==128);
 for(unsigned i=0;i<128;++i)assert(received[i]==(int16_t)(i/32));
 atomic_store(&hold,true);atomic_store(&waiting,false);push(4);
 while(!atomic_load(&waiting))delay();
 lp_audio_stream_pause(true);push(5);atomic_store(&hold,false);
 while(!atomic_load(&muted))delay();
 lp_audio_stream_pause(false);push(6);sent(80);
 for(unsigned i=128;i<160;++i)assert(received[i]==6);
 atomic_store(&fail_play,true);push(7);
 while(lp_audio_stream_stats().errors==0)delay();
 assert(lp_audio_stream_stats().sent==80);
 int16_t large[2*1066];for(unsigned i=0;i<2*1066;++i)large[i]=i;
 lp_audio_stream_push(large,1066);sent(1146);
 for(unsigned i=0;i<2*1066;++i)assert(received[160+i]==(int16_t)i);
 assert(atomic_load(&unmuted)==0);
 unsigned remaining=LP_AUDIO_RESUME_PRIME_FRAMES-16-1066,total=1146;
 /* Unmute only after every stale ring address has been replaced. */
 int16_t prime[2*1024]={0};
 while(remaining>1){unsigned n=remaining-1;if(n>1024)n=1024;
  lp_audio_stream_push(prime,n);total+=n;sent(total);remaining-=n;
 }
 assert(atomic_load(&unmuted)==0);
 lp_audio_stream_push(prime,1);sent(total+1);assert(atomic_load(&unmuted)==1);
 stats=lp_audio_stream_stats();assert(stats.errors==1&&stats.dropped==32);
 unsigned previous_waits=atomic_load(&waits),previous_plays=atomic_load(&plays);
 atomic_store(&full_copies,true);push(8);sent(stats.sent+16);
 assert(atomic_load(&plays)==previous_plays+1&&atomic_load(&waits)==previous_waits);
 puts("Audio worker: blocked IOP leaves producer free; FIFO, copied PCM, short writes, pause epochs, muted ring priming, overflow and error recovery passed.");
 return 0;
}
