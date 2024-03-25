/*
 * ARIB STD-B24 caption decoder using the libaribcaption library
 * Copyright (c) 2022 TADANO Tokumei
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

#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"
#include "libavcodec/ass.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

#include <aribcaption/aribcaption.h>

#if !defined(DEFAULT_FONT_ASS)
# define DEFAULT_FONT_ASS "sans-serif"
#endif

#define ARIBC_BPRINT_SIZE_INIT         64
#define ARIBC_BPRINT_SIZE_MAX          (8 * 1024)
#define ARIBC_ALPHA_MAX_NUM            4
#define ARIBC_ALPHA_DEFAULT_FRONT      0xFF
#define ARIBC_ALPHA_DEFAULT_BACK       0x80

#define ARIBCC_COLOR_RGB(c)            ((c) & 0xFFFFFF)
#define ARIBCC_COLOR_DIFF_RGB(c1,c2)   (((c1) ^ (c2)) & 0x00FFFFFF)
#define ARIBCC_COLOR_DIFF_A(c1,c2)     (((c1) ^ (c2)) & 0xFF000000)

#define CLUT_RGBA(r,g,b,a) (((unsigned)(a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define CLUT_A(c)          (((c) >> 24) & 0xFF)
#define CLUT_R(c)          (((c) >> 16) & 0xFF)
#define CLUT_G(c)          (((c) >>  8) & 0xFF)
#define CLUT_B(c)          ( (c)        & 0xFF)

#define ARIBCC_COLOR_TO_CLUT_RGBA(c,a) (((ARIBCC_COLOR_A(c) ? ARIBCC_COLOR_A(c) : (a)) << 24) | \
                                        (ARIBCC_COLOR_R(c) << 16) | \
                                        (ARIBCC_COLOR_G(c) <<  8) | \
                                        (ARIBCC_COLOR_B(c)))

typedef struct ARIBCaptionContext {
    AVClass *class;
    AVCodecContext *avctx;
    const AVPacket *avpkt;
    AVSubtitle *sub;

    aribcc_context_t *context;
    aribcc_decoder_t *decoder;
    aribcc_renderer_t *renderer;

    int subtitle_type;
    int encoding_scheme;
    int ass_single_rect;
    char *font;
    int force_stroke_text;
    int ignore_background;
    int ignore_ruby;
    float stroke_width;
    int replace_drcs;
    int replace_msz_ascii;
    int replace_msz_japanese;
    int replace_msz_glyph;

    int64_t pts;
    AVRational time_base;
    int canvas_width;
    int canvas_height;
    int plane_width;
    int plane_height;
    int frame_width;
    int frame_height;
    int bitmap_plane_width;
    int bitmap_plane_height;
    int font_size;
    int charstyle;
    int border_style;
    int readorder;

    aribcc_caption_t caption;
    aribcc_render_result_t render_result;
    uint32_t *clut;
    int clut_idx;
    int clut_overflow;
    uint8_t clut_alpha[ARIBC_ALPHA_MAX_NUM];
} ARIBCaptionContext;

static void hex_dump_debug(void *ctx, const char *buf, int buf_size)
{
    int i;

    for (i = 0; i < buf_size; i++) {
        ff_dlog(ctx, "%02hhx ", buf[i]);
        if (i % 16 == 15)
            ff_dlog(ctx, "\n");
    }
    if (i % 16)
        ff_dlog(ctx, "\n");
}

static void logcat_callback(aribcc_loglevel_t level, const char* message, void* userdata)
{
    ARIBCaptionContext *ctx = userdata;
    int lvl;

    if (ctx->decoder != NULL) {
        switch (level) {
        case ARIBCC_LOGLEVEL_ERROR:
            lvl = AV_LOG_ERROR;
            break;
        case ARIBCC_LOGLEVEL_WARNING:
            lvl = AV_LOG_WARNING;
            break;
        default:
            lvl = AV_LOG_INFO;
        }

        av_log(ctx, lvl, "%s\n", message);
    }
}

static void estimate_video_frame_size(ARIBCaptionContext *ctx)
{
    if (ctx->avctx->width > 0 && ctx->avctx->height > 0) {
        /* input video size specified by -canvas_size option */
        ctx->bitmap_plane_width = ctx->avctx->width;
        ctx->bitmap_plane_height = ctx->avctx->height;
    } else if (ctx->plane_width == 960) {
        /* ARIB TR-B14 Fascicle 2 Volume 3 [Section 2] 4.3.1 */
        /* ARIB TR-B14 Fascicle 2 Volume 3 [Section 2] Appendix-4 */
        ctx->bitmap_plane_width = 1440;
        ctx->bitmap_plane_height = 1080;
    } else {
        ctx->bitmap_plane_width = ctx->plane_width;
        ctx->bitmap_plane_height = ctx->plane_height;
    }
    /* Expand either width or height */
    if (ctx->bitmap_plane_height * ctx->plane_width > ctx->bitmap_plane_width * ctx->plane_height) {
        ctx->frame_height = ctx->bitmap_plane_height;
        ctx->frame_width = ctx->frame_height * ctx->plane_width / ctx->plane_height;
    } else {
        ctx->frame_width = ctx->bitmap_plane_width;
        ctx->frame_height = ctx->frame_width * ctx->plane_height / ctx->plane_width;
    }
}

static void clut_set_alpha(ARIBCaptionContext *ctx, uint8_t a)
{
    int i;

    for (i = 0; i < ARIBC_ALPHA_MAX_NUM; i++) {
        if (ctx->clut_alpha[i] == 0) {
            ctx->clut_alpha[i] = a;
            return;
        }
        if (ctx->clut_alpha[i] == a)
            return;
    }
    return;
}

static uint8_t clut_find_nearlest_alpha(ARIBCaptionContext *ctx, uint8_t a)
{
    int i, j, d;

    if (a == 0)
        return a;
    d = 256;
    j = 0;
    for (i = 0; i < ARIBC_ALPHA_MAX_NUM; i++) {
        if (ctx->clut_alpha[i] == a)
            return a;
        if (ctx->clut_alpha[i] == 0)
            break;
        if (abs((int)a - (int)ctx->clut_alpha[i]) < d) {
            d = abs((int)a - (int)ctx->clut_alpha[i]);
            j = i;
        }
    }
    return ctx->clut_alpha[j];
}

