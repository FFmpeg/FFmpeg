/*
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol
 * Copyright (c) 2006 Baptiste Coudurier
 * Copyright (c) 2018 Bjorn Roche
 * Copyright (c) 2018 Paul B Mahol
 *
 * first version by Francois Revol <revol@free.fr>
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
 * GIF encoder
 * @see http://www.w3.org/Graphics/GIF/spec-gif89a.txt
 */

#define BITSTREAM_WRITER_LE
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "gif.h"
#include "giflossy.h"

#include "put_bits.h"

#define DEFAULT_TRANSPARENCY_INDEX 0x1f

typedef struct GIFContext {
    const AVClass *class;
    void *lossy;
    uint8_t *buf;
    int buf_size;
    AVFrame *last_frame;
    int flags;
    int image;
    uint32_t palette[AVPALETTE_COUNT];  ///< local reference palette for !pal8
    int palette_loaded;
    int transparent_index;
    uint8_t *tmpl;                      ///< temporary line buffer
} GIFContext;

enum {
    GF_OFFSETTING = 1<<0,
    GF_TRANSDIFF  = 1<<1,
};

static int is_image_translucent(AVCodecContext *avctx,
                                const uint8_t *buf, const int linesize)
{
    GIFContext *s = avctx->priv_data;
    int trans = s->transparent_index;

    if (trans < 0)
        return 0;

    for (int y = 0; y < avctx->height; y++) {
        for (int x = 0; x < avctx->width; x++) {
            if (buf[x] == trans) {
                return 1;
            }
        }
        buf += linesize;
    }

    return 0;
}

static int get_palette_transparency_index(const uint32_t *palette)
{
    int transparent_color_index = -1;
    unsigned i, smallest_alpha = 0xff;

    if (!palette)
        return -1;

    for (i = 0; i < AVPALETTE_COUNT; i++) {
        const uint32_t v = palette[i];
        if (v >> 24 < smallest_alpha) {
            smallest_alpha = v >> 24;
            transparent_color_index = i;
        }
    }
    return smallest_alpha < 128 ? transparent_color_index : -1;
}

static int pick_palette_entry(const uint8_t *buf, int linesize, int w, int h)
{
    int histogram[AVPALETTE_COUNT] = {0};
    int x, y, i;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++)
            histogram[buf[x]]++;
        buf += linesize;
    }
    for (i = 0; i < FF_ARRAY_ELEMS(histogram); i++)
        if (!histogram[i])
            return i;
    return -1;
}

