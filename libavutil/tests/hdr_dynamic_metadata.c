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

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/hdr_dynamic_metadata.h"
#include "libavutil/mem.h"
#include "libavutil/rational.h"

/* Denominators av_dynamic_hdr_plus_to_t35() quantises to. The fixtures below
 * express every rational in these terms, so a correct round trip reproduces
 * the input exactly and the decoded values printed here can be compared
 * against the fixture directly. */
#define LUMINANCE_DEN          1
#define PEAK_LUMINANCE_DEN    15
#define RGB_DEN           100000
#define FRACTION_PIXEL_DEN  1000
#define KNEE_POINT_DEN      4095
#define BEZIER_ANCHOR_DEN   1023
#define SATURATION_DEN         8

/*
 * Printers. Every field a fixture populates is printed, so that a serializer
 * or parser which drops or corrupts a value changes the FATE reference
 * instead of silently passing a flag-only check.
 */

static void print_peak_matrix(const char *label, int rows, int cols,
                              const AVRational m[25][25])
{
    printf("    %s %dx%d:", label, rows, cols);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            printf(" %d/%d", m[i][j].num, m[i][j].den);
    printf("\n");
}

static void print_window(int w, const AVHDRPlusColorTransformParams *p,
                         int geometry)
{
    printf("  window %d:\n", w);
    /* Window 0 covers the whole frame; its geometry is not serialized. */
    if (geometry) {
        printf("    corners: ul=%d/%d,%d/%d lr=%d/%d,%d/%d\n",
               p->window_upper_left_corner_x.num,
               p->window_upper_left_corner_x.den,
               p->window_upper_left_corner_y.num,
               p->window_upper_left_corner_y.den,
               p->window_lower_right_corner_x.num,
               p->window_lower_right_corner_x.den,
               p->window_lower_right_corner_y.num,
               p->window_lower_right_corner_y.den);
        printf("    ellipse: center=%u,%u rotation=%u semimajor_int=%u"
               " semimajor_ext=%u semiminor_ext=%u overlap=%u\n",
               p->center_of_ellipse_x, p->center_of_ellipse_y,
               p->rotation_angle, p->semimajor_axis_internal_ellipse,
               p->semimajor_axis_external_ellipse,
               p->semiminor_axis_external_ellipse,
               p->overlap_process_option);
    }
    printf("    maxscl: %d/%d %d/%d %d/%d average_maxrgb=%d/%d\n",
           p->maxscl[0].num, p->maxscl[0].den,
           p->maxscl[1].num, p->maxscl[1].den,
           p->maxscl[2].num, p->maxscl[2].den,
           p->average_maxrgb.num, p->average_maxrgb.den);
    printf("    distribution_maxrgb: n=%u",
           p->num_distribution_maxrgb_percentiles);
    for (int i = 0; i < p->num_distribution_maxrgb_percentiles; i++)
        printf(" %u%%=%d/%d", p->distribution_maxrgb[i].percentage,
               p->distribution_maxrgb[i].percentile.num,
               p->distribution_maxrgb[i].percentile.den);
    printf("\n    fraction_bright_pixels=%d/%d\n",
           p->fraction_bright_pixels.num, p->fraction_bright_pixels.den);
    printf("    tone_mapping_flag=%u", p->tone_mapping_flag);
    if (p->tone_mapping_flag) {
        printf(" knee=%d/%d,%d/%d anchors=%u:",
               p->knee_point_x.num, p->knee_point_x.den,
               p->knee_point_y.num, p->knee_point_y.den,
               p->num_bezier_curve_anchors);
        for (int i = 0; i < p->num_bezier_curve_anchors; i++)
            printf(" %d/%d", p->bezier_curve_anchors[i].num,
                   p->bezier_curve_anchors[i].den);
    }
    printf("\n    color_saturation_mapping_flag=%u",
           p->color_saturation_mapping_flag);
    if (p->color_saturation_mapping_flag)
        printf(" weight=%d/%d", p->color_saturation_weight.num,
               p->color_saturation_weight.den);
    printf("\n");
}

