/*
 * MCC subtitle muxer
 * Copyright (c) 2025 Jacob Lifshay
 * Copyright (c) 2017 Paul B Mahol
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

#include "avformat.h"
#include "internal.h"
#include "mux.h"

#include "libavcodec/codec_id.h"
#include "libavcodec/smpte_436m.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/error.h"
#include "libavutil/ffversion.h"
#include "libavutil/log.h"
#include "libavutil/macros.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/rational.h"
#include "libavutil/time_internal.h" // for localtime_r
#include "libavutil/timecode.h"

typedef struct MCCContext {
    const AVClass *class;
    AVTimecode timecode;
    int64_t    twenty_four_hr;
    char      *override_time_code_rate;
    int        use_u_alias;
    unsigned   mcc_version;
    char      *creation_program;
    char      *creation_time;
} MCCContext;

typedef enum MCCVersion
{
    MCC_VERSION_1   = 1,
    MCC_VERSION_2   = 2,
    MCC_VERSION_MIN = MCC_VERSION_1,
    MCC_VERSION_MAX = MCC_VERSION_2,
} MCCVersion;

static const char mcc_header_v1[] = //
    "File Format=MacCaption_MCC V1.0\n"
    "\n"
    "///////////////////////////////////////////////////////////////////////////////////\n"
    "// Computer Prompting and Captioning Company\n"
    "// Ancillary Data Packet Transfer File\n"
    "//\n"
    "// Permission to generate this format is granted provided that\n"
    "//   1. This ANC Transfer file format is used on an as-is basis and no warranty is given, and\n"
    "//   2. This entire descriptive information text is included in a generated .mcc file.\n"
    "//\n"
    "// General file format:\n"
    "//   HH:MM:SS:FF(tab)[Hexadecimal ANC data in groups of 2 characters]\n"
    "//     Hexadecimal data starts with the Ancillary Data Packet DID (Data ID defined in S291M)\n"
    "//       and concludes with the Check Sum following the User Data Words.\n"
    "//     Each time code line must contain at most one complete ancillary data packet.\n"
    "//     To transfer additional ANC Data successive lines may contain identical time code.\n"
    "//     Time Code Rate=[24, 25, 30, 30DF, 50, 60]\n"
    "//\n"
    "//   ANC data bytes may be represented by one ASCII character according to the following schema:\n"
    "//     G  FAh 00h 00h\n"
    "//     H  2 x (FAh 00h 00h)\n"
    "//     I  3 x (FAh 00h 00h)\n"
    "//     J  4 x (FAh 00h 00h)\n"
    "//     K  5 x (FAh 00h 00h)\n"
    "//     L  6 x (FAh 00h 00h)\n"
    "//     M  7 x (FAh 00h 00h)\n"
    "//     N  8 x (FAh 00h 00h)\n"
    "//     O  9 x (FAh 00h 00h)\n"
    "//     P  FBh 80h 80h\n"
    "//     Q  FCh 80h 80h\n"
    "//     R  FDh 80h 80h\n"
    "//     S  96h 69h\n"
    "//     T  61h 01h\n"
    "//     U  E1h 00h 00h 00h\n"
    "//     Z  00h\n"
    "//\n"
    "///////////////////////////////////////////////////////////////////////////////////\n";

static const char mcc_header_v2[] = //
    "File Format=MacCaption_MCC V2.0\n"
    "\n"
    "///////////////////////////////////////////////////////////////////////////////////\n"
    "// Computer Prompting and Captioning Company\n"
    "// Ancillary Data Packet Transfer File\n"
    "//\n"
    "// Permission to generate this format is granted provided that\n"
    "//   1. This ANC Transfer file format is used on an as-is basis and no warranty is given, and\n"
    "//   2. This entire descriptive information text is included in a generated .mcc file.\n"
    "//\n"
    "// General file format:\n"
    "//   HH:MM:SS:FF(tab)[Hexadecimal ANC data in groups of 2 characters]\n"
    "//     Hexadecimal data starts with the Ancillary Data Packet DID (Data ID defined in S291M)\n"
    "//       and concludes with the Check Sum following the User Data Words.\n"
    "//     Each time code line must contain at most one complete ancillary data packet.\n"
    "//     To transfer additional ANC Data successive lines may contain identical time code.\n"
    "//     Time Code Rate=[24, 25, 30, 30DF, 50, 60, 60DF]\n"
    "//\n"
    "//   ANC data bytes may be represented by one ASCII character according to the following schema:\n"
    "//     G  FAh 00h 00h\n"
    "//     H  2 x (FAh 00h 00h)\n"
    "//     I  3 x (FAh 00h 00h)\n"
    "//     J  4 x (FAh 00h 00h)\n"
    "//     K  5 x (FAh 00h 00h)\n"
    "//     L  6 x (FAh 00h 00h)\n"
    "//     M  7 x (FAh 00h 00h)\n"
    "//     N  8 x (FAh 00h 00h)\n"
    "//     O  9 x (FAh 00h 00h)\n"
    "//     P  FBh 80h 80h\n"
    "//     Q  FCh 80h 80h\n"
    "//     R  FDh 80h 80h\n"
    "//     S  96h 69h\n"
    "//     T  61h 01h\n"
    "//     U  E1h 00h 00h 00h\n"
    "//     Z  00h\n"
    "//\n"
    "///////////////////////////////////////////////////////////////////////////////////\n";

/**
 * generated with the bash command:
 * ```bash
 * URL="https://code.ffmpeg.org/FFmpeg/FFmpeg/src/branch/master/libavformat/mccenc.c"
 * python3 -c "from uuid import *; print(str(uuid5(NAMESPACE_URL, '$URL')).upper())"
 * ```
 */
