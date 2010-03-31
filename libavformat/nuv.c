/*
 * NuppelVideo demuxer.
 * Copyright (c) 2006 Reimar Doeffinger
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

typedef struct {
    int v_id;
    int a_id;
    int rtjpg_video;
} NUVContext;

typedef enum {
    NUV_VIDEO = 'V',
    NUV_EXTRADATA = 'D',
    NUV_AUDIO = 'A',
    NUV_SEEKP = 'R',
    NUV_MYTHEXT = 'X'
} nuv_frametype;

static int nuv_probe(AVProbeData *p) {
    if (!memcmp(p->buf, "NuppelVideo", 12))
        return AVPROBE_SCORE_MAX;
    if (!memcmp(p->buf, "MythTVVideo", 12))
        return AVPROBE_SCORE_MAX;
    return 0;
}

//! little macro to sanitize packet size
#define PKTSIZE(s) (s &  0xffffff)

/**
 * \brief read until we found all data needed for decoding
 * \param vst video stream of which to change parameters
 * \param ast video stream of which to change parameters
 * \param myth set if this is a MythTVVideo format file
 * \return 1 if all required codec data was found
 */
static int get_codec_data(ByteIOContext *pb, AVStream *vst,
                          AVStream *ast, int myth) {
    nuv_frametype frametype;
    if (!vst && !myth)
        return 1; // no codec data needed
    while (!url_feof(pb)) {
        int size, subtype;
        frametype = get_byte(pb);
        switch (frametype) {
            case NUV_EXTRADATA:
                subtype = get_byte(pb);
                url_fskip(pb, 6);
                size = PKTSIZE(get_le32(pb));
                if (vst && subtype == 'R') {
                    vst->codec->extradata_size = size;
                    vst->codec->extradata = av_malloc(size);
                    get_buffer(pb, vst->codec->extradata, size);
                    size = 0;
                    if (!myth)
                        return 1;
                }
                break;
            case NUV_MYTHEXT:
                url_fskip(pb, 7);
                size = PKTSIZE(get_le32(pb));
                if (size != 128 * 4)
                    break;
                get_le32(pb); // version
                if (vst) {
                    vst->codec->codec_tag = get_le32(pb);
                    vst->codec->codec_id =
                        ff_codec_get_id(ff_codec_bmp_tags, vst->codec->codec_tag);
                    if (vst->codec->codec_tag == MKTAG('R', 'J', 'P', 'G'))
                        vst->codec->codec_id = CODEC_ID_NUV;
                } else
                    url_fskip(pb, 4);

                if (ast) {
                    ast->codec->codec_tag = get_le32(pb);
                    ast->codec->sample_rate = get_le32(pb);
                    ast->codec->bits_per_coded_sample = get_le32(pb);
                    ast->codec->channels = get_le32(pb);
                    ast->codec->codec_id =
                        ff_wav_codec_get_id(ast->codec->codec_tag,
                                         ast->codec->bits_per_coded_sample);
                    ast->need_parsing = AVSTREAM_PARSE_FULL;
                } else
                    url_fskip(pb, 4 * 4);

                size -= 6 * 4;
                url_fskip(pb, size);
                return 1;
            case NUV_SEEKP:
                size = 11;
                break;
            default:
                url_fskip(pb, 7);
                size = PKTSIZE(get_le32(pb));
                break;
        }
        url_fskip(pb, size);
    }
    return 0;
}

