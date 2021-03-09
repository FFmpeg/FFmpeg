/*
 * ISO Media common code
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002 Francois Revol <revol@free.fr>
 * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@free.fr>
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
#include "isom.h"
#include "libavcodec/mpeg4audio.h"
#include "libavcodec/mpegaudiodata.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"

/* http://www.mp4ra.org */
/* ordered by muxing preference */
const AVCodecTag ff_mp4_obj_type[] = {
    { AV_CODEC_ID_MOV_TEXT    , 0x08 },
    { AV_CODEC_ID_MPEG4       , 0x20 },
    { AV_CODEC_ID_H264        , 0x21 },
    { AV_CODEC_ID_HEVC        , 0x23 },
    { AV_CODEC_ID_AAC         , 0x40 },
    { AV_CODEC_ID_MP4ALS      , 0x40 }, /* 14496-3 ALS */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x61 }, /* MPEG-2 Main */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x60 }, /* MPEG-2 Simple */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x62 }, /* MPEG-2 SNR */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x63 }, /* MPEG-2 Spatial */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x64 }, /* MPEG-2 High */
    { AV_CODEC_ID_MPEG2VIDEO  , 0x65 }, /* MPEG-2 422 */
    { AV_CODEC_ID_AAC         , 0x66 }, /* MPEG-2 AAC Main */
    { AV_CODEC_ID_AAC         , 0x67 }, /* MPEG-2 AAC Low */
    { AV_CODEC_ID_AAC         , 0x68 }, /* MPEG-2 AAC SSR */
    { AV_CODEC_ID_MP3         , 0x69 }, /* 13818-3 */
    { AV_CODEC_ID_MP2         , 0x69 }, /* 11172-3 */
    { AV_CODEC_ID_MPEG1VIDEO  , 0x6A }, /* 11172-2 */
    { AV_CODEC_ID_MP3         , 0x6B }, /* 11172-3 */
    { AV_CODEC_ID_MJPEG       , 0x6C }, /* 10918-1 */
    { AV_CODEC_ID_PNG         , 0x6D },
    { AV_CODEC_ID_JPEG2000    , 0x6E }, /* 15444-1 */
    { AV_CODEC_ID_VC1         , 0xA3 },
    { AV_CODEC_ID_DIRAC       , 0xA4 },
    { AV_CODEC_ID_AC3         , 0xA5 },
    { AV_CODEC_ID_EAC3        , 0xA6 },
    { AV_CODEC_ID_DTS         , 0xA9 }, /* mp4ra.org */
    { AV_CODEC_ID_OPUS        , 0xAD }, /* mp4ra.org */
    { AV_CODEC_ID_VP9         , 0xB1 }, /* mp4ra.org */
    { AV_CODEC_ID_FLAC        , 0xC1 }, /* nonstandard, update when there is a standard value */
    { AV_CODEC_ID_TSCC2       , 0xD0 }, /* nonstandard, camtasia uses it */
    { AV_CODEC_ID_EVRC        , 0xD1 }, /* nonstandard, pvAuthor uses it */
    { AV_CODEC_ID_VORBIS      , 0xDD }, /* nonstandard, gpac uses it */
    { AV_CODEC_ID_DVD_SUBTITLE, 0xE0 }, /* nonstandard, see unsupported-embedded-subs-2.mp4 */
    { AV_CODEC_ID_QCELP       , 0xE1 },
    { AV_CODEC_ID_MPEG4SYSTEMS, 0x01 },
    { AV_CODEC_ID_MPEG4SYSTEMS, 0x02 },
    { AV_CODEC_ID_NONE        ,    0 },
};

const AVCodecTag ff_codec_movsubtitle_tags[] = {
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t', 'e', 'x', 't') },
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t', 'x', '3', 'g') },
    { AV_CODEC_ID_EIA_608,  MKTAG('c', '6', '0', '8') },
    { AV_CODEC_ID_NONE, 0 },
};

const AVCodecTag ff_codec_movdata_tags[] = {
    { AV_CODEC_ID_BIN_DATA, MKTAG('g', 'p', 'm', 'd') },
    { AV_CODEC_ID_NONE, 0 },
};

