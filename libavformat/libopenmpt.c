/*
 * Tracked MOD demuxer (libopenmpt)
 * Copyright (c) 2016 Josh de Kock
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

#include <libopenmpt/libopenmpt.h>
#include <libopenmpt/libopenmpt_stream_callbacks_file.h>
#include <libopenmpt/libopenmpt_version.h>
/* Shims to support libopenmpt < 0.3.0 (as documented by libopenmpt) */
#if !defined(OPENMPT_API_VERSION_MAKE)
#define OPENMPT_API_VERSION_MAKE(major, minor, patch) (((major)<<24)|((minor)<<16)|((patch)<<0))
#endif
#if !defined(OPENMPT_API_VERSION_AT_LEAST)
#define OPENMPT_API_VERSION_AT_LEAST(major, minor, patch) (OPENMPT_API_VERSION >= OPENMPT_API_VERSION_MAKE((major), (minor), (patch)))
#endif

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

typedef struct OpenMPTContext {
    const AVClass *class;
    openmpt_module *module;

    int channels;
    double duration;
    /* options */
    int sample_rate;
    int64_t layout;
    int subsong;
} OpenMPTContext;

#define OFFSET(x) offsetof(OpenMPTContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "sample_rate", "set sample rate",    OFFSET(sample_rate), AV_OPT_TYPE_INT,            { .i64 = 48000 },               1000, INT_MAX,   A | D },
    { "layout",      "set channel layout", OFFSET(layout),      AV_OPT_TYPE_CHANNEL_LAYOUT, { .i64 = AV_CH_LAYOUT_STEREO }, 0,    INT64_MAX, A | D },
    { "subsong",     "set subsong",        OFFSET(subsong),     AV_OPT_TYPE_INT,            { .i64 = -2 },                  -2,   INT_MAX,   A | D, "subsong"},
    { "all",         "all",                0,                   AV_OPT_TYPE_CONST,          { .i64 = -1},                   0,    0,         A | D, "subsong" },
    { "auto",        "auto",               0,                   AV_OPT_TYPE_CONST,          { .i64 = -2},                   0,    0,         A | D, "subsong" },
    { NULL }
};

static void openmpt_logfunc(const char *message, void *userdata)
{
    int level = AV_LOG_INFO;
    if (strstr(message, "ERROR") != NULL) {
        level = AV_LOG_ERROR;
    }
    av_log(userdata, level, "%s\n", message);
}

#define add_meta(s, name, meta)                    \
do {                                               \
    const char *value = meta;                      \
    if (value && value[0])                         \
        av_dict_set(&s->metadata, name, value, 0); \
    openmpt_free_string(value);                    \
} while(0)

