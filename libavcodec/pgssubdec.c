/*
 * PGS subtitle decoder
 * Copyright (c) 2009 Stephen Backway
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

/**
 * @file
 * PGS subtitle decoder
 */

#include "avcodec.h"
#include "dsputil.h"
#include "bytestream.h"
#include "libavutil/colorspace.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"

#define RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

enum SegmentType {
    PALETTE_SEGMENT      = 0x14,
    PICTURE_SEGMENT      = 0x15,
    PRESENTATION_SEGMENT = 0x16,
    WINDOW_SEGMENT       = 0x17,
    DISPLAY_SEGMENT      = 0x80,
};

typedef struct PGSSubPictureReference {
    int x;
    int y;
    int picture_id;
    int composition;
} PGSSubPictureReference;

typedef struct PGSSubPresentation {
    int                    id_number;
    int                    object_count;
    PGSSubPictureReference *objects;
} PGSSubPresentation;

typedef struct PGSSubPicture {
    int          w;
    int          h;
    uint8_t      *rle;
    unsigned int rle_buffer_size, rle_data_len;
    unsigned int rle_remaining_len;
} PGSSubPicture;

typedef struct PGSSubContext {
    AVClass *class;
    PGSSubPresentation presentation;
    uint32_t           clut[256];
    PGSSubPicture      pictures[UINT16_MAX];
    int64_t            pts;
    int forced_subs_only;
} PGSSubContext;

