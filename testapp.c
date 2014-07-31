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

int gt;
void work(void* arg)
{
    while (!finish)
    {
        // do some work
        int i;
        int t = 0;
        for (i = 0; i < 1000000; i++)
            t += i;
        gt = t;
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

void main(int argc, char** argv)
{
    if (argc == 3)
    {
        pid_t pid;
        sscanf(argv[2], "%d", &pid);

        printf("PROFILER (monitoring pid - %d)!\n", pid);

        // setup CTRL-C handler
        signal(SIGINT, kill_test);

        if (opro_ignored_addresses_read(pid) == OPRO_FAILURE)
            return;
        opro_start(pid, argv[1], 10);
        printf("wait..\n");
        while (!finish)
            sleep(1);
        opro_stop();
    }
    else
    {
        printf("WORKER!\n");

        pid_t pid = fork();
        if (pid == 0)
        {
            printf("main() entry, pid: %d\n", getpid());
            // setup CTRL-C handler
            signal(SIGINT, kill_test);
            // setup ignored address
            opro_ignore_address(load_wob_address_get());
            opro_ignored_addresses_serve(getpid());
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
            // stop serving ignored addresses
            opro_ignored_addresses_serve_cancel();
        }
        else if (pid > 0)
        {
            int status;
            wait(&status);
        }
    }
}
