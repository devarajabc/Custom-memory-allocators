#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mach/mach_time.h>
#include "custommem.h"

#define N 6000

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

void test_custommem(int frag)
{
    long long pre_size = 0;
    init_custommem_helper();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; i++){
        size_t size = rand()%1500+2853;
        ptr_custoMalloc[i] = customMalloc(size);
        pre_size += size;
    }
    for(int i = 0; i < N; i+=frag){
        //printf("ptr i = %d ", i);
        customFree(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;
    }
    struct timespec start, end;

    void* real_ptr_custoMalloc[N];
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; i++){
        real_ptr_custoMalloc[i] = customMalloc(rand()%1500+2853);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc rbtree", N, elapsed);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i = 0; i < N; i++){
        //printf("real_ptr i = %d ", i);
        customFree(real_ptr_custoMalloc[i]);
        real_ptr_custoMalloc[i] = NULL;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = timespec_diff(&start, &end);
    print_timing("customFree rbtree", N, elapsed);
    
   
    for(int i = 0; i < N; i++){
        //printf("ptr i = %d ", i);
        if(ptr_custoMalloc[i])customFree(ptr_custoMalloc[i]);
    }
    printf("Shallow Size = %lld\n",N*(2583+sizeof(blockmark_t))+pre_size);
    fini_custommem_helper();
}

void test_custommem_box64(int frag)
{
    long long pre_size = 0;
    init_custommem_helper_box64();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; i++){
        size_t size = rand()%1500+2853;
        ptr_custoMalloc[i] = customMalloc_box64(size);
        pre_size += size;
    }
    for(int i = 0; i < N; i+=frag){
        customFree_box64(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;

    }
   
    struct timespec start, end;
    void* real_ptr_custoMalloc[N];
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; i++)
        real_ptr_custoMalloc[i] = customMalloc_box64(rand()%1500+2853);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    print_timing("customMalloc box64", N, elapsed);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for(int i = 0; i < N; i++){
        customFree_box64(real_ptr_custoMalloc[i]);
        real_ptr_custoMalloc[i] = NULL;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = timespec_diff(&start, &end);
    print_timing("customFree box64", N, elapsed);
    
    
    for(int i = 0; i < N; i++){
        if(ptr_custoMalloc[i])customFree_box64(ptr_custoMalloc[i]);
        ptr_custoMalloc[i] = NULL;

    }
   
    printf("Shallow Size = %lld\n",N*(2583+sizeof(blockmark_box64_t)) + pre_size);
    fini_custommem_helper_box64();
}





/*
An allocator’s throughput, which is defined as the
number of requests that it completes per unit time. For example, if an alloca-
tor completes 500 allocate requests and 500 free requests in 1 second, then its
throughput is 1,000 operations per second.
*/

void test_mixed(int frag)
{
    printf("\nPrepared for allocator perf test under heavy fragmentation... \n(free every %d block in list A, then allocate all blocks in list B from the same pool)\n", frag);
    printf("Test Times: %lld \n", N);
    test_custommem(frag);
    test_custommem_box64(frag);
}


int main(int argc, char *argv[]){
    if (argc > 1) {
        int frag_times = atoi(argv[1]);
          srand(time(NULL));
        test_mixed(frag_times);
    }   
    return 0;
}



