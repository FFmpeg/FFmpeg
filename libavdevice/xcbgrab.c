/*
 * XCB input grabber
 * Copyright (C) 2014 Luca Barbato <lu_zero@gentoo.org>
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

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#if CONFIG_LIBXCB_XFIXES
#include <xcb/xfixes.h>
#endif

#if CONFIG_LIBXCB_SHM
#include <sys/shm.h>
#include <xcb/shm.h>
#endif

#if CONFIG_LIBXCB_SHAPE
#include <xcb/shape.h>
#endif

#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

typedef struct XCBGrabContext {
    const AVClass *class;

    xcb_connection_t *conn;
    xcb_screen_t *screen;
    xcb_window_t window;
#if CONFIG_LIBXCB_SHM
    AVBufferPool *shm_pool;
#endif
    int64_t time_frame;
    AVRational time_base;
    int64_t frame_duration;

    xcb_window_t window_id;
    int x, y;
    int width, height;
    int frame_size;
    int bpp;

    int draw_mouse;
    int follow_mouse;
    int show_region;
    int region_border;
    int centered;
    int select_region;

    const char *framerate;

    int has_shm;
} XCBGrabContext;

#define FOLLOW_CENTER -1

#define OFFSET(x) offsetof(XCBGrabContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "window_id", "Window to capture.", OFFSET(window_id), AV_OPT_TYPE_INT, { .i64 = XCB_NONE }, 0, UINT32_MAX, D },
    { "x", "Initial x coordinate.", OFFSET(x), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, D },
    { "y", "Initial y coordinate.", OFFSET(y), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, D },
    { "grab_x", "Initial x coordinate.", OFFSET(x), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, D },
    { "grab_y", "Initial y coordinate.", OFFSET(y), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, D },
    { "video_size", "A string describing frame size, such as 640x480 or hd720.", OFFSET(width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL }, 0, 0, D },
    { "framerate", "", OFFSET(framerate), AV_OPT_TYPE_STRING, {.str = "ntsc" }, 0, 0, D },
    { "draw_mouse", "Draw the mouse pointer.", OFFSET(draw_mouse), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, D },
    { "follow_mouse", "Move the grabbing region when the mouse pointer reaches within specified amount of pixels to the edge of region.",
      OFFSET(follow_mouse), AV_OPT_TYPE_INT, { .i64 = 0 },  FOLLOW_CENTER, INT_MAX, D, "follow_mouse" },
    { "centered", "Keep the mouse pointer at the center of grabbing region when following.", 0, AV_OPT_TYPE_CONST, { .i64 = -1 }, INT_MIN, INT_MAX, D, "follow_mouse" },
    { "show_region", "Show the grabbing region.", OFFSET(show_region), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, D },
    { "region_border", "Set the region border thickness.", OFFSET(region_border), AV_OPT_TYPE_INT, { .i64 = 3 }, 1, 128, D },
    { "select_region", "Select the grabbing region graphically using the pointer.", OFFSET(select_region), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, D },
    { NULL },
};

static const AVClass xcbgrab_class = {
    .class_name = "xcbgrab indev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_INPUT,
};

static int xcbgrab_reposition(AVFormatContext *s,
                              xcb_query_pointer_reply_t *p,
                              xcb_get_geometry_reply_t *geo)
{
    XCBGrabContext *c = s->priv_data;
    int x = c->x, y = c->y;
    int w = c->width, h = c->height, f = c->follow_mouse;
    int p_x, p_y;

    if (!p || !geo)
        return AVERROR(EIO);

    p_x = p->win_x;
    p_y = p->win_y;

    if (f == FOLLOW_CENTER) {
        x = p_x - w / 2;
        y = p_y - h / 2;
    } else {
        int left   = x + f;
        int right  = x + w - f;
        int top    = y + f;
        int bottom = y + h - f;
        if (p_x > right) {
            x += p_x - right;
        } else if (p_x < left) {
            x -= left - p_x;
        }
        if (p_y > bottom) {
            y += p_y - bottom;
        } else if (p_y < top) {
            y -= top - p_y;
        }
    }

    c->x = FFMIN(FFMAX(0, x), geo->width  - w);
    c->y = FFMIN(FFMAX(0, y), geo->height - h);

    return 0;
}

static void xcbgrab_image_reply_free(void *opaque, uint8_t *data)
{
    free(opaque);
}

static int xcbgrab_frame(AVFormatContext *s, AVPacket *pkt)
{
    XCBGrabContext *c = s->priv_data;
    xcb_get_image_cookie_t iq;
    xcb_get_image_reply_t *img;
    xcb_drawable_t drawable = c->window_id;
    xcb_generic_error_t *e = NULL;
    uint8_t *data;
    int length;

    iq  = xcb_get_image(c->conn, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable,
                        c->x, c->y, c->width, c->height, ~0);

    img = xcb_get_image_reply(c->conn, iq, &e);

    if (e) {
        av_log(s, AV_LOG_ERROR,
               "Cannot get the image data "
               "event_error: response_type:%u error_code:%u "
               "sequence:%u resource_id:%u minor_code:%u major_code:%u.\n",
               e->response_type, e->error_code,
               e->sequence, e->resource_id, e->minor_code, e->major_code);
        free(e);
        return AVERROR(EACCES);
    }

    if (!img)
        return AVERROR(EAGAIN);

    data   = xcb_get_image_data(img);
    length = xcb_get_image_data_length(img);

    pkt->buf = av_buffer_create(data, length, xcbgrab_image_reply_free, img, 0);
    if (!pkt->buf) {
        free(img);
        return AVERROR(ENOMEM);
    }

    pkt->data = data;
    pkt->size = length;

    return 0;
}

static int64_t wait_frame(AVFormatContext *s, AVPacket *pkt)
{
    XCBGrabContext *c = s->priv_data;
    int64_t curtime, delay;

    c->time_frame += c->frame_duration;

    for (;;) {
        curtime = av_gettime_relative();
        delay   = c->time_frame - curtime;
        if (delay <= 0)
            break;
        av_usleep(delay);
    }

    return curtime;
}

#if CONFIG_LIBXCB_SHM
static int check_shm(xcb_connection_t *conn)
{
    xcb_shm_query_version_cookie_t cookie = xcb_shm_query_version(conn);
    xcb_shm_query_version_reply_t *reply;

    reply = xcb_shm_query_version_reply(conn, cookie, NULL);
    if (reply) {
        free(reply);
        return 1;
    }

    return 0;
}

static void free_shm_buffer(void *opaque, uint8_t *data)
{
    shmdt(data);
}

static AVBufferRef *allocate_shm_buffer(void *opaque, buffer_size_t size)
{
    xcb_connection_t *conn = opaque;
    xcb_shm_seg_t segment;
    AVBufferRef *ref;
    uint8_t *data;
    int id;

    id = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (id == -1)
        return NULL;

    segment = xcb_generate_id(conn);
    xcb_shm_attach(conn, segment, id, 0);
    data = shmat(id, NULL, 0);
    shmctl(id, IPC_RMID, 0);
    if ((intptr_t)data == -1 || !data)
        return NULL;

    ref = av_buffer_create(data, size, free_shm_buffer, (void *)(ptrdiff_t)segment, 0);
    if (!ref)
        shmdt(data);

    return ref;
}

static int xcbgrab_frame_shm(AVFormatContext *s, AVPacket *pkt)
{
    XCBGrabContext *c = s->priv_data;
    xcb_shm_get_image_cookie_t iq;
    xcb_shm_get_image_reply_t *img;
    xcb_drawable_t drawable = c->window_id;
    xcb_generic_error_t *e = NULL;
    AVBufferRef *buf;
    xcb_shm_seg_t segment;

    buf = av_buffer_pool_get(c->shm_pool);
    if (!buf) {
        av_log(s, AV_LOG_ERROR, "Could not get shared memory buffer.\n");
        return AVERROR(ENOMEM);
    }
    segment = (xcb_shm_seg_t)av_buffer_pool_buffer_get_opaque(buf);

    iq = xcb_shm_get_image(c->conn, drawable,
                           c->x, c->y, c->width, c->height, ~0,
                           XCB_IMAGE_FORMAT_Z_PIXMAP, segment, 0);
    img = xcb_shm_get_image_reply(c->conn, iq, &e);

    xcb_flush(c->conn);

    if (e) {
        av_log(s, AV_LOG_ERROR,
               "Cannot get the image data "
               "event_error: response_type:%u error_code:%u "
               "sequence:%u resource_id:%u minor_code:%u major_code:%u.\n",
               e->response_type, e->error_code,
               e->sequence, e->resource_id, e->minor_code, e->major_code);

        free(e);
        av_buffer_unref(&buf);
        return AVERROR(EACCES);
    }

    free(img);

    pkt->buf = buf;
    pkt->data = buf->data;
    pkt->size = c->frame_size;

    return 0;
}
#endif /* CONFIG_LIBXCB_SHM */

