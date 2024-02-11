/*
 * Copyright (C) 2007 by Andrew Zabolotny (author of lensfun, from which this filter derives from)
 * Copyright (C) 2018 Stephen Seo
 *
 * This file is part of FFmpeg.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 * Lensfun filter, applies lens correction with parameters from the lensfun database
 *
 * @see https://lensfun.sourceforge.net/
 */

#include <float.h>
#include <math.h>

#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "video.h"

#include <lensfun.h>

#define LANCZOS_RESOLUTION 256

enum Mode {
    VIGNETTING = 0x1,
    GEOMETRY_DISTORTION = 0x2,
    SUBPIXEL_DISTORTION = 0x4
};

enum InterpolationType {
    NEAREST,
    LINEAR,
    LANCZOS
};

typedef struct VignettingThreadData {
    int width, height;
    uint8_t *data_in;
    int linesize_in;
    int pixel_composition;
    lfModifier *modifier;
} VignettingThreadData;

typedef struct DistortionCorrectionThreadData {
    int width, height;
    const float *distortion_coords;
    const uint8_t *data_in;
    uint8_t *data_out;
    int linesize_in, linesize_out;
    const float *interpolation;
    int mode;
    int interpolation_type;
} DistortionCorrectionThreadData;

typedef struct LensfunContext {
    const AVClass *class;
    const char *make, *model, *lens_model, *db_path;
    int mode;
    float focal_length;
    float aperture;
    float focus_distance;
    float scale;
    int target_geometry;
    int reverse;
    int interpolation_type;

    float *distortion_coords;
    float *interpolation;

    lfLens *lens;
    lfCamera *camera;
    lfModifier *modifier;
} LensfunContext;

