/*
 * PGS subtitle decoder
 * Copyright (c) 2009 Stephen Backway
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * PGS subtitle decoder
 */

#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "mathops.h"

#include "libavutil/colorspace.h"
#include "libavutil/imgutils.h"

#define RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define MAX_EPOCH_PALETTES 8   // Max 8 allowed per PGS epoch
#define MAX_EPOCH_OBJECTS  64  // Max 64 allowed per PGS epoch
#define MAX_OBJECT_REFS    2   // Max objects per display set

enum SegmentType {
    PALETTE_SEGMENT      = 0x14,
    OBJECT_SEGMENT       = 0x15,
    PRESENTATION_SEGMENT = 0x16,
    WINDOW_SEGMENT       = 0x17,
    DISPLAY_SEGMENT      = 0x80,
};

typedef struct PGSSubObjectRef {
    int     id;
    int     window_id;
    uint8_t composition_flag;
    int     x;
    int     y;
    int     crop_x;
    int     crop_y;
    int     crop_w;
    int     crop_h;
} PGSSubObjectRef;

typedef struct PGSSubPresentation {
    int id_number;
    int palette_id;
    int object_count;
    PGSSubObjectRef objects[MAX_OBJECT_REFS];
    int64_t pts;
} PGSSubPresentation;

typedef struct PGSSubObject {
    int          id;
    int          w;
    int          h;
    uint8_t      *rle;
    unsigned int rle_buffer_size, rle_data_len;
    unsigned int rle_remaining_len;
} PGSSubObject;

typedef struct PGSSubObjects {
    int          count;
    PGSSubObject object[MAX_EPOCH_OBJECTS];
} PGSSubObjects;

typedef struct PGSSubPalette {
    int         id;
    uint32_t    clut[256];
} PGSSubPalette;

typedef struct PGSSubPalettes {
    int           count;
    PGSSubPalette palette[MAX_EPOCH_PALETTES];
} PGSSubPalettes;

typedef struct PGSSubContext {
    PGSSubPresentation presentation;
    PGSSubPalettes     palettes;
    PGSSubObjects      objects;
} PGSSubContext;

static void flush_cache(AVCodecContext *avctx)
{
    PGSSubContext *ctx = avctx->priv_data;
    int i;

    for (i = 0; i < ctx->objects.count; i++) {
        av_freep(&ctx->objects.object[i].rle);
        ctx->objects.object[i].rle_buffer_size  = 0;
        ctx->objects.object[i].rle_remaining_len  = 0;
    }
    ctx->objects.count = 0;
    ctx->palettes.count = 0;
}

static PGSSubObject * find_object(int id, PGSSubObjects *objects)
{
    int i;

    for (i = 0; i < objects->count; i++) {
        if (objects->object[i].id == id)
            return &objects->object[i];
    }
    return NULL;
}

static PGSSubPalette * find_palette(int id, PGSSubPalettes *palettes)
{
    int i;

    for (i = 0; i < palettes->count; i++) {
        if (palettes->palette[i].id == id)
            return &palettes->palette[i];
    }
    return NULL;
}

static av_cold int init_decoder(AVCodecContext *avctx)
{
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    return 0;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    flush_cache(avctx);

    return 0;
}

/**
 * Decode the RLE data.
 *
 * The subtitle is stored as an Run Length Encoded image.
 *
 * @param avctx contains the current codec context
 * @param sub pointer to the processed subtitle data
 * @param buf pointer to the RLE data to process
 * @param buf_size size of the RLE data to process
 */
