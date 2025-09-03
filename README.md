# RBTree-Accelerated Free List for `internal_customMalloc`

This repository experiments with using a **red-black tree free list** (based on `jserv/rbtree`) to speed up best-fit allocation in a custom memory allocator from box64. 
> TL;DR: We index *free blocks by size* in a balanced tree. Best-fit becomes `O(log n)` and extremely fast in fragmented heaps.

