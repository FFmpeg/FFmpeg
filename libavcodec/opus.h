/*
 * Opus decoder/demuxer common functions
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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

#ifndef AVCODEC_OPUS_H
#define AVCODEC_OPUS_H

#include <stdint.h>

#include "libavutil/audio_fifo.h"
#include "libavutil/float_dsp.h"
#include "libavutil/frame.h"

#include "libswresample/swresample.h"

#include "avcodec.h"
#include "get_bits.h"

#define MAX_FRAME_SIZE               1275
#define MAX_FRAMES                   48
#define MAX_PACKET_DUR               5760

#define CELT_SHORT_BLOCKSIZE         120
#define CELT_OVERLAP                 CELT_SHORT_BLOCKSIZE
#define CELT_MAX_LOG_BLOCKS          3
#define CELT_MAX_FRAME_SIZE          (CELT_SHORT_BLOCKSIZE * (1 << CELT_MAX_LOG_BLOCKS))
#define CELT_MAX_BANDS               21
#define CELT_VECTORS                 11
#define CELT_ALLOC_STEPS             6
#define CELT_FINE_OFFSET             21
#define CELT_MAX_FINE_BITS           8
#define CELT_NORM_SCALE              16384
#define CELT_QTHETA_OFFSET           4
#define CELT_QTHETA_OFFSET_TWOPHASE  16
#define CELT_DEEMPH_COEFF            0.85000610f
#define CELT_POSTFILTER_MINPERIOD    15
#define CELT_ENERGY_SILENCE          (-28.0f)

#define SILK_HISTORY                 322
#define SILK_MAX_LPC                 16

#define ROUND_MULL(a,b,s) (((MUL64(a, b) >> ((s) - 1)) + 1) >> 1)
#define ROUND_MUL16(a,b)  ((MUL16(a, b) + 16384) >> 15)
#define opus_ilog(i) (av_log2(i) + !!(i))

#define OPUS_TS_HEADER     0x7FE0        // 0x3ff (11 bits)
#define OPUS_TS_MASK       0xFFE0        // top 11 bits

static const uint8_t opus_default_extradata[30] = {
    'O', 'p', 'u', 's', 'H', 'e', 'a', 'd',
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

enum OpusMode {
    OPUS_MODE_SILK,
    OPUS_MODE_HYBRID,
    OPUS_MODE_CELT
};

enum OpusBandwidth {
    OPUS_BANDWIDTH_NARROWBAND,
    OPUS_BANDWIDTH_MEDIUMBAND,
    OPUS_BANDWIDTH_WIDEBAND,
    OPUS_BANDWIDTH_SUPERWIDEBAND,
    OPUS_BANDWIDTH_FULLBAND
};

typedef struct RawBitsContext {
    const uint8_t *position;
    unsigned int bytes;
    unsigned int cachelen;
    unsigned int cacheval;
} RawBitsContext;

typedef struct OpusRangeCoder {
    GetBitContext gb;
    RawBitsContext rb;
    unsigned int range;
    unsigned int value;
    unsigned int total_read_bits;
} OpusRangeCoder;

typedef struct SilkContext SilkContext;

typedef struct CeltContext CeltContext;

typedef struct OpusPacket {
    int packet_size;                /**< packet size */
    int data_size;                  /**< size of the useful data -- packet size - padding */
    int code;                       /**< packet code: specifies the frame layout */
    int stereo;                     /**< whether this packet is mono or stereo */
    int vbr;                        /**< vbr flag */
    int config;                     /**< configuration: tells the audio mode,
                                     **                bandwidth, and frame duration */
    int frame_count;                /**< frame count */
    int frame_offset[MAX_FRAMES];   /**< frame offsets */
    int frame_size[MAX_FRAMES];     /**< frame sizes */
    int frame_duration;             /**< frame duration, in samples @ 48kHz */
    enum OpusMode mode;             /**< mode */
    enum OpusBandwidth bandwidth;   /**< bandwidth */
} OpusPacket;

typedef struct OpusStreamContext {
    AVCodecContext *avctx;
    int output_channels;

    OpusRangeCoder rc;
    OpusRangeCoder redundancy_rc;
    SilkContext *silk;
    CeltContext *celt;
    AVFloatDSPContext *fdsp;

    float silk_buf[2][960];
    float *silk_output[2];
    DECLARE_ALIGNED(32, float, celt_buf)[2][960];
    float *celt_output[2];

    float redundancy_buf[2][960];
    float *redundancy_output[2];

    /* data buffers for the final output data */
    float *out[2];
    int out_size;

    float *out_dummy;
    int    out_dummy_allocated_size;

    SwrContext *swr;
    AVAudioFifo *celt_delay;
    int silk_samplerate;
    /* number of samples we still want to get from the resampler */
    int delayed_samples;

    OpusPacket packet;

    int redundancy_idx;
} OpusStreamContext;