static const char mcc_ffmpeg_uuid[] = "0087C4F6-A6B4-5469-8C8E-BBF44950401D";

static AVRational valid_time_code_rates[] = {
    { .num = 24,    .den = 1    },
    { .num = 25,    .den = 1    },
    { .num = 30000, .den = 1001 },
    { .num = 30,    .den = 1    },
    { .num = 50,    .den = 1    },
    { .num = 60000, .den = 1001 },
    { .num = 60,    .den = 1    },
};

static int mcc_write_header(AVFormatContext *avf)
{
    MCCContext *mcc = avf->priv_data;
    if (avf->nb_streams != 1) {
        av_log(avf, AV_LOG_ERROR, "mcc muxer supports at most one stream\n");
        return AVERROR(EINVAL);
    }
    avpriv_set_pts_info(avf->streams[0], 64, mcc->timecode.rate.den, mcc->timecode.rate.num);
    const char *mcc_header = mcc_header_v1;
    switch ((MCCVersion)mcc->mcc_version) {
    case MCC_VERSION_1:
        if (mcc->timecode.fps == 60 && mcc->timecode.flags & AV_TIMECODE_FLAG_DROPFRAME) {
            av_log(avf, AV_LOG_FATAL, "MCC Version 1.0 doesn't support 60DF (59.94 fps drop-frame)");
            return AVERROR(EINVAL);
        }
        break;
    case MCC_VERSION_2:
        mcc_header = mcc_header_v2;
        break;
    }
    const char *creation_program = mcc->creation_program;
    if (!creation_program) {
        if (avf->flags & AVFMT_FLAG_BITEXACT)
            creation_program = "FFmpeg";
        else
            creation_program = "FFmpeg version " FFMPEG_VERSION;
    } else if (strchr(creation_program, '\n')) {
        av_log(avf, AV_LOG_FATAL, "creation_program must not contain multiple lines of text");
        return AVERROR(EINVAL);
    }
    if (avf->flags & AVFMT_FLAG_BITEXACT && !av_strcasecmp(mcc->creation_time, "now"))
        av_log(avf, AV_LOG_ERROR, "creation_time must be overridden for bit-exact output");
    int64_t timeval = 0;
    int     ret     = av_parse_time(&timeval, mcc->creation_time, 0);
    if (ret < 0) {
        av_log(avf, AV_LOG_FATAL, "can't parse creation_time");
        return ret;
    }
    struct tm tm;
    if (!localtime_r((time_t[1]){ timeval / 1000000 }, &tm))
        return AVERROR(EINVAL);
    // we can't rely on having the C locale, so convert the date/time to a string ourselves:
    static const char *const months[12] = {
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    };
    // assert that values are sane so we don't index out of bounds
    av_assert0(tm.tm_mon >= 0 && tm.tm_mon <= FF_ARRAY_ELEMS(months));
    const char *month = months[tm.tm_mon];

    static const char *const weekdays[7] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    // assert that values are sane so we don't index out of bounds
    av_assert0(tm.tm_wday >= 0 && tm.tm_wday < FF_ARRAY_ELEMS(weekdays));
    const char *weekday = weekdays[tm.tm_wday];

    avio_printf(avf->pb,
                "%s\n"
                "UUID=%s\n"
                "Creation Program=%s\n"
                "Creation Date=%s, %s %d, %d\n"
                "Creation Time=%02d:%02d:%02d\n"
                "Time Code Rate=%u%s\n\n",
                mcc_header,
                mcc_ffmpeg_uuid,
                creation_program,
                weekday,
                month,
                tm.tm_mday,
                tm.tm_year + 1900,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                mcc->timecode.fps,
                mcc->timecode.flags & AV_TIMECODE_FLAG_DROPFRAME ? "DF" : "");

    return 0;
}

