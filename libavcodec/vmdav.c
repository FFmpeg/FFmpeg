/*
 * Sierra VMD Audio & Video Decoders
 * Copyright (C) 2004 the ffmpeg project
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
 * @file vmdvideo.c
 * Sierra VMD audio & video decoders
 * by Vladimir "VAG" Gneushev (vagsoft at mail.ru)
 * for more information on the Sierra VMD format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *
 * The video decoder outputs PAL8 colorspace data. The decoder expects
 * a 0x330-byte VMD file header to be transmitted via extradata during
 * codec initialization. Each encoded frame that is sent to this decoder
 * is expected to be prepended with the appropriate 16-byte frame 
 * information record from the VMD file.
 *
 * The audio decoder, like the video decoder, expects each encoded data
 * chunk to be prepended with the appropriate 16-byte frame information
 * record from the VMD file. It does not require the 0x330-byte VMD file
 * header, but it does need the audio setup parameters passed in through
 * normal libavcodec API means.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "avcodec.h"
#include "dsputil.h"

#define VMD_HEADER_SIZE 0x330
#define PALETTE_COUNT 256

/*
 * Video Decoder
 */

typedef struct VmdVideoContext {

    AVCodecContext *avctx;
    DSPContext dsp;
    AVFrame frame;
    AVFrame prev_frame;

    unsigned char *buf;
    int size;

    unsigned char palette[PALETTE_COUNT * 4];
    unsigned char *unpack_buffer;
    int unpack_buffer_size;

} VmdVideoContext;

#define QUEUE_SIZE 0x1000
#define QUEUE_MASK 0x0FFF

static void lz_unpack(unsigned char *src, unsigned char *dest, int dest_len)
{
    unsigned char *s;
    unsigned char *d;
    unsigned char *d_end;
    unsigned char queue[QUEUE_SIZE];
    unsigned int qpos;
    unsigned int dataleft;
    unsigned int chainofs;
    unsigned int chainlen;
    unsigned int speclen;
    unsigned char tag;
    unsigned int i, j;

    s = src;
    d = dest;
    d_end = d + dest_len;
    dataleft = LE_32(s);
    s += 4;
    memset(queue, QUEUE_SIZE, 0x20);
    if (LE_32(s) == 0x56781234) {
        s += 4;
        qpos = 0x111;
        speclen = 0xF + 3;
    } else {
        qpos = 0xFEE;
        speclen = 100;  /* no speclen */
    }

    while (dataleft > 0) {
        tag = *s++;
        if ((tag == 0xFF) && (dataleft > 8)) {
            if (d + 8 > d_end)
                return;
            for (i = 0; i < 8; i++) {
                queue[qpos++] = *d++ = *s++;
                qpos &= QUEUE_MASK;
            }
            dataleft -= 8;
        } else {
            for (i = 0; i < 8; i++) {
                if (dataleft == 0)
                    break;
                if (tag & 0x01) {
                    if (d + 1 > d_end)
                        return;
                    queue[qpos++] = *d++ = *s++;
                    qpos &= QUEUE_MASK;
                    dataleft--;
                } else {
                    chainofs = *s++;
                    chainofs |= ((*s & 0xF0) << 4);
                    chainlen = (*s++ & 0x0F) + 3;
                    if (chainlen == speclen)
                        chainlen = *s++ + 0xF + 3;
                    if (d + chainlen > d_end)
                        return;
                    for (j = 0; j < chainlen; j++) {
                        *d = queue[chainofs++ & QUEUE_MASK];
                        queue[qpos++] = *d++;
                        qpos &= QUEUE_MASK;
                    }
                    dataleft -= chainlen;
                }
                tag >>= 1;
            }
        }
    }
}

static int rle_unpack(unsigned char *src, unsigned char *dest, 
    int src_len, int dest_len)
{
    unsigned char *ps;
    unsigned char *pd;
    int i, l;
    unsigned char *dest_end = dest + dest_len;

    ps = src;
    pd = dest;
    if (src_len & 1)
        *pd++ = *ps++;

    src_len >>= 1;
    i = 0;
    do {
        l = *ps++;
        if (l & 0x80) {
            l = (l & 0x7F) * 2;
            if (pd + l > dest_end)
                return (ps - src);
            memcpy(pd, ps, l);
            ps += l;
            pd += l;
        } else {
            if (pd + i > dest_end)
                return (ps - src);
            for (i = 0; i < l; i++) {
                *pd++ = ps[0];
                *pd++ = ps[1];
            }
            ps += 2;
        }
        i += l;
    } while (i < src_len);

    return (ps - src);
}

