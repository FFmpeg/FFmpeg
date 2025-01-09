/**
 * Copyright (c) 2015-2021, Facebook, Inc.
 * All rights reserved.
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

/* Computes the Structural Similarity Metric between two 360 video streams.
 * original SSIM algorithm:
 * Z. Wang, A. C. Bovik, H. R. Sheikh and E. P. Simoncelli,
 *   "Image quality assessment: From error visibility to structural similarity,"
 *   IEEE Transactions on Image Processing, vol. 13, no. 4, pp. 600-612, Apr. 2004.
 *
 * To improve speed, this implementation uses the standard approximation of
 * overlapped 8x8 block sums, rather than the original gaussian weights.
 *
 * To address warping from 360 projections for videos with same
 * projection and resolution, the 8x8 blocks sampled are weighted by
 * their location in the image.
 *
 * To apply SSIM across projections and video sizes, we render the video on to
 * a flat "tape" from which the 8x8 are selected and compared.
 */

/*
 * @file
 * Caculate the SSIM between two input 360 videos.
 */

#include <math.h>

#include "libavutil/avstring.h"
#include "libavutil/file_open.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "framesync.h"

#define RIGHT   0
#define LEFT    1
#define TOP     2
#define BOTTOM  3
#define FRONT   4
#define BACK    5

#define DEFAULT_HEATMAP_W 32
#define DEFAULT_HEATMAP_H 16

#define M_PI_F ((float)M_PI)
#define M_PI_2_F ((float)M_PI_2)
#define M_PI_4_F ((float)M_PI_4)
#define M_SQRT2_F ((float)M_SQRT2)

#define DEFAULT_EXPANSION_COEF 1.01f

#define BARREL_THETA_RANGE (DEFAULT_EXPANSION_COEF *  2.0f * M_PI_F)
#define BARREL_PHI_RANGE   (DEFAULT_EXPANSION_COEF *  M_PI_2_F)

// Use fixed-point with 16 bit precision for fast bilinear math
#define FIXED_POINT_PRECISION 16

// Use 1MB per channel for the histogram to get 5-digit precise SSIM value
#define SSIM360_HIST_SIZE 131072

// The last number is a marker < 0 to mark end of list
static const double PERCENTILE_LIST[] = {
    1.0, 0.9, 0.8, 0.7, 0.6,
    0.5, 0.4, 0.3, 0.2, 0.1, 0, -1
};

typedef enum StereoFormat {
    STEREO_FORMAT_TB,
    STEREO_FORMAT_LR,
    STEREO_FORMAT_MONO,
    STEREO_FORMAT_N
} StereoFormat;

typedef enum Projection {
    PROJECTION_CUBEMAP32,
    PROJECTION_CUBEMAP23,
    PROJECTION_BARREL,
    PROJECTION_BARREL_SPLIT,
    PROJECTION_EQUIRECT,
    PROJECTION_N
} Projection;

typedef struct Map2D {
    int w, h;
    double *value;
} Map2D;

typedef struct HeatmapList {
    Map2D map;
    struct HeatmapList *next;
} HeatmapList;

typedef struct SampleParams {
    int stride;
    int planewidth;
    int planeheight;
    int x_image_offset;
    int y_image_offset;
    int x_image_range;
    int y_image_range;
    int projection;
    float expand_coef;
} SampleParams;

typedef struct BilinearMap {
    // Indices to the 4 samples to compute bilinear
    int tli;
    int tri;
    int bli;
    int bri;

    // Fixed point factors with which the above 4 sample vector's
    // dot product needs to be computed for the final bilinear value
    int tlf;
    int trf;
    int blf;
    int brf;
} BilinearMap;

typedef struct SSIM360Context {
    const AVClass *class;

    FFFrameSync fs;
    // Stats file configuration
    FILE *stats_file;
    char *stats_file_str;

    // Component properties
    int nb_components;
    double coefs[4];
    char comps[4];
    int max;

    // Channel configuration & properties
    int compute_chroma;

    int is_rgb;
    uint8_t rgba_map[4];

    // Standard SSIM computation configuration & workspace
    uint64_t frame_skip_ratio;

    int *temp;
    uint64_t nb_ssim_frames;
    uint64_t nb_net_frames;
    double ssim360[4], ssim360_total;
    double *ssim360_hist[4];
    double ssim360_hist_net[4];
    double ssim360_percentile_sum[4][256];

    // 360 projection configuration & workspace
    int ref_projection;
    int main_projection;
    int ref_stereo_format;
    int main_stereo_format;
    float ref_pad;
    float main_pad;
    int use_tape;
    char *heatmap_str;
    int default_heatmap_w;
    int default_heatmap_h;

    Map2D density;
    HeatmapList *heatmaps;
    int ref_planewidth[4];
    int ref_planeheight[4];
    int main_planewidth[4];
    int main_planeheight[4];
    int tape_length[4];
    BilinearMap *ref_tape_map[4][2];
    BilinearMap *main_tape_map[4][2];
    float angular_resolution[4][2];
    double (*ssim360_plane)(
        uint8_t *main, int main_stride,
        uint8_t *ref, int ref_stride,
        int width, int height, void *temp,
        int max, Map2D density);
} SSIM360Context;

#define OFFSET(x) offsetof(SSIM360Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption ssim360_options[] = {
    { "stats_file", "Set file where to store per-frame difference information",
      OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "f",          "Set file where to store per-frame difference information",
      OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },

    { "compute_chroma",
      "Specifies if non-luma channels must be computed",
      OFFSET(compute_chroma), AV_OPT_TYPE_INT, {.i64 = 1},
      0, 1, .flags = FLAGS },

    { "frame_skip_ratio",
      "Specifies the number of frames to be skipped from evaluation, for every evaluated frame",
      OFFSET(frame_skip_ratio), AV_OPT_TYPE_INT, {.i64 = 0},
      0, 1000000, .flags = FLAGS },

    { "ref_projection", "projection of the reference video",
      OFFSET(ref_projection), AV_OPT_TYPE_INT, {.i64 = PROJECTION_EQUIRECT},
      0, PROJECTION_N - 1, .flags = FLAGS, .unit = "projection" },

    { "e",           "equirectangular",                     0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_EQUIRECT},           0, 0, FLAGS, .unit = "projection" },
    { "equirect",    "equirectangular",                     0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_EQUIRECT},           0, 0, FLAGS, .unit = "projection" },
    { "c3x2",        "cubemap 3x2",                         0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_CUBEMAP32},          0, 0, FLAGS, .unit = "projection" },
    { "c2x3",        "cubemap 2x3",                         0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_CUBEMAP23},          0, 0, FLAGS, .unit = "projection" },
    { "barrel",      "barrel facebook's 360 format",        0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_BARREL},             0, 0, FLAGS, .unit = "projection" },
    { "barrelsplit", "barrel split facebook's 360 format",  0, AV_OPT_TYPE_CONST, {.i64 = PROJECTION_BARREL_SPLIT},       0, 0, FLAGS, .unit = "projection" },

    { "main_projection", "projection of the main video",
      OFFSET(main_projection), AV_OPT_TYPE_INT, {.i64 = PROJECTION_N},
      0, PROJECTION_N, .flags = FLAGS, .unit = "projection" },

    { "ref_stereo", "stereo format of the reference video",
      OFFSET(ref_stereo_format), AV_OPT_TYPE_INT, {.i64 = STEREO_FORMAT_MONO},
      0, STEREO_FORMAT_N - 1, .flags = FLAGS, .unit = "stereo_format" },

    { "mono", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = STEREO_FORMAT_MONO }, 0, 0, FLAGS, .unit = "stereo_format" },
    { "tb",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = STEREO_FORMAT_TB },   0, 0, FLAGS, .unit = "stereo_format" },
    { "lr",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = STEREO_FORMAT_LR },   0, 0, FLAGS, .unit = "stereo_format" },

    { "main_stereo", "stereo format of main video",
      OFFSET(main_stereo_format), AV_OPT_TYPE_INT, {.i64 = STEREO_FORMAT_N},
      0, STEREO_FORMAT_N, .flags = FLAGS, .unit = "stereo_format" },

    { "ref_pad",
      "Expansion (padding) coefficient for each cube face of the reference video",
      OFFSET(ref_pad), AV_OPT_TYPE_FLOAT, {.dbl = .0f}, 0, 10, .flags = FLAGS },

    { "main_pad",
      "Expansion (padding) coeffiecient for each cube face of the main video",
      OFFSET(main_pad), AV_OPT_TYPE_FLOAT, {.dbl = .0f}, 0, 10, .flags = FLAGS },

    { "use_tape",
      "Specifies if the tape based SSIM 360 algorithm must be used independent of the input video types",
      OFFSET(use_tape), AV_OPT_TYPE_INT, {.i64 = 0},
      0, 1, .flags = FLAGS },

    { "heatmap_str",
      "Heatmap data for view-based evaluation. For heatmap file format, please refer to EntSphericalVideoHeatmapData.",
      OFFSET(heatmap_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, .flags = FLAGS },

    { "default_heatmap_width",
      "Default heatmap dimension. Will be used when dimension is not specified in heatmap data.",
      OFFSET(default_heatmap_w), AV_OPT_TYPE_INT, {.i64 = 32}, 1, 4096, .flags = FLAGS },

    { "default_heatmap_height",
      "Default heatmap dimension. Will be used when dimension is not specified in heatmap data.",
      OFFSET(default_heatmap_h), AV_OPT_TYPE_INT, {.i64 = 16}, 1, 4096, .flags = FLAGS },

    { NULL }
};