static void print_hdr_plus(const char *label, const AVDynamicHDRPlus *s)
{
    printf("%s:\n", label);
    printf("  application_version=%u num_windows=%u"
           " targeted_system_display_maximum_luminance=%d/%d\n",
           s->application_version, s->num_windows,
           s->targeted_system_display_maximum_luminance.num,
           s->targeted_system_display_maximum_luminance.den);
    printf("  targeted_system_display_actual_peak_luminance_flag=%u\n",
           s->targeted_system_display_actual_peak_luminance_flag);
    if (s->targeted_system_display_actual_peak_luminance_flag)
        print_peak_matrix("targeted_peak",
                          s->num_rows_targeted_system_display_actual_peak_luminance,
                          s->num_cols_targeted_system_display_actual_peak_luminance,
                          s->targeted_system_display_actual_peak_luminance);
    printf("  mastering_display_actual_peak_luminance_flag=%u\n",
           s->mastering_display_actual_peak_luminance_flag);
    if (s->mastering_display_actual_peak_luminance_flag)
        print_peak_matrix("mastering_peak",
                          s->num_rows_mastering_display_actual_peak_luminance,
                          s->num_cols_mastering_display_actual_peak_luminance,
                          s->mastering_display_actual_peak_luminance);
    for (int w = 0; w < s->num_windows; w++)
        print_window(w, &s->params[w], w > 0);
}

static void print_app5(const char *label, const AVDynamicHDRSmpte2094App5 *s)
{
    printf("%s:\n", label);
    printf("  application_version=%u minimum_application_version=%u\n",
           s->application_version, s->minimum_application_version);
    printf("  has_custom_hdr_reference_white_flag=%u",
           s->has_custom_hdr_reference_white_flag);
    if (s->has_custom_hdr_reference_white_flag)
        printf(" hdr_reference_white=0x%04x", s->hdr_reference_white);
    printf("\n  has_adaptive_tone_map_flag=%u\n",
           s->has_adaptive_tone_map_flag);
    if (!s->has_adaptive_tone_map_flag)
        return;

    printf("  baseline_hdr_headroom=0x%04x"
           " use_reference_white_tone_mapping_flag=%u\n",
           s->baseline_hdr_headroom, s->use_reference_white_tone_mapping_flag);
    if (s->use_reference_white_tone_mapping_flag)
        return;

    printf("  num_alternate_images=%u chromaticities_flag=%u"
           " has_common_component_mix_params_flag=%u"
           " has_common_curve_params_flag=%u\n",
           s->num_alternate_images,
           s->gain_application_space_chromaticities_flag,
           s->has_common_component_mix_params_flag,
           s->has_common_curve_params_flag);
    if (s->gain_application_space_chromaticities_flag == 3) {
        printf("  chromaticities:");
        for (int r = 0; r < 8; r++)
            printf(" 0x%04x", s->gain_application_space_chromaticities[r]);
        printf("\n");
    }

    for (int a = 0; a < s->num_alternate_images; a++) {
        int n = s->gain_curve_num_control_points_minus_1[a] + 1;
        printf("  alternate %d: hdr_headroom=0x%04x component_mixing_type=%u\n",
               a, s->alternate_hdr_headrooms[a], s->component_mixing_type[a]);
        if (s->component_mixing_type[a] == 3) {
            printf("    mixing flag:coefficient:");
            for (int k = 0; k < 6; k++)
                printf(" %u:0x%04x",
                       s->has_component_mixing_coefficient_flag[a][k],
                       s->component_mixing_coefficient[a][k]);
            printf("\n");
        }
        printf("    curve: num_control_points=%d use_pchip_slope=%u\n", n,
               s->gain_curve_use_pchip_slope_flag[a]);
        printf("    x:");
        for (int c = 0; c < n; c++)
            printf(" 0x%04x", s->gain_curve_control_points_x[a][c]);
        printf("\n    y:");
        for (int c = 0; c < n; c++)
            printf(" 0x%04x", s->gain_curve_control_points_y[a][c]);
        printf("\n");
        /* theta is only serialized when the pchip slope is not used. */
        if (!s->gain_curve_use_pchip_slope_flag[a]) {
            printf("    theta:");
            for (int c = 0; c < n; c++)
                printf(" 0x%04x", s->gain_curve_control_points_theta[a][c]);
            printf("\n");
        }
    }
}