static void vmd_decode(VmdVideoContext *s)
{
    int i;
    unsigned int *palette32;
    unsigned char r, g, b;

    /* point to the start of the encoded data */
    unsigned char *p = s->buf + 16;

    unsigned char *pb;
    unsigned char meth;
    unsigned char *dp;   /* pointer to current frame */
    unsigned char *pp;   /* pointer to previous frame */
    unsigned char len;
    int ofs;

    int frame_x, frame_y;
    int frame_width, frame_height;
    int dp_size;

    frame_x = LE_16(&s->buf[6]);
    frame_y = LE_16(&s->buf[8]);
    frame_width = LE_16(&s->buf[10]) - frame_x + 1;
    frame_height = LE_16(&s->buf[12]) - frame_y + 1;

    /* if only a certain region will be updated, copy the entire previous
     * frame before the decode */
    if (frame_x || frame_y || (frame_width != s->avctx->width) ||
        (frame_height != s->avctx->height)) {

        memcpy(s->frame.data[0], s->prev_frame.data[0], 
            s->avctx->height * s->frame.linesize[0]);
    }

    /* check if there is a new palette */
    if (s->buf[15] & 0x02) {
        p += 2;
        palette32 = (unsigned int *)s->palette;
        for (i = 0; i < PALETTE_COUNT; i++) {
            r = *p++ * 4;
            g = *p++ * 4;
            b = *p++ * 4;
            palette32[i] = (r << 16) | (g << 8) | (b);
        }
        s->size -= (256 * 3 + 2);
    }
    if (s->size >= 0) {
        /* originally UnpackFrame in VAG's code */
        pb = p;
        meth = *pb++;
        if (meth & 0x80) {
            lz_unpack(pb, s->unpack_buffer, s->unpack_buffer_size);
            meth &= 0x7F;
            pb = s->unpack_buffer;
        }

        dp = &s->frame.data[0][frame_y * s->frame.linesize[0] + frame_x];
        dp_size = s->frame.linesize[0] * s->avctx->height;
        pp = &s->prev_frame.data[0][frame_y * s->prev_frame.linesize[0] + frame_x];
        switch (meth) {
        case 1:
            for (i = 0; i < frame_height; i++) {
                ofs = 0;
                do {
                    len = *pb++;
                    if (len & 0x80) {
                        len = (len & 0x7F) + 1;
                        if (ofs + len > frame_width)
                            return;
                        memcpy(&dp[ofs], pb, len);
                        pb += len;
                        ofs += len;
                    } else {
                        /* interframe pixel copy */
                        if (ofs + len + 1 > frame_width)
                            return;
                        memcpy(&dp[ofs], &pp[ofs], len + 1);
                        ofs += len + 1;
                    }
                } while (ofs < frame_width);
                if (ofs > frame_width) {
                    av_log(s->avctx, AV_LOG_ERROR, "VMD video: offset > width (%d > %d)\n",
                        ofs, frame_width);
                    break;
                }
                dp += s->frame.linesize[0];
                pp += s->prev_frame.linesize[0];
            }
            break;

        case 2:
            for (i = 0; i < frame_height; i++) {
                memcpy(dp, pb, frame_width);
                pb += frame_width;
                dp += s->frame.linesize[0];
                pp += s->prev_frame.linesize[0];
            }
            break;

        case 3:
            for (i = 0; i < frame_height; i++) {
                ofs = 0;
                do {
                    len = *pb++;
                    if (len & 0x80) {
                        len = (len & 0x7F) + 1;
                        if (*pb++ == 0xFF)
                            len = rle_unpack(pb, &dp[ofs], len, frame_width - ofs);
                        else
                            memcpy(&dp[ofs], pb, len);
                        pb += len;
                        ofs += len;
                    } else {
                        /* interframe pixel copy */
                        if (ofs + len + 1 > frame_width)
                            return;
                        memcpy(&dp[ofs], &pp[ofs], len + 1);
                        ofs += len + 1;
                    }
                } while (ofs < frame_width);
                if (ofs > frame_width) {
                    av_log(s->avctx, AV_LOG_ERROR, "VMD video: offset > width (%d > %d)\n",
                        ofs, frame_width);
                }
                dp += s->frame.linesize[0];
                pp += s->prev_frame.linesize[0];
            }
            break;
        }
    }
}

