#ifndef MOCK_LIBMC_H
#define MOCK_LIBMC_H
#include <stdint.h>
#define MC_TYPE_MC 0
#define MC_TYPE_NONE 0
#define MC_TYPE_PS2 2
#define MC_WAIT 0
#define MC_ATTR_SUBDIR 0x20
#define sceMcResChangedCard -1
#define sceMcResNoFormat -2
#define sceMcResFullDevice -3
#define sceMcResNoEntry -4
typedef struct {
 unsigned char head[4];uint16_t type,nlOffset;unsigned unknown2,trans;
 int bgCol[4][4];float lightDir[3][4],lightCol[3][4],lightAmbient[4];
 unsigned short title[34];unsigned char view[64],copy[64],del[64],unknown3[512];
} mcIcon;
typedef struct { uint32_t FileSizeByte;uint16_t AttrFile; } sceMcTblGetDir;
int mcInit(int type);int mcGetInfo(int p,int s,int *t,int *f,int *fmt);
int mcOpen(int p,int s,const char *n,int m);int mcClose(int fd);
int mcRead(int fd,void *b,int n);int mcWrite(int fd,const void *b,int n);int mcFlush(int fd);
int mcGetDir(int p,int s,const char *n,unsigned m,int c,sceMcTblGetDir *d);
int mcMkDir(int p,int s,const char *n);int mcSync(int mode,int *cmd,int *result);
#endif
