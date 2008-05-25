/*
 * Real Audio 1.0 (14.4K)
 * Copyright (c) 2003 the ffmpeg project
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

#include "avcodec.h"
#include "bitstream.h"
#include "ra144.h"

#define NBLOCKS         4       /* number of segments within a block */
#define BLOCKSIZE       40      /* (quarter) block size in 16-bit words (80 bytes) */
#define HALFBLOCK       20      /* BLOCKSIZE/2 */
#define BUFFERSIZE      146     /* for do_output */


/* internal globals */
typedef struct {
    unsigned int     oldval;

    /* the swapped buffers */
    unsigned int     swapbuffers[4][10];
    unsigned int    *swapbuf1;
    unsigned int    *swapbuf2;
    unsigned int    *swapbuf1alt;
    unsigned int    *swapbuf2alt;

    unsigned int buffer[5];
    unsigned short int buffer_2[148];
} Real144_internal;

static int ra144_decode_init(AVCodecContext * avctx)
{
    Real144_internal *glob = avctx->priv_data;

    glob->swapbuf1    = glob->swapbuffers[0];
    glob->swapbuf2    = glob->swapbuffers[1];
    glob->swapbuf1alt = glob->swapbuffers[2];
    glob->swapbuf2alt = glob->swapbuffers[3];

    return 0;
}

/* lookup square roots in table */
static int t_sqrt(unsigned int x)
{
    int s = 0;
    while (x > 0xfff) {
        s++;
        x = x >> 2;
    }

    return (ff_sqrt(x << 20) << s) << 2;
}

/* do 'voice' */
static void do_voice(const int *a1, int *a2)
{
    int buffer[10];
    int *b1 = buffer;
    int *b2 = a2;
    int x, y;

    for (x=0; x < 10; x++) {
        b1[x] = a1[x] << 4;

        for (y=0; y < x; y++)
            b1[y] = ((a1[x] * b2[x-y-1]) >> 12) + b2[y];

        FFSWAP(int *, b1, b2);
    }

    for (x=0; x < 10; x++)
        a2[x] >>= 4;
}

/* rotate block */
static void rotate_block(const short *source, short *target, int offset)
{
    int i=0, k=0;
    source += BUFFERSIZE - offset;

    while (i<BLOCKSIZE) {
        target[i++] = source[k++];

        if (k == offset)
            k = 0;
    }
}

/* inverse root mean square */
static int irms(const short *data, int factor)
{
    unsigned int i, sum = 0;

    for (i=0; i < BLOCKSIZE; i++)
        sum += data[i] * data[i];

    if (sum == 0)
        return 0; /* OOPS - division by zero */

    return (0x20000000 / (t_sqrt(sum) >> 8)) * factor;
}

/* multiply/add wavetable */
static void add_wav(int n, int skip_first, int *m, const short *s1,
                    const int8_t *s2, const int8_t *s3, short *dest)
{
    int i;
    int v[3];

    v[0] = 0;
    for (i=!skip_first; i<3; i++)
        v[i] = (wavtable1[n][i] * m[i]) >> (wavtable2[n][i] + 1);

    for (i=0; i < BLOCKSIZE; i++)
        dest[i] = ((*(s1++))*v[0] + (*(s2++))*v[1] + (*(s3++))*v[2]) >> 12;
}


static void final(const short *i1, const short *i2,
                  void *out, int *statbuf, int len)
{
    int x, i;
    unsigned short int work[50];
    short *ptr = work;

    memcpy(work, statbuf,20);
    memcpy(work + 10, i2, len * 2);

    for (i=0; i<len; i++) {
        int sum = 0;
        int new_val;

        for(x=0; x<10; x++)
            sum += i1[9-x] * ptr[x];

        sum >>= 12;

        new_val = ptr[10] - sum;

        if (new_val < -32768 || new_val > 32767) {
            memset(out, 0, len * 2);
            memset(statbuf, 0, 20);
            return;
        }

        ptr[10] = new_val;
        ptr++;
    }

    memcpy(out, work+10, len * 2);
    memcpy(statbuf, work + 40, 20);
}

static unsigned int rms(const int *data, int f)
{
    int x;
    unsigned int res = 0x10000;
    int b = 0;

    for (x=0; x<10; x++) {
        res = (((0x1000000 - (*data) * (*data)) >> 12) * res) >> 12;

        if (res == 0)
            return 0;

        if (res > 0x10000)
            return 0; /* We're screwed, might as well go out with a bang. :P */

        while (res <= 0x3fff) {
            b++;
            res <<= 2;
        }
        data++;
    }

    if (res > 0)
        res = t_sqrt(res);

    res >>= (b + 10);
    res = (res * f) >> 10;
    return res;
}