#if CONFIG_LIBXCB_XFIXES
static int check_xfixes(xcb_connection_t *conn)
{
    xcb_xfixes_query_version_cookie_t cookie;
    xcb_xfixes_query_version_reply_t *reply;

    cookie = xcb_xfixes_query_version(conn, XCB_XFIXES_MAJOR_VERSION,
                                      XCB_XFIXES_MINOR_VERSION);
    reply  = xcb_xfixes_query_version_reply(conn, cookie, NULL);

    if (reply) {
        free(reply);
        return 1;
    }
    return 0;
}

#define BLEND(target, source, alpha) \
    (target) + ((source) * (255 - (alpha)) + 255 / 2) / 255

static void xcbgrab_draw_mouse(AVFormatContext *s, AVPacket *pkt,
                               xcb_query_pointer_reply_t *p,
                               xcb_get_geometry_reply_t *geo,
                               int win_x, int win_y)
{
    XCBGrabContext *gr = s->priv_data;
    uint32_t *cursor;
    uint8_t *image = pkt->data;
    int stride     = gr->bpp / 8;
    xcb_xfixes_get_cursor_image_cookie_t cc;
    xcb_xfixes_get_cursor_image_reply_t *ci;
    int cx, cy, x, y, w, h, c_off, i_off;

    cc = xcb_xfixes_get_cursor_image(gr->conn);
    ci = xcb_xfixes_get_cursor_image_reply(gr->conn, cc, NULL);
    if (!ci)
        return;

    cursor = xcb_xfixes_get_cursor_image_cursor_image(ci);
    if (!cursor)
        return;

    cx = ci->x - ci->xhot;
    cy = ci->y - ci->yhot;

    x = FFMAX(cx, win_x + gr->x);
    y = FFMAX(cy, win_y + gr->y);

    w = FFMIN(cx + ci->width,  win_x + gr->x + gr->width)  - x;
    h = FFMIN(cy + ci->height, win_y + gr->y + gr->height) - y;

    c_off = x - cx;
    i_off = x - gr->x - win_x;

    cursor += (y - cy) * ci->width;
    image  += (y - gr->y - win_y) * gr->width * stride;

    for (y = 0; y < h; y++) {
        cursor += c_off;
        image  += i_off * stride;
        for (x = 0; x < w; x++, cursor++, image += stride) {
            int r, g, b, a;

            r =  *cursor        & 0xff;
            g = (*cursor >>  8) & 0xff;
            b = (*cursor >> 16) & 0xff;
            a = (*cursor >> 24) & 0xff;

            if (!a)
                continue;

            if (a == 255) {
                image[0] = r;
                image[1] = g;
                image[2] = b;
            } else {
                image[0] = BLEND(r, image[0], a);
                image[1] = BLEND(g, image[1], a);
                image[2] = BLEND(b, image[2], a);
            }

        }
        cursor +=  ci->width - w - c_off;
        image  += (gr->width - w - i_off) * stride;
    }

    free(ci);
}
#endif /* CONFIG_LIBXCB_XFIXES */

