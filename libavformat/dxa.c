/*
 * DXA demuxer
 * Copyright (c) 2007 Konstantin Shishkov
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "riff.h"

#define DXA_EXTRA_SIZE  9

typedef struct{
    int frames;
    int has_sound;
    int bpc;
    uint32_t bytes_left;
    int64_t wavpos, vidpos;
    int readvid;
}DXAContext;

static int dxa_probe(AVProbeData *p)
{
    int w, h;
    if (p->buf_size < 15)
        return 0;
    w = AV_RB16(p->buf + 11);
    h = AV_RB16(p->buf + 13);
    /* check file header */
    if (p->buf[0] == 'D' && p->buf[1] == 'E' &&
        p->buf[2] == 'X' && p->buf[3] == 'A' &&
        w && w <= 2048 && h && h <= 2048)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int dxa_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    DXAContext *c = s->priv_data;
    AVStream *st, *ast;
    uint32_t tag;
    int32_t fps;
    int w, h;
    int num, den;
    int flags;

    tag = get_le32(pb);
    if (tag != MKTAG('D', 'E', 'X', 'A'))
        return -1;
    flags = get_byte(pb);
    c->frames = get_be16(pb);
    if(!c->frames){
        av_log(s, AV_LOG_ERROR, "File contains no frames ???\n");
        return -1;
    }

    fps = get_be32(pb);
    if(fps > 0){
        den = 1000;
        num = fps;
    }else if (fps < 0){
        den = 100000;
        num = -fps;
    }else{
        den = 10;
        num = 1;
    }
    w = get_be16(pb);
    h = get_be16(pb);
    c->has_sound = 0;

    st = av_new_stream(s, 0);
    if (!st)
        return -1;

    // Parse WAV data header
    if(get_le32(pb) == MKTAG('W', 'A', 'V', 'E')){
        uint32_t size, fsize;
        c->has_sound = 1;
        size = get_be32(pb);
        c->vidpos = url_ftell(pb) + size;
        url_fskip(pb, 16);
        fsize = get_le32(pb);

        ast = av_new_stream(s, 0);
        if (!ast)
            return -1;
        ff_get_wav_header(pb, ast->codec, fsize);
        // find 'data' chunk
        while(url_ftell(pb) < c->vidpos && !url_feof(pb)){
            tag = get_le32(pb);
            fsize = get_le32(pb);
            if(tag == MKTAG('d', 'a', 't', 'a')) break;
            url_fskip(pb, fsize);
        }
        c->bpc = (fsize + c->frames - 1) / c->frames;
        if(ast->codec->block_align)
            c->bpc = ((c->bpc + ast->codec->block_align - 1) / ast->codec->block_align) * ast->codec->block_align;
        c->bytes_left = fsize;
        c->wavpos = url_ftell(pb);
        url_fseek(pb, c->vidpos, SEEK_SET);
    }

    /* now we are ready: build format streams */
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = CODEC_ID_DXA;
    st->codec->width      = w;
    st->codec->height     = h;
    av_reduce(&den, &num, den, num, (1UL<<31)-1);
    av_set_pts_info(st, 33, num, den);
    /* flags & 0x80 means that image is interlaced,
     * flags & 0x40 means that image has double height
     * either way set true height
     */
    if(flags & 0xC0){
        st->codec->height >>= 1;
    }
    c->readvid = !c->has_sound;
    c->vidpos  = url_ftell(pb);
    s->start_time = 0;
    s->duration = (int64_t)c->frames * AV_TIME_BASE * num / den;
    av_log(s, AV_LOG_DEBUG, "%d frame(s)\n",c->frames);

    return 0;
}

static int dxa_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DXAContext *c = s->priv_data;
    int ret;
    uint32_t size;
    uint8_t buf[DXA_EXTRA_SIZE], pal[768+4];
    int pal_size = 0;

    if(!c->readvid && c->has_sound && c->bytes_left){
        c->readvid = 1;
        url_fseek(s->pb, c->wavpos, SEEK_SET);
        size = FFMIN(c->bytes_left, c->bpc);
        ret = av_get_packet(s->pb, pkt, size);
        pkt->stream_index = 1;
        if(ret != size)
            return AVERROR(EIO);
        c->bytes_left -= size;
        c->wavpos = url_ftell(s->pb);
        return 0;
    }
    url_fseek(s->pb, c->vidpos, SEEK_SET);
    while(!url_feof(s->pb) && c->frames){
        get_buffer(s->pb, buf, 4);
        switch(AV_RL32(buf)){
        case MKTAG('N', 'U', 'L', 'L'):
            if(av_new_packet(pkt, 4 + pal_size) < 0)
                return AVERROR(ENOMEM);
            pkt->stream_index = 0;
            if(pal_size) memcpy(pkt->data, pal, pal_size);
            memcpy(pkt->data + pal_size, buf, 4);
            c->frames--;
            c->vidpos = url_ftell(s->pb);
            c->readvid = 0;
            return 0;
        case MKTAG('C', 'M', 'A', 'P'):
            pal_size = 768+4;
            memcpy(pal, buf, 4);
            get_buffer(s->pb, pal + 4, 768);
            break;
        case MKTAG('F', 'R', 'A', 'M'):
            get_buffer(s->pb, buf + 4, DXA_EXTRA_SIZE - 4);
            size = AV_RB32(buf + 5);
            if(size > 0xFFFFFF){
                av_log(s, AV_LOG_ERROR, "Frame size is too big: %d\n", size);
                return -1;
            }
            if(av_new_packet(pkt, size + DXA_EXTRA_SIZE + pal_size) < 0)
                return AVERROR(ENOMEM);
            memcpy(pkt->data + pal_size, buf, DXA_EXTRA_SIZE);
            ret = get_buffer(s->pb, pkt->data + DXA_EXTRA_SIZE + pal_size, size);
            if(ret != size){
                av_free_packet(pkt);
                return AVERROR(EIO);
            }
            if(pal_size) memcpy(pkt->data, pal, pal_size);
            pkt->stream_index = 0;
            c->frames--;
            c->vidpos = url_ftell(s->pb);
            c->readvid = 0;
            return 0;
        default:
            av_log(s, AV_LOG_ERROR, "Unknown tag %c%c%c%c\n", buf[0], buf[1], buf[2], buf[3]);
            return -1;
        }
    }
    return AVERROR(EIO);
}

AVInputFormat dxa_demuxer = {
    "dxa",
    NULL_IF_CONFIG_SMALL("DXA"),
    sizeof(DXAContext),
    dxa_probe,
    dxa_read_header,
    dxa_read_packet,
};