static int read_header_openmpt(AVFormatContext *s)
{
    AVStream *st;
    OpenMPTContext *openmpt = s->priv_data;
    int64_t size;
    char *buf;
#if OPENMPT_API_VERSION_AT_LEAST(0,3,0)
    int error;
#endif
    int ret;

    size = avio_size(s->pb);
    if (size <= 0)
        return AVERROR_INVALIDDATA;
    buf = av_malloc(size);
    if (!buf)
        return AVERROR(ENOMEM);
    size = avio_read(s->pb, buf, size);
    if (size < 0) {
        av_log(s, AV_LOG_ERROR, "Reading input buffer failed.\n");
        av_freep(&buf);
        return size;
    }

#if OPENMPT_API_VERSION_AT_LEAST(0,3,0)
    error = OPENMPT_ERROR_OK;
    openmpt->module = openmpt_module_create_from_memory2(buf, size, openmpt_logfunc, s, NULL, NULL, &error, NULL, NULL);
    av_freep(&buf);
    if (!openmpt->module) {
        if (error == OPENMPT_ERROR_OUT_OF_MEMORY)
            return AVERROR(ENOMEM);
        else if (error >= OPENMPT_ERROR_GENERAL)
            return AVERROR_INVALIDDATA;
        else
            return AVERROR_UNKNOWN;
    }
#else
    openmpt->module = openmpt_module_create_from_memory(buf, size, openmpt_logfunc, s, NULL);
    av_freep(&buf);
    if (!openmpt->module)
            return AVERROR_INVALIDDATA;
#endif

    openmpt->channels = av_get_channel_layout_nb_channels(openmpt->layout);

    if (openmpt->subsong >= openmpt_module_get_num_subsongs(openmpt->module)) {
        openmpt_module_destroy(openmpt->module);
        av_log(s, AV_LOG_ERROR, "Invalid subsong index: %d\n", openmpt->subsong);
        return AVERROR(EINVAL);
    }

    if (openmpt->subsong != -2) {
        if (openmpt->subsong >= 0) {
            av_dict_set_int(&s->metadata, "track", openmpt->subsong + 1, 0);
        }
        ret = openmpt_module_select_subsong(openmpt->module, openmpt->subsong);
        if (!ret){
            openmpt_module_destroy(openmpt->module);
            av_log(s, AV_LOG_ERROR, "Could not select requested subsong: %d", openmpt->subsong);
            return AVERROR(EINVAL);
        }
    }

    openmpt->duration = openmpt_module_get_duration_seconds(openmpt->module);

    add_meta(s, "artist",  openmpt_module_get_metadata(openmpt->module, "artist"));
    add_meta(s, "title",   openmpt_module_get_metadata(openmpt->module, "title"));
    add_meta(s, "encoder", openmpt_module_get_metadata(openmpt->module, "tracker"));
    add_meta(s, "comment", openmpt_module_get_metadata(openmpt->module, "message"));
    add_meta(s, "date",    openmpt_module_get_metadata(openmpt->module, "date"));

    st = avformat_new_stream(s, NULL);
    if (!st) {
        openmpt_module_destroy(openmpt->module);
        openmpt->module = NULL;
        return AVERROR(ENOMEM);
    }
    avpriv_set_pts_info(st, 64, 1, AV_TIME_BASE);
    st->duration = llrint(openmpt->duration*AV_TIME_BASE);

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = AV_NE(AV_CODEC_ID_PCM_F32BE, AV_CODEC_ID_PCM_F32LE);
    st->codecpar->channels    = openmpt->channels;
    st->codecpar->sample_rate = openmpt->sample_rate;

    return 0;
}

#define AUDIO_PKT_SIZE 2048

static int read_packet_openmpt(AVFormatContext *s, AVPacket *pkt)
{
    OpenMPTContext *openmpt = s->priv_data;
    int n_samples = AUDIO_PKT_SIZE / (openmpt->channels ? openmpt->channels*4 : 4);
    int ret;

    if ((ret = av_new_packet(pkt, AUDIO_PKT_SIZE)) < 0)
        return ret;

    switch (openmpt->channels) {
    case 1:
        ret = openmpt_module_read_float_mono(openmpt->module, openmpt->sample_rate,
                                             n_samples, (float *)pkt->data);
        break;
    case 2:
        ret = openmpt_module_read_interleaved_float_stereo(openmpt->module, openmpt->sample_rate,
                                                           n_samples, (float *)pkt->data);
        break;
    case 4:
        ret = openmpt_module_read_interleaved_float_quad(openmpt->module, openmpt->sample_rate,
                                                         n_samples, (float *)pkt->data);
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unsupported number of channels: %d", openmpt->channels);
        return AVERROR(EINVAL);
    }

    if (ret < 1) {
        pkt->size = 0;
        return AVERROR_EOF;
    }

    pkt->size = ret * (openmpt->channels * 4);

    return 0;
}

static int read_close_openmpt(AVFormatContext *s)
{
    OpenMPTContext *openmpt = s->priv_data;
    openmpt_module_destroy(openmpt->module);
    openmpt->module = NULL;
    return 0;
}