FRAMESYNC_DEFINE_CLASS(ssim360, SSIM360Context, fs);

static void set_meta(AVDictionary **metadata, const char *key, char comp, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%0.2f", d);
    if (comp) {
        char key2[128];
        snprintf(key2, sizeof(key2), "%s%c", key, comp);
        av_dict_set(metadata, key2, value, 0);
    } else {
        av_dict_set(metadata, key, value, 0);
    }
}

static void map_uninit(Map2D *map)
{
    av_freep(&map->value);
}

static int map_init(Map2D *map, int w, int h)
{
    map->value = av_calloc(h * w, sizeof(*map->value));
    if (!map->value)
        return AVERROR(ENOMEM);

    map->h = h;
    map->w = w;

    return 0;
}

static void map_list_free(HeatmapList **pl)
{
    HeatmapList *l = *pl;

    while (l) {
        HeatmapList *next = l->next;
        map_uninit(&l->map);
        av_freep(&l);
        l = next;
    }

    *pl = NULL;
}

static int map_alloc(HeatmapList **pl, int w, int h)
{
    HeatmapList *l;
    int ret;

    l = av_mallocz(sizeof(*l));
    if (!l)
        return AVERROR(ENOMEM);

    ret = map_init(&l->map, w, h);
    if (ret < 0) {
        av_freep(&l);
        return ret;
    }

    *pl = l;
    return 0;
}

static void
ssim360_4x4xn_16bit(const uint8_t *main8, ptrdiff_t main_stride,
                    const uint8_t *ref8,  ptrdiff_t ref_stride,
                    int64_t (*sums)[4], int width)
{
    const uint16_t *main16 = (const uint16_t *)main8;
    const uint16_t *ref16  = (const uint16_t *)ref8;

    main_stride >>= 1;
    ref_stride >>= 1;

    for (int z = 0; z < width; z++) {
        uint64_t s1 = 0, s2 = 0, ss = 0, s12 = 0;

        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                unsigned a = main16[x + y * main_stride];
                unsigned b = ref16[x + y * ref_stride];

                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        }

        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;
        main16 += 4;
        ref16 += 4;
    }
}

static void
ssim360_4x4xn_8bit(const uint8_t *main, ptrdiff_t main_stride,
                   const uint8_t *ref,  ptrdiff_t ref_stride,
                   int (*sums)[4], int width)
{
    for (int z = 0; z < width; z++) {
        uint32_t s1 = 0, s2 = 0, ss = 0, s12 = 0;

        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                int a = main[x + y * main_stride];
                int b = ref[x + y * ref_stride];

                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        }

        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;
        main += 4;
        ref += 4;
    }
}

