/*
 * Flash Compatible Streaming Format demuxer
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2003 Tinic Uro
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

#include "config.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "swf.h"

static const AVCodecTag swf_audio_codec_tags[] = {
    { AV_CODEC_ID_PCM_S16LE,  0x00 },
    { AV_CODEC_ID_ADPCM_SWF,  0x01 },
    { AV_CODEC_ID_MP3,        0x02 },
    { AV_CODEC_ID_PCM_S16LE,  0x03 },
//  { AV_CODEC_ID_NELLYMOSER, 0x06 },
    { AV_CODEC_ID_NONE,          0 },
};

static int get_swf_tag(AVIOContext *pb, int *len_ptr)
{
    int tag, len;

    if (pb->eof_reached)
        return -1;

    tag = avio_rl16(pb);
    len = tag & 0x3f;
    tag = tag >> 6;
    if (len == 0x3f) {
        len = avio_rl32(pb);
    }
    *len_ptr = len;
    return tag;
}


static int swf_probe(AVProbeData *p)
{
    /* check file header */
    if ((p->buf[0] == 'F' || p->buf[0] == 'C') && p->buf[1] == 'W' &&
        p->buf[2] == 'S')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

#if CONFIG_ZLIB
static int zlib_refill(void *opaque, uint8_t *buf, int buf_size)
{
    AVFormatContext *s = opaque;
    SWFContext *swf = s->priv_data;
    z_stream *z = &swf->zstream;
    int ret;

retry:
    if (!z->avail_in) {
        int n = avio_read(s->pb, swf->zbuf_in, ZBUF_SIZE);
        if (n < 0)
            return n;
        z->next_in  = swf->zbuf_in;
        z->avail_in = n;
    }

    z->next_out  = buf;
    z->avail_out = buf_size;

    ret = inflate(z, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        av_log(s, AV_LOG_ERROR, "Inflate error: %d\n", ret);
        return AVERROR_UNKNOWN;
    }

    if (buf_size - z->avail_out == 0)
        goto retry;

    return buf_size - z->avail_out;
}
#endif

