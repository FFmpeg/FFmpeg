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

#define ALT_BITSTREAM_READER
#include "libavutil/crc.h"
#include "avcodec.h"
#include "bitstream.h"
#include "golomb.h"
#include "flac.h"

#undef NDEBUG
#include <assert.h>

#define MAX_CHANNELS 8
#define MAX_BLOCKSIZE 65535

enum decorrelation_type {
    INDEPENDENT,
    LEFT_SIDE,
    RIGHT_SIDE,
    MID_SIDE,
};

typedef struct FLACContext {
    FLACSTREAMINFO

    AVCodecContext *avctx;                  ///< parent AVCodecContext
    GetBitContext gb;                       ///< GetBitContext initialized to start at the current frame

    int blocksize;                          ///< number of samples in the current frame
    int curr_bps;                           ///< bps for current subframe, adjusted for channel correlation and wasted bits
    int sample_shift;                       ///< shift required to make output samples 16-bit or 32-bit
    int is32;                               ///< flag to indicate if output should be 32-bit instead of 16-bit
    enum decorrelation_type decorrelation;  ///< channel decorrelation type in the current frame

    int32_t *decoded[MAX_CHANNELS];         ///< decoded samples
    uint8_t *bitstream;
    unsigned int bitstream_size;
    unsigned int bitstream_index;
    unsigned int allocated_bitstream_size;
} FLACContext;

static const int sample_rate_table[] =
{ 0,
  88200, 176400, 192000,
  8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000,
  0, 0, 0, 0 };

static const int sample_size_table[] =
{ 0, 8, 12, 0, 16, 20, 24, 0 };

static const int blocksize_table[] = {
     0,    192, 576<<0, 576<<1, 576<<2, 576<<3,      0,      0,
256<<0, 256<<1, 256<<2, 256<<3, 256<<4, 256<<5, 256<<6, 256<<7
};

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

    return 0;
}

static void dump_headers(AVCodecContext *avctx, FLACStreaminfo *s)
{
    av_log(avctx, AV_LOG_DEBUG, "  Blocksize: %d .. %d\n", s->min_blocksize,
           s->max_blocksize);
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
        // FIXME header overhead
        s->max_framesize= (s->channels * s->bps * s->max_blocksize + 7)/ 8;
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

    /* mandatory streaminfo */
    s->min_blocksize = get_bits(&gb, 16);
    s->max_blocksize = get_bits(&gb, 16);

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
    s->samples |= get_bits_long(&gb, 4);

    skip_bits(&gb, 64); /* md5 sum */
    skip_bits(&gb, 64); /* md5 sum */

    dump_headers(avctx, s);
}

/**
 * Parse a list of metadata blocks. This list of blocks must begin with
 * the fLaC marker.
 * @param s the flac decoding context containing the gb bit reader used to
 *          parse metadata
 * @return 1 if some metadata was read, 0 if no fLaC marker was found
 */
