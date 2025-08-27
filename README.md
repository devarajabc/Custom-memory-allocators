# RBTree-Accelerated Free List for `internal_customMalloc`

This repository experiments with using a **red-black tree free list** (based on `jserv/rbtree`) to speed up best-fit allocation in a custom memory allocator. In heavy-fragmentation workloads, throughput improves by **~100–200×** when paired with a larger `MMAPSIZE`.

> TL;DR: We index *free blocks by size* in a balanced tree. Best-fit becomes `O(log n)` and extremely fast in fragmented heaps.

