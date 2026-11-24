#ifndef MOCK_KERNEL_H
#define MOCK_KERNEL_H
extern void *_gp;
typedef struct { int init_count,max_count; } ee_sema_t;
typedef struct { void *func,*stack; int stack_size; void *gp_reg; int initial_priority; } ee_thread_t;
int CreateSema(ee_sema_t *s);int DeleteSema(int s);int SignalSema(int s);int WaitSema(int s);
int CreateThread(ee_thread_t *t);int StartThread(int t,void *arg);int DeleteThread(int t);
#endif
