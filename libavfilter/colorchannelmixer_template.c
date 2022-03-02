/*
 * Copyright (c) 2013 Paul B Mahol
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

#include <float.h>

#undef pixel
#undef cpixel
#undef ROUND
#if DEPTH == 8
#define pixel uint8_t
#define cpixel int
#define ROUND lrintf
#elif DEPTH == 16
#define pixel uint16_t
#define cpixel int
#define ROUND lrintf
#else
#define NOP(x) (x)
#define pixel float
#define cpixel float
#define ROUND NOP
#endif

#undef fn
#undef fn2
#undef fn3
#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, DEPTH)

static av_always_inline int fn(filter_slice_rgba_planar)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs,
                                                         int have_alpha, int depth, int pc)
{
    ColorChannelMixerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const float pa = s->preserve_amount;
    const float max = (1 << depth) - 1;
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const pixel *srcg = (const pixel *)(in->data[0] + slice_start * in->linesize[0]);
    const pixel *srcb = (const pixel *)(in->data[1] + slice_start * in->linesize[1]);
    const pixel *srcr = (const pixel *)(in->data[2] + slice_start * in->linesize[2]);
    const pixel *srca = (const pixel *)(in->data[3] + slice_start * in->linesize[3]);
    pixel *dstg = (pixel *)(out->data[0] + slice_start * out->linesize[0]);
    pixel *dstb = (pixel *)(out->data[1] + slice_start * out->linesize[1]);
    pixel *dstr = (pixel *)(out->data[2] + slice_start * out->linesize[2]);
    pixel *dsta = (pixel *)(out->data[3] + slice_start * out->linesize[3]);

    for (int i = slice_start; i < slice_end; i++) {
        for (int j = 0; j < out->width; j++) {
            const pixel rin = srcr[j];
            const pixel gin = srcg[j];
            const pixel bin = srcb[j];
            const pixel ain = have_alpha ? srca[j] : 0;
            cpixel rout, gout, bout;

#if DEPTH == 32
            rout = s->rr * rin +
                   s->rg * gin +
                   s->rb * bin +
                   (have_alpha == 1 ? s->ra * ain : 0);
            gout = s->gr * rin +
                   s->gg * gin +
                   s->gb * bin +
                   (have_alpha == 1 ? s->ga * ain : 0);
            bout = s->br * rin +
                   s->bg * gin +
                   s->bb * bin +
                   (have_alpha == 1 ? s->ba * ain : 0);
#else
            rout = s->lut[R][R][rin] +
                   s->lut[R][G][gin] +
                   s->lut[R][B][bin] +
                   (have_alpha == 1 ? s->lut[R][A][ain] : 0);
            gout = s->lut[G][R][rin] +
                   s->lut[G][G][gin] +
                   s->lut[G][B][bin] +
                   (have_alpha == 1 ? s->lut[G][A][ain] : 0);
            bout = s->lut[B][R][rin] +
                   s->lut[B][G][gin] +
                   s->lut[B][B][bin] +
                   (have_alpha == 1 ? s->lut[B][A][ain] : 0);
#endif

            if (pc) {
                float frout, fgout, fbout, lin, lout;

#if DEPTH < 32
                frout = av_clipf(rout, 0.f, max);
                fgout = av_clipf(gout, 0.f, max);
                fbout = av_clipf(bout, 0.f, max);
#else
                frout = rout;
                fgout = gout;
                fbout = bout;
#endif

                preserve_color(s->preserve_color, rin, gin, bin,
                               rout, gout, bout, max, &lin, &lout);
                preservel(&frout, &fgout, &fbout, lin, lout, max);

                rout = ROUND(lerpf(rout, frout, pa));
                gout = ROUND(lerpf(gout, fgout, pa));
                bout = ROUND(lerpf(bout, fbout, pa));
            }

#if DEPTH < 32
            dstr[j] = av_clip_uintp2(rout, depth);
            dstg[j] = av_clip_uintp2(gout, depth);
            dstb[j] = av_clip_uintp2(bout, depth);
#else
            dstr[j] = rout;
            dstg[j] = gout;
            dstb[j] = bout;
#endif

            if (have_alpha == 1) {
#if DEPTH < 32
                dsta[j] = av_clip_uintp2(s->lut[A][R][rin] +
                                         s->lut[A][G][gin] +
                                         s->lut[A][B][bin] +
                                         s->lut[A][A][ain], depth);
#else
                dsta[j] = s->ar * rin +
                          s->ag * gin +
                          s->ab * bin +
                          s->aa * ain;
#endif
            }
        }

        srcg += in->linesize[0] / sizeof(pixel);
        srcb += in->linesize[1] / sizeof(pixel);
        srcr += in->linesize[2] / sizeof(pixel);
        srca += in->linesize[3] / sizeof(pixel);
        dstg += out->linesize[0] / sizeof(pixel);
        dstb += out->linesize[1] / sizeof(pixel);
        dstr += out->linesize[2] / sizeof(pixel);
        dsta += out->linesize[3] / sizeof(pixel);
    }

    return 0;
}

#if DEPTH < 32

static av_always_inline int fn(filter_slice_rgba_packed)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs,
                                                         int have_alpha, int step, int pc, int depth)
{
    ColorChannelMixerContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *in = td->in;
    AVFrame *out = td->out;
    const float pa = s->preserve_amount;
    const float max = (1 << depth) - 1;
    const int slice_start = (out->height * jobnr) / nb_jobs;
    const int slice_end = (out->height * (jobnr+1)) / nb_jobs;
    const uint8_t roffset = s->rgba_map[R];
    const uint8_t goffset = s->rgba_map[G];
    const uint8_t boffset = s->rgba_map[B];
    const uint8_t aoffset = s->rgba_map[A];
    const uint8_t *srcrow = in->data[0] + slice_start * in->linesize[0];
    uint8_t *dstrow = out->data[0] + slice_start * out->linesize[0];
    int i, j;

    for (i = slice_start; i < slice_end; i++) {
        const pixel *src = (const pixel *)srcrow;
        pixel *dst = (pixel *)dstrow;

        for (j = 0; j < out->width * step; j += step) {
            const pixel rin = src[j + roffset];
            const pixel gin = src[j + goffset];
            const pixel bin = src[j + boffset];
            const pixel ain = src[j + aoffset];
            int rout, gout, bout;

            rout = s->lut[R][R][rin] +
                   s->lut[R][G][gin] +
                   s->lut[R][B][bin] +
                   (have_alpha == 1 ? s->lut[R][A][ain] : 0);
            gout = s->lut[G][R][rin] +
                   s->lut[G][G][gin] +
                   s->lut[G][B][bin] +
                   (have_alpha == 1 ? s->lut[G][A][ain] : 0);
            bout = s->lut[B][R][rin] +
                   s->lut[B][G][gin] +
                   s->lut[B][B][bin] +
                   (have_alpha == 1 ? s->lut[B][A][ain] : 0);

            if (pc) {
                float frout = av_clipf(rout, 0.f, max);
                float fgout = av_clipf(gout, 0.f, max);
                float fbout = av_clipf(bout, 0.f, max);
                float lin, lout;

                preserve_color(s->preserve_color, rin, gin, bin,
                               rout, gout, bout, max, &lin, &lout);
                preservel(&frout, &fgout, &fbout, lin, lout, max);

                rout = lrintf(lerpf(rout, frout, pa));
                gout = lrintf(lerpf(gout, fgout, pa));
                bout = lrintf(lerpf(bout, fbout, pa));
            }

            dst[j + roffset] = av_clip_uintp2(rout, depth);
            dst[j + goffset] = av_clip_uintp2(gout, depth);
            dst[j + boffset] = av_clip_uintp2(bout, depth);

            if (have_alpha == 1) {
                dst[j + aoffset] = av_clip_uintp2(s->lut[A][R][rin] +
                                                  s->lut[A][G][gin] +
                                                  s->lut[A][B][bin] +
                                                  s->lut[A][A][ain], depth);
            }
        }

        srcrow += in->linesize[0];
        dstrow += out->linesize[0];
    }

    return 0;
}

#endif
