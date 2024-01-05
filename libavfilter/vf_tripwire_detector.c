/**
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
 *
 * All the MV drawing code from Michael Niedermayer is extracted from
 * libavcodec/mpegvideo.c.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "math.h"
#include "float.h"
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/colorspace.h"
#include "libavcodec/mathops.h"
#include "libavutil/common.h"
#include "libavutil/parseutils.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/eval.h"
#include "libavutil/pixdesc.h"
#include "libavutil/motion_vector.h"
#include "libavutil/video_enc_params.h"
#include "libavformat/url.h"
#include "libavformat/http.h"
#include "libavformat/avio.h"
#include "motion_estimation.h"
#include "avfilter.h"
#include "formats.h"
#include "drawutils.h"
#include "internal.h"
#include "video.h"

#define PI 3.14159265358979323846
#define SIZE 50000

static const char* version = "1.03.01";
static const char* release_date = "2022.09.02";
static int upper_x, upper_y, down_x, down_y;
static int video_frame_count = 0;
static int obj_counter = 0; ///< number of objects on a frame
static int signal = 0;		///< if > 0 we have to inspect a frame more than once
static int counter = 0;
static int id_counter = 0;
static int printed_counter = 0;
static int printed_ids[500];
static int ids[SIZE];
static char cross_direction[10];

uint8_t history_rgba_color[4]; ///< rgba for the box history
unsigned char history_yuv_color[4]; ///< yuv for the box history

static const char *const var_names[] = {
    "in_w", "iw",   ///< width  of the input video
    "in_h", "ih",	///< height of the input video
    NULL
};

enum var_name {
    VAR_IN_W,  VAR_IW,
    VAR_IN_H,  VAR_IH,
	VARS_NB
};

enum { Y, U, V, A };
enum { R, G, B };

struct TDContext;
typedef int (*PixelBelongsToRegion)(struct TDContext *s, int x, int y);

typedef struct TDContext {
	URLContext *uc;
	unsigned char* buffer;
	int bytes;
	const AVClass *class;
    uint8_t rgba_map[4]; ///< color maps
    uint8_t intersect_rgba_color[4];
    unsigned char intersect_yuv_color[4];
    uint8_t box_rgba_color[4];
    unsigned char box_yuv_color[4];
	char *object_marker_box_intersect_color; ///< variables for the input parameters
	char *object_marker_box_color;
	double angle;
	double angle_range;
	int max_distance;
	int min_obj_area;
	int min_mv;
	int start_x;
	int start_y;
	int end_x;
	int end_y;
	int tripwire_marker_line;
	int object_marker_box;
	int object_marker_box_history;
    int thickness;
    int object_marker_info;
    int print_only_intersect_trigger;
    int detection_threshold;
    int line_break;
    int parameters;
	AVExpr *area_pexpr;	///< parsed expressions for the parameters
	AVExpr *start_x_pexpr;
	AVExpr *start_y_pexpr;
	AVExpr *end_x_pexpr;
	AVExpr *end_y_pexpr;
	AVExpr *distance_pexpr;
	AVExpr *tripwire_center_x_pexpr;
	AVExpr *tripwire_center_y_pexpr;
	char *area_expr;	///< expressions for the parameters
	char *start_x_expr;
	char *start_y_expr;
	char *end_x_expr;
	char *end_y_expr;
	char *distance_expr;
	char *tripwire_center_x_expr;
	char *tripwire_center_y_expr;
    int vsub, hsub; ///< helping variables for the color
    int step_;
    int step;
    int invert_color;
    int x;
    int y;
    int w;
    int h;
    int filter_id; ///< unique id for the filter
    const char* url;
    double tripwire_line_angle;
    int tripwire_line_center_x;
    int tripwire_line_center_y;
    int std_err_text_output_enable;
    int mv_resample;
    double var_values[VARS_NB];
} TDContext;


typedef struct ResampledMV
{
	double length;
	double angle;
	int src_x;
	int src_y;
	int dst_x;
	int dst_y;
	int direction;
	int flag;
	int zeros;
	int ones;
	int	lower_x;
	int upper_x;
	int lower_y;
	int upper_y;
	int counter;
	int center_x;
	int center_y;
}ResampledMV;

/**
 * Struct to store a single object
 *
 */
typedef struct Object {
	double area;
	double average_angle;
	double average_length;
	int x1_arrow; ///< helping coordinates for the arrow inside the object
	int x2_arrow;
	int y1_arrow;
	int y2_arrow;
	int x_endp;
	int y_endp;
	int distance_from_center;
	int mv_box_num; ///< number of macroblocks in the video
	int framenum;
	int counter;
	int id;
	int center_x;
	int center_y;
	int x_min;
	int y_min;
	int x_max;
	int y_max;
	int src_x_s[SIZE];
	int src_y_s[SIZE];
	int dst_x_s[SIZE];
	int dst_y_s[SIZE];
	int directions[3];
	int intersect;
	int crossed;
	int dir_counter;
	int side;	///< 1 - from left or down to the tripwire, -1 - from right or above the tripwire
} Object;

Object *every_object[SIZE]; ///< storage for every object in the video
Object *objects_with_id[SIZE]; ///< storage for the IDs

#define OFFSET(x) offsetof(TDContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM|AVFILTER_FLAG_DYNAMIC_OUTPUTS

#define ASSIGN_THREE_CHANNELS                                        \
    row[0] = frame->data[0] +  y               * frame->linesize[0]; \
    row[1] = frame->data[1] + (y >> ctx->vsub) * frame->linesize[1]; \
    row[2] = frame->data[2] + (y >> ctx->vsub) * frame->linesize[2];

