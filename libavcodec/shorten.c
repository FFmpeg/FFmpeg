/*
 * Shorten decoder
 * Copyright (c) 2005 Jeff Muizelaar
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/**
 * @file shorten.c
 * Shorten decoder
 * @author Jeff Muizelaar
 *
 */

#define DEBUG
#include <limits.h>
#include "avcodec.h"
#include "bitstream.h"
#include "golomb.h"

#define MAX_CHANNELS 8
#define MAX_BLOCKSIZE 65535

#define OUT_BUFFER_SIZE 16384

#define ULONGSIZE 2

#define WAVE_FORMAT_PCM 0x0001

#define DEFAULT_BLOCK_SIZE 256

#define TYPESIZE 4
#define CHANSIZE 0
#define LPCQSIZE 2
#define ENERGYSIZE 3
#define BITSHIFTSIZE 2

#define TYPE_S16HL 3
#define TYPE_S16LH 5

#define NWRAP 3
#define NSKIPSIZE 1

#define LPCQUANT 5
#define V2LPCQOFFSET (1 << LPCQUANT)

#define FNSIZE 2
#define FN_DIFF0        0
#define FN_DIFF1        1
#define FN_DIFF2        2
#define FN_DIFF3        3
#define FN_QUIT         4
#define FN_BLOCKSIZE    5
#define FN_BITSHIFT     6
#define FN_QLPC         7
#define FN_ZERO         8
#define FN_VERBATIM     9

#define VERBATIM_CKSIZE_SIZE 5
#define VERBATIM_BYTE_SIZE 8
#define CANONICAL_HEADER_SIZE 44

typedef struct ShortenContext {
    AVCodecContext *avctx;
    GetBitContext gb;

    int min_framesize, max_framesize;
    int channels;

    int32_t *decoded[MAX_CHANNELS];
    int32_t *offset[MAX_CHANNELS];
    uint8_t *bitstream;
    int bitstream_size;
    int bitstream_index;
    int allocated_bitstream_size;
    int header_size;
    uint8_t header[OUT_BUFFER_SIZE];
    int version;
    int cur_chan;
    int bitshift;
    int nmean;
    int internal_ftype;
    int nwrap;
    int blocksize;
    int bitindex;
    int32_t lpcqoffset;
} ShortenContext;

static int shorten_decode_init(AVCodecContext * avctx)
{
    ShortenContext *s = avctx->priv_data;
    s->avctx = avctx;

    return 0;
}

static void allocate_buffers(ShortenContext *s)
{
    int i, chan;
    for (chan=0; chan<s->channels; chan++) {
        s->offset[chan] = av_realloc(s->offset[chan], sizeof(int32_t)*FFMAX(1, s->nmean));

        s->decoded[chan] = av_realloc(s->decoded[chan], sizeof(int32_t)*(s->blocksize + s->nwrap));
        for (i=0; i<s->nwrap; i++)
            s->decoded[chan][i] = 0;
        s->decoded[chan] += s->nwrap;

    }
}


static inline unsigned int get_uint(ShortenContext *s, int k)
{
    if (s->version != 0)
        k = get_ur_golomb_shorten(&s->gb, ULONGSIZE);
    return get_ur_golomb_shorten(&s->gb, k);
}


static void fix_bitshift(ShortenContext *s, int32_t *buffer)
{
    int i;

    if (s->bitshift != 0)
        for (i = 0; i < s->blocksize; i++)
            buffer[s->nwrap + i] <<= s->bitshift;
}


static void init_offset(ShortenContext *s)
{
    int32_t mean = 0;
    int  chan, i;
    int nblock = FFMAX(1, s->nmean);
    /* initialise offset */
    switch (s->internal_ftype)
    {
        case TYPE_S16HL:
        case TYPE_S16LH:
            mean = 0;
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "unknown audio type");
            abort();
    }

    for (chan = 0; chan < s->channels; chan++)
        for (i = 0; i < nblock; i++)
            s->offset[chan][i] = mean;
}

static int inline get_le32(GetBitContext *gb)
{
    return bswap_32(get_bits_long(gb, 32));
}

static short inline get_le16(GetBitContext *gb)
{
    return bswap_16(get_bits_long(gb, 16));
}

