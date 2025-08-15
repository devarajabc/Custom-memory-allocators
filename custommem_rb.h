#ifndef __CUSTOM_MEM__H_
#define __CUSTOM_MEM__H_
#include <unistd.h>
#include <stdint.h>




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

void updateProtection(uintptr_t addr, size_t size, uint32_t prot);
void setProtection(uintptr_t addr, size_t size, uint32_t prot);
void setProtection_mmap(uintptr_t addr, size_t size, uint32_t prot);
void setProtection_elf(uintptr_t addr, size_t size, uint32_t prot);
void freeProtection(uintptr_t addr, size_t size);
void refreshProtection(uintptr_t addr);
uint32_t getProtection(uintptr_t addr);
uint32_t getProtection_fast(uintptr_t addr);
int getMmapped(uintptr_t addr);
int memExist(uintptr_t addr);
void loadProtectionFromMap(void);
void* find32bitBlock(size_t size);
void* find31bitBlockNearHint(void* hint, size_t size, uintptr_t mask);
void* find47bitBlock(size_t size);
void* find47bitBlockNearHint(void* hint, size_t size, uintptr_t mask); // mask can be 0 for default one (0xffff)
void* find47bitBlockElf(size_t size, int mainbin, uintptr_t mask);
void* find31bitBlockElf(size_t size, int mainbin, uintptr_t mask);
int isBlockFree(void* hint, size_t size);



#endif //__CUSTOM_MEM__H_