/* map numeric codes from mdhd atom to ISO 639 */
/* cf. QTFileFormat.pdf p253, qtff.pdf p205 */
/* http://developer.apple.com/documentation/mac/Text/Text-368.html */
/* deprecated by putting the code as 3*5 bits ASCII */
static const char mov_mdhd_language_map[][4] = {
    "eng",    /*   0 English */
    "fra",    /*   1 French */
    "ger",    /*   2 German */
    "ita",    /*   3 Italian */
    "dut",    /*   4 Dutch */
    "sve",    /*   5 Swedish */
    "spa",    /*   6 Spanish */
    "dan",    /*   7 Danish */
    "por",    /*   8 Portuguese */
    "nor",    /*   9 Norwegian */
    "heb",    /*  10 Hebrew */
    "jpn",    /*  11 Japanese */
    "ara",    /*  12 Arabic */
    "fin",    /*  13 Finnish */
    "gre",    /*  14 Greek */
    "ice",    /*  15 Icelandic */
    "mlt",    /*  16 Maltese */
    "tur",    /*  17 Turkish */
    "hr ",    /*  18 Croatian */
    "chi",    /*  19 Traditional Chinese */
    "urd",    /*  20 Urdu */
    "hin",    /*  21 Hindi */
    "tha",    /*  22 Thai */
    "kor",    /*  23 Korean */
    "lit",    /*  24 Lithuanian */
    "pol",    /*  25 Polish */
    "hun",    /*  26 Hungarian */
    "est",    /*  27 Estonian */
    "lav",    /*  28 Latvian */
       "",    /*  29 Sami */
    "fo ",    /*  30 Faroese */
       "",    /*  31 Farsi */
    "rus",    /*  32 Russian */
    "chi",    /*  33 Simplified Chinese */
       "",    /*  34 Flemish */
    "iri",    /*  35 Irish */
    "alb",    /*  36 Albanian */
    "ron",    /*  37 Romanian */
    "ces",    /*  38 Czech */
    "slk",    /*  39 Slovak */
    "slv",    /*  40 Slovenian */
    "yid",    /*  41 Yiddish */
    "sr ",    /*  42 Serbian */
    "mac",    /*  43 Macedonian */
    "bul",    /*  44 Bulgarian */
    "ukr",    /*  45 Ukrainian */
    "bel",    /*  46 Belarusian */
    "uzb",    /*  47 Uzbek */
    "kaz",    /*  48 Kazakh */
    "aze",    /*  49 Azerbaijani */
    "aze",    /*  50 AzerbaijanAr */
    "arm",    /*  51 Armenian */
    "geo",    /*  52 Georgian */
    "mol",    /*  53 Moldavian */
    "kir",    /*  54 Kirghiz */
    "tgk",    /*  55 Tajiki */
    "tuk",    /*  56 Turkmen */
    "mon",    /*  57 Mongolian */
       "",    /*  58 MongolianCyr */
    "pus",    /*  59 Pashto */
    "kur",    /*  60 Kurdish */
    "kas",    /*  61 Kashmiri */
    "snd",    /*  62 Sindhi */
    "tib",    /*  63 Tibetan */
    "nep",    /*  64 Nepali */
    "san",    /*  65 Sanskrit */
    "mar",    /*  66 Marathi */
    "ben",    /*  67 Bengali */
    "asm",    /*  68 Assamese */
    "guj",    /*  69 Gujarati */
    "pa ",    /*  70 Punjabi */
    "ori",    /*  71 Oriya */
    "mal",    /*  72 Malayalam */
    "kan",    /*  73 Kannada */
    "tam",    /*  74 Tamil */
    "tel",    /*  75 Telugu */
       "",    /*  76 Sinhala */
    "bur",    /*  77 Burmese */
    "khm",    /*  78 Khmer */
    "lao",    /*  79 Lao */
    "vie",    /*  80 Vietnamese */
    "ind",    /*  81 Indonesian */
    "tgl",    /*  82 Tagalog */
    "may",    /*  83 MalayRoman */
    "may",    /*  84 MalayArabic */
    "amh",    /*  85 Amharic */
    "tir",    /*  86 Galla */
    "orm",    /*  87 Oromo */
    "som",    /*  88 Somali */
    "swa",    /*  89 Swahili */
       "",    /*  90 Kinyarwanda */
    "run",    /*  91 Rundi */
       "",    /*  92 Nyanja */
    "mlg",    /*  93 Malagasy */
    "epo",    /*  94 Esperanto */
       "",    /*  95  */
       "",    /*  96  */
       "",    /*  97  */
       "",    /*  98  */
       "",    /*  99  */
       "",    /* 100  */
       "",    /* 101  */
       "",    /* 102  */
       "",    /* 103  */
       "",    /* 104  */
       "",    /* 105  */
       "",    /* 106  */
       "",    /* 107  */
       "",    /* 108  */
       "",    /* 109  */
       "",    /* 110  */
       "",    /* 111  */
       "",    /* 112  */
       "",    /* 113  */
       "",    /* 114  */
       "",    /* 115  */
       "",    /* 116  */
       "",    /* 117  */
       "",    /* 118  */
       "",    /* 119  */
       "",    /* 120  */
       "",    /* 121  */
       "",    /* 122  */
       "",    /* 123  */
       "",    /* 124  */
       "",    /* 125  */
       "",    /* 126  */
       "",    /* 127  */
    "wel",    /* 128 Welsh */
    "baq",    /* 129 Basque */
    "cat",    /* 130 Catalan */
    "lat",    /* 131 Latin */
    "que",    /* 132 Quechua */
    "grn",    /* 133 Guarani */
    "aym",    /* 134 Aymara */
    "tat",    /* 135 Tatar */
    "uig",    /* 136 Uighur */
    "dzo",    /* 137 Dzongkha */
    "jav",    /* 138 JavaneseRom */
};

