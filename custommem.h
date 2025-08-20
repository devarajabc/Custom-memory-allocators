#ifndef __CUSTOM_MEM__H_
#define __CUSTOM_MEM__H_
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>
#include "rbtree_tmp.h"
#define MMAPSIZE (512*1024*1024)     // allocate 512kb sized blocks

void* customMalloc(size_t size);
void* customMalloc_box64(size_t size);
void customFree(void* p);
void customFree_box64(void* p);
void init_custommem_helper();
void init_custommem_helper_box64();
size_t fini_custommem_helper();
size_t fini_custommem_helper_box64();

typedef union mark_s {
    struct {
        unsigned int    offs:31;
        unsigned int    fill:1;
    };
    uint32_t            x32;
} mark_t;
typedef struct blockmark_s {
    rb_node_t node;
    mark_t  prev;
    mark_t  next;
    uint8_t mark[];
} blockmark_t;

typedef struct blockmark_box64_s {
    mark_t  prev;
    mark_t  next;
    uint8_t mark[];
} blockmark_box64_t;


#define ALIGN(p) (((p)+4095)&~(4095))

#endif //__CUSTOM_MEM__H_
