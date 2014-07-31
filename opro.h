#ifndef _OPRO_H_
#define _OPRO_H_

#include <sys/types.h>

#define OPRO_SUCCESS 0
#define OPRO_FAILURE -1

#ifdef __cplusplus
extern "C" {
#endif
int opro_start(pid_t pid, char* image_name, int sample_rate_ms);
int opro_stop();
int opro_ignore_address(void* address);
int opro_ignored_addresses_clear(void);
int opro_ignored_addresses_serve(pid_t pid);
int opro_ignored_addresses_serve_cancel(void);
int opro_ignored_addresses_read(pid_t pid);
#ifdef __cplusplus
}
#endif

#endif
