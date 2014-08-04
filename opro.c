#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <inttypes.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#ifdef LIBUNWIND
#include <libunwind-ptrace.h>
#endif
#ifdef LIBCORKSCREW
#include <corkscrew/backtrace.h>
#endif

#include <sys/ptrace.h>

#include "opro.h"

#define PUBLIC __attribute__ ((visibility ("default")))

#define MAX_NAME_LEN 256
#define MEMORY_ONLY  "[memory]"
struct mm_t
{
	char name[MAX_NAME_LEN];
	uint64_t start, end;
};

static pid_t gpid;
static int finish_profiling, finish_serving;
static pthread_t pt_prof, pt_serve;
#define MAX_THREAD_SAMPLE_LOGS 50
struct thread_sample_t
{
    pid_t tid;
    uint32_t sample_count;
};
static struct thread_sample_t thread_samples[MAX_THREAD_SAMPLE_LOGS] = {};
static struct mm_t image_mm;
static int profile_counter_image = 0;
static int profile_counter_other = 0;
static int sample_rate = 10;

#define MAX_IGNORED_ADDRESSES 100
struct ignored_addresses_t
{
    int count;
    uint64_t addr[MAX_IGNORED_ADDRESSES];
};
static struct ignored_addresses_t ignored_addr = {};

#define PRINTF(...) printf(__VA_ARGS__)

static void print_error(const char* msg)
{
    perror(msg);
}

static void print_result()
{
    PRINTF("\n\n");
    int i;
    int total = 0;
    for (i = 0; i < MAX_THREAD_SAMPLE_LOGS; i++)
    {
        if (thread_samples[i].tid == 0)
            break;
        PRINTF("thread %02d: %d\tsample count: %d\n", i, thread_samples[i].tid, thread_samples[i].sample_count);
        total += thread_samples[i].sample_count;
    }
    PRINTF("total: %d\n", total);
    PRINTF("\n\n");
    PRINTF("profile_counter_image: %d (%d%%)\n", profile_counter_image, (int)((float)profile_counter_image / (profile_counter_image + profile_counter_other) * 100));
    PRINTF("profile_counter_other: %d\n", profile_counter_other);
    PRINTF("total:                 %d\n", profile_counter_image + profile_counter_other);
}

static int is_ignored_addr(uint64_t addr)
{
#define THRESHOLD 15
    int i;
    for (i = 0; i < ignored_addr.count; i++)
        if (addr >= ignored_addr.addr[i] - THRESHOLD &&
                addr <= ignored_addr.addr[i] + THRESHOLD)
        {
            //PRINTF("ignoring addr: %p\n", addr);
            return 1;
        }
    return 0;
}

#ifndef ANDROID
pid_t gettid()
{
    return syscall(SYS_gettid);
}
#endif

static int load_thread_list(pid_t pid, int ignore_sleeping, pid_t* threads, int threads_max, int* threads_count)
{
    *threads_count = 0;
    char path[1024];
    sprintf(path, "/proc/%d/task", pid);
    DIR* d = opendir(path);
    if (!d)
    {
        print_error("opendir(\"%s\") failed");
        return 0;
    }
    struct dirent* ent = readdir(d);
    while (ent && *threads_count < threads_max)
    {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
        {
            // check if thread is sleeping
            if (ignore_sleeping)
            {
                sprintf(path, "/proc/%d/task/%s/stat", pid, ent->d_name);
                int fd = open(path, O_RDONLY);
                if (fd > 0)
                {
#define DAT_SIZE 0x1000
                    char data[DAT_SIZE];
                    ssize_t size = read(fd, data, DAT_SIZE);
                    close(fd);
                    if (size > 0)
                    {
                        char state = *(strchr(data, ')') + 2);
                        if (state != 'R')
                        {
                            ent = readdir(d);
                            continue;
                        }
                    }
                    else
                        print_error("read failed");
                }
                else
                    print_error("open failed");
            }
            // add thread to list
            pid_t tid;
            sscanf(ent->d_name, "%d", &tid);
            //PRINTF("thread: %d\n", tid);
            threads[*threads_count] = tid;
            (*threads_count)++;
        }
        ent = readdir(d);
    }
    closedir(d);
    return 0;
}