// a mapping between an opus stream and an output channel
typedef struct ChannelMap {
    int stream_idx;
    int channel_idx;

    // when a single decoded channel is mapped to multiple output channels, we
    // write to the first output directly and copy from it to the others
    // this field is set to 1 for those copied output channels
    int copy;
    // this is the index of the output channel to copy from
    int copy_idx;

    // this channel is silent
    int silence;
} ChannelMap;

typedef struct OpusContext {
    OpusStreamContext *streams;
    int             nb_streams;
    int      nb_stereo_streams;

    AVFloatDSPContext *fdsp;
    int16_t gain_i;
    float   gain;

    ChannelMap *channel_maps;
} OpusContext;

static av_always_inline void opus_rc_normalize(OpusRangeCoder *rc)
{
    while (rc->range <= 1<<23) {
        rc->value = ((rc->value << 8) | (get_bits(&rc->gb, 8) ^ 0xFF)) & ((1u << 31) - 1);
        rc->range          <<= 8;
        rc->total_read_bits += 8;
    }
}

static av_always_inline void opus_rc_update(OpusRangeCoder *rc, unsigned int scale,
                                          unsigned int low, unsigned int high,
                                          unsigned int total)
{
    rc->value -= scale * (total - high);
    rc->range  = low ? scale * (high - low)
                      : rc->range - scale * (total - high);
    opus_rc_normalize(rc);
}

static av_always_inline unsigned int opus_rc_getsymbol(OpusRangeCoder *rc, const uint16_t *cdf)
{
    unsigned int k, scale, total, symbol, low, high;

    total = *cdf++;

    scale   = rc->range / total;
    symbol = rc->value / scale + 1;
    symbol = total - FFMIN(symbol, total);

    for (k = 0; cdf[k] <= symbol; k++);
    high = cdf[k];
    low  = k ? cdf[k-1] : 0;

    opus_rc_update(rc, scale, low, high, total);

    return k;
}

static av_always_inline unsigned int opus_rc_p2model(OpusRangeCoder *rc, unsigned int bits)
{
    unsigned int k, scale;
    scale = rc->range >> bits; // in this case, scale = symbol

    if (rc->value >= scale) {
        rc->value -= scale;
        rc->range -= scale;
        k = 0;
    } else {
        rc->range = scale;
        k = 1;
    }
    opus_rc_normalize(rc);
    return k;
}

/**
 * CELT: estimate bits of entropy that have thus far been consumed for the
 *       current CELT frame, to integer and fractional (1/8th bit) precision
 */
static av_always_inline unsigned int opus_rc_tell(const OpusRangeCoder *rc)
{
    return rc->total_read_bits - av_log2(rc->range) - 1;
}

static av_always_inline unsigned int opus_rc_tell_frac(const OpusRangeCoder *rc)
{
    unsigned int i, total_bits, rcbuffer, range;

    total_bits = rc->total_read_bits << 3;
    rcbuffer   = av_log2(rc->range) + 1;
    range      = rc->range >> (rcbuffer-16);

    for (i = 0; i < 3; i++) {
        int bit;
        range = range * range >> 15;
        bit = range >> 16;
        rcbuffer = rcbuffer << 1 | bit;
        range >>= bit;
    }

    return total_bits - rcbuffer;
}

/**
 * CELT: read 1-25 raw bits at the end of the frame, backwards byte-wise
 */
static av_always_inline unsigned int opus_getrawbits(OpusRangeCoder *rc, unsigned int count)
{
    unsigned int value = 0;

    while (rc->rb.bytes && rc->rb.cachelen < count) {
        rc->rb.cacheval |= *--rc->rb.position << rc->rb.cachelen;
        rc->rb.cachelen += 8;
        rc->rb.bytes--;
    }

    value = rc->rb.cacheval & ((1<<count)-1);
    rc->rb.cacheval    >>= count;
    rc->rb.cachelen     -= count;
    rc->total_read_bits += count;

    return value;
}

/**
 * CELT: read a uniform distribution
 */
