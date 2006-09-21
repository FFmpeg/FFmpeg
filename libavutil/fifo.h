#ifndef FIFO_H
#define FIFO_H

typedef struct AVFifoBuffer {
    uint8_t *buffer;
    uint8_t *rptr, *wptr, *end;
} AVFifoBuffer;

int av_fifo_init(AVFifoBuffer *f, int size);
void av_fifo_free(AVFifoBuffer *f);
int av_fifo_size(AVFifoBuffer *f);
int av_fifo_read(AVFifoBuffer *f, uint8_t *buf, int buf_size);
int av_fifo_generic_read(AVFifoBuffer *f, int buf_size, void (*func)(void*, void*, int), void* dest);
void av_fifo_write(AVFifoBuffer *f, const uint8_t *buf, int size);
void av_fifo_realloc(AVFifoBuffer *f, unsigned int size);
void av_fifo_drain(AVFifoBuffer *f, int size);

static inline uint8_t av_fifo_peek(AVFifoBuffer *f, int offs)
{
    uint8_t *ptr = f->rptr + offs;
    if (ptr >= f->end)
        ptr -= f->end - f->buffer;
    return *ptr;
}
#endif /* FIFO_H */
