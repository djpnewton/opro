#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
//#include <ucontext.h>
#include <sys/time.h>
#include <sys/syscall.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include "opro.h"

#ifdef ANDROID
#include <android/log.h>
#endif

#define PUBLIC __attribute__ ((visibility ("default")))

#define MAX_NAME_LEN 256
#define MEMORY_ONLY  "[memory]"
struct mm_t
{
	char name[MAX_NAME_LEN];
	uint64_t start, end;
};

static int finish = 0;
static pthread_t t;
#define MAX_THREAD_SAMPLE_LOGS 50
struct thread_sample_t
{
    pid_t tid;
    uint32_t sample_count;
};
static struct thread_sample_t thread_samples[MAX_THREAD_SAMPLE_LOGS] = {};
static struct mm_t image_mm;
static profile_counter_image = 0;
static profile_counter_other = 0;
static pthread_mutex_t lock;

#define MAX_IGNORED_ADDRESSES 100
static int num_ignored_addresses = 0;
static void* ignored_addresses[MAX_IGNORED_ADDRESSES];

static int is_ignored_addr(uint64_t addr)
{
#define THRESHOLD 5
    int i;
    for (i = 0; i < num_ignored_addresses; i++)
        if (addr >= (uint64_t)ignored_addresses[i] - THRESHOLD &&
                addr <= (uint64_t)ignored_addresses[i] + THRESHOLD)
            return 1;
    return 0;
}

static void profile_action(int sig, siginfo_t* info, void* context)
{
    pthread_mutex_lock(&lock);
/*
    ucontext_t* ucontext = (ucontext_t*)context;
    mcontext_t* mcontext = &ucontext->uc_mcontext;
#if defined(__amd64)
#define REG_PC 16
#define TASK_SIZE 0xffff880000000000
#elif defined(__i386)
#define REG_PC 13
#define TASK_SIZE 0xc0000000
#else
#error ("cpu not supported")
#endif
    uint64_t pc = mcontext->gregs[REG_PC];
    if (pc <= TASK_SIZE)
        printf("profile_action tid: %d, pc: 0x%"PRIx64"\n", (int)syscall(SYS_gettid), pc);
*/

    // update thread sample count
    int i;
#ifdef ANDROID
    pid_t tid = gettid();
#else
    pid_t tid = syscall(SYS_gettid);
#endif
    for (i = 0; i < MAX_THREAD_SAMPLE_LOGS; i++)
    {
        if (thread_samples[i].tid == 0)
        {
            thread_samples[i].tid = tid;
            thread_samples[i].sample_count++;
            break;
        }
        else if (thread_samples[i].tid == tid)
        {
            thread_samples[i].sample_count++;
            break;
        }
    }

#define FRAME_MAX 10
    // initialize libunwind
    unw_context_t ctx;
    unw_getcontext(&ctx);
    unw_cursor_t cursor;
    unw_init_local(&cursor, &ctx);
    // search calls from our image code
    i = 0;
    int in_image = 0;
    while (unw_step(&cursor) > 0 && i < FRAME_MAX)
    {
        // ignore this function and sigaction() entries in backtrace
        if (i > 1)
        {
            unw_word_t ip, sp;
            unw_get_reg(&cursor, UNW_REG_IP, &ip);
            unw_get_reg(&cursor, UNW_REG_SP, &sp);
            //printf("ip = %lx, sp = %lx\n", (long) ip, (long) sp);
            if (ip >= image_mm.start && ip <= image_mm.end && !is_ignored_addr(ip))
            {
                profile_counter_image++;
                in_image = 1;
                break;
            }
        }
        i++;
    }
    if (!in_image)
        profile_counter_other++;

    pthread_mutex_unlock(&lock);
}

static void setup_profile_handler()
{
    struct sigaction action;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    action.sa_sigaction = profile_action;
    sigemptyset(&action.sa_mask);
    int res = sigaction(SIGPROF, &action, NULL);
    if (res)
        perror("sigaction failed");

	struct itimerval timer;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 10 * 1000; // 10ms
    timer.it_value = timer.it_interval;
    res = setitimer(ITIMER_PROF, &timer, NULL);
    if (res)
        perror("setitimer failed");
}

static void remove_profile_handler(void)
{
    struct itimerval timer;
    memset(&timer, 0, sizeof(timer));
    int res = setitimer(ITIMER_PROF, &timer, NULL);
    if (res)
        perror("setitimer failed");
}

static void profile_thread(void* arg)
{   
    setup_profile_handler();

    while (!finish)
    {
        const struct timespec t = {0, 100 * 1000000}; // 100ms
        nanosleep(&t, NULL);
    }
    
    remove_profile_handler();
}

