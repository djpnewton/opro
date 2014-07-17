#include "load.h"

#define PUBLIC __attribute__ ((visibility ("default")))

static void work_local(void* arg)
{
    work_t work = (work_t)arg;
    work(NULL);
}

static void work_on_behalf(void* arg)
{
    work_t work = (work_t)arg;
    work(NULL);
}

PUBLIC void* load_wob_address_get()
{
    return work_on_behalf;
}

PUBLIC void load_stop(int* finish_flag, int num_threads, pthread_t* threads)
{
    *finish_flag = 1;
    // wait for threads
    int i;
    for (i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);
}

PUBLIC int load_work(work_t work, int num_threads, pthread_t* threads)
{
    // create threads
    int i;
    for (i = 0; i < num_threads; i++)
        pthread_create(&threads[i], NULL, (void*)&work_local, work);
    return 1;
}

PUBLIC int load_work_on_behalf(work_t work, int num_threads, pthread_t* threads)
{
    // create threads
    int i;
    for (i = 0; i < num_threads; i++)
        pthread_create(&threads[i], NULL, (void*)&work_on_behalf, (void*)work);
    return 1;
}
