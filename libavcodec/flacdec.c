/*
 * FLAC (Free Lossless Audio Codec) decoder
 * Copyright (c) 2003 Alex Beregszaszi
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
 * FLAC (Free Lossless Audio Codec) decoder
 * @author Alex Beregszaszi
 * @see http://flac.sourceforge.net/
 *
 * This decoder can be used in 1 of 2 ways: Either raw FLAC data can be fed
 * through, starting from the initial 'fLaC' signature; or by passing the
 * 34-byte streaminfo structure through avctx->extradata[_size] followed
 * by data starting with the 0xFFF8 marker.
 */

#include <limits.h>

#include "libavutil/avassert.h"
#include "libavutil/crc.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "bytestream.h"
#include "golomb.h"
#include "flac.h"
#include "flacdata.h"
#include "flacdsp.h"
#include "thread.h"
#include "unary.h"


typedef struct FLACContext {
    AVClass *class;
    struct FLACStreaminfo flac_stream_info;

    AVCodecContext *avctx;                  ///< parent AVCodecContext
    GetBitContext gb;                       ///< GetBitContext initialized to start at the current frame

    int blocksize;                          ///< number of samples in the current frame
    int sample_shift;                       ///< shift required to make output samples 16-bit or 32-bit
    int ch_mode;                            ///< channel decorrelation type in the current frame
    int got_streaminfo;                     ///< indicates if the STREAMINFO has been read

    int32_t *decoded[FLAC_MAX_CHANNELS];    ///< decoded samples
    uint8_t *decoded_buffer;
    unsigned int decoded_buffer_size;
    int buggy_lpc;                          ///< use workaround for old lavc encoded files

    FLACDSPContext dsp;
} FLACContext;

static int allocate_buffers(FLACContext *s);

static void flac_set_bps(FLACContext *s)
{
    enum AVSampleFormat req = s->avctx->request_sample_fmt;
    int need32 = s->flac_stream_info.bps > 16;
    int want32 = av_get_bytes_per_sample(req) > 2;
    int planar = av_sample_fmt_is_planar(req);

    if (need32 || want32) {
        if (planar)
            s->avctx->sample_fmt = AV_SAMPLE_FMT_S32P;
        else
            s->avctx->sample_fmt = AV_SAMPLE_FMT_S32;
        s->sample_shift = 32 - s->flac_stream_info.bps;
    } else {
        if (planar)
            s->avctx->sample_fmt = AV_SAMPLE_FMT_S16P;
        else
            s->avctx->sample_fmt = AV_SAMPLE_FMT_S16;
        s->sample_shift = 16 - s->flac_stream_info.bps;
    }
}

static av_cold int flac_decode_init(AVCodecContext *avctx)
{
    enum FLACExtradataFormat format;
    uint8_t *streaminfo;
    int ret;
    FLACContext *s = avctx->priv_data;
    s->avctx = avctx;

    /* for now, the raw FLAC header is allowed to be passed to the decoder as
       frame data instead of extradata. */
    if (!avctx->extradata)
        return 0;

    if (!ff_flac_is_extradata_valid(avctx, &format, &streaminfo))
        return AVERROR_INVALIDDATA;

    /* initialize based on the demuxer-supplied streamdata header */
    ret = ff_flac_parse_streaminfo(avctx, &s->flac_stream_info, streaminfo);
    if (ret < 0)
        return ret;
    ret = allocate_buffers(s);
    if (ret < 0)
        return ret;
    flac_set_bps(s);
    ff_flacdsp_init(&s->dsp, avctx->sample_fmt,
                    s->flac_stream_info.channels, s->flac_stream_info.bps);
    s->got_streaminfo = 1;

    return 0;
}

