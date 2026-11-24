#ifndef MOCK_SIFDMA_H
#define MOCK_SIFDMA_H
#define SIF_REG_SMFLAG 4
#define SIF_SYSREG_SUBADDR 0x80000000u
#define SIF_SYSREG_RPCINIT 0x80000002u
#define SIF_STAT_BOOTEND 0x40000u
#define SIF_STAT_SIFINIT 0x10000u
#define SIF_STAT_CMDINIT 0x20000u
#define SIF_DMA_ERT 0x40
#define SIF_DMA_INT_O 0x4
typedef struct { void *src, *dest; int size, attr; } SifDmaTransfer_t;
int sceSifSetDma(SifDmaTransfer_t *transfers, int count);
#endif