static int decode_rle(AVCodecContext *avctx, AVSubtitleRect *rect,
                      const uint8_t *buf, unsigned int buf_size)
{
    const uint8_t *rle_bitmap_end;
    int pixel_count, line_count;

    rle_bitmap_end = buf + buf_size;

    rect->data[0] = av_malloc(rect->w * rect->h);

    if (!rect->data[0])
        return AVERROR(ENOMEM);

    pixel_count = 0;
    line_count  = 0;

    while (buf < rle_bitmap_end && line_count < rect->h) {
        uint8_t flags, color;
        int run;

        color = bytestream_get_byte(&buf);
        run   = 1;

        if (color == 0x00) {
            flags = bytestream_get_byte(&buf);
            run   = flags & 0x3f;
            if (flags & 0x40)
                run = (run << 8) + bytestream_get_byte(&buf);
            color = flags & 0x80 ? bytestream_get_byte(&buf) : 0;
        }

        if (run > 0 && pixel_count + run <= rect->w * rect->h) {
            memset(rect->data[0] + pixel_count, color, run);
            pixel_count += run;
        } else if (!run) {
            /*
             * New Line. Check if correct pixels decoded, if not display warning
             * and adjust bitmap pointer to correct new line position.
             */
            if (pixel_count % rect->w > 0) {
                av_log(avctx, AV_LOG_ERROR, "Decoded %d pixels, when line should be %d pixels\n",
                       pixel_count % rect->w, rect->w);
                if (avctx->err_recognition & AV_EF_EXPLODE) {
                    return AVERROR_INVALIDDATA;
                }
            }
            line_count++;
        }
    }

    if (pixel_count < rect->w * rect->h) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient RLE data for subtitle\n");
        return AVERROR_INVALIDDATA;
    }

    ff_dlog(avctx, "Pixel Count = %d, Area = %d\n", pixel_count, rect->w * rect->h);

    return 0;
}

/**
 * Parse the picture segment packet.
 *
 * The picture segment contains details on the sequence id,
 * width, height and Run Length Encoded (RLE) bitmap data.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int parse_object_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;
    PGSSubObject *object;

    uint8_t sequence_desc;
    unsigned int rle_bitmap_len, width, height;
    int id;

    if (buf_size <= 4)
        return AVERROR_INVALIDDATA;
    buf_size -= 4;

    id = bytestream_get_be16(&buf);
    object = find_object(id, &ctx->objects);
    if (!object) {
        if (ctx->objects.count >= MAX_EPOCH_OBJECTS) {
            av_log(avctx, AV_LOG_ERROR, "Too many objects in epoch\n");
            return AVERROR_INVALIDDATA;
        }
        object = &ctx->objects.object[ctx->objects.count++];
        object->id = id;
    }

    /* skip object version number */
    buf += 1;

    /* Read the Sequence Description to determine if start of RLE data or appended to previous RLE */
    sequence_desc = bytestream_get_byte(&buf);

    if (!(sequence_desc & 0x80)) {
        /* Additional RLE data */
        if (buf_size > object->rle_remaining_len)
            return AVERROR_INVALIDDATA;

        memcpy(object->rle + object->rle_data_len, buf, buf_size);
        object->rle_data_len += buf_size;
        object->rle_remaining_len -= buf_size;

        return 0;
    }

    if (buf_size <= 7)
        return AVERROR_INVALIDDATA;
    buf_size -= 7;

    /* Decode rle bitmap length, stored size includes width/height data */
    rle_bitmap_len = bytestream_get_be24(&buf) - 2*2;

    if (buf_size > rle_bitmap_len) {
        av_log(avctx, AV_LOG_ERROR,
               "Buffer dimension %d larger than the expected RLE data %d\n",
               buf_size, rle_bitmap_len);
        return AVERROR_INVALIDDATA;
    }

    /* Get bitmap dimensions from data */
    width  = bytestream_get_be16(&buf);
    height = bytestream_get_be16(&buf);

    /* Make sure the bitmap is not too large */
    if (avctx->width < width || avctx->height < height) {
        av_log(avctx, AV_LOG_ERROR, "Bitmap dimensions larger than video.\n");
        return AVERROR_INVALIDDATA;
    }

    object->w = width;
    object->h = height;

    av_fast_malloc(&object->rle, &object->rle_buffer_size, rle_bitmap_len);

    if (!object->rle)
        return AVERROR(ENOMEM);

    memcpy(object->rle, buf, buf_size);
    object->rle_data_len = buf_size;
    object->rle_remaining_len = rle_bitmap_len - buf_size;

    return 0;
}

