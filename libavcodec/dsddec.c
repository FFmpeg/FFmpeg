/*
 * Direct Stream Digital (DSD) decoder
 * based on BSD licensed dsd2pcm by Sebastian Gesemann
 * Copyright (c) 2009, 2011 Sebastian Gesemann. All rights reserved.
 * Copyright (c) 2014 Peter Ross
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
 * @file
 * Direct Stream Digital (DSD) decoder
 */

#include "libavcodec/internal.h"
#include "libavcodec/mathops.h"
#include "avcodec.h"
#include "dsd_tablegen.h"

#define FIFOSIZE 16              /** must be a power of two */
#define FIFOMASK (FIFOSIZE - 1)  /** bit mask for FIFO offsets */

#if FIFOSIZE * 8 < HTAPS * 2
#error "FIFOSIZE too small"
#endif

/**
 * Per-channel buffer
 */
typedef struct {
    unsigned char buf[FIFOSIZE];
    unsigned pos;
} DSDContext;

static void dsd2pcm_translate(DSDContext* s, size_t samples, int lsbf,
                              const unsigned char *src, ptrdiff_t src_stride,
                              float *dst, ptrdiff_t dst_stride)
{
    unsigned pos, i;
    unsigned char* p;
    double sum;

    pos = s->pos;

    while (samples-- > 0) {
        s->buf[pos] = lsbf ? ff_reverse[*src] : *src;
        src += src_stride;

        p = s->buf + ((pos - CTABLES) & FIFOMASK);
        *p = ff_reverse[*p];

        sum = 0.0;
        for (i = 0; i < CTABLES; i++) {
            unsigned char a = s->buf[(pos                   - i) & FIFOMASK];
            unsigned char b = s->buf[(pos - (CTABLES*2 - 1) + i) & FIFOMASK];
            sum += ctables[i][a] + ctables[i][b];
        }

        *dst = (float)sum;
        dst += dst_stride;

        pos = (pos + 1) & FIFOMASK;
    }

    s->pos = pos;
}

static av_cold void init_static_data(void)
{
    static int done = 0;
    if (done)
        return;
    dsd_ctables_tableinit();
    done = 1;
}

static av_cold int decode_init(AVCodecContext *avctx)
{
    DSDContext * s;
    int i;

    init_static_data();

    s = av_malloc_array(sizeof(DSDContext), avctx->channels);
    if (!s)
        return AVERROR(ENOMEM);

    for (i = 0; i < avctx->channels; i++) {
        s[i].pos = 0;
        memset(s[i].buf, 0x69, sizeof(s[i].buf));

        /* 0x69 = 01101001
         * This pattern "on repeat" makes a low energy 352.8 kHz tone
         * and a high energy 1.0584 MHz tone which should be filtered
         * out completely by any playback system --> silence
         */
    }

    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    avctx->priv_data  = s;
    return 0;
}

static int decode_frame(AVCodecContext *avctx, void *data,
                        int *got_frame_ptr, AVPacket *avpkt)
{
    DSDContext * s = avctx->priv_data;
    AVFrame *frame = data;
    int ret, i;
    int lsbf = avctx->codec_id == AV_CODEC_ID_DSD_LSBF || avctx->codec_id == AV_CODEC_ID_DSD_LSBF_PLANAR;
    int src_next;
    int src_stride;

    frame->nb_samples = avpkt->size / avctx->channels;

    if (avctx->codec_id == AV_CODEC_ID_DSD_LSBF_PLANAR || avctx->codec_id == AV_CODEC_ID_DSD_MSBF_PLANAR) {
        src_next   = frame->nb_samples;
        src_stride = 1;
    } else {
        src_next   = 1;
        src_stride = avctx->channels;
    }

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    for (i = 0; i < avctx->channels; i++) {
        float * dst = ((float **)frame->extended_data)[i];
        dsd2pcm_translate(&s[i], frame->nb_samples, lsbf,
            avpkt->data + i * src_next, src_stride,
            dst, 1);
    }

    *got_frame_ptr = 1;
    return frame->nb_samples * avctx->channels;
}

#define DSD_DECODER(id_, name_, long_name_) \
AVCodec ff_##name_##_decoder = { \
    .name         = #name_, \
    .long_name    = NULL_IF_CONFIG_SMALL(long_name_), \
    .type         = AVMEDIA_TYPE_AUDIO, \
    .id           = AV_CODEC_ID_##id_, \
    .init         = decode_init, \
    .decode       = decode_frame, \
    .sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_FLTP, \
                                                   AV_SAMPLE_FMT_NONE }, \
};

DSD_DECODER(DSD_LSBF, dsd_lsbf, "DSD (Direct Stream Digital), least significant bit first")
DSD_DECODER(DSD_MSBF, dsd_msbf, "DSD (Direct Stream Digital), most significant bit first")
DSD_DECODER(DSD_MSBF_PLANAR, dsd_msbf_planar, "DSD (Direct Stream Digital), most significant bit first, planar")
DSD_DECODER(DSD_LSBF_PLANAR, dsd_lsbf_planar, "DSD (Direct Stream Digital), least significant bit first, planar")
