/*
 * FLAC (Free Lossless Audio Codec) decoder
 * Copyright (c) 2003 Alex Beregszaszi
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
 * @file flac.c
 * FLAC (Free Lossless Audio Codec) decoder
 * @author Alex Beregszaszi
 */
 
#include "avcodec.h"
#include "golomb.h"

#define MAX_CHANNELS 8
#define MAX_BLOCKSIZE 65535

enum channel_order {
    INDEPENDENT,
    LEFT_SIDE,
    RIGHT_SIDE,
    MID_SIDE,
};

typedef struct FLACContext {
    AVCodecContext *avctx;
    GetBitContext gb;

    int min_blocksize, max_blocksize;
    int min_framesize, max_framesize;
    int samplerate, channels;
    int blocksize, last_blocksize;
    int bps, curr_bps;
    enum channel_order order;

    uint8_t *residual[MAX_CHANNELS];
    uint32_t *decoded[MAX_CHANNELS];
} FLACContext;

#define METADATA_TYPE_STREAMINFO 0

static int sample_rate_table[] =
{ 0, 0, 0, 0,
  8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000,
  0, 0, 0, 0 }; 

static int sample_size_table[] = 
{ 0, 8, 12, 0, 16, 20, 24, 0 };

static uint64_t get_uvlc(GetBitContext *gb, int is64)
{
    uint64_t val = 0;
    int i = 0;

    while(i++ < 5+is64)
    {
        const int tmp = get_bits(gb, 8);
        
        if (tmp & 0x80)
            val = (val << 7) + tmp - 0x80;
        else
            return (val << 7) + tmp;
    }
    return -1;
}

static int flac_decode_init(AVCodecContext * avctx)
{
    return 0;
}

static void dump_headers(FLACContext *s)
{
    printf("  Blocksize: %d .. %d (%d)\n", s->min_blocksize, s->max_blocksize, s->blocksize);
    printf("  Framesize: %d .. %d\n", s->min_framesize, s->max_framesize);
    printf("  Samplerate: %d\n", s->samplerate);
    printf("  Channels: %d\n", s->channels);
    printf("  Bits: %d\n", s->bps);
}

static void metadata_streaminfo(FLACContext *s)
{
    int i;

    /* mandatory streaminfo */
    s->min_blocksize = get_bits(&s->gb, 16);
    s->max_blocksize = get_bits(&s->gb, 16);

    s->min_framesize = get_bits_long(&s->gb, 24);
    s->max_framesize = get_bits_long(&s->gb, 24);
    
    s->samplerate = get_bits_long(&s->gb, 20);
    s->channels = get_bits(&s->gb, 3) + 1;
    s->bps = get_bits(&s->gb, 5) + 1;
    
    s->avctx->channels = s->channels;
    s->avctx->sample_rate = s->samplerate;

    skip_bits(&s->gb, 36); /* total num of samples */
    
    skip_bits(&s->gb, 64); /* md5 sum */
    skip_bits(&s->gb, 64); /* md5 sum */

    for (i = 0; i < s->channels; i++)
    {
        s->decoded[i] = av_realloc(s->decoded[i], sizeof(uint32_t)*s->max_blocksize);
        s->residual[i] = av_realloc(s->residual[i], sizeof(uint8_t)*s->max_blocksize);
    }
}

static int decode_residuals(FLACContext *s, int channel, int pred_order)
{
    int i, tmp, partition, method_type, rice_order;
    int sample = 0, samples;

    method_type = get_bits(&s->gb, 2);
    if (method_type != 0)
        return -1;
    
    rice_order = get_bits(&s->gb, 4);

    samples = (rice_order > 0) ?
        (s->blocksize >> rice_order) : (s->blocksize - pred_order);

    for (partition = 0; partition < (1 << rice_order); partition++)
    {
        tmp = get_bits(&s->gb, 4);
        if (tmp == 0)
        {
            i = (!rice_order || partition) ? 0 : pred_order;
            for (; i < samples; i++, sample++)
                s->residual[channel][sample] = get_sr_golomb_flac(&s->gb, tmp, 0, 0);
            printf("zero k\n");
        }
        else if (tmp == 15)
        {
            printf("fixed len partition\n");
            tmp = get_bits(&s->gb, 5);
            i = (!rice_order || partition) ? 0 : pred_order;
            for (; i < samples; i++, sample++)
                s->residual[channel][sample] = get_bits(&s->gb, tmp);
        }
        else
        {
//            printf("rice coded partition\n");
#if 1
            i = (!rice_order || partition) ? 0 : pred_order;
            for (; i < samples; i++, sample++)
                s->residual[channel][sample] = get_sr_golomb_flac(&s->gb, tmp, 0, 0);
#else
            i = ((!rice_order || partition) ? samples : samples - pred_order) + sample;
            for (; sample < i; sample++)
                s->residual[channel][sample] = get_ur_golomb(&s->gb, tmp, 0, 0);
//                s->residual[channel][sample] = get_se_golomb(&s->gb);
#endif
        }
    }

    printf("partitions: %d, samples: %d\n", 1 << rice_order, sample);

    return 0;
}    