static av_always_inline unsigned int opus_rc_unimodel(OpusRangeCoder *rc, unsigned int size)
{
    unsigned int bits, k, scale, total;

    bits  = opus_ilog(size - 1);
    total = (bits > 8) ? ((size - 1) >> (bits - 8)) + 1 : size;

    scale  = rc->range / total;
    k      = rc->value / scale + 1;
    k      = total - FFMIN(k, total);
    opus_rc_update(rc, scale, k, k + 1, total);

    if (bits > 8) {
        k = k << (bits - 8) | opus_getrawbits(rc, bits - 8);
        return FFMIN(k, size - 1);
    } else
        return k;
}

static av_always_inline int opus_rc_laplace(OpusRangeCoder *rc, unsigned int symbol, int decay)
{
    /* extends the range coder to model a Laplace distribution */
    int value = 0;
    unsigned int scale, low = 0, center;

    scale  = rc->range >> 15;
    center = rc->value / scale + 1;
    center = (1 << 15) - FFMIN(center, 1 << 15);

    if (center >= symbol) {
        value++;
        low = symbol;
        symbol = 1 + ((32768 - 32 - symbol) * (16384-decay) >> 15);

        while (symbol > 1 && center >= low + 2 * symbol) {
            value++;
            symbol *= 2;
            low    += symbol;
            symbol  = (((symbol - 2) * decay) >> 15) + 1;
        }

        if (symbol <= 1) {
            int distance = (center - low) >> 1;
            value += distance;
            low   += 2 * distance;
        }

        if (center < low + symbol)
            value *= -1;
        else
            low += symbol;
    }

    opus_rc_update(rc, scale, low, FFMIN(low + symbol, 32768), 32768);

    return value;
}

static av_always_inline unsigned int opus_rc_stepmodel(OpusRangeCoder *rc, int k0)
{
    /* Use a probability of 3 up to itheta=8192 and then use 1 after */
    unsigned int k, scale, symbol, total = (k0+1)*3 + k0;
    scale  = rc->range / total;
    symbol = rc->value / scale + 1;
    symbol = total - FFMIN(symbol, total);

    k = (symbol < (k0+1)*3) ? symbol/3 : symbol - (k0+1)*2;

    opus_rc_update(rc, scale, (k <= k0) ? 3*(k+0) : (k-1-k0) + 3*(k0+1),
                   (k <= k0) ? 3*(k+1) : (k-0-k0) + 3*(k0+1), total);
    return k;
}

static av_always_inline unsigned int opus_rc_trimodel(OpusRangeCoder *rc, int qn)
{
    unsigned int k, scale, symbol, total, low, center;

    total = ((qn>>1) + 1) * ((qn>>1) + 1);
    scale   = rc->range / total;
    center = rc->value / scale + 1;
    center = total - FFMIN(center, total);

    if (center < total >> 1) {
        k      = (ff_sqrt(8 * center + 1) - 1) >> 1;
        low    = k * (k + 1) >> 1;
        symbol = k + 1;
    } else {
        k      = (2*(qn + 1) - ff_sqrt(8*(total - center - 1) + 1)) >> 1;
        low    = total - ((qn + 1 - k) * (qn + 2 - k) >> 1);
        symbol = qn + 1 - k;
    }

    opus_rc_update(rc, scale, low, low + symbol, total);

    return k;
}

int ff_opus_parse_packet(OpusPacket *pkt, const uint8_t *buf, int buf_size,
                         int self_delimited);

int ff_opus_parse_extradata(AVCodecContext *avctx, OpusContext *s);

int ff_silk_init(AVCodecContext *avctx, SilkContext **ps, int output_channels);
void ff_silk_free(SilkContext **ps);
void ff_silk_flush(SilkContext *s);

/**
 * Decode the LP layer of one Opus frame (which may correspond to several SILK
 * frames).
 */
int ff_silk_decode_superframe(SilkContext *s, OpusRangeCoder *rc,
                              float *output[2],
                              enum OpusBandwidth bandwidth, int coded_channels,
                              int duration_ms);

int ff_celt_init(AVCodecContext *avctx, CeltContext **s, int output_channels);

void ff_celt_free(CeltContext **s);

void ff_celt_flush(CeltContext *s);

int ff_celt_decode_frame(CeltContext *s, OpusRangeCoder *rc,
                         float **output, int coded_channels, int frame_size,
                         int startband,  int endband);

extern const float ff_celt_window2[120];

#endif /* AVCODEC_OPUS_H */
