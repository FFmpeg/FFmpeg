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
    unsigned int     gbuf1[4];
    unsigned short   gbuf2[4][30];
    unsigned int    *decptr;                /* decoder ptr */

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
static void add_wav(int n, int f, int m1, int m2, int m3, const short *s1,
                    const short *s2, const short *s3, short *dest)
{
    int a = 0;
    int b, c, i;
    const short *ptr, *ptr2;

    ptr  = wavtable1[n];
    ptr2 = wavtable2[n];

    if (f)
        a = (ptr[0] * m1) >> (ptr2[0] + 1);

    b = (ptr[1] * m2) >> (ptr2[1] + 1);
    c = (ptr[2] * m3) >> (ptr2[2] + 1);

    for (i=0; i < BLOCKSIZE; i++)
        dest[i] = ((*(s1++)) * a + (*(s2++)) * b + (*(s3++)) * c) >> 12;
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

        for(x=0; x<10; x++)
            sum += i1[9-x] * ptr[x];

        sum >>= 12;

        if (ptr[10] - sum < -32768 || ptr[10] - sum > 32767) {
            memset(out, 0, len * 2);
            memset(statbuf, 0, 20);
            return;
        }

        ptr[10] -= sum;
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
    int e, f, g;
    int a = get_bits(gb, 7);
    int d = get_bits(gb, 8);
    int b = get_bits(gb, 7);
    int c = get_bits(gb, 7);

    if (a) {
        a += HALFBLOCK - 1;
        rotate_block(glob->buffer_2, buffer_a, a);
    }

    e = ((ftable1[b] >> 4) * gval) >> 8;
    f = ((ftable2[c] >> 4) * gval) >> 8;

    if (a)
        g = irms(buffer_a, gval) >> 12;
    else
        g = 0;

    memmove(glob->buffer_2, glob->buffer_2 + BLOCKSIZE, (BUFFERSIZE - BLOCKSIZE) * 2);
    block = glob->buffer_2 + BUFFERSIZE - BLOCKSIZE;

    add_wav(d, a, g, e, f, buffer_a, etable1[b],
            etable2[c], block);

    final(gsp, block, output_buffer, glob->buffer, BLOCKSIZE);
}

static void dec1(Real144_internal *glob, const int *data, const int *inp,
                 int n, int f, int block_idx)
{
    short *ptr,*end;
    signed   short  *decsp = glob->gbuf2[block_idx];

     *(glob->decptr++) = rms(data, f);
    end = (ptr = decsp) + (n * 10);

    while (ptr < end)
        *(ptr++) = *(inp++);
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

static void dec2(Real144_internal *glob, const int *data, const int *inp,
                 int n, int f, const int *inp2, int l)
{
    unsigned const int *ptr1,*ptr2;
    int work[10];
    int a,b;
    int x;
    int result;
    signed   short *decsp = glob->gbuf2[l];
    unsigned short *sptr  = decsp;

    if(l + 1 < NBLOCKS / 2)
        a = NBLOCKS - (l + 1);
    else
        a = l + 1;

    b = NBLOCKS - a;

    if (l == 0) {
        glob->decptr = glob->gbuf1;
    }
    ptr1 = inp;
    ptr2 = inp2;

    for (x=0; x<10*n; x++)
        *(sptr++) = (a * (*ptr1++) + b * (*ptr2++)) >> 2;

    result = eq(decsp, work);

    if (result == 1) {
        dec1(glob, data, inp, n, f, l);
    } else {
        *(glob->decptr++) = rms(work, f);
    }
}

/* Uncompress one block (20 bytes -> 160*2 bytes) */
static int ra144_decode_frame(AVCodecContext * avctx,
            void *vdata, int *data_size,
            const uint8_t * buf, int buf_size)
{
    static const uint8_t sizes[10] = {6, 5, 5, 4, 4, 3, 3, 3, 3, 2};
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

    dec2(glob, glob->swapbuf1alt, glob->swapbuf2alt, 3, glob->oldval, glob->swapbuf2, 0);
    if (glob->oldval < val) {
        dec2(glob, glob->swapbuf1, glob->swapbuf2, 3, a, glob->swapbuf2alt, 1);
    } else {
        dec2(glob, glob->swapbuf1alt, glob->swapbuf2alt, 3, a, glob->swapbuf2, 1);
    }
    dec2(glob, glob->swapbuf1, glob->swapbuf2, 3, val, glob->swapbuf2alt, 2);
    dec1(glob, glob->swapbuf1, glob->swapbuf2, 3, val, 3);

    /* do output */
    for (c=0; c<4; c++) {
        do_output_subblock(glob, glob->gbuf2[c], glob->gbuf1[c], data, &gb);

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
