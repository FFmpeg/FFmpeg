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
 * @file libavcodec/flacdec.c
 * FLAC (Free Lossless Audio Codec) decoder
 * @author Alex Beregszaszi
 *
 * For more information on the FLAC format, visit:
 *  http://flac.sourceforge.net/
 *
 * This decoder can be used in 1 of 2 ways: Either raw FLAC data can be fed
 * through, starting from the initial 'fLaC' signature; or by passing the
 * 34-byte streaminfo structure through avctx->extradata[_size] followed
 * by data starting with the 0xFFF8 marker.
 */

#include <limits.h>

#include "libavutil/crc.h"
#include "avcodec.h"
#include "internal.h"
#include "bitstream.h"
#include "bytestream.h"
#include "golomb.h"
#include "flac.h"
#include "flacdata.h"

#undef NDEBUG
#include <assert.h>

typedef struct FLACContext {
    FLACSTREAMINFO

    AVCodecContext *avctx;                  ///< parent AVCodecContext
    GetBitContext gb;                       ///< GetBitContext initialized to start at the current frame

    int blocksize;                          ///< number of samples in the current frame
    int curr_bps;                           ///< bps for current subframe, adjusted for channel correlation and wasted bits
    int sample_shift;                       ///< shift required to make output samples 16-bit or 32-bit
    int is32;                               ///< flag to indicate if output should be 32-bit instead of 16-bit
    int ch_mode;                            ///< channel decorrelation type in the current frame
    int got_streaminfo;                     ///< indicates if the STREAMINFO has been read

    int32_t *decoded[FLAC_MAX_CHANNELS];    ///< decoded samples
    uint8_t *bitstream;
    unsigned int bitstream_size;
    unsigned int bitstream_index;
    unsigned int allocated_bitstream_size;
} FLACContext;

static const int sample_size_table[] =
{ 0, 8, 12, 0, 16, 20, 24, 0 };

static int64_t get_utf8(GetBitContext *gb)
{
    int64_t val;
    GET_UTF8(val, get_bits(gb, 8), return -1;)
    return val;
}

static void allocate_buffers(FLACContext *s);

int ff_flac_is_extradata_valid(AVCodecContext *avctx,
                               enum FLACExtradataFormat *format,
                               uint8_t **streaminfo_start)
{
    if (!avctx->extradata || avctx->extradata_size < FLAC_STREAMINFO_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "extradata NULL or too small.\n");
        return 0;
    }
    if (AV_RL32(avctx->extradata) != MKTAG('f','L','a','C')) {
        /* extradata contains STREAMINFO only */
        if (avctx->extradata_size != FLAC_STREAMINFO_SIZE) {
            av_log(avctx, AV_LOG_WARNING, "extradata contains %d bytes too many.\n",
                   FLAC_STREAMINFO_SIZE-avctx->extradata_size);
        }
        *format = FLAC_EXTRADATA_FORMAT_STREAMINFO;
        *streaminfo_start = avctx->extradata;
    } else {
        if (avctx->extradata_size < 8+FLAC_STREAMINFO_SIZE) {
            av_log(avctx, AV_LOG_ERROR, "extradata too small.\n");
            return 0;
        }
        *format = FLAC_EXTRADATA_FORMAT_FULL_HEADER;
        *streaminfo_start = &avctx->extradata[8];
    }
    return 1;
}

