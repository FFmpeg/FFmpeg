/*
 * Copyright (c) 2014 Michael Niedermayer <michaelni@gmx.at>
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
#include "idctdsp.h"
#include "fdctdsp.h"
#include "pixblockdsp.h"
#include "avdct.h"

#define OFFSET(x) offsetof(AVDCT,x)
#define DEFAULT 0 //should be NAN but it does not work as it is not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define V AV_OPT_FLAG_VIDEO_PARAM
#define A AV_OPT_FLAG_AUDIO_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption avdct_options[] = {
{"dct", "DCT algorithm", OFFSET(dct_algo), AV_OPT_TYPE_INT, {.i64 = DEFAULT }, 0, INT_MAX, V|E, "dct"},
{"auto", "autoselect a good one", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_AUTO }, INT_MIN, INT_MAX, V|E, "dct"},
{"fastint", "fast integer (experimental / for debugging)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_FASTINT }, INT_MIN, INT_MAX, V|E, "dct"},
{"int", "accurate integer", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_INT }, INT_MIN, INT_MAX, V|E, "dct"},
{"mmx", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_MMX }, INT_MIN, INT_MAX, V|E, "dct"},
{"altivec", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_ALTIVEC }, INT_MIN, INT_MAX, V|E, "dct"},
{"faan", "floating point AAN DCT (experimental / for debugging)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_DCT_FAAN }, INT_MIN, INT_MAX, V|E, "dct"},

{"idct", "select IDCT implementation", OFFSET(idct_algo), AV_OPT_TYPE_INT, {.i64 = DEFAULT }, 0, INT_MAX, V|E|D, "idct"},
{"auto", "autoselect a good one", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_AUTO }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"int", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_INT }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simple", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLE }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplemmx", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLEMMX }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"arm", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_ARM }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"altivec", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_ALTIVEC }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearm", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLEARM }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearmv5te", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLEARMV5TE }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simplearmv6", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLEARMV6 }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"simpleneon", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLENEON }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"xvid", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_XVID }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"xvidmmx", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_XVID }, INT_MIN, INT_MAX, V|E|D, "idct"},
{"faani", "floating point AAN IDCT (experimental / for debugging)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_FAAN }, INT_MIN, INT_MAX, V|D|E, "idct"},
{"simpleauto", "experimental / for debugging", 0, AV_OPT_TYPE_CONST, {.i64 = FF_IDCT_SIMPLEAUTO }, INT_MIN, INT_MAX, V|E|D, "idct"},

{"bits_per_sample", "", OFFSET(bits_per_sample), AV_OPT_TYPE_INT, {.i64 = 8 }, 0, 14, 0,},
{NULL},
};

static const AVClass avdct_class = {
    .class_name              = "AVDCT",
    .option                  = avdct_options,
    .version                 = LIBAVUTIL_VERSION_INT,
};

const AVClass *avcodec_dct_get_class(void)
{
    return &avdct_class;
}

AVDCT *avcodec_dct_alloc(void)
{
    AVDCT *dsp = av_mallocz(sizeof(AVDCT));

    if (!dsp)
        return NULL;

    dsp->av_class = &avdct_class;
    av_opt_set_defaults(dsp);

    return dsp;
}

int avcodec_dct_init(AVDCT *dsp)
{
    AVCodecContext *avctx = avcodec_alloc_context3(NULL);

    if (!avctx)
        return AVERROR(ENOMEM);

    avctx->idct_algo = dsp->idct_algo;
    avctx->dct_algo  = dsp->dct_algo;
    avctx->bits_per_raw_sample = dsp->bits_per_sample;

#define COPY(src, name) memcpy(&dsp->name, &src.name, sizeof(dsp->name))

#if CONFIG_IDCTDSP
    {
        IDCTDSPContext idsp = {0};
        ff_idctdsp_init(&idsp, avctx);
        COPY(idsp, idct);
        COPY(idsp, idct_permutation);
    }
#endif

#if CONFIG_FDCTDSP
    {
        FDCTDSPContext fdsp;
        ff_fdctdsp_init(&fdsp, avctx);
        COPY(fdsp, fdct);
    }
#endif

#if CONFIG_PIXBLOCKDSP
    {
        PixblockDSPContext pdsp;
        ff_pixblockdsp_init(&pdsp, avctx);
        COPY(pdsp, get_pixels);
        COPY(pdsp, get_pixels_unaligned);
    }
#endif

    avcodec_free_context(&avctx);

    return 0;
}
