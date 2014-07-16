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
pthread_t threads_wob[NUM_THREADS];

void work(void* arg)
{
    while (!finish)
    {
        // do some work
        int i;
        for (i = 0; i < 1000; i++)
            free(malloc(0x1000));
        // limit loop to STEP_MS
#define STEP_MS 10
#define STEP_NS (STEP_MS * 1000000)
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        long sleep_ns = STEP_NS - (t.tv_nsec % STEP_NS);
        t.tv_sec = 0;
        t.tv_nsec = sleep_ns;
        nanosleep(&t, NULL);    
    }
}

void kill_test(int sig)
{
    load_stop(&finish, NUM_THREADS, threads);
    load_stop(&finish, NUM_THREADS, threads_wob);
}

void main()
{
    // opro start
    opro_ignore_address(load_wob_address_get);
    opro_start("libload.so");
    // setup CTRL-C handler
    signal(SIGINT, kill_test);
    // do work in libload.so
    load_work(work, NUM_THREADS, threads);
    // do work via work_on_behalf function
    load_work_on_behalf(work, NUM_THREADS, threads_wob);
    // wait
    while (!finish)
    {
        struct timespec t;
        t.tv_sec = 0;
        t.tv_nsec = 100 * 1000000;
        nanosleep(&t, NULL);    
    }
    // opro stop
    opro_stop();
}