static int vmdvideo_decode_init(AVCodecContext *avctx)
{
    VmdVideoContext *s = (VmdVideoContext *)avctx->priv_data;
    int i;
    unsigned int *palette32;
    int palette_index = 0;
    unsigned char r, g, b;
    unsigned char *vmd_header;
    unsigned char *raw_palette;

    s->avctx = avctx;
    avctx->pix_fmt = PIX_FMT_PAL8;
    avctx->has_b_frames = 0;
    dsputil_init(&s->dsp, avctx);

    /* make sure the VMD header made it */
    if (s->avctx->extradata_size != VMD_HEADER_SIZE) {
        av_log(s->avctx, AV_LOG_ERROR, "VMD video: expected extradata size of %d\n", 
            VMD_HEADER_SIZE);
        return -1;
    }
    vmd_header = (unsigned char *)avctx->extradata;

    s->unpack_buffer_size = LE_32(&vmd_header[800]);
    s->unpack_buffer = av_malloc(s->unpack_buffer_size);
    if (!s->unpack_buffer)
        return -1;

    /* load up the initial palette */
    raw_palette = &vmd_header[28];
    palette32 = (unsigned int *)s->palette;
    for (i = 0; i < PALETTE_COUNT; i++) {
        r = raw_palette[palette_index++] * 4;
        g = raw_palette[palette_index++] * 4;
        b = raw_palette[palette_index++] * 4;
        palette32[i] = (r << 16) | (g << 8) | (b);
    }

    s->frame.data[0] = s->prev_frame.data[0] = NULL;

    return 0;
}

static int vmdvideo_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 uint8_t *buf, int buf_size)
{
    VmdVideoContext *s = (VmdVideoContext *)avctx->priv_data;

    s->buf = buf;
    s->size = buf_size;

    if (buf_size < 16)
        return buf_size;

    s->frame.reference = 1;
    if (avctx->get_buffer(avctx, &s->frame)) {
        av_log(s->avctx, AV_LOG_ERROR, "VMD Video: get_buffer() failed\n");
        return -1;
    }

    vmd_decode(s);

    /* make the palette available on the way out */
    memcpy(s->frame.data[1], s->palette, PALETTE_COUNT * 4);

    if (s->prev_frame.data[0])
        avctx->release_buffer(avctx, &s->prev_frame);

    /* shuffle frames */
    s->prev_frame = s->frame;

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->frame;

    /* report that the buffer was completely consumed */
    return buf_size;
}

static int vmdvideo_decode_end(AVCodecContext *avctx)
{
    VmdVideoContext *s = (VmdVideoContext *)avctx->priv_data;

    if (s->prev_frame.data[0])
        avctx->release_buffer(avctx, &s->prev_frame);
    av_free(s->unpack_buffer);

    return 0;
}


/*
 * Audio Decoder
 */

typedef struct VmdAudioContext {
    AVCodecContext *avctx;
    int channels;
    int bits;
    int block_align;
    unsigned char steps8[16];
    unsigned short steps16[16];
    unsigned short steps128[256];
    short predictors[2];
} VmdAudioContext;

static int vmdaudio_decode_init(AVCodecContext *avctx)
{
    VmdAudioContext *s = (VmdAudioContext *)avctx->priv_data;
    int i;

    s->avctx = avctx;
    s->channels = avctx->channels;
    s->bits = avctx->bits_per_sample;
    s->block_align = avctx->block_align;

    av_log(s->avctx, AV_LOG_DEBUG, "%d channels, %d bits/sample, block align = %d, sample rate = %d\n",
	    s->channels, s->bits, s->block_align, avctx->sample_rate);

    /* set up the steps8 and steps16 tables */
    for (i = 0; i < 8; i++) {
        if (i < 4)
            s->steps8[i] = i;
        else
            s->steps8[i] = s->steps8[i - 1] + i - 1;

        if (i == 0)
            s->steps16[i] = 0;
        else if (i == 1)
            s->steps16[i] = 4;
        else if (i == 2)
            s->steps16[i] = 16;
        else
            s->steps16[i] = 1 << (i + 4);
    }

    /* set up the step128 table */
    s->steps128[0] = 0;
    s->steps128[1] = 8;
    for (i = 0x02; i <= 0x20; i++)
        s->steps128[i] = (i - 1) << 4;
    for (i = 0x21; i <= 0x60; i++)
        s->steps128[i] = (i + 0x1F) << 3;
    for (i = 0x61; i <= 0x70; i++)
        s->steps128[i] = (i - 0x51) << 6;
    for (i = 0x71; i <= 0x78; i++)
        s->steps128[i] = (i - 0x69) << 8;
    for (i = 0x79; i <= 0x7D; i++)
        s->steps128[i] = (i - 0x75) << 10;
    s->steps128[0x7E] = 0x3000;
    s->steps128[0x7F] = 0x4000;

    /* set up the negative half of each table */
    for (i = 0; i < 8; i++) {
        s->steps8[i + 8] = -s->steps8[i];
        s->steps16[i + 8] = -s->steps16[i];
    }
    for (i = 0; i < 128; i++)
      s->steps128[i + 128] = -s->steps128[i];

    return 0;
}

