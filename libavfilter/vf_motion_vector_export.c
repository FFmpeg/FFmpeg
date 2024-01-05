/**
 * Copyright (c) 2016 Davinder Singh (DSM_) <ds.mudhar<@gmail.com>
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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/imgutils.h"
#include "libavutil/motion_vector.h"
#include "libavcodec/mathops.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "libavutil/video_enc_params.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "internal.h"
#include "video.h"
#include "formats.h"
#include "float.h"

static int video_frame_count = 0;
static int changes;
static int changes_x;
static int changes_y;
static double c_length;

typedef struct MVEContext {
	const AVClass *class;
	int changes;
	int changes_x;
	int changes_y;
	double c_length;
} MVEContext;

#define OFFSET(x) offsetof(MVEContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define CONST(name, help, val, unit) { name, help, 0, AV_OPT_TYPE_CONST, {.i64=val}, 0, 0, FLAGS, unit }

static const AVOption motion_vector_export_options[] = {
	{ "changes", "filtering changes", OFFSET(changes), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS },
	{ "changes_filter_x", "filtering x changes", OFFSET(changes_x), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
	{ "changes_filter_y", "filtering y changes", OFFSET(changes_y), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, FLAGS },
	{ "length", "filtering length of motion vectors", OFFSET(c_length), AV_OPT_TYPE_DOUBLE, {.i64 = 0}, 0, DBL_MAX, FLAGS },
    { NULL }
};

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV410P, AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

AVFILTER_DEFINE_CLASS(motion_vector_export);

static int clip_line(int *sx, int *sy, int *ex, int *ey, int maxx)
{
    if(*sx > *ex)
        return clip_line(ex, ey, sx, sy, maxx);

    if (*sx < 0) {
        if (*ex < 0)
            return 1;
        *sy = *ey + (*sy - *ey) * (int64_t)*ex / (*ex - *sx);
        *sx = 0;
    }

    if (*ex > maxx) {
        if (*sx > maxx)
            return 1;
        *ey = *sy + (*ey - *sy) * (int64_t)(maxx - *sx) / (*ex - *sx);
        *ex = maxx;
    }
    return 0;
}

/**
 * Draw a line from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */

static void draw_line(uint8_t *buf, int sx, int sy, int ex, int ey,
                      int w, int h, int stride, int color)
{
    int x, y, fr, f;

    if (clip_line(&sx, &sy, &ex, &ey, w - 1))
        return;
    if (clip_line(&sy, &sx, &ey, &ex, h - 1))
        return;

    sx = av_clip(sx, 0, w - 1);
    sy = av_clip(sy, 0, h - 1);
    ex = av_clip(ex, 0, w - 1);
    ey = av_clip(ey, 0, h - 1);

    buf[ey * stride + ex] += color;



    if (FFABS(ex - sx) > FFABS(ey - sy)) {
        if (sx > ex) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ex  -= sx;
        f    = ((ey - sy) * (1 << 16)) / ex;
        for (x = 0; x <= ex; x++) {
            y  = (x * f) >> 16;
            fr = (x * f) & 0xFFFF;
                   buf[ y      * stride + x] += (color * (0x10000 - fr)) >> 16;
            if(fr) {
            	buf[(y + 1) * stride + x] += (color *            fr ) >> 16;
            }
        }
    } else {
        if (sy > ey) {
            FFSWAP(int, sx, ex);
            FFSWAP(int, sy, ey);
        }
        buf += sx + sy * stride;
        ey  -= sy;
        if (ey)
            f = ((ex - sx) * (1 << 16)) / ey;
        else
            f = 0;
        for(y= 0; y <= ey; y++){
            x  = (y*f) >> 16;
            fr = (y*f) & 0xFFFF;
                   buf[y * stride + x    ] += (color * (0x10000 - fr)) >> 16;
            if(fr) {
            	buf[y * stride + x + 1] += (color *            fr ) >> 16;
            }
        }
    }
}

/**
 * Draw an arrow from (ex, ey) -> (sx, sy).
 * @param w width of the image
 * @param h height of the image
 * @param stride stride/linesize of the image
 * @param color color of the arrow
 */