/* do quarter-block output */
static void do_output_subblock(Real144_internal *glob, const unsigned short  *gsp, unsigned int gval, signed short *output_buffer, GetBitContext *gb)
{
    unsigned short int buffer_a[40];
    unsigned short int *block;
    int a = get_bits(gb, 7);
    int d = get_bits(gb, 8);
    int b = get_bits(gb, 7);
    int c = get_bits(gb, 7);
    int m[3];

    if (a) {
        a += HALFBLOCK - 1;
        rotate_block(glob->buffer_2, buffer_a, a);
    }

    m[1] = ((ftable1[b] >> 4) * gval) >> 8;
    m[2] = ((ftable2[c] >> 4) * gval) >> 8;

    if (a)
        m[0] = irms(buffer_a, gval) >> 12;
    else
        m[0] = 0;

    memmove(glob->buffer_2, glob->buffer_2 + BLOCKSIZE, (BUFFERSIZE - BLOCKSIZE) * 2);
    block = glob->buffer_2 + BUFFERSIZE - BLOCKSIZE;

    add_wav(d, a, m, buffer_a, etable1[b], etable2[c], block);

    final(gsp, block, output_buffer, glob->buffer, BLOCKSIZE);
}

static int dec1(signed short *decsp, const int *data, const int *inp,
                 int f)
{
    int i;

    for (i=0; i<30; i++)
        *(decsp++) = *(inp++);

    return rms(data, f);
}

static int eq(const short *in, int *target)
{
    int retval = 0;
    int b, c, i;
    unsigned int u;
    int buffer1[10];
    int buffer2[10];
    int *bp1 = buffer1;
    int *bp2 = buffer2;

    for (i=0; i < 10; i++)
        buffer2[i] = in[i];

    u = target[9] = bp2[9];

    if (u + 0x1000 > 0x1fff)
        return 0; /* We're screwed, might as well go out with a bang. :P */

    for (c=8; c >= 0; c--) {
        if (u == 0x1000)
            u++;

        if (u == 0xfffff000)
            u--;

        b = 0x1000-((u * u) >> 12);

        if (b == 0)
            b++;

        for (u=0; u<=c; u++)
            bp1[u] = ((bp2[u] - ((target[c+1] * bp2[c-u]) >> 12)) * (0x1000000 / b)) >> 12;

        target[c] = u = bp1[c];

        if ((u + 0x1000) > 0x1fff)
            retval = 1;

        FFSWAP(int *, bp1, bp2);
    }
    return retval;
}

static int dec2(signed short *decsp, const int *data, const int *inp,
                 int f, const int *inp2, int a)
{
    int work[10];
    int b = NBLOCKS - a;
    int x;

    for (x=0; x<30; x++)
        decsp[x] = (a * inp[x] + b * inp2[x]) >> 2;

    if (eq(decsp, work))
        return dec1(decsp, data, inp, f);
    else
        return rms(work, f);
}

/* Uncompress one block (20 bytes -> 160*2 bytes) */
static int ra144_decode_frame(AVCodecContext * avctx,
            void *vdata, int *data_size,
            const uint8_t * buf, int buf_size)
{
    static const uint8_t sizes[10] = {6, 5, 5, 4, 4, 3, 3, 3, 3, 2};
    unsigned int gbuf1[4];
    unsigned short gbuf2[4][30];
    unsigned int a, c;
    int i;
    int16_t *data = vdata;
    unsigned int val;

    Real144_internal *glob = avctx->priv_data;
    GetBitContext gb;

    if(buf_size < 20) {
        av_log(avctx, AV_LOG_ERROR,
               "Frame too small (%d bytes). Truncated file?\n", buf_size);
        return buf_size;
    }
    init_get_bits(&gb, buf, 20 * 8);

    for (i=0; i<10; i++)
        // "<< 1"? Doesn't this make one value out of two of the table useless?
        glob->swapbuf1[i] = decodetable[i][get_bits(&gb, sizes[i]) << 1];

    do_voice(glob->swapbuf1, glob->swapbuf2);

    val = decodeval[get_bits(&gb, 5) << 1]; // Useless table entries?
    a = t_sqrt(val*glob->oldval) >> 12;

    gbuf1[0] = dec2(gbuf2[0], glob->swapbuf1alt, glob->swapbuf2alt, glob->oldval, glob->swapbuf2, 3);
    if (glob->oldval < val) {
        gbuf1[1] = dec2(gbuf2[1], glob->swapbuf1, glob->swapbuf2, a, glob->swapbuf2alt, 2);
    } else {
        gbuf1[1] = dec2(gbuf2[1], glob->swapbuf1alt, glob->swapbuf2alt, a, glob->swapbuf2, 2);
    }
    gbuf1[2] = dec2(gbuf2[2], glob->swapbuf1, glob->swapbuf2, val, glob->swapbuf2alt, 3);
    gbuf1[3] = dec1(gbuf2[3], glob->swapbuf1, glob->swapbuf2, val);

    /* do output */
    for (c=0; c<4; c++) {
        do_output_subblock(glob, gbuf2[c], gbuf1[c], data, &gb);

        for (i=0; i<BLOCKSIZE; i++) {
            *data = av_clip_int16(*data << 2);
            data++;
        }
    }

    glob->oldval = val;

    FFSWAP(unsigned int *, glob->swapbuf1alt, glob->swapbuf1);
    FFSWAP(unsigned int *, glob->swapbuf2alt, glob->swapbuf2);

    *data_size = 2*160;
    return 20;
}


AVCodec ra_144_decoder =
{
    "real_144",
    CODEC_TYPE_AUDIO,
    CODEC_ID_RA_144,
    sizeof(Real144_internal),
    ra144_decode_init,
    NULL,
    NULL,
    ra144_decode_frame,
    .long_name = "RealAudio 1.0 (14.4K)",
};