static unsigned int stack_start;
static unsigned int stack_end;
static int load_memmap(pid_t pid, struct mm_t *mm, int *nmmp)
{
	char raw[80000]; // this depends on the number of libraries an executable uses
	char name[MAX_NAME_LEN];
	char *p;
	unsigned long start, end;
	struct mm_t *m;
	int nmm = 0;
	int fd, rv;
	int i;


	sprintf(raw, "/proc/%d/maps", pid);
	fd = open(raw, O_RDONLY);
	if (0 > fd) {
		printf("Can't open %s for reading\n", raw);
		return -1;
	}

	/* Zero to ensure data is null terminated */
	memset(raw, 0, sizeof(raw));

	p = raw;
	while (1) {
		rv = read(fd, p, sizeof(raw)-(p-raw));
		if (0 > rv) {
			//perror("read");
			return -1;
		}
		if (0 == rv)
			break;
		p += rv;
		if (p-raw >= (signed) sizeof(raw)) {
			printf("Too many memory mapping\n");
			return -1;
		}
	}
	close(fd);

	p = strtok(raw, "\n");
	m = mm;
	while (p) {
		/* parse current map line */
		rv = sscanf(p, "%16lx-%16lx %*s %*s %*s %*s %s\n",
			    &start, &end, name);

		p = strtok(NULL, "\n");

		if (rv == 2) {
			m = &mm[nmm++];
			m->start = start;
			m->end = end;
			strcpy(m->name, MEMORY_ONLY);
			continue;
		}

		if (strstr(name, "stack") != 0) {
			stack_start = start;
			stack_end = end;
		}

		/* search backward for other mapping with same name */
		for (i = nmm-1; i >= 0; i--) {
			m = &mm[i];
			if (!strcmp(m->name, name))
				break;
		}

		if (i >= 0) {
			if (start < m->start)
				m->start = start;
			if (end > m->end)
				m->end = end;
		} else {
			/* new entry */
			m = &mm[nmm++];
			m->start = start;
			m->end = end;
			strcpy(m->name, name);
		}
	}

	*nmmp = nmm;
	return 0;
}

struct mm_t* find_image(struct mm_t* mm, int mm_count, char* image_name)
{
    int i;
    for (i = 0; i < mm_count; i++)
    {
        if (strstr(mm[i].name, image_name))
            return &mm[i];
    }
    return NULL;
}

PUBLIC int opro_start(char* image_name)
{
    struct mm_t mm[1000];
    int nmm;
    if (load_memmap(getpid(), mm, &nmm))
    {
        printf("error: cant load memory map\n");
        return OPRO_FAILURE;
    }
    struct mm_t* imm = find_image(mm, nmm, image_name);
    if (!imm)
    {
        printf("error: cant find image memory map\n");
        return OPRO_FAILURE;
    }
    image_mm = *imm;
    printf("profiling image: %s, 0x%"PRIx64"-0x%"PRIx64"\n", imm->name, imm->start, imm->end);
    int res = pthread_mutex_init(&lock, NULL);
    if (res)
    {
        perror("pthread_mutex_init failed");
        return OPRO_FAILURE;
    }
    res = pthread_create(&t, NULL, (void*)&profile_thread, NULL);
    if (res)
    {
        perror("pthread_create failed");
        return OPRO_FAILURE;
    }
    return OPRO_SUCCESS;
}

PUBLIC int opro_stop()
{
    // stop sampling
    finish = 1;
    pthread_join(t, NULL);
    pthread_mutex_destroy(&lock);
    // print profile result
    printf("\n\n");
    int i;
    int total = 0;
    for (i = 0; i < MAX_THREAD_SAMPLE_LOGS; i++)
    {
        if (thread_samples[i].tid == 0)
            break;
        printf("thread %02d: %d\tsample count: %d\n", i, thread_samples[i].tid, thread_samples[i].sample_count);
        total += thread_samples[i].sample_count;
    }
    printf("total: %d\n", total);
    printf("\n\n");
    printf("profile_counter_image: %d\n", profile_counter_image);
    printf("profile_counter_other: %d\n", profile_counter_other);
    printf("total:                 %d\n", profile_counter_image + profile_counter_other);
    return OPRO_SUCCESS;
}

PUBLIC int opro_ignore_address(void* address)
{
    if (num_ignored_addresses < MAX_IGNORED_ADDRESSES)
    {
        printf("ignoring %p\n", address);
        ignored_addresses[num_ignored_addresses] = address;
        num_ignored_addresses++;
        return OPRO_SUCCESS;
    }
    else
    {
        printf("error: no space left in ignored_addresses\n");
        return OPRO_FAILURE;
    }
}