static float ssim360_end1x(int64_t s1, int64_t s2, int64_t ss, int64_t s12, int max)
{
    int64_t ssim_c1 = (int64_t)(.01 * .01 * max * max * 64      + .5);
    int64_t ssim_c2 = (int64_t)(.03 * .03 * max * max * 64 * 63 + .5);

    int64_t fs1 = s1;
    int64_t fs2 = s2;
    int64_t fss = ss;
    int64_t fs12 = s12;
    int64_t vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
    int64_t covar = fs12 * 64 - fs1 * fs2;

    return (float)(2 * fs1 * fs2 + ssim_c1) * (float)(2 * covar + ssim_c2)
         / ((float)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static float ssim360_end1(int s1, int s2, int ss, int s12)
{
    static const int ssim_c1 = (int)(.01*.01*255*255*64 + .5);
    static const int ssim_c2 = (int)(.03*.03*255*255*64*63 + .5);

    int fs1 = s1;
    int fs2 = s2;
    int fss = ss;
    int fs12 = s12;
    int vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
    int covar = fs12 * 64 - fs1 * fs2;

    return (float)(2 * fs1 * fs2 + ssim_c1) * (float)(2 * covar + ssim_c2)
         / ((float)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static double
ssim360_endn_16bit(const int64_t (*sum0)[4], const int64_t (*sum1)[4],
                   int width, int max,
                   double *density_map, int map_width, double *total_weight)
{
    double ssim360 = 0.0, weight;

    for (int i = 0; i < width; i++) {
        weight = density_map ? density_map[(int) ((0.5 + i) / width * map_width)] : 1.0;
        ssim360 += weight * ssim360_end1x(
            sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
            sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
            sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
            sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3],
            max);
        *total_weight += weight;
    }
    return ssim360;
}

static double
ssim360_endn_8bit(const int (*sum0)[4], const int (*sum1)[4], int width,
                  double *density_map, int map_width, double *total_weight)
{
    double ssim360 = 0.0, weight;

    for (int i = 0; i < width; i++) {
        weight = density_map ? density_map[(int) ((0.5 + i) / width * map_width)] : 1.0;
        ssim360 += weight * ssim360_end1(
            sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
            sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
            sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
            sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3]);
        *total_weight += weight;
    }
    return ssim360;
}

static double
ssim360_plane_16bit(uint8_t *main, int main_stride,
                    uint8_t *ref, int ref_stride,
                    int width, int height, void *temp,
                    int max, Map2D density)
{
    int z = 0;
    double ssim360 = 0.0;
    int64_t (*sum0)[4] = temp;
    int64_t (*sum1)[4] = sum0 + (width >> 2) + 3;
    double total_weight = 0.0;

    width >>= 2;
    height >>= 2;

    for (int y = 1; y < height; y++) {
        for (; z <= y; z++) {
            FFSWAP(void*, sum0, sum1);
            ssim360_4x4xn_16bit(&main[4 * z * main_stride], main_stride,
                                &ref[4 * z * ref_stride], ref_stride,
                                sum0, width);
        }
        ssim360 += ssim360_endn_16bit(
            (const int64_t (*)[4])sum0, (const int64_t (*)[4])sum1,
            width - 1, max,
            density.value ? density.value + density.w * ((int) ((z - 1.0) / height * density.h)) : NULL,
            density.w, &total_weight);
    }

    return (double) (ssim360 / total_weight);
}

static double
ssim360_plane_8bit(uint8_t *main, int main_stride,
                   uint8_t *ref, int ref_stride,
                   int width, int height, void *temp,
                   int max, Map2D density)
{
    int z = 0;
    double ssim360 = 0.0;
    int (*sum0)[4] = temp;
    int (*sum1)[4] = sum0 + (width >> 2) + 3;
    double total_weight = 0.0;

    width >>= 2;
    height >>= 2;

    for (int y = 1; y < height; y++) {
        for (; z <= y; z++) {
            FFSWAP(void*, sum0, sum1);
            ssim360_4x4xn_8bit(
                &main[4 * z * main_stride], main_stride,
                &ref[4 * z * ref_stride], ref_stride,
                sum0, width);
        }
        ssim360 += ssim360_endn_8bit(
            (const int (*)[4])sum0, (const int (*)[4])sum1, width - 1,
            density.value ? density.value + density.w * ((int) ((z - 1.0) / height * density.h)) : NULL,
            density.w, &total_weight);
    }

    return (double) (ssim360 / total_weight);
}

static double ssim360_db(double ssim360, double weight)
{
    return 10 * log10(weight / (weight - ssim360));
}

static int get_bilinear_sample(const uint8_t *data, BilinearMap *m, int max_value)
{
    static const int fixed_point_half = 1 << (FIXED_POINT_PRECISION - 1);
    static const int inv_byte_mask = UINT_MAX << 8;

    int tl, tr, bl, br, v;

    if (max_value & inv_byte_mask) {
        uint16_t *data16 = (uint16_t *)data;
        tl = data16[m->tli];
        tr = data16[m->tri];
        bl = data16[m->bli];
        br = data16[m->bri];
    } else {
        tl = data[m->tli];
        tr = data[m->tri];
        bl = data[m->bli];
        br = data[m->bri];
    }

    v = m->tlf * tl +
        m->trf * tr +
        m->blf * bl +
        m->brf * br;

    // Round by half, and revert the fixed-point offset
    return ((v + fixed_point_half) >> FIXED_POINT_PRECISION) & max_value;
}

static void
ssim360_4x4x2_tape(const uint8_t *main, BilinearMap *main_maps,
                   const uint8_t *ref, BilinearMap *ref_maps,
                   int offset_y, int max_value, int (*sums)[4])
{
    int offset_x = 0;

    // Two blocks along the width
    for (int z = 0; z < 2; z++) {
        int s1 = 0, s2 = 0, ss = 0, s12 = 0;

        // 4 pixel block from (offset_x, offset_y)
        for (int y = offset_y; y < offset_y + 4; y++) {
            int y_stride = y << 3;
            for (int x = offset_x; x < offset_x + 4; x++) {
                int map_index = x + y_stride;
                int a = get_bilinear_sample(main, main_maps + map_index, max_value);
                int b = get_bilinear_sample(ref, ref_maps + map_index, max_value);

                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        }

        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;

        offset_x += 4;
    }
}

static float get_radius_between_negative_and_positive_pi(float theta)
{
    int floor_theta_by_2pi, floor_theta_by_pi;

    // Convert theta to range [0, 2*pi]
    floor_theta_by_2pi = (int)(theta / (2.0f * M_PI_F)) - (theta < 0.0f);
    theta -= 2.0f * M_PI_F * floor_theta_by_2pi;

    // Convert theta to range [-pi, pi]
    floor_theta_by_pi = theta / M_PI_F;
    theta -= 2.0f * M_PI_F * floor_theta_by_pi;
    return FFMIN(M_PI_F, FFMAX(-M_PI_F, theta));
}

static float get_heat(HeatmapList *heatmaps, float angular_resoluation, float norm_tape_pos)
{
    float pitch, yaw, norm_pitch, norm_yaw;
    int w, h;

    if (!heatmaps)
        return 1.0f;

    pitch = asinf(norm_tape_pos*2);
    yaw   = M_PI_2_F * pitch / angular_resoluation;
    yaw   = get_radius_between_negative_and_positive_pi(yaw);

    // normalize into [0,1]
    norm_pitch = 1.0f - (pitch / M_PI_F + 0.5f);
    norm_yaw   = yaw / 2.0f / M_PI_F + 0.5f;

    // get heat on map
    w = FFMIN(heatmaps->map.w - 1, FFMAX(0, heatmaps->map.w * norm_yaw));
    h = FFMIN(heatmaps->map.h - 1, FFMAX(0, heatmaps->map.h * norm_pitch));
    return heatmaps->map.value[h * heatmaps->map.w + w];
}

static double
ssim360_tape(uint8_t *main, BilinearMap *main_maps,
             uint8_t *ref, BilinearMap *ref_maps,
             int tape_length, int max_value, void *temp,
             double *ssim360_hist, double *ssim360_hist_net,
             float angular_resolution, HeatmapList *heatmaps)
{
    int horizontal_block_count = 2;
    int vertical_block_count = tape_length >> 2;

    int z = 0, y;
    // Since the tape will be very long and we need to average over all 8x8 blocks, use double
    double ssim360 = 0.0;
    double sum_weight = 0.0;

    int (*sum0)[4] = temp;
    int (*sum1)[4] = sum0 + horizontal_block_count + 3;

    for (y = 1; y < vertical_block_count; y++) {
        int fs1, fs2, fss, fs12, hist_index;
        float norm_tape_pos, weight;
        double sample_ssim360;

        for (; z <= y; z++) {
            FFSWAP(void*, sum0, sum1);
            ssim360_4x4x2_tape(main, main_maps, ref, ref_maps, z*4, max_value, sum0);
        }

        // Given we have only one 8x8 block, following sums fit within 26 bits even for 10bit videos
        fs1  = sum0[0][0] + sum0[1][0] + sum1[0][0] + sum1[1][0];
        fs2  = sum0[0][1] + sum0[1][1] + sum1[0][1] + sum1[1][1];
        fss  = sum0[0][2] + sum0[1][2] + sum1[0][2] + sum1[1][2];
        fs12 = sum0[0][3] + sum0[1][3] + sum1[0][3] + sum1[1][3];

        if (max_value > 255) {
            // Since we need high precision to multiply fss / fs12 by 64, use double
            double ssim_c1_d = .01*.01*64*max_value*max_value;
            double ssim_c2_d = .03*.03*64*63*max_value*max_value;

            double vars = 64. * fss - 1. * fs1 * fs1 - 1. * fs2 * fs2;
            double covar = 64. * fs12 - 1.*fs1 * fs2;
            sample_ssim360 = (2. * fs1 * fs2 + ssim_c1_d) * (2. * covar + ssim_c2_d)
                        / ((1. * fs1 * fs1 + 1. * fs2 * fs2 + ssim_c1_d) * (1. * vars + ssim_c2_d));
        } else {
            static const int ssim_c1 = (int)(.01*.01*255*255*64 + .5);
            static const int ssim_c2 = (int)(.03*.03*255*255*64*63 + .5);

            int vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
            int covar = fs12 * 64 - fs1 * fs2;
            sample_ssim360 = (double)(2 * fs1 * fs2 + ssim_c1) * (double)(2 * covar + ssim_c2)
                        / ((double)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (double)(vars + ssim_c2));
        }

        hist_index = (int)(sample_ssim360 * ((double)SSIM360_HIST_SIZE - .5));
        hist_index = av_clip(hist_index, 0, SSIM360_HIST_SIZE - 1);

        norm_tape_pos = (y - 0.5f) / (vertical_block_count - 1.0f) - 0.5f;
        // weight from an input heatmap if available, otherwise weight = 1.0
        weight = get_heat(heatmaps, angular_resolution, norm_tape_pos);
        ssim360_hist[hist_index] += weight;
        *ssim360_hist_net += weight;

        ssim360 += (sample_ssim360 * weight);
        sum_weight += weight;
    }

    return ssim360 / sum_weight;
}

static void compute_bilinear_map(SampleParams *p, BilinearMap *m, float x, float y)
{
    float fixed_point_scale = (float)(1 << FIXED_POINT_PRECISION);

    // All operations in here will fit in the 22 bit mantissa of floating point,
    // since the fixed point precision is well under 22 bits
    float x_image = av_clipf(x * p->x_image_range, 0, p->x_image_range) + p->x_image_offset;
    float y_image = av_clipf(y * p->y_image_range, 0, p->y_image_range) + p->y_image_offset;

    int x_floor = x_image;
    int y_floor = y_image;
    float x_diff = x_image - x_floor;
    float y_diff = y_image - y_floor;

    int x_ceil = x_floor + (x_diff > 1e-6);
    int y_ceil = y_floor + (y_diff > 1e-6);
    float x_inv_diff = 1.0f - x_diff;
    float y_inv_diff = 1.0f - y_diff;

    // Indices of the 4 samples from source frame
    m->tli = x_floor    + y_floor   * p->stride;
    m->tri = x_ceil     + y_floor   * p->stride;
    m->bli = x_floor    + y_ceil    * p->stride;
    m->bri = x_ceil     + y_ceil    * p->stride;

    // Scale to be applied to each of the 4 samples from source frame
    m->tlf = x_inv_diff * y_inv_diff * fixed_point_scale;
    m->trf = x_diff     * y_inv_diff * fixed_point_scale;
    m->blf = x_inv_diff * y_diff     * fixed_point_scale;
    m->brf = x_diff     * y_diff     * fixed_point_scale;
}

static void get_equirect_map(float phi, float theta, float *x, float *y)
{
    *x = 0.5f + theta / (2.0f * M_PI_F);
    // y increases downwards
    *y = 0.5f - phi / M_PI_F;
}

static void get_barrel_map(float phi, float theta, float *x, float *y)
{
    float abs_phi = FFABS(phi);

    if (abs_phi <= M_PI_4_F) {
        // Equirect region
        *x = 0.8f * (0.5f + theta / BARREL_THETA_RANGE);
        // y increases downwards
        *y = 0.5f - phi / BARREL_PHI_RANGE;
    } else {
        // Radial ratio on a unit circle = cot(abs_phi) / (expansion_cefficient).
        // Using cos(abs_phi)/sin(abs_phi) explicitly to avoid division by zero
        float radial_ratio = cosf(abs_phi) / (sinf(abs_phi) * DEFAULT_EXPANSION_COEF);
        float circle_x = radial_ratio * sinf(theta);
        float circle_y = radial_ratio * cosf(theta);
        float offset_y = 0.25f;
        if (phi < 0) {
            // Bottom circle: theta increases clockwise, and front is upward
            circle_y *= -1.0f;
            offset_y += 0.5f;
        }

        *x = 0.8f + 0.1f * (1.0f + circle_x);
        *y = offset_y + 0.25f * circle_y;
    }
}

static void get_barrel_split_map(float phi, float theta, float expand_coef, float *x, float *y)
{
    float abs_phi = FFABS(phi);

    // Front Face [-PI/2, PI/2] -> [0,1].
    // Back Face  [PI/2, PI] and [-PI, -PI/2] -> [1, 2]
    float radian_pi_theta = theta / M_PI_F + 0.5f;
    int vFace;

    if (radian_pi_theta < 0.0f)
        radian_pi_theta += 2.0f;

    // Front face at top (= 0), back face at bottom (= 1).
    vFace = radian_pi_theta >= 1.0f;

    if (abs_phi <= M_PI_4_F) {
        // Equirect region
        *x = 2.0f / 3.0f * (0.5f + (radian_pi_theta - vFace - 0.5f) / expand_coef);
        // y increases downwards
        *y = 0.25f + 0.5f * vFace - phi / (M_PI_F * expand_coef);
    } else {
        // Radial ratio on a unit circle = cot(abs_phi) / (expansion_cefficient).
        // Using cos(abs_phi)/sin(abs_phi) explicitly to avoid division by zero
        float radial_ratio = cosf(abs_phi) /  (sinf(abs_phi) * expand_coef);
        float circle_x = radial_ratio * sinf(theta);
        float circle_y = radial_ratio * cosf(theta);
        float offset_y = 0.25f;

        if (vFace == 1) {
            // Back Face: Flip
            circle_x *= -1.0f;
            circle_y = (circle_y >= 0.0f) ? (1 - circle_y) : (-1 - circle_y);
            offset_y += 0.5f;

            // Bottom circle: theta increases clockwise
            if (phi < 0)
                circle_y *= -1.0f;
        } else {
            // Front Face
            // Bottom circle: theta increases clockwise
            if (phi < 0)
                circle_y *= -1.0f;
        }

        *x = 2.0f / 3.0f + 0.5f / 3.0f * (1.0f + circle_x);
        *y = offset_y + 0.25f * circle_y / expand_coef;  // y direction of expand_coeff (margin)
    }
}

// Returns cube face, and provided face_x & face_y will range from [0, 1]
static int get_cubemap_face_map(float axis_vec_x, float axis_vec_y, float axis_vec_z, float *face_x, float *face_y)
{
    // To check if phi, theta hits the top / bottom faces, we check the hit point of
    // the axis vector on planes y = 1 and y = -1, and see if x & z are within [-1, 1]

    // 0.577 < 1 / sqrt(3), which is less than the smallest sin(phi) falling on top/bottom faces
    // This angle check will save computation from unnecessarily checking the top/bottom faces
    if (FFABS(axis_vec_y) > 0.577f) {
        float x_hit = axis_vec_x / FFABS(axis_vec_y);
        float z_hit = axis_vec_z / axis_vec_y;

        if (FFABS(x_hit) <= 1.f && FFABS(z_hit) <= 1.f) {
            *face_x = x_hit;
            // y increases downwards
            *face_y = z_hit;
            return axis_vec_y > 0 ? TOP : BOTTOM;
        }
    }

    // Check for left / right faces
    if (FFABS(axis_vec_x) > 0.577f) {
        float z_hit = -axis_vec_z / axis_vec_x;
        float y_hit = axis_vec_y / FFABS(axis_vec_x);

        if (FFABS(z_hit) <= 1.f && FFABS(y_hit) <= 1.f) {
            *face_x = z_hit;
            // y increases downwards
            *face_y = -y_hit;
            return axis_vec_x > 0 ? RIGHT : LEFT;
        }
    }

    // Front / back faces
    *face_x = axis_vec_x / axis_vec_z;
    // y increases downwards
    *face_y = -axis_vec_y / FFABS(axis_vec_z);

    return axis_vec_z > 0 ? FRONT : BACK;
}

static void get_cubemap32_map(float phi, float theta, float *x, float *y)
{
    // face_projection_map maps each cube face to an index representing the face on the projection
    // The indices 0->5 for cubemap 32 goes as:
    // [0, 1, 2] as row 1, left to right
    // [3, 4, 5] as row 2, left to right
    static const int face_projection_map[] = {
        [RIGHT]   = 0,  [LEFT]    = 1,  [TOP]     = 2,
        [BOTTOM]  = 3,  [FRONT]   = 4,  [BACK]    = 5,
    };

    float axis_vec_x = cosf(phi) * sinf(theta);
    float axis_vec_y = sinf(phi);
    float axis_vec_z = cosf(phi) * cosf(theta);
    float face_x = 0, face_y = 0;
    int face_index = get_cubemap_face_map(axis_vec_x, axis_vec_y, axis_vec_z, &face_x, &face_y);

    float x_offset = 1.f / 3.f * (face_projection_map[face_index] % 3);
    float y_offset = .5f * (face_projection_map[face_index] / 3);

    *x = x_offset + (face_x / DEFAULT_EXPANSION_COEF + 1.f) / 6.f;
    *y = y_offset + (face_y / DEFAULT_EXPANSION_COEF + 1.f) / 4.f;
}

static void get_rotated_cubemap_map(float phi, float theta, float expand_coef, float *x, float *y)
{
    // face_projection_map maps each cube face to an index representing the face on the projection
    // The indices 0->5 for rotated cubemap goes as:
    // [0, 1] as row 1, left to right
    // [2, 3] as row 2, left to right
    // [4, 5] as row 3, left to right
    static const int face_projection_map[] = {
        [LEFT]    = 0,  [TOP]     = 1,
        [FRONT]   = 2,  [BACK]    = 3,
        [RIGHT]   = 4,  [BOTTOM]  = 5,
    };

    float axis_yaw_vec_x, axis_yaw_vec_y, axis_yaw_vec_z;
    float axis_pitch_vec_z, axis_pitch_vec_y;
    float x_offset, y_offset;
    float face_x = 0, face_y = 0;
    int face_index;

    // Unrotate the cube and fix the face map:
    // First undo the 45 degree yaw
    theta += M_PI_4_F;

    // Now we are looking at the middle of an edge. So convert to axis vector & undo the pitch
    axis_yaw_vec_x = cosf(phi) * sinf(theta);
    axis_yaw_vec_y = sinf(phi);
    axis_yaw_vec_z = cosf(phi) * cosf(theta);

    // The pitch axis is along +x, and has value of -45 degree. So, only y and z components change
    axis_pitch_vec_z = (axis_yaw_vec_z - axis_yaw_vec_y) / M_SQRT2_F;
    axis_pitch_vec_y = (axis_yaw_vec_y + axis_yaw_vec_z) / M_SQRT2_F;

    face_index = get_cubemap_face_map(axis_yaw_vec_x, axis_pitch_vec_y, axis_pitch_vec_z, &face_x, &face_y);

    // Correct for the orientation of the axes on the faces
    if (face_index == LEFT || face_index == FRONT || face_index == RIGHT) {
        // x increases downwards & y increases towards left
        float upright_y = face_y;
        face_y = face_x;
        face_x = -upright_y;
    } else if (face_index == TOP || face_index == BOTTOM) {
        // turn the face upside-down for top and bottom
        face_x *= -1.f;
        face_y *= -1.f;
    }

    x_offset = .5f * (face_projection_map[face_index] & 1);
    y_offset = 1.f / 3.f * (face_projection_map[face_index] >> 1);

    *x = x_offset + (face_x / expand_coef + 1.f) / 4.f;
    *y = y_offset + (face_y / expand_coef + 1.f) / 6.f;
}

static void get_projected_map(float phi, float theta, SampleParams *p, BilinearMap *m)
{
    float x = 0, y = 0;
    switch(p->projection) {
// TODO: Calculate for CDS
    case PROJECTION_CUBEMAP23:
        get_rotated_cubemap_map(phi, theta, p->expand_coef, &x, &y);
        break;
    case PROJECTION_CUBEMAP32:
        get_cubemap32_map(phi, theta, &x, &y);
        break;
    case PROJECTION_BARREL:
        get_barrel_map(phi, theta, &x, &y);
        break;
    case PROJECTION_BARREL_SPLIT:
        get_barrel_split_map(phi, theta, p->expand_coef, &x, &y);
        break;
    // Assume PROJECTION_EQUIRECT as the default
    case PROJECTION_EQUIRECT:
    default:
        get_equirect_map(phi, theta, &x, &y);
        break;
    }
    compute_bilinear_map(p, m, x, y);
}

static int tape_supports_projection(int projection)
{
    switch(projection) {
    case PROJECTION_CUBEMAP23:
    case PROJECTION_CUBEMAP32:
    case PROJECTION_BARREL:
    case PROJECTION_BARREL_SPLIT:
    case PROJECTION_EQUIRECT:
        return 1;
    default:
        return 0;
    }
}

static float get_tape_angular_resolution(int projection, float expand_coef, int image_width, int image_height)
{
    // NOTE: The angular resolution of a projected sphere is defined as
    // the maximum possible horizontal angle of a pixel on the equator.
    // We apply an intentional bias to the horizon as opposed to the meridian,
    // since the view direction of most content is rarely closer to the poles

    switch(projection) {
// TODO: Calculate for CDS
    case PROJECTION_CUBEMAP23:
        // Approximating atanf(pixel_width / (half_edge_width * sqrt2)) = pixel_width / (half_face_width * sqrt2)
        return expand_coef / (M_SQRT2_F * image_width / 4.f);
    case PROJECTION_CUBEMAP32:
        // Approximating atanf(pixel_width / half_face_width) = pixel_width / half_face_width
        return DEFAULT_EXPANSION_COEF / (image_width / 6.f);
    case PROJECTION_BARREL:
        return FFMAX(BARREL_THETA_RANGE / (0.8f * image_width), BARREL_PHI_RANGE / image_height);
    case PROJECTION_BARREL_SPLIT:
        return FFMAX((expand_coef * M_PI_F) / (2.0f / 3.0f * image_width),
                     expand_coef * M_PI_2_F / (image_height / 2.0f));
    // Assume PROJECTION_EQUIRECT as the default
    case PROJECTION_EQUIRECT:
    default:
        return FFMAX(2.0f * M_PI_F / image_width, M_PI_F / image_height);
    }
}

static int
generate_eye_tape_map(SSIM360Context *s,
                      int plane, int eye,
                      SampleParams *ref_sample_params,
                      SampleParams *main_sample_params)
{
    int ref_image_width = ref_sample_params->x_image_range + 1;
    int ref_image_height = ref_sample_params->y_image_range + 1;

    float angular_resolution =
        get_tape_angular_resolution(s->ref_projection, 1.f + s->ref_pad,
                                    ref_image_width, ref_image_height);

    float conversion_factor = M_PI_2_F / (angular_resolution * angular_resolution);
    float start_phi = -M_PI_2_F + 4.0f * angular_resolution;
    float start_x = conversion_factor * sinf(start_phi);
    float end_phi = M_PI_2_F - 3.0f * angular_resolution;
    float end_x = conversion_factor * sinf(end_phi);
    float x_range = end_x - start_x;

    // Ensure tape length is a multiple of 4, for full SSIM block coverage
    int tape_length = s->tape_length[plane] = ((int)ROUNDED_DIV(x_range, 4)) << 2;

    s->ref_tape_map[plane][eye]  = av_malloc_array(tape_length * 8, sizeof(BilinearMap));
    s->main_tape_map[plane][eye] = av_malloc_array(tape_length * 8, sizeof(BilinearMap));
    if (!s->ref_tape_map[plane][eye] || !s->main_tape_map[plane][eye])
        return AVERROR(ENOMEM);

    s->angular_resolution[plane][eye] = angular_resolution;

    // For easy memory access, we navigate the tape lengthwise on y
    for (int y_index = 0; y_index < tape_length; y_index ++) {
        int y_stride = y_index << 3;

        float x = start_x + x_range * (y_index / (tape_length - 1.0f));
        // phi will be in range [-pi/2, pi/2]
        float mid_phi = asinf(x / conversion_factor);

        float theta = mid_phi * M_PI_2_F / angular_resolution;
        theta = get_radius_between_negative_and_positive_pi(theta);

        for (int x_index = 0; x_index < 8; x_index ++) {
            float phi = mid_phi + angular_resolution * (3.0f - x_index);
            int tape_index = y_stride + x_index;
            get_projected_map(phi, theta, ref_sample_params,  &s->ref_tape_map [plane][eye][tape_index]);
            get_projected_map(phi, theta, main_sample_params, &s->main_tape_map[plane][eye][tape_index]);
        }
    }

    return 0;
}

static int generate_tape_maps(SSIM360Context *s, AVFrame *main, const AVFrame *ref)
{
    // A tape is a long segment with 8 pixels thickness, with the angular center at the middle (below 4th pixel).
    // When it takes a full loop around a sphere, it will overlap the starting point at half the width from above.
    int ref_stereo_format = s->ref_stereo_format;
    int main_stereo_format = s->main_stereo_format;
    int are_both_stereo = (main_stereo_format != STEREO_FORMAT_MONO) && (ref_stereo_format != STEREO_FORMAT_MONO);
    int min_eye_count = 1 + are_both_stereo;
    int ret;

    for (int i = 0; i < s->nb_components; i ++) {
        int ref_width = s->ref_planewidth[i];
        int ref_height = s->ref_planeheight[i];
        int main_width = s->main_planewidth[i];
        int main_height = s->main_planeheight[i];

        int is_ref_LR = (ref_stereo_format == STEREO_FORMAT_LR);
        int is_ref_TB = (ref_stereo_format == STEREO_FORMAT_TB);
        int is_main_LR = (main_stereo_format == STEREO_FORMAT_LR);
        int is_main_TB = (main_stereo_format == STEREO_FORMAT_TB);

        int ref_image_width = is_ref_LR ? ref_width >> 1 : ref_width;
        int ref_image_height = is_ref_TB ? ref_height >> 1 : ref_height;
        int main_image_width = is_main_LR ? main_width >> 1 : main_width;
        int main_image_height = is_main_TB ? main_height >> 1 : main_height;

        for (int eye = 0; eye < min_eye_count; eye ++) {
            SampleParams ref_sample_params = {
                .stride         = ref->linesize[i],
                .planewidth     = ref_width,
                .planeheight    = ref_height,
                .x_image_range  = ref_image_width - 1,
                .y_image_range  = ref_image_height - 1,
                .x_image_offset = is_ref_LR * eye * ref_image_width,
                .y_image_offset = is_ref_TB * eye * ref_image_height,
                .projection     = s->ref_projection,
                .expand_coef    = 1.f + s->ref_pad,
            };

            SampleParams main_sample_params = {
                .stride         = main->linesize[i],
                .planewidth     = main_width,
                .planeheight    = main_height,
                .x_image_range  = main_image_width - 1,
                .y_image_range  = main_image_height - 1,
                .x_image_offset = is_main_LR * eye * main_image_width,
                .y_image_offset = is_main_TB * eye * main_image_height,
                .projection     = s->main_projection,
                .expand_coef    = 1.f + s->main_pad,
            };

            ret = generate_eye_tape_map(s, i, eye, &ref_sample_params, &main_sample_params);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int do_ssim360(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    SSIM360Context *s = ctx->priv;
    AVFrame *master, *ref;
    AVDictionary **metadata;
    double c[4], ssim360v = 0.0, ssim360p50 = 0.0;
    int ret;
    int need_frame_skip = s->nb_net_frames % (s->frame_skip_ratio + 1);
    HeatmapList* h_ptr = NULL;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;

    s->nb_net_frames++;

    if (need_frame_skip)
        return ff_filter_frame(ctx->outputs[0], master);

    metadata = &master->metadata;

    if (s->use_tape && !s->tape_length[0]) {
        ret = generate_tape_maps(s, master, ref);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < s->nb_components; i++) {
        if (s->use_tape) {
            c[i] = ssim360_tape(master->data[i], s->main_tape_map[i][0],
                                ref->data[i],    s->ref_tape_map [i][0],
                                s->tape_length[i], s->max, s->temp,
                                s->ssim360_hist[i], &s->ssim360_hist_net[i],
                                s->angular_resolution[i][0], s->heatmaps);

            if (s->ref_tape_map[i][1]) {
                c[i] += ssim360_tape(master->data[i], s->main_tape_map[i][1],
                                     ref->data[i],    s->ref_tape_map[i][1],
                                     s->tape_length[i], s->max, s->temp,
                                     s->ssim360_hist[i], &s->ssim360_hist_net[i],
                                     s->angular_resolution[i][1], s->heatmaps);
                c[i] /= 2.f;
            }
        } else {
            c[i] = s->ssim360_plane(master->data[i], master->linesize[i],
                                    ref->data[i],    ref->linesize[i],
                                    s->ref_planewidth[i], s->ref_planeheight[i],
                                    s->temp, s->max, s->density);
        }

        s->ssim360[i] += c[i];
        ssim360v      += s->coefs[i] * c[i];
    }

    s->nb_ssim_frames++;
    if (s->heatmaps) {
        map_uninit(&s->heatmaps->map);
        h_ptr = s->heatmaps;
        s->heatmaps = s->heatmaps->next;
        av_freep(&h_ptr);
    }
    s->ssim360_total += ssim360v;

    // Record percentiles from histogram and attach metadata when using tape
    if (s->use_tape) {
        int hist_indices[4];
        double hist_weight[4];

        for (int i = 0; i < s->nb_components; i++) {
            hist_indices[i] = SSIM360_HIST_SIZE - 1;
            hist_weight[i] = 0;
        }

        for (int p = 0; PERCENTILE_LIST[p] >= 0.0; p ++) {
            for (int i = 0; i < s->nb_components; i++) {
                double target_weight, ssim360p;

                // Target weight = total number of samples above the specified percentile
                target_weight = (1. - PERCENTILE_LIST[p]) * s->ssim360_hist_net[i];
                target_weight = FFMAX(target_weight, 1);
                while(hist_indices[i] >= 0 && hist_weight[i] < target_weight) {
                    hist_weight[i] += s->ssim360_hist[i][hist_indices[i]];
                    hist_indices[i] --;
                }

                ssim360p = (double)(hist_indices[i] + 1) / (double)(SSIM360_HIST_SIZE - 1);
                if (PERCENTILE_LIST[p] == 0.5)
                    ssim360p50 += s->coefs[i] * ssim360p;
                s->ssim360_percentile_sum[i][p] += ssim360p;
            }
        }

        for (int i = 0; i < s->nb_components; i++) {
            memset(s->ssim360_hist[i], 0, SSIM360_HIST_SIZE * sizeof(double));
            s->ssim360_hist_net[i] = 0;
        }

        for (int i = 0; i < s->nb_components; i++) {
            int cidx = s->is_rgb ? s->rgba_map[i] : i;
            set_meta(metadata, "lavfi.ssim360.", s->comps[i], c[cidx]);
        }

        // Use p50 as the aggregated value
        set_meta(metadata, "lavfi.ssim360.All", 0, ssim360p50);
        set_meta(metadata, "lavfi.ssim360.dB", 0, ssim360_db(ssim360p50, 1.0));

        if (s->stats_file) {
            fprintf(s->stats_file, "n:%"PRId64" ", s->nb_ssim_frames);

            for (int i = 0; i < s->nb_components; i++) {
                int cidx = s->is_rgb ? s->rgba_map[i] : i;
                fprintf(s->stats_file, "%c:%f ", s->comps[i], c[cidx]);
            }

            fprintf(s->stats_file, "All:%f (%f)\n", ssim360p50, ssim360_db(ssim360p50, 1.0));
        }
    }

    return ff_filter_frame(ctx->outputs[0], master);
}

static int parse_heatmaps(void *logctx, HeatmapList **proot,
                          const char *data, int w, int h)
{
    HeatmapList  *root = NULL;
    HeatmapList **next = &root;

    int ret;

    // skip video id line
    data = strchr(data, '\n');
    if (!data) {
        av_log(logctx, AV_LOG_ERROR, "Invalid heatmap syntax\n");
        return AVERROR(EINVAL);
    }
    data++;

    while (*data) {
        HeatmapList *cur;
        char *line = av_get_token(&data, "\n");
        char *saveptr, *val;
        int i;

        if (!line) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        // first value is frame id
        av_strtok(line, ",", &saveptr);

        ret = map_alloc(next, w, h);
        if (ret < 0)
            goto line_fail;

        cur  = *next;
        next = &cur->next;

        i = 0;
        while ((val = av_strtok(NULL, ",", &saveptr))) {
            if (i >= w * h) {
                av_log(logctx, AV_LOG_ERROR, "Too many entries in a heat map\n");
                ret = AVERROR(EINVAL);
                goto line_fail;
            }

            cur->map.value[i++] = atof(val);
        }

line_fail:
        av_freep(&line);
        if (ret < 0)
            goto fail;
    }

    *proot = root;

    return 0;
fail:
    map_list_free(&root);
    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    SSIM360Context *s = ctx->priv;
    int err;

    if (s->stats_file_str) {
        if (!strcmp(s->stats_file_str, "-")) {
            s->stats_file = stdout;
        } else {
            s->stats_file = avpriv_fopen_utf8(s->stats_file_str, "w");
            if (!s->stats_file) {
                err = AVERROR(errno);
                av_log(ctx, AV_LOG_ERROR, "Could not open stats file %s: %s\n",
                       s->stats_file_str, av_err2str(err));
                return err;
            }
        }
    }

    if (s->use_tape && s->heatmap_str) {
        err = parse_heatmaps(ctx, &s->heatmaps, s->heatmap_str,
                             s->default_heatmap_w, s->default_heatmap_h);
        if (err < 0)
            return err;
    }

    s->fs.on_event = do_ssim360;
    return 0;
}

static int config_input_main(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext           *ctx = inlink->dst;
    SSIM360Context              *s = ctx->priv;

    s->main_planeheight[0] = inlink->h;
    s->main_planeheight[3] = inlink->h;
    s->main_planeheight[1] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->main_planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);

    s->main_planewidth[0]  = inlink->w;
    s->main_planewidth[3]  = inlink->w;
    s->main_planewidth[1]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->main_planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);

    // If main projection is unindentified, assume it is same as reference
    if (s->main_projection == PROJECTION_N)
        s->main_projection = s->ref_projection;

    // If main stereo format is unindentified, assume it is same as reference
    if (s->main_stereo_format == STEREO_FORMAT_N)
        s->main_stereo_format = s->ref_stereo_format;

    return 0;
}

static int generate_density_map(SSIM360Context *s, int w, int h)
{
    double d, r_square, cos_square;
    int ow, oh, ret;

    ret = map_init(&s->density, w, h);
    if (ret < 0)
        return ret;

    switch (s->ref_stereo_format) {
    case STEREO_FORMAT_TB:
        h >>= 1;
        break;
    case STEREO_FORMAT_LR:
        w >>= 1;
        break;
    }

    switch (s->ref_projection) {
    case PROJECTION_EQUIRECT:
        for (int i = 0; i < h; i++) {
            d = cos(((0.5 + i) / h - 0.5) * M_PI);
            for (int j = 0; j < w; j++)
                s->density.value[i * w + j] = d;
        }
        break;
    case PROJECTION_CUBEMAP32:
        // for one quater of a face
        for (int i = 0; i < h / 4; i++) {
            for (int j = 0; j < w / 6; j++) {
                // r = normalized distance to the face center
                r_square =
                  (0.5 + i) / (h / 2) * (0.5 + i) / (h / 2) +
                  (0.5 + j) / (w / 3) * (0.5 + j) / (w / 3);
                r_square /= DEFAULT_EXPANSION_COEF * DEFAULT_EXPANSION_COEF;
                cos_square = 0.25 / (r_square + 0.25);
                d = pow(cos_square, 1.5);

                for (int face = 0; face < 6; face++) {
                    // center of a face
                    switch (face) {
                    case 0:
                        oh = h / 4;
                        ow = w / 6;
                        break;
                    case 1:
                        oh = h / 4;
                        ow = w / 6 + w / 3;
                        break;
                    case 2:
                        oh = h / 4;
                        ow = w / 6 + 2 * w / 3;
                        break;
                    case 3:
                        oh = h / 4 + h / 2;
                        ow = w / 6;
                        break;
                    case 4:
                        oh = h / 4 + h / 2;
                        ow = w / 6 + w / 3;
                        break;
                    case 5:
                        oh = h / 4 + h / 2;
                        ow = w / 6 + 2 * w / 3;
                        break;
                    }
                    s->density.value[(oh - 1 - i) * w + ow - 1 - j] = d;
                    s->density.value[(oh - 1 - i) * w + ow     + j] = d;
                    s->density.value[(oh     + i) * w + ow - 1 - j] = d;
                    s->density.value[(oh     + i) * w + ow     + j] = d;
                }
            }
        }
        break;
    case PROJECTION_CUBEMAP23:
        // for one quater of a face
        for (int i = 0; i < h / 6; i++) {
            for (int j = 0; j < w / 4; j++) {
                // r = normalized distance to the face center
                r_square =
                    (0.5 + i) / (h / 3) * (0.5 + i) / (h / 3) +
                    (0.5 + j) / (w / 2) * (0.5 + j) / (w / 2);
                r_square /= (1.f + s->ref_pad) * (1.f + s->ref_pad);
                cos_square = 0.25 / (r_square + 0.25);
                d = pow(cos_square, 1.5);

                for (int face = 0; face < 6; face++) {
                    // center of a face
                    switch (face) {
                    case 0:
                        ow = w / 4;
                        oh = h / 6;
                        break;
                    case 1:
                        ow = w / 4;
                        oh = h / 6 + h / 3;
                        break;
                    case 2:
                        ow = w / 4;
                        oh = h / 6 + 2 * h / 3;
                        break;
                    case 3:
                        ow = w / 4 + w / 2;
                        oh = h / 6;
                        break;
                    case 4:
                        ow = w / 4 + w / 2;
                        oh = h / 6 + h / 3;
                        break;
                    case 5:
                        ow = w / 4 + w / 2;
                        oh = h / 6 + 2 * h / 3;
                        break;
                    }
                  s->density.value[(oh - 1 - i) * w + ow - 1 - j] = d;
                  s->density.value[(oh - 1 - i) * w + ow     + j] = d;
                  s->density.value[(oh     + i) * w + ow - 1 - j] = d;
                  s->density.value[(oh     + i) * w + ow     + j] = d;
                }
            }
        }
        break;
    case PROJECTION_BARREL:
        // side face
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w * 4 / 5; j++) {
                d = cos(((0.5 + i) / h - 0.5) * DEFAULT_EXPANSION_COEF * M_PI_2);
                s->density.value[i * w + j] = d * d * d;
            }
        }
        // top and bottom
        for (int i = 0; i < h; i++) {
            for (int j = w * 4 / 5; j < w; j++) {
                double dx = DEFAULT_EXPANSION_COEF * (0.5 + j - w * 0.90) / (w * 0.10);
                double dx_squared = dx * dx;

                double top_dy = DEFAULT_EXPANSION_COEF * (0.5 + i - h * 0.25) / (h * 0.25);
                double top_dy_squared = top_dy * top_dy;

                double bottom_dy = DEFAULT_EXPANSION_COEF * (0.5 + i - h * 0.75) / (h * 0.25);
                double bottom_dy_squared = bottom_dy * bottom_dy;

                // normalized distance to the circle center
                r_square = (i < h / 2 ? top_dy_squared : bottom_dy_squared) + dx_squared;
                if (r_square > 1.0)
                    continue;

                cos_square = 1.0 / (r_square + 1.0);
                d = pow(cos_square, 1.5);
                s->density.value[i * w + j] = d;
            }
        }
        break;
    default:
        // TODO: SSIM360_v1
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++)
                s->density.value[i * w + j] = 0;
        }
    }

    switch (s->ref_stereo_format) {
    case STEREO_FORMAT_TB:
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++)
                s->density.value[(i + h) * w + j] = s->density.value[i * w + j];
        }
        break;
    case STEREO_FORMAT_LR:
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++)
                s->density.value[i * w + j + w] = s->density.value[i * w + j];
        }
    }

    return 0;
}

