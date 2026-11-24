#ifndef MOCK_SIFRPC_H
#define MOCK_SIFRPC_H
#define SIF_RPC_M_NOWAIT 1
typedef struct { void *server;unsigned id; } SifRpcClientData_t;
void SifInitRpc(int mode);
void SifExitRpc(void);
int SifBindRpc(SifRpcClientData_t *client,unsigned id,int mode);
int SifCheckStatRpc(SifRpcClientData_t *client);
#endif
