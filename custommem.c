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
typedef enum {
    MEM_UNUSED = 0,
    MEM_ALLOCATED = 1,
    MEM_RESERVED = 2,
    MEM_MMAP = 3
} mem_flag_t;
static rbtree_t*  blockstree = NULL;

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
    rb_t                free_list;
} blocklist_t;


#define MMAPSIZE64 (64*2048)   // allocate 128kb sized blocks for 64byte map
#define MMAPSIZE128 (128*1024)  // allocate 128kb sized blocks for 128byte map
#define DYNMMAPSZ (2*1024*1024) // allocate 2Mb block for dynarec
#define DYNMMAPSZ0 (128*1024)   // allocate 128kb block for 1st page, to avoid wasting too much memory on small program / libs

static int                 n_blocks = 0;       // number of blocks for custom malloc
static int                 c_blocks = 0;       // capacity of blocks for custom malloc
static blocklist_t*        p_blocks = NULL;    // actual blocks for custom malloc

#define NEXT_BLOCK(b) (blockmark_t*)((uintptr_t)(b) + (b)->next.offs)
#define PREV_BLOCK(b) (blockmark_t*)(((uintptr_t)(b) - (b)->prev.offs))
#define LAST_BLOCK(b, s) (blockmark_t*)(((uintptr_t)(b)+(s))-sizeof(blockmark_t))
#define SIZE_BLOCK(b) (((ssize_t)b.offs)-sizeof(blockmark_t))
void rb_block_check(void* block, rb_t* tree);
static bool block_lessthan(const rb_node_t *a, const rb_node_t *b)
{
    const blockmark_t *block_a = container_of(a, blockmark_t, node);
    const blockmark_t *block_b = container_of(b, blockmark_t, node);
    if(SIZE_BLOCK(block_a->next) == SIZE_BLOCK(block_b->next))
        return block_a->mark < block_b->mark;
    return SIZE_BLOCK(block_a->next) < SIZE_BLOCK(block_b->next);
}



// return true if the size of the block is less than the input size

static size_t block_size(const rb_node_t* n) {
    const blockmark_t* bm = container_of(n, blockmark_t, node);
    return SIZE_BLOCK(bm->next); // or whatever holds the free-block size
}

static blockmark_t* block_node(const rb_node_t* n) {
    return  container_of(n, blockmark_t, node); 
}
 
static rb_node_t* rb_lower_bound(rb_t* t, size_t need) {
    if (!t || !t->root || !need)
        return NULL;
    rb_node_t *n = t->root, *cand = NULL;
    if (block_size(n) == need) return n;
    int i = 0;
    while (n) {
        size_t k = block_size(n);
        if (k < need) {
            n = get_child(n, RB_RIGHT);         // right
        } else {                          // k >= need
            cand = n;
            n = get_child(n, RB_LEFT);        // left
        }
    }
    return cand; // smallest size >= need, or NULL
}

//getFirstBlock(&(p_blocks[i].free_list), init_size, &rsize, p_blocks[i].first);
// get first subblock free in block. Return NULL if no block, else first subblock free (mark included), filling size
static blockmark_t* getFirstBlock(rb_t* tree, size_t maxsize, size_t* size, void* start)
{
   rb_node_t* block = rb_lower_bound(tree, maxsize); 
   if(block){
        blockmark_t *m = block_node(block);
        rb_remove(tree, block);
        *size = SIZE_BLOCK(m->next);
        return m;
   }
   return NULL;
}

static size_t getMaxFreeBlock(rb_t* tree, size_t block_size, void* start)
{
    
    rb_node_t* maxfree = rb_get_max(tree);
    if(!maxfree){
        //printf("getmax is NULL \n");
        return 0;
    }
    blockmark_t *m = container_of(maxfree, blockmark_t, node);
    //printf("getmax %d\n", SIZE_BLOCK(m->next));
    return SIZE_BLOCK(m->next);
}

#define THRESHOLD   (128-1*sizeof(blockmark_t))