static int decode_wave_header(AVCodecContext *avctx, uint8_t *header, int header_size)
{
    GetBitContext hb;
    int len;
    int chunk_size;
    short wave_format;

    init_get_bits(&hb, header, header_size*8);
    if (get_le32(&hb) != MKTAG('R','I','F','F')) {
        av_log(avctx, AV_LOG_ERROR, "missing RIFF tag\n");
        return -1;
    }

    chunk_size = get_le32(&hb);

    if (get_le32(&hb) != MKTAG('W','A','V','E')) {
        av_log(avctx, AV_LOG_ERROR, "missing WAVE tag\n");
        return -1;
    }

    while (get_le32(&hb) != MKTAG('f','m','t',' ')) {
        len = get_le32(&hb);
        skip_bits(&hb, 8*len);
    }
    len = get_le32(&hb);

    if (len < 16) {
        av_log(avctx, AV_LOG_ERROR, "fmt chunk was too short\n");
        return -1;
    }

    wave_format = get_le16(&hb);

    switch (wave_format) {
        case WAVE_FORMAT_PCM:
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "unsupported wave format\n");
            return -1;
    }

    avctx->channels = get_le16(&hb);
    avctx->sample_rate = get_le32(&hb);
    avctx->bit_rate = get_le32(&hb) * 8;
    avctx->block_align = get_le16(&hb);
    avctx->bits_per_sample = get_le16(&hb);

    if (avctx->bits_per_sample != 16) {
        av_log(avctx, AV_LOG_ERROR, "unsupported number of bits per sample\n");
        return -1;
    }

    len -= 16;
    if (len > 0)
        av_log(avctx, AV_LOG_INFO, "%d header bytes unparsed\n", len);

    return 0;
}

static int16_t * interleave_buffer(int16_t *samples, int nchan, int blocksize, int32_t **buffer) {
    int i, chan;
    for (i=0; i<blocksize; i++)
        for (chan=0; chan < nchan; chan++)
            *samples++ = FFMIN(buffer[chan][i], 32768);
    return samples;
}

static void decode_subframe_lpc(ShortenContext *s, int channel, int residual_size, int pred_order)
{
    int sum, i, j;
    int coeffs[pred_order];

    for (i=0; i<pred_order; i++)
        coeffs[i] = get_sr_golomb_shorten(&s->gb, LPCQUANT);

    for (i=0; i < s->blocksize; i++) {
        sum = s->lpcqoffset;
        for (j=0; j<pred_order; j++)
            sum += coeffs[j] * s->decoded[channel][i-j-1];
        s->decoded[channel][i] = get_sr_golomb_shorten(&s->gb, residual_size) + (sum >> LPCQUANT);
    }
}