/*
 * Round-trip helpers: serialize, then parse back into a zeroed struct.
 */

static int hdr_plus_round_trip(const AVDynamicHDRPlus *in, AVDynamicHDRPlus *out)
{
    uint8_t *buf = NULL;
    size_t size = 0;
    int ret;

    ret = av_dynamic_hdr_plus_to_t35(in, &buf, &size);
    if (ret < 0)
        return ret;

    memset(out, 0, sizeof(*out));
    ret = av_dynamic_hdr_plus_from_t35(out, buf, size);
    av_free(buf);
    return ret;
}

static int app5_round_trip(const AVDynamicHDRSmpte2094App5 *in,
                           AVDynamicHDRSmpte2094App5 *out)
{
    uint8_t *buf = NULL;
    size_t size = 0;
    int ret;

    ret = av_dynamic_hdr_smpte2094_app5_to_t35(in, &buf, &size);
    if (ret < 0)
        return ret;

    memset(out, 0, sizeof(*out));
    ret = av_dynamic_hdr_smpte2094_app5_from_t35(out, buf, size);
    av_free(buf);
    return ret;
}

/*
 * Fixtures.
 */

/* A conforming ST 2094-40:2020 ApplicationVersion 1 payload: exactly one
 * processing window, neither actual-peak-luminance item and no
 * ColorSaturationWeight (§9.4 excludes all three for Version 1), and the nine
 * distribution maxrgb percentiles §8.5.4 requires for that version.
 * CTA-861-H fixes application_version at 1 for HDR10+ carried over T.35.
 *
 * The entries for 5% and 10% are not CFD percentile samples: §8.5.4 reserves
 * every value other than 0.00000 and 0.00255 for that pair. No other entry may
 * exceed the largest MaxSCL component either, since §8.3 takes MaxSCL over each
 * RGB component separately while §8.5.2 draws the distribution from the
 * per-pixel maximum component. */
static void fill_hdr_plus_conforming(AVDynamicHDRPlus *s)
{
    static const uint8_t percentages[9] = { 1, 5, 10, 25, 50, 75, 90, 95, 99 };
    static const int percentiles[9] = {
        1000, 0, 255, 25000, 50000, 75000, 90000, 95000, 99000,
    };

    memset(s, 0, sizeof(*s));

    s->application_version = 1;
    s->num_windows         = 1;
    s->targeted_system_display_maximum_luminance =
        (AVRational){ 400, LUMINANCE_DEN };
    s->targeted_system_display_actual_peak_luminance_flag = 0;
    s->mastering_display_actual_peak_luminance_flag       = 0;

    s->params[0].maxscl[0]      = (AVRational){ 50000, RGB_DEN };
    s->params[0].maxscl[1]      = (AVRational){ 60000, RGB_DEN };
    s->params[0].maxscl[2]      = (AVRational){ 99000, RGB_DEN };
    s->params[0].average_maxrgb = (AVRational){ 40000, RGB_DEN };

    s->params[0].num_distribution_maxrgb_percentiles = 9;
    for (int i = 0; i < 9; i++) {
        s->params[0].distribution_maxrgb[i].percentage = percentages[i];
        s->params[0].distribution_maxrgb[i].percentile =
            (AVRational){ percentiles[i], RGB_DEN };
    }
    s->params[0].fraction_bright_pixels =
        (AVRational){ 250, FRACTION_PIXEL_DEN };

    s->params[0].tone_mapping_flag = 1;
    s->params[0].knee_point_x = (AVRational){  512, KNEE_POINT_DEN };
    s->params[0].knee_point_y = (AVRational){ 1024, KNEE_POINT_DEN };
    s->params[0].num_bezier_curve_anchors = 9;
    for (int i = 0; i < 9; i++)
        s->params[0].bezier_curve_anchors[i] =
            (AVRational){ 100 * (i + 1), BEZIER_ANCHOR_DEN };

    s->params[0].color_saturation_mapping_flag = 0;
}

