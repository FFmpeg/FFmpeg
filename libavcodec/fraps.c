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
 */

/**
 * @file
 * Lossless Fraps 'FPS1' decoder
 * @author Roine Gustafsson (roine at users sf net)
 * @author Konstantin Shishkov
 *
 * Codec algorithm for version 0 is taken from Transcode <www.transcoding.org>
 *
 * Version 2 files support by Konstantin Shishkov
 */

#include "avcodec.h"
#include "get_bits.h"
#include "huffman.h"
#include "bytestream.h"
#include "dsputil.h"

#define FPS_TAG MKTAG('F', 'P', 'S', 'x')

/**
 * local variable storage
 */
typedef struct FrapsContext{
    AVCodecContext *avctx;
    AVFrame frame;
    uint8_t *tmpbuf;
    int tmpbuf_size;
    DSPContext dsp;
} FrapsContext;


/**
 * initializes decoder
 * @param avctx codec context
 * @return 0 on success or negative if fails
 */
static av_cold int decode_init(AVCodecContext *avctx)
{
    FrapsContext * const s = avctx->priv_data;

    avcodec_get_frame_defaults(&s->frame);
    avctx->coded_frame = (AVFrame*)&s->frame;

    s->avctx = avctx;
    s->tmpbuf = NULL;

    dsputil_init(&s->dsp, avctx);

    return 0;
}

/**
 * Comparator - our nodes should ascend by count
 * but with preserved symbol order
 */
static int huff_cmp(const void *va, const void *vb){
    const Node *a = va, *b = vb;
    return (a->count - b->count)*256 + a->sym - b->sym;
}

/**
 * decode Fraps v2 packed plane
 */