static void xcbgrab_update_region(AVFormatContext *s, int win_x, int win_y)
{
    XCBGrabContext *c     = s->priv_data;
    const uint32_t args[] = { win_x + c->x - c->region_border,
                              win_y + c->y - c->region_border };

    xcb_configure_window(c->conn,
                         c->window,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                         args);
}

static int xcbgrab_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    XCBGrabContext *c = s->priv_data;
    xcb_query_pointer_cookie_t pc;
    xcb_get_geometry_cookie_t gc;
    xcb_translate_coordinates_cookie_t tc;
    xcb_query_pointer_reply_t *p  = NULL;
    xcb_get_geometry_reply_t *geo = NULL;
    xcb_translate_coordinates_reply_t *translate = NULL;
    int ret = 0;
    int64_t pts;
    int win_x = 0, win_y = 0;

    wait_frame(s, pkt);
    pts = av_gettime();

    if (c->follow_mouse || c->draw_mouse) {
        pc  = xcb_query_pointer(c->conn, c->window_id);
        gc  = xcb_get_geometry(c->conn, c->window_id);
        p   = xcb_query_pointer_reply(c->conn, pc, NULL);
        if (!p) {
            av_log(s, AV_LOG_ERROR, "Failed to query xcb pointer\n");
            return AVERROR_EXTERNAL;
        }
        geo = xcb_get_geometry_reply(c->conn, gc, NULL);
        if (!geo) {
            av_log(s, AV_LOG_ERROR, "Failed to get xcb geometry\n");
            free(p);
            return AVERROR_EXTERNAL;
        }
    }
    if (c->window_id != c->screen->root) {
        tc = xcb_translate_coordinates(c->conn, c->window_id, c->screen->root, 0, 0);
        translate = xcb_translate_coordinates_reply(c->conn, tc, NULL);
        if (!translate) {
            free(p);
            free(geo);
            av_log(s, AV_LOG_ERROR, "Failed to translate xcb geometry\n");
            return AVERROR_EXTERNAL;
        }
        win_x = translate->dst_x;
        win_y = translate->dst_y;
        free(translate);
    }

    if (c->follow_mouse && p->same_screen)
        xcbgrab_reposition(s, p, geo);

    if (c->show_region)
        xcbgrab_update_region(s, win_x, win_y);

