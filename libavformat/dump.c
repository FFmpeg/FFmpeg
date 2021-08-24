/*
 * Various pretty-printing functions for use within FFmpeg
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include <stdio.h>
#include <stdint.h>

#include "libavutil/channel_layout.h"
#include "libavutil/display.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/replaygain.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timecode.h"

#include "avformat.h"
#include "internal.h"

#define HEXDUMP_PRINT(...)                                                    \
    do {                                                                      \
        if (!f)                                                               \
            av_log(avcl, level, __VA_ARGS__);                                 \
        else                                                                  \
            fprintf(f, __VA_ARGS__);                                          \
    } while (0)

static void hex_dump_internal(void *avcl, FILE *f, int level,
                              const uint8_t *buf, int size)
{
    int len, i, j, c;

    for (i = 0; i < size; i += 16) {
        len = size - i;
        if (len > 16)
            len = 16;
        HEXDUMP_PRINT("%08x ", i);
        for (j = 0; j < 16; j++) {
            if (j < len)
                HEXDUMP_PRINT(" %02x", buf[i + j]);
            else
                HEXDUMP_PRINT("   ");
        }
        HEXDUMP_PRINT(" ");
        for (j = 0; j < len; j++) {
            c = buf[i + j];
            if (c < ' ' || c > '~')
                c = '.';
            HEXDUMP_PRINT("%c", c);
        }
        HEXDUMP_PRINT("\n");
    }
}

void av_hex_dump(FILE *f, const uint8_t *buf, int size)
{
    hex_dump_internal(NULL, f, 0, buf, size);
}

void av_hex_dump_log(void *avcl, int level, const uint8_t *buf, int size)
{
    hex_dump_internal(avcl, NULL, level, buf, size);
}

static void pkt_dump_internal(void *avcl, FILE *f, int level, const AVPacket *pkt,
                              int dump_payload, AVRational time_base)
{
    HEXDUMP_PRINT("stream #%d:\n", pkt->stream_index);
    HEXDUMP_PRINT("  keyframe=%d\n", (pkt->flags & AV_PKT_FLAG_KEY) != 0);
    HEXDUMP_PRINT("  duration=%0.3f\n", pkt->duration * av_q2d(time_base));
    /* DTS is _always_ valid after av_read_frame() */
    HEXDUMP_PRINT("  dts=");
    if (pkt->dts == AV_NOPTS_VALUE)
        HEXDUMP_PRINT("N/A");
    else
        HEXDUMP_PRINT("%0.3f", pkt->dts * av_q2d(time_base));
    /* PTS may not be known if B-frames are present. */
    HEXDUMP_PRINT("  pts=");
    if (pkt->pts == AV_NOPTS_VALUE)
        HEXDUMP_PRINT("N/A");
    else
        HEXDUMP_PRINT("%0.3f", pkt->pts * av_q2d(time_base));
    HEXDUMP_PRINT("\n");
    HEXDUMP_PRINT("  size=%d\n", pkt->size);
    if (dump_payload)
        hex_dump_internal(avcl, f, level, pkt->data, pkt->size);
}

void av_pkt_dump2(FILE *f, const AVPacket *pkt, int dump_payload, const AVStream *st)
{
    pkt_dump_internal(NULL, f, 0, pkt, dump_payload, st->time_base);
}

void av_pkt_dump_log2(void *avcl, int level, const AVPacket *pkt, int dump_payload,
                      const AVStream *st)
{
    pkt_dump_internal(avcl, NULL, level, pkt, dump_payload, st->time_base);
}


static void print_fps(double d, const char *postfix)
{
    uint64_t v = lrintf(d * 100);
    if (!v)
        av_log(NULL, AV_LOG_INFO, "%1.4f %s", d, postfix);
    else if (v % 100)
        av_log(NULL, AV_LOG_INFO, "%3.2f %s", d, postfix);
    else if (v % (100 * 1000))
        av_log(NULL, AV_LOG_INFO, "%1.0f %s", d, postfix);
    else
        av_log(NULL, AV_LOG_INFO, "%1.0fk %s", d / 1000, postfix);
}