static int swf_read_header(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    int nbits, len, tag;

    tag = avio_rb32(pb) & 0xffffff00;
    avio_rl32(pb);

    if (tag == MKBETAG('C', 'W', 'S', 0)) {
        av_log(s, AV_LOG_INFO, "Compressed SWF file detected\n");
#if CONFIG_ZLIB
        if (inflateInit(&swf->zstream) != Z_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to init zlib context\n");
            return AVERROR(EINVAL);
        }
        swf->zbuf_in  = av_malloc(ZBUF_SIZE);
        swf->zbuf_out = av_malloc(ZBUF_SIZE);
        swf->zpb = avio_alloc_context(swf->zbuf_out, ZBUF_SIZE, 0, s,
                                      zlib_refill, NULL, NULL);
        if (!swf->zbuf_in || !swf->zbuf_out || !swf->zpb) {
            av_freep(&swf->zbuf_in);
            av_freep(&swf->zbuf_out);
            av_freep(&swf->zpb);
            return AVERROR(ENOMEM);
        }
        swf->zpb->seekable = 0;
        pb = swf->zpb;
#else
        av_log(s, AV_LOG_ERROR, "missing zlib support, unable to open\n");
        return AVERROR(EIO);
#endif
    } else if (tag != MKBETAG('F', 'W', 'S', 0))
        return AVERROR(EIO);
    /* skip rectangle size */
    nbits = avio_r8(pb) >> 3;
    len = (4 * nbits - 3 + 7) / 8;
    avio_skip(pb, len);
    swf->frame_rate = avio_rl16(pb); /* 8.8 fixed */
    avio_rl16(pb); /* frame count */

    swf->samples_per_frame = 0;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

static int swf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SWFContext *swf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *vst = NULL, *ast = NULL, *st = 0;
    int tag, len, i, frame, v, res;

#if CONFIG_ZLIB
    if (swf->zpb)
        pb = swf->zpb;
#endif

    for(;;) {
        uint64_t pos = avio_tell(pb);
        tag = get_swf_tag(pb, &len);
        if (tag < 0)
            return AVERROR(EIO);
        if (len < 0) {
            av_log(s, AV_LOG_ERROR, "invalid tag length: %d\n", len);
            return AVERROR_INVALIDDATA;
        }
        if (tag == TAG_VIDEOSTREAM) {
            int ch_id = avio_rl16(pb);
            len -= 2;

            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && st->id == ch_id)
                    goto skip;
            }

            avio_rl16(pb);
            avio_rl16(pb);
            avio_rl16(pb);
            avio_r8(pb);
            /* Check for FLV1 */
            vst = avformat_new_stream(s, NULL);
            if (!vst)
                return -1;
            vst->id = ch_id;
            vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            vst->codecpar->codec_id = ff_codec_get_id(ff_swf_codec_tags, avio_r8(pb));
            avpriv_set_pts_info(vst, 16, 256, swf->frame_rate);
            len -= 8;
        } else if (tag == TAG_STREAMHEAD || tag == TAG_STREAMHEAD2) {
            /* streaming found */
            int sample_rate_code;

            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->id == -1)
                    goto skip;
            }

            avio_r8(pb);
            v = avio_r8(pb);
            swf->samples_per_frame = avio_rl16(pb);
            ast = avformat_new_stream(s, NULL);
            if (!ast)
                return -1;
            ast->id = -1; /* -1 to avoid clash with video stream ch_id */
            if (v & 1) {
                ast->codecpar->channels       = 2;
                ast->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
            } else {
                ast->codecpar->channels       = 1;
                ast->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
            }
            ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            ast->codecpar->codec_id = ff_codec_get_id(swf_audio_codec_tags, (v>>4) & 15);
            ast->need_parsing = AVSTREAM_PARSE_FULL;
            sample_rate_code= (v>>2) & 3;
            ast->codecpar->sample_rate = 44100 >> (3 - sample_rate_code);
            avpriv_set_pts_info(ast, 64, 1, ast->codecpar->sample_rate);
            len -= 4;
        } else if (tag == TAG_VIDEOFRAME) {
            int ch_id = avio_rl16(pb);
            len -= 2;
            for(i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && st->id == ch_id) {
                    frame = avio_rl16(pb);
                    len -= 2;
                    if (len <= 0)
                        goto skip;
                    if ((res = av_get_packet(pb, pkt, len)) < 0)
                        return res;
                    pkt->pos = pos;
                    pkt->pts = frame;
                    pkt->stream_index = st->index;
                    return pkt->size;
                }
            }
        } else if (tag == TAG_STREAMBLOCK) {
            for (i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->id == -1) {
                    if (st->codecpar->codec_id == AV_CODEC_ID_MP3) {
                        avio_skip(pb, 4);
                        len -= 4;
                        if (len <= 0)
                            goto skip;
                        if ((res = av_get_packet(pb, pkt, len)) < 0)
                            return res;
                    } else { // ADPCM, PCM
                        if (len <= 0)
                            goto skip;
                        if ((res = av_get_packet(pb, pkt, len)) < 0)
                            return res;
                    }
                    pkt->pos          = pos;
                    pkt->stream_index = st->index;
                    return pkt->size;
                }
            }
        } else if (tag == TAG_JPEG2) {
            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codecpar->codec_id == AV_CODEC_ID_MJPEG && st->id == -2)
                    break;
            }
            if (i == s->nb_streams) {
                vst = avformat_new_stream(s, NULL);
                if (!vst)
                    return -1;
                vst->id = -2; /* -2 to avoid clash with video stream and audio stream */
                vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                vst->codecpar->codec_id = AV_CODEC_ID_MJPEG;
                avpriv_set_pts_info(vst, 64, 256, swf->frame_rate);
                st = vst;
            }
            avio_rl16(pb); /* BITMAP_ID */
            len -= 2;
            if (len < 4)
                goto skip;
            if ((res = av_new_packet(pkt, len)) < 0)
                return res;
            avio_read(pb, pkt->data, 4);
            if (AV_RB32(pkt->data) == 0xffd8ffd9 ||
                AV_RB32(pkt->data) == 0xffd9ffd8) {
                /* old SWF files containing SOI/EOI as data start */
                /* files created by swink have reversed tag */
                pkt->size -= 4;
                avio_read(pb, pkt->data, pkt->size);
            } else {
                avio_read(pb, pkt->data + 4, pkt->size - 4);
            }
            pkt->pos = pos;
            pkt->stream_index = st->index;
            return pkt->size;
        }
    skip:
        len = FFMAX(0, len);
        avio_skip(pb, len);
    }
}

#if CONFIG_ZLIB
static av_cold int swf_read_close(AVFormatContext *avctx)
{
    SWFContext *s = avctx->priv_data;
    inflateEnd(&s->zstream);
    av_freep(&s->zbuf_in);
    av_freep(&s->zbuf_out);
    av_freep(&s->zpb);
    return 0;
}
#endif

AVInputFormat ff_swf_demuxer = {
    .name           = "swf",
    .long_name      = NULL_IF_CONFIG_SMALL("SWF (ShockWave Flash)"),
    .priv_data_size = sizeof(SWFContext),
    .read_probe     = swf_probe,
    .read_header    = swf_read_header,
    .read_packet    = swf_read_packet,
#if CONFIG_ZLIB
    .read_close     = swf_read_close,
#endif
};