static av_cold int init_decoder(AVCodecContext *avctx)
{
    avctx->pix_fmt     = PIX_FMT_PAL8;

    return 0;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    uint16_t picture;

    PGSSubContext *ctx = avctx->priv_data;

    av_freep(&ctx->presentation.objects);
    ctx->presentation.object_count = 0;

    for (picture = 0; picture < UINT16_MAX; ++picture) {
        av_freep(&ctx->pictures[picture].rle);
        ctx->pictures[picture].rle_buffer_size = 0;
    }

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
static int decode_rle(AVCodecContext *avctx, AVSubtitle *sub, int rect,
                      const uint8_t *buf, unsigned int buf_size)
{
    const uint8_t *rle_bitmap_end;
    int pixel_count, line_count;

    rle_bitmap_end = buf + buf_size;

    sub->rects[rect]->pict.data[0] = av_malloc(sub->rects[rect]->w * sub->rects[rect]->h);

    if (!sub->rects[rect]->pict.data[0])
        return -1;

    pixel_count = 0;
    line_count  = 0;

    while (buf < rle_bitmap_end && line_count < sub->rects[rect]->h) {
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

        if (run > 0 && pixel_count + run <= sub->rects[rect]->w * sub->rects[rect]->h) {
            memset(sub->rects[rect]->pict.data[0] + pixel_count, color, run);
            pixel_count += run;
        } else if (!run) {
            /*
             * New Line. Check if correct pixels decoded, if not display warning
             * and adjust bitmap pointer to correct new line position.
             */
            if (pixel_count % sub->rects[rect]->w > 0)
                av_log(avctx, AV_LOG_ERROR, "Decoded %d pixels, when line should be %d pixels\n",
                       pixel_count % sub->rects[rect]->w, sub->rects[rect]->w);
            line_count++;
        }
    }

    if (pixel_count < sub->rects[rect]->w * sub->rects[rect]->h) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient RLE data for subtitle\n");
        return -1;
    }

    av_dlog(avctx, "Pixel Count = %d, Area = %d\n", pixel_count, sub->rects[rect]->w * sub->rects[rect]->h);

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
 * @todo TODO: Enable support for RLE data over multiple packets
 */
static int parse_picture_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;

    uint8_t sequence_desc;
    unsigned int rle_bitmap_len, width, height;
    uint16_t picture_id;

    if (buf_size <= 4)
        return -1;
    buf_size -= 4;

    picture_id = bytestream_get_be16(&buf);

    /* skip 1 unknown byte: Version Number */
    buf++;

    /* Read the Sequence Description to determine if start of RLE data or appended to previous RLE */
    sequence_desc = bytestream_get_byte(&buf);

    if (!(sequence_desc & 0x80)) {
        /* Additional RLE data */
        if (buf_size > ctx->pictures[picture_id].rle_remaining_len)
            return -1;

        memcpy(ctx->pictures[picture_id].rle + ctx->pictures[picture_id].rle_data_len, buf, buf_size);
        ctx->pictures[picture_id].rle_data_len += buf_size;
        ctx->pictures[picture_id].rle_remaining_len -= buf_size;

        return 0;
    }

    if (buf_size <= 7)
        return -1;
    buf_size -= 7;

    /* Decode rle bitmap length, stored size includes width/height data */
    rle_bitmap_len = bytestream_get_be24(&buf) - 2*2;

    /* Get bitmap dimensions from data */
    width  = bytestream_get_be16(&buf);
    height = bytestream_get_be16(&buf);

    /* Make sure the bitmap is not too large */
    if (avctx->width < width || avctx->height < height) {
        av_log(avctx, AV_LOG_ERROR, "Bitmap dimensions larger than video.\n");
        return -1;
    }

    ctx->pictures[picture_id].w = width;
    ctx->pictures[picture_id].h = height;

    av_fast_malloc(&ctx->pictures[picture_id].rle, &ctx->pictures[picture_id].rle_buffer_size, rle_bitmap_len);

    if (!ctx->pictures[picture_id].rle)
        return -1;

    memcpy(ctx->pictures[picture_id].rle, buf, buf_size);
    ctx->pictures[picture_id].rle_data_len      = buf_size;
    ctx->pictures[picture_id].rle_remaining_len = rle_bitmap_len - buf_size;

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
static void parse_palette_segment(AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;

    const uint8_t *buf_end = buf + buf_size;
    const uint8_t *cm      = ff_cropTbl + MAX_NEG_CROP;
    int color_id;
    int y, cb, cr, alpha;
    int r, g, b, r_add, g_add, b_add;

    /* Skip two null bytes */
    buf += 2;

    while (buf < buf_end) {
        color_id  = bytestream_get_byte(&buf);
        y         = bytestream_get_byte(&buf);
        cr        = bytestream_get_byte(&buf);
        cb        = bytestream_get_byte(&buf);
        alpha     = bytestream_get_byte(&buf);

        YUV_TO_RGB1(cb, cr);
        YUV_TO_RGB2(r, g, b, y);

        av_dlog(avctx, "Color %d := (%d,%d,%d,%d)\n", color_id, r, g, b, alpha);

        /* Store color in palette */
        ctx->clut[color_id] = RGBA(r,g,b,alpha);
    }
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
static void parse_presentation_segment(AVCodecContext *avctx,
                                       const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;

    int w = bytestream_get_be16(&buf);
    int h = bytestream_get_be16(&buf);

    uint16_t object_index;

    av_dlog(avctx, "Video Dimensions %dx%d\n",
            w, h);
    if (av_image_check_size(w, h, 0, avctx) >= 0)
        avcodec_set_dimensions(avctx, w, h);

    /* Skip 1 bytes of unknown, frame rate? */
    buf++;

    ctx->presentation.id_number = bytestream_get_be16(&buf);

    /*
     * Skip 3 bytes of unknown:
     *     state
     *     palette_update_flag (0x80),
     *     palette_id_to_use,
     */
    buf += 3;

    ctx->presentation.object_count = bytestream_get_byte(&buf);
    if (!ctx->presentation.object_count)
        return;

    /* Verify that enough bytes are remaining for all of the objects. */
    buf_size -= 11;
    if (buf_size < ctx->presentation.object_count * 8) {
        ctx->presentation.object_count = 0;
        return;
    }

    av_freep(&ctx->presentation.objects);
    ctx->presentation.objects = av_malloc(sizeof(PGSSubPictureReference) * ctx->presentation.object_count);
    if (!ctx->presentation.objects) {
        ctx->presentation.object_count = 0;
        return;
    }

    for (object_index = 0; object_index < ctx->presentation.object_count; ++object_index) {
        PGSSubPictureReference *reference = &ctx->presentation.objects[object_index];
        reference->picture_id             = bytestream_get_be16(&buf);

        /* Skip window_id_ref */
        buf++;
        /* composition_flag (0x80 - object cropped, 0x40 - object forced) */
        reference->composition = bytestream_get_byte(&buf);

        reference->x = bytestream_get_be16(&buf);
        reference->y = bytestream_get_be16(&buf);

        /* TODO If cropping, cropping_x, cropping_y, cropping_width, cropping_height (all 2 bytes).*/
        av_dlog(avctx, "Subtitle Placement ID=%d, x=%d, y=%d\n", reference->picture_id, reference->x, reference->y);

        if (reference->x > avctx->width || reference->y > avctx->height) {
            av_log(avctx, AV_LOG_ERROR, "Subtitle out of video bounds. x = %d, y = %d, video width = %d, video height = %d.\n",
                   reference->x, reference->y, avctx->width, avctx->height);
            reference->x = 0;
            reference->y = 0;
        }
    }
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
 * @todo TODO: Fix start time, relies on correct PTS, currently too late
 *
 * @todo TODO: Fix end time, normally cleared by a second display
 * @todo       segment, which is currently ignored as it clears
 * @todo       the subtitle too early.
 */
static int display_end_segment(AVCodecContext *avctx, void *data,
                               const uint8_t *buf, int buf_size)
{
    AVSubtitle    *sub = data;
    PGSSubContext *ctx = avctx->priv_data;
    int64_t pts;

    uint16_t rect;

    /*
     *      The end display time is a timeout value and is only reached
     *      if the next subtitle is later than timeout or subtitle has
     *      not been cleared by a subsequent empty display command.
     */

    pts = ctx->pts != AV_NOPTS_VALUE ? ctx->pts : sub->pts;
    memset(sub, 0, sizeof(*sub));
    sub->pts = pts;
    ctx->pts = AV_NOPTS_VALUE;

    // Blank if last object_count was 0.
    if (!ctx->presentation.object_count)
        return 1;

    sub->start_display_time = 0;
    sub->end_display_time   = 20000;
    sub->format             = 0;

    sub->num_rects = ctx->presentation.object_count;
    sub->rects     = av_mallocz(sizeof(*sub->rects) * sub->num_rects);

    for (rect = 0; rect < sub->num_rects; ++rect) {
        uint16_t picture_id    = ctx->presentation.objects[rect].picture_id;
        sub->rects[rect]       = av_mallocz(sizeof(*sub->rects[rect]));
        sub->rects[rect]->x    = ctx->presentation.objects[rect].x;
        sub->rects[rect]->y    = ctx->presentation.objects[rect].y;
        sub->rects[rect]->w    = ctx->pictures[picture_id].w;
        sub->rects[rect]->h    = ctx->pictures[picture_id].h;
        sub->rects[rect]->type = SUBTITLE_BITMAP;

        /* Process bitmap */
        sub->rects[rect]->pict.linesize[0] = ctx->pictures[picture_id].w;
        if (ctx->pictures[picture_id].rle) {
            if (ctx->pictures[picture_id].rle_remaining_len)
                av_log(avctx, AV_LOG_ERROR, "RLE data length %u is %u bytes shorter than expected\n",
                       ctx->pictures[picture_id].rle_data_len, ctx->pictures[picture_id].rle_remaining_len);
            if (decode_rle(avctx, sub, rect, ctx->pictures[picture_id].rle, ctx->pictures[picture_id].rle_data_len) < 0)
                return 0;
        }

        /* Allocate memory for colors */
        sub->rects[rect]->nb_colors    = 256;
        sub->rects[rect]->pict.data[1] = av_mallocz(AVPALETTE_SIZE);

        /* Copy the forced flag */
        sub->rects[rect]->forced = (ctx->presentation.objects[rect].composition & 0x40) != 0;

        if (!ctx->forced_subs_only || ctx->presentation.objects[rect].composition & 0x40)
        memcpy(sub->rects[rect]->pict.data[1], ctx->clut, sub->rects[rect]->nb_colors * sizeof(uint32_t));
    }

    return 1;
}

static int decode(AVCodecContext *avctx, void *data, int *data_size,
                  AVPacket *avpkt)
{
    PGSSubContext *ctx = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AVSubtitle *sub    = data;

    const uint8_t *buf_end;
    uint8_t       segment_type;
    int           segment_length;
    int i;

    av_dlog(avctx, "PGS sub packet:\n");

    for (i = 0; i < buf_size; i++) {
        av_dlog(avctx, "%02x ", buf[i]);
        if (i % 16 == 15)
            av_dlog(avctx, "\n");
    }

    if (i & 15)
        av_dlog(avctx, "\n");

    *data_size = 0;

    /* Ensure that we have received at a least a segment code and segment length */
    if (buf_size < 3)
        return -1;

    buf_end = buf + buf_size;

    /* Step through buffer to identify segments */
    while (buf < buf_end) {
        segment_type   = bytestream_get_byte(&buf);
        segment_length = bytestream_get_be16(&buf);

        av_dlog(avctx, "Segment Length %d, Segment Type %x\n", segment_length, segment_type);

        if (segment_type != DISPLAY_SEGMENT && segment_length > buf_end - buf)
            break;

        switch (segment_type) {
        case PALETTE_SEGMENT:
            parse_palette_segment(avctx, buf, segment_length);
            break;
        case PICTURE_SEGMENT:
            parse_picture_segment(avctx, buf, segment_length);
            break;
        case PRESENTATION_SEGMENT:
            parse_presentation_segment(avctx, buf, segment_length);
            ctx->pts = sub->pts;
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
            *data_size = display_end_segment(avctx, data, buf, segment_length);
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "Unknown subtitle segment type 0x%x, length %d\n",
                   segment_type, segment_length);
            break;
        }

        buf += segment_length;
    }

    return buf_size;
}

#define OFFSET(x) offsetof(PGSSubContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"forced_subs_only", "Only show forced subtitles", OFFSET(forced_subs_only), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, SD},
    { NULL },
};

static const AVClass pgsdec_class = {
    .class_name = "PGS subtitle decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_pgssub_decoder = {
    .name           = "pgssub",
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_HDMV_PGS_SUBTITLE,
    .priv_data_size = sizeof(PGSSubContext),
    .init           = init_decoder,
    .close          = close_decoder,
    .decode         = decode,
    .long_name      = NULL_IF_CONFIG_SMALL("HDMV Presentation Graphic Stream subtitles"),
    .priv_class     = &pgsdec_class,
};