static int decode_subframe_fixed(FLACContext *s, int channel, int pred_order)
{
    int i;
        
    printf("  SUBFRAME FIXED\n");
        
    /* warm up samples */
    printf("   warm up samples: %d\n", pred_order);
        
    for (i = 0; i < pred_order; i++)
    {
        s->decoded[channel][i] = get_bits(&s->gb, s->curr_bps);
        printf("    %d: %d\n", i, s->decoded[channel][i]);
    }
    
    if (decode_residuals(s, channel, pred_order) < 0)
        return -1;

    switch(pred_order)
    {
        case 0:
            for (i = pred_order; i < s->blocksize; i++)
                s->decoded[channel][i] = s->residual[channel][i];
            break;
        case 1:
            for (i = pred_order; i < s->blocksize; i++)
                s->decoded[channel][i] = s->residual[channel][i] +
                                        s->decoded[channel][i-1];
            break;
        case 2:
            for (i = pred_order; i < s->blocksize; i++)
                s->decoded[channel][i] = s->residual[channel][i] +
                                        (s->decoded[channel][i-1] << 1) -
                                        s->decoded[channel][i-2];
            break;
        case 3:
            for (i = pred_order; i < s->blocksize; i++)
                s->decoded[channel][i] = s->residual[channel][i] +
                                        (((s->decoded[channel][i-1] -
                                        s->decoded[channel][i-2]) << 1) +
                                        (s->decoded[channel][i-1] -
                                        s->decoded[channel][i-2])) +
                                        s->decoded[channel][i-3];
            break;
        case 4:
            for (i = pred_order; i < s->blocksize; i++)
                s->decoded[channel][i] = s->residual[channel][i] +
                                        ((s->decoded[channel][i-1] +
                                        s->decoded[channel][i-3]) << 2) -
                                        ((s->decoded[channel][i-2] << 2) +
                                        (s->decoded[channel][i-2] << 1)) -
                                        s->decoded[channel][i-4];
            break;
    }
    
    return 0;
}

static int decode_subframe_lpc(FLACContext *s, int channel, int pred_order)
{
    int sum, i, j;
    int coeff_prec, qlevel;
    int coeffs[pred_order];
        
    printf("  SUBFRAME LPC\n");
        
    /* warm up samples */
    printf("   warm up samples: %d\n", pred_order);
        
    for (i = 0; i < pred_order; i++)
    {
        s->decoded[channel][i] = get_bits(&s->gb, s->curr_bps);
        printf("    %d: %d\n", i, s->decoded[channel][i]);
    }
    
    coeff_prec = get_bits(&s->gb, 4) + 1;
    if (coeff_prec == 16)
    {
        printf("invalid coeff precision\n");
        return -1;
    }
    printf("   qlp coeff prec: %d\n", coeff_prec);
    qlevel = get_bits(&s->gb, 5);
    printf("   quant level: %d\n", qlevel);
    
    for (i = 0; i < pred_order; i++)
    {
        coeffs[i] = get_bits(&s->gb, coeff_prec);
        printf("    %d: %d\n", i, coeffs[i]);
    }
    
    if (decode_residuals(s, channel, pred_order) < 0)
        return -1;

    for (i = pred_order; i < s->blocksize; i++)
    {
        sum = 0;
        for (j = 0; j < pred_order; j++)
            sum += coeffs[j] * s->decoded[channel][i-j-1];
        s->decoded[channel][i] = s->residual[channel][i] + (sum >> qlevel);
    }
    
    return 0;
}