/// convert the input bytes to hexadecimal with mcc's aliases
static void mcc_bytes_to_hex(char *dest, const uint8_t *bytes, size_t bytes_size, int use_u_alias)
{
    while (bytes_size != 0) {
        switch (bytes[0]) {
        case 0xFA:
            *dest = '\0';
            for (unsigned char code = 'G'; code <= (unsigned char)'O'; code++) {
                if (bytes_size < 3)
                    break;
                if (bytes[0] != 0xFA || bytes[1] != 0 || bytes[2] != 0)
                    break;
                *dest = code;
                bytes += 3;
                bytes_size -= 3;
            }
            if (*dest) {
                dest++;
                continue;
            }
            break;
        case 0xFB:
        case 0xFC:
        case 0xFD:
            if (bytes_size >= 3 && bytes[1] == 0x80 && bytes[2] == 0x80) {
                *dest++ = bytes[0] - 0xFB + 'P';
                bytes += 3;
                bytes_size -= 3;
                continue;
            }
            break;
        case 0x96:
            if (bytes_size >= 2 && bytes[1] == 0x69) {
                *dest++ = 'S';
                bytes += 2;
                bytes_size -= 2;
                continue;
            }
            break;
        case 0x61:
            if (bytes_size >= 2 && bytes[1] == 0x01) {
                *dest++ = 'T';
                bytes += 2;
                bytes_size -= 2;
                continue;
            }
            break;
        case 0xE1:
            if (use_u_alias && bytes_size >= 4 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0) {
                *dest++ = 'U';
                bytes += 4;
                bytes_size -= 4;
                continue;
            }
            break;
        case 0:
            *dest++ = 'Z';
            bytes++;
            bytes_size--;
            continue;
        default:
            // any other bytes falls through to writing hex
            break;
        }
        for (int shift = 4; shift >= 0; shift -= 4) {
            int v = (bytes[0] >> shift) & 0xF;
            if (v < 0xA)
                *dest++ = v + '0';
            else
                *dest++ = v - 0xA + 'A';
        }
        bytes++;
        bytes_size--;
    }
    *dest = '\0';
}