static void* allocBlock(rb_t* tree, blockmark_t* sub, size_t size, void** pstart)
{
    //printf("allocBlock ");
    blockmark_t *s = (blockmark_t*)sub;
    blockmark_t *n = NEXT_BLOCK(s);
    size+=sizeof(blockmark_t); // count current blockmark
    s->next.fill = 1;
    // check if a new mark is worth it
    if(SIZE_BLOCK(s->next)>size+2*sizeof(blockmark_t)+THRESHOLD) {
        //printf("block size %d ", SIZE_BLOCK(s->next));
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
    //printf ("count = %d ", tree->count);
    return sub->mark;
}
/*freeBlock(&(l->free_list), l->size, sub, &l->first);*/
static size_t freeBlock(rb_t *tree, size_t bsize, blockmark_t* sub, void** pstart)
{
    /*
    
    */
    //printf("freeBlock ");
    blockmark_t *m;
    blockmark_t *s = sub;
    //printf("s->node = 0x%llx ", &(s->node));
    blockmark_t *n = NEXT_BLOCK(s);
    s->next.fill = 0;
    n->prev.fill = 0;
    // check if merge with next
    while (n->next.x32 && !n->next.fill) {
        blockmark_t *n2 = NEXT_BLOCK(n);
        s->next.offs += n->next.offs;
        n2->prev.offs = s->next.offs;
        //printf("remove n... ");
        rb_remove(tree, &(n->node));
        n = n2;
    }
    // check if merge with previous
    while (s->prev.x32 && !s->prev.fill) {
        m = PREV_BLOCK(s);
        rb_remove(tree, &(m->node));
        m->next.offs += s->next.offs;
        n->prev.offs = m->next.offs;
        //printf("remove s... ");
        s = m;
    }
    /*
    There s->prev.fill => should not added to the red black tree
    */
    //printf("insert s %lld\n", SIZE_BLOCK(s->next));
    /*if(rb_contains(tree, &(s->node))){
        printf(" dd 0x%llx ",&(s->node) );
    }*/
    rb_insert(tree, &(s->node));
    
    if(pstart && (uintptr_t)*pstart>(uintptr_t)s) {
        *pstart = (void*)s;
    }
    // return free size at current block (might be bigger)
    //printf("node count = %d\n", tree->count);
    //printf ("cuont = %d", tree->count);
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
    printf("!!!map64_customMalloc\n");
    size = 64;
    //int* ptr = NULL;
    //printf("%d\n", *ptr);
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
    //check
    size_t init_size = size;
    size = roundSize(size);
    // look for free space
    blockmark_t* sub = NULL;
    size_t fullsize = size+2*sizeof(blockmark_t);
    //printf("n_block = %d\n", n_blocks);
    // selecet the block with count > 1 first then select the count = 1
    for(int i=0; i<n_blocks; ++i) {
        if(p_blocks[i].block && (p_blocks[i].type == BTYPE_LIST) && p_blocks[i].maxfree>=init_size) {
            //printf("good p_blocks[%d].block \n", i);
            //if(p_blocks[i].free_list.count == 1 && p_blocks[i].size > p_blocks[i].maxfree + sizeof(blocklist_t)*2)
            //    continue;
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
                    //printf("count = %d\n", p_blocks[i].free_list.count);
                return ret;
            }
        }
        rb_block_check(p_blocks[i].block, &(p_blocks[i].free_list));
    }
    // add a new block
    //printf("bb n_block = %d\n", n_blocks);
    int i = n_blocks++;
    //printf("need new block\n");
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
    memset(&(p_blocks[i].free_list), 0, sizeof(rb_t));
    p_blocks[i].free_list.root = NULL;
    p_blocks[i].free_list.cmp_func = block_lessthan;
    //p_blocks[i].free_list.cmp_func_by_value = size_lessthan;
    p_blocks[i].free_list.count = 0;
    //if(i > 0) printf("%d count = %d\n", i-1 , p_blocks[i-1].free_list.count);

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
    if(!p) {
        return;
    }
    uintptr_t addr = (uintptr_t)p;
    blocklist_t* l = findBlock(addr);
    //printf(" l = %lld\n", l);
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
        printf("error\n");
    printf("\n==================================== Start ==================================== \n");
}



size_t fini_custommem_helper()
{
    rbtree_delete(blockstree);
    blockstree = NULL;
    for(int i=0; i<n_blocks; ++i){
        //printf("p_blocks[%d].free_list.count = %lld\n", i, p_blocks[i].free_list.count);
        munmap(p_blocks[i].block, p_blocks[i].size);
    }
    free(p_blocks);
    p_blocks = NULL;
    //printf("Allocations = %d\n", N);
    printf("Native Size = %d\n", (n_blocks*MMAPSIZE));
    printf("MMAPSIZE = %d\n", MMAPSIZE);
    printf("mmap times = %d\n", n_blocks);
    printf("===================================== End ===================================== \n");
    n_blocks = 0;       // number of blocks for custom malloc
    c_blocks = 0;  
    return n_blocks*MMAPSIZE; // Native Size
}

