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

#include "libavutil/channel_layout.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "avformat.h"
#include "internal.h"
#include "riff.h"

static const AVCodecTag nuv_audio_tags[] = {
    { AV_CODEC_ID_PCM_S16LE, MKTAG('R', 'A', 'W', 'A') },
    { AV_CODEC_ID_MP3,       MKTAG('L', 'A', 'M', 'E') },
    { AV_CODEC_ID_NONE,      0 },
};

typedef struct NUVContext {
    int v_id;
    int a_id;
    int rtjpg_video;
} NUVContext;

typedef enum {
    NUV_VIDEO     = 'V',
    NUV_EXTRADATA = 'D',
    NUV_AUDIO     = 'A',
    NUV_SEEKP     = 'R',
    NUV_MYTHEXT   = 'X'
} nuv_frametype;

static int nuv_probe(AVProbeData *p)
{
    if (!memcmp(p->buf, "NuppelVideo", 12))
        return AVPROBE_SCORE_MAX;
    if (!memcmp(p->buf, "MythTVVideo", 12))
        return AVPROBE_SCORE_MAX;
    return 0;
}

/// little macro to sanitize packet size
#define PKTSIZE(s) (s &  0xffffff)

/**
 * @brief read until we found all data needed for decoding
 * @param vst video stream of which to change parameters
 * @param ast video stream of which to change parameters
 * @param myth set if this is a MythTVVideo format file
 * @return 0 or AVERROR code
 */
static int get_codec_data(AVFormatContext *s, AVIOContext *pb, AVStream *vst,
                          AVStream *ast, int myth)
{
    nuv_frametype frametype;

    if (!vst && !myth)
        return 1; // no codec data needed
    while (!avio_feof(pb)) {
        int size, subtype;

        frametype = avio_r8(pb);
        switch (frametype) {
        case NUV_EXTRADATA:
            subtype = avio_r8(pb);
            avio_skip(pb, 6);
            size = PKTSIZE(avio_rl32(pb));
            if (vst && subtype == 'R') {
                if (vst->codecpar->extradata) {
                    av_freep(&vst->codecpar->extradata);
                    vst->codecpar->extradata_size = 0;
                }
                if (ff_get_extradata(NULL, vst->codecpar, pb, size) < 0)
                    return AVERROR(ENOMEM);
                size = 0;
                if (!myth)
                    return 0;
            }
            break;
        case NUV_MYTHEXT:
            avio_skip(pb, 7);
            size = PKTSIZE(avio_rl32(pb));
            if (size != 128 * 4)
                break;
            avio_rl32(pb); // version
            if (vst) {
                vst->codecpar->codec_tag = avio_rl32(pb);
                vst->codecpar->codec_id =
                    ff_codec_get_id(ff_codec_bmp_tags, vst->codecpar->codec_tag);
                if (vst->codecpar->codec_tag == MKTAG('R', 'J', 'P', 'G'))
                    vst->codecpar->codec_id = AV_CODEC_ID_NUV;
            } else
                avio_skip(pb, 4);

            if (ast) {
                int id;

                ast->codecpar->codec_tag             = avio_rl32(pb);
                ast->codecpar->sample_rate           = avio_rl32(pb);
                if (ast->codecpar->sample_rate <= 0) {
                    av_log(s, AV_LOG_ERROR, "Invalid sample rate %d\n", ast->codecpar->sample_rate);
                    return AVERROR_INVALIDDATA;
                }
                ast->codecpar->bits_per_coded_sample = avio_rl32(pb);
                ast->codecpar->channels              = avio_rl32(pb);
                ast->codecpar->channel_layout        = 0;

                id = ff_wav_codec_get_id(ast->codecpar->codec_tag,
                                         ast->codecpar->bits_per_coded_sample);
                if (id == AV_CODEC_ID_NONE) {
                    id = ff_codec_get_id(nuv_audio_tags, ast->codecpar->codec_tag);
                    if (id == AV_CODEC_ID_PCM_S16LE)
                        id = ff_get_pcm_codec_id(ast->codecpar->bits_per_coded_sample,
                                                 0, 0, ~1);
                }
                ast->codecpar->codec_id = id;

                ast->need_parsing = AVSTREAM_PARSE_FULL;
            } else
                avio_skip(pb, 4 * 4);

            size -= 6 * 4;
            avio_skip(pb, size);
            return 0;
        case NUV_SEEKP:
            size = 11;
            break;
        default:
            avio_skip(pb, 7);
            size = PKTSIZE(avio_rl32(pb));
            break;
        }
        avio_skip(pb, size);
    }

    return 0;
}