#ifdef ANDROID
#define SYS_tgkill __NR_tgkill
#endif
static int tgkill(pid_t tgid, pid_t tid, int signalno)
{
    return syscall(SYS_tgkill, tgid, tid, signalno);
}

#ifdef LIBCORKSCREW
static int sample_corkscrew(pid_t tid)
{
    ptrace_context_t* context = load_ptrace_context(tid);
    if (!context)
        perror("eeeeee!\n");
#define MAX_FRAMES 10
    backtrace_frame_t frames[MAX_FRAMES];
    ssize_t count = unwind_backtrace_ptrace(tid, context, frames, 0, MAX_FRAMES);
    if (count == -1)
    {
        perror("unwind_backtrace_ptrace failed");
        return 0;
    }
    backtrace_symbol_t symbols[MAX_FRAMES];
    get_backtrace_symbols_ptrace(context, frames, count, symbols);
    int in_image = 0;
    int i;
    for (i = 0; i < count; i++)
    {
        uint64_t ip = (uint64_t)frames[i].absolute_pc;
        //printf("%d - ip: %p, %s, %s, %s\n", i, ip, symbols[i].map_name, symbols[i].symbol_name, symbols[i].demangled_name);
        if (ip >= image_mm.start && ip <= image_mm.end && !is_ignored_addr(ip))
        {
            profile_counter_image++;
            in_image = 1;
            break;
        }
    }
    if (!in_image)
        profile_counter_other++;
    free_ptrace_context(context);
    return 1;
}
#endif

#ifdef LIBUNWIND
static int sample_unwind(pid_t tid)
{
    struct UPT_info* ui = _UPT_create(tid);
    unw_cursor_t cursor;
    unw_addr_space_t as = unw_create_addr_space(&_UPT_accessors, 0);
    if (!as)
    {
        perror("unw_create_addr_space() failed");
        return 0;
    }
    int res = unw_init_remote(&cursor, as, ui);
    if (res < 0)
    {
        perror("unw_init_remote() failed");
        unw_destroy_addr_space(as);
        return 0;
    }
    int i = 0;
    int in_image = 0;
#define MAX_FRAMES 10
    while (unw_step(&cursor) > 0 && i < MAX_FRAMES)
    {
        unw_word_t ip, sp;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        unw_get_reg(&cursor, UNW_REG_SP, &sp);
        //PRINTF("i = %d, ip = %016lx, sp = %016lx\n", i, (long) ip, (long) sp);
        if (ip >= image_mm.start && ip <= image_mm.end && !is_ignored_addr(ip))
        {
            profile_counter_image++;
            in_image = 1;
            break;
        }
        i++;
    }
    if (!in_image)
        profile_counter_other++;
    unw_destroy_addr_space(as);
    _UPT_destroy(ui);
    return 1;
}
#endif

const int sleep_time_usec = 50000;         /* 0.05 seconds */
const int max_total_sleep_usec = 10000000; /* 10 seconds*/

void wait_for_stop(pid_t tid, int* total_sleep_time_usec)
{
    siginfo_t si;
    while (TEMP_FAILURE_RETRY(ptrace(PTRACE_GETSIGINFO, tid, 0, &si)) < 0 && errno == ESRCH)
    {
        if (*total_sleep_time_usec > max_total_sleep_usec)
        {
            printf("timed out waiting for tid=%d to stop\n", tid);
            break;
        }
        usleep(sleep_time_usec);
        *total_sleep_time_usec += sleep_time_usec;
    }
}