/**
 * Parse the palette segment packet.
 *
 * The palette segment contains details of the palette,
 * a maximum of 256 colors can be defined.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int parse_palette_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;
    PGSSubPalette *palette;

    const uint8_t *buf_end = buf + buf_size;
    const uint8_t *cm      = ff_crop_tab + MAX_NEG_CROP;
    int color_id;
    int y, cb, cr, alpha;
    int r, g, b, r_add, g_add, b_add;
    int id;

    id  = bytestream_get_byte(&buf);
    palette = find_palette(id, &ctx->palettes);
    if (!palette) {
        if (ctx->palettes.count >= MAX_EPOCH_PALETTES) {
            av_log(avctx, AV_LOG_ERROR, "Too many palettes in epoch\n");
            return AVERROR_INVALIDDATA;
        }
        palette = &ctx->palettes.palette[ctx->palettes.count++];
        palette->id  = id;
    }

    /* Skip palette version */
    buf += 1;

    while (buf < buf_end) {
        color_id  = bytestream_get_byte(&buf);
        y         = bytestream_get_byte(&buf);
        cr        = bytestream_get_byte(&buf);
        cb        = bytestream_get_byte(&buf);
        alpha     = bytestream_get_byte(&buf);

        /* Default to BT.709 colorspace. In case of <= 576 height use BT.601 */
        if (avctx->height <= 0 || avctx->height > 576) {
            YUV_TO_RGB1_CCIR_BT709(cb, cr);
        } else {
            YUV_TO_RGB1_CCIR(cb, cr);
        }

        YUV_TO_RGB2_CCIR(r, g, b, y);

        ff_dlog(avctx, "Color %d := (%d,%d,%d,%d)\n", color_id, r, g, b, alpha);

        /* Store color in palette */
        palette->clut[color_id] = RGBA(r,g,b,alpha);
    }
    return 0;
}

/**
 * Parse the presentation segment packet.
 *
 * The presentation segment contains details on the video
 * width, video height, x & y subtitle position.
 *
 * @param avctx contains the current codec context
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 * @todo TODO: Implement cropping
 */
static int parse_presentation_segment(AVCodecContext *avctx,
                                      const uint8_t *buf, int buf_size,
                                      int64_t pts)
{
    PGSSubContext *ctx = avctx->priv_data;

    int i, state, ret;

    // Video descriptor
    int w = bytestream_get_be16(&buf);
    int h = bytestream_get_be16(&buf);

    ctx->presentation.pts = pts;

    ff_dlog(avctx, "Video Dimensions %dx%d\n",
            w, h);
    ret = ff_set_dimensions(avctx, w, h);
    if (ret < 0)
        return ret;

    /* Skip 1 bytes of unknown, frame rate */
    buf++;

    // Composition descriptor
    ctx->presentation.id_number = bytestream_get_be16(&buf);
    /*
     * state is a 2 bit field that defines pgs epoch boundaries
     * 00 - Normal, previously defined objects and palettes are still valid
     * 01 - Acquisition point, previous objects and palettes can be released
     * 10 - Epoch start, previous objects and palettes can be released
     * 11 - Epoch continue, previous objects and palettes can be released
     *
     * reserved 6 bits discarded
     */
    state = bytestream_get_byte(&buf) >> 6;
    if (state != 0) {
        flush_cache(avctx);
    }

    /*
     * skip palette_update_flag (0x80),
     */
    buf += 1;
    ctx->presentation.palette_id = bytestream_get_byte(&buf);
    ctx->presentation.object_count = bytestream_get_byte(&buf);
    if (ctx->presentation.object_count > MAX_OBJECT_REFS) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid number of presentation objects %d\n",
               ctx->presentation.object_count);
        ctx->presentation.object_count = 2;
        if (avctx->err_recognition & AV_EF_EXPLODE) {
            return AVERROR_INVALIDDATA;
        }
    }

    for (i = 0; i < ctx->presentation.object_count; i++)
    {
        ctx->presentation.objects[i].id = bytestream_get_be16(&buf);
        ctx->presentation.objects[i].window_id = bytestream_get_byte(&buf);
        ctx->presentation.objects[i].composition_flag = bytestream_get_byte(&buf);

        ctx->presentation.objects[i].x = bytestream_get_be16(&buf);
        ctx->presentation.objects[i].y = bytestream_get_be16(&buf);

        // If cropping
        if (ctx->presentation.objects[i].composition_flag & 0x80) {
            ctx->presentation.objects[i].crop_x = bytestream_get_be16(&buf);
            ctx->presentation.objects[i].crop_y = bytestream_get_be16(&buf);
            ctx->presentation.objects[i].crop_w = bytestream_get_be16(&buf);
            ctx->presentation.objects[i].crop_h = bytestream_get_be16(&buf);
        }

        ff_dlog(avctx, "Subtitle Placement x=%d, y=%d\n",
                ctx->presentation.objects[i].x, ctx->presentation.objects[i].y);

        if (ctx->presentation.objects[i].x > avctx->width ||
            ctx->presentation.objects[i].y > avctx->height) {
            av_log(avctx, AV_LOG_ERROR, "Subtitle out of video bounds. x = %d, y = %d, video width = %d, video height = %d.\n",
                   ctx->presentation.objects[i].x,
                   ctx->presentation.objects[i].y,
                    avctx->width, avctx->height);
            ctx->presentation.objects[i].x = 0;
            ctx->presentation.objects[i].y = 0;
            if (avctx->err_recognition & AV_EF_EXPLODE) {
                return AVERROR_INVALIDDATA;
            }
        }
    }

    return 0;
}