static int read_seek_openmpt(AVFormatContext *s, int stream_idx, int64_t ts, int flags)
{
    OpenMPTContext *openmpt = s->priv_data;
    openmpt_module_set_position_seconds(openmpt->module, (double)ts/AV_TIME_BASE);
    return 0;
}

static int probe_openmpt_extension(const AVProbeData *p)
{
    const char *ext;
    if (p->filename) {
        ext = strrchr(p->filename, '.');
        if (ext && strlen(ext + 1) > 0) {
            ext++;  /* skip '.' */
            if (openmpt_is_extension_supported(ext) == 1)
                return AVPROBE_SCORE_EXTENSION;
        }
    }
    return 0;
}

static int read_probe_openmpt(const AVProbeData *p)
{
#if OPENMPT_API_VERSION_AT_LEAST(0,3,0)
    int probe_result;
    if (p->buf && p->buf_size > 0) {
        probe_result = openmpt_probe_file_header_without_filesize(
                           OPENMPT_PROBE_FILE_HEADER_FLAGS_DEFAULT,
                           p->buf, p->buf_size,
                           &openmpt_logfunc, NULL, NULL, NULL, NULL, NULL);
        if (probe_result == OPENMPT_PROBE_FILE_HEADER_RESULT_SUCCESS) {
            /* As probing here relies on code external to FFmpeg, do not return
             * AVPROBE_SCORE_MAX in order to reduce the impact in the rare
             * cases of false positives.
             */
            return AVPROBE_SCORE_MIME + 1;
        } else if (probe_result == OPENMPT_PROBE_FILE_HEADER_RESULT_WANTMOREDATA) {
            if (probe_openmpt_extension(p) > 0) {
                return AVPROBE_SCORE_RETRY;
            } else {
                if (p->buf_size >= openmpt_probe_file_header_get_recommended_size()) {
                    /* We have already received the recommended amount of data
                     * and still cannot decide. Return a rather low score.
                     */
                    return AVPROBE_SCORE_RETRY / 2;
                } else {
                    /* The file extension is unknown and we have very few data
                     * bytes available. libopenmpt cannot decide anything here,
                     * and returning any score > 0 would result in successful
                     * probing of random data.
                     */
                    return 0;
                }
            }
        } else if (probe_result == OPENMPT_PROBE_FILE_HEADER_RESULT_FAILURE) {
            return 0;
        }
    }
#endif
    /* for older libopenmpt, fall back to file extension probing */
    return probe_openmpt_extension(p);
}

static const AVClass class_openmpt = {
    .class_name = "libopenmpt",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_libopenmpt_demuxer = {
    .name           = "libopenmpt",
    .long_name      = NULL_IF_CONFIG_SMALL("Tracker formats (libopenmpt)"),
    .priv_data_size = sizeof(OpenMPTContext),
    .read_probe     = read_probe_openmpt,
    .read_header    = read_header_openmpt,
    .read_packet    = read_packet_openmpt,
    .read_close     = read_close_openmpt,
    .read_seek      = read_seek_openmpt,
    .priv_class     = &class_openmpt,
#if OPENMPT_API_VERSION_AT_LEAST(0,3,0)
    .extensions     = "669,amf,ams,dbm,digi,dmf,dsm,dtm,far,gdm,ice,imf,it,j2b,m15,mdl,med,mmcmp,mms,mo3,mod,mptm,mt2,mtm,nst,okt,plm,ppm,psm,pt36,ptm,s3m,sfx,sfx2,st26,stk,stm,stp,ult,umx,wow,xm,xpk",
#else
    .extensions     = "669,amf,ams,dbm,digi,dmf,dsm,far,gdm,ice,imf,it,j2b,m15,mdl,med,mmcmp,mms,mo3,mod,mptm,mt2,mtm,nst,okt,plm,ppm,psm,pt36,ptm,s3m,sfx,sfx2,st26,stk,stm,ult,umx,wow,xm,xpk",
#endif
};
