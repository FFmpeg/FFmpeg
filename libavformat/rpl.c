/*
 * ARMovie/RPL demuxer
 * Copyright (c) 2007 Christian Ohm, 2008 Eli Friedman
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

#include <inttypes.h>
#include <stdlib.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "internal.h"

#define RPL_SIGNATURE "ARMovie\x0A"
#define RPL_SIGNATURE_SIZE 8

/** 256 is arbitrary, but should be big enough for any reasonable file. */
#define RPL_LINE_LENGTH 256

static int rpl_probe(const AVProbeData *p)
{
    if (memcmp(p->buf, RPL_SIGNATURE, RPL_SIGNATURE_SIZE))
        return 0;

    return AVPROBE_SCORE_MAX;
}

typedef struct RPLContext {
    // RPL header data
    int32_t frames_per_chunk;

    // Stream position data
    uint32_t chunk_number;
    uint32_t chunk_part;
    uint32_t frame_in_part;
} RPLContext;

static int read_line(AVIOContext * pb, char* line, int bufsize)
{
    int i;
    for (i = 0; i < bufsize - 1; i++) {
        int b = avio_r8(pb);
        if (b == 0)
            break;
        if (b == '\n') {
            line[i] = '\0';
            return avio_feof(pb) ? -1 : 0;
        }
        line[i] = b;
    }
    line[i] = '\0';
    return -1;
}

static int32_t read_int(const char* line, const char** endptr, int* error)
{
    unsigned long result = 0;
    for (; *line>='0' && *line<='9'; line++) {
        if (result > (0x7FFFFFFF - 9) / 10)
            *error = -1;
        result = 10 * result + *line - '0';
    }
    *endptr = line;
    return result;
}

static int32_t read_line_and_int(AVIOContext * pb, int* error)
{
    char line[RPL_LINE_LENGTH];
    const char *endptr;
    *error |= read_line(pb, line, sizeof(line));
    return read_int(line, &endptr, error);
}

/** Parsing for fps, which can be a fraction. Unfortunately,
  * the spec for the header leaves out a lot of details,
  * so this is mostly guessing.
  */
static AVRational read_fps(const char* line, int* error)
{
    int64_t num, den = 1;
    AVRational result;
    num = read_int(line, &line, error);
    if (*line == '.')
        line++;
    for (; *line>='0' && *line<='9'; line++) {
        // Truncate any numerator too large to fit into an int64_t
        if (num > (INT64_MAX - 9) / 10 || den > INT64_MAX / 10)
            break;
        num  = 10 * num + (*line - '0');
        den *= 10;
    }
    if (!num)
        *error = -1;
    av_reduce(&result.num, &result.den, num, den, 0x7FFFFFFF);
    return result;
}

