#ifndef SWSCALE_EMU_H
#define SWSCALE_EMU_H
/* Dummy, only useful for compilation! */
#define SWS_FAST_BILINEAR 1
#define SWS_BILINEAR 2
#define SWS_BICUBIC  4
#define SWS_X        8
#define SWS_POINT    0x10
#define SWS_AREA     0x20
#define SWS_BICUBLIN 0x40
#define SWS_GAUSS    0x80
#define SWS_SINC     0x100
#define SWS_LANCZOS  0x200
#define SWS_SPLINE   0x400

#define SwsFilter void
struct SwsContext {
    struct ImgReSampleContext *resampling_ctx;
    enum PixelFormat src_pix_fmt, dst_pix_fmt;
};

struct SwsContext *sws_getContext(int srcW, int srcH, int srcFormat,
                                  int dstW, int dstH, int dstFormat,
                                  int flags, SwsFilter *srcFilter,
                                  SwsFilter *dstFilter, double *param);

int sws_scale(struct SwsContext *ctx, uint8_t* src[], int srcStride[],
              int srcSliceY, int srcSliceH, uint8_t* dst[], int dstStride[]);

void sws_freeContext(struct SwsContext *swsContext);

static inline void sws_global_init(void *(*alloc)(unsigned int size),
                     void (*free)(void *ptr),
                     void (*log)(void*, int level, const char *fmt, ...))
{
}

#endif /* SWSCALE_EMU_H */