static av_cold int flac_decode_init(AVCodecContext *avctx)
{
    enum FLACExtradataFormat format;
    uint8_t *streaminfo;
    FLACContext *s = avctx->priv_data;
    s->avctx = avctx;

    avctx->sample_fmt = SAMPLE_FMT_S16;

    /* for now, the raw FLAC header is allowed to be passed to the decoder as
       frame data instead of extradata. */
    if (!avctx->extradata)
        return 0;

    if (!ff_flac_is_extradata_valid(avctx, &format, &streaminfo))
        return -1;

    /* initialize based on the demuxer-supplied streamdata header */
    ff_flac_parse_streaminfo(avctx, (FLACStreaminfo *)s, streaminfo);
    allocate_buffers(s);
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

static void allocate_buffers(FLACContext *s)
{
    int i;

    assert(s->max_blocksize);

    if (s->max_framesize == 0 && s->max_blocksize) {
        s->max_framesize = ff_flac_get_max_frame_size(s->max_blocksize,
                                                      s->channels, s->bps);
    }

    for (i = 0; i < s->channels; i++) {
        s->decoded[i] = av_realloc(s->decoded[i],
                                   sizeof(int32_t)*s->max_blocksize);
    }

    if (s->allocated_bitstream_size < s->max_framesize)
        s->bitstream= av_fast_realloc(s->bitstream,
                                      &s->allocated_bitstream_size,
                                      s->max_framesize);
}

void ff_flac_parse_streaminfo(AVCodecContext *avctx, struct FLACStreaminfo *s,
                              const uint8_t *buffer)
{
    GetBitContext gb;
    init_get_bits(&gb, buffer, FLAC_STREAMINFO_SIZE*8);

    skip_bits(&gb, 16); /* skip min blocksize */
    s->max_blocksize = get_bits(&gb, 16);
    if (s->max_blocksize < FLAC_MIN_BLOCKSIZE) {
        av_log(avctx, AV_LOG_WARNING, "invalid max blocksize: %d\n",
               s->max_blocksize);
        s->max_blocksize = 16;
    }

    skip_bits(&gb, 24); /* skip min frame size */
    s->max_framesize = get_bits_long(&gb, 24);

    s->samplerate = get_bits_long(&gb, 20);
    s->channels = get_bits(&gb, 3) + 1;
    s->bps = get_bits(&gb, 5) + 1;

    avctx->channels = s->channels;
    avctx->sample_rate = s->samplerate;
    avctx->bits_per_raw_sample = s->bps;
    if (s->bps > 16)
        avctx->sample_fmt = SAMPLE_FMT_S32;
    else
        avctx->sample_fmt = SAMPLE_FMT_S16;

    s->samples  = get_bits_long(&gb, 32) << 4;
    s->samples |= get_bits(&gb, 4);

    skip_bits_long(&gb, 64); /* md5 sum */
    skip_bits_long(&gb, 64); /* md5 sum */

    dump_headers(avctx, s);
}

void ff_flac_parse_block_header(const uint8_t *block_header,
                                int *last, int *type, int *size)
{
    int tmp = bytestream_get_byte(&block_header);
    if (last)
        *last = tmp & 0x80;
    if (type)
        *type = tmp & 0x7F;
    if (size)
        *size = bytestream_get_be24(&block_header);
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
    int metadata_type, metadata_size;

    if (buf_size < FLAC_STREAMINFO_SIZE+8) {
        /* need more data */
        return 0;
    }
    ff_flac_parse_block_header(&buf[4], NULL, &metadata_type, &metadata_size);
    if (metadata_type != FLAC_METADATA_TYPE_STREAMINFO ||
        metadata_size != FLAC_STREAMINFO_SIZE) {
        return AVERROR_INVALIDDATA;
    }
    ff_flac_parse_streaminfo(s->avctx, (FLACStreaminfo *)s, &buf[8]);
    allocate_buffers(s);
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
        ff_flac_parse_block_header(buf, &metadata_last, NULL, &metadata_size);
        buf += 4;
        if (buf + metadata_size > buf_end) {
            /* need more data in order to read the complete header */
            return 0;
        }
        buf += metadata_size;
    } while (!metadata_last);

    return buf_size - (buf_end - buf);
}

static int decode_residuals(FLACContext *s, int channel, int pred_order)
{
    int i, tmp, partition, method_type, rice_order;
    int sample = 0, samples;

    method_type = get_bits(&s->gb, 2);
    if (method_type > 1) {
        av_log(s->avctx, AV_LOG_ERROR, "illegal residual coding method %d\n",
               method_type);
        return -1;
    }

    rice_order = get_bits(&s->gb, 4);

    samples= s->blocksize >> rice_order;
    if (pred_order > samples) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid predictor order: %i > %i\n",
               pred_order, samples);
        return -1;
    }

    sample=
    i= pred_order;
    for (partition = 0; partition < (1 << rice_order); partition++) {
        tmp = get_bits(&s->gb, method_type == 0 ? 4 : 5);
        if (tmp == (method_type == 0 ? 15 : 31)) {
            tmp = get_bits(&s->gb, 5);
            for (; i < samples; i++, sample++)
                s->decoded[channel][sample] = get_sbits_long(&s->gb, tmp);
        } else {
            for (; i < samples; i++, sample++) {
                s->decoded[channel][sample] = get_sr_golomb_flac(&s->gb, tmp, INT_MAX, 0);
            }
        }
        i= 0;
    }

    return 0;
}