static int rpl_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    RPLContext *rpl = s->priv_data;
    AVStream *vst = NULL, *ast = NULL;
    int total_audio_size;
    int error = 0;
    const char *endptr;
    char audio_type[RPL_LINE_LENGTH];
    char audio_codec[RPL_LINE_LENGTH];

    uint32_t i;

    int32_t video_format, audio_format, chunk_catalog_offset, number_of_chunks;
    AVRational fps;

    char line[RPL_LINE_LENGTH];

    // The header for RPL/ARMovie files is 21 lines of text
    // containing the various header fields.  The fields are always
    // in the same order, and other text besides the first
    // number usually isn't important.
    // (The spec says that there exists some significance
    // for the text in a few cases; samples needed.)
    error |= read_line(pb, line, sizeof(line));      // ARMovie
    error |= read_line(pb, line, sizeof(line));      // movie name
    av_dict_set(&s->metadata, "title"    , line, 0);
    error |= read_line(pb, line, sizeof(line));      // date/copyright
    av_dict_set(&s->metadata, "copyright", line, 0);
    error |= read_line(pb, line, sizeof(line));      // author and other
    av_dict_set(&s->metadata, "author"   , line, 0);

    // video headers
    video_format = read_line_and_int(pb, &error);
    if (video_format) {
        vst = avformat_new_stream(s, NULL);
        if (!vst)
            return AVERROR(ENOMEM);
        vst->codecpar->codec_type      = AVMEDIA_TYPE_VIDEO;
        vst->codecpar->codec_tag       = video_format;
        vst->codecpar->width           = read_line_and_int(pb, &error);  // video width
        vst->codecpar->height          = read_line_and_int(pb, &error);  // video height
        vst->codecpar->bits_per_coded_sample = read_line_and_int(pb, &error);  // video bits per sample

        // Figure out the video codec
        switch (vst->codecpar->codec_tag) {
#if 0
            case 122:
                vst->codecpar->codec_id = AV_CODEC_ID_ESCAPE122;
                break;
#endif
            case 124:
                vst->codecpar->codec_id = AV_CODEC_ID_ESCAPE124;
                // The header is wrong here, at least sometimes
                vst->codecpar->bits_per_coded_sample = 16;
                break;
            case 130:
                vst->codecpar->codec_id = AV_CODEC_ID_ESCAPE130;
                break;
            default:
                avpriv_report_missing_feature(s, "Video format %s",
                                              av_fourcc2str(vst->codecpar->codec_tag));
                vst->codecpar->codec_id = AV_CODEC_ID_NONE;
        }
    } else {
        for (i = 0; i < 3; i++)
            error |= read_line(pb, line, sizeof(line));
    }

    error |= read_line(pb, line, sizeof(line));                   // video frames per second
    fps = read_fps(line, &error);
    if (vst)
        avpriv_set_pts_info(vst, 32, fps.den, fps.num);

    // Audio headers

    // ARMovie supports multiple audio tracks; I don't have any
    // samples, though. This code will ignore additional tracks.
    error |= read_line(pb, line, sizeof(line));
    audio_format = read_int(line, &endptr, &error);  // audio format ID
    av_strlcpy(audio_codec, endptr, RPL_LINE_LENGTH);
    if (audio_format) {
        ast = avformat_new_stream(s, NULL);
        if (!ast)
            return AVERROR(ENOMEM);
        ast->codecpar->codec_type      = AVMEDIA_TYPE_AUDIO;
        ast->codecpar->codec_tag       = audio_format;
        ast->codecpar->sample_rate     = read_line_and_int(pb, &error);  // audio bitrate
        ast->codecpar->channels        = read_line_and_int(pb, &error);  // number of audio channels
        error |= read_line(pb, line, sizeof(line));
        ast->codecpar->bits_per_coded_sample = read_int(line, &endptr, &error);  // audio bits per sample
        av_strlcpy(audio_type, endptr, RPL_LINE_LENGTH);
        // At least one sample uses 0 for ADPCM, which is really 4 bits
        // per sample.
        if (ast->codecpar->bits_per_coded_sample == 0)
            ast->codecpar->bits_per_coded_sample = 4;

        ast->codecpar->bit_rate = ast->codecpar->sample_rate *
                                  (int64_t)ast->codecpar->channels;
        if (ast->codecpar->bit_rate > INT64_MAX / ast->codecpar->bits_per_coded_sample)
            return AVERROR_INVALIDDATA;
        ast->codecpar->bit_rate *= ast->codecpar->bits_per_coded_sample;

        ast->codecpar->codec_id = AV_CODEC_ID_NONE;
        switch (audio_format) {
            case 1:
                if (ast->codecpar->bits_per_coded_sample == 16) {
                    // 16-bit audio is always signed
                    ast->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
                } else if (ast->codecpar->bits_per_coded_sample == 8) {
                    if (av_stristr(audio_type, "unsigned") != NULL)
                        ast->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
                    else if (av_stristr(audio_type, "linear") != NULL)
                        ast->codecpar->codec_id = AV_CODEC_ID_PCM_S8;
                    else
                        ast->codecpar->codec_id = AV_CODEC_ID_PCM_VIDC;
                }
                // There are some other formats listed as legal per the spec;
                // samples needed.
                break;
            case 2:
                if (av_stristr(audio_codec, "adpcm") != NULL) {
                    ast->codecpar->codec_id = AV_CODEC_ID_ADPCM_IMA_ACORN;
                }
                break;
            case 101:
                if (ast->codecpar->bits_per_coded_sample == 8) {
                    // The samples with this kind of audio that I have
                    // are all unsigned.
                    ast->codecpar->codec_id = AV_CODEC_ID_PCM_U8;
                } else if (ast->codecpar->bits_per_coded_sample == 4) {
                    ast->codecpar->codec_id = AV_CODEC_ID_ADPCM_IMA_EA_SEAD;
                }
                break;
        }
        if (ast->codecpar->codec_id == AV_CODEC_ID_NONE)
            avpriv_request_sample(s, "Audio format %"PRId32" (%s)",
                                  audio_format, audio_codec);
        avpriv_set_pts_info(ast, 32, 1, ast->codecpar->bit_rate);
    } else {
        for (i = 0; i < 3; i++)
            error |= read_line(pb, line, sizeof(line));
    }

    if (s->nb_streams == 0)
        return AVERROR_INVALIDDATA;

    rpl->frames_per_chunk = read_line_and_int(pb, &error);  // video frames per chunk
    if (vst && rpl->frames_per_chunk > 1 && vst->codecpar->codec_tag != 124)
        av_log(s, AV_LOG_WARNING,
               "Don't know how to split frames for video format %s. "
               "Video stream will be broken!\n", av_fourcc2str(vst->codecpar->codec_tag));

    number_of_chunks = read_line_and_int(pb, &error);  // number of chunks in the file
    // The number in the header is actually the index of the last chunk.
    number_of_chunks++;

    error |= read_line(pb, line, sizeof(line));  // "even" chunk size in bytes
    error |= read_line(pb, line, sizeof(line));  // "odd" chunk size in bytes
    chunk_catalog_offset =                       // offset of the "chunk catalog"
        read_line_and_int(pb, &error);           //   (file index)
    error |= read_line(pb, line, sizeof(line));  // offset to "helpful" sprite
    error |= read_line(pb, line, sizeof(line));  // size of "helpful" sprite
    if (vst) {
        error |= read_line(pb, line, sizeof(line));  // offset to key frame list
        vst->duration = number_of_chunks * rpl->frames_per_chunk;
    }

    // Read the index
    avio_seek(pb, chunk_catalog_offset, SEEK_SET);
    total_audio_size = 0;
    for (i = 0; !error && i < number_of_chunks; i++) {
        int64_t offset, video_size, audio_size;
        error |= read_line(pb, line, sizeof(line));
        if (3 != sscanf(line, "%"SCNd64" , %"SCNd64" ; %"SCNd64,
                        &offset, &video_size, &audio_size)) {
            error = -1;
            continue;
        }
        if (vst)
            av_add_index_entry(vst, offset, i * rpl->frames_per_chunk,
                               video_size, rpl->frames_per_chunk, 0);
        if (ast)
            av_add_index_entry(ast, offset + video_size, total_audio_size,
                               audio_size, audio_size * 8, 0);
        total_audio_size += audio_size * 8;
    }

    if (error)
        return AVERROR(EIO);

    return 0;
}