#if CONFIG_LIBXCB_SHM
    if (c->has_shm && xcbgrab_frame_shm(s, pkt) < 0) {
        av_log(s, AV_LOG_WARNING, "Continuing without shared memory.\n");
        c->has_shm = 0;
    }
#endif
    if (!c->has_shm)
        ret = xcbgrab_frame(s, pkt);
    pkt->dts = pkt->pts = pts;
    pkt->duration = c->frame_duration;

#if CONFIG_LIBXCB_XFIXES
    if (ret >= 0 && c->draw_mouse && p->same_screen)
        xcbgrab_draw_mouse(s, pkt, p, geo, win_x, win_y);
#endif

    free(p);
    free(geo);

    return ret;
}

static av_cold int xcbgrab_read_close(AVFormatContext *s)
{
    XCBGrabContext *ctx = s->priv_data;

#if CONFIG_LIBXCB_SHM
    av_buffer_pool_uninit(&ctx->shm_pool);
#endif

    xcb_disconnect(ctx->conn);

    return 0;
}

static xcb_screen_t *get_screen(const xcb_setup_t *setup, int screen_num)
{
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen     = NULL;

    for (; it.rem > 0; xcb_screen_next (&it)) {
        if (!screen_num) {
            screen = it.data;
            break;
        }

        screen_num--;
    }

    return screen;
}

static int pixfmt_from_pixmap_format(AVFormatContext *s, int depth,
                                     int *pix_fmt, int *bpp)
{
    XCBGrabContext *c        = s->priv_data;
    const xcb_setup_t *setup = xcb_get_setup(c->conn);
    const xcb_format_t *fmt  = xcb_setup_pixmap_formats(setup);
    int length               = xcb_setup_pixmap_formats_length(setup);

    *pix_fmt = 0;

    while (length--) {
        if (fmt->depth == depth) {
            switch (depth) {
            case 32:
                if (fmt->bits_per_pixel == 32)
                    *pix_fmt = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ?
                               AV_PIX_FMT_BGR0 : AV_PIX_FMT_0RGB;
                break;
            case 24:
                if (fmt->bits_per_pixel == 32)
                    *pix_fmt = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ?
                               AV_PIX_FMT_BGR0 : AV_PIX_FMT_0RGB;
                else if (fmt->bits_per_pixel == 24)
                    *pix_fmt = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ?
                               AV_PIX_FMT_BGR24 : AV_PIX_FMT_RGB24;
                break;
            case 16:
                if (fmt->bits_per_pixel == 16)
                    *pix_fmt = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ?
                               AV_PIX_FMT_RGB565LE : AV_PIX_FMT_RGB565BE;
                break;
            case 15:
                if (fmt->bits_per_pixel == 16)
                    *pix_fmt = setup->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST ?
                               AV_PIX_FMT_RGB555LE : AV_PIX_FMT_RGB555BE;
                break;
            case 8:
                if (fmt->bits_per_pixel == 8)
                    *pix_fmt = AV_PIX_FMT_RGB8;
                break;
            }
        }

        if (*pix_fmt) {
            *bpp        = fmt->bits_per_pixel;
            return 0;
        }

        fmt++;
    }
    avpriv_report_missing_feature(s, "Mapping this pixmap format");

    return AVERROR_PATCHWELCOME;
}