int ff_mov_iso639_to_lang(const char lang[4], int mp4)
{
    int i, code = 0;

    /* old way, only for QT? */
    for (i = 0; lang[0] && !mp4 && i < FF_ARRAY_ELEMS(mov_mdhd_language_map); i++) {
        if (!strcmp(lang, mov_mdhd_language_map[i]))
            return i;
    }
    /* XXX:can we do that in mov too? */
    if (!mp4)
        return -1;
    /* handle undefined as such */
    if (lang[0] == '\0')
        lang = "und";
    /* 5 bits ASCII */
    for (i = 0; i < 3; i++) {
        uint8_t c = lang[i];
        c -= 0x60;
        if (c > 0x1f)
            return -1;
        code <<= 5;
        code |= c;
    }
    return code;
}

int ff_mov_lang_to_iso639(unsigned code, char to[4])
{
    int i;
    memset(to, 0, 4);
    /* is it the mangled iso code? */
    /* see http://www.geocities.com/xhelmboyx/quicktime/formats/mp4-layout.txt */
    if (code >= 0x400 && code != 0x7fff) {
        for (i = 2; i >= 0; i--) {
            to[i] = 0x60 + (code & 0x1f);
            code >>= 5;
        }
        return 1;
    }
    /* old fashion apple lang code */
    if (code >= FF_ARRAY_ELEMS(mov_mdhd_language_map))
        return 0;
    if (!mov_mdhd_language_map[code][0])
        return 0;
    memcpy(to, mov_mdhd_language_map[code], 4);
    return 1;
}