static int decode_subframe_fixed(FLACContext *s, int channel, int pred_order)
{
    const int blocksize = s->blocksize;
    int32_t *decoded = s->decoded[channel];
    int av_uninit(a), av_uninit(b), av_uninit(c), av_uninit(d), i;

    /* warm up samples */
    for (i = 0; i < pred_order; i++) {
        decoded[i] = get_sbits_long(&s->gb, s->curr_bps);
    }

    if (decode_residuals(s, channel, pred_order) < 0)
        return -1;

    if (pred_order > 0)
        a = decoded[pred_order-1];
    if (pred_order > 1)
        b = a - decoded[pred_order-2];
    if (pred_order > 2)
        c = b - decoded[pred_order-2] + decoded[pred_order-3];
    if (pred_order > 3)
        d = c - decoded[pred_order-2] + 2*decoded[pred_order-3] - decoded[pred_order-4];

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
        return -1;
    }

    return 0;
}

static int decode_subframe_lpc(FLACContext *s, int channel, int pred_order)
{
    int i, j;
    int coeff_prec, qlevel;
    int coeffs[pred_order];
    int32_t *decoded = s->decoded[channel];

    /* warm up samples */
    for (i = 0; i < pred_order; i++) {
        decoded[i] = get_sbits_long(&s->gb, s->curr_bps);
    }

    coeff_prec = get_bits(&s->gb, 4) + 1;
    if (coeff_prec == 16) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid coeff precision\n");
        return -1;
    }
    qlevel = get_sbits(&s->gb, 5);
    if (qlevel < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "qlevel %d not supported, maybe buggy stream\n",
               qlevel);
        return -1;
    }

    for (i = 0; i < pred_order; i++) {
        coeffs[i] = get_sbits(&s->gb, coeff_prec);
    }

    if (decode_residuals(s, channel, pred_order) < 0)
        return -1;

    if (s->bps > 16) {
        int64_t sum;
        for (i = pred_order; i < s->blocksize; i++) {
            sum = 0;
            for (j = 0; j < pred_order; j++)
                sum += (int64_t)coeffs[j] * decoded[i-j-1];
            decoded[i] += sum >> qlevel;
        }
    } else {
        for (i = pred_order; i < s->blocksize-1; i += 2) {
            int c;
            int d = decoded[i-pred_order];
            int s0 = 0, s1 = 0;
            for (j = pred_order-1; j > 0; j--) {
                c = coeffs[j];
                s0 += c*d;
                d = decoded[i-j];
                s1 += c*d;
            }
            c = coeffs[0];
            s0 += c*d;
            d = decoded[i] += s0 >> qlevel;
            s1 += c*d;
            decoded[i+1] += s1 >> qlevel;
        }
        if (i < s->blocksize) {
            int sum = 0;
            for (j = 0; j < pred_order; j++)
                sum += coeffs[j] * decoded[i-j-1];
            decoded[i] += sum >> qlevel;
        }
    }

    return 0;
}