static int create_stream(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    AVStream *st      = avformat_new_stream(s, NULL);
    xcb_get_geometry_cookie_t gc;
    xcb_get_geometry_reply_t *geo;
    int64_t frame_size_bits;
    int ret;

    if (!st)
        return AVERROR(ENOMEM);

    ret = av_parse_video_rate(&st->avg_frame_rate, c->framerate);
    if (ret < 0)
        return ret;

    avpriv_set_pts_info(st, 64, 1, 1000000);

    gc  = xcb_get_geometry(c->conn, c->window_id);
    geo = xcb_get_geometry_reply(c->conn, gc, NULL);
    if (!geo) {
        av_log(s, AV_LOG_ERROR, "Can't find window '0x%x', aborting.\n", c->window_id);
        return AVERROR_EXTERNAL;
    }

    if (!c->width || !c->height) {
        c->width = geo->width;
        c->height = geo->height;
    }

    if (c->x + c->width > geo->width ||
        c->y + c->height > geo->height) {
        av_log(s, AV_LOG_ERROR,
               "Capture area %dx%d at position %d.%d "
               "outside the screen size %dx%d\n",
               c->width, c->height,
               c->x, c->y,
               geo->width, geo->height);
        free(geo);
        return AVERROR(EINVAL);
    }

    c->time_base  = (AVRational){ st->avg_frame_rate.den,
                                  st->avg_frame_rate.num };
    c->frame_duration = av_rescale_q(1, c->time_base, AV_TIME_BASE_Q);
    c->time_frame = av_gettime_relative();

    ret = pixfmt_from_pixmap_format(s, geo->depth, &st->codecpar->format, &c->bpp);
    free(geo);
    if (ret < 0)
        return ret;

    frame_size_bits = (int64_t)c->width * c->height * c->bpp;
    if (frame_size_bits / 8 + AV_INPUT_BUFFER_PADDING_SIZE > INT_MAX) {
        av_log(s, AV_LOG_ERROR, "Captured area is too large\n");
        return AVERROR_PATCHWELCOME;
    }
    c->frame_size = frame_size_bits / 8;

#if CONFIG_LIBXCB_SHM
    c->shm_pool = av_buffer_pool_init2(c->frame_size + AV_INPUT_BUFFER_PADDING_SIZE,
                                           c->conn, allocate_shm_buffer, NULL);
    if (!c->shm_pool)
        return AVERROR(ENOMEM);
#endif

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->width      = c->width;
    st->codecpar->height     = c->height;
    st->codecpar->bit_rate   = av_rescale(frame_size_bits, st->avg_frame_rate.num, st->avg_frame_rate.den);

    return ret;
}

static void draw_rectangle(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    xcb_gcontext_t gc = xcb_generate_id(c->conn);
    uint32_t mask     = XCB_GC_FOREGROUND |
                        XCB_GC_BACKGROUND |
                        XCB_GC_LINE_WIDTH |
                        XCB_GC_LINE_STYLE |
                        XCB_GC_FILL_STYLE;
    uint32_t values[] = { c->screen->black_pixel,
                          c->screen->white_pixel,
                          c->region_border,
                          XCB_LINE_STYLE_DOUBLE_DASH,
                          XCB_FILL_STYLE_SOLID };
    xcb_rectangle_t r = { 1, 1,
                          c->width  + c->region_border * 2 - 3,
                          c->height + c->region_border * 2 - 3 };

    xcb_create_gc(c->conn, gc, c->window, mask, values);

    xcb_poly_rectangle(c->conn, c->window, gc, 1, &r);
}