/*For Box64*/
static rbtree_t*  blockstree_box64 = NULL;


typedef struct blocklist_box64_s {
    void*               block;
    size_t              maxfree;
    size_t              size;
    void*               first;
    uint32_t            lowest;
    uint8_t             type;
} bblocklist_box64_t;

static int                 n_blocks_box64 = 0;       // number of blocks for custom malloc
static int                 c_blocks_box64 = 0;       // capacity of blocks for custom malloc
static bblocklist_box64_t*        p_blocks_box64 = NULL;    // actual blocks for custom malloc

#define NEXT_BLOCK_BOX64(b) (blockmark_box64_t*)((uintptr_t)(b) + (b)->next.offs)
#define PREV_BLOCK_BOX64(b) (blockmark_box64_t*)(((uintptr_t)(b) - (b)->prev.offs))
#define LAST_BLOCK_BOX64(b, s) (blockmark_box64_t*)(((uintptr_t)(b)+(s))-sizeof(blockmark_box64_t))
#define SIZE_BLOCK_BOX64(b) (((ssize_t)b.offs)-sizeof(blockmark_box64_t))

// get first subblock free in block. Return NULL if no block, else first subblock free (mark included), filling size
//sub = getFirstBlock_box64(p_blocks_box64[i].block, init_size, &rsize, p_blocks_box64[i].first);
static blockmark_box64_t* getFirstBlock_box64(void* block, size_t maxsize, size_t* size, void* start)
{
    // get start of block
    blockmark_box64_t *m = (blockmark_box64_t*)((start)?start:block);
    while(m->next.x32) {    // while there is a subblock
        if(!m->next.fill && SIZE_BLOCK_BOX64(m->next)>=maxsize) {
            *size = SIZE_BLOCK_BOX64(m->next);
            return m;
        }
        m = NEXT_BLOCK_BOX64(m);
    }

    return NULL;
}

static size_t getMaxfreeBlock_box64_box64(void* block, size_t block_size, void* start)
{
    // get start of block
    if(start) {
        blockmark_box64_t *m = (blockmark_box64_t*)start;
        ssize_t maxsize = 0;
        while(m->next.x32) {    // while there is a subblock
            if(!m->next.fill && SIZE_BLOCK_BOX64(m->next)>maxsize) {
                maxsize = SIZE_BLOCK_BOX64(m->next);
            }
            m = NEXT_BLOCK_BOX64(m);
        }
        return maxsize;
    } else {
        blockmark_box64_t *m = LAST_BLOCK_BOX64(block, block_size); // start with the end
        ssize_t maxsize = 0;
        while(m->prev.x32 && (((uintptr_t)block+maxsize)<(uintptr_t)m)) {    // while there is a subblock
            if(!m->prev.fill && SIZE_BLOCK_BOX64(m->prev)>maxsize) {
                maxsize = SIZE_BLOCK_BOX64(m->prev);
            }
            m = PREV_BLOCK_BOX64(m);
        }
        return maxsize;
    }
}

#define THRESHOLD_BOX64   (128-1*sizeof(blockmark_box64_t))

