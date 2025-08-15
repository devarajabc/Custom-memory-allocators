#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "custommem.h"
#include "rbtree.h"
#include "rbtree_tmp.h"

int box64_pagesize = 4096;
int have48bits = 0;
static int inited = 0;
typedef enum {
    MEM_UNUSED = 0,
    MEM_ALLOCATED = 1,
    MEM_RESERVED = 2,
    MEM_MMAP = 3
} mem_flag_t;
static rbtree_t*  blockstree = NULL;
static rbtree_t*  blockstree_box64 = NULL;



#define BTYPE_MAP   1
#define BTYPE_LIST  0
#define BTYPE_MAP64 2

typedef struct blocklist_s {
    void*               block;
    size_t              maxfree;
    size_t              size;
    void*               first;
    uint32_t            lowest;
    uint8_t             type;
    rb_t               free_list;
} blocklist_t;

typedef struct blocklist_box64_s {
    void*               block;
    size_t              maxfree;
    size_t              size;
    void*               first;
    uint32_t            lowest;
    uint8_t             type;
} blocklist_box64_t;

#define MMAPSIZE (512*1024)     // allocate 512kb sized blocks
#define MMAPSIZE64 (64*2048)   // allocate 128kb sized blocks for 64byte map
#define MMAPSIZE128 (128*1024)  // allocate 128kb sized blocks for 128byte map
#define DYNMMAPSZ (2*1024*1024) // allocate 2Mb block for dynarec
#define DYNMMAPSZ0 (128*1024)   // allocate 128kb block for 1st page, to avoid wasting too much memory on small program / libs

static int                 n_blocks = 0;       // number of blocks for custom malloc
static int                 c_blocks = 0;       // capacity of blocks for custom malloc
static blocklist_t*        p_blocks = NULL;    // actual blocks for custom malloc

/*For Box64*/
static int                 n_blocks_box64 = 0;       // number of blocks for custom malloc
static int                 c_blocks_box64 = 0;       // capacity of blocks for custom malloc
static blocklist_t*        p_blocks_box64 = NULL;    // actual blocks for custom malloc


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

#define NEXT_BLOCK(b) (blockmark_t*)((uintptr_t)(b) + (b)->next.offs)
#define PREV_BLOCK(b) (blockmark_t*)(((uintptr_t)(b) - (b)->prev.offs))
#define LAST_BLOCK(b, s) (blockmark_t*)(((uintptr_t)(b)+(s))-sizeof(blockmark_t))
#define SIZE_BLOCK(b) (((ssize_t)b.offs)-sizeof(blockmark_t))

static bool block_lessthan(const rb_node_t *a, const rb_node_t *b)
{
    if (!a || !b)
        return false;
    const blockmark_t *block_a = container_of(a, blockmark_t, node);
    const blockmark_t *block_b = container_of(b, blockmark_t, node);
    return SIZE_BLOCK(block_a->next) < SIZE_BLOCK(block_b->next);
}

// return true if the size of the block is less than the input size
static bool size_lessthan(const rb_node_t *a, const size_t size)
{
    if (!a || !size)
        return false;
    const blockmark_t *block_a = container_of(a, blockmark_t, node);
    return SIZE_BLOCK(block_a->next) < size;
}

static bool size_equal(const rb_node_t *a, const size_t size)
{
    if (!a || !size)
        return false;
    const blockmark_t *block_a = container_of(a, blockmark_t, node);
    return SIZE_BLOCK(block_a->next) == size;
}

// get first subblock free in block. Return NULL if no block, else first subblock free (mark included), filling size
static blockmark_t* getFirstBlock(rb_t* tree, size_t maxsize, size_t* size, void* start)
{
   rb_node_t* block = find_best_fit(tree, maxsize); 
   if(block){
        blockmark_t *m = container_of(block, blockmark_t, node);
        rb_remove(tree, block);
        *size = SIZE_BLOCK(m->next);
        return m;
   }
   return NULL;
}

static size_t getMaxFreeBlock(rb_t* tree, size_t block_size, void* start)
{
    rb_node_t* maxfree = rb_get_max(tree);
    if(!maxfree) return 0;
    blockmark_t *m = container_of(maxfree, blockmark_t, node);
    return SIZE_BLOCK(m->next);
}

#define THRESHOLD   (128-1*sizeof(blockmark_t))

