#ifndef LP_DISPLAY_H
#define LP_DISPLAY_H
enum lp_signal { SIGNAL_480I,SIGNAL_480P,SIGNAL_720P,SIGNAL_COUNT };
enum lp_aspect { ASPECT_4_3,ASPECT_16_9,ASPECT_COUNT };
struct lp_rect { unsigned x,width; };
static inline struct lp_rect lp_game_rect(unsigned width,enum lp_signal signal,enum lp_aspect aspect)
{
    unsigned w=aspect==ASPECT_16_9||signal==SIGNAL_720P?width*3/4:width;
    return (struct lp_rect){(width-w)/2,w};
}
#endif
