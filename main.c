#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mach/mach_time.h>
#include "custommem.h"

#define N 10000

/*
Latency: How fast are malloc() / free() / realloc()?

Throughput: How many allocations per second?

Fragmentation: How much memory is wasted?
*/

/* Timing utilities */
static double timespec_diff(const struct timespec *start,
                            const struct timespec *end)
{
    return (double) (end->tv_sec - start->tv_sec) +
           (double) (end->tv_nsec - start->tv_nsec) * 1e-9;
}

static void print_timing(const char *operation, int count, double elapsed)
{
    printf("%-20s: %d ops in %.3f sec (%.3f µs/op, %.0f ops/sec)\n", operation,
           count, elapsed, elapsed / count * 1e6, count / elapsed);
}


void test_custommem_rand()
{
    init_custommem_helper();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    int alloc_count = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i){
        int index = rand()%N;
        if(ptr_custoMalloc[index]){  // Deallocation benchmark
            customFree(ptr_custoMalloc[index]);
            ptr_custoMalloc[index] = NULL;
        }else if (index&1 && !ptr_custoMalloc[index]){ // Allocation benchmark
            ptr_custoMalloc[index] = customMalloc(rand()%1500 + 2853);
        }else{
        i-- ;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc (rbtree) rand", N, elapsed);
    fini_custommem_helper();
}

void test_custommem_box64_rand()
{
    init_custommem_helper_box64();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    int alloc_count = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i){
        int index = rand()%N;
        if(ptr_custoMalloc[index]){  // Deallocation benchmark
            customFree_box64(ptr_custoMalloc[index]);
            ptr_custoMalloc[index] = NULL;
            alloc_count--;
        }else if (index&1 && !ptr_custoMalloc[index]){ // Allocation benchmark
            ptr_custoMalloc[index] = customMalloc_box64(rand()%1500 + 2853);
            alloc_count++;
        }else{
        i-- ;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc (box64) rand", N, elapsed);
    fini_custommem_helper_box64();
}

void test_custommem_sequ()
{
    init_custommem_helper();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    int alloc_count = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = customMalloc(rand()%1500 + 2853);
    for (int i = 0; i < N; ++i)
        customFree(ptr_custoMalloc[i]);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc (rbtree) sequ", N, elapsed);
    fini_custommem_helper();
}

void test_custommem_box64_sequ()
{
    init_custommem_helper_box64();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    int alloc_count = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = customMalloc_box64(rand()%1500 + 2853);
    for (int i = 0; i < N; ++i)
        customFree_box64(ptr_custoMalloc[i]);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc (box64) sequ", N, elapsed);
    fini_custommem_helper_box64();
}


void test_custommem_fixsize()
{
    init_custommem_helper();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; i++){
         //printf("ptr i = %d ", i);
        ptr_custoMalloc[i] = customMalloc(rand()%300+2853);
    }
    for(int i = 0; i < N; i+=2){
        //printf("ptr i = %d ", i);
        customFree(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;
    }
    struct timespec start, end;

    void* real_ptr_custoMalloc[N];
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; i++){
        //printf("real_ptr i = %d ", i);
        real_ptr_custoMalloc[i] = customMalloc(2853);
    }
    for(int i = 0; i < N; i++){
        //printf("real_ptr i = %d ", i);
        customFree(real_ptr_custoMalloc[i]);
        real_ptr_custoMalloc[i] = NULL;

    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    for(int i = 0; i < N; i++){
        //printf("ptr i = %d ", i);
        if(ptr_custoMalloc[i])customFree(ptr_custoMalloc[i]);
    }
    print_timing("customMalloc rbtree fix", N, elapsed);
    fini_custommem_helper();
}

void test_custommem_box64_fixsize()
{
    init_custommem_helper_box64();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; i++)
        ptr_custoMalloc[i] = customMalloc_box64(rand()%2853+129);
    for(int i = 0; i < N; i+=2){
        customFree_box64(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;

    }
   
    struct timespec start, end;
    void* real_ptr_custoMalloc[N];
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; i++)
        real_ptr_custoMalloc[i] = customMalloc_box64(2853);
    
    for(int i = 0; i < N; i++){
        customFree_box64(real_ptr_custoMalloc[i]);
        real_ptr_custoMalloc[i] = NULL;

    }
    
        clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    for(int i = 0; i < N; i++){
        if(ptr_custoMalloc[i])customFree_box64(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;

    }
    print_timing("customMalloc box64 fix", N, elapsed);
    fini_custommem_helper_box64();
}





/*
An allocator’s throughput, which is defined as the
number of requests that it completes per unit time. For example, if an alloca-
tor completes 500 allocate requests and 500 free requests in 1 second, then its
throughput is 1,000 operations per second.
*/


// --- knobs you can tune ---

#define WARMUP_OPS 200000   // warmup to build a messy heap
#define MAX_LIVE   20000    // target live set size
#define PIN_COUNT  512      // long-lived blockers that prevent full coalescing

// Optional stats hooks; define these in your allocator if you want periodic prints.
#ifdef HAVE_ALLOCATOR_STATS
extern size_t cm_total_free_bytes(void);
extern size_t cm_max_free_block(void);
extern int    cm_rbt_node_count(void);
#endif

// Simple fast RNG (deterministic)
static inline uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

// Skewed size picker: small-heavy, occasional large to hit the RBT
static inline size_t pick_size(uint32_t *seed) {
    uint32_t r = xorshift32(seed);
    // ~1/64 requests are large (64 KiB .. 512 KiB), rest are 16..4096 bytes geometric-ish
    if ((r & 63u) == 0u) {
        size_t big = ((r >> 8) & 0x7) + 1;        // 1..8
        return (size_t)big * 64 * 1024;           // 64 KiB .. 512 KiB
    }
    // bias toward smaller sizes; tweak as needed
    size_t small = 16u << ( (r >> 8) & 3u );      // 16,32,64,128
    small += (r >> 12) & 0x3FFu;                  // +0..1023
    small += 129;
    return small;
}

static void mixed_workload(uint32_t seed_init, int ops, int report_every)
{
    uint32_t seed = seed_init;
    void **live = (void**)customMalloc(sizeof(void*) * (MAX_LIVE + PIN_COUNT));
    int live_n = 0;

    // Pins: allocated once and never freed until the very end
    for (int i = 0; i < PIN_COUNT; i++) {
        size_t sz = (i & 7) ? 8192 : 131072; // a mix of 8 KiB and 128 KiB
        void *p = customMalloc(sz);
        if (p) live[live_n++] = p;
    }

    for (int i = 0; i < ops; i++) {
        // Mostly allocate while live set < MAX_LIVE; otherwise free randomly
        int do_alloc = (live_n == 0) || (live_n < MAX_LIVE && (xorshift32(&seed) & 3));

        if (do_alloc) {
            size_t sz = pick_size(&seed);
            void *p = customMalloc(sz);
            if (p) {
                if (live_n < MAX_LIVE + PIN_COUNT) live[live_n++] = p;
                else customFree(p); // shouldn't happen, but be safe
            }
        } else {
            int idx = (xorshift32(&seed) % live_n);
            // Don’t free pins during the run: keep them blocking coalescing
            if (idx < PIN_COUNT) idx = PIN_COUNT + (idx % (live_n - PIN_COUNT));
            customFree(live[idx]);
            live[idx] = live[--live_n];
        }

#ifdef HAVE_ALLOCATOR_STATS
        if (report_every && ((i % report_every) == 0)) {
            size_t tf = cm_total_free_bytes();
            size_t mf = cm_max_free_block();
            int nodes = cm_rbt_node_count();
            double frag = (tf > 0 && mf <= tf) ? (double)(tf - mf) / (double)tf : 0.0;
            fprintf(stderr, "[ops=%d] nodes=%d total_free=%zu max_free=%zu frag=%.3f\n",
                    i, nodes, tf, mf, frag);
        }
#endif
    }

    // Tear down (free non-pin live blocks; pins freed by caller)
    for (int i = live_n - 1; i >= PIN_COUNT; --i)
        customFree(live[i]);
    customFree(live);
}

void test_custommem_mixed()
{
    init_custommem_helper();

    // --- Warm up to create a fragmented heap and populate the RBT ---
    mixed_workload(/*seed=*/0xC0FFEEu, /*ops=*/WARMUP_OPS, /*report_every=*/50000);

    // --- Timed phase: interleaved random alloc/free with mixed sizes ---
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    mixed_workload(/*seed=*/0xBADC0DEu, /*ops=*/N, /*report_every=*/0);

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc rbtree mixed", N, elapsed);

    // Full cleanup: free the pins by doing one last linear free/teardown
    fini_custommem_helper();
}


int main(){
    srand(time(NULL));
    //test_custommem_sequ();
    //test_custommem_box64_sequ();
    //test_custommem_rand();
    //test_custommem_box64_rand();
    test_custommem_fixsize();
    test_custommem_box64_fixsize();
    //test_custommem_mixed();
    return 0;
}



