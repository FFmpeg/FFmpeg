/*
 * A52 decoder using liba52
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file a52dec.c
 * A52 decoder using liba52
 */

#include "avcodec.h"
#include <a52dec/a52.h>

#ifdef CONFIG_LIBA52BIN
#include <dlfcn.h>
static const char* liba52name = "liba52.so.0";
#endif

/**
 * liba52 - Copyright (C) Aaron Holtzman
 * released under the GPL license.
 */
typedef struct AC3DecodeState {
    int flags;
    int channels;
    a52_state_t* state;
    sample_t* samples;

    /*
     * virtual method table
     *
     * using this function table so the liba52 doesn't
     * have to be really linked together with ffmpeg
     * and might be linked in runtime - this allows binary
     * distribution of ffmpeg library which doens't depend
     * on liba52 library - but if user has it installed
     * it will be used - user might install such library
     * separately
     */
    void* handle;
    a52_state_t* (*a52_init)(uint32_t mm_accel);
    sample_t* (*a52_samples)(a52_state_t * state);
    int (*a52_syncinfo)(uint8_t * buf, int * flags,
                          int * sample_rate, int * bit_rate);
    int (*a52_frame)(a52_state_t * state, uint8_t * buf, int * flags,
                       sample_t * level, sample_t bias);
    void (*a52_dynrng)(a52_state_t * state,
                         sample_t (* call) (sample_t, void *), void * data);
    int (*a52_block)(a52_state_t * state);
    void (*a52_free)(a52_state_t * state);

} AC3DecodeState;

#ifdef CONFIG_LIBA52BIN
static void* dlsymm(void* handle, const char* symbol)
{
    void* f = dlsym(handle, symbol);
    if (!f)
        av_log( NULL, AV_LOG_ERROR, "A52 Decoder - function '%s' can't be resolved\n", symbol);
    return f;
}
#endif

static av_cold int a52_decode_init(AVCodecContext *avctx)
{
    AC3DecodeState *s = avctx->priv_data;

#ifdef CONFIG_LIBA52BIN
    s->handle = dlopen(liba52name, RTLD_LAZY);
    if (!s->handle)
    {
        av_log( avctx, AV_LOG_ERROR, "A52 library %s could not be opened! \n%s\n", liba52name, dlerror());
        return -1;
    }
    s->a52_init = (a52_state_t* (*)(uint32_t)) dlsymm(s->handle, "a52_init");
    s->a52_samples = (sample_t* (*)(a52_state_t*)) dlsymm(s->handle, "a52_samples");
    s->a52_syncinfo = (int (*)(uint8_t*, int*, int*, int*)) dlsymm(s->handle, "a52_syncinfo");
    s->a52_frame = (int (*)(a52_state_t*, uint8_t*, int*, sample_t*, sample_t)) dlsymm(s->handle, "a52_frame");
    s->a52_block = (int (*)(a52_state_t*)) dlsymm(s->handle, "a52_block");
    s->a52_free = (void (*)(a52_state_t*)) dlsymm(s->handle, "a52_free");
    if (!s->a52_init || !s->a52_samples || !s->a52_syncinfo
        || !s->a52_frame || !s->a52_block || !s->a52_free)
    {
        dlclose(s->handle);
        return -1;
    }
#else
    s->handle = 0;
    s->a52_init = a52_init;
    s->a52_samples = a52_samples;
    s->a52_syncinfo = a52_syncinfo;
    s->a52_frame = a52_frame;
    s->a52_block = a52_block;
    s->a52_free = a52_free;
#endif
    s->state = s->a52_init(0); /* later use CPU flags */
    s->samples = s->a52_samples(s->state);

    /* allow downmixing to stereo or mono */
    if (avctx->channels > 0 && avctx->request_channels > 0 &&
            avctx->request_channels < avctx->channels &&
            avctx->request_channels <= 2) {
        avctx->channels = avctx->request_channels;
    }

    return 0;
}

/**** the following function comes from a52dec */
static inline void float_to_int (float * _f, int16_t * s16, int nchannels)
{
    int i, j, c;
    int32_t * f = (int32_t *) _f;       // XXX assumes IEEE float format

    j = 0;
    nchannels *= 256;
    for (i = 0; i < 256; i++) {
        for (c = 0; c < nchannels; c += 256)
            s16[j++] = av_clip_int16(f[i + c] - 0x43c00000);
    }
}

/**** end */

#define HEADER_SIZE 7

static int a52_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            uint8_t *buf, int buf_size)
{
    AC3DecodeState *s = avctx->priv_data;
    int flags, i, len;
    int sample_rate, bit_rate;
    short *out_samples = data;
    float level;
    static const int ac3_channels[8] = {
        2, 1, 2, 3, 3, 4, 4, 5
    };

    *data_size= 0;

    if (buf_size < HEADER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame, not enough bytes for header\n");
        return -1;
    }
    len = s->a52_syncinfo(buf, &s->flags, &sample_rate, &bit_rate);
    if (len == 0) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame, no sync byte at begin\n");
        return -1;
    }
    if (buf_size < len) {
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame, not enough bytes\n");
        return -1;
    }
    /* update codec info */
    avctx->sample_rate = sample_rate;
    s->channels = ac3_channels[s->flags & 7];
    if (s->flags & A52_LFE)
            s->channels++;
    if (avctx->request_channels > 0 &&
        avctx->request_channels <= 2 &&
        avctx->request_channels < s->channels) {
        avctx->channels = avctx->request_channels;
    } else {
        avctx->channels = s->channels;
    }
    avctx->bit_rate = bit_rate;
    flags = s->flags;
    if (avctx->channels == 1)
        flags = A52_MONO;
    else if (avctx->channels == 2)
        flags = A52_STEREO;
    else
        flags |= A52_ADJUST_LEVEL;
    level = 1;
    if (s->a52_frame(s->state, buf, &flags, &level, 384)) {
    fail:
        av_log(avctx, AV_LOG_ERROR, "Error decoding frame\n");
        return -1;
    }
    for (i = 0; i < 6; i++) {
        if (s->a52_block(s->state))
            goto fail;
        float_to_int(s->samples, out_samples + i * 256 * avctx->channels, avctx->channels);
    }
    *data_size = 6 * avctx->channels * 256 * sizeof(int16_t);
    return len;
}

static av_cold int a52_decode_end(AVCodecContext *avctx)
{
    AC3DecodeState *s = avctx->priv_data;
    s->a52_free(s->state);
#ifdef CONFIG_LIBA52BIN
    dlclose(s->handle);
#endif
    return 0;
}

AVCodec liba52_decoder = {
    "liba52",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AC3,
    sizeof(AC3DecodeState),
    a52_decode_init,
    NULL,
    a52_decode_end,
    a52_decode_frame,
};