/* Deliberately NOT a conforming Version 1 payload: it combines a second
 * processing window, both actual-peak-luminance matrices and
 * ColorSaturationWeight, all of which §9.4 excludes for Version 1. It exists
 * only to drive the optional serializer/parser branches those fields guard.
 *
 * tone_mapping_flag stays set for every window because the serializer writes
 * color_saturation_mapping_flag inside the tone-mapping block while the parser
 * reads it outside; only tone_mapping_flag=1 round-trips today. */
static void fill_hdr_plus_synthetic(AVDynamicHDRPlus *s)
{
    memset(s, 0, sizeof(*s));

    s->application_version = 1;
    s->num_windows         = 2;
    s->targeted_system_display_maximum_luminance =
        (AVRational){ 10000, LUMINANCE_DEN };

    s->params[1].window_upper_left_corner_x  = (AVRational){ 100, 1 };
    s->params[1].window_upper_left_corner_y  = (AVRational){ 120, 1 };
    s->params[1].window_lower_right_corner_x = (AVRational){ 500, 1 };
    s->params[1].window_lower_right_corner_y = (AVRational){ 520, 1 };
    s->params[1].center_of_ellipse_x             = 300;
    s->params[1].center_of_ellipse_y             = 320;
    s->params[1].rotation_angle                  = 45;
    s->params[1].semimajor_axis_internal_ellipse = 50;
    s->params[1].semimajor_axis_external_ellipse = 100;
    s->params[1].semiminor_axis_external_ellipse = 80;
    s->params[1].overlap_process_option          = 1;

    s->params[0].maxscl[0]      = (AVRational){ 50000, RGB_DEN };
    s->params[0].maxscl[1]      = (AVRational){ 60000, RGB_DEN };
    s->params[0].maxscl[2]      = (AVRational){ 70000, RGB_DEN };
    s->params[0].average_maxrgb = (AVRational){ 40000, RGB_DEN };
    s->params[0].num_distribution_maxrgb_percentiles = 2;
    s->params[0].distribution_maxrgb[0].percentage = 50;
    s->params[0].distribution_maxrgb[0].percentile =
        (AVRational){ 30000, RGB_DEN };
    s->params[0].distribution_maxrgb[1].percentage = 99;
    s->params[0].distribution_maxrgb[1].percentile =
        (AVRational){ 90000, RGB_DEN };
    s->params[0].fraction_bright_pixels =
        (AVRational){ 250, FRACTION_PIXEL_DEN };

    s->params[1].maxscl[0]      = (AVRational){ 20000, RGB_DEN };
    s->params[1].maxscl[1]      = (AVRational){ 30000, RGB_DEN };
    s->params[1].maxscl[2]      = (AVRational){ 40000, RGB_DEN };
    s->params[1].average_maxrgb = (AVRational){ 25000, RGB_DEN };
    s->params[1].num_distribution_maxrgb_percentiles = 1;
    s->params[1].distribution_maxrgb[0].percentage = 25;
    s->params[1].distribution_maxrgb[0].percentile =
        (AVRational){ 12500, RGB_DEN };
    s->params[1].fraction_bright_pixels =
        (AVRational){ 100, FRACTION_PIXEL_DEN };

    s->targeted_system_display_actual_peak_luminance_flag = 1;
    s->num_rows_targeted_system_display_actual_peak_luminance = 2;
    s->num_cols_targeted_system_display_actual_peak_luminance = 3;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++)
            s->targeted_system_display_actual_peak_luminance[i][j] =
                (AVRational){ i * 3 + j, PEAK_LUMINANCE_DEN };

    s->mastering_display_actual_peak_luminance_flag = 1;
    s->num_rows_mastering_display_actual_peak_luminance = 3;
    s->num_cols_mastering_display_actual_peak_luminance = 2;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
            s->mastering_display_actual_peak_luminance[i][j] =
                (AVRational){ 15 - (i * 2 + j), PEAK_LUMINANCE_DEN };

    for (int w = 0; w < 2; w++) {
        s->params[w].tone_mapping_flag = 1;
        s->params[w].knee_point_x = (AVRational){ 500 + w, KNEE_POINT_DEN };
        s->params[w].knee_point_y = (AVRational){ 800 + w, KNEE_POINT_DEN };
        s->params[w].num_bezier_curve_anchors = 3;
        s->params[w].bezier_curve_anchors[0] =
            (AVRational){ 100 + w, BEZIER_ANCHOR_DEN };
        s->params[w].bezier_curve_anchors[1] =
            (AVRational){ 500 + w, BEZIER_ANCHOR_DEN };
        s->params[w].bezier_curve_anchors[2] =
            (AVRational){ 900 + w, BEZIER_ANCHOR_DEN };
        s->params[w].color_saturation_mapping_flag = 1;
        s->params[w].color_saturation_weight =
            (AVRational){ 8 + w, SATURATION_DEN };
    }
}

