#ifndef MOCK_SIFCMD_H
#define MOCK_SIFCMD_H
#include <stdint.h>
typedef struct { uint32_t psize, dest, cid, opt; } SifCmdHeader_t;
typedef struct { SifCmdHeader_t header; int arglen, mode; char arg[80]; } SifCmdResetData_t;
#define SIF_CMD_RESET_CMD 0x80000003u
void sceSifWriteBackDCache(void *data, int size);
#endif
