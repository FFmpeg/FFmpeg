/*
 * Fraps FPS1 decoder
 * Copyright (c) 2005 Roine Gustafsson
 * Copyright (c) 2006 Konstantin Shishkov
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
 */

/**
 * @file fraps.c
 * Lossless Fraps 'FPS1' decoder
 * @author Roine Gustafsson <roine at users sf net>
 * @author Konstantin Shishkov
 *
 * Codec algorithm for version 0 is taken from Transcode <www.transcoding.org>
 *
 * Version 2 files support by Konstantin Shishkov
 */

#include "avcodec.h"
#include "bitstream.h"
#include "dsputil.h"

#define FPS_TAG MKTAG('F', 'P', 'S', 'x')

/* symbol for Huffman tree node */
#define HNODE -1

/**
 * Huffman node
 * FIXME one day this should belong to one general framework
 */
typedef struct Node{
    int16_t sym;
    int16_t n0;
    int count;
}Node;

/**
 * local variable storage
 */
typedef struct FrapsContext{
    AVCodecContext *avctx;
    AVFrame frame;
    Node nodes[512];
    uint8_t *tmpbuf;
    DSPContext dsp;
} FrapsContext;


/**
 * initializes decoder
 * @param avctx codec context
 * @return 0 on success or negative if fails
 */
static int decode_init(AVCodecContext *avctx)
{
    FrapsContext * const s = avctx->priv_data;

    avctx->coded_frame = (AVFrame*)&s->frame;
    avctx->has_b_frames = 0;
    avctx->pix_fmt= PIX_FMT_NONE; /* set in decode_frame */

    s->avctx = avctx;
    s->frame.data[0] = NULL;
    s->tmpbuf = NULL;

    dsputil_init(&s->dsp, avctx);

    return 0;
}

/**
 * Comparator - our nodes should ascend by count
 * but with preserved symbol order
 */
static int huff_cmp(const Node *a, const Node *b){
    return (a->count - b->count)*256 + a->sym - b->sym;
}

static void get_tree_codes(uint32_t *bits, int16_t *lens, uint8_t *xlat, Node *nodes, int node, uint32_t pfx, int pl, int *pos)
{
    int s;

    s = nodes[node].sym;
    if(s != HNODE || !nodes[node].count){
        bits[*pos] = pfx;
        lens[*pos] = pl;
        xlat[*pos] = s;
        (*pos)++;
    }else{
        pfx <<= 1;
        pl++;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0, pfx, pl, pos);
        pfx |= 1;
        get_tree_codes(bits, lens, xlat, nodes, nodes[node].n0+1, pfx, pl, pos);
    }
}

static int build_huff_tree(VLC *vlc, Node *nodes, uint8_t *xlat)
{
    uint32_t bits[256];
    int16_t lens[256];
    int pos = 0;

    get_tree_codes(bits, lens, xlat, nodes, 510, 0, 0, &pos);
    return init_vlc(vlc, 9, pos, lens, 2, 2, bits, 4, 4, 0);
}


/**
 * decode Fraps v2 packed plane
 */
static int fraps2_decode_plane(FrapsContext *s, uint8_t *dst, int stride, int w,
                               int h, uint8_t *src, int size, int Uoff)
{
    int i, j;
    int cur_node;
    GetBitContext gb;
    VLC vlc;
    int64_t sum = 0;
    uint8_t recode[256];

    for(i = 0; i < 256; i++){
        s->nodes[i].sym = i;
        s->nodes[i].count = AV_RL32(src);
        s->nodes[i].n0 = -2;
        if(s->nodes[i].count < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Symbol count < 0\n");
            return -1;
        }
        src += 4;
        sum += s->nodes[i].count;
    }
    size -= 1024;

    if(sum >> 31) {
        av_log(s->avctx, AV_LOG_ERROR, "Too high symbol frequencies. Tree construction is not possible\n");
        return -1;
    }
    qsort(s->nodes, 256, sizeof(Node), huff_cmp);
    cur_node = 256;
    for(i = 0; i < 511; i += 2){
        s->nodes[cur_node].sym = HNODE;
        s->nodes[cur_node].count = s->nodes[i].count + s->nodes[i+1].count;
        s->nodes[cur_node].n0 = i;
        for(j = cur_node; j > 0; j--){
            if(s->nodes[j].count >= s->nodes[j - 1].count) break;
            FFSWAP(Node, s->nodes[j], s->nodes[j - 1]);
        }
        cur_node++;
    }
    if(build_huff_tree(&vlc, s->nodes, recode) < 0){
        av_log(s->avctx, AV_LOG_ERROR, "Error building tree\n");
        return -1;
    }
    /* we have built Huffman table and are ready to decode plane */

    /* convert bits so they may be used by standard bitreader */
    s->dsp.bswap_buf(s->tmpbuf, src, size >> 2);

    init_get_bits(&gb, s->tmpbuf, size * 8);
    for(j = 0; j < h; j++){
        for(i = 0; i < w; i++){
            dst[i] = recode[get_vlc2(&gb, vlc.table, 9, 3)];
            /* lines are stored as deltas between previous lines
             * and we need to add 0x80 to the first lines of chroma planes
             */
            if(j) dst[i] += dst[i - stride];
            else if(Uoff) dst[i] += 0x80;
        }
        dst += stride;
    }
    free_vlc(&vlc);
    return 0;
}

/**
 * decode a frame
 * @param avctx codec context
 * @param data output AVFrame
 * @param data_size size of output data or 0 if no picture is returned
 * @param buf input data frame
 * @param buf_size size of input data frame
 * @return number of consumed bytes on success or negative if decode fails
 */