static void dump_metadata(void *ctx, const AVDictionary *m, const char *indent)
{
    if (m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0))) {
        const AVDictionaryEntry *tag = NULL;

        av_log(ctx, AV_LOG_INFO, "%sMetadata:\n", indent);
        while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX)))
            if (strcmp("language", tag->key)) {
                const char *p = tag->value;
                av_log(ctx, AV_LOG_INFO,
                       "%s  %-16s: ", indent, tag->key);
                while (*p) {
                    char tmp[256];
                    size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
                    av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len+1));
                    av_log(ctx, AV_LOG_INFO, "%s", tmp);
                    p += len;
                    if (*p == 0xd) av_log(ctx, AV_LOG_INFO, " ");
                    if (*p == 0xa) av_log(ctx, AV_LOG_INFO, "\n%s  %-16s: ", indent, "");
                    if (*p) p++;
                }
                av_log(ctx, AV_LOG_INFO, "\n");
            }
    }
}

/* param change side data*/
static void dump_paramchange(void *ctx, const AVPacketSideData *sd)
{
    int size = sd->size;
    const uint8_t *data = sd->data;
    uint32_t flags, channels, sample_rate, width, height;
    uint64_t layout;

    if (!data || sd->size < 4)
        goto fail;

    flags = AV_RL32(data);
    data += 4;
    size -= 4;

    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
        if (size < 4)
            goto fail;
        channels = AV_RL32(data);
        data += 4;
        size -= 4;
        av_log(ctx, AV_LOG_INFO, "channel count %"PRIu32", ", channels);
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
        if (size < 8)
            goto fail;
        layout = AV_RL64(data);
        data += 8;
        size -= 8;
        av_log(ctx, AV_LOG_INFO,
               "channel layout: %s, ", av_get_channel_name(layout));
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
        if (size < 4)
            goto fail;
        sample_rate = AV_RL32(data);
        data += 4;
        size -= 4;
        av_log(ctx, AV_LOG_INFO, "sample_rate %"PRIu32", ", sample_rate);
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
        if (size < 8)
            goto fail;
        width = AV_RL32(data);
        data += 4;
        size -= 4;
        height = AV_RL32(data);
        data += 4;
        size -= 4;
        av_log(ctx, AV_LOG_INFO, "width %"PRIu32" height %"PRIu32, width, height);
    }

    return;
fail:
    av_log(ctx, AV_LOG_ERROR, "unknown param\n");
}

/* replaygain side data*/
static void print_gain(void *ctx, const char *str, int32_t gain)
{
    av_log(ctx, AV_LOG_INFO, "%s - ", str);
    if (gain == INT32_MIN)
        av_log(ctx, AV_LOG_INFO, "unknown");
    else
        av_log(ctx, AV_LOG_INFO, "%f", gain / 100000.0f);
    av_log(ctx, AV_LOG_INFO, ", ");
}

static void print_peak(void *ctx, const char *str, uint32_t peak)
{
    av_log(ctx, AV_LOG_INFO, "%s - ", str);
    if (!peak)
        av_log(ctx, AV_LOG_INFO, "unknown");
    else
        av_log(ctx, AV_LOG_INFO, "%f", (float) peak / UINT32_MAX);
    av_log(ctx, AV_LOG_INFO, ", ");
}

static void dump_replaygain(void *ctx, const AVPacketSideData *sd)
{
    const AVReplayGain *rg;

    if (sd->size < sizeof(*rg)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }
    rg = (const AVReplayGain *)sd->data;

    print_gain(ctx, "track gain", rg->track_gain);
    print_peak(ctx, "track peak", rg->track_peak);
    print_gain(ctx, "album gain", rg->album_gain);
    print_peak(ctx, "album peak", rg->album_peak);
}