int ff_mp4_read_descr_len(AVIOContext *pb)
{
    int len = 0;
    int count = 4;
    while (count--) {
        int c = avio_r8(pb);
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return len;
}

int ff_mp4_read_descr(AVFormatContext *fc, AVIOContext *pb, int *tag)
{
    int len;
    *tag = avio_r8(pb);
    len = ff_mp4_read_descr_len(pb);
    av_log(fc, AV_LOG_TRACE, "MPEG-4 description: tag=0x%02x len=%d\n", *tag, len);
    return len;
}

void ff_mp4_parse_es_descr(AVIOContext *pb, int *es_id)
{
     int flags;
     if (es_id) *es_id = avio_rb16(pb);
     else                avio_rb16(pb);
     flags = avio_r8(pb);
     if (flags & 0x80) //streamDependenceFlag
         avio_rb16(pb);
     if (flags & 0x40) { //URL_Flag
         int len = avio_r8(pb);
         avio_skip(pb, len);
     }
     if (flags & 0x20) //OCRstreamFlag
         avio_rb16(pb);
}

static const AVCodecTag mp4_audio_types[] = {
    { AV_CODEC_ID_MP3ON4, AOT_PS   }, /* old mp3on4 draft */
    { AV_CODEC_ID_MP3ON4, AOT_L1   }, /* layer 1 */
    { AV_CODEC_ID_MP3ON4, AOT_L2   }, /* layer 2 */
    { AV_CODEC_ID_MP3ON4, AOT_L3   }, /* layer 3 */
    { AV_CODEC_ID_MP4ALS, AOT_ALS  }, /* MPEG-4 ALS */
    { AV_CODEC_ID_NONE,   AOT_NULL },
};

int ff_mp4_read_dec_config_descr(AVFormatContext *fc, AVStream *st, AVIOContext *pb)
{
    enum AVCodecID codec_id;
    int len, tag;
    int ret;
    int object_type_id = avio_r8(pb);
    avio_r8(pb); /* stream type */
    avio_rb24(pb); /* buffer size db */
    avio_rb32(pb); /* rc_max_rate */

    st->codecpar->bit_rate = avio_rb32(pb); /* avg bitrate */

    codec_id= ff_codec_get_id(ff_mp4_obj_type, object_type_id);
    if (codec_id)
        st->codecpar->codec_id = codec_id;
    av_log(fc, AV_LOG_TRACE, "esds object type id 0x%02x\n", object_type_id);
    len = ff_mp4_read_descr(fc, pb, &tag);
    if (tag == MP4DecSpecificDescrTag) {
        av_log(fc, AV_LOG_TRACE, "Specific MPEG-4 header len=%d\n", len);
        /* As per 14496-3:2009 9.D.2.2, No decSpecificInfo is defined
           for MPEG-1 Audio or MPEG-2 Audio; MPEG-2 AAC excluded. */
        if (object_type_id == 0x69 || object_type_id == 0x6b)
            return 0;
        if (!len || (uint64_t)len > (1<<30))
            return AVERROR_INVALIDDATA;
        if ((ret = ff_get_extradata(fc, st->codecpar, pb, len)) < 0)
            return ret;
        if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
            MPEG4AudioConfig cfg = {0};
            ret = avpriv_mpeg4audio_get_config2(&cfg, st->codecpar->extradata,
                                                st->codecpar->extradata_size, 1, fc);
            if (ret < 0)
                return ret;
            st->codecpar->channels = cfg.channels;
            if (cfg.object_type == 29 && cfg.sampling_index < 3) // old mp3on4
                st->codecpar->sample_rate = ff_mpa_freq_tab[cfg.sampling_index];
            else if (cfg.ext_sample_rate)
                st->codecpar->sample_rate = cfg.ext_sample_rate;
            else
                st->codecpar->sample_rate = cfg.sample_rate;
            av_log(fc, AV_LOG_TRACE, "mp4a config channels %d obj %d ext obj %d "
                    "sample rate %d ext sample rate %d\n", st->codecpar->channels,
                    cfg.object_type, cfg.ext_object_type,
                    cfg.sample_rate, cfg.ext_sample_rate);
            if (!(st->codecpar->codec_id = ff_codec_get_id(mp4_audio_types,
                                                        cfg.object_type)))
                st->codecpar->codec_id = AV_CODEC_ID_AAC;
        }
    }
    return 0;
}

typedef struct MovChannelLayout {
    int64_t  channel_layout;
    uint32_t layout_tag;
} MovChannelLayout;