static void* allocBlock(rb_t* tree, blockmark_t* sub, size_t size, void** pstart)
{
    blockmark_t *s = (blockmark_t*)sub;
    blockmark_t *n = NEXT_BLOCK(s);
    size+=sizeof(blockmark_t); // count current blockmark
    s->next.fill = 1;
    // check if a new mark is worth it
    if(SIZE_BLOCK(s->next)>size+2*sizeof(blockmark_t)+THRESHOLD) {
        size_t old_offs = s->next.offs;
        s->next.offs = size;
        blockmark_t *m = NEXT_BLOCK(s);
        m->prev.x32 = s->next.x32;
        m->next.fill = 0;
        m->next.offs = old_offs - size;
        n->prev.x32 = m->next.x32;
        rb_insert(tree, &(m->node));
        n = m;
    } else {
        // just fill the blok
        n->prev.fill = 1;
    }

    if(pstart && sub==*pstart) {
        // get the next free block
        while(n->next.fill)
            n = NEXT_BLOCK(n);
        *pstart = (void*)n;
    }
    return sub->mark;
}
static size_t freeBlock(rb_t *tree, size_t bsize, blockmark_t* sub, void** pstart)
{

    blockmark_t *m;
    blockmark_t *s = sub;
    blockmark_t *n = NEXT_BLOCK(s);
    s->next.fill = 0;
    n->prev.fill = 0;
    // check if merge with next
    while (n->next.x32 && !n->next.fill) {
        blockmark_t *n2 = NEXT_BLOCK(n);
        //remove n
        s->next.offs += n->next.offs;
        n2->prev.offs = s->next.offs;
        rb_remove(tree, &(n->node));
        n = n2;
    }
    // check if merge with previous
    while (s->prev.x32 && !s->prev.fill) {
        m = PREV_BLOCK(s);
        // remove s...
        m->next.offs += s->next.offs;
        n->prev.offs = m->next.offs;
        rb_remove(tree, &(s->node));
        s = m;
    }
    rb_insert(tree, &(m->node));
    if(pstart && (uintptr_t)*pstart>(uintptr_t)s) {
        *pstart = (void*)s;
    }
    // return free size at current block (might be bigger)
    return SIZE_BLOCK(s->next);
}

static size_t roundSize(size_t size)
{
    if(!size)
        return size;
    size = (size+7)&~7LL;   // 8 bytes align in size

    if(size<THRESHOLD)
        size = THRESHOLD;

    return size;
}

uintptr_t blockstree_start = 0;
uintptr_t blockstree_end = 0;
int blockstree_index = 0;

blocklist_t* findBlock(uintptr_t addr)
{
    if(blockstree) {
        uint32_t i;
        uintptr_t end;
        if(rb_get_end(blockstree, addr, &i, &end))
            return &p_blocks[i];
    } else {
        for(int i=0; i<n_blocks; ++i)
            if((addr>=(uintptr_t)p_blocks[i].block) && (addr<=(uintptr_t)p_blocks[i].block+p_blocks[i].size))
                return &p_blocks[i];
    }
    return NULL;
}
void add_blockstree(uintptr_t start, uintptr_t end, int idx)
{
    if(!blockstree)
        return;
    static int reent = 0;
    if(reent) {
        blockstree_start = start;
        blockstree_end = end;
        blockstree_index = idx;
        return;
    }
    reent = 1;
    blockstree_start = blockstree_end = 0;
    rb_set(blockstree, start, end, idx);
    while(blockstree_start || blockstree_end) {
        start = blockstree_start;
        end = blockstree_end;
        idx = blockstree_index;
        blockstree_start = blockstree_end = 0;
        rb_set(blockstree, start, end, idx);
    }
    reent = 0;
}

static uintptr_t    defered_prot_p = 0;
static size_t       defered_prot_sz = 0;
static uint32_t     defered_prot_prot = 0;
static sigset_t     critical_prot = {0};

