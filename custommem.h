#ifndef __CUSTOM_MEM__H_
#define __CUSTOM_MEM__H_
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

void* customMalloc(size_t size);
void* customMalloc_box64(size_t size);
void customFree(void* p);
void customFree_box64(void* p);
void init_custommem_helper();
void init_custommem_helper_box64();
void fini_custommem_helper();
void fini_custommem_helper_box64();


#define ALIGN(p) (((p)+4095)&~(4095))

#endif //__CUSTOM_MEM__H_