static void dump_headers(AVCodecContext *avctx, FLACStreaminfo *s)
{
    av_log(avctx, AV_LOG_DEBUG, "  Max Blocksize: %d\n", s->max_blocksize);
    av_log(avctx, AV_LOG_DEBUG, "  Max Framesize: %d\n", s->max_framesize);
    av_log(avctx, AV_LOG_DEBUG, "  Samplerate: %d\n", s->samplerate);
    av_log(avctx, AV_LOG_DEBUG, "  Channels: %d\n", s->channels);
    av_log(avctx, AV_LOG_DEBUG, "  Bits: %d\n", s->bps);
}

static int allocate_buffers(FLACContext *s)
{
    int buf_size;
    int ret;

    av_assert0(s->flac_stream_info.max_blocksize);

    buf_size = av_samples_get_buffer_size(NULL, s->flac_stream_info.channels,
                                          s->flac_stream_info.max_blocksize,
                                          AV_SAMPLE_FMT_S32P, 0);
    if (buf_size < 0)
        return buf_size;

    av_fast_malloc(&s->decoded_buffer, &s->decoded_buffer_size, buf_size);
    if (!s->decoded_buffer)
        return AVERROR(ENOMEM);

    ret = av_samples_fill_arrays((uint8_t **)s->decoded, NULL,
                                 s->decoded_buffer,
                                 s->flac_stream_info.channels,
                                 s->flac_stream_info.max_blocksize,
                                 AV_SAMPLE_FMT_S32P, 0);
    return ret < 0 ? ret : 0;
}

/**
 * Parse the STREAMINFO from an inline header.
 * @param s the flac decoding context
 * @param buf input buffer, starting with the "fLaC" marker
 * @param buf_size buffer size
 * @return non-zero if metadata is invalid
 */
static int parse_streaminfo(FLACContext *s, const uint8_t *buf, int buf_size)
{
    int metadata_type, metadata_size, ret;

    if (buf_size < FLAC_STREAMINFO_SIZE+8) {
        /* need more data */
        return 0;
    }
    flac_parse_block_header(&buf[4], NULL, &metadata_type, &metadata_size);
    if (metadata_type != FLAC_METADATA_TYPE_STREAMINFO ||
        metadata_size != FLAC_STREAMINFO_SIZE) {
        return AVERROR_INVALIDDATA;
    }
    ret = ff_flac_parse_streaminfo(s->avctx, &s->flac_stream_info, &buf[8]);
    if (ret < 0)
        return ret;
    ret = allocate_buffers(s);
    if (ret < 0)
        return ret;
    flac_set_bps(s);
    ff_flacdsp_init(&s->dsp, s->avctx->sample_fmt,
                    s->flac_stream_info.channels, s->flac_stream_info.bps);
    s->got_streaminfo = 1;

    return 0;
}

/**
 * Determine the size of an inline header.
 * @param buf input buffer, starting with the "fLaC" marker
 * @param buf_size buffer size
 * @return number of bytes in the header, or 0 if more data is needed
 */
static int get_metadata_size(const uint8_t *buf, int buf_size)
{
    int metadata_last, metadata_size;
    const uint8_t *buf_end = buf + buf_size;

    buf += 4;
    do {
        if (buf_end - buf < 4)
            return AVERROR_INVALIDDATA;
        flac_parse_block_header(buf, &metadata_last, NULL, &metadata_size);
        buf += 4;
        if (buf_end - buf < metadata_size) {
            /* need more data in order to read the complete header */
            return AVERROR_INVALIDDATA;
        }
        buf += metadata_size;
    } while (!metadata_last);

    return buf_size - (buf_end - buf);
}

