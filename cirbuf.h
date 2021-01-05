#ifndef CIRBUF_H
#define CIRBUF_H

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

// Optimized circular buffer, works only with sizes power of two.
class CirBuf
{
#ifdef TESTING
    friend class MainTests;
#endif

    char *buf = NULL;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t size = 0;

    time_t resizedAt = 0;
public:

    CirBuf(size_t size);
    ~CirBuf();

    uint32_t usedBytes() const;
    uint32_t freeSpace() const;
    uint32_t maxWriteSize() const;
    uint32_t maxReadSize() const;
    char *headPtr();
    char *tailPtr();
    void advanceHead(uint32_t n);
    void advanceTail(uint32_t n);
    char peakAhead(uint32_t offset) const;
    void doubleSize();
    uint32_t getSize() const;

    time_t bufferLastResizedSecondsAgo() const;
    void resetSize(size_t size);
};

#endif // CIRBUF_H