static inline int decode_subframe(FLACContext *s, int channel)
{
    int type, wasted = 0;
    int i, tmp;

    s->curr_bps = s->bps;
    if (channel == 0) {
        if (s->ch_mode == FLAC_CHMODE_RIGHT_SIDE)
            s->curr_bps++;
    } else {
        if (s->ch_mode == FLAC_CHMODE_LEFT_SIDE || s->ch_mode == FLAC_CHMODE_MID_SIDE)
            s->curr_bps++;
    }

    if (get_bits1(&s->gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid subframe padding\n");
        return -1;
    }
    type = get_bits(&s->gb, 6);

    if (get_bits1(&s->gb)) {
        wasted = 1;
        while (!get_bits1(&s->gb))
            wasted++;
        s->curr_bps -= wasted;
    }
    if (s->curr_bps > 32) {
        ff_log_missing_feature(s->avctx, "decorrelated bit depth > 32", 0);
        return -1;
    }

//FIXME use av_log2 for types
    if (type == 0) {
        tmp = get_sbits_long(&s->gb, s->curr_bps);
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = tmp;
    } else if (type == 1) {
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = get_sbits_long(&s->gb, s->curr_bps);
    } else if ((type >= 8) && (type <= 12)) {
        if (decode_subframe_fixed(s, channel, type & ~0x8) < 0)
            return -1;
    } else if (type >= 32) {
        if (decode_subframe_lpc(s, channel, (type & ~0x20)+1) < 0)
            return -1;
    } else {
        av_log(s->avctx, AV_LOG_ERROR, "invalid coding type\n");
        return -1;
    }

    if (wasted) {
        int i;
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] <<= wasted;
    }

    return 0;
}

/**
 * Validate and decode a frame header.
 * @param      avctx AVCodecContext to use as av_log() context
 * @param      gb    GetBitContext from which to read frame header
 * @param[out] fi    frame information
 * @return non-zero on error, 0 if ok
 */
static int decode_frame_header(AVCodecContext *avctx, GetBitContext *gb,
                               FLACFrameInfo *fi)
{
    int bs_code, sr_code, bps_code;

    /* frame sync code */
    skip_bits(gb, 16);

    /* block size and sample rate codes */
    bs_code = get_bits(gb, 4);
    sr_code = get_bits(gb, 4);

    /* channels and decorrelation */
    fi->ch_mode = get_bits(gb, 4);
    if (fi->ch_mode < FLAC_MAX_CHANNELS) {
        fi->channels = fi->ch_mode + 1;
        fi->ch_mode = FLAC_CHMODE_INDEPENDENT;
    } else if (fi->ch_mode <= FLAC_CHMODE_MID_SIDE) {
        fi->channels = 2;
    } else {
        av_log(avctx, AV_LOG_ERROR, "invalid channel mode: %d\n", fi->ch_mode);
        return -1;
    }

    /* bits per sample */
    bps_code = get_bits(gb, 3);
    if (bps_code == 3 || bps_code == 7) {
        av_log(avctx, AV_LOG_ERROR, "invalid sample size code (%d)\n",
               bps_code);
        return -1;
    }
    fi->bps = sample_size_table[bps_code];

    /* reserved bit */
    if (get_bits1(gb)) {
        av_log(avctx, AV_LOG_ERROR, "broken stream, invalid padding\n");
        return -1;
    }

    /* sample or frame count */
    if (get_utf8(gb) < 0) {
        av_log(avctx, AV_LOG_ERROR, "utf8 fscked\n");
        return -1;
    }

    /* blocksize */
    if (bs_code == 0) {
        av_log(avctx, AV_LOG_ERROR, "reserved blocksize code: 0\n");
        return -1;
    } else if (bs_code == 6) {
        fi->blocksize = get_bits(gb, 8) + 1;
    } else if (bs_code == 7) {
        fi->blocksize = get_bits(gb, 16) + 1;
    } else {
        fi->blocksize = ff_flac_blocksize_table[bs_code];
    }

    /* sample rate */
    if (sr_code < 12) {
        fi->samplerate = ff_flac_sample_rate_table[sr_code];
    } else if (sr_code == 12) {
        fi->samplerate = get_bits(gb, 8) * 1000;
    } else if (sr_code == 13) {
        fi->samplerate = get_bits(gb, 16);
    } else if (sr_code == 14) {
        fi->samplerate = get_bits(gb, 16) * 10;
    } else {
        av_log(avctx, AV_LOG_ERROR, "illegal sample rate code %d\n",
               sr_code);
        return -1;
    }

    /* header CRC-8 check */
    skip_bits(gb, 8);
    if (av_crc(av_crc_get_table(AV_CRC_8_ATM), 0, gb->buffer,
               get_bits_count(gb)/8)) {
        av_log(avctx, AV_LOG_ERROR, "header crc mismatch\n");
        return -1;
    }

    return 0;
}