static void vmdaudio_decode_audio(VmdAudioContext *s, unsigned char *data,
    uint8_t *buf, int ratio) {

}

static int vmdaudio_loadsound(VmdAudioContext *s, unsigned char *data,
    uint8_t *buf, int silence)
{
    int bytes_decoded = 0;
    int i;

    if (silence)
	av_log(s->avctx, AV_LOG_INFO, "silent block!\n");
    if (s->channels == 2) {

        /* stereo handling */
        if ((s->block_align & 0x01) == 0) {
            if (silence)
                memset(data, 0, s->block_align * 2);
            else
                vmdaudio_decode_audio(s, data, buf, 1);
        } else {
            if (silence)
                memset(data, 0, s->block_align * 2);
            else
                vmdaudio_decode_audio(s, data, buf, 1);
        }
    } else {

        /* mono handling */
        if (silence) {
            if (s->bits == 16) {
                memset(data, 0, s->block_align * 2);
                bytes_decoded = s->block_align * 2;
            } else {
//                memset(data, 0x00, s->block_align);
//                bytes_decoded = s->block_align;
memset(data, 0x00, s->block_align * 2);
bytes_decoded = s->block_align * 2;
            }
        } else {
            /* copy the data but convert it to signed */
            for (i = 0; i < s->block_align; i++)
                data[i * 2 + 1] = buf[i] + 0x80;
            bytes_decoded = s->block_align * 2;
        }
    }

    return bytes_decoded;
}

static int vmdaudio_decode_frame(AVCodecContext *avctx,
                                 void *data, int *data_size,
                                 uint8_t *buf, int buf_size)
{
    VmdAudioContext *s = (VmdAudioContext *)avctx->priv_data;
    unsigned int sound_flags;
    unsigned char *output_samples = (unsigned char *)data;

    /* point to the start of the encoded data */
    unsigned char *p = buf + 16;
    unsigned char *p_end = buf + buf_size;

    if (buf_size < 16)
        return buf_size;

    if (buf[6] == 1) {
        /* the chunk contains audio */
        *data_size = vmdaudio_loadsound(s, output_samples, p, 0);
    } else if (buf[6] == 2) {
        /* the chunk contains audio and silence mixed together */
        sound_flags = LE_32(p);
        p += 4;

        /* do something with extrabufs here? */

        while (p < p_end) {
            if (sound_flags & 0x01)
                /* silence */
                *data_size += vmdaudio_loadsound(s, output_samples, p, 1);
            else {
                /* audio */
                *data_size += vmdaudio_loadsound(s, output_samples, p, 0);
                p += s->block_align;
            }
            output_samples += (s->block_align * s->bits / 8);
            sound_flags >>= 1;
        }
    } else if (buf[6] == 3) {
        /* silent chunk */
        *data_size = vmdaudio_loadsound(s, output_samples, p, 1);
    }

    return buf_size;
}


/*
 * Public Data Structures
 */

AVCodec vmdvideo_decoder = {
    "vmdvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_VMDVIDEO,
    sizeof(VmdVideoContext),
    vmdvideo_decode_init,
    NULL,
    vmdvideo_decode_end,
    vmdvideo_decode_frame,
    CODEC_CAP_DR1,
};

AVCodec vmdaudio_decoder = {
    "vmdaudio",
    CODEC_TYPE_AUDIO,
    CODEC_ID_VMDAUDIO,
    sizeof(VmdAudioContext),
    vmdaudio_decode_init,
    NULL,
    NULL,
    vmdaudio_decode_frame,
};