#define OFFSET(x) offsetof(LensfunContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption lensfun_options[] = {
    { "make", "set camera maker", OFFSET(make), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "model", "set camera model", OFFSET(model), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "lens_model", "set lens model", OFFSET(lens_model), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "db_path", "set path to database", OFFSET(db_path), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=GEOMETRY_DISTORTION}, 0, VIGNETTING | GEOMETRY_DISTORTION | SUBPIXEL_DISTORTION, FLAGS, .unit = "mode" },
        { "vignetting", "fix lens vignetting", 0, AV_OPT_TYPE_CONST, {.i64=VIGNETTING}, 0, 0, FLAGS, .unit = "mode" },
        { "geometry", "correct geometry distortion", 0, AV_OPT_TYPE_CONST, {.i64=GEOMETRY_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
        { "subpixel", "fix chromatic aberrations", 0, AV_OPT_TYPE_CONST, {.i64=SUBPIXEL_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
        { "vig_geo", "fix lens vignetting and correct geometry distortion", 0, AV_OPT_TYPE_CONST, {.i64=VIGNETTING | GEOMETRY_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
        { "vig_subpixel", "fix lens vignetting and chromatic aberrations", 0, AV_OPT_TYPE_CONST, {.i64=VIGNETTING | SUBPIXEL_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
        { "distortion", "correct geometry distortion and chromatic aberrations", 0, AV_OPT_TYPE_CONST, {.i64=GEOMETRY_DISTORTION | SUBPIXEL_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
        { "all", NULL, 0, AV_OPT_TYPE_CONST, {.i64=VIGNETTING | GEOMETRY_DISTORTION | SUBPIXEL_DISTORTION}, 0, 0, FLAGS, .unit = "mode" },
    { "focal_length", "focal length of video (zoom; constant for the duration of the use of this filter)", OFFSET(focal_length), AV_OPT_TYPE_FLOAT, {.dbl=18}, 0.0, DBL_MAX, FLAGS },
    { "aperture", "aperture (constant for the duration of the use of this filter)", OFFSET(aperture), AV_OPT_TYPE_FLOAT, {.dbl=3.5}, 0.0, DBL_MAX, FLAGS },
    { "focus_distance", "focus distance (constant for the duration of the use of this filter)", OFFSET(focus_distance), AV_OPT_TYPE_FLOAT, {.dbl=1000.0f}, 0.0, DBL_MAX, FLAGS },
    { "scale", "scale factor applied after corrections (0.0 means automatic scaling)", OFFSET(scale), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, DBL_MAX, FLAGS },
    { "target_geometry", "target geometry of the lens correction (only when geometry correction is enabled)", OFFSET(target_geometry), AV_OPT_TYPE_INT, {.i64=LF_RECTILINEAR}, 0, INT_MAX, FLAGS, .unit = "lens_geometry" },
        { "rectilinear", "rectilinear lens (default)", 0, AV_OPT_TYPE_CONST, {.i64=LF_RECTILINEAR}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "fisheye", "fisheye lens", 0, AV_OPT_TYPE_CONST, {.i64=LF_FISHEYE}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "panoramic", "panoramic (cylindrical)", 0, AV_OPT_TYPE_CONST, {.i64=LF_PANORAMIC}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "equirectangular", "equirectangular", 0, AV_OPT_TYPE_CONST, {.i64=LF_EQUIRECTANGULAR}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "fisheye_orthographic", "orthographic fisheye", 0, AV_OPT_TYPE_CONST, {.i64=LF_FISHEYE_ORTHOGRAPHIC}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "fisheye_stereographic", "stereographic fisheye", 0, AV_OPT_TYPE_CONST, {.i64=LF_FISHEYE_STEREOGRAPHIC}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "fisheye_equisolid", "equisolid fisheye", 0, AV_OPT_TYPE_CONST, {.i64=LF_FISHEYE_EQUISOLID}, 0, 0, FLAGS, .unit = "lens_geometry" },
        { "fisheye_thoby", "fisheye as measured by thoby", 0, AV_OPT_TYPE_CONST, {.i64=LF_FISHEYE_THOBY}, 0, 0, FLAGS, .unit = "lens_geometry" },
    { "reverse", "Does reverse correction (regular image to lens distorted)", OFFSET(reverse), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { "interpolation", "Type of interpolation", OFFSET(interpolation_type), AV_OPT_TYPE_INT, {.i64=LINEAR}, 0, LANCZOS, FLAGS, .unit = "interpolation" },
        { "nearest", NULL, 0, AV_OPT_TYPE_CONST, {.i64=NEAREST}, 0, 0, FLAGS, .unit = "interpolation" },
        { "linear", NULL, 0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, .unit = "interpolation" },
        { "lanczos", NULL, 0, AV_OPT_TYPE_CONST, {.i64=LANCZOS}, 0, 0, FLAGS, .unit = "interpolation" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(lensfun);

static av_cold int init(AVFilterContext *ctx)
{
    LensfunContext *lensfun = ctx->priv;
    lfDatabase *db;
    const lfCamera **cameras;
    const lfLens **lenses;

    db = lf_db_create();
    if ((lensfun->db_path ? lf_db_load_path(db, lensfun->db_path) : lf_db_load(db)) != LF_NO_ERROR) {
        lf_db_destroy(db);
        av_log(ctx, AV_LOG_FATAL, "Failed to load lensfun database from %s path\n",
               lensfun->db_path ? lensfun->db_path : "default");
        return AVERROR_INVALIDDATA;
    }

    if (!lensfun->make || !lensfun->model) {
        const lfCamera *const *cameras = lf_db_get_cameras(db);

        av_log(ctx, AV_LOG_FATAL, "Option \"make\" or option \"model\" not specified\n");
        av_log(ctx, AV_LOG_INFO, "Available values for \"make\" and \"model\":\n");
        for (int i = 0; cameras && cameras[i]; i++)
            av_log(ctx, AV_LOG_INFO, "\t%s\t%s\n", cameras[i]->Maker, cameras[i]->Model);
        lf_db_destroy(db);
        return AVERROR(EINVAL);
    } else if (!lensfun->lens_model) {
        const lfLens *const *lenses = lf_db_get_lenses(db);

        av_log(ctx, AV_LOG_FATAL, "Option \"lens_model\" not specified\n");
        av_log(ctx, AV_LOG_INFO, "Available values for \"lens_model\":\n");
        for (int i = 0; lenses && lenses[i]; i++)
            av_log(ctx, AV_LOG_INFO, "\t%s\t(make %s)\n", lenses[i]->Model, lenses[i]->Maker);
        lf_db_destroy(db);
        return AVERROR(EINVAL);
    }

    lensfun->lens = lf_lens_create();
    lensfun->camera = lf_camera_create();

    cameras = lf_db_find_cameras(db, lensfun->make, lensfun->model);
    if (cameras && *cameras) {
        lf_camera_copy(lensfun->camera, *cameras);
        av_log(ctx, AV_LOG_INFO, "Using camera %s\n", lensfun->camera->Model);
    } else {
        lf_free(cameras);
        lf_db_destroy(db);
        av_log(ctx, AV_LOG_FATAL, "Failed to find camera in lensfun database\n");
        return AVERROR_INVALIDDATA;
    }
    lf_free(cameras);

    lenses = lf_db_find_lenses(db, lensfun->camera, NULL, lensfun->lens_model, 0);
    if (lenses && *lenses) {
        lf_lens_copy(lensfun->lens, *lenses);
        av_log(ctx, AV_LOG_INFO, "Using lens %s\n", lensfun->lens->Model);
    } else {
        lf_free(lenses);
        lf_db_destroy(db);
        av_log(ctx, AV_LOG_FATAL, "Failed to find lens in lensfun database\n");
        return AVERROR_INVALIDDATA;
    }
    lf_free(lenses);

    lf_db_destroy(db);
    return 0;
}

static float lanczos_kernel(float x)
{
    if (x == 0.0f) {
        return 1.0f;
    } else if (x > -2.0f && x < 2.0f) {
        return (2.0f * sin(M_PI * x) * sin(M_PI / 2.0f * x)) / (M_PI * M_PI * x * x);
    } else {
        return 0.0f;
    }
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LensfunContext *lensfun = ctx->priv;
    int index;
    float a;

    if (!lensfun->modifier) {
        if (lensfun->camera && lensfun->lens) {
            lensfun->modifier = lf_modifier_create(lensfun->lens,
                                                   lensfun->focal_length,
                                                   lensfun->camera->CropFactor,
                                                   inlink->w,
                                                   inlink->h, LF_PF_U8, lensfun->reverse);
            if (lensfun->mode & VIGNETTING)
                lf_modifier_enable_vignetting_correction(lensfun->modifier, lensfun->aperture, lensfun->focus_distance);
            if (lensfun->mode & GEOMETRY_DISTORTION) {
                lf_modifier_enable_distortion_correction(lensfun->modifier);
                lf_modifier_enable_projection_transform(lensfun->modifier, lensfun->target_geometry);
                lf_modifier_enable_scaling(lensfun->modifier, lensfun->scale);
            }
            if (lensfun->mode & SUBPIXEL_DISTORTION)
                lf_modifier_enable_tca_correction(lensfun->modifier);
        } else {
            // lensfun->camera and lensfun->lens should have been initialized
            return AVERROR_BUG;
        }
    }

    if (!lensfun->distortion_coords) {
        if (lensfun->mode & SUBPIXEL_DISTORTION) {
            lensfun->distortion_coords = av_malloc_array(inlink->w * inlink->h, sizeof(float) * 2 * 3);
            if (!lensfun->distortion_coords)
                return AVERROR(ENOMEM);
            if (lensfun->mode & GEOMETRY_DISTORTION) {
                // apply both geometry and subpixel distortion
                lf_modifier_apply_subpixel_geometry_distortion(lensfun->modifier,
                                                               0, 0,
                                                               inlink->w, inlink->h,
                                                               lensfun->distortion_coords);
            } else {
                // apply only subpixel distortion
                lf_modifier_apply_subpixel_distortion(lensfun->modifier,
                                                      0, 0,
                                                      inlink->w, inlink->h,
                                                      lensfun->distortion_coords);
            }
        } else if (lensfun->mode & GEOMETRY_DISTORTION) {
            lensfun->distortion_coords = av_malloc_array(inlink->w * inlink->h, sizeof(float) * 2);
            if (!lensfun->distortion_coords)
                return AVERROR(ENOMEM);
            // apply only geometry distortion
            lf_modifier_apply_geometry_distortion(lensfun->modifier,
                                                  0, 0,
                                                  inlink->w, inlink->h,
                                                  lensfun->distortion_coords);
        }
    }

    if (!lensfun->interpolation)
        if (lensfun->interpolation_type == LANCZOS) {
            lensfun->interpolation = av_malloc_array(LANCZOS_RESOLUTION, sizeof(float) * 4);
            if (!lensfun->interpolation)
                return AVERROR(ENOMEM);
            for (index = 0; index < 4 * LANCZOS_RESOLUTION; ++index) {
                if (index == 0) {
                    lensfun->interpolation[index] = 1.0f;
                } else {
                    a = sqrtf((float)index / LANCZOS_RESOLUTION);
                    lensfun->interpolation[index] = lanczos_kernel(a);
                }
            }
        }

    return 0;
}

static int vignetting_filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const VignettingThreadData *thread_data = arg;
    const int slice_start = thread_data->height *  jobnr      / nb_jobs;
    const int slice_end   = thread_data->height * (jobnr + 1) / nb_jobs;

    lf_modifier_apply_color_modification(thread_data->modifier,
                                         thread_data->data_in + slice_start * thread_data->linesize_in,
                                         0,
                                         slice_start,
                                         thread_data->width,
                                         slice_end - slice_start,
                                         thread_data->pixel_composition,
                                         thread_data->linesize_in);

    return 0;
}

static float square(float x)
{
    return x * x;
}

static int distortion_correction_filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    const DistortionCorrectionThreadData *thread_data = arg;
    const int slice_start = thread_data->height *  jobnr      / nb_jobs;
    const int slice_end   = thread_data->height * (jobnr + 1) / nb_jobs;

    int x, y, i, j, rgb_index;
    float interpolated, new_x, new_y, d, norm;
    int new_x_int, new_y_int;
    for (y = slice_start; y < slice_end; ++y)
        for (x = 0; x < thread_data->width; ++x)
            for (rgb_index = 0; rgb_index < 3; ++rgb_index) {
                if (thread_data->mode & SUBPIXEL_DISTORTION) {
                    // subpixel (and possibly geometry) distortion correction was applied, correct distortion
                    switch(thread_data->interpolation_type) {
                    case NEAREST:
                        new_x_int = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2]     + 0.5f;
                        new_y_int = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2 + 1] + 0.5f;
                        if (new_x_int < 0 || new_x_int >= thread_data->width || new_y_int < 0 || new_y_int >= thread_data->height) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = thread_data->data_in[new_x_int * 3 + rgb_index + new_y_int * thread_data->linesize_in];
                        }
                        break;
                    case LINEAR:
                        interpolated = 0.0f;
                        new_x = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2];
                        new_x_int = new_x;
                        new_y = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2 + 1];
                        new_y_int = new_y;
                        if (new_x_int < 0 || new_x_int + 1 >= thread_data->width || new_y_int < 0 || new_y_int + 1 >= thread_data->height) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] =
                                  thread_data->data_in[ new_x_int      * 3 + rgb_index +  new_y_int      * thread_data->linesize_in] * (new_x_int + 1 - new_x) * (new_y_int + 1 - new_y)
                                + thread_data->data_in[(new_x_int + 1) * 3 + rgb_index +  new_y_int      * thread_data->linesize_in] * (new_x - new_x_int) * (new_y_int + 1 - new_y)
                                + thread_data->data_in[ new_x_int      * 3 + rgb_index + (new_y_int + 1) * thread_data->linesize_in] * (new_x_int + 1 - new_x) * (new_y - new_y_int)
                                + thread_data->data_in[(new_x_int + 1) * 3 + rgb_index + (new_y_int + 1) * thread_data->linesize_in] * (new_x - new_x_int) * (new_y - new_y_int);
                        }
                        break;
                    case LANCZOS:
                        interpolated = 0.0f;
                        norm = 0.0f;
                        new_x = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2];
                        new_x_int = new_x;
                        new_y = thread_data->distortion_coords[x * 2 * 3 + y * thread_data->width * 2 * 3 + rgb_index * 2 + 1];
                        new_y_int = new_y;
                        for (j = 0; j < 4; ++j)
                            for (i = 0; i < 4; ++i) {
                                if (new_x_int + i - 2 < 0 || new_x_int + i - 2 >= thread_data->width || new_y_int + j - 2 < 0 || new_y_int + j - 2 >= thread_data->height)
                                    continue;
                                d = square(new_x - (new_x_int + i - 2)) * square(new_y - (new_y_int + j - 2));
                                if (d >= 4.0f)
                                    continue;
                                d = thread_data->interpolation[(int)(d * LANCZOS_RESOLUTION)];
                                norm += d;
                                interpolated += thread_data->data_in[(new_x_int + i - 2) * 3 + rgb_index + (new_y_int + j - 2) * thread_data->linesize_in] * d;
                            }
                        if (norm == 0.0f) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            interpolated /= norm;
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = interpolated < 0.0f ? 0.0f : interpolated > 255.0f ? 255.0f : interpolated;
                        }
                        break;
                    }
                } else if (thread_data->mode & GEOMETRY_DISTORTION) {
                    // geometry distortion correction was applied, correct distortion
                    switch(thread_data->interpolation_type) {
                    case NEAREST:
                        new_x_int = thread_data->distortion_coords[x * 2 + y * thread_data->width * 2]     + 0.5f;
                        new_y_int = thread_data->distortion_coords[x * 2 + y * thread_data->width * 2 + 1] + 0.5f;
                        if (new_x_int < 0 || new_x_int >= thread_data->width || new_y_int < 0 || new_y_int >= thread_data->height) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = thread_data->data_in[new_x_int * 3 + rgb_index + new_y_int * thread_data->linesize_in];
                        }
                        break;
                    case LINEAR:
                        interpolated = 0.0f;
                        new_x = thread_data->distortion_coords[x * 2 + y * thread_data->width * 2];
                        new_x_int = new_x;
                        new_y = thread_data->distortion_coords[x * 2 + y * thread_data->width * 2 + 1];
                        new_y_int = new_y;
                        if (new_x_int < 0 || new_x_int + 1 >= thread_data->width || new_y_int < 0 || new_y_int + 1 >= thread_data->height) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] =
                                  thread_data->data_in[ new_x_int      * 3 + rgb_index +  new_y_int      * thread_data->linesize_in] * (new_x_int + 1 - new_x) * (new_y_int + 1 - new_y)
                                + thread_data->data_in[(new_x_int + 1) * 3 + rgb_index +  new_y_int      * thread_data->linesize_in] * (new_x - new_x_int) * (new_y_int + 1 - new_y)
                                + thread_data->data_in[ new_x_int      * 3 + rgb_index + (new_y_int + 1) * thread_data->linesize_in] * (new_x_int + 1 - new_x) * (new_y - new_y_int)
                                + thread_data->data_in[(new_x_int + 1) * 3 + rgb_index + (new_y_int + 1) * thread_data->linesize_in] * (new_x - new_x_int) * (new_y - new_y_int);
                        }
                        break;
                    case LANCZOS:
                        interpolated = 0.0f;
                        norm = 0.0f;
                        new_x = thread_data->distortion_coords[x * 2     + y * thread_data->width * 2];
                        new_x_int = new_x;
                        new_y = thread_data->distortion_coords[x * 2 + 1 + y * thread_data->width * 2];
                        new_y_int = new_y;
                        for (j = 0; j < 4; ++j)
                            for (i = 0; i < 4; ++i) {
                                if (new_x_int + i - 2 < 0 || new_x_int + i - 2 >= thread_data->width || new_y_int + j - 2 < 0 || new_y_int + j - 2 >= thread_data->height)
                                    continue;
                                d = square(new_x - (new_x_int + i - 2)) * square(new_y - (new_y_int + j - 2));
                                if (d >= 4.0f)
                                    continue;
                                d = thread_data->interpolation[(int)(d * LANCZOS_RESOLUTION)];
                                norm += d;
                                interpolated += thread_data->data_in[(new_x_int + i - 2) * 3 + rgb_index + (new_y_int + j - 2) * thread_data->linesize_in] * d;
                            }
                        if (norm == 0.0f) {
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = 0;
                        } else {
                            interpolated /= norm;
                            thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = interpolated < 0.0f ? 0.0f : interpolated > 255.0f ? 255.0f : interpolated;
                        }
                        break;
                    }
                } else {
                    // no distortion correction was applied
                    thread_data->data_out[x * 3 + rgb_index + y * thread_data->linesize_out] = thread_data->data_in[x * 3 + rgb_index + y * thread_data->linesize_in];
                }
            }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LensfunContext *lensfun = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out;
    VignettingThreadData vignetting_thread_data;
    DistortionCorrectionThreadData distortion_correction_thread_data;
    int ret;

    if (lensfun->mode & VIGNETTING) {
        ret = ff_inlink_make_frame_writable(inlink, &in);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }

        vignetting_thread_data = (VignettingThreadData) {
            .width = inlink->w,
            .height = inlink->h,
            .data_in = in->data[0],
            .linesize_in = in->linesize[0],
            .pixel_composition = LF_CR_3(RED, GREEN, BLUE),
            .modifier = lensfun->modifier
        };

        ff_filter_execute(ctx, vignetting_filter_slice,
                          &vignetting_thread_data, NULL,
                          FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));
    }

    if (lensfun->mode & (GEOMETRY_DISTORTION | SUBPIXEL_DISTORTION)) {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);

        distortion_correction_thread_data = (DistortionCorrectionThreadData) {
            .width = inlink->w,
            .height = inlink->h,
            .distortion_coords = lensfun->distortion_coords,
            .data_in = in->data[0],
            .data_out = out->data[0],
            .linesize_in = in->linesize[0],
            .linesize_out = out->linesize[0],
            .interpolation = lensfun->interpolation,
            .mode = lensfun->mode,
            .interpolation_type = lensfun->interpolation_type
        };

        ff_filter_execute(ctx, distortion_correction_filter_slice,
                          &distortion_correction_thread_data, NULL,
                          FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

        av_frame_free(&in);
        return ff_filter_frame(outlink, out);
    } else {
        return ff_filter_frame(outlink, in);
    }
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LensfunContext *lensfun = ctx->priv;

    if (lensfun->camera)
        lf_camera_destroy(lensfun->camera);
    if (lensfun->lens)
        lf_lens_destroy(lensfun->lens);
    if (lensfun->modifier)
        lf_modifier_destroy(lensfun->modifier);
    av_freep(&lensfun->distortion_coords);
    av_freep(&lensfun->interpolation);
}

static const AVFilterPad lensfun_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_lensfun = {
    .name          = "lensfun",
    .description   = NULL_IF_CONFIG_SMALL("Apply correction to an image based on info derived from the lensfun database."),
    .priv_size     = sizeof(LensfunContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(lensfun_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_RGB24),
    .priv_class    = &lensfun_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