static int decode_residuals(FLACContext *s, int32_t *decoded, int pred_order)
{
    GetBitContext gb = s->gb;
    int i, tmp, partition, method_type, rice_order;
    int rice_bits, rice_esc;
    int samples;

    method_type = get_bits(&gb, 2);
    rice_order  = get_bits(&gb, 4);

    samples   = s->blocksize >> rice_order;
    rice_bits = 4 + method_type;
    rice_esc  = (1 << rice_bits) - 1;

    decoded += pred_order;
    i        = pred_order;

    if (method_type > 1) {
        av_log(s->avctx, AV_LOG_ERROR, "illegal residual coding method %d\n",
               method_type);
        return AVERROR_INVALIDDATA;
    }

    if (samples << rice_order != s->blocksize) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid rice order: %i blocksize %i\n",
               rice_order, s->blocksize);
        return AVERROR_INVALIDDATA;
    }

    if (pred_order > samples) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid predictor order: %i > %i\n",
               pred_order, samples);
        return AVERROR_INVALIDDATA;
    }

    for (partition = 0; partition < (1 << rice_order); partition++) {
        tmp = get_bits(&gb, rice_bits);
        if (tmp == rice_esc) {
            tmp = get_bits(&gb, 5);
            for (; i < samples; i++)
                *decoded++ = get_sbits_long(&gb, tmp);
        } else {
            int real_limit = tmp ? (INT_MAX >> tmp) + 2 : INT_MAX;
            for (; i < samples; i++) {
                int v = get_sr_golomb_flac(&gb, tmp, real_limit, 0);
                if (v == 0x80000000){
                    av_log(s->avctx, AV_LOG_ERROR, "invalid residual\n");
                    return AVERROR_INVALIDDATA;
                }

                *decoded++ = v;
            }
        }
        i= 0;
    }

    s->gb = gb;

    return 0;
}

static int decode_subframe_fixed(FLACContext *s, int32_t *decoded,
                                 int pred_order, int bps)
{
    const int blocksize = s->blocksize;
    unsigned av_uninit(a), av_uninit(b), av_uninit(c), av_uninit(d);
    int i;
    int ret;

    /* warm up samples */
    for (i = 0; i < pred_order; i++) {
        decoded[i] = get_sbits_long(&s->gb, bps);
    }

    if ((ret = decode_residuals(s, decoded, pred_order)) < 0)
        return ret;

    if (pred_order > 0)
        a = decoded[pred_order-1];
    if (pred_order > 1)
        b = a - decoded[pred_order-2];
    if (pred_order > 2)
        c = b - decoded[pred_order-2] + decoded[pred_order-3];
    if (pred_order > 3)
        d = c - decoded[pred_order-2] + 2U*decoded[pred_order-3] - decoded[pred_order-4];

    switch (pred_order) {
    case 0:
        break;
    case 1:
        for (i = pred_order; i < blocksize; i++)
            decoded[i] = a += decoded[i];
        break;
    case 2:
        for (i = pred_order; i < blocksize; i++)
            decoded[i] = a += b += decoded[i];
        break;
    case 3:
        for (i = pred_order; i < blocksize; i++)
            decoded[i] = a += b += c += decoded[i];
        break;
    case 4:
        for (i = pred_order; i < blocksize; i++)
            decoded[i] = a += b += c += d += decoded[i];
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "illegal pred order %d\n", pred_order);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static void lpc_analyze_remodulate(SUINT32 *decoded, const int coeffs[32],
                                   int order, int qlevel, int len, int bps)
{
    int i, j;
    int ebps = 1 << (bps-1);
    unsigned sigma = 0;

    for (i = order; i < len; i++)
        sigma |= decoded[i] + ebps;

    if (sigma < 2*ebps)
        return;

    for (i = len - 1; i >= order; i--) {
        int64_t p = 0;
        for (j = 0; j < order; j++)
            p += coeffs[j] * (int64_t)(int32_t)decoded[i-order+j];
        decoded[i] -= p >> qlevel;
    }
    for (i = order; i < len; i++, decoded++) {
        int32_t p = 0;
        for (j = 0; j < order; j++)
            p += coeffs[j] * (uint32_t)decoded[j];
        decoded[j] += p >> qlevel;
    }
}

static int decode_subframe_lpc(FLACContext *s, int32_t *decoded, int pred_order,
                               int bps)
{
    int i, ret;
    int coeff_prec, qlevel;
    int coeffs[32];

    /* warm up samples */
    for (i = 0; i < pred_order; i++) {
        decoded[i] = get_sbits_long(&s->gb, bps);
    }

    coeff_prec = get_bits(&s->gb, 4) + 1;
    if (coeff_prec == 16) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid coeff precision\n");
        return AVERROR_INVALIDDATA;
    }
    qlevel = get_sbits(&s->gb, 5);
    if (qlevel < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "qlevel %d not supported, maybe buggy stream\n",
               qlevel);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < pred_order; i++) {
        coeffs[pred_order - i - 1] = get_sbits(&s->gb, coeff_prec);
    }

    if ((ret = decode_residuals(s, decoded, pred_order)) < 0)
        return ret;

    if (   (    s->buggy_lpc && s->flac_stream_info.bps <= 16)
        || (   !s->buggy_lpc && bps <= 16
            && bps + coeff_prec + av_log2(pred_order) <= 32)) {
        s->dsp.lpc16(decoded, coeffs, pred_order, qlevel, s->blocksize);
    } else {
        s->dsp.lpc32(decoded, coeffs, pred_order, qlevel, s->blocksize);
        if (s->flac_stream_info.bps <= 16)
            lpc_analyze_remodulate(decoded, coeffs, pred_order, qlevel, s->blocksize, bps);
    }

    return 0;
}

