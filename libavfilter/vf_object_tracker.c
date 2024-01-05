/**
 *
 *
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

/**
 * The filter made by Procontrol Elektronika kft.
 * The object_tracker filter is a motion tracker, with a built in tripwire detector.
 * 
 * This filter only works on videos which contains motion vectors
 *  (More about motion vectors: https://trac.ffmpeg.org/wiki/Debug/MacroblocksAndMotionVectors)
 * 
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "math.h"
#include "float.h"
#include <stdlib.h>
#include <time.h>
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
#define SIZE 10000

static const char* version = "2.06.10";
static const char* release_date = "2023.11.17";
static int video_frame_count = 0;
static int counter = 0;  // Used for history storing, to store, how many objects we have
static int id_counter = 0;
static int printed_counter = 0;
static int printed_ids[500];
static int ids[SIZE];
static int motion_image_size_x = 0;
static int motion_image_size_y = 0;
static int last_frame_object_counter = 0;
static int last_detected_objects_counter = 0;
static int last_mask_repeated_for = 0; 
static int last_frame_skipped = 0;
static int tripwire_event_detected_on_the_frame = 0;
static int first_frame_returned = 0;

struct TDContext;
typedef int (*PixelBelongsToRegion)(struct TDContext *s, int x, int y);

typedef struct TDContext {
    URLContext *uc;
    unsigned char* buffer;
    int bytes;
    int angle_enabled;
    double angle;
    double angle_range;
    double crop_x;
    double crop_y;
    double crop_width;
    double crop_height;
    int resize_to_crop;
    int black_filter;
    int mask_static_areas;
    int mask_i_frames;
    int draw_diagonal;
    int keep_mask_on_image;
    double resize_ratio_x;
    double resize_ratio_y;
    int max_distance;
    int min_mv;
    double start_x;  // its double because we set thiese values at the input, what coud be a float to use relative size
    double start_y;
    int end_x;
    int end_y;
    int tripwire;
    int tripwire_marker_line;
    int object_marker_box;
    int object_marker_box_history;
    int thickness;
    int min_mv_length;
    int scene_static_frames;
    int select_frames_where_tripwire;
    int grid_size;
    int obj_survival_time;
    int max_angle_diff;
    int max_obj_distance_history;
    int print_only_intersect_trigger;
    int print_lite_mode;
    int print_rectangles_position;
    int print_stderr;
    int detection_threshold;
    int line_break;
    int parameters;
    int filter_id; ///< unique id for the filter
    const char* url;
    double tripwire_line_angle;
    int tripwire_type;
    int std_err_text_output_enable;
} TDContext;

typedef struct Rectangle_center
{
    int x;
    int y;
} Rectangle_center;

/**
 * Struct to store a single object
 */
typedef struct Object {
    double average_angle;
    double average_length;
    double distance_x;  // distance for x-y. It is used for angle calculation.
    double distance_y;
    int frame_num;
    int counter;
    int id;
    int center_x;
    int center_y;
    int predicted_x;
    int predicted_y;
    int speed_x;
    int speed_y;
    int x_min;
    int y_min;
    int x_max;
    int y_max;
    Rectangle_center rectangles[SIZE];
    int rectangle_counter;
    int src_x_s[SIZE];
    int src_y_s[SIZE];
    int dst_x_s[SIZE];
    int dst_y_s[SIZE];
    int intersect;
    int crossed;
    int dir_counter;
    int side;    ///< 1 - from left or down to the tripwire, -1 - from right or above the tripwire
    int color[3];
    int exists_counter; // how much times the objects get detected
    int stayed_in_side; // how much times the object get detected in a particular state
    int reset_stayed_in_side;
} Object;

Object *last_frames_object[SIZE];
Object *last_detected_objects[SIZE];
Object *every_object[SIZE]; ///< storage for every object in the video
Object *objects_with_id[SIZE]; ///< storage for the IDs

#define OFFSET(x) offsetof(TDContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM|AVFILTER_FLAG_DYNAMIC_OUTPUTS

#define ASSIGN_THREE_CHANNELS                                        \
    row[0] = frame->data[0] +  y               * frame->linesize[0]; \
    row[1] = frame->data[1] + (y >> ctx->vsub) * frame->linesize[1]; \
    row[2] = frame->data[2] + (y >> ctx->vsub) * frame->linesize[2];

