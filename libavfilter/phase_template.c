/*
 * Copyright (c) 2004 Ville Saari
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avassert.h"
#include "avfilter.h"
#include "video.h"

#undef pixel
#undef accumulator
#if DEPTH == 8
#define pixel uint8_t
#define accumulator int
#else
#define pixel uint16_t
#define accumulator int64_t
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, DEPTH)

/*
 * This macro interpolates the value of both fields at a point halfway
 * between lines and takes the squared difference. In field resolution
 * the point is a quarter pixel below a line in one field and a quarter
 * pixel above a line in other.
 *
 * (The result is actually multiplied by 25)
 */
#define DIFF(a, as, b, bs) ((t) = ((*(a) - (b)[bs]) * 4) + (a)[(as) * 2] - (b)[-(bs)], (t) * (t))

/*
 * Find which field combination has the smallest average squared difference
 * between the fields.
 */
static enum PhaseMode fn(analyze_plane)(void *ctx, enum PhaseMode mode, AVFrame *old, AVFrame *new)
{
    double bdiff, tdiff, pdiff;

    if (mode == AUTO) {
        mode = (new->flags & AV_FRAME_FLAG_INTERLACED) ? (new->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ?
               TOP_FIRST : BOTTOM_FIRST : PROGRESSIVE;
    } else if (mode == AUTO_ANALYZE) {
        mode = (new->flags & AV_FRAME_FLAG_INTERLACED) ? (new->flags & AV_FRAME_FLAG_TOP_FIELD_FIRST) ?
               TOP_FIRST_ANALYZE : BOTTOM_FIRST_ANALYZE : FULL_ANALYZE;
    }

    if (mode <= BOTTOM_FIRST) {
        bdiff = pdiff = tdiff = 65536.0;
    } else {
        const double factor = 1. / (25. * (1 << (DEPTH - 8)) * (1 << (DEPTH - 8)));
        const int ns = new->linesize[0] / sizeof(pixel);
        const int os = old->linesize[0] / sizeof(pixel);
        const pixel *nptr = (pixel *)new->data[0];
        const pixel *optr = (pixel *)old->data[0];
        const int h = new->height;
        const int w = new->width;
        accumulator bdif, tdif, pdif;
        double scale;

        int top = 0, t;
        const pixel *rend, *end = nptr + (h - 2) * ns;

        bdiff = pdiff = tdiff = 0.0;

        nptr += ns;
        optr += os;
        while (nptr < end) {
            pdif = tdif = bdif = 0;

            switch (mode) {
            case TOP_FIRST_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(nptr, ns, optr, os);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            case BOTTOM_FIRST_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(nptr, ns, optr, os);
                    }
                }
                break;
            case ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        tdif += DIFF(nptr, ns, optr, os);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        bdif += DIFF(nptr, ns, optr, os);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            case FULL_ANALYZE:
                if (top) {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        tdif += DIFF(nptr, ns, optr, os);
                        bdif += DIFF(optr, os, nptr, ns);
                    }
                } else {
                    for (rend = nptr + w; nptr < rend; nptr++, optr++) {
                        pdif += DIFF(nptr, ns, nptr, ns);
                        bdif += DIFF(nptr, ns, optr, os);
                        tdif += DIFF(optr, os, nptr, ns);
                    }
                }
                break;
            default:
                av_assert0(0);
            }

            pdiff += (double)pdif;
            tdiff += (double)tdif;
            bdiff += (double)bdif;
            nptr += ns - w;
            optr += os - w;
            top ^= 1;
        }

        scale = 1.0 / (w * (h - 3)) * factor;
        pdiff *= scale;
        tdiff *= scale;
        bdiff *= scale;

        if (mode == TOP_FIRST_ANALYZE) {
            bdiff = 65536.0;
        } else if (mode == BOTTOM_FIRST_ANALYZE) {
            tdiff = 65536.0;
        } else if (mode == ANALYZE) {
            pdiff = 65536.0;
        }

        if (bdiff < pdiff && bdiff < tdiff) {
            mode = BOTTOM_FIRST;
        } else if (tdiff < pdiff && tdiff < bdiff) {
            mode = TOP_FIRST;
        } else {
            mode = PROGRESSIVE;
        }
    }

    av_log(ctx, AV_LOG_DEBUG, "mode=%c tdiff=%f bdiff=%f pdiff=%f\n",
           mode == BOTTOM_FIRST ? 'b' : mode == TOP_FIRST ? 't' : 'p',
           tdiff, bdiff, pdiff);
    return mode;
}