static int fraps2_decode_plane(FrapsContext *s, uint8_t *dst, int stride, int w,
                               int h, const uint8_t *src, int size, int Uoff,
                               const int step)
{
    int i, j;
    GetBitContext gb;
    VLC vlc;
    Node nodes[512];

    for(i = 0; i < 256; i++)
        nodes[i].count = bytestream_get_le32(&src);
    size -= 1024;
    if (ff_huff_build_tree(s->avctx, &vlc, 256, nodes, huff_cmp,
                           FF_HUFFMAN_FLAG_ZERO_COUNT) < 0)
        return -1;
    /* we have built Huffman table and are ready to decode plane */

    /* convert bits so they may be used by standard bitreader */
    s->dsp.bswap_buf((uint32_t *)s->tmpbuf, (const uint32_t *)src, size >> 2);

    init_get_bits(&gb, s->tmpbuf, size * 8);
    for(j = 0; j < h; j++){
        for(i = 0; i < w*step; i += step){
            dst[i] = get_vlc2(&gb, vlc.table, 9, 3);
            /* lines are stored as deltas between previous lines
             * and we need to add 0x80 to the first lines of chroma planes
             */
            if(j) dst[i] += dst[i - stride];
            else if(Uoff) dst[i] += 0x80;
            if(get_bits_left(&gb) < 0){
                free_vlc(&vlc);
                return -1;
            }
        }
        dst += stride;
    }
    free_vlc(&vlc);
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                        void *data, int *data_size,
                        AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    FrapsContext * const s = avctx->priv_data;
    AVFrame *frame = data;
    AVFrame * const f = (AVFrame*)&s->frame;
    uint32_t header;
    unsigned int version,header_size;
    unsigned int x, y;
    const uint32_t *buf32;
    uint32_t *luma1,*luma2,*cb,*cr;
    uint32_t offs[4];
    int i, j, is_chroma, planes;


    header = AV_RL32(buf);
    version = header & 0xff;
    header_size = (header & (1<<30))? 8 : 4; /* bit 30 means pad to 8 bytes */

    if (version > 5) {
        av_log(avctx, AV_LOG_ERROR,
               "This file is encoded with Fraps version %d. " \
               "This codec can only decode versions <= 5.\n", version);
        return -1;
    }

    buf+=4;
    if (header_size == 8)
        buf+=4;

    switch(version) {
    case 0:
    default:
        /* Fraps v0 is a reordered YUV420 */
        avctx->pix_fmt = PIX_FMT_YUVJ420P;

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

        f->reference = 3;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1U<<31))? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
        f->key_frame = f->pict_type == AV_PICTURE_TYPE_I;

        if (f->pict_type == AV_PICTURE_TYPE_I) {
            buf32=(const uint32_t*)buf;
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

        f->reference = 3;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* bit 31 means same as previous pic */
        f->pict_type = (header & (1U<<31))? AV_PICTURE_TYPE_P : AV_PICTURE_TYPE_I;
        f->key_frame = f->pict_type == AV_PICTURE_TYPE_I;

        if (f->pict_type == AV_PICTURE_TYPE_I) {
            for(y=0; y<avctx->height; y++)
                memcpy(&f->data[0][ (avctx->height-y)*f->linesize[0] ],
                       &buf[y*avctx->width*3],
                       3*avctx->width);
        }
        break;

    case 2:
    case 4:
        /**
         * Fraps v2 is Huffman-coded YUV420 planes
         * Fraps v4 is virtually the same
         */
        avctx->pix_fmt = PIX_FMT_YUVJ420P;
        planes = 3;
        f->reference = 3;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* skip frame */
        if(buf_size == 8) {
            f->pict_type = AV_PICTURE_TYPE_P;
            f->key_frame = 0;
            break;
        }
        f->pict_type = AV_PICTURE_TYPE_I;
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
            av_fast_malloc(&s->tmpbuf, &s->tmpbuf_size, offs[i + 1] - offs[i] - 1024 + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->tmpbuf)
                return AVERROR(ENOMEM);
            if(fraps2_decode_plane(s, f->data[i], f->linesize[i], avctx->width >> is_chroma,
                    avctx->height >> is_chroma, buf + offs[i], offs[i + 1] - offs[i], is_chroma, 1) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding plane %i\n", i);
                return -1;
            }
        }
        break;
    case 3:
    case 5:
        /* Virtually the same as version 4, but is for RGB24 */
        avctx->pix_fmt = PIX_FMT_BGR24;
        planes = 3;
        f->reference = 3;
        f->buffer_hints = FF_BUFFER_HINTS_VALID |
                          FF_BUFFER_HINTS_PRESERVE |
                          FF_BUFFER_HINTS_REUSABLE;
        if (avctx->reget_buffer(avctx, f)) {
            av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
            return -1;
        }
        /* skip frame */
        if(buf_size == 8) {
            f->pict_type = AV_PICTURE_TYPE_P;
            f->key_frame = 0;
            break;
        }
        f->pict_type = AV_PICTURE_TYPE_I;
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
            av_fast_malloc(&s->tmpbuf, &s->tmpbuf_size, offs[i + 1] - offs[i] - 1024 + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!s->tmpbuf)
                return AVERROR(ENOMEM);
            if(fraps2_decode_plane(s, f->data[0] + i + (f->linesize[0] * (avctx->height - 1)), -f->linesize[0],
                    avctx->width, avctx->height, buf + offs[i], offs[i + 1] - offs[i], 0, 3) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error decoding plane %i\n", i);
                return -1;
            }
        }
        // convert pseudo-YUV into real RGB
        for(j = 0; j < avctx->height; j++){
            for(i = 0; i < avctx->width; i++){
                f->data[0][0 + i*3 + j*f->linesize[0]] += f->data[0][1 + i*3 + j*f->linesize[0]];
                f->data[0][2 + i*3 + j*f->linesize[0]] += f->data[0][1 + i*3 + j*f->linesize[0]];
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
static av_cold int decode_end(AVCodecContext *avctx)
{
    FrapsContext *s = (FrapsContext*)avctx->priv_data;

    if (s->frame.data[0])
        avctx->release_buffer(avctx, &s->frame);

    av_freep(&s->tmpbuf);
    return 0;
}


AVCodec ff_fraps_decoder = {
    .name           = "fraps",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_FRAPS,
    .priv_data_size = sizeof(FrapsContext),
    .init           = decode_init,
    .close          = decode_end,
    .decode         = decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("Fraps"),
};