static const MovChannelLayout mov_channel_layout[] = {
    { AV_CH_LAYOUT_MONO,                         (100<<16) | 1}, // kCAFChannelLayoutTag_Mono
    { AV_CH_LAYOUT_STEREO,                       (101<<16) | 2}, // kCAFChannelLayoutTag_Stereo
    { AV_CH_LAYOUT_STEREO,                       (102<<16) | 2}, // kCAFChannelLayoutTag_StereoHeadphones
    { AV_CH_LAYOUT_2_1,                          (131<<16) | 3}, // kCAFChannelLayoutTag_ITU_2_1
    { AV_CH_LAYOUT_QUAD,                         (132<<16) | 4}, // kCAFChannelLayoutTag_ITU_2_2
    { AV_CH_LAYOUT_2_2,                          (132<<16) | 4}, // kCAFChannelLayoutTag_ITU_2_2
    { AV_CH_LAYOUT_QUAD,                         (108<<16) | 4}, // kCAFChannelLayoutTag_Quadraphonic
    { AV_CH_LAYOUT_SURROUND,                     (113<<16) | 3}, // kCAFChannelLayoutTag_MPEG_3_0_A
    { AV_CH_LAYOUT_4POINT0,                      (115<<16) | 4}, // kCAFChannelLayoutTag_MPEG_4_0_A
    { AV_CH_LAYOUT_5POINT0_BACK,                 (117<<16) | 5}, // kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT0,                      (117<<16) | 5}, // kCAFChannelLayoutTag_MPEG_5_0_A
    { AV_CH_LAYOUT_5POINT1_BACK,                 (121<<16) | 6}, // kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_5POINT1,                      (121<<16) | 6}, // kCAFChannelLayoutTag_MPEG_5_1_A
    { AV_CH_LAYOUT_7POINT1,                      (128<<16) | 8}, // kCAFChannelLayoutTag_MPEG_7_1_C
    { AV_CH_LAYOUT_7POINT1_WIDE,                 (126<<16) | 8}, // kCAFChannelLayoutTag_MPEG_7_1_A
    { AV_CH_LAYOUT_5POINT1_BACK|AV_CH_LAYOUT_STEREO_DOWNMIX, (130<<16) | 8}, // kCAFChannelLayoutTag_SMPTE_DTV
    { AV_CH_LAYOUT_STEREO|AV_CH_LOW_FREQUENCY,   (133<<16) | 3}, // kCAFChannelLayoutTag_DVD_4
    { AV_CH_LAYOUT_2_1|AV_CH_LOW_FREQUENCY,      (134<<16) | 4}, // kCAFChannelLayoutTag_DVD_5
    { AV_CH_LAYOUT_QUAD|AV_CH_LOW_FREQUENCY,     (135<<16) | 4}, // kCAFChannelLayoutTag_DVD_6
    { AV_CH_LAYOUT_2_2|AV_CH_LOW_FREQUENCY,      (135<<16) | 4}, // kCAFChannelLayoutTag_DVD_6
    { AV_CH_LAYOUT_SURROUND|AV_CH_LOW_FREQUENCY, (136<<16) | 4}, // kCAFChannelLayoutTag_DVD_10
    { AV_CH_LAYOUT_4POINT0|AV_CH_LOW_FREQUENCY,  (137<<16) | 5}, // kCAFChannelLayoutTag_DVD_11
    { 0, 0},
};

void ff_mov_write_chan(AVIOContext *pb, int64_t channel_layout)
{
    const MovChannelLayout *layouts;
    uint32_t layout_tag = 0;

    for (layouts = mov_channel_layout; layouts->channel_layout; layouts++)
        if (channel_layout == layouts->channel_layout) {
            layout_tag = layouts->layout_tag;
            break;
        }

    if (layout_tag) {
        avio_wb32(pb, layout_tag); // mChannelLayoutTag
        avio_wb32(pb, 0);          // mChannelBitmap
    } else {
        avio_wb32(pb, 0x10000);    // kCAFChannelLayoutTag_UseChannelBitmap
        avio_wb32(pb, channel_layout);
    }
    avio_wb32(pb, 0);              // mNumberChannelDescriptions
}

static const struct MP4TrackKindValueMapping dash_role_map[] = {
    { AV_DISPOSITION_HEARING_IMPAIRED|AV_DISPOSITION_CAPTIONS,
        "caption" },
    { AV_DISPOSITION_COMMENT,
        "commentary" },
    { AV_DISPOSITION_VISUAL_IMPAIRED|AV_DISPOSITION_DESCRIPTIONS,
        "description" },
    { AV_DISPOSITION_DUB,
        "dub" },
    { AV_DISPOSITION_FORCED,
        "forced-subtitle" },
    { 0, NULL }
};

const struct MP4TrackKindMapping ff_mov_track_kind_table[] = {
    { "urn:mpeg:dash:role:2011", dash_role_map },
    { 0, NULL }
};
