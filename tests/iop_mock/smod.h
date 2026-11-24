#ifndef MOCK_SMOD_H
#define MOCK_SMOD_H
typedef struct { unsigned short id,version; } smod_mod_info_t;
int smod_get_mod_by_name(const char *name,smod_mod_info_t *info);
#endif