static void profile_thread(void* arg)
{   
    PRINTF("profile_thread() entry, tid: %d\n", gettid());

#define MAX_THREAD_LIST 50
    int thread_count = 0;
    pid_t thread_list[MAX_THREAD_LIST];

    while (!finish_profiling)
    {
        load_thread_list(gpid, 1, thread_list, MAX_THREAD_LIST, &thread_count);
        //printf("thread count: %d\n", thread_count);
        int i;
        for (i = 0; i < thread_count; i++)
        {
            pid_t tid = thread_list[i];
            // update thread sample count
            int j;
            for (j = 0; j < MAX_THREAD_SAMPLE_LOGS; j++)
            {
                if (thread_samples[j].tid == 0)
                {
                    thread_samples[j].tid = tid;
                    thread_samples[j].sample_count++;
                    break;
                }
                else if (thread_samples[j].tid == tid)
                {
                    thread_samples[j].sample_count++;
                    break;
                }
            }
            // attach to tid via ptrace
            if (ptrace(PTRACE_ATTACH, tid, 0, 0))
            {
                perror("ptrace attach failed");
                continue;
            }
            int total_sleep_time_usec = 0;
            wait_for_stop(tid, &total_sleep_time_usec);
#ifdef LIBCORKSCREW
            if (!sample_corkscrew(tid))
            {
                print_result();
                exit(-1);
            }
#endif
#ifdef LIBUNWIND
            sample_unwind(tid);
#endif
            // detatch from thread
            ptrace(PTRACE_DETACH, tid, 0, 0);
        }

        const struct timespec t = {sample_rate / 1000, (sample_rate % 1000) * 1000000};
        nanosleep(&t, NULL);
    }
}