static int mcc_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    MCCContext *mcc = avf->priv_data;
    int64_t     pts = pkt->pts;
    int         ret;

    if (pts == AV_NOPTS_VALUE) {
        av_log(avf, AV_LOG_WARNING, "Insufficient timestamps.\n");
        return 0;
    }

    char timecode_str[AV_TIMECODE_STR_SIZE];

    // wrap pts values at 24hr ourselves since they can be bigger than fits in an int
    av_timecode_make_string(&mcc->timecode, timecode_str, pts % mcc->twenty_four_hr);

    for (char *p = timecode_str; *p; p++) {
        // .mcc doesn't use ; for drop-frame time codes
        if (*p == ';')
            *p = ':';
    }

    AVSmpte436mAncIterator iter;
    ret = av_smpte_436m_anc_iter_init(&iter, pkt->data, pkt->size);
    if (ret < 0)
        return ret;
    AVSmpte436mCodedAnc coded_anc;
    while ((ret = av_smpte_436m_anc_iter_next(&iter, &coded_anc)) >= 0) {
        AVSmpte291mAnc8bit anc;
        ret = av_smpte_291m_anc_8bit_decode(
            &anc, coded_anc.payload_sample_coding, coded_anc.payload_sample_count, coded_anc.payload, avf);
        if (ret < 0)
            return ret;
        // 4 for did, sdid_or_dbn, data_count, and checksum fields.
        uint8_t mcc_anc[4 + AV_SMPTE_291M_ANC_PAYLOAD_CAPACITY];
        size_t  mcc_anc_len = 0;

        mcc_anc[mcc_anc_len++] = anc.did;
        mcc_anc[mcc_anc_len++] = anc.sdid_or_dbn;
        mcc_anc[mcc_anc_len++] = anc.data_count;
        memcpy(mcc_anc + mcc_anc_len, anc.payload, anc.data_count);
        mcc_anc_len += anc.data_count;
        mcc_anc[mcc_anc_len++] = anc.checksum;

        unsigned field_number;
        switch (coded_anc.wrapping_type) {
        case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FRAME:
        case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_1:
        case AV_SMPTE_436M_WRAPPING_TYPE_VANC_PROGRESSIVE_FRAME:
            field_number = 0;
            break;
        case AV_SMPTE_436M_WRAPPING_TYPE_VANC_FIELD_2:
            field_number = 1;
            break;
        default:
            av_log(avf,
                   AV_LOG_WARNING,
                   "Unsupported SMPTE 436M ANC Wrapping Type %#x -- discarding ANC packet",
                   (unsigned)coded_anc.wrapping_type);
            continue;
        }

        char field_and_line[32] = "";
        if (coded_anc.line_number != 9) {
            snprintf(field_and_line, sizeof(field_and_line), ".%u,%u", field_number, (unsigned)coded_anc.line_number);
        } else if (field_number != 0) {
            snprintf(field_and_line, sizeof(field_and_line), ".%u", field_number);
        }

        switch ((MCCVersion)mcc->mcc_version) {
        case MCC_VERSION_1:
            if (field_and_line[0] != '\0') {
                av_log(avf,
                       AV_LOG_WARNING,
                       "MCC Version 1.0 doesn't support ANC packets where the field number (got %u) isn't 0 and "
                       "line number (got %u) isn't 9: discarding ANC packet",
                       field_number,
                       (unsigned)coded_anc.line_number);
                continue;
            }
            break;
        case MCC_VERSION_2:
            break;
        }

        // 1 for terminating nul. 2 since there's 2 hex digits per byte.
        char hex[1 + 2 * sizeof(mcc_anc)];
        mcc_bytes_to_hex(hex, mcc_anc, mcc_anc_len, mcc->use_u_alias);
        avio_printf(avf->pb, "%s%s\t%s\n", timecode_str, field_and_line, hex);
    }
    if (ret != AVERROR_EOF)
        return ret;
    return 0;
}