static int clut_find(ARIBCaptionContext *ctx, uint32_t rgba)
{
    int i;

    for (i = 0; i < ctx->clut_idx; i++) {
        if (ctx->clut[i] == rgba)
            return i;
    }
    return -1;
}

static inline int clut_color_distance(uint32_t rgba1, uint32_t rgba2)
{
    return abs((int)CLUT_R(rgba1) - (int)CLUT_R(rgba2)) +
           abs((int)CLUT_G(rgba1) - (int)CLUT_G(rgba2)) +
           abs((int)CLUT_B(rgba1) - (int)CLUT_B(rgba2));
}

static uint8_t clut_pick_or_set(ARIBCaptionContext *ctx, int r, int g, int b, int a)
{
    int c, i, d, d_min;
    uint32_t rgba;

    a = clut_find_nearlest_alpha(ctx, a);
    if (a == 0)
        return 0; /* transparent */
    rgba = CLUT_RGBA(r,g,b,a);

    d_min = 256 * 3;
    c = 0;
    for (i = 0; i < ctx->clut_idx; i++) {
        if (ctx->clut[i] == rgba)
            return i;
        if (CLUT_A(ctx->clut[i]) != a)
            continue;
        d = clut_color_distance(ctx->clut[i], rgba);
        if (d < d_min) {
            d_min = d;
            c = i;
        }
    }
    if (d_min > 3) {
        if (ctx->clut_idx >= AVPALETTE_COUNT)
            ctx->clut_overflow++;
        else {
            c = ctx->clut_idx;
            ctx->clut[ctx->clut_idx++] = rgba;
        }
    }
    return c;
}

/* initialiaze CLUT with each character colors */
static void clut_init(ARIBCaptionContext *ctx, aribcc_caption_region_t *region)
{
    aribcc_color_t text_color, back_color, stroke_color;
    uint32_t rgba;

    ctx->clut[0] = CLUT_RGBA(0,0,0,0); /* transparent */
    ctx->clut_alpha[0] = 0xFF;
    ctx->clut_idx = 1;
    ctx->clut_overflow = 0;
    text_color = region->chars[0].text_color;
    back_color = region->chars[0].back_color;
    stroke_color = region->chars[0].stroke_color;
    rgba = ARIBCC_COLOR_TO_CLUT_RGBA(text_color, ARIBC_ALPHA_DEFAULT_FRONT);
    ctx->clut[ctx->clut_idx++] = rgba;
    clut_set_alpha(ctx, CLUT_A(rgba));
    rgba = ARIBCC_COLOR_TO_CLUT_RGBA(back_color, ARIBC_ALPHA_DEFAULT_BACK);
    ctx->clut[ctx->clut_idx++] = rgba;
    clut_set_alpha(ctx, CLUT_A(rgba));
    rgba = ARIBCC_COLOR_TO_CLUT_RGBA(stroke_color, ARIBC_ALPHA_DEFAULT_FRONT);
    if (clut_find(ctx, rgba) < 0) {
        ctx->clut[ctx->clut_idx++] = rgba;
        clut_set_alpha(ctx, CLUT_A(rgba));
    }

    for (int i = 1; i < region->char_count; i++) {
        if (region->chars[i].text_color != text_color) {
            rgba = ARIBCC_COLOR_TO_CLUT_RGBA(region->chars[i].text_color,
                                             ARIBC_ALPHA_DEFAULT_FRONT);
            if (clut_find(ctx, rgba) < 0) {
                ctx->clut[ctx->clut_idx++] = rgba;
                clut_set_alpha(ctx, CLUT_A(rgba));
            }
        }
        if (region->chars[i].back_color != back_color) {
            rgba = ARIBCC_COLOR_TO_CLUT_RGBA(region->chars[i].back_color,
                                             ARIBC_ALPHA_DEFAULT_BACK);
            if (clut_find(ctx, rgba) < 0) {
                ctx->clut[ctx->clut_idx++] = rgba;
                clut_set_alpha(ctx, CLUT_A(rgba));
            }
        }
        if (region->chars[i].stroke_color != stroke_color) {
            rgba = ARIBCC_COLOR_TO_CLUT_RGBA(region->chars[i].stroke_color,
                                             ARIBC_ALPHA_DEFAULT_FRONT);
            if (clut_find(ctx, rgba) < 0) {
                if (ctx->clut_idx < AVPALETTE_COUNT)
                    ctx->clut[ctx->clut_idx++] = rgba;
                clut_set_alpha(ctx, CLUT_A(rgba));
            }
        }
    }
}

/**
 * aribcaption_trans_{bitmap|ass|text}_subtitle()
 *
 * Transfer decoded subtitle to AVSubtitle with corresponding subtitle type.
 *
 * @param ctx pointer to the ARIBCaptionContext
 * @return > 0 number of rectangles to be displayed
 *         = 0 no subtitle
 *         < 0 error code
 */