static int rpl_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RPLContext *rpl = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream* stream;
    FFStream *sti;
    AVIndexEntry* index_entry;
    int ret;

    if (rpl->chunk_part == s->nb_streams) {
        rpl->chunk_number++;
        rpl->chunk_part = 0;
    }

    stream = s->streams[rpl->chunk_part];
    sti    = ffstream(stream);

    if (rpl->chunk_number >= sti->nb_index_entries)
        return AVERROR_EOF;

    index_entry = &sti->index_entries[rpl->chunk_number];

    if (rpl->frame_in_part == 0) {
        if (avio_seek(pb, index_entry->pos, SEEK_SET) < 0)
            return AVERROR(EIO);
    }

    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
        stream->codecpar->codec_tag == 124) {
        // We have to split Escape 124 frames because there are
        // multiple frames per chunk in Escape 124 samples.
        uint32_t frame_size;

        avio_skip(pb, 4); /* flags */
        frame_size = avio_rl32(pb);
        if (avio_feof(pb) || avio_seek(pb, -8, SEEK_CUR) < 0 || !frame_size)
            return AVERROR(EIO);

        ret = av_get_packet(pb, pkt, frame_size);
        if (ret < 0)
            return ret;
        if (ret != frame_size)
            return AVERROR(EIO);

        pkt->duration = 1;
        pkt->pts = index_entry->timestamp + rpl->frame_in_part;
        pkt->stream_index = rpl->chunk_part;

        rpl->frame_in_part++;
        if (rpl->frame_in_part == rpl->frames_per_chunk) {
            rpl->frame_in_part = 0;
            rpl->chunk_part++;
        }
    } else {
        ret = av_get_packet(pb, pkt, index_entry->size);
        if (ret < 0)
            return ret;
        if (ret != index_entry->size)
            return AVERROR(EIO);

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // frames_per_chunk should always be one here; the header
            // parsing will warn if it isn't.
            pkt->duration = rpl->frames_per_chunk;
        } else {
            // All the audio codecs supported in this container
            // (at least so far) are constant-bitrate.
            pkt->duration = ret * 8;
        }
        pkt->pts = index_entry->timestamp;
        pkt->stream_index = rpl->chunk_part;
        rpl->chunk_part++;
    }

    // None of the Escape formats have keyframes, and the ADPCM
    // format used doesn't have keyframes.
    if (rpl->chunk_number == 0 && rpl->frame_in_part == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;

    return ret;
}

const AVInputFormat ff_rpl_demuxer = {
    .name           = "rpl",
    .long_name      = NULL_IF_CONFIG_SMALL("RPL / ARMovie"),
    .priv_data_size = sizeof(RPLContext),
    .read_probe     = rpl_probe,
    .read_header    = rpl_read_header,
    .read_packet    = rpl_read_packet,
};
