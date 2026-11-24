#ifndef MOCK_LOADFILE_H
#define MOCK_LOADFILE_H
int SifSearchModuleByName(const char *name);
int SifLoadStartModule(const char *path,int size,const char *args,int *result);
int SifExecModuleBuffer(void *data,int size,int arg_size,const char *args,int *result);
#endif
