/*
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "avcodec.h"
#include "dct.h"
#include "faanidct.h"
#include "idctdsp.h"
#include "simple_idct.h"

av_cold void ff_init_scantable(uint8_t *permutation, ScanTable *st,
                               const uint8_t *src_scantable)
{
    int i, end;

    st->scantable = src_scantable;

    for (i = 0; i < 64; i++) {
        int j = src_scantable[i];
        st->permutated[i] = permutation[j];
    }

    end = -1;
    for (i = 0; i < 64; i++) {
        int j = st->permutated[i];
        if (j > end)
            end = j;
        st->raster_end[i] = end;
    }
}

av_cold void ff_init_scantable_permutation(uint8_t *idct_permutation,
                                           int idct_permutation_type)
{
    int i;

    if (ARCH_X86)
        if (ff_init_scantable_permutation_x86(idct_permutation,
                                              idct_permutation_type))
            return;

    switch (idct_permutation_type) {
    case FF_NO_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = i;
        break;
    case FF_LIBMPEG2_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x38) | ((i & 6) >> 1) | ((i & 1) << 2);
        break;
    case FF_TRANSPOSE_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = ((i & 7) << 3) | (i >> 3);
        break;
    case FF_PARTTRANS_IDCT_PERM:
        for (i = 0; i < 64; i++)
            idct_permutation[i] = (i & 0x24) | ((i & 3) << 3) | ((i >> 3) & 3);
        break;
    default:
        av_log(NULL, AV_LOG_ERROR,
               "Internal error, IDCT permutation not set\n");
    }
}

static void put_pixels_clamped_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);
        pixels[2] = av_clip_uint8(block[2]);
        pixels[3] = av_clip_uint8(block[3]);
        pixels[4] = av_clip_uint8(block[4]);
        pixels[5] = av_clip_uint8(block[5]);
        pixels[6] = av_clip_uint8(block[6]);
        pixels[7] = av_clip_uint8(block[7]);

        pixels += line_size;
        block  += 8;
    }
}

static void put_pixels_clamped4_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<4;i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);
        pixels[2] = av_clip_uint8(block[2]);
        pixels[3] = av_clip_uint8(block[3]);

        pixels += line_size;
        block += 8;
    }
}

static void put_pixels_clamped2_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<2;i++) {
        pixels[0] = av_clip_uint8(block[0]);
        pixels[1] = av_clip_uint8(block[1]);

        pixels += line_size;
        block += 8;
    }
}

static void put_signed_pixels_clamped_c(const int16_t *block,
                                        uint8_t *av_restrict pixels,
                                        int line_size)
{
    int i, j;

    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            if (*block < -128)
                *pixels = 0;
            else if (*block > 127)
                *pixels = 255;
            else
                *pixels = (uint8_t) (*block + 128);
            block++;
            pixels++;
        }
        pixels += (line_size - 8);
    }
}

static void add_pixels_clamped_c(const int16_t *block, uint8_t *av_restrict pixels,
                                 int line_size)
{
    int i;

    /* read the pixels */
    for (i = 0; i < 8; i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels[2] = av_clip_uint8(pixels[2] + block[2]);
        pixels[3] = av_clip_uint8(pixels[3] + block[3]);
        pixels[4] = av_clip_uint8(pixels[4] + block[4]);
        pixels[5] = av_clip_uint8(pixels[5] + block[5]);
        pixels[6] = av_clip_uint8(pixels[6] + block[6]);
        pixels[7] = av_clip_uint8(pixels[7] + block[7]);
        pixels   += line_size;
        block    += 8;
    }
}

static void add_pixels_clamped4_c(const int16_t *block, uint8_t *av_restrict pixels,
                          int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<4;i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels[2] = av_clip_uint8(pixels[2] + block[2]);
        pixels[3] = av_clip_uint8(pixels[3] + block[3]);
        pixels += line_size;
        block += 8;
    }
}

static void add_pixels_clamped2_c(const int16_t *block, uint8_t *av_restrict pixels,
                          int line_size)
{
    int i;

    /* read the pixels */
    for(i=0;i<2;i++) {
        pixels[0] = av_clip_uint8(pixels[0] + block[0]);
        pixels[1] = av_clip_uint8(pixels[1] + block[1]);
        pixels += line_size;
        block += 8;
    }
}