static inline int decode_subframe(FLACContext *s, int channel)
{
    int32_t *decoded = s->decoded[channel];
    int type, wasted = 0;
    int bps = s->flac_stream_info.bps;
    int i, tmp, ret;

    if (channel == 0) {
        if (s->ch_mode == FLAC_CHMODE_RIGHT_SIDE)
            bps++;
    } else {
        if (s->ch_mode == FLAC_CHMODE_LEFT_SIDE || s->ch_mode == FLAC_CHMODE_MID_SIDE)
            bps++;
    }

    if (get_bits1(&s->gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid subframe padding\n");
        return AVERROR_INVALIDDATA;
    }
    type = get_bits(&s->gb, 6);

    if (get_bits1(&s->gb)) {
        int left = get_bits_left(&s->gb);
        if ( left <= 0 ||
            (left < bps && !show_bits_long(&s->gb, left)) ||
                           !show_bits_long(&s->gb, bps)) {
            av_log(s->avctx, AV_LOG_ERROR,
                   "Invalid number of wasted bits > available bits (%d) - left=%d\n",
                   bps, left);
            return AVERROR_INVALIDDATA;
        }
        wasted = 1 + get_unary(&s->gb, 1, get_bits_left(&s->gb));
        bps -= wasted;
    }
    if (bps > 32) {
        avpriv_report_missing_feature(s->avctx, "Decorrelated bit depth > 32");
        return AVERROR_PATCHWELCOME;
    }

//FIXME use av_log2 for types
    if (type == 0) {
        tmp = get_sbits_long(&s->gb, bps);
        for (i = 0; i < s->blocksize; i++)
            decoded[i] = tmp;
    } else if (type == 1) {
        for (i = 0; i < s->blocksize; i++)
            decoded[i] = get_sbits_long(&s->gb, bps);
    } else if ((type >= 8) && (type <= 12)) {
        if ((ret = decode_subframe_fixed(s, decoded, type & ~0x8, bps)) < 0)
            return ret;
    } else if (type >= 32) {
        if ((ret = decode_subframe_lpc(s, decoded, (type & ~0x20)+1, bps)) < 0)
            return ret;
    } else {
        av_log(s->avctx, AV_LOG_ERROR, "invalid coding type\n");
        return AVERROR_INVALIDDATA;
    }

    if (wasted && wasted < 32) {
        int i;
        for (i = 0; i < s->blocksize; i++)
            decoded[i] = (unsigned)decoded[i] << wasted;
    }

    return 0;
}

static int decode_frame(FLACContext *s)
{
    int i, ret;
    GetBitContext *gb = &s->gb;
    FLACFrameInfo fi;

    if ((ret = ff_flac_decode_frame_header(s->avctx, gb, &fi, 0)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid frame header\n");
        return ret;
    }

    if (   s->flac_stream_info.channels
        && fi.channels != s->flac_stream_info.channels
        && s->got_streaminfo) {
        s->flac_stream_info.channels = s->avctx->channels = fi.channels;
        ff_flac_set_channel_layout(s->avctx);
        ret = allocate_buffers(s);
        if (ret < 0)
            return ret;
    }
    s->flac_stream_info.channels = s->avctx->channels = fi.channels;
    if (!s->avctx->channel_layout)
        ff_flac_set_channel_layout(s->avctx);
    s->ch_mode = fi.ch_mode;

    if (!s->flac_stream_info.bps && !fi.bps) {
        av_log(s->avctx, AV_LOG_ERROR, "bps not found in STREAMINFO or frame header\n");
        return AVERROR_INVALIDDATA;
    }
    if (!fi.bps) {
        fi.bps = s->flac_stream_info.bps;
    } else if (s->flac_stream_info.bps && fi.bps != s->flac_stream_info.bps) {
        av_log(s->avctx, AV_LOG_ERROR, "switching bps mid-stream is not "
                                       "supported\n");
        return AVERROR_INVALIDDATA;
    }

    if (!s->flac_stream_info.bps) {
        s->flac_stream_info.bps = s->avctx->bits_per_raw_sample = fi.bps;
        flac_set_bps(s);
    }

    if (!s->flac_stream_info.max_blocksize)
        s->flac_stream_info.max_blocksize = FLAC_MAX_BLOCKSIZE;
    if (fi.blocksize > s->flac_stream_info.max_blocksize) {
        av_log(s->avctx, AV_LOG_ERROR, "blocksize %d > %d\n", fi.blocksize,
               s->flac_stream_info.max_blocksize);
        return AVERROR_INVALIDDATA;
    }
    s->blocksize = fi.blocksize;

    if (!s->flac_stream_info.samplerate && !fi.samplerate) {
        av_log(s->avctx, AV_LOG_ERROR, "sample rate not found in STREAMINFO"
                                        " or frame header\n");
        return AVERROR_INVALIDDATA;
    }
    if (fi.samplerate == 0)
        fi.samplerate = s->flac_stream_info.samplerate;
    s->flac_stream_info.samplerate = s->avctx->sample_rate = fi.samplerate;

    if (!s->got_streaminfo) {
        ret = allocate_buffers(s);
        if (ret < 0)
            return ret;
        s->got_streaminfo = 1;
        dump_headers(s->avctx, &s->flac_stream_info);
    }
    ff_flacdsp_init(&s->dsp, s->avctx->sample_fmt,
                    s->flac_stream_info.channels, s->flac_stream_info.bps);

//    dump_headers(s->avctx, &s->flac_stream_info);

    /* subframes */
    for (i = 0; i < s->flac_stream_info.channels; i++) {
        if ((ret = decode_subframe(s, i)) < 0)
            return ret;
    }

    align_get_bits(gb);

    /* frame footer */
    skip_bits(gb, 16); /* data crc */

    return 0;
}

static int flac_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_frame_ptr, AVPacket *avpkt)
{
    AVFrame *frame     = data;
    ThreadFrame tframe = { .f = data };
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    FLACContext *s = avctx->priv_data;
    int bytes_read = 0;
    int ret;

    *got_frame_ptr = 0;

    if (s->flac_stream_info.max_framesize == 0) {
        s->flac_stream_info.max_framesize =
            ff_flac_get_max_frame_size(s->flac_stream_info.max_blocksize ? s->flac_stream_info.max_blocksize : FLAC_MAX_BLOCKSIZE,
                                       FLAC_MAX_CHANNELS, 32);
    }

    if (buf_size > 5 && !memcmp(buf, "\177FLAC", 5)) {
        av_log(s->avctx, AV_LOG_DEBUG, "skipping flac header packet 1\n");
        return buf_size;
    }

    if (buf_size > 0 && (*buf & 0x7F) == FLAC_METADATA_TYPE_VORBIS_COMMENT) {
        av_log(s->avctx, AV_LOG_DEBUG, "skipping vorbis comment\n");
        return buf_size;
    }

    /* check that there is at least the smallest decodable amount of data.
       this amount corresponds to the smallest valid FLAC frame possible.
       FF F8 69 02 00 00 9A 00 00 34 46 */
    if (buf_size < FLAC_MIN_FRAME_SIZE)
        return buf_size;

    /* check for inline header */
    if (AV_RB32(buf) == MKBETAG('f','L','a','C')) {
        if (!s->got_streaminfo && (ret = parse_streaminfo(s, buf, buf_size))) {
            av_log(s->avctx, AV_LOG_ERROR, "invalid header\n");
            return ret;
        }
        return get_metadata_size(buf, buf_size);
    }

    /* decode frame */
    if ((ret = init_get_bits8(&s->gb, buf, buf_size)) < 0)
        return ret;
    if ((ret = decode_frame(s)) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "decode_frame() failed\n");
        return ret;
    }
    bytes_read = get_bits_count(&s->gb)/8;

    if ((s->avctx->err_recognition & (AV_EF_CRCCHECK|AV_EF_COMPLIANT)) &&
        av_crc(av_crc_get_table(AV_CRC_16_ANSI),
               0, buf, bytes_read)) {
        av_log(s->avctx, AV_LOG_ERROR, "CRC error at PTS %"PRId64"\n", avpkt->pts);
        if (s->avctx->err_recognition & AV_EF_EXPLODE)
            return AVERROR_INVALIDDATA;
    }

    /* get output buffer */
    frame->nb_samples = s->blocksize;
    if ((ret = ff_thread_get_buffer(avctx, &tframe, 0)) < 0)
        return ret;

    s->dsp.decorrelate[s->ch_mode](frame->data, s->decoded,
                                   s->flac_stream_info.channels,
                                   s->blocksize, s->sample_shift);

    if (bytes_read > buf_size) {
        av_log(s->avctx, AV_LOG_ERROR, "overread: %d\n", bytes_read - buf_size);
        return AVERROR_INVALIDDATA;
    }
    if (bytes_read < buf_size) {
        av_log(s->avctx, AV_LOG_DEBUG, "underread: %d orig size: %d\n",
               buf_size - bytes_read, buf_size);
    }

    *got_frame_ptr = 1;

    return bytes_read;
}

static av_cold int flac_decode_close(AVCodecContext *avctx)
{
    FLACContext *s = avctx->priv_data;

    av_freep(&s->decoded_buffer);

    return 0;
}

static const AVOption options[] = {
{ "use_buggy_lpc", "emulate old buggy lavc behavior", offsetof(FLACContext, buggy_lpc), AV_OPT_TYPE_BOOL, {.i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM },
{ NULL },
};

static const AVClass flac_decoder_class = {
    "FLAC decoder",
    av_default_item_name,
    options,
    LIBAVUTIL_VERSION_INT,
};

AVCodec ff_flac_decoder = {
    .name           = "flac",
    .long_name      = NULL_IF_CONFIG_SMALL("FLAC (Free Lossless Audio Codec)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_FLAC,
    .priv_data_size = sizeof(FLACContext),
    .init           = flac_decode_init,
    .close          = flac_decode_close,
    .decode         = flac_decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_FRAME_THREADS,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S32,
                                                      AV_SAMPLE_FMT_S32P,
                                                      AV_SAMPLE_FMT_NONE },
    .priv_class     = &flac_decoder_class,
};