static int nuv_header(AVFormatContext *s, AVFormatParameters *ap) {
    NUVContext *ctx = s->priv_data;
    ByteIOContext *pb = s->pb;
    char id_string[12];
    double aspect, fps;
    int is_mythtv, width, height, v_packs, a_packs;
    int stream_nr = 0;
    AVStream *vst = NULL, *ast = NULL;
    get_buffer(pb, id_string, 12);
    is_mythtv = !memcmp(id_string, "MythTVVideo", 12);
    url_fskip(pb, 5); // version string
    url_fskip(pb, 3); // padding
    width = get_le32(pb);
    height = get_le32(pb);
    get_le32(pb); // unused, "desiredwidth"
    get_le32(pb); // unused, "desiredheight"
    get_byte(pb); // 'P' == progressive, 'I' == interlaced
    url_fskip(pb, 3); // padding
    aspect = av_int2dbl(get_le64(pb));
    if (aspect > 0.9999 && aspect < 1.0001)
        aspect = 4.0 / 3.0;
    fps = av_int2dbl(get_le64(pb));

    // number of packets per stream type, -1 means unknown, e.g. streaming
    v_packs = get_le32(pb);
    a_packs = get_le32(pb);
    get_le32(pb); // text

    get_le32(pb); // keyframe distance (?)

    if (v_packs) {
        ctx->v_id = stream_nr++;
        vst = av_new_stream(s, ctx->v_id);
        if (!vst)
            return AVERROR(ENOMEM);
        vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        vst->codec->codec_id = CODEC_ID_NUV;
        vst->codec->width = width;
        vst->codec->height = height;
        vst->codec->bits_per_coded_sample = 10;
        vst->sample_aspect_ratio = av_d2q(aspect * height / width, 10000);
        vst->r_frame_rate = av_d2q(fps, 60000);
        av_set_pts_info(vst, 32, 1, 1000);
    } else
        ctx->v_id = -1;

    if (a_packs) {
        ctx->a_id = stream_nr++;
        ast = av_new_stream(s, ctx->a_id);
        if (!ast)
            return AVERROR(ENOMEM);
        ast->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        ast->codec->codec_id = CODEC_ID_PCM_S16LE;
        ast->codec->channels = 2;
        ast->codec->sample_rate = 44100;
        ast->codec->bit_rate = 2 * 2 * 44100 * 8;
        ast->codec->block_align = 2 * 2;
        ast->codec->bits_per_coded_sample = 16;
        av_set_pts_info(ast, 32, 1, 1000);
    } else
        ctx->a_id = -1;

    get_codec_data(pb, vst, ast, is_mythtv);
    ctx->rtjpg_video = vst && vst->codec->codec_id == CODEC_ID_NUV;
    return 0;
}

#define HDRSIZE 12

static int nuv_packet(AVFormatContext *s, AVPacket *pkt) {
    NUVContext *ctx = s->priv_data;
    ByteIOContext *pb = s->pb;
    uint8_t hdr[HDRSIZE];
    nuv_frametype frametype;
    int ret, size;
    while (!url_feof(pb)) {
        int copyhdrsize = ctx->rtjpg_video ? HDRSIZE : 0;
        uint64_t pos = url_ftell(pb);
        ret = get_buffer(pb, hdr, HDRSIZE);
        if (ret < HDRSIZE)
            return ret < 0 ? ret : AVERROR(EIO);
        frametype = hdr[0];
        size = PKTSIZE(AV_RL32(&hdr[8]));
        switch (frametype) {
            case NUV_EXTRADATA:
                if (!ctx->rtjpg_video) {
                    url_fskip(pb, size);
                    break;
                }
            case NUV_VIDEO:
                if (ctx->v_id < 0) {
                    av_log(s, AV_LOG_ERROR, "Video packet in file without video stream!\n");
                    url_fskip(pb, size);
                    break;
                }
                ret = av_new_packet(pkt, copyhdrsize + size);
                if (ret < 0)
                    return ret;
                // HACK: we have no idea if it is a keyframe,
                // but if we mark none seeking will not work at all.
                pkt->flags |= AV_PKT_FLAG_KEY;
                pkt->pos = pos;
                pkt->pts = AV_RL32(&hdr[4]);
                pkt->stream_index = ctx->v_id;
                memcpy(pkt->data, hdr, copyhdrsize);
                ret = get_buffer(pb, pkt->data + copyhdrsize, size);
                if (ret < 0) {
                    av_free_packet(pkt);
                    return ret;
                }
                if (ret < size)
                    av_shrink_packet(pkt, copyhdrsize + ret);
                return 0;
            case NUV_AUDIO:
                if (ctx->a_id < 0) {
                    av_log(s, AV_LOG_ERROR, "Audio packet in file without audio stream!\n");
                    url_fskip(pb, size);
                    break;
                }
                ret = av_get_packet(pb, pkt, size);
                pkt->flags |= AV_PKT_FLAG_KEY;
                pkt->pos = pos;
                pkt->pts = AV_RL32(&hdr[4]);
                pkt->stream_index = ctx->a_id;
                if (ret < 0) return ret;
                return 0;
            case NUV_SEEKP:
                // contains no data, size value is invalid
                break;
            default:
                url_fskip(pb, size);
                break;
        }
    }
    return AVERROR(EIO);
}

AVInputFormat nuv_demuxer = {
    "nuv",
    NULL_IF_CONFIG_SMALL("NuppelVideo format"),
    sizeof(NUVContext),
    nuv_probe,
    nuv_header,
    nuv_packet,
    NULL,
    NULL,
    .flags = AVFMT_GENERIC_INDEX,
};
