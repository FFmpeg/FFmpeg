/*
 * Id Quake II CIN Video Decoder
 * Copyright (C) 2003 the ffmpeg project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**
 * @file idcinvideo.c
 * Id Quake II Cin Video Decoder by Dr. Tim Ferguson
 * For more information about the Id CIN format, visit:
 *   http://www.csse.monash.edu.au/~timf/
 *
 * This video decoder outputs PAL8 colorspace data. Interacting with this
 * decoder is a little involved. During initialization, the demuxer must
 * transmit the 65536-byte Huffman table(s) to the decoder via extradata.
 * Then, whenever a palette change is encountered while demuxing the file,
 * the demuxer must use the same extradata space to transmit an
 * AVPaletteControl structure.
 *
 * Id CIN video is purely Huffman-coded, intraframe-only codec. It achieves
 * a little more compression by exploiting the fact that adjacent pixels
 * tend to be similar.
 *
 * Note that this decoder could use ffmpeg's optimized VLC facilities
 * rather than naive, tree-based Huffman decoding. However, there are 256
 * Huffman tables. Plus, the VLC bit coding order is right -> left instead
 * or left -> right, so all of the bits would have to be reversed. Further,
 * the original Quake II implementation likely used a similar naive
 * decoding algorithm and it worked fine on much lower spec machines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#define HUFFMAN_TABLE_SIZE 64 * 1024
#define HUF_TOKENS 256
#define PALETTE_COUNT 256

typedef struct
{
  int count;
  unsigned char used;
  int children[2];
} hnode_t;

typedef struct IdcinContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frame;

    unsigned char *buf;
    int size;

    hnode_t huff_nodes[256][HUF_TOKENS*2];
    int num_huff_nodes[256];

} IdcinContext;

/*
 * Find the lowest probability node in a Huffman table, and mark it as
 * being assigned to a higher probability.
 * Returns the node index of the lowest unused node, or -1 if all nodes
 * are used.
 */
static int huff_smallest_node(hnode_t *hnodes, int num_hnodes) {
    int i;
    int best, best_node;

    best = 99999999;
    best_node = -1;
    for(i = 0; i < num_hnodes; i++) {
        if(hnodes[i].used)
            continue;
        if(!hnodes[i].count)
            continue;
        if(hnodes[i].count < best) {
            best = hnodes[i].count;
            best_node = i;
        }
    }

    if(best_node == -1)
        return -1;
    hnodes[best_node].used = 1;
    return best_node;
}

/*
 * Build the Huffman tree using the generated/loaded probabilities histogram.
 *
 * On completion:
 *  huff_nodes[prev][i < HUF_TOKENS] - are the nodes at the base of the tree.
 *  huff_nodes[prev][i >= HUF_TOKENS] - are used to construct the tree.
 *  num_huff_nodes[prev] - contains the index to the root node of the tree.
 *    That is: huff_nodes[prev][num_huff_nodes[prev]] is the root node.
 */
static void huff_build_tree(IdcinContext *s, int prev) {
    hnode_t *node, *hnodes;
     int num_hnodes, i;

    num_hnodes = HUF_TOKENS;
    hnodes = s->huff_nodes[prev];
    for(i = 0; i < HUF_TOKENS * 2; i++)
        hnodes[i].used = 0;

    while (1) {
        node = &hnodes[num_hnodes];             /* next free node */

        /* pick two lowest counts */
        node->children[0] = huff_smallest_node(hnodes, num_hnodes);
        if(node->children[0] == -1)
            break;      /* reached the root node */

        node->children[1] = huff_smallest_node(hnodes, num_hnodes);
        if(node->children[1] == -1)
            break;      /* reached the root node */

        /* combine nodes probability for new node */
        node->count = hnodes[node->children[0]].count +
        hnodes[node->children[1]].count;
        num_hnodes++;
    }

    s->num_huff_nodes[prev] = num_hnodes - 1;
}

static int idcin_decode_init(AVCodecContext *avctx)
{
    IdcinContext *s = (IdcinContext *)avctx->priv_data;
    int i, j, histogram_index = 0;
    unsigned char *histograms;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    /* make sure the Huffman tables make it */
    if (s->avctx->extradata_size != HUFFMAN_TABLE_SIZE) {
        av_log(s->avctx, AV_LOG_ERROR, "  Id CIN video: expected extradata size of %d\n", HUFFMAN_TABLE_SIZE);
        return -1;
    }

    /* build the 256 Huffman decode trees */
    histograms = (unsigned char *)s->avctx->extradata;
    for (i = 0; i < 256; i++) {
        for(j = 0; j < HUF_TOKENS; j++)
            s->huff_nodes[i][j].count = histograms[histogram_index++];
        huff_build_tree(s, i);
    }

    s->frame.data[0] = NULL;

    return 0;
}

static void idcin_decode_vlcs(IdcinContext *s)
{
    hnode_t *hnodes;
    long x, y;
    int prev;
    unsigned char v = 0;
    int bit_pos, node_num, dat_pos;

    prev = bit_pos = dat_pos = 0;
    for (y = 0; y < (s->frame.linesize[0] * s->avctx->height);
        y += s->frame.linesize[0]) {
        for (x = y; x < y + s->avctx->width; x++) {
            node_num = s->num_huff_nodes[prev];
            hnodes = s->huff_nodes[prev];

            while(node_num >= HUF_TOKENS) {
                if(!bit_pos) {
                    if(dat_pos >= s->size) {
                        av_log(s->avctx, AV_LOG_ERROR, "Huffman decode error.\n");
                        return;
                    }
                    bit_pos = 8;
                    v = s->buf[dat_pos++];
                }

                node_num = hnodes[node_num].children[v & 0x01];
                v = v >> 1;
                bit_pos--;
            }

            s->frame.data[0][x] = node_num;
            prev = node_num;
        }
    }
}

static int idcin_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    IdcinContext *s = (IdcinContext *)avctx->priv_data;
    AVPaletteControl *palette_control = avctx->palctrl;

    s->buf = buf;
    s->size = buf_size;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    if (avctx->get_buffer(avctx, &s->frame)) {
        av_log(avctx, AV_LOG_ERROR, "  Id CIN Video: get_buffer() failed\n");
        return -1;
    }

    idcin_decode_vlcs(s);

    /* make the palette available on the way out */
    memcpy(s->frame.data[1], palette_control->palette, PALETTE_COUNT * 4);
    /* If palette changed inform application*/
    if (palette_control->palette_changed) {
        palette_control->palette_changed = 0;
        s->frame.palette_has_changed = 1;
    }

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int idcin_decode_end(AVCodecContext *avctx)
{
    IdcinContext *s = (IdcinContext *)avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    return 0;
}

AVCodec idcin_decoder = {
    "idcinvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_IDCIN,
    sizeof(IdcinContext),
    idcin_decode_init,
    NULL,
    idcin_decode_end,
    idcin_decode_frame,
    CODEC_CAP_DR1,
};