static int nuv_header(AVFormatContext *s)
{
    NUVContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    char id_string[12];
    double aspect, fps;
    int is_mythtv, width, height, v_packs, a_packs, ret;
    AVStream *vst = NULL, *ast = NULL;

    avio_read(pb, id_string, 12);
    is_mythtv = !memcmp(id_string, "MythTVVideo", 12);
    avio_skip(pb, 5);       // version string
    avio_skip(pb, 3);       // padding
    width  = avio_rl32(pb);
    height = avio_rl32(pb);
    avio_rl32(pb);          // unused, "desiredwidth"
    avio_rl32(pb);          // unused, "desiredheight"
    avio_r8(pb);            // 'P' == progressive, 'I' == interlaced
    avio_skip(pb, 3);       // padding
    aspect = av_int2double(avio_rl64(pb));
    if (aspect > 0.9999 && aspect < 1.0001)
        aspect = 4.0 / 3.0;
    fps = av_int2double(avio_rl64(pb));
    if (fps < 0.0f) {
        if (s->error_recognition & AV_EF_EXPLODE) {
            av_log(s, AV_LOG_ERROR, "Invalid frame rate %f\n", fps);
            return AVERROR_INVALIDDATA;
        } else {
            av_log(s, AV_LOG_WARNING, "Invalid frame rate %f, setting to 0.\n", fps);
            fps = 0.0f;
        }
    }

    // number of packets per stream type, -1 means unknown, e.g. streaming
    v_packs = avio_rl32(pb);
    a_packs = avio_rl32(pb);
    avio_rl32(pb); // text

    avio_rl32(pb); // keyframe distance (?)

    if (v_packs) {
        vst = avformat_new_stream(s, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        ctx->v_id = vst->index;

        ret = av_image_check_size(width, height, 0, s);
        if (ret < 0)
            return ret;

        vst->codecpar->codec_type            = AVMEDIA_TYPE_VIDEO;
        vst->codecpar->codec_id              = AV_CODEC_ID_NUV;
        vst->codecpar->width                 = width;
        vst->codecpar->height                = height;
        vst->codecpar->bits_per_coded_sample = 10;
        vst->sample_aspect_ratio          = av_d2q(aspect * height / width,
                                                   10000);
#if FF_API_R_FRAME_RATE
        vst->r_frame_rate =
#endif
        vst->avg_frame_rate = av_d2q(fps, 60000);
        avpriv_set_pts_info(vst, 32, 1, 1000);
    } else
        ctx->v_id = -1;

    if (a_packs) {
        ast = avformat_new_stream(s, NULL);
        if (!ast)
            return AVERROR(ENOMEM);
        ctx->a_id = ast->index;

        ast->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_id              = AV_CODEC_ID_PCM_S16LE;
        ast->codecpar->channels              = 2;
        ast->codecpar->channel_layout        = AV_CH_LAYOUT_STEREO;
        ast->codecpar->sample_rate           = 44100;
        ast->codecpar->bit_rate              = 2 * 2 * 44100 * 8;
        ast->codecpar->block_align           = 2 * 2;
        ast->codecpar->bits_per_coded_sample = 16;
        avpriv_set_pts_info(ast, 32, 1, 1000);
    } else
        ctx->a_id = -1;

    if ((ret = get_codec_data(s, pb, vst, ast, is_mythtv)) < 0)
        return ret;

    ctx->rtjpg_video = vst && vst->codecpar->codec_id == AV_CODEC_ID_NUV;

    return 0;
}

#define HDRSIZE 12

static int nuv_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUVContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    uint8_t hdr[HDRSIZE];
    nuv_frametype frametype;
    int ret, size;

    while (!avio_feof(pb)) {
        int copyhdrsize = ctx->rtjpg_video ? HDRSIZE : 0;
        uint64_t pos    = avio_tell(pb);

        ret = avio_read(pb, hdr, HDRSIZE);
        if (ret < HDRSIZE)
            return ret < 0 ? ret : AVERROR(EIO);

        frametype = hdr[0];
        size      = PKTSIZE(AV_RL32(&hdr[8]));

        switch (frametype) {
        case NUV_EXTRADATA:
            if (!ctx->rtjpg_video) {
                avio_skip(pb, size);
                break;
            }
        case NUV_VIDEO:
            if (ctx->v_id < 0) {
                av_log(s, AV_LOG_ERROR, "Video packet in file without video stream!\n");
                avio_skip(pb, size);
                break;
            }
            ret = av_new_packet(pkt, copyhdrsize + size);
            if (ret < 0)
                return ret;

            pkt->pos          = pos;
            pkt->flags       |= hdr[2] == 0 ? AV_PKT_FLAG_KEY : 0;
            pkt->pts          = AV_RL32(&hdr[4]);
            pkt->stream_index = ctx->v_id;
            memcpy(pkt->data, hdr, copyhdrsize);
            ret = avio_read(pb, pkt->data + copyhdrsize, size);
            if (ret < 0) {
                av_packet_unref(pkt);
                return ret;
            }
            if (ret < size)
                av_shrink_packet(pkt, copyhdrsize + ret);
            return 0;
        case NUV_AUDIO:
            if (ctx->a_id < 0) {
                av_log(s, AV_LOG_ERROR, "Audio packet in file without audio stream!\n");
                avio_skip(pb, size);
                break;
            }
            ret               = av_get_packet(pb, pkt, size);
            pkt->flags       |= AV_PKT_FLAG_KEY;
            pkt->pos          = pos;
            pkt->pts          = AV_RL32(&hdr[4]);
            pkt->stream_index = ctx->a_id;
            if (ret < 0)
                return ret;
            return 0;
        case NUV_SEEKP:
            // contains no data, size value is invalid
            break;
        default:
            avio_skip(pb, size);
            break;
        }
    }

    return AVERROR(EIO);
}