static int config_input_ref(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx  = inlink->dst;
    SSIM360Context *s = ctx->priv;
    int sum = 0;

    s->nb_components = desc->nb_components;

    s->ref_planeheight[0] = inlink->h;
    s->ref_planeheight[3] = inlink->h;
    s->ref_planeheight[1] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->ref_planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);

    s->ref_planewidth[0]  = inlink->w;
    s->ref_planewidth[3]  = inlink->w;
    s->ref_planewidth[1]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->ref_planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);

    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;
    s->comps[0] = s->is_rgb ? 'R' : 'Y';
    s->comps[1] = s->is_rgb ? 'G' : 'U';
    s->comps[2] = s->is_rgb ? 'B' : 'V';
    s->comps[3] = 'A';

    // If chroma computation is disabled, and the format is YUV, skip U & V channels
    if (!s->is_rgb && !s->compute_chroma)
        s->nb_components = 1;

    s->max = (1 << desc->comp[0].depth) - 1;

    s->ssim360_plane = desc->comp[0].depth > 8 ? ssim360_plane_16bit : ssim360_plane_8bit;

    for (int i = 0; i < s->nb_components; i++)
        sum += s->ref_planeheight[i] * s->ref_planewidth[i];
    for (int i = 0; i < s->nb_components; i++)
        s->coefs[i] = (double) s->ref_planeheight[i] * s->ref_planewidth[i] / sum;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext   *ctx = outlink->src;
    SSIM360Context      *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    AVFilterLink  *reflink = ctx->inputs[0];
    FilterLink         *il = ff_filter_link(mainlink);
    FilterLink         *ol = ff_filter_link(outlink);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(outlink->format);
    int ret;

    // Use tape algorithm if any of frame sizes, projections or stereo format are not equal
    if (ctx->inputs[0]->w != ctx->inputs[1]->w || ctx->inputs[0]->h != ctx->inputs[1]->h ||
        s->ref_projection != s->main_projection || s->ref_stereo_format != s->main_stereo_format)
        s->use_tape = 1;

    // Finally, if we have decided to / forced to use tape, check if tape supports both input and output projection
    if (s->use_tape &&
        !(tape_supports_projection(s->main_projection) &&
          tape_supports_projection(s->ref_projection))) {
        av_log(ctx, AV_LOG_ERROR, "Projection is unsupported for the tape based algorithm\n");
        return AVERROR(EINVAL);
    }

    if (s->use_tape) {
        // s->temp will be allocated for the tape width = 8. The tape is long downwards
        s->temp = av_malloc_array((2 * 8 + 12), sizeof(*s->temp));
        if (!s->temp)
            return AVERROR(ENOMEM);

        memset(s->ssim360_percentile_sum, 0, sizeof(s->ssim360_percentile_sum));

        for (int i = 0; i < s->nb_components; i++) {
            FF_ALLOCZ_TYPED_ARRAY(s->ssim360_hist[i], SSIM360_HIST_SIZE);
            if (!s->ssim360_hist[i])
                return AVERROR(ENOMEM);
        }
    } else {
        s->temp = av_malloc_array((2 * reflink->w + 12), sizeof(*s->temp) * (1 + (desc->comp[0].depth > 8)));
        if (!s->temp)
            return AVERROR(ENOMEM);

        if (!s->density.value) {
            ret = generate_density_map(s, reflink->w, reflink->h);
            if (ret < 0)
                return ret;
        }
    }

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    s->fs.opt_shortest   = 1;
    s->fs.opt_repeatlast = 1;

    ret = ff_framesync_configure(&s->fs);
    if (ret < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    SSIM360Context *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SSIM360Context *s = ctx->priv;

    if (s->nb_ssim_frames > 0) {
        char buf[256];
        buf[0] = 0;
        // Log average SSIM360 values
        for (int i = 0; i < s->nb_components; i++) {
            int c = s->is_rgb ? s->rgba_map[i] : i;
            av_strlcatf(buf, sizeof(buf), " %c:%f (%f)", s->comps[i], s->ssim360[c] / s->nb_ssim_frames,
                        ssim360_db(s->ssim360[c], s->nb_ssim_frames));
        }
        av_log(ctx, AV_LOG_INFO, "SSIM360%s All:%f (%f)\n", buf,
               s->ssim360_total / s->nb_ssim_frames, ssim360_db(s->ssim360_total, s->nb_ssim_frames));

        // Log percentiles from histogram when using tape
        if (s->use_tape) {
            for (int p = 0; PERCENTILE_LIST[p] >= 0.0; p++) {
                buf[0] = 0;
                for (int i = 0; i < s->nb_components; i++) {
                    int c = s->is_rgb ? s->rgba_map[i] : i;
                    double ssim360p = s->ssim360_percentile_sum[i][p] / (double)(s->nb_ssim_frames);
                    av_strlcatf(buf, sizeof(buf), " %c:%f (%f)", s->comps[c], ssim360p, ssim360_db(ssim360p, 1));
                }
                av_log(ctx, AV_LOG_INFO, "SSIM360_p%d%s\n", (int)(PERCENTILE_LIST[p] * 100.), buf);
            }
        }
    }

    // free density map
    map_uninit(&s->density);

    map_list_free(&s->heatmaps);

    for (int i = 0; i < s->nb_components; i++) {
        for (int eye = 0; eye < 2; eye++) {
            av_freep(&s->ref_tape_map[i][eye]);
            av_freep(&s->main_tape_map[i][eye]);
        }
        av_freep(&s->ssim360_hist[i]);
    }

    ff_framesync_uninit(&s->fs);

    if (s->stats_file && s->stats_file != stdout)
        fclose(s->stats_file);

    av_freep(&s->temp);
}

#define PF(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf, AV_PIX_FMT_GBR##suf
static const enum AVPixelFormat ssim360_pixfmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_GBRP,
    PF(P9), PF(P10), PF(P12), PF(P14), PF(P16),
    AV_PIX_FMT_NONE
};
#undef PF

static const AVFilterPad ssim360_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_main,
    },
    {
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
};

static const AVFilterPad ssim360_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_vf_ssim360 = {
    .p.name        = "ssim360",
    .p.description = NULL_IF_CONFIG_SMALL("Calculate the SSIM between two 360 video streams."),
    .p.priv_class  = &ssim360_class,
    .preinit       = ssim360_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(SSIM360Context),
    FILTER_INPUTS(ssim360_inputs),
    FILTER_OUTPUTS(ssim360_outputs),
    FILTER_PIXFMTS_ARRAY(ssim360_pixfmts),
};