static const AVOption object_tracker_options[] = {
        // tripwire
        {"tripwire", "turn the tripwire on or off", OFFSET(tripwire), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
        {"tripwire_line_px", "a point's x coordinate what the tripwire will cross", OFFSET(start_x), AV_OPT_TYPE_DOUBLE, {.dbl = -1}, -1, 10000, FLAGS },
        {"tripwire_line_py", "a point's y coordinate what the tripwire will cross", OFFSET(start_y), AV_OPT_TYPE_DOUBLE, {.dbl = -1}, -1, 10000, FLAGS },
        {"tripwire_line_angle", "set the angle for the tripwire", OFFSET(tripwire_line_angle), AV_OPT_TYPE_DOUBLE, {.dbl = 90}, 0, 180, FLAGS },  
        {"tripwire_marker_line", "set the tripwire visibility", OFFSET(tripwire_marker_line), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
        {"tripwire_type", "0: touch, 1: the center goes throug", OFFSET(tripwire_type), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },

         //motion vector filter
        {"max_mv_distance", "max mv & object distance, to put the mv into the object", OFFSET(max_distance), AV_OPT_TYPE_INT, {.i64 = 50}, 0, 10000, FLAGS },
        {"min_mv_length", "minimum length of a motion vector", OFFSET(min_mv_length), AV_OPT_TYPE_INT, {.i64 = 15}, 1, 1000, FLAGS },
        {"max_angle_diff", "maximum angle difference where motion_vector is filtered", OFFSET(max_angle_diff), AV_OPT_TYPE_INT, {.i64 = 45}, 0, 359, FLAGS }, // 0 means any angle
        {"crop_x", "filter motion vectors out of the image, as the crop filter does", OFFSET(crop_x), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 1, FLAGS},
        {"crop_y", "filter motion vectors out of the image, as the crop filter does", OFFSET(crop_y), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 1, FLAGS},
        {"crop_width", "filter motion vectors out of the image, as the crop filter does", OFFSET(crop_width), AV_OPT_TYPE_DOUBLE, {.dbl = 1}, 0, 1.0001, FLAGS},
        {"crop_height", "filter motion vectors out of the image, as the crop filter does", OFFSET(crop_height), AV_OPT_TYPE_DOUBLE, {.dbl = 1}, 0, 1.0001, FLAGS},
        {"resize_to_crop", "resize motion vectors to fit into cropped screen", OFFSET(resize_to_crop), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"black_filter", "ignore motion vectors above black pixels", OFFSET(black_filter), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
        
        // Object filter
        {"min_mv_num", "object filter, based on motion vector number", OFFSET(min_mv), AV_OPT_TYPE_INT, {.i64 = 5}, 1, INT_MAX, FLAGS },
        {"angle_filter", "turn on or of angle filter", OFFSET(angle_enabled), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"angle_filter_angle", "object filter based on the angle", OFFSET(angle), AV_OPT_TYPE_DOUBLE, {.dbl = 0}, 0, 360, FLAGS },
        {"angle_filter_range", "set the allowed range of the angle", OFFSET(angle_range), AV_OPT_TYPE_DOUBLE, {.dbl = 45}, 0, 360, FLAGS },

        // visuals
        {"object_marker_box", "set the object marker box visibility", OFFSET(object_marker_box), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
        {"object_rectangle_thickness", "set the rectangles thickness", OFFSET(thickness), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 200, FLAGS },
        {"object_marker_box_history", "set the object marker box history visibility", OFFSET(object_marker_box_history), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"object_history_draw_length", "set how many object are visible in the past", OFFSET(detection_threshold), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 50, FLAGS },
        {"mask_static_image_parts", "masking to black color all non object image part", OFFSET(mask_static_areas), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"mask_i_frames", "at the I frames, the previous frame will be showed", OFFSET(mask_i_frames), AV_OPT_TYPE_INT, { .i64 = 2 }, 0, 2, FLAGS },  // 0 Dont mask at all, 1 full black frame, 2: mask as the previous frame
        {"keep_mask_on_static_image", "how many static frame will get the last moved mask.", OFFSET(keep_mask_on_image), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 1000, FLAGS },
        {"draw_object_diagonal", "draw the two diagonal for the detected object", OFFSET(draw_diagonal), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },

        // Logging
        {"json_output_line_break", "set the output line breaks", OFFSET(line_break), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"print_only_intersect_trigger", "print only on intersect and once per object", OFFSET(print_only_intersect_trigger), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"print_lite_mode", "print only important information", OFFSET(print_lite_mode), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"print_rectangle_positions", "print the rectangle centers in the log", OFFSET(print_rectangles_position), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        {"url", "url to send data", OFFSET(url), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, FLAGS },
        {"std_err_text_output_enable", "enable text output on std err", OFFSET(std_err_text_output_enable), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 2, FLAGS },
        {"parameter_summary_row", "print a highlight about the set parameters", OFFSET(parameters), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
        {"grid_size", "rectangle side length", OFFSET(grid_size), AV_OPT_TYPE_INT, {.i64 = 32}, 1, 1000, FLAGS },

        // History detection
        {"object_survival_time", "after that many frame, we delete old objects", OFFSET(obj_survival_time), AV_OPT_TYPE_INT, {.i64 = 25}, 1, 1000, FLAGS },
        {"max_obj_distance_history", "max distance between two object on two frame get the same id", OFFSET(max_obj_distance_history), AV_OPT_TYPE_INT, {.i64 = 100}, 0, 10000, FLAGS },

        // frame filter
        {"select_frames_where_object_detected", "skip frames if not a single object get detected", OFFSET(scene_static_frames), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 2, FLAGS },  // 0 no skip // 1 skipp all no motion, i frames as the previous  // 2 skip frames, but not i frames
        {"select_frames_where_tripwire_detected", "only returns frames where tripwire event was detected", OFFSET(select_frames_where_tripwire), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
        { NULL } };

AVFILTER_DEFINE_CLASS(object_tracker);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,     AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV411P,     AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ444P,    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV440P,     AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA420P,    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA444P,
    AV_PIX_FMT_RGB24,       AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,        AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ARGB,        AV_PIX_FMT_ABGR,
    AV_PIX_FMT_0RGB,        AV_PIX_FMT_0BGR,
    AV_PIX_FMT_RGB0,        AV_PIX_FMT_BGR0,
    AV_PIX_FMT_NONE
};

/**
 * Deciding which side a point to a line lies
 */
static int which_side(int line_x1, int line_y1, int line_x2, int line_y2, int point_x1, int point_y1)
{
    int position_value;
    if (line_x1 > line_x2){
        FFSWAP(int, line_x1, line_x2);
        FFSWAP(int, line_y1, line_y2);
    }
    position_value = (line_x2 - line_x1) * (point_y1 - line_y1) - (line_y2 - line_y1) * (point_x1 - line_x1);
    if (position_value > 0)
        return -1; // to left or under
    else if (position_value < 0)
        return 1; // to right or above
    return 1;  // return 0;
}

/**
 * Draw a box from the point (left, top).
 * @param x1 upper line height of the rectangle
 * @param y1 left side of the rectangle
 * @param x2 button line height of the rectangle
 * @param y2 right side of the rectangle
 * @param color color of the rectangle
 * @param thickness thickness of the rectangle side's
 */
static void draw_rectangle_on_frame(AVFrame *frame, int x1, int y1, int x2, int y2, int color[3], int thickness) {
    int i, j;
    uint8_t *ptr;
    int width = frame->width;
    int height = frame->height;
    x1 = av_clip(x1, 0, width - 1);
    y1 = av_clip(y1, 0, height - 1);
    x2 = av_clip(x2, 0, width - 1);
    y2 = av_clip(y2, 0, height - 1);
    
    // Draw the rectangle border on the frame
    for (i = y1; i <= y2; i++) {        // y color
        for (j = x1; j <= x2; j++) {
            if (i >= y1 && i < y1 + thickness || i <= y2 && i > y2 - thickness || // Horizontal lines
                j >= x1 && j < x1 + thickness || j <= x2 && j > x2 - thickness) { // Vertical lines
                ptr = &frame->data[0][(i * frame->linesize[0]) + j];
                ptr[0] = color[0];
            }
        }
    }

    for (i = y1 / 2; i <= y2 / 2; i++) {    // u color
        for (j = x1 / 2; j <= x2 / 2; j++) {
            if (i >= y1 / 2 && i < y1 / 2 + thickness / 2 || i <= y2 / 2 && i > y2 / 2 - thickness / 2 || // Horizontal lines
                j >= x1 / 2 && j < x1 / 2 + thickness / 2 || j <= x2 / 2 && j > x2 / 2 - thickness / 2) { // Vertical lines
                ptr = &frame->data[1][(i * frame->linesize[1]) + j];
                ptr[0] = color[1];
            }
        }
    }

    for (i = y1 / 2; i <= y2 / 2; i++) {    // v color
        for (j = x1 / 2; j <= x2 / 2; j++) {
            if (i >= y1 / 2 && i < y1 / 2 + thickness / 2 || i <= y2 / 2 && i > y2 / 2 - thickness / 2 || // Horizontal lines
                j >= x1 / 2 && j < x1 / 2 + thickness / 2 || j <= x2 / 2 && j > x2 / 2 - thickness / 2) { // Vertical lines
                ptr = &frame->data[2][(i * frame->linesize[2]) + j];
                ptr[0] = color[2];
            }
        }
    }
}


/**
 * Returns the value of the difference between 2 angle.
 */
static double smallest_angle(double alpha, double beta) {
    double diff = fabs(alpha - beta);
    return FFMIN(diff, 360.0 - diff);
}

/**
 * Returns if an object complyte the filters.
 * Returns 0 if an object shoud be filterred out.
*/
static int is_object_not_filtered(Object *obj, TDContext *s){
    if ((obj->counter > s->min_mv) && ((smallest_angle(s->angle, obj->average_angle) < s->angle_range) || !s->angle))
        return 1;
    return 0;
}

/**
 * Generate random color for id.
 * Its just a visual, for making the debug session easier.
 */
static void generate_random_rgb_to_obj(Object *obj){
    double r, g, b;
    int color_y, color_u, color_v;
    srand(obj->id*341); // seed the random number generator with a random prime
    r = rand() % 255;
    srand(obj->id*113);
    g = rand() % 255;
    srand(obj->id*199);
    b = rand() % 255;
    color_y  = (0.257 * r) + (0.504 * g) + (0.098 * b) + 16;
    color_v =  (0.439 * r) - (0.368 * g) - (0.071 * b) + 128;
    color_u = -(0.148 * r) - (0.291 * g) + (0.439 * b) + 128;
    if (color_y < 0)
        color_y += 255;
    if (color_u < 0)
        color_u += 255;
    if (color_v < 0)
        color_v += 255;
    obj->color[0] = color_y;
    obj->color[1] = color_u;
    obj->color[2] = color_v;
}

/**
 * Return true, if the point is inside of the rectangle
 * @param point_x x coordinate of point
 * @param point_y y coordinate of point
 * @param x_max upper line height of the rectangle
 * @param x_min under line height of the rectangle
 * @param y_max right side of the rectangle
 * @param y_min left side of the rectangle
 */
static int point_in_rectangle(int point_x, int point_y, int x_max, int x_min, int y_max, int y_min) {
    if (point_x >= x_min && point_x <= x_max &&
        point_y >= y_min && point_y <= y_max) {
        return 1; // point is inside rectangle
    } else
        return 0; // point is outside rectangle
}

/**
 * Draw a line from (sx, sy) -> (ex, ey).
 * @param x1 x coordinate of start point
 * @param y1 y coordinate of start point
 * @param x2 x coordinate of end point
 * @param y2 y coordinate of end point
 * @param color color of the arrow
 */
static void draw_line(AVFrame *frame, int x1, int y1, int x2, int y2, int color[3])
{
    uint8_t *ptr;
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    int e2 = 2 * err;

    while (1) {
        if (x1 >= 0 && x1 < frame->width && y1 >= 0 && y1 < frame->height) {
            ptr = &frame->data[0][y1 * frame->linesize[0] + x1];
            *ptr = color[0];
            ptr = &frame->data[1][(y1 / 2) * frame->linesize[1] + x1 / 2];
            *ptr = color[1];
            ptr = &frame->data[2][(y1 / 2) * frame->linesize[2] + x1 / 2];
            *ptr = color[2];
        }
        if (x1 == x2 && y1 == y2)
            break;
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (x1 == x2 && y1 == y2) {
            if (x1 >= 0 && x1 < frame->width && y1 >= 0 && y1 < frame->height) {
                ptr = &frame->data[0][y1 * frame->linesize[0] + x1];
                *ptr = color[0];
                ptr = &frame->data[1][(y1 / 2) * frame->linesize[1] + x1 / 2];
                *ptr = color[1];
                ptr = &frame->data[2][(y1 / 2) * frame->linesize[2] + x1 / 2];
                *ptr = color[2];
            }
            break;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/**
 * Draw arrow, based on the center point of the object and the average motion vector length. 
 */
static void draw_object_arrow(Object *obj, AVFrame *frame){
    int point_x, point_y, x3, y3, x4, y4;
    float math_angle = obj->average_angle;
    double angle_rad;
    math_angle -= 360;  // mirror to the x axle for the drawing method
    math_angle *= -1;
    if (math_angle < 0)
        math_angle += 360;
    angle_rad = math_angle * (PI / 180);
    for(int i = 0; i < 5; i++){
        point_x = obj->center_x + ((obj->average_length * 2) * cos(angle_rad));
        point_y = obj->center_y + ((obj->average_length * 2) * sin(angle_rad));
        x3 = point_x - obj->average_length*cos(angle_rad + M_PI/6);
        y3 = point_y - obj->average_length*sin(angle_rad + M_PI/6);
        x4 = point_x - obj->average_length*cos(angle_rad - M_PI/6);
        y4 = point_y - obj->average_length*sin(angle_rad - M_PI/6);
        draw_line(frame, point_x, point_y, obj->center_x - (obj->average_length * 2 * cos(angle_rad)),
                  obj->center_y - (obj->average_length * 2 * sin(angle_rad)), obj->color);
        draw_line(frame, point_x, point_y, x3, y3, obj->color);
        draw_line(frame, point_x, point_y, x4, y4, obj->color);
        obj->center_x -= 1;
        obj->center_y -= 1;
    }
    obj->center_x += 5; // return to original point
    obj->center_y += 5; 
}

/**
 * Find and draw the rectangles for the object.
 * Two object rectangles' lines, could be shifted apart from each other.
 */
static void draw_polygon(Object *obj, AVFrame *frame, TDContext *s){
    int half_grid = s->grid_size/2;
    generate_random_rgb_to_obj(obj);
    draw_object_arrow(obj, frame);
    for (int i = 0; obj->rectangle_counter > i; i++){
        int x1 = obj->rectangles[i].x + half_grid;
        int x2 = obj->rectangles[i].x - half_grid;
        int y1 = obj->rectangles[i].y + half_grid;
        int y2 = obj->rectangles[i].y - half_grid;
        draw_rectangle_on_frame(frame, x2, y2, x1, y1, obj->color, s->thickness); // Draw out the rectangles.
    }
    return;
}

/**
 * Find the rectangles, what define the object position
*/
static void get_object_rectangles(Object *obj, AVFrame *frame, TDContext *s)
{
    int finder_counter = 0;
    int grid_size = s->grid_size;
    int half_grid;
    if (grid_size < 16)  // The grid size is too low. Set Higher.
        grid_size = 32;
    half_grid = grid_size/2;
    for (int i = obj->y_min; i < obj->y_max; i += grid_size){       // Loop trough y direction.
        for (int k = obj->x_min; k < obj->x_max; k += grid_size){   // Loop trough x direction.
            for (int z = 0; z < obj->counter;  z++){ // Loop trough on motion vectors in the object.
                // destination points are more accurate to draw the object.
                if (point_in_rectangle(obj->dst_x_s[z], obj->dst_y_s[z], k + grid_size, k, i + grid_size, i)){
                    obj->rectangles[finder_counter].x = k + half_grid;
                    obj->rectangles[finder_counter].y = i + half_grid;
                    finder_counter++;
                    break;
                }
            }
        }
    }
    obj->rectangle_counter = finder_counter;
}

/**
 * Write http message
*/
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

/**
 * opening the connection to the given url and allocating the URLContext
 */
static int open_connection(TDContext *s, const char* filename)
{
    int ret;

    if ((ret = ffurl_alloc(&s->uc, filename, AVIO_FLAG_WRITE, NULL)) < 0)
        return AVERROR(EINVAL);

    //setting up POST request
    if ((ret = ffurl_connect(s->uc, NULL)) < 0)
        return AVERROR(EINVAL);

    return ret;
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
    o->center_x = 0;
    o->center_y = 0;
    o->dir_counter = 0;
    o->x_max = -1;  // If the x_max -1 the object has lass mv that needed...
    o->side = 0;
    o->distance_x = 0;
    o->distance_y = 0;
    o->speed_x = 0;
    o->speed_y = 0;
    o->predicted_x = 0;
    o->predicted_y = 0;
    o->exists_counter = 0;
    o->stayed_in_side = 0;
    o->reset_stayed_in_side = 0;    
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
 * Returns true if the motion vector part of the object.
 * @param obj Object
 * @param src_x x coordinate of motion vector
 * @param src_y y coordinate of motion vector
 * @param angle angle of the motion vector
 * @param s TdContext
 * @return 1 if the motion vector part of the object, otherwise 0
 */
static int part_of_object(Object *o, int src_x, int src_y, double angle, TDContext *s){
    int tmp = o->counter;    
    for(int j = 0; j < tmp; j += 1) { // Loop trough all motion vector in the object.
        if (distance(o->src_x_s[j], o->src_y_s[j], src_x, src_y) <= s->max_distance){ // If mv in object close to mv input is closer than the max distance.
            if (smallest_angle(o->average_angle, angle) <= s->max_angle_diff || s->max_angle_diff == 0)// looking for angle difference
                return 1;  // Then the mv as input, part of the object.
        }
    }
    return 0; // Cant be part of the object
}

/**
 * Replace string with other strings
 * 
 * @param o_string The base string
 * @param s_string start string, change this
 * @param r_string result_string, change to this
 */
static void replace(char * o_string, char * s_string, char * r_string)
{
      // a buffer variable to do all replace things
      char buffer[4096];
      // to store the pointer returned from strstr
      char *ch;

      if(!(ch = strstr(o_string, s_string)))
              return;

      // copy all the content to buffer before the first occurrence of the search string
      strncpy(buffer, o_string, ch - o_string);

      // prepare the buffer for appending by adding a null to the end of it
      buffer[ch - o_string] = 0;

      sprintf(buffer + (ch - o_string), "%s%s", r_string, ch + strlen(s_string));

      o_string[0] = 0;
      strcpy(o_string, buffer);
      return replace(o_string, s_string, r_string);
 }

/**
 * Function to print the output in json format
 */
static void print_json(Object *obj, TDContext *s)
{ 
    int crossed_event;
    int watcher = 0;
    // char *side = (obj->side == -1 ? (char *)"A" : obj->side == 1 ? (char *)"B" : (char *)"AB");
    char lite_str[] ="{\n\t\"module\": \"object_tracker\",\n\t\"frame\": %d,\n\t\"obj_id\": %d,\n\t\"obj_center_x\": %d,\n\t\"obj_center_y\": %d\n}\n";
    char str[] = "{\n\t\"module\": \"object_tracker\",\n\t\"filter_id\": %d,\n\t\"frame\": %d,\n\t\"detected_objects\": %d,\n\t\"obj_id\": %d,\n\t\"obj_avg_angle\": %4.2f,\n"
                "\t\"obj_center_x\": %d,\n\t\"obj_center_y\": %d,\n\t\"mv_num\": %d,\n\t\"mv_avg_len\": %4.2f,\n\t\"obj_x1\": %d,\n\t\"obj_y1\": %d,\n\t\"obj_x2\": %d,\n\t\"obj_y2\": %d,\n"
                "\t\"obj_x3\": %d,\n\t\"obj_y3\": %d,\n\t\"obj_x4\": %d,\n\t\"obj_y4\": %d,\n\t\"crossed\": %d,\n\t\"crossed_direction\": %d,\n\t\"stayed_in_side\": %d,\n\t\"side\": %d%s}\n";
    char *output_str;
    char rect_position[5*SIZE] = {""};
    char rect[SIZE] = ""; // Initialize to empty string
    char temp[SIZE]; // Temporary buffer
    int size;
    if(s->print_lite_mode)
        output_str = lite_str;
    else{
        output_str = str;  
        if(s->print_rectangles_position){
            for(int i=0; i < obj->rectangle_counter ; i++){
                sprintf(temp, "{\"x\": %d, \"y\": %d}, ", obj->rectangles[i].x, obj->rectangles[i].y);
                sprintf(rect, "%s%s", rect, temp);
            }
            size = strlen(rect);
            if (size >= 2) {
                rect[size-1] = '\0';    // remove last space
                rect[size-2] = '\0';    // remove last comma
            }
            sprintf(rect_position, ", \"rectangle_pos\":[%s]", rect);
        }
    }
    if (!s->line_break) {
        replace(output_str, (char *)"{\n\t", (char *)"{");
        replace(output_str, (char *)"\n}"  , (char *)"}");
        replace(output_str, (char *)"[\n\t", (char *)"[");
        replace(output_str, (char *)"\n]"  , (char *)"]");
        replace(output_str, (char *)"\n\t" , (char *)" ");
    }

    crossed_event = 0;
    if (obj->intersect) {
        for (int i = 0; i < printed_counter; i++) {
            if (printed_ids[i] == obj->id) {
                crossed_event = 0;
                watcher = 1;
                break;
            }
        }
        if (!watcher) {
            printed_ids[printed_counter] = obj->id;
            printed_counter++;
            crossed_event = 1;
            tripwire_event_detected_on_the_frame = 1;
        }
    }
    if (crossed_event || !s->print_only_intersect_trigger) {  // its a crossed event, or the not crossed events arent filtered
        if(s->print_lite_mode){
            if (s->std_err_text_output_enable == 0) //stdout
                printf(output_str, video_frame_count, obj->id, obj->center_x, obj->center_y); 
            if (s->std_err_text_output_enable == 1) // stderr
                fprintf(stderr, output_str, video_frame_count, obj->id, obj->center_x, obj->center_y);
            if (s->url) {
                s->bytes += sprintf(s->buffer + s->bytes, (const char*)output_str, video_frame_count, obj->id, obj->center_x, obj->center_y);
                s->buffer = (char*) realloc(s->buffer, s->bytes * sizeof(int));
            }
        }else{
            if (s->std_err_text_output_enable == 0) {
                printf(output_str,          s->filter_id, video_frame_count, id_counter, obj->id, obj->average_angle, obj->center_x, obj->center_y, obj->counter, obj->average_length,
                                            obj->x_min, obj->y_min, obj->x_max, obj->y_min, obj->x_max, obj->y_max, obj->x_min, obj->y_max, crossed_event, obj->crossed, obj->stayed_in_side, obj->side, rect_position); 
            }
            if (s->std_err_text_output_enable == 1) {
                fprintf(stderr, output_str, s->filter_id, video_frame_count, id_counter, obj->id, obj->average_angle, obj->center_x, obj->center_y, obj->counter, obj->average_length,
                                            obj->x_min, obj->y_min, obj->x_max, obj->y_min, obj->x_max, obj->y_max, obj->x_min, obj->y_max, crossed_event, obj->crossed, obj->stayed_in_side, obj->side, rect_position);
            }
            if (s->url) {
                s->bytes += sprintf(s->buffer + s->bytes, (const char*)str, 
                                            s->filter_id, video_frame_count, id_counter, obj->id, obj->average_angle, obj->center_x, obj->center_y, obj->counter, obj->average_length, 
                                            obj->x_min, obj->y_min, obj->x_max, obj->y_min, obj->x_max, obj->y_max, obj->x_min, obj->y_max, crossed_event, obj->crossed, obj->stayed_in_side, obj->side, rect_position);
                s->buffer = (char*) realloc(s->buffer, s->bytes * sizeof(int));
            }  
        }
    }
    fflush(stdout);
    fflush(stderr);
}

/**
 * Store objects for future identification.
 */
static void store_object(Object *obj, TDContext *s)
{   
    int i;
    for (i = 0; i < id_counter; i++){
        if (objects_with_id[i] == NULL)
            continue;
        if (video_frame_count - objects_with_id[i]->frame_num > s->obj_survival_time){
            // to save memory, replace the old object with new
            free(objects_with_id[i]);
            objects_with_id[i] = NULL;
        }
    }
    objects_with_id[id_counter] = create_empty_object();
    objects_with_id[id_counter]->frame_num = obj->frame_num;
    objects_with_id[id_counter]->center_x = obj->center_x;
    objects_with_id[id_counter]->center_y = obj->center_y;
    objects_with_id[id_counter]->id = id_counter;
    objects_with_id[id_counter]->dir_counter = 0;
    objects_with_id[id_counter]->average_angle = obj->average_angle;
    objects_with_id[id_counter]->side = obj->side;
    id_counter++;
}

/**
 * Save the stored objects.
 */
static void store_box_history(Object *obj, TDContext *s)
{
    for (int i = 0; i < counter; i++){
        if (video_frame_count - every_object[i]->frame_num > s->obj_survival_time){ // The object on id i, is to old
            *every_object[i] = *obj;
            return;
        }
    }
    every_object[counter] = create_empty_object();
    *every_object[counter] = *obj;
    counter++;
}

/**
 * Draw the history back for objects
 * detection_threshold: that many history object we should draw
 */
static void draw_box_history(TDContext *s, AVFrame *frame)
{
    for (int i = 0; i < counter; i++) {
		if (every_object[i]->id != -1) {  // Means empty object
            if (video_frame_count - every_object[i]->frame_num < s->obj_survival_time)
                draw_polygon(every_object[i], frame, s);
		}
	}
}

/**
 * Compare objects with saved objects.
 * If the object in the frame has common properties with previously saved object,
 * it is assumed to be the same object.
 */
static void object_id_check(Object *obj, TDContext *s)
{   
    int index;
    double _distance = 50000; // random huge numbers, so any result is going to be smallest.
    double best_score = 10000000;
    double distance_tmp, angle_diff, predicted_distance;
    int punish_point = 0;
    if (!id_counter) {  // First detected object
        obj->id = id_counter;
        ids[id_counter]++;
        store_object(obj, s);
    } else {
        for (int i = 0; i < id_counter; i++) {  // loop through every object
            if (objects_with_id[i] == NULL)     // Object deleted
                continue;
            punish_point = 0;
            predicted_distance = distance(objects_with_id[i]->predicted_x, objects_with_id[i]->predicted_y, obj->center_x, obj->center_y);
            distance_tmp = distance(objects_with_id[i]->center_x, objects_with_id[i]->center_y, obj->center_x, obj->center_y);
            if (predicted_distance < distance_tmp)
                distance_tmp = predicted_distance;
            angle_diff = smallest_angle(objects_with_id[i]->average_angle, obj->average_angle);

            if (obj->frame_num - objects_with_id[i]->frame_num > s->obj_survival_time)  // the object is too old
                continue;
            if (angle_diff > s->max_angle_diff){
                if (s->max_angle_diff > 0){
                    if (angle_diff > s->max_angle_diff*1.5) // allow a bit higher angle difference, but use punish points...
                        continue;
                    punish_point = s->max_distance/2;
                }
             }
            if (objects_with_id[i]->frame_num != obj->frame_num) { // The object is from the past
                // Weighted score calculation
                double score = ((distance_tmp / 10) + 2 * (angle_diff / 360)) + punish_point; // weighted score create
                if (score < best_score) {  // smaller score are better
                    best_score = score;
                    _distance = distance_tmp;
                    index = i;
                }
            }
        }
    }
    if (_distance < s->max_obj_distance_history) { // If in this distance the two object is small enough the two object get the same id & refresh data
        obj->id = objects_with_id[index]->id;
        if (s->tripwire_type){  // center goes throug
            if ((objects_with_id[index]->side == 1 && obj->side == -1) || (objects_with_id[index]->side == -1 && obj->side == 1))
                obj->intersect = 1;
        }else{  // touch
            if ((objects_with_id[index]->side == 2 && obj->side != 2) || (objects_with_id[index]->side == -2 && obj->side != -2) || (objects_with_id[index]->exists_counter == 0 && (obj->side == 1 || obj->side == -1)))  // if someone went from +-2 to +-1 or first frame when the object is detected and the state is 1
                obj->intersect = 1;
        }
        if (objects_with_id[index]->reset_stayed_in_side){
            objects_with_id[index]->stayed_in_side = 0;
            objects_with_id[index]->reset_stayed_in_side = 0;
        }

        if (objects_with_id[index]->side == obj->side)
            objects_with_id[index]->stayed_in_side++;
        else
            objects_with_id[index]->reset_stayed_in_side = 1;

        obj->stayed_in_side = objects_with_id[index]->stayed_in_side;
        objects_with_id[index]->predicted_x = obj->center_x + (obj->center_x - objects_with_id[index]->center_x);
        objects_with_id[index]->predicted_y = obj->center_y + (obj->center_y - objects_with_id[index]->center_y);
        objects_with_id[index]->frame_num = obj->frame_num;
        objects_with_id[index]->center_x = obj->center_x;
        objects_with_id[index]->center_y = obj->center_y;
        objects_with_id[index]->average_angle = obj->average_angle;
        objects_with_id[index]->exists_counter++;
        obj->exists_counter = objects_with_id[index]->exists_counter;
        if (obj->intersect)
            obj->crossed = objects_with_id[index]->side;
        objects_with_id[index]->side = obj->side;
    } else {
        obj->id = id_counter;
        obj->predicted_x = obj->center_x;
        obj->predicted_y = obj->center_y;
        ids[id_counter]++;
        store_object(obj, s);
        
    }
}

/**
 * Find the highest value in the array.
 */
static int find_max(int array[], int array_length)
{
    int max;
    if (array_length < 1)
        return -1;
    max = array[0];
    for (int i = 1; i < array_length; i++) {
        if (array[i] > max)
            max = array[i];
    }
    return max;
}

/**
 * Find the lowest value in the array.
 */
static int find_min(int array[], int array_length)
{
    int min;
    if (array_length < 1)
        return -1;
    min = array[0];
    for (int i = 1; i < array_length; i++) {
        if (array[i] < min)
            min = array[i];
    }
    return min;
}

static int is_object_multiple_side(Object *obj, TDContext *s){

    int side = 0;
    int previous_side = 0;
    for (int i = 0; i < obj->rectangle_counter; i++)
    {
        side = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->rectangles[i].x, obj->rectangles[i].y);
        if (side == previous_side || previous_side == 0){
            previous_side = side;
        }else{
            return 1;
        }
    }
    return 0;
}

/**
 * Calculate object variables after the motion vector sorting is done.
 * 
 * Function to find the x and y min and max coordinates of the object,
 * and inspect some conditions.
 */
static void check_object(Object *obj, TDContext *s, AVFrame *frame)
{
    int left_upper, right_down, left_down, right_upper;
    int center_side;
    // Calculate average length (we added together them trough the process, and then divide with motion vector number, to get the average)
    // Filter by angle where the object goes
    get_object_rectangles(obj, frame, s);
    if(s->tripwire){ // if we care about the tripwire
        if (s->draw_diagonal && s->object_marker_box){
            draw_line(frame, obj->x_max, obj->y_max, obj->x_min, obj->y_min, obj->color);
            draw_line(frame, obj->x_min, obj->y_max, obj->x_max, obj->y_min, obj->color);
        }
        left_upper = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->x_min, obj->y_min);
        left_down = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->x_min, obj->y_max);
        right_upper = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->x_max, obj->y_min);
        right_down = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->x_max, obj->y_max);
        center_side = which_side(s->start_x, s->start_y, s->end_x, s->end_y, obj->center_x, obj->center_y);
        obj->side = center_side;

        if (right_down == 1 && right_upper == 1 && left_upper == 1  && left_down == 1){
            obj->side = 2;
        }
        if (right_down == -1 && right_upper == -1 && left_upper == -1  && left_down == -1){
            obj->side = -2;
        }

        if (obj->side == -1 || obj->side == 1){
            if (!is_object_multiple_side(obj, s))
                obj->side *= 2;
        }
    }
    object_id_check(obj, s);
    print_json(obj, s);

    if(s->object_marker_box_history)
        store_box_history(obj, s);
    if (s->object_marker_box){
        draw_polygon(obj, frame, s);
        if (s->object_marker_box_history){
            draw_box_history(s, frame);
        }
    }
    
}

/**
 * Add the motion vector for the object
 * @param obj Object to add
 * @param src_x motion vector start point's x coordinate
 * @param src_y motion vector start point's y coordinate
 * @param dst_x motion vector end point's x coordinate
 * @param dst_y motion vector end point's y coordinate
 * @param length length of the motion vector
 */
static void add_to_object(Object* obj, int src_x, int src_y, int dst_x, int dst_y)
{
    obj->src_x_s[obj->counter] = src_x;
    obj->src_y_s[obj->counter] = src_y;
    obj->dst_x_s[obj->counter] = dst_x;
    obj->dst_y_s[obj->counter] = dst_y;
    obj->distance_x += (dst_x - src_x); //  Calculate object relative distance each direction
    obj->distance_y += (dst_y - src_y);
    obj->frame_num = video_frame_count;
    obj->average_angle = (atan2(obj->distance_y, obj->distance_x)) * (180 / M_PI); // Calculate average angle from absolute motion vectors
    obj->average_angle *= -1;
    if (obj->average_angle < 0)
        obj->average_angle += 360;  //  -180, 180 -> 0, 360
    // obj->average_length += length;
    obj->center_x += dst_x;
    obj->center_y += dst_y;
    obj->counter++;
}

static int config_input(AVFilterLink *inlink)
{
    double angle_rad, dx, dy;
    double x_left, y_left, x_right, y_right, x_top, y_top, x_bottom, y_bottom;
    int x_coordinates[4], y_coordinates[4];
    int valid_coordinates[8];
    int valid_coordinate_counter = 0;

    AVFilterContext *ctx = inlink->dst;
    TDContext *s = ctx->priv;
    s->filter_id = ctx->name[strlen(ctx->name) - 1] - '0';

    s->bytes = 0;

    //  if there is an url set, open connection
    if (s->url) 
        open_connection(s, s->url);

    if (0 < s->start_x && s->start_x < 1)  // between 1 and 0, use relative size
        s->start_x = inlink->w * s->start_x;
    if (0 < s->start_y && s->start_y < 1)
        s->start_y = inlink->h * s->start_y;
    
    if (s->start_x == -1)  //   set the default value from -1 to center point.
        s->start_x = inlink->w/2;
    if (s->start_y == -1)
        s->start_y = inlink->h/2;

    angle_rad = s->tripwire_line_angle * M_PI / 180.0;
    angle_rad = M_PI - angle_rad;
    
    dx = cos(angle_rad);
    dy = sin(angle_rad);

    if(fabs(dx) > DBL_EPSILON) {
        x_left = 0;
        y_left = s->start_y - (s->start_x - x_left) * dy / dx;

        x_right = inlink->w;
        y_right = s->start_y + (x_right - s->start_x) * dy / dx;
    }

    if(fabs(dy) > DBL_EPSILON) {
        y_top = 0;
        x_top = s->start_x - (s->start_y - y_top) * dx / dy;

        y_bottom = inlink->h;
        x_bottom = s->start_x + (y_bottom - s->start_y) * dx / dy;
    }

    x_coordinates[0] = x_top; x_coordinates[1] = x_bottom; x_coordinates[2] = x_right; x_coordinates[3] = x_left; 
    y_coordinates[0] = y_top; y_coordinates[1] = y_bottom; y_coordinates[2] = y_right; y_coordinates[3] = y_left;
    for (int i = 0; i < 4; i++)
    {   
        if (0 <= x_coordinates[i] && x_coordinates[i] <= inlink->w && 0 <= y_coordinates[i] && y_coordinates[i] <= inlink->h){
            valid_coordinates[0+valid_coordinate_counter] = x_coordinates[i];   
            valid_coordinates[1+valid_coordinate_counter] = y_coordinates[i];
            valid_coordinate_counter += 2;
        }
    }
    if (s->tripwire_line_angle == 0 || s->tripwire_line_angle == 180){
        s->start_x = 0;
        s->start_y = s->start_y;
        s->end_x = inlink->w;
        s->end_y = s->start_y;
    }
    else{
        s->start_x = valid_coordinates[0];
        s->start_y = valid_coordinates[1];
        s->end_x = valid_coordinates[2];
        s->end_y = valid_coordinates[3];
    }

    if (s->url)
        s->buffer = (char*) malloc(512 * sizeof(int));

    if (s->parameters) { 
        char parameters[] = "{\n\t\"module\": \"object_tracker\",\n"
                    "\t\"version\": \"%s\",\n\t\"release_date\": \"%s\",\n\t\"tripwire\": %d,\n\t\"tripwire_type\": %d,\n\t\"tripwire_line_px\": %.0f,\n\t\"tripwire_line_py\": %.0f,\n\t\"tripwire_line_angle\": %.0f,\n\t\"tripwire_marker_line\": %d,\n"
                    "\t\"max_mv_distance\": %d,\n\t\"min_mv_length\": %d,\n\t\"max_angle_diff\": %d,\n\t\"crop_x\": %.0lf,\n\t\"crop_y\": %.0lf,\n\t\"crop_width\": %.0lf,\n\t\"crop_height\": %.0lf,\n\t\"resize_to_crop\": %d,\n\t\"black_filter\": %d,\n"
                    "\t\"min_mv_num\": %d,\n\t\"angle_filter\": %d,\n\t\"angle_filter_angle\": %.0lf,\n\t\"angle_filter_range\": %.0lf,\n\t\"object_marker_box\": %d,\n\t\"object_rectangle_thickness\": %d,\n\t\"object_marker_box_history\": %d,\n\t\"object_history_draw_length\": %d,\n"
                    "\t\"mask_static_image_parts\": %d,\n\t\"mask_i_frames\": %d,\n\t\"keep_mask_on_static_image\": %d,\n\t\"json_output_line_break\": %d,\n"
                    "\t\"print_only_intersect_trigger\": %d,\n\t\"print_lite_mode\": %d,\n\t\"print_rectangle_positions\": %d,\n\t\"url\": \"%s\",\n\t\"std_err_text_output_enable\": %d,\n"
                    "\t\"parameter_summary_row\": %d,\n\t\"grid_size\": %d,\n\t\"object_survival_time\": %d,\n\t\"max_obj_distance_history\": %d,\n\t\"select_frames_where_tripwire_detected\": %d,\n\t\"select_frames_where_object_detected\": %d\n}\n";

        if (!s->line_break) {
            replace(parameters, (char *)"{\n\t", (char *)"{");
            replace(parameters, (char *)"\n\t", (char *)" ");
            replace(parameters, (char *)"\n}", (char *)"}");
        }

        if (s->url) {
            // Sending the data to the buffer
            s->bytes = sprintf(s->buffer, (const char *)parameters, version, release_date, s->tripwire, s->tripwire_type, s->start_x, s->start_y, s->tripwire_line_angle, s->tripwire_marker_line,
            s->max_distance, s->min_mv_length, s->max_angle_diff, s->crop_x, s->crop_y, s->crop_width, s->crop_height, s->resize_to_crop, s->black_filter,
            s->min_mv, s->angle_enabled, s->angle, s->angle_range, s->object_marker_box, s->thickness, s->object_marker_box_history, s->detection_threshold,
            s->mask_static_areas, s->mask_i_frames, s->keep_mask_on_image, s->line_break,
            s->print_only_intersect_trigger, s->print_lite_mode, s->print_rectangles_position, s->url, s->std_err_text_output_enable,
            s->parameters, s->grid_size, s->obj_survival_time, s->max_obj_distance_history, s->select_frames_where_tripwire, s->scene_static_frames);

            // Reallocating the amount of memory the buffer needs
            s->buffer = (char*)realloc(s->buffer, s->bytes * sizeof(int));
        }

        if (s->std_err_text_output_enable) {  // If the error log or URL is enabled, send the data there
            fprintf(stderr, parameters, version, release_date, s->tripwire, s->tripwire_type, s->start_x, s->start_y, s->tripwire_line_angle, s->tripwire_marker_line,
            s->max_distance, s->min_mv_length, s->max_angle_diff, s->crop_x, s->crop_y, s->crop_width, s->crop_height, s->resize_to_crop, s->black_filter,
            s->min_mv, s->angle_enabled, s->angle, s->angle_range, s->object_marker_box, s->thickness, s->object_marker_box_history, s->detection_threshold,
            s->mask_static_areas, s->mask_i_frames, s->keep_mask_on_image, s->line_break,
            s->print_only_intersect_trigger, s->print_lite_mode, s->print_rectangles_position, s->url, s->std_err_text_output_enable,
            s->parameters, s->grid_size, s->obj_survival_time, s->max_obj_distance_history, s->select_frames_where_tripwire, s->scene_static_frames);
        }else{
            printf(parameters, version, release_date, s->tripwire, s->tripwire_type, s->start_x, s->start_y, s->tripwire_line_angle, s->tripwire_marker_line,
            s->max_distance, s->min_mv_length, s->max_angle_diff, s->crop_x, s->crop_y, s->crop_width, s->crop_height, s->resize_to_crop, s->black_filter,
            s->min_mv, s->angle_enabled, s->angle, s->angle_range, s->object_marker_box, s->thickness, s->object_marker_box_history, s->detection_threshold,
            s->mask_static_areas, s->mask_i_frames, s->keep_mask_on_image, s->line_break,
            s->print_only_intersect_trigger, s->print_lite_mode, s->print_rectangles_position, s->url, s->std_err_text_output_enable,
            s->parameters, s->grid_size, s->obj_survival_time, s->max_obj_distance_history, s->select_frames_where_tripwire, s->scene_static_frames);

        }
    }
    s->angle *= s->angle_enabled;
    return 0;
}

/**
 * Gets two object as input.
 * The first object gets all the motion vectors from the second object.
 * The second object x_max value will be set to -1 to definde that the object get merged, the rest of the datas stays the same
*/
static void merge_two_object(Object *object_into, Object *for_merging)
{
    int counter = 0;

    object_into->distance_x += for_merging->distance_x;
    object_into->distance_y += for_merging->distance_y;

    for (int i = object_into->counter; i < object_into->counter+for_merging->counter; i++)
    {
        object_into->src_x_s[i] = for_merging->src_x_s[counter];
        object_into->src_y_s[i] = for_merging->src_y_s[counter];
        object_into->dst_x_s[i] = for_merging->dst_x_s[counter];
        object_into->dst_y_s[i] = for_merging->dst_y_s[counter];

        counter++;
    }

    object_into->center_y = (object_into->center_y * object_into->counter + for_merging->center_y * for_merging->counter) / (for_merging->counter + object_into->counter);
    object_into->center_x = (object_into->center_x * object_into->counter + for_merging->center_x * for_merging->counter) / (for_merging->counter + object_into->counter);
    
    object_into->counter += for_merging->counter;

    object_into->x_max = FFMAX(object_into->x_max, for_merging->x_max);
    object_into->x_min = FFMIN(object_into->x_min, for_merging->x_min);
    object_into->y_max = FFMAX(object_into->y_max, for_merging->y_max);
    object_into->y_min = FFMIN(object_into->y_min, for_merging->y_min);
    for_merging->x_max = -1;

    object_into->average_angle = (atan2(object_into->distance_y, object_into->distance_x)) * (180 / M_PI); // Calculate average angle from absolute motion vectors
    object_into->average_angle *= -1;
    if (object_into->average_angle < 0)
        object_into->average_angle += 360;  //  -180, 180 -> 0, 360
    

}

/**
 * This function is checks, if 2 obejct coud be the same moving object on the image,
 *  just get detected as multiple for some reason.
*/
static int compare_two_object(Object *o1, Object *o2)
{
    double o1_area, o2_area, both_o_area;
    double relative_o1_shared_area, relative_o2_shared_area, biggest_shared_area;
    double relative_angle_difference, share_point;
    int x1 = FFMIN(o1->x_max, o2->x_max);
    int x2 = FFMAX(o1->x_min, o2->x_min);
    int y1 = FFMIN(o1->y_max, o2->y_max);
    int y2 = FFMAX(o1->y_min, o2->y_min);

    if (x1 < x2)
        return 0;
    if (y1<y2)
        return 0;
    o1_area = (o1->x_max-o1->x_min) * (o1->y_max-o1->y_min);
    o2_area = (o2->x_max-o2->x_min) * (o2->y_max-o2->y_min);
    both_o_area = (x2-x1) * (y2-y1);
    relative_o1_shared_area = both_o_area / o1_area;
    relative_o2_shared_area = both_o_area / o2_area;
    if(relative_o1_shared_area > relative_o2_shared_area)
        biggest_shared_area = relative_o1_shared_area;
    else
        biggest_shared_area = relative_o2_shared_area;
    if (biggest_shared_area > 0.6)  // if more than half shared no questions...
        return 1;
    relative_angle_difference = (smallest_angle(o1->average_angle, o2->average_angle))/180;
    share_point = biggest_shared_area / relative_angle_difference;  // Bigger this number the 2 object more "same"
    if (share_point > 0.4)
        return 1;

    return 0;
}

/**
 * Copy obj2 data into obj1
 * @param obj1 Object to copy into
 * @param obj2 Object to be copied
*/
static void copy_object_data(Object* obj1, Object *obj2)
{
    obj1->average_angle = obj2->average_angle;
    obj1->average_length = obj2->average_length;
    obj1->distance_x = obj2->distance_x;
    obj1->distance_y = obj2->distance_y;
    obj1->frame_num = obj2->frame_num;
    obj1->counter = obj2->counter;
    obj1->id = obj2->id;
    obj1->center_x = obj2->center_x;
    obj1->center_y = obj2->center_y;
    obj1->x_min = obj2->x_min;
    obj1->y_min = obj2->y_min;
    obj1->x_max = obj2->x_max;
    obj1->y_max = obj2->y_max;
    obj1->rectangle_counter = obj2->rectangle_counter;
    obj1->intersect = obj2->intersect;
    obj1->crossed = obj2->crossed;
    obj1->dir_counter = obj2->dir_counter;
    obj1->side = obj2->side;

    // Copy the array elements
    for (int i = 0; i < obj1->counter; i++) {
        obj1->src_x_s[i] = obj2->src_x_s[i];
        obj1->src_y_s[i] = obj2->src_y_s[i];
        obj1->dst_x_s[i] = obj2->dst_x_s[i];
        obj1->dst_y_s[i] = obj2->dst_y_s[i];
    }

    // Copy the color array
    for (int i = 0; i < 3; i++) {
        obj1->color[i] = obj2->color[i];
    }

}

/**
 * Lopp through exitsting objects and merge objects what are the same moving object on the image
*/
static void merge_objects(Object** objects, TDContext *s, int *ptr_object_counter)
{
    Object *new_objects[2000];
    int object_counter = 0;
    for (int i=0; i<*ptr_object_counter; i++){
        if (objects[i]->x_max == -1)
            continue;
        for (int other_object = i+1; other_object<*ptr_object_counter; other_object++){
            if (objects[other_object]->x_max == -1)
                continue;
            if(compare_two_object(objects[i], objects[other_object])){
                merge_two_object(objects[i], objects[other_object]);
                // objects[other_object] = NULL;
            }
        }
    }
    for (int i = 0; i < *ptr_object_counter; i++)  // reorder objects
    {
        if (objects[i]->x_max == -1){
            free(objects[i]);
            continue;
        }
        
        new_objects[object_counter] = create_object();
        
        copy_object_data(new_objects[object_counter], objects[i]);
        free(objects[i]);
        object_counter++;
    }

    
    for (int i = 0; i < object_counter; i++)
    {
        objects[i] = create_object();
        copy_object_data(objects[i], new_objects[i]);
        free(new_objects[i]);
    }
    *ptr_object_counter = object_counter;
}

/**
 * If the filter gets a video what is reseized or get cropped that not effect the motion vectors only the image.
 * To visualize the object correctly we have to find out what the original video size was, and rescale the motion vectors.
 * For this reason we check the size of the motion vector table and make an estimate.
*/
static void find_motion_vector_image_size(AVFrameSideData *motion_vector_table, TDContext *s, int frame_width, int frame_height){
    int mv_table_size = motion_vector_table->size / sizeof(AVMotionVector);
    AVMotionVector *mvs = (AVMotionVector *)motion_vector_table->data;
    int max_x = 0, max_y = 0;
    int motion_vector_image_size_x, motion_vector_image_size_y;
    for (int i = 0; i < mv_table_size; i++) {   // Loop trough all the mvs of the frame
        AVMotionVector *mv = &mvs[i];
        if (mv->src_x > max_x)
            max_x = mv->src_x;
        if (mv->src_y > max_y)
            max_y = mv->src_y;
    }
    motion_image_size_x = max_x;
    motion_image_size_y = max_y;

    // set the crop size for the image
    if (s->crop_x <= 1)
        s->crop_x = s->crop_x * motion_image_size_x;
    if (s->crop_y <= 1)
        s->crop_y = s->crop_y * motion_image_size_y;
    if (s->crop_width <= 1)
        motion_vector_image_size_x = s->crop_width * motion_image_size_x;
    if (s->crop_height <= 1)
        motion_vector_image_size_y = s->crop_height * motion_image_size_y;
        
    s->crop_width = s->crop_width * motion_image_size_x + s->crop_x;
    s->crop_height = s->crop_height * motion_image_size_y + s->crop_y;
    s->resize_ratio_x = (double) frame_width / motion_vector_image_size_x;
    s->resize_ratio_y = (double) frame_height / motion_vector_image_size_y;
}

/**
 * Mask all the iamge balck what not an object
*/
static void mask_image(Object** object_list, int object_counter, AVFrame *frame, TDContext *s)
{
    int x_min = frame->width, y_min = frame->height, x_max = 0, y_max = 0;
    int color[3] = {16, 128, 128};  // black
    int left_out[50][4];
    int active_objects = 0;
    int object_in_rectangle = 0;
    for (int i=0; i < object_counter; i++){
        if (is_object_not_filtered(object_list[i], s)) {
            left_out[active_objects][0] = object_list[i]->x_min;
            left_out[active_objects][1] = object_list[i]->y_min;
            left_out[active_objects][2] = object_list[i]->x_max;
            left_out[active_objects][3] = object_list[i]->y_max;
            active_objects++;
            if (x_min > object_list[i]->x_min)
                x_min = object_list[i]->x_min;

            if (y_min > object_list[i]->y_min)
                y_min = object_list[i]->y_min;

            if (x_max < object_list[i]->x_max)
                x_max = object_list[i]->x_max;

            if (y_max < object_list[i]->y_max)
                y_max = object_list[i]->y_max;
        }
    }
    for (int z=0; z <= y_min; z++){  // From up to down: full width, down to y_min
        draw_line(frame, 0, z, frame->width, z, color);
    }
    for (int z=y_max; z <= frame->height-1; z++){
        draw_line(frame, 0, z, frame->width, z, color);
    }
    for (int z=0; z <= x_min; z++){
        draw_line(frame, z, y_min, z, y_max, color);
    }
    for (int z=x_max+1; z < frame->width; z++){
        draw_line(frame, z, y_max-1, z, y_min-1, color);
    }
    
    if (active_objects <= 1)  // there is only one or 0 object:
        return;
    for (int x=x_min-5; x < x_max; x += 8){  // -5 to make a little big bigger window
        for (int y = y_min-5; y < y_max; y += 8){
            object_in_rectangle = 0;
            for (int i = 0; i < active_objects; i++){
                if (point_in_rectangle(x, y, left_out[i][2], left_out[i][0], left_out[i][3], left_out[i][1]))
                    object_in_rectangle = 1;
                }
            if (!object_in_rectangle){
                for (int line = 0; line < 16; line ++){
                    draw_line(frame, x+line, y, x+line, y+16, color);
                }
            }
        }
    }
}

/**
 * If the mask_static_image_parts is turned on, and we want to keep the mask for many frames after no objects get detected, this funciton will count and keep the mask on.
*/
static void keep_mask_on_image(Object** object_lsit, int object_counter, AVFrame *frame, TDContext *s)
{
    if(s->keep_mask_on_image > 0){
        if (s->keep_mask_on_image > last_mask_repeated_for){  // The mask shoud be repeated
            last_mask_repeated_for++;
            mask_image(object_lsit, object_counter, frame, s);
        }else{
            mask_image(object_lsit, 0, frame, s); // Mask the image to black. its can't get here if there are object detected
        }
    }
}

/**
 * This function calculates the object datas, after the motion vector sortier is done.
*/
static void calculate_result_data_to_object(Object *obj)
{
    int x_min_src = find_min(obj->src_x_s, obj->counter);
    int x_max_src = find_max(obj->src_x_s, obj->counter);
    int y_min_src = find_min(obj->src_y_s, obj->counter);
    int y_max_src = find_max(obj->src_y_s, obj->counter);
    
    int x_min_dst = find_min(obj->dst_x_s, obj->counter);
    int x_max_dst = find_max(obj->dst_x_s, obj->counter);
    int y_min_dst = find_min(obj->dst_y_s, obj->counter);
    int y_max_dst = find_max(obj->dst_y_s, obj->counter);
    
    obj->x_min = FFMIN(x_min_dst, x_min_src);
    obj->y_min = FFMIN(y_min_dst, y_min_src);
    obj->x_max = FFMAX(x_max_dst, x_max_src);
    obj->y_max = FFMAX(y_max_dst, y_max_src);

    obj->average_length = sqrt(obj->distance_x * obj->distance_x + obj->distance_y * obj->distance_y)/obj->counter;
    
    obj->center_x /= obj->counter;
    obj->center_y /= obj->counter;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame) {
    AVFilterContext *ctx = inlink->dst;
    TDContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int i = 0;
    uint8_t *ptr;
    int mv_processed, ret;
    int obj_counter = 0;
    double length, angle;
    int color[3] = {255, 0, 0};
    int active_frame = 0;
    Object *objects[5000];
    AVFrameSideData *motion_vector_table;
    video_frame_count++;
    tripwire_event_detected_on_the_frame = 0;
    motion_vector_table = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS); // Get the mvs from the frame
    if (motion_vector_table) {  // If the frame has mvs.
        int mv_table_size = motion_vector_table->size / sizeof(AVMotionVector);
        AVMotionVector *mvs = (AVMotionVector *)motion_vector_table->data;

        // draw tripwire line if needed
        if (s->tripwire_marker_line && s->tripwire){
            draw_line(frame, s->start_x, s->start_y, s->end_x, s->end_y, color);
        }
            
        if (motion_image_size_x == 0)
            find_motion_vector_image_size(motion_vector_table, s, frame->width, frame->height);

        for (i = 0; i < mv_table_size; i++) {   // Loop trough all the mvs of the frame
            AVMotionVector *mv = &mvs[i];

            if (mv->dst_x - mv->src_x == 0 && mv->dst_y - mv->src_y == 0)   // If destination and source point the same, then is a 0 vector.
                continue;

            length = distance(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);

            if (length <= s->min_mv_length) // Filter short mvs, based on min length.
                continue;
            if (s->crop_x > mv->src_x || mv->src_x > s->crop_width)  // filter with crop
                continue;   
            if (s->crop_y > mv->src_y || mv->src_y > s->crop_height) // filter with crop
                continue;

            if (s->resize_to_crop == 1){  // Resize motion vectors
                mv->src_x -= s->crop_x;
                mv->dst_x -= s->crop_x;
                mv->src_y -= s->crop_y;
                mv->dst_y -= s->crop_y;
                mv->src_x *= s->resize_ratio_x;
                mv->dst_x *= s->resize_ratio_x;
                mv->src_y *= s->resize_ratio_y;
                mv->dst_y *= s->resize_ratio_y;
            }
            if (mv->source == 1){    // change direction for the backwards motion vectors
                FFSWAP(int, mv->dst_x, mv->src_x);
                FFSWAP(int, mv->dst_y, mv->src_y);
            }

            if (s->black_filter){
                if (mv->src_x >= frame->width || mv->src_y <= 0 || mv->src_y >= frame->height || mv->src_x <= 0){ // the mv is on the last pixel line, or outside of the image
                    continue;
                }
                ptr = &frame->data[0][mv->src_y * frame->linesize[0] + mv->src_x];
                if (*ptr == 16){
                    ptr = &frame->data[1][(mv->src_y / 2) * frame->linesize[1] + mv->src_x / 2];
                    if (*ptr == 128){
                        ptr = &frame->data[2][(mv->src_y / 2) * frame->linesize[2] + mv->src_x / 2];
                        if (*ptr == 128){
                            continue;
                        }
                    }
                }
            }

            mv_processed = 0;

            // angle = get_angle(mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
            angle = (atan2(mv->dst_y-mv->src_y, mv->dst_x-mv->src_x)) * (180 / M_PI);
            angle *= -1;
            if (angle < 0)
                angle += 360;
            for (int j = 0; j < obj_counter; j++) { // Loop trough on Objects, searching for objects what are close
                if (part_of_object(objects[j], mv->src_x, mv->src_y, angle, s)){ // return True if its part of the object
                    add_to_object(objects[j], mv->src_x, mv->src_y, mv->dst_x, mv->dst_y);
                    mv_processed = 1;
                    break; // Stop searching if the object was founded.
                }
            }
            if(!mv_processed){ // If we did not find an object, create a new one and the first element will be the new mv.
                objects[obj_counter] = create_object();
                add_to_object(objects[obj_counter], mv->src_x,  mv->src_y, mv->dst_x, mv->dst_y);
                obj_counter++;
            }
        }
    }else{  // No motion vector was found
        if (s->scene_static_frames == 1){ // Make sure, that the first frame will be send to the output
            if (last_frame_skipped == 1 && first_frame_returned == 1){
                av_frame_free(&frame);
                return 0;
            }
        }
        if(s->select_frames_where_tripwire  && first_frame_returned){
            av_frame_free(&frame);
            return 0;
        }
        if (s->mask_static_areas){
            if (s->mask_i_frames == 2)  // mask as the previous frame
                keep_mask_on_image(last_detected_objects, last_detected_objects_counter, frame, s);
            if (s->mask_i_frames == 1)  // a full black image
                mask_image(objects, 0, frame, s);
            // if (s->mask_i_frames == 0)  // Dont mask the i frame, returns the original frame
        }
        first_frame_returned = 1;
        return ff_filter_frame(outlink, frame);
    }
    // Go through all of the detected objects in a frame and decide if it belongs to an already existing object and get some information about it
        
    for (int i = 0; i < obj_counter; i++) {
        if (objects[i]->counter > s->min_mv) { // Filter objects, based on mv number.
            calculate_result_data_to_object(objects[i]);
        }
    }
    merge_objects(objects, s, &obj_counter);
    for (int i = 0; i < obj_counter; i++) {
        if (is_object_not_filtered(objects[i], s)) {
            active_frame = 1;
            check_object(objects[i], s, frame);
        }
    }
    if (s->mask_static_areas){
        if (active_frame){
            last_mask_repeated_for = 0;
            mask_image(objects, obj_counter, frame, s);
        } else{
            keep_mask_on_image(last_detected_objects, last_detected_objects_counter, frame, s);
        }
    }    
    
    //if the url is set and there is data to send then we write the output to the url
    if (s->url && s->bytes) {
        //writing the data
        if ((ret = http_write(s->uc, s->buffer, s->bytes) < 0))
            return AVERROR(EINVAL);
        //freeing the buffer for the next frame
        free(s->buffer);
        s->bytes = 0;
        s->buffer = (char*) malloc(512 * sizeof(int));
    }
    if (s->mask_i_frames){  // save the last frame's data for mask i frames
        for (int i = 0; i < last_frame_object_counter; i++){  // free the previous frame's data
            free(last_frames_object[i]);
        }
        for(int i = 0; i < obj_counter; i++) {   // Save the current frame's objects
            last_frames_object[i] = malloc(sizeof(Object));
            *last_frames_object[i] = *objects[i];
        }
        last_frame_object_counter = obj_counter;
    }
    
    if (s->keep_mask_on_image > 0){  // save the last detected objects
        if (active_frame){  // has object on it what has mask on it
            for (int i = 0; i < last_detected_objects_counter; i++){  // free the previous frame's data
                free(last_detected_objects[i]);
            }
            for(int i = 0; i < obj_counter; i++) {   // Save the current frame's objectss
                last_detected_objects[i] = malloc(sizeof(Object));
                *last_detected_objects[i] = *objects[i];
            }
            last_detected_objects_counter = obj_counter;
        }
    }
    

    for (int i = 0; i < obj_counter; i++) {  // free the allocated memory
        free(objects[i]);
    }

    last_frame_skipped = 0;
    if (active_frame == 0){  // If the scene_static_frames is on and no object detected dont send the frames.
        if (s->scene_static_frames > 0 && first_frame_returned == 1){
            last_frame_skipped = 1;
            av_frame_free(&frame);
            return 0;
        }
    }
    if (s->select_frames_where_tripwire && tripwire_event_detected_on_the_frame == 0  && first_frame_returned == 1){
        av_frame_free(&frame);
        return 0;
    }
    first_frame_returned = 1;
    return ff_filter_frame(outlink, frame);
}

static void uninit(AVFilterContext *ctx)
{
    for (int i = 0; i < counter; i++) {
        free(every_object[i]);
    }
}


static const AVFilterPad object_tracker_inputs[] = {
        {
                .name = "default",
                .type = AVMEDIA_TYPE_VIDEO,
                .filter_frame = filter_frame,
                .config_props = config_input,
                .flags = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        },
};

static const AVFilterPad object_tracker_outputs[] = { { .name = "default", .type = AVMEDIA_TYPE_VIDEO, }, };

const AVFilter ff_vf_object_tracker = { 
        .name           = "object_tracker", 
        .description    = NULL_IF_CONFIG_SMALL("Tracking object based on motion vectors from video encoding."), 
        .priv_size      = sizeof(TDContext), 
        .priv_class     = &object_tracker_class,
        .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
        .uninit         = uninit,
        FILTER_INPUTS(object_tracker_inputs),
        FILTER_OUTPUTS(object_tracker_outputs),
        FILTER_PIXFMTS_ARRAY(pix_fmts), 
};
