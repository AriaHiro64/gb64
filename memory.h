
#ifndef _MEMORY_H
#define _MEMORY_H

#include <stddef.h>

// Aligns size to 8 bytes
#define ALIGN_8(size) (((size) + 0x7) & ~0x7);

extern struct HeapSegment* gFirstHeapSegment;

struct HeapSegment
{
    struct HeapSegment* nextSegment;
    // struct HeapSegment* prevSegment;
    void* segmentEnd;
};

void initHeap(void* heapEnd);
void *cacheFreePointer(void* target);
void *malloc(unsigned int size);
void free(void* target);
void markAllocated(void* addr, int length);
int calculateBytesFree();
int calculateLargestFreeChunk();
extern void zeroMemory(void* memory, int size);
extern void memCopy(void* target, const void* src, int size);

#endif