static void setup_window(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    uint32_t mask     = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    uint32_t values[] = { 1,
                          XCB_EVENT_MASK_EXPOSURE |
                          XCB_EVENT_MASK_STRUCTURE_NOTIFY };
    av_unused xcb_rectangle_t rect = { 0, 0, c->width, c->height };

    c->window = xcb_generate_id(c->conn);

    xcb_create_window(c->conn, XCB_COPY_FROM_PARENT,
                      c->window,
                      c->screen->root,
                      c->x - c->region_border,
                      c->y - c->region_border,
                      c->width + c->region_border * 2,
                      c->height + c->region_border * 2,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      mask, values);

#if CONFIG_LIBXCB_SHAPE
    xcb_shape_rectangles(c->conn, XCB_SHAPE_SO_SUBTRACT,
                         XCB_SHAPE_SK_BOUNDING, XCB_CLIP_ORDERING_UNSORTED,
                         c->window,
                         c->region_border, c->region_border,
                         1, &rect);
#endif

    xcb_map_window(c->conn, c->window);

    draw_rectangle(s);
}

#define CROSSHAIR_CURSOR 34

static xcb_rectangle_t rectangle_from_corners(xcb_point_t *corner_a,
                                              xcb_point_t *corner_b)
{
    xcb_rectangle_t rectangle;
    rectangle.x = FFMIN(corner_a->x, corner_b->x);
    rectangle.y = FFMIN(corner_a->y, corner_b->y);
    rectangle.width = FFABS(corner_a->x - corner_b->x);
    rectangle.height = FFABS(corner_a->y - corner_b->y);
    return rectangle;
}

static int select_region(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    xcb_connection_t *conn = c->conn;
    xcb_screen_t *screen = c->screen;

    int ret = 0, done = 0, was_pressed = 0;
    xcb_cursor_t cursor;
    xcb_font_t cursor_font;
    xcb_point_t press_position;
    xcb_generic_event_t *event;
    xcb_rectangle_t rectangle = { 0 };
    xcb_grab_pointer_reply_t *reply;
    xcb_grab_pointer_cookie_t cookie;

    xcb_window_t root_window = screen->root;
    xcb_gcontext_t gc = xcb_generate_id(conn);
    uint32_t mask = XCB_GC_FUNCTION | XCB_GC_SUBWINDOW_MODE;
    uint32_t values[] = { XCB_GX_INVERT, XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS };
    xcb_create_gc(conn, gc, root_window, mask, values);

    cursor_font = xcb_generate_id(conn);
    xcb_open_font(conn, cursor_font, strlen("cursor"), "cursor");
    cursor = xcb_generate_id(conn);
    xcb_create_glyph_cursor(conn, cursor, cursor_font, cursor_font,
                            CROSSHAIR_CURSOR, CROSSHAIR_CURSOR + 1, 0, 0, 0,
                            0xFFFF, 0xFFFF, 0xFFFF);
    cookie = xcb_grab_pointer(conn, 0, root_window,
                              XCB_EVENT_MASK_BUTTON_PRESS |
                              XCB_EVENT_MASK_BUTTON_RELEASE |
                              XCB_EVENT_MASK_BUTTON_MOTION,
                              XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                              root_window, cursor, XCB_CURRENT_TIME);
    reply = xcb_grab_pointer_reply(conn, cookie, NULL);
    if (!reply || reply->status != XCB_GRAB_STATUS_SUCCESS) {
        av_log(s, AV_LOG_ERROR,
               "Failed to select region. Could not grab pointer.\n");
        ret = AVERROR(EIO);
        free(reply);
        goto fail;
    }
    free(reply);

    xcb_grab_server(conn);

    while (!done && (event = xcb_wait_for_event(conn))) {
        switch (event->response_type & ~0x80) {
        case XCB_BUTTON_PRESS: {
            xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
            press_position = (xcb_point_t){ press->event_x, press->event_y };
            rectangle.x = press_position.x;
            rectangle.y = press_position.y;
            xcb_poly_rectangle(conn, root_window, gc, 1, &rectangle);
            was_pressed = 1;
            break;
        }
        case XCB_MOTION_NOTIFY: {
            if (was_pressed) {
                xcb_motion_notify_event_t *motion =
                    (xcb_motion_notify_event_t *)event;
                xcb_point_t cursor_position = { motion->event_x, motion->event_y };
                xcb_poly_rectangle(conn, root_window, gc, 1, &rectangle);
                rectangle = rectangle_from_corners(&press_position, &cursor_position);
                xcb_poly_rectangle(conn, root_window, gc, 1, &rectangle);
            }
            break;
        }
        case XCB_BUTTON_RELEASE: {
            xcb_poly_rectangle(conn, root_window, gc, 1, &rectangle);
            done = 1;
            break;
        }
        default:
            break;
        }
        xcb_flush(conn);
        free(event);
    }
    c->width  = rectangle.width;
    c->height = rectangle.height;
    if (c->width && c->height) {
        c->x = rectangle.x;
        c->y = rectangle.y;
    } else {
        c->x = 0;
        c->y = 0;
    }
    xcb_ungrab_server(conn);
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
    xcb_flush(conn);

fail:
    xcb_free_cursor(conn, cursor);
    xcb_close_font(conn, cursor_font);
    xcb_free_gc(conn, gc);
    return ret;
}

