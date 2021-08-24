/*
 * Magic Lantern Video (MLV) demuxer
 * Copyright (c) 2014 Peter Ross
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
 * Magic Lantern Video (MLV) demuxer
 */

#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/rational.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "riff.h"

#define MLV_VERSION "v2.0"

#define MLV_VIDEO_CLASS_RAW  1
#define MLV_VIDEO_CLASS_YUV  2
#define MLV_VIDEO_CLASS_JPEG 3
#define MLV_VIDEO_CLASS_H264 4

#define MLV_AUDIO_CLASS_WAV  1

#define MLV_CLASS_FLAG_DELTA 0x40
#define MLV_CLASS_FLAG_LZMA  0x80

typedef struct {
    AVIOContext *pb[101];
    int class[2];
    int stream_index;
    uint64_t pts;
} MlvContext;

static int probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('M','L','V','I') &&
        AV_RL32(p->buf + 4) >= 52 &&
        !memcmp(p->buf + 8, MLV_VERSION, 5))
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int check_file_header(AVIOContext *pb, uint64_t guid)
{
    unsigned int size;
    uint8_t version[8];

    avio_skip(pb, 4);
    size = avio_rl32(pb);
    if (size < 52)
        return AVERROR_INVALIDDATA;
    avio_read(pb, version, 8);
    if (memcmp(version, MLV_VERSION, 5) || avio_rl64(pb) != guid)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, size - 24);
    return 0;
}

static void read_string(AVFormatContext *avctx, AVIOContext *pb, const char *tag, unsigned size)
{
    char * value = av_malloc(size + 1);
    if (!value) {
        avio_skip(pb, size);
        return;
    }

    avio_read(pb, value, size);
    if (!value[0]) {
        av_free(value);
        return;
    }

    value[size] = 0;
    av_dict_set(&avctx->metadata, tag, value, AV_DICT_DONT_STRDUP_VAL);
}