static void gif_crop_translucent(AVCodecContext *avctx,
                                 const uint8_t *buf, const int linesize,
                                 int *width, int *height,
                                 int *x_start, int *y_start)
{
    GIFContext *s = avctx->priv_data;
    int trans = s->transparent_index;

    /* Crop image */
    if ((s->flags & GF_OFFSETTING) && trans >= 0) {
        const int w = avctx->width;
        const int h = avctx->height;
        int x_end = w - 1,
            y_end = h - 1;

        // crop top
        while (*y_start < y_end) {
            int is_trans = 1;
            for (int i = 0; i < w; i++) {
                if (buf[w * *y_start + i] != trans) {
                    is_trans = 0;
                    break;
                }
            }

            if (!is_trans)
                break;
            (*y_start)++;
        }

        // crop bottom
        while (y_end < h) {
            int is_trans = 1;
            for (int i = 0; i < w; i++) {
                if (buf[w * y_end + i] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            y_end--;
        }

        // crop left
        while (*x_start < x_end) {
            int is_trans = 1;
            for (int i = *y_start; i < y_end; i++) {
                if (buf[w * i + *x_start] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            (*x_start)++;
        }

        // crop right
        while (x_end < w) {
            int is_trans = 1;
            for (int i = *y_start; i < y_end; i++) {
                if (buf[w * i + x_end] != trans) {
                    is_trans = 0;
                    break;
                }
            }
            if (!is_trans)
                break;
            x_end--;
        }

        *height = y_end + 1 - *y_start;
        *width  = x_end + 1 - *x_start;
        av_log(avctx, AV_LOG_DEBUG,"%dx%d image at pos (%d;%d) [area:%dx%d]\n",
               *width, *height, *x_start, *y_start, avctx->width, avctx->height);
    }
}

static void gif_crop_opaque(AVCodecContext *avctx,
                            const uint32_t *palette,
                            const uint8_t *buf, const int linesize,
                            int *width, int *height, int *x_start, int *y_start)
{
    GIFContext *s = avctx->priv_data;

    /* Crop image */
    if ((s->flags & GF_OFFSETTING) && s->last_frame && !palette) {
        const uint8_t *ref = s->last_frame->data[0];
        const int ref_linesize = s->last_frame->linesize[0];
        int x_end = avctx->width  - 1,
            y_end = avctx->height - 1;

        /* skip common lines */
        while (*y_start < y_end) {
            if (memcmp(ref + *y_start*ref_linesize, buf + *y_start*linesize, *width))
                break;
            (*y_start)++;
        }
        while (y_end > *y_start) {
            if (memcmp(ref + y_end*ref_linesize, buf + y_end*linesize, *width))
                break;
            y_end--;
        }
        *height = y_end + 1 - *y_start;

        /* skip common columns */
        while (*x_start < x_end) {
            int same_column = 1;
            for (int y = *y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + *x_start] != buf[y*linesize + *x_start]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            (*x_start)++;
        }
        while (x_end > *x_start) {
            int same_column = 1;
            for (int y = *y_start; y <= y_end; y++) {
                if (ref[y*ref_linesize + x_end] != buf[y*linesize + x_end]) {
                    same_column = 0;
                    break;
                }
            }
            if (!same_column)
                break;
            x_end--;
        }
        *width = x_end + 1 - *x_start;

        av_log(avctx, AV_LOG_DEBUG,"%dx%d image at pos (%d;%d) [area:%dx%d]\n",
               *width, *height, *x_start, *y_start, avctx->width, avctx->height);
    }
}

static int ff_lossy_write_compressed_data(Gif_Colormap *gfcm, Gif_Image *gfi, int min_code_bits, int loss, uint8_t **bytestream, uint8_t *end);

static int gif_image_write_image(AVCodecContext *avctx,
                                 uint8_t **bytestream, uint8_t *end,
                                 const uint32_t *local_palette,
                                 const uint8_t *buf, const int linesize,
                                 AVPacket *pkt)
{
    GIFContext *s = avctx->priv_data;
    int disposal, len = 0, height = avctx->height, width = avctx->width, x, y;
    int x_start = 0, y_start = 0, trans = s->transparent_index;
    int bcid = -1, honor_transparency = (s->flags & GF_TRANSDIFF) && s->last_frame && !local_palette;


    if (!s->image && avctx->frame_number && is_image_translucent(avctx, buf, linesize)) {
        gif_crop_translucent(avctx, buf, linesize, &width, &height, &x_start, &y_start);
        honor_transparency = 0;
        disposal = GCE_DISPOSAL_BACKGROUND;
    } else {
        gif_crop_opaque(avctx, local_palette, buf, linesize, &width, &height, &x_start, &y_start);
        disposal = GCE_DISPOSAL_INPLACE;
    }

    if (s->image || !avctx->frame_number) { /* GIF header */
        const uint32_t *global_palette = local_palette ? local_palette : s->palette;
        const AVRational sar = avctx->sample_aspect_ratio;
        int64_t aspect = 0;

        if (sar.num > 0 && sar.den > 0) {
            aspect = sar.num * 64LL / sar.den - 15;
            if (aspect < 0 || aspect > 255)
                aspect = 0;
        }

        bytestream_put_buffer(bytestream, gif89a_sig, sizeof(gif89a_sig));
        bytestream_put_le16(bytestream, avctx->width);
        bytestream_put_le16(bytestream, avctx->height);

        bcid = get_palette_transparency_index(global_palette);

        bytestream_put_byte(bytestream, 0xf7); /* flags: global clut, 256 entries */
        bytestream_put_byte(bytestream, bcid < 0 ? DEFAULT_TRANSPARENCY_INDEX : bcid); /* background color index */
        bytestream_put_byte(bytestream, aspect);
        for (int i = 0; i < 256; i++) {
            const uint32_t v = global_palette[i] & 0xffffff;
            bytestream_put_be24(bytestream, v);
        }
    }

    if (honor_transparency && trans < 0) {
        trans = pick_palette_entry(buf + y_start*linesize + x_start,
                                   linesize, width, height);
        if (trans < 0) // TODO, patch welcome
            av_log(avctx, AV_LOG_DEBUG, "No available color, can not use transparency\n");
    }

    if (trans < 0)
        honor_transparency = 0;

    bcid = honor_transparency || disposal == GCE_DISPOSAL_BACKGROUND ? trans : get_palette_transparency_index(local_palette);

    /* graphic control extension */
    bytestream_put_byte(bytestream, GIF_EXTENSION_INTRODUCER);
    bytestream_put_byte(bytestream, GIF_GCE_EXT_LABEL);
    bytestream_put_byte(bytestream, 0x04); /* block size */
    bytestream_put_byte(bytestream, disposal<<2 | (bcid >= 0));
    bytestream_put_le16(bytestream, 5); // default delay
    bytestream_put_byte(bytestream, bcid < 0 ? DEFAULT_TRANSPARENCY_INDEX : bcid);
    bytestream_put_byte(bytestream, 0x00);

    /* image block */
    bytestream_put_byte(bytestream, GIF_IMAGE_SEPARATOR);
    bytestream_put_le16(bytestream, x_start);
    bytestream_put_le16(bytestream, y_start);
    bytestream_put_le16(bytestream, width);
    bytestream_put_le16(bytestream, height);

    if (!local_palette) {
        bytestream_put_byte(bytestream, 0x00); /* flags */
    } else {
        unsigned i;
        bytestream_put_byte(bytestream, 1<<7 | 0x7); /* flags */
        for (i = 0; i < AVPALETTE_COUNT; i++) {
            const uint32_t v = local_palette[i];
            bytestream_put_be24(bytestream, v);
        }
    }


    assert(!honor_transparency);

    fprintf(stderr, "linesize=%d, width=%d\n", linesize, width);

    Gif_Image gfi = {
        .image_data = buf + y_start*linesize + x_start,
        .width = width,
        .height = height,
        .linesize = linesize,
    };
    Gif_Color stub_palette[AVPALETTE_COUNT];
    const uint32_t *palette = local_palette ? local_palette : s->palette;
    for(int i=0; i < AVPALETTE_COUNT; i++) {
        stub_palette[i] = (Gif_Color){
            .gfc_red = palette[i]>>16, .gfc_green = palette[i]>>8, .gfc_blue = palette[i],
        };
    }
    Gif_Colormap gfcm = {
        .ncol = 256,
        .col = stub_palette,
    };
    ff_lossy_write_compressed_data(&gfcm, &gfi, 8, 10000, bytestream, end);

    const uint8_t *ptr = s->buf;
    while (len > 0) {
        int size = FFMIN(255, len);
        bytestream_put_byte(bytestream, size);
        if (end - *bytestream < size)
            return -1;
        bytestream_put_buffer(bytestream, ptr, size);
        ptr += size;
        len -= size;
    }
    bytestream_put_byte(bytestream, 0x00); /* end of image block */
    return 0;
}

static av_cold int giflossy_encode_init(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    if (avctx->width > 65535 || avctx->height > 65535) {
        av_log(avctx, AV_LOG_ERROR, "GIF does not support resolutions above 65535x65535\n");
        return AVERROR(EINVAL);
    }

    s->transparent_index = -1;

    s->lossy = 0;
    s->buf_size = avctx->width*avctx->height*2 + 1000;
    s->buf = av_malloc(s->buf_size);
    s->tmpl = av_malloc(avctx->width);
    if (!s->tmpl || !s->buf)
        return AVERROR(ENOMEM);

    for (int i = 0; i < 256; i++) {
        int r=i, g=i, b=i;
        s->palette[i] = (rand() << 16) ^ rand();//b + (g << 8) + (r << 16) + (0xFFU << 24);
    }

    return 0;
}

static int giflossy_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                            const AVFrame *pict, int *got_packet)
{
    GIFContext *s = avctx->priv_data;
    uint8_t *outbuf_ptr, *end;
    const uint32_t *local_palette = NULL;
    int ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, avctx->width*avctx->height*7/5 + AV_INPUT_BUFFER_MIN_SIZE, 0)) < 0)
        return ret;
    outbuf_ptr = pkt->data;
    end        = pkt->data + pkt->size;



    gif_image_write_image(avctx, &outbuf_ptr, end, local_palette,
                          pict->data[0], pict->linesize[0], pkt);
    if (!s->last_frame && !s->image) {
        s->last_frame = av_frame_alloc();
        if (!s->last_frame)
            return AVERROR(ENOMEM);
    }

    if (!s->image) {
        av_frame_unref(s->last_frame);
        ret = av_frame_ref(s->last_frame, (AVFrame*)pict);
        if (ret < 0)
            return ret;
    }

    pkt->size   = outbuf_ptr - pkt->data;
    if (s->image || !avctx->frame_number)
        pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static int giflossy_encode_close(AVCodecContext *avctx)
{
    GIFContext *s = avctx->priv_data;

    av_freep(&s->buf);
    s->buf_size = 0;
    av_frame_free(&s->last_frame);
    av_freep(&s->tmpl);
    return 0;
}

static const AVClass giflossy_class = {
    .class_name = "GIF lossy encoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_giflossy_encoder = {
    .name           = "giflossy",
    .long_name      = NULL_IF_CONFIG_SMALL("GIF encoder with lossy LZW"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_GIF,
    .priv_data_size = sizeof(GIFContext),
    .init           = giflossy_encode_init,
    .encode2        = giflossy_encode_frame,
    .close          = giflossy_encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_RGBA, AV_PIX_FMT_NONE
    },
    .priv_class     = &giflossy_class,
};


static inline void
gfc_delete(Gif_CodeTable *gfc)
{
    Gif_DeleteArray(gfc->nodes);
    Gif_DeleteArray(gfc->links);
}

static inline void
gfc_reinit(Gif_CodeTable *gfc, Gif_Code clear_code)
{
  if (!gfc->nodes) gfc->nodes = Gif_NewArray(Gif_Node, NODES_SIZE);
  if (!gfc->links) gfc->links = Gif_NewArray(Gif_Node*, LINKS_SIZE);

  int c;
  /* The first clear_code nodes are reserved for single-pixel codes */
  gfc->nodes_pos = clear_code;
  gfc->links_pos = 0;
  for (c = 0; c < clear_code; c++) {
    gfc->nodes[c].code = c;
    gfc->nodes[c].type = LINKS_TYPE;
    gfc->nodes[c].suffix = c;
    gfc->nodes[c].child.s = 0;
  }
  gfc->clear_code = clear_code;
}

static inline unsigned int color_diff(Gif_Color a, Gif_Color b, int a_transaprent, int b_transparent, gfc_rgbdiff dither);

static inline Gif_RGBA rgba_color_at_pos(const Gif_Image *gfi, unsigned pos)
{
    const unsigned pixel_size = 4;
    if (gfi->width * pixel_size != gfi->linesize) {
        abort();
        // unsigned y = pos / gfi->width, x = pos - y * gfi->width;
        // return gfi->image_data[y * gfi->linesize + x];
    }
    return ((Gif_RGBA*)gfi->image_data)[pos];
}

static inline uint8_t gif_pixel_at_pos(const Gif_Colormap *gfcm, const Gif_Image *gfi, unsigned pos, gfc_rgbdiff dither)
{
    Gif_RGBA rgba = rgba_color_at_pos(gfi, pos);
    int is_transparent = rgba.a < 128;
    Gif_Color color = {rgba.r, rgba.g, rgba.b};

    // TODO: when using dispose=keep, compare to background pixel color if the source is transparent
    int best = 0;
    int min_diff = color_diff(color, gfcm->col[0], is_transparent, gfi->transparent == 0, dither);
    for(int i = 1; i < gfcm->ncol; i++) {
        int diff = color_diff(color, gfcm->col[i], is_transparent, gfi->transparent == i, dither);
        if (diff < min_diff) {
            min_diff = diff;
            best = i;
        }
    }
    return best;
}


struct selected_node {
  Gif_Node *node; /* which node has been chosen by gfc_lookup_lossy */
  unsigned long pos, /* where the node ends */
  diff; /* what is the overall quality loss for that node */
  gfc_rgbdiff dither;
};


/* Difference (MSE) between given color indexes + dithering error */
static inline unsigned int color_diff(Gif_Color a, Gif_Color b, int a_transaprent, int b_transparent, gfc_rgbdiff dither)
{
  /* if one is transparent and the other is not, then return maximum difference */
  /* TODO: figure out what color is in the canvas under the transparent pixel and match against that */
  if (a_transaprent != b_transparent) return 1<<25;

  /* Two transparent colors are identical */
  if (a_transaprent) return 0;

  /* squared error with or without dithering. */
  unsigned int dith = (a.gfc_red-b.gfc_red+dither.r)*(a.gfc_red-b.gfc_red+dither.r)
  + (a.gfc_green-b.gfc_green+dither.g)*(a.gfc_green-b.gfc_green+dither.g)
  + (a.gfc_blue-b.gfc_blue+dither.b)*(a.gfc_blue-b.gfc_blue+dither.b);

  return dith;
}


/* difference between expected color a+dither and color b (used to calculate dithering required) */
static inline gfc_rgbdiff diffused_difference(Gif_Color a, Gif_Color b, int a_transaprent, int b_transaprent, gfc_rgbdiff dither)
{
  if (a_transaprent || b_transaprent) return (gfc_rgbdiff){0,0,0};

  return (gfc_rgbdiff) {
    a.gfc_red - b.gfc_red + dither.r * 3/4,
    a.gfc_green - b.gfc_green + dither.g * 3/4,
    a.gfc_blue - b.gfc_blue + dither.b * 3/4,
  };
}

static inline void
gfc_lookup_lossy_try_node(Gif_CodeTable *gfc, const Gif_Colormap *gfcm, Gif_Image *gfi,
  unsigned pos, Gif_Node *node, uint8_t suffix, uint8_t next_suffix,
  gfc_rgbdiff dither, unsigned long base_diff, const unsigned int max_diff, struct selected_node *best_t);

/* Recursive loop
 * Find node that is descendant of node (or start new search if work_node is null) that best matches pixels starting at pos
 * base_diff and dither are distortion from search made so far */
static struct selected_node
gfc_lookup_lossy(Gif_CodeTable *gfc, const Gif_Colormap *gfcm, Gif_Image *gfi,
  unsigned pos, Gif_Node *node, unsigned long base_diff, gfc_rgbdiff dither, const unsigned int max_diff)
{
  unsigned image_endpos = gfi->width * gfi->height;

  struct selected_node best_t = {node, pos, base_diff, dither};
  if (pos >= image_endpos) return best_t;

  uint8_t suffix = gif_pixel_at_pos(gfcm, gfi, pos, dither);
  assert(!node || (node >= gfc->nodes && node < gfc->nodes + NODES_SIZE));
  assert(suffix < gfc->clear_code);
  if (!node) {
    /* prefix of the new node must be same as suffix of previously added node */
    // TODO: calculate dither vs previous pos
    return gfc_lookup_lossy(gfc, gfcm, gfi, pos+1, &gfc->nodes[suffix], base_diff, (gfc_rgbdiff){0,0,0}, max_diff);
  }

  /* search all nodes that are less than max_diff different from the desired pixel */
  if (node->type == TABLE_TYPE) {
    int i;
    for(i=0; i < gfc->clear_code; i++) {
      if (!node->child.m[i]) continue;
      gfc_lookup_lossy_try_node(gfc, gfcm, gfi, pos, node->child.m[i], suffix, i, dither, base_diff, max_diff, &best_t);
    }
  }
  else {
    for (node = node->child.s; node; node = node->sibling) {
      gfc_lookup_lossy_try_node(gfc, gfcm, gfi, pos, node, suffix, node->suffix, dither, base_diff, max_diff, &best_t);
    }
  }

  return best_t;
}

/**
 * Replaces best_t with a new node if it's better
 *
 * @param node        Current node to search
 * @param suffix      Previous pixel
 * @param next_suffix Next pixel to evaluate (must correspond to the node given)
 * @param dither      Desired dithering
 * @param base_diff   Difference accumulated in the search so far
 * @param max_diff    Maximum allowed pixel difference
 * @param best_t      Current best candidate (input/output argument)
 */
static inline void
gfc_lookup_lossy_try_node(Gif_CodeTable *gfc, const Gif_Colormap *gfcm, Gif_Image *gfi,
  unsigned pos, Gif_Node *node, uint8_t suffix, uint8_t next_suffix,
  gfc_rgbdiff dither, unsigned long base_diff, const unsigned int max_diff, struct selected_node *best_t)
{
  unsigned int diff = suffix == next_suffix ? 0 : color_diff(gfcm->col[suffix], gfcm->col[next_suffix], suffix == gfi->transparent, next_suffix == gfi->transparent, dither);
  if (diff <= max_diff) {
    gfc_rgbdiff new_dither = diffused_difference(gfcm->col[suffix], gfcm->col[next_suffix], suffix == gfi->transparent, next_suffix == gfi->transparent, dither);
    /* if the candidate pixel is good enough, check all possible continuations of that dictionary string */
    struct selected_node t = gfc_lookup_lossy(gfc, gfcm, gfi, pos+1, node, base_diff + diff, new_dither, max_diff);

    /* search is biased towards finding longest candidate that is below treshold rather than a match with minimum average error */
    if (t.pos > best_t->pos || (t.pos == best_t->pos && t.diff < best_t->diff)) {
      *best_t = t;
    }
  }
}

static void
gfc_change_node_to_table(Gif_CodeTable *gfc, Gif_Node *work_node,
                         Gif_Node *next_node)
{
  /* change links node to table node */
  Gif_Code c;
  Gif_Node **table = &gfc->links[gfc->links_pos];
  Gif_Node *n;
  gfc->links_pos += gfc->clear_code;

  for (c = 0; c < gfc->clear_code; c++)
    table[c] = 0;
  table[next_node->suffix] = next_node;
  for (n = work_node->child.s; n; n = n->sibling)
    table[n->suffix] = n;

  work_node->type = TABLE_TYPE;
  work_node->child.m = table;
}

static inline void
gfc_define(Gif_CodeTable *gfc, Gif_Node *work_node, uint8_t suffix,
           Gif_Code next_code)
{
  /* Add a new code to our dictionary. First reserve a node for the
     added code. It's LINKS_TYPE at first. */
  Gif_Node *next_node = &gfc->nodes[gfc->nodes_pos];
  gfc->nodes_pos++;
  next_node->code = next_code;
  next_node->type = LINKS_TYPE;
  next_node->suffix = suffix;
  next_node->child.s = 0;

  /* link next_node into work_node's set of children */
  if (work_node->type == TABLE_TYPE)
    work_node->child.m[suffix] = next_node;
  else if (work_node->type < MAX_LINKS_TYPE
           || gfc->links_pos + gfc->clear_code > LINKS_SIZE) {
    next_node->sibling = work_node->child.s;
    work_node->child.s = next_node;
    if (work_node->type < MAX_LINKS_TYPE)
      work_node->type++;
  } else
    gfc_change_node_to_table(gfc, work_node, next_node);
}

static int ff_lossy_write_compressed_data(Gif_Colormap *gfcm, Gif_Image *gfi, int min_code_bits, int loss, uint8_t **bytestream, uint8_t *end)
{
    assert(gfcm);
    assert(gfcm->col);

  Gif_CodeTable gfc = {0};

  uint8_t stack_buffer[512 - 24];
  uint8_t *buf = stack_buffer;
  unsigned bufpos = 0;
  unsigned bufcap = sizeof(stack_buffer) * 8;
  unsigned clear_bufpos, clear_pos;

  unsigned run = 0;
  const unsigned int RUN_EWMA_SHIFT = 4;
  const unsigned int RUN_EWMA_SCALE = 19;
  const unsigned int RUN_INV_THRESH = ((unsigned) (1 << RUN_EWMA_SCALE) / 3000);
  unsigned run_ewma = 0;
  Gif_Code next_code = 0;

  const Gif_Code CLEAR_CODE = (1 << min_code_bits);
  const Gif_Code EOI_CODE =   (CLEAR_CODE + 1);
#define CUR_BUMP_CODE   (1 << cur_code_bits)
  // grr->cleared = 0;

  int cur_code_bits = min_code_bits + 1;

  /* Here we go! */
  bytestream_put_byte(bytestream, min_code_bits);

  /* next_code set by first runthrough of output clear_code */
  GIF_DEBUG(("clear(%d) eoi(%d) bits(%d) ", CLEAR_CODE, EOI_CODE, cur_code_bits));

  Gif_Node *work_node = NULL;
  Gif_Code output_code = CLEAR_CODE;
  /* Because output_code is clear_code, we'll initialize next_code, et al.
     below. */

  unsigned pos = clear_pos = clear_bufpos = 0;
  unsigned image_endpos = gfi->height * gfi->width;

  fprintf(stderr, "main write loop\n");

  while (1) {

    /*****
     * Output 'output_code' to the memory buffer. */
    if (bufpos + 32 >= bufcap) {
      unsigned ncap = bufcap * 2 + (24 << 3);
      uint8_t *nbuf = Gif_NewArray(uint8_t, ncap >> 3);
      if (!nbuf)
        goto error;
      memcpy(nbuf, buf, bufcap >> 3);
      if (buf != stack_buffer)
        Gif_DeleteArray(buf);
      buf = nbuf;
      bufcap = ncap;
    }

    {
      unsigned endpos = bufpos + cur_code_bits;
      do {
        if (bufpos & 7)
          buf[bufpos >> 3] |= output_code << (bufpos & 7);
        else if (bufpos & 0x7FF)
          buf[bufpos >> 3] = output_code >> (bufpos - endpos + cur_code_bits);
        else {
          buf[bufpos >> 3] = 255;
          endpos += 8;
        }

        bufpos += 8 - (bufpos & 7);
      } while (bufpos < endpos);
      bufpos = endpos;
    }


    /*****
     * Handle special codes. */

    if (output_code == CLEAR_CODE) {
      /* Clear data and prepare gfc */
      cur_code_bits = min_code_bits + 1;
      next_code = EOI_CODE + 1;
      run_ewma = 1 << RUN_EWMA_SCALE;
      run = 0;
      gfc_reinit(&gfc, CLEAR_CODE);
      clear_pos = clear_bufpos = 0;

      GIF_DEBUG(("clear "));

    } else if (output_code == EOI_CODE)
      break;

    else {
      if (next_code > CUR_BUMP_CODE && cur_code_bits < GIF_MAX_CODE_BITS)
        /* bump up compression size */
        ++cur_code_bits;

      /* Adjust current run length average. */
      run = (run << RUN_EWMA_SCALE) + (1 << (RUN_EWMA_SHIFT - 1));
      if (run < run_ewma)
        run_ewma -= (run_ewma - run) >> RUN_EWMA_SHIFT;
      else
        run_ewma += (run - run_ewma) >> RUN_EWMA_SHIFT;

      /* Reset run length. */
      run = !!work_node;
    }


  gfc_rgbdiff dither = {0,0,0};
    /*****
     * Find the next code to output. */
    {
      struct selected_node t = gfc_lookup_lossy(&gfc, gfcm, gfi, pos, NULL, 0, dither, loss);
      dither = t.dither;

      work_node = t.node;
      run = t.pos - pos;
      pos = t.pos;

      if (pos < image_endpos) {
        /* Output the current code. */
        if (next_code < GIF_MAX_CODE) {
          gfc_define(&gfc, work_node, gif_pixel_at_pos(gfcm, gfi, pos, dither), next_code);
          next_code++;
        } else
          next_code = GIF_MAX_CODE + 1; /* to match "> CUR_BUMP_CODE" above */

        /* Check whether to clear table. */
        if (next_code > 4094) {
          int do_clear = 0;

          unsigned pixels_left = image_endpos - pos - 1;
          if (pixels_left) {
            /* Always clear if run_ewma gets small relative to
               min_code_bits. Otherwise, clear if #images/run is smaller
               than an empirical threshold, meaning it will take more than
               3000 or so average runs to complete the image. */
            if (run_ewma < ((36U << RUN_EWMA_SCALE) / min_code_bits)
                || pixels_left > UINT_MAX / RUN_INV_THRESH
                || run_ewma < pixels_left * RUN_INV_THRESH)
              do_clear = 1;
          }


          if ((do_clear || run < 7) && !clear_pos) {
            clear_pos = pos - run;
            clear_bufpos = bufpos;
          } else if (!do_clear && run > 50)
            clear_pos = clear_bufpos = 0;

          if (do_clear) {
            GIF_DEBUG(("rewind %u pixels/%d bits", pos + 1 - clear_pos, bufpos + cur_code_bits - clear_bufpos));
            output_code = CLEAR_CODE;
            pos = clear_pos;

            bufpos = clear_bufpos;
            buf[bufpos >> 3] &= (1 << (bufpos & 7)) - 1;
            // grr->cleared = 1;
            continue;
          }
        }

        /* Adjust current run length average. */
        run = (run << RUN_EWMA_SCALE) + (1 << (RUN_EWMA_SHIFT - 1));
        if (run < run_ewma)
          run_ewma -= (run_ewma - run) >> RUN_EWMA_SHIFT;
        else
          run_ewma += (run - run_ewma) >> RUN_EWMA_SHIFT;
      }

      output_code = (work_node ? work_node->code : EOI_CODE);
    }
  }

  /* Output memory buffer to stream. */
  bufpos = (bufpos + 7) >> 3;
  buf[(bufpos - 1) & 0xFFFFFF00] = (bufpos - 1) & 0xFF;
  buf[bufpos] = 0;
  bytestream_put_buffer(bytestream, buf, bufpos + 1);

  if (buf != stack_buffer) {
    Gif_DeleteArray(buf);
  }
  gfc_delete(&gfc);
  return 1;

 error:
  if (buf != stack_buffer) {
    Gif_DeleteArray(buf);
  }
  gfc_delete(&gfc);
  return 0;
}