static unsigned int stack_start;
static unsigned int stack_end;
static int load_memmap(pid_t pid, struct mm_t *mm, int *nmmp)
{
	char raw[180000]; // this depends on the number of libraries an executable uses
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
		PRINTF("Can't open %s for reading\n", raw);
		return -1;
	}

	/* Zero to ensure data is null terminated */
	memset(raw, 0, sizeof(raw));

	p = raw;
	while (1) {
		rv = read(fd, p, sizeof(raw)-(p-raw));
		if (0 > rv) {
			print_error("read");
			return -1;
		}
		if (0 == rv)
			break;
		p += rv;
		if (p-raw >= (signed) sizeof(raw)) {
			PRINTF("Too many memory mapping\n");
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

PUBLIC int opro_start(pid_t pid, char* image_name, int sample_rate_ms)
{
    // clear globals
    gpid = pid;
    finish_profiling = 0;
    memset(&thread_samples, 0, sizeof(thread_samples));
    memset(&image_mm, 0, sizeof(image_mm));
    profile_counter_image = 0;
    profile_counter_other = 0;
    sample_rate = sample_rate_ms;
    // continue initialization
    struct mm_t mm[1000];
    int nmm;
    if (load_memmap(pid, mm, &nmm))
    {
        PRINTF("error: cant load memory map\n");
        return OPRO_FAILURE;
    }
    struct mm_t* imm = find_image(mm, nmm, image_name);
    if (!imm)
    {
        PRINTF("error: cant find image memory map\n");
        return OPRO_FAILURE;
    }
    image_mm = *imm;
    PRINTF("profiling image: %s, 0x%"PRIx64"-0x%"PRIx64"\n", imm->name, imm->start, imm->end);
    int res = pthread_create(&pt_prof, NULL, (void*)&profile_thread, NULL);
    if (res)
    {
        print_error("pthread_create failed");
        return OPRO_FAILURE;
    }
    return OPRO_SUCCESS;
}

PUBLIC int opro_stop()
{
    // stop sampling
    finish_profiling = 1;
    pthread_join(pt_prof, NULL);
    // print profile result
    print_result();
    return OPRO_SUCCESS;
}

PUBLIC int opro_ignore_address(void* address)
{
    if (ignored_addr.count < MAX_IGNORED_ADDRESSES)
    {
        //PRINTF("ignoring %p\n", address);
        ignored_addr.addr[ignored_addr.count] = (uint64_t)address;
        ignored_addr.count++;
        return OPRO_SUCCESS;
    }
    else
    {
        PRINTF("error: no space left in ignored_addresses\n");
        return OPRO_FAILURE;
    }
}

PUBLIC int opro_ignored_addresses_clear()
{
    ignored_addr.count = 0;
}

static int make_addr(const char* name, struct sockaddr_un* addr, socklen_t* socklen)
{
    // create a UNIX-domain socket address in the Linux abstract namespace
    int name_len = strlen(name);
    if (name_len >= (int) sizeof(addr->sun_path) - 1)
        return 0;
    addr->sun_path[0] = '\0';
    strcpy(addr->sun_path + 1, name);
    addr->sun_family = AF_LOCAL;
    *socklen = offsetof(struct sockaddr_un, sun_path) + name_len + 1;
    return 1;
}

static char* socket_name(pid_t pid)
{
#define LEN 100
    static char name[LEN];
    snprintf(name, LEN, "opro_ignored_addreses_%d", pid);
    return name;
}

static void serve_addresses(void* arg)
{
    pid_t pid = *((pid_t*)arg);
    struct sockaddr_un sockaddr = {};
    socklen_t socklen;
    char* name = socket_name(pid);
    if (!make_addr(name, &sockaddr, &socklen))
    {
        printf("make_addr() failed\n");
        return;
    }
    printf("SERVER %s\n", sockaddr.sun_path + 1);
    int fd = socket(AF_LOCAL, SOCK_STREAM, PF_UNIX);
    if (fd < 0)
    {
        perror("socket()");
        return;
    }
    if (bind(fd, (const struct sockaddr*) &sockaddr, socklen) < 0)
    {
        perror("bind()");
        goto cleanup;
    }
    if (listen(fd, 5) < 0)
    {
        perror("listen()");
        goto cleanup;
    }
    while (!finish_serving)
    {
        int client_sock = accept(fd, NULL, NULL);
        if (client_sock < 0)
        {
            perror("accept");
            break;
        }
        int count = write(client_sock, &ignored_addr, sizeof(ignored_addr));
        close(client_sock);
        if (count < 0)
        {
            perror("write");
            break;
        }
    }
cleanup:
    close(fd);
}

PUBLIC int opro_ignored_addresses_serve(pid_t pid)
{
    static pid_t p;
    p = pid;
    PRINTF("serving ignored addresses\n");
    int res = pthread_create(&pt_serve, NULL, (void*)&serve_addresses, &p);
    if (res)
    {
        print_error("pthread_create failed");
        return OPRO_FAILURE;
    }
    return OPRO_SUCCESS;
}

PUBLIC int opro_ignored_addresses_serve_cancel()
{
    // stop serving
    finish_serving = 1;
    pthread_join(pt_serve, NULL);
    return OPRO_SUCCESS;
}

PUBLIC int opro_ignored_addresses_read(pid_t pid)
{
    int res = OPRO_FAILURE;
    opro_ignored_addresses_clear();
    struct sockaddr_un sockaddr;
    socklen_t socklen;
    char* name = socket_name(pid);
    if (!make_addr(name, &sockaddr, &socklen))
    {
        printf("make_addr() failed\n");
        return;
    }
    printf("CLIENT %s\n", sockaddr.sun_path + 1);
    int fd = socket(AF_LOCAL, SOCK_STREAM, PF_UNIX);
    if (fd < 0)
    {
        perror("socket()");
        return;
    }
    if (connect(fd, (const struct sockaddr*) &sockaddr, socklen) < 0)
    {
        perror("connect()");
        goto cleanup;
    }
    if (read(fd, &ignored_addr, sizeof(ignored_addr)) < 0)
    {
        perror("read()");
        goto cleanup;
    }
    printf("ignored_addr.count: %d\n", ignored_addr.count);
    res = OPRO_SUCCESS;
cleanup:
    close(fd);
    return res;
}
