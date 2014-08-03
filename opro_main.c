#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "opro.h"

int finish = 0;

void _kill(int sig)
{
    finish = 1;
}

void main(int argc, char** argv)
{
    if (argc == 3)
    {
        pid_t pid;
        sscanf(argv[2], "%d", &pid);

        printf("PROFILER (monitoring pid - %d)!\n", pid);

        // setup CTRL-C handler
        signal(SIGINT, _kill);
        // and others
        signal(SIGTERM, _kill);
        signal(SIGKILL, _kill);

        if (opro_ignored_addresses_read(pid) == OPRO_FAILURE)
            return;
        opro_start(pid, argv[1], 10);
        printf("wait..\n");
        while (!finish)
            sleep(1);
        opro_stop();
    }
}