static int aribcaption_trans_bitmap_subtitle(ARIBCaptionContext *ctx)
{
    int ret = 0;
    AVSubtitle *sub = ctx->sub;
    int status, rect_idx;
    int old_width = ctx->frame_width;
    int old_height = ctx->frame_height;

    if (ctx->caption.plane_width > 0 && ctx->caption.plane_height > 0) {
        ctx->plane_width = ctx->caption.plane_width;
        ctx->plane_height = ctx->caption.plane_height;
    }
    estimate_video_frame_size(ctx);
    if (ctx->frame_width != old_width || ctx->frame_height != old_height) {
        ff_dlog(ctx, "canvas: %dx%d  plane: %dx%d  bitmap: %dx%d  frame: %dx%d\n",
                ctx->avctx->width, ctx->avctx->height,
                ctx->plane_width, ctx->plane_height,
                ctx->bitmap_plane_width, ctx->bitmap_plane_height,
                ctx->frame_width, ctx->frame_height);
        if (!aribcc_renderer_set_frame_size(ctx->renderer,
                                 ctx->frame_width, ctx->frame_height)) {
            av_log(ctx, AV_LOG_ERROR,
                   "aribcc_renderer_set_frame_size() returned with error.\n");
            return AVERROR_EXTERNAL;
        }
    }

    status = aribcc_renderer_append_caption(ctx->renderer, &ctx->caption);
    if (!status) {
        av_log(ctx, AV_LOG_ERROR,
               "aribcc_renderer_append_caption() returned with error.\n");
        return AVERROR_EXTERNAL;
    }

    status = aribcc_renderer_render(ctx->renderer, ctx->pts, &ctx->render_result);
    switch (status) {
    case ARIBCC_RENDER_STATUS_GOT_IMAGE:
        break;

    case ARIBCC_RENDER_STATUS_GOT_IMAGE_UNCHANGED:
        aribcc_render_result_cleanup(&ctx->render_result);
        ff_dlog(ctx, "got image unchanged\n");
        return 0;

    case ARIBCC_RENDER_STATUS_NO_IMAGE:
        ff_dlog(ctx, "no image\n");
        return 0;

    case ARIBCC_RENDER_STATUS_ERROR:
        av_log(ctx, AV_LOG_ERROR,
               "aribcc_renderer_render() returned with error.\n");
        return AVERROR_EXTERNAL;

    default:
        aribcc_render_result_cleanup(&ctx->render_result);
        av_log(ctx, AV_LOG_ERROR,
               "aribcc_renderer_render() returned unknown status: %d\n", status);
        return AVERROR_EXTERNAL;
    }

    if (!ctx->render_result.image_count || ctx->render_result.images == NULL) {
        aribcc_render_result_cleanup(&ctx->render_result);
        ff_dlog(ctx, "no image (%d)\n", ctx->render_result.image_count);
        return 0;
    }

    sub->format = 0; /* graphic */
    sub->rects = av_calloc(ctx->render_result.image_count, sizeof(*sub->rects));
    if (!sub->rects) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    for (int i = 0; i < ctx->render_result.image_count; i++) {
        sub->rects[i] = av_mallocz(sizeof(*sub->rects[i]));
        if (!sub->rects[i]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    for (rect_idx = 0; rect_idx < ctx->caption.region_count; rect_idx++) {
        AVSubtitleRect *rect = sub->rects[rect_idx];
        aribcc_image_t *image = &ctx->render_result.images[rect_idx];
        int w, h, shrink_height, dst_idx;

        clut_init(ctx, &ctx->caption.regions[rect_idx]);

        rect->w = image->width * ctx->bitmap_plane_width / ctx->frame_width;
        rect->h = image->height * ctx->bitmap_plane_height / ctx->frame_height;
        rect->data[0] = av_mallocz(rect->w * rect->h);
        if (!rect->data[0]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if ((image->height != rect->h && image->width != rect->w) ||
            image->stride < image->width * 4 ||
            image->stride * image->height > image->bitmap_size) {
            av_log(ctx, AV_LOG_ERROR, "Bug: unexpected rendered image: %d(%d)x%d -> %dx%d\n",
                   image->width, image->stride / 4, image->height, rect->w, rect->h);
            ret = AVERROR_EXTERNAL;
            goto fail;
        }

        shrink_height = image->height != rect->h;
        dst_idx = 0;
        for (h = 0; h < rect->h; h++) {
            for (w = 0; w < rect->w; w++) {
                /* Bi-linear interpolation */
                int n, m, idx0, idx1, r, g, b, a;
                if (shrink_height) {
                    int div_a, y0, y1;
                    div_a = h * ctx->frame_height;
                    n = ctx->bitmap_plane_height;
                    y0 = div_a / n;
                    y1 = FFMIN(y0 + 1, image->height - 1);
                    m = div_a - n * y0;
                    idx0 = image->stride * y0 + w * 4;
                    idx1 = image->stride * y1 + w * 4;
                } else {
                    int div_a, x0, x1;
                    div_a = w * ctx->frame_width;
                    n = ctx->bitmap_plane_width;
                    x0 = div_a / n;
                    x1 = FFMIN(x0 + 1, image->width - 1);
                    m = div_a - n * x0;
                    idx0 = image->stride * h + x0 * 4;
                    idx1 = image->stride * h + x1 * 4;
                }
                r = (image->bitmap[idx0++] * (n - m) + image->bitmap[idx1++] * m) / n;
                g = (image->bitmap[idx0++] * (n - m) + image->bitmap[idx1++] * m) / n;
                b = (image->bitmap[idx0++] * (n - m) + image->bitmap[idx1++] * m) / n;
                a = (image->bitmap[idx0++] * (n - m) + image->bitmap[idx1++] * m) / n;
                rect->data[0][dst_idx++] = clut_pick_or_set(ctx, r, g, b, a);
            }
        }
        rect->data[1] = av_memdup(ctx->clut, AVPALETTE_SIZE);
        if (!rect->data[1]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (ctx->avctx->profile == AV_PROFILE_ARIB_PROFILE_C) {
            /* ARIB TR-B14 version 3.8 Fascicle 1-(2/2) Volume 3 [Section 4] */
            /* No position information is provided for profile C */
            rect->x = (ctx->frame_width - rect->w) / 2;
            rect->y = ctx->frame_height - rect->h * (ctx->caption.region_count - rect_idx);
        } else {
            rect->x = image->dst_x * ctx->bitmap_plane_width / ctx->frame_width;
            rect->y = image->dst_y * ctx->bitmap_plane_height / ctx->frame_height;
        }
        rect->type = SUBTITLE_BITMAP;
        rect->linesize[0] = rect->w;
        rect->nb_colors = 256;

        ff_dlog(ctx, "BITMAP subtitle%s (%d,%d) %dx%d -> (%d,%d) %dx%d [%d]: %d colors\n",
                (ctx->caption.regions[rect_idx].is_ruby) ? " (ruby)" : "",
                image->dst_x, image->dst_y, image->width, image->height,
                rect->x, rect->y, rect->w, rect->h,
                rect_idx, ctx->clut_idx);
        if (ctx->clut_overflow)
            av_log(ctx, AV_LOG_WARNING, "CLUT overflow (%d).\n", ctx->clut_overflow);
    }
    sub->num_rects = rect_idx;

    return rect_idx;

fail:
    if (sub->rects) {
        for (int i = 0; i < ctx->caption.region_count; i++) {
            if (sub->rects[i]) {
                av_freep(&sub->rects[i]->data[0]);
                av_freep(&sub->rects[i]->data[1]);
                av_freep(&sub->rects[i]);
            }
        }
        av_freep(&sub->rects);
    }
    sub->num_rects = 0;

    return ret;
}

static int set_ass_header(ARIBCaptionContext *ctx)
{
    AVCodecContext *avctx = ctx->avctx;
    int outline, shadow;
    const char *font_name;
    const char *fonts = ctx->font;

    if (ctx->border_style == 4) {
        outline = 0;
        shadow = 4;
    } else {
        outline = 1;
        shadow = 0;
    }
    if (ctx->force_stroke_text)
        outline = (int)(ctx->stroke_width * 4.0 / 3.0);

    if (fonts && *fonts)
        font_name = av_get_token(&fonts, ",");
    else
        font_name = av_strdup(DEFAULT_FONT_ASS);
    if (!font_name)
        return AVERROR(ENOMEM);

    av_freep(&avctx->subtitle_header);
    avctx->subtitle_header = av_asprintf(
            "[Script Info]\r\n"
            "ScriptType: v4.00+\r\n"
            "PlayResX: %d\r\n"
            "PlayResY: %d\r\n"
            "WrapStyle: 2\r\n"      /* 2: no word wrapping */
            "\r\n"

            "[V4+ Styles]\r\n"
             "Format: Name, "
             "Fontname, Fontsize, "
             "PrimaryColour, SecondaryColour, OutlineColour, BackColour, "
             "Bold, Italic, Underline, StrikeOut, "
             "ScaleX, ScaleY, "
             "Spacing, Angle, "
             "BorderStyle, Outline, Shadow, "
             "Alignment, MarginL, MarginR, MarginV, "
             "Encoding\r\n"

             "Style: "
             "Default,"             /* Name */
             "%s,%d,"               /* Font{name,size} */
             "&H%x,&H%x,&H%x,&H%x," /* {Primary,Secondary,Outline,Back}Colour */
             "%d,%d,%d,0,"          /* Bold, Italic, Underline, StrikeOut */
             "100,100,"             /* Scale{X,Y} */
             "0,0,"                 /* Spacing, Angle */
             "%d,%d,%d,"            /* BorderStyle, Outline, Shadow */
             "%d,10,10,10,"         /* Alignment, Margin[LRV] */
             "0\r\n"                /* Encoding */
             "\r\n"

             "[Events]\r\n"
             "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n",
            ctx->plane_width, ctx->plane_height,
            font_name, ctx->font_size,
            ASS_DEFAULT_COLOR, ASS_DEFAULT_COLOR,
            ASS_DEFAULT_BACK_COLOR, ASS_DEFAULT_BACK_COLOR,
            -ASS_DEFAULT_BOLD, -ASS_DEFAULT_ITALIC, -ASS_DEFAULT_UNDERLINE,
            ctx->border_style, outline, shadow, ASS_DEFAULT_ALIGNMENT);

    av_freep(&font_name);
    if (!avctx->subtitle_header)
        return AVERROR(ENOMEM);
    avctx->subtitle_header_size = strlen(avctx->subtitle_header);
    return 0;
}

static void set_ass_color(AVBPrint *buf, int color_num,
                          aribcc_color_t new_color, aribcc_color_t old_color)
{
    if (ARIBCC_COLOR_DIFF_RGB(new_color, old_color))
        av_bprintf(buf, "{\\%dc&H%06x&}", color_num,
                                          ARIBCC_COLOR_RGB(new_color));
    if (ARIBCC_COLOR_DIFF_A(new_color, old_color))
        av_bprintf(buf, "{\\%da&H%02x&}", color_num,
                                          0xFF - ARIBCC_COLOR_A(new_color));
}

static int aribcaption_trans_ass_subtitle(ARIBCaptionContext *ctx)
{
    AVSubtitle *sub = ctx->sub;
    AVBPrint buf;
    bool single_rect = ctx->ass_single_rect;
    int ret = 0, rect_idx;

    if (ctx->caption.plane_width > 0 && ctx->caption.plane_height > 0 &&
        (ctx->caption.plane_width != ctx->plane_width ||
         ctx->caption.plane_height != ctx->plane_height)) {
        ctx->plane_width = ctx->caption.plane_width;
        ctx->plane_height = ctx->caption.plane_height;
        if ((ret = set_ass_header(ctx)) < 0)
            return ret;
    }

    /* ARIB TR-B14 version 3.8 Fascicle 1-(2/2) Volume 3 [Section 4] */
    /* No position information is provided for profile C */
    if (ctx->avctx->profile == AV_PROFILE_ARIB_PROFILE_C)
        single_rect = true;

    sub->format = 1; /* text */
    if (ctx->caption.region_count == 0) {
        /* clear previous caption for indefinite duration  */
        ff_ass_add_rect(sub, "", ctx->readorder++, 0, NULL, NULL);
        return 1;
    }

    av_bprint_init(&buf, ARIBC_BPRINT_SIZE_INIT, ARIBC_BPRINT_SIZE_MAX);

    if (single_rect && ctx->avctx->profile != AV_PROFILE_ARIB_PROFILE_C) {
        int x, y, rx, ry;
        x = ctx->plane_width;
        y = ctx->plane_height;
        for (int i = 0; i < ctx->caption.region_count; i++) {
            rx = ctx->caption.regions[i].x;
            ry = ctx->caption.regions[i].y;
            if (rx < x)
                x = rx;
            if (ry < y)
                y = ry;
        }
        av_bprintf(&buf, "{\\an7}");
        if (y < 0)
            y += ctx->plane_height;
        if (x > 0 || y > 0)
            av_bprintf(&buf, "{\\pos(%d,%d)}", x, y);
    }

    rect_idx = 0;
    for (int i = 0; i < ctx->caption.region_count; i++) {
        aribcc_caption_region_t *region = &ctx->caption.regions[i];
        aribcc_color_t text_color = ARIBCC_MAKE_RGBA(0xFF, 0xFF, 0xFF,
                                                     ARIBC_ALPHA_DEFAULT_FRONT);
        aribcc_color_t stroke_color = ARIBCC_MAKE_RGBA(0, 0, 0,
                                                       ARIBC_ALPHA_DEFAULT_FRONT);
        aribcc_color_t back_color = ARIBCC_MAKE_RGBA(0, 0, 0,
                                                     ARIBC_ALPHA_DEFAULT_BACK);
        aribcc_charstyle_t charstyle = ctx->charstyle;
        int char_width = ctx->font_size;
        int char_height = ctx->font_size;
        int char_horizontal_spacing = 0;

        if (region->is_ruby && ctx->ignore_ruby)
            continue;

        if (!single_rect) {
            int x = region->x;
            int y = region->y;
            if (x < 0)
                x += ctx->plane_width;
            if (y < 0)
                y += ctx->plane_height;
            av_bprint_clear(&buf);
            av_bprintf(&buf, "{\\an7}");
            if (x > 0 || y > 0)
                av_bprintf(&buf, "{\\pos(%d,%d)}", x, y);
        }
        if (region->is_ruby)
            av_bprintf(&buf, "{\\fs%d}", char_height / 2);

        for (int j = 0; j < region->char_count; j++) {
            aribcc_caption_char_t *ch = &region->chars[j];

            if (ctx->avctx->profile != AV_PROFILE_ARIB_PROFILE_C) {
                if (ch->char_horizontal_spacing != char_horizontal_spacing) {
                    av_bprintf(&buf, "{\\fsp%d}", (region->is_ruby) ?
                                     ch->char_horizontal_spacing / 2 :
                                     ch->char_horizontal_spacing);
                    char_horizontal_spacing = ch->char_horizontal_spacing;
                }
                if (ch->char_width != char_width) {
                    av_bprintf(&buf, "{\\fscx%"PRId64"}",
                               av_rescale(ch->char_width, 100, ctx->font_size));
                    char_width = ch->char_width;
                }
                if (ch->char_height != char_height) {
                    av_bprintf(&buf, "{\\fscy%"PRId64"}",
                               av_rescale(ch->char_height, 100, ctx->font_size));
                    char_height = ch->char_height;
                }
            }
            if (ch->style != charstyle) {
                aribcc_charstyle_t diff = ch->style ^ charstyle;
                if (diff & ARIBCC_CHARSTYLE_STROKE) {
                    if (charstyle & ARIBCC_CHARSTYLE_STROKE) {
                        if (ctx->force_stroke_text)
                            av_bprintf(&buf, "{\\bord%d}",
                                       (int)(ctx->stroke_width * 4.0 / 3.0));
                        else
                            av_bprintf(&buf, "{\\bord0}");
                    } else
                        av_bprintf(&buf, "{\\bord3}");
                }
                if (diff & ARIBCC_CHARSTYLE_BOLD) {
                    if (charstyle & ARIBCC_CHARSTYLE_BOLD)
                        av_bprintf(&buf, "{\\b0}");
                    else
                        av_bprintf(&buf, "{\\b1}");
                }
                if (diff & ARIBCC_CHARSTYLE_ITALIC) {
                    if (charstyle & ARIBCC_CHARSTYLE_ITALIC)
                        av_bprintf(&buf, "{\\i0}");
                    else
                        av_bprintf(&buf, "{\\i1}");
                }
                if (diff & ARIBCC_CHARSTYLE_UNDERLINE) {
                    if (charstyle & ARIBCC_CHARSTYLE_UNDERLINE)
                        av_bprintf(&buf, "{\\u0}");
                    else
                        av_bprintf(&buf, "{\\u1}");
                }
                charstyle = ch->style;
            }
            if (ch->text_color != text_color) {
                set_ass_color(&buf, 1, ch->text_color, text_color);
                text_color = ch->text_color;
            }
            if (ch->stroke_color != stroke_color) {
                set_ass_color(&buf, 3, ch->stroke_color, stroke_color);
                stroke_color = ch->stroke_color;
            }
            if (ch->back_color != back_color) {
                if (ctx->border_style == 4)
                    set_ass_color(&buf, 4, ch->back_color, back_color);
                else
                    set_ass_color(&buf, 3, ch->back_color, back_color);
                back_color = ch->back_color;
            }
            if (region->chars[j].type == ARIBCC_CHARTYPE_DRCS)
                av_bprintf(&buf, "\xe3\x80\x93");  /* Geta Mark */
            else
                ff_ass_bprint_text_event(&buf, ch->u8str, strlen(ch->u8str), "", 0);
        }

        if (single_rect) {
            if (i + 1 < ctx->caption.region_count)
                av_bprintf(&buf, "{\\r}\\N");
            ff_dlog(ctx, "ASS subtitle%s (%d,%d) %dx%d [%d]\n",
                    (region->is_ruby) ? " (ruby)" : "",
                    region->x, region->y, region->width, region->height,
                    rect_idx);
        } else {
            if (!av_bprint_is_complete(&buf)) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }
            ff_dlog(ctx, "ASS subtitle%s (%d,%d) %dx%d [%d]: %s\n",
                    (region->is_ruby) ? " (ruby)" : "",
                    region->x, region->y, region->width, region->height,
                    rect_idx, buf.str);

            ret = ff_ass_add_rect(sub, buf.str, ctx->readorder++, 0 , NULL, NULL);
            if (ret != 0)
                goto fail;
            rect_idx++;
        }
    }
    if (single_rect) {
        if (!av_bprint_is_complete(&buf)) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ff_dlog(ctx, "ASS subtitle: %s\n", buf.str);

        ret = ff_ass_add_rect(sub, buf.str, ctx->readorder++, 0 , NULL, NULL);
        if (ret != 0)
            goto fail;
        rect_idx++;
    }

    av_bprint_finalize(&buf, NULL);
    return rect_idx;

fail:
    if (sub->rects) {
        for (int i = 0; i < ctx->caption.region_count; i++) {
            if (sub->rects[i]) {
                av_freep(&sub->rects[i]->ass);
                av_freep(&sub->rects[i]);
            }
        }
        av_freep(&sub->rects);
    }
    sub->num_rects = 0;
    av_bprint_finalize(&buf, NULL);

    return ret;
}

static int aribcaption_trans_text_subtitle(ARIBCaptionContext *ctx)
{
    AVSubtitle *sub = ctx->sub;
    AVSubtitleRect *rect;
    int ret = 0;
    const char *text;

    sub->rects = av_calloc(ctx->caption.region_count, sizeof(*sub->rects));
    if (!sub->rects) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    sub->num_rects = 1;

    sub->rects[0] = av_mallocz(sizeof(*sub->rects[0]));
    if (!sub->rects[0]) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    rect = sub->rects[0];

    if (ctx->caption.region_count == 0)
        text = ""; /* clear previous caption */
    else {
        text = ctx->caption.text;
        ff_dlog(ctx, "TEXT subtitle: %s\n", text);
    }
    rect->text = av_strdup(text);
    if (!rect->text) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    sub->format = 1; /* text */
    rect->type = SUBTITLE_TEXT;

    return 1;

fail:
    if (sub->rects) {
        rect = sub->rects[0];
        if (rect) {
            av_freep(&rect->text);
            av_freep(&rect);
        }
        av_freep(&sub->rects);
    }
    sub->num_rects = 0;

    return ret;
}

static int aribcaption_decode(AVCodecContext *avctx, AVSubtitle *sub,
                              int *got_sub_ptr, const AVPacket *avpkt)
{
    ARIBCaptionContext *ctx = avctx->priv_data;
    int status;

    ff_dlog(ctx, "ARIB caption packet pts=%"PRIx64":\n", avpkt->pts);
    if (sub->num_rects) {
        avpriv_request_sample(ctx, "Different Version of Segment asked Twice");
        return AVERROR_PATCHWELCOME;
    }
    hex_dump_debug(ctx, avpkt->data, avpkt->size);

    ctx->sub = sub;
    ctx->avpkt = avpkt;
    ctx->time_base = avctx->pkt_timebase;
    if (ctx->time_base.num <= 0 || ctx->time_base.den <= 0) {
        av_log(ctx, AV_LOG_VERBOSE, "No timebase set. assuming 90kHz.\n");
        ctx->time_base = av_make_q(1, 90000);
    }
    if (avpkt->pts == AV_NOPTS_VALUE)
        ctx->pts = ARIBCC_PTS_NOPTS;
    else
        ctx->pts = av_rescale_q(avpkt->pts, ctx->time_base, (AVRational){1, 1000});

    status = aribcc_decoder_decode(ctx->decoder, avpkt->data, avpkt->size,
                                   ctx->pts, &ctx->caption);
    if (status == ARIBCC_DECODE_STATUS_ERROR) {
        av_log(ctx, AV_LOG_ERROR,
               "aribcc_decoder_decode() returned with error.\n");
        return AVERROR(EAGAIN);
    }
    if (status == ARIBCC_DECODE_STATUS_NO_CAPTION) {
        ff_dlog(ctx, "No caption.\n");
        return avpkt->size;
    } else {
        ff_dlog(ctx, "type=%02x, flags=%x, lang=%03x\n",
                ctx->caption.type, ctx->caption.flags, ctx->caption.iso6392_language_code);
        ff_dlog(ctx, "region count = %d, start=%d.%d, duration=%d.%d\n",
                ctx->caption.region_count,
                (int)(ctx->caption.pts / 1000), (int)(ctx->caption.pts % 1000),
                (int)((ctx->caption.wait_duration == ARIBCC_DURATION_INDEFINITE) ?
                      -1 : ctx->caption.wait_duration / 1000),
                (int)((ctx->caption.wait_duration == ARIBCC_DURATION_INDEFINITE) ?
                      0 : ctx->caption.wait_duration % 1000));
    }

    switch ((enum AVSubtitleType) ctx->subtitle_type) {
    case SUBTITLE_TEXT:
        status = aribcaption_trans_text_subtitle(ctx);
        break;

    case SUBTITLE_ASS:
        status = aribcaption_trans_ass_subtitle(ctx);
        break;

    case SUBTITLE_BITMAP:
        status = aribcaption_trans_bitmap_subtitle(ctx);
        break;

    case SUBTITLE_NONE:
    default:
        status = 0;
    }

    if (status < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to set Subtitle: %s\n",
               av_err2str(status));
        aribcc_caption_cleanup(&ctx->caption);
        return status;
    }
    if (status > 0) {
        *got_sub_ptr = 1;
        if (ctx->avpkt->pts != AV_NOPTS_VALUE)
            sub->pts = av_rescale_q(ctx->avpkt->pts,
                                    ctx->time_base, AV_TIME_BASE_Q);
        if (ctx->caption.wait_duration == ARIBCC_DURATION_INDEFINITE)
            sub->end_display_time = UINT32_MAX;
        else
            sub->end_display_time = (uint32_t)ctx->caption.wait_duration;
    }

    aribcc_caption_cleanup(&ctx->caption);
    return avpkt->size;
}

static void aribcaption_flush(AVCodecContext *avctx)
{
    ARIBCaptionContext *ctx = avctx->priv_data;

    if (ctx->decoder)
        aribcc_decoder_flush(ctx->decoder);
    if (ctx->renderer)
        aribcc_renderer_flush(ctx->renderer);
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        ctx->readorder = 0;
}

static int aribcaption_close(AVCodecContext *avctx)
{
    ARIBCaptionContext *ctx = avctx->priv_data;

    av_freep(&ctx->clut);
    if (ctx->renderer)
        aribcc_renderer_free(ctx->renderer);
    if (ctx->decoder)
        aribcc_decoder_free(ctx->decoder);
    if (ctx->context)
        aribcc_context_free(ctx->context);

    return 0;
}

static int aribcaption_init(AVCodecContext *avctx)
{
    ARIBCaptionContext *ctx = avctx->priv_data;
    aribcc_profile_t profile;
    int ret = 0;

    ctx->avctx = avctx;

    switch (avctx->profile) {
    case AV_PROFILE_ARIB_PROFILE_A:
        profile = ARIBCC_PROFILE_A;
        /* assume 960x540 at initial state */
        ctx->plane_width = 960;
        ctx->plane_height = 540;
        ctx->font_size = 36;
        break;
    case AV_PROFILE_ARIB_PROFILE_C:
        profile = ARIBCC_PROFILE_C;
        ctx->plane_width = 320;
        ctx->plane_height = 180;
        ctx->font_size = 16;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Unknown or unsupported profile set.\n");
        return AVERROR(EINVAL);
    }
    /* determine BorderStyle of ASS header */
    if (ctx->ignore_background)
        ctx->border_style = 1;
    else
        ctx->border_style = 4;
    ctx->charstyle = ARIBCC_CHARSTYLE_DEFAULT;
    if (ctx->force_stroke_text || ctx->ignore_background)
        ctx->charstyle |= ARIBCC_CHARSTYLE_STROKE;

    if (!(ctx->context = aribcc_context_alloc())) {
        av_log(avctx, AV_LOG_ERROR, "Failed to alloc libaribcaption context.\n");
        return AVERROR_EXTERNAL;
    }
    aribcc_context_set_logcat_callback(ctx->context, logcat_callback, avctx);
    if (!(ctx->decoder = aribcc_decoder_alloc(ctx->context))) {
        av_log(avctx, AV_LOG_ERROR, "Failed to alloc libaribcaption decoder.\n");
        return AVERROR_EXTERNAL;
    }
    if (!aribcc_decoder_initialize(ctx->decoder,
                                   (enum aribcc_encoding_scheme_t) ctx->encoding_scheme,
                                   ARIBCC_CAPTIONTYPE_CAPTION,
                                   profile,
                                   ARIBCC_LANGUAGEID_FIRST)) {
        av_log(avctx, AV_LOG_ERROR, "Failed to initialize libaribcaption decoder.\n");
        return AVERROR_EXTERNAL;
    }
    aribcc_decoder_set_replace_msz_fullwidth_ascii(ctx->decoder,
                                                   ctx->replace_msz_ascii);
    aribcc_decoder_set_replace_msz_fullwidth_japanese(ctx->decoder,
                                                      ctx->replace_msz_japanese);

    /* Similar behavior as ffmpeg tool to set canvas size */
    if (ctx->canvas_width > 0 && ctx->canvas_height > 0 &&
        (ctx->avctx->width == 0 || ctx->avctx->height == 0)) {
        ctx->avctx->width = ctx->canvas_width;
        ctx->avctx->height = ctx->canvas_height;
    }

    switch ((enum AVSubtitleType) ctx->subtitle_type) {
    case SUBTITLE_ASS:
        ret = set_ass_header(ctx);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to set ASS header: %s\n",
                                        av_err2str(ret));
            return ret;
        }
        break;

    case SUBTITLE_BITMAP:
        if(!(ctx->renderer = aribcc_renderer_alloc(ctx->context))) {
            av_log(avctx, AV_LOG_ERROR, "Failed to alloc libaribcaption renderer.\n");
            return AVERROR_EXTERNAL;
        }
        if(!aribcc_renderer_initialize(ctx->renderer,
                                       ARIBCC_CAPTIONTYPE_CAPTION,
                                       ARIBCC_FONTPROVIDER_TYPE_AUTO,
                                       ARIBCC_TEXTRENDERER_TYPE_AUTO)) {
            av_log(avctx, AV_LOG_ERROR, "Failed to initialize libaribcaption renderer.\n");
            return AVERROR_EXTERNAL;
        }
        estimate_video_frame_size(ctx);
        ff_dlog(ctx, "canvas: %dx%d  plane: %dx%d  bitmap: %dx%d  frame: %dx%d\n",
                ctx->avctx->width, ctx->avctx->height,
                ctx->plane_width, ctx->plane_height,
                ctx->bitmap_plane_width, ctx->bitmap_plane_height,
                ctx->frame_width, ctx->frame_height);
        if (!aribcc_renderer_set_frame_size(ctx->renderer,
                                            ctx->frame_width, ctx->frame_height)) {
            av_log(ctx, AV_LOG_ERROR,
                   "aribcc_renderer_set_frame_size() returned with error.\n");
            return AVERROR_EXTERNAL;
        }

        if (!(ctx->clut = av_mallocz(AVPALETTE_SIZE)))
            return AVERROR(ENOMEM);

        aribcc_renderer_set_storage_policy(ctx->renderer, ARIBCC_CAPTION_STORAGE_POLICY_MINIMUM, 0);
        aribcc_renderer_set_replace_drcs(ctx->renderer, ctx->replace_drcs);
        aribcc_renderer_set_force_stroke_text(ctx->renderer, ctx->force_stroke_text);
        aribcc_renderer_set_force_no_background(ctx->renderer, ctx->ignore_background);
        aribcc_renderer_set_force_no_ruby(ctx->renderer, ctx->ignore_ruby);
        aribcc_renderer_set_stroke_width(ctx->renderer, ctx->stroke_width);
        aribcc_renderer_set_replace_msz_halfwidth_glyph(ctx->renderer,
                                                        ctx->replace_msz_glyph);
        if (ctx->font) {
            int is_nomem = 0;
            size_t count = 0;
            const char **font_families = NULL;
            const char *fonts = ctx->font;

            while (*fonts) {
                const char **ff = av_realloc_array(font_families, count + 1, sizeof(*font_families));
                if (!ff) {
                    is_nomem = 1;
                    break;
                } else {
                    font_families = ff;
                    ff[count++] = av_get_token(&fonts, ",");
                    if (!ff[count - 1]) {
                        is_nomem = 1;
                        break;
                    } else if (*fonts)
                        fonts++;
                }
            }
            if (!is_nomem && count)
                aribcc_renderer_set_default_font_family(ctx->renderer, font_families, count, true);
            while (count)
                av_freep(&font_families[--count]);
            av_freep(&font_families);
            if (is_nomem)
                return AVERROR(ENOMEM);
        }
        break;

    case SUBTITLE_TEXT:
    case SUBTITLE_NONE:
    default:
        /* do nothing */ ;
    }

    ctx->readorder = 0;

    return 0;
}

