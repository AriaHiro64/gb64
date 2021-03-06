
#include <ultra64.h>
#include "memory.h"

struct HeapSegment* gFirstHeapSegment;

/*
 * Symbol genererated by "makerom" (RAM)
 */
extern char     _codeSegmentEnd[];
extern char    *_gEndSegments;

void initHeap(void* heapEnd)
{
    gFirstHeapSegment = (struct HeapSegment*)(((int)_codeSegmentEnd + 7) & ~0x7);

    gFirstHeapSegment->nextSegment = 0;
    gFirstHeapSegment->segmentEnd = heapEnd;

    markAllocated(_codeSegmentEnd, 
        (int)_gEndSegments - (int)_codeSegmentEnd
    );
}

void *cacheFreePointer(void* target)
{
    return (void*)((int)target & 0x0FFFFFFF | 0xA0000000);
}

void *malloc(unsigned int size)
{
    void *result;
    struct HeapSegment* prevSegment;
    struct HeapSegment* currentSegment;
    struct HeapSegment* nextSegment;
    int segmentSize;
    // 8 byte align for DMA
    size = (size + 7) & (~0x7);

    if (size < sizeof(struct HeapSegment))
    {
        size = sizeof(struct HeapSegment);
    }

    prevSegment = 0;
    currentSegment = gFirstHeapSegment;

    while (currentSegment)
    {
        segmentSize = (int)currentSegment->segmentEnd - (int)currentSegment;

        if (segmentSize >= size)
        {
            if (segmentSize >= size + sizeof(struct HeapSegment))
            {
                nextSegment = (struct HeapSegment*)((char*)currentSegment + size);
                nextSegment->nextSegment = currentSegment->nextSegment;
                nextSegment->segmentEnd = currentSegment->segmentEnd;

                if (prevSegment)
                {
                    prevSegment->nextSegment = nextSegment;
                }
                else
                {
                    gFirstHeapSegment = nextSegment;
                }
            }
            else
            {
                if (prevSegment)
                {
                    prevSegment->nextSegment = currentSegment->nextSegment;
                }
                else
                {
                    gFirstHeapSegment = currentSegment->nextSegment;
                }
            }

            return currentSegment;
        }

        prevSegment = currentSegment;
        currentSegment = currentSegment->nextSegment;
    }
    
    return 0;
}


void free(void* target) {
    // TODO
}

void markAllocated(void* addr, int length)
{
    struct HeapSegment* prevSegment;
    struct HeapSegment* currentSegment; 
    struct HeapSegment* nextSegment;
    void* addrEnd;

    prevSegment = 0;
    currentSegment = gFirstHeapSegment;
    addrEnd = (void*)((int)addr + length);

    while (currentSegment != 0)
    {
        if ((int)addr >= (int)currentSegment &&
            (int)addr < (int)currentSegment->segmentEnd
        )
        {
            int newCurrentSize;
            int newNextSize;

            newCurrentSize = (int)addr - (int)currentSegment;
            newNextSize = (int)currentSegment->segmentEnd - (int)addrEnd;

            if (newCurrentSize >= sizeof(struct HeapSegment))
            {
                if (newNextSize >= sizeof(struct HeapSegment))
                {
                    nextSegment = (struct HeapSegment*)addrEnd;
                    nextSegment->nextSegment = currentSegment->nextSegment;
                    nextSegment->segmentEnd = currentSegment->segmentEnd;
                    currentSegment->nextSegment = nextSegment;
                    currentSegment->segmentEnd = addr;
                }
                else
                {
                    currentSegment->segmentEnd = addr;
                }
            }
            else
            {
                if (newNextSize >= sizeof(struct HeapSegment))
                {
                    nextSegment = (struct HeapSegment*)addrEnd;
                    nextSegment->nextSegment = currentSegment->nextSegment;
                    nextSegment->segmentEnd = currentSegment->segmentEnd;
                }
                else
                {
                    nextSegment = currentSegment->nextSegment;
                }

                if (prevSegment)
                {
                    prevSegment->nextSegment = nextSegment;
                }
                else
                {
                    gFirstHeapSegment = nextSegment;
                }
            }

            return;
        }

        prevSegment = currentSegment;
        currentSegment = currentSegment->nextSegment;
    }
}

int calculateBytesFree()
{
    int result;
    struct HeapSegment* currentSegment; 

    currentSegment = gFirstHeapSegment;
    result = 0;

    while (currentSegment != 0) {
        result += (int)currentSegment->segmentEnd - (int)currentSegment;
        currentSegment = currentSegment->nextSegment;
    }

    return result;
}

int calculateLargestFreeChunk()
{
    int result;
    int current;
    struct HeapSegment* currentSegment; 

    currentSegment = gFirstHeapSegment;
    result = 0;

    while (currentSegment != 0) {
        current = (int)currentSegment->segmentEnd - (int)currentSegment;

        if (current > result)
        {
            result = current;
        }

        currentSegment = currentSegment->nextSegment;
    }

    return result;
}

void zeroMemory(void* memory, int size)
{
    unsigned char* asChar = (unsigned char*)memory;
    
    while (size > 0) {
        *asChar = 0;
        ++asChar;
        --size;
    }
}

void memCopy(void* target, const void* src, int size)
{
    unsigned char* targetAsChar;
    unsigned char* srcAsChar;

    targetAsChar = (unsigned char*)target;
    srcAsChar = (unsigned char*)src;
    
    while (size > 0) {
        *targetAsChar = *srcAsChar;
        ++targetAsChar;
        ++srcAsChar;
        --size;
    }
}