static inline int decode_subframe(FLACContext *s, int channel)
{
    int type, wasted = 0;
    int i, tmp;
    
    s->curr_bps = s->bps;
    
    if (get_bits1(&s->gb))
    {
        printf("invalid subframe padding\n");
        return -1;
    }
    type = get_bits(&s->gb, 6);
//    wasted = get_bits1(&s->gb);
    
//    if (wasted)
//    {
//        while (!get_bits1(&s->gb))
//            wasted++;
//        if (wasted)
//            wasted++;
//        s->curr_bps -= wasted;
//    }

    if (get_bits1(&s->gb))
    {
        wasted = 1;
        while (!get_bits1(&s->gb))
            wasted++;
        s->curr_bps -= wasted;
    }

    if (type == 0)
    {
        printf("coding type: constant\n");
        tmp = get_bits(&s->gb, s->curr_bps);
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = tmp;
    }
    else if (type == 1)
    {
        printf("coding type: verbatim\n");
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] = get_bits(&s->gb, s->curr_bps);
    }
    else if ((type >= 8) && (type <= 12))
    {
        printf("coding type: fixed\n");
        if (decode_subframe_fixed(s, channel, type & ~0x8) < 0)
            return -1;
    }
    else if (type >= 32)
    {
        printf("coding type: lpc\n");
        if (decode_subframe_lpc(s, channel, (type & ~0x20)+1) < 0)
            return -1;
    }
    else
    {
        printf("invalid coding type\n");
        return -1;
    }
        
    if (wasted)
    {
        int i;
        for (i = 0; i < s->blocksize; i++)
            s->decoded[channel][i] <<= wasted;
    }

    return 0;
}