static int mcc_init(AVFormatContext *avf)
{
    MCCContext *mcc = avf->priv_data;
    int         ret;

    if (avf->nb_streams != 1) {
        av_log(avf, AV_LOG_ERROR, "mcc muxer supports at most one stream\n");
        return AVERROR(EINVAL);
    }

    AVStream  *st             = avf->streams[0];
    AVRational time_code_rate = st->avg_frame_rate;
    int        timecode_flags = 0;
    AVTimecode twenty_four_hr;

    if (mcc->override_time_code_rate && (ret = av_parse_video_rate(&time_code_rate, mcc->override_time_code_rate)) < 0)
        return ret;

    ret = AVERROR(EINVAL);

    for (size_t i = 0; i < FF_ARRAY_ELEMS(valid_time_code_rates); i++) {
        if (time_code_rate.num == valid_time_code_rates[i].num && time_code_rate.den == valid_time_code_rates[i].den) {
            ret = 0;
            break;
        }
    }

    if (ret != 0) {
        if (!mcc->override_time_code_rate && (time_code_rate.num <= 0 || time_code_rate.den <= 0)) {
            av_log(avf, AV_LOG_FATAL, "time code rate not set, you need to use -override_time_code_rate to set it\n");
        } else {
            av_log(avf,
                   AV_LOG_FATAL,
                   "time code rate not supported by mcc: %d/%d\n",
                   time_code_rate.num,
                   time_code_rate.den);
        }
        return AVERROR(EINVAL);
    }

    avpriv_set_pts_info(st, 64, time_code_rate.den, time_code_rate.num);

    if (time_code_rate.den == 1001 && time_code_rate.num % 30000 == 0) {
        timecode_flags |= AV_TIMECODE_FLAG_DROPFRAME;
    }

    ret = av_timecode_init(&mcc->timecode, time_code_rate, timecode_flags, 0, avf);
    if (ret < 0)
        return ret;

    // get av_timecode to calculate how many frames are in 24hr
    ret = av_timecode_init_from_components(&twenty_four_hr, time_code_rate, timecode_flags, 24, 0, 0, 0, avf);
    if (ret < 0)
        return ret;

    mcc->twenty_four_hr = twenty_four_hr.start;

    if (st->codecpar->codec_id == AV_CODEC_ID_EIA_608) {
        char args[64];
        snprintf(args, sizeof(args), "cdp_frame_rate=%d/%d", time_code_rate.num, time_code_rate.den);
        ret = ff_stream_add_bitstream_filter(st, "eia608_to_smpte436m", args);
        if (ret < 0)
            return ret;
    } else if (st->codecpar->codec_id != AV_CODEC_ID_SMPTE_436M_ANC) {
        av_log(avf,
               AV_LOG_ERROR,
               "mcc muxer supports only codec %s or codec %s\n",
               avcodec_get_name(AV_CODEC_ID_SMPTE_436M_ANC),
               avcodec_get_name(AV_CODEC_ID_EIA_608));
        return AVERROR(EINVAL);
    }

    return 0;
}

static int mcc_query_codec(enum AVCodecID codec_id, int std_compliance)
{
    (void)std_compliance;
    if (codec_id == AV_CODEC_ID_EIA_608 || codec_id == AV_CODEC_ID_SMPTE_436M_ANC)
        return 1;
    return 0;
}

#define OFFSET(x) offsetof(MCCContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
// clang-format off
static const AVOption options[] = {
    { "override_time_code_rate", "override the `Time Code Rate` value in the output", OFFSET(override_time_code_rate), AV_OPT_TYPE_STRING, { .str = NULL }, 0, INT_MAX, ENC },
    { "use_u_alias", "use the U alias for E1h 00h 00h 00h, disabled by default because some .mcc files disagree on whether it has 2 or 3 zero bytes", OFFSET(use_u_alias), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, ENC },
    { "mcc_version", "the mcc file format version", OFFSET(mcc_version), AV_OPT_TYPE_UINT, { .i64 = MCC_VERSION_2 }, MCC_VERSION_MIN, MCC_VERSION_MAX, ENC },
    { "creation_program", "the creation program", OFFSET(creation_program), AV_OPT_TYPE_STRING, { .str = NULL }, 0, INT_MAX, ENC },
    { "creation_time", "the creation time", OFFSET(creation_time), AV_OPT_TYPE_STRING, { .str = "now" }, 0, INT_MAX, ENC },
    { NULL },
};
// clang-format on

static const AVClass mcc_muxer_class = {
    .class_name = "mcc muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFOutputFormat ff_mcc_muxer = {
    .p.name           = "mcc",
    .p.long_name      = NULL_IF_CONFIG_SMALL("MacCaption"),
    .p.extensions     = "mcc",
    .p.flags          = AVFMT_GLOBALHEADER,
    .p.video_codec    = AV_CODEC_ID_NONE,
    .p.audio_codec    = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_EIA_608,
    .p.priv_class     = &mcc_muxer_class,
    .priv_data_size   = sizeof(MCCContext),
    .init             = mcc_init,
    .query_codec      = mcc_query_codec,
    .write_header     = mcc_write_header,
    .write_packet     = mcc_write_packet,
};