/**
 * Parse the display segment packet.
 *
 * The display segment controls the updating of the display.
 *
 * @param avctx contains the current codec context
 * @param data pointer to the data pertaining the subtitle to display
 * @param buf pointer to the packet to process
 * @param buf_size size of packet to process
 */
static int display_end_segment(AVCodecContext *avctx, void *data,
                               const uint8_t *buf, int buf_size)
{
    AVSubtitle    *sub = data;
    PGSSubContext *ctx = avctx->priv_data;
    PGSSubPalette *palette;
    int i, ret;

    memset(sub, 0, sizeof(*sub));
    sub->pts = ctx->presentation.pts;
    sub->start_display_time = 0;
    // There is no explicit end time for PGS subtitles.  The end time
    // is defined by the start of the next sub which may contain no
    // objects (i.e. clears the previous sub)
    sub->end_display_time   = UINT32_MAX;
    sub->format             = 0;

    // Blank if last object_count was 0.
    if (!ctx->presentation.object_count)
        return 1;
    sub->rects = av_mallocz(sizeof(*sub->rects) * ctx->presentation.object_count);
    if (!sub->rects) {
        return AVERROR(ENOMEM);
    }
    palette = find_palette(ctx->presentation.palette_id, &ctx->palettes);
    if (!palette) {
        // Missing palette.  Should only happen with damaged streams.
        av_log(avctx, AV_LOG_ERROR, "Invalid palette id %d\n",
               ctx->presentation.palette_id);
        avsubtitle_free(sub);
        return AVERROR_INVALIDDATA;
    }
    for (i = 0; i < ctx->presentation.object_count; i++) {
        PGSSubObject *object;

        sub->rects[i]  = av_mallocz(sizeof(*sub->rects[0]));
        if (!sub->rects[i]) {
            avsubtitle_free(sub);
            return AVERROR(ENOMEM);
        }
        sub->num_rects++;
        sub->rects[i]->type = SUBTITLE_BITMAP;

        /* Process bitmap */
        object = find_object(ctx->presentation.objects[i].id, &ctx->objects);
        if (!object) {
            // Missing object.  Should only happen with damaged streams.
            av_log(avctx, AV_LOG_ERROR, "Invalid object id %d\n",
                   ctx->presentation.objects[i].id);
            if (avctx->err_recognition & AV_EF_EXPLODE) {
                avsubtitle_free(sub);
                return AVERROR_INVALIDDATA;
            }
            // Leaves rect empty with 0 width and height.
            continue;
        }
        if (ctx->presentation.objects[i].composition_flag & 0x40)
            sub->rects[i]->flags |= AV_SUBTITLE_FLAG_FORCED;

        sub->rects[i]->x    = ctx->presentation.objects[i].x;
        sub->rects[i]->y    = ctx->presentation.objects[i].y;
        sub->rects[i]->w    = object->w;
        sub->rects[i]->h    = object->h;

        sub->rects[i]->linesize[0] = object->w;

        if (object->rle) {
            if (object->rle_remaining_len) {
                av_log(avctx, AV_LOG_ERROR, "RLE data length %u is %u bytes shorter than expected\n",
                       object->rle_data_len, object->rle_remaining_len);
                if (avctx->err_recognition & AV_EF_EXPLODE) {
                    avsubtitle_free(sub);
                    return AVERROR_INVALIDDATA;
                }
            }
            ret = decode_rle(avctx, sub->rects[i], object->rle, object->rle_data_len);
            if (ret < 0) {
                if ((avctx->err_recognition & AV_EF_EXPLODE) ||
                    ret == AVERROR(ENOMEM)) {
                    avsubtitle_free(sub);
                    return ret;
                }
                sub->rects[i]->w = 0;
                sub->rects[i]->h = 0;
                continue;
            }
        }
        /* Allocate memory for colors */
        sub->rects[i]->nb_colors    = 256;
        sub->rects[i]->data[1] = av_mallocz(AVPALETTE_SIZE);
        if (!sub->rects[i]->data[1]) {
            avsubtitle_free(sub);
            return AVERROR(ENOMEM);
        }

#if FF_API_AVPICTURE
FF_DISABLE_DEPRECATION_WARNINGS
{
        AVSubtitleRect *rect;
        int j;
        rect = sub->rects[i];
        for (j = 0; j < 4; j++) {
            rect->pict.data[j] = rect->data[j];
            rect->pict.linesize[j] = rect->linesize[j];
        }
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        memcpy(sub->rects[i]->data[1], palette->clut, sub->rects[i]->nb_colors * sizeof(uint32_t));

    }
    return 1;
}

static int decode(AVCodecContext *avctx, void *data, int *data_size,
                  AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;

    const uint8_t *buf_end;
    uint8_t       segment_type;
    int           segment_length;
    int i, ret;

    ff_dlog(avctx, "PGS sub packet:\n");

    for (i = 0; i < buf_size; i++) {
        ff_dlog(avctx, "%02x ", buf[i]);
        if (i % 16 == 15)
            ff_dlog(avctx, "\n");
    }

    if (i & 15)
        ff_dlog(avctx, "\n");

    *data_size = 0;

    /* Ensure that we have received at a least a segment code and segment length */
    if (buf_size < 3)
        return -1;

    buf_end = buf + buf_size;

    /* Step through buffer to identify segments */
    while (buf < buf_end) {
        segment_type   = bytestream_get_byte(&buf);
        segment_length = bytestream_get_be16(&buf);

        ff_dlog(avctx, "Segment Length %d, Segment Type %x\n", segment_length, segment_type);

        if (segment_type != DISPLAY_SEGMENT && segment_length > buf_end - buf)
            break;

        ret = 0;
        switch (segment_type) {
        case PALETTE_SEGMENT:
            ret = parse_palette_segment(avctx, buf, segment_length);
            break;
        case OBJECT_SEGMENT:
            ret = parse_object_segment(avctx, buf, segment_length);
            break;
        case PRESENTATION_SEGMENT:
            ret = parse_presentation_segment(avctx, buf, segment_length, avpkt->pts);
            break;
        case WINDOW_SEGMENT:
            /*
             * Window Segment Structure (No new information provided):
             *     2 bytes: Unknown,
             *     2 bytes: X position of subtitle,
             *     2 bytes: Y position of subtitle,
             *     2 bytes: Width of subtitle,
             *     2 bytes: Height of subtitle.
             */
            break;
        case DISPLAY_SEGMENT:
            ret = display_end_segment(avctx, data, buf, segment_length);
            if (ret >= 0)
                *data_size = ret;
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown subtitle segment type 0x%x, length %d\n",
                   segment_type, segment_length);
            ret = AVERROR_INVALIDDATA;
            break;
        }
        if (ret < 0 && (avctx->err_recognition & AV_EF_EXPLODE))
            return ret;

        buf += segment_length;
    }

    return buf_size;
}

AVCodec ff_pgssub_decoder = {
    .name           = "pgssub",
    .long_name      = NULL_IF_CONFIG_SMALL("HDMV Presentation Graphic Stream subtitles"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_HDMV_PGS_SUBTITLE,
    .priv_data_size = sizeof(PGSSubContext),
    .init           = init_decoder,
    .close          = close_decoder,
    .decode         = decode,
};
