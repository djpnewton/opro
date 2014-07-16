#ifndef _LOAD_H_
#define _LOAD_H_

#include <pthread.h>

typedef void (*work_t)(void* arg);
void* load_wob_address_get();
void load_stop(int* finish_flag, int num_threads, pthread_t* threads);
int load_work(work_t work, int num_threads, pthread_t* threads);
int load_work_on_behalf(work_t work, int num_threads, pthread_t* threads);

#endif
