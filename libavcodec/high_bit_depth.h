#include "dsputil.h"

#ifndef BIT_DEPTH
#define BIT_DEPTH 8
#endif

#ifdef AVCODEC_H264_HIGH_DEPTH_H
#   undef pixel
#   undef pixel2
#   undef pixel4
#   undef dctcoef
#   undef INIT_CLIP
#   undef no_rnd_avg_pixel4
#   undef rnd_avg_pixel4
#   undef AV_RN2P
#   undef AV_RN4P
#   undef AV_RN4PA
#   undef AV_WN2P
#   undef AV_WN4P
#   undef AV_WN4PA
#   undef CLIP
#   undef FUNC
#   undef FUNCC
#   undef av_clip_pixel
#   undef PIXEL_SPLAT_X4
#else
#   define AVCODEC_H264_HIGH_DEPTH_H
#   define CLIP_PIXEL(depth)\
    static inline uint16_t av_clip_pixel_ ## depth (int p)\
    {\
        const int pixel_max = (1 << depth)-1;\
        return (p & ~pixel_max) ? (-p)>>31 & pixel_max : p;\
    }

CLIP_PIXEL( 9)
CLIP_PIXEL(10)
#endif

#if BIT_DEPTH > 8
#   define pixel  uint16_t
#   define pixel2 uint32_t
#   define pixel4 uint64_t
#   define dctcoef int32_t

#   define INIT_CLIP
#   define no_rnd_avg_pixel4 no_rnd_avg64
#   define    rnd_avg_pixel4    rnd_avg64
#   define AV_RN2P  AV_RN32
#   define AV_RN4P  AV_RN64
#   define AV_RN4PA AV_RN64A
#   define AV_WN2P  AV_WN32
#   define AV_WN4P  AV_WN64
#   define AV_WN4PA AV_WN64A
#   define PIXEL_SPLAT_X4(x) ((x)*0x0001000100010001ULL)
#else
#   define pixel  uint8_t
#   define pixel2 uint16_t
#   define pixel4 uint32_t
#   define dctcoef int16_t

#   define INIT_CLIP uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
#   define no_rnd_avg_pixel4 no_rnd_avg32
#   define    rnd_avg_pixel4    rnd_avg32
#   define AV_RN2P  AV_RN16
#   define AV_RN4P  AV_RN32
#   define AV_RN4PA AV_RN32A
#   define AV_WN2P  AV_WN16
#   define AV_WN4P  AV_WN32
#   define AV_WN4PA AV_WN32A
#   define PIXEL_SPLAT_X4(x) ((x)*0x01010101U)
#endif

#if BIT_DEPTH == 8
#   define av_clip_pixel(a) av_clip_uint8(a)
#   define CLIP(a) cm[a]
#   define FUNC(a)  a ## _8
#   define FUNCC(a) a ## _8_c
#elif BIT_DEPTH == 9
#   define av_clip_pixel(a) av_clip_pixel_9(a)
#   define CLIP(a)          av_clip_pixel_9(a)
#   define FUNC(a)  a ## _9
#   define FUNCC(a) a ## _9_c
#elif BIT_DEPTH == 10
#   define av_clip_pixel(a) av_clip_pixel_10(a)
#   define CLIP(a)          av_clip_pixel_10(a)
#   define FUNC(a)  a ## _10
#   define FUNCC(a) a ## _10_c
#endif