static void* allocBlock_box64(void* block, blockmark_box64_t* sub, size_t size, void** pstart)
{
    (void)block;

    blockmark_box64_t *s = (blockmark_box64_t*)sub;
    blockmark_box64_t *n = NEXT_BLOCK_BOX64(s);

    size+=sizeof(blockmark_box64_t); // count current blockmark
    s->next.fill = 1;
    // check if a new mark is worth it
    if(SIZE_BLOCK_BOX64(s->next)>size+2*sizeof(blockmark_box64_t)+THRESHOLD_BOX64) {
        // create a new mark
        size_t old_offs = s->next.offs;
        s->next.offs = size;
        blockmark_box64_t *m = NEXT_BLOCK_BOX64(s);
        m->prev.x32 = s->next.x32;
        m->next.fill = 0;
        m->next.offs = old_offs - size;
        n->prev.x32 = m->next.x32;
        n = m;
    } else {
        // just fill the blok
        n->prev.fill = 1;
    }

    if(pstart && sub==*pstart) {
        // get the next free block
        while(n->next.fill)
            n = NEXT_BLOCK_BOX64(n);
        *pstart = (void*)n;
    }
    return sub->mark;
}
static size_t freeBlock_box64(void *block, size_t bsize, blockmark_box64_t* sub, void** pstart)
{
    blockmark_box64_t *m = (blockmark_box64_t*)block;
    blockmark_box64_t *s = sub;
    blockmark_box64_t *n = NEXT_BLOCK_BOX64(s);
    s->next.fill = 0;
    n->prev.fill = 0;
    // check if merge with next
    while (n->next.x32 && !n->next.fill) {
        blockmark_box64_t *n2 = NEXT_BLOCK_BOX64(n);
        //remove n
        s->next.offs += n->next.offs;
        n2->prev.offs = s->next.offs;
        n = n2;
    }
    // check if merge with previous
    while (s->prev.x32 && !s->prev.fill) {
        m = PREV_BLOCK_BOX64(s);
        // remove s...
        m->next.offs += s->next.offs;
        n->prev.offs = m->next.offs;
        s = m;
    }
    if(pstart && (uintptr_t)*pstart>(uintptr_t)s) {
        *pstart = (void*)s;
    }
    // return free size at current block (might be bigger)
    return SIZE_BLOCK_BOX64(s->next);
}


static size_t roundSize_box64(size_t size)
{
    if(!size)
        return size;
    size = (size+7)&~7LL;   // 8 bytes align in size

    if(size<THRESHOLD_BOX64)
        size = THRESHOLD_BOX64;

    return size;
}

uintptr_t blockstree_box64_start = 0;
uintptr_t blockstree_box64_end = 0;
int blockstree_box64_index = 0;

bblocklist_box64_t* findBlock_box64(uintptr_t addr)
{
    if(blockstree_box64) {
        uint32_t i;
        uintptr_t end;
        if(rb_get_end(blockstree_box64, addr, &i, &end))
            return &p_blocks_box64[i];
    } else {
        for(int i=0; i<n_blocks_box64; ++i)
            if((addr>=(uintptr_t)p_blocks_box64[i].block) && (addr<=(uintptr_t)p_blocks_box64[i].block+p_blocks_box64[i].size))
                return &p_blocks_box64[i];
    }
    return NULL;
}
void add_blockstree_box64_box64(uintptr_t start, uintptr_t end, int idx)
{
    if(!blockstree_box64)
        return;
    static int reent = 0;
    if(reent) {
        blockstree_box64_start = start;
        blockstree_box64_end = end;
        blockstree_box64_index = idx;
        return;
    }
    reent = 1;
    blockstree_box64_start = blockstree_box64_end = 0;
    rb_set(blockstree_box64, start, end, idx);
    while(blockstree_box64_start || blockstree_box64_end) {
        start = blockstree_box64_start;
        end = blockstree_box64_end;
        idx = blockstree_box64_index;
        blockstree_box64_start = blockstree_box64_end = 0;
        rb_set(blockstree_box64, start, end, idx);
    }
    reent = 0;
}

