/*
 * Flash Compatible Streaming Format demuxer
 * Copyright (c) 2000 Fabrice Bellard
 * Copyright (c) 2003 Tinic Uro
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
#include "swf.h"

static int get_swf_tag(ByteIOContext *pb, int *len_ptr)
{
    int tag, len;

    if (url_feof(pb))
        return -1;

    tag = get_le16(pb);
    len = tag & 0x3f;
    tag = tag >> 6;
    if (len == 0x3f) {
        len = get_le32(pb);
    }
//    av_log(NULL, AV_LOG_DEBUG, "Tag: %d - Len: %d\n", tag, len);
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

static int swf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int nbits, len, tag;

    tag = get_be32(pb) & 0xffffff00;

    if (tag == MKBETAG('C', 'W', 'S', 0)) {
        av_log(s, AV_LOG_ERROR, "Compressed SWF format not supported\n");
        return AVERROR(EIO);
    }
    if (tag != MKBETAG('F', 'W', 'S', 0))
        return AVERROR(EIO);
    get_le32(pb);
    /* skip rectangle size */
    nbits = get_byte(pb) >> 3;
    len = (4 * nbits - 3 + 7) / 8;
    url_fskip(pb, len);
    swf->frame_rate = get_le16(pb); /* 8.8 fixed */
    get_le16(pb); /* frame count */

    swf->samples_per_frame = 0;
    s->ctx_flags |= AVFMTCTX_NOHEADER;
    return 0;
}

static int swf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *vst = NULL, *ast = NULL, *st = 0;
    int tag, len, i, frame, v;

    for(;;) {
        uint64_t pos = url_ftell(pb);
        tag = get_swf_tag(pb, &len);
        if (tag < 0)
            return AVERROR(EIO);
        if (tag == TAG_VIDEOSTREAM) {
            int ch_id = get_le16(pb);
            len -= 2;

            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codec->codec_type == CODEC_TYPE_VIDEO && st->id == ch_id)
                    goto skip;
            }

            get_le16(pb);
            get_le16(pb);
            get_le16(pb);
            get_byte(pb);
            /* Check for FLV1 */
            vst = av_new_stream(s, ch_id);
            if (!vst)
                return -1;
            vst->codec->codec_type = CODEC_TYPE_VIDEO;
            vst->codec->codec_id = codec_get_id(swf_codec_tags, get_byte(pb));
            av_set_pts_info(vst, 64, 256, swf->frame_rate);
            vst->codec->time_base = (AVRational){ 256, swf->frame_rate };
            len -= 8;
        } else if (tag == TAG_STREAMHEAD || tag == TAG_STREAMHEAD2) {
            /* streaming found */
            int sample_rate_code;

            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codec->codec_type == CODEC_TYPE_AUDIO && st->id == -1)
                    goto skip;
            }

            get_byte(pb);
            v = get_byte(pb);
            swf->samples_per_frame = get_le16(pb);
            ast = av_new_stream(s, -1); /* -1 to avoid clash with video stream ch_id */
            if (!ast)
                return -1;
            swf->audio_stream_index = ast->index;
            ast->codec->channels = 1 + (v&1);
            ast->codec->codec_type = CODEC_TYPE_AUDIO;
            ast->codec->codec_id = codec_get_id(swf_audio_codec_tags, (v>>4) & 15);
            ast->need_parsing = AVSTREAM_PARSE_FULL;
            sample_rate_code= (v>>2) & 3;
            if (!sample_rate_code)
                return AVERROR(EIO);
            ast->codec->sample_rate = 11025 << (sample_rate_code-1);
            av_set_pts_info(ast, 64, 1, ast->codec->sample_rate);
            len -= 4;
        } else if (tag == TAG_VIDEOFRAME) {
            int ch_id = get_le16(pb);
            len -= 2;
            for(i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codec->codec_type == CODEC_TYPE_VIDEO && st->id == ch_id) {
                    frame = get_le16(pb);
                    av_get_packet(pb, pkt, len-2);
                    pkt->pos = pos;
                    pkt->pts = frame;
                    pkt->stream_index = st->index;
                    return pkt->size;
                }
            }
        } else if (tag == TAG_STREAMBLOCK) {
            for (i = 0; i < s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codec->codec_type == CODEC_TYPE_AUDIO && st->id == -1) {
            if (st->codec->codec_id == CODEC_ID_MP3) {
                url_fskip(pb, 4);
                av_get_packet(pb, pkt, len-4);
            } else { // ADPCM, PCM
                av_get_packet(pb, pkt, len);
            }
            pkt->pos = pos;
            pkt->stream_index = st->index;
            return pkt->size;
                }
            }
        } else if (tag == TAG_JPEG2) {
            for (i=0; i<s->nb_streams; i++) {
                st = s->streams[i];
                if (st->codec->codec_id == CODEC_ID_MJPEG && st->id == -2)
                    break;
            }
            if (i == s->nb_streams) {
                vst = av_new_stream(s, -2); /* -2 to avoid clash with video stream and audio stream */
                if (!vst)
                    return -1;
                vst->codec->codec_type = CODEC_TYPE_VIDEO;
                vst->codec->codec_id = CODEC_ID_MJPEG;
                av_set_pts_info(vst, 64, 256, swf->frame_rate);
                vst->codec->time_base = (AVRational){ 256, swf->frame_rate };
                st = vst;
            }
            get_le16(pb); /* BITMAP_ID */
            av_new_packet(pkt, len-2);
            get_buffer(pb, pkt->data, 4);
            if (AV_RB32(pkt->data) == 0xffd8ffd9 ||
                AV_RB32(pkt->data) == 0xffd9ffd8) {
                /* old SWF files containing SOI/EOI as data start */
                /* files created by swink have reversed tag */
                pkt->size -= 4;
                get_buffer(pb, pkt->data, pkt->size);
            } else {
                get_buffer(pb, pkt->data + 4, pkt->size - 4);
            }
            pkt->pos = pos;
            pkt->stream_index = st->index;
            return pkt->size;
        }
    skip:
        url_fskip(pb, len);
    }
    return 0;
}

AVInputFormat swf_demuxer = {
    "swf",
    NULL_IF_CONFIG_SMALL("Flash format"),
    sizeof(SWFContext),
    swf_probe,
    swf_read_header,
    swf_read_packet,
};