static void test_hdr_plus(void)
{
    AVDynamicHDRPlus *hdr, *hdr_rt;
    AVFrame *frame;
    size_t size = 0, required = 0;
    uint8_t *buf = NULL;
    int ret;

    printf("=== AVDynamicHDRPlus ===\n");

    printf("Testing av_dynamic_hdr_plus_alloc()\n");
    hdr = av_dynamic_hdr_plus_alloc(&size);
    printf("alloc: %s size>0=%s\n", hdr ? "OK" : "FAIL",
           size > 0 ? "yes" : "no");
    av_freep(&hdr);

    hdr = av_dynamic_hdr_plus_alloc(NULL);
    printf("alloc (no size): %s\n", hdr ? "OK" : "FAIL");
    av_freep(&hdr);

    printf("\nTesting av_dynamic_hdr_plus_create_side_data()\n");
    frame = av_frame_alloc();
    if (frame) {
        hdr = av_dynamic_hdr_plus_create_side_data(frame);
        printf("create_side_data: %s\n", hdr ? "OK" : "FAIL");
        av_frame_free(&frame);
    }

    hdr    = av_dynamic_hdr_plus_alloc(NULL);
    hdr_rt = av_dynamic_hdr_plus_alloc(NULL);
    if (!hdr || !hdr_rt) {
        av_freep(&hdr);
        av_freep(&hdr_rt);
        return;
    }
    fill_hdr_plus_conforming(hdr);

    /* Size-query mode: data=NULL, size receives the required byte count. */
    printf("\nTesting av_dynamic_hdr_plus_to_t35() size query\n");
    required = 0;
    ret = av_dynamic_hdr_plus_to_t35(hdr, NULL, &required);
    printf("size query: ret=%d required>0=%s\n", ret,
           required > 0 ? "yes" : "no");

    /* Allocation mode: *data=NULL, the function allocates the buffer. */
    printf("\nTesting av_dynamic_hdr_plus_to_t35() allocation\n");
    buf  = NULL;
    size = 0;
    ret = av_dynamic_hdr_plus_to_t35(hdr, &buf, &size);
    printf("alloc mode: ret=%d size_match=%s\n", ret,
           size == required ? "yes" : "no");
    av_freep(&buf);

    /* Existing-buffer mode: a caller-owned buffer is filled in place. */
    printf("\nTesting av_dynamic_hdr_plus_to_t35() existing buffer\n");
    buf = av_malloc(required);
    if (buf) {
        uint8_t *orig = buf;
        size = required;
        ret = av_dynamic_hdr_plus_to_t35(hdr, &buf, &size);
        printf("existing buf: ret=%d same_pointer=%s size_match=%s\n", ret,
               buf == orig ? "yes" : "no",
               size == required ? "yes" : "no");
        av_freep(&buf);
    }

    /* Buffer-too-small: caller-owned buffer shorter than required. */
    printf("\nTesting av_dynamic_hdr_plus_to_t35() buffer too small\n");
    if (required > 1) {
        buf = av_malloc(required - 1);
        if (buf) {
            size = required - 1;
            ret = av_dynamic_hdr_plus_to_t35(hdr, &buf, &size);
            printf("too small: ret==AVERROR_BUFFER_TOO_SMALL=%s\n",
                   ret == AVERROR_BUFFER_TOO_SMALL ? "yes" : "no");
            av_freep(&buf);
        }
    }

    printf("\nTesting round trip, conforming ST 2094-40 Version 1 payload\n");
    ret = hdr_plus_round_trip(hdr, hdr_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_hdr_plus("decoded", hdr_rt);

    /* to_t35() ignores application_version and always writes the CTA-861-H
     * value, so the parsed field does not echo an out-of-range input. */
    printf("\nTesting that to_t35() overrides application_version\n");
    hdr->application_version = 7;
    ret = hdr_plus_round_trip(hdr, hdr_rt);
    printf("input=7 ret=%d decoded application_version=%u\n", ret,
           hdr_rt->application_version);
    hdr->application_version = 1;

    printf("\nTesting round trip, synthetic payload (branch coverage only)\n");
    fill_hdr_plus_synthetic(hdr);
    ret = hdr_plus_round_trip(hdr, hdr_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_hdr_plus("decoded", hdr_rt);

    printf("\nTesting error paths\n");
    buf  = NULL;
    size = 0;
    ret = av_dynamic_hdr_plus_to_t35(NULL, &buf, &size);
    printf("to_t35 NULL s: ret==AVERROR(EINVAL)=%s\n",
           ret == AVERROR(EINVAL) ? "yes" : "no");

    ret = av_dynamic_hdr_plus_to_t35(hdr, NULL, NULL);
    printf("to_t35 no data no size: ret==AVERROR(EINVAL)=%s\n",
           ret == AVERROR(EINVAL) ? "yes" : "no");

    ret = av_dynamic_hdr_plus_from_t35(NULL, (const uint8_t *)"", 0);
    printf("from_t35 NULL s: ret==AVERROR(EINVAL)=%s\n",
           ret == AVERROR(EINVAL) ? "yes" : "no");

    {
        /* Oversized input is rejected before parsing. */
        size_t big = AV_HDR_PLUS_MAX_PAYLOAD_SIZE + 1;
        uint8_t *oversized = av_mallocz(big);
        if (oversized) {
            ret = av_dynamic_hdr_plus_from_t35(hdr_rt, oversized, big);
            printf("from_t35 oversized: ret==AVERROR(EINVAL)=%s\n",
                   ret == AVERROR(EINVAL) ? "yes" : "no");
            av_free(oversized);
        }
    }

    av_freep(&hdr);
    av_freep(&hdr_rt);

    printf("\nTesting OOM path\n");
    av_max_alloc(1);
    hdr = av_dynamic_hdr_plus_alloc(&size);
    printf("alloc OOM: %s\n", hdr ? "FAIL" : "OK");
    av_max_alloc(INT_MAX);
    av_freep(&hdr);
}

/* Table C.2 early-out: no adaptive tone map, custom reference white only. */
static void fill_app5_minimal(AVDynamicHDRSmpte2094App5 *s)
{
    memset(s, 0, sizeof(*s));
    s->application_version         = 1;
    s->minimum_application_version = 0;
    s->has_custom_hdr_reference_white_flag = 1;
    s->hdr_reference_white         = 0x0203;
    s->has_adaptive_tone_map_flag  = 0;
}

/* Table C.3 early-out: reference-white tone mapping, no alternate images. */
static void fill_app5_reference_white(AVDynamicHDRSmpte2094App5 *s)
{
    memset(s, 0, sizeof(*s));
    s->application_version        = 2;
    s->has_adaptive_tone_map_flag = 1;
    s->baseline_hdr_headroom      = 0x0abc;
    s->use_reference_white_tone_mapping_flag = 1;
}

/* Per-image parameters: explicit gain application space chromaticities,
 * component mixing type 3 with a mix of present and absent coefficients, and
 * explicit control point slopes (theta). */
static void fill_app5_per_image(AVDynamicHDRSmpte2094App5 *s)
{
    memset(s, 0, sizeof(*s));
    s->application_version        = 1;
    s->has_adaptive_tone_map_flag = 1;
    s->baseline_hdr_headroom      = 0x0abc;
    s->num_alternate_images       = 2;
    s->gain_application_space_chromaticities_flag = 3;
    for (int r = 0; r < 8; r++)
        s->gain_application_space_chromaticities[r] = 100 * (r + 1);
    s->has_common_component_mix_params_flag = 0;
    s->has_common_curve_params_flag         = 0;
    for (int a = 0; a < 2; a++) {
        s->alternate_hdr_headrooms[a] = 0x0100 + a;
        s->component_mixing_type[a]   = 3;
        for (int k = 0; k < 6; k++) {
            s->has_component_mixing_coefficient_flag[a][k] = k & 1;
            s->component_mixing_coefficient[a][k] = 0x1000 + 0x10 * a + k;
        }
        s->gain_curve_num_control_points_minus_1[a] = 1;
        s->gain_curve_use_pchip_slope_flag[a]       = 0;
        for (int c = 0; c < 2; c++) {
            s->gain_curve_control_points_x[a][c]     = 0x2000 + 0x10 * a + c;
            s->gain_curve_control_points_y[a][c]     = 0x3000 + 0x10 * a + c;
            s->gain_curve_control_points_theta[a][c] = 0x4000 + 0x10 * a + c;
        }
    }
}

/* The complementary selectors: no explicit chromaticities, a component mixing
 * type other than 3, mixing and curve parameters shared from alternate 0, and
 * the pchip slope, which omits theta from the payload.
 *
 * The common_* flags make the parser copy alternate 0's parameters to the
 * other alternates, so the fixture sets them identically everywhere; only
 * alternate_hdr_headrooms and the y control points stay per-image. */
static void fill_app5_common(AVDynamicHDRSmpte2094App5 *s)
{
    memset(s, 0, sizeof(*s));
    s->application_version        = 1;
    s->has_adaptive_tone_map_flag = 1;
    s->baseline_hdr_headroom      = 0x0def;
    s->num_alternate_images       = 3;
    s->gain_application_space_chromaticities_flag = 0;
    s->has_common_component_mix_params_flag = 1;
    s->has_common_curve_params_flag         = 1;
    for (int a = 0; a < 3; a++) {
        s->alternate_hdr_headrooms[a] = 0x0200 + a;
        s->component_mixing_type[a]   = 1;
        s->gain_curve_num_control_points_minus_1[a] = 2;
        s->gain_curve_use_pchip_slope_flag[a]       = 1;
        for (int c = 0; c < 3; c++) {
            s->gain_curve_control_points_x[a][c] = 0x5000 + c;
            s->gain_curve_control_points_y[a][c] = 0x6000 + 0x10 * a + c;
        }
    }
}

static void test_app5(void)
{
    AVDynamicHDRSmpte2094App5 *app5, *app5_rt;
    AVFrame *frame;
    size_t size = 0, required = 0;
    uint8_t *buf = NULL;
    int ret;

    printf("\n=== AVDynamicHDRSmpte2094App5 ===\n");
    printf("Testing av_dynamic_hdr_smpte2094_app5_alloc()\n");
    app5 = av_dynamic_hdr_smpte2094_app5_alloc(&size);
    printf("alloc: %s size>0=%s\n", app5 ? "OK" : "FAIL",
           size > 0 ? "yes" : "no");
    av_freep(&app5);

    printf("\nTesting av_dynamic_hdr_smpte2094_app5_create_side_data()\n");
    frame = av_frame_alloc();
    if (frame) {
        app5 = av_dynamic_hdr_smpte2094_app5_create_side_data(frame);
        printf("create_side_data: %s\n", app5 ? "OK" : "FAIL");
        av_frame_free(&frame);
    }

    app5    = av_dynamic_hdr_smpte2094_app5_alloc(NULL);
    app5_rt = av_dynamic_hdr_smpte2094_app5_alloc(NULL);
    if (!app5 || !app5_rt) {
        av_freep(&app5);
        av_freep(&app5_rt);
        return;
    }
    fill_app5_minimal(app5);

    printf("\nTesting av_dynamic_hdr_smpte2094_app5_to_t35()\n");
    required = 0;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(app5, NULL, &required);
    printf("size query: ret=%d required>0=%s\n", ret,
           required > 0 ? "yes" : "no");

    buf  = NULL;
    size = 0;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(app5, &buf, &size);
    printf("alloc mode: ret=%d size_match=%s\n", ret,
           size == required ? "yes" : "no");
    av_freep(&buf);

    printf("\nTesting round trip, no adaptive tone map\n");
    ret = app5_round_trip(app5, app5_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_app5("decoded", app5_rt);

    printf("\nTesting round trip, reference-white tone mapping\n");
    fill_app5_reference_white(app5);
    ret = app5_round_trip(app5, app5_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_app5("decoded", app5_rt);

    printf("\nTesting round trip, per-image mixing and curve parameters\n");
    fill_app5_per_image(app5);
    ret = app5_round_trip(app5, app5_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_app5("decoded", app5_rt);

    printf("\nTesting round trip, common mixing and curve parameters\n");
    fill_app5_common(app5);
    ret = app5_round_trip(app5, app5_rt);
    printf("round trip: ret=%d\n", ret);
    if (ret >= 0)
        print_app5("decoded", app5_rt);

    printf("\nTesting error paths\n");
    buf  = NULL;
    size = 0;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(NULL, &buf, &size);
    printf("to_t35 NULL s: ret==AVERROR(EINVAL)=%s\n",
           ret == AVERROR(EINVAL) ? "yes" : "no");

    ret = av_dynamic_hdr_smpte2094_app5_from_t35(NULL, (const uint8_t *)"", 0);
    printf("from_t35 NULL s: ret==AVERROR(EINVAL)=%s\n",
           ret == AVERROR(EINVAL) ? "yes" : "no");

    /* application_version does not fit the 3-bit field. */
    app5->application_version = 8;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(app5, NULL, &size);
    printf("to_t35 invalid application_version: ret==AVERROR_INVALIDDATA=%s\n",
           ret == AVERROR_INVALIDDATA ? "yes" : "no");
    app5->application_version = 1;

    /* minimum_application_version above the documented maximum. */
    app5->minimum_application_version = 3;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(app5, NULL, &size);
    printf("to_t35 invalid minimum_application_version:"
           " ret==AVERROR_INVALIDDATA=%s\n",
           ret == AVERROR_INVALIDDATA ? "yes" : "no");
    app5->minimum_application_version = 0;

    /* More alternate images than the 3-bit count admits. */
    app5->num_alternate_images = 5;
    ret = av_dynamic_hdr_smpte2094_app5_to_t35(app5, NULL, &size);
    printf("to_t35 too many alternate images: ret==AVERROR_INVALIDDATA=%s\n",
           ret == AVERROR_INVALIDDATA ? "yes" : "no");

    av_freep(&app5);
    av_freep(&app5_rt);

    printf("\nTesting OOM path\n");
    av_max_alloc(1);
    app5 = av_dynamic_hdr_smpte2094_app5_alloc(&size);
    printf("alloc OOM: %s\n", app5 ? "FAIL" : "OK");
    av_max_alloc(INT_MAX);
    av_freep(&app5);
}

int main(void)
{
    test_hdr_plus();
    test_app5();
    return 0;
}
