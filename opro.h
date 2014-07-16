#ifndef _OPRO_H_
#define _OPRO_H_

#define OPRO_SUCCESS 0
#define OPRO_FAILURE -1

int opro_start(char* image_name);
int opro_stop();
int opro_ignore_address(void* address);

#endif