static int metadata_parse(FLACContext *s)
{
    int i, metadata_last, metadata_type, metadata_size, streaminfo_updated=0;
    int initial_pos= get_bits_count(&s->gb);

    if (show_bits_long(&s->gb, 32) == MKBETAG('f','L','a','C')) {
        skip_bits(&s->gb, 32);

        do {
            metadata_last = get_bits1(&s->gb);
            metadata_type = get_bits(&s->gb, 7);
            metadata_size = get_bits_long(&s->gb, 24);

            if (get_bits_count(&s->gb) + 8*metadata_size > s->gb.size_in_bits) {
                skip_bits_long(&s->gb, initial_pos - get_bits_count(&s->gb));
                break;
            }

            if (metadata_size) {
                switch (metadata_type) {
                case FLAC_METADATA_TYPE_STREAMINFO:
                    ff_flac_parse_streaminfo(s->avctx, (FLACStreaminfo *)s,
                                             s->gb.buffer+get_bits_count(&s->gb)/8);
                    streaminfo_updated = 1;

                default:
                    for (i = 0; i < metadata_size; i++)
                        skip_bits(&s->gb, 8);
                }
            }
        } while (!metadata_last);

        if (streaminfo_updated)
            allocate_buffers(s);
        return 1;
    }
    return 0;
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
                s->decoded[channel][sample] = get_sbits(&s->gb, tmp);
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
        decoded[i] = get_sbits(&s->gb, s->curr_bps);
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
        decoded[i] = get_sbits(&s->gb, s->curr_bps);
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
        if (s->decorrelation == RIGHT_SIDE)
            s->curr_bps++;
    } else {
        if (s->decorrelation == LEFT_SIDE || s->decorrelation == MID_SIDE)
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

//FIXME use av_log2 for types
    if (type == 0) {
        tmp = get_sbits(&s->gb, s->curr_bps);
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = tmp;
    } else if (type == 1) {
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = get_sbits(&s->gb, s->curr_bps);
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

static int decode_frame(FLACContext *s, int alloc_data_size)
{
    int blocksize_code, sample_rate_code, sample_size_code, assignment, i, crc8;
    int decorrelation, bps, blocksize, samplerate;

    blocksize_code = get_bits(&s->gb, 4);

    sample_rate_code = get_bits(&s->gb, 4);

    assignment = get_bits(&s->gb, 4); /* channel assignment */
    if (assignment < 8 && s->channels == assignment+1)
        decorrelation = INDEPENDENT;
    else if (assignment >=8 && assignment < 11 && s->channels == 2)
        decorrelation = LEFT_SIDE + assignment - 8;
    else {
        av_log(s->avctx, AV_LOG_ERROR, "unsupported channel assignment %d (channels=%d)\n",
               assignment, s->channels);
        return -1;
    }

    sample_size_code = get_bits(&s->gb, 3);
    if (sample_size_code == 0)
        bps= s->bps;
    else if ((sample_size_code != 3) && (sample_size_code != 7))
        bps = sample_size_table[sample_size_code];
    else {
        av_log(s->avctx, AV_LOG_ERROR, "invalid sample size code (%d)\n",
               sample_size_code);
        return -1;
    }
    if (bps > 16) {
        s->avctx->sample_fmt = SAMPLE_FMT_S32;
        s->sample_shift = 32 - bps;
        s->is32 = 1;
    } else {
        s->avctx->sample_fmt = SAMPLE_FMT_S16;
        s->sample_shift = 16 - bps;
        s->is32 = 0;
    }
    s->bps = s->avctx->bits_per_raw_sample = bps;

    if (get_bits1(&s->gb)) {
        av_log(s->avctx, AV_LOG_ERROR, "broken stream, invalid padding\n");
        return -1;
    }

    if (get_utf8(&s->gb) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "utf8 fscked\n");
        return -1;
    }

    if (blocksize_code == 0)
        blocksize = s->min_blocksize;
    else if (blocksize_code == 6)
        blocksize = get_bits(&s->gb, 8)+1;
    else if (blocksize_code == 7)
        blocksize = get_bits(&s->gb, 16)+1;
    else
        blocksize = blocksize_table[blocksize_code];

    if (blocksize > s->max_blocksize) {
        av_log(s->avctx, AV_LOG_ERROR, "blocksize %d > %d\n", blocksize,
               s->max_blocksize);
        return -1;
    }

    if (blocksize * s->channels * sizeof(int16_t) > alloc_data_size)
        return -1;

    if (sample_rate_code == 0)
        samplerate= s->samplerate;
    else if (sample_rate_code < 12)
        samplerate = sample_rate_table[sample_rate_code];
    else if (sample_rate_code == 12)
        samplerate = get_bits(&s->gb, 8) * 1000;
    else if (sample_rate_code == 13)
        samplerate = get_bits(&s->gb, 16);
    else if (sample_rate_code == 14)
        samplerate = get_bits(&s->gb, 16) * 10;
    else {
        av_log(s->avctx, AV_LOG_ERROR, "illegal sample rate code %d\n",
               sample_rate_code);
        return -1;
    }

    skip_bits(&s->gb, 8);
    crc8 = av_crc(av_crc_get_table(AV_CRC_8_ATM), 0,
                  s->gb.buffer, get_bits_count(&s->gb)/8);
    if (crc8) {
        av_log(s->avctx, AV_LOG_ERROR, "header crc mismatch crc=%2X\n", crc8);
        return -1;
    }

    s->blocksize    = blocksize;
    s->samplerate   = samplerate;
    s->bps          = bps;
    s->decorrelation= decorrelation;

//    dump_headers(s->avctx, (FLACStreaminfo *)s);

    /* subframes */
    for (i = 0; i < s->channels; i++) {
        if (decode_subframe(s, i) < 0)
            return -1;
    }

    align_get_bits(&s->gb);

    /* frame footer */
    skip_bits(&s->gb, 16); /* data crc */

    return 0;
}

static int flac_decode_frame(AVCodecContext *avctx,
                            void *data, int *data_size,
                            const uint8_t *buf, int buf_size)
{
    FLACContext *s = avctx->priv_data;
    int tmp = 0, i, j = 0, input_buf_size = 0;
    int16_t *samples_16 = data;
    int32_t *samples_32 = data;
    int alloc_data_size= *data_size;

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

    init_get_bits(&s->gb, buf, buf_size*8);

    if (metadata_parse(s))
        goto end;

    tmp = show_bits(&s->gb, 16);
    if ((tmp & 0xFFFE) != 0xFFF8) {
        av_log(s->avctx, AV_LOG_ERROR, "FRAME HEADER not here\n");
        while (get_bits_count(&s->gb)/8+2 < buf_size && (show_bits(&s->gb, 16) & 0xFFFE) != 0xFFF8)
            skip_bits(&s->gb, 8);
        goto end; // we may not have enough bits left to decode a frame, so try next time
    }
    skip_bits(&s->gb, 16);
    if (decode_frame(s, alloc_data_size) < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "decode_frame() failed\n");
        s->bitstream_size=0;
        s->bitstream_index=0;
        return -1;
    }

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

    switch (s->decorrelation) {
    case INDEPENDENT:
        for (j = 0; j < s->blocksize; j++) {
            for (i = 0; i < s->channels; i++) {
                if (s->is32)
                    *samples_32++ = s->decoded[i][j] << s->sample_shift;
                else
                    *samples_16++ = s->decoded[i][j] << s->sample_shift;
            }
        }
        break;
    case LEFT_SIDE:
        DECORRELATE(a,a-b)
    case RIGHT_SIDE:
        DECORRELATE(a+b,b)
    case MID_SIDE:
        DECORRELATE( (a-=b>>1) + b, a)
    }

    *data_size = s->blocksize * s->channels * (s->is32 ? 4 : 2);

end:
    i= (get_bits_count(&s->gb)+7)/8;
    if (i > buf_size) {
        av_log(s->avctx, AV_LOG_ERROR, "overread: %d\n", i - buf_size);
        s->bitstream_size=0;
        s->bitstream_index=0;
        return -1;
    }

    if (s->bitstream_size) {
        s->bitstream_index += i;
        s->bitstream_size  -= i;
        return input_buf_size;
    } else
        return i;
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