static int decode_frame(FLACContext *s)
{
    int i;
    GetBitContext *gb = &s->gb;
    FLACFrameInfo fi;

    if (decode_frame_header(s->avctx, gb, &fi)) {
        av_log(s->avctx, AV_LOG_ERROR, "invalid frame header\n");
        return -1;
    }

    if (fi.channels != s->channels) {
        av_log(s->avctx, AV_LOG_ERROR, "switching channel layout mid-stream "
                                       "is not supported\n");
        return -1;
    }
    s->ch_mode = fi.ch_mode;

    if (fi.bps && fi.bps != s->bps) {
        av_log(s->avctx, AV_LOG_ERROR, "switching bps mid-stream is not "
                                       "supported\n");
        return -1;
    }
    if (s->bps > 16) {
        s->avctx->sample_fmt = SAMPLE_FMT_S32;
        s->sample_shift = 32 - s->bps;
        s->is32 = 1;
    } else {
        s->avctx->sample_fmt = SAMPLE_FMT_S16;
        s->sample_shift = 16 - s->bps;
        s->is32 = 0;
    }

    if (fi.blocksize > s->max_blocksize) {
        av_log(s->avctx, AV_LOG_ERROR, "blocksize %d > %d\n", fi.blocksize,
               s->max_blocksize);
        return -1;
    }
    s->blocksize = fi.blocksize;

    if (fi.samplerate == 0) {
        fi.samplerate = s->samplerate;
    } else if (fi.samplerate != s->samplerate) {
        av_log(s->avctx, AV_LOG_WARNING, "sample rate changed from %d to %d\n",
               s->samplerate, fi.samplerate);
    }
    s->samplerate = s->avctx->sample_rate = fi.samplerate;

//    dump_headers(s->avctx, (FLACStreaminfo *)s);

    /* subframes */
    for (i = 0; i < s->channels; i++) {
        if (decode_subframe(s, i) < 0)
            return -1;
    }

    align_get_bits(gb);

    /* frame footer */
    skip_bits(gb, 16); /* data crc */

    return 0;
}

static int flac_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    FLACContext *s = avctx->priv_data;
    int i, j = 0, input_buf_size = 0, bytes_read = 0;
    int16_t *samples_16 = data;
    int32_t *samples_32 = data;
    int alloc_data_size= *data_size;
    int output_size;

    *data_size=0;

    if (s->max_framesize == 0) {
        s->max_framesize= FFMAX(4, buf_size); // should hopefully be enough for the first header
        s->bitstream= av_fast_realloc(s->bitstream, &s->allocated_bitstream_size, s->max_framesize);
    }

    if (1 && s->max_framesize) { //FIXME truncated
        if (s->bitstream_size < 4 || AV_RL32(s->bitstream) != MKTAG('f','L','a','C'))
            buf_size= FFMIN(buf_size, s->max_framesize - FFMIN(s->bitstream_size, s->max_framesize));
        input_buf_size= buf_size;

        if (s->bitstream_size + buf_size < buf_size || s->bitstream_index + s->bitstream_size + buf_size < s->bitstream_index)
            return -1;

        if (s->allocated_bitstream_size < s->bitstream_size + buf_size)
            s->bitstream= av_fast_realloc(s->bitstream, &s->allocated_bitstream_size, s->bitstream_size + buf_size);

        if (s->bitstream_index + s->bitstream_size + buf_size > s->allocated_bitstream_size) {
            memmove(s->bitstream, &s->bitstream[s->bitstream_index],
                    s->bitstream_size);
            s->bitstream_index=0;
        }
        memcpy(&s->bitstream[s->bitstream_index + s->bitstream_size],
               buf, buf_size);
        buf= &s->bitstream[s->bitstream_index];
        buf_size += s->bitstream_size;
        s->bitstream_size= buf_size;

        if (buf_size < s->max_framesize && input_buf_size) {
            return input_buf_size;
        }
    }

    /* check that there is at least the smallest decodable amount of data.
       this amount corresponds to the smallest valid FLAC frame possible.
       FF F8 69 02 00 00 9A 00 00 34 46 */
    if (buf_size < 11)
        goto end;

    /* check for inline header */
    if (AV_RB32(buf) == MKBETAG('f','L','a','C')) {
        if (!s->got_streaminfo && parse_streaminfo(s, buf, buf_size)) {
            av_log(s->avctx, AV_LOG_ERROR, "invalid header\n");
            return -1;
        }
        bytes_read = get_metadata_size(buf, buf_size);
        goto end;
    }

    /* check for frame sync code and resync stream if necessary */
    if ((AV_RB16(buf) & 0xFFFE) != 0xFFF8) {
        const uint8_t *buf_end = buf + buf_size;
        av_log(s->avctx, AV_LOG_ERROR, "FRAME HEADER not here\n");
        while (buf+2 < buf_end && (AV_RB16(buf) & 0xFFFE) != 0xFFF8)
            buf++;
        bytes_read = buf_size - (buf_end - buf);
        goto end; // we may not have enough bits left to decode a frame, so try next time
    }

    /* decode frame */
    init_get_bits(&s->gb, buf, buf_size*8);
    if (decode_frame(s) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "decode_frame() failed\n");
        s->bitstream_size=0;
        s->bitstream_index=0;
        return -1;
    }
    bytes_read = (get_bits_count(&s->gb)+7)/8;

    /* check if allocated data size is large enough for output */
    output_size = s->blocksize * s->channels * (s->is32 ? 4 : 2);
    if (output_size > alloc_data_size) {
        av_log(s->avctx, AV_LOG_ERROR, "output data size is larger than "
                                       "allocated data size\n");
        goto end;
    }
    *data_size = output_size;