static void dump_stereo3d(void *ctx, const AVPacketSideData *sd)
{
    const AVStereo3D *stereo;

    if (sd->size < sizeof(*stereo)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    stereo = (const AVStereo3D *)sd->data;

    av_log(ctx, AV_LOG_INFO, "%s", av_stereo3d_type_name(stereo->type));

    if (stereo->flags & AV_STEREO3D_FLAG_INVERT)
        av_log(ctx, AV_LOG_INFO, " (inverted)");
}

static void dump_audioservicetype(void *ctx, const AVPacketSideData *sd)
{
    const enum AVAudioServiceType *ast = (const enum AVAudioServiceType *)sd->data;

    if (sd->size < sizeof(*ast)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    switch (*ast) {
    case AV_AUDIO_SERVICE_TYPE_MAIN:
        av_log(ctx, AV_LOG_INFO, "main");
        break;
    case AV_AUDIO_SERVICE_TYPE_EFFECTS:
        av_log(ctx, AV_LOG_INFO, "effects");
        break;
    case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED:
        av_log(ctx, AV_LOG_INFO, "visually impaired");
        break;
    case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:
        av_log(ctx, AV_LOG_INFO, "hearing impaired");
        break;
    case AV_AUDIO_SERVICE_TYPE_DIALOGUE:
        av_log(ctx, AV_LOG_INFO, "dialogue");
        break;
    case AV_AUDIO_SERVICE_TYPE_COMMENTARY:
        av_log(ctx, AV_LOG_INFO, "commentary");
        break;
    case AV_AUDIO_SERVICE_TYPE_EMERGENCY:
        av_log(ctx, AV_LOG_INFO, "emergency");
        break;
    case AV_AUDIO_SERVICE_TYPE_VOICE_OVER:
        av_log(ctx, AV_LOG_INFO, "voice over");
        break;
    case AV_AUDIO_SERVICE_TYPE_KARAOKE:
        av_log(ctx, AV_LOG_INFO, "karaoke");
        break;
    default:
        av_log(ctx, AV_LOG_WARNING, "unknown");
        break;
    }
}

static void dump_cpb(void *ctx, const AVPacketSideData *sd)
{
    const AVCPBProperties *cpb = (const AVCPBProperties *)sd->data;

    if (sd->size < sizeof(*cpb)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    av_log(ctx, AV_LOG_INFO,
           "bitrate max/min/avg: %"PRId64"/%"PRId64"/%"PRId64" buffer size: %"PRId64" ",
           cpb->max_bitrate, cpb->min_bitrate, cpb->avg_bitrate,
           cpb->buffer_size);
    if (cpb->vbv_delay == UINT64_MAX)
        av_log(ctx, AV_LOG_INFO, "vbv_delay: N/A");
    else
        av_log(ctx, AV_LOG_INFO, "vbv_delay: %"PRIu64"", cpb->vbv_delay);
}

static void dump_mastering_display_metadata(void *ctx, const AVPacketSideData *sd)
{
    const AVMasteringDisplayMetadata *metadata =
        (const AVMasteringDisplayMetadata *)sd->data;
    av_log(ctx, AV_LOG_INFO, "Mastering Display Metadata, "
           "has_primaries:%d has_luminance:%d "
           "r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f) "
           "min_luminance=%f, max_luminance=%f",
           metadata->has_primaries, metadata->has_luminance,
           av_q2d(metadata->display_primaries[0][0]),
           av_q2d(metadata->display_primaries[0][1]),
           av_q2d(metadata->display_primaries[1][0]),
           av_q2d(metadata->display_primaries[1][1]),
           av_q2d(metadata->display_primaries[2][0]),
           av_q2d(metadata->display_primaries[2][1]),
           av_q2d(metadata->white_point[0]), av_q2d(metadata->white_point[1]),
           av_q2d(metadata->min_luminance), av_q2d(metadata->max_luminance));
}

static void dump_content_light_metadata(void *ctx, const AVPacketSideData *sd)
{
    const AVContentLightMetadata *metadata =
        (const AVContentLightMetadata *)sd->data;
    av_log(ctx, AV_LOG_INFO, "Content Light Level Metadata, "
           "MaxCLL=%d, MaxFALL=%d",
           metadata->MaxCLL, metadata->MaxFALL);
}

static void dump_spherical(void *ctx, const AVCodecParameters *par,
                           const AVPacketSideData *sd)
{
    const AVSphericalMapping *spherical = (const AVSphericalMapping *)sd->data;
    double yaw, pitch, roll;

    if (sd->size < sizeof(*spherical)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    av_log(ctx, AV_LOG_INFO, "%s ", av_spherical_projection_name(spherical->projection));

    yaw = ((double)spherical->yaw) / (1 << 16);
    pitch = ((double)spherical->pitch) / (1 << 16);
    roll = ((double)spherical->roll) / (1 << 16);
    av_log(ctx, AV_LOG_INFO, "(%f/%f/%f) ", yaw, pitch, roll);

    if (spherical->projection == AV_SPHERICAL_EQUIRECTANGULAR_TILE) {
        size_t l, t, r, b;
        av_spherical_tile_bounds(spherical, par->width, par->height,
                                 &l, &t, &r, &b);
        av_log(ctx, AV_LOG_INFO,
               "[%"SIZE_SPECIFIER", %"SIZE_SPECIFIER", %"SIZE_SPECIFIER", %"SIZE_SPECIFIER"] ",
               l, t, r, b);
    } else if (spherical->projection == AV_SPHERICAL_CUBEMAP) {
        av_log(ctx, AV_LOG_INFO, "[pad %"PRIu32"] ", spherical->padding);
    }
}

static void dump_dovi_conf(void *ctx, const AVPacketSideData *sd)
{
    const AVDOVIDecoderConfigurationRecord *dovi =
        (const AVDOVIDecoderConfigurationRecord *)sd->data;

    av_log(ctx, AV_LOG_INFO, "version: %d.%d, profile: %d, level: %d, "
           "rpu flag: %d, el flag: %d, bl flag: %d, compatibility id: %d",
           dovi->dv_version_major, dovi->dv_version_minor,
           dovi->dv_profile, dovi->dv_level,
           dovi->rpu_present_flag,
           dovi->el_present_flag,
           dovi->bl_present_flag,
           dovi->dv_bl_signal_compatibility_id);
}

static void dump_s12m_timecode(void *ctx, const AVStream *st, const AVPacketSideData *sd)
{
    const uint32_t *tc = (const uint32_t *)sd->data;

    if ((sd->size != sizeof(uint32_t) * 4) || (tc[0] > 3)) {
        av_log(ctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    for (int j = 1; j <= tc[0]; j++) {
        char tcbuf[AV_TIMECODE_STR_SIZE];
        av_timecode_make_smpte_tc_string2(tcbuf, st->avg_frame_rate, tc[j], 0, 0);
        av_log(ctx, AV_LOG_INFO, "timecode - %s%s", tcbuf, j != tc[0] ? ", " : "");
    }
}

static void dump_sidedata(void *ctx, const AVStream *st, const char *indent)
{
    int i;

    if (st->nb_side_data)
        av_log(ctx, AV_LOG_INFO, "%sSide data:\n", indent);

    for (i = 0; i < st->nb_side_data; i++) {
        const AVPacketSideData *sd = &st->side_data[i];
        av_log(ctx, AV_LOG_INFO, "%s  ", indent);

        switch (sd->type) {
        case AV_PKT_DATA_PALETTE:
            av_log(ctx, AV_LOG_INFO, "palette");
            break;
        case AV_PKT_DATA_NEW_EXTRADATA:
            av_log(ctx, AV_LOG_INFO, "new extradata");
            break;
        case AV_PKT_DATA_PARAM_CHANGE:
            av_log(ctx, AV_LOG_INFO, "paramchange: ");
            dump_paramchange(ctx, sd);
            break;
        case AV_PKT_DATA_H263_MB_INFO:
            av_log(ctx, AV_LOG_INFO, "H.263 macroblock info");
            break;
        case AV_PKT_DATA_REPLAYGAIN:
            av_log(ctx, AV_LOG_INFO, "replaygain: ");
            dump_replaygain(ctx, sd);
            break;
        case AV_PKT_DATA_DISPLAYMATRIX:
            av_log(ctx, AV_LOG_INFO, "displaymatrix: rotation of %.2f degrees",
                   av_display_rotation_get((const int32_t *)sd->data));
            break;
        case AV_PKT_DATA_STEREO3D:
            av_log(ctx, AV_LOG_INFO, "stereo3d: ");
            dump_stereo3d(ctx, sd);
            break;
        case AV_PKT_DATA_AUDIO_SERVICE_TYPE:
            av_log(ctx, AV_LOG_INFO, "audio service type: ");
            dump_audioservicetype(ctx, sd);
            break;
        case AV_PKT_DATA_QUALITY_STATS:
            av_log(ctx, AV_LOG_INFO, "quality factor: %"PRId32", pict_type: %c",
                   AV_RL32(sd->data), av_get_picture_type_char(sd->data[4]));
            break;
        case AV_PKT_DATA_CPB_PROPERTIES:
            av_log(ctx, AV_LOG_INFO, "cpb: ");
            dump_cpb(ctx, sd);
            break;
        case AV_PKT_DATA_MASTERING_DISPLAY_METADATA:
            dump_mastering_display_metadata(ctx, sd);
            break;
        case AV_PKT_DATA_SPHERICAL:
            av_log(ctx, AV_LOG_INFO, "spherical: ");
            dump_spherical(ctx, st->codecpar, sd);
            break;
        case AV_PKT_DATA_CONTENT_LIGHT_LEVEL:
            dump_content_light_metadata(ctx, sd);
            break;
        case AV_PKT_DATA_ICC_PROFILE:
            av_log(ctx, AV_LOG_INFO, "ICC Profile");
            break;
        case AV_PKT_DATA_DOVI_CONF:
            av_log(ctx, AV_LOG_INFO, "DOVI configuration record: ");
            dump_dovi_conf(ctx, sd);
            break;
        case AV_PKT_DATA_S12M_TIMECODE:
            av_log(ctx, AV_LOG_INFO, "SMPTE ST 12-1:2014: ");
            dump_s12m_timecode(ctx, st, sd);
            break;
        default:
            av_log(ctx, AV_LOG_INFO, "unknown side data type %d "
                   "(%"SIZE_SPECIFIER" bytes)", sd->type, sd->size);
            break;
        }

        av_log(ctx, AV_LOG_INFO, "\n");
    }
}

/* "user interface" functions */
static void dump_stream_format(const AVFormatContext *ic, int i,
                               int index, int is_output)
{
    char buf[256];
    int flags = (is_output ? ic->oformat->flags : ic->iformat->flags);
    const AVStream *st = ic->streams[i];
    const FFStream *const sti = cffstream(st);
    const AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);
    const char *separator = ic->dump_separator;
    AVCodecContext *avctx;
    int ret;

    avctx = avcodec_alloc_context3(NULL);
    if (!avctx)
        return;

    ret = avcodec_parameters_to_context(avctx, st->codecpar);
    if (ret < 0) {
        avcodec_free_context(&avctx);
        return;
    }

    // Fields which are missing from AVCodecParameters need to be taken from the AVCodecContext
    avctx->properties   = sti->avctx->properties;
    avctx->codec        = sti->avctx->codec;
    avctx->qmin         = sti->avctx->qmin;
    avctx->qmax         = sti->avctx->qmax;
    avctx->coded_width  = sti->avctx->coded_width;
    avctx->coded_height = sti->avctx->coded_height;

    if (separator)
        av_opt_set(avctx, "dump_separator", separator, 0);
    avcodec_string(buf, sizeof(buf), avctx, is_output);
    avcodec_free_context(&avctx);

    av_log(NULL, AV_LOG_INFO, "  Stream #%d:%d", index, i);

    /* the pid is an important information, so we display it */
    /* XXX: add a generic system */
    if (flags & AVFMT_SHOW_IDS)
        av_log(NULL, AV_LOG_INFO, "[0x%x]", st->id);
    if (lang)
        av_log(NULL, AV_LOG_INFO, "(%s)", lang->value);
    av_log(NULL, AV_LOG_DEBUG, ", %d, %d/%d", sti->codec_info_nb_frames,
           st->time_base.num, st->time_base.den);
    av_log(NULL, AV_LOG_INFO, ": %s", buf);

    if (st->sample_aspect_ratio.num &&
        av_cmp_q(st->sample_aspect_ratio, st->codecpar->sample_aspect_ratio)) {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codecpar->width  * (int64_t)st->sample_aspect_ratio.num,
                  st->codecpar->height * (int64_t)st->sample_aspect_ratio.den,
                  1024 * 1024);
        av_log(NULL, AV_LOG_INFO, ", SAR %d:%d DAR %d:%d",
               st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
               display_aspect_ratio.num, display_aspect_ratio.den);
    }

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        int fps = st->avg_frame_rate.den && st->avg_frame_rate.num;
        int tbr = st->r_frame_rate.den && st->r_frame_rate.num;
        int tbn = st->time_base.den && st->time_base.num;

        if (fps || tbr || tbn)
            av_log(NULL, AV_LOG_INFO, "%s", separator);

        if (fps)
            print_fps(av_q2d(st->avg_frame_rate), tbr || tbn ? "fps, " : "fps");
        if (tbr)
            print_fps(av_q2d(st->r_frame_rate), tbn ? "tbr, " : "tbr");
        if (tbn)
            print_fps(1 / av_q2d(st->time_base), "tbn");
    }

    if (st->disposition & AV_DISPOSITION_DEFAULT)
        av_log(NULL, AV_LOG_INFO, " (default)");
    if (st->disposition & AV_DISPOSITION_DUB)
        av_log(NULL, AV_LOG_INFO, " (dub)");
    if (st->disposition & AV_DISPOSITION_ORIGINAL)
        av_log(NULL, AV_LOG_INFO, " (original)");
    if (st->disposition & AV_DISPOSITION_COMMENT)
        av_log(NULL, AV_LOG_INFO, " (comment)");
    if (st->disposition & AV_DISPOSITION_LYRICS)
        av_log(NULL, AV_LOG_INFO, " (lyrics)");
    if (st->disposition & AV_DISPOSITION_KARAOKE)
        av_log(NULL, AV_LOG_INFO, " (karaoke)");
    if (st->disposition & AV_DISPOSITION_FORCED)
        av_log(NULL, AV_LOG_INFO, " (forced)");
    if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
        av_log(NULL, AV_LOG_INFO, " (hearing impaired)");
    if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
        av_log(NULL, AV_LOG_INFO, " (visual impaired)");
    if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS)
        av_log(NULL, AV_LOG_INFO, " (clean effects)");
    if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
        av_log(NULL, AV_LOG_INFO, " (attached pic)");
    if (st->disposition & AV_DISPOSITION_TIMED_THUMBNAILS)
        av_log(NULL, AV_LOG_INFO, " (timed thumbnails)");
    if (st->disposition & AV_DISPOSITION_CAPTIONS)
        av_log(NULL, AV_LOG_INFO, " (captions)");
    if (st->disposition & AV_DISPOSITION_DESCRIPTIONS)
        av_log(NULL, AV_LOG_INFO, " (descriptions)");
    if (st->disposition & AV_DISPOSITION_METADATA)
        av_log(NULL, AV_LOG_INFO, " (metadata)");
    if (st->disposition & AV_DISPOSITION_DEPENDENT)
        av_log(NULL, AV_LOG_INFO, " (dependent)");
    if (st->disposition & AV_DISPOSITION_STILL_IMAGE)
        av_log(NULL, AV_LOG_INFO, " (still image)");
    av_log(NULL, AV_LOG_INFO, "\n");

    dump_metadata(NULL, st->metadata, "    ");

    dump_sidedata(NULL, st, "    ");
}

void av_dump_format(AVFormatContext *ic, int index,
                    const char *url, int is_output)
{
    int i;
    uint8_t *printed = ic->nb_streams ? av_mallocz(ic->nb_streams) : NULL;
    if (ic->nb_streams && !printed)
        return;

    av_log(NULL, AV_LOG_INFO, "%s #%d, %s, %s '%s':\n",
           is_output ? "Output" : "Input",
           index,
           is_output ? ic->oformat->name : ic->iformat->name,
           is_output ? "to" : "from", url);
    dump_metadata(NULL, ic->metadata, "  ");

    if (!is_output) {
        av_log(NULL, AV_LOG_INFO, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int64_t hours, mins, secs, us;
            int64_t duration = ic->duration + (ic->duration <= INT64_MAX - 5000 ? 5000 : 0);
            secs  = duration / AV_TIME_BASE;
            us    = duration % AV_TIME_BASE;
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            av_log(NULL, AV_LOG_INFO, "%02"PRId64":%02"PRId64":%02"PRId64".%02"PRId64"", hours, mins, secs,
                   (100 * us) / AV_TIME_BASE);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            av_log(NULL, AV_LOG_INFO, ", start: ");
            secs = llabs(ic->start_time / AV_TIME_BASE);
            us   = llabs(ic->start_time % AV_TIME_BASE);
            av_log(NULL, AV_LOG_INFO, "%s%d.%06d",
                   ic->start_time >= 0 ? "" : "-",
                   secs,
                   (int) av_rescale(us, 1000000, AV_TIME_BASE));
        }
        av_log(NULL, AV_LOG_INFO, ", bitrate: ");
        if (ic->bit_rate)
            av_log(NULL, AV_LOG_INFO, "%"PRId64" kb/s", ic->bit_rate / 1000);
        else
            av_log(NULL, AV_LOG_INFO, "N/A");
        av_log(NULL, AV_LOG_INFO, "\n");
    }

    if (ic->nb_chapters)
        av_log(NULL, AV_LOG_INFO, "  Chapters:\n");
    for (i = 0; i < ic->nb_chapters; i++) {
        const AVChapter *ch = ic->chapters[i];
        av_log(NULL, AV_LOG_INFO, "    Chapter #%d:%d: ", index, i);
        av_log(NULL, AV_LOG_INFO,
               "start %f, ", ch->start * av_q2d(ch->time_base));
        av_log(NULL, AV_LOG_INFO,
               "end %f\n", ch->end * av_q2d(ch->time_base));

        dump_metadata(NULL, ch->metadata, "      ");
    }

    if (ic->nb_programs) {
        int j, k, total = 0;
        for (j = 0; j < ic->nb_programs; j++) {
            const AVProgram *program = ic->programs[j];
            const AVDictionaryEntry *name = av_dict_get(program->metadata,
                                                        "name", NULL, 0);
            av_log(NULL, AV_LOG_INFO, "  Program %d %s\n", program->id,
                   name ? name->value : "");
            dump_metadata(NULL, program->metadata, "    ");
            for (k = 0; k < program->nb_stream_indexes; k++) {
                dump_stream_format(ic, program->stream_index[k],
                                   index, is_output);
                printed[program->stream_index[k]] = 1;
            }
            total += program->nb_stream_indexes;
        }
        if (total < ic->nb_streams)
            av_log(NULL, AV_LOG_INFO, "  No Program\n");
    }

    for (i = 0; i < ic->nb_streams; i++)
        if (!printed[i])
            dump_stream_format(ic, i, index, is_output);

    av_free(printed);
}
