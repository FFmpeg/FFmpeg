#ifndef RTJPEG_H
#define RTJPEG_H

typedef struct {
    int w, h;
    DSPContext *dsp;
    DCTELEM block[64];
    uint8_t scan[64];
    uint32_t lquant[64];
    uint32_t cquant[64];
} RTJpegContext;

void rtjpeg_decode_init(RTJpegContext *c, DSPContext *dsp,
                        int width, int height,
                        uint32_t *lquant, uint32_t *cquant);

int rtjpeg_decode_frame_yuv420(RTJpegContext *c, AVFrame *f,
                               uint8_t *buf, int buf_size);
#endif
