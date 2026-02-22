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

#include "libavutil/film_grain_params.h"
#include "libavutil/mem.h"

static AVFrame *create_frame(enum AVPixelFormat format, int width, int height)
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return NULL;
    frame->format = format;
    frame->width  = width;
    frame->height = height;
    return frame;
}

static AVFilmGrainParams *add_grain(AVFrame *frame,
                                     enum AVFilmGrainParamsType type,
                                     int width, int height,
                                     int sub_x, int sub_y,
                                     int bd_luma, int bd_chroma)
{
    AVFilmGrainParams *fgp = av_film_grain_params_create_side_data(frame);
    if (!fgp)
        return NULL;
    fgp->type             = type;
    fgp->width            = width;
    fgp->height           = height;
    fgp->subsampling_x    = sub_x;
    fgp->subsampling_y    = sub_y;
    fgp->bit_depth_luma   = bd_luma;
    fgp->bit_depth_chroma = bd_chroma;
    return fgp;
}

int main(void)
{
    AVFilmGrainParams *fgp;
    const AVFilmGrainParams *sel;
    AVFrame *frame;
    size_t size;

    printf("Testing av_film_grain_params_alloc()\n");

    fgp = av_film_grain_params_alloc(&size);
    printf("alloc with size: %s\n", (fgp && size > 0) ? "OK" : "FAIL");
    av_free(fgp);

    fgp = av_film_grain_params_alloc(NULL);
    printf("alloc without size: %s\n", fgp ? "OK" : "FAIL");
    av_free(fgp);

    printf("\nTesting av_film_grain_params_create_side_data()\n");

    frame = av_frame_alloc();
    fgp = av_film_grain_params_create_side_data(frame);
    if (fgp)
        printf("create: OK\ndefaults: range=%d pri=%d trc=%d space=%d\n",
               fgp->color_range, fgp->color_primaries,
               fgp->color_trc, fgp->color_space);
    else
        printf("create: FAIL\n");
    av_frame_free(&frame);

    printf("\nTesting av_film_grain_params_select()\n");

    /* invalid format */
    frame = av_frame_alloc();
    frame->format = -1;
    sel = av_film_grain_params_select(frame);
    printf("invalid format: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* no side data */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    sel = av_film_grain_params_select(frame);
    printf("no side data: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* NONE type - skipped */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_NONE, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("NONE type: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* AV1 exact subsampling match (YUV420P: sub 1,1) */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("AV1 match: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* AV1 subsampling mismatch */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 0, 0, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("AV1 sub mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* H274 exact match */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_H274, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("H274 match: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* H274 lower subsampling OK (grain sub 0,0 < YUV420P sub 1,1) */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_H274, 0, 0, 0, 0, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("H274 lower sub: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* H274 higher subsampling FAIL (grain sub 1,1 > YUV444P sub 0,0) */
    frame = create_frame(AV_PIX_FMT_YUV444P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_H274, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("H274 higher sub: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* width too large */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 3840, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("width too large: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* height too large */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 2160, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("height too large: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* width/height = 0 (unspecified) - passes */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("size unspecified: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* bit_depth_luma mismatch (grain=10, YUV420P=8) */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 10, 0);
    sel = av_film_grain_params_select(frame);
    printf("bd_luma mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* bit_depth_chroma mismatch (grain=10, YUV420P=8) */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 10);
    sel = av_film_grain_params_select(frame);
    printf("bd_chroma mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* bit_depth = 0 (unspecified) - passes */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("bd unspecified: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* bit_depth exact match (grain=8, YUV420P=8) */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 8, 8);
    sel = av_film_grain_params_select(frame);
    printf("bd exact match: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* color_range mismatch */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    frame->color_range = AVCOL_RANGE_MPEG;
    fgp = add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    fgp->color_range = AVCOL_RANGE_JPEG;
    sel = av_film_grain_params_select(frame);
    printf("color_range mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* color_primaries mismatch */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    frame->color_primaries = AVCOL_PRI_BT709;
    fgp = add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    fgp->color_primaries = AVCOL_PRI_BT470M;
    sel = av_film_grain_params_select(frame);
    printf("color_primaries mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* color_trc mismatch */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    frame->color_trc = AVCOL_TRC_BT709;
    fgp = add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    fgp->color_trc = AVCOL_TRC_GAMMA22;
    sel = av_film_grain_params_select(frame);
    printf("color_trc mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* color_space mismatch */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    frame->colorspace = AVCOL_SPC_BT709;
    fgp = add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    fgp->color_space = AVCOL_SPC_BT470BG;
    sel = av_film_grain_params_select(frame);
    printf("color_space mismatch: %s\n", sel ? "FAIL" : "NULL");
    av_frame_free(&frame);

    /* color properties UNSPECIFIED - passes */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 0, 0, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("color unspecified: %s\n", sel ? "OK" : "FAIL");
    av_frame_free(&frame);

    /* multiple entries - best selection by size */
    frame = create_frame(AV_PIX_FMT_YUV420P, 1920, 1080);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 640,  480,  1, 1, 0, 0);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 1280, 720,  1, 1, 0, 0);
    add_grain(frame, AV_FILM_GRAIN_PARAMS_AV1, 1000, 1080, 1, 1, 0, 0);
    sel = av_film_grain_params_select(frame);
    printf("best selection: width=%d height=%d\n",
           sel ? sel->width : -1, sel ? sel->height : -1);
    av_frame_free(&frame);

    return 0;
}
