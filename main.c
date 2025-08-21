#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mach/mach_time.h>
#include "custommem.h"

#define N 1000000

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
            alloc_count--;
        }else if (rand()&1 && !ptr_custoMalloc[index]){ // Allocation benchmark
            ptr_custoMalloc[index] = customMalloc(rand()%1500 + 2853);
            alloc_count++;
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
        }else if (rand()&1 && !ptr_custoMalloc[index]){ // Allocation benchmark
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
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = customMalloc(2853);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    for(int i = 0; i < N; i++)
        customFree(ptr_custoMalloc[i]);
    print_timing("customMalloc rbtree fix", N, elapsed);
    fini_custommem_helper();
}

void test_custommem_box64_fixsize()
{
    init_custommem_helper_box64();
    void* ptr_custoMalloc[N];
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = NULL;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < N; ++i)
        ptr_custoMalloc[i] = customMalloc_box64(2853);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);
    for(int i = 0; i < N; i++)
        customFree_box64(ptr_custoMalloc[i]);
    

    print_timing("customMalloc box64 fix", N, elapsed);
    fini_custommem_helper_box64();
}

void test_malloc()
{
    void* ptr_Malloc[N];
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

     // Allocation benchmark
    for (int i = 0; i < N; ++i)
        ptr_Malloc[i] = NULL;
    // Deallocation benchmark
    for (int i = 0; i < N; ++i)
        ptr_Malloc[i] = customMalloc(2853);
    for(int i = 0; i < N; i++)
        customFree(ptr_Malloc[i]);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    print_timing("Malloc rand", N, elapsed);
}

void test_malloc_fix()
{
    void* ptr_Malloc[N];
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

     // Allocation benchmark
    for (int i = 0; i < N; ++i)
        ptr_Malloc[i] = NULL;
    // Deallocation benchmark
    for (int i = 0; i < N; ++i){
        int index = rand()%N;
        if(ptr_Malloc[index]){  // Deallocation benchmark
            free(ptr_Malloc[index]);
            ptr_Malloc[index] = NULL;
        }else if (rand()&1 && !ptr_Malloc[index]){ // Allocation benchmark
            ptr_Malloc[index] = malloc(56);
        }else{
        i-- ;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = timespec_diff(&start, &end);

    print_timing("Malloc 56", N, elapsed);
}



/*
An allocator’s throughput, which is defined as the
number of requests that it completes per unit time. For example, if an alloca-
tor completes 500 allocate requests and 500 free requests in 1 second, then its
throughput is 1,000 operations per second.
*/




int main(){
    srand(time(NULL));
    test_custommem_sequ();
    test_custommem_box64_sequ();
    test_custommem_rand();
    test_custommem_box64_rand();
    test_custommem_fixsize();
    test_custommem_box64_fixsize();
    return 0;
}



