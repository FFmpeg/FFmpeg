/*
 * MOV demuxer
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 *
 * first version by Francois Revol <revol@free.fr>
 * seek function by Gael Chardon <gael.dev@4now.net>
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

#include "config_components.h"

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/mathematics.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/aes.h"
#include "libavutil/aes_ctr.h"
#include "libavutil/pixdesc.h"
#include "libavutil/sha.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timecode.h"
#include "libavutil/uuid.h"
#include "libavcodec/ac3tab.h"
#include "libavcodec/flac.h"
#include "libavcodec/hevc/hevc.h"
#include "libavcodec/mpegaudiodecheader.h"
#include "libavcodec/mlp_parse.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "demux.h"
#include "dvdclut.h"
#include "iamf_parse.h"
#include "iamf_reader.h"
#include "dovi_isom.h"
#include "riff.h"
#include "isom.h"
#include "libavcodec/get_bits.h"
#include "id3v1.h"
#include "mov_chan.h"
#include "replaygain.h"

#if CONFIG_ZLIB
#include <zlib.h>
#endif

#include "qtpalette.h"

/* those functions parse an atom */
/* links atom IDs to parse functions */
typedef struct MOVParseTableEntry {
    uint32_t type;
    int (*parse)(MOVContext *ctx, AVIOContext *pb, MOVAtom atom);
} MOVParseTableEntry;

static int mov_read_default(MOVContext *c, AVIOContext *pb, MOVAtom atom);
static int mov_read_mfra(MOVContext *c, AVIOContext *f);
static void mov_free_stream_context(AVFormatContext *s, AVStream *st);

static int mov_metadata_track_or_disc_number(MOVContext *c, AVIOContext *pb,
                                             unsigned len, const char *key)
{
    char buf[16];

    short current, total = 0;
    avio_rb16(pb); // unknown
    current = avio_rb16(pb);
    if (len >= 6)
        total = avio_rb16(pb);
    if (!total)
        snprintf(buf, sizeof(buf), "%d", current);
    else
        snprintf(buf, sizeof(buf), "%d/%d", current, total);
    c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
    av_dict_set(&c->fc->metadata, key, buf, 0);

    return 0;
}

static int mov_metadata_int8_bypass_padding(MOVContext *c, AVIOContext *pb,
                                            unsigned len, const char *key)
{
    /* bypass padding bytes */
    avio_r8(pb);
    avio_r8(pb);
    avio_r8(pb);

    c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
    av_dict_set_int(&c->fc->metadata, key, avio_r8(pb), 0);

    return 0;
}

static int mov_metadata_int8_no_padding(MOVContext *c, AVIOContext *pb,
                                        unsigned len, const char *key)
{
    c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
    av_dict_set_int(&c->fc->metadata, key, avio_r8(pb), 0);

    return 0;
}

static int mov_metadata_gnre(MOVContext *c, AVIOContext *pb,
                             unsigned len, const char *key)
{
    short genre;

    avio_r8(pb); // unknown

    genre = avio_r8(pb);
    if (genre < 1 || genre > ID3v1_GENRE_MAX)
        return 0;
    c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
    av_dict_set(&c->fc->metadata, key, ff_id3v1_genre_str[genre-1], 0);

    return 0;
}

static const uint32_t mac_to_unicode[128] = {
    0x00C4,0x00C5,0x00C7,0x00C9,0x00D1,0x00D6,0x00DC,0x00E1,
    0x00E0,0x00E2,0x00E4,0x00E3,0x00E5,0x00E7,0x00E9,0x00E8,
    0x00EA,0x00EB,0x00ED,0x00EC,0x00EE,0x00EF,0x00F1,0x00F3,
    0x00F2,0x00F4,0x00F6,0x00F5,0x00FA,0x00F9,0x00FB,0x00FC,
    0x2020,0x00B0,0x00A2,0x00A3,0x00A7,0x2022,0x00B6,0x00DF,
    0x00AE,0x00A9,0x2122,0x00B4,0x00A8,0x2260,0x00C6,0x00D8,
    0x221E,0x00B1,0x2264,0x2265,0x00A5,0x00B5,0x2202,0x2211,
    0x220F,0x03C0,0x222B,0x00AA,0x00BA,0x03A9,0x00E6,0x00F8,
    0x00BF,0x00A1,0x00AC,0x221A,0x0192,0x2248,0x2206,0x00AB,
    0x00BB,0x2026,0x00A0,0x00C0,0x00C3,0x00D5,0x0152,0x0153,
    0x2013,0x2014,0x201C,0x201D,0x2018,0x2019,0x00F7,0x25CA,
    0x00FF,0x0178,0x2044,0x20AC,0x2039,0x203A,0xFB01,0xFB02,
    0x2021,0x00B7,0x201A,0x201E,0x2030,0x00C2,0x00CA,0x00C1,
    0x00CB,0x00C8,0x00CD,0x00CE,0x00CF,0x00CC,0x00D3,0x00D4,
    0xF8FF,0x00D2,0x00DA,0x00DB,0x00D9,0x0131,0x02C6,0x02DC,
    0x00AF,0x02D8,0x02D9,0x02DA,0x00B8,0x02DD,0x02DB,0x02C7,
};

static int mov_read_mac_string(MOVContext *c, AVIOContext *pb, int len,
                               char *dst, int dstlen)
{
    char *p = dst;
    char *end = dst+dstlen-1;
    int i;

    for (i = 0; i < len; i++) {
        uint8_t t, c = avio_r8(pb);

        if (p >= end)
            continue;

        if (c < 0x80)
            *p++ = c;
        else if (p < end)
            PUT_UTF8(mac_to_unicode[c-0x80], t, if (p < end) *p++ = t;);
    }
    *p = 0;
    return p - dst;
}

/**
 * Get the current item in the parsing process.
 */
static HEIFItem *heif_cur_item(MOVContext *c)
{
    HEIFItem *item = NULL;

    for (int i = 0; i < c->nb_heif_item; i++) {
        if (!c->heif_item[i] || c->heif_item[i]->item_id != c->cur_item_id)
            continue;

        item = c->heif_item[i];
        break;
    }

    return item;
}

/**
 * Get the current stream in the parsing process. This can either be the
 * latest stream added to the context, or the stream referenced by an item.
 */
static AVStream *get_curr_st(MOVContext *c)
{
    AVStream *st = NULL;
    HEIFItem *item;

    if (c->fc->nb_streams < 1)
        return NULL;

    if (c->cur_item_id == -1)
        return c->fc->streams[c->fc->nb_streams-1];

    item = heif_cur_item(c);
    if (item)
        st = item->st;

    return st;
}

static int mov_read_covr(MOVContext *c, AVIOContext *pb, int type, int len)
{
    AVStream *st;
    MOVStreamContext *sc;
    enum AVCodecID id;
    int ret;

    switch (type) {
    case 0xd:  id = AV_CODEC_ID_MJPEG; break;
    case 0xe:  id = AV_CODEC_ID_PNG;   break;
    case 0x1b: id = AV_CODEC_ID_BMP;   break;
    default:
        av_log(c->fc, AV_LOG_WARNING, "Unknown cover type: 0x%x.\n", type);
        avio_skip(pb, len);
        return 0;
    }

    sc = av_mallocz(sizeof(*sc));
    if (!sc)
        return AVERROR(ENOMEM);
    ret = ff_add_attached_pic(c->fc, NULL, pb, NULL, len);
    if (ret < 0) {
        av_free(sc);
        return ret;
    }
    st = c->fc->streams[c->fc->nb_streams - 1];
    st->priv_data = sc;
    sc->id = st->id;
    sc->refcount = 1;

    if (st->attached_pic.size >= 8 && id != AV_CODEC_ID_BMP) {
        if (AV_RB64(st->attached_pic.data) == 0x89504e470d0a1a0a) {
            id = AV_CODEC_ID_PNG;
        } else {
            id = AV_CODEC_ID_MJPEG;
        }
    }
    st->codecpar->codec_id   = id;

    return 0;
}

// 3GPP TS 26.244
static int mov_metadata_loci(MOVContext *c, AVIOContext *pb, unsigned len)
{
    char language[4] = { 0 };
    char buf[200], place[100];
    uint16_t langcode = 0;
    double longitude, latitude, altitude;
    const char *key = "location";

    if (len < 4 + 2 + 1 + 1 + 4 + 4 + 4) {
        av_log(c->fc, AV_LOG_ERROR, "loci too short\n");
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 4); // version+flags
    langcode = avio_rb16(pb);
    ff_mov_lang_to_iso639(langcode, language);
    len -= 6;

    len -= avio_get_str(pb, len, place, sizeof(place));
    if (len < 1) {
        av_log(c->fc, AV_LOG_ERROR, "place name too long\n");
        return AVERROR_INVALIDDATA;
    }
    avio_skip(pb, 1); // role
    len -= 1;

    if (len < 12) {
        av_log(c->fc, AV_LOG_ERROR,
               "loci too short (%u bytes left, need at least %d)\n", len, 12);
        return AVERROR_INVALIDDATA;
    }
    longitude = ((int32_t) avio_rb32(pb)) / (float) (1 << 16);
    latitude  = ((int32_t) avio_rb32(pb)) / (float) (1 << 16);
    altitude  = ((int32_t) avio_rb32(pb)) / (float) (1 << 16);

    // Try to output in the same format as the ?xyz field
    snprintf(buf, sizeof(buf), "%+08.4f%+09.4f",  latitude, longitude);
    if (altitude)
        av_strlcatf(buf, sizeof(buf), "%+f", altitude);
    av_strlcatf(buf, sizeof(buf), "/%s", place);

    if (*language && strcmp(language, "und")) {
        char key2[16];
        snprintf(key2, sizeof(key2), "%s-%s", key, language);
        av_dict_set(&c->fc->metadata, key2, buf, 0);
    }
    c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
    return av_dict_set(&c->fc->metadata, key, buf, 0);
}

static int mov_metadata_hmmt(MOVContext *c, AVIOContext *pb, unsigned len)
{
    int i, n_hmmt;

    if (len < 2)
        return 0;
    if (c->ignore_chapters)
        return 0;

    n_hmmt = avio_rb32(pb);
    if (n_hmmt > len / 4)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < n_hmmt && !pb->eof_reached; i++) {
        int moment_time = avio_rb32(pb);
        avpriv_new_chapter(c->fc, i, av_make_q(1, 1000), moment_time, AV_NOPTS_VALUE, NULL);
    }
    if (avio_feof(pb))
        return AVERROR_INVALIDDATA;
    return 0;
}

static int mov_read_udta_string(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    char tmp_key[AV_FOURCC_MAX_STRING_SIZE] = {0};
    char key2[32], language[4] = {0};
    char *str = NULL;
    const char *key = NULL;
    uint16_t langcode = 0;
    uint32_t data_type = 0, str_size_alloc;
    uint64_t str_size;
    int (*parse)(MOVContext*, AVIOContext*, unsigned, const char*) = NULL;
    int raw = 0;
    int num = 0;
    AVDictionary **metadata;

    if (c->trak_index >= 0 && c->trak_index < c->fc->nb_streams)
        metadata = &c->fc->streams[c->trak_index]->metadata;
    else
        metadata = &c->fc->metadata;

    switch (atom.type) {
    case MKTAG( '@','P','R','M'): key = "premiere_version"; raw = 1; break;
    case MKTAG( '@','P','R','Q'): key = "quicktime_version"; raw = 1; break;
    case MKTAG( 'X','M','P','_'):
        if (c->export_xmp) { key = "xmp"; raw = 1; } break;
    case MKTAG( 'a','A','R','T'): key = "album_artist";    break;
    case MKTAG( 'a','k','I','D'): key = "account_type";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'a','p','I','D'): key = "account_id"; break;
    case MKTAG( 'c','a','t','g'): key = "category"; break;
    case MKTAG( 'c','p','i','l'): key = "compilation";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'c','p','r','t'): key = "copyright"; break;
    case MKTAG( 'd','e','s','c'): key = "description"; break;
    case MKTAG( 'd','i','s','k'): key = "disc";
        parse = mov_metadata_track_or_disc_number; break;
    case MKTAG( 'e','g','i','d'): key = "episode_uid";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'F','I','R','M'): key = "firmware"; raw = 1; break;
    case MKTAG( 'g','n','r','e'): key = "genre";
        parse = mov_metadata_gnre; break;
    case MKTAG( 'h','d','v','d'): key = "hd_video";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'H','M','M','T'):
        return mov_metadata_hmmt(c, pb, atom.size);
    case MKTAG( 'k','e','y','w'): key = "keywords";  break;
    case MKTAG( 'l','d','e','s'): key = "synopsis";  break;
    case MKTAG( 'l','o','c','i'):
        return mov_metadata_loci(c, pb, atom.size);
    case MKTAG( 'm','a','n','u'): key = "make"; break;
    case MKTAG( 'm','o','d','l'): key = "model"; break;
    case MKTAG( 'p','c','s','t'): key = "podcast";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'p','g','a','p'): key = "gapless_playback";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'p','u','r','d'): key = "purchase_date"; break;
    case MKTAG( 'r','t','n','g'): key = "rating";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 's','o','a','a'): key = "sort_album_artist"; break;
    case MKTAG( 's','o','a','l'): key = "sort_album";   break;
    case MKTAG( 's','o','a','r'): key = "sort_artist";  break;
    case MKTAG( 's','o','c','o'): key = "sort_composer"; break;
    case MKTAG( 's','o','n','m'): key = "sort_name";    break;
    case MKTAG( 's','o','s','n'): key = "sort_show";    break;
    case MKTAG( 's','t','i','k'): key = "media_type";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 't','r','k','n'): key = "track";
        parse = mov_metadata_track_or_disc_number; break;
    case MKTAG( 't','v','e','n'): key = "episode_id"; break;
    case MKTAG( 't','v','e','s'): key = "episode_sort";
        parse = mov_metadata_int8_bypass_padding; break;
    case MKTAG( 't','v','n','n'): key = "network";   break;
    case MKTAG( 't','v','s','h'): key = "show";      break;
    case MKTAG( 't','v','s','n'): key = "season_number";
        parse = mov_metadata_int8_bypass_padding; break;
    case MKTAG(0xa9,'A','R','T'): key = "artist";    break;
    case MKTAG(0xa9,'P','R','D'): key = "producer";  break;
    case MKTAG(0xa9,'a','l','b'): key = "album";     break;
    case MKTAG(0xa9,'a','u','t'): key = "artist";    break;
    case MKTAG(0xa9,'c','h','p'): key = "chapter";   break;
    case MKTAG(0xa9,'c','m','t'): key = "comment";   break;
    case MKTAG(0xa9,'c','o','m'): key = "composer";  break;
    case MKTAG(0xa9,'c','p','y'): key = "copyright"; break;
    case MKTAG(0xa9,'d','a','y'): key = "date";      break;
    case MKTAG(0xa9,'d','i','r'): key = "director";  break;
    case MKTAG(0xa9,'d','i','s'): key = "disclaimer"; break;
    case MKTAG(0xa9,'e','d','1'): key = "edit_date"; break;
    case MKTAG(0xa9,'e','n','c'): key = "encoder";   break;
    case MKTAG(0xa9,'f','m','t'): key = "original_format"; break;
    case MKTAG(0xa9,'g','e','n'): key = "genre";     break;
    case MKTAG(0xa9,'g','r','p'): key = "grouping";  break;
    case MKTAG(0xa9,'h','s','t'): key = "host_computer"; break;
    case MKTAG(0xa9,'i','n','f'): key = "comment";   break;
    case MKTAG(0xa9,'l','y','r'): key = "lyrics";    break;
    case MKTAG(0xa9,'m','a','k'): key = "make";      break;
    case MKTAG(0xa9,'m','o','d'): key = "model";     break;
    case MKTAG(0xa9,'n','a','m'): key = "title";     break;
    case MKTAG(0xa9,'o','p','e'): key = "original_artist"; break;
    case MKTAG(0xa9,'p','r','d'): key = "producer";  break;
    case MKTAG(0xa9,'p','r','f'): key = "performers"; break;
    case MKTAG(0xa9,'r','e','q'): key = "playback_requirements"; break;
    case MKTAG(0xa9,'s','r','c'): key = "original_source"; break;
    case MKTAG(0xa9,'s','t','3'): key = "subtitle";  break;
    case MKTAG(0xa9,'s','w','r'): key = "encoder";   break;
    case MKTAG(0xa9,'t','o','o'): key = "encoder";   break;
    case MKTAG(0xa9,'t','r','k'): key = "track";     break;
    case MKTAG(0xa9,'u','r','l'): key = "URL";       break;
    case MKTAG(0xa9,'w','r','n'): key = "warning";   break;
    case MKTAG(0xa9,'w','r','t'): key = "composer";  break;
    case MKTAG(0xa9,'x','y','z'): key = "location";  break;
    }
retry:
    if (c->itunes_metadata && atom.size > 8) {
        int data_size = avio_rb32(pb);
        int tag = avio_rl32(pb);
        if (tag == MKTAG('d','a','t','a') && data_size <= atom.size && data_size >= 16) {
            data_type = avio_rb32(pb); // type
            avio_rb32(pb); // unknown
            str_size = data_size - 16;
            atom.size -= 16;

            if (!key && c->found_hdlr_mdta && c->meta_keys) {
                uint32_t index = av_bswap32(atom.type); // BE number has been read as LE
                if (index < c->meta_keys_count && index > 0) {
                    key = c->meta_keys[index];
                } else if (atom.type != MKTAG('c', 'o', 'v', 'r')) {
                    av_log(c->fc, AV_LOG_WARNING,
                           "The index of 'data' is out of range: %"PRId32" < 1 or >= %d.\n",
                           index, c->meta_keys_count);
                }
            }
            if (atom.type == MKTAG('c', 'o', 'v', 'r') ||
                (key && !strcmp(key, "com.apple.quicktime.artwork"))) {
                int ret = mov_read_covr(c, pb, data_type, str_size);
                if (ret < 0) {
                    av_log(c->fc, AV_LOG_ERROR, "Error parsing cover art.\n");
                    return ret;
                }
                atom.size -= str_size;
                if (atom.size > 8)
                    goto retry;
                return ret;
            }
        } else return 0;
    } else if (atom.size > 4 && key && !c->itunes_metadata && !raw) {
        str_size = avio_rb16(pb); // string length
        if (str_size > atom.size) {
            raw = 1;
            avio_seek(pb, -2, SEEK_CUR);
            av_log(c->fc, AV_LOG_WARNING, "UDTA parsing failed retrying raw\n");
            goto retry;
        }
        langcode = avio_rb16(pb);
        ff_mov_lang_to_iso639(langcode, language);
        atom.size -= 4;
    } else
        str_size = atom.size;

    if (c->export_all && !key) {
        key = av_fourcc_make_string(tmp_key, atom.type);
    }

    if (!key)
        return 0;
    if (atom.size < 0 || str_size >= INT_MAX/2)
        return AVERROR_INVALIDDATA;

    // Allocates enough space if data_type is a int32 or float32 number, otherwise
    // worst-case requirement for output string in case of utf8 coded input
    num = (data_type >= 21 && data_type <= 23);
    str_size_alloc = (num ? 512 : (raw ? str_size : str_size * 2)) + 1;
    str = av_mallocz(str_size_alloc);
    if (!str)
        return AVERROR(ENOMEM);

    if (parse)
        parse(c, pb, str_size, key);
    else {
        if (!raw && (data_type == 3 || (data_type == 0 && (langcode < 0x400 || langcode == 0x7fff)))) { // MAC Encoded
            mov_read_mac_string(c, pb, str_size, str, str_size_alloc);
        } else if (data_type == 21) { // BE signed integer, variable size
            int val = 0;
            if (str_size == 1)
                val = (int8_t)avio_r8(pb);
            else if (str_size == 2)
                val = (int16_t)avio_rb16(pb);
            else if (str_size == 3)
                val = ((int32_t)(avio_rb24(pb)<<8))>>8;
            else if (str_size == 4)
                val = (int32_t)avio_rb32(pb);
            if (snprintf(str, str_size_alloc, "%d", val) >= str_size_alloc) {
                av_log(c->fc, AV_LOG_ERROR,
                       "Failed to store the number (%d) in string.\n", val);
                av_free(str);
                return AVERROR_INVALIDDATA;
            }
        } else if (data_type == 22) { // BE unsigned integer, variable size
            unsigned int val = 0;
            if (str_size == 1)
                val = avio_r8(pb);
            else if (str_size == 2)
                val = avio_rb16(pb);
            else if (str_size == 3)
                val = avio_rb24(pb);
            else if (str_size == 4)
                val = avio_rb32(pb);
            if (snprintf(str, str_size_alloc, "%u", val) >= str_size_alloc) {
                av_log(c->fc, AV_LOG_ERROR,
                       "Failed to store the number (%u) in string.\n", val);
                av_free(str);
                return AVERROR_INVALIDDATA;
            }
        } else if (data_type == 23 && str_size >= 4) {  // BE float32
            float val = av_int2float(avio_rb32(pb));
            if (snprintf(str, str_size_alloc, "%f", val) >= str_size_alloc) {
                av_log(c->fc, AV_LOG_ERROR,
                       "Failed to store the float32 number (%f) in string.\n", val);
                av_free(str);
                return AVERROR_INVALIDDATA;
            }
        } else if (data_type > 1 && data_type != 4) {
            // data_type can be 0 if not set at all above. data_type 1 means
            // UTF8 and 4 means "UTF8 sort". For any other type (UTF16 or e.g.
            // a picture), don't return it blindly in a string that is supposed
            // to be UTF8 text.
            av_log(c->fc, AV_LOG_WARNING, "Skipping unhandled metadata %s of type %d\n", key, data_type);
            av_free(str);
            return 0;
        } else {
            int ret = ffio_read_size(pb, str, str_size);
            if (ret < 0) {
                av_free(str);
                return ret;
            }
            str[str_size] = 0;
        }
        c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
        av_dict_set(metadata, key, str, 0);
        if (*language && strcmp(language, "und")) {
            snprintf(key2, sizeof(key2), "%s-%s", key, language);
            av_dict_set(metadata, key2, str, 0);
        }
        if (!strcmp(key, "encoder")) {
            int major, minor, micro;
            if (sscanf(str, "HandBrake %d.%d.%d", &major, &minor, &micro) == 3) {
                c->handbrake_version = 1000000*major + 1000*minor + micro;
            }
        }
    }

    av_freep(&str);
    return 0;
}

static int mov_read_chpl(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t start;
    int i, nb_chapters, str_len, version;
    char str[256+1];
    int ret;

    if (c->ignore_chapters)
        return 0;

    if ((atom.size -= 5) < 0)
        return 0;

    version = avio_r8(pb);
    avio_rb24(pb);
    if (version)
        avio_rb32(pb); // ???
    nb_chapters = avio_r8(pb);

    for (i = 0; i < nb_chapters; i++) {
        if (atom.size < 9)
            return 0;

        start = avio_rb64(pb);
        str_len = avio_r8(pb);

        if ((atom.size -= 9+str_len) < 0)
            return 0;

        ret = ffio_read_size(pb, str, str_len);
        if (ret < 0)
            return ret;
        str[str_len] = 0;
        avpriv_new_chapter(c->fc, i, (AVRational){1,10000000}, start, AV_NOPTS_VALUE, str);
    }
    return 0;
}

#define MIN_DATA_ENTRY_BOX_SIZE 12
static int mov_read_dref(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int entries, i, j;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_rb32(pb); // version + flags
    entries = avio_rb32(pb);
    if (!entries ||
        entries >  (atom.size - 1) / MIN_DATA_ENTRY_BOX_SIZE + 1 ||
        entries >= UINT_MAX / sizeof(*sc->drefs))
        return AVERROR_INVALIDDATA;

    for (i = 0; i < sc->drefs_count; i++) {
        MOVDref *dref = &sc->drefs[i];
        av_freep(&dref->path);
        av_freep(&dref->dir);
    }
    av_free(sc->drefs);
    sc->drefs_count = 0;
    sc->drefs = av_mallocz(entries * sizeof(*sc->drefs));
    if (!sc->drefs)
        return AVERROR(ENOMEM);
    sc->drefs_count = entries;

    for (i = 0; i < entries; i++) {
        MOVDref *dref = &sc->drefs[i];
        uint32_t size = avio_rb32(pb);
        int64_t next = avio_tell(pb);

        if (size < 12 || next < 0 || next > INT64_MAX - size)
            return AVERROR_INVALIDDATA;

        next += size - 4;

        dref->type = avio_rl32(pb);
        avio_rb32(pb); // version + flags

        if (dref->type == MKTAG('a','l','i','s') && size > 150) {
            /* macintosh alias record */
            uint16_t volume_len, len;
            int16_t type;
            int ret;

            avio_skip(pb, 10);

            volume_len = avio_r8(pb);
            volume_len = FFMIN(volume_len, 27);
            ret = ffio_read_size(pb, dref->volume, 27);
            if (ret < 0)
                return ret;
            dref->volume[volume_len] = 0;
            av_log(c->fc, AV_LOG_DEBUG, "volume %s, len %d\n", dref->volume, volume_len);

            avio_skip(pb, 12);

            len = avio_r8(pb);
            len = FFMIN(len, 63);
            ret = ffio_read_size(pb, dref->filename, 63);
            if (ret < 0)
                return ret;
            dref->filename[len] = 0;
            av_log(c->fc, AV_LOG_DEBUG, "filename %s, len %d\n", dref->filename, len);

            avio_skip(pb, 16);

            /* read next level up_from_alias/down_to_target */
            dref->nlvl_from = avio_rb16(pb);
            dref->nlvl_to   = avio_rb16(pb);
            av_log(c->fc, AV_LOG_DEBUG, "nlvl from %d, nlvl to %d\n",
                   dref->nlvl_from, dref->nlvl_to);

            avio_skip(pb, 16);

            for (type = 0; type != -1 && avio_tell(pb) < next; ) {
                if (avio_feof(pb))
                    return AVERROR_EOF;
                type = avio_rb16(pb);
                len = avio_rb16(pb);
                av_log(c->fc, AV_LOG_DEBUG, "type %d, len %d\n", type, len);
                if (len&1)
                    len += 1;
                if (type == 2) { // absolute path
                    av_free(dref->path);
                    dref->path = av_mallocz(len+1);
                    if (!dref->path)
                        return AVERROR(ENOMEM);

                    ret = ffio_read_size(pb, dref->path, len);
                    if (ret < 0) {
                        av_freep(&dref->path);
                        return ret;
                    }
                    if (len > volume_len && !strncmp(dref->path, dref->volume, volume_len)) {
                        len -= volume_len;
                        memmove(dref->path, dref->path+volume_len, len);
                        dref->path[len] = 0;
                    }
                    // trim string of any ending zeros
                    for (j = len - 1; j >= 0; j--) {
                        if (dref->path[j] == 0)
                            len--;
                        else
                            break;
                    }
                    for (j = 0; j < len; j++)
                        if (dref->path[j] == ':' || dref->path[j] == 0)
                            dref->path[j] = '/';
                    av_log(c->fc, AV_LOG_DEBUG, "path %s\n", dref->path);
                } else if (type == 0) { // directory name
                    av_free(dref->dir);
                    dref->dir = av_malloc(len+1);
                    if (!dref->dir)
                        return AVERROR(ENOMEM);

                    ret = ffio_read_size(pb, dref->dir, len);
                    if (ret < 0) {
                        av_freep(&dref->dir);
                        return ret;
                    }
                    dref->dir[len] = 0;
                    for (j = 0; j < len; j++)
                        if (dref->dir[j] == ':')
                            dref->dir[j] = '/';
                    av_log(c->fc, AV_LOG_DEBUG, "dir %s\n", dref->dir);
                } else
                    avio_skip(pb, len);
            }
        } else {
            av_log(c->fc, AV_LOG_DEBUG, "Unknown dref type 0x%08"PRIx32" size %"PRIu32"\n",
                   dref->type, size);
            entries--;
            i--;
        }
        avio_seek(pb, next, SEEK_SET);
    }
    return 0;
}

static int mov_read_hdlr(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint32_t type;
    uint32_t ctype;
    int64_t title_size;
    char *title_str;
    int ret;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    /* component type */
    ctype = avio_rl32(pb);
    type = avio_rl32(pb); /* component subtype */

    av_log(c->fc, AV_LOG_TRACE, "ctype=%s\n", av_fourcc2str(ctype));
    av_log(c->fc, AV_LOG_TRACE, "stype=%s\n", av_fourcc2str(type));

    if (c->trak_index < 0) {  // meta not inside a trak
        if (type == MKTAG('m','d','t','a')) {
            c->found_hdlr_mdta = 1;
        }
        return 0;
    }

    st = c->fc->streams[c->fc->nb_streams-1];

    if     (type == MKTAG('v','i','d','e'))
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    else if (type == MKTAG('s','o','u','n'))
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    else if (type == MKTAG('m','1','a',' '))
        st->codecpar->codec_id = AV_CODEC_ID_MP2;
    else if ((type == MKTAG('s','u','b','p')) || (type == MKTAG('c','l','c','p')))
        st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;

    avio_rb32(pb); /* component  manufacture */
    avio_rb32(pb); /* component flags */
    avio_rb32(pb); /* component flags mask */

    title_size = atom.size - 24;
    if (title_size > 0) {
        if (title_size > FFMIN(INT_MAX, SIZE_MAX-1))
            return AVERROR_INVALIDDATA;
        title_str = av_malloc(title_size + 1); /* Add null terminator */
        if (!title_str)
            return AVERROR(ENOMEM);

        ret = ffio_read_size(pb, title_str, title_size);
        if (ret < 0) {
            av_freep(&title_str);
            return ret;
        }
        title_str[title_size] = 0;
        if (title_str[0]) {
            int off = (!c->isom && title_str[0] == title_size - 1);
            // flag added so as to not set stream handler name if already set from mdia->hdlr
            av_dict_set(&st->metadata, "handler_name", title_str + off, AV_DICT_DONT_OVERWRITE);
        }
        av_freep(&title_str);
    }

    return 0;
}

static int mov_read_esds(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return ff_mov_read_esds(c->fc, pb);
}

static int mov_read_dac3(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    AVPacketSideData *sd;
    enum AVAudioServiceType *ast;
    int ac3info, acmod, lfeon, bsmod;
    uint64_t mask;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_AUDIO_SERVICE_TYPE,
                                 sizeof(*ast), 0);
    if (!sd)
        return AVERROR(ENOMEM);

    ast = (enum AVAudioServiceType*)sd->data;
    ac3info = avio_rb24(pb);
    bsmod = (ac3info >> 14) & 0x7;
    acmod = (ac3info >> 11) & 0x7;
    lfeon = (ac3info >> 10) & 0x1;

    mask = ff_ac3_channel_layout_tab[acmod];
    if (lfeon)
        mask |= AV_CH_LOW_FREQUENCY;
    av_channel_layout_uninit(&st->codecpar->ch_layout);
    av_channel_layout_from_mask(&st->codecpar->ch_layout, mask);

    *ast = bsmod;
    if (st->codecpar->ch_layout.nb_channels > 1 && bsmod == 0x7)
        *ast = AV_AUDIO_SERVICE_TYPE_KARAOKE;

    return 0;
}

#if CONFIG_IAMFDEC
static int mov_read_iacb(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    FFIOContext b;
    AVIOContext *descriptor_pb;
    AVDictionary *metadata;
    IAMFContext *iamf;
    int64_t start_time, duration;
    unsigned descriptors_size;
    int nb_frames, disposition;
    int version, ret;

    if (atom.size < 5)
        return AVERROR_INVALIDDATA;

    if (c->fc->nb_streams < 1)
        return 0;

    version = avio_r8(pb);
    if (version != 1) {
        av_log(c->fc, AV_LOG_ERROR, "%s configurationVersion %d",
               version < 1 ? "invalid" : "unsupported", version);
        return AVERROR_INVALIDDATA;
    }

    descriptors_size = ffio_read_leb(pb);
    if (!descriptors_size || descriptors_size > INT_MAX)
        return AVERROR_INVALIDDATA;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    if (st->codecpar->extradata) {
        av_log(c->fc, AV_LOG_WARNING, "ignoring iacb\n");
        return 0;
    }

    sc->iamf = av_mallocz(sizeof(*sc->iamf));
    if (!sc->iamf)
        return AVERROR(ENOMEM);
    iamf = &sc->iamf->iamf;

    st->codecpar->extradata = av_malloc(descriptors_size);
    if (!st->codecpar->extradata)
        return AVERROR(ENOMEM);
    st->codecpar->extradata_size = descriptors_size;

    ret = avio_read(pb, st->codecpar->extradata, descriptors_size);
    if (ret != descriptors_size)
        return ret < 0 ? ret : AVERROR_INVALIDDATA;

    ffio_init_read_context(&b, st->codecpar->extradata, descriptors_size);
    descriptor_pb = &b.pub;

    ret = ff_iamfdec_read_descriptors(iamf, descriptor_pb, descriptors_size, c->fc);
    if (ret < 0)
        return ret;

    metadata = st->metadata;
    st->metadata = NULL;
    start_time = st->start_time;
    nb_frames = st->nb_frames;
    duration = st->duration;
    disposition = st->disposition;

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        IAMFAudioElement *audio_element = iamf->audio_elements[i];
        const AVIAMFAudioElement *element;
        AVStreamGroup *stg =
            avformat_stream_group_create(c->fc, AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT, NULL);

        if (!stg) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_iamf_audio_element_free(&stg->params.iamf_audio_element);
        stg->id = audio_element->audio_element_id;
        /* Transfer ownership */
        element = stg->params.iamf_audio_element = audio_element->element;
        audio_element->element = NULL;

        for (int j = 0; j < audio_element->nb_substreams; j++) {
            IAMFSubStream *substream = &audio_element->substreams[j];
            AVStream *stream;

            if (!i && !j) {
                if (audio_element->layers[0].substream_count != 1)
                    disposition &= ~AV_DISPOSITION_DEFAULT;
                stream = st;
            } else
                stream = avformat_new_stream(c->fc, NULL);
            if (!stream) {
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            stream->start_time = start_time;
            stream->nb_frames = nb_frames;
            stream->duration = duration;
            stream->disposition = disposition;
            if (stream != st) {
                stream->priv_data = sc;
                sc->refcount++;
            }

            if (element->audio_element_type == AV_IAMF_AUDIO_ELEMENT_TYPE_SCENE)
                stream->disposition |= AV_DISPOSITION_DEPENDENT;
            if (i || j) {
                stream->disposition |= AV_DISPOSITION_DEPENDENT;
                if (audio_element->layers[0].substream_count == 1)
                    stream->disposition &= ~AV_DISPOSITION_DEFAULT;
            }

            ret = avcodec_parameters_copy(stream->codecpar, substream->codecpar);
            if (ret < 0)
                goto fail;

            stream->id = substream->audio_substream_id;

            avpriv_set_pts_info(st, 64, 1, sc->time_scale);

            ret = avformat_stream_group_add_stream(stg, stream);
            if (ret < 0)
                goto fail;
        }

        ret = av_dict_copy(&stg->metadata, metadata, 0);
        if (ret < 0)
            goto fail;
    }

    for (int i = 0; i < iamf->nb_mix_presentations; i++) {
        IAMFMixPresentation *mix_presentation = iamf->mix_presentations[i];
        const AVIAMFMixPresentation *mix = mix_presentation->cmix;
        AVStreamGroup *stg =
            avformat_stream_group_create(c->fc, AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION, NULL);

        if (!stg) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        av_iamf_mix_presentation_free(&stg->params.iamf_mix_presentation);
        stg->id = mix_presentation->mix_presentation_id;
        /* Transfer ownership */
        stg->params.iamf_mix_presentation = mix_presentation->mix;
        mix_presentation->mix = NULL;

        for (int j = 0; j < mix->nb_submixes; j++) {
            const AVIAMFSubmix *submix = mix->submixes[j];

            for (int k = 0; k < submix->nb_elements; k++) {
                const AVIAMFSubmixElement *submix_element = submix->elements[k];
                const AVStreamGroup *audio_element = NULL;

                for (int l = 0; l < c->fc->nb_stream_groups; l++)
                    if (c->fc->stream_groups[l]->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT &&
                        c->fc->stream_groups[l]->id   == submix_element->audio_element_id) {
                        audio_element = c->fc->stream_groups[l];
                        break;
                    }
                av_assert0(audio_element);

                for (int l = 0; l < audio_element->nb_streams; l++) {
                    ret = avformat_stream_group_add_stream(stg, audio_element->streams[l]);
                    if (ret < 0 && ret != AVERROR(EEXIST))
                        goto fail;
                }
            }
        }

        ret = av_dict_copy(&stg->metadata, metadata, 0);
        if (ret < 0)
            goto fail;
    }

    ret = 0;
fail:
    av_dict_free(&metadata);

    return ret;
}
#endif

static int mov_read_dec3(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    AVPacketSideData *sd;
    enum AVAudioServiceType *ast;
    int eac3info, acmod, lfeon, bsmod;
    uint64_t mask;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_AUDIO_SERVICE_TYPE,
                                 sizeof(*ast), 0);
    if (!sd)
        return AVERROR(ENOMEM);

    ast = (enum AVAudioServiceType*)sd->data;

    /* No need to parse fields for additional independent substreams and its
     * associated dependent substreams since libavcodec's E-AC-3 decoder
     * does not support them yet. */
    avio_rb16(pb); /* data_rate and num_ind_sub */
    eac3info = avio_rb24(pb);
    bsmod = (eac3info >> 12) & 0x1f;
    acmod = (eac3info >>  9) & 0x7;
    lfeon = (eac3info >>  8) & 0x1;

    mask = ff_ac3_channel_layout_tab[acmod];
    if (lfeon)
        mask |= AV_CH_LOW_FREQUENCY;
    av_channel_layout_uninit(&st->codecpar->ch_layout);
    av_channel_layout_from_mask(&st->codecpar->ch_layout, mask);

    *ast = bsmod;
    if (st->codecpar->ch_layout.nb_channels > 1 && bsmod == 0x7)
        *ast = AV_AUDIO_SERVICE_TYPE_KARAOKE;

    return 0;
}

static int mov_read_ddts(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
#define DDTS_SIZE 20
    uint8_t buf[DDTS_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    AVStream *st = NULL;
    uint32_t frame_duration_code = 0;
    uint32_t channel_layout_code = 0;
    GetBitContext gb;
    int ret;

    if ((ret = ffio_read_size(pb, buf, DDTS_SIZE)) < 0)
        return ret;

    init_get_bits(&gb, buf, 8 * DDTS_SIZE);

    if (c->fc->nb_streams < 1) {
        return 0;
    }
    st = c->fc->streams[c->fc->nb_streams-1];

    st->codecpar->sample_rate = get_bits_long(&gb, 32);
    if (st->codecpar->sample_rate <= 0) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid sample rate %d\n", st->codecpar->sample_rate);
        return AVERROR_INVALIDDATA;
    }
    skip_bits_long(&gb, 32); /* max bitrate */
    st->codecpar->bit_rate = get_bits_long(&gb, 32);
    st->codecpar->bits_per_coded_sample = get_bits(&gb, 8);
    frame_duration_code = get_bits(&gb, 2);
    skip_bits(&gb, 30); /* various fields */
    channel_layout_code = get_bits(&gb, 16);

    st->codecpar->frame_size =
            (frame_duration_code == 0) ? 512 :
            (frame_duration_code == 1) ? 1024 :
            (frame_duration_code == 2) ? 2048 :
            (frame_duration_code == 3) ? 4096 : 0;

    if (channel_layout_code > 0xff) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported DTS audio channel layout\n");
    }
    av_channel_layout_uninit(&st->codecpar->ch_layout);
    av_channel_layout_from_mask(&st->codecpar->ch_layout,
            ((channel_layout_code & 0x1) ? AV_CH_FRONT_CENTER : 0) |
            ((channel_layout_code & 0x2) ? AV_CH_FRONT_LEFT : 0) |
            ((channel_layout_code & 0x2) ? AV_CH_FRONT_RIGHT : 0) |
            ((channel_layout_code & 0x4) ? AV_CH_SIDE_LEFT : 0) |
            ((channel_layout_code & 0x4) ? AV_CH_SIDE_RIGHT : 0) |
            ((channel_layout_code & 0x8) ? AV_CH_LOW_FREQUENCY : 0));

    return 0;
}

static int mov_read_chan(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (atom.size < 16)
        return 0;

    /* skip version and flags */
    avio_skip(pb, 4);

    ff_mov_read_chan(c->fc, pb, st, atom.size - 4);

    return 0;
}

static int mov_read_chnl(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t end = av_sat_add64(avio_tell(pb), atom.size);
    int version, flags;
    int ret;
    AVStream *st;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    version = avio_r8(pb);
    flags   = avio_rb24(pb);
    if (version != 0 || flags != 0) {
        av_log(c->fc, AV_LOG_ERROR,
               "Unsupported 'chnl' box with version %d, flags: %#x",
               version, flags);
        return AVERROR_INVALIDDATA;
    }

    ret = ff_mov_read_chnl(c->fc, pb, st);
    if (ret < 0)
        return ret;

    if (avio_tell(pb) != end) {
        av_log(c->fc, AV_LOG_WARNING, "skip %" PRId64 " bytes of unknown data inside chnl\n",
                end - avio_tell(pb));
        avio_seek(pb, end, SEEK_SET);
    }
    return ret;
}

static int mov_read_wfex(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((ret = ff_get_wav_header(c->fc, pb, st->codecpar, atom.size, 0)) < 0)
        av_log(c->fc, AV_LOG_WARNING, "get_wav_header failed\n");

    return ret;
}

static int mov_read_clap(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    HEIFItem *item;
    AVPacketSideData *sd;
    int width, height, err = 0;
    AVRational aperture_width, aperture_height, horiz_off, vert_off;
    AVRational pc_x, pc_y;
    uint64_t top, bottom, left, right;

    item = heif_cur_item(c);
    st = get_curr_st(c);
    if (!st)
        return 0;

    width  = st->codecpar->width;
    height = st->codecpar->height;
    if ((!width || !height) && item) {
        width  = item->width;
        height = item->height;
    }
    if (!width || !height) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    aperture_width.num  = avio_rb32(pb);
    aperture_width.den  = avio_rb32(pb);
    aperture_height.num = avio_rb32(pb);
    aperture_height.den = avio_rb32(pb);

    horiz_off.num = avio_rb32(pb);
    horiz_off.den = avio_rb32(pb);
    vert_off.num  = avio_rb32(pb);
    vert_off.den  = avio_rb32(pb);

    if (aperture_width.num  < 0 || aperture_width.den  < 0 ||
        aperture_height.num < 0 || aperture_height.den < 0 ||
        horiz_off.den       < 0 || vert_off.den        < 0) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }
    if ((av_cmp_q((AVRational) { width,  1 }, aperture_width)  < 0) ||
        (av_cmp_q((AVRational) { height, 1 }, aperture_height) < 0)) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }
    av_log(c->fc, AV_LOG_TRACE, "clap: apertureWidth %d/%d, apertureHeight %d/%d "
                                "horizOff %d/%d vertOff %d/%d\n",
           aperture_width.num, aperture_width.den, aperture_height.num, aperture_height.den,
           horiz_off.num, horiz_off.den, vert_off.num, vert_off.den);

    pc_x   = av_mul_q((AVRational) { width  - 1, 1 }, (AVRational) { 1, 2 });
    pc_x   = av_add_q(pc_x, horiz_off);
    pc_y   = av_mul_q((AVRational) { height - 1, 1 }, (AVRational) { 1, 2 });
    pc_y   = av_add_q(pc_y, vert_off);

    aperture_width  = av_sub_q(aperture_width,  (AVRational) { 1, 1 });
    aperture_width  = av_mul_q(aperture_width,  (AVRational) { 1, 2 });
    aperture_height = av_sub_q(aperture_height, (AVRational) { 1, 1 });
    aperture_height = av_mul_q(aperture_height, (AVRational) { 1, 2 });

    left   = av_q2d(av_sub_q(pc_x, aperture_width));
    right  = av_q2d(av_add_q(pc_x, aperture_width));
    top    = av_q2d(av_sub_q(pc_y, aperture_height));
    bottom = av_q2d(av_add_q(pc_y, aperture_height));

    if (bottom > (height - 1) ||
        right  > (width  - 1)) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    bottom = height - 1 - bottom;
    right  = width  - 1 - right;

    if (!(left | right | top | bottom))
        return 0;

    if ((left + right) >= width ||
        (top + bottom) >= height) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_FRAME_CROPPING,
                                 sizeof(uint32_t) * 4, 0);
    if (!sd)
        return AVERROR(ENOMEM);

    AV_WL32A(sd->data,      top);
    AV_WL32A(sd->data + 4,  bottom);
    AV_WL32A(sd->data + 8,  left);
    AV_WL32A(sd->data + 12, right);

fail:
    if (err < 0) {
        int explode = !!(c->fc->error_recognition & AV_EF_EXPLODE);
        av_log(c->fc, explode ? AV_LOG_ERROR : AV_LOG_WARNING, "Invalid clap box\n");
        if (!explode)
            err = 0;
    }

    return err;
}

/* This atom overrides any previously set aspect ratio */
static int mov_read_pasp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    const int num = avio_rb32(pb);
    const int den = avio_rb32(pb);
    AVStream *st;
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    av_log(c->fc, AV_LOG_TRACE, "pasp: hSpacing %d, vSpacing %d\n", num, den);

    if (den != 0) {
        sc->h_spacing = num;
        sc->v_spacing = den;
    }
    return 0;
}

/* this atom contains actual media data */
static int mov_read_mdat(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    if (atom.size == 0) /* wrong one (MP4) */
        return 0;
    c->found_mdat=1;
    return 0; /* now go for moov */
}

#define DRM_BLOB_SIZE 56

static int mov_read_adrm(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    uint8_t intermediate_key[20];
    uint8_t intermediate_iv[20];
    uint8_t input[64];
    uint8_t output[64];
    uint8_t file_checksum[20];
    uint8_t calculated_checksum[20];
    char checksum_string[2 * sizeof(file_checksum) + 1];
    struct AVSHA *sha;
    int i;
    int ret = 0;
    uint8_t *activation_bytes = c->activation_bytes;
    uint8_t *fixed_key = c->audible_fixed_key;

    c->aax_mode = 1;

    sha = av_sha_alloc();
    if (!sha)
        return AVERROR(ENOMEM);
    av_free(c->aes_decrypt);
    c->aes_decrypt = av_aes_alloc();
    if (!c->aes_decrypt) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* drm blob processing */
    avio_read(pb, output, 8); // go to offset 8, absolute position 0x251
    avio_read(pb, input, DRM_BLOB_SIZE);
    avio_read(pb, output, 4); // go to offset 4, absolute position 0x28d
    avio_read(pb, file_checksum, 20);

    // required by external tools
    ff_data_to_hex(checksum_string, file_checksum, sizeof(file_checksum), 1);
    av_log(c->fc, AV_LOG_INFO, "[aax] file checksum == %s\n", checksum_string);

    /* verify activation data */
    if (!activation_bytes) {
        av_log(c->fc, AV_LOG_WARNING, "[aax] activation_bytes option is missing!\n");
        ret = 0;  /* allow ffprobe to continue working on .aax files */
        goto fail;
    }
    if (c->activation_bytes_size != 4) {
        av_log(c->fc, AV_LOG_FATAL, "[aax] activation_bytes value needs to be 4 bytes!\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* verify fixed key */
    if (c->audible_fixed_key_size != 16) {
        av_log(c->fc, AV_LOG_FATAL, "[aax] audible_fixed_key value needs to be 16 bytes!\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    /* AAX (and AAX+) key derivation */
    av_sha_init(sha, 160);
    av_sha_update(sha, fixed_key, 16);
    av_sha_update(sha, activation_bytes, 4);
    av_sha_final(sha, intermediate_key);
    av_sha_init(sha, 160);
    av_sha_update(sha, fixed_key, 16);
    av_sha_update(sha, intermediate_key, 20);
    av_sha_update(sha, activation_bytes, 4);
    av_sha_final(sha, intermediate_iv);
    av_sha_init(sha, 160);
    av_sha_update(sha, intermediate_key, 16);
    av_sha_update(sha, intermediate_iv, 16);
    av_sha_final(sha, calculated_checksum);
    if (memcmp(calculated_checksum, file_checksum, 20)) { // critical error
        av_log(c->fc, AV_LOG_ERROR, "[aax] mismatch in checksums!\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    av_aes_init(c->aes_decrypt, intermediate_key, 128, 1);
    av_aes_crypt(c->aes_decrypt, output, input, DRM_BLOB_SIZE >> 4, intermediate_iv, 1);
    for (i = 0; i < 4; i++) {
        // file data (in output) is stored in big-endian mode
        if (activation_bytes[i] != output[3 - i]) { // critical error
            av_log(c->fc, AV_LOG_ERROR, "[aax] error in drm blob decryption!\n");
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
    }
    memcpy(c->file_key, output + 8, 16);
    memcpy(input, output + 26, 16);
    av_sha_init(sha, 160);
    av_sha_update(sha, input, 16);
    av_sha_update(sha, c->file_key, 16);
    av_sha_update(sha, fixed_key, 16);
    av_sha_final(sha, c->file_iv);

fail:
    av_free(sha);

    return ret;
}

static int mov_aaxc_crypto(MOVContext *c)
{
    if (c->audible_key_size != 16) {
        av_log(c->fc, AV_LOG_FATAL, "[aaxc] audible_key value needs to be 16 bytes!\n");
        return AVERROR(EINVAL);
    }

    if (c->audible_iv_size != 16) {
        av_log(c->fc, AV_LOG_FATAL, "[aaxc] audible_iv value needs to be 16 bytes!\n");
        return AVERROR(EINVAL);
    }

    c->aes_decrypt = av_aes_alloc();
    if (!c->aes_decrypt) {
        return AVERROR(ENOMEM);
    }

    memcpy(c->file_key, c->audible_key, 16);
    memcpy(c->file_iv, c->audible_iv, 16);
    c->aax_mode = 1;

    return 0;
}

// Audible AAX (and AAX+) bytestream decryption
static int aax_filter(uint8_t *input, int size, MOVContext *c)
{
    int blocks = 0;
    unsigned char iv[16];

    memcpy(iv, c->file_iv, 16); // iv is overwritten
    blocks = size >> 4; // trailing bytes are not encrypted!
    av_aes_init(c->aes_decrypt, c->file_key, 128, 1);
    av_aes_crypt(c->aes_decrypt, input, input, blocks, iv, 1);

    return 0;
}

/* read major brand, minor version and compatible brands and store them as metadata */
static int mov_read_ftyp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    uint32_t minor_ver;
    int comp_brand_size;
    char* comp_brands_str;
    uint8_t type[5] = {0};
    int ret = ffio_read_size(pb, type, 4);
    if (ret < 0)
        return ret;
    if (c->fc->nb_streams) {
        if (c->fc->strict_std_compliance >= FF_COMPLIANCE_STRICT)
            return AVERROR_INVALIDDATA;
        av_log(c->fc, AV_LOG_DEBUG, "Ignoring duplicate FTYP\n");
        return 0;
    }

    if (strcmp(type, "qt  "))
        c->isom = 1;
    av_log(c->fc, AV_LOG_DEBUG, "ISO: File Type Major Brand: %.4s\n",(char *)&type);
    av_dict_set(&c->fc->metadata, "major_brand", type, 0);
    minor_ver = avio_rb32(pb); /* minor version */
    av_dict_set_int(&c->fc->metadata, "minor_version", minor_ver, 0);

    comp_brand_size = atom.size - 8;
    if (comp_brand_size < 0 || comp_brand_size == INT_MAX)
        return AVERROR_INVALIDDATA;
    comp_brands_str = av_malloc(comp_brand_size + 1); /* Add null terminator */
    if (!comp_brands_str)
        return AVERROR(ENOMEM);

    ret = ffio_read_size(pb, comp_brands_str, comp_brand_size);
    if (ret < 0) {
        av_freep(&comp_brands_str);
        return ret;
    }
    comp_brands_str[comp_brand_size] = 0;
    av_dict_set(&c->fc->metadata, "compatible_brands",
                comp_brands_str, AV_DICT_DONT_STRDUP_VAL);

    // Logic for handling Audible's .aaxc files
    if (!strcmp(type, "aaxc")) {
        mov_aaxc_crypto(c);
    }

    return 0;
}

/* this atom should contain all header atoms */
static int mov_read_moov(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret;

    if (c->found_moov) {
        av_log(c->fc, AV_LOG_WARNING, "Found duplicated MOOV Atom. Skipped it\n");
        avio_skip(pb, atom.size);
        return 0;
    }

    if ((ret = mov_read_default(c, pb, atom)) < 0)
        return ret;
    /* we parsed the 'moov' atom, we can terminate the parsing as soon as we find the 'mdat' */
    /* so we don't parse the whole file if over a network */
    c->found_moov=1;
    return 0; /* now go for mdat */
}

static MOVFragmentStreamInfo * get_frag_stream_info(
    MOVFragmentIndex *frag_index,
    int index,
    int id)
{
    int i;
    MOVFragmentIndexItem * item;

    if (index < 0 || index >= frag_index->nb_items)
        return NULL;
    item = &frag_index->item[index];
    for (i = 0; i < item->nb_stream_info; i++)
        if (item->stream_info[i].id == id)
            return &item->stream_info[i];

    // This shouldn't happen
    return NULL;
}

static void set_frag_stream(MOVFragmentIndex *frag_index, int id)
{
    int i;
    MOVFragmentIndexItem * item;

    if (frag_index->current < 0 ||
        frag_index->current >= frag_index->nb_items)
        return;

    item = &frag_index->item[frag_index->current];
    for (i = 0; i < item->nb_stream_info; i++)
        if (item->stream_info[i].id == id) {
            item->current = i;
            return;
        }

    // id not found.  This shouldn't happen.
    item->current = -1;
}

static MOVFragmentStreamInfo * get_current_frag_stream_info(
    MOVFragmentIndex *frag_index)
{
    MOVFragmentIndexItem *item;
    if (frag_index->current < 0 ||
        frag_index->current >= frag_index->nb_items)
        return NULL;

    item = &frag_index->item[frag_index->current];
    if (item->current >= 0 && item->current < item->nb_stream_info)
        return &item->stream_info[item->current];

    // This shouldn't happen
    return NULL;
}

static int search_frag_moof_offset(MOVFragmentIndex *frag_index, int64_t offset)
{
    int a, b, m;
    int64_t moof_offset;

    // Optimize for appending new entries
    if (!frag_index->nb_items ||
        frag_index->item[frag_index->nb_items - 1].moof_offset < offset)
        return frag_index->nb_items;

    a = -1;
    b = frag_index->nb_items;

    while (b - a > 1) {
        m = (a + b) >> 1;
        moof_offset = frag_index->item[m].moof_offset;
        if (moof_offset >= offset)
            b = m;
        if (moof_offset <= offset)
            a = m;
    }
    return b;
}

static int64_t get_stream_info_time(MOVFragmentStreamInfo * frag_stream_info)
{
    av_assert0(frag_stream_info);
    if (frag_stream_info->sidx_pts != AV_NOPTS_VALUE)
        return frag_stream_info->sidx_pts;
    if (frag_stream_info->first_tfra_pts != AV_NOPTS_VALUE)
        return frag_stream_info->first_tfra_pts;
    return frag_stream_info->tfdt_dts;
}

static int64_t get_frag_time(AVFormatContext *s, AVStream *dst_st,
                             MOVFragmentIndex *frag_index, int index)
{
    MOVFragmentStreamInfo * frag_stream_info;
    MOVStreamContext *sc = dst_st->priv_data;
    int64_t timestamp;
    int i, j;

    // If the stream is referenced by any sidx, limit the search
    // to fragments that referenced this stream in the sidx
    if (sc->has_sidx) {
        frag_stream_info = get_frag_stream_info(frag_index, index, sc->id);
        if (!frag_stream_info)
            return AV_NOPTS_VALUE;
        if (frag_stream_info->sidx_pts != AV_NOPTS_VALUE)
            return frag_stream_info->sidx_pts;
        if (frag_stream_info->first_tfra_pts != AV_NOPTS_VALUE)
            return frag_stream_info->first_tfra_pts;
        return frag_stream_info->sidx_pts;
    }

    for (i = 0; i < frag_index->item[index].nb_stream_info; i++) {
        AVStream *frag_stream = NULL;
        frag_stream_info = &frag_index->item[index].stream_info[i];
        for (j = 0; j < s->nb_streams; j++) {
            MOVStreamContext *sc2 = s->streams[j]->priv_data;
            if (sc2->id == frag_stream_info->id)
                frag_stream = s->streams[j];
        }
        if (!frag_stream) {
            av_log(s, AV_LOG_WARNING, "No stream matching sidx ID found.\n");
            continue;
        }
        timestamp = get_stream_info_time(frag_stream_info);
        if (timestamp != AV_NOPTS_VALUE)
            return av_rescale_q(timestamp, frag_stream->time_base, dst_st->time_base);
    }
    return AV_NOPTS_VALUE;
}

static int search_frag_timestamp(AVFormatContext *s, MOVFragmentIndex *frag_index,
                                 AVStream *st, int64_t timestamp)
{
    int a, b, m, m0;
    int64_t frag_time;

    a = -1;
    b = frag_index->nb_items;

    while (b - a > 1) {
        m0 = m = (a + b) >> 1;

        while (m < b &&
               (frag_time = get_frag_time(s, st, frag_index, m)) == AV_NOPTS_VALUE)
            m++;

        if (m < b && frag_time <= timestamp)
            a = m;
        else
            b = m0;
    }

    return a;
}

static int update_frag_index(MOVContext *c, int64_t offset)
{
    int index, i;
    MOVFragmentIndexItem * item;
    MOVFragmentStreamInfo * frag_stream_info;

    // If moof_offset already exists in frag_index, return index to it
    index = search_frag_moof_offset(&c->frag_index, offset);
    if (index < c->frag_index.nb_items &&
        c->frag_index.item[index].moof_offset == offset)
        return index;

    // offset is not yet in frag index.
    // Insert new item at index (sorted by moof offset)
    item = av_fast_realloc(c->frag_index.item,
                           &c->frag_index.allocated_size,
                           (c->frag_index.nb_items + 1) *
                           sizeof(*c->frag_index.item));
    if (!item)
        return -1;
    c->frag_index.item = item;

    frag_stream_info = av_realloc_array(NULL, c->fc->nb_streams,
                                        sizeof(*item->stream_info));
    if (!frag_stream_info)
        return -1;

    for (i = 0; i < c->fc->nb_streams; i++) {
        // Avoid building frag index if streams lack track id.
        MOVStreamContext *sc = c->fc->streams[i]->priv_data;
        if (sc->id < 0) {
            av_free(frag_stream_info);
            return AVERROR_INVALIDDATA;
        }

        frag_stream_info[i].id = sc->id;
        frag_stream_info[i].sidx_pts = AV_NOPTS_VALUE;
        frag_stream_info[i].tfdt_dts = AV_NOPTS_VALUE;
        frag_stream_info[i].next_trun_dts = AV_NOPTS_VALUE;
        frag_stream_info[i].first_tfra_pts = AV_NOPTS_VALUE;
        frag_stream_info[i].index_base = -1;
        frag_stream_info[i].index_entry = -1;
        frag_stream_info[i].encryption_index = NULL;
        frag_stream_info[i].stsd_id = -1;
    }

    if (index < c->frag_index.nb_items)
        memmove(c->frag_index.item + index + 1, c->frag_index.item + index,
                (c->frag_index.nb_items - index) * sizeof(*c->frag_index.item));

    item = &c->frag_index.item[index];
    item->headers_read = 0;
    item->current = 0;
    item->nb_stream_info = c->fc->nb_streams;
    item->moof_offset = offset;
    item->stream_info = frag_stream_info;
    c->frag_index.nb_items++;

    return index;
}

static void fix_frag_index_entries(MOVFragmentIndex *frag_index, int index,
                                   int id, int entries)
{
    int i;
    MOVFragmentStreamInfo * frag_stream_info;

    if (index < 0)
        return;
    for (i = index; i < frag_index->nb_items; i++) {
        frag_stream_info = get_frag_stream_info(frag_index, i, id);
        if (frag_stream_info && frag_stream_info->index_entry >= 0)
            frag_stream_info->index_entry += entries;
    }
}

static int mov_read_moof(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    // Set by mov_read_tfhd(). mov_read_trun() will reject files missing tfhd.
    c->fragment.found_tfhd = 0;

    if (!c->has_looked_for_mfra && c->use_mfra_for > 0) {
        c->has_looked_for_mfra = 1;
        if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
            int ret;
            av_log(c->fc, AV_LOG_VERBOSE, "stream has moof boxes, will look "
                    "for a mfra\n");
            if ((ret = mov_read_mfra(c, pb)) < 0) {
                av_log(c->fc, AV_LOG_VERBOSE, "found a moof box but failed to "
                        "read the mfra (may be a live ismv)\n");
            }
        } else {
            av_log(c->fc, AV_LOG_VERBOSE, "found a moof box but stream is not "
                    "seekable, can not look for mfra\n");
        }
    }
    c->fragment.moof_offset = c->fragment.implicit_offset = avio_tell(pb) - 8;
    av_log(c->fc, AV_LOG_TRACE, "moof offset %"PRIx64"\n", c->fragment.moof_offset);
    c->frag_index.current = update_frag_index(c, c->fragment.moof_offset);
    return mov_read_default(c, pb, atom);
}

static void mov_metadata_creation_time(MOVContext *c, AVIOContext *pb, AVDictionary **metadata, int version)
{
    int64_t time;
    if (version == 1) {
        time = avio_rb64(pb);
        avio_rb64(pb);
        if (time < 0) {
            av_log(c->fc, AV_LOG_DEBUG, "creation_time is negative\n");
            return;
        }
    } else {
        time = avio_rb32(pb);
        avio_rb32(pb); /* modification time */
        if (time > 0 && time < 2082844800) {
            av_log(c->fc, AV_LOG_WARNING, "Detected creation time before 1970, parsing as unix timestamp.\n");
            time += 2082844800;
        }
    }
    if (time) {
        time -= 2082844800;  /* seconds between 1904-01-01 and Epoch */

        if ((int64_t)(time * 1000000ULL) / 1000000 != time) {
            av_log(c->fc, AV_LOG_DEBUG, "creation_time is not representable\n");
            return;
        }

        ff_dict_set_timestamp(metadata, "creation_time", time * 1000000);
    }
}

static int mov_read_mdhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int version;
    char language[4] = {0};
    unsigned lang;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    if (sc->time_scale) {
        av_log(c->fc, AV_LOG_ERROR, "Multiple mdhd?\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version > 1) {
        avpriv_request_sample(c->fc, "Version %d", version);
        return AVERROR_PATCHWELCOME;
    }
    avio_rb24(pb); /* flags */
    mov_metadata_creation_time(c, pb, &st->metadata, version);

    sc->time_scale = avio_rb32(pb);
    if (sc->time_scale <= 0) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid mdhd time scale %d, defaulting to 1\n", sc->time_scale);
        sc->time_scale = 1;
    }
    st->duration = (version == 1) ? avio_rb64(pb) : avio_rb32(pb); /* duration */

    if ((version == 1 && st->duration == UINT64_MAX) ||
        (version != 1 && st->duration == UINT32_MAX)) {
        st->duration = 0;
    }

    lang = avio_rb16(pb); /* language */
    if (ff_mov_lang_to_iso639(lang, language))
        av_dict_set(&st->metadata, "language", language, 0);
    avio_rb16(pb); /* quality */

    return 0;
}

static int mov_read_mvhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int i;
    int version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    mov_metadata_creation_time(c, pb, &c->fc->metadata, version);
    c->time_scale = avio_rb32(pb); /* time scale */
    if (c->time_scale <= 0) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid mvhd time scale %d, defaulting to 1\n", c->time_scale);
        c->time_scale = 1;
    }
    av_log(c->fc, AV_LOG_TRACE, "time scale = %i\n", c->time_scale);

    c->duration = (version == 1) ? avio_rb64(pb) : avio_rb32(pb); /* duration */
    avio_rb32(pb); /* preferred scale */

    avio_rb16(pb); /* preferred volume */

    avio_skip(pb, 10); /* reserved */

    /* movie display matrix, store it in main context and use it later on */
    for (i = 0; i < 3; i++) {
        c->movie_display_matrix[i][0] = avio_rb32(pb); // 16.16 fixed point
        c->movie_display_matrix[i][1] = avio_rb32(pb); // 16.16 fixed point
        c->movie_display_matrix[i][2] = avio_rb32(pb); //  2.30 fixed point
    }

    avio_rb32(pb); /* preview time */
    avio_rb32(pb); /* preview duration */
    avio_rb32(pb); /* poster time */
    avio_rb32(pb); /* selection time */
    avio_rb32(pb); /* selection duration */
    avio_rb32(pb); /* current time */
    avio_rb32(pb); /* next track ID */

    return 0;
}

static void set_last_stream_little_endian(AVFormatContext *fc)
{
    AVStream *st;

    if (fc->nb_streams < 1)
        return;
    st = fc->streams[fc->nb_streams-1];

    switch (st->codecpar->codec_id) {
    case AV_CODEC_ID_PCM_S16BE:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        break;
    case AV_CODEC_ID_PCM_S24BE:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
        break;
    case AV_CODEC_ID_PCM_S32BE:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_S32LE;
        break;
    case AV_CODEC_ID_PCM_F32BE:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
        break;
    case AV_CODEC_ID_PCM_F64BE:
        st->codecpar->codec_id = AV_CODEC_ID_PCM_F64LE;
        break;
    default:
        break;
    }
}

static int mov_read_enda(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int little_endian = avio_rb16(pb) & 0xFF;
    av_log(c->fc, AV_LOG_TRACE, "enda %d\n", little_endian);
    if (little_endian == 1)
        set_last_stream_little_endian(c->fc);
    return 0;
}

static int mov_read_pcmc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int format_flags;
    int version, flags;
    int pcm_sample_size;
    AVFormatContext *fc = c->fc;
    AVStream *st;
    MOVStreamContext *sc;

    if (atom.size < 6) {
        av_log(c->fc, AV_LOG_ERROR, "Empty pcmC box\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    flags   = avio_rb24(pb);

    if (version != 0 || flags != 0) {
        av_log(c->fc, AV_LOG_ERROR,
               "Unsupported 'pcmC' box with version %d, flags: %x",
               version, flags);
        return AVERROR_INVALIDDATA;
    }

    format_flags = avio_r8(pb);
    pcm_sample_size = avio_r8(pb);

    if (fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    st = fc->streams[fc->nb_streams - 1];
    sc = st->priv_data;

    if (sc->format == MOV_MP4_FPCM_TAG) {
        switch (pcm_sample_size) {
        case 32:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_F32BE;
            break;
        case 64:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_F64BE;
            break;
        default:
            av_log(fc, AV_LOG_ERROR, "invalid pcm_sample_size %d for %s\n",
                                     pcm_sample_size,
                                     av_fourcc2str(sc->format));
            return AVERROR_INVALIDDATA;
        }
    } else if (sc->format == MOV_MP4_IPCM_TAG) {
        switch (pcm_sample_size) {
        case 16:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
            break;
        case 24:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_S24BE;
            break;
        case 32:
            st->codecpar->codec_id = AV_CODEC_ID_PCM_S32BE;
            break;
        default:
            av_log(fc, AV_LOG_ERROR, "invalid pcm_sample_size %d for %s\n",
                                     pcm_sample_size,
                                     av_fourcc2str(sc->format));
            return AVERROR_INVALIDDATA;
        }
    } else {
        av_log(fc, AV_LOG_ERROR, "'pcmC' with invalid sample entry '%s'\n",
                av_fourcc2str(sc->format));
        return AVERROR_INVALIDDATA;
    }

    if (format_flags & 1) // indicates little-endian format. If not present, big-endian format is used
        set_last_stream_little_endian(c->fc);
    st->codecpar->bits_per_coded_sample = av_get_bits_per_sample(st->codecpar->codec_id);

    return 0;
}

static int mov_read_colr(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    HEIFItem *item = NULL;
    char color_parameter_type[5] = { 0 };
    uint16_t color_primaries, color_trc, color_matrix;
    int ret;

    st = get_curr_st(c);
    if (!st) {
        item = heif_cur_item(c);
        if (!item)
            return 0;
    }

    ret = ffio_read_size(pb, color_parameter_type, 4);
    if (ret < 0)
        return ret;
    if (strncmp(color_parameter_type, "nclx", 4) &&
        strncmp(color_parameter_type, "nclc", 4) &&
        strncmp(color_parameter_type, "prof", 4)) {
        av_log(c->fc, AV_LOG_WARNING, "unsupported color_parameter_type %s\n",
               color_parameter_type);
        return 0;
    }

    if (!strncmp(color_parameter_type, "prof", 4)) {
        AVPacketSideData *sd;
        uint8_t *icc_profile;
        if (st) {
            sd = av_packet_side_data_new(&st->codecpar->coded_side_data,
                                         &st->codecpar->nb_coded_side_data,
                                         AV_PKT_DATA_ICC_PROFILE,
                                         atom.size - 4, 0);
            if (!sd)
                return AVERROR(ENOMEM);
            icc_profile = sd->data;
        } else {
            av_freep(&item->icc_profile);
            icc_profile = item->icc_profile = av_malloc(atom.size - 4);
            if (!icc_profile) {
                item->icc_profile_size = 0;
                return AVERROR(ENOMEM);
            }
            item->icc_profile_size = atom.size - 4;
        }
        ret = ffio_read_size(pb, icc_profile, atom.size - 4);
        if (ret < 0)
            return ret;
    } else if (st) {
        color_primaries = avio_rb16(pb);
        color_trc = avio_rb16(pb);
        color_matrix = avio_rb16(pb);

        av_log(c->fc, AV_LOG_TRACE,
               "%s: pri %d trc %d matrix %d",
               color_parameter_type, color_primaries, color_trc, color_matrix);

        if (!strncmp(color_parameter_type, "nclx", 4)) {
            uint8_t color_range = avio_r8(pb) >> 7;
            av_log(c->fc, AV_LOG_TRACE, " full %"PRIu8"", color_range);
            if (color_range)
                st->codecpar->color_range = AVCOL_RANGE_JPEG;
            else
                st->codecpar->color_range = AVCOL_RANGE_MPEG;
        }

        if (!av_color_primaries_name(color_primaries))
            color_primaries = AVCOL_PRI_UNSPECIFIED;
        if (!av_color_transfer_name(color_trc))
            color_trc = AVCOL_TRC_UNSPECIFIED;
        if (!av_color_space_name(color_matrix))
            color_matrix = AVCOL_SPC_UNSPECIFIED;

        st->codecpar->color_primaries = color_primaries;
        st->codecpar->color_trc       = color_trc;
        st->codecpar->color_space     = color_matrix;
        av_log(c->fc, AV_LOG_TRACE, "\n");
    }
    return 0;
}

static int mov_read_fiel(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    unsigned mov_field_order;
    enum AVFieldOrder decoded_field_order = AV_FIELD_UNKNOWN;

    if (c->fc->nb_streams < 1) // will happen with jp2 files
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    if (atom.size < 2)
        return AVERROR_INVALIDDATA;
    mov_field_order = avio_rb16(pb);
    if ((mov_field_order & 0xFF00) == 0x0100)
        decoded_field_order = AV_FIELD_PROGRESSIVE;
    else if ((mov_field_order & 0xFF00) == 0x0200) {
        switch (mov_field_order & 0xFF) {
        case 0x01: decoded_field_order = AV_FIELD_TT;
                   break;
        case 0x06: decoded_field_order = AV_FIELD_BB;
                   break;
        case 0x09: decoded_field_order = AV_FIELD_TB;
                   break;
        case 0x0E: decoded_field_order = AV_FIELD_BT;
                   break;
        }
    }
    if (decoded_field_order == AV_FIELD_UNKNOWN && mov_field_order) {
        av_log(c->fc, AV_LOG_ERROR, "Unknown MOV field order 0x%04x\n", mov_field_order);
    }
    st->codecpar->field_order = decoded_field_order;

    return 0;
}

static int mov_realloc_extradata(AVCodecParameters *par, MOVAtom atom)
{
    int err = 0;
    uint64_t size = (uint64_t)par->extradata_size + atom.size + 8 + AV_INPUT_BUFFER_PADDING_SIZE;
    if (size > INT_MAX || (uint64_t)atom.size > INT_MAX)
        return AVERROR_INVALIDDATA;
    if ((err = av_reallocp(&par->extradata, size)) < 0) {
        par->extradata_size = 0;
        return err;
    }
    par->extradata_size = size - AV_INPUT_BUFFER_PADDING_SIZE;
    return 0;
}

/* Read a whole atom into the extradata return the size of the atom read, possibly truncated if != atom.size */
static int64_t mov_read_atom_into_extradata(MOVContext *c, AVIOContext *pb, MOVAtom atom,
                                        AVCodecParameters *par, uint8_t *buf)
{
    int64_t result = atom.size;
    int err;

    AV_WB32(buf    , atom.size + 8);
    AV_WL32(buf + 4, atom.type);
    err = ffio_read_size(pb, buf + 8, atom.size);
    if (err < 0) {
        par->extradata_size -= atom.size;
        return err;
    } else if (err < atom.size) {
        av_log(c->fc, AV_LOG_WARNING, "truncated extradata\n");
        par->extradata_size -= atom.size - err;
        result = err;
    }
    memset(buf + 8 + err, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    return result;
}

/* FIXME modify QDM2/SVQ3/H.264 decoders to take full atom as extradata */
static int mov_read_extradata(MOVContext *c, AVIOContext *pb, MOVAtom atom,
                              enum AVCodecID codec_id)
{
    AVStream *st;
    uint64_t original_size;
    int err;

    if (c->fc->nb_streams < 1) // will happen with jp2 files
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (st->codecpar->codec_id != codec_id)
        return 0; /* unexpected codec_id - don't mess with extradata */

    original_size = st->codecpar->extradata_size;
    err = mov_realloc_extradata(st->codecpar, atom);
    if (err)
        return err;

    err =  mov_read_atom_into_extradata(c, pb, atom, st->codecpar,  st->codecpar->extradata + original_size);
    if (err < 0)
        return err;
    return 0; // Note: this is the original behavior to ignore truncation.
}

/* wrapper functions for reading ALAC/AVS/MJPEG/MJPEG2000 extradata atoms only for those codecs */
static int mov_read_alac(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_ALAC);
}

static int mov_read_avss(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_CAVS);
}

static int mov_read_jp2h(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_JPEG2000);
}

static int mov_read_dpxe(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_R10K);
}

static int mov_read_avid(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret = mov_read_extradata(c, pb, atom, AV_CODEC_ID_AVUI);
    if (!ret)
        ret = mov_read_extradata(c, pb, atom, AV_CODEC_ID_DNXHD);
    return ret;
}

static int mov_read_targa_y216(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret = mov_read_extradata(c, pb, atom, AV_CODEC_ID_TARGA_Y216);

    if (!ret && c->fc->nb_streams >= 1) {
        AVCodecParameters *par = c->fc->streams[c->fc->nb_streams-1]->codecpar;
        if (par->extradata_size >= 40) {
            par->height = AV_RB16(&par->extradata[36]);
            par->width  = AV_RB16(&par->extradata[38]);
        }
    }
    return ret;
}

static int mov_read_ares(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    if (c->fc->nb_streams >= 1) {
        AVStream *const  st = c->fc->streams[c->fc->nb_streams - 1];
        FFStream *const sti = ffstream(st);
        AVCodecParameters *par = st->codecpar;

        if (par->codec_tag == MKTAG('A', 'V', 'i', 'n') &&
            par->codec_id == AV_CODEC_ID_H264 &&
            atom.size > 11) {
            int cid;
            avio_skip(pb, 10);
            cid = avio_rb16(pb);
            /* For AVID AVCI50, force width of 1440 to be able to select the correct SPS and PPS */
            if (cid == 0xd4d || cid == 0xd4e)
                par->width = 1440;
            return 0;
        } else if ((par->codec_tag == MKTAG('A', 'V', 'd', '1') ||
                    par->codec_tag == MKTAG('A', 'V', 'j', '2') ||
                    par->codec_tag == MKTAG('A', 'V', 'd', 'n')) &&
                   atom.size >= 24) {
            int num, den;
            avio_skip(pb, 12);
            num = avio_rb32(pb);
            den = avio_rb32(pb);
            if (num <= 0 || den <= 0)
                return 0;
            switch (avio_rb32(pb)) {
            case 2:
                if (den >= INT_MAX / 2)
                    return 0;
                den *= 2;
            case 1:
                sti->display_aspect_ratio = (AVRational){ num, den };
            default:
                return 0;
            }
        }
    }

    return mov_read_avid(c, pb, atom);
}

static int mov_read_aclr(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret = 0;
    int length = 0;
    uint64_t original_size;
    if (c->fc->nb_streams >= 1) {
        AVCodecParameters *par = c->fc->streams[c->fc->nb_streams-1]->codecpar;
        if (par->codec_id == AV_CODEC_ID_H264)
            return 0;
        if (atom.size == 16) {
            original_size = par->extradata_size;
            ret = mov_realloc_extradata(par, atom);
            if (!ret) {
                length =  mov_read_atom_into_extradata(c, pb, atom, par, par->extradata + original_size);
                if (length == atom.size) {
                    const uint8_t range_value = par->extradata[original_size + 19];
                    switch (range_value) {
                    case 1:
                        par->color_range = AVCOL_RANGE_MPEG;
                        break;
                    case 2:
                        par->color_range = AVCOL_RANGE_JPEG;
                        break;
                    default:
                        av_log(c->fc, AV_LOG_WARNING, "ignored unknown aclr value (%d)\n", range_value);
                        break;
                    }
                    ff_dlog(c->fc, "color_range: %d\n", par->color_range);
                } else {
                  /* For some reason the whole atom was not added to the extradata */
                  av_log(c->fc, AV_LOG_ERROR, "aclr not decoded - incomplete atom\n");
                }
            } else {
                av_log(c->fc, AV_LOG_ERROR, "aclr not decoded - unable to add atom to extradata\n");
            }
        } else {
            av_log(c->fc, AV_LOG_WARNING, "aclr not decoded - unexpected size %"PRId64"\n", atom.size);
        }
    }

    return ret;
}

static int mov_read_svq3(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_SVQ3);
}

static int mov_read_wave(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((uint64_t)atom.size > (1<<30))
        return AVERROR_INVALIDDATA;

    if (st->codecpar->codec_id == AV_CODEC_ID_QDM2 ||
        st->codecpar->codec_id == AV_CODEC_ID_QDMC ||
        st->codecpar->codec_id == AV_CODEC_ID_SPEEX) {
        // pass all frma atom to codec, needed at least for QDMC and QDM2
        ret = ff_get_extradata(c->fc, st->codecpar, pb, atom.size);
        if (ret < 0)
            return ret;
    } else if (atom.size > 8) { /* to read frma, esds atoms */
        if (st->codecpar->codec_id == AV_CODEC_ID_ALAC && atom.size >= 24) {
            uint64_t buffer;
            ret = ffio_ensure_seekback(pb, 8);
            if (ret < 0)
                return ret;
            buffer = avio_rb64(pb);
            atom.size -= 8;
            if (  (buffer & 0xFFFFFFFF) == MKBETAG('f','r','m','a')
                && buffer >> 32 <= atom.size
                && buffer >> 32 >= 8) {
                avio_skip(pb, -8);
                atom.size += 8;
            } else if (!st->codecpar->extradata_size) {
#define ALAC_EXTRADATA_SIZE 36
                st->codecpar->extradata = av_mallocz(ALAC_EXTRADATA_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!st->codecpar->extradata)
                    return AVERROR(ENOMEM);
                st->codecpar->extradata_size = ALAC_EXTRADATA_SIZE;
                AV_WB32(st->codecpar->extradata    , ALAC_EXTRADATA_SIZE);
                AV_WB32(st->codecpar->extradata + 4, MKTAG('a','l','a','c'));
                AV_WB64(st->codecpar->extradata + 12, buffer);
                avio_read(pb, st->codecpar->extradata + 20, 16);
                avio_skip(pb, atom.size - 24);
                return 0;
            }
        }
        if ((ret = mov_read_default(c, pb, atom)) < 0)
            return ret;
    } else
        avio_skip(pb, atom.size);
    return 0;
}

/**
 * This function reads atom content and puts data in extradata without tag
 * nor size unlike mov_read_extradata.
 */
static int mov_read_glbl(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int ret;

    st = get_curr_st(c);
    if (!st)
        return 0;

    if ((uint64_t)atom.size > (1<<30))
        return AVERROR_INVALIDDATA;

    if (atom.type == MKTAG('v','v','c','C')) {
        avio_skip(pb, 4);
        atom.size -= 4;
    }

    if (atom.size >= 10) {
        // Broken files created by legacy versions of libavformat will
        // wrap a whole fiel atom inside of a glbl atom.
        unsigned size = avio_rb32(pb);
        unsigned type = avio_rl32(pb);
        if (avio_feof(pb))
            return AVERROR_INVALIDDATA;
        avio_seek(pb, -8, SEEK_CUR);
        if (type == MKTAG('f','i','e','l') && size == atom.size)
            return mov_read_default(c, pb, atom);
    }
    if (st->codecpar->extradata_size > 1 && st->codecpar->extradata) {
        av_log(c->fc, AV_LOG_WARNING, "ignoring multiple glbl\n");
        return 0;
    }
    ret = ff_get_extradata(c->fc, st->codecpar, pb, atom.size);
    if (ret < 0)
        return ret;
    if (atom.type == MKTAG('h','v','c','C') && st->codecpar->codec_tag == MKTAG('d','v','h','1'))
        /* HEVC-based Dolby Vision derived from hvc1.
           Happens to match with an identifier
           previously utilized for DV. Thus, if we have
           the hvcC extradata box available as specified,
           set codec to HEVC */
        st->codecpar->codec_id = AV_CODEC_ID_HEVC;

    return 0;
}

static int mov_read_dvc1(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint8_t profile_level;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (atom.size >= (1<<28) || atom.size < 7)
        return AVERROR_INVALIDDATA;

    profile_level = avio_r8(pb);
    if ((profile_level & 0xf0) != 0xc0)
        return 0;

    avio_seek(pb, 6, SEEK_CUR);
    ret = ff_get_extradata(c->fc, st->codecpar, pb, atom.size - 7);
    if (ret < 0)
        return ret;

    return 0;
}

static int mov_read_sbas(MOVContext* c, AVIOContext* pb, MOVAtom atom)
{
    AVStream* st;
    MOVStreamContext* sc;

    if (c->fc->nb_streams < 1)
        return 0;

    /* For SBAS this should be fine - though beware if someone implements a
     * tref atom processor that doesn't drop down to default then this may
     * be lost. */
    if (atom.size > 4) {
        av_log(c->fc, AV_LOG_ERROR, "Only a single tref of type sbas is supported\n");
        return AVERROR_PATCHWELCOME;
    }

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;
    sc->tref_id = avio_rb32(pb);
    sc->tref_flags |= MOV_TREF_FLAG_ENHANCEMENT;

    return 0;
}

/**
 * An strf atom is a BITMAPINFOHEADER struct. This struct is 40 bytes itself,
 * but can have extradata appended at the end after the 40 bytes belonging
 * to the struct.
 */
static int mov_read_strf(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    if (atom.size <= 40)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((uint64_t)atom.size > (1<<30))
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 40);
    ret = ff_get_extradata(c->fc, st->codecpar, pb, atom.size - 40);
    if (ret < 0)
        return ret;

    return 0;
}

static int mov_read_stco(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STCO outside TRAK\n");
        return 0;
    }
    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    // Clamp allocation size for `chunk_offsets` -- don't throw an error for an
    // invalid count since the EOF path doesn't throw either.
    entries = avio_rb32(pb);
    entries =
        FFMIN(entries,
              FFMAX(0, (atom.size - 8) /
                           (atom.type == MKTAG('s', 't', 'c', 'o') ? 4 : 8)));

    if (!entries)
        return 0;

    if (sc->chunk_offsets) {
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicated STCO atom\n");
        return 0;
    }

    av_free(sc->chunk_offsets);
    sc->chunk_count = 0;
    sc->chunk_offsets = av_malloc_array(entries, sizeof(*sc->chunk_offsets));
    if (!sc->chunk_offsets)
        return AVERROR(ENOMEM);
    sc->chunk_count = entries;

    if      (atom.type == MKTAG('s','t','c','o'))
        for (i = 0; i < entries && !pb->eof_reached; i++)
            sc->chunk_offsets[i] = avio_rb32(pb);
    else if (atom.type == MKTAG('c','o','6','4'))
        for (i = 0; i < entries && !pb->eof_reached; i++) {
            sc->chunk_offsets[i] = avio_rb64(pb);
            if (sc->chunk_offsets[i] < 0) {
                av_log(c->fc, AV_LOG_WARNING, "Impossible chunk_offset\n");
                sc->chunk_offsets[i] = 0;
            }
        }
    else
        return AVERROR_INVALIDDATA;

    sc->chunk_count = i;

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STCO atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static int mov_codec_id(AVStream *st, uint32_t format)
{
    int id = ff_codec_get_id(ff_codec_movaudio_tags, format);

    if (id <= 0 &&
        ((format & 0xFFFF) == 'm' + ('s' << 8) ||
         (format & 0xFFFF) == 'T' + ('S' << 8)))
        id = ff_codec_get_id(ff_codec_wav_tags, av_bswap32(format) & 0xFFFF);

    if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO && id > 0) {
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    } else if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
               /* skip old ASF MPEG-4 tag */
               format && format != MKTAG('m','p','4','s')) {
        id = ff_codec_get_id(ff_codec_movvideo_tags, format);
        if (id <= 0)
            id = ff_codec_get_id(ff_codec_bmp_tags, format);
        if (id > 0)
            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA ||
                    (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE &&
                    st->codecpar->codec_id == AV_CODEC_ID_NONE)) {
            id = ff_codec_get_id(ff_codec_movsubtitle_tags, format);
            if (id <= 0) {
                id = (format == MOV_MP4_TTML_TAG || format == MOV_ISMV_TTML_TAG) ?
                     AV_CODEC_ID_TTML : id;
            }

            if (id > 0)
                st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
            else
                id = ff_codec_get_id(ff_codec_movdata_tags, format);
        }
    }

    st->codecpar->codec_tag = format;

    return id;
}

static void mov_parse_stsd_video(MOVContext *c, AVIOContext *pb,
                                 AVStream *st, MOVStreamContext *sc)
{
    uint8_t codec_name[32] = { 0 };
    int64_t stsd_start;
    unsigned int len;
    uint32_t id = 0;

    /* The first 16 bytes of the video sample description are already
     * read in ff_mov_read_stsd_entries() */
    stsd_start = avio_tell(pb) - 16;

    avio_rb16(pb); /* version */
    avio_rb16(pb); /* revision level */
    id = avio_rl32(pb); /* vendor */
    av_dict_set(&st->metadata, "vendor_id", av_fourcc2str(id), 0);
    avio_rb32(pb); /* temporal quality */
    avio_rb32(pb); /* spatial quality */

    st->codecpar->width  = avio_rb16(pb); /* width */
    st->codecpar->height = avio_rb16(pb); /* height */

    avio_rb32(pb); /* horiz resolution */
    avio_rb32(pb); /* vert resolution */
    avio_rb32(pb); /* data size, always 0 */
    avio_rb16(pb); /* frames per samples */

    len = avio_r8(pb); /* codec name, pascal string */
    if (len > 31)
        len = 31;
    mov_read_mac_string(c, pb, len, codec_name, sizeof(codec_name));
    if (len < 31)
        avio_skip(pb, 31 - len);

    if (codec_name[0])
        av_dict_set(&st->metadata, "encoder", codec_name, 0);

    /* codec_tag YV12 triggers an UV swap in rawdec.c */
    if (!strncmp(codec_name, "Planar Y'CbCr 8-bit 4:2:0", 25)) {
        st->codecpar->codec_tag = MKTAG('I', '4', '2', '0');
        st->codecpar->width &= ~1;
        st->codecpar->height &= ~1;
    }
    /* Flash Media Server uses tag H.263 with Sorenson Spark */
    if (st->codecpar->codec_tag == MKTAG('H','2','6','3') &&
        !strncmp(codec_name, "Sorenson H263", 13))
        st->codecpar->codec_id = AV_CODEC_ID_FLV1;

    st->codecpar->bits_per_coded_sample = avio_rb16(pb); /* depth */

    avio_seek(pb, stsd_start, SEEK_SET);

    if (ff_get_qtpalette(st->codecpar->codec_id, pb, sc->palette)) {
        st->codecpar->bits_per_coded_sample &= 0x1F;
        sc->has_palette = 1;
    }
}

static void mov_parse_stsd_audio(MOVContext *c, AVIOContext *pb,
                                 AVStream *st, MOVStreamContext *sc)
{
    int bits_per_sample, flags;
    uint16_t version = avio_rb16(pb);
    uint32_t id = 0;
    AVDictionaryEntry *compatible_brands = av_dict_get(c->fc->metadata, "compatible_brands", NULL, AV_DICT_MATCH_CASE);
    int channel_count;

    avio_rb16(pb); /* revision level */
    id = avio_rl32(pb); /* vendor */
    av_dict_set(&st->metadata, "vendor_id", av_fourcc2str(id), 0);

    channel_count = avio_rb16(pb);

    st->codecpar->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
    st->codecpar->ch_layout.nb_channels = channel_count;
    st->codecpar->bits_per_coded_sample = avio_rb16(pb); /* sample size */
    av_log(c->fc, AV_LOG_TRACE, "audio channels %d\n", channel_count);

    sc->audio_cid = avio_rb16(pb);
    avio_rb16(pb); /* packet size = 0 */

    st->codecpar->sample_rate = ((avio_rb32(pb) >> 16));

    // Read QT version 1 fields. In version 0 these do not exist.
    av_log(c->fc, AV_LOG_TRACE, "version =%d, isom =%d\n", version, c->isom);
    if (!c->isom ||
        (compatible_brands && strstr(compatible_brands->value, "qt  ")) ||
        (sc->stsd_version == 0 && version > 0)) {
        if (version == 1) {
            sc->samples_per_frame = avio_rb32(pb);
            avio_rb32(pb); /* bytes per packet */
            sc->bytes_per_frame = avio_rb32(pb);
            avio_rb32(pb); /* bytes per sample */
        } else if (version == 2) {
            avio_rb32(pb); /* sizeof struct only */
            st->codecpar->sample_rate = av_int2double(avio_rb64(pb));
            channel_count = avio_rb32(pb);
            st->codecpar->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
            st->codecpar->ch_layout.nb_channels = channel_count;
            avio_rb32(pb); /* always 0x7F000000 */
            st->codecpar->bits_per_coded_sample = avio_rb32(pb);

            flags = avio_rb32(pb); /* lpcm format specific flag */
            sc->bytes_per_frame   = avio_rb32(pb);
            sc->samples_per_frame = avio_rb32(pb);
            if (st->codecpar->codec_tag == MKTAG('l','p','c','m'))
                st->codecpar->codec_id =
                    ff_mov_get_lpcm_codec_id(st->codecpar->bits_per_coded_sample,
                                             flags);
        }
        if (version == 0 || (version == 1 && sc->audio_cid != -2)) {
            /* can't correctly handle variable sized packet as audio unit */
            switch (st->codecpar->codec_id) {
            case AV_CODEC_ID_MP2:
            case AV_CODEC_ID_MP3:
                ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
                break;
            }
        }
    }

    if (sc->format == 0) {
        if (st->codecpar->bits_per_coded_sample == 8)
            st->codecpar->codec_id = mov_codec_id(st, MKTAG('r','a','w',' '));
        else if (st->codecpar->bits_per_coded_sample == 16)
            st->codecpar->codec_id = mov_codec_id(st, MKTAG('t','w','o','s'));
    }

    switch (st->codecpar->codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
        if (st->codecpar->bits_per_coded_sample == 16)
            st->codecpar->codec_id = AV_CODEC_ID_PCM_S16BE;
        break;
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
        if (st->codecpar->bits_per_coded_sample == 8)
            st->codecpar->codec_id = AV_CODEC_ID_PCM_S8;
        else if (st->codecpar->bits_per_coded_sample == 24)
            st->codecpar->codec_id =
                st->codecpar->codec_id == AV_CODEC_ID_PCM_S16BE ?
                AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
        else if (st->codecpar->bits_per_coded_sample == 32)
             st->codecpar->codec_id =
                st->codecpar->codec_id == AV_CODEC_ID_PCM_S16BE ?
                AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
        break;
    /* set values for old format before stsd version 1 appeared */
    case AV_CODEC_ID_MACE3:
        sc->samples_per_frame = 6;
        sc->bytes_per_frame   = 2 * st->codecpar->ch_layout.nb_channels;
        break;
    case AV_CODEC_ID_MACE6:
        sc->samples_per_frame = 6;
        sc->bytes_per_frame   = 1 * st->codecpar->ch_layout.nb_channels;
        break;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        sc->samples_per_frame = 64;
        sc->bytes_per_frame   = 34 * st->codecpar->ch_layout.nb_channels;
        break;
    case AV_CODEC_ID_GSM:
        sc->samples_per_frame = 160;
        sc->bytes_per_frame   = 33;
        break;
    default:
        break;
    }

    bits_per_sample = av_get_bits_per_sample(st->codecpar->codec_id);
    if (bits_per_sample && (bits_per_sample >> 3) * (uint64_t)st->codecpar->ch_layout.nb_channels <= INT_MAX) {
        st->codecpar->bits_per_coded_sample = bits_per_sample;
        sc->sample_size = (bits_per_sample >> 3) * st->codecpar->ch_layout.nb_channels;
    }
}

static void mov_parse_stsd_subtitle(MOVContext *c, AVIOContext *pb,
                                    AVStream *st, MOVStreamContext *sc,
                                    int64_t size)
{
    // ttxt stsd contains display flags, justification, background
    // color, fonts, and default styles, so fake an atom to read it
    MOVAtom fake_atom = { .size = size };
    // mp4s contains a regular esds atom, dfxp ISMV TTML has no content
    // in extradata unlike stpp MP4 TTML.
    if (st->codecpar->codec_tag != AV_RL32("mp4s") &&
        st->codecpar->codec_tag != MOV_ISMV_TTML_TAG)
        mov_read_glbl(c, pb, fake_atom);
    st->codecpar->width  = sc->width;
    st->codecpar->height = sc->height;
}

static int mov_parse_stsd_data(MOVContext *c, AVIOContext *pb,
                                AVStream *st, MOVStreamContext *sc,
                                int64_t size)
{
    int ret;

    if (st->codecpar->codec_tag == MKTAG('t','m','c','d')) {
        if ((int)size != size)
            return AVERROR(ENOMEM);

        ret = ff_get_extradata(c->fc, st->codecpar, pb, size);
        if (ret < 0)
            return ret;
        if (size > 16) {
            MOVStreamContext *tmcd_ctx = st->priv_data;
            int val;
            val = AV_RB32(st->codecpar->extradata + 4);
            tmcd_ctx->tmcd_flags = val;
            st->avg_frame_rate.num = AV_RB32(st->codecpar->extradata + 8); /* timescale */
            st->avg_frame_rate.den = AV_RB32(st->codecpar->extradata + 12); /* frameDuration */
            tmcd_ctx->tmcd_nb_frames = st->codecpar->extradata[16]; /* number of frames */
            if (size > 30) {
                uint32_t len = AV_RB32(st->codecpar->extradata + 18); /* name atom length */
                uint32_t format = AV_RB32(st->codecpar->extradata + 22);
                if (format == AV_RB32("name") && (int64_t)size >= (int64_t)len + 18) {
                    uint16_t str_size = AV_RB16(st->codecpar->extradata + 26); /* string length */
                    if (str_size > 0 && size >= (int)str_size + 30 &&
                        st->codecpar->extradata[30] /* Don't add empty string */) {
                        char *reel_name = av_malloc(str_size + 1);
                        if (!reel_name)
                            return AVERROR(ENOMEM);
                        memcpy(reel_name, st->codecpar->extradata + 30, str_size);
                        reel_name[str_size] = 0; /* Add null terminator */
                        av_dict_set(&st->metadata, "reel_name", reel_name,
                                    AV_DICT_DONT_STRDUP_VAL);
                    }
                }
            }
        }
    } else {
        /* other codec type, just skip (rtp, mp4s ...) */
        avio_skip(pb, size);
    }
    return 0;
}

static int mov_finalize_stsd_codec(MOVContext *c, AVIOContext *pb,
                                   AVStream *st, MOVStreamContext *sc)
{
    FFStream *const sti = ffstream(st);

    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
        !st->codecpar->sample_rate && sc->time_scale > 1)
        st->codecpar->sample_rate = sc->time_scale;

    /* special codec parameters handling */
    switch (st->codecpar->codec_id) {
#if CONFIG_DV_DEMUXER
    case AV_CODEC_ID_DVAUDIO:
        if (c->dv_fctx) {
            avpriv_request_sample(c->fc, "multiple DV audio streams");
            return AVERROR(ENOSYS);
        }

        c->dv_fctx = avformat_alloc_context();
        if (!c->dv_fctx) {
            av_log(c->fc, AV_LOG_ERROR, "dv demux context alloc error\n");
            return AVERROR(ENOMEM);
        }
        c->dv_demux = avpriv_dv_init_demux(c->dv_fctx);
        if (!c->dv_demux) {
            av_log(c->fc, AV_LOG_ERROR, "dv demux context init error\n");
            return AVERROR(ENOMEM);
        }
        sc->dv_audio_container = 1;
        st->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        break;
#endif
    /* no ifdef since parameters are always those */
    case AV_CODEC_ID_QCELP:
        av_channel_layout_uninit(&st->codecpar->ch_layout);
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        // force sample rate for qcelp when not stored in mov
        if (st->codecpar->codec_tag != MKTAG('Q','c','l','p'))
            st->codecpar->sample_rate = 8000;
        // FIXME: Why is the following needed for some files?
        sc->samples_per_frame = 160;
        if (!sc->bytes_per_frame)
            sc->bytes_per_frame = 35;
        break;
    case AV_CODEC_ID_AMR_NB:
        av_channel_layout_uninit(&st->codecpar->ch_layout);
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        /* force sample rate for amr, stsd in 3gp does not store sample rate */
        st->codecpar->sample_rate = 8000;
        break;
    case AV_CODEC_ID_AMR_WB:
        av_channel_layout_uninit(&st->codecpar->ch_layout);
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
        st->codecpar->sample_rate = 16000;
        break;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        /* force type after stsd for m1a hdlr */
        st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        break;
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_ADPCM_MS:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ILBC:
    case AV_CODEC_ID_MACE3:
    case AV_CODEC_ID_MACE6:
    case AV_CODEC_ID_QDM2:
        st->codecpar->block_align = sc->bytes_per_frame;
        break;
    case AV_CODEC_ID_ALAC:
        if (st->codecpar->extradata_size == 36) {
            int channel_count = AV_RB8(st->codecpar->extradata + 21);
            if (st->codecpar->ch_layout.nb_channels != channel_count) {
                av_channel_layout_uninit(&st->codecpar->ch_layout);
                st->codecpar->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;
                st->codecpar->ch_layout.nb_channels = channel_count;
            }
            st->codecpar->sample_rate = AV_RB32(st->codecpar->extradata + 32);
        }
        break;
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_VC1:
    case AV_CODEC_ID_VP8:
    case AV_CODEC_ID_VP9:
        sti->need_parsing = AVSTREAM_PARSE_FULL;
        break;
    case AV_CODEC_ID_EVC:
    case AV_CODEC_ID_AV1:
        /* field_order detection of H264 requires parsing */
    case AV_CODEC_ID_H264:
        sti->need_parsing = AVSTREAM_PARSE_HEADERS;
        break;
    default:
        break;
    }
    return 0;
}

static int mov_skip_multiple_stsd(MOVContext *c, AVIOContext *pb,
                                  int codec_tag, int format,
                                  int64_t size)
{
    if (codec_tag &&
         (codec_tag != format &&
          // AVID 1:1 samples with differing data format and codec tag exist
          (codec_tag != AV_RL32("AV1x") || format != AV_RL32("AVup")) &&
          // prores is allowed to have differing data format and codec tag
          codec_tag != AV_RL32("apcn") && codec_tag != AV_RL32("apch") &&
          // so is dv (sigh)
          codec_tag != AV_RL32("dvpp") && codec_tag != AV_RL32("dvcp") &&
          (c->fc->video_codec_id ? ff_codec_get_id(ff_codec_movvideo_tags, format) != c->fc->video_codec_id
                                 : codec_tag != MKTAG('j','p','e','g')))) {
        /* Multiple fourcc, we skip JPEG. This is not correct, we should
         * export it as a separate AVStream but this needs a few changes
         * in the MOV demuxer, patch welcome. */

        av_log(c->fc, AV_LOG_WARNING, "multiple fourcc not supported\n");
        avio_skip(pb, size);
        return 1;
    }

    return 0;
}

int ff_mov_read_stsd_entries(MOVContext *c, AVIOContext *pb, int entries)
{
    AVStream *st;
    MOVStreamContext *sc;
    int pseudo_stream_id;

    av_assert0 (c->fc->nb_streams >= 1);
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    for (pseudo_stream_id = 0;
         pseudo_stream_id < entries && !pb->eof_reached;
         pseudo_stream_id++) {
        //Parsing Sample description table
        enum AVCodecID id;
        int ret, dref_id = 1;
        MOVAtom a = { AV_RL32("stsd") };
        int64_t start_pos = avio_tell(pb);
        int64_t size    = avio_rb32(pb); /* size */
        uint32_t format = avio_rl32(pb); /* data format */

        if (size >= 16) {
            avio_rb32(pb); /* reserved */
            avio_rb16(pb); /* reserved */
            dref_id = avio_rb16(pb);
        } else if (size <= 7) {
            av_log(c->fc, AV_LOG_ERROR,
                   "invalid size %"PRId64" in stsd\n", size);
            return AVERROR_INVALIDDATA;
        }

        if (mov_skip_multiple_stsd(c, pb, st->codecpar->codec_tag, format,
                                   size - (avio_tell(pb) - start_pos))) {
            sc->stsd_count++;
            continue;
        }

        sc->pseudo_stream_id = st->codecpar->codec_tag ? -1 : pseudo_stream_id;
        sc->dref_id= dref_id;
        sc->format = format;

        id = mov_codec_id(st, format);

        av_log(c->fc, AV_LOG_TRACE,
               "size=%"PRId64" 4CC=%s codec_type=%d\n", size,
               av_fourcc2str(format), st->codecpar->codec_type);

        st->codecpar->codec_id = id;
        if (st->codecpar->codec_type==AVMEDIA_TYPE_VIDEO) {
            mov_parse_stsd_video(c, pb, st, sc);
        } else if (st->codecpar->codec_type==AVMEDIA_TYPE_AUDIO) {
            mov_parse_stsd_audio(c, pb, st, sc);
            if (st->codecpar->sample_rate < 0) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid sample rate %d\n", st->codecpar->sample_rate);
                return AVERROR_INVALIDDATA;
            }
            if (st->codecpar->ch_layout.nb_channels < 0) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid channels %d\n", st->codecpar->ch_layout.nb_channels);
                return AVERROR_INVALIDDATA;
            }
        } else if (st->codecpar->codec_type==AVMEDIA_TYPE_SUBTITLE){
            mov_parse_stsd_subtitle(c, pb, st, sc,
                                    size - (avio_tell(pb) - start_pos));
        } else {
            ret = mov_parse_stsd_data(c, pb, st, sc,
                                      size - (avio_tell(pb) - start_pos));
            if (ret < 0)
                return ret;
        }
        /* this will read extra atoms at the end (wave, alac, damr, avcC, hvcC, SMI ...) */
        a.size = size - (avio_tell(pb) - start_pos);
        if (a.size > 8) {
            if ((ret = mov_read_default(c, pb, a)) < 0)
                return ret;
        } else if (a.size > 0)
            avio_skip(pb, a.size);

        if (sc->extradata && st->codecpar->extradata) {
            int extra_size = st->codecpar->extradata_size;

            /* Move the current stream extradata to the stream context one. */
            sc->extradata_size[pseudo_stream_id] = extra_size;
            sc->extradata[pseudo_stream_id] = st->codecpar->extradata;
            st->codecpar->extradata      = NULL;
            st->codecpar->extradata_size = 0;
        }
        sc->stsd_count++;
    }

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STSD atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static int mov_read_stsd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int ret, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    sc->stsd_version = avio_r8(pb);
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    /* Each entry contains a size (4 bytes) and format (4 bytes). */
    if (entries <= 0 || entries > atom.size / 8 || entries > 1024) {
        av_log(c->fc, AV_LOG_ERROR, "invalid STSD entries %d\n", entries);
        return AVERROR_INVALIDDATA;
    }

    if (sc->extradata) {
        av_log(c->fc, AV_LOG_ERROR,
               "Duplicate stsd found in this track.\n");
        return AVERROR_INVALIDDATA;
    }

    /* Prepare space for hosting multiple extradata. */
    sc->extradata = av_calloc(entries, sizeof(*sc->extradata));
    if (!sc->extradata)
        return AVERROR(ENOMEM);

    sc->extradata_size = av_calloc(entries, sizeof(*sc->extradata_size));
    if (!sc->extradata_size) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = ff_mov_read_stsd_entries(c, pb, entries);
    if (ret < 0)
        goto fail;

    /* Restore back the primary extradata. */
    av_freep(&st->codecpar->extradata);
    st->codecpar->extradata_size = sc->extradata_size[0];
    if (sc->extradata_size[0]) {
        st->codecpar->extradata = av_mallocz(sc->extradata_size[0] + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codecpar->extradata)
            return AVERROR(ENOMEM);
        memcpy(st->codecpar->extradata, sc->extradata[0], sc->extradata_size[0]);
    }

    return mov_finalize_stsd_codec(c, pb, st, sc);
fail:
    if (sc->extradata) {
        int j;
        for (j = 0; j < sc->stsd_count; j++)
            av_freep(&sc->extradata[j]);
    }

    av_freep(&sc->extradata);
    av_freep(&sc->extradata_size);
    return ret;
}

static int mov_read_stsc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STSC outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    entries = avio_rb32(pb);
    if ((uint64_t)entries * 12 + 4 > atom.size)
        return AVERROR_INVALIDDATA;

    av_log(c->fc, AV_LOG_TRACE, "track[%u].stsc.entries = %u\n", c->fc->nb_streams - 1, entries);

    if (!entries)
        return 0;
    if (sc->stsc_data) {
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicated STSC atom\n");
        return 0;
    }
    av_free(sc->stsc_data);
    sc->stsc_count = 0;
    sc->stsc_data = av_malloc_array(entries, sizeof(*sc->stsc_data));
    if (!sc->stsc_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        sc->stsc_data[i].first = avio_rb32(pb);
        sc->stsc_data[i].count = avio_rb32(pb);
        sc->stsc_data[i].id = avio_rb32(pb);
    }

    sc->stsc_count = i;
    for (i = sc->stsc_count - 1; i < UINT_MAX; i--) {
        int64_t first_min = i + 1;
        if ((i+1 < sc->stsc_count && sc->stsc_data[i].first >= sc->stsc_data[i+1].first) ||
            (i > 0 && sc->stsc_data[i].first <= sc->stsc_data[i-1].first) ||
            sc->stsc_data[i].first < first_min ||
            sc->stsc_data[i].count < 1 ||
            sc->stsc_data[i].id < 1) {
            av_log(c->fc, AV_LOG_WARNING, "STSC entry %d is invalid (first=%d count=%d id=%d)\n", i, sc->stsc_data[i].first, sc->stsc_data[i].count, sc->stsc_data[i].id);
            if (i+1 >= sc->stsc_count) {
                if (sc->stsc_data[i].count == 0 && i > 0) {
                    sc->stsc_count --;
                    continue;
                }
                sc->stsc_data[i].first = FFMAX(sc->stsc_data[i].first, first_min);
                if (i > 0 && sc->stsc_data[i].first <= sc->stsc_data[i-1].first)
                    sc->stsc_data[i].first = FFMIN(sc->stsc_data[i-1].first + 1LL, INT_MAX);
                sc->stsc_data[i].count = FFMAX(sc->stsc_data[i].count, 1);
                sc->stsc_data[i].id    = FFMAX(sc->stsc_data[i].id, 1);
                continue;
            }
            av_assert0(sc->stsc_data[i+1].first >= 2);
            // We replace this entry by the next valid
            sc->stsc_data[i].first = sc->stsc_data[i+1].first - 1;
            sc->stsc_data[i].count = sc->stsc_data[i+1].count;
            sc->stsc_data[i].id    = sc->stsc_data[i+1].id;
        }
    }

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STSC atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static inline int mov_stsc_index_valid(unsigned int index, unsigned int count)
{
    return index < count - 1;
}

/* Compute the samples value for the stsc entry at the given index. */
static inline int64_t mov_get_stsc_samples(MOVStreamContext *sc, unsigned int index)
{
    int chunk_count;

    if (mov_stsc_index_valid(index, sc->stsc_count))
        chunk_count = sc->stsc_data[index + 1].first - sc->stsc_data[index].first;
    else {
        // Validation for stsc / stco  happens earlier in mov_read_stsc + mov_read_trak.
        av_assert0(sc->stsc_data[index].first <= sc->chunk_count);
        chunk_count = sc->chunk_count - (sc->stsc_data[index].first - 1);
    }

    return sc->stsc_data[index].count * (int64_t)chunk_count;
}

static int mov_read_stps(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned i, entries;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STPS outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_rb32(pb); // version + flags

    entries = avio_rb32(pb);
    if (sc->stps_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STPS atom\n");
    av_free(sc->stps_data);
    sc->stps_count = 0;
    sc->stps_data = av_malloc_array(entries, sizeof(*sc->stps_data));
    if (!sc->stps_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        sc->stps_data[i] = avio_rb32(pb);
    }

    sc->stps_count = i;

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STPS atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static int mov_read_stss(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    FFStream *sti;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STSS outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sti = ffstream(st);
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "keyframe_count = %u\n", entries);

    if (!entries) {
        sc->keyframe_absent = 1;
        if (!sti->need_parsing && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            sti->need_parsing = AVSTREAM_PARSE_HEADERS;
        return 0;
    }
    if (sc->keyframes)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STSS atom\n");
    if (entries >= UINT_MAX / sizeof(int))
        return AVERROR_INVALIDDATA;
    av_freep(&sc->keyframes);
    sc->keyframe_count = 0;
    sc->keyframes = av_malloc_array(entries, sizeof(*sc->keyframes));
    if (!sc->keyframes)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        sc->keyframes[i] = avio_rb32(pb);
    }

    sc->keyframe_count = i;

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STSS atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static int mov_read_stsz(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries, sample_size, field_size, num_bytes;
    GetBitContext gb;
    unsigned char* buf;
    int ret;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STSZ outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    if (atom.type == MKTAG('s','t','s','z')) {
        sample_size = avio_rb32(pb);
        if (!sc->sample_size) /* do not overwrite value computed in stsd */
            sc->sample_size = sample_size;
        sc->stsz_sample_size = sample_size;
        field_size = 32;
    } else {
        sample_size = 0;
        avio_rb24(pb); /* reserved */
        field_size = avio_r8(pb);
    }
    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "sample_size = %u sample_count = %u\n", sc->sample_size, entries);

    sc->sample_count = entries;
    if (sample_size)
        return 0;

    if (field_size != 4 && field_size != 8 && field_size != 16 && field_size != 32) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid sample field size %u\n", field_size);
        return AVERROR_INVALIDDATA;
    }

    if (!entries)
        return 0;
    if (entries >= (INT_MAX - 4 - 8 * AV_INPUT_BUFFER_PADDING_SIZE) / field_size)
        return AVERROR_INVALIDDATA;
    if (sc->sample_sizes)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STSZ atom\n");
    av_free(sc->sample_sizes);
    sc->sample_count = 0;
    sc->sample_sizes = av_malloc_array(entries, sizeof(*sc->sample_sizes));
    if (!sc->sample_sizes)
        return AVERROR(ENOMEM);

    num_bytes = (entries*field_size+4)>>3;

    buf = av_malloc(num_bytes+AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf) {
        av_freep(&sc->sample_sizes);
        return AVERROR(ENOMEM);
    }

    ret = ffio_read_size(pb, buf, num_bytes);
    if (ret < 0) {
        av_freep(&sc->sample_sizes);
        av_free(buf);
        av_log(c->fc, AV_LOG_WARNING, "STSZ atom truncated\n");
        return 0;
    }

    init_get_bits(&gb, buf, 8*num_bytes);

    for (i = 0; i < entries; i++) {
        sc->sample_sizes[i] = get_bits_long(&gb, field_size);
        if (sc->sample_sizes[i] > INT64_MAX - sc->data_size) {
            av_free(buf);
            av_log(c->fc, AV_LOG_ERROR, "Sample size overflow in STSZ\n");
            return AVERROR_INVALIDDATA;
        }
        sc->data_size += sc->sample_sizes[i];
    }

    sc->sample_count = i;

    av_free(buf);

    return 0;
}

static int mov_read_stts(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;
    int64_t duration = 0;
    int64_t total_sample_count = 0;
    int64_t current_dts = 0;
    int64_t corrected_dts = 0;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "STTS outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "track[%u].stts.entries = %u\n",
            c->fc->nb_streams-1, entries);

    if (sc->stts_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STTS atom\n");
    av_freep(&sc->stts_data);
    sc->stts_count = 0;
    if (entries >= INT_MAX / sizeof(*sc->stts_data))
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        unsigned int sample_duration;
        unsigned int sample_count;
        unsigned int min_entries = FFMIN(FFMAX(i + 1, 1024 * 1024), entries);
        MOVStts *stts_data = av_fast_realloc(sc->stts_data, &sc->stts_allocated_size,
                                             min_entries * sizeof(*sc->stts_data));
        if (!stts_data) {
            av_freep(&sc->stts_data);
            sc->stts_count = 0;
            return AVERROR(ENOMEM);
        }
        sc->stts_count = min_entries;
        sc->stts_data = stts_data;

        sample_count    = avio_rb32(pb);
        sample_duration = avio_rb32(pb);

        sc->stts_data[i].count= sample_count;
        sc->stts_data[i].duration= sample_duration;

        av_log(c->fc, AV_LOG_TRACE, "sample_count=%u, sample_duration=%u\n",
                sample_count, sample_duration);

        /* STTS sample offsets are uint32 but some files store it as int32
         * with negative values used to correct DTS delays.
           There may be abnormally large values as well. */
        if (sample_duration > c->max_stts_delta) {
            // assume high delta is a correction if negative when cast as int32
            int32_t delta_magnitude = (int32_t)sample_duration;
            av_log(c->fc, AV_LOG_WARNING, "Too large sample offset %u in stts entry %u with count %u in st:%d. Clipping to 1.\n",
                   sample_duration, i, sample_count, st->index);
            sc->stts_data[i].duration = 1;
            corrected_dts = av_sat_add64(corrected_dts, (delta_magnitude < 0 ? (int64_t)delta_magnitude : 1) * sample_count);
        } else {
            corrected_dts += sample_duration * (uint64_t)sample_count;
        }

        current_dts += sc->stts_data[i].duration * (uint64_t)sample_count;

        if (current_dts > corrected_dts) {
            int64_t drift = av_sat_sub64(current_dts, corrected_dts) / FFMAX(sample_count, 1);
            uint32_t correction = (sc->stts_data[i].duration > drift) ? drift : sc->stts_data[i].duration - 1;
            current_dts -= correction * (uint64_t)sample_count;
            sc->stts_data[i].duration -= correction;
        }

        duration+=(int64_t)sc->stts_data[i].duration*(uint64_t)sc->stts_data[i].count;
        total_sample_count+=sc->stts_data[i].count;
    }

    sc->stts_count = i;

    if (duration > 0 &&
        duration <= INT64_MAX - sc->duration_for_fps &&
        total_sample_count <= INT_MAX - sc->nb_frames_for_fps) {
        sc->duration_for_fps  += duration;
        sc->nb_frames_for_fps += total_sample_count;
    }

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted STTS atom\n");
        return AVERROR_EOF;
    }

    st->nb_frames= total_sample_count;
    if (duration)
        st->duration= FFMIN(st->duration, duration);

    // All samples have zero duration. They have higher chance be chose by
    // mov_find_next_sample, which leads to seek again and again.
    //
    // It's AVERROR_INVALIDDATA actually, but such files exist in the wild.
    // So only mark data stream as discarded for safety.
    if (!duration && sc->stts_count &&
            st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
        av_log(c->fc, AV_LOG_WARNING,
               "All samples in data stream index:id [%d:%d] have zero "
               "duration, stream set to be discarded by default. Override "
               "using AVStream->discard or -discard for ffmpeg command.\n",
               st->index, sc->id);
        st->discard = AVDISCARD_ALL;
    }
    sc->track_end = duration;
    return 0;
}

static int mov_read_sdtp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int64_t i, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = atom.size - 4;

    av_log(c->fc, AV_LOG_TRACE, "track[%u].sdtp.entries = %" PRId64 "\n",
           c->fc->nb_streams - 1, entries);

    if (sc->sdtp_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated SDTP atom\n");
    av_freep(&sc->sdtp_data);
    sc->sdtp_count = 0;

    sc->sdtp_data = av_malloc(entries);
    if (!sc->sdtp_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++)
        sc->sdtp_data[i] = avio_r8(pb);
    sc->sdtp_count = i;

    return 0;
}

static void mov_update_dts_shift(MOVStreamContext *sc, int duration, void *logctx)
{
    if (duration < 0) {
        if (duration == INT_MIN) {
            av_log(logctx, AV_LOG_WARNING, "mov_update_dts_shift(): dts_shift set to %d\n", INT_MAX);
            duration++;
        }
        sc->dts_shift = FFMAX(sc->dts_shift, -duration);
    }
}

static int mov_read_ctts(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries, ctts_count = 0;

    if (c->trak_index < 0) {
        av_log(c->fc, AV_LOG_WARNING, "CTTS outside TRAK\n");
        return 0;
    }

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "track[%u].ctts.entries = %u\n", c->fc->nb_streams - 1, entries);

    if (!entries)
        return 0;
    if (entries >= UINT_MAX / sizeof(*sc->ctts_data))
        return AVERROR_INVALIDDATA;
    av_freep(&sc->ctts_data);
    sc->ctts_data = av_fast_realloc(NULL, &sc->ctts_allocated_size, entries * sizeof(*sc->ctts_data));
    if (!sc->ctts_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        MOVCtts *ctts_data;
        const size_t min_size_needed = (ctts_count + 1) * sizeof(MOVCtts);
        const size_t requested_size =
            min_size_needed > sc->ctts_allocated_size ?
            FFMAX(min_size_needed, 2 * sc->ctts_allocated_size) :
            min_size_needed;
        int count    = avio_rb32(pb);
        int duration = avio_rb32(pb);

        if (count <= 0) {
            av_log(c->fc, AV_LOG_TRACE,
                   "ignoring CTTS entry with count=%d duration=%d\n",
                   count, duration);
            continue;
        }

        if (ctts_count >= UINT_MAX / sizeof(MOVCtts) - 1)
            return AVERROR(ENOMEM);

        ctts_data = av_fast_realloc(sc->ctts_data, &sc->ctts_allocated_size, requested_size);

        if (!ctts_data)
            return AVERROR(ENOMEM);

        sc->ctts_data = ctts_data;

        ctts_data[ctts_count].count = count;
        ctts_data[ctts_count].offset = duration;
        ctts_count++;

        av_log(c->fc, AV_LOG_TRACE, "count=%d, duration=%d\n",
                count, duration);

        if (i+2<entries)
            mov_update_dts_shift(sc, duration, c->fc);
    }

    sc->ctts_count = ctts_count;

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted CTTS atom\n");
        return AVERROR_EOF;
    }

    av_log(c->fc, AV_LOG_TRACE, "dts shift %d\n", sc->dts_shift);

    return 0;
}

static int mov_read_sgpd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    uint8_t version;
    uint32_t grouping_type;
    uint32_t default_length;
    av_unused uint32_t default_group_description_index;
    uint32_t entry_count;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    grouping_type = avio_rl32(pb);

    /*
     * This function only supports "sync" boxes, but the code is able to parse
     * other boxes (such as "tscl", "tsas" and "stsa")
     */
    if (grouping_type != MKTAG('s','y','n','c'))
        return 0;

    default_length = version >= 1 ? avio_rb32(pb) : 0;
    default_group_description_index = version >= 2 ? avio_rb32(pb) : 0;
    entry_count = avio_rb32(pb);

    av_freep(&sc->sgpd_sync);
    sc->sgpd_sync_count = entry_count;
    sc->sgpd_sync = av_calloc(entry_count, sizeof(*sc->sgpd_sync));
    if (!sc->sgpd_sync)
        return AVERROR(ENOMEM);

    for (uint32_t i = 0; i < entry_count && !pb->eof_reached; i++) {
        uint32_t description_length = default_length;
        if (version >= 1 && default_length == 0)
            description_length = avio_rb32(pb);
        if (grouping_type == MKTAG('s','y','n','c')) {
            const uint8_t nal_unit_type = avio_r8(pb) & 0x3f;
            sc->sgpd_sync[i] = nal_unit_type;
            description_length -= 1;
        }
        avio_skip(pb, description_length);
    }

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted SGPD atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

static int mov_read_sbgp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;
    uint8_t version;
    uint32_t grouping_type;
    MOVSbgp *table, **tablep;
    int *table_count;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    grouping_type = avio_rl32(pb);

    if (grouping_type == MKTAG('r','a','p',' ')) {
        tablep = &sc->rap_group;
        table_count = &sc->rap_group_count;
    } else if (grouping_type == MKTAG('s','y','n','c')) {
        tablep = &sc->sync_group;
        table_count = &sc->sync_group_count;
    } else {
        return 0;
    }

    if (version == 1)
        avio_rb32(pb); /* grouping_type_parameter */

    entries = avio_rb32(pb);
    if (!entries)
        return 0;
    if (*tablep)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated SBGP %s atom\n", av_fourcc2str(grouping_type));
    av_freep(tablep);
    table = av_malloc_array(entries, sizeof(*table));
    if (!table)
        return AVERROR(ENOMEM);
    *tablep = table;

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        table[i].count = avio_rb32(pb); /* sample_count */
        table[i].index = avio_rb32(pb); /* group_description_index */
    }

    *table_count = i;

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted SBGP atom\n");
        return AVERROR_EOF;
    }

    return 0;
}

/**
 * Get ith edit list entry (media time, duration).
 */
static int get_edit_list_entry(MOVContext *mov,
                               const MOVStreamContext *msc,
                               unsigned int edit_list_index,
                               int64_t *edit_list_media_time,
                               int64_t *edit_list_duration,
                               int64_t global_timescale)
{
    if (edit_list_index == msc->elst_count) {
        return 0;
    }
    *edit_list_media_time = msc->elst_data[edit_list_index].time;
    *edit_list_duration = msc->elst_data[edit_list_index].duration;

    /* duration is in global timescale units;convert to msc timescale */
    if (global_timescale == 0) {
      avpriv_request_sample(mov->fc, "Support for mvhd.timescale = 0 with editlists");
      return 0;
    }
    *edit_list_duration = av_rescale(*edit_list_duration, msc->time_scale,
                                     global_timescale);

    if (*edit_list_duration + (uint64_t)*edit_list_media_time > INT64_MAX)
        *edit_list_duration = 0;

    return 1;
}

/**
 * Find the closest previous frame to the timestamp_pts, in e_old index
 * entries. Searching for just any frame / just key frames can be controlled by
 * last argument 'flag'.
 * Note that if ctts_data is not NULL, we will always search for a key frame
 * irrespective of the value of 'flag'. If we don't find any keyframe, we will
 * return the first frame of the video.
 *
 * Here the timestamp_pts is considered to be a presentation timestamp and
 * the timestamp of index entries are considered to be decoding timestamps.
 *
 * Returns 0 if successful in finding a frame, else returns -1.
 * Places the found index corresponding output arg.
 *
 * If ctts_old is not NULL, then refines the searched entry by searching
 * backwards from the found timestamp, to find the frame with correct PTS.
 *
 * Places the found ctts_index and ctts_sample in corresponding output args.
 */
static int find_prev_closest_index(AVStream *st,
                                   AVIndexEntry *e_old,
                                   int nb_old,
                                   MOVTimeToSample *tts_data,
                                   int64_t tts_count,
                                   int64_t timestamp_pts,
                                   int flag,
                                   int64_t* index,
                                   int64_t* tts_index,
                                   int64_t* tts_sample)
{
    MOVStreamContext *msc = st->priv_data;
    FFStream *const sti = ffstream(st);
    AVIndexEntry *e_keep = sti->index_entries;
    int nb_keep = sti->nb_index_entries;
    int64_t i = 0;

    av_assert0(index);

    // If dts_shift > 0, then all the index timestamps will have to be offset by
    // at least dts_shift amount to obtain PTS.
    // Hence we decrement the searched timestamp_pts by dts_shift to find the closest index element.
    if (msc->dts_shift > 0) {
        timestamp_pts -= msc->dts_shift;
    }

    sti->index_entries    = e_old;
    sti->nb_index_entries = nb_old;
    *index = av_index_search_timestamp(st, timestamp_pts, flag | AVSEEK_FLAG_BACKWARD);

    // Keep going backwards in the index entries until the timestamp is the same.
    if (*index >= 0) {
        for (i = *index; i > 0 && e_old[i].timestamp == e_old[i - 1].timestamp;
             i--) {
            if ((flag & AVSEEK_FLAG_ANY) ||
                (e_old[i - 1].flags & AVINDEX_KEYFRAME)) {
                *index = i - 1;
            }
        }
    }

    // If we have CTTS then refine the search, by searching backwards over PTS
    // computed by adding corresponding CTTS durations to index timestamps.
    if (msc->ctts_count && *index >= 0) {
        av_assert0(tts_index);
        av_assert0(tts_sample);
        // Find out the ctts_index for the found frame.
        *tts_index = 0;
        *tts_sample = 0;
        for (int64_t index_tts_count = 0; index_tts_count < *index; index_tts_count++) {
            if (*tts_index < tts_count) {
                (*tts_sample)++;
                if (tts_data[*tts_index].count == *tts_sample) {
                    (*tts_index)++;
                    *tts_sample = 0;
                }
            }
        }

        while (*index >= 0 && (*tts_index) >= 0 && (*tts_index) < tts_count) {
            // Find a "key frame" with PTS <= timestamp_pts (So that we can decode B-frames correctly).
            // No need to add dts_shift to the timestamp here becase timestamp_pts has already been
            // compensated by dts_shift above.
            if ((e_old[*index].timestamp + tts_data[*tts_index].offset) <= timestamp_pts &&
                (e_old[*index].flags & AVINDEX_KEYFRAME)) {
                break;
            }

            (*index)--;
            if (*tts_sample == 0) {
                (*tts_index)--;
                if (*tts_index >= 0)
                  *tts_sample = tts_data[*tts_index].count - 1;
            } else {
                (*tts_sample)--;
            }
        }
    }

    /* restore AVStream state*/
    sti->index_entries    = e_keep;
    sti->nb_index_entries = nb_keep;
    return *index >= 0 ? 0 : -1;
}

/**
 * Add index entry with the given values, to the end of ffstream(st)->index_entries.
 * Returns the new size ffstream(st)->index_entries if successful, else returns -1.
 *
 * This function is similar to ff_add_index_entry in libavformat/utils.c
 * except that here we are always unconditionally adding an index entry to
 * the end, instead of searching the entries list and skipping the add if
 * there is an existing entry with the same timestamp.
 * This is needed because the mov_fix_index calls this func with the same
 * unincremented timestamp for successive discarded frames.
 */
static int64_t add_index_entry(AVStream *st, int64_t pos, int64_t timestamp,
                               int size, int distance, int flags)
{
    FFStream *const sti = ffstream(st);
    AVIndexEntry *entries, *ie;
    int64_t index = -1;
    const size_t min_size_needed = (sti->nb_index_entries + 1) * sizeof(AVIndexEntry);

    // Double the allocation each time, to lower memory fragmentation.
    // Another difference from ff_add_index_entry function.
    const size_t requested_size =
        min_size_needed > sti->index_entries_allocated_size ?
        FFMAX(min_size_needed, 2 * sti->index_entries_allocated_size) :
        min_size_needed;

    if (sti->nb_index_entries + 1U >= UINT_MAX / sizeof(AVIndexEntry))
        return -1;

    entries = av_fast_realloc(sti->index_entries,
                              &sti->index_entries_allocated_size,
                              requested_size);
    if (!entries)
        return -1;

    sti->index_entries = entries;

    index = sti->nb_index_entries++;
    ie= &entries[index];

    ie->pos = pos;
    ie->timestamp = timestamp;
    ie->min_distance= distance;
    ie->size= size;
    ie->flags = flags;
    return index;
}

/**
 * Rewrite timestamps of index entries in the range [end_index - frame_duration_buffer_size, end_index)
 * by subtracting end_ts successively by the amounts given in frame_duration_buffer.
 */
static void fix_index_entry_timestamps(AVStream* st, int end_index, int64_t end_ts,
                                       int64_t* frame_duration_buffer,
                                       int frame_duration_buffer_size) {
    FFStream *const sti = ffstream(st);
    int i = 0;
    av_assert0(end_index >= 0 && end_index <= sti->nb_index_entries);
    for (i = 0; i < frame_duration_buffer_size; i++) {
        end_ts -= frame_duration_buffer[frame_duration_buffer_size - 1 - i];
        sti->index_entries[end_index - 1 - i].timestamp = end_ts;
    }
}

static int add_tts_entry(MOVTimeToSample **tts_data, unsigned int *tts_count, unsigned int *allocated_size,
                         int count, int offset, unsigned int duration)
{
    MOVTimeToSample *tts_buf_new;
    const size_t min_size_needed = (*tts_count + 1) * sizeof(MOVTimeToSample);
    const size_t requested_size =
        min_size_needed > *allocated_size ?
        FFMAX(min_size_needed, 2 * (*allocated_size)) :
        min_size_needed;

    if ((unsigned)(*tts_count) >= UINT_MAX / sizeof(MOVTimeToSample) - 1)
        return -1;

    tts_buf_new = av_fast_realloc(*tts_data, allocated_size, requested_size);

    if (!tts_buf_new)
        return -1;

    *tts_data = tts_buf_new;

    tts_buf_new[*tts_count].count = count;
    tts_buf_new[*tts_count].offset = offset;
    tts_buf_new[*tts_count].duration = duration;

    *tts_count = (*tts_count) + 1;
    return 0;
}

#define MAX_REORDER_DELAY 16
static void mov_estimate_video_delay(MOVContext *c, AVStream* st)
{
    MOVStreamContext *msc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int ctts_ind = 0;
    int ctts_sample = 0;
    int64_t pts_buf[MAX_REORDER_DELAY + 1]; // Circular buffer to sort pts.
    int buf_start = 0;
    int j, r, num_swaps;

    for (j = 0; j < MAX_REORDER_DELAY + 1; j++)
        pts_buf[j] = INT64_MIN;

    if (st->codecpar->video_delay <= 0 && msc->ctts_count &&
        st->codecpar->codec_id == AV_CODEC_ID_H264) {
        st->codecpar->video_delay = 0;
        for (int ind = 0; ind < sti->nb_index_entries && ctts_ind < msc->tts_count; ++ind) {
            // Point j to the last elem of the buffer and insert the current pts there.
            j = buf_start;
            buf_start = (buf_start + 1);
            if (buf_start == MAX_REORDER_DELAY + 1)
                buf_start = 0;

            pts_buf[j] = sti->index_entries[ind].timestamp + msc->tts_data[ctts_ind].offset;

            // The timestamps that are already in the sorted buffer, and are greater than the
            // current pts, are exactly the timestamps that need to be buffered to output PTS
            // in correct sorted order.
            // Hence the video delay (which is the buffer size used to sort DTS and output PTS),
            // can be computed as the maximum no. of swaps any particular timestamp needs to
            // go through, to keep this buffer in sorted order.
            num_swaps = 0;
            while (j != buf_start) {
                r = j - 1;
                if (r < 0) r = MAX_REORDER_DELAY;
                if (pts_buf[j] < pts_buf[r]) {
                    FFSWAP(int64_t, pts_buf[j], pts_buf[r]);
                    ++num_swaps;
                } else {
                    break;
                }
                j = r;
            }
            st->codecpar->video_delay = FFMAX(st->codecpar->video_delay, num_swaps);

            ctts_sample++;
            if (ctts_sample == msc->tts_data[ctts_ind].count) {
                ctts_ind++;
                ctts_sample = 0;
            }
        }
        av_log(c->fc, AV_LOG_DEBUG, "Setting codecpar->delay to %d for stream st: %d\n",
               st->codecpar->video_delay, st->index);
    }
}

static void mov_current_sample_inc(MOVStreamContext *sc)
{
    sc->current_sample++;
    sc->current_index++;
    if (sc->index_ranges &&
        sc->current_index >= sc->current_index_range->end &&
        sc->current_index_range->end) {
        sc->current_index_range++;
        sc->current_index = sc->current_index_range->start;
    }
}

static void mov_current_sample_dec(MOVStreamContext *sc)
{
    sc->current_sample--;
    sc->current_index--;
    if (sc->index_ranges &&
        sc->current_index < sc->current_index_range->start &&
        sc->current_index_range > sc->index_ranges) {
        sc->current_index_range--;
        sc->current_index = sc->current_index_range->end - 1;
    }
}

static void mov_current_sample_set(MOVStreamContext *sc, int current_sample)
{
    int64_t range_size;

    sc->current_sample = current_sample;
    sc->current_index = current_sample;
    if (!sc->index_ranges) {
        return;
    }

    for (sc->current_index_range = sc->index_ranges;
        sc->current_index_range->end;
        sc->current_index_range++) {
        range_size = sc->current_index_range->end - sc->current_index_range->start;
        if (range_size > current_sample) {
            sc->current_index = sc->current_index_range->start + current_sample;
            break;
        }
        current_sample -= range_size;
    }
}

/**
 * Fix ffstream(st)->index_entries, so that it contains only the entries (and the entries
 * which are needed to decode them) that fall in the edit list time ranges.
 * Also fixes the timestamps of the index entries to match the timeline
 * specified the edit lists.
 */
static void mov_fix_index(MOVContext *mov, AVStream *st)
{
    MOVStreamContext *msc = st->priv_data;
    FFStream *const sti = ffstream(st);
    AVIndexEntry *e_old = sti->index_entries;
    int nb_old = sti->nb_index_entries;
    const AVIndexEntry *e_old_end = e_old + nb_old;
    const AVIndexEntry *current = NULL;
    MOVTimeToSample *tts_data_old = msc->tts_data;
    int64_t tts_index_old = 0;
    int64_t tts_sample_old = 0;
    int64_t tts_count_old = msc->tts_count;
    int64_t edit_list_media_time = 0;
    int64_t edit_list_duration = 0;
    int64_t frame_duration = 0;
    int64_t edit_list_dts_counter = 0;
    int64_t edit_list_dts_entry_end = 0;
    int64_t edit_list_start_tts_sample = 0;
    int64_t curr_cts;
    int64_t curr_ctts = 0;
    int64_t empty_edits_sum_duration = 0;
    int64_t edit_list_index = 0;
    int64_t index;
    int flags;
    int64_t start_dts = 0;
    int64_t edit_list_start_encountered = 0;
    int64_t search_timestamp = 0;
    int64_t* frame_duration_buffer = NULL;
    int num_discarded_begin = 0;
    int first_non_zero_audio_edit = -1;
    int packet_skip_samples = 0;
    MOVIndexRange *current_index_range = NULL;
    int found_keyframe_after_edit = 0;
    int found_non_empty_edit = 0;

    if (!msc->elst_data || msc->elst_count <= 0 || nb_old <= 0) {
        return;
    }

    // allocate the index ranges array
    msc->index_ranges = av_malloc_array(msc->elst_count + 1,
                                        sizeof(msc->index_ranges[0]));
    if (!msc->index_ranges) {
        av_log(mov->fc, AV_LOG_ERROR, "Cannot allocate index ranges buffer\n");
        return;
    }
    msc->current_index_range = msc->index_ranges;

    // Clean AVStream from traces of old index
    sti->index_entries = NULL;
    sti->index_entries_allocated_size = 0;
    sti->nb_index_entries = 0;

    // Clean time to sample fields of MOVStreamContext
    msc->tts_data = NULL;
    msc->tts_count = 0;
    msc->tts_index = 0;
    msc->tts_sample = 0;
    msc->tts_allocated_size = 0;

    // Reinitialize min_corrected_pts so that it can be computed again.
    msc->min_corrected_pts = -1;

    // If the dts_shift is positive (in case of negative ctts values in mov),
    // then negate the DTS by dts_shift
    if (msc->dts_shift > 0) {
        edit_list_dts_entry_end -= msc->dts_shift;
        av_log(mov->fc, AV_LOG_DEBUG, "Shifting DTS by %d because of negative CTTS.\n", msc->dts_shift);
    }

    start_dts = edit_list_dts_entry_end;

    while (get_edit_list_entry(mov, msc, edit_list_index, &edit_list_media_time,
                               &edit_list_duration, mov->time_scale)) {
        av_log(mov->fc, AV_LOG_DEBUG, "Processing st: %d, edit list %"PRId64" - media time: %"PRId64", duration: %"PRId64"\n",
               st->index, edit_list_index, edit_list_media_time, edit_list_duration);
        edit_list_index++;
        edit_list_dts_counter = edit_list_dts_entry_end;
        edit_list_dts_entry_end += edit_list_duration;
        num_discarded_begin = 0;
        if (!found_non_empty_edit && edit_list_media_time == -1) {
            empty_edits_sum_duration += edit_list_duration;
            continue;
        }
        found_non_empty_edit = 1;

        // If we encounter a non-negative edit list reset the skip_samples/start_pad fields and set them
        // according to the edit list below.
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (first_non_zero_audio_edit < 0) {
                first_non_zero_audio_edit = 1;
            } else {
                first_non_zero_audio_edit = 0;
            }

            if (first_non_zero_audio_edit > 0)
                sti->skip_samples = msc->start_pad = 0;
        }

        // While reordering frame index according to edit list we must handle properly
        // the scenario when edit list entry starts from none key frame.
        // We find closest previous key frame and preserve it and consequent frames in index.
        // All frames which are outside edit list entry time boundaries will be dropped after decoding.
        search_timestamp = edit_list_media_time;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // Audio decoders like AAC need need a decoder delay samples previous to the current sample,
            // to correctly decode this frame. Hence for audio we seek to a frame 1 sec. before the
            // edit_list_media_time to cover the decoder delay.
            search_timestamp = FFMAX(search_timestamp - msc->time_scale, e_old[0].timestamp);
        }

        if (find_prev_closest_index(st, e_old, nb_old, tts_data_old, tts_count_old, search_timestamp, 0,
                                    &index, &tts_index_old, &tts_sample_old) < 0) {
            av_log(mov->fc, AV_LOG_WARNING,
                   "st: %d edit list: %"PRId64" Missing key frame while searching for timestamp: %"PRId64"\n",
                   st->index, edit_list_index, search_timestamp);
            if (find_prev_closest_index(st, e_old, nb_old, tts_data_old, tts_count_old, search_timestamp, AVSEEK_FLAG_ANY,
                                        &index, &tts_index_old, &tts_sample_old) < 0) {
                av_log(mov->fc, AV_LOG_WARNING,
                       "st: %d edit list %"PRId64" Cannot find an index entry before timestamp: %"PRId64".\n",
                       st->index, edit_list_index, search_timestamp);
                index = 0;
                tts_index_old = 0;
                tts_sample_old = 0;
            }
        }
        current = e_old + index;
        edit_list_start_tts_sample = tts_sample_old;

        // Iterate over index and arrange it according to edit list
        edit_list_start_encountered = 0;
        found_keyframe_after_edit = 0;
        for (; current < e_old_end; current++, index++) {
            // check  if frame outside edit list mark it for discard
            frame_duration = (current + 1 <  e_old_end) ?
                             ((current + 1)->timestamp - current->timestamp) : edit_list_duration;

            flags = current->flags;

            // frames (pts) before or after edit list
            curr_cts = current->timestamp + msc->dts_shift;
            curr_ctts = 0;

            if (tts_data_old && tts_index_old < tts_count_old) {
                curr_ctts = tts_data_old[tts_index_old].offset;
                av_log(mov->fc, AV_LOG_TRACE, "stts: %"PRId64" ctts: %"PRId64", tts_index: %"PRId64", tts_count: %"PRId64"\n",
                       curr_cts, curr_ctts, tts_index_old, tts_count_old);
                curr_cts += curr_ctts;
                tts_sample_old++;
                if (tts_sample_old == tts_data_old[tts_index_old].count) {
                    if (add_tts_entry(&msc->tts_data, &msc->tts_count,
                                       &msc->tts_allocated_size,
                                       tts_data_old[tts_index_old].count - edit_list_start_tts_sample,
                                       tts_data_old[tts_index_old].offset, tts_data_old[tts_index_old].duration) == -1) {
                        av_log(mov->fc, AV_LOG_ERROR, "Cannot add Time To Sample entry %"PRId64" - {%"PRId64", %d}\n",
                               tts_index_old,
                               tts_data_old[tts_index_old].count - edit_list_start_tts_sample,
                               tts_data_old[tts_index_old].offset);
                        break;
                    }
                    tts_index_old++;
                    tts_sample_old = 0;
                    edit_list_start_tts_sample = 0;
                }
            }

            if (curr_cts < edit_list_media_time || curr_cts >= (edit_list_duration + edit_list_media_time)) {
                if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && st->codecpar->codec_id != AV_CODEC_ID_VORBIS &&
                    curr_cts < edit_list_media_time && curr_cts + frame_duration > edit_list_media_time &&
                    first_non_zero_audio_edit > 0) {
                    packet_skip_samples = edit_list_media_time - curr_cts;
                    sti->skip_samples += packet_skip_samples;

                    // Shift the index entry timestamp by packet_skip_samples to be correct.
                    edit_list_dts_counter -= packet_skip_samples;
                    if (edit_list_start_encountered == 0)  {
                        edit_list_start_encountered = 1;
                        // Make timestamps strictly monotonically increasing for audio, by rewriting timestamps for
                        // discarded packets.
                        if (frame_duration_buffer) {
                            fix_index_entry_timestamps(st, sti->nb_index_entries, edit_list_dts_counter,
                                                       frame_duration_buffer, num_discarded_begin);
                            av_freep(&frame_duration_buffer);
                        }
                    }

                    av_log(mov->fc, AV_LOG_DEBUG, "skip %d audio samples from curr_cts: %"PRId64"\n", packet_skip_samples, curr_cts);
                } else {
                    flags |= AVINDEX_DISCARD_FRAME;
                    av_log(mov->fc, AV_LOG_DEBUG, "drop a frame at curr_cts: %"PRId64" @ %"PRId64"\n", curr_cts, index);

                    if (edit_list_start_encountered == 0) {
                        num_discarded_begin++;
                        frame_duration_buffer = av_realloc(frame_duration_buffer,
                                                           num_discarded_begin * sizeof(int64_t));
                        if (!frame_duration_buffer) {
                            av_log(mov->fc, AV_LOG_ERROR, "Cannot reallocate frame duration buffer\n");
                            break;
                        }
                        frame_duration_buffer[num_discarded_begin - 1] = frame_duration;

                        // Increment skip_samples for the first non-zero audio edit list
                        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                            first_non_zero_audio_edit > 0 && st->codecpar->codec_id != AV_CODEC_ID_VORBIS) {
                            sti->skip_samples += frame_duration;
                        }
                    }
                }
            } else {
                if (msc->min_corrected_pts < 0) {
                    msc->min_corrected_pts = edit_list_dts_counter + curr_ctts + msc->dts_shift;
                } else {
                    msc->min_corrected_pts = FFMIN(msc->min_corrected_pts, edit_list_dts_counter + curr_ctts + msc->dts_shift);
                }
                if (edit_list_start_encountered == 0) {
                    edit_list_start_encountered = 1;
                    // Make timestamps strictly monotonically increasing by rewriting timestamps for
                    // discarded packets.
                    if (frame_duration_buffer) {
                        fix_index_entry_timestamps(st, sti->nb_index_entries, edit_list_dts_counter,
                                                   frame_duration_buffer, num_discarded_begin);
                        av_freep(&frame_duration_buffer);
                    }
                }
            }

            if (add_index_entry(st, current->pos, edit_list_dts_counter, current->size,
                                current->min_distance, flags) == -1) {
                av_log(mov->fc, AV_LOG_ERROR, "Cannot add index entry\n");
                break;
            }

            // Update the index ranges array
            if (!current_index_range || index != current_index_range->end) {
                current_index_range = current_index_range ? current_index_range + 1
                                                          : msc->index_ranges;
                current_index_range->start = index;
            }
            current_index_range->end = index + 1;

            // Only start incrementing DTS in frame_duration amounts, when we encounter a frame in edit list.
            if (edit_list_start_encountered > 0) {
                edit_list_dts_counter = edit_list_dts_counter + frame_duration;
            }

            // Break when found first key frame after edit entry completion
            if ((curr_cts + frame_duration >= (edit_list_duration + edit_list_media_time)) &&
                ((flags & AVINDEX_KEYFRAME) || ((st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)))) {
                if (msc->ctts_count) {
                    // If we have CTTS and this is the first keyframe after edit elist,
                    // wait for one more, because there might be trailing B-frames after this I-frame
                    // that do belong to the edit.
                    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO && found_keyframe_after_edit == 0) {
                        found_keyframe_after_edit = 1;
                        continue;
                    }
                    if (tts_sample_old != 0) {
                        if (add_tts_entry(&msc->tts_data, &msc->tts_count,
                                           &msc->tts_allocated_size,
                                           tts_sample_old - edit_list_start_tts_sample,
                                           tts_data_old[tts_index_old].offset, tts_data_old[tts_index_old].duration) == -1) {
                            av_log(mov->fc, AV_LOG_ERROR, "Cannot add Time To Sample entry %"PRId64" - {%"PRId64", %d}\n",
                                   tts_index_old, tts_sample_old - edit_list_start_tts_sample,
                                   tts_data_old[tts_index_old].offset);
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    // If there are empty edits, then msc->min_corrected_pts might be positive
    // intentionally. So we subtract the sum duration of emtpy edits here.
    msc->min_corrected_pts -= empty_edits_sum_duration;

    // If the minimum pts turns out to be greater than zero after fixing the index, then we subtract the
    // dts by that amount to make the first pts zero.
    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (msc->min_corrected_pts > 0) {
            av_log(mov->fc, AV_LOG_DEBUG, "Offset DTS by %"PRId64" to make first pts zero.\n", msc->min_corrected_pts);
            for (int i = 0; i < sti->nb_index_entries; ++i)
                sti->index_entries[i].timestamp -= msc->min_corrected_pts;
        }
    }
    // Start time should be equal to zero or the duration of any empty edits.
    st->start_time = empty_edits_sum_duration;

    // Update av stream length, if it ends up shorter than the track's media duration
    st->duration = FFMIN(st->duration, edit_list_dts_entry_end - start_dts);
    msc->start_pad = sti->skip_samples;

    // Free the old index and the old CTTS structures
    av_free(e_old);
    av_free(tts_data_old);
    av_freep(&frame_duration_buffer);

    // Null terminate the index ranges array
    current_index_range = current_index_range ? current_index_range + 1
                                              : msc->index_ranges;
    current_index_range->start = 0;
    current_index_range->end = 0;
    msc->current_index = msc->index_ranges[0].start;
}

static uint32_t get_sgpd_sync_index(const MOVStreamContext *sc, int nal_unit_type)
{
    for (uint32_t i = 0; i < sc->sgpd_sync_count; i++)
        if (sc->sgpd_sync[i] == nal_unit_type)
            return i + 1;
    return 0;
}

static int build_open_gop_key_points(AVStream *st)
{
    int k;
    int sample_id = 0;
    uint32_t cra_index;
    MOVStreamContext *sc = st->priv_data;

    if (st->codecpar->codec_id != AV_CODEC_ID_HEVC || !sc->sync_group_count)
        return 0;

    /* Build an unrolled index of the samples */
    sc->sample_offsets_count = 0;
    for (uint32_t i = 0; i < sc->ctts_count; i++) {
        if (sc->ctts_data[i].count > INT_MAX - sc->sample_offsets_count)
            return AVERROR(ENOMEM);
        sc->sample_offsets_count += sc->ctts_data[i].count;
    }
    av_freep(&sc->sample_offsets);
    sc->sample_offsets = av_calloc(sc->sample_offsets_count, sizeof(*sc->sample_offsets));
    if (!sc->sample_offsets)
        return AVERROR(ENOMEM);
    k = 0;
    for (uint32_t i = 0; i < sc->ctts_count; i++)
        for (int j = 0; j < sc->ctts_data[i].count; j++)
             sc->sample_offsets[k++] = sc->ctts_data[i].offset;

    /* The following HEVC NAL type reveal the use of open GOP sync points
     * (TODO: BLA types may also be concerned) */
    cra_index = get_sgpd_sync_index(sc, HEVC_NAL_CRA_NUT); /* Clean Random Access */
    if (!cra_index)
        return 0;

    /* Build a list of open-GOP key samples */
    sc->open_key_samples_count = 0;
    for (uint32_t i = 0; i < sc->sync_group_count; i++)
        if (sc->sync_group[i].index == cra_index) {
            if (sc->sync_group[i].count > INT_MAX - sc->open_key_samples_count)
                return AVERROR(ENOMEM);
            sc->open_key_samples_count += sc->sync_group[i].count;
        }
    av_freep(&sc->open_key_samples);
    sc->open_key_samples = av_calloc(sc->open_key_samples_count, sizeof(*sc->open_key_samples));
    if (!sc->open_key_samples)
        return AVERROR(ENOMEM);
    k = 0;
    for (uint32_t i = 0; i < sc->sync_group_count; i++) {
        const MOVSbgp *sg = &sc->sync_group[i];
        if (sg->index == cra_index)
            for (uint32_t j = 0; j < sg->count; j++)
                sc->open_key_samples[k++] = sample_id;
        if (sg->count > INT_MAX - sample_id)
            return AVERROR_PATCHWELCOME;
        sample_id += sg->count;
    }

    /* Identify the minimal time step between samples */
    sc->min_sample_duration = UINT_MAX;
    for (uint32_t i = 0; i < sc->stts_count; i++)
        sc->min_sample_duration = FFMIN(sc->min_sample_duration, sc->stts_data[i].duration);

    return 0;
}

#define MOV_MERGE_CTTS 1
#define MOV_MERGE_STTS 2
/*
 * Merge stts and ctts arrays into a new combined array.
 * stts_count and ctts_count may be left untouched as they will be
 * used to check for the presence of either of them.
 */
static int mov_merge_tts_data(MOVContext *mov, AVStream *st, int flags)
{
    MOVStreamContext *sc = st->priv_data;
    int ctts = sc->ctts_data && (flags & MOV_MERGE_CTTS);
    int stts = sc->stts_data && (flags & MOV_MERGE_STTS);
    int idx = 0;

    if (!sc->ctts_data && !sc->stts_data)
        return 0;
    // Expand time to sample entries such that we have a 1-1 mapping with samples
    if (!sc->sample_count || sc->sample_count >= UINT_MAX / sizeof(*sc->tts_data))
        return -1;

    if (ctts) {
        sc->tts_data = av_fast_realloc(NULL, &sc->tts_allocated_size,
                                       sc->sample_count * sizeof(*sc->tts_data));
        if (!sc->tts_data)
            return -1;

        memset(sc->tts_data, 0, sc->tts_allocated_size);

        for (int i = 0; i < sc->ctts_count &&
                    idx < sc->sample_count; i++)
            for (int j = 0; j < sc->ctts_data[i].count &&
                        idx < sc->sample_count; j++) {
                sc->tts_data[idx].offset = sc->ctts_data[i].offset;
                sc->tts_data[idx++].count = 1;
            }

        sc->tts_count = idx;
    } else
        sc->ctts_count = 0;
    av_freep(&sc->ctts_data);
    sc->ctts_allocated_size = 0;

    idx = 0;
    if (stts) {
        MOVTimeToSample *tts_data = av_fast_realloc(sc->tts_data, &sc->tts_allocated_size,
                                                    sc->sample_count * sizeof(*sc->tts_data));
        if (!tts_data)
            return -1;

        if (!sc->tts_data)
            memset(tts_data, 0, sc->tts_allocated_size);
        sc->tts_data = tts_data;

        for (int i = 0; i < sc->stts_count &&
                    idx < sc->sample_count; i++)
            for (int j = 0; j < sc->stts_data[i].count &&
                        idx < sc->sample_count; j++) {
                sc->tts_data[idx].duration = sc->stts_data[i].duration;
                sc->tts_data[idx++].count = 1;
            }

        sc->tts_count = FFMAX(sc->tts_count, idx);
    } else
        sc->stts_count = 0;
    av_freep(&sc->stts_data);
    sc->stts_allocated_size = 0;

    return 0;
}

static void mov_build_index(MOVContext *mov, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int64_t current_offset;
    int64_t current_dts = 0;
    unsigned int stts_index = 0;
    unsigned int stsc_index = 0;
    unsigned int stss_index = 0;
    unsigned int stps_index = 0;
    unsigned int i, j;
    uint64_t stream_size = 0;

    int ret = build_open_gop_key_points(st);
    if (ret < 0)
        return;

    if (sc->elst_count) {
        int i, edit_start_index = 0, multiple_edits = 0;
        int64_t empty_duration = 0; // empty duration of the first edit list entry
        int64_t start_time = 0; // start time of the media

        for (i = 0; i < sc->elst_count; i++) {
            const MOVElst *e = &sc->elst_data[i];
            if (i == 0 && e->time == -1) {
                /* if empty, the first entry is the start time of the stream
                 * relative to the presentation itself */
                empty_duration = e->duration;
                edit_start_index = 1;
            } else if (i == edit_start_index && e->time >= 0) {
                start_time = e->time;
            } else {
                multiple_edits = 1;
            }
        }

        if (multiple_edits && !mov->advanced_editlist) {
            if (mov->advanced_editlist_autodisabled)
                av_log(mov->fc, AV_LOG_WARNING, "multiple edit list entries, "
                       "not supported in fragmented MP4 files\n");
            else
                av_log(mov->fc, AV_LOG_WARNING, "multiple edit list entries, "
                       "Use -advanced_editlist to correctly decode otherwise "
                       "a/v desync might occur\n");
        }

        /* adjust first dts according to edit list */
        if ((empty_duration || start_time) && mov->time_scale > 0) {
            if (empty_duration)
                empty_duration = av_rescale(empty_duration, sc->time_scale, mov->time_scale);

            if (av_sat_sub64(start_time, empty_duration) != start_time - (uint64_t)empty_duration)
                av_log(mov->fc, AV_LOG_WARNING, "start_time - empty_duration is not representable\n");

            sc->time_offset = start_time -  (uint64_t)empty_duration;
            sc->min_corrected_pts = start_time;
            if (!mov->advanced_editlist)
                current_dts = -sc->time_offset;
        }

        if (!multiple_edits && !mov->advanced_editlist &&
            st->codecpar->codec_id == AV_CODEC_ID_AAC && start_time > 0)
            sc->start_pad = start_time;
    }

    /* only use old uncompressed audio chunk demuxing when stts specifies it */
    if (!(st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
          sc->stts_count == 1 && sc->stts_data && sc->stts_data[0].duration == 1)) {
        unsigned int current_sample = 0;
        unsigned int stts_sample = 0;
        unsigned int sample_size;
        unsigned int distance = 0;
        unsigned int rap_group_index = 0;
        unsigned int rap_group_sample = 0;
        int rap_group_present = sc->rap_group_count && sc->rap_group;
        int key_off = (sc->keyframe_count && sc->keyframes[0] > 0) || (sc->stps_count && sc->stps_data[0] > 0);

        current_dts -= sc->dts_shift;

        if (!sc->sample_count || sti->nb_index_entries || sc->tts_count)
            return;
        if (sc->sample_count >= UINT_MAX / sizeof(*sti->index_entries) - sti->nb_index_entries)
            return;
        if (av_reallocp_array(&sti->index_entries,
                              sti->nb_index_entries + sc->sample_count,
                              sizeof(*sti->index_entries)) < 0) {
            sti->nb_index_entries = 0;
            return;
        }
        sti->index_entries_allocated_size = (sti->nb_index_entries + sc->sample_count) * sizeof(*sti->index_entries);

        ret = mov_merge_tts_data(mov, st, MOV_MERGE_CTTS | MOV_MERGE_STTS);
        if (ret < 0)
            return;

        for (i = 0; i < sc->chunk_count; i++) {
            int64_t next_offset = i+1 < sc->chunk_count ? sc->chunk_offsets[i+1] : INT64_MAX;
            current_offset = sc->chunk_offsets[i];
            while (mov_stsc_index_valid(stsc_index, sc->stsc_count) &&
                i + 1 == sc->stsc_data[stsc_index + 1].first)
                stsc_index++;

            if (next_offset > current_offset && sc->sample_size>0 && sc->sample_size < sc->stsz_sample_size &&
                sc->stsc_data[stsc_index].count * (int64_t)sc->stsz_sample_size > next_offset - current_offset) {
                av_log(mov->fc, AV_LOG_WARNING, "STSZ sample size %d invalid (too large), ignoring\n", sc->stsz_sample_size);
                sc->stsz_sample_size = sc->sample_size;
            }
            if (sc->stsz_sample_size>0 && sc->stsz_sample_size < sc->sample_size) {
                av_log(mov->fc, AV_LOG_WARNING, "STSZ sample size %d invalid (too small), ignoring\n", sc->stsz_sample_size);
                sc->stsz_sample_size = sc->sample_size;
            }

            for (j = 0; j < sc->stsc_data[stsc_index].count; j++) {
                int keyframe = 0;
                if (current_sample >= sc->sample_count) {
                    av_log(mov->fc, AV_LOG_ERROR, "wrong sample count\n");
                    return;
                }

                if (!sc->keyframe_absent && (!sc->keyframe_count || current_sample+key_off == sc->keyframes[stss_index])) {
                    keyframe = 1;
                    if (stss_index + 1 < sc->keyframe_count)
                        stss_index++;
                } else if (sc->stps_count && current_sample+key_off == sc->stps_data[stps_index]) {
                    keyframe = 1;
                    if (stps_index + 1 < sc->stps_count)
                        stps_index++;
                }
                if (rap_group_present && rap_group_index < sc->rap_group_count) {
                    if (sc->rap_group[rap_group_index].index > 0)
                        keyframe = 1;
                    if (++rap_group_sample == sc->rap_group[rap_group_index].count) {
                        rap_group_sample = 0;
                        rap_group_index++;
                    }
                }
                if (sc->keyframe_absent
                    && !sc->stps_count
                    && !rap_group_present
                    && (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || (i==0 && j==0)))
                     keyframe = 1;
                if (keyframe)
                    distance = 0;
                sample_size = sc->stsz_sample_size > 0 ? sc->stsz_sample_size : sc->sample_sizes[current_sample];
                if (current_offset > INT64_MAX - sample_size) {
                    av_log(mov->fc, AV_LOG_ERROR, "Current offset %"PRId64" or sample size %u is too large\n",
                           current_offset,
                           sample_size);
                    return;
                }

                if (sc->pseudo_stream_id == -1 ||
                   sc->stsc_data[stsc_index].id - 1 == sc->pseudo_stream_id) {
                    AVIndexEntry *e;
                    if (sample_size > 0x3FFFFFFF) {
                        av_log(mov->fc, AV_LOG_ERROR, "Sample size %u is too large\n", sample_size);
                        return;
                    }
                    e = &sti->index_entries[sti->nb_index_entries++];
                    e->pos = current_offset;
                    e->timestamp = current_dts;
                    e->size = sample_size;
                    e->min_distance = distance;
                    e->flags = keyframe ? AVINDEX_KEYFRAME : 0;
                    av_log(mov->fc, AV_LOG_TRACE, "AVIndex stream %d, sample %u, offset %"PRIx64", dts %"PRId64", "
                            "size %u, distance %u, keyframe %d\n", st->index, current_sample,
                            current_offset, current_dts, sample_size, distance, keyframe);
                    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && sti->nb_index_entries < 100)
                        ff_rfps_add_frame(mov->fc, st, current_dts);
                }

                current_offset += sample_size;
                stream_size += sample_size;

                current_dts += sc->tts_data[stts_index].duration;

                distance++;
                stts_sample++;
                current_sample++;
                if (stts_index + 1 < sc->tts_count && stts_sample == sc->tts_data[stts_index].count) {
                    stts_sample = 0;
                    stts_index++;
                }
            }
        }
        if (st->duration > 0)
            st->codecpar->bit_rate = stream_size*8*sc->time_scale/st->duration;
    } else {
        unsigned chunk_samples, total = 0;

        if (!sc->chunk_count || sc->tts_count)
            return;

        ret = mov_merge_tts_data(mov, st, MOV_MERGE_CTTS);
        if (ret < 0)
            return;

        // compute total chunk count
        for (i = 0; i < sc->stsc_count; i++) {
            unsigned count, chunk_count;

            chunk_samples = sc->stsc_data[i].count;
            if (i != sc->stsc_count - 1 &&
                sc->samples_per_frame && chunk_samples % sc->samples_per_frame) {
                av_log(mov->fc, AV_LOG_ERROR, "error unaligned chunk\n");
                return;
            }

            if (sc->samples_per_frame >= 160) { // gsm
                count = chunk_samples / sc->samples_per_frame;
            } else if (sc->samples_per_frame > 1) {
                unsigned samples = (1024/sc->samples_per_frame)*sc->samples_per_frame;
                count = (chunk_samples+samples-1) / samples;
            } else {
                count = (chunk_samples+1023) / 1024;
            }

            if (mov_stsc_index_valid(i, sc->stsc_count))
                chunk_count = sc->stsc_data[i+1].first - sc->stsc_data[i].first;
            else
                chunk_count = sc->chunk_count - (sc->stsc_data[i].first - 1);
            total += chunk_count * count;
        }

        av_log(mov->fc, AV_LOG_TRACE, "chunk count %u\n", total);
        if (total >= UINT_MAX / sizeof(*sti->index_entries) - sti->nb_index_entries)
            return;
        if (av_reallocp_array(&sti->index_entries,
                              sti->nb_index_entries + total,
                              sizeof(*sti->index_entries)) < 0) {
            sti->nb_index_entries = 0;
            return;
        }
        sti->index_entries_allocated_size = (sti->nb_index_entries + total) * sizeof(*sti->index_entries);

        // populate index
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (mov_stsc_index_valid(stsc_index, sc->stsc_count) &&
                i + 1 == sc->stsc_data[stsc_index + 1].first)
                stsc_index++;
            chunk_samples = sc->stsc_data[stsc_index].count;

            while (chunk_samples > 0) {
                AVIndexEntry *e;
                unsigned size, samples;

                if (sc->samples_per_frame > 1 && !sc->bytes_per_frame) {
                    avpriv_request_sample(mov->fc,
                           "Zero bytes per frame, but %d samples per frame",
                           sc->samples_per_frame);
                    return;
                }

                if (sc->samples_per_frame >= 160) { // gsm
                    samples = sc->samples_per_frame;
                    size = sc->bytes_per_frame;
                } else {
                    if (sc->samples_per_frame > 1) {
                        samples = FFMIN((1024 / sc->samples_per_frame)*
                                        sc->samples_per_frame, chunk_samples);
                        size = (samples / sc->samples_per_frame) * sc->bytes_per_frame;
                    } else {
                        samples = FFMIN(1024, chunk_samples);
                        size = samples * sc->sample_size;
                    }
                }

                if (sti->nb_index_entries >= total) {
                    av_log(mov->fc, AV_LOG_ERROR, "wrong chunk count %u\n", total);
                    return;
                }
                if (size > 0x3FFFFFFF) {
                    av_log(mov->fc, AV_LOG_ERROR, "Sample size %u is too large\n", size);
                    return;
                }
                e = &sti->index_entries[sti->nb_index_entries++];
                e->pos = current_offset;
                e->timestamp = current_dts;
                e->size = size;
                e->min_distance = 0;
                e->flags = AVINDEX_KEYFRAME;
                av_log(mov->fc, AV_LOG_TRACE, "AVIndex stream %d, chunk %u, offset %"PRIx64", dts %"PRId64", "
                       "size %u, duration %u\n", st->index, i, current_offset, current_dts,
                       size, samples);

                current_offset += size;
                current_dts += samples;
                chunk_samples -= samples;
            }
        }
    }

    if (!mov->ignore_editlist && mov->advanced_editlist) {
        // Fix index according to edit lists.
        mov_fix_index(mov, st);
    }

    // Update start time of the stream.
    if (st->start_time == AV_NOPTS_VALUE && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && sti->nb_index_entries > 0) {
        st->start_time = sti->index_entries[0].timestamp + sc->dts_shift;
        if (sc->tts_data) {
            st->start_time += sc->tts_data[0].offset;
        }
    }

    mov_estimate_video_delay(mov, st);
}

static int test_same_origin(const char *src, const char *ref) {
    char src_proto[64];
    char ref_proto[64];
    char src_auth[256];
    char ref_auth[256];
    char src_host[256];
    char ref_host[256];
    int src_port=-1;
    int ref_port=-1;

    av_url_split(src_proto, sizeof(src_proto), src_auth, sizeof(src_auth), src_host, sizeof(src_host), &src_port, NULL, 0, src);
    av_url_split(ref_proto, sizeof(ref_proto), ref_auth, sizeof(ref_auth), ref_host, sizeof(ref_host), &ref_port, NULL, 0, ref);

    if (strlen(src) == 0) {
        return -1;
    } else if (strlen(src_auth) + 1 >= sizeof(src_auth) ||
        strlen(ref_auth) + 1 >= sizeof(ref_auth) ||
        strlen(src_host) + 1 >= sizeof(src_host) ||
        strlen(ref_host) + 1 >= sizeof(ref_host)) {
        return 0;
    } else if (strcmp(src_proto, ref_proto) ||
               strcmp(src_auth, ref_auth) ||
               strcmp(src_host, ref_host) ||
               src_port != ref_port) {
        return 0;
    } else
        return 1;
}

static int mov_open_dref(MOVContext *c, AVIOContext **pb, const char *src, MOVDref *ref)
{
    /* try relative path, we do not try the absolute because it can leak information about our
       system to an attacker */
    if (ref->nlvl_to > 0 && ref->nlvl_from > 0) {
        char filename[1025];
        const char *src_path;
        int i, l;

        /* find a source dir */
        src_path = strrchr(src, '/');
        if (src_path)
            src_path++;
        else
            src_path = src;

        /* find a next level down to target */
        for (i = 0, l = strlen(ref->path) - 1; l >= 0; l--)
            if (ref->path[l] == '/') {
                if (i == ref->nlvl_to - 1)
                    break;
                else
                    i++;
            }

        /* compose filename if next level down to target was found */
        if (i == ref->nlvl_to - 1 && src_path - src  < sizeof(filename)) {
            memcpy(filename, src, src_path - src);
            filename[src_path - src] = 0;

            for (i = 1; i < ref->nlvl_from; i++)
                av_strlcat(filename, "../", sizeof(filename));

            av_strlcat(filename, ref->path + l + 1, sizeof(filename));
            if (!c->use_absolute_path) {
                int same_origin = test_same_origin(src, filename);

                if (!same_origin) {
                    av_log(c->fc, AV_LOG_ERROR,
                        "Reference with mismatching origin, %s not tried for security reasons, "
                        "set demuxer option use_absolute_path to allow it anyway\n",
                        ref->path);
                    return AVERROR(ENOENT);
                }

                if (strstr(ref->path + l + 1, "..") ||
                    strstr(ref->path + l + 1, ":") ||
                    (ref->nlvl_from > 1 && same_origin < 0) ||
                    (filename[0] == '/' && src_path == src))
                    return AVERROR(ENOENT);
            }

            if (strlen(filename) + 1 == sizeof(filename))
                return AVERROR(ENOENT);
            if (!c->fc->io_open(c->fc, pb, filename, AVIO_FLAG_READ, NULL))
                return 0;
        }
    } else if (c->use_absolute_path) {
        av_log(c->fc, AV_LOG_WARNING, "Using absolute path on user request, "
               "this is a possible security issue\n");
        if (!c->fc->io_open(c->fc, pb, ref->path, AVIO_FLAG_READ, NULL))
            return 0;
    } else {
        av_log(c->fc, AV_LOG_ERROR,
               "Absolute path %s not tried for security reasons, "
               "set demuxer option use_absolute_path to allow absolute paths\n",
               ref->path);
    }

    return AVERROR(ENOENT);
}

static void fix_timescale(MOVContext *c, MOVStreamContext *sc)
{
    if (sc->time_scale <= 0) {
        av_log(c->fc, AV_LOG_WARNING, "stream %d, timescale not set\n", sc->ffindex);
        sc->time_scale = c->time_scale;
        if (sc->time_scale <= 0)
            sc->time_scale = 1;
    }
}

#if CONFIG_IAMFDEC
static int mov_update_iamf_streams(MOVContext *c, const AVStream *st)
{
    const MOVStreamContext *sc = st->priv_data;
    const IAMFContext *iamf = &sc->iamf->iamf;

    for (int i = 0; i < iamf->nb_audio_elements; i++) {
        const AVStreamGroup *stg = NULL;

        for (int j = 0; j < c->fc->nb_stream_groups; j++)
            if (c->fc->stream_groups[j]->id == iamf->audio_elements[i]->audio_element_id)
                stg = c->fc->stream_groups[j];
        av_assert0(stg);

        for (int j = 0; j < stg->nb_streams; j++) {
            const FFStream *sti = cffstream(st);
            AVStream *out = stg->streams[j];
            FFStream *out_sti = ffstream(stg->streams[j]);

            out->codecpar->bit_rate = 0;

            if (out == st)
                continue;

            out->time_base           = st->time_base;
            out->start_time          = st->start_time;
            out->duration            = st->duration;
            out->nb_frames           = st->nb_frames;
            out->discard             = st->discard;

            av_assert0(!out_sti->index_entries);
            out_sti->index_entries = av_malloc(sti->index_entries_allocated_size);
            if (!out_sti->index_entries)
                return AVERROR(ENOMEM);

            out_sti->index_entries_allocated_size = sti->index_entries_allocated_size;
            out_sti->nb_index_entries = sti->nb_index_entries;
            out_sti->skip_samples = sti->skip_samples;
            memcpy(out_sti->index_entries, sti->index_entries, sti->index_entries_allocated_size);
        }
    }

    return 0;
}
#endif

static int sanity_checks(void *log_obj, MOVStreamContext *sc, int index)
{
    if ((sc->chunk_count && (!sc->stts_count || !sc->stsc_count ||
                            (!sc->sample_size && !sc->sample_count))) ||
        (!sc->chunk_count && sc->sample_count)) {
        av_log(log_obj, AV_LOG_ERROR, "stream %d, missing mandatory atoms, broken header\n",
               index);
        return 1;
    }

    if (sc->stsc_count && sc->stsc_data[ sc->stsc_count - 1 ].first > sc->chunk_count) {
        av_log(log_obj, AV_LOG_ERROR, "stream %d, contradictionary STSC and STCO\n",
               index);
        return 2;
    }
    return 0;
}

static int mov_read_trak(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int ret;

    st = avformat_new_stream(c->fc, NULL);
    if (!st) return AVERROR(ENOMEM);
    st->id = -1;
    sc = av_mallocz(sizeof(MOVStreamContext));
    if (!sc) return AVERROR(ENOMEM);

    st->priv_data = sc;
    st->codecpar->codec_type = AVMEDIA_TYPE_DATA;
    sc->ffindex = st->index;
    c->trak_index = st->index;
    sc->tref_flags = 0;
    sc->tref_id = -1;
    sc->refcount = 1;

    if ((ret = mov_read_default(c, pb, atom)) < 0)
        return ret;

    c->trak_index = -1;

    // Here stsc refers to a chunk not described in stco. This is technically invalid,
    // but we can overlook it (clearing stsc) whenever stts_count == 0 (indicating no samples).
    if (!sc->chunk_count && !sc->stts_count && sc->stsc_count) {
        sc->stsc_count = 0;
        av_freep(&sc->stsc_data);
    }

    ret = sanity_checks(c->fc, sc, st->index);
    if (ret)
        return ret > 1 ? AVERROR_INVALIDDATA : 0;

    fix_timescale(c, sc);

    avpriv_set_pts_info(st, 64, 1, sc->time_scale);

    /*
     * Advanced edit list support does not work with fragemented MP4s, which
     * have stsc, stsz, stco, and stts with zero entries in the moov atom.
     * In these files, trun atoms may be streamed in.
     */
    if (!sc->stts_count && c->advanced_editlist) {

        av_log(c->fc, AV_LOG_VERBOSE, "advanced_editlist does not work with fragmented "
                                      "MP4. disabling.\n");
        c->advanced_editlist = 0;
        c->advanced_editlist_autodisabled = 1;
    }

    mov_build_index(c, st);

#if CONFIG_IAMFDEC
    if (sc->iamf) {
        ret = mov_update_iamf_streams(c, st);
        if (ret < 0)
            return ret;
    }
#endif

    if (sc->dref_id-1 < sc->drefs_count && sc->drefs[sc->dref_id-1].path) {
        MOVDref *dref = &sc->drefs[sc->dref_id - 1];
        if (c->enable_drefs) {
            if (mov_open_dref(c, &sc->pb, c->fc->url, dref) < 0)
                av_log(c->fc, AV_LOG_ERROR,
                       "stream %d, error opening alias: path='%s', dir='%s', "
                       "filename='%s', volume='%s', nlvl_from=%d, nlvl_to=%d\n",
                       st->index, dref->path, dref->dir, dref->filename,
                       dref->volume, dref->nlvl_from, dref->nlvl_to);
        } else {
            av_log(c->fc, AV_LOG_WARNING,
                   "Skipped opening external track: "
                   "stream %d, alias: path='%s', dir='%s', "
                   "filename='%s', volume='%s', nlvl_from=%d, nlvl_to=%d."
                   "Set enable_drefs to allow this.\n",
                   st->index, dref->path, dref->dir, dref->filename,
                   dref->volume, dref->nlvl_from, dref->nlvl_to);
        }
    } else {
        sc->pb = c->fc->pb;
        sc->pb_is_copied = 1;
    }

    if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        int stts_constant = sc->stts_count && sc->tts_count;
        if (sc->h_spacing && sc->v_spacing)
            av_reduce(&st->sample_aspect_ratio.num, &st->sample_aspect_ratio.den,
                      sc->h_spacing, sc->v_spacing, INT_MAX);
        if (!st->sample_aspect_ratio.num && st->codecpar->width && st->codecpar->height &&
            sc->height && sc->width &&
            (st->codecpar->width != sc->width || st->codecpar->height != sc->height)) {
            av_reduce(&st->sample_aspect_ratio.num, &st->sample_aspect_ratio.den,
                      (int64_t)st->codecpar->height * sc->width,
                      (int64_t)st->codecpar->width  * sc->height, INT_MAX);
        }

#if FF_API_R_FRAME_RATE
        for (unsigned int i = 1; sc->stts_count && i + 1 < sc->tts_count; i++) {
            if (sc->tts_data[i].duration == sc->tts_data[0].duration)
                continue;
            stts_constant = 0;
        }
        if (stts_constant)
            av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den,
                      sc->time_scale, sc->tts_data[0].duration, INT_MAX);
#endif
    }

    // done for ai5q, ai52, ai55, ai1q, ai12 and ai15.
    if (!st->codecpar->extradata_size && st->codecpar->codec_id == AV_CODEC_ID_H264 &&
        TAG_IS_AVCI(st->codecpar->codec_tag)) {
        ret = ff_generate_avci_extradata(st);
        if (ret < 0)
            return ret;
    }

    switch (st->codecpar->codec_id) {
#if CONFIG_H261_DECODER
    case AV_CODEC_ID_H261:
#endif
#if CONFIG_H263_DECODER
    case AV_CODEC_ID_H263:
#endif
#if CONFIG_MPEG4_DECODER
    case AV_CODEC_ID_MPEG4:
#endif
        st->codecpar->width = 0; /* let decoder init width/height */
        st->codecpar->height= 0;
        break;
    }

    // If the duration of the mp3 packets is not constant, then they could need a parser
    if (st->codecpar->codec_id == AV_CODEC_ID_MP3
        && sc->time_scale == st->codecpar->sample_rate) {
        int stts_constant = 1;
        for (int i = 1; sc->stts_count && i < sc->tts_count; i++) {
            if (sc->tts_data[i].duration == sc->tts_data[0].duration)
                continue;
            stts_constant = 0;
        }
        if (!stts_constant)
            ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
    }
    /* Do not need those anymore. */
    av_freep(&sc->chunk_offsets);
    av_freep(&sc->sample_sizes);
    av_freep(&sc->keyframes);
    av_freep(&sc->stps_data);
    av_freep(&sc->elst_data);
    av_freep(&sc->rap_group);
    av_freep(&sc->sync_group);
    av_freep(&sc->sgpd_sync);

    return 0;
}

static int mov_read_ilst(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret;
    c->itunes_metadata = 1;
    ret = mov_read_default(c, pb, atom);
    c->itunes_metadata = 0;
    return ret;
}

static int mov_read_keys(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    uint32_t count;
    uint32_t i;

    if (atom.size < 8)
        return 0;

    avio_skip(pb, 4);
    count = avio_rb32(pb);
    atom.size -= 8;
    if (count >= UINT_MAX / sizeof(*c->meta_keys)) {
        av_log(c->fc, AV_LOG_ERROR,
               "The 'keys' atom with the invalid key count: %"PRIu32"\n", count);
        return AVERROR_INVALIDDATA;
    }

    c->meta_keys_count = count + 1;
    c->meta_keys = av_mallocz(c->meta_keys_count * sizeof(*c->meta_keys));
    if (!c->meta_keys)
        return AVERROR(ENOMEM);

    for (i = 1; i <= count; ++i) {
        uint32_t key_size = avio_rb32(pb);
        uint32_t type = avio_rl32(pb);
        if (key_size < 8 || key_size > atom.size) {
            av_log(c->fc, AV_LOG_ERROR,
                   "The key# %"PRIu32" in meta has invalid size:"
                   "%"PRIu32"\n", i, key_size);
            return AVERROR_INVALIDDATA;
        }
        atom.size -= key_size;
        key_size -= 8;
        if (type != MKTAG('m','d','t','a')) {
            avio_skip(pb, key_size);
            continue;
        }
        c->meta_keys[i] = av_mallocz(key_size + 1);
        if (!c->meta_keys[i])
            return AVERROR(ENOMEM);
        avio_read(pb, c->meta_keys[i], key_size);
    }

    return 0;
}

static int mov_read_custom(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t end = av_sat_add64(avio_tell(pb), atom.size);
    uint8_t *key = NULL, *val = NULL, *mean = NULL;
    int i;
    int ret = 0;
    AVStream *st;
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    for (i = 0; i < 3; i++) {
        uint8_t **p;
        uint32_t len, tag;

        if (end - avio_tell(pb) <= 12)
            break;

        len = avio_rb32(pb);
        tag = avio_rl32(pb);
        avio_skip(pb, 4); // flags

        if (len < 12 || len - 12 > end - avio_tell(pb))
            break;
        len -= 12;

        if (tag == MKTAG('m', 'e', 'a', 'n'))
            p = &mean;
        else if (tag == MKTAG('n', 'a', 'm', 'e'))
            p = &key;
        else if (tag == MKTAG('d', 'a', 't', 'a') && len > 4) {
            avio_skip(pb, 4);
            len -= 4;
            p = &val;
        } else
            break;

        if (*p)
            break;

        *p = av_malloc(len + 1);
        if (!*p) {
            ret = AVERROR(ENOMEM);
            break;
        }
        ret = ffio_read_size(pb, *p, len);
        if (ret < 0) {
            av_freep(p);
            break;
        }
        (*p)[len] = 0;
    }

    if (mean && key && val) {
        if (strcmp(key, "iTunSMPB") == 0) {
            int priming, remainder, samples;
            if(sscanf(val, "%*X %X %X %X", &priming, &remainder, &samples) == 3){
                if(priming>0 && priming<16384)
                    sc->start_pad = priming;
            }
        }
        if (strcmp(key, "cdec") != 0) {
            av_dict_set(&c->fc->metadata, key, val,
                        AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
            key = val = NULL;
        }
    } else {
        av_log(c->fc, AV_LOG_VERBOSE,
               "Unhandled or malformed custom metadata of size %"PRId64"\n", atom.size);
    }

    avio_seek(pb, end, SEEK_SET);
    av_freep(&key);
    av_freep(&val);
    av_freep(&mean);
    return ret;
}

static int heif_add_stream(MOVContext *c, HEIFItem *item)
{
    MOVStreamContext *sc;
    AVStream *st;

    st = avformat_new_stream(c->fc, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    sc = av_mallocz(sizeof(MOVStreamContext));
    if (!sc)
        return AVERROR(ENOMEM);

    item->st = st;
    st->id = item->item_id;
    st->priv_data = sc;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = mov_codec_id(st, item->type);
    sc->id = st->id;
    sc->ffindex = st->index;
    st->avg_frame_rate.num = st->avg_frame_rate.den = 1;
    st->time_base.num = st->time_base.den = 1;
    st->nb_frames = 1;
    sc->time_scale = 1;
    sc->pb = c->fc->pb;
    sc->pb_is_copied = 1;
    sc->refcount = 1;

    if (item->name)
        av_dict_set(&st->metadata, "title", item->name, 0);

    // Populate the necessary fields used by mov_build_index.
    sc->stsc_count = 1;
    sc->stsc_data = av_malloc_array(1, sizeof(*sc->stsc_data));
    if (!sc->stsc_data)
        return AVERROR(ENOMEM);
    sc->stsc_data[0].first = 1;
    sc->stsc_data[0].count = 1;
    sc->stsc_data[0].id = 1;
    sc->chunk_offsets = av_malloc_array(1, sizeof(*sc->chunk_offsets));
    if (!sc->chunk_offsets)
        return AVERROR(ENOMEM);
    sc->chunk_count = 1;
    sc->sample_sizes = av_malloc_array(1, sizeof(*sc->sample_sizes));
    if (!sc->sample_sizes)
        return AVERROR(ENOMEM);
    sc->sample_count = 1;
    sc->stts_data = av_malloc_array(1, sizeof(*sc->stts_data));
    if (!sc->stts_data)
        return AVERROR(ENOMEM);
    sc->stts_count = 1;
    sc->stts_data[0].count = 1;
    // Not used for still images. But needed by mov_build_index.
    sc->stts_data[0].duration = 0;

    return 0;
}

static int mov_read_meta(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    while (atom.size > 8) {
        uint32_t tag;
        if (avio_feof(pb))
            return AVERROR_EOF;
        tag = avio_rl32(pb);
        atom.size -= 4;
        if (tag == MKTAG('h','d','l','r')) {
            avio_seek(pb, -8, SEEK_CUR);
            atom.size += 8;
            return mov_read_default(c, pb, atom);
        }
    }
    return 0;
}

// return 1 when matrix is identity, 0 otherwise
#define IS_MATRIX_IDENT(matrix)            \
    ( (matrix)[0][0] == (1 << 16) &&       \
      (matrix)[1][1] == (1 << 16) &&       \
      (matrix)[2][2] == (1 << 30) &&       \
     !(matrix)[0][1] && !(matrix)[0][2] && \
     !(matrix)[1][0] && !(matrix)[1][2] && \
     !(matrix)[2][0] && !(matrix)[2][1])

static int mov_read_tkhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int i, j, e;
    int width;
    int height;
    int display_matrix[3][3];
    int res_display_matrix[3][3] = { { 0 } };
    AVStream *st;
    MOVStreamContext *sc;
    int version;
    int flags;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    // Each stream (trak) should have exactly 1 tkhd. This catches bad files and
    // avoids corrupting AVStreams mapped to an earlier tkhd.
    if (st->id != -1)
        return AVERROR_INVALIDDATA;

    version = avio_r8(pb);
    flags = avio_rb24(pb);
    st->disposition |= (flags & MOV_TKHD_FLAG_ENABLED) ? AV_DISPOSITION_DEFAULT : 0;

    if (version == 1) {
        avio_rb64(pb);
        avio_rb64(pb);
    } else {
        avio_rb32(pb); /* creation time */
        avio_rb32(pb); /* modification time */
    }
    st->id = (int)avio_rb32(pb); /* track id (NOT 0 !)*/
    sc->id = st->id;
    avio_rb32(pb); /* reserved */

    /* highlevel (considering edits) duration in movie timebase */
    (version == 1) ? avio_rb64(pb) : avio_rb32(pb);
    avio_rb32(pb); /* reserved */
    avio_rb32(pb); /* reserved */

    avio_rb16(pb); /* layer */
    avio_rb16(pb); /* alternate group */
    avio_rb16(pb); /* volume */
    avio_rb16(pb); /* reserved */

    //read in the display matrix (outlined in ISO 14496-12, Section 6.2.2)
    // they're kept in fixed point format through all calculations
    // save u,v,z to store the whole matrix in the AV_PKT_DATA_DISPLAYMATRIX
    // side data, but the scale factor is not needed to calculate aspect ratio
    for (i = 0; i < 3; i++) {
        display_matrix[i][0] = avio_rb32(pb);   // 16.16 fixed point
        display_matrix[i][1] = avio_rb32(pb);   // 16.16 fixed point
        display_matrix[i][2] = avio_rb32(pb);   //  2.30 fixed point
    }

    width = avio_rb32(pb);       // 16.16 fixed point track width
    height = avio_rb32(pb);      // 16.16 fixed point track height
    sc->width = width >> 16;
    sc->height = height >> 16;

    // apply the moov display matrix (after the tkhd one)
    for (i = 0; i < 3; i++) {
        const int sh[3] = { 16, 16, 30 };
        for (j = 0; j < 3; j++) {
            for (e = 0; e < 3; e++) {
                res_display_matrix[i][j] +=
                    ((int64_t) display_matrix[i][e] *
                     c->movie_display_matrix[e][j]) >> sh[e];
            }
        }
    }

    // save the matrix when it is not the default identity
    if (!IS_MATRIX_IDENT(res_display_matrix)) {
        av_freep(&sc->display_matrix);
        sc->display_matrix = av_malloc(sizeof(int32_t) * 9);
        if (!sc->display_matrix)
            return AVERROR(ENOMEM);

        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                sc->display_matrix[i * 3 + j] = res_display_matrix[i][j];
    }

    // transform the display width/height according to the matrix
    // to keep the same scale, use [width height 1<<16]
    if (width && height && sc->display_matrix) {
        double disp_transform[2];

        for (i = 0; i < 2; i++)
            disp_transform[i] = hypot(sc->display_matrix[0 + i],
                                      sc->display_matrix[3 + i]);

        if (disp_transform[0] > 1       && disp_transform[1] > 1 &&
            disp_transform[0] < (1<<24) && disp_transform[1] < (1<<24) &&
            fabs((disp_transform[0] / disp_transform[1]) - 1.0) > 0.01)
            st->sample_aspect_ratio = av_d2q(
                disp_transform[0] / disp_transform[1],
                INT_MAX);
    }
    return 0;
}

static int mov_read_tfhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    MOVTrackExt *trex = NULL;
    int flags, track_id, i;
    MOVFragmentStreamInfo * frag_stream_info;

    avio_r8(pb); /* version */
    flags = avio_rb24(pb);

    track_id = avio_rb32(pb);
    if (!track_id)
        return AVERROR_INVALIDDATA;
    for (i = 0; i < c->trex_count; i++)
        if (c->trex_data[i].track_id == track_id) {
            trex = &c->trex_data[i];
            break;
        }
    if (!trex) {
        av_log(c->fc, AV_LOG_WARNING, "could not find corresponding trex (id %u)\n", track_id);
        return 0;
    }
    c->fragment.found_tfhd = 1;
    frag->track_id = track_id;
    set_frag_stream(&c->frag_index, track_id);

    frag->base_data_offset = flags & MOV_TFHD_BASE_DATA_OFFSET ?
                             avio_rb64(pb) : flags & MOV_TFHD_DEFAULT_BASE_IS_MOOF ?
                             frag->moof_offset : frag->implicit_offset;
    frag->stsd_id  = flags & MOV_TFHD_STSD_ID ? avio_rb32(pb) : trex->stsd_id;

    frag->duration = flags & MOV_TFHD_DEFAULT_DURATION ?
                     avio_rb32(pb) : trex->duration;
    frag->size     = flags & MOV_TFHD_DEFAULT_SIZE ?
                     avio_rb32(pb) : trex->size;
    frag->flags    = flags & MOV_TFHD_DEFAULT_FLAGS ?
                     avio_rb32(pb) : trex->flags;
    av_log(c->fc, AV_LOG_TRACE, "frag flags 0x%x\n", frag->flags);

    frag_stream_info = get_current_frag_stream_info(&c->frag_index);
    if (frag_stream_info) {
        frag_stream_info->next_trun_dts = AV_NOPTS_VALUE;
        frag_stream_info->stsd_id = frag->stsd_id;
    }
    return 0;
}

static int mov_read_chap(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    unsigned i, num;
    void *new_tracks;

    num = atom.size / 4;
    if (!(new_tracks = av_malloc_array(num, sizeof(int))))
        return AVERROR(ENOMEM);

    av_free(c->chapter_tracks);
    c->chapter_tracks = new_tracks;
    c->nb_chapter_tracks = num;

    for (i = 0; i < num && !pb->eof_reached; i++)
        c->chapter_tracks[i] = avio_rb32(pb);

    c->nb_chapter_tracks = i;

    return 0;
}

static int mov_read_trex(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVTrackExt *trex;
    int err;

    if ((uint64_t)c->trex_count+1 >= UINT_MAX / sizeof(*c->trex_data))
        return AVERROR_INVALIDDATA;
    if ((err = av_reallocp_array(&c->trex_data, c->trex_count + 1,
                                 sizeof(*c->trex_data))) < 0) {
        c->trex_count = 0;
        return err;
    }

    c->fc->duration = AV_NOPTS_VALUE; // the duration from mvhd is not representing the whole file when fragments are used.

    trex = &c->trex_data[c->trex_count++];
    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    trex->track_id = avio_rb32(pb);
    trex->stsd_id  = avio_rb32(pb);
    trex->duration = avio_rb32(pb);
    trex->size     = avio_rb32(pb);
    trex->flags    = avio_rb32(pb);
    return 0;
}

static int mov_read_tfdt(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    AVStream *st = NULL;
    MOVStreamContext *sc;
    int version, i;
    MOVFragmentStreamInfo * frag_stream_info;
    int64_t base_media_decode_time;

    for (i = 0; i < c->fc->nb_streams; i++) {
        sc = c->fc->streams[i]->priv_data;
        if (sc->id == frag->track_id) {
            st = c->fc->streams[i];
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_WARNING, "could not find corresponding track id %u\n", frag->track_id);
        return 0;
    }
    sc = st->priv_data;
    if (sc->pseudo_stream_id + 1 != frag->stsd_id && sc->pseudo_stream_id != -1)
        return 0;
    version = avio_r8(pb);
    avio_rb24(pb); /* flags */
    if (version) {
        base_media_decode_time = avio_rb64(pb);
    } else {
        base_media_decode_time = avio_rb32(pb);
    }

    frag_stream_info = get_current_frag_stream_info(&c->frag_index);
    if (frag_stream_info)
        frag_stream_info->tfdt_dts = base_media_decode_time;
    sc->track_end = base_media_decode_time;

    return 0;
}

static int mov_read_trun(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    AVStream *st = NULL;
    FFStream *sti = NULL;
    MOVStreamContext *sc;
    MOVTimeToSample *tts_data;
    uint64_t offset;
    int64_t dts, pts = AV_NOPTS_VALUE;
    int data_offset = 0;
    unsigned entries, first_sample_flags = frag->flags;
    int flags, distance, i;
    int64_t prev_dts = AV_NOPTS_VALUE;
    int next_frag_index = -1, index_entry_pos;
    size_t requested_size;
    size_t old_allocated_size;
    AVIndexEntry *new_entries;
    MOVFragmentStreamInfo * frag_stream_info;

    if (!frag->found_tfhd) {
        av_log(c->fc, AV_LOG_ERROR, "trun track id unknown, no tfhd was found\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < c->fc->nb_streams; i++) {
        sc = c->fc->streams[i]->priv_data;
        if (sc->id == frag->track_id) {
            st = c->fc->streams[i];
            sti = ffstream(st);
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_WARNING, "could not find corresponding track id %u\n", frag->track_id);
        return 0;
    }
    sc = st->priv_data;
    if (sc->pseudo_stream_id+1 != frag->stsd_id && sc->pseudo_stream_id != -1)
        return 0;

    // Find the next frag_index index that has a valid index_entry for
    // the current track_id.
    //
    // A valid index_entry means the trun for the fragment was read
    // and it's samples are in index_entries at the given position.
    // New index entries will be inserted before the index_entry found.
    index_entry_pos = sti->nb_index_entries;
    for (i = c->frag_index.current + 1; i < c->frag_index.nb_items; i++) {
        frag_stream_info = get_frag_stream_info(&c->frag_index, i, frag->track_id);
        if (frag_stream_info && frag_stream_info->index_entry >= 0) {
            next_frag_index = i;
            index_entry_pos = frag_stream_info->index_entry;
            break;
        }
    }
    av_assert0(index_entry_pos <= sti->nb_index_entries);

    avio_r8(pb); /* version */
    flags = avio_rb24(pb);
    entries = avio_rb32(pb);
    av_log(c->fc, AV_LOG_TRACE, "flags 0x%x entries %u\n", flags, entries);

    if ((uint64_t)entries+sc->tts_count >= UINT_MAX/sizeof(*sc->tts_data))
        return AVERROR_INVALIDDATA;
    if (flags & MOV_TRUN_DATA_OFFSET)        data_offset        = avio_rb32(pb);
    if (flags & MOV_TRUN_FIRST_SAMPLE_FLAGS) first_sample_flags = avio_rb32(pb);

    frag_stream_info = get_current_frag_stream_info(&c->frag_index);
    if (frag_stream_info) {
        if (frag_stream_info->next_trun_dts != AV_NOPTS_VALUE) {
            dts = frag_stream_info->next_trun_dts - sc->time_offset;
        } else if (frag_stream_info->first_tfra_pts != AV_NOPTS_VALUE &&
            c->use_mfra_for == FF_MOV_FLAG_MFRA_PTS) {
            pts = frag_stream_info->first_tfra_pts;
            av_log(c->fc, AV_LOG_DEBUG, "found mfra time %"PRId64
                    ", using it for pts\n", pts);
        } else if (frag_stream_info->first_tfra_pts != AV_NOPTS_VALUE &&
            c->use_mfra_for == FF_MOV_FLAG_MFRA_DTS) {
            dts = frag_stream_info->first_tfra_pts;
            av_log(c->fc, AV_LOG_DEBUG, "found mfra time %"PRId64
                    ", using it for dts\n", pts);
        } else {
            int has_tfdt = frag_stream_info->tfdt_dts != AV_NOPTS_VALUE;
            int has_sidx = frag_stream_info->sidx_pts != AV_NOPTS_VALUE;
            int fallback_tfdt = !c->use_tfdt && !has_sidx && has_tfdt;
            int fallback_sidx =  c->use_tfdt && !has_tfdt && has_sidx;

            if (fallback_sidx) {
                av_log(c->fc, AV_LOG_DEBUG, "use_tfdt set but no tfdt found, using sidx instead\n");
            }
            if (fallback_tfdt) {
                av_log(c->fc, AV_LOG_DEBUG, "use_tfdt not set but no sidx found, using tfdt instead\n");
            }

            if (has_tfdt && c->use_tfdt || fallback_tfdt) {
                dts = frag_stream_info->tfdt_dts - sc->time_offset;
                av_log(c->fc, AV_LOG_DEBUG, "found tfdt time %"PRId64
                        ", using it for dts\n", dts);
            } else if (has_sidx && !c->use_tfdt || fallback_sidx) {
                // FIXME: sidx earliest_presentation_time is *PTS*, s.b.
                // pts = frag_stream_info->sidx_pts;
                dts = frag_stream_info->sidx_pts - sc->time_offset;
                av_log(c->fc, AV_LOG_DEBUG, "found sidx time %"PRId64
                        ", using it for dts\n", frag_stream_info->sidx_pts);
            } else {
                dts = sc->track_end - sc->time_offset;
                av_log(c->fc, AV_LOG_DEBUG, "found track end time %"PRId64
                        ", using it for dts\n", dts);
            }
        }
    } else {
        dts = sc->track_end - sc->time_offset;
        av_log(c->fc, AV_LOG_DEBUG, "found track end time %"PRId64
                ", using it for dts\n", dts);
    }
    offset   = frag->base_data_offset + data_offset;
    distance = 0;
    av_log(c->fc, AV_LOG_TRACE, "first sample flags 0x%x\n", first_sample_flags);

    // realloc space for new index entries
    if ((uint64_t)sti->nb_index_entries + entries >= UINT_MAX / sizeof(AVIndexEntry)) {
        entries = UINT_MAX / sizeof(AVIndexEntry) - sti->nb_index_entries;
        av_log(c->fc, AV_LOG_ERROR, "Failed to add index entry\n");
    }
    if (entries == 0)
        return 0;

    requested_size = (sti->nb_index_entries + entries) * sizeof(AVIndexEntry);
    new_entries = av_fast_realloc(sti->index_entries,
                                  &sti->index_entries_allocated_size,
                                  requested_size);
    if (!new_entries)
        return AVERROR(ENOMEM);
    sti->index_entries= new_entries;

    requested_size = (sti->nb_index_entries + entries) * sizeof(*sc->tts_data);
    old_allocated_size = sc->tts_allocated_size;
    tts_data = av_fast_realloc(sc->tts_data, &sc->tts_allocated_size,
                                requested_size);
    if (!tts_data)
        return AVERROR(ENOMEM);
    sc->tts_data = tts_data;

    // In case there were samples without time to sample entries, ensure they get
    // zero valued entries. This ensures clips which mix boxes with and
    // without time to sample entries don't pickup uninitialized data.
    memset((uint8_t*)(sc->tts_data) + old_allocated_size, 0,
           sc->tts_allocated_size - old_allocated_size);

    if (index_entry_pos < sti->nb_index_entries) {
        // Make hole in index_entries and tts_data for new samples
        memmove(sti->index_entries + index_entry_pos + entries,
                sti->index_entries + index_entry_pos,
                sizeof(*sti->index_entries) *
                (sti->nb_index_entries - index_entry_pos));
        memmove(sc->tts_data + index_entry_pos + entries,
                sc->tts_data + index_entry_pos,
                sizeof(*sc->tts_data) * (sc->tts_count - index_entry_pos));
        if (index_entry_pos < sc->current_sample) {
            sc->current_sample += entries;
        }
    }

    sti->nb_index_entries += entries;
    sc->tts_count = sti->nb_index_entries;
    sc->stts_count = sti->nb_index_entries;
    if (flags & MOV_TRUN_SAMPLE_CTS)
        sc->ctts_count = sti->nb_index_entries;

    // Record the index_entry position in frag_index of this fragment
    if (frag_stream_info) {
        frag_stream_info->index_entry = index_entry_pos;
        if (frag_stream_info->index_base < 0)
            frag_stream_info->index_base = index_entry_pos;
    }

    if (index_entry_pos > 0)
        prev_dts = sti->index_entries[index_entry_pos-1].timestamp;

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        unsigned sample_size = frag->size;
        int sample_flags = i ? frag->flags : first_sample_flags;
        unsigned sample_duration = frag->duration;
        unsigned ctts_duration = 0;
        int keyframe = 0;
        int index_entry_flags = 0;

        if (flags & MOV_TRUN_SAMPLE_DURATION) sample_duration = avio_rb32(pb);
        if (flags & MOV_TRUN_SAMPLE_SIZE)     sample_size     = avio_rb32(pb);
        if (flags & MOV_TRUN_SAMPLE_FLAGS)    sample_flags    = avio_rb32(pb);
        if (flags & MOV_TRUN_SAMPLE_CTS)      ctts_duration   = avio_rb32(pb);

        mov_update_dts_shift(sc, ctts_duration, c->fc);
        if (pts != AV_NOPTS_VALUE) {
            dts = pts - sc->dts_shift;
            if (flags & MOV_TRUN_SAMPLE_CTS) {
                dts -= ctts_duration;
            } else {
                dts -= sc->time_offset;
            }
            av_log(c->fc, AV_LOG_DEBUG,
                   "pts %"PRId64" calculated dts %"PRId64
                   " sc->dts_shift %d ctts.duration %d"
                   " sc->time_offset %"PRId64
                   " flags & MOV_TRUN_SAMPLE_CTS %d\n",
                   pts, dts,
                   sc->dts_shift, ctts_duration,
                   sc->time_offset, flags & MOV_TRUN_SAMPLE_CTS);
            pts = AV_NOPTS_VALUE;
        }

        keyframe =
                !(sample_flags & (MOV_FRAG_SAMPLE_FLAG_IS_NON_SYNC |
                                  MOV_FRAG_SAMPLE_FLAG_DEPENDS_YES));
        if (keyframe) {
            distance = 0;
            index_entry_flags |= AVINDEX_KEYFRAME;
        }
        // Fragments can overlap in time.  Discard overlapping frames after
        // decoding.
        if (prev_dts >= dts)
            index_entry_flags |= AVINDEX_DISCARD_FRAME;

        sti->index_entries[index_entry_pos].pos   = offset;
        sti->index_entries[index_entry_pos].timestamp = dts;
        sti->index_entries[index_entry_pos].size  = sample_size;
        sti->index_entries[index_entry_pos].min_distance = distance;
        sti->index_entries[index_entry_pos].flags = index_entry_flags;

        sc->tts_data[index_entry_pos].count = 1;
        sc->tts_data[index_entry_pos].offset = ctts_duration;
        sc->tts_data[index_entry_pos].duration = sample_duration;
        index_entry_pos++;

        av_log(c->fc, AV_LOG_TRACE, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", "
                "size %u, distance %d, keyframe %d\n", st->index,
                index_entry_pos, offset, dts, sample_size, distance, keyframe);
        distance++;
        if (av_sat_add64(dts, sample_duration) != dts + (uint64_t)sample_duration)
            return AVERROR_INVALIDDATA;
        if (!sample_size)
            return AVERROR_INVALIDDATA;
        dts += sample_duration;
        offset += sample_size;
        sc->data_size += sample_size;

        if (sample_duration <= INT64_MAX - sc->duration_for_fps &&
            1 <= INT_MAX - sc->nb_frames_for_fps
        ) {
            sc->duration_for_fps += sample_duration;
            sc->nb_frames_for_fps ++;
        }
    }
    if (frag_stream_info)
        frag_stream_info->next_trun_dts = dts + sc->time_offset;
    if (i < entries) {
        // EOF found before reading all entries.  Fix the hole this would
        // leave in index_entries and tts_data
        int gap = entries - i;
        memmove(sti->index_entries + index_entry_pos,
                sti->index_entries + index_entry_pos + gap,
                sizeof(*sti->index_entries) *
                (sti->nb_index_entries - (index_entry_pos + gap)));
        memmove(sc->tts_data + index_entry_pos,
                sc->tts_data + index_entry_pos + gap,
                sizeof(*sc->tts_data) *
                (sc->tts_count - (index_entry_pos + gap)));

        sti->nb_index_entries -= gap;
        sc->tts_count -= gap;
        if (index_entry_pos < sc->current_sample) {
            sc->current_sample -= gap;
        }
        entries = i;
    }

    // The end of this new fragment may overlap in time with the start
    // of the next fragment in index_entries. Mark the samples in the next
    // fragment that overlap with AVINDEX_DISCARD_FRAME
    prev_dts = AV_NOPTS_VALUE;
    if (index_entry_pos > 0)
        prev_dts = sti->index_entries[index_entry_pos-1].timestamp;
    for (int i = index_entry_pos; i < sti->nb_index_entries; i++) {
        if (prev_dts < sti->index_entries[i].timestamp)
            break;
        sti->index_entries[i].flags |= AVINDEX_DISCARD_FRAME;
    }

    // If a hole was created to insert the new index_entries into,
    // the index_entry recorded for all subsequent moof must
    // be incremented by the number of entries inserted.
    fix_frag_index_entries(&c->frag_index, next_frag_index,
                           frag->track_id, entries);

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_WARNING, "reached eof, corrupted TRUN atom\n");
        return AVERROR_EOF;
    }

    frag->implicit_offset = offset;

    sc->track_end = dts + sc->time_offset;
    if (st->duration < sc->track_end)
        st->duration = sc->track_end;

    return 0;
}

static int mov_read_sidx(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t stream_size = avio_size(pb);
    int64_t offset = av_sat_add64(avio_tell(pb), atom.size), pts, timestamp;
    uint8_t version, is_complete;
    int64_t offadd;
    unsigned i, j, track_id, item_count;
    AVStream *st = NULL;
    AVStream *ref_st = NULL;
    MOVStreamContext *sc, *ref_sc = NULL;
    AVRational timescale;

    version = avio_r8(pb);
    if (version > 1) {
        avpriv_request_sample(c->fc, "sidx version %u", version);
        return 0;
    }

    avio_rb24(pb); // flags

    track_id = avio_rb32(pb); // Reference ID
    for (i = 0; i < c->fc->nb_streams; i++) {
        sc = c->fc->streams[i]->priv_data;
        if (sc->id == track_id) {
            st = c->fc->streams[i];
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_WARNING, "could not find corresponding track id %d\n", track_id);
        return 0;
    }

    sc = st->priv_data;

    timescale = av_make_q(1, avio_rb32(pb));

    if (timescale.den <= 0) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid sidx timescale 1/%d\n", timescale.den);
        return AVERROR_INVALIDDATA;
    }

    if (version == 0) {
        pts = avio_rb32(pb);
        offadd= avio_rb32(pb);
    } else {
        pts = avio_rb64(pb);
        offadd= avio_rb64(pb);
    }
    if (av_sat_add64(offset, offadd) != offset + (uint64_t)offadd)
        return AVERROR_INVALIDDATA;

    offset += (uint64_t)offadd;

    avio_rb16(pb); // reserved

    item_count = avio_rb16(pb);
    if (item_count == 0)
        return AVERROR_INVALIDDATA;

    for (i = 0; i < item_count; i++) {
        int index;
        MOVFragmentStreamInfo * frag_stream_info;
        uint32_t size = avio_rb32(pb);
        uint32_t duration = avio_rb32(pb);
        if (size & 0x80000000) {
            avpriv_request_sample(c->fc, "sidx reference_type 1");
            return AVERROR_PATCHWELCOME;
        }
        avio_rb32(pb); // sap_flags
        timestamp = av_rescale_q(pts, timescale, st->time_base);

        index = update_frag_index(c, offset);
        frag_stream_info = get_frag_stream_info(&c->frag_index, index, track_id);
        if (frag_stream_info)
            frag_stream_info->sidx_pts = timestamp;

        if (av_sat_add64(offset, size) != offset + (uint64_t)size ||
            av_sat_add64(pts, duration) != pts + (uint64_t)duration
        )
            return AVERROR_INVALIDDATA;
        offset += size;
        pts += duration;
    }

    st->duration = sc->track_end = pts;

    sc->has_sidx = 1;

    // See if the remaining bytes are just an mfra which we can ignore.
    is_complete = offset == stream_size;
    if (!is_complete && (pb->seekable & AVIO_SEEKABLE_NORMAL) && stream_size > 0 ) {
        int64_t ret;
        int64_t original_pos = avio_tell(pb);
        if (!c->have_read_mfra_size) {
            if ((ret = avio_seek(pb, stream_size - 4, SEEK_SET)) < 0)
                return ret;
            c->mfra_size = avio_rb32(pb);
            c->have_read_mfra_size = 1;
            if ((ret = avio_seek(pb, original_pos, SEEK_SET)) < 0)
                return ret;
        }
        if (offset == stream_size - c->mfra_size)
            is_complete = 1;
    }

    if (is_complete) {
        // Find first entry in fragment index that came from an sidx.
        // This will pretty much always be the first entry.
        for (i = 0; i < c->frag_index.nb_items; i++) {
            MOVFragmentIndexItem * item = &c->frag_index.item[i];
            for (j = 0; ref_st == NULL && j < item->nb_stream_info; j++) {
                MOVFragmentStreamInfo * si;
                si = &item->stream_info[j];
                if (si->sidx_pts != AV_NOPTS_VALUE) {
                    ref_st = c->fc->streams[j];
                    ref_sc = ref_st->priv_data;
                    break;
                }
            }
        }
        if (ref_st) for (i = 0; i < c->fc->nb_streams; i++) {
            st = c->fc->streams[i];
            sc = st->priv_data;
            if (!sc->has_sidx) {
                st->duration = sc->track_end = av_rescale(ref_st->duration, sc->time_scale, ref_sc->time_scale);
            }
        }

        c->frag_index.complete = 1;
    }

    return 0;
}

/* this atom should be null (from specs), but some buggy files put the 'moov' atom inside it... */
/* like the files created with Adobe Premiere 5.0, for samples see */
/* http://graphics.tudelft.nl/~wouter/publications/soundtests/ */
static int mov_read_wide(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int err;

    if (atom.size < 8)
        return 0; /* continue */
    if (avio_rb32(pb) != 0) { /* 0 sized mdat atom... use the 'wide' atom size */
        avio_skip(pb, atom.size - 4);
        return 0;
    }
    atom.type = avio_rl32(pb);
    atom.size -= 8;
    if (atom.type != MKTAG('m','d','a','t')) {
        avio_skip(pb, atom.size);
        return 0;
    }
    err = mov_read_mdat(c, pb, atom);
    return err;
}

static int mov_read_cmov(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
#if CONFIG_ZLIB
    FFIOContext ctx;
    uint8_t *cmov_data;
    uint8_t *moov_data; /* uncompressed data */
    long cmov_len, moov_len;
    int ret = -1;

    avio_rb32(pb); /* dcom atom */
    if (avio_rl32(pb) != MKTAG('d','c','o','m'))
        return AVERROR_INVALIDDATA;
    if (avio_rl32(pb) != MKTAG('z','l','i','b')) {
        av_log(c->fc, AV_LOG_ERROR, "unknown compression for cmov atom !\n");
        return AVERROR_INVALIDDATA;
    }
    avio_rb32(pb); /* cmvd atom */
    if (avio_rl32(pb) != MKTAG('c','m','v','d'))
        return AVERROR_INVALIDDATA;
    moov_len = avio_rb32(pb); /* uncompressed size */
    cmov_len = atom.size - 6 * 4;

    cmov_data = av_malloc(cmov_len);
    if (!cmov_data)
        return AVERROR(ENOMEM);
    moov_data = av_malloc(moov_len);
    if (!moov_data) {
        av_free(cmov_data);
        return AVERROR(ENOMEM);
    }
    ret = ffio_read_size(pb, cmov_data, cmov_len);
    if (ret < 0)
        goto free_and_return;

    ret = AVERROR_INVALIDDATA;
    if (uncompress (moov_data, (uLongf *) &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        goto free_and_return;
    ffio_init_read_context(&ctx, moov_data, moov_len);
    ctx.pub.seekable = AVIO_SEEKABLE_NORMAL;
    atom.type = MKTAG('m','o','o','v');
    atom.size = moov_len;
    ret = mov_read_default(c, &ctx.pub, atom);
free_and_return:
    av_free(moov_data);
    av_free(cmov_data);
    return ret;
#else
    av_log(c->fc, AV_LOG_ERROR, "this file requires zlib support compiled in\n");
    return AVERROR(ENOSYS);
#endif
}

/* edit list atom */
static int mov_read_elst(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;
    int i, edit_count, version;
    int64_t elst_entry_size;

    if (c->fc->nb_streams < 1 || c->ignore_editlist)
        return 0;
    sc = c->fc->streams[c->fc->nb_streams-1]->priv_data;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    edit_count = avio_rb32(pb); /* entries */
    atom.size -= 8;

    elst_entry_size = version == 1 ? 20 : 12;
    if (atom.size != edit_count * elst_entry_size) {
        if (c->fc->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
            av_log(c->fc, AV_LOG_ERROR, "Invalid edit list entry_count: %d for elst atom of size: %"PRId64" bytes.\n",
                   edit_count, atom.size + 8);
            return AVERROR_INVALIDDATA;
        } else {
            edit_count = atom.size / elst_entry_size;
            if (edit_count * elst_entry_size != atom.size) {
                av_log(c->fc, AV_LOG_WARNING, "ELST atom of %"PRId64" bytes, bigger than %d entries.\n", atom.size, edit_count);
            }
        }
    }

    if (!edit_count)
        return 0;
    if (sc->elst_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated ELST atom\n");
    av_free(sc->elst_data);
    sc->elst_count = 0;
    sc->elst_data = av_malloc_array(edit_count, sizeof(*sc->elst_data));
    if (!sc->elst_data)
        return AVERROR(ENOMEM);

    av_log(c->fc, AV_LOG_TRACE, "track[%u].edit_count = %i\n", c->fc->nb_streams - 1, edit_count);
    for (i = 0; i < edit_count && atom.size > 0 && !pb->eof_reached; i++) {
        MOVElst *e = &sc->elst_data[i];

        if (version == 1) {
            e->duration = avio_rb64(pb);
            e->time     = avio_rb64(pb);
            atom.size -= 16;
        } else {
            e->duration = avio_rb32(pb); /* segment duration */
            e->time     = (int32_t)avio_rb32(pb); /* media time */
            atom.size -= 8;
        }
        e->rate = avio_rb32(pb) / 65536.0;
        atom.size -= 4;
        av_log(c->fc, AV_LOG_TRACE, "duration=%"PRId64" time=%"PRId64" rate=%f\n",
               e->duration, e->time, e->rate);

        if (e->time < 0 && e->time != -1 &&
            c->fc->strict_std_compliance >= FF_COMPLIANCE_STRICT) {
            av_log(c->fc, AV_LOG_ERROR, "Track %d, edit %d: Invalid edit list media time=%"PRId64"\n",
                   c->fc->nb_streams-1, i, e->time);
            return AVERROR_INVALIDDATA;
        }
        if (e->duration < 0) {
            av_log(c->fc, AV_LOG_ERROR, "Track %d, edit %d: Invalid edit list duration=%"PRId64"\n",
                   c->fc->nb_streams-1, i, e->duration);
            return AVERROR_INVALIDDATA;
        }
    }
    sc->elst_count = i;

    return 0;
}

static int mov_read_tmcd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;
    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;
    sc->timecode_track = avio_rb32(pb);
    return 0;
}

static int mov_read_vpcc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int version, color_range, color_primaries, color_trc, color_space;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];

    if (atom.size < 5) {
        av_log(c->fc, AV_LOG_ERROR, "Empty VP Codec Configuration box\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version != 1) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported VP Codec Configuration box version %d\n", version);
        return 0;
    }
    avio_skip(pb, 3); /* flags */

    avio_skip(pb, 2); /* profile + level */
    color_range     = avio_r8(pb); /* bitDepth, chromaSubsampling, videoFullRangeFlag */
    color_primaries = avio_r8(pb);
    color_trc       = avio_r8(pb);
    color_space     = avio_r8(pb);
    if (avio_rb16(pb)) /* codecIntializationDataSize */
        return AVERROR_INVALIDDATA;

    if (!av_color_primaries_name(color_primaries))
        color_primaries = AVCOL_PRI_UNSPECIFIED;
    if (!av_color_transfer_name(color_trc))
        color_trc = AVCOL_TRC_UNSPECIFIED;
    if (!av_color_space_name(color_space))
        color_space = AVCOL_SPC_UNSPECIFIED;

    st->codecpar->color_range     = (color_range & 1) ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    st->codecpar->color_primaries = color_primaries;
    st->codecpar->color_trc       = color_trc;
    st->codecpar->color_space     = color_space;

    return 0;
}

static int mov_read_smdm(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;
    int i, version;

    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;

    if (atom.size < 5) {
        av_log(c->fc, AV_LOG_ERROR, "Empty Mastering Display Metadata box\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported Mastering Display Metadata box version %d\n", version);
        return 0;
    }
    if (sc->mastering) {
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicate Mastering Display Metadata\n");
        return 0;
    }

    avio_skip(pb, 3); /* flags */

    sc->mastering = av_mastering_display_metadata_alloc_size(&sc->mastering_size);
    if (!sc->mastering)
        return AVERROR(ENOMEM);

    for (i = 0; i < 3; i++) {
        sc->mastering->display_primaries[i][0] = av_make_q(avio_rb16(pb), 1 << 16);
        sc->mastering->display_primaries[i][1] = av_make_q(avio_rb16(pb), 1 << 16);
    }
    sc->mastering->white_point[0] = av_make_q(avio_rb16(pb), 1 << 16);
    sc->mastering->white_point[1] = av_make_q(avio_rb16(pb), 1 << 16);

    sc->mastering->max_luminance = av_make_q(avio_rb32(pb), 1 << 8);
    sc->mastering->min_luminance = av_make_q(avio_rb32(pb), 1 << 14);

    sc->mastering->has_primaries = 1;
    sc->mastering->has_luminance = 1;

    return 0;
}

static int mov_read_mdcv(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;
    const int mapping[3] = {1, 2, 0};
    const int chroma_den = 50000;
    const int luma_den = 10000;
    int i;

    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;

    if (atom.size < 24) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid Mastering Display Color Volume box\n");
        return AVERROR_INVALIDDATA;
    }

    if (sc->mastering) {
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicate Mastering Display Color Volume\n");
        return 0;
    }

    sc->mastering = av_mastering_display_metadata_alloc_size(&sc->mastering_size);
    if (!sc->mastering)
        return AVERROR(ENOMEM);

    for (i = 0; i < 3; i++) {
        const int j = mapping[i];
        sc->mastering->display_primaries[j][0] = av_make_q(avio_rb16(pb), chroma_den);
        sc->mastering->display_primaries[j][1] = av_make_q(avio_rb16(pb), chroma_den);
    }
    sc->mastering->white_point[0] = av_make_q(avio_rb16(pb), chroma_den);
    sc->mastering->white_point[1] = av_make_q(avio_rb16(pb), chroma_den);

    sc->mastering->max_luminance = av_make_q(avio_rb32(pb), luma_den);
    sc->mastering->min_luminance = av_make_q(avio_rb32(pb), luma_den);

    sc->mastering->has_luminance = 1;
    sc->mastering->has_primaries = 1;

    return 0;
}

static int mov_read_coll(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;
    int version;

    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;

    if (atom.size < 5) {
        av_log(c->fc, AV_LOG_ERROR, "Empty Content Light Level box\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported Content Light Level box version %d\n", version);
        return 0;
    }
    avio_skip(pb, 3); /* flags */

    if (sc->coll){
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicate COLL\n");
        return 0;
    }

    sc->coll = av_content_light_metadata_alloc(&sc->coll_size);
    if (!sc->coll)
        return AVERROR(ENOMEM);

    sc->coll->MaxCLL  = avio_rb16(pb);
    sc->coll->MaxFALL = avio_rb16(pb);

    return 0;
}

static int mov_read_clli(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;

    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;

    if (atom.size < 4) {
        av_log(c->fc, AV_LOG_ERROR, "Empty Content Light Level Info box\n");
        return AVERROR_INVALIDDATA;
    }

    if (sc->coll){
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicate CLLI/COLL\n");
        return 0;
    }

    sc->coll = av_content_light_metadata_alloc(&sc->coll_size);
    if (!sc->coll)
        return AVERROR(ENOMEM);

    sc->coll->MaxCLL  = avio_rb16(pb);
    sc->coll->MaxFALL = avio_rb16(pb);

    return 0;
}

static int mov_read_amve(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVStreamContext *sc;
    const int illuminance_den = 10000;
    const int ambient_den = 50000;
    if (c->fc->nb_streams < 1)
        return AVERROR_INVALIDDATA;
    sc = c->fc->streams[c->fc->nb_streams - 1]->priv_data;
    if (atom.size < 6) {
        av_log(c->fc, AV_LOG_ERROR, "Empty Ambient Viewing Environment Info box\n");
        return AVERROR_INVALIDDATA;
    }
    if (sc->ambient){
        av_log(c->fc, AV_LOG_WARNING, "Ignoring duplicate AMVE\n");
        return 0;
    }
    sc->ambient = av_ambient_viewing_environment_alloc(&sc->ambient_size);
    if (!sc->ambient)
        return AVERROR(ENOMEM);
    sc->ambient->ambient_illuminance  = av_make_q(avio_rb32(pb), illuminance_den);
    sc->ambient->ambient_light_x = av_make_q(avio_rb16(pb), ambient_den);
    sc->ambient->ambient_light_y = av_make_q(avio_rb16(pb), ambient_den);
    return 0;
}

static int mov_read_st3d(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    enum AVStereo3DType type;
    int mode;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    if (atom.size < 5) {
        av_log(c->fc, AV_LOG_ERROR, "Empty stereoscopic video box\n");
        return AVERROR_INVALIDDATA;
    }

    if (sc->stereo3d)
        return AVERROR_INVALIDDATA;

    avio_skip(pb, 4); /* version + flags */

    mode = avio_r8(pb);
    switch (mode) {
    case 0:
        type = AV_STEREO3D_2D;
        break;
    case 1:
        type = AV_STEREO3D_TOPBOTTOM;
        break;
    case 2:
        type = AV_STEREO3D_SIDEBYSIDE;
        break;
    default:
        av_log(c->fc, AV_LOG_WARNING, "Unknown st3d mode value %d\n", mode);
        return 0;
    }

    sc->stereo3d = av_stereo3d_alloc_size(&sc->stereo3d_size);
    if (!sc->stereo3d)
        return AVERROR(ENOMEM);

    sc->stereo3d->type = type;
    return 0;
}

static int mov_read_sv3d(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int size, version, layout;
    int32_t yaw, pitch, roll;
    uint32_t l = 0, t = 0, r = 0, b = 0;
    uint32_t tag, padding = 0;
    enum AVSphericalProjection projection;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    if (atom.size < 8) {
        av_log(c->fc, AV_LOG_ERROR, "Empty spherical video box\n");
        return AVERROR_INVALIDDATA;
    }

    size = avio_rb32(pb);
    if (size <= 12 || size > atom.size)
        return AVERROR_INVALIDDATA;

    tag = avio_rl32(pb);
    if (tag != MKTAG('s','v','h','d')) {
        av_log(c->fc, AV_LOG_ERROR, "Missing spherical video header\n");
        return 0;
    }
    version = avio_r8(pb);
    if (version != 0) {
        av_log(c->fc, AV_LOG_WARNING, "Unknown spherical version %d\n",
               version);
        return 0;
    }
    avio_skip(pb, 3); /* flags */
    avio_skip(pb, size - 12); /* metadata_source */

    size = avio_rb32(pb);
    if (size > atom.size)
        return AVERROR_INVALIDDATA;

    tag = avio_rl32(pb);
    if (tag != MKTAG('p','r','o','j')) {
        av_log(c->fc, AV_LOG_ERROR, "Missing projection box\n");
        return 0;
    }

    size = avio_rb32(pb);
    if (size > atom.size)
        return AVERROR_INVALIDDATA;

    tag = avio_rl32(pb);
    if (tag != MKTAG('p','r','h','d')) {
        av_log(c->fc, AV_LOG_ERROR, "Missing projection header box\n");
        return 0;
    }
    version = avio_r8(pb);
    if (version != 0) {
        av_log(c->fc, AV_LOG_WARNING, "Unknown spherical version %d\n",
               version);
        return 0;
    }
    avio_skip(pb, 3); /* flags */

    /* 16.16 fixed point */
    yaw   = avio_rb32(pb);
    pitch = avio_rb32(pb);
    roll  = avio_rb32(pb);

    size = avio_rb32(pb);
    if (size > atom.size)
        return AVERROR_INVALIDDATA;

    tag = avio_rl32(pb);
    version = avio_r8(pb);
    if (version != 0) {
        av_log(c->fc, AV_LOG_WARNING, "Unknown spherical version %d\n",
               version);
        return 0;
    }
    avio_skip(pb, 3); /* flags */
    switch (tag) {
    case MKTAG('c','b','m','p'):
        layout = avio_rb32(pb);
        if (layout) {
            av_log(c->fc, AV_LOG_WARNING,
                   "Unsupported cubemap layout %d\n", layout);
            return 0;
        }
        projection = AV_SPHERICAL_CUBEMAP;
        padding = avio_rb32(pb);
        break;
    case MKTAG('e','q','u','i'):
        t = avio_rb32(pb);
        b = avio_rb32(pb);
        l = avio_rb32(pb);
        r = avio_rb32(pb);

        if (b >= UINT_MAX - t || r >= UINT_MAX - l) {
            av_log(c->fc, AV_LOG_ERROR,
                   "Invalid bounding rectangle coordinates "
                   "%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32"\n", l, t, r, b);
            return AVERROR_INVALIDDATA;
        }

        if (l || t || r || b)
            projection = AV_SPHERICAL_EQUIRECTANGULAR_TILE;
        else
            projection = AV_SPHERICAL_EQUIRECTANGULAR;
        break;
    default:
        av_log(c->fc, AV_LOG_ERROR, "Unknown projection type: %s\n", av_fourcc2str(tag));
        return 0;
    }

    sc->spherical = av_spherical_alloc(&sc->spherical_size);
    if (!sc->spherical)
        return AVERROR(ENOMEM);

    sc->spherical->projection = projection;

    sc->spherical->yaw   = yaw;
    sc->spherical->pitch = pitch;
    sc->spherical->roll  = roll;

    sc->spherical->padding = padding;

    sc->spherical->bound_left   = l;
    sc->spherical->bound_top    = t;
    sc->spherical->bound_right  = r;
    sc->spherical->bound_bottom = b;

    return 0;
}

static int mov_read_vexu_proj(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int size;
    uint32_t tag;
    enum AVSphericalProjection projection;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    if (atom.size != 16) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid size for proj box: %"PRIu64"\n", atom.size);
        return AVERROR_INVALIDDATA;
    }

    size = avio_rb32(pb);
    if (size != 16) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid size for prji box: %d\n", size);
        return AVERROR_INVALIDDATA;
    }

    tag = avio_rl32(pb);
    if (tag != MKTAG('p','r','j','i')) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid child box of proj box: 0x%08X\n", tag);
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 1); // version
    avio_skip(pb, 3); // flags

    tag = avio_rl32(pb);
    switch (tag) {
    case MKTAG('r','e','c','t'):
        projection = AV_SPHERICAL_RECTILINEAR;
        break;
    case MKTAG('e','q','u','i'):
        projection = AV_SPHERICAL_EQUIRECTANGULAR;
        break;
    case MKTAG('h','e','q','u'):
        projection = AV_SPHERICAL_HALF_EQUIRECTANGULAR;
        break;
    case MKTAG('f','i','s','h'):
        projection = AV_SPHERICAL_FISHEYE;
        break;
    default:
        av_log(c->fc, AV_LOG_ERROR, "Invalid projection type in prji box: 0x%08X\n", tag);
        return AVERROR_INVALIDDATA;
    }

    sc->spherical = av_spherical_alloc(&sc->spherical_size);
    if (!sc->spherical)
        return AVERROR(ENOMEM);

    sc->spherical->projection = projection;

    return 0;
}

static int mov_read_eyes(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int size, flags = 0;
    int64_t remaining;
    uint32_t tag, baseline = 0;
    enum AVStereo3DView view = AV_STEREO3D_VIEW_UNSPEC;
    enum AVStereo3DType type = AV_STEREO3D_2D;
    enum AVStereo3DPrimaryEye primary_eye = AV_PRIMARY_EYE_NONE;
    AVRational horizontal_disparity_adjustment = { 0, 1 };

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    remaining = atom.size;
    while (remaining > 0) {
        size = avio_rb32(pb);
        if (size < 8 || size > remaining ) {
            av_log(c->fc, AV_LOG_ERROR, "Invalid child size in eyes box\n");
            return AVERROR_INVALIDDATA;
        }

        tag = avio_rl32(pb);
        switch (tag) {
        case MKTAG('s','t','r','i'): {
            int has_right, has_left;
            uint8_t tmp;
            if (size != 13) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of stri box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, 1); // version
            avio_skip(pb, 3); // flags

            tmp = avio_r8(pb);

            // eye_views_reversed
            if (tmp & 8) {
                flags |= AV_STEREO3D_FLAG_INVERT;
            }
            // has_additional_views
            if (tmp & 4) {
                // skip...
            }

            has_right = tmp & 2; // has_right_eye_view
            has_left = tmp & 1; // has_left_eye_view

            if (has_left && has_right)
                view = AV_STEREO3D_VIEW_PACKED;
            else if (has_left)
                view = AV_STEREO3D_VIEW_LEFT;
            else if (has_right)
                view = AV_STEREO3D_VIEW_RIGHT;
            if (has_left || has_right)
                type = AV_STEREO3D_UNSPEC;

            break;
        }
        case MKTAG('h','e','r','o'): {
            int tmp;
            if (size != 13) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of hero box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }
            avio_skip(pb, 1); // version
            avio_skip(pb, 3); // flags

            tmp = avio_r8(pb);
            if (tmp == 0)
                primary_eye = AV_PRIMARY_EYE_NONE;
            else if (tmp == 1)
                primary_eye = AV_PRIMARY_EYE_LEFT;
            else if (tmp == 2)
                primary_eye = AV_PRIMARY_EYE_RIGHT;
            else
                av_log(c->fc, AV_LOG_WARNING, "Unknown hero eye type: %d\n", tmp);

            break;
        }
        case MKTAG('c','a','m','s'): {
            uint32_t subtag;
            int subsize;
            if (size != 24) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of cams box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }

            subsize = avio_rb32(pb);
            if (subsize != 16) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of blin box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }

            subtag = avio_rl32(pb);
            if (subtag != MKTAG('b','l','i','n')) {
                av_log(c->fc, AV_LOG_ERROR, "Expected blin box, got 0x%08X\n", subtag);
                return AVERROR_INVALIDDATA;
            }

            avio_skip(pb, 1); // version
            avio_skip(pb, 3); // flags

            baseline = avio_rb32(pb);

            break;
        }
        case MKTAG('c','m','f','y'): {
            uint32_t subtag;
            int subsize;
            int32_t adjustment;
            if (size != 24) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of cmfy box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }

            subsize = avio_rb32(pb);
            if (subsize != 16) {
                av_log(c->fc, AV_LOG_ERROR, "Invalid size of dadj box: %d\n", size);
                return AVERROR_INVALIDDATA;
            }

            subtag = avio_rl32(pb);
            if (subtag != MKTAG('d','a','d','j')) {
                av_log(c->fc, AV_LOG_ERROR, "Expected dadj box, got 0x%08X\n", subtag);
                return AVERROR_INVALIDDATA;
            }

            avio_skip(pb, 1); // version
            avio_skip(pb, 3); // flags

            adjustment = (int32_t) avio_rb32(pb);

            horizontal_disparity_adjustment.num = (int) adjustment;
            horizontal_disparity_adjustment.den = 10000;

            break;
        }
        default:
            av_log(c->fc, AV_LOG_WARNING, "Unknown tag in eyes: 0x%08X\n", tag);
            avio_skip(pb, size - 8);
            break;
        }
        remaining -= size;
    }

    if (remaining != 0) {
        av_log(c->fc, AV_LOG_ERROR, "Broken eyes box\n");
        return AVERROR_INVALIDDATA;
    }

    if (type == AV_STEREO3D_2D)
        return 0;

    if (!sc->stereo3d) {
        sc->stereo3d = av_stereo3d_alloc_size(&sc->stereo3d_size);
        if (!sc->stereo3d)
            return AVERROR(ENOMEM);
    }

    sc->stereo3d->flags                           = flags;
    sc->stereo3d->type                            = type;
    sc->stereo3d->view                            = view;
    sc->stereo3d->primary_eye                     = primary_eye;
    sc->stereo3d->baseline                        = baseline;
    sc->stereo3d->horizontal_disparity_adjustment = horizontal_disparity_adjustment;

    return 0;
}

static int mov_read_vexu(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int size;
    int64_t remaining;
    uint32_t tag;

    if (c->fc->nb_streams < 1)
        return 0;

    if (atom.size < 8) {
        av_log(c->fc, AV_LOG_ERROR, "Empty video extension usage box\n");
        return AVERROR_INVALIDDATA;
    }

    remaining = atom.size;
    while (remaining > 0) {
        size = avio_rb32(pb);
        if (size < 8 || size > remaining ) {
            av_log(c->fc, AV_LOG_ERROR, "Invalid child size in vexu box\n");
            return AVERROR_INVALIDDATA;
        }

        tag = avio_rl32(pb);
        switch (tag) {
        case MKTAG('p','r','o','j'): {
            MOVAtom proj = { tag, size - 8 };
            int ret = mov_read_vexu_proj(c, pb, proj);
            if (ret < 0)
                return ret;
            break;
        }
        case MKTAG('e','y','e','s'): {
            MOVAtom eyes = { tag, size - 8 };
            int ret = mov_read_eyes(c, pb, eyes);
            if (ret < 0)
                return ret;
            break;
        }
        default:
            av_log(c->fc, AV_LOG_WARNING, "Unknown tag in vexu: 0x%08X\n", tag);
            avio_skip(pb, size - 8);
            break;
        }
        remaining -= size;
    }

    if (remaining != 0) {
        av_log(c->fc, AV_LOG_ERROR, "Broken vexu box\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int mov_read_hfov(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    if (atom.size != 4) {
         av_log(c->fc, AV_LOG_ERROR, "Invalid size of hfov box: %"PRIu64"\n", atom.size);
         return AVERROR_INVALIDDATA;
    }


    if (!sc->stereo3d) {
        sc->stereo3d = av_stereo3d_alloc_size(&sc->stereo3d_size);
        if (!sc->stereo3d)
            return AVERROR(ENOMEM);
    }

    sc->stereo3d->horizontal_field_of_view.num = avio_rb32(pb);
    sc->stereo3d->horizontal_field_of_view.den = 1000; // thousands of a degree

    return 0;
}

static int mov_parse_uuid_spherical(MOVStreamContext *sc, AVIOContext *pb, size_t len)
{
    int ret = 0;
    uint8_t *buffer = av_malloc(len + 1);
    const char *val;

    if (!buffer)
        return AVERROR(ENOMEM);
    buffer[len] = '\0';

    ret = ffio_read_size(pb, buffer, len);
    if (ret < 0)
        goto out;

    /* Check for mandatory keys and values, try to support XML as best-effort */
    if (!sc->spherical &&
        av_stristr(buffer, "<GSpherical:StitchingSoftware>") &&
        (val = av_stristr(buffer, "<GSpherical:Spherical>")) &&
        av_stristr(val, "true") &&
        (val = av_stristr(buffer, "<GSpherical:Stitched>")) &&
        av_stristr(val, "true") &&
        (val = av_stristr(buffer, "<GSpherical:ProjectionType>")) &&
        av_stristr(val, "equirectangular")) {
        sc->spherical = av_spherical_alloc(&sc->spherical_size);
        if (!sc->spherical)
            goto out;

        sc->spherical->projection = AV_SPHERICAL_EQUIRECTANGULAR;

        if (av_stristr(buffer, "<GSpherical:StereoMode>") && !sc->stereo3d) {
            enum AVStereo3DType mode;

            if (av_stristr(buffer, "left-right"))
                mode = AV_STEREO3D_SIDEBYSIDE;
            else if (av_stristr(buffer, "top-bottom"))
                mode = AV_STEREO3D_TOPBOTTOM;
            else
                mode = AV_STEREO3D_2D;

            sc->stereo3d = av_stereo3d_alloc_size(&sc->stereo3d_size);
            if (!sc->stereo3d)
                goto out;

            sc->stereo3d->type = mode;
        }

        /* orientation */
        val = av_stristr(buffer, "<GSpherical:InitialViewHeadingDegrees>");
        if (val)
            sc->spherical->yaw = strtol(val, NULL, 10) * (1 << 16);
        val = av_stristr(buffer, "<GSpherical:InitialViewPitchDegrees>");
        if (val)
            sc->spherical->pitch = strtol(val, NULL, 10) * (1 << 16);
        val = av_stristr(buffer, "<GSpherical:InitialViewRollDegrees>");
        if (val)
            sc->spherical->roll = strtol(val, NULL, 10) * (1 << 16);
    }

out:
    av_free(buffer);
    return ret;
}

static int mov_read_uuid(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int64_t ret;
    AVUUID uuid;
    static const AVUUID uuid_isml_manifest = {
        0xa5, 0xd4, 0x0b, 0x30, 0xe8, 0x14, 0x11, 0xdd,
        0xba, 0x2f, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66
    };
    static const AVUUID uuid_xmp = {
        0xbe, 0x7a, 0xcf, 0xcb, 0x97, 0xa9, 0x42, 0xe8,
        0x9c, 0x71, 0x99, 0x94, 0x91, 0xe3, 0xaf, 0xac
    };
    static const AVUUID uuid_spherical = {
        0xff, 0xcc, 0x82, 0x63, 0xf8, 0x55, 0x4a, 0x93,
        0x88, 0x14, 0x58, 0x7a, 0x02, 0x52, 0x1f, 0xdd,
    };

    if (atom.size < AV_UUID_LEN || atom.size >= FFMIN(INT_MAX, SIZE_MAX))
        return AVERROR_INVALIDDATA;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    ret = ffio_read_size(pb, uuid, AV_UUID_LEN);
    if (ret < 0)
        return ret;
    if (av_uuid_equal(uuid, uuid_isml_manifest)) {
        uint8_t *buffer, *ptr;
        char *endptr;
        size_t len = atom.size - AV_UUID_LEN;

        if (len < 4) {
            return AVERROR_INVALIDDATA;
        }
        ret = avio_skip(pb, 4); // zeroes
        len -= 4;

        buffer = av_mallocz(len + 1);
        if (!buffer) {
            return AVERROR(ENOMEM);
        }
        ret = ffio_read_size(pb, buffer, len);
        if (ret < 0) {
            av_free(buffer);
            return ret;
        }

        ptr = buffer;
        while ((ptr = av_stristr(ptr, "systemBitrate=\""))) {
            ptr += sizeof("systemBitrate=\"") - 1;
            c->bitrates_count++;
            c->bitrates = av_realloc_f(c->bitrates, c->bitrates_count, sizeof(*c->bitrates));
            if (!c->bitrates) {
                c->bitrates_count = 0;
                av_free(buffer);
                return AVERROR(ENOMEM);
            }
            errno = 0;
            ret = strtol(ptr, &endptr, 10);
            if (ret < 0 || errno || *endptr != '"') {
                c->bitrates[c->bitrates_count - 1] = 0;
            } else {
                c->bitrates[c->bitrates_count - 1] = ret;
            }
        }

        av_free(buffer);
    } else if (av_uuid_equal(uuid, uuid_xmp)) {
        uint8_t *buffer;
        size_t len = atom.size - AV_UUID_LEN;
        if (c->export_xmp) {
            buffer = av_mallocz(len + 1);
            if (!buffer) {
                return AVERROR(ENOMEM);
            }
            ret = ffio_read_size(pb, buffer, len);
            if (ret < 0) {
                av_free(buffer);
                return ret;
            }
            buffer[len] = '\0';
            av_dict_set(&c->fc->metadata, "xmp",
                        buffer, AV_DICT_DONT_STRDUP_VAL);
        } else {
            // skip all uuid atom, which makes it fast for long uuid-xmp file
            ret = avio_skip(pb, len);
            if (ret < 0)
                return ret;
        }
    } else if (av_uuid_equal(uuid, uuid_spherical)) {
        size_t len = atom.size - AV_UUID_LEN;
        ret = mov_parse_uuid_spherical(sc, pb, len);
        if (ret < 0)
            return ret;
        if (!sc->spherical)
            av_log(c->fc, AV_LOG_WARNING, "Invalid spherical metadata found\n");
    }

    return 0;
}

static int mov_read_free(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret;
    uint8_t content[16];

    if (atom.size < 8)
        return 0;

    ret = ffio_read_size(pb, content, FFMIN(sizeof(content), atom.size));
    if (ret < 0)
        return ret;

    if (   !c->found_moov
        && !c->found_mdat
        && !memcmp(content, "Anevia\x1A\x1A", 8)
        && c->use_mfra_for == FF_MOV_FLAG_MFRA_AUTO) {
        c->use_mfra_for = FF_MOV_FLAG_MFRA_PTS;
    }

    return 0;
}

static int mov_read_frma(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    uint32_t format = avio_rl32(pb);
    MOVStreamContext *sc;
    enum AVCodecID id;
    AVStream *st;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];
    sc = st->priv_data;

    switch (sc->format)
    {
    case MKTAG('e','n','c','v'):        // encrypted video
    case MKTAG('e','n','c','a'):        // encrypted audio
        id = mov_codec_id(st, format);
        if (st->codecpar->codec_id != AV_CODEC_ID_NONE &&
            st->codecpar->codec_id != id) {
            av_log(c->fc, AV_LOG_WARNING,
                   "ignoring 'frma' atom of '%.4s', stream has codec id %d\n",
                   (char*)&format, st->codecpar->codec_id);
            break;
        }

        st->codecpar->codec_id = id;
        sc->format = format;
        break;

    default:
        if (format != sc->format) {
            av_log(c->fc, AV_LOG_WARNING,
                   "ignoring 'frma' atom of '%.4s', stream format is '%.4s'\n",
                   (char*)&format, (char*)&sc->format);
        }
        break;
    }

    return 0;
}

/**
 * Gets the current encryption info and associated current stream context.  If
 * we are parsing a track fragment, this will return the specific encryption
 * info for this fragment; otherwise this will return the global encryption
 * info for the current stream.
 */
static int get_current_encryption_info(MOVContext *c, MOVEncryptionIndex **encryption_index, MOVStreamContext **sc)
{
    MOVFragmentStreamInfo *frag_stream_info;
    AVStream *st;
    int i;

    frag_stream_info = get_current_frag_stream_info(&c->frag_index);
    if (frag_stream_info) {
        for (i = 0; i < c->fc->nb_streams; i++) {
            *sc = c->fc->streams[i]->priv_data;
            if ((*sc)->id == frag_stream_info->id) {
              st = c->fc->streams[i];
              break;
            }
        }
        if (i == c->fc->nb_streams)
            return 0;
        *sc = st->priv_data;

        if (!frag_stream_info->encryption_index) {
            // If this stream isn't encrypted, don't create the index.
            if (!(*sc)->cenc.default_encrypted_sample)
                return 0;
            frag_stream_info->encryption_index = av_mallocz(sizeof(*frag_stream_info->encryption_index));
            if (!frag_stream_info->encryption_index)
                return AVERROR(ENOMEM);
        }
        *encryption_index = frag_stream_info->encryption_index;
        return 1;
    } else {
        // No current track fragment, using stream level encryption info.

        if (c->fc->nb_streams < 1)
            return 0;
        st = c->fc->streams[c->fc->nb_streams - 1];
        *sc = st->priv_data;

        if (!(*sc)->cenc.encryption_index) {
            // If this stream isn't encrypted, don't create the index.
            if (!(*sc)->cenc.default_encrypted_sample)
                return 0;
            (*sc)->cenc.encryption_index = av_mallocz(sizeof(*frag_stream_info->encryption_index));
            if (!(*sc)->cenc.encryption_index)
                return AVERROR(ENOMEM);
        }

        *encryption_index = (*sc)->cenc.encryption_index;
        return 1;
    }
}

static int mov_read_sample_encryption_info(MOVContext *c, AVIOContext *pb, MOVStreamContext *sc, AVEncryptionInfo **sample, int use_subsamples)
{
    int i, ret;
    unsigned int subsample_count;
    AVSubsampleEncryptionInfo *subsamples;

    if (!sc->cenc.default_encrypted_sample) {
        av_log(c->fc, AV_LOG_ERROR, "Missing schm or tenc\n");
        return AVERROR_INVALIDDATA;
    }

    if (sc->cenc.per_sample_iv_size || use_subsamples) {
        *sample = av_encryption_info_clone(sc->cenc.default_encrypted_sample);
        if (!*sample)
            return AVERROR(ENOMEM);
    } else
        *sample = NULL;

    if (sc->cenc.per_sample_iv_size != 0) {
        if ((ret = ffio_read_size(pb, (*sample)->iv, sc->cenc.per_sample_iv_size)) < 0) {
            av_log(c->fc, AV_LOG_ERROR, "failed to read the initialization vector\n");
            av_encryption_info_free(*sample);
            *sample = NULL;
            return ret;
        }
    }

    if (use_subsamples) {
        subsample_count = avio_rb16(pb);
        av_free((*sample)->subsamples);
        (*sample)->subsamples = av_calloc(subsample_count, sizeof(*subsamples));
        if (!(*sample)->subsamples) {
            av_encryption_info_free(*sample);
            *sample = NULL;
            return AVERROR(ENOMEM);
        }

        for (i = 0; i < subsample_count && !pb->eof_reached; i++) {
            (*sample)->subsamples[i].bytes_of_clear_data = avio_rb16(pb);
            (*sample)->subsamples[i].bytes_of_protected_data = avio_rb32(pb);
        }

        if (pb->eof_reached) {
            av_log(c->fc, AV_LOG_ERROR, "hit EOF while reading sub-sample encryption info\n");
            av_encryption_info_free(*sample);
            *sample = NULL;
            return AVERROR_INVALIDDATA;
        }
        (*sample)->subsample_count = subsample_count;
    }

    return 0;
}

static int mov_read_senc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVEncryptionInfo **encrypted_samples;
    MOVEncryptionIndex *encryption_index;
    MOVStreamContext *sc;
    int use_subsamples, ret;
    unsigned int sample_count, i, alloc_size = 0;

    ret = get_current_encryption_info(c, &encryption_index, &sc);
    if (ret != 1)
        return ret;

    if (encryption_index->nb_encrypted_samples) {
        // This can happen if we have both saio/saiz and senc atoms.
        av_log(c->fc, AV_LOG_DEBUG, "Ignoring duplicate encryption info in senc\n");
        return 0;
    }

    avio_r8(pb); /* version */
    use_subsamples = avio_rb24(pb) & 0x02; /* flags */

    sample_count = avio_rb32(pb);
    if (sample_count >= INT_MAX / sizeof(*encrypted_samples))
        return AVERROR(ENOMEM);

    for (i = 0; i < sample_count; i++) {
        unsigned int min_samples = FFMIN(FFMAX(i + 1, 1024 * 1024), sample_count);
        encrypted_samples = av_fast_realloc(encryption_index->encrypted_samples, &alloc_size,
                                            min_samples * sizeof(*encrypted_samples));
        if (encrypted_samples) {
            encryption_index->encrypted_samples = encrypted_samples;

            ret = mov_read_sample_encryption_info(
                c, pb, sc, &encryption_index->encrypted_samples[i], use_subsamples);
        } else {
            ret = AVERROR(ENOMEM);
        }
        if (pb->eof_reached) {
            av_log(c->fc, AV_LOG_ERROR, "Hit EOF while reading senc\n");
            if (ret >= 0)
                av_encryption_info_free(encryption_index->encrypted_samples[i]);
            ret = AVERROR_INVALIDDATA;
        }

        if (ret < 0) {
            for (; i > 0; i--)
                av_encryption_info_free(encryption_index->encrypted_samples[i - 1]);
            av_freep(&encryption_index->encrypted_samples);
            return ret;
        }
    }
    encryption_index->nb_encrypted_samples = sample_count;

    return 0;
}

static int mov_parse_auxiliary_info(MOVContext *c, MOVStreamContext *sc, AVIOContext *pb, MOVEncryptionIndex *encryption_index)
{
    AVEncryptionInfo **sample, **encrypted_samples;
    int64_t prev_pos;
    size_t sample_count, sample_info_size, i;
    int ret = 0;
    unsigned int alloc_size = 0;

    if (encryption_index->nb_encrypted_samples)
        return 0;
    sample_count = encryption_index->auxiliary_info_sample_count;
    if (encryption_index->auxiliary_offsets_count != 1) {
        av_log(c->fc, AV_LOG_ERROR, "Multiple auxiliary info chunks are not supported\n");
        return AVERROR_PATCHWELCOME;
    }
    if (sample_count >= INT_MAX / sizeof(*encrypted_samples))
        return AVERROR(ENOMEM);

    prev_pos = avio_tell(pb);
    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL) ||
        avio_seek(pb, encryption_index->auxiliary_offsets[0], SEEK_SET) != encryption_index->auxiliary_offsets[0]) {
        av_log(c->fc, AV_LOG_INFO, "Failed to seek for auxiliary info, will only parse senc atoms for encryption info\n");
        goto finish;
    }

    for (i = 0; i < sample_count && !pb->eof_reached; i++) {
        unsigned int min_samples = FFMIN(FFMAX(i + 1, 1024 * 1024), sample_count);
        encrypted_samples = av_fast_realloc(encryption_index->encrypted_samples, &alloc_size,
                                            min_samples * sizeof(*encrypted_samples));
        if (!encrypted_samples) {
            ret = AVERROR(ENOMEM);
            goto finish;
        }
        encryption_index->encrypted_samples = encrypted_samples;

        sample = &encryption_index->encrypted_samples[i];
        sample_info_size = encryption_index->auxiliary_info_default_size
                               ? encryption_index->auxiliary_info_default_size
                               : encryption_index->auxiliary_info_sizes[i];

        ret = mov_read_sample_encryption_info(c, pb, sc, sample, sample_info_size > sc->cenc.per_sample_iv_size);
        if (ret < 0)
            goto finish;
    }
    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_ERROR, "Hit EOF while reading auxiliary info\n");
        ret = AVERROR_INVALIDDATA;
    } else {
        encryption_index->nb_encrypted_samples = sample_count;
    }

finish:
    avio_seek(pb, prev_pos, SEEK_SET);
    if (ret < 0) {
        for (; i > 0; i--) {
            av_encryption_info_free(encryption_index->encrypted_samples[i - 1]);
        }
        av_freep(&encryption_index->encrypted_samples);
    }
    return ret;
}

static int mov_read_saiz(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVEncryptionIndex *encryption_index;
    MOVStreamContext *sc;
    int ret;
    unsigned int sample_count, aux_info_type, aux_info_param;

    ret = get_current_encryption_info(c, &encryption_index, &sc);
    if (ret != 1)
        return ret;

    if (encryption_index->nb_encrypted_samples) {
        // This can happen if we have both saio/saiz and senc atoms.
        av_log(c->fc, AV_LOG_DEBUG, "Ignoring duplicate encryption info in saiz\n");
        return 0;
    }

    if (encryption_index->auxiliary_info_sample_count) {
        av_log(c->fc, AV_LOG_ERROR, "Duplicate saiz atom\n");
        return AVERROR_INVALIDDATA;
    }

    avio_r8(pb); /* version */
    if (avio_rb24(pb) & 0x01) {  /* flags */
        aux_info_type = avio_rb32(pb);
        aux_info_param = avio_rb32(pb);
        if (sc->cenc.default_encrypted_sample) {
            if (aux_info_type != sc->cenc.default_encrypted_sample->scheme) {
                av_log(c->fc, AV_LOG_DEBUG, "Ignoring saiz box with non-zero aux_info_type\n");
                return 0;
            }
            if (aux_info_param != 0) {
                av_log(c->fc, AV_LOG_DEBUG, "Ignoring saiz box with non-zero aux_info_type_parameter\n");
                return 0;
            }
        } else {
            // Didn't see 'schm' or 'tenc', so this isn't encrypted.
            if ((aux_info_type == MKBETAG('c','e','n','c') ||
                 aux_info_type == MKBETAG('c','e','n','s') ||
                 aux_info_type == MKBETAG('c','b','c','1') ||
                 aux_info_type == MKBETAG('c','b','c','s')) &&
                aux_info_param == 0) {
                av_log(c->fc, AV_LOG_ERROR, "Saw encrypted saiz without schm/tenc\n");
                return AVERROR_INVALIDDATA;
            } else {
                return 0;
            }
        }
    } else if (!sc->cenc.default_encrypted_sample) {
        // Didn't see 'schm' or 'tenc', so this isn't encrypted.
        return 0;
    }

    encryption_index->auxiliary_info_default_size = avio_r8(pb);
    sample_count = avio_rb32(pb);

    if (encryption_index->auxiliary_info_default_size == 0) {
        if (sample_count == 0)
            return AVERROR_INVALIDDATA;

        encryption_index->auxiliary_info_sizes = av_malloc(sample_count);
        if (!encryption_index->auxiliary_info_sizes)
            return AVERROR(ENOMEM);

        ret = avio_read(pb, encryption_index->auxiliary_info_sizes, sample_count);
        if (ret != sample_count) {
            av_freep(&encryption_index->auxiliary_info_sizes);

            if (ret >= 0)
                ret = AVERROR_INVALIDDATA;
            av_log(c->fc, AV_LOG_ERROR, "Failed to read the auxiliary info, %s\n",
                   av_err2str(ret));
            return ret;
        }
    }
    encryption_index->auxiliary_info_sample_count = sample_count;

    if (encryption_index->auxiliary_offsets_count) {
        return mov_parse_auxiliary_info(c, sc, pb, encryption_index);
    }

    return 0;
}

static int mov_read_saio(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    uint64_t *auxiliary_offsets;
    MOVEncryptionIndex *encryption_index;
    MOVStreamContext *sc;
    int i, ret;
    unsigned int version, entry_count, aux_info_type, aux_info_param;
    unsigned int alloc_size = 0;

    ret = get_current_encryption_info(c, &encryption_index, &sc);
    if (ret != 1)
        return ret;

    if (encryption_index->nb_encrypted_samples) {
        // This can happen if we have both saio/saiz and senc atoms.
        av_log(c->fc, AV_LOG_DEBUG, "Ignoring duplicate encryption info in saio\n");
        return 0;
    }

    if (encryption_index->auxiliary_offsets_count) {
        av_log(c->fc, AV_LOG_ERROR, "Duplicate saio atom\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb); /* version */
    if (avio_rb24(pb) & 0x01) {  /* flags */
        aux_info_type = avio_rb32(pb);
        aux_info_param = avio_rb32(pb);
        if (sc->cenc.default_encrypted_sample) {
            if (aux_info_type != sc->cenc.default_encrypted_sample->scheme) {
                av_log(c->fc, AV_LOG_DEBUG, "Ignoring saio box with non-zero aux_info_type\n");
                return 0;
            }
            if (aux_info_param != 0) {
                av_log(c->fc, AV_LOG_DEBUG, "Ignoring saio box with non-zero aux_info_type_parameter\n");
                return 0;
            }
        } else {
            // Didn't see 'schm' or 'tenc', so this isn't encrypted.
            if ((aux_info_type == MKBETAG('c','e','n','c') ||
                 aux_info_type == MKBETAG('c','e','n','s') ||
                 aux_info_type == MKBETAG('c','b','c','1') ||
                 aux_info_type == MKBETAG('c','b','c','s')) &&
                aux_info_param == 0) {
                av_log(c->fc, AV_LOG_ERROR, "Saw encrypted saio without schm/tenc\n");
                return AVERROR_INVALIDDATA;
            } else {
                return 0;
            }
        }
    } else if (!sc->cenc.default_encrypted_sample) {
        // Didn't see 'schm' or 'tenc', so this isn't encrypted.
        return 0;
    }

    entry_count = avio_rb32(pb);
    if (entry_count >= INT_MAX / sizeof(*auxiliary_offsets))
        return AVERROR(ENOMEM);

    for (i = 0; i < entry_count && !pb->eof_reached; i++) {
        unsigned int min_offsets = FFMIN(FFMAX(i + 1, 1024), entry_count);
        auxiliary_offsets = av_fast_realloc(
            encryption_index->auxiliary_offsets, &alloc_size,
            min_offsets * sizeof(*auxiliary_offsets));
        if (!auxiliary_offsets) {
            av_freep(&encryption_index->auxiliary_offsets);
            return AVERROR(ENOMEM);
        }
        encryption_index->auxiliary_offsets = auxiliary_offsets;

        if (version == 0) {
            encryption_index->auxiliary_offsets[i] = avio_rb32(pb);
        } else {
            encryption_index->auxiliary_offsets[i] = avio_rb64(pb);
        }
        if (c->frag_index.current >= 0) {
            encryption_index->auxiliary_offsets[i] += c->fragment.base_data_offset;
        }
    }

    if (pb->eof_reached) {
        av_log(c->fc, AV_LOG_ERROR, "Hit EOF while reading saio\n");
        av_freep(&encryption_index->auxiliary_offsets);
        return AVERROR_INVALIDDATA;
    }

    encryption_index->auxiliary_offsets_count = entry_count;

    if (encryption_index->auxiliary_info_sample_count) {
        return mov_parse_auxiliary_info(c, sc, pb, encryption_index);
    }

    return 0;
}

static int mov_read_pssh(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVEncryptionInitInfo *info, *old_init_info;
    uint8_t **key_ids;
    AVStream *st;
    const AVPacketSideData *old_side_data;
    uint8_t *side_data, *extra_data;
    size_t side_data_size;
    int ret = 0;
    unsigned int version, kid_count, extra_data_size, alloc_size = 0;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    version = avio_r8(pb); /* version */
    avio_rb24(pb);  /* flags */

    info = av_encryption_init_info_alloc(/* system_id_size */ 16, /* num_key_ids */ 0,
                                         /* key_id_size */ 16, /* data_size */ 0);
    if (!info)
        return AVERROR(ENOMEM);

    if ((ret = ffio_read_size(pb, info->system_id, 16)) < 0) {
        av_log(c->fc, AV_LOG_ERROR, "Failed to read the system id\n");
        goto finish;
    }

    if (version > 0) {
        kid_count = avio_rb32(pb);
        if (kid_count >= INT_MAX / sizeof(*key_ids)) {
            ret = AVERROR(ENOMEM);
            goto finish;
        }

        for (unsigned int i = 0; i < kid_count && !pb->eof_reached; i++) {
            unsigned int min_kid_count = FFMIN(FFMAX(i + 1, 1024), kid_count);
            key_ids = av_fast_realloc(info->key_ids, &alloc_size,
                                      min_kid_count * sizeof(*key_ids));
            if (!key_ids) {
                ret = AVERROR(ENOMEM);
                goto finish;
            }
            info->key_ids = key_ids;

            info->key_ids[i] = av_mallocz(16);
            if (!info->key_ids[i]) {
                ret = AVERROR(ENOMEM);
                goto finish;
            }
            info->num_key_ids = i + 1;

            if ((ret = ffio_read_size(pb, info->key_ids[i], 16)) < 0) {
                av_log(c->fc, AV_LOG_ERROR, "Failed to read the key id\n");
                goto finish;
            }
        }

        if (pb->eof_reached) {
            av_log(c->fc, AV_LOG_ERROR, "Hit EOF while reading pssh\n");
            ret = AVERROR_INVALIDDATA;
            goto finish;
        }
    }

    extra_data_size = avio_rb32(pb);
    extra_data = av_malloc(extra_data_size);
    if (!extra_data) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }
    ret = avio_read(pb, extra_data, extra_data_size);
    if (ret != extra_data_size) {
        av_free(extra_data);

        if (ret >= 0)
            ret = AVERROR_INVALIDDATA;
        goto finish;
    }

    av_freep(&info->data);  // malloc(0) may still allocate something.
    info->data = extra_data;
    info->data_size = extra_data_size;

    // If there is existing initialization data, append to the list.
    old_side_data = av_packet_side_data_get(st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data,
                                            AV_PKT_DATA_ENCRYPTION_INIT_INFO);
    if (old_side_data) {
        old_init_info = av_encryption_init_info_get_side_data(old_side_data->data, old_side_data->size);
        if (old_init_info) {
            // Append to the end of the list.
            for (AVEncryptionInitInfo *cur = old_init_info;; cur = cur->next) {
                if (!cur->next) {
                    cur->next = info;
                    break;
                }
            }
            info = old_init_info;
        } else {
            // Assume existing side-data will be valid, so the only error we could get is OOM.
            ret = AVERROR(ENOMEM);
            goto finish;
        }
    }

    side_data = av_encryption_init_info_add_side_data(info, &side_data_size);
    if (!side_data) {
        ret = AVERROR(ENOMEM);
        goto finish;
    }
    if (!av_packet_side_data_add(&st->codecpar->coded_side_data,
                                 &st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_ENCRYPTION_INIT_INFO,
                                 side_data, side_data_size, 0))
        av_free(side_data);

finish:
    av_encryption_init_info_free(info);
    return ret;
}

static int mov_read_schm(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    if (sc->pseudo_stream_id != 0) {
        av_log(c->fc, AV_LOG_ERROR, "schm boxes are only supported in first sample descriptor\n");
        return AVERROR_PATCHWELCOME;
    }

    if (atom.size < 8)
        return AVERROR_INVALIDDATA;

    avio_rb32(pb); /* version and flags */

    if (!sc->cenc.default_encrypted_sample) {
        sc->cenc.default_encrypted_sample = av_encryption_info_alloc(0, 16, 16);
        if (!sc->cenc.default_encrypted_sample) {
            return AVERROR(ENOMEM);
        }
    }

    sc->cenc.default_encrypted_sample->scheme = avio_rb32(pb);
    return 0;
}

static int mov_read_tenc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int version, pattern, is_protected, iv_size;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    if (sc->pseudo_stream_id != 0) {
        av_log(c->fc, AV_LOG_ERROR, "tenc atom are only supported in first sample descriptor\n");
        return AVERROR_PATCHWELCOME;
    }

    if (!sc->cenc.default_encrypted_sample) {
        sc->cenc.default_encrypted_sample = av_encryption_info_alloc(0, 16, 16);
        if (!sc->cenc.default_encrypted_sample) {
            return AVERROR(ENOMEM);
        }
    }

    if (atom.size < 20)
        return AVERROR_INVALIDDATA;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    avio_r8(pb); /* reserved */
    pattern = avio_r8(pb);

    if (version > 0) {
        sc->cenc.default_encrypted_sample->crypt_byte_block = pattern >> 4;
        sc->cenc.default_encrypted_sample->skip_byte_block = pattern & 0xf;
    }

    is_protected = avio_r8(pb);
    if (is_protected && !sc->cenc.encryption_index) {
        // The whole stream should be by-default encrypted.
        sc->cenc.encryption_index = av_mallocz(sizeof(MOVEncryptionIndex));
        if (!sc->cenc.encryption_index)
            return AVERROR(ENOMEM);
    }
    sc->cenc.per_sample_iv_size = avio_r8(pb);
    if (sc->cenc.per_sample_iv_size != 0 && sc->cenc.per_sample_iv_size != 8 &&
        sc->cenc.per_sample_iv_size != 16) {
        av_log(c->fc, AV_LOG_ERROR, "invalid per-sample IV size value\n");
        return AVERROR_INVALIDDATA;
    }
    if (avio_read(pb, sc->cenc.default_encrypted_sample->key_id, 16) != 16) {
        av_log(c->fc, AV_LOG_ERROR, "failed to read the default key ID\n");
        return AVERROR_INVALIDDATA;
    }

    if (is_protected && !sc->cenc.per_sample_iv_size) {
        iv_size = avio_r8(pb);
        if (iv_size != 8 && iv_size != 16) {
            av_log(c->fc, AV_LOG_ERROR, "invalid default_constant_IV_size in tenc atom\n");
            return AVERROR_INVALIDDATA;
        }

        if (avio_read(pb, sc->cenc.default_encrypted_sample->iv, iv_size) != iv_size) {
            av_log(c->fc, AV_LOG_ERROR, "failed to read the default IV\n");
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int mov_read_dfla(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int last, type, size, ret;
    uint8_t buf[4];

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((uint64_t)atom.size > (1<<30) || atom.size < 42)
        return AVERROR_INVALIDDATA;

    /* Check FlacSpecificBox version. */
    if (avio_r8(pb) != 0)
        return AVERROR_INVALIDDATA;

    avio_rb24(pb); /* Flags */

    if (avio_read(pb, buf, sizeof(buf)) != sizeof(buf)) {
        av_log(c->fc, AV_LOG_ERROR, "failed to read FLAC metadata block header\n");
        return pb->error < 0 ? pb->error : AVERROR_INVALIDDATA;
    }
    flac_parse_block_header(buf, &last, &type, &size);

    if (type != FLAC_METADATA_TYPE_STREAMINFO || size != FLAC_STREAMINFO_SIZE) {
        av_log(c->fc, AV_LOG_ERROR, "STREAMINFO must be first FLACMetadataBlock\n");
        return AVERROR_INVALIDDATA;
    }

    ret = ff_get_extradata(c->fc, st->codecpar, pb, size);
    if (ret < 0)
        return ret;

    if (!last)
        av_log(c->fc, AV_LOG_WARNING, "non-STREAMINFO FLACMetadataBlock(s) ignored\n");

    return 0;
}

static int cenc_scheme_decrypt(MOVContext *c, MOVStreamContext *sc, AVEncryptionInfo *sample, uint8_t *input, int size)
{
    int i, ret;
    int bytes_of_protected_data;

    if (!sc->cenc.aes_ctr) {
        /* initialize the cipher */
        sc->cenc.aes_ctr = av_aes_ctr_alloc();
        if (!sc->cenc.aes_ctr) {
            return AVERROR(ENOMEM);
        }

        ret = av_aes_ctr_init(sc->cenc.aes_ctr, c->decryption_key);
        if (ret < 0) {
            return ret;
        }
    }

    av_aes_ctr_set_full_iv(sc->cenc.aes_ctr, sample->iv);

    if (!sample->subsample_count) {
        /* decrypt the whole packet */
        av_aes_ctr_crypt(sc->cenc.aes_ctr, input, input, size);
        return 0;
    }

    for (i = 0; i < sample->subsample_count; i++) {
        if (sample->subsamples[i].bytes_of_clear_data + sample->subsamples[i].bytes_of_protected_data > size) {
            av_log(c->fc, AV_LOG_ERROR, "subsample size exceeds the packet size left\n");
            return AVERROR_INVALIDDATA;
        }

        /* skip the clear bytes */
        input += sample->subsamples[i].bytes_of_clear_data;
        size -= sample->subsamples[i].bytes_of_clear_data;

        /* decrypt the encrypted bytes */

        bytes_of_protected_data = sample->subsamples[i].bytes_of_protected_data;
        av_aes_ctr_crypt(sc->cenc.aes_ctr, input, input, bytes_of_protected_data);

        input += bytes_of_protected_data;
        size -= bytes_of_protected_data;
    }

    if (size > 0) {
        av_log(c->fc, AV_LOG_ERROR, "leftover packet bytes after subsample processing\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int cbc1_scheme_decrypt(MOVContext *c, MOVStreamContext *sc, AVEncryptionInfo *sample, uint8_t *input, int size)
{
    int i, ret;
    int num_of_encrypted_blocks;
    uint8_t iv[16];

    if (!sc->cenc.aes_ctx) {
        /* initialize the cipher */
        sc->cenc.aes_ctx = av_aes_alloc();
        if (!sc->cenc.aes_ctx) {
            return AVERROR(ENOMEM);
        }

        ret = av_aes_init(sc->cenc.aes_ctx, c->decryption_key, 16 * 8, 1);
        if (ret < 0) {
            return ret;
        }
    }

    memcpy(iv, sample->iv, 16);

    /* whole-block full sample encryption */
    if (!sample->subsample_count) {
        /* decrypt the whole packet */
        av_aes_crypt(sc->cenc.aes_ctx, input, input, size/16, iv, 1);
        return 0;
    }

    for (i = 0; i < sample->subsample_count; i++) {
        if (sample->subsamples[i].bytes_of_clear_data + sample->subsamples[i].bytes_of_protected_data > size) {
            av_log(c->fc, AV_LOG_ERROR, "subsample size exceeds the packet size left\n");
            return AVERROR_INVALIDDATA;
        }

        if (sample->subsamples[i].bytes_of_protected_data % 16) {
            av_log(c->fc, AV_LOG_ERROR, "subsample BytesOfProtectedData is not a multiple of 16\n");
            return AVERROR_INVALIDDATA;
        }

        /* skip the clear bytes */
        input += sample->subsamples[i].bytes_of_clear_data;
        size -= sample->subsamples[i].bytes_of_clear_data;

        /* decrypt the encrypted bytes */
        num_of_encrypted_blocks = sample->subsamples[i].bytes_of_protected_data/16;
        if (num_of_encrypted_blocks > 0) {
            av_aes_crypt(sc->cenc.aes_ctx, input, input, num_of_encrypted_blocks, iv, 1);
        }
        input += sample->subsamples[i].bytes_of_protected_data;
        size -= sample->subsamples[i].bytes_of_protected_data;
    }

    if (size > 0) {
        av_log(c->fc, AV_LOG_ERROR, "leftover packet bytes after subsample processing\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int cens_scheme_decrypt(MOVContext *c, MOVStreamContext *sc, AVEncryptionInfo *sample, uint8_t *input, int size)
{
    int i, ret, rem_bytes;
    uint8_t *data;

    if (!sc->cenc.aes_ctr) {
        /* initialize the cipher */
        sc->cenc.aes_ctr = av_aes_ctr_alloc();
        if (!sc->cenc.aes_ctr) {
            return AVERROR(ENOMEM);
        }

        ret = av_aes_ctr_init(sc->cenc.aes_ctr, c->decryption_key);
        if (ret < 0) {
            return ret;
        }
    }

    av_aes_ctr_set_full_iv(sc->cenc.aes_ctr, sample->iv);

    /* whole-block full sample encryption */
    if (!sample->subsample_count) {
        /* decrypt the whole packet */
        av_aes_ctr_crypt(sc->cenc.aes_ctr, input, input, size);
        return 0;
    } else if (!sample->crypt_byte_block && !sample->skip_byte_block) {
        av_log(c->fc, AV_LOG_ERROR, "pattern encryption is not present in 'cens' scheme\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < sample->subsample_count; i++) {
        if (sample->subsamples[i].bytes_of_clear_data + sample->subsamples[i].bytes_of_protected_data > size) {
            av_log(c->fc, AV_LOG_ERROR, "subsample size exceeds the packet size left\n");
            return AVERROR_INVALIDDATA;
        }

        /* skip the clear bytes */
        input += sample->subsamples[i].bytes_of_clear_data;
        size -= sample->subsamples[i].bytes_of_clear_data;

        /* decrypt the encrypted bytes */
        data = input;
        rem_bytes = sample->subsamples[i].bytes_of_protected_data;
        while (rem_bytes > 0) {
            if (rem_bytes < 16*sample->crypt_byte_block) {
                break;
            }
            av_aes_ctr_crypt(sc->cenc.aes_ctr, data, data, 16*sample->crypt_byte_block);
            data += 16*sample->crypt_byte_block;
            rem_bytes -= 16*sample->crypt_byte_block;
            data += FFMIN(16*sample->skip_byte_block, rem_bytes);
            rem_bytes -= FFMIN(16*sample->skip_byte_block, rem_bytes);
        }
        input += sample->subsamples[i].bytes_of_protected_data;
        size -= sample->subsamples[i].bytes_of_protected_data;
    }

    if (size > 0) {
        av_log(c->fc, AV_LOG_ERROR, "leftover packet bytes after subsample processing\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int cbcs_scheme_decrypt(MOVContext *c, MOVStreamContext *sc, AVEncryptionInfo *sample, uint8_t *input, int size)
{
    int i, ret, rem_bytes;
    uint8_t iv[16];
    uint8_t *data;

    if (!sc->cenc.aes_ctx) {
        /* initialize the cipher */
        sc->cenc.aes_ctx = av_aes_alloc();
        if (!sc->cenc.aes_ctx) {
            return AVERROR(ENOMEM);
        }

        ret = av_aes_init(sc->cenc.aes_ctx, c->decryption_key, 16 * 8, 1);
        if (ret < 0) {
            return ret;
        }
    }

    /* whole-block full sample encryption */
    if (!sample->subsample_count) {
        /* decrypt the whole packet */
        memcpy(iv, sample->iv, 16);
        av_aes_crypt(sc->cenc.aes_ctx, input, input, size/16, iv, 1);
        return 0;
    } else if (!sample->crypt_byte_block && !sample->skip_byte_block) {
        av_log(c->fc, AV_LOG_ERROR, "pattern encryption is not present in 'cbcs' scheme\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < sample->subsample_count; i++) {
        if (sample->subsamples[i].bytes_of_clear_data + sample->subsamples[i].bytes_of_protected_data > size) {
            av_log(c->fc, AV_LOG_ERROR, "subsample size exceeds the packet size left\n");
            return AVERROR_INVALIDDATA;
        }

        /* skip the clear bytes */
        input += sample->subsamples[i].bytes_of_clear_data;
        size -= sample->subsamples[i].bytes_of_clear_data;

        /* decrypt the encrypted bytes */
        memcpy(iv, sample->iv, 16);
        data = input;
        rem_bytes = sample->subsamples[i].bytes_of_protected_data;
        while (rem_bytes > 0) {
            if (rem_bytes < 16*sample->crypt_byte_block) {
                break;
            }
            av_aes_crypt(sc->cenc.aes_ctx, data, data, sample->crypt_byte_block, iv, 1);
            data += 16*sample->crypt_byte_block;
            rem_bytes -= 16*sample->crypt_byte_block;
            data += FFMIN(16*sample->skip_byte_block, rem_bytes);
            rem_bytes -= FFMIN(16*sample->skip_byte_block, rem_bytes);
        }
        input += sample->subsamples[i].bytes_of_protected_data;
        size -= sample->subsamples[i].bytes_of_protected_data;
    }

    if (size > 0) {
        av_log(c->fc, AV_LOG_ERROR, "leftover packet bytes after subsample processing\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int cenc_decrypt(MOVContext *c, MOVStreamContext *sc, AVEncryptionInfo *sample, uint8_t *input, int size)
{
    if (sample->scheme == MKBETAG('c','e','n','c') && !sample->crypt_byte_block && !sample->skip_byte_block) {
        return cenc_scheme_decrypt(c, sc, sample, input, size);
    } else if (sample->scheme == MKBETAG('c','b','c','1') && !sample->crypt_byte_block && !sample->skip_byte_block) {
        return cbc1_scheme_decrypt(c, sc, sample, input, size);
    } else if (sample->scheme == MKBETAG('c','e','n','s')) {
        return cens_scheme_decrypt(c, sc, sample, input, size);
    } else if (sample->scheme == MKBETAG('c','b','c','s')) {
        return cbcs_scheme_decrypt(c, sc, sample, input, size);
    } else {
        av_log(c->fc, AV_LOG_ERROR, "invalid encryption scheme\n");
        return AVERROR_INVALIDDATA;
    }
}

static MOVFragmentStreamInfo *get_frag_stream_info_from_pkt(MOVFragmentIndex *frag_index, AVPacket *pkt, int id)
{
    int current = frag_index->current;

    if (!frag_index->nb_items)
        return NULL;

    // Check frag_index->current is the right one for pkt. It can out of sync.
    if (current >= 0 && current < frag_index->nb_items) {
        if (frag_index->item[current].moof_offset < pkt->pos &&
            (current + 1 == frag_index->nb_items ||
             frag_index->item[current + 1].moof_offset > pkt->pos))
            return get_frag_stream_info(frag_index, current, id);
    }


    for (int i = 0; i < frag_index->nb_items; i++) {
        if (frag_index->item[i].moof_offset > pkt->pos)
            break;
        current = i;
    }
    frag_index->current = current;
    return get_frag_stream_info(frag_index, current, id);
}

static int cenc_filter(MOVContext *mov, AVStream* st, MOVStreamContext *sc, AVPacket *pkt, int current_index)
{
    MOVFragmentStreamInfo *frag_stream_info;
    MOVEncryptionIndex *encryption_index;
    AVEncryptionInfo *encrypted_sample;
    int encrypted_index, ret;

    frag_stream_info = get_frag_stream_info_from_pkt(&mov->frag_index, pkt, sc->id);
    encrypted_index = current_index;
    encryption_index = NULL;
    if (frag_stream_info) {
        // Note this only supports encryption info in the first sample descriptor.
        if (frag_stream_info->stsd_id == 1) {
            if (frag_stream_info->encryption_index) {
                encrypted_index = current_index - frag_stream_info->index_base;
                encryption_index = frag_stream_info->encryption_index;
            } else {
                encryption_index = sc->cenc.encryption_index;
            }
        }
    } else {
        encryption_index = sc->cenc.encryption_index;
    }

    if (encryption_index) {
        if (encryption_index->auxiliary_info_sample_count &&
            !encryption_index->nb_encrypted_samples) {
            av_log(mov->fc, AV_LOG_ERROR, "saiz atom found without saio\n");
            return AVERROR_INVALIDDATA;
        }
        if (encryption_index->auxiliary_offsets_count &&
            !encryption_index->nb_encrypted_samples) {
            av_log(mov->fc, AV_LOG_ERROR, "saio atom found without saiz\n");
            return AVERROR_INVALIDDATA;
        }

        encrypted_sample = NULL;
        if (!encryption_index->nb_encrypted_samples) {
            // Full-sample encryption with default settings.
            encrypted_sample = sc->cenc.default_encrypted_sample;
        } else if (encrypted_index >= 0 && encrypted_index < encryption_index->nb_encrypted_samples) {
            // Per-sample setting override.
            encrypted_sample = encryption_index->encrypted_samples[encrypted_index];
            if (!encrypted_sample) {
                encrypted_sample = sc->cenc.default_encrypted_sample;
            }
        }

        if (!encrypted_sample) {
            av_log(mov->fc, AV_LOG_ERROR, "Incorrect number of samples in encryption info\n");
            return AVERROR_INVALIDDATA;
        }

        if (mov->decryption_key) {
            return cenc_decrypt(mov, sc, encrypted_sample, pkt->data, pkt->size);
        } else {
            size_t size;
            uint8_t *side_data = av_encryption_info_add_side_data(encrypted_sample, &size);
            if (!side_data)
                return AVERROR(ENOMEM);
            ret = av_packet_add_side_data(pkt, AV_PKT_DATA_ENCRYPTION_INFO, side_data, size);
            if (ret < 0)
                av_free(side_data);
            return ret;
        }
    }

    return 0;
}

static int mov_read_dops(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    const int OPUS_SEEK_PREROLL_MS = 80;
    int ret;
    AVStream *st;
    size_t size;
    uint16_t pre_skip;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((uint64_t)atom.size > (1<<30) || atom.size < 11)
        return AVERROR_INVALIDDATA;

    /* Check OpusSpecificBox version. */
    if (avio_r8(pb) != 0) {
        av_log(c->fc, AV_LOG_ERROR, "unsupported OpusSpecificBox version\n");
        return AVERROR_INVALIDDATA;
    }

    /* OpusSpecificBox size plus magic for Ogg OpusHead header. */
    size = atom.size + 8;

    if ((ret = ff_alloc_extradata(st->codecpar, size)) < 0)
        return ret;

    AV_WL32A(st->codecpar->extradata, MKTAG('O','p','u','s'));
    AV_WL32A(st->codecpar->extradata + 4, MKTAG('H','e','a','d'));
    AV_WB8(st->codecpar->extradata + 8, 1); /* OpusHead version */
    avio_read(pb, st->codecpar->extradata + 9, size - 9);

    /* OpusSpecificBox is stored in big-endian, but OpusHead is
       little-endian; aside from the preceeding magic and version they're
       otherwise currently identical.  Data after output gain at offset 16
       doesn't need to be bytewapped. */
    pre_skip = AV_RB16A(st->codecpar->extradata + 10);
    AV_WL16A(st->codecpar->extradata + 10, pre_skip);
    AV_WL32A(st->codecpar->extradata + 12, AV_RB32A(st->codecpar->extradata + 12));
    AV_WL16A(st->codecpar->extradata + 16, AV_RB16A(st->codecpar->extradata + 16));

    st->codecpar->initial_padding = pre_skip;
    st->codecpar->seek_preroll = av_rescale_q(OPUS_SEEK_PREROLL_MS,
                                              (AVRational){1, 1000},
                                              (AVRational){1, 48000});

    return 0;
}

static int mov_read_dmlp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    unsigned format_info;
    int channel_assignment, channel_assignment1, channel_assignment2;
    int ratebits;
    uint64_t chmask;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (atom.size < 10)
        return AVERROR_INVALIDDATA;

    format_info = avio_rb32(pb);

    ratebits            = (format_info >> 28) & 0xF;
    channel_assignment1 = (format_info >> 15) & 0x1F;
    channel_assignment2 = format_info & 0x1FFF;
    if (channel_assignment2)
        channel_assignment = channel_assignment2;
    else
        channel_assignment = channel_assignment1;

    st->codecpar->frame_size = 40 << (ratebits & 0x7);
    st->codecpar->sample_rate = mlp_samplerate(ratebits);

    av_channel_layout_uninit(&st->codecpar->ch_layout);
    chmask = truehd_layout(channel_assignment);
    av_channel_layout_from_mask(&st->codecpar->ch_layout, chmask);

    return 0;
}

static int mov_read_dvcc_dvvc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint8_t buf[ISOM_DVCC_DVVC_SIZE];
    int ret;
    int64_t read_size = atom.size;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    // At most 24 bytes
    read_size = FFMIN(read_size, ISOM_DVCC_DVVC_SIZE);

    if ((ret = ffio_read_size(pb, buf, read_size)) < 0)
        return ret;

    return ff_isom_parse_dvcc_dvvc(c->fc, st, buf, read_size);
}

static int mov_read_lhvc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint8_t *buf;
    int ret, old_size, num_arrays;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (!st->codecpar->extradata_size)
        // TODO: handle lhvC when present before hvcC
        return 0;

    if (atom.size < 6 || st->codecpar->extradata_size < 23)
        return AVERROR_INVALIDDATA;

    buf = av_malloc(atom.size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);
    memset(buf + atom.size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    ret = ffio_read_size(pb, buf, atom.size);
    if (ret < 0) {
        av_free(buf);
        av_log(c->fc, AV_LOG_WARNING, "lhvC atom truncated\n");
        return 0;
    }

    num_arrays = buf[5];
    old_size = st->codecpar->extradata_size;
    atom.size -= 8 /* account for mov_realloc_extradata offseting */
               + 6 /* lhvC bytes before the arrays*/;

    ret = mov_realloc_extradata(st->codecpar, atom);
    if (ret < 0) {
        av_free(buf);
        return ret;
    }

    st->codecpar->extradata[22] += num_arrays;
    memcpy(st->codecpar->extradata + old_size, buf + 6, atom.size + 8);

    st->disposition |= AV_DISPOSITION_MULTILAYER;

    av_free(buf);
    return 0;
}

static int mov_read_kind(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVFormatContext *ctx = c->fc;
    AVStream *st = NULL;
    AVBPrint scheme_buf, value_buf;
    int64_t scheme_str_len = 0, value_str_len = 0;
    int version, flags, ret = AVERROR_BUG;
    int64_t size = atom.size;

    if (atom.size < 6)
        // 4 bytes for version + flags, 2x 1 byte for null
        return AVERROR_INVALIDDATA;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    version = avio_r8(pb);
    flags   = avio_rb24(pb);
    size   -= 4;

    if (version != 0 || flags != 0) {
        av_log(ctx, AV_LOG_ERROR,
               "Unsupported 'kind' box with version %d, flags: %x",
               version, flags);
        return AVERROR_INVALIDDATA;
    }

    av_bprint_init(&scheme_buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&value_buf,  0, AV_BPRINT_SIZE_UNLIMITED);

    if ((scheme_str_len = ff_read_string_to_bprint_overwrite(pb, &scheme_buf,
                                                             size)) < 0) {
        ret = scheme_str_len;
        goto cleanup;
    }

    if (scheme_str_len + 1 >= size) {
        // we need to have another string, even if nullptr.
        // we check with + 1 since we expect that if size was not hit,
        // an additional null was read.
        ret = AVERROR_INVALIDDATA;
        goto cleanup;
    }

    size -= scheme_str_len + 1;

    if ((value_str_len = ff_read_string_to_bprint_overwrite(pb, &value_buf,
                                                            size)) < 0) {
        ret = value_str_len;
        goto cleanup;
    }

    if (value_str_len == size) {
        // in case of no trailing null, box is not valid.
        ret = AVERROR_INVALIDDATA;
        goto cleanup;
    }

    av_log(ctx, AV_LOG_TRACE,
           "%s stream %d KindBox(scheme: %s, value: %s)\n",
           av_get_media_type_string(st->codecpar->codec_type),
           st->index,
           scheme_buf.str, value_buf.str);

    for (int i = 0; ff_mov_track_kind_table[i].scheme_uri; i++) {
        const struct MP4TrackKindMapping map = ff_mov_track_kind_table[i];
        if (!av_strstart(scheme_buf.str, map.scheme_uri, NULL))
            continue;

        for (int j = 0; map.value_maps[j].disposition; j++) {
            const struct MP4TrackKindValueMapping value_map = map.value_maps[j];
            if (!av_strstart(value_buf.str, value_map.value, NULL))
                continue;

            st->disposition |= value_map.disposition;
        }
    }

    ret = 0;

cleanup:

    av_bprint_finalize(&scheme_buf, NULL);
    av_bprint_finalize(&value_buf,  NULL);

    return ret;
}

static int mov_read_SA3D(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    AVChannelLayout ch_layout = { 0 };
    int ret, i, version, type;
    int ambisonic_order, channel_order, normalization, channel_count;
    int ambi_channels, non_diegetic_channels;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];

    if (atom.size < 16) {
        av_log(c->fc, AV_LOG_ERROR, "SA3D audio box too small\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported SA3D box version %d\n", version);
        return 0;
    }

    type = avio_r8(pb);
    if (type & 0x7f) {
        av_log(c->fc, AV_LOG_WARNING,
               "Unsupported ambisonic type %d\n", type & 0x7f);
        return 0;
    }
    non_diegetic_channels = (type >> 7) * 2; // head_locked_stereo

    ambisonic_order = avio_rb32(pb);

    channel_order = avio_r8(pb);
    if (channel_order) {
        av_log(c->fc, AV_LOG_WARNING,
               "Unsupported channel_order %d\n", channel_order);
        return 0;
    }

    normalization = avio_r8(pb);
    if (normalization) {
        av_log(c->fc, AV_LOG_WARNING,
               "Unsupported normalization %d\n", normalization);
        return 0;
    }

    channel_count = avio_rb32(pb);
    if (ambisonic_order < 0 || ambisonic_order > 31 ||
        channel_count != ((ambisonic_order + 1LL) * (ambisonic_order + 1LL) +
                           non_diegetic_channels)) {
        av_log(c->fc, AV_LOG_ERROR,
               "Invalid number of channels (%d / %d)\n",
               channel_count, ambisonic_order);
        return 0;
    }
    ambi_channels = channel_count - non_diegetic_channels;

    ret = av_channel_layout_custom_init(&ch_layout, channel_count);
    if (ret < 0)
        return 0;

    for (i = 0; i < channel_count; i++) {
        unsigned channel = avio_rb32(pb);

        if (channel >= channel_count) {
            av_log(c->fc, AV_LOG_ERROR, "Invalid channel index (%d / %d)\n",
                   channel, ambisonic_order);
            av_channel_layout_uninit(&ch_layout);
            return 0;
        }
        if (channel >= ambi_channels)
            ch_layout.u.map[i].id = channel - ambi_channels;
        else
            ch_layout.u.map[i].id = AV_CHAN_AMBISONIC_BASE + channel;
    }

    ret = av_channel_layout_retype(&ch_layout, 0, AV_CHANNEL_LAYOUT_RETYPE_FLAG_CANONICAL);
    if (ret < 0) {
        av_channel_layout_uninit(&ch_layout);
        return 0;
    }

    av_channel_layout_uninit(&st->codecpar->ch_layout);
    st->codecpar->ch_layout = ch_layout;

    return 0;
}

static int mov_read_SAND(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int version;

    if (c->fc->nb_streams < 1)
        return 0;

    st = c->fc->streams[c->fc->nb_streams - 1];

    if (atom.size < 5) {
        av_log(c->fc, AV_LOG_ERROR, "Empty SAND audio box\n");
        return AVERROR_INVALIDDATA;
    }

    version = avio_r8(pb);
    if (version) {
        av_log(c->fc, AV_LOG_WARNING, "Unsupported SAND box version %d\n", version);
        return 0;
    }

    st->disposition |= AV_DISPOSITION_NON_DIEGETIC;

    return 0;
}

static int rb_size(AVIOContext *pb, int64_t *value, int size)
{
    if (size == 0)
        *value = 0;
    else if (size == 1)
        *value = avio_r8(pb);
    else if (size == 2)
        *value = avio_rb16(pb);
    else if (size == 4)
        *value = avio_rb32(pb);
    else if (size == 8) {
        *value = avio_rb64(pb);
        if (*value < 0)
            return -1;
    } else
        return -1;
    return size;
}

static int mov_read_pitm(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    avio_rb32(pb);  // version & flags.
    c->primary_item_id = avio_rb16(pb);
    av_log(c->fc, AV_LOG_TRACE, "pitm: primary_item_id %d\n", c->primary_item_id);
    return atom.size;
}

static int mov_read_idat(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    c->idat_offset = avio_tell(pb);
    return 0;
}

static int mov_read_iloc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    HEIFItem **heif_item;
    int version, offset_size, length_size, base_offset_size, index_size;
    int item_count, extent_count;
    int64_t base_offset, extent_offset, extent_length;
    uint8_t value;

    if (c->found_iloc) {
        av_log(c->fc, AV_LOG_INFO, "Duplicate iloc box found\n");
        return 0;
    }

    version = avio_r8(pb);
    avio_rb24(pb);  // flags.

    value = avio_r8(pb);
    offset_size = (value >> 4) & 0xF;
    length_size = value & 0xF;
    value = avio_r8(pb);
    base_offset_size = (value >> 4) & 0xF;
    index_size = !version ? 0 : (value & 0xF);
    if (index_size) {
        avpriv_report_missing_feature(c->fc, "iloc: index_size != 0");
        return AVERROR_PATCHWELCOME;
    }
    item_count = (version < 2) ? avio_rb16(pb) : avio_rb32(pb);

    heif_item = av_realloc_array(c->heif_item, FFMAX(item_count, c->nb_heif_item), sizeof(*c->heif_item));
    if (!heif_item)
        return AVERROR(ENOMEM);
    c->heif_item = heif_item;
    if (item_count > c->nb_heif_item)
        memset(&c->heif_item[c->nb_heif_item], 0,
               sizeof(*c->heif_item) * (item_count - c->nb_heif_item));
    c->nb_heif_item = FFMAX(c->nb_heif_item, item_count);

    av_log(c->fc, AV_LOG_TRACE, "iloc: item_count %d\n", item_count);
    for (int i = 0; i < item_count; i++) {
        HEIFItem *item = c->heif_item[i];
        int item_id = (version < 2) ? avio_rb16(pb) : avio_rb32(pb);
        int offset_type = (version > 0) ? avio_rb16(pb) & 0xf : 0;

        if (avio_feof(pb))
            return AVERROR_INVALIDDATA;
        if (offset_type > 1) {
            avpriv_report_missing_feature(c->fc, "iloc offset type %d", offset_type);
            return AVERROR_PATCHWELCOME;
        }

        avio_rb16(pb);  // data_reference_index.
        if (rb_size(pb, &base_offset, base_offset_size) < 0)
            return AVERROR_INVALIDDATA;
        extent_count = avio_rb16(pb);
        if (extent_count > 1) {
            // For still AVIF images, we only support one extent item.
            avpriv_report_missing_feature(c->fc, "iloc: extent_count > 1");
            return AVERROR_PATCHWELCOME;
        }

        if (rb_size(pb, &extent_offset, offset_size) < 0 ||
            rb_size(pb, &extent_length, length_size) < 0 ||
            base_offset > INT64_MAX - extent_offset)
            return AVERROR_INVALIDDATA;

        if (!item)
            item = c->heif_item[i] = av_mallocz(sizeof(*item));
        if (!item)
            return AVERROR(ENOMEM);

        item->item_id = item_id;

        if (offset_type == 1)
            item->is_idat_relative = 1;
        item->extent_length = extent_length;
        item->extent_offset = base_offset + extent_offset;
        av_log(c->fc, AV_LOG_TRACE, "iloc: item_idx %d, offset_type %d, "
                                    "extent_offset %"PRId64", extent_length %"PRId64"\n",
               i, offset_type, item->extent_offset, item->extent_length);
    }

    c->found_iloc = 1;
    return atom.size;
}

static int mov_read_infe(MOVContext *c, AVIOContext *pb, MOVAtom atom, int idx)
{
    HEIFItem *item;
    AVBPrint item_name;
    int64_t size = atom.size;
    uint32_t item_type;
    int item_id;
    int version, ret;

    version = avio_r8(pb);
    avio_rb24(pb);  // flags.
    size -= 4;
    if (size < 0)
        return AVERROR_INVALIDDATA;

    if (version < 2) {
        avpriv_report_missing_feature(c->fc, "infe version < 2");
        avio_skip(pb, size);
        return 1;
    }

    item_id = version > 2 ? avio_rb32(pb) : avio_rb16(pb);
    avio_rb16(pb); // item_protection_index
    item_type = avio_rl32(pb);
    size -= 8;
    if (size < 1)
        return AVERROR_INVALIDDATA;

    av_bprint_init(&item_name, 0, AV_BPRINT_SIZE_UNLIMITED);
    ret = ff_read_string_to_bprint_overwrite(pb, &item_name, size);
    if (ret < 0) {
        av_bprint_finalize(&item_name, NULL);
        return ret;
    }

    av_log(c->fc, AV_LOG_TRACE, "infe: item_id %d, item_type %s, item_name %s\n",
           item_id, av_fourcc2str(item_type), item_name.str);

    size -= ret + 1;
    if (size > 0)
        avio_skip(pb, size);

    item = c->heif_item[idx];
    if (!item)
        item = c->heif_item[idx] = av_mallocz(sizeof(*item));
    if (!item)
        return AVERROR(ENOMEM);

    if (ret)
        av_bprint_finalize(&item_name, &c->heif_item[idx]->name);
    c->heif_item[idx]->item_id = item_id;
    c->heif_item[idx]->type    = item_type;

    switch (item_type) {
    case MKTAG('a','v','0','1'):
    case MKTAG('h','v','c','1'):
        ret = heif_add_stream(c, c->heif_item[idx]);
        if (ret < 0)
            return ret;
        break;
    }

    return 0;
}

static int mov_read_iinf(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    HEIFItem **heif_item;
    int entry_count;
    int version, got_stream = 0, ret, i;

    if (c->found_iinf) {
        av_log(c->fc, AV_LOG_WARNING, "Duplicate iinf box found\n");
        return 0;
    }

    version = avio_r8(pb);
    avio_rb24(pb);  // flags.
    entry_count = version ? avio_rb32(pb) : avio_rb16(pb);

    heif_item = av_realloc_array(c->heif_item, FFMAX(entry_count, c->nb_heif_item), sizeof(*c->heif_item));
    if (!heif_item)
        return AVERROR(ENOMEM);
    c->heif_item = heif_item;
    if (entry_count > c->nb_heif_item)
        memset(&c->heif_item[c->nb_heif_item], 0,
               sizeof(*c->heif_item) * (entry_count - c->nb_heif_item));
    c->nb_heif_item = FFMAX(c->nb_heif_item, entry_count);

    for (i = 0; i < entry_count; i++) {
        MOVAtom infe;

        infe.size = avio_rb32(pb) - 8;
        infe.type = avio_rl32(pb);
        if (avio_feof(pb)) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        ret = mov_read_infe(c, pb, infe, i);
        if (ret < 0)
            goto fail;
        if (!ret)
            got_stream = 1;
    }

    c->found_iinf = got_stream;
    return 0;
fail:
    for (; i >= 0; i--) {
        HEIFItem *item = c->heif_item[i];

        if (!item)
            continue;

        av_freep(&item->name);
        if (!item->st)
            continue;

        mov_free_stream_context(c->fc, item->st);
        ff_remove_stream(c->fc, item->st);
        item->st = NULL;
    }
    return ret;
}

static int mov_read_iref_dimg(MOVContext *c, AVIOContext *pb, int version)
{
    HEIFItem *item = NULL;
    HEIFGrid *grid;
    int entries, i;
    int from_item_id = version ? avio_rb32(pb) : avio_rb16(pb);

    for (int i = 0; i < c->nb_heif_grid; i++) {
        if (c->heif_grid[i].item->item_id == from_item_id) {
            av_log(c->fc, AV_LOG_ERROR, "More than one 'dimg' box "
                                        "referencing the same Derived Image item\n");
            return AVERROR_INVALIDDATA;
        }
    }
    for (int i = 0; i < c->nb_heif_item; i++) {
        if (!c->heif_item[i] || c->heif_item[i]->item_id != from_item_id)
            continue;
        item = c->heif_item[i];

        switch (item->type) {
        case MKTAG('g','r','i','d'):
        case MKTAG('i','o','v','l'):
            break;
        default:
            avpriv_report_missing_feature(c->fc, "Derived Image item of type %s",
                                          av_fourcc2str(item->type));
            return 0;
        }
        break;
    }
    if (!item) {
        av_log(c->fc, AV_LOG_ERROR, "Missing grid information\n");
        return AVERROR_INVALIDDATA;
    }

    grid = av_realloc_array(c->heif_grid, c->nb_heif_grid + 1U,
                            sizeof(*c->heif_grid));
    if (!grid)
        return AVERROR(ENOMEM);
    c->heif_grid = grid;
    grid = &grid[c->nb_heif_grid++];

    entries = avio_rb16(pb);
    grid->tile_id_list = av_malloc_array(entries, sizeof(*grid->tile_id_list));
    grid->tile_idx_list = av_calloc(entries, sizeof(*grid->tile_idx_list));
    grid->tile_item_list = av_calloc(entries, sizeof(*grid->tile_item_list));
    if (!grid->tile_id_list || !grid->tile_item_list || !grid->tile_idx_list)
        return AVERROR(ENOMEM);
    /* 'to' item ids */
    for (i = 0; i < entries; i++)
        grid->tile_id_list[i] = version ? avio_rb32(pb) : avio_rb16(pb);
    grid->nb_tiles = entries;
    grid->item = item;

    av_log(c->fc, AV_LOG_TRACE, "dimg: from_item_id %d, entries %d\n",
           from_item_id, entries);

    return 0;
}

static int mov_read_iref_thmb(MOVContext *c, AVIOContext *pb, int version)
{
    int entries;
    int to_item_id, from_item_id = version ? avio_rb32(pb) : avio_rb16(pb);

    entries = avio_rb16(pb);
    if (entries > 1) {
        avpriv_request_sample(c->fc, "thmb in iref referencing several items");
        return AVERROR_PATCHWELCOME;
    }
    /* 'to' item ids */
    to_item_id = version ? avio_rb32(pb) : avio_rb16(pb);

    if (to_item_id != c->primary_item_id)
        return 0;

    c->thmb_item_id = from_item_id;

    av_log(c->fc, AV_LOG_TRACE, "thmb: from_item_id %d, entries %d\n",
           from_item_id, entries);

    return 0;
}

static int mov_read_iref(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int version = avio_r8(pb);
    avio_rb24(pb); // flags
    atom.size -= 4;

    if (version > 1) {
        av_log(c->fc, AV_LOG_WARNING, "Unknown iref box version %d\n", version);
        return 0;
    }

    while (atom.size) {
        uint32_t type, size = avio_rb32(pb);
        int64_t next = avio_tell(pb);

        if (size < 14 || next < 0 || next > INT64_MAX - size)
            return AVERROR_INVALIDDATA;

        next += size - 4;
        type = avio_rl32(pb);
        switch (type) {
        case MKTAG('d','i','m','g'):
            mov_read_iref_dimg(c, pb, version);
            break;
        case MKTAG('t','h','m','b'):
            mov_read_iref_thmb(c, pb, version);
            break;
        default:
            av_log(c->fc, AV_LOG_DEBUG, "Unknown iref type %s size %"PRIu32"\n",
                   av_fourcc2str(type), size);
        }

        atom.size -= size;
        avio_seek(pb, next, SEEK_SET);
    }
    return 0;
}

static int mov_read_ispe(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    HEIFItem *item;
    uint32_t width, height;

    avio_r8(pb);  /* version */
    avio_rb24(pb);  /* flags */
    width  = avio_rb32(pb);
    height = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "ispe: item_id %d, width %u, height %u\n",
           c->cur_item_id, width, height);

    item = heif_cur_item(c);
    if (item) {
        item->width  = width;
        item->height = height;
    }

    return 0;
}

static int mov_read_irot(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    HEIFItem *item;
    int angle;

    angle = avio_r8(pb) & 0x3;

    av_log(c->fc, AV_LOG_TRACE, "irot: item_id %d, angle %u\n",
           c->cur_item_id, angle);

    item = heif_cur_item(c);
    if (item) {
        // angle * 90 specifies the angle (in anti-clockwise direction)
        // in units of degrees.
        item->rotation = angle * 90;
    }

    return 0;
}

static int mov_read_imir(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    HEIFItem *item;
    int axis;

    axis = avio_r8(pb) & 0x1;

    av_log(c->fc, AV_LOG_TRACE, "imir: item_id %d, axis %u\n",
           c->cur_item_id, axis);

    item = heif_cur_item(c);
    if (item) {
        item->hflip =  axis;
        item->vflip = !axis;
    }

    return 0;
}

static int mov_read_iprp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    typedef struct MOVAtoms {
        FFIOContext b;
        uint32_t type;
        int64_t  size;
        uint8_t *data;
    } MOVAtoms;
    MOVAtoms *atoms = NULL;
    MOVAtom a;
    unsigned count;
    int nb_atoms = 0;
    int version, flags;
    int ret;

    a.size = avio_rb32(pb);
    a.type = avio_rl32(pb);

    if (a.size < 8 || a.type != MKTAG('i','p','c','o'))
        return AVERROR_INVALIDDATA;

    a.size -= 8;
    while (a.size >= 8) {
        MOVAtoms *ref = av_dynarray2_add((void**)&atoms, &nb_atoms, sizeof(MOVAtoms), NULL);
        if (!ref) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ref->data = NULL;
        ref->size = avio_rb32(pb);
        ref->type = avio_rl32(pb);
        if (ref->size > a.size || ref->size < 8)
            break;
        ref->data = av_malloc(ref->size);
        if (!ref->data) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        av_log(c->fc, AV_LOG_TRACE, "ipco: index %d, box type %s\n", nb_atoms, av_fourcc2str(ref->type));
        avio_seek(pb, -8, SEEK_CUR);
        if (avio_read(pb, ref->data, ref->size) != ref->size) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
        ffio_init_read_context(&ref->b, ref->data, ref->size);
        a.size -= ref->size;
    }

    if (a.size) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    a.size = avio_rb32(pb);
    a.type = avio_rl32(pb);

    if (a.size < 8 || a.type != MKTAG('i','p','m','a')) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    version = avio_r8(pb);
    flags   = avio_rb24(pb);
    count   = avio_rb32(pb);

    for (int i = 0; i < count; i++) {
        int item_id = version ? avio_rb32(pb) : avio_rb16(pb);
        int assoc_count = avio_r8(pb);

        if (avio_feof(pb)) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        for (int j = 0; j < assoc_count; j++) {
            MOVAtoms *ref;
            int index = avio_r8(pb) & 0x7f;
            if (flags & 1) {
                index <<= 8;
                index |= avio_r8(pb);
            }
            if (index > nb_atoms || index <= 0) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            ref = &atoms[--index];

            av_log(c->fc, AV_LOG_TRACE, "ipma: property_index %d, item_id %d, item_type %s\n",
                   index + 1, item_id, av_fourcc2str(ref->type));

            c->cur_item_id = item_id;

            ret = mov_read_default(c, &ref->b.pub,
                                   (MOVAtom) { .size = ref->size,
                                               .type = MKTAG('i','p','c','o') });
            if (ret < 0)
                goto fail;
            ffio_init_read_context(&ref->b, ref->data, ref->size);
        }
    }

    ret = 0;
fail:
    c->cur_item_id = -1;
    for (int i = 0; i < nb_atoms; i++)
        av_free(atoms[i].data);
    av_free(atoms);

    return ret;
}

static const MOVParseTableEntry mov_default_parse_table[] = {
{ MKTAG('A','C','L','R'), mov_read_aclr },
{ MKTAG('A','P','R','G'), mov_read_avid },
{ MKTAG('A','A','L','P'), mov_read_avid },
{ MKTAG('A','R','E','S'), mov_read_ares },
{ MKTAG('a','v','s','s'), mov_read_avss },
{ MKTAG('a','v','1','C'), mov_read_glbl },
{ MKTAG('c','h','p','l'), mov_read_chpl },
{ MKTAG('c','o','6','4'), mov_read_stco },
{ MKTAG('c','o','l','r'), mov_read_colr },
{ MKTAG('c','t','t','s'), mov_read_ctts }, /* composition time to sample */
{ MKTAG('d','i','n','f'), mov_read_default },
{ MKTAG('D','p','x','E'), mov_read_dpxe },
{ MKTAG('d','r','e','f'), mov_read_dref },
{ MKTAG('e','d','t','s'), mov_read_default },
{ MKTAG('e','l','s','t'), mov_read_elst },
{ MKTAG('e','n','d','a'), mov_read_enda },
{ MKTAG('f','i','e','l'), mov_read_fiel },
{ MKTAG('a','d','r','m'), mov_read_adrm },
{ MKTAG('f','t','y','p'), mov_read_ftyp },
{ MKTAG('g','l','b','l'), mov_read_glbl },
{ MKTAG('h','d','l','r'), mov_read_hdlr },
{ MKTAG('i','l','s','t'), mov_read_ilst },
{ MKTAG('j','p','2','h'), mov_read_jp2h },
{ MKTAG('m','d','a','t'), mov_read_mdat },
{ MKTAG('m','d','h','d'), mov_read_mdhd },
{ MKTAG('m','d','i','a'), mov_read_default },
{ MKTAG('m','e','t','a'), mov_read_meta },
{ MKTAG('m','i','n','f'), mov_read_default },
{ MKTAG('m','o','o','f'), mov_read_moof },
{ MKTAG('m','o','o','v'), mov_read_moov },
{ MKTAG('m','v','e','x'), mov_read_default },
{ MKTAG('m','v','h','d'), mov_read_mvhd },
{ MKTAG('S','M','I',' '), mov_read_svq3 },
{ MKTAG('a','l','a','c'), mov_read_alac }, /* alac specific atom */
{ MKTAG('a','v','c','C'), mov_read_glbl },
{ MKTAG('p','a','s','p'), mov_read_pasp },
{ MKTAG('c','l','a','p'), mov_read_clap },
{ MKTAG('s','b','a','s'), mov_read_sbas },
{ MKTAG('s','i','d','x'), mov_read_sidx },
{ MKTAG('s','t','b','l'), mov_read_default },
{ MKTAG('s','t','c','o'), mov_read_stco },
{ MKTAG('s','t','p','s'), mov_read_stps },
{ MKTAG('s','t','r','f'), mov_read_strf },
{ MKTAG('s','t','s','c'), mov_read_stsc },
{ MKTAG('s','t','s','d'), mov_read_stsd }, /* sample description */
{ MKTAG('s','t','s','s'), mov_read_stss }, /* sync sample */
{ MKTAG('s','t','s','z'), mov_read_stsz }, /* sample size */
{ MKTAG('s','t','t','s'), mov_read_stts },
{ MKTAG('s','t','z','2'), mov_read_stsz }, /* compact sample size */
{ MKTAG('s','d','t','p'), mov_read_sdtp }, /* independent and disposable samples */
{ MKTAG('t','k','h','d'), mov_read_tkhd }, /* track header */
{ MKTAG('t','f','d','t'), mov_read_tfdt },
{ MKTAG('t','f','h','d'), mov_read_tfhd }, /* track fragment header */
{ MKTAG('t','r','a','k'), mov_read_trak },
{ MKTAG('t','r','a','f'), mov_read_default },
{ MKTAG('t','r','e','f'), mov_read_default },
{ MKTAG('t','m','c','d'), mov_read_tmcd },
{ MKTAG('c','h','a','p'), mov_read_chap },
{ MKTAG('t','r','e','x'), mov_read_trex },
{ MKTAG('t','r','u','n'), mov_read_trun },
{ MKTAG('u','d','t','a'), mov_read_default },
{ MKTAG('w','a','v','e'), mov_read_wave },
{ MKTAG('e','s','d','s'), mov_read_esds },
{ MKTAG('d','a','c','3'), mov_read_dac3 }, /* AC-3 info */
{ MKTAG('d','e','c','3'), mov_read_dec3 }, /* EAC-3 info */
{ MKTAG('d','d','t','s'), mov_read_ddts }, /* DTS audio descriptor */
{ MKTAG('w','i','d','e'), mov_read_wide }, /* place holder */
{ MKTAG('w','f','e','x'), mov_read_wfex },
{ MKTAG('c','m','o','v'), mov_read_cmov },
{ MKTAG('c','h','a','n'), mov_read_chan }, /* channel layout from quicktime */
{ MKTAG('c','h','n','l'), mov_read_chnl }, /* channel layout from ISO-14496-12 */
{ MKTAG('d','v','c','1'), mov_read_dvc1 },
{ MKTAG('s','g','p','d'), mov_read_sgpd },
{ MKTAG('s','b','g','p'), mov_read_sbgp },
{ MKTAG('h','v','c','C'), mov_read_glbl },
{ MKTAG('v','v','c','C'), mov_read_glbl },
{ MKTAG('u','u','i','d'), mov_read_uuid },
{ MKTAG('C','i','n', 0x8e), mov_read_targa_y216 },
{ MKTAG('f','r','e','e'), mov_read_free },
{ MKTAG('-','-','-','-'), mov_read_custom },
{ MKTAG('s','i','n','f'), mov_read_default },
{ MKTAG('f','r','m','a'), mov_read_frma },
{ MKTAG('s','e','n','c'), mov_read_senc },
{ MKTAG('s','a','i','z'), mov_read_saiz },
{ MKTAG('s','a','i','o'), mov_read_saio },
{ MKTAG('p','s','s','h'), mov_read_pssh },
{ MKTAG('s','c','h','m'), mov_read_schm },
{ MKTAG('s','c','h','i'), mov_read_default },
{ MKTAG('t','e','n','c'), mov_read_tenc },
{ MKTAG('d','f','L','a'), mov_read_dfla },
{ MKTAG('s','t','3','d'), mov_read_st3d }, /* stereoscopic 3D video box */
{ MKTAG('s','v','3','d'), mov_read_sv3d }, /* spherical video box */
{ MKTAG('v','e','x','u'), mov_read_vexu }, /* video extension usage */
{ MKTAG('h','f','o','v'), mov_read_hfov },
{ MKTAG('d','O','p','s'), mov_read_dops },
{ MKTAG('d','m','l','p'), mov_read_dmlp },
{ MKTAG('S','m','D','m'), mov_read_smdm },
{ MKTAG('C','o','L','L'), mov_read_coll },
{ MKTAG('v','p','c','C'), mov_read_vpcc },
{ MKTAG('m','d','c','v'), mov_read_mdcv },
{ MKTAG('c','l','l','i'), mov_read_clli },
{ MKTAG('d','v','c','C'), mov_read_dvcc_dvvc },
{ MKTAG('d','v','v','C'), mov_read_dvcc_dvvc },
{ MKTAG('d','v','w','C'), mov_read_dvcc_dvvc },
{ MKTAG('k','i','n','d'), mov_read_kind },
{ MKTAG('S','A','3','D'), mov_read_SA3D }, /* ambisonic audio box */
{ MKTAG('S','A','N','D'), mov_read_SAND }, /* non diegetic audio box */
{ MKTAG('i','l','o','c'), mov_read_iloc },
{ MKTAG('p','c','m','C'), mov_read_pcmc }, /* PCM configuration box */
{ MKTAG('p','i','t','m'), mov_read_pitm },
{ MKTAG('e','v','c','C'), mov_read_glbl },
{ MKTAG('i','d','a','t'), mov_read_idat },
{ MKTAG('i','m','i','r'), mov_read_imir },
{ MKTAG('i','r','e','f'), mov_read_iref },
{ MKTAG('i','s','p','e'), mov_read_ispe },
{ MKTAG('i','r','o','t'), mov_read_irot },
{ MKTAG('i','p','r','p'), mov_read_iprp },
{ MKTAG('i','i','n','f'), mov_read_iinf },
{ MKTAG('a','m','v','e'), mov_read_amve }, /* ambient viewing environment box */
{ MKTAG('l','h','v','C'), mov_read_lhvc },
{ MKTAG('l','v','c','C'), mov_read_glbl },
#if CONFIG_IAMFDEC
{ MKTAG('i','a','c','b'), mov_read_iacb },
#endif
{ 0, NULL }
};

static int mov_read_default(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t total_size = 0;
    MOVAtom a;
    int i;

    if (c->atom_depth > 10) {
        av_log(c->fc, AV_LOG_ERROR, "Atoms too deeply nested\n");
        return AVERROR_INVALIDDATA;
    }
    c->atom_depth ++;

    if (atom.size < 0)
        atom.size = INT64_MAX;
    while (total_size <= atom.size - 8) {
        int (*parse)(MOVContext*, AVIOContext*, MOVAtom) = NULL;
        a.size = avio_rb32(pb);
        a.type = avio_rl32(pb);
        if (avio_feof(pb))
            break;
        if (((a.type == MKTAG('f','r','e','e') && c->moov_retry) ||
              a.type == MKTAG('h','o','o','v')) &&
            a.size >= 8 &&
            c->fc->strict_std_compliance < FF_COMPLIANCE_STRICT) {
                uint32_t type;
                avio_skip(pb, 4);
                type = avio_rl32(pb);
                if (avio_feof(pb))
                    break;
                avio_seek(pb, -8, SEEK_CUR);
                if (type == MKTAG('m','v','h','d') ||
                    type == MKTAG('c','m','o','v')) {
                    av_log(c->fc, AV_LOG_ERROR, "Detected moov in a free or hoov atom.\n");
                    a.type = MKTAG('m','o','o','v');
                }
        }
        if (atom.type != MKTAG('r','o','o','t') &&
            atom.type != MKTAG('m','o','o','v')) {
            if (a.type == MKTAG('t','r','a','k') ||
                a.type == MKTAG('m','d','a','t')) {
                av_log(c->fc, AV_LOG_ERROR, "Broken file, trak/mdat not at top-level\n");
                avio_skip(pb, -8);
                c->atom_depth --;
                return 0;
            }
        }
        total_size += 8;
        if (a.size == 1 && total_size + 8 <= atom.size) { /* 64 bit extended size */
            a.size = avio_rb64(pb) - 8;
            total_size += 8;
        }
        av_log(c->fc, AV_LOG_TRACE, "type:'%s' parent:'%s' sz: %"PRId64" %"PRId64" %"PRId64"\n",
               av_fourcc2str(a.type), av_fourcc2str(atom.type), a.size, total_size, atom.size);
        if (a.size == 0) {
            a.size = atom.size - total_size + 8;
        }
        if (a.size < 0)
            break;
        a.size -= 8;
        if (a.size < 0)
            break;
        a.size = FFMIN(a.size, atom.size - total_size);

        for (i = 0; mov_default_parse_table[i].type; i++)
            if (mov_default_parse_table[i].type == a.type) {
                parse = mov_default_parse_table[i].parse;
                break;
            }

        // container is user data
        if (!parse && (atom.type == MKTAG('u','d','t','a') ||
                       atom.type == MKTAG('i','l','s','t')))
            parse = mov_read_udta_string;

        // Supports parsing the QuickTime Metadata Keys.
        // https://developer.apple.com/library/mac/documentation/QuickTime/QTFF/Metadata/Metadata.html
        if (!parse && c->found_hdlr_mdta &&
            atom.type == MKTAG('m','e','t','a') &&
            a.type == MKTAG('k','e','y','s') &&
            c->meta_keys_count == 0) {
            parse = mov_read_keys;
        }

        if (!parse) { /* skip leaf atoms data */
            avio_skip(pb, a.size);
        } else {
            int64_t start_pos = avio_tell(pb);
            int64_t left;
            int err = parse(c, pb, a);
            if (err < 0) {
                c->atom_depth --;
                return err;
            }
            if (c->found_moov && c->found_mdat && a.size <= INT64_MAX - start_pos &&
                ((!(pb->seekable & AVIO_SEEKABLE_NORMAL) || c->fc->flags & AVFMT_FLAG_IGNIDX || c->frag_index.complete) ||
                 start_pos + a.size == avio_size(pb))) {
                if (!(pb->seekable & AVIO_SEEKABLE_NORMAL) || c->fc->flags & AVFMT_FLAG_IGNIDX || c->frag_index.complete)
                    c->next_root_atom = start_pos + a.size;
                c->atom_depth --;
                return 0;
            }
            left = a.size - avio_tell(pb) + start_pos;
            if (left > 0) /* skip garbage at atom end */
                avio_skip(pb, left);
            else if (left < 0) {
                av_log(c->fc, AV_LOG_WARNING,
                       "overread end of atom '%s' by %"PRId64" bytes\n",
                       av_fourcc2str(a.type), -left);
                avio_seek(pb, left, SEEK_CUR);
            }
        }

        total_size += a.size;
    }

    if (total_size < atom.size && atom.size < 0x7ffff)
        avio_skip(pb, atom.size - total_size);

    c->atom_depth --;
    return 0;
}

static int mov_probe(const AVProbeData *p)
{
    int64_t offset;
    uint32_t tag;
    int score = 0;
    int moov_offset = -1;

    /* check file header */
    offset = 0;
    for (;;) {
        int64_t size;
        int minsize = 8;
        /* ignore invalid offset */
        if ((offset + 8ULL) > (unsigned int)p->buf_size)
            break;
        size = AV_RB32(p->buf + offset);
        if (size == 1 && offset + 16 <= (unsigned int)p->buf_size) {
            size = AV_RB64(p->buf+offset + 8);
            minsize = 16;
        } else if (size == 0) {
            size = p->buf_size - offset;
        }
        if (size < minsize) {
            offset += 4;
            continue;
        }
        tag = AV_RL32(p->buf + offset + 4);
        switch(tag) {
        /* check for obvious tags */
        case MKTAG('m','o','o','v'):
            moov_offset = offset + 4;
        case MKTAG('m','d','a','t'):
        case MKTAG('p','n','o','t'): /* detect movs with preview pics like ew.mov and april.mov */
        case MKTAG('u','d','t','a'): /* Packet Video PVAuthor adds this and a lot of more junk */
        case MKTAG('f','t','y','p'):
            if (tag == MKTAG('f','t','y','p') &&
                       (   AV_RL32(p->buf + offset + 8) == MKTAG('j','p','2',' ')
                        || AV_RL32(p->buf + offset + 8) == MKTAG('j','p','x',' ')
                        || AV_RL32(p->buf + offset + 8) == MKTAG('j','x','l',' ')
                    )) {
                score = FFMAX(score, 5);
            } else {
                score = AVPROBE_SCORE_MAX;
            }
            break;
        /* those are more common words, so rate then a bit less */
        case MKTAG('e','d','i','w'): /* xdcam files have reverted first tags */
        case MKTAG('w','i','d','e'):
        case MKTAG('f','r','e','e'):
        case MKTAG('j','u','n','k'):
        case MKTAG('p','i','c','t'):
            score  = FFMAX(score, AVPROBE_SCORE_MAX - 5);
            break;
        case MKTAG(0x82,0x82,0x7f,0x7d):
            score  = FFMAX(score, AVPROBE_SCORE_EXTENSION - 5);
            break;
        case MKTAG('s','k','i','p'):
        case MKTAG('u','u','i','d'):
        case MKTAG('p','r','f','l'):
            /* if we only find those cause probedata is too small at least rate them */
            score  = FFMAX(score, AVPROBE_SCORE_EXTENSION);
            break;
        }
        if (size > INT64_MAX - offset)
            break;
        offset += size;
    }
    if (score > AVPROBE_SCORE_MAX - 50 && moov_offset != -1) {
        /* moov atom in the header - we should make sure that this is not a
         * MOV-packed MPEG-PS */
        offset = moov_offset;

        while (offset < (p->buf_size - 16)) { /* Sufficient space */
               /* We found an actual hdlr atom */
            if (AV_RL32(p->buf + offset     ) == MKTAG('h','d','l','r') &&
                AV_RL32(p->buf + offset +  8) == MKTAG('m','h','l','r') &&
                AV_RL32(p->buf + offset + 12) == MKTAG('M','P','E','G')) {
                av_log(NULL, AV_LOG_WARNING, "Found media data tag MPEG indicating this is a MOV-packed MPEG-PS.\n");
                /* We found a media handler reference atom describing an
                 * MPEG-PS-in-MOV, return a
                 * low score to force expanding the probe window until
                 * mpegps_probe finds what it needs */
                return 5;
            } else {
                /* Keep looking */
                offset += 2;
            }
        }
    }

    return score;
}

// must be done after parsing all trak because there's no order requirement
static void mov_read_chapters(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    MOVStreamContext *sc;
    int64_t cur_pos;
    int i, j;
    int chapter_track;

    for (j = 0; j < mov->nb_chapter_tracks; j++) {
        AVStream *st = NULL;
        FFStream *sti = NULL;
        chapter_track = mov->chapter_tracks[j];
        for (i = 0; i < s->nb_streams; i++) {
            sc = mov->fc->streams[i]->priv_data;
            if (sc->id == chapter_track) {
                st = s->streams[i];
                break;
            }
        }
        if (!st) {
            av_log(s, AV_LOG_ERROR, "Referenced QT chapter track not found\n");
            continue;
        }
        sti = ffstream(st);

        sc = st->priv_data;
        cur_pos = avio_tell(sc->pb);

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            st->disposition |= AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_TIMED_THUMBNAILS;
            if (!st->attached_pic.data && sti->nb_index_entries) {
                // Retrieve the first frame, if possible
                AVIndexEntry *sample = &sti->index_entries[0];
                if (avio_seek(sc->pb, sample->pos, SEEK_SET) != sample->pos) {
                    av_log(s, AV_LOG_ERROR, "Failed to retrieve first frame\n");
                    goto finish;
                }

                if (ff_add_attached_pic(s, st, sc->pb, NULL, sample->size) < 0)
                    goto finish;
            }
        } else {
            st->codecpar->codec_type = AVMEDIA_TYPE_DATA;
            st->codecpar->codec_id = AV_CODEC_ID_BIN_DATA;
            st->discard = AVDISCARD_ALL;
            for (int i = 0; i < sti->nb_index_entries; i++) {
                AVIndexEntry *sample = &sti->index_entries[i];
                int64_t end = i+1 < sti->nb_index_entries ? sti->index_entries[i+1].timestamp : st->duration;
                uint8_t *title;
                uint16_t ch;
                int len, title_len;

                if (end < sample->timestamp) {
                    av_log(s, AV_LOG_WARNING, "ignoring stream duration which is shorter than chapters\n");
                    end = AV_NOPTS_VALUE;
                }

                if (avio_seek(sc->pb, sample->pos, SEEK_SET) != sample->pos) {
                    av_log(s, AV_LOG_ERROR, "Chapter %d not found in file\n", i);
                    goto finish;
                }

                // the first two bytes are the length of the title
                len = avio_rb16(sc->pb);
                if (len > sample->size-2)
                    continue;
                title_len = 2*len + 1;
                if (!(title = av_mallocz(title_len)))
                    goto finish;

                // The samples could theoretically be in any encoding if there's an encd
                // atom following, but in practice are only utf-8 or utf-16, distinguished
                // instead by the presence of a BOM
                if (!len) {
                    title[0] = 0;
                } else {
                    ch = avio_rb16(sc->pb);
                    if (ch == 0xfeff)
                        avio_get_str16be(sc->pb, len, title, title_len);
                    else if (ch == 0xfffe)
                        avio_get_str16le(sc->pb, len, title, title_len);
                    else {
                        AV_WB16(title, ch);
                        if (len == 1 || len == 2)
                            title[len] = 0;
                        else
                            avio_get_str(sc->pb, INT_MAX, title + 2, len - 1);
                    }
                }

                avpriv_new_chapter(s, i, st->time_base, sample->timestamp, end, title);
                av_freep(&title);
            }
        }
finish:
        avio_seek(sc->pb, cur_pos, SEEK_SET);
    }
}

static int parse_timecode_in_framenum_format(AVFormatContext *s, AVStream *st,
                                             int64_t value, int flags)
{
    AVTimecode tc;
    char buf[AV_TIMECODE_STR_SIZE];
    AVRational rate = st->avg_frame_rate;
    int ret = av_timecode_init(&tc, rate, flags, 0, s);
    if (ret < 0)
        return ret;
    av_dict_set(&st->metadata, "timecode",
                av_timecode_make_string(&tc, buf, value), 0);
    return 0;
}

static int mov_read_rtmd_track(AVFormatContext *s, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    char buf[AV_TIMECODE_STR_SIZE];
    int64_t cur_pos = avio_tell(sc->pb);
    int hh, mm, ss, ff, drop;

    if (!sti->nb_index_entries)
        return -1;

    avio_seek(sc->pb, sti->index_entries->pos, SEEK_SET);
    avio_skip(s->pb, 13);
    hh = avio_r8(s->pb);
    mm = avio_r8(s->pb);
    ss = avio_r8(s->pb);
    drop = avio_r8(s->pb);
    ff = avio_r8(s->pb);
    snprintf(buf, AV_TIMECODE_STR_SIZE, "%02d:%02d:%02d%c%02d",
             hh, mm, ss, drop ? ';' : ':', ff);
    av_dict_set(&st->metadata, "timecode", buf, 0);

    avio_seek(sc->pb, cur_pos, SEEK_SET);
    return 0;
}

static int mov_read_timecode_track(AVFormatContext *s, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int flags = 0;
    int64_t cur_pos = avio_tell(sc->pb);
    int64_t value;
    AVRational tc_rate = st->avg_frame_rate;
    int tmcd_nb_frames = sc->tmcd_nb_frames;
    int rounded_tc_rate;

    if (!sti->nb_index_entries)
        return -1;

    if (!tc_rate.num || !tc_rate.den || !tmcd_nb_frames)
        return -1;

    avio_seek(sc->pb, sti->index_entries->pos, SEEK_SET);
    value = avio_rb32(s->pb);

    if (sc->tmcd_flags & 0x0001) flags |= AV_TIMECODE_FLAG_DROPFRAME;
    if (sc->tmcd_flags & 0x0002) flags |= AV_TIMECODE_FLAG_24HOURSMAX;
    if (sc->tmcd_flags & 0x0004) flags |= AV_TIMECODE_FLAG_ALLOWNEGATIVE;

    /* Assume Counter flag is set to 1 in tmcd track (even though it is likely
     * not the case) and thus assume "frame number format" instead of QT one.
     * No sample with tmcd track can be found with a QT timecode at the moment,
     * despite what the tmcd track "suggests" (Counter flag set to 0 means QT
     * format). */

    /* 60 fps content have tmcd_nb_frames set to 30 but tc_rate set to 60, so
     * we multiply the frame number with the quotient.
     * See tickets #9492, #9710. */
    rounded_tc_rate = (tc_rate.num + tc_rate.den / 2LL) / tc_rate.den;
    /* Work around files where tmcd_nb_frames is rounded down from frame rate
     * instead of up. See ticket #5978. */
    if (tmcd_nb_frames == tc_rate.num / tc_rate.den &&
        s->strict_std_compliance < FF_COMPLIANCE_STRICT)
        tmcd_nb_frames = rounded_tc_rate;
    value = av_rescale(value, rounded_tc_rate, tmcd_nb_frames);

    parse_timecode_in_framenum_format(s, st, value, flags);

    avio_seek(sc->pb, cur_pos, SEEK_SET);
    return 0;
}

static void mov_free_encryption_index(MOVEncryptionIndex **index) {
    int i;
    if (!index || !*index) return;
    for (i = 0; i < (*index)->nb_encrypted_samples; i++) {
        av_encryption_info_free((*index)->encrypted_samples[i]);
    }
    av_freep(&(*index)->encrypted_samples);
    av_freep(&(*index)->auxiliary_info_sizes);
    av_freep(&(*index)->auxiliary_offsets);
    av_freep(index);
}

static void mov_free_stream_context(AVFormatContext *s, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;

    if (!sc || --sc->refcount) {
        st->priv_data = NULL;
        return;
    }

    av_freep(&sc->tts_data);
    for (int i = 0; i < sc->drefs_count; i++) {
        av_freep(&sc->drefs[i].path);
        av_freep(&sc->drefs[i].dir);
    }
    av_freep(&sc->drefs);

    sc->drefs_count = 0;

    if (!sc->pb_is_copied)
        ff_format_io_close(s, &sc->pb);

    sc->pb = NULL;
    av_freep(&sc->chunk_offsets);
    av_freep(&sc->stsc_data);
    av_freep(&sc->sample_sizes);
    av_freep(&sc->keyframes);
    av_freep(&sc->ctts_data);
    av_freep(&sc->stts_data);
    av_freep(&sc->sdtp_data);
    av_freep(&sc->stps_data);
    av_freep(&sc->elst_data);
    av_freep(&sc->rap_group);
    av_freep(&sc->sync_group);
    av_freep(&sc->sgpd_sync);
    av_freep(&sc->sample_offsets);
    av_freep(&sc->open_key_samples);
    av_freep(&sc->display_matrix);
    av_freep(&sc->index_ranges);

    if (sc->extradata)
        for (int i = 0; i < sc->stsd_count; i++)
            av_free(sc->extradata[i]);
    av_freep(&sc->extradata);
    av_freep(&sc->extradata_size);

    mov_free_encryption_index(&sc->cenc.encryption_index);
    av_encryption_info_free(sc->cenc.default_encrypted_sample);
    av_aes_ctr_free(sc->cenc.aes_ctr);

    av_freep(&sc->stereo3d);
    av_freep(&sc->spherical);
    av_freep(&sc->mastering);
    av_freep(&sc->coll);
    av_freep(&sc->ambient);

#if CONFIG_IAMFDEC
    if (sc->iamf)
        ff_iamf_read_deinit(sc->iamf);
#endif
    av_freep(&sc->iamf);
}

static int mov_read_close(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    int i, j;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        mov_free_stream_context(s, st);
    }

    av_freep(&mov->dv_demux);
    avformat_free_context(mov->dv_fctx);
    mov->dv_fctx = NULL;

    if (mov->meta_keys) {
        for (i = 1; i < mov->meta_keys_count; i++) {
            av_freep(&mov->meta_keys[i]);
        }
        av_freep(&mov->meta_keys);
    }

    av_freep(&mov->trex_data);
    av_freep(&mov->bitrates);

    for (i = 0; i < mov->frag_index.nb_items; i++) {
        MOVFragmentStreamInfo *frag = mov->frag_index.item[i].stream_info;
        for (j = 0; j < mov->frag_index.item[i].nb_stream_info; j++) {
            mov_free_encryption_index(&frag[j].encryption_index);
        }
        av_freep(&mov->frag_index.item[i].stream_info);
    }
    av_freep(&mov->frag_index.item);

    av_freep(&mov->aes_decrypt);
    av_freep(&mov->chapter_tracks);
    for (i = 0; i < mov->nb_heif_item; i++) {
        if (!mov->heif_item[i])
            continue;
        av_freep(&mov->heif_item[i]->name);
        av_freep(&mov->heif_item[i]->icc_profile);
        av_freep(&mov->heif_item[i]);
    }
    av_freep(&mov->heif_item);
    for (i = 0; i < mov->nb_heif_grid; i++) {
        av_freep(&mov->heif_grid[i].tile_id_list);
        av_freep(&mov->heif_grid[i].tile_idx_list);
        av_freep(&mov->heif_grid[i].tile_item_list);
    }
    av_freep(&mov->heif_grid);

    return 0;
}

static int tmcd_is_referenced(AVFormatContext *s, int tmcd_id)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            sc->timecode_track == tmcd_id)
            return 1;
    }
    return 0;
}

/* look for a tmcd track not referenced by any video track, and export it globally */
static void export_orphan_timecode(AVFormatContext *s)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        if (st->codecpar->codec_tag  == MKTAG('t','m','c','d') &&
            !tmcd_is_referenced(s, i + 1)) {
            AVDictionaryEntry *tcr = av_dict_get(st->metadata, "timecode", NULL, 0);
            if (tcr) {
                av_dict_set(&s->metadata, "timecode", tcr->value, 0);
                break;
            }
        }
    }
}

static int read_tfra(MOVContext *mov, AVIOContext *f)
{
    int version, fieldlength, i, j;
    int64_t pos = avio_tell(f);
    uint32_t size = avio_rb32(f);
    unsigned track_id, item_count;

    if (avio_rb32(f) != MKBETAG('t', 'f', 'r', 'a')) {
        return 1;
    }
    av_log(mov->fc, AV_LOG_VERBOSE, "found tfra\n");

    version = avio_r8(f);
    avio_rb24(f);
    track_id = avio_rb32(f);
    fieldlength = avio_rb32(f);
    item_count = avio_rb32(f);
    for (i = 0; i < item_count; i++) {
        int64_t time, offset;
        int index;
        MOVFragmentStreamInfo * frag_stream_info;

        if (avio_feof(f)) {
            return AVERROR_INVALIDDATA;
        }

        if (version == 1) {
            time   = avio_rb64(f);
            offset = avio_rb64(f);
        } else {
            time   = avio_rb32(f);
            offset = avio_rb32(f);
        }

        // The first sample of each stream in a fragment is always a random
        // access sample.  So it's entry in the tfra can be used as the
        // initial PTS of the fragment.
        index = update_frag_index(mov, offset);
        frag_stream_info = get_frag_stream_info(&mov->frag_index, index, track_id);
        if (frag_stream_info &&
            frag_stream_info->first_tfra_pts == AV_NOPTS_VALUE)
            frag_stream_info->first_tfra_pts = time;

        for (j = 0; j < ((fieldlength >> 4) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 2) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 0) & 3) + 1; j++)
            avio_r8(f);
    }

    avio_seek(f, pos + size, SEEK_SET);
    return 0;
}

static int mov_read_mfra(MOVContext *c, AVIOContext *f)
{
    int64_t stream_size = avio_size(f);
    int64_t original_pos = avio_tell(f);
    int64_t seek_ret;
    int ret = -1;
    if ((seek_ret = avio_seek(f, stream_size - 4, SEEK_SET)) < 0) {
        ret = seek_ret;
        goto fail;
    }
    c->mfra_size = avio_rb32(f);
    c->have_read_mfra_size = 1;
    if (!c->mfra_size || c->mfra_size > stream_size) {
        av_log(c->fc, AV_LOG_DEBUG, "doesn't look like mfra (unreasonable size)\n");
        goto fail;
    }
    if ((seek_ret = avio_seek(f, -((int64_t) c->mfra_size), SEEK_CUR)) < 0) {
        ret = seek_ret;
        goto fail;
    }
    if (avio_rb32(f) != c->mfra_size) {
        av_log(c->fc, AV_LOG_DEBUG, "doesn't look like mfra (size mismatch)\n");
        goto fail;
    }
    if (avio_rb32(f) != MKBETAG('m', 'f', 'r', 'a')) {
        av_log(c->fc, AV_LOG_DEBUG, "doesn't look like mfra (tag mismatch)\n");
        goto fail;
    }
    av_log(c->fc, AV_LOG_VERBOSE, "stream has mfra\n");
    do {
        ret = read_tfra(c, f);
        if (ret < 0)
            goto fail;
    } while (!ret);
    ret = 0;
    c->frag_index.complete = 1;
fail:
    seek_ret = avio_seek(f, original_pos, SEEK_SET);
    if (seek_ret < 0) {
        av_log(c->fc, AV_LOG_ERROR,
               "failed to seek back after looking for mfra\n");
        ret = seek_ret;
    }
    return ret;
}

static int set_icc_profile_from_item(AVPacketSideData **coded_side_data, int *nb_coded_side_data,
                                     const HEIFItem *item)
{
    AVPacketSideData *sd = av_packet_side_data_new(coded_side_data, nb_coded_side_data,
                                                   AV_PKT_DATA_ICC_PROFILE,
                                                   item->icc_profile_size, 0);
    if (!sd)
        return AVERROR(ENOMEM);

    memcpy(sd->data, item->icc_profile, item->icc_profile_size);

    return 0;
}

static int set_display_matrix_from_item(AVPacketSideData **coded_side_data, int *nb_coded_side_data,
                                        const HEIFItem *item)
{
    int32_t *matrix;
    AVPacketSideData *sd = av_packet_side_data_new(coded_side_data,
                                                   nb_coded_side_data,
                                                   AV_PKT_DATA_DISPLAYMATRIX,
                                                   9 * sizeof(*matrix), 0);
    if (!sd)
        return AVERROR(ENOMEM);

    matrix = (int32_t*)sd->data;
    /* rotation is in the counter-clockwise direction whereas
     * av_display_rotation_set() expects its argument to be
     * oriented clockwise, so we need to negate it. */
    av_display_rotation_set(matrix, -item->rotation);
    av_display_matrix_flip(matrix, item->hflip, item->vflip);

    return 0;
}

static int read_image_grid(AVFormatContext *s, const HEIFGrid *grid,
                           AVStreamGroupTileGrid *tile_grid)
{
    MOVContext *c = s->priv_data;
    const HEIFItem *item = grid->item;
    int64_t offset = 0, pos = avio_tell(s->pb);
    int x = 0, y = 0, i = 0;
    int tile_rows, tile_cols;
    int flags, size;

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(c->fc, AV_LOG_INFO, "grid box with non seekable input\n");
        return AVERROR_PATCHWELCOME;
    }
    if (item->is_idat_relative) {
        if (!c->idat_offset) {
            av_log(c->fc, AV_LOG_ERROR, "missing idat box required by the image grid\n");
            return AVERROR_INVALIDDATA;
        }
        offset = c->idat_offset;
    }

    avio_seek(s->pb, item->extent_offset + offset, SEEK_SET);

    avio_r8(s->pb);    /* version */
    flags = avio_r8(s->pb);

    tile_rows = avio_r8(s->pb) + 1;
    tile_cols = avio_r8(s->pb) + 1;
    /* actual width and height of output image */
    tile_grid->width  = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
    tile_grid->height = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);

    /* ICC profile */
    if (item->icc_profile_size) {
        int ret = set_icc_profile_from_item(&tile_grid->coded_side_data,
                                            &tile_grid->nb_coded_side_data, item);
        if (ret < 0)
            return ret;
    }
    /* rotation */
    if (item->rotation || item->hflip || item->vflip) {
        int ret = set_display_matrix_from_item(&tile_grid->coded_side_data,
                                               &tile_grid->nb_coded_side_data, item);
        if (ret < 0)
            return ret;
    }

    av_log(c->fc, AV_LOG_TRACE, "grid: grid_rows %d grid_cols %d output_width %d output_height %d\n",
           tile_rows, tile_cols, tile_grid->width, tile_grid->height);

    avio_seek(s->pb, pos, SEEK_SET);

    size = tile_rows * tile_cols;
    tile_grid->nb_tiles = grid->nb_tiles;

    if (tile_grid->nb_tiles != size)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < tile_cols; i++)
        tile_grid->coded_width  += grid->tile_item_list[i]->width;
    for (int i = 0; i < size; i += tile_cols)
        tile_grid->coded_height += grid->tile_item_list[i]->height;

    tile_grid->offsets = av_calloc(tile_grid->nb_tiles, sizeof(*tile_grid->offsets));
    if (!tile_grid->offsets)
        return AVERROR(ENOMEM);

    while (y < tile_grid->coded_height) {
        int left_col = i;

        while (x < tile_grid->coded_width) {
            if (i == tile_grid->nb_tiles)
                return AVERROR_INVALIDDATA;

            tile_grid->offsets[i].idx        = grid->tile_idx_list[i];
            tile_grid->offsets[i].horizontal = x;
            tile_grid->offsets[i].vertical   = y;

            x += grid->tile_item_list[i++]->width;
        }

        if (x > tile_grid->coded_width) {
            av_log(c->fc, AV_LOG_ERROR, "Non uniform HEIF tiles\n");
            return AVERROR_INVALIDDATA;
        }

        x  = 0;
        y += grid->tile_item_list[left_col]->height;
    }

    if (y > tile_grid->coded_height || i != tile_grid->nb_tiles) {
        av_log(c->fc, AV_LOG_ERROR, "Non uniform HEIF tiles\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int read_image_iovl(AVFormatContext *s, const HEIFGrid *grid,
                           AVStreamGroupTileGrid *tile_grid)
{
    MOVContext *c = s->priv_data;
    const HEIFItem *item = grid->item;
    uint16_t canvas_fill_value[4];
    int64_t offset = 0, pos = avio_tell(s->pb);
    int ret = 0, flags;

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(c->fc, AV_LOG_INFO, "iovl box with non seekable input\n");
        return AVERROR_PATCHWELCOME;
    }
    if (item->is_idat_relative) {
        if (!c->idat_offset) {
            av_log(c->fc, AV_LOG_ERROR, "missing idat box required by the image overlay\n");
            return AVERROR_INVALIDDATA;
        }
        offset = c->idat_offset;
    }

    avio_seek(s->pb, item->extent_offset + offset, SEEK_SET);

    avio_r8(s->pb);    /* version */
    flags = avio_r8(s->pb);

    for (int i = 0; i < 4; i++)
        canvas_fill_value[i] = avio_rb16(s->pb);
    av_log(c->fc, AV_LOG_TRACE, "iovl: canvas_fill_value { %u, %u, %u, %u }\n",
           canvas_fill_value[0], canvas_fill_value[1],
           canvas_fill_value[2], canvas_fill_value[3]);
    for (int i = 0; i < 4; i++)
        tile_grid->background[i] = canvas_fill_value[i];

    /* actual width and height of output image */
    tile_grid->width        =
    tile_grid->coded_width  = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
    tile_grid->height       =
    tile_grid->coded_height = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);

    /* rotation */
    if (item->rotation || item->hflip || item->vflip) {
        int ret = set_display_matrix_from_item(&tile_grid->coded_side_data,
                                               &tile_grid->nb_coded_side_data, item);
        if (ret < 0)
            return ret;
    }

    /* ICC profile */
    if (item->icc_profile_size) {
        int ret = set_icc_profile_from_item(&tile_grid->coded_side_data,
                                            &tile_grid->nb_coded_side_data, item);
        if (ret < 0)
            return ret;
    }

    av_log(c->fc, AV_LOG_TRACE, "iovl: output_width %d, output_height %d\n",
           tile_grid->width, tile_grid->height);

    tile_grid->nb_tiles = grid->nb_tiles;
    tile_grid->offsets = av_malloc_array(tile_grid->nb_tiles, sizeof(*tile_grid->offsets));
    if (!tile_grid->offsets) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (int i = 0; i < tile_grid->nb_tiles; i++) {
        tile_grid->offsets[i].idx        = grid->tile_idx_list[i];
        tile_grid->offsets[i].horizontal = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
        tile_grid->offsets[i].vertical   = (flags & 1) ? avio_rb32(s->pb) : avio_rb16(s->pb);
        av_log(c->fc, AV_LOG_TRACE, "iovl: stream_idx[%d] %u, "
                                    "horizontal_offset[%d] %d, vertical_offset[%d] %d\n",
               i, tile_grid->offsets[i].idx,
               i, tile_grid->offsets[i].horizontal, i, tile_grid->offsets[i].vertical);
    }

fail:
    avio_seek(s->pb, pos, SEEK_SET);

    return ret;
}

static int mov_parse_tiles(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;

    for (int i = 0; i < mov->nb_heif_grid; i++) {
        AVStreamGroup *stg = avformat_stream_group_create(s, AV_STREAM_GROUP_PARAMS_TILE_GRID, NULL);
        AVStreamGroupTileGrid *tile_grid;
        const HEIFGrid *grid = &mov->heif_grid[i];
        int err, loop = 1;

        if (!stg)
            return AVERROR(ENOMEM);

        stg->id = grid->item->item_id;
        tile_grid = stg->params.tile_grid;

        for (int j = 0; j < grid->nb_tiles; j++) {
            int tile_id = grid->tile_id_list[j];
            int k;

            for (k = 0; k < mov->nb_heif_item; k++) {
                HEIFItem *item = mov->heif_item[k];
                AVStream *st;

                if (!item || item->item_id != tile_id)
                    continue;
                st = item->st;
                if (!st) {
                    av_log(s, AV_LOG_WARNING, "HEIF item id %d from grid id %d doesn't "
                                              "reference a stream\n",
                           tile_id, grid->item->item_id);
                    ff_remove_stream_group(s, stg);
                    loop = 0;
                    break;
                }

                grid->tile_item_list[j] = item;
                grid->tile_idx_list[j] = stg->nb_streams;

                err = avformat_stream_group_add_stream(stg, st);
                if (err < 0) {
                    int l;
                    if (err != AVERROR(EEXIST))
                        return err;

                    for (l = 0; l < stg->nb_streams; l++)
                        if (stg->streams[l]->index == st->index)
                            break;
                    av_assert0(l < stg->nb_streams);
                    grid->tile_idx_list[j] = l;
                }

                if (item->item_id != mov->primary_item_id)
                    st->disposition |= AV_DISPOSITION_DEPENDENT;
                break;
            }

            if (k == mov->nb_heif_item) {
                av_assert0(loop);
                av_log(s, AV_LOG_WARNING, "HEIF item id %d referenced by grid id %d doesn't "
                                          "exist\n",
                       tile_id, grid->item->item_id);
                ff_remove_stream_group(s, stg);
                loop = 0;
            }
            if (!loop)
                break;
        }

        if (!loop)
            continue;

        switch (grid->item->type) {
        case MKTAG('g','r','i','d'):
            err = read_image_grid(s, grid, tile_grid);
            break;
        case MKTAG('i','o','v','l'):
            err = read_image_iovl(s, grid, tile_grid);
            break;
        default:
            av_assert0(0);
        }
        if (err < 0)
            return err;


        if (grid->item->name)
            av_dict_set(&stg->metadata, "title", grid->item->name, 0);
        if (grid->item->item_id == mov->primary_item_id)
            stg->disposition |= AV_DISPOSITION_DEFAULT;
    }

    return 0;
}

static int mov_parse_heif_items(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    int err;

    for (int i = 0; i < mov->nb_heif_item; i++) {
        HEIFItem *item = mov->heif_item[i];
        MOVStreamContext *sc;
        AVStream *st;
        int64_t offset = 0;

        if (!item)
            continue;
        if (!item->st) {
            if (item->item_id == mov->thmb_item_id) {
                av_log(s, AV_LOG_ERROR, "HEIF thumbnail doesn't reference a stream\n");
                return AVERROR_INVALIDDATA;
            }
            continue;
        }
        if (item->is_idat_relative) {
            if (!mov->idat_offset) {
                av_log(s, AV_LOG_ERROR, "Missing idat box for item %d\n", item->item_id);
                return AVERROR_INVALIDDATA;
            }
            offset = mov->idat_offset;
        }

        st = item->st;
        sc = st->priv_data;
        st->codecpar->width  = item->width;
        st->codecpar->height = item->height;

        err = sanity_checks(s, sc, item->item_id);
        if (err)
            return AVERROR_INVALIDDATA;

        sc->sample_sizes[0]  = item->extent_length;
        sc->chunk_offsets[0] = item->extent_offset + offset;

        if (item->item_id == mov->primary_item_id)
            st->disposition |= AV_DISPOSITION_DEFAULT;

        if (item->rotation || item->hflip || item->vflip) {
            err = set_display_matrix_from_item(&st->codecpar->coded_side_data,
                                               &st->codecpar->nb_coded_side_data, item);
            if (err < 0)
                return err;
        }

        mov_build_index(mov, st);
    }

    if (mov->nb_heif_grid) {
        err = mov_parse_tiles(s);
        if (err < 0)
            return err;
    }

    return 0;
}

static AVStream *mov_find_reference_track(AVFormatContext *s, AVStream *st,
                                          int first_index)
{
    MOVStreamContext *sc = st->priv_data;

    if (sc->tref_id < 0)
        return NULL;

    for (int i = first_index; i < s->nb_streams; i++)
        if (s->streams[i]->id == sc->tref_id)
            return s->streams[i];

    return NULL;
}

static int mov_parse_lcevc_streams(AVFormatContext *s)
{
    int err;

    for (int i = 0; i < s->nb_streams; i++) {
        AVStreamGroup *stg;
        AVStream *st = s->streams[i];
        AVStream *st_base;
        MOVStreamContext *sc = st->priv_data;
        int j = 0;

        /* Find an enhancement stream. */
        if (st->codecpar->codec_id != AV_CODEC_ID_LCEVC ||
            !(sc->tref_flags & MOV_TREF_FLAG_ENHANCEMENT))
            continue;

        st->codecpar->codec_type = AVMEDIA_TYPE_DATA;

        stg = avformat_stream_group_create(s, AV_STREAM_GROUP_PARAMS_LCEVC, NULL);
        if (!stg)
            return AVERROR(ENOMEM);

        stg->id = st->id;
        stg->params.lcevc->width  = st->codecpar->width;
        stg->params.lcevc->height = st->codecpar->height;
        st->codecpar->width = 0;
        st->codecpar->height = 0;

        while (st_base = mov_find_reference_track(s, st, j)) {
            err = avformat_stream_group_add_stream(stg, st_base);
            if (err < 0)
                return err;

            j = st_base->index + 1;
        }
        if (!j) {
            av_log(s, AV_LOG_ERROR, "Failed to find base stream for enhancement stream\n");
            return AVERROR_INVALIDDATA;
        }

        err = avformat_stream_group_add_stream(stg, st);
        if (err < 0)
            return err;

        stg->params.lcevc->lcevc_index = stg->nb_streams - 1;
    }

    return 0;
}

static void fix_stream_ids(AVFormatContext *s)
{
    int highest_id = 0;

    for (int i = 0; i < s->nb_streams; i++) {
        const AVStream *st = s->streams[i];
        const MOVStreamContext *sc = st->priv_data;
        if (!sc->iamf)
            highest_id = FFMAX(highest_id, st->id);
    }
    highest_id += !highest_id;
    for (int i = 0; highest_id > 1 && i < s->nb_stream_groups; i++) {
        AVStreamGroup *stg = s->stream_groups[i];
        if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            continue;
        for (int j = 0; j < stg->nb_streams; j++) {
            AVStream *st = stg->streams[j];
            MOVStreamContext *sc = st->priv_data;
            st->id += highest_id;
            sc->iamf_stream_offset = highest_id;
        }
    }
}

static int mov_read_header(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    int j, err;
    MOVAtom atom = { AV_RL32("root") };
    int i;

    if (mov->decryption_key_len != 0 && mov->decryption_key_len != AES_CTR_KEY_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid decryption key len %d expected %d\n",
            mov->decryption_key_len, AES_CTR_KEY_SIZE);
        return AVERROR(EINVAL);
    }

    mov->fc = s;
    mov->trak_index = -1;
    mov->thmb_item_id = -1;
    mov->primary_item_id = -1;
    mov->cur_item_id = -1;
    /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
    if (pb->seekable & AVIO_SEEKABLE_NORMAL)
        atom.size = avio_size(pb);
    else
        atom.size = INT64_MAX;

    /* check MOV header */
    do {
        if (mov->moov_retry)
            avio_seek(pb, 0, SEEK_SET);
        if ((err = mov_read_default(mov, pb, atom)) < 0) {
            av_log(s, AV_LOG_ERROR, "error reading header\n");
            return err;
        }
    } while ((pb->seekable & AVIO_SEEKABLE_NORMAL) &&
             !mov->found_moov && (!mov->found_iloc || !mov->found_iinf) && !mov->moov_retry++);
    if (!mov->found_moov && !mov->found_iloc && !mov->found_iinf) {
        av_log(s, AV_LOG_ERROR, "moov atom not found\n");
        return AVERROR_INVALIDDATA;
    }
    av_log(mov->fc, AV_LOG_TRACE, "on_parse_exit_offset=%"PRId64"\n", avio_tell(pb));

    if (mov->found_iloc && mov->found_iinf) {
        err = mov_parse_heif_items(s);
        if (err < 0)
            return err;
    }
    // prevent iloc and iinf boxes from being parsed while reading packets.
    // this is needed because an iinf box may have been parsed but ignored
    // for having old infe boxes which create no streams.
    mov->found_iloc = mov->found_iinf = 1;

    if (pb->seekable & AVIO_SEEKABLE_NORMAL) {
        if (mov->nb_chapter_tracks > 0 && !mov->ignore_chapters)
            mov_read_chapters(s);
        for (i = 0; i < s->nb_streams; i++)
            if (s->streams[i]->codecpar->codec_tag == AV_RL32("tmcd")) {
                mov_read_timecode_track(s, s->streams[i]);
            } else if (s->streams[i]->codecpar->codec_tag == AV_RL32("rtmd")) {
                mov_read_rtmd_track(s, s->streams[i]);
            }
    }

    /* copy timecode metadata from tmcd tracks to the related video streams */
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;
        if (sc->timecode_track > 0) {
            AVDictionaryEntry *tcr;
            int tmcd_st_id = -1;

            for (j = 0; j < s->nb_streams; j++) {
                MOVStreamContext *sc2 = s->streams[j]->priv_data;
                if (sc2->id == sc->timecode_track)
                    tmcd_st_id = j;
            }

            if (tmcd_st_id < 0 || tmcd_st_id == i)
                continue;
            tcr = av_dict_get(s->streams[tmcd_st_id]->metadata, "timecode", NULL, 0);
            if (tcr)
                av_dict_set(&st->metadata, "timecode", tcr->value, 0);
        }
    }
    export_orphan_timecode(s);

    /* Create LCEVC stream groups. */
    err = mov_parse_lcevc_streams(s);
    if (err < 0)
        return err;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        FFStream *const sti = ffstream(st);
        MOVStreamContext *sc = st->priv_data;
        uint32_t dvdsub_clut[FF_DVDCLUT_CLUT_LEN] = {0};
        fix_timescale(mov, sc);
        if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            st->codecpar->codec_id   == AV_CODEC_ID_AAC) {
            sti->skip_samples = sc->start_pad;
        }
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && sc->nb_frames_for_fps > 0 && sc->duration_for_fps > 0)
            av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                      sc->time_scale*(int64_t)sc->nb_frames_for_fps, sc->duration_for_fps, INT_MAX);
        if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (st->codecpar->width <= 0 || st->codecpar->height <= 0) {
                st->codecpar->width  = sc->width;
                st->codecpar->height = sc->height;
            }
            if (st->codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE &&
                st->codecpar->extradata_size == FF_DVDCLUT_CLUT_SIZE) {

                for (j = 0; j < FF_DVDCLUT_CLUT_LEN; j++)
                    dvdsub_clut[j] = AV_RB32(st->codecpar->extradata + j * 4);

                err = ff_dvdclut_yuv_to_rgb(dvdsub_clut, FF_DVDCLUT_CLUT_SIZE);
                if (err < 0)
                    return err;

                av_freep(&st->codecpar->extradata);
                st->codecpar->extradata_size = 0;

                err = ff_dvdclut_palette_extradata_cat(dvdsub_clut, FF_DVDCLUT_CLUT_SIZE,
                                                       st->codecpar);
                if (err < 0)
                    return err;
            }
        }
        if (mov->handbrake_version &&
            mov->handbrake_version <= 1000000*0 + 1000*10 + 2 &&  // 0.10.2
            st->codecpar->codec_id == AV_CODEC_ID_MP3) {
            av_log(s, AV_LOG_VERBOSE, "Forcing full parsing for mp3 stream\n");
            sti->need_parsing = AVSTREAM_PARSE_FULL;
        }
    }

    if (mov->trex_data || mov->use_mfra_for > 0) {
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            MOVStreamContext *sc = st->priv_data;
            if (sc->duration_for_fps > 0) {
                /* Akin to sc->data_size * 8 * sc->time_scale / sc->duration_for_fps but accounting for overflows. */
                st->codecpar->bit_rate = av_rescale(sc->data_size, ((int64_t) sc->time_scale) * 8, sc->duration_for_fps);
                if (st->codecpar->bit_rate == INT64_MIN) {
                    av_log(s, AV_LOG_WARNING, "Overflow during bit rate calculation %"PRId64" * 8 * %d\n",
                           sc->data_size, sc->time_scale);
                    st->codecpar->bit_rate = 0;
                    if (s->error_recognition & AV_EF_EXPLODE)
                        return AVERROR_INVALIDDATA;
                }
            }
        }
    }

    for (i = 0; i < mov->bitrates_count && i < s->nb_streams; i++) {
        if (mov->bitrates[i]) {
            s->streams[i]->codecpar->bit_rate = mov->bitrates[i];
        }
    }

    ff_rfps_calculate(s);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;

        switch (st->codecpar->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            err = ff_replaygain_export(st, s->metadata);
            if (err < 0)
                return err;
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (sc->display_matrix) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_DISPLAYMATRIX,
                                             (uint8_t*)sc->display_matrix, sizeof(int32_t) * 9, 0))
                    return AVERROR(ENOMEM);

                sc->display_matrix = NULL;
            }
            if (sc->stereo3d) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_STEREO3D,
                                             (uint8_t *)sc->stereo3d, sc->stereo3d_size, 0))
                    return AVERROR(ENOMEM);

                sc->stereo3d = NULL;
            }
            if (sc->spherical) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_SPHERICAL,
                                                 (uint8_t *)sc->spherical, sc->spherical_size, 0))
                    return AVERROR(ENOMEM);

                sc->spherical = NULL;
            }
            if (sc->mastering) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
                                             (uint8_t *)sc->mastering, sc->mastering_size, 0))
                    return AVERROR(ENOMEM);

                sc->mastering = NULL;
            }
            if (sc->coll) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_CONTENT_LIGHT_LEVEL,
                                             (uint8_t *)sc->coll, sc->coll_size, 0))
                    return AVERROR(ENOMEM);

                sc->coll = NULL;
            }
            if (sc->ambient) {
                if (!av_packet_side_data_add(&st->codecpar->coded_side_data, &st->codecpar->nb_coded_side_data,
                                             AV_PKT_DATA_AMBIENT_VIEWING_ENVIRONMENT,
                                             (uint8_t *) sc->ambient, sc->ambient_size, 0))
                    return AVERROR(ENOMEM);

                sc->ambient = NULL;
            }
            break;
        }
    }

    fix_stream_ids(s);

    ff_configure_buffers_for_index(s, AV_TIME_BASE);

    for (i = 0; i < mov->frag_index.nb_items; i++)
        if (mov->frag_index.item[i].moof_offset <= mov->fragment.moof_offset)
            mov->frag_index.item[i].headers_read = 1;

    return 0;
}

static AVIndexEntry *mov_find_next_sample(AVFormatContext *s, AVStream **st)
{
    AVIndexEntry *sample = NULL;
    int64_t best_dts = INT64_MAX;
    int i;
    MOVContext *mov = s->priv_data;
    int no_interleave = !mov->interleaved_read || !(s->pb->seekable & AVIO_SEEKABLE_NORMAL);
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *avst = s->streams[i];
        FFStream *const avsti = ffstream(avst);
        MOVStreamContext *msc = avst->priv_data;
        if (msc->pb && msc->current_sample < avsti->nb_index_entries) {
            AVIndexEntry *current_sample = &avsti->index_entries[msc->current_sample];
            int64_t dts = av_rescale(current_sample->timestamp, AV_TIME_BASE, msc->time_scale);
            uint64_t dtsdiff = best_dts > dts ? best_dts - (uint64_t)dts : ((uint64_t)dts - best_dts);
            av_log(s, AV_LOG_TRACE, "stream %d, sample %d, dts %"PRId64"\n", i, msc->current_sample, dts);
            if (!sample || (no_interleave && current_sample->pos < sample->pos) ||
                ((s->pb->seekable & AVIO_SEEKABLE_NORMAL) &&
                 ((msc->pb != s->pb && dts < best_dts) || (msc->pb == s->pb && dts != AV_NOPTS_VALUE &&
                 ((dtsdiff <= AV_TIME_BASE && current_sample->pos < sample->pos) ||
                  (dtsdiff > AV_TIME_BASE && dts < best_dts && mov->interleaved_read)))))) {
                sample = current_sample;
                best_dts = dts;
                *st = avst;
            }
        }
    }
    return sample;
}

static int should_retry(AVIOContext *pb, int error_code) {
    if (error_code == AVERROR_EOF || avio_feof(pb))
        return 0;

    return 1;
}

static int mov_switch_root(AVFormatContext *s, int64_t target, int index)
{
    int ret;
    MOVContext *mov = s->priv_data;

    if (index >= 0 && index < mov->frag_index.nb_items)
        target = mov->frag_index.item[index].moof_offset;
    if (target >= 0 && avio_seek(s->pb, target, SEEK_SET) != target) {
        av_log(mov->fc, AV_LOG_ERROR, "root atom offset 0x%"PRIx64": partial file\n", target);
        return AVERROR_INVALIDDATA;
    }

    mov->next_root_atom = 0;
    if ((index < 0 && target >= 0) || index >= mov->frag_index.nb_items)
        index = search_frag_moof_offset(&mov->frag_index, target);
    if (index >= 0 && index < mov->frag_index.nb_items &&
        mov->frag_index.item[index].moof_offset == target) {
        if (index + 1 < mov->frag_index.nb_items)
            mov->next_root_atom = mov->frag_index.item[index + 1].moof_offset;
        if (mov->frag_index.item[index].headers_read)
            return 0;
        mov->frag_index.item[index].headers_read = 1;
    }

    mov->found_mdat = 0;

    ret = mov_read_default(mov, s->pb, (MOVAtom){ AV_RL32("root"), INT64_MAX });
    if (ret < 0)
        return ret;
    if (avio_feof(s->pb))
        return AVERROR_EOF;
    av_log(s, AV_LOG_TRACE, "read fragments, offset 0x%"PRIx64"\n", avio_tell(s->pb));

    return 1;
}

static int mov_change_extradata(AVStream *st, AVPacket *pkt)
{
    MOVStreamContext *sc = st->priv_data;
    uint8_t *side, *extradata;
    int extradata_size;

    /* Save the current index. */
    sc->last_stsd_index = sc->stsc_data[sc->stsc_index].id - 1;

    /* Notify the decoder that extradata changed. */
    extradata_size = sc->extradata_size[sc->last_stsd_index];
    extradata = sc->extradata[sc->last_stsd_index];
    if (st->discard != AVDISCARD_ALL && extradata_size > 0 && extradata) {
        side = av_packet_new_side_data(pkt,
                                       AV_PKT_DATA_NEW_EXTRADATA,
                                       extradata_size);
        if (!side)
            return AVERROR(ENOMEM);
        memcpy(side, extradata, extradata_size);
    }

    return 0;
}

static int get_eia608_packet(AVIOContext *pb, AVPacket *pkt, int src_size)
{
    /* We can't make assumptions about the structure of the payload,
       because it may include multiple cdat and cdt2 samples. */
    const uint32_t cdat = AV_RB32("cdat");
    const uint32_t cdt2 = AV_RB32("cdt2");
    int ret, out_size = 0;

    /* a valid payload must have size, 4cc, and at least 1 byte pair: */
    if (src_size < 10)
        return AVERROR_INVALIDDATA;

    /* avoid an int overflow: */
    if ((src_size - 8) / 2 >= INT_MAX / 3)
        return AVERROR_INVALIDDATA;

    ret = av_new_packet(pkt, ((src_size - 8) / 2) * 3);
    if (ret < 0)
        return ret;

    /* parse and re-format the c608 payload in one pass. */
    while (src_size >= 10) {
        const uint32_t atom_size = avio_rb32(pb);
        const uint32_t atom_type = avio_rb32(pb);
        const uint32_t data_size = atom_size - 8;
        const uint8_t cc_field =
            atom_type == cdat ? 1 :
            atom_type == cdt2 ? 2 :
            0;

        /* account for bytes consumed for atom size and type. */
        src_size -= 8;

        /* make sure the data size stays within the buffer boundaries. */
        if (data_size < 2 || data_size > src_size) {
            ret = AVERROR_INVALIDDATA;
            break;
        }

        /* make sure the data size is consistent with N byte pairs. */
        if (data_size % 2 != 0) {
            ret = AVERROR_INVALIDDATA;
            break;
        }

        if (!cc_field) {
            /* neither cdat or cdt2 ... skip it */
            avio_skip(pb, data_size);
            src_size -= data_size;
            continue;
        }

        for (uint32_t i = 0; i < data_size; i += 2) {
            pkt->data[out_size] = (0x1F << 3) | (1 << 2) | (cc_field - 1);
            pkt->data[out_size + 1] = avio_r8(pb);
            pkt->data[out_size + 2] = avio_r8(pb);
            out_size += 3;
            src_size -= 2;
        }
    }

    if (src_size > 0)
        /* skip any remaining unread portion of the input payload */
        avio_skip(pb, src_size);

    av_shrink_packet(pkt, out_size);
    return ret;
}

static int mov_finalize_packet(AVFormatContext *s, AVStream *st, AVIndexEntry *sample,
                                int64_t current_index, AVPacket *pkt)
{
    MOVStreamContext *sc = st->priv_data;

    pkt->stream_index = sc->ffindex;
    pkt->dts = sample->timestamp;
    if (sample->flags & AVINDEX_DISCARD_FRAME) {
        pkt->flags |= AV_PKT_FLAG_DISCARD;
    }
    if (sc->stts_count && sc->tts_index < sc->tts_count)
        pkt->duration = sc->tts_data[sc->tts_index].duration;
    if (sc->ctts_count && sc->tts_index < sc->tts_count) {
        pkt->pts = av_sat_add64(pkt->dts, av_sat_add64(sc->dts_shift, sc->tts_data[sc->tts_index].offset));
    } else {
        if (pkt->duration == 0) {
            int64_t next_dts = (sc->current_sample < ffstream(st)->nb_index_entries) ?
                ffstream(st)->index_entries[sc->current_sample].timestamp : st->duration;
            if (next_dts >= pkt->dts)
                pkt->duration = next_dts - pkt->dts;
        }
        pkt->pts = pkt->dts;
    }

    if (sc->tts_data && sc->tts_index < sc->tts_count) {
        /* update tts context */
        sc->tts_sample++;
        if (sc->tts_index < sc->tts_count &&
            sc->tts_data[sc->tts_index].count == sc->tts_sample) {
            sc->tts_index++;
            sc->tts_sample = 0;
        }
    }

    if (sc->sdtp_data && sc->current_sample <= sc->sdtp_count) {
        uint8_t sample_flags = sc->sdtp_data[sc->current_sample - 1];
        uint8_t sample_is_depended_on = (sample_flags >> 2) & 0x3;
        pkt->flags |= sample_is_depended_on == MOV_SAMPLE_DEPENDENCY_NO ? AV_PKT_FLAG_DISPOSABLE : 0;
    }
    pkt->flags |= sample->flags & AVINDEX_KEYFRAME ? AV_PKT_FLAG_KEY : 0;
    pkt->pos = sample->pos;

    /* Multiple stsd handling. */
    if (sc->stsc_data) {
        if (sc->stsc_data[sc->stsc_index].id > 0 &&
            sc->stsc_data[sc->stsc_index].id - 1 < sc->stsd_count &&
            sc->stsc_data[sc->stsc_index].id - 1 != sc->last_stsd_index) {
            int ret = mov_change_extradata(st, pkt);
            if (ret < 0)
                return ret;
        }

        /* Update the stsc index for the next sample */
        sc->stsc_sample++;
        if (mov_stsc_index_valid(sc->stsc_index, sc->stsc_count) &&
            mov_get_stsc_samples(sc, sc->stsc_index) == sc->stsc_sample) {
            sc->stsc_index++;
            sc->stsc_sample = 0;
        }
    }

    return 0;
}

static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    MOVStreamContext *sc;
    AVIndexEntry *sample;
    AVStream *st = NULL;
    FFStream *avsti = NULL;
    int64_t current_index;
    int ret;
    int i;
    mov->fc = s;
 retry:
    if (s->pb->pos == 0) {

        // Discard current fragment index
        if (mov->frag_index.allocated_size > 0) {
            for(int i = 0; i < mov->frag_index.nb_items; i++) {
                av_freep(&mov->frag_index.item[i].stream_info);
            }
            av_freep(&mov->frag_index.item);
            mov->frag_index.nb_items = 0;
            mov->frag_index.allocated_size = 0;
            mov->frag_index.current = -1;
            mov->frag_index.complete = 0;
        }

        for (i = 0; i < s->nb_streams; i++) {
            AVStream *avst = s->streams[i];
            MOVStreamContext *msc = avst->priv_data;

            // Clear current sample
            mov_current_sample_set(msc, 0);
            msc->tts_index = 0;

            // Discard current index entries
            avsti = ffstream(avst);
            if (avsti->index_entries_allocated_size > 0) {
                av_freep(&avsti->index_entries);
                avsti->index_entries_allocated_size = 0;
                avsti->nb_index_entries = 0;
            }
        }

        if ((ret = mov_switch_root(s, -1, -1)) < 0)
            return ret;
    }
    sample = mov_find_next_sample(s, &st);
    if (!sample || (mov->next_root_atom && sample->pos > mov->next_root_atom)) {
        if (!mov->next_root_atom)
            return AVERROR_EOF;
        if ((ret = mov_switch_root(s, mov->next_root_atom, -1)) < 0)
            return ret;
        goto retry;
    }
    sc = st->priv_data;
    /* must be done just before reading, to avoid infinite loop on sample */
    current_index = sc->current_index;
    mov_current_sample_inc(sc);

    if (mov->next_root_atom) {
        sample->pos = FFMIN(sample->pos, mov->next_root_atom);
        sample->size = FFMIN(sample->size, (mov->next_root_atom - sample->pos));
    }

    if (st->discard != AVDISCARD_ALL) {
        int64_t ret64 = avio_seek(sc->pb, sample->pos, SEEK_SET);
        if (ret64 != sample->pos) {
            av_log(mov->fc, AV_LOG_ERROR, "stream %d, offset 0x%"PRIx64": partial file\n",
                   sc->ffindex, sample->pos);
            if (should_retry(sc->pb, ret64)) {
                mov_current_sample_dec(sc);
            } else if (ret64 < 0) {
                return (int)ret64;
            }
            return AVERROR_INVALIDDATA;
        }

        if (st->discard == AVDISCARD_NONKEY && !(sample->flags & AVINDEX_KEYFRAME)) {
            av_log(mov->fc, AV_LOG_DEBUG, "Nonkey frame from stream %d discarded due to AVDISCARD_NONKEY\n", sc->ffindex);
            goto retry;
        }

        if (st->codecpar->codec_id == AV_CODEC_ID_EIA_608 && sample->size > 8)
            ret = get_eia608_packet(sc->pb, pkt, sample->size);
#if CONFIG_IAMFDEC
        else if (sc->iamf) {
            int64_t pts, dts, pos, duration;
            int flags, size = sample->size;
            ret = mov_finalize_packet(s, st, sample, current_index, pkt);
            pts = pkt->pts; dts = pkt->dts;
            pos = pkt->pos; flags = pkt->flags;
            duration = pkt->duration;
            while (!ret && size > 0) {
                ret = ff_iamf_read_packet(s, sc->iamf, sc->pb, size, sc->iamf_stream_offset, pkt);
                if (ret < 0) {
                    if (should_retry(sc->pb, ret))
                        mov_current_sample_dec(sc);
                    return ret;
                }
                size -= ret;
                pkt->pts = pts; pkt->dts = dts;
                pkt->pos = pos; pkt->flags |= flags;
                pkt->duration = duration;
                ret = ff_buffer_packet(s, pkt);
            }
            if (!ret)
                return FFERROR_REDO;
        }
#endif
        else
            ret = av_get_packet(sc->pb, pkt, sample->size);
        if (ret < 0) {
            if (should_retry(sc->pb, ret)) {
                mov_current_sample_dec(sc);
            }
            return ret;
        }
#if CONFIG_DV_DEMUXER
        if (mov->dv_demux && sc->dv_audio_container) {
            ret = avpriv_dv_produce_packet(mov->dv_demux, NULL, pkt->data, pkt->size, pkt->pos);
            av_packet_unref(pkt);
            if (ret < 0)
                return ret;
            ret = avpriv_dv_get_packet(mov->dv_demux, pkt);
            if (ret < 0)
                return ret;
        }
#endif
        if (sc->has_palette) {
            uint8_t *pal;

            pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
            if (!pal) {
                av_log(mov->fc, AV_LOG_ERROR, "Cannot append palette to packet\n");
            } else {
                memcpy(pal, sc->palette, AVPALETTE_SIZE);
                sc->has_palette = 0;
            }
        }
        if (st->codecpar->codec_id == AV_CODEC_ID_MP3 && !ffstream(st)->need_parsing && pkt->size > 4) {
            if (ff_mpa_check_header(AV_RB32(pkt->data)) < 0)
                ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL;
        }
    }

    ret = mov_finalize_packet(s, st, sample, current_index, pkt);
    if (ret < 0)
        return ret;

    if (st->discard == AVDISCARD_ALL)
        goto retry;

    if (mov->aax_mode)
        aax_filter(pkt->data, pkt->size, mov);

    ret = cenc_filter(mov, st, sc, pkt, current_index);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int mov_seek_fragment(AVFormatContext *s, AVStream *st, int64_t timestamp)
{
    MOVContext *mov = s->priv_data;
    int index;

    if (!mov->frag_index.complete)
        return 0;

    index = search_frag_timestamp(s, &mov->frag_index, st, timestamp);
    if (index < 0)
        index = 0;
    if (!mov->frag_index.item[index].headers_read)
        return mov_switch_root(s, -1, index);
    if (index + 1 < mov->frag_index.nb_items)
        mov->next_root_atom = mov->frag_index.item[index + 1].moof_offset;

    return 0;
}

static int is_open_key_sample(const MOVStreamContext *sc, int sample)
{
    // TODO: a bisect search would scale much better
    for (int i = 0; i < sc->open_key_samples_count; i++) {
        const int oks = sc->open_key_samples[i];
        if (oks == sample)
            return 1;
        if (oks > sample) /* list is monotically increasing so we can stop early */
            break;
    }
    return 0;
}

/*
 * Some key sample may be key frames but not IDR frames, so a random access to
 * them may not be allowed.
 */
static int can_seek_to_key_sample(AVStream *st, int sample, int64_t requested_pts)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int64_t key_sample_dts, key_sample_pts;

    if (st->codecpar->codec_id != AV_CODEC_ID_HEVC)
        return 1;

    if (sample >= sc->sample_offsets_count)
        return 1;

    key_sample_dts = sti->index_entries[sample].timestamp;
    key_sample_pts = key_sample_dts + sc->sample_offsets[sample] + sc->dts_shift;

    /*
     * If the sample needs to be presented before an open key sample, they may
     * not be decodable properly, even though they come after in decoding
     * order.
     */
    if (is_open_key_sample(sc, sample) && key_sample_pts > requested_pts)
        return 0;

    return 1;
}

static int mov_seek_stream(AVFormatContext *s, AVStream *st, int64_t timestamp, int flags)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int sample, time_sample, ret, next_ts, requested_sample;
    unsigned int i;

    // Here we consider timestamp to be PTS, hence try to offset it so that we
    // can search over the DTS timeline.
    timestamp -= (sc->min_corrected_pts + sc->dts_shift);

    ret = mov_seek_fragment(s, st, timestamp);
    if (ret < 0)
        return ret;

    for (;;) {
        sample = av_index_search_timestamp(st, timestamp, flags);
        av_log(s, AV_LOG_TRACE, "stream %d, timestamp %"PRId64", sample %d\n", st->index, timestamp, sample);
        if (sample < 0 && sti->nb_index_entries && timestamp < sti->index_entries[0].timestamp)
            sample = 0;
        if (sample < 0) /* not sure what to do */
            return AVERROR_INVALIDDATA;

        if (!sample || can_seek_to_key_sample(st, sample, timestamp))
            break;

        next_ts = timestamp - FFMAX(sc->min_sample_duration, 1);
        requested_sample = av_index_search_timestamp(st, next_ts, flags);

        // If we've reached a different sample trying to find a good pts to
        // seek to, give up searching because we'll end up seeking back to
        // sample 0 on every seek.
        if (sample != requested_sample && !can_seek_to_key_sample(st, requested_sample, next_ts))
            break;

        timestamp = next_ts;
    }

    mov_current_sample_set(sc, sample);
    av_log(s, AV_LOG_TRACE, "stream %d, found sample %d\n", st->index, sc->current_sample);
    /* adjust time to sample index */
    if (sc->tts_data) {
        time_sample = 0;
        for (i = 0; i < sc->tts_count; i++) {
            int next = time_sample + sc->tts_data[i].count;
            if (next > sc->current_sample) {
                sc->tts_index = i;
                sc->tts_sample = sc->current_sample - time_sample;
                break;
            }
            time_sample = next;
        }
    }

    /* adjust stsd index */
    if (sc->chunk_count) {
        time_sample = 0;
        for (i = 0; i < sc->stsc_count; i++) {
            int64_t next = time_sample + mov_get_stsc_samples(sc, i);
            if (next > sc->current_sample) {
                sc->stsc_index = i;
                sc->stsc_sample = sc->current_sample - time_sample;
                break;
            }
            av_assert0(next == (int)next);
            time_sample = next;
        }
    }

    return sample;
}

static int64_t mov_get_skip_samples(AVStream *st, int sample)
{
    MOVStreamContext *sc = st->priv_data;
    FFStream *const sti = ffstream(st);
    int64_t first_ts = sti->index_entries[0].timestamp;
    int64_t ts = sti->index_entries[sample].timestamp;
    int64_t off;

    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        return 0;

    /* compute skip samples according to stream start_pad, seek ts and first ts */
    off = av_rescale_q(ts - first_ts, st->time_base,
                       (AVRational){1, st->codecpar->sample_rate});
    return FFMAX(sc->start_pad - off, 0);
}

static int mov_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    MOVContext *mc = s->priv_data;
    AVStream *st;
    FFStream *sti;
    int sample;
    int i;

    if (stream_index >= s->nb_streams)
        return AVERROR_INVALIDDATA;

    st = s->streams[stream_index];
    sti = ffstream(st);
    sample = mov_seek_stream(s, st, sample_time, flags);
    if (sample < 0)
        return sample;

    if (mc->seek_individually) {
        /* adjust seek timestamp to found sample timestamp */
        int64_t seek_timestamp = sti->index_entries[sample].timestamp;
        sti->skip_samples = mov_get_skip_samples(st, sample);

        for (i = 0; i < s->nb_streams; i++) {
            AVStream *const st  = s->streams[i];
            FFStream *const sti = ffstream(st);
            int64_t timestamp;

            if (stream_index == i)
                continue;

            timestamp = av_rescale_q(seek_timestamp, s->streams[stream_index]->time_base, st->time_base);
            sample = mov_seek_stream(s, st, timestamp, flags);
            if (sample >= 0)
                sti->skip_samples = mov_get_skip_samples(st, sample);
        }
    } else {
        for (i = 0; i < s->nb_streams; i++) {
            MOVStreamContext *sc;
            st = s->streams[i];
            sc = st->priv_data;
            mov_current_sample_set(sc, 0);
        }
        while (1) {
            MOVStreamContext *sc;
            AVIndexEntry *entry = mov_find_next_sample(s, &st);
            if (!entry)
                return AVERROR_INVALIDDATA;
            sc = st->priv_data;
            if (sc->ffindex == stream_index && sc->current_sample == sample)
                break;
            mov_current_sample_inc(sc);
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(MOVContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption mov_options[] = {
    {"use_absolute_path",
        "allow using absolute path when opening alias, this is a possible security issue",
        OFFSET(use_absolute_path), AV_OPT_TYPE_BOOL, {.i64 = 0},
        0, 1, FLAGS},
    {"seek_streams_individually",
        "Seek each stream individually to the closest point",
        OFFSET(seek_individually), AV_OPT_TYPE_BOOL, { .i64 = 1 },
        0, 1, FLAGS},
    {"ignore_editlist", "Ignore the edit list atom.", OFFSET(ignore_editlist), AV_OPT_TYPE_BOOL, {.i64 = 0},
        0, 1, FLAGS},
    {"advanced_editlist",
        "Modify the AVIndex according to the editlists. Use this option to decode in the order specified by the edits.",
        OFFSET(advanced_editlist), AV_OPT_TYPE_BOOL, {.i64 = 1},
        0, 1, FLAGS},
    {"ignore_chapters", "", OFFSET(ignore_chapters), AV_OPT_TYPE_BOOL, {.i64 = 0},
        0, 1, FLAGS},
    {"use_mfra_for",
        "use mfra for fragment timestamps",
        OFFSET(use_mfra_for), AV_OPT_TYPE_INT, {.i64 = FF_MOV_FLAG_MFRA_AUTO},
        -1, FF_MOV_FLAG_MFRA_PTS, FLAGS,
        .unit = "use_mfra_for"},
    {"auto", "auto", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_AUTO}, 0, 0,
        FLAGS, .unit = "use_mfra_for" },
    {"dts", "dts", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_DTS}, 0, 0,
        FLAGS, .unit = "use_mfra_for" },
    {"pts", "pts", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_PTS}, 0, 0,
        FLAGS, .unit = "use_mfra_for" },
    {"use_tfdt", "use tfdt for fragment timestamps", OFFSET(use_tfdt), AV_OPT_TYPE_BOOL, {.i64 = 1},
        0, 1, FLAGS},
    { "export_all", "Export unrecognized metadata entries", OFFSET(export_all),
        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, .flags = FLAGS },
    { "export_xmp", "Export full XMP metadata", OFFSET(export_xmp),
        AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, .flags = FLAGS },
    { "activation_bytes", "Secret bytes for Audible AAX files", OFFSET(activation_bytes),
        AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "audible_key", "AES-128 Key for Audible AAXC files", OFFSET(audible_key),
        AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "audible_iv", "AES-128 IV for Audible AAXC files", OFFSET(audible_iv),
        AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "audible_fixed_key", // extracted from libAAX_SDK.so and AAXSDKWin.dll files!
        "Fixed key used for handling Audible AAX files", OFFSET(audible_fixed_key),
        AV_OPT_TYPE_BINARY, {.str="77214d4b196a87cd520045fd20a51d67"},
        .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "decryption_key", "The media decryption key (hex)", OFFSET(decryption_key), AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "enable_drefs", "Enable external track support.", OFFSET(enable_drefs), AV_OPT_TYPE_BOOL,
        {.i64 = 0}, 0, 1, FLAGS },
    { "max_stts_delta", "treat offsets above this value as invalid", OFFSET(max_stts_delta), AV_OPT_TYPE_INT, {.i64 = UINT_MAX-48000*10 }, 0, UINT_MAX, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "interleaved_read", "Interleave packets from multiple tracks at demuxer level", OFFSET(interleaved_read), AV_OPT_TYPE_BOOL, {.i64 = 1 }, 0, 1, .flags = AV_OPT_FLAG_DECODING_PARAM },

    { NULL },
};

static const AVClass mov_class = {
    .class_name = "mov,mp4,m4a,3gp,3g2,mj2",
    .item_name  = av_default_item_name,
    .option     = mov_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFInputFormat ff_mov_demuxer = {
    .p.name         = "mov,mp4,m4a,3gp,3g2,mj2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("QuickTime / MOV"),
    .p.priv_class   = &mov_class,
    .p.extensions   = "mov,mp4,m4a,3gp,3g2,mj2,psp,m4b,ism,ismv,isma,f4v,avif,heic,heif",
    .p.flags        = AVFMT_NO_BYTE_SEEK | AVFMT_SEEK_TO_PTS | AVFMT_SHOW_IDS,
    .priv_data_size = sizeof(MOVContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = mov_probe,
    .read_header    = mov_read_header,
    .read_packet    = mov_read_packet,
    .read_close     = mov_read_close,
    .read_seek      = mov_read_seek,
};