static void read_uint8(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
{
    av_dict_set_int(&avctx->metadata, tag, avio_r8(pb), 0);
}

static void read_uint16(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
{
    av_dict_set_int(&avctx->metadata, tag, avio_rl16(pb), 0);
}

static void read_uint32(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
{
    av_dict_set_int(&avctx->metadata, tag, avio_rl32(pb), 0);
}

static void read_uint64(AVFormatContext *avctx, AVIOContext *pb, const char *tag, const char *fmt)
{
    av_dict_set_int(&avctx->metadata, tag, avio_rl64(pb), 0);
}

static int scan_file(AVFormatContext *avctx, AVStream *vst, AVStream *ast, int file)
{
    FFStream *const vsti = ffstream(vst), *const asti = ffstream(ast);
    MlvContext *mlv = avctx->priv_data;
    AVIOContext *pb = mlv->pb[file];
    int ret;
    while (!avio_feof(pb)) {
        int type;
        unsigned int size;
        type = avio_rl32(pb);
        size = avio_rl32(pb);
        avio_skip(pb, 8); //timestamp
        if (size < 16)
            break;
        size -= 16;
        if (vst && type == MKTAG('R','A','W','I') && size >= 164) {
            unsigned width  = avio_rl16(pb);
            unsigned height = avio_rl16(pb);
            unsigned bits_per_coded_sample;
            ret = av_image_check_size(width, height, 0, avctx);
            if (ret < 0)
                return ret;
            if (avio_rl32(pb) != 1)
                avpriv_request_sample(avctx, "raw api version");
            avio_skip(pb, 20); // pointer, width, height, pitch, frame_size
            bits_per_coded_sample = avio_rl32(pb);
            if (bits_per_coded_sample > (INT_MAX - 7) / (width * height)) {
                av_log(avctx, AV_LOG_ERROR,
                       "invalid bits_per_coded_sample %u (size: %ux%u)\n",
                       bits_per_coded_sample, width, height);
                return AVERROR_INVALIDDATA;
            }
            vst->codecpar->width  = width;
            vst->codecpar->height = height;
            vst->codecpar->bits_per_coded_sample = bits_per_coded_sample;
            avio_skip(pb, 8 + 16 + 24); // black_level, white_level, xywh, active_area, exposure_bias
            if (avio_rl32(pb) != 0x2010100) /* RGGB */
                avpriv_request_sample(avctx, "cfa_pattern");
            avio_skip(pb, 80); // calibration_illuminant1, color_matrix1, dynamic_range
            vst->codecpar->format    = AV_PIX_FMT_BAYER_RGGB16LE;
            vst->codecpar->codec_tag = MKTAG('B', 'I', 'T', 16);
            size -= 164;
        } else if (ast && type == MKTAG('W', 'A', 'V', 'I') && size >= 16) {
            ret = ff_get_wav_header(avctx, pb, ast->codecpar, 16, 0);
            if (ret < 0)
                return ret;
            size -= 16;
        } else if (type == MKTAG('I','N','F','O')) {
            if (size > 0)
                read_string(avctx, pb, "info", size);
            continue;
        } else if (type == MKTAG('I','D','N','T') && size >= 36) {
            read_string(avctx, pb, "cameraName", 32);
            read_uint32(avctx, pb, "cameraModel", "0x%"PRIx32);
            size -= 36;
            if (size >= 32) {
                read_string(avctx, pb, "cameraSerial", 32);
                size -= 32;
            }
        } else if (type == MKTAG('L','E','N','S') && size >= 48) {
            read_uint16(avctx, pb, "focalLength", "%i");
            read_uint16(avctx, pb, "focalDist", "%i");
            read_uint16(avctx, pb, "aperture", "%i");
            read_uint8(avctx, pb, "stabilizerMode", "%i");
            read_uint8(avctx, pb, "autofocusMode", "%i");
            read_uint32(avctx, pb, "flags", "0x%"PRIx32);
            read_uint32(avctx, pb, "lensID", "%"PRIi32);
            read_string(avctx, pb, "lensName", 32);
            size -= 48;
            if (size >= 32) {
                read_string(avctx, pb, "lensSerial", 32);
                size -= 32;
            }
        } else if (vst && type == MKTAG('V', 'I', 'D', 'F') && size >= 4) {
            uint64_t pts = avio_rl32(pb);
            ff_add_index_entry(&vsti->index_entries, &vsti->nb_index_entries,
                               &vsti->index_entries_allocated_size,
                               avio_tell(pb) - 20, pts, file, 0, AVINDEX_KEYFRAME);
            size -= 4;
        } else if (ast && type == MKTAG('A', 'U', 'D', 'F') && size >= 4) {
            uint64_t pts = avio_rl32(pb);
            ff_add_index_entry(&asti->index_entries, &asti->nb_index_entries,
                               &asti->index_entries_allocated_size,
                               avio_tell(pb) - 20, pts, file, 0, AVINDEX_KEYFRAME);
            size -= 4;
        } else if (vst && type == MKTAG('W','B','A','L') && size >= 28) {
            read_uint32(avctx, pb, "wb_mode", "%"PRIi32);
            read_uint32(avctx, pb, "kelvin", "%"PRIi32);
            read_uint32(avctx, pb, "wbgain_r", "%"PRIi32);
            read_uint32(avctx, pb, "wbgain_g", "%"PRIi32);
            read_uint32(avctx, pb, "wbgain_b", "%"PRIi32);
            read_uint32(avctx, pb, "wbs_gm", "%"PRIi32);
            read_uint32(avctx, pb, "wbs_ba", "%"PRIi32);
            size -= 28;
        } else if (type == MKTAG('R','T','C','I') && size >= 20) {
            char str[32];
            struct tm time = { 0 };
            time.tm_sec    = avio_rl16(pb);
            time.tm_min    = avio_rl16(pb);
            time.tm_hour   = avio_rl16(pb);
            time.tm_mday   = avio_rl16(pb);
            time.tm_mon    = avio_rl16(pb);
            time.tm_year   = avio_rl16(pb);
            time.tm_wday   = avio_rl16(pb);
            time.tm_yday   = avio_rl16(pb);
            time.tm_isdst  = avio_rl16(pb);
            avio_skip(pb, 2);
            if (strftime(str, sizeof(str), "%Y-%m-%d %H:%M:%S", &time))
                av_dict_set(&avctx->metadata, "time", str, 0);
            size -= 20;
        } else if (type == MKTAG('E','X','P','O') && size >= 16) {
            av_dict_set(&avctx->metadata, "isoMode", avio_rl32(pb) ? "auto" : "manual", 0);
            read_uint32(avctx, pb, "isoValue", "%"PRIi32);
            read_uint32(avctx, pb, "isoAnalog", "%"PRIi32);
            read_uint32(avctx, pb, "digitalGain", "%"PRIi32);
            size -= 16;
            if (size >= 8) {
                read_uint64(avctx, pb, "shutterValue", "%"PRIi64);
                size -= 8;
            }
        } else if (type == MKTAG('S','T','Y','L') && size >= 36) {
            read_uint32(avctx, pb, "picStyleId", "%"PRIi32);
            read_uint32(avctx, pb, "contrast", "%"PRIi32);
            read_uint32(avctx, pb, "sharpness", "%"PRIi32);
            read_uint32(avctx, pb, "saturation", "%"PRIi32);
            read_uint32(avctx, pb, "colortone", "%"PRIi32);
            read_string(avctx, pb, "picStyleName", 16);
            size -= 36;
        } else if (type == MKTAG('M','A','R','K')) {
        } else if (type == MKTAG('N','U','L','L')) {
        } else if (type == MKTAG('M','L','V','I')) { /* occurs when MLV and Mnn files are concatenated */
        } else {
            av_log(avctx, AV_LOG_INFO, "unsupported tag %s, size %u\n",
                   av_fourcc2str(type), size);
        }
        avio_skip(pb, size);
    }
    return 0;
}

static int read_header(AVFormatContext *avctx)
{
    MlvContext *mlv = avctx->priv_data;
    AVIOContext *pb = avctx->pb;
    AVStream *vst = NULL, *ast = NULL;
    FFStream *vsti = NULL, *asti = NULL;
    int size, ret;
    unsigned nb_video_frames, nb_audio_frames;
    uint64_t guid;
    char guidstr[32];

    avio_skip(pb, 4);
    size = avio_rl32(pb);
    if (size < 52)
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 8);

    guid = avio_rl64(pb);
    snprintf(guidstr, sizeof(guidstr), "0x%"PRIx64, guid);
    av_dict_set(&avctx->metadata, "guid", guidstr, 0);

    avio_skip(pb, 8); //fileNum, fileCount, fileFlags

    mlv->class[0] = avio_rl16(pb);
    mlv->class[1] = avio_rl16(pb);

    nb_video_frames = avio_rl32(pb);
    nb_audio_frames = avio_rl32(pb);

    if (nb_video_frames && mlv->class[0]) {
        vst = avformat_new_stream(avctx, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        vsti = ffstream(vst);

        vst->id = 0;
        vst->nb_frames = nb_video_frames;
        if ((mlv->class[0] & (MLV_CLASS_FLAG_DELTA|MLV_CLASS_FLAG_LZMA)))
            avpriv_request_sample(avctx, "compression");
        vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        switch (mlv->class[0] & ~(MLV_CLASS_FLAG_DELTA|MLV_CLASS_FLAG_LZMA)) {
        case MLV_VIDEO_CLASS_RAW:
            vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
            break;
        case MLV_VIDEO_CLASS_YUV:
            vst->codecpar->format   = AV_PIX_FMT_YUV420P;
            vst->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
            vst->codecpar->codec_tag = 0;
            break;
        case MLV_VIDEO_CLASS_JPEG:
            vst->codecpar->codec_id = AV_CODEC_ID_MJPEG;
            vst->codecpar->codec_tag = 0;
            break;
        case MLV_VIDEO_CLASS_H264:
            vst->codecpar->codec_id = AV_CODEC_ID_H264;
            vst->codecpar->codec_tag = 0;
            break;
        default:
            avpriv_request_sample(avctx, "unknown video class");
        }
    }

    if (nb_audio_frames && mlv->class[1]) {
        ast = avformat_new_stream(avctx, NULL);
        if (!ast)
            return AVERROR(ENOMEM);
        asti = ffstream(ast);
        ast->id = 1;
        ast->nb_frames = nb_audio_frames;
        if ((mlv->class[1] & MLV_CLASS_FLAG_LZMA))
            avpriv_request_sample(avctx, "compression");
        if ((mlv->class[1] & ~MLV_CLASS_FLAG_LZMA) != MLV_AUDIO_CLASS_WAV)
            avpriv_request_sample(avctx, "unknown audio class");

        ast->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        avpriv_set_pts_info(ast, 33, 1, ast->codecpar->sample_rate);
    }

    if (vst) {
       AVRational framerate;
       framerate.num = avio_rl32(pb);
       framerate.den = avio_rl32(pb);
       avpriv_set_pts_info(vst, 64, framerate.den, framerate.num);
    } else
       avio_skip(pb, 8);

    avio_skip(pb, size - 52);

    /* scan primary file */
    mlv->pb[100] = avctx->pb;
    ret = scan_file(avctx, vst, ast, 100);
    if (ret < 0)
        return ret;

    /* scan secondary files */
    if (strlen(avctx->url) > 2) {
        int i;
        char *filename = av_strdup(avctx->url);

        if (!filename)
            return AVERROR(ENOMEM);

        for (i = 0; i < 100; i++) {
            snprintf(filename + strlen(filename) - 2, 3, "%02d", i);
            if (avctx->io_open(avctx, &mlv->pb[i], filename, AVIO_FLAG_READ, NULL) < 0)
                break;
            if (check_file_header(mlv->pb[i], guid) < 0) {
                av_log(avctx, AV_LOG_WARNING, "ignoring %s; bad format or guid mismatch\n", filename);
                ff_format_io_close(avctx, &mlv->pb[i]);
                continue;
            }
            av_log(avctx, AV_LOG_INFO, "scanning %s\n", filename);
            ret = scan_file(avctx, vst, ast, i);
            if (ret < 0) {
                av_log(avctx, AV_LOG_WARNING, "ignoring %s; %s\n", filename, av_err2str(ret));
                ff_format_io_close(avctx, &mlv->pb[i]);
                continue;
            }
        }
        av_free(filename);
    }

    if (vst)
        vst->duration = vsti->nb_index_entries;
    if (ast)
        ast->duration = asti->nb_index_entries;

    if ((vst && !vsti->nb_index_entries) || (ast && !asti->nb_index_entries)) {
        av_log(avctx, AV_LOG_ERROR, "no index entries found\n");
        return AVERROR_INVALIDDATA;
    }

    if (vst && ast)
        avio_seek(pb, FFMIN(vsti->index_entries[0].pos, asti->index_entries[0].pos), SEEK_SET);
    else if (vst)
        avio_seek(pb, vsti->index_entries[0].pos, SEEK_SET);
    else if (ast)
        avio_seek(pb, asti->index_entries[0].pos, SEEK_SET);

    return 0;
}

static int read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    MlvContext *mlv = avctx->priv_data;
    AVIOContext *pb;
    AVStream *st;
    FFStream *sti;
    int index, ret;
    unsigned int size, space;

    if (!avctx->nb_streams)
        return AVERROR_EOF;

    st = avctx->streams[mlv->stream_index];
    sti = ffstream(st);
    if (mlv->pts >= st->duration)
        return AVERROR_EOF;

    index = av_index_search_timestamp(st, mlv->pts, AVSEEK_FLAG_ANY);
    if (index < 0) {
        av_log(avctx, AV_LOG_ERROR, "could not find index entry for frame %"PRId64"\n", mlv->pts);
        return AVERROR(EIO);
    }

    pb = mlv->pb[sti->index_entries[index].size];
    if (!pb) {
        ret = FFERROR_REDO;
        goto next_packet;
    }
    avio_seek(pb, sti->index_entries[index].pos, SEEK_SET);

    avio_skip(pb, 4); // blockType
    size = avio_rl32(pb);
    if (size < 16)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, 12); //timestamp, frameNumber
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        avio_skip(pb, 8); // cropPosX, cropPosY, panPosX, panPosY
    space = avio_rl32(pb);
    avio_skip(pb, space);

    if ((mlv->class[st->id] & (MLV_CLASS_FLAG_DELTA|MLV_CLASS_FLAG_LZMA))) {
        ret = AVERROR_PATCHWELCOME;
    } else if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = av_get_packet(pb, pkt, (st->codecpar->width * st->codecpar->height * st->codecpar->bits_per_coded_sample + 7) >> 3);
    } else { // AVMEDIA_TYPE_AUDIO
        if (space > UINT_MAX - 24 || size < (24 + space))
            return AVERROR_INVALIDDATA;
        ret = av_get_packet(pb, pkt, size - (24 + space));
    }

    if (ret < 0)
        return ret;

    pkt->stream_index = mlv->stream_index;
    pkt->pts = mlv->pts;

    ret = 0;
next_packet:
    mlv->stream_index++;
    if (mlv->stream_index == avctx->nb_streams) {
        mlv->stream_index = 0;
        mlv->pts++;
    }
    return ret;
}

static int read_seek(AVFormatContext *avctx, int stream_index, int64_t timestamp, int flags)
{
    MlvContext *mlv = avctx->priv_data;

    if ((flags & AVSEEK_FLAG_FRAME) || (flags & AVSEEK_FLAG_BYTE))
        return AVERROR(ENOSYS);

    if (!(avctx->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return AVERROR(EIO);

    mlv->pts = timestamp;
    return 0;
}

static int read_close(AVFormatContext *s)
{
    MlvContext *mlv = s->priv_data;
    int i;
    for (i = 0; i < 100; i++)
        ff_format_io_close(s, &mlv->pb[i]);
    return 0;
}

const AVInputFormat ff_mlv_demuxer = {
    .name           = "mlv",
    .long_name      = NULL_IF_CONFIG_SMALL("Magic Lantern Video (MLV)"),
    .priv_data_size = sizeof(MlvContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = read_close,
    .read_seek      = read_seek,
};
