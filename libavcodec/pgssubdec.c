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
#include "libavcore/imgutils.h"

//#define DEBUG_PACKET_CONTENTS

#define RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

enum SegmentType {
    PALETTE_SEGMENT      = 0x14,
    PICTURE_SEGMENT      = 0x15,
    PRESENTATION_SEGMENT = 0x16,
    WINDOW_SEGMENT       = 0x17,
    DISPLAY_SEGMENT      = 0x80,
};

typedef struct PGSSubPresentation {
    int x;
    int y;
    int id_number;
    int object_number;
} PGSSubPresentation;

typedef struct PGSSubPicture {
    int          w;
    int          h;
    uint8_t      *rle;
    unsigned int rle_buffer_size, rle_data_len;
    unsigned int rle_remaining_len;
} PGSSubPicture;

typedef struct PGSSubContext {
    PGSSubPresentation presentation;
    uint32_t           clut[256];
    PGSSubPicture      picture;
} PGSSubContext;

static av_cold int init_decoder(AVCodecContext *avctx)
{
    avctx->pix_fmt = PIX_FMT_PAL8;

    return 0;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    PGSSubContext *ctx = avctx->priv_data;

    av_freep(&ctx->picture.rle);
    ctx->picture.rle_buffer_size  = 0;

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
static int decode_rle(AVCodecContext *avctx, AVSubtitle *sub,
                      const uint8_t *buf, unsigned int buf_size)
{
    const uint8_t *rle_bitmap_end;
    int pixel_count, line_count;

    rle_bitmap_end = buf + buf_size;

    sub->rects[0]->pict.data[0] = av_malloc(sub->rects[0]->w * sub->rects[0]->h);

    if (!sub->rects[0]->pict.data[0])
        return -1;

    pixel_count = 0;
    line_count  = 0;

    while (buf < rle_bitmap_end && line_count < sub->rects[0]->h) {
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

        if (run > 0 && pixel_count + run <= sub->rects[0]->w * sub->rects[0]->h) {
            memset(sub->rects[0]->pict.data[0] + pixel_count, color, run);
            pixel_count += run;
        } else if (!run) {
            /*
             * New Line. Check if correct pixels decoded, if not display warning
             * and adjust bitmap pointer to correct new line position.
             */
            if (pixel_count % sub->rects[0]->w > 0)
                av_log(avctx, AV_LOG_ERROR, "Decoded %d pixels, when line should be %d pixels\n",
                       pixel_count % sub->rects[0]->w, sub->rects[0]->w);
            line_count++;
        }
    }

    if (pixel_count < sub->rects[0]->w * sub->rects[0]->h) {
        av_log(avctx, AV_LOG_ERROR, "Insufficient RLE data for subtitle\n");
        return -1;
    }

    dprintf(avctx, "Pixel Count = %d, Area = %d\n", pixel_count, sub->rects[0]->w * sub->rects[0]->h);

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

    if (buf_size <= 4)
        return -1;
    buf_size -= 4;

    /* skip 3 unknown bytes: Object ID (2 bytes), Version Number */
    buf += 3;

    /* Read the Sequence Description to determine if start of RLE data or appended to previous RLE */
    sequence_desc = bytestream_get_byte(&buf);

    if (!(sequence_desc & 0x80)) {
        /* Additional RLE data */
        if (buf_size > ctx->picture.rle_remaining_len)
            return -1;

        memcpy(ctx->picture.rle + ctx->picture.rle_data_len, buf, buf_size);
        ctx->picture.rle_data_len += buf_size;
        ctx->picture.rle_remaining_len -= buf_size;

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
        av_log(avctx, AV_LOG_ERROR, "Bitmap dimensions larger then video.\n");
        return -1;
    }

    ctx->picture.w = width;
    ctx->picture.h = height;

    av_fast_malloc(&ctx->picture.rle, &ctx->picture.rle_buffer_size, rle_bitmap_len);

    if (!ctx->picture.rle)
        return -1;

    memcpy(ctx->picture.rle, buf, buf_size);
    ctx->picture.rle_data_len = buf_size;
    ctx->picture.rle_remaining_len = rle_bitmap_len - buf_size;

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
        cb        = bytestream_get_byte(&buf);
        cr        = bytestream_get_byte(&buf);
        alpha     = bytestream_get_byte(&buf);

        YUV_TO_RGB1(cb, cr);
        YUV_TO_RGB2(r, g, b, y);

        dprintf(avctx, "Color %d := (%d,%d,%d,%d)\n", color_id, r, g, b, alpha);

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
 * @todo TODO: Implement forcing of subtitles
 */
static void parse_presentation_segment(AVCodecContext *avctx,
                                       const uint8_t *buf, int buf_size)
{
    PGSSubContext *ctx = avctx->priv_data;

    int x, y;

    int w = bytestream_get_be16(&buf);
    int h = bytestream_get_be16(&buf);

    dprintf(avctx, "Video Dimensions %dx%d\n",
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

    ctx->presentation.object_number = bytestream_get_byte(&buf);
    if (!ctx->presentation.object_number)
        return;

    /*
     * Skip 4 bytes of unknown:
     *     object_id_ref (2 bytes),
     *     window_id_ref,
     *     composition_flag (0x80 - object cropped, 0x40 - object forced)
     */
    buf += 4;

    x = bytestream_get_be16(&buf);
    y = bytestream_get_be16(&buf);

    /* TODO If cropping, cropping_x, cropping_y, cropping_width, cropping_height (all 2 bytes).*/

    dprintf(avctx, "Subtitle Placement x=%d, y=%d\n", x, y);

    if (x > avctx->width || y > avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "Subtitle out of video bounds. x = %d, y = %d, video width = %d, video height = %d.\n",
               x, y, avctx->width, avctx->height);
        x = 0; y = 0;
    }

    /* Fill in dimensions */
    ctx->presentation.x = x;
    ctx->presentation.y = y;
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

    /*
     *      The end display time is a timeout value and is only reached
     *      if the next subtitle is later then timeout or subtitle has
     *      not been cleared by a subsequent empty display command.
     */

    memset(sub, 0, sizeof(*sub));
    // Blank if last object_number was 0.
    // Note that this may be wrong for more complex subtitles.
    if (!ctx->presentation.object_number)
        return 1;
    sub->start_display_time = 0;
    sub->end_display_time   = 20000;
    sub->format             = 0;

    sub->rects     = av_mallocz(sizeof(*sub->rects));
    sub->rects[0]  = av_mallocz(sizeof(*sub->rects[0]));
    sub->num_rects = 1;

    sub->rects[0]->x    = ctx->presentation.x;
    sub->rects[0]->y    = ctx->presentation.y;
    sub->rects[0]->w    = ctx->picture.w;
    sub->rects[0]->h    = ctx->picture.h;
    sub->rects[0]->type = SUBTITLE_BITMAP;

    /* Process bitmap */
    sub->rects[0]->pict.linesize[0] = ctx->picture.w;

    if (ctx->picture.rle) {
        if (ctx->picture.rle_remaining_len)
            av_log(avctx, AV_LOG_ERROR, "RLE data length %u is %u bytes shorter than expected\n",
                   ctx->picture.rle_data_len, ctx->picture.rle_remaining_len);
        if(decode_rle(avctx, sub, ctx->picture.rle, ctx->picture.rle_data_len) < 0)
            return 0;
    }
    /* Allocate memory for colors */
    sub->rects[0]->nb_colors    = 256;
    sub->rects[0]->pict.data[1] = av_mallocz(AVPALETTE_SIZE);

    memcpy(sub->rects[0]->pict.data[1], ctx->clut, sub->rects[0]->nb_colors * sizeof(uint32_t));

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

#ifdef DEBUG_PACKET_CONTENTS
    int i;

    av_log(avctx, AV_LOG_INFO, "PGS sub packet:\n");

    for (i = 0; i < buf_size; i++) {
        av_log(avctx, AV_LOG_INFO, "%02x ", buf[i]);
        if (i % 16 == 15)
            av_log(avctx, AV_LOG_INFO, "\n");
    }

    if (i & 15)
        av_log(avctx, AV_LOG_INFO, "\n");
#endif

    *data_size = 0;

    /* Ensure that we have received at a least a segment code and segment length */
    if (buf_size < 3)
        return -1;

    buf_end = buf + buf_size;

    /* Step through buffer to identify segments */
    while (buf < buf_end) {
        segment_type   = bytestream_get_byte(&buf);
        segment_length = bytestream_get_be16(&buf);

        dprintf(avctx, "Segment Length %d, Segment Type %x\n", segment_length, segment_type);

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
            break;
        case WINDOW_SEGMENT:
            /*
             * Window Segment Structure (No new information provided):
             *     2 bytes: Unkown,
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

AVCodec pgssub_decoder = {
    "pgssub",
    AVMEDIA_TYPE_SUBTITLE,
    CODEC_ID_HDMV_PGS_SUBTITLE,
    sizeof(PGSSubContext),
    init_decoder,
    NULL,
    close_decoder,
    decode,
    .long_name = NULL_IF_CONFIG_SMALL("HDMV Presentation Graphic Stream subtitles"),
};
