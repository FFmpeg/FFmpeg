/*
 * Copyright (c) 2016 Ronald S. Bultje <rsbultje@gmail.com>
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
 * @file Colorspace functions for libavutil
 * @author Ronald S. Bultje <rsbultje@gmail.com>
 * @author Leo Izen <leo.izen@gmail.com>
 */

#include <stdlib.h>

#include "attributes.h"
#include "csp.h"
#include "pixfmt.h"
#include "rational.h"

#define AVR(d) { (int)(d * 100000 + 0.5), 100000 }

/*
 * All constants explained in e.g. https://linuxtv.org/downloads/v4l-dvb-apis/ch02s06.html
 * The older ones (bt470bg/m) are also explained in their respective ITU docs
 * (e.g. https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.470-5-199802-S!!PDF-E.pdf)
 * whereas the newer ones can typically be copied directly from wikipedia :)
 */
static const struct AVLumaCoefficients luma_coefficients[AVCOL_SPC_NB] = {
    [AVCOL_SPC_FCC]        = { AVR(0.30),   AVR(0.59),   AVR(0.11)   },
    [AVCOL_SPC_BT470BG]    = { AVR(0.299),  AVR(0.587),  AVR(0.114)  },
    [AVCOL_SPC_SMPTE170M]  = { AVR(0.299),  AVR(0.587),  AVR(0.114)  },
    [AVCOL_SPC_BT709]      = { AVR(0.2126), AVR(0.7152), AVR(0.0722) },
    [AVCOL_SPC_SMPTE240M]  = { AVR(0.212),  AVR(0.701),  AVR(0.087)  },
    [AVCOL_SPC_YCOCG]      = { AVR(0.25),   AVR(0.5),    AVR(0.25)   },
    [AVCOL_SPC_RGB]        = { AVR(1),      AVR(1),      AVR(1)      },
    [AVCOL_SPC_BT2020_NCL] = { AVR(0.2627), AVR(0.6780), AVR(0.0593) },
    [AVCOL_SPC_BT2020_CL]  = { AVR(0.2627), AVR(0.6780), AVR(0.0593) },
};

const struct AVLumaCoefficients *av_csp_luma_coeffs_from_avcsp(enum AVColorSpace csp)
{
    const AVLumaCoefficients *coeffs;

    if (csp >= AVCOL_SPC_NB)
        return NULL;
    coeffs = &luma_coefficients[csp];
    if (!coeffs->cr.num)
        return NULL;

    return coeffs;
}

#define WP_D65 { AVR(0.3127), AVR(0.3290) }
#define WP_C   { AVR(0.3100), AVR(0.3160) }
#define WP_DCI { AVR(0.3140), AVR(0.3510) }
#define WP_E   { {1, 3}, {1, 3} }

static const AVColorPrimariesDesc color_primaries[AVCOL_PRI_NB] = {
    [AVCOL_PRI_BT709]     = { WP_D65, { { AVR(0.640), AVR(0.330) }, { AVR(0.300), AVR(0.600) }, { AVR(0.150), AVR(0.060) } } },
    [AVCOL_PRI_BT470M]    = { WP_C,   { { AVR(0.670), AVR(0.330) }, { AVR(0.210), AVR(0.710) }, { AVR(0.140), AVR(0.080) } } },
    [AVCOL_PRI_BT470BG]   = { WP_D65, { { AVR(0.640), AVR(0.330) }, { AVR(0.290), AVR(0.600) }, { AVR(0.150), AVR(0.060) } } },
    [AVCOL_PRI_SMPTE170M] = { WP_D65, { { AVR(0.630), AVR(0.340) }, { AVR(0.310), AVR(0.595) }, { AVR(0.155), AVR(0.070) } } },
    [AVCOL_PRI_SMPTE240M] = { WP_D65, { { AVR(0.630), AVR(0.340) }, { AVR(0.310), AVR(0.595) }, { AVR(0.155), AVR(0.070) } } },
    [AVCOL_PRI_SMPTE428]  = { WP_E,   { { AVR(0.735), AVR(0.265) }, { AVR(0.274), AVR(0.718) }, { AVR(0.167), AVR(0.009) } } },
    [AVCOL_PRI_SMPTE431]  = { WP_DCI, { { AVR(0.680), AVR(0.320) }, { AVR(0.265), AVR(0.690) }, { AVR(0.150), AVR(0.060) } } },
    [AVCOL_PRI_SMPTE432]  = { WP_D65, { { AVR(0.680), AVR(0.320) }, { AVR(0.265), AVR(0.690) }, { AVR(0.150), AVR(0.060) } } },
    [AVCOL_PRI_FILM]      = { WP_C,   { { AVR(0.681), AVR(0.319) }, { AVR(0.243), AVR(0.692) }, { AVR(0.145), AVR(0.049) } } },
    [AVCOL_PRI_BT2020]    = { WP_D65, { { AVR(0.708), AVR(0.292) }, { AVR(0.170), AVR(0.797) }, { AVR(0.131), AVR(0.046) } } },
    [AVCOL_PRI_JEDEC_P22] = { WP_D65, { { AVR(0.630), AVR(0.340) }, { AVR(0.295), AVR(0.605) }, { AVR(0.155), AVR(0.077) } } },
};

const AVColorPrimariesDesc *av_csp_primaries_desc_from_id(enum AVColorPrimaries prm)
{
    const AVColorPrimariesDesc *p;

    if (prm >= AVCOL_PRI_NB)
        return NULL;
    p = &color_primaries[prm];
    if (!p->prim.r.x.num)
        return NULL;

    return p;
}

static av_always_inline AVRational abs_sub_q(AVRational r1, AVRational r2)
{
    AVRational diff = av_sub_q(r1, r2);
    /* denominator assumed to be positive */
    return av_make_q(abs(diff.num), diff.den);
}

enum AVColorPrimaries av_csp_primaries_id_from_desc(const AVColorPrimariesDesc *prm)
{
    AVRational delta;

    for (enum AVColorPrimaries p = 0; p < AVCOL_PRI_NB; p++) {
        const AVColorPrimariesDesc *ref = &color_primaries[p];
        if (!ref->prim.r.x.num)
            continue;

        delta = abs_sub_q(prm->prim.r.x, ref->prim.r.x);
        delta = av_add_q(delta, abs_sub_q(prm->prim.r.y, ref->prim.r.y));
        delta = av_add_q(delta, abs_sub_q(prm->prim.g.x, ref->prim.g.x));
        delta = av_add_q(delta, abs_sub_q(prm->prim.g.y, ref->prim.g.y));
        delta = av_add_q(delta, abs_sub_q(prm->prim.b.x, ref->prim.b.x));
        delta = av_add_q(delta, abs_sub_q(prm->prim.b.y, ref->prim.b.y));
        delta = av_add_q(delta, abs_sub_q(prm->wp.x, ref->wp.x));
        delta = av_add_q(delta, abs_sub_q(prm->wp.y, ref->wp.y));

        if (av_cmp_q(delta, av_make_q(1, 1000)) < 0)
            return p;
    }

    return AVCOL_PRI_UNSPECIFIED;
}
