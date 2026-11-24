#ifndef MOCK_AUDSRV_H
#define MOCK_AUDSRV_H
#define MIN_VOLUME 0
#define MAX_VOLUME 100
int audsrv_wait_audio(int bytes);
int audsrv_play_audio(const char *pcm,int bytes);
int audsrv_stop_audio(void);
int audsrv_set_volume(int volume);
int audsrv_get_error(void);
#endif