static void jref_idct_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct(block);
    put_pixels_clamped_c(block, dest, line_size);
}

static void jref_idct_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct(block);
    add_pixels_clamped_c(block, dest, line_size);
}
static void ff_jref_idct4_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct4 (block);
    put_pixels_clamped4_c(block, dest, line_size);
}
static void ff_jref_idct4_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct4 (block);
    add_pixels_clamped4_c(block, dest, line_size);
}

static void ff_jref_idct2_put(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct2 (block);
    put_pixels_clamped2_c(block, dest, line_size);
}
static void ff_jref_idct2_add(uint8_t *dest, int line_size, int16_t *block)
{
    ff_j_rev_dct2 (block);
    add_pixels_clamped2_c(block, dest, line_size);
}

static void ff_jref_idct1_put(uint8_t *dest, int line_size, int16_t *block)
{
    dest[0] = av_clip_uint8((block[0] + 4)>>3);
}
static void ff_jref_idct1_add(uint8_t *dest, int line_size, int16_t *block)
{
    dest[0] = av_clip_uint8(dest[0] + ((block[0] + 4)>>3));
}

av_cold void ff_idctdsp_init(IDCTDSPContext *c, AVCodecContext *avctx)
{
    const unsigned high_bit_depth = avctx->bits_per_raw_sample > 8;

    if (avctx->lowres==1) {
        c->idct_put              = ff_jref_idct4_put;
        c->idct_add              = ff_jref_idct4_add;
        c->idct                  = ff_j_rev_dct4;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else if (avctx->lowres==2) {
        c->idct_put              =  ff_jref_idct2_put;
        c->idct_add              =  ff_jref_idct2_add;
        c->idct                  =  ff_j_rev_dct2;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else if (avctx->lowres==3) {
        c->idct_put              =  ff_jref_idct1_put;
        c->idct_add              =  ff_jref_idct1_add;
        c->idct                  =  ff_j_rev_dct1;
        c->idct_permutation_type = FF_NO_IDCT_PERM;
    } else {
        if (avctx->bits_per_raw_sample == 10) {
            c->idct_put              = ff_simple_idct_put_10;
            c->idct_add              = ff_simple_idct_add_10;
            c->idct                  = ff_simple_idct_10;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else if (avctx->bits_per_raw_sample == 12) {
            c->idct_put              = ff_simple_idct_put_12;
            c->idct_add              = ff_simple_idct_add_12;
            c->idct                  = ff_simple_idct_12;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else {
        if (avctx->idct_algo == FF_IDCT_INT) {
            c->idct_put              = jref_idct_put;
            c->idct_add              = jref_idct_add;
            c->idct                  = ff_j_rev_dct;
            c->idct_permutation_type = FF_LIBMPEG2_IDCT_PERM;
        } else if (avctx->idct_algo == FF_IDCT_FAAN) {
            c->idct_put              = ff_faanidct_put;
            c->idct_add              = ff_faanidct_add;
            c->idct                  = ff_faanidct;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        } else { // accurate/default
            c->idct_put              = ff_simple_idct_put_8;
            c->idct_add              = ff_simple_idct_add_8;
            c->idct                  = ff_simple_idct_8;
            c->idct_permutation_type = FF_NO_IDCT_PERM;
        }
        }
    }

    c->put_pixels_clamped        = put_pixels_clamped_c;
    c->put_signed_pixels_clamped = put_signed_pixels_clamped_c;
    c->add_pixels_clamped        = add_pixels_clamped_c;

    if (ARCH_ALPHA)
        ff_idctdsp_init_alpha(c, avctx, high_bit_depth);
    if (ARCH_ARM)
        ff_idctdsp_init_arm(c, avctx, high_bit_depth);
    if (ARCH_PPC)
        ff_idctdsp_init_ppc(c, avctx, high_bit_depth);
    if (ARCH_X86)
        ff_idctdsp_init_x86(c, avctx, high_bit_depth);

    ff_init_scantable_permutation(c->idct_permutation,
                                  c->idct_permutation_type);
}