static int decode_frame(FLACContext *s)
{
    int blocksize_code, sample_rate_code, sample_size_code, assignment, i;
    
    blocksize_code = get_bits(&s->gb, 4);
    if (blocksize_code == 0)
        s->blocksize = s->min_blocksize;
    else if (blocksize_code == 1)
        s->blocksize = 192;
    else if (blocksize_code <= 5)
        s->blocksize = 576 << (blocksize_code - 2);
    else if (blocksize_code >= 8)
        s->blocksize = 256 << (blocksize_code - 8);

    sample_rate_code = get_bits(&s->gb, 4);
    if ((sample_rate_code > 3) && (sample_rate_code < 12))
        s->samplerate = sample_rate_table[sample_rate_code];
    
    assignment = get_bits(&s->gb, 4); /* channel assignment */
    if (assignment < 8)
    {
        s->order = INDEPENDENT;
        if (s->channels != assignment+1)
            printf("channel number and number of assigned channels differ!\n");
        printf("channels: %d\n", assignment+1);
    }
    else if (assignment == 8)
    {
        s->order = LEFT_SIDE;
        printf("left/side\n");
    }
    else if (assignment == 9)
    {
        s->order = RIGHT_SIDE;
        printf("right/side\n");
    }
    else if (assignment == 10)
    {
        s->order = MID_SIDE;
        printf("mid/side\n");
    }
    else
    {
        printf("unsupported channel assignment\n");
        return -1;
    }

    if ((assignment >= 8) && (s->channels != 2))
    {
        return -1;
    }
        
    sample_size_code = get_bits(&s->gb, 3);
    if (s->bps != 0)
        s->bps = sample_size_table[sample_size_code];

    if ((sample_size_code == 3) || (sample_size_code == 7))
    {
        printf("invalid sample size code (%d)\n", sample_size_code);
        return -1;
    }

    if (get_bits1(&s->gb))
    {
        printf("broken stream, invalid padding\n");
//        return -1;
    }
    
    if (((blocksize_code == 6) || (blocksize_code == 7)) &&
        (s->min_blocksize != s->max_blocksize))
    {
        get_uvlc(&s->gb, 1);
    }
    else
        get_uvlc(&s->gb, 0);
    
    if (blocksize_code == 6)
        s->blocksize = get_bits(&s->gb, 8)+1;
    if (blocksize_code == 7)
        s->blocksize = get_bits(&s->gb, 16)+1;

    if ((sample_rate_code > 11) && (sample_rate_code < 15))
    {
        switch(sample_rate_code)
        {
            case 12:
                s->samplerate = get_bits(&s->gb, 8) * 1000;
                break;
            case 13:
                s->samplerate = get_bits(&s->gb, 16);
                break;
            case 14:
                s->samplerate = get_bits(&s->gb, 16) * 10;
                break;
        }
    }

    skip_bits(&s->gb, 8); /* header crc */

    dump_headers(s);

    /* subframes */
    for (i = 0; i < s->channels; i++)
    {
        if (s->blocksize != s->last_blocksize)
        {
            s->decoded[i] = av_realloc(s->decoded[i], sizeof(uint32_t)*s->blocksize);
            s->residual[i] = av_realloc(s->residual[i], sizeof(uint8_t)*s->blocksize);
        }
        printf("decoded: %x residual: %x\n", s->decoded[i], s->residual[i]);
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
                            uint8_t *buf, int buf_size)
{
    FLACContext *s = avctx->priv_data;
    int metadata_flag, metadata_type, metadata_size;
    int tmp = 0, i, j = 0;
    int16_t *samples = data, *left, *right;

    *data_size = 0;

    s->avctx = avctx;

    init_get_bits(&s->gb, buf, buf_size*8);
    
    /* fLaC signature (be) */
    if (get_bits_long(&s->gb, 32) == bswap_32(ff_get_fourcc("fLaC")))
    {
        printf("STREAM HEADER\n");
        do {
            metadata_flag = get_bits(&s->gb, 1);
            metadata_type = get_bits(&s->gb, 7);
            metadata_size = get_bits_long(&s->gb, 24);
            
            printf(" metadata block: flag = %d, type = %d, size = %d\n",
                metadata_flag, metadata_type,
                metadata_size);

            switch(metadata_type)
            {
                case METADATA_TYPE_STREAMINFO:
                    metadata_streaminfo(s);
                    dump_headers(s);
                    break;
                default:
                    while ((metadata_size -= 8) > 0)
                        skip_bits(&s->gb, 8);
            }
        } while(metadata_flag != 1);
    }
    else
    {
        init_get_bits(&s->gb, buf, buf_size*8);
        tmp = get_bits(&s->gb, 16);
        if (tmp == 0xfff8)
            printf("FRAME HEADER\n");

        if (decode_frame(s) < 0)
            return -1;
    }
    
#if 0
    /* fix the channel order here */
    if (s->order == MID_SIDE)
    {
        short *left = samples;
        short *right = samples + s->blocksize;
        for (i = 0; i < s->blocksize; i += 2)
        {
            uint32_t x = s->decoded[0][i];
            uint32_t y = s->decoded[0][i+1];

            right[i] = x - (y / 2);
            left[i] = right[i] + y;
        }
        *data_size = 2 * s->blocksize;
    }
    else
    {
    for (i = 0; i < s->channels; i++)
    {
        switch(s->order)
        {
            case INDEPENDENT:
                for (j = 0; j < s->blocksize; j++)
                    samples[(s->blocksize*i)+j] = s->decoded[i][j];
                break;
            case LEFT_SIDE:
            case RIGHT_SIDE:
                if (i == 0)
                    for (j = 0; j < s->blocksize; j++)
                        samples[(s->blocksize*i)+j] = s->decoded[0][j];
                else
                    for (j = 0; j < s->blocksize; j++)
                        samples[(s->blocksize*i)+j] = s->decoded[0][j] - s->decoded[i][j];
                break;
//            case MID_SIDE:
//                printf("mid-side unsupported\n");
        }
        *data_size += s->blocksize;
    }
    }
#else
    switch(s->order)
    {
        case INDEPENDENT:
            for (i = 0; i < s->channels; i++)
            {
                for (j = 0; j < s->blocksize; j++)
                    *(samples++) = s->decoded[i][j];
                *data_size += s->blocksize;
            }
            break;
        case LEFT_SIDE:
            assert(s->channels == 2);
            for (i = 0; i < s->blocksize; i++)
            {
                *(samples++) = s->decoded[0][i];
                *(samples++) = s->decoded[0][i] - s->decoded[1][i];
            }
            *data_size = 2*s->blocksize;
            break;
        case RIGHT_SIDE:
            assert(s->channels == 2);
            for (i = 0; i < s->blocksize; i++)
            {
                *(samples++) = s->decoded[0][i] + s->decoded[1][i];
                *(samples++) = s->decoded[1][i];
            }
            *data_size = 2*s->blocksize;
            break;
        case MID_SIDE:
            assert(s->channels == 2);
            for (i = 0; i < s->blocksize; i++)
            {
                int16_t mid, side;
                mid = s->decoded[0][i];
                side = s->decoded[1][i];
                
                mid <<= 1;
                if (side & 1)
                    mid++;
                *(samples++) = (mid + side) >> 1;
                *(samples++) = (mid - side) >> 1;
            }
            *data_size = 2*s->blocksize;
            break;
    }
#endif

//    *data_size = (int8_t *)samples - (int8_t *)data;
    printf("data size: %d\n", *data_size);

    s->last_blocksize = s->blocksize;

    return (get_bits_count(&s->gb)+7)/8;
}

static int flac_decode_close(AVCodecContext *avctx)
{
    FLACContext *s = avctx->priv_data;
    int i;
    
    for (i = 0; i < s->channels; i++)
    {
        if (s->decoded[i])
            av_free(s->decoded[i]);
        if (s->residual[i])
            av_free(s->residual[i]);
    }
    
    return 0;
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
};
