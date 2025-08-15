#ifndef __CUSTOM_MEM__H_
#define __CUSTOM_MEM__H_
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

void* customMalloc(size_t size);
void* customCalloc(size_t n, size_t size);
void* customRealloc(void* p, size_t size);
void* customMemAligned(size_t align, size_t size);
void* customMemAligned32(size_t align, size_t size);
void customFree(void* p);
void init_custommem_helper();
void fini_custommem_helper();
size_t customGetUsableSize(void* p);

#define ALIGN(p) (((p)+4095)&~(4095))


#define PROT_NEVERCLEAN 0x100
#define PROT_DYNAREC    0x80
#define PROT_DYNAREC_R  0x40
#define PROT_NOPROT     0x20
#define PROT_DYN        (PROT_DYNAREC | PROT_DYNAREC_R | PROT_NOPROT | PROT_NEVERCLEAN)
#define PROT_CUSTOM     (PROT_DYNAREC | PROT_DYNAREC_R | PROT_NOPROT | PROT_NEVERCLEAN)
#define PROT_NEVERPROT  (PROT_NOPROT | PROT_NEVERCLEAN)
#define PROT_WAIT       0xFF

#endif //__CUSTOM_MEM__H_
