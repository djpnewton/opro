#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/prctl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#include "opro.h"
#include "load.h"

int finish = 0;
#define NUM_THREADS 5
pthread_t threads[NUM_THREADS];
pthread_t threads_lazy[NUM_THREADS];
pthread_t threads_wob[NUM_THREADS];

void rate_limit(int milliseconds)
{
    long step_ns = milliseconds * 1000000;
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    long sleep_ns = step_ns - (t.tv_nsec % step_ns);
    t.tv_sec = 0;
    t.tv_nsec = sleep_ns;
    nanosleep(&t, NULL);    
}

void work_lazy(void* arg)
{
    while (!finish)
        rate_limit(100);
}

void work(void* arg)
{
    while (!finish)
    {
        // do some work
        int i;
        for (i = 0; i < 100; i++)
            free(malloc(0x1000));
        // rate limit
        rate_limit(10);
    }
}

void kill_test(int sig)
{
    load_stop(&finish, NUM_THREADS, threads);
    load_stop(&finish, NUM_THREADS, threads_lazy);
    load_stop(&finish, NUM_THREADS, threads_wob);
}

void main()
{
    printf("main() entry, pid: %d\n", getpid());
    // opro start
    opro_test_unwind();
    opro_ignore_address(load_wob_address_get);
    opro_start("libload.so");
    printf("opro_start()'ed\n");
    // setup CTRL-C handler
    signal(SIGINT, kill_test);
    // do work in libload.so
    printf("load_work()\n");
    load_work(work, NUM_THREADS, threads);
    load_work(work_lazy, NUM_THREADS, threads_lazy);
    // do work via work_on_behalf function
    printf("load_work_on_behalf()\n");
    load_work_on_behalf(work, NUM_THREADS, threads_wob);
    // wait
    printf("wait..\n");
    while (!finish)
        sleep(1);
    // opro stop
    printf("opro_stop()\n");
    opro_stop();
}