// the BTYPE_MAP is a simple bitmap based allocator: it will allocate slices of 128bytes only, from a large 128k mapping
// the bitmap itself is also allocated in that mapping, as a slice of 128bytes, at the end of the mapping (and so marked as allocated)
void* map128_customMalloc(size_t size, int is32bits)
{
    //printf("map128_customMalloc\n");
    size = 128;
    for(int i=0; i<n_blocks; ++i) {
        if(p_blocks[i].block && (p_blocks[i].type == BTYPE_MAP) && p_blocks[i].maxfree) {
            // look for a free block
            uint8_t* map = p_blocks[i].first;
            for(uint32_t idx=p_blocks[i].lowest; idx<(p_blocks[i].size>>7); ++idx) {
                if(!(idx&7) && map[idx>>3]==0xff)
                    idx+=7;
                else if(!(map[idx>>3]&(1<<(idx&7)))) {
                    map[idx>>3] |= 1<<(idx&7);
                    p_blocks[i].maxfree -= 128;
                    p_blocks[i].lowest = idx+1;
                    return p_blocks[i].block+(idx<<7);
                }
            }
        }
    }
    // add a new block
    int i = n_blocks++;
    if(n_blocks>c_blocks) {
        c_blocks += 8;
        p_blocks = (blocklist_t*)realloc(p_blocks, c_blocks*sizeof(blocklist_t));// just use realloa
    }
    size_t allocsize = MMAPSIZE128;
    p_blocks[i].block = NULL;   // incase there is a re-entrance
    p_blocks[i].first = NULL;
    p_blocks[i].size = 0;
    void* p = mmap(NULL, allocsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    size_t mapsize = (allocsize/128)/8;
    mapsize = (mapsize+127)&~127LL;
    p_blocks[i].type = BTYPE_MAP;
    p_blocks[i].block = p;
    p_blocks[i].first = p+allocsize-mapsize;
    p_blocks[i].size = allocsize;
    // setup marks
    uint8_t* map = p_blocks[i].first;
    for(int idx=(allocsize-mapsize)>>7;  idx<(allocsize>>7); ++idx)
        map[idx>>3] |= (1<<(idx&7));
    // 32bits check
    if(is32bits && p>(void*)0xffffffffLL) {
        p_blocks[i].maxfree = allocsize - mapsize;
        return NULL;
    }
    // alloc 1st block
    void* ret = p_blocks[i].block;
    map[0] |= 1;
    p_blocks[i].lowest = 1;
    p_blocks[i].maxfree = allocsize - (mapsize+128);
    add_blockstree((uintptr_t)p, (uintptr_t)p+allocsize, i);
    return ret;
}
// the BTYPE_MAP64 is a simple bitmap based allocator: it will allocate slices of 64bytes only, from a large 64k mapping
// the bitmap itself is also allocated in that mapping, as a slice of 256bytes, at the end of the mapping (and so marked as allocated)
void* map64_customMalloc(size_t size, int is32bits)
{
    //printf("map64_customMalloc\n");
    size = 64;
    for(int i = 0; i < n_blocks; ++i) {
        if (p_blocks[i].block
         && p_blocks[i].type == BTYPE_MAP64
         && p_blocks[i].maxfree
        ) {
            uint16_t* map = p_blocks[i].first;
            uint32_t slices = p_blocks[i].size >> 6; 
            for (uint32_t idx = p_blocks[i].lowest; idx < slices; ++idx) {
                if (!(idx & 15) && map[idx >> 4] == 0xFFFF)
                    idx += 15;
                else if (!(map[idx >> 4] & (1u << (idx & 15)))) {
                    map[idx >> 4] |= 1u << (idx & 15);
                    p_blocks[i].maxfree -= 64;
                    p_blocks[i].lowest = idx + 1;
                    return p_blocks[i].block + (idx << 6);
                }
            }
        }
    }
    int i = n_blocks++;
    if (n_blocks > c_blocks) {
        c_blocks += 8;
        p_blocks = (blocklist_t*)realloc(p_blocks, c_blocks * sizeof(blocklist_t));
    }

    size_t allocsize = MMAPSIZE64; 
    p_blocks[i].block = NULL;    // guard re-entrance
    p_blocks[i].first = NULL;
    p_blocks[i].size  = 0;

    void* p = mmap(NULL, allocsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    size_t mapsize = (allocsize / 64) / 8; 
    mapsize = (mapsize + 255) & ~255LL;

    p_blocks[i].type  = BTYPE_MAP64;
    p_blocks[i].block = p;
    p_blocks[i].first = p+allocsize-mapsize;
    p_blocks[i].size  = allocsize;

    // mark the bitmap area itself as "used"
    uint16_t* map = p_blocks[i].first;
    for (size_t idx = (allocsize - mapsize) >> 6; idx < (allocsize >> 6); ++idx) {
        map[idx >> 4] |= 1u << (idx & 15);
    }

    if (is32bits && p > (void*)0xffffffffLL) {
        p_blocks[i].maxfree = allocsize - mapsize;
        return NULL;
    }

    void* ret = p_blocks[i].block;
    map[0] |= 1u;
    p_blocks[i].lowest  = 1;
    p_blocks[i].maxfree = allocsize - (mapsize + 64);
    add_blockstree((uintptr_t)p, (uintptr_t)p + allocsize, i);
    return ret;
}


void* internal_customMalloc(size_t size, int is32bits)
{
    //printf("Size = %lld\n", size);
    if(size<=64)
        return map64_customMalloc(size, is32bits);
    if(size<=128)
        return map128_customMalloc(size, is32bits);
    //makeprintf("internal_customMalloc\n");
    size_t init_size = size;
    size = roundSize(size);
    // look for free space
    blockmark_t* sub = NULL;
    size_t fullsize = size+2*sizeof(blockmark_t);
    for(int i=0; i<n_blocks; ++i) {
        if(p_blocks[i].block && (p_blocks[i].type == BTYPE_LIST) && p_blocks[i].maxfree>=init_size) {
            size_t rsize = 0;
            sub = getFirstBlock(&(p_blocks[i].free_list), init_size, &rsize, p_blocks[i].first);
            if(sub) {
                if(size>rsize)
                    size = init_size;
                if(rsize-size<THRESHOLD)
                    size = rsize;
                void* ret = allocBlock(&(p_blocks[i].free_list), sub, size, &p_blocks[i].first);
                if(rsize==p_blocks[i].maxfree)
                    p_blocks[i].maxfree = getMaxFreeBlock(&(p_blocks[i].free_list), p_blocks[i].size, p_blocks[i].first);
                return ret;
            }
        }
    }
    // add a new block
    int i = n_blocks++;
    if(n_blocks>c_blocks) {
        c_blocks += 8;
        p_blocks = (blocklist_t*)realloc(p_blocks, c_blocks*sizeof(blocklist_t));
    }
    size_t allocsize = (fullsize>MMAPSIZE)?fullsize:MMAPSIZE;
    allocsize = (allocsize+box64_pagesize-1)&~(box64_pagesize-1);
    if(is32bits) allocsize = (allocsize+0xffffLL)&~(0xffffLL);
    p_blocks[i].block = NULL;   // incase there is a re-entrance
    p_blocks[i].first = NULL;
    p_blocks[i].size = 0;
    p_blocks[i].type = BTYPE_LIST;
    void* p =mmap(NULL, allocsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    p_blocks[i].block = p;
    p_blocks[i].first = p;
    p_blocks[i].size = allocsize;
    // set up tree
    p_blocks[i].free_list.root = NULL;
    p_blocks[i].free_list.cmp_func = block_lessthan;
    p_blocks[i].free_list.cmp_func_by_value = size_lessthan;
    p_blocks[i].free_list.cmp_qual = size_equal;

    // setup marks
    blockmark_t* m = (blockmark_t*)p;
    m->prev.x32 = 0;
    m->next.fill = 0;
    m->next.offs = allocsize-sizeof(blockmark_t);
    blockmark_t* n = NEXT_BLOCK(m);
    n->next.x32 = 0;
    n->prev.x32 = m->next.x32;
    // alloc 1st block
    void* ret  = allocBlock(&(p_blocks[i].free_list), p, size, &p_blocks[i].first);
    p_blocks[i].maxfree = getMaxFreeBlock(&(p_blocks[i].free_list), p_blocks[i].size, p_blocks[i].first);
    add_blockstree((uintptr_t)p, (uintptr_t)p+allocsize, i);
    return ret;
}
void* customMalloc(size_t size)
{
    return internal_customMalloc(size, 0);
}

void internal_customFree(void* p, int is32bits)
{
    if(!p || !inited) {
        return;
    }
    uintptr_t addr = (uintptr_t)p;
    blocklist_t* l = findBlock(addr);
    if(l) {
        if(l->type==BTYPE_LIST) {
            blockmark_t* sub = (blockmark_t*)(addr-sizeof(blockmark_t));
            size_t newfree = freeBlock(&(l->free_list), l->size, sub, &l->first);
            if(l->maxfree < newfree) l->maxfree = newfree;
            return;
        } else if(l->type == BTYPE_MAP) {
            //BTYPE_MAP
            size_t idx = (addr-(uintptr_t)l->block)>>7;
            uint8_t* map = l->first;
            if(map[idx>>3]&(1<<(idx&7))) {
                map[idx>>3] ^= (1<<(idx&7));
                l->maxfree += 128;
            }   // warn if double free?
            if(l->lowest>idx)
                l->lowest = idx;
            return;
        }else{
            //BTYPE_MAP
            size_t idx = (addr-(uintptr_t)l->block)>>6;
            uint16_t* map = l->first;
            if(map[idx>>4]&(1<<(idx&15))) {
                map[idx>>4] ^= (1<<(idx&15));
                l->maxfree += 64;
            }   // warn if double free?
            if(l->lowest>idx)
                l->lowest = idx;
            return;
        }
    }
    if(n_blocks) {
        if(is32bits) {
            free(p);
        }
    }
}
void customFree(void* p)
{
    internal_customFree(p, 0);
}

void init_custommem_helper()
{
    blockstree = rbtree_init("blockstree");
    if(n_blocks)
        for(int i=0; i<n_blocks; ++i)
            rb_set(blockstree, (uintptr_t)p_blocks[i].block, (uintptr_t)p_blocks[i].block+p_blocks[i].size, i);
}


void fini_custommem_helper()
{
    rbtree_delete(blockstree);
    blockstree = NULL;
    for(int i=0; i<n_blocks; ++i)
        munmap(p_blocks[i].block, p_blocks[i].size);
    free(p_blocks);
    p_blocks = NULL;
    printf("n_blocks = %d\n", n_blocks);
    printf("c_blocks = %d\n", c_blocks);
    printf("uti = %lld\n", (n_blocks*MMAPSIZE));
    n_blocks = 0;       // number of blocks for custom malloc
    c_blocks = 0;    
}