static av_cold int xcbgrab_read_header(AVFormatContext *s)
{
    XCBGrabContext *c = s->priv_data;
    int screen_num, ret;
    const xcb_setup_t *setup;
    char *display_name = av_strdup(s->url);

    if (!display_name)
        return AVERROR(ENOMEM);

    if (!sscanf(s->url, "%[^+]+%d,%d", display_name, &c->x, &c->y)) {
        *display_name = 0;
        sscanf(s->url, "+%d,%d", &c->x, &c->y);
    }

    c->conn = xcb_connect(display_name[0] ? display_name : NULL, &screen_num);
    av_freep(&display_name);

    if ((ret = xcb_connection_has_error(c->conn))) {
        av_log(s, AV_LOG_ERROR, "Cannot open display %s, error %d.\n",
               s->url[0] ? s->url : "default", ret);
        return AVERROR(EIO);
    }

    setup = xcb_get_setup(c->conn);

    c->screen = get_screen(setup, screen_num);
    if (!c->screen) {
        av_log(s, AV_LOG_ERROR, "The screen %d does not exist.\n",
               screen_num);
        xcbgrab_read_close(s);
        return AVERROR(EIO);
    }

    if (c->window_id == XCB_NONE)
        c->window_id = c->screen->root;
    else {
        if (c->select_region) {
            av_log(s, AV_LOG_WARNING, "select_region ignored with window_id.\n");
            c->select_region = 0;
        }
        if (c->follow_mouse) {
            av_log(s, AV_LOG_WARNING, "follow_mouse ignored with window_id.\n");
            c->follow_mouse = 0;
        }
    }

    if (c->select_region) {
        ret = select_region(s);
        if (ret < 0) {
            xcbgrab_read_close(s);
            return ret;
        }
    }

    ret = create_stream(s);

    if (ret < 0) {
        xcbgrab_read_close(s);
        return ret;
    }

#if CONFIG_LIBXCB_SHM
    c->has_shm = check_shm(c->conn);
#endif

#if CONFIG_LIBXCB_XFIXES
    if (c->draw_mouse) {
        if (!(c->draw_mouse = check_xfixes(c->conn))) {
            av_log(s, AV_LOG_WARNING,
                   "XFixes not available, cannot draw the mouse.\n");
        }
        if (c->bpp < 24) {
            avpriv_report_missing_feature(s, "%d bits per pixel screen",
                                          c->bpp);
            c->draw_mouse = 0;
        }
    }
#endif

    if (c->show_region)
        setup_window(s);

    return 0;
}

AVInputFormat ff_xcbgrab_demuxer = {
    .name           = "x11grab",
    .long_name      = NULL_IF_CONFIG_SMALL("X11 screen capture, using XCB"),
    .priv_data_size = sizeof(XCBGrabContext),
    .read_header    = xcbgrab_read_header,
    .read_packet    = xcbgrab_read_packet,
    .read_close     = xcbgrab_read_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &xcbgrab_class,
};