static void draw_arrow(uint8_t *buf, int sx, int sy, int ex,
                       int ey, int w, int h, int stride, int color, int tail, int direction)
{
    int dx,dy;

    if (direction) {
        FFSWAP(int, sx, ex);
        FFSWAP(int, sy, ey);
    }

    sx = av_clip(sx, -100, w + 100);
    sy = av_clip(sy, -100, h + 100);
    ex = av_clip(ex, -100, w + 100);
    ey = av_clip(ey, -100, h + 100);

    dx = ex - sx;
    dy = ey - sy;

    if (dx * dx + dy * dy > 3 * 3) {
        int rx =  dx + dy;
        int ry = -dx + dy;
        int length = sqrt((rx * rx + ry * ry) << 8);

        // FIXME subpixel accuracy
        rx = ROUNDED_DIV(rx * (3 << 4), length);
        ry = ROUNDED_DIV(ry * (3 << 4), length);

        if (tail) {
            rx = -rx;
            ry = -ry;
        }

        draw_line(buf, sx, sy, sx + rx, sy + ry, w, h, stride, color);
        draw_line(buf, sx, sy, sx - ry, sy + rx, w, h, stride, color);

    }
    draw_line(buf, sx, sy, ex, ey, w, h, stride, color);
}

static void print(int frame, int counter, int source, int width, int height,
		double src_x, double src_y, double dst_x, double dst_y,
		double length, long long flags, MVEContext *s)
{
	av_log(s, AV_LOG_INFO, "{frame: %3d, mv: %4d, source: %2d, width: %2d, height: %2d, src_x: %5.0f, src_y: %5.0f, dst_x: %5.0f, dst_y: %5.0f, length: %4.2f, flags: 0x%"PRIx64"}\n",
			frame, counter, source,
			width, height, src_x, src_y,
			dst_x, dst_y, length, flags);
}

static void check_conditions(int frame, int counter, int source, int width, int height, double src_x, double src_y, double dst_x, double dst_y,
		double length, long long flags, uint8_t *buf, int linesize, int frame_height, int frame_width, MVEContext *s)
{
	if (changes) {
		if (dst_x - src_x != 0 || dst_y - src_y != 0) {
			if (c_length) {
				if (length >= c_length) {
					print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
                	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
				}
			} else {
				print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
            	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
			}
		}
	} else if (changes_x && changes_y) {
		if (abs(dst_x - src_x) >= changes_x && abs(dst_y - src_y) >= changes_y) {
			if (c_length) {
				if (length >= c_length) {
					print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
                	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
				}
			} else {
				print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
            	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
			}
		}
	} else if (changes_x) {
		if (abs(dst_x - src_x) >= changes_x) {
			if (c_length) {
				if (length >= c_length) {
					print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
                	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
				}
			} else {
				print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
            	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
			}
		}
	} else if (changes_y) {
		if (abs(dst_y - src_y) >= changes_y) {
			if (c_length) {
				if (length >= c_length) {
					print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
                	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
				}
			} else {
				print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
            	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
			}
		}
	} else {
		if (c_length) {
			if (length >= c_length) {
				print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
            	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
			}
		} else {
			print(frame, counter, source, width, height, src_x, src_y, dst_x, dst_y, length, flags, s);
        	draw_arrow(buf, dst_x, dst_y, src_x, src_y, frame_width, frame_height, linesize, 100, 0, source);
		}
	}
}

//length of a vector
static double _length(int src_x, int src_y, int dst_x, int dst_y)
{
	int horizontal = dst_x - src_x;
	int vertical = dst_y - src_y;
	return sqrt(horizontal * horizontal + vertical * vertical);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    MVEContext *s = ctx->priv;
    changes = s->changes;
    changes_x = s->changes_x;
    changes_y = s->changes_y;
    c_length = s->c_length;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFilterContext *ctx = inlink->dst;
	MVEContext *s = ctx->priv;
	AVFilterLink *outlink = ctx->outputs[0];
	int counter = 0;
	int direction;

	AVFrameSideData *sd_;
	video_frame_count++;
	sd_ = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
	if (sd_) {
		int i = 0;
        double length;
        const AVMotionVector *mvs = (const AVMotionVector *)sd_->data;
        for (i = 0; i < sd_->size / sizeof(*mvs); i++) {
        	const AVMotionVector *mv = &mvs[i];
			length = _length(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
            counter++;
            if (mv->source == 1) {
                direction = 1;
            } else {
                direction = 0;
            }
            check_conditions(video_frame_count, counter, direction, mv->w, mv->h, mv->src_x, mv->src_y, mv->dst_x, mv->dst_y,
            		   	   	   length, mv->flags, frame->data[0], frame->linesize[0], frame->height, frame->width, s);
		}
	}

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad motion_vector_export_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
		.flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame  = filter_frame,
        .config_props  = config_input,
    },
};

static const AVFilterPad motion_vector_export_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_motion_vector_export = {
    .name          = "motion_vector_export",
    .description   = NULL_IF_CONFIG_SMALL("Export motion vectors."),
    .priv_size     = sizeof(MVEContext),
    .priv_class    = &motion_vector_export_class,
	.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    FILTER_INPUTS(motion_vector_export_inputs),
    FILTER_OUTPUTS(motion_vector_export_outputs),
	FILTER_SINGLE_PIXFMT(AV_PIX_FMT_YUV420P),
};