static int shorten_decode_frame(AVCodecContext *avctx,
        void *data, int *data_size,
        uint8_t *buf, int buf_size)
{
    ShortenContext *s = avctx->priv_data;
    int i, input_buf_size = 0;
    int16_t *samples = data;
    if(s->max_framesize == 0){
        s->max_framesize= 1024; // should hopefully be enough for the first header
        s->bitstream= av_fast_realloc(s->bitstream, &s->allocated_bitstream_size, s->max_framesize);
    }

    if(1 && s->max_framesize){//FIXME truncated
        buf_size= FFMIN(buf_size, s->max_framesize - s->bitstream_size);
        input_buf_size= buf_size;

        if(s->bitstream_index + s->bitstream_size + buf_size > s->allocated_bitstream_size){
            //                printf("memmove\n");
            memmove(s->bitstream, &s->bitstream[s->bitstream_index], s->bitstream_size);
            s->bitstream_index=0;
        }
        memcpy(&s->bitstream[s->bitstream_index + s->bitstream_size], buf, buf_size);
        buf= &s->bitstream[s->bitstream_index];
        buf_size += s->bitstream_size;
        s->bitstream_size= buf_size;

        if(buf_size < s->max_framesize){
            //dprintf("wanna more data ... %d\n", buf_size);
            return input_buf_size;
        }
    }
    init_get_bits(&s->gb, buf, buf_size*8);
    get_bits(&s->gb, s->bitindex);
    if (!s->blocksize)
    {
        int maxnlpc = 0;
        /* shorten signature */
        if (get_bits_long(&s->gb, 32) != bswap_32(ff_get_fourcc("ajkg"))) {
            av_log(s->avctx, AV_LOG_ERROR, "missing shorten magic 'ajkg'\n");
            return -1;
        }

        s->lpcqoffset = 0;
        s->blocksize = DEFAULT_BLOCK_SIZE;
        s->channels = 1;
        s->nmean = -1;
        s->version = get_bits(&s->gb, 8);
        s->internal_ftype = get_uint(s, TYPESIZE);

        s->channels = get_uint(s, CHANSIZE);
        if (s->channels > MAX_CHANNELS) {
            av_log(s->avctx, AV_LOG_ERROR, "too many channels: %d\n", s->channels);
            return -1;
        }

        /* get blocksize if version > 0 */
        if (s->version > 0) {
            int skip_bytes;
            s->blocksize = get_uint(s, av_log2(DEFAULT_BLOCK_SIZE));
            maxnlpc = get_uint(s, LPCQSIZE);
            s->nmean = get_uint(s, 0);

            skip_bytes = get_uint(s, NSKIPSIZE);
            for (i=0; i<skip_bytes; i++) {
                skip_bits(&s->gb, 8);
            }
        }
        s->nwrap = FFMAX(NWRAP, maxnlpc);

        allocate_buffers(s);

        init_offset(s);

        if (s->version > 1)
            s->lpcqoffset = V2LPCQOFFSET;

        if (get_ur_golomb_shorten(&s->gb, FNSIZE) != FN_VERBATIM) {
            av_log(s->avctx, AV_LOG_ERROR, "missing verbatim section at begining of stream\n");
            return -1;
        }

        s->header_size = get_ur_golomb_shorten(&s->gb, VERBATIM_CKSIZE_SIZE);
        if (s->header_size >= OUT_BUFFER_SIZE || s->header_size < CANONICAL_HEADER_SIZE) {
            av_log(s->avctx, AV_LOG_ERROR, "header is wrong size: %d\n", s->header_size);
            return -1;
        }

        for (i=0; i<s->header_size; i++)
            s->header[i] = (char)get_ur_golomb_shorten(&s->gb, VERBATIM_BYTE_SIZE);

        if (decode_wave_header(avctx, s->header, s->header_size) < 0)
            return -1;

        s->cur_chan = 0;
        s->bitshift = 0;
    }
    else
    {
        int cmd;
        int len;
        cmd = get_ur_golomb_shorten(&s->gb, FNSIZE);
        switch (cmd) {
            case FN_ZERO:
            case FN_DIFF0:
            case FN_DIFF1:
            case FN_DIFF2:
            case FN_DIFF3:
            case FN_QLPC:
                {
                    int residual_size = 0;
                    int channel = s->cur_chan;
                    int32_t coffset;
                    if (cmd != FN_ZERO) {
                        residual_size = get_ur_golomb_shorten(&s->gb, ENERGYSIZE);
                        /* this is a hack as version 0 differed in defintion of get_sr_golomb_shorten */
                        if (s->version == 0)
                            residual_size--;
                    }

                    if (s->nmean == 0)
                        coffset = s->offset[channel][0];
                    else {
                        int32_t sum = (s->version < 2) ? 0 : s->nmean / 2;
                        for (i=0; i<s->nmean; i++)
                            sum += s->offset[channel][i];
                        coffset = sum / s->nmean;
                        if (s->version >= 2)
                            coffset >>= FFMIN(1, s->bitshift);
                    }
                    switch (cmd) {
                        case FN_ZERO:
                            for (i=0; i<s->blocksize; i++)
                                s->decoded[channel][i] = 0;
                            break;
                        case FN_DIFF0:
                            for (i=0; i<s->blocksize; i++)
                                s->decoded[channel][i] = get_sr_golomb_shorten(&s->gb, residual_size) + coffset;
                            break;
                        case FN_DIFF1:
                            for (i=0; i<s->blocksize; i++)
                                s->decoded[channel][i] = get_sr_golomb_shorten(&s->gb, residual_size) + s->decoded[channel][i - 1];
                            break;
                        case FN_DIFF2:
                            for (i=0; i<s->blocksize; i++)
                                s->decoded[channel][i] = get_sr_golomb_shorten(&s->gb, residual_size) + 2*s->decoded[channel][i-1]
                                                                                                      -   s->decoded[channel][i-2];
                            break;
                        case FN_DIFF3:
                            for (i=0; i<s->blocksize; i++)
                                s->decoded[channel][i] = get_sr_golomb_shorten(&s->gb, residual_size) + 3*s->decoded[channel][i-1]
                                                                                                      - 3*s->decoded[channel][i-2]
                                                                                                      +   s->decoded[channel][i-3];
                            break;
                        case FN_QLPC:
                            {
                                int pred_order = get_ur_golomb_shorten(&s->gb, LPCQSIZE);
                                for (i=0; i<pred_order; i++)
                                    s->decoded[channel][i - pred_order] -= coffset;
                                decode_subframe_lpc(s, channel, residual_size, pred_order);
                                if (coffset != 0)
                                    for (i=0; i < s->blocksize; i++)
                                        s->decoded[channel][i] += coffset;
                            }
                    }
                    if (s->nmean > 0) {
                        int32_t sum = (s->version < 2) ? 0 : s->blocksize / 2;
                        for (i=0; i<s->blocksize; i++)
                            sum += s->decoded[channel][i];

                        for (i=1; i<s->nmean; i++)
                            s->offset[channel][i-1] = s->offset[channel][i];

                        if (s->version < 2)
                            s->offset[channel][s->nmean - 1] = sum / s->blocksize;
                        else
                            s->offset[channel][s->nmean - 1] = (sum / s->blocksize) << s->bitshift;
                    }
                    for (i=-s->nwrap; i<0; i++)
                        s->decoded[channel][i] = s->decoded[channel][i + s->blocksize];

                    fix_bitshift(s, s->decoded[channel]);

                    s->cur_chan++;
                    if (s->cur_chan == s->channels) {
                        samples = interleave_buffer(samples, s->channels, s->blocksize, s->decoded);
                        s->cur_chan = 0;
                        goto frame_done;
                    }
                    break;
                }
                break;
            case FN_VERBATIM:
                len = get_ur_golomb_shorten(&s->gb, VERBATIM_CKSIZE_SIZE);
                while (len--) {
                    get_ur_golomb_shorten(&s->gb, VERBATIM_BYTE_SIZE);
                }
                break;
            case FN_BITSHIFT:
                s->bitshift = get_ur_golomb_shorten(&s->gb, BITSHIFTSIZE);
                break;
            case FN_BLOCKSIZE:
                s->blocksize = get_uint(s, av_log2(s->blocksize));
                break;
            case FN_QUIT:
                return buf_size;
                break;
            default:
                av_log(avctx, AV_LOG_ERROR, "unknown shorten function %d\n", cmd);
                return -1;
                break;
        }
    }
frame_done:
    *data_size = (int8_t *)samples - (int8_t *)data;

    //    s->last_blocksize = s->blocksize;
    s->bitindex = get_bits_count(&s->gb) - 8*((get_bits_count(&s->gb))/8);
    i= (get_bits_count(&s->gb))/8;
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

static int shorten_decode_close(AVCodecContext *avctx)
{
    ShortenContext *s = avctx->priv_data;
    int i;

    for (i = 0; i < s->channels; i++) {
        s->decoded[i] -= s->nwrap;
        av_freep(&s->decoded[i]);
        av_freep(&s->offset[i]);
    }
    av_freep(&s->bitstream);
    return 0;
}

static void shorten_flush(AVCodecContext *avctx){
    ShortenContext *s = avctx->priv_data;

    s->bitstream_size=
        s->bitstream_index= 0;
}

AVCodec shorten_decoder = {
    "shorten",
    CODEC_TYPE_AUDIO,
    CODEC_ID_SHORTEN,
    sizeof(ShortenContext),
    shorten_decode_init,
    NULL,
    shorten_decode_close,
    shorten_decode_frame,
    .flush= shorten_flush,
};