/**
 * \brief looks for the string RTjjjjjjjjjj in the stream too resync reading
 * \return 1 if the syncword is found 0 otherwise.
 */
static int nuv_resync(AVFormatContext *s, int64_t pos_limit) {
    AVIOContext *pb = s->pb;
    uint32_t tag = 0;
    while(!avio_feof(pb) && avio_tell(pb) < pos_limit) {
        tag = (tag << 8) | avio_r8(pb);
        if (tag                  == MKBETAG('R','T','j','j') &&
           (tag = avio_rb32(pb)) == MKBETAG('j','j','j','j') &&
           (tag = avio_rb32(pb)) == MKBETAG('j','j','j','j'))
            return 1;
    }
    return 0;
}

/**
 * \brief attempts to read a timestamp from stream at the given stream position
 * \return timestamp if successful and AV_NOPTS_VALUE if failure
 */
static int64_t nuv_read_dts(AVFormatContext *s, int stream_index,
                            int64_t *ppos, int64_t pos_limit)
{
    NUVContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    uint8_t hdr[HDRSIZE];
    nuv_frametype frametype;
    int size, key, idx;
    int64_t pos, dts;

    if (avio_seek(pb, *ppos, SEEK_SET) < 0)
        return AV_NOPTS_VALUE;

    if (!nuv_resync(s, pos_limit))
        return AV_NOPTS_VALUE;

    while (!avio_feof(pb) && avio_tell(pb) < pos_limit) {
        if (avio_read(pb, hdr, HDRSIZE) < HDRSIZE)
            return AV_NOPTS_VALUE;
        frametype = hdr[0];
        size = PKTSIZE(AV_RL32(&hdr[8]));
        switch (frametype) {
            case NUV_SEEKP:
                break;
            case NUV_AUDIO:
            case NUV_VIDEO:
                if (frametype == NUV_VIDEO) {
                    idx = ctx->v_id;
                    key = hdr[2] == 0;
                } else {
                    idx = ctx->a_id;
                    key = 1;
                }
                if (stream_index == idx) {

                    pos = avio_tell(s->pb) - HDRSIZE;
                    dts = AV_RL32(&hdr[4]);

                    // TODO - add general support in av_gen_search, so it adds positions after reading timestamps
                    av_add_index_entry(s->streams[stream_index], pos, dts, size + HDRSIZE, 0,
                            key ? AVINDEX_KEYFRAME : 0);

                    *ppos = pos;
                    return dts;
                }
            default:
                avio_skip(pb, size);
                break;
        }
    }
    return AV_NOPTS_VALUE;
}


AVInputFormat ff_nuv_demuxer = {
    .name           = "nuv",
    .long_name      = NULL_IF_CONFIG_SMALL("NuppelVideo"),
    .priv_data_size = sizeof(NUVContext),
    .read_probe     = nuv_probe,
    .read_header    = nuv_header,
    .read_packet    = nuv_packet,
    .read_timestamp = nuv_read_dts,
    .flags          = AVFMT_GENERIC_INDEX,
};