#define DECORRELATE(left, right)\
            assert(s->channels == 2);\
            for (i = 0; i < s->blocksize; i++) {\
                int a= s->decoded[0][i];\
                int b= s->decoded[1][i];\
                if (s->is32) {\
                    *samples_32++ = (left)  << s->sample_shift;\
                    *samples_32++ = (right) << s->sample_shift;\
                } else {\
                    *samples_16++ = (left)  << s->sample_shift;\
                    *samples_16++ = (right) << s->sample_shift;\
                }\
            }\
            break;

    switch (s->ch_mode) {
    case FLAC_CHMODE_INDEPENDENT:
        for (j = 0; j < s->blocksize; j++) {
            for (i = 0; i < s->channels; i++) {
                if (s->is32)
                    *samples_32++ = s->decoded[i][j] << s->sample_shift;
                else
                    *samples_16++ = s->decoded[i][j] << s->sample_shift;
            }
        }
        break;
    case FLAC_CHMODE_LEFT_SIDE:
        DECORRELATE(a,a-b)
    case FLAC_CHMODE_RIGHT_SIDE:
        DECORRELATE(a+b,b)
    case FLAC_CHMODE_MID_SIDE:
        DECORRELATE( (a-=b>>1) + b, a)
    }

end:
    if (bytes_read > buf_size) {
        av_log(s->avctx, AV_LOG_ERROR, "overread: %d\n", bytes_read - buf_size);
        s->bitstream_size=0;
        s->bitstream_index=0;
        return -1;
    }

    if (s->bitstream_size) {
        s->bitstream_index += bytes_read;
        s->bitstream_size  -= bytes_read;
        return input_buf_size;
    } else
        return bytes_read;
}

static av_cold int flac_decode_close(AVCodecContext *avctx)
{
    FLACContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->channels; i++) {
        av_freep(&s->decoded[i]);
    }
    av_freep(&s->bitstream);

    return 0;
}

static void flac_flush(AVCodecContext *avctx)
{
    FLACContext *s = avctx->priv_data;

    s->bitstream_size=
    s->bitstream_index= 0;
}

AVCodec flac_decoder = {
    "flac",
    CODEC_TYPE_AUDIO,
    CODEC_ID_FLAC,
    sizeof(FLACContext),
    flac_decode_init,
    NULL,
    flac_decode_close,
    flac_decode_frame,
    CODEC_CAP_DELAY,
    .flush= flac_flush,
    .long_name= NULL_IF_CONFIG_SMALL("FLAC (Free Lossless Audio Codec)"),
};
