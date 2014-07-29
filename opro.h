#ifndef _OPRO_H_
#define _OPRO_H_

#define OPRO_SUCCESS 0
#define OPRO_FAILURE -1

#ifdef __cplusplus
extern "C" {
#endif
int opro_start(char* image_name, int sample_rate_ms);
int opro_stop();
int opro_ignore_address(void* address);
int opro_ignored_addresses_clear(void);
void opro_test_unwind(void);
#ifdef __cplusplus
}
#endif

#endif