#if !defined(ASS_SINGLE_RECT)
# define ASS_SINGLE_RECT 0
#endif

#define OFFSET(x) offsetof(ARIBCaptionContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "sub_type", "subtitle rendering type",
      OFFSET(subtitle_type), AV_OPT_TYPE_INT,
      { .i64 = SUBTITLE_ASS }, SUBTITLE_NONE, SUBTITLE_ASS, SD, .unit = "type" },
    { "none",   "do nothing", 0, AV_OPT_TYPE_CONST,
      { .i64 = SUBTITLE_NONE }, .flags = SD, .unit = "type" },
    { "bitmap", "bitmap rendering", 0, AV_OPT_TYPE_CONST,
      { .i64 = SUBTITLE_BITMAP }, .flags = SD, .unit = "type" },
    { "text",   "plain text", 0, AV_OPT_TYPE_CONST,
      { .i64 = SUBTITLE_TEXT }, .flags = SD, .unit = "type" },
    { "ass",    "formatted text", 0, AV_OPT_TYPE_CONST,
      { .i64 = SUBTITLE_ASS }, .flags = SD, .unit = "type" },
    { "caption_encoding", "encoding scheme of subtitle text",
      OFFSET(encoding_scheme), AV_OPT_TYPE_INT, { .i64 = ARIBCC_ENCODING_SCHEME_AUTO },
      ARIBCC_ENCODING_SCHEME_AUTO, ARIBCC_ENCODING_SCHEME_ABNT_NBR_15606_1_LATIN, SD, .unit = "encoding" },
    { "auto",   "automatically detect encoding scheme", 0, AV_OPT_TYPE_CONST,
      { .i64 = ARIBCC_ENCODING_SCHEME_AUTO }, .flags = SD, .unit = "encoding" },
    { "jis",    "8bit-char JIS encoding (Japanese ISDB captions)", 0, AV_OPT_TYPE_CONST,
      { .i64 = ARIBCC_ENCODING_SCHEME_ARIB_STD_B24_JIS }, .flags = SD, .unit = "encoding" },
    { "utf8",   "UTF-8 encoding (Philippines ISDB-T captions)", 0, AV_OPT_TYPE_CONST,
      { .i64 = ARIBCC_ENCODING_SCHEME_ARIB_STD_B24_UTF8 }, .flags = SD, .unit = "encoding" },
    { "latin",  "latin characters (SBTVD / ISDB-Tb captions used in South America)", 0, AV_OPT_TYPE_CONST,
      { .i64 = ARIBCC_ENCODING_SCHEME_ABNT_NBR_15606_1_LATIN }, .flags = SD, .unit = "encoding" },
    { "ass_single_rect", "workaround of ASS subtitle for players which can't handle multi-rectangle [ass]",
      OFFSET(ass_single_rect), AV_OPT_TYPE_BOOL, { .i64 = ASS_SINGLE_RECT }, 0, 1, SD },
    { "font", "comma-separated font family [ass, bitmap]",
      OFFSET(font), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, SD },
    { "force_outline_text", "always render characters with outline [(ass), bitmap]",
      OFFSET(force_stroke_text), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, SD },
    { "ignore_background", "ignore rendering caption background [(ass), bitmap]",
      OFFSET(ignore_background), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, SD },
    { "ignore_ruby", "ignore ruby-like characters [ass, bitmap]",
      OFFSET(ignore_ruby), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, SD },
    { "outline_width", "outline width of text [(ass), bitmap]",
      OFFSET(stroke_width), AV_OPT_TYPE_FLOAT, { .dbl = 1.5 }, 0.0, 3.0, SD },
    { "replace_drcs", "replace known DRCS [bitmap]",
      OFFSET(replace_drcs), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, SD },
    { "replace_msz_ascii", "replace MSZ fullwidth alphanumerics with halfwidth alphanumerics [ass, bitmap]",
      OFFSET(replace_msz_ascii), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, SD },
    { "replace_msz_japanese", "replace MSZ fullwidth Japanese with halfwidth [ass, bitmap]",
      OFFSET(replace_msz_japanese), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, SD },
    { "replace_msz_glyph", "replace MSZ characters with halfwidth glyphs [bitmap]",
      OFFSET(replace_msz_glyph), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, SD },
    {"canvas_size", "set input video size (WxH or abbreviation) [bitmap]",
      OFFSET(canvas_width), AV_OPT_TYPE_IMAGE_SIZE, { .str = NULL }, 0, INT_MAX, SD },
    { NULL }
};

static const AVClass aribcaption_class = {
    .class_name = "aribcaption decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_libaribcaption_decoder = {
    .p.name         = "libaribcaption",
    .p.long_name    = NULL_IF_CONFIG_SMALL("ARIB STD-B24 caption decoder"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_ARIB_CAPTION,
    .priv_data_size = sizeof(ARIBCaptionContext),
    .init           = aribcaption_init,
    .close          = aribcaption_close,
    FF_CODEC_DECODE_SUB_CB(aribcaption_decode),
    .flush          = aribcaption_flush,
    .p.priv_class   = &aribcaption_class,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
