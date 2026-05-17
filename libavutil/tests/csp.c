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

#include <stdio.h>

#include "libavutil/csp.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

static void print_cie(const char *label, AVCIExy xy)
{
    printf("  %s=(%d/%d, %d/%d)\n", label,
           xy.x.num, xy.x.den, xy.y.num, xy.y.den);
}

static int cie_eq(AVCIExy a, AVCIExy b)
{
    return av_cmp_q(a.x, b.x) == 0 && av_cmp_q(a.y, b.y) == 0;
}

static int desc_eq(const AVColorPrimariesDesc *a, const AVColorPrimariesDesc *b)
{
    return cie_eq(a->wp,     b->wp)     &&
           cie_eq(a->prim.r, b->prim.r) &&
           cie_eq(a->prim.g, b->prim.g) &&
           cie_eq(a->prim.b, b->prim.b);
}

int main(void)
{
    /* av_csp_luma_coeffs_from_avcsp: iterate every defined colorspace */
    printf("Testing av_csp_luma_coeffs_from_avcsp()\n");
    for (enum AVColorSpace csp = 0; csp < AVCOL_SPC_NB; csp++) {
        const AVLumaCoefficients *c = av_csp_luma_coeffs_from_avcsp(csp);
        const char *name = av_color_space_name(csp);
        if (!c) {
            printf("csp=%-16s -> NULL\n", name ? name : "?");
            continue;
        }
        printf("csp=%-16s -> cr=%d/%d cg=%d/%d cb=%d/%d\n",
               name ? name : "?",
               c->cr.num, c->cr.den,
               c->cg.num, c->cg.den,
               c->cb.num, c->cb.den);
    }
    /* out-of-range enum */
    printf("csp=AVCOL_SPC_NB         -> %s\n",
           av_csp_luma_coeffs_from_avcsp(AVCOL_SPC_NB) ? "FAIL" : "NULL");

    /* av_csp_primaries_desc_from_id + av_csp_primaries_id_from_desc */
    printf("\nTesting av_csp_primaries_desc_from_id() round trip\n");
    for (enum AVColorPrimaries prm = 0; prm < AVCOL_PRI_EXT_NB; prm++) {
        const AVColorPrimariesDesc *d;
        const AVColorPrimariesDesc *d_back;
        enum AVColorPrimaries back;
        const char *name, *back_name, *status;

        if (prm == AVCOL_PRI_NB)
            prm = AVCOL_PRI_EXT_BASE;

        d    = av_csp_primaries_desc_from_id(prm);
        name = av_color_primaries_name(prm);
        if (!d) {
            printf("prm=%-16s -> NULL\n", name ? name : "?");
            continue;
        }
        printf("prm=%-16s ->\n", name ? name : "?");
        print_cie("wp ", d->wp);
        print_cie("r  ", d->prim.r);
        print_cie("g  ", d->prim.g);
        print_cie("b  ", d->prim.b);

        /* For colorspaces with identical primaries (e.g. smpte170m and
         * smpte240m), the canonical first match may differ from the input
         * enum, so compare descs not enums.  id_from_desc only searches
         * the base AVCOL_PRI_* range and returns UNSPECIFIED for
         * extended-range inputs that have no base match. */
        back      = av_csp_primaries_id_from_desc(d);
        d_back    = av_csp_primaries_desc_from_id(back);
        back_name = av_color_primaries_name(back);
        if (back == AVCOL_PRI_UNSPECIFIED)
            status = prm >= AVCOL_PRI_EXT_BASE ? "ext-no-base-match"
                                               : "MISMATCH";
        else
            status = d_back && desc_eq(d, d_back) ? "OK" : "MISMATCH";
        printf("  round-trip id=%-16s desc=%s\n",
               back_name ? back_name : "?", status);
    }
    /* out-of-range enum */
    printf("prm=AVCOL_PRI_NB         -> %s\n",
           av_csp_primaries_desc_from_id(AVCOL_PRI_NB) ? "FAIL" : "NULL");

    /* id_from_desc on a garbage description returns UNSPECIFIED */
    {
        AVColorPrimariesDesc garbage = {
            .wp   = { { 0, 1 }, { 0, 1 } },
            .prim = {
                .r = { { 0, 1 }, { 0, 1 } },
                .g = { { 0, 1 }, { 0, 1 } },
                .b = { { 0, 1 }, { 0, 1 } },
            },
        };
        enum AVColorPrimaries back = av_csp_primaries_id_from_desc(&garbage);
        printf("garbage desc -> %s (expect AVCOL_PRI_UNSPECIFIED)\n",
               av_color_primaries_name(back) ? av_color_primaries_name(back) : "?");
    }

    /* av_csp_approximate_trc_gamma + av_csp_approximate_eotf_gamma:
     * both return values from static tables, so output is bitexact. */
    printf("\nTesting av_csp_approximate_{trc,eotf}_gamma()\n");
    for (enum AVColorTransferCharacteristic trc = 0; trc < AVCOL_TRC_EXT_NB; trc++) {
        const char *name;
        double g_trc, g_eotf;

        if (trc == AVCOL_TRC_NB)
            trc = AVCOL_TRC_EXT_BASE;

        name   = av_color_transfer_name(trc);
        g_trc  = av_csp_approximate_trc_gamma(trc);
        g_eotf = av_csp_approximate_eotf_gamma(trc);
        printf("trc=%-16s trc_gamma=%.6f eotf_gamma=%.6f\n",
               name ? name : "?", g_trc, g_eotf);
    }
    /* out-of-range enum: both must return 0.0 */
    printf("trc=AVCOL_TRC_NB         trc_gamma=%.6f eotf_gamma=%.6f\n",
           av_csp_approximate_trc_gamma(AVCOL_TRC_NB),
           av_csp_approximate_eotf_gamma(AVCOL_TRC_NB));

    return 0;
}