static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    FrapsContext * const s = avctx->priv_data;
    AVFrame *frame = data;
    AVFrame * const f = (AVFrame*)&s->frame;
    uint32_t header;
    unsigned int version,header_size;
    unsigned int x, y;
    uint32_t *buf32;
    uint32_t *luma1,*luma2,*cb,*cr;
    uint32_t offs[4];
    int i, is_chroma, planes;


    header = AV_RL32(buf);
    version = header & 0xff;
    header_size = (header & (1<<30))? 8 : 4; /* bit 30 means pad to 8 bytes */

    if (version > 2 && version != 4) {
        av_log(avctx, AV_LOG_ERROR,
               "This file is encoded with Fraps version %d. " \
               "This codec can only decode version 0, 1, 2 and 4.\n", version);
        return -1;
    }

    buf+=4;
    if (header_size == 8)
        buf+=4;

    switch(version) {
    case 0:
    default:
        /* Fraps v0 is a reordered YUV420 */
        avctx->pix_fmt = PIX_FMT_YUV420P;

        if ( (buf_size != avctx->width*avctx->height*3/2+header_size) &&
             (buf_size != header_size) ) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid frame length %d (should be %d)\n",
                   buf_size, avctx->width*avctx->height*3/2+header_size);
            return -1;
        }

        if (( (avctx->width % 8) != 0) || ( (avctx->height % 2) != 0 )) {
            av_log(avctx, AV_LOG_ERROR, "Invalid frame size %dx%d\n",
                   avctx->width, avctx->height);
            return -1;
        }

        f->reference = 1;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1<<31))? FF_P_TYPE : FF_I_TYPE;
        f->key_frame = f->pict_type == FF_I_TYPE;

        if (f->pict_type == FF_I_TYPE) {
            buf32=(uint32_t*)buf;
            for(y=0; y<avctx->height/2; y++){
                luma1=(uint32_t*)&f->data[0][ y*2*f->linesize[0] ];
                luma2=(uint32_t*)&f->data[0][ (y*2+1)*f->linesize[0] ];
                cr=(uint32_t*)&f->data[1][ y*f->linesize[1] ];
                cb=(uint32_t*)&f->data[2][ y*f->linesize[2] ];
                for(x=0; x<avctx->width; x+=8){
                    *(luma1++) = *(buf32++);
                    *(luma1++) = *(buf32++);
                    *(luma2++) = *(buf32++);
                    *(luma2++) = *(buf32++);
                    *(cr++) = *(buf32++);
                    *(cb++) = *(buf32++);
                }
            }
        }
        break;

    case 1:
        /* Fraps v1 is an upside-down BGR24 */
        avctx->pix_fmt = PIX_FMT_BGR24;

        if ( (buf_size != avctx->width*avctx->height*3+header_size) &&
             (buf_size != header_size) ) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid frame length %d (should be %d)\n",
                   buf_size, avctx->width*avctx->height*3+header_size);
            return -1;
        }

        f->reference = 1;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1<<31))? FF_P_TYPE : FF_I_TYPE;
        f->key_frame = f->pict_type == FF_I_TYPE;

        if (f->pict_type == FF_I_TYPE) {
            for(y=0; y<avctx->height; y++)
                memcpy(&f->data[0][ (avctx->height-y)*f->linesize[0] ],
                       &buf[y*avctx->width*3],
                       f->linesize[0]);
        }
        break;

    case 2:
    case 4:
        /**
         * Fraps v2 is Huffman-coded YUV420 planes
         * Fraps v4 is virtually the same
         */
        avctx->pix_fmt = PIX_FMT_YUV420P;
        planes = 3;
        f->reference = 1;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* skip frame */
        if(buf_size == 8) {
            f->pict_type = FF_P_TYPE;
            f->key_frame = 0;
            break;
        }
        f->pict_type = FF_I_TYPE;
        f->key_frame = 1;
        if ((AV_RL32(buf) != FPS_TAG)||(buf_size < (planes*1024 + 24))) {
            av_log(avctx, AV_LOG_ERROR, "Fraps: error in data stream\n");
            return -1;
        }
        for(i = 0; i < planes; i++) {
            offs[i] = AV_RL32(buf + 4 + i * 4);
            if(offs[i] >= buf_size || (i && offs[i] <= offs[i - 1] + 1024)) {
                av_log(avctx, AV_LOG_ERROR, "Fraps: plane %i offset is out of bounds\n", i);
                return -1;
            }
        }
        offs[planes] = buf_size;
        for(i = 0; i < planes; i++){
            is_chroma = !!i;
            s->tmpbuf = av_realloc(s->tmpbuf, offs[i + 1] - offs[i] - 1024 + FF_INPUT_BUFFER_PADDING_SIZE);
            if(fraps2_decode_plane(s, f->data[i], f->linesize[i], avctx->width >> is_chroma,
                    avctx->height >> is_chroma, buf + offs[i], offs[i + 1] - offs[i], is_chroma) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding plane %i\n", i);
                return -1;
            }
        }
        break;
    }

    *frame = *f;
    *data_size = sizeof(AVFrame);

    return buf_size;
}


/**
 * closes decoder
 * @param avctx codec context
 * @return 0 on success or negative if fails
 */
static int decode_end(AVCodecContext *avctx)
{
    FrapsContext *s = (FrapsContext*)avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    av_freep(&s->tmpbuf);
    return 0;
}


AVCodec fraps_decoder = {
    "fraps",
    CODEC_TYPE_VIDEO,
    CODEC_ID_FRAPS,
    sizeof(FrapsContext),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    CODEC_CAP_DR1,
};