static const AVOption tripwire_detector_options[] = {
		{"start_x", "starting x coordinate", OFFSET(start_x_expr), AV_OPT_TYPE_STRING, {.str = "iw/2"}, 0, 0, FLAGS },
		{"start_y", "starting y coordinate", OFFSET(start_y_expr), AV_OPT_TYPE_STRING, {.str = "0"}, 0, 0, FLAGS },
		{"end_x", "ending x coordinate", OFFSET(end_x_expr), AV_OPT_TYPE_STRING, {.str = "iw/2"}, 0, 0, FLAGS },
		{"end_y", "ending y coordinate", OFFSET(end_y_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, 0, 0, FLAGS },
		{"max_mv_distance_filter", "maximum distance between objects", OFFSET(distance_expr), AV_OPT_TYPE_STRING, {.str = "50"}, 0, 0, FLAGS },
		{"min_mv_num_filter", "minimum number of motion vectors per object", OFFSET(min_mv), AV_OPT_TYPE_INT, {.i64 = 5}, 1, INT_MAX, FLAGS },
		{"angle_filter", "set the angle of the object", OFFSET(angle), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 360, FLAGS },
		{"angle_filter_range", "set the allowed range of the angle", OFFSET(angle_range), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 360, FLAGS },
		{"tripwire_marker_line", "set the tripwire visibility", OFFSET(tripwire_marker_line), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
		{"object_marker_box", "set the object marker box visibility", OFFSET(object_marker_box), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
		{"object_marker_box_intersect_color", "set the object marker box intersect color", OFFSET(object_marker_box_intersect_color), AV_OPT_TYPE_STRING, {.str = "red"}, 0, 0, FLAGS },
		{"object_marker_box_color", "set the object marker box color", OFFSET(object_marker_box_color), AV_OPT_TYPE_STRING, {.str = "white"}, 0, 0, FLAGS },
		{"min_obj_area_filter", "set the minimum area of the object", OFFSET(area_expr), AV_OPT_TYPE_STRING, {.str = "iw*ih/40"}, 0, 0, FLAGS },
		{"object_marker_box_thickness", "set the object marker box thickness", OFFSET(thickness), AV_OPT_TYPE_INT, { .i64 = 3 }, 0, 200, FLAGS },
		{"json_output_line_break", "set the output line breaks", OFFSET(line_break), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
		{"object_marker_info", "set object marker information", OFFSET(object_marker_info), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
		{"print_only_intersect_trigger", "print only on intersect and once per object", OFFSET(print_only_intersect_trigger), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
		{"object_marker_box_history", "set the object marker box history visibility", OFFSET(object_marker_box_history), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
		{"object_detection_threshold", "set the threshold of the minimum number of appearance of an object", OFFSET(detection_threshold), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 50, FLAGS },
		{"parameter_summary_row", "print a highlight about the set parameters", OFFSET(parameters), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
		{"url", "url to send data", OFFSET(url), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
		{"tripwire_line_angle", "set the angle for the tripwire", OFFSET(tripwire_line_angle), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 360, FLAGS },
		{"tripwire_line_center_x", "x-coordinate of the tripwire's center point", OFFSET(tripwire_center_x_expr), AV_OPT_TYPE_STRING, {.str = "iw/2"}, 0, 0, FLAGS },
		{"tripwire_line_center_y", "y-coordinate of the tripwire's center point", OFFSET(tripwire_center_y_expr), AV_OPT_TYPE_STRING, {.str = "ih/2"}, 0, 0, FLAGS },
		{"std_err_text_output_enable", "Enable text output on std err", OFFSET(std_err_text_output_enable), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
		{"step", "Set the step of the iteration", OFFSET(step), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 500, FLAGS },
		{"mv_resample", "Set the step of the iteration", OFFSET(mv_resample), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 500, FLAGS },
		{ NULL } };

AVFILTER_DEFINE_CLASS(tripwire_detector);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV440P,  AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_RGB24,  AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,   AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,   AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB,   AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0,   AV_PIX_FMT_BGR0,
    AV_PIX_FMT_NONE
};

//deciding which side a point to a line lies
static int which_side(int line_x1, int line_y1, int line_x2, int line_y2, int point_x1, int point_y1)
{
	if ((line_x2 - line_x1) * (point_y1 - line_y1) - (line_y2 - line_y1) * (point_x1 - line_x1) > 0) {
		return -1; //to left or under
	} else if ((line_x2 - line_x1) * (point_y1 - line_y1) - (line_y2 - line_y1) * (point_x1 - line_x1) < 0) {
		return 1; //to right or above
	}
	return 0;
}

/**
 * Draw a box from the point (left, top).
 * @param right width of the box
 * @param down height of the box
 */

static void draw_box(AVFrame *frame, TDContext *ctx, int left, int top, int right, int down,
                        PixelBelongsToRegion pixel_belongs_to_region,  unsigned char yuv_color[4])
{
    unsigned char *row[4];
    int x, y;
	for (y = top; y < down; y++) {
	    ASSIGN_THREE_CHANNELS
	    for (x = left; x < right; x++) {
	        double alpha = (double)yuv_color[A] / 255;

	        if (pixel_belongs_to_region(ctx, x, y)) {
	        row[0][x             ] = (1 - alpha) * row[0][x             ] + alpha * yuv_color[Y];
	        row[1][x >> ctx->hsub] = (1 - alpha) * row[1][x >> ctx->hsub] + alpha * yuv_color[U];
	        row[2][x >> ctx->hsub] = (1 - alpha) * row[2][x >> ctx->hsub] + alpha * yuv_color[V];
	        }
	     }
	}
}

static int http_write(URLContext *h, const uint8_t *buf, int size)
{
    char temp[11] = "";
    int ret;
    char crlf[] = "\r\n";
    //HTTPContext *s = h->priv_data;

    if (size > 0) {
        snprintf(temp, sizeof(temp), "%x\r\n", size);

        if ((ret = ffurl_write(h, temp, strlen(temp))) < 0 ||
            (ret = ffurl_write(h, buf, size)) < 0          ||
            (ret = ffurl_write(h, crlf, sizeof(crlf) - 1)) < 0)
            return ret;
    }
    return size;
}

//opening the connection to the given url and allocating the URLContext
static int open_connection(TDContext *s, const char* filename)
{
	int ret;

    if ((ret = ffurl_alloc(&s->uc, filename, AVIO_FLAG_WRITE, NULL)) < 0) {
    	return AVERROR(EINVAL);
    }

    //setting up POST request
    if ((ret = ffurl_connect(s->uc, NULL)) < 0) {
    	return AVERROR(EINVAL);
    }

    return ret;
}

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
 * Initialize object
 */
static Object* create_object(void)
{
	Object* o = malloc(sizeof *o);
	o->counter = 0;
	o->average_angle = 0;
	o->average_length = 0;
	o->id = -1;
	o->intersect = 0;
	o->crossed = 0;
	o->dir_counter = 0;
	for (int i = 0; i < 3; i++) {
		o->directions[i] = -2;
	}
	return o;
}

static Object* create_empty_object(void)
{
	Object* o = malloc(sizeof *o);
	return o;
}

/**
 * Calculate the distance between two points
 */
static double distance(int x_1, int y_1, int x_2, int y_2)
{
	return sqrt(pow(x_1 - x_2, 2) + pow(y_1 - y_2, 2));
}

/**
 * Calculate the length of a vector
 */
static double _length(int src_x, int src_y, int dst_x, int dst_y)
{
	int horizontal = dst_x - src_x;
	int vertical = (dst_y - src_y) * (-1);
	return sqrt(horizontal * horizontal + vertical * vertical);
}

/**
 * Check if two line segments intersect
 */
static int get_line_intersection(int wire_start_x, int wire_start_y, int wire_end_x, int wire_end_y,
	int mv_src_x, int mv_src_y, int mv_dst_x, int mv_dst_y, TDContext *s) {
    double slope_x1, slope_y1, slope_x2, slope_y2, q, t;

    slope_x1 = wire_end_x - wire_start_x;   slope_y1 = wire_end_y - wire_start_y;
    slope_x2 = mv_dst_x - mv_src_x;     	slope_y2 = mv_dst_y - mv_src_y;

    q = (-slope_y1 * (wire_start_x - mv_src_x) + slope_x1 * (wire_start_y - mv_src_y)) / (-slope_x2 * slope_y1 + slope_x1 * slope_y2);
    t = ( slope_x2 * (wire_start_y - mv_src_y) - slope_y2 * (wire_start_x - mv_src_x)) / (-slope_x2 * slope_y1 + slope_x1 * slope_y2);

    //if a motion vector of an object crosses the tripwire return 1, else 0
    if (q >= -0.5 && q <= 0.5 && t >= -0.5 && t <= 0.5) {
        return 1;
    }
    return 0;
}

/**
 * Check if a vector belongs to an object. If it fulfils the condition, it is added to the object.
 */
static int compare_vectors(Object *o, int src_x, int src_y, int dst_x, int dst_y, TDContext *s, double length, double angle)
{
	int tmp = o->counter;
	for(int j = 0; j < tmp; j += 1) {
		if (distance(o->src_x_s[j], o->src_y_s[j], src_x, src_y) <= s->max_distance ||
			distance(o->src_x_s[j], o->src_y_s[j], dst_x, dst_y) <= s->max_distance ||
			distance(o->dst_x_s[j], o->dst_y_s[j], dst_x, dst_y) <= s->max_distance ||
			distance(o->dst_x_s[j], o->dst_y_s[j], src_x, src_y) <= s->max_distance) {
			o->src_x_s[tmp] = src_x;
			o->src_y_s[tmp] = src_y;
			o->dst_x_s[tmp] = dst_x;
			o->dst_y_s[tmp] = dst_y;
			o->framenum = video_frame_count;
			o->average_angle += angle;
			o->average_length += length;
			o->counter++;
			return 1;
		}
	}
	signal++;
	return 0;
}

/**
 * Check if a pixel belongs to the box border
 */
static av_pure av_always_inline int pixel_belongs_to_box(TDContext *s, int x, int y)
{
    return (y - s->y < s->thickness) || (s->y + s->h - 1 - y < s->thickness) ||
           (x - s->x < s->thickness) || (s->x + s->w - 1 - x < s->thickness);
}

/**
 * Function to draw the objects
 * @param x_max, y_max, x_min, y_min upper-left and down-right points of the object
 * @param intersect variable to store whether the object intersects the tripwire or not
 */
static void draw_object(AVFrame *frame, Object *obj, TDContext *s)
{
	//drawing the box, calculate block size
	s->x = obj->x_min;
	s->y = obj->y_min;
	s->w = distance(obj->x_min, obj->y_min, obj->x_max, obj->y_min);
	s->h = distance(obj->x_min, obj->y_min, obj->x_min, obj->y_max);
	//draw arrow
	if (s->object_marker_info) {
		//arrow line thickness
		for (int i = 0; i < 5; i++) {
			// drawing the arrow with the direction into the box
			if (obj->distance_from_center < 0) {
				draw_line(frame->data[0], FFMAX(obj->x_endp, obj->x_min), FFMAX(obj->y_endp, obj->y_min), FFMIN(obj->center_x - obj->distance_from_center, obj->x_max), obj->center_y, frame->width, frame->height, frame->linesize[0], 100);
				draw_line(frame->data[0], FFMAX(obj->x1_arrow, obj->x_min), FFMAX(obj->y1_arrow, obj->y_min), FFMAX(obj->x_endp, obj->x_min), FFMAX(obj->y_endp, obj->y_min), frame->width, frame->height, frame->linesize[0], 100);
				draw_line(frame->data[0], FFMAX(obj->x2_arrow, obj->x_min), FFMAX(obj->y2_arrow, obj->y_min), FFMAX(obj->x_endp, obj->x_min), FFMAX(obj->y_endp, obj->y_min), frame->width, frame->height, frame->linesize[0], 100);
			} else {
				draw_line(frame->data[0], FFMIN(obj->x_endp, obj->x_max), FFMIN(obj->y_endp, obj->y_max), FFMAX(obj->center_x - obj->distance_from_center, obj->x_min), obj->center_y, frame->width, frame->height, frame->linesize[0], 100);
				draw_line(frame->data[0], FFMIN(obj->x1_arrow, obj->x_max), FFMIN(obj->y1_arrow, obj->y_max), FFMIN(obj->x_endp, obj->x_max), FFMIN(obj->y_endp, obj->y_max), frame->width, frame->height, frame->linesize[0], 100);
				draw_line(frame->data[0], FFMIN(obj->x2_arrow, obj->x_max), FFMIN(obj->y2_arrow, obj->y_max), FFMIN(obj->x_endp, obj->x_max), FFMIN(obj->y_endp, obj->y_max), frame->width, frame->height, frame->linesize[0], 100);
			}
			obj->y_endp--;
			obj->center_y--;
			obj->y1_arrow--;
			obj->y2_arrow--;
		}
	}

	//draw box
	if (s->object_marker_box) {
		if (obj->intersect) {
	        draw_box(frame, s, FFMAX(s->x, 0), FFMAX(s->y, 0), FFMIN(s->x + s->w, frame->width),
	                FFMIN(s->y + s->h, frame->height), pixel_belongs_to_box, s->intersect_yuv_color);
		} else {
			draw_box(frame, s, FFMAX(s->x, 0), FFMAX(s->y, 0), FFMIN(s->x + s->w, frame->width),
                    FFMIN(s->y + s->h, frame->height), pixel_belongs_to_box, s->box_yuv_color);
		}
	}
}

//replace a substring with another in a string

static void replace(char * o_string, char * s_string, char * r_string) {
      //a buffer variable to do all replace things
      char buffer[4096];
      //to store the pointer returned from strstr
      char *ch;

      if(!(ch = strstr(o_string, s_string)))
              return;

      //copy all the content to buffer before the first occurrence of the search string
      strncpy(buffer, o_string, ch - o_string);

      //prepare the buffer for appending by adding a null to the end of it
      buffer[ch - o_string] = 0;

      sprintf(buffer + (ch - o_string), "%s%s", r_string, ch + strlen(s_string));

      //empty o_string for copying
      o_string[0] = 0;

      strcpy(o_string, buffer);

      //pass recursively to replace other occurrences
      return replace(o_string, s_string, r_string);
 }

/**
 * Function to print the output in json format
 */
static void print_json(Object *obj, TDContext *s)
{
	int bool = 1;
	int watcher = 0;
	char *side = (obj->side == -1 ? (char *)"A" : obj->side == 1 ? (char *)"B" : (char *)"AB");

	char str[] = "{\n\t\"module\": \"tripwire_detector\",\n\t\"filter_id\": %d,\n\t\"intersect\": %d,\n\t\"frame\": %d,\n\t\"detected_objects\": %d,\n\t\"obj_id\": %d,\n\t\"obj_area\": %4.0f,\n\t\"obj_avg_angle\": %4.2f,\n"
				"\t\"obj_center_x\": %d,\n\t\"obj_center_y\": %d,\n\t\"mv_num\": %d,\n\t\"mv_avg_len\": %4.2f,\n\t\"obj_x1\": %d,\n\t\"obj_y1\": %d,\n\t\"obj_x2\": %d,\n\t\"obj_y2\": %d,\n"
				"\t\"obj_x3\": %d,\n\t\"obj_y3\": %d,\n\t\"obj_x4\": %d,\n\t\"obj_y4\": %d,\n\t\"crossed\": %d,\n\t\"cross-direction\": %s,\n\t\"side\": %s\n}\n";

	if (!s->line_break) {
		replace(str, (char *)"{\n\t", (char *)"{");
		replace(str, (char *)"\n\t", (char *)" ");
		replace(str, (char *)"\n}", (char *)"}");
	}

	if (s->print_only_intersect_trigger) {
		bool = 0;
		if (obj->intersect) {
			for (int i = 0; i < printed_counter; i++) {
				if (printed_ids[i] == obj->id) {
					bool = 0;
					watcher = 1;
					break;
				}
			}
			if (!watcher) {
				printed_ids[printed_counter] = obj->id;
				printed_counter++;
				bool = 1;
			}
		}
	}
	if (bool) {
		if (s->std_err_text_output_enable) {
			printf(str, s->filter_id, obj->intersect, video_frame_count, id_counter, obj->id, obj->area, obj->average_angle, obj->center_x, obj->center_y, obj->counter, obj->average_length,
					obj->x_min, obj->y_min, obj->x_max, obj->y_min, obj->x_max, obj->y_max, obj->x_min, obj->y_max, obj->crossed, (obj->crossed ? cross_direction : "-"), side);
		}
		if (s->url) {
			s->bytes += sprintf(s->buffer + s->bytes, (const char*)str, s->filter_id, obj->intersect, video_frame_count, id_counter, obj->id, obj->area, obj->average_angle,
					obj->center_x, obj->center_y, obj->counter, obj->average_length, obj->x_min, obj->y_min, obj->x_max, obj->y_min, obj->x_max, obj->y_max, obj->x_min, obj->y_max, obj->crossed,
					(obj->crossed ? cross_direction : "-"), side);
			s->buffer = (char*) realloc(s->buffer, s->bytes * sizeof(int));
		}
	}
}


static void store_object(Object *obj)
{
	objects_with_id[id_counter] = create_empty_object();
	objects_with_id[id_counter]->framenum = obj->framenum;
	objects_with_id[id_counter]->center_x = obj->center_x;
	objects_with_id[id_counter]->center_y = obj->center_y;
	objects_with_id[id_counter]->id = id_counter;
	objects_with_id[id_counter]->dir_counter = 0;
	for (int i = 0; i < 3; i++) {
		objects_with_id[id_counter]->directions[i] = -2;
	}
	id_counter++;
}

static void store_box_history(Object *obj)
{
	if (counter < SIZE) {
		every_object[counter] = create_empty_object();
		*every_object[counter] = *obj;
		counter++;
	}
}

static void object_id_check(Object *obj)
{
	int watcher;
	if (!id_counter) {
		obj->id = id_counter;
		ids[id_counter]++;
		store_object(obj);
	} else {

		int index;
		double _distance = 1500;
		for (int i = 0; i < id_counter; i++) {
			double distance_tmp = distance(objects_with_id[i]->center_x, objects_with_id[i]->center_y, obj->center_x, obj->center_y);
			if (objects_with_id[i]->framenum != obj->framenum && obj->framenum - objects_with_id[i]->framenum < 20 &&
				distance_tmp < _distance) {
				_distance = distance_tmp;
				index = i;
			}
		}
		if (_distance < 250) {
			obj->id = objects_with_id[index]->id;
			ids[objects_with_id[index]->id]++;
			objects_with_id[index]->framenum = obj->framenum;
			objects_with_id[index]->center_x = obj->center_x;
			objects_with_id[index]->center_y = obj->center_y;
		} else {
			obj->id = id_counter;
			ids[id_counter]++;
			store_object(obj);
		}
	}
	watcher = 0;
	for (int i = 0; i < 3; i++) {
		if (objects_with_id[obj->id]->directions[i] == obj->side) {
			watcher = 1;
		}
	}
	if (!watcher) {
		objects_with_id[obj->id]->directions[objects_with_id[obj->id]->dir_counter] = obj->side;
		objects_with_id[obj->id]->dir_counter++;
	}
}

static int find_max(int array[], int array_length, TDContext *s)
{
	int max;

	if (array_length < 1) {
        av_log(s, AV_LOG_ERROR, "Error, array length %d is not valid.\n", array_length);
		return -1;
	}

	max = array[0];

	for (int i = 1; i < array_length; i++) {
		if (array[i] > max) {
			max = array[i];
		}
	}
	return max;
}

static int find_min(int array[], int array_length, TDContext *s)
{
	int min;

	if (array_length < 1) {
        av_log(s, AV_LOG_ERROR, "Error, array length %d is not valid.\n", array_length);
		return -1;
	}

	min = array[0];

	for (int i = 1; i < array_length; i++) {
		if (array[i] < min) {
			min = array[i];
		}
	}
	return min;
}

static int check_cross(Object *obj)
{
	char second[5];
	if (objects_with_id[obj->id]->dir_counter == 3) {
		objects_with_id[obj->id]->crossed = 1;
		obj->crossed = 1;
		if (objects_with_id[obj->id]->directions[0] == -1) {
			strcpy(cross_direction, (char *)"A");
		} else {
			strcpy(cross_direction, (char *)"B");
		}
		if (objects_with_id[obj->id]->directions[2] == 1) {
			strcpy(second, (char *)"B");
		} else {
			strcpy(second, (char *)"A");
		}
		strcat(cross_direction, second);
		for (int i = 0; i < 3; i++) {
			objects_with_id[obj->id]->directions[i] = -2;
		}
		objects_with_id[obj->id]->dir_counter = 0;
		return 1;
	}
	return 0;
}

/**
 * Function to find the x and y min and max coordinates of the object, calculate the area, the center and inspect some conditions
 */
static void check_object(Object *obj, TDContext *s, AVFrame *frame)
{
	int obj_center_x, obj_center_y, mv_box_num, x1_arrow, y1_arrow, x2_arrow, y2_arrow, x_endp, y_endp, distance_from_center, x_min, y_min, x_max, y_max, left_upper, right_down;
	double min_angle, max_angle, area, angle, first_angle, second_angle, angle_in_degree;
	//find the min and max x and y
	obj->x_min = x_min = find_min(obj->src_x_s, obj->counter, s);
	obj->x_max = x_max = find_max(obj->src_x_s, obj->counter, s);
	obj->y_min = y_min = find_min(obj->src_y_s, obj->counter, s);
	obj->y_max = y_max = find_max(obj->src_y_s, obj->counter, s);

	obj->area = area = distance(x_min, y_min, x_max, y_min) * distance(x_max, y_min, x_max, y_max);
	obj_center_x = (x_min + x_max) / 2;
	obj_center_y = (y_min + y_max) / 2;

	obj->center_x = obj_center_x;
	obj->center_y = obj_center_y;
	min_angle = s->angle - s->angle_range / 2;
	max_angle = s->angle + s->angle_range / 2;

	obj->average_angle /= obj->counter;
	obj->average_length /= obj->counter;

	obj->mv_box_num = mv_box_num = (frame->width * frame->height) / 256;

	angle = obj->average_angle;
	angle_in_degree = angle;
	//converting degree to rad
	angle = angle * (PI / 180);
	if (angle_in_degree > 180 && angle_in_degree < 360) {
		obj->distance_from_center = distance_from_center = -50;
	} else {
		obj->distance_from_center = distance_from_center = 50;
	}
	obj->x_endp = x_endp = (obj->average_length + 100) * sin(angle) + (obj_center_x - distance_from_center);
	obj->y_endp = y_endp = (obj->average_length + 100) * cos(angle) + obj_center_y;

	//calculate the angles of the arrow legs
	first_angle = angle_in_degree - 135;
	second_angle = angle_in_degree - 225;

	if (first_angle < 0) {
		first_angle += 360;
	}
	if (second_angle < 0) {
		second_angle += 360;
	}

	obj->x1_arrow = x1_arrow = (obj->average_length + 20) * sin(first_angle * (PI / 180)) + FFMAX(x_endp, x_min - 2);
	obj->y1_arrow = y1_arrow = (obj->average_length + 20) * cos(first_angle * (PI / 180)) + FFMAX(y_endp, y_min - 2);
	obj->x2_arrow = x2_arrow = (obj->average_length + 20) * sin(second_angle * (PI / 180)) + FFMAX(x_endp, x_min - 2);
	obj->y2_arrow = y2_arrow = (obj->average_length + 20) * cos(second_angle * (PI / 180)) + FFMAX(y_endp, y_min - 2);

	if (s->angle) {
		if (obj->average_angle >= min_angle && obj->average_angle <= max_angle) {
			if (area >= s->min_obj_area) {
				for (int j = 0; j < obj->counter; j++) {
					if (get_line_intersection(s->start_x, s->start_y, s->end_x, s->end_y, obj->src_x_s[j], obj->src_y_s[j], obj->dst_x_s[j], obj->dst_y_s[j], s)) {
						obj->intersect = 1;
						break;
					}
				}
				left_upper = which_side(s->start_x, s->start_y, s->end_x, s->end_y, x_min, y_min);
				right_down = which_side(s->start_x, s->start_y, s->end_x, s->end_y, x_max, y_max);
				if (left_upper == right_down) {
					obj->side = left_upper;
				} else {
					obj->side = 0; // both sides of the tripwire
				}
				object_id_check(obj);
				check_cross(obj);
				if (!s->object_marker_box_history) {
					if (ids[obj->id] > s->detection_threshold) {
						draw_object(frame, obj, s);
						print_json(obj, s);
					}
				} else {
					store_box_history(obj);
				}
			}
		}
	} else {
		if (area >= s->min_obj_area) {
			for (int j = 0; j < obj->counter; j++) {
				if (get_line_intersection(s->start_x, s->start_y, s->end_x, s->end_y, obj->src_x_s[j], obj->src_y_s[j], obj->dst_x_s[j], obj->dst_y_s[j], s)){
					obj->intersect = 1;
					break;
				}
			}
			left_upper = which_side(s->start_x, s->start_y, s->end_x, s->end_y, x_min, y_min);
			right_down = which_side(s->start_x, s->start_y, s->end_x, s->end_y, x_max, y_max);
			if (left_upper == right_down) {
				obj->side = left_upper;
			} else {
				obj->side = 0; //both sides of the tripwire
			}
			object_id_check(obj);
			check_cross(obj);
			if (!s->object_marker_box_history) {
				if (ids[obj->id] > s->detection_threshold) {
					draw_object(frame, obj, s);
					print_json(obj, s);}
			} else {
				store_box_history(obj);
			}
		}
	}
}

/**
 * Getting the angle of a vector
 */
static double get_angle(int src_x, int src_y, int dst_x, int dst_y, int direction)
{
	double angle;
	int horizontal, vertical;

    if (direction) {
        FFSWAP(int, dst_x, src_x);
        FFSWAP(int, dst_y, src_y);
    }
	horizontal = dst_x - src_x;
	vertical = -(dst_y - src_y);

	angle = atan2(vertical, horizontal);
	angle = angle * (180 / PI);

	if (angle < 0) {
		angle += 360;
	} else if (angle == 0) {
		angle = 360;
	}

	//converting to bearings
	angle = 90 - angle;
	if (angle < 0) {
		angle += 360;
	} else if (angle > 360) {
		angle -= 360;
	}
	if (angle == 0) {
		angle = 360;
	}
	return angle;
}

/**
 * Checking if a vector already belongs to an object
 * @param objects struct containing every object of a frame
 */
/*static int check_vector(Object *objects[], int src_x, int src_y, int dst_x, int dst_y)
{
    for (int i = 0; i < obj_counter; i++) {
    	for (int j = 0; j < objects[i]->counter; j++) {
    		if (src_x == objects[i]->src_x_s[j] && src_y == objects[i]->src_y_s[j]) {
    			return 1;
    		}
    		if (dst_x == objects[i]->dst_x_s[j] && dst_y == objects[i]->dst_y_s[j]) {
    			return 1;
    		}
    	}
    }
    return 0;
}*/

/**
 * Adding a vector to an object
 */
static void add_to_object(Object* obj, int src_x, int src_y, int dst_x, int dst_y, double length, double angle)
{
		obj->src_x_s[0] = src_x;
		obj->src_y_s[0] = src_y;
		obj->dst_x_s[0] = dst_x;
		obj->dst_y_s[0] = dst_y;
		obj->framenum = video_frame_count;
		obj->average_angle += angle;
		obj->average_length += length;
		obj->counter++;
}

static int config_input(AVFilterLink *inlink)
{
    int ret;
	double res, other_angle;
    const char *expr;
    int mv_box_num = (inlink->w * inlink->h) / 256;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
	AVFilterContext *ctx = inlink->dst;
	TDContext *s = ctx->priv;
	printf("%s\n", av_get_media_type_string(ctx->filter->inputs[0].type));
	s->filter_id = ctx->name[strlen(ctx->name) - 1] - '0';
    ff_fill_rgba_map(s->rgba_map, inlink->format);
    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;

	s->bytes = 0;

	//if there is an url set, open connection
	if (s->url) {
		open_connection(s, s->url);
	}

    s->step_ = av_get_padded_bits_per_pixel(desc) >> 3;
    s->var_values[VAR_IN_H] = s->var_values[VAR_IH] = inlink->h;
    s->var_values[VAR_IN_W] = s->var_values[VAR_IW] = inlink->w;

    //evaluate and parse the expressions in the parameters, like (iw/2) * 4 + ih
    av_expr_free(s->area_pexpr);
    av_expr_free(s->start_x_pexpr);
    av_expr_free(s->start_y_pexpr);
    av_expr_free(s->end_x_pexpr);
    av_expr_free(s->end_y_pexpr);
    av_expr_free(s->distance_pexpr);
    av_expr_free(s->tripwire_center_x_pexpr);
    av_expr_free(s->tripwire_center_y_pexpr);

    s->area_pexpr = NULL;
    s->start_x_pexpr = NULL;
    s->start_y_pexpr = NULL;
    s->end_x_pexpr = NULL;
    s->end_y_pexpr = NULL;
    s->distance_pexpr = NULL;
    s->tripwire_center_x_pexpr = NULL;
    s->tripwire_center_y_pexpr = NULL;

    if (ret = av_expr_parse(&s->area_pexpr, expr = s->area_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->start_x_pexpr, expr = s->start_x_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->start_y_pexpr, expr = s->start_y_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->end_x_pexpr, expr = s->end_x_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->end_y_pexpr, expr = s->end_y_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->distance_pexpr, expr = s->distance_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->tripwire_center_x_pexpr, expr = s->tripwire_center_x_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

    if (ret = av_expr_parse(&s->tripwire_center_y_pexpr, expr = s->tripwire_center_y_expr, var_names, NULL, NULL, NULL, NULL, 0, ctx)) {
    	return AVERROR(EINVAL);
    }

	s->min_obj_area = res = av_expr_eval(s->area_pexpr, s->var_values, s);
	s->start_x = res = av_expr_eval(s->start_x_pexpr, s->var_values, s);
	s->start_y = res = av_expr_eval(s->start_y_pexpr, s->var_values, s);
	s->end_x = res = av_expr_eval(s->end_x_pexpr, s->var_values, s);
	s->end_y = res = av_expr_eval(s->end_y_pexpr, s->var_values, s);
	s->max_distance = res = av_expr_eval(s->distance_pexpr, s->var_values, s);
	s->tripwire_line_center_x = res = av_expr_eval(s->tripwire_center_x_pexpr, s->var_values, s);
	s->tripwire_line_center_y = res = av_expr_eval(s->tripwire_center_y_pexpr, s->var_values, s);

	if (s->tripwire_line_angle) {
		s->tripwire_line_angle = 360 - s->tripwire_line_angle;
		if (s->tripwire_line_angle < 0) {
				s->tripwire_line_angle += 360;
		}

		if (s->tripwire_line_angle == 0) {
			s->tripwire_line_angle = 360;
		}

		other_angle = s->tripwire_line_angle + 180;
		if (other_angle > 360) {
			other_angle -= 360;
		}

		s->tripwire_line_angle *= (PI / 180);
		other_angle *= (PI / 180);

		s->start_x = upper_x = (inlink->h) * sin(s->tripwire_line_angle) + s->tripwire_line_center_x;
		s->start_y = upper_y = (inlink->h) * cos(s->tripwire_line_angle) + s->tripwire_line_center_y;

		s->end_x = down_x = (inlink->h) * sin(other_angle) + s->tripwire_line_center_x;
		s->end_y = down_y = (inlink->h) * cos(other_angle) + s->tripwire_line_center_y;
	}

	if (s->parameters) {
		char parameters[] = "{\n\t\"module\": \"tripwire_detector\",\n\t\"version\": %s,\n\t\"release_date\": %s,\n\t\"min_mv_num_filter\": %d,\n\t\"max_mv_distance_filter\": %d,\n\t\"angle_filter\": %.2f, \n\t\"angle_filter_range\": %.2f, \n\t\"start_x\": %d,\n"
							"\t\"start_y: %d,\n\t\"end_x\": %d,\n\t\"end_y\": %d,\n\t\"tripwire_marker_line: %d,\n\t\"object_marker_box\": %d,\n\t\"object_marker_box_color\": %s,\n\t\"object_marker_box_intersect_color\": %s,\n"
							"\t\"object_marker_info\": %d,\n\t\"object_marker_box_history\": %d,\n\t\"min_obj_area_filter\": %d,\n\t\"object_marker_box_thickness\": %d,\n\t\"json_output_line_break\": %d,\n"
							"\t\"print_only_intersect_trigger\": %d,\n\t\"object_detection_threshold\": %d, \n\t\"mv_box_num\": %d\n}\n";

		if (!s->line_break) {
			replace(parameters, (char *)"{\n\t", (char *)"{");
			replace(parameters, (char *)"\n\t", (char *)" ");
			replace(parameters, (char *)"\n}", (char *)"}");
		}

		if (s->url) {
			//allocating memory for the first time
			s->buffer = (char*) malloc(512 * sizeof(int));

			//sending the data to the buffer
			s->bytes = sprintf(s->buffer, (const char*)parameters, version, release_date, s->min_mv, s->max_distance, s->angle, s->angle_range, s->start_x, s->start_y, s->end_x, s->end_y, s->tripwire_marker_line, s->object_marker_box, s->object_marker_box_color,
					s->object_marker_box_intersect_color, s->object_marker_info, s->object_marker_box_history, s->min_obj_area, s->thickness, s->line_break, s->print_only_intersect_trigger, s->detection_threshold,
					mv_box_num);

			//reallocating the amount of memory the buffer needs
			s->buffer = (char*) realloc(s->buffer, s->bytes * sizeof(int));
		}
		if (s->std_err_text_output_enable) {
			printf(parameters, version, release_date, s->min_mv, s->max_distance, s->angle, s->angle_range, s->start_x, s->start_y, s->end_x, s->end_y, s->tripwire_marker_line, s->object_marker_box, s->object_marker_box_color,
					s->object_marker_box_intersect_color, s->object_marker_info, s->object_marker_box_history, s->min_obj_area, s->thickness, s->line_break, s->print_only_intersect_trigger, s->detection_threshold, mv_box_num);
		}
	}

	return 0;
}


//calculating the fading color for the box history
static void fade(TDContext *s, AVFilterContext *ctx, char* color)
{
	char str[50];
	strcpy(str, s->object_marker_box_color);
	strcat(str, color);
	if (av_parse_color(history_rgba_color, str, -1, ctx) < 0) {
		return;
	}
	history_yuv_color[Y] = RGB_TO_Y_CCIR(history_rgba_color[0], history_rgba_color[1], history_rgba_color[2]);
	history_yuv_color[U] = RGB_TO_U_CCIR(history_rgba_color[0], history_rgba_color[1], history_rgba_color[2], 0);
	history_yuv_color[V] = RGB_TO_V_CCIR(history_rgba_color[0], history_rgba_color[1], history_rgba_color[2], 0);
	history_yuv_color[A] = history_rgba_color[3];
}

//deciding which alpha value should be used
static char* get_alpha(int number)
{
	char* alpha;

	if (counter - number < 6) {
		alpha = (char *)"@0.9";
	} else if (counter - number < 7) {
		alpha = (char *)"@0.8";
	} else if (counter - number < 10) {
		alpha = (char *)"@0.7";
	} else if (counter - number < 13) {
		alpha = (char *)"@0.6";
	} else if (counter - number < 16) {
		alpha = (char *)"@0.5";
	} else if (counter - number < 19) {
		alpha = (char *)"@0.4";
	} else if (counter - number < 22) {
		alpha = (char *)"@0.3";
	} else if (counter - number < 25) {
		alpha = (char *)"@0.2";
	} else if (counter - number < 28) {
		alpha = (char *)"@0.1";
	} else {
		alpha = NULL;
	}

	return (char *)alpha;
}

static void box_history(TDContext *s, AVFilterContext *ctx, AVFrame *frame)
{
	if (obj_counter < 5) {
		if (counter < SIZE) {
			every_object[counter] = create_empty_object();
			every_object[counter]->x_min = -1;
			every_object[counter]->x_max = -1;
			every_object[counter]->y_min = -1;
			every_object[counter]->y_max = -1;
			every_object[counter]->id = -1;
			counter++;
		}
	}
	//we go through all stored objects and check the parameters and then print and draw based on the conditions
	for (int i = 0; i < counter; i++) {
		if (every_object[i]->id != -1) {
			if (ids[every_object[i]->id] > s->detection_threshold) {
				if (every_object[i]->framenum == video_frame_count) {
					draw_object(frame, every_object[i], s);
					print_json(every_object[i], s);
				}
				if (every_object[i]->framenum < video_frame_count) {
					s->x = every_object[i]->x_min;
					s->y = every_object[i]->y_min;
					s->w = distance(every_object[i]->x_min, every_object[i]->y_min, every_object[i]->x_max, every_object[i]->y_min);
					s->h = distance(every_object[i]->x_min, every_object[i]->y_min, every_object[i]->x_min, every_object[i]->y_max);
					if (get_alpha(i)) {
						fade(s, ctx, get_alpha(i));
						draw_box(frame, s, FFMAX(s->x, 0), FFMAX(s->y, 0), FFMIN(s->x + s->w, frame->width), FFMIN(s->y + s->h, frame->height), pixel_belongs_to_box, history_yuv_color);
					}
				}
			}
		}
	}
}

static ResampledMV* create_resampled(double length, double angle, int src_x, int src_y,
									int dst_x, int dst_y, int direction, int flag,
									int zeros, int ones, int lower_x, int upper_x, int lower_y, int upper_y)
{
	ResampledMV* rmv = malloc (sizeof *rmv);
	rmv->length = length;
	rmv->angle = angle;
	rmv->src_x = src_x;
	rmv->src_y = src_y;
	rmv->dst_x = dst_x;
	rmv->dst_y = dst_y;
	rmv->direction = direction;
	rmv->flag = flag;
	rmv->zeros = zeros;
	rmv->ones = ones;
	rmv->lower_x = lower_x;
	rmv->upper_x = upper_x;
	rmv->lower_y = lower_y;
	rmv->upper_y = upper_y;
	rmv->counter = 0;
	rmv->center_x = (lower_x + upper_x) / 2;
	rmv->center_y = (lower_y + upper_y) / 2;
	return rmv;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
	AVFrameSideData *sd_;
	AVFilterContext *ctx = inlink->dst;
	TDContext *s = ctx->priv;
	AVFilterLink *outlink = ctx->outputs[0];
	double length, angle;
	int direction;
	int ret;
	int i = 0;
	Object *objects[500];
	video_frame_count++;
	sd_ = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
	if (sd_) {
		objects[obj_counter] = create_object();
		if (s->tripwire_marker_line && !s->tripwire_line_angle) {
			draw_line(frame->data[0], s->start_x, s->start_y, s->end_x, s->end_y,
		               frame->width, frame->height, frame->linesize[0],
					   100);
		}

		if (s->tripwire_line_angle) {
			draw_line(frame->data[0], down_x, down_y, upper_x, upper_y,
		               frame->width, frame->height, frame->linesize[0],
					   100);
		}

        // If parameters need mv resample function. Not resample a picture, only mv table
		AVMotionVector *mvs = (AVMotionVector *)sd_->data;
		if (s->mv_resample) {
	        ResampledMV *resampled[SIZE];
			int step = s->mv_resample;
	        int block_counter = 0;
	        int h_border = frame->height;
	        int w_border = frame->width;
	        int h_remain = frame->height % step;
	        int w_remain = frame->width % step;
	        h_border = (h_remain == 0 ? h_border : h_border + h_remain);
	        w_border = (w_remain == 0 ? w_border : w_border + w_remain);
	        //make blocks
	        for (i = 0; i < h_border; i += step) {
	        	for (int j = 0; j < w_border; j += step) {
	        		resampled[block_counter] = create_resampled(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, j, j + step, i, i + step);
	        		/*printf("%f %f %d %d %d %d %d %d %d %d %d %d %d %d\n",
	        				resampled[block_counter]->length, resampled[block_counter]->angle,resampled[block_counter]->src_x,resampled[block_counter]->src_y,
							resampled[block_counter]->dst_x,resampled[block_counter]->dst_y,resampled[block_counter]->direction,resampled[block_counter]->flag,
							resampled[block_counter]->zeros,resampled[block_counter]->ones,resampled[block_counter]->lower_x,resampled[block_counter]->upper_x,
							resampled[block_counter]->lower_y,resampled[block_counter]->upper_y);*/
	        		block_counter++;
	        	}
	        }
	        //selecting mv to blocks
	        for (i = 0; i < sd_->size / sizeof(*mvs); i++) {
	        	AVMotionVector *mv = &mvs[i];
	        	if (mv->dst_x - mv->src_x != 0 || mv->dst_y - mv->src_y != 0) {
	        		for (int j = 0; j < block_counter; j++) {
	                    if (mv->src_x >= resampled[j]->lower_x && mv->src_x <= resampled[j]->upper_x &&
	                    	mv->src_y >= resampled[j]->lower_y && mv->src_y <= resampled[j]->upper_y &&
	    					mv->dst_x >= resampled[j]->lower_x && mv->dst_x <= resampled[j]->upper_x &&
	    					mv->dst_y >= resampled[j]->lower_y && mv->dst_y <= resampled[j]->upper_y) {
	                    	length = _length(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
	                    	if (mv->source == -1) {
	                    		direction = 0;
	                    	} else {
	                    		direction = 1;
	                    	}
	                    	angle = get_angle(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, direction);
	                    	(direction == 0 ? resampled[j]->zeros++ : resampled[j]->ones++);
	                    	resampled[j]->src_x 	+= mv->src_x;
	                    	resampled[j]->src_y 	+= mv->src_y;
	                    	resampled[j]->dst_x 	+= mv->dst_x;
	                    	resampled[j]->dst_y 	+= mv->dst_y;
	                    	resampled[j]->length 	+= length;
	                    	resampled[j]->angle 	+= angle;
	                    	resampled[j]->counter++;
	                    	/*printf("%d %f %f %d %d %d %d %d %d %d %d %d %d %d %d szamlalo: %d\n", j,
	                    			resampled[j]->length, resampled[j]->angle,resampled[j]->src_x,resampled[j]->src_y,
									resampled[j]->dst_x,resampled[j]->dst_y,resampled[j]->direction,resampled[j]->flag,
									resampled[j]->zeros,resampled[j]->ones,resampled[j]->lower_x,resampled[j]->upper_x,
									resampled[j]->lower_y,resampled[j]->upper_y, resampled[j]->counter);*/
	                    }
	        		}
	        	}
	        }
	        //calculate average values of blocks
	        for (i = 0; i < block_counter; i++) {
	        	if (resampled[i]->counter) {
	            	resampled[i]->src_x 	/= resampled[i]->counter;
	            	resampled[i]->src_y 	/= resampled[i]->counter;
	            	resampled[i]->dst_x 	/= resampled[i]->counter;
	            	resampled[i]->dst_y 	/= resampled[i]->counter;
	            	resampled[i]->length 	/= resampled[i]->counter;
	            	resampled[i]->angle 	/= resampled[i]->counter;
	            	resampled[i]->direction = (resampled[i]->zeros >= resampled[i]->ones ? 0 : 1);
	            	/*printf("%d %f %f %d %d %d %d %d %d %d %d %d %d %d %d szamlalo: %d\n", i,
	            			resampled[i]->length, resampled[i]->angle,resampled[i]->src_x,resampled[i]->src_y,
							resampled[i]->dst_x,resampled[i]->dst_y,resampled[i]->direction,resampled[i]->flag,
							resampled[i]->zeros,resampled[i]->ones,resampled[i]->lower_x,resampled[i]->upper_x,
							resampled[i]->lower_y,resampled[i]->upper_y, resampled[i]->counter);*/
	        	}
	        }
	        //object detection for resampled mv
	        for (i = 0; i < block_counter; i++) {
	        	ResampledMV *mv = resampled[i];
	            if (mv->src_x - mv->dst_x != 0 || mv->src_y - mv->dst_y != 0) {
	            	if (!objects[obj_counter]->counter) {
	            		add_to_object(objects[obj_counter], mv->src_x,  mv->src_y, mv->dst_x, mv->dst_y, mv->length, mv->angle);
	            		//add_to_object(objects[obj_counter], resampled[i]->center_x, resampled[i]->center_y, mv->length, mv->angle);
	        			mv->flag = 1;
	            	} else if (objects[obj_counter]->counter > 0) {
	            		if (compare_vectors(objects[obj_counter], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, mv->length, mv->angle)) {
	            		//if (compare_vectors(objects[obj_counter], resampled[i]->center_x, resampled[i]->center_y, s, mv->length, mv->angle)) {
	            			mv->flag = 1;
	            		}
	            	}
	            		//compare_vectors(objects[obj_counter], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length);
	            }
	        }
	        obj_counter++;

	        while (signal > 0) {
	        	objects[obj_counter] = create_object();
	        	int tmp = obj_counter;
	        	signal = 0;
	            for (i = 0; i < block_counter; i++) {
	            	ResampledMV *mv = resampled[i];
	            	if (!mv->flag) {
	                	if (mv->dst_x - mv->src_x != 0 || mv->dst_y - mv->src_y != 0) {
	                        if (!objects[tmp]->counter) {
	                        	add_to_object(objects[tmp], mv->src_x,  mv->src_y, mv->dst_x, mv->dst_y, mv->length, mv->angle);
	                        	//add_to_object(objects[obj_counter], resampled[i]->center_x, resampled[i]->center_y, mv->length, mv->angle);
	                    		mv->flag = 1;
	                        } else {
	                        	if (compare_vectors(objects[tmp], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, mv->length, mv->angle)) {
	                        	//if (compare_vectors(objects[obj_counter], resampled[i]->center_x, resampled[i]->center_y, s, mv->length, mv->angle)) {
	                        		mv->flag = 1;
	                        	}
	                        	//compare_vectors(objects[tmp], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length);
	                        }
	                	}
	            	}
	            }
	        	obj_counter++;
	        }
	    }

		// object detection
        if (!s->mv_resample) {
	        //inspecting a frame for the first time
	        for (i = 0; i < sd_->size / sizeof(*mvs); i += s->step) {
	        	AVMotionVector *mv = &mvs[i];
	            // If mv length 0, then do nothing
                if (mv->dst_x - mv->src_x != 0 || mv->dst_y - mv->src_y != 0) {
	            	length = _length(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
	            	if (mv->source == -1) {
	            		direction = 0;
	            	} else {
	            		direction = 1;
	            	}
	                angle = get_angle(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, direction);
	            	// First mv in object (counter == 0)
                    if (objects[obj_counter]->counter == 0) {
	            		add_to_object(objects[obj_counter], mv->src_x,  mv->src_y, mv->dst_x, mv->dst_y, length, angle);
	        			mvs[i].source = 50;
	            	} else if (objects[obj_counter]->counter > 0) {
	            		if (compare_vectors(objects[obj_counter], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length, angle)) {
	            			mvs[i].source = 50;
	            		}
	            		//compare_vectors(objects[obj_counter], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length, angle);
	            	}
	            }
	        }
	        obj_counter++;
	        //we go through the frame as long as there are leftover motion vectors that do not belong to any object
            // signal helyett így kéne hívni: objektumba nem besorolt mv -k száma: mv_counter_without_object_link
	        while (signal > 0) {
	        	objects[obj_counter] = create_object();
	        	int tmp = obj_counter;
	        	signal = 0;
	            for (i = 0; i < sd_->size / sizeof(*mvs); i += s->step) {
	        		AVMotionVector *mv = &mvs[i];
	            	if (mv->source != 50) {
                        // If mv length 0, then do nothing
	                	if (mv->dst_x - mv->src_x != 0 || mv->dst_y - mv->src_y != 0) {
	                    	length = _length(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
	                    	if (mv->source == -1) {
	                    		direction = 0;
	                    	} else {
	                    		direction = 1;
	                    	}
	                		angle = get_angle(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, direction);
	                        if (!objects[tmp]->counter) {
	                        	add_to_object(objects[tmp], mv->src_x,  mv->src_y, mv->dst_x, mv->dst_y, length, angle);
	                    		mvs[i].source = 50;
	                        } else {
	                        	if (compare_vectors(objects[tmp], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length, angle)) {
	                        		mvs[i].source = 50;
	                        	}
	                        	//compare_vectors(objects[tmp], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y, s, length, angle);
	                        }
	                	}
	            	}
	            }
	        	obj_counter++;
	        }
		}
	}

/*-------------------------------------------------------------------------------------------------------------- */

	//we go through all of the detected objects in a frame and decide if it belongs to an already existing object and get some information about it
	for (int i = 0; i < obj_counter; i++) {
		if (objects[i]->counter > s->min_mv) {
			check_object(objects[i], s, frame);
		}
		//free(objects[i]);
	}

	if (s->object_marker_box_history) {
		box_history(s, ctx, frame);
	}

	obj_counter = 0;

	//if the url is set and there is data to send then we write the output to the url
	if (s->url && s->bytes) {
		//writing the data
	    if ((ret = http_write(s->uc, s->buffer, s->bytes) < 0)) {
	    	return AVERROR(EINVAL);
	    }
	    //freeing the buffer for the next frame
	    free(s->buffer);
	    s->bytes = 0;
	    s->buffer = (char*) malloc(512 * sizeof(int));
	}
	return ff_filter_frame(outlink, frame);
}

static av_cold int init(AVFilterContext *ctx)
{
    TDContext *s = ctx->priv;

    //parsing the color parameter
    if (!strcmp(s->object_marker_box_intersect_color, "invert")) {
        s->invert_color = 1;
    } else if (av_parse_color(s->intersect_rgba_color, s->object_marker_box_intersect_color, -1, ctx) < 0) {
        return AVERROR(EINVAL);
    }
    if (!strcmp(s->object_marker_box_color, "invert")) {
        s->invert_color = 1;
    } else if (av_parse_color(s->box_rgba_color, s->object_marker_box_color, -1, ctx) < 0) {
        return AVERROR(EINVAL);
    }

    if (!s->invert_color) {
        s->intersect_yuv_color[Y] = RGB_TO_Y_CCIR(s->intersect_rgba_color[0], s->intersect_rgba_color[1], s->intersect_rgba_color[2]);
        s->box_yuv_color[Y] = RGB_TO_Y_CCIR(s->box_rgba_color[0], s->box_rgba_color[1], s->box_rgba_color[2]);
        s->intersect_yuv_color[U] = RGB_TO_U_CCIR(s->intersect_rgba_color[0], s->intersect_rgba_color[1], s->intersect_rgba_color[2], 0);
        s->box_yuv_color[U] = RGB_TO_U_CCIR(s->box_rgba_color[0], s->box_rgba_color[1], s->box_rgba_color[2], 0);
        s->intersect_yuv_color[V] = RGB_TO_V_CCIR(s->intersect_rgba_color[0], s->intersect_rgba_color[1], s->intersect_rgba_color[2], 0);
        s->box_yuv_color[V] = RGB_TO_V_CCIR(s->box_rgba_color[0], s->box_rgba_color[1], s->box_rgba_color[2], 0);
        s->intersect_yuv_color[A] = s->intersect_rgba_color[3];
        s->box_yuv_color[A] = s->box_rgba_color[3];
    }

    return 0;
}

//close the url connection and free buffer
static void uninit(AVFilterContext *ctx)
{
    TDContext *s = ctx->priv;

    if (s->url) {
        ffurl_closep(&s->uc);
    	free(s->buffer);
    }

    for (int i = 0; i < counter; i++) {
    	free(every_object[i]);
    }
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    TDContext *s = ctx->priv;
    int ret;

    if (!strcmp(cmd, "min_obj_area") || !strcmp(cmd, "start_x") ||
    	!strcmp(cmd, "start_y") || !strcmp(cmd, "end_x") ||
		!strcmp(cmd, "end_y") || !strcmp(cmd, "max_distance")) {
        AVExpr *old_area = s->area_pexpr;
        AVExpr *old_start_x = s->start_x_pexpr;
        AVExpr *old_start_y = s->start_y_pexpr;
        AVExpr *old_end_x = s->end_x_pexpr;
        AVExpr *old_end_y = s->end_y_pexpr;
        AVExpr *old_distance = s->distance_pexpr;
        ret = av_expr_parse(&s->area_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->area_pexpr = old_area;
            return ret;
        }
        av_expr_free(old_area);

        ret = av_expr_parse(&s->start_x_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->start_x_pexpr = old_start_x;
            return ret;
        }
        av_expr_free(old_start_x);

        ret = av_expr_parse(&s->start_y_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->start_y_pexpr = old_start_y;
            return ret;
        }
        av_expr_free(old_start_y);

        ret = av_expr_parse(&s->end_x_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->end_x_pexpr = old_end_x;
            return ret;
        }
        av_expr_free(old_end_x);

        ret = av_expr_parse(&s->end_y_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->end_y_pexpr = old_end_y;
            return ret;
        }
        av_expr_free(old_end_y);

        ret = av_expr_parse(&s->distance_pexpr, args, var_names,
                            NULL, NULL, NULL, NULL, 0, ctx);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Error when parsing the expression '%s' for angle command\n", args);
            s->distance_pexpr = old_distance;
            return ret;
        }
        av_expr_free(old_distance);
    } else
        ret = AVERROR(ENOSYS);

    return ret;
}

static const AVFilterPad tripwire_detector_inputs[] = {
		{
				.name = "default",
				.type = AVMEDIA_TYPE_VIDEO,
				.filter_frame = filter_frame,
				.config_props = config_input,
				.flags = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
		},
};

static const AVFilterPad tripwire_detector_outputs[] = { { .name = "default", .type =
		AVMEDIA_TYPE_VIDEO, }, };

const AVFilter ff_vf_tripwire_detector = { .name = "tripwire_detector", .description =
		NULL_IF_CONFIG_SMALL("Send signal if an object crosses the tripwire."), .priv_size =
		sizeof(TDContext), .priv_class = &tripwire_detector_class,
		.flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
	    .init          = init,
	    .uninit        = uninit,
		.process_command = process_command,
		FILTER_INPUTS(tripwire_detector_inputs),
		FILTER_OUTPUTS(tripwire_detector_outputs),
		FILTER_PIXFMTS_ARRAY(pix_fmts), };