void* internal_customMalloc_box64(size_t size, int is32bits)
{
    //printf("Size = %lld\n", size);
    if(size<=64)
        return map64_customMalloc(size, is32bits);
    if(size<=128)
        return map128_customMalloc(size, is32bits);
    //makeprintf("internal_customMalloc_box64\n");
    size_t init_size = size;
    size = roundSize_box64(size);
    // look for free space
    blockmark_box64_t* sub = NULL;
    size_t fullsize = size+2*sizeof(blockmark_box64_t);
    for(int i=0; i<n_blocks_box64; ++i) {
        if(p_blocks_box64[i].block && (p_blocks_box64[i].type == BTYPE_LIST) && p_blocks_box64[i].maxfree>=init_size) {
            size_t rsize = 0;
            sub = getFirstBlock_box64(p_blocks_box64[i].block, init_size, &rsize, p_blocks_box64[i].first);
            if(sub) {
                if(size>rsize)
                    size = init_size;
                if(rsize-size<THRESHOLD_BOX64)
                    size = rsize;
                void* ret = allocBlock_box64(p_blocks_box64[i].block, sub, size, &p_blocks_box64[i].first);
                if(rsize==p_blocks_box64[i].maxfree)
                    p_blocks_box64[i].maxfree = getMaxfreeBlock_box64_box64(p_blocks_box64[i].block, p_blocks_box64[i].size, p_blocks_box64[i].first);
                //printf("box new free = %lld\n", p_blocks_box64[i].maxfree);
                return ret;
            }
        }
    }
    // add a new block
    int i = n_blocks_box64++;
    if(n_blocks_box64>c_blocks_box64) {
        c_blocks_box64 += 8;
        p_blocks_box64 = (bblocklist_box64_t*)realloc(p_blocks_box64, c_blocks_box64*sizeof(bblocklist_box64_t));
    }
    size_t allocsize = (fullsize>MMAPSIZE)?fullsize:MMAPSIZE;
    allocsize = (allocsize+box64_pagesize-1)&~(box64_pagesize-1);
    if(is32bits) allocsize = (allocsize+0xffffLL)&~(0xffffLL);
    p_blocks_box64[i].block = NULL;   // incase there is a re-entrance
    p_blocks_box64[i].first = NULL;
    p_blocks_box64[i].size = 0;
    p_blocks_box64[i].type = BTYPE_LIST;
    void* p =mmap(NULL, allocsize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    p_blocks_box64[i].block = p;
    p_blocks_box64[i].first = p;
    p_blocks_box64[i].size = allocsize;
    // setup marks
    blockmark_box64_t* m = (blockmark_box64_t*)p;
    m->prev.x32 = 0;
    m->next.fill = 0;
    m->next.offs = allocsize-sizeof(blockmark_box64_t);
    blockmark_box64_t* n = NEXT_BLOCK_BOX64(m);
    n->next.x32 = 0;
    n->prev.x32 = m->next.x32;
    // alloc 1st block
    void* ret  = allocBlock_box64(p_blocks_box64[i].block, p, size, &p_blocks_box64[i].first);
    p_blocks_box64[i].maxfree = getMaxfreeBlock_box64_box64(p_blocks_box64[i].block, p_blocks_box64[i].size, p_blocks_box64[i].first);
    add_blockstree_box64_box64((uintptr_t)p, (uintptr_t)p+allocsize, i);
    return ret;
}
void* customMalloc_box64(size_t size)
{
    return internal_customMalloc_box64(size, 0);
}

void internal_customFree_box64(void* p, int is32bits)
{
    //printf("internal_customFree_box64\n");
    if(!p) {
        return;
    }
    uintptr_t addr = (uintptr_t)p;
    bblocklist_box64_t* l = findBlock_box64(addr);
    if(l) {
        if(l->type==BTYPE_LIST) {
            blockmark_box64_t* sub = (blockmark_box64_t*)(addr-sizeof(blockmark_box64_t));
            size_t newfree = freeBlock_box64(l->block, l->size, sub, &l->first);
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
    if(n_blocks_box64) {
        if(is32bits) {
            free(p);
        }
    }
}
void customFree_box64(void* p)
{
    internal_customFree_box64(p, 0);
}

void init_custommem_helper_box64()
{
    blockstree_box64 = rbtree_init("blockstree_box64");
    // if there is some blocks already
    if(n_blocks_box64)
       printf("error\n");
    printf("\n==================================== Start ==================================== \n");
    n_blocks_box64 = 0;       // number of blocks for custom malloc
    c_blocks_box64 = 0;  
}


size_t fini_custommem_helper_box64()
{
    rbtree_delete(blockstree_box64);
    blockstree_box64 = NULL;
    for(int i=0; i<n_blocks_box64; ++i)
        munmap(p_blocks_box64[i].block, p_blocks_box64[i].size);
    free(p_blocks_box64);
    p_blocks_box64 = NULL;
    printf("Native Size = %d MB\n", (n_blocks_box64*MMAPSIZE)/(1048576));
    printf("MMAPSIZE = %d\n", MMAPSIZE);
    printf("mmap times = %d\n", n_blocks_box64);
    printf("===================================== End ===================================== \n");
    n_blocks_box64 = 0;       // number of blocks for custom malloc
    c_blocks_box64 = 0;  
    return n_blocks_box64*MMAPSIZE;
}


void rb_block_check(void* block, rb_t* tree)
{
    blockmark_t *m = (blockmark_t*)block;
    while(m->next.x32) {    // while there is a subblock
        if(!m->next.fill && !  rb_contains(tree, &(m->node)))
            printf("error, Didn't contain %lld\n ", SIZE_BLOCK(m->next)); 
        m = NEXT_BLOCK(m);
    }
}

