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

#include <inttypes.h>
#include <limits.h>
#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "libavutil/mathematics.h"
#include "libavutil/time_internal.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/opt.h"
#include "libavutil/aes.h"
#include "libavutil/sha.h"
#include "libavutil/timecode.h"
#include "libavcodec/ac3tab.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
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
        if (c < 0x80 && p < end)
            *p++ = c;
        else if (p < end)
            PUT_UTF8(mac_to_unicode[c-0x80], t, if (p < end) *p++ = t;);
    }
    *p = 0;
    return p - dst;
}

static int mov_read_covr(MOVContext *c, AVIOContext *pb, int type, int len)
{
    AVPacket pkt;
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

    st = avformat_new_stream(c->fc, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    sc = av_mallocz(sizeof(*sc));
    if (!sc)
        return AVERROR(ENOMEM);
    st->priv_data = sc;

    ret = av_get_packet(pb, &pkt, len);
    if (ret < 0)
        return ret;

    st->disposition              |= AV_DISPOSITION_ATTACHED_PIC;

    st->attached_pic              = pkt;
    st->attached_pic.stream_index = st->index;
    st->attached_pic.flags       |= AV_PKT_FLAG_KEY;

    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id   = id;

    return 0;
}

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
        av_log(c->fc, AV_LOG_ERROR, "no space for coordinates left (%d)\n", len);
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

static int mov_read_udta_string(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    char tmp_key[5];
    char key2[32], language[4] = {0};
    char *str = NULL;
    const char *key = NULL;
    uint16_t langcode = 0;
    uint32_t data_type = 0, str_size, str_size_alloc;
    int (*parse)(MOVContext*, AVIOContext*, unsigned, const char*) = NULL;
    int raw = 0;

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
    case MKTAG( 'g','n','r','e'): key = "genre";
        parse = mov_metadata_gnre; break;
    case MKTAG( 'h','d','v','d'): key = "hd_video";
        parse = mov_metadata_int8_no_padding; break;
    case MKTAG( 'k','e','y','w'): key = "keywords";  break;
    case MKTAG( 'l','d','e','s'): key = "synopsis";  break;
    case MKTAG( 'l','o','c','i'):
        return mov_metadata_loci(c, pb, atom.size);
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
        if (tag == MKTAG('d','a','t','a') && data_size <= atom.size) {
            data_type = avio_rb32(pb); // type
            avio_rb32(pb); // unknown
            str_size = data_size - 16;
            atom.size -= 16;

            if (atom.type == MKTAG('c', 'o', 'v', 'r')) {
                int ret = mov_read_covr(c, pb, data_type, str_size);
                if (ret < 0) {
                    av_log(c->fc, AV_LOG_ERROR, "Error parsing cover art.\n");
                }
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
        snprintf(tmp_key, 5, "%.4s", (char*)&atom.type);
        key = tmp_key;
    }

    if (!key)
        return 0;
    if (atom.size < 0 || str_size >= INT_MAX/2)
        return AVERROR_INVALIDDATA;

    // worst-case requirement for output string in case of utf8 coded input
    str_size_alloc = (raw ? str_size : str_size * 2) + 1;
    str = av_mallocz(str_size_alloc);
    if (!str)
        return AVERROR(ENOMEM);

    if (parse)
        parse(c, pb, str_size, key);
    else {
        if (!raw && (data_type == 3 || (data_type == 0 && (langcode < 0x400 || langcode == 0x7fff)))) { // MAC Encoded
            mov_read_mac_string(c, pb, str_size, str, str_size_alloc);
        } else {
            int ret = ffio_read_size(pb, str, str_size);
            if (ret < 0) {
                av_free(str);
                return ret;
            }
            str[str_size] = 0;
        }
        c->fc->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
        av_dict_set(&c->fc->metadata, key, str, 0);
        if (*language && strcmp(language, "und")) {
            snprintf(key2, sizeof(key2), "%s-%s", key, language);
            av_dict_set(&c->fc->metadata, key2, str, 0);
        }
    }
    av_log(c->fc, AV_LOG_TRACE, "lang \"%3s\" ", language);
    av_log(c->fc, AV_LOG_TRACE, "tag \"%s\" value \"%s\" atom \"%.4s\" %d %"PRId64"\n",
            key, str, (char*)&atom.type, str_size_alloc, atom.size);

    av_freep(&str);
    return 0;
}

static int mov_read_chpl(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t start;
    int i, nb_chapters, str_len, version;
    char str[256+1];
    int ret;

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
    if (entries >  (atom.size - 1) / MIN_DATA_ENTRY_BOX_SIZE + 1 ||
        entries >= UINT_MAX / sizeof(*sc->drefs))
        return AVERROR_INVALIDDATA;
    av_free(sc->drefs);
    sc->drefs_count = 0;
    sc->drefs = av_mallocz(entries * sizeof(*sc->drefs));
    if (!sc->drefs)
        return AVERROR(ENOMEM);
    sc->drefs_count = entries;

    for (i = 0; i < sc->drefs_count; i++) {
        MOVDref *dref = &sc->drefs[i];
        uint32_t size = avio_rb32(pb);
        int64_t next = avio_tell(pb) + size - 4;

        if (size < 12)
            return AVERROR_INVALIDDATA;

        dref->type = avio_rl32(pb);
        avio_rb32(pb); // version + flags
        av_log(c->fc, AV_LOG_TRACE, "type %.4s size %d\n", (char*)&dref->type, size);

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
                if(avio_feof(pb))
                    return AVERROR_EOF;
                type = avio_rb16(pb);
                len = avio_rb16(pb);
                av_log(c->fc, AV_LOG_DEBUG, "type %d, len %d\n", type, len);
                if (len&1)
                    len += 1;
                if (type == 2 || type == 18) { // absolute path
                    av_free(dref->path);
                    dref->path = av_mallocz(len+1);
                    if (!dref->path)
                        return AVERROR(ENOMEM);

                    ret = ffio_read_size(pb, dref->path, len);
                    if (ret < 0) {
                        av_freep(&dref->path);
                        return ret;
                    }
                    if (type == 18) // no additional processing needed
                        continue;
                    if (len > volume_len && !strncmp(dref->path, dref->volume, volume_len)) {
                        len -= volume_len;
                        memmove(dref->path, dref->path+volume_len, len);
                        dref->path[len] = 0;
                    }
                    for (j = 0; j < len; j++)
                        if (dref->path[j] == ':')
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
        }
        avio_seek(pb, next, SEEK_SET);
    }
    return 0;
}

static int mov_read_hdlr(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    uint32_t type;
    uint32_t av_unused ctype;
    int64_t title_size;
    char *title_str;
    int ret;

    if (c->fc->nb_streams < 1) // meta before first trak
        return 0;

    st = c->fc->streams[c->fc->nb_streams-1];

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    /* component type */
    ctype = avio_rl32(pb);
    type = avio_rl32(pb); /* component subtype */

    av_log(c->fc, AV_LOG_TRACE, "ctype= %.4s (0x%08x)\n", (char*)&ctype, ctype);
    av_log(c->fc, AV_LOG_TRACE, "stype= %.4s\n", (char*)&type);

    if     (type == MKTAG('v','i','d','e'))
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    else if (type == MKTAG('s','o','u','n'))
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    else if (type == MKTAG('m','1','a',' '))
        st->codec->codec_id = AV_CODEC_ID_MP2;
    else if ((type == MKTAG('s','u','b','p')) || (type == MKTAG('c','l','c','p')))
        st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;

    avio_rb32(pb); /* component  manufacture */
    avio_rb32(pb); /* component flags */
    avio_rb32(pb); /* component flags mask */

    title_size = atom.size - 24;
    if (title_size > 0) {
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
            av_dict_set(&st->metadata, "handler_name", title_str + off, 0);
        }
        av_freep(&title_str);
    }

    return 0;
}

int ff_mov_read_esds(AVFormatContext *fc, AVIOContext *pb)
{
    AVStream *st;
    int tag;

    if (fc->nb_streams < 1)
        return 0;
    st = fc->streams[fc->nb_streams-1];

    avio_rb32(pb); /* version + flags */
    ff_mp4_read_descr(fc, pb, &tag);
    if (tag == MP4ESDescrTag) {
        ff_mp4_parse_es_descr(pb, NULL);
    } else
        avio_rb16(pb); /* ID */

    ff_mp4_read_descr(fc, pb, &tag);
    if (tag == MP4DecConfigDescrTag)
        ff_mp4_read_dec_config_descr(fc, st, pb);
    return 0;
}

static int mov_read_esds(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    return ff_mov_read_esds(c->fc, pb);
}

static int mov_read_dac3(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    enum AVAudioServiceType *ast;
    int ac3info, acmod, lfeon, bsmod;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    ast = (enum AVAudioServiceType*)ff_stream_new_side_data(st, AV_PKT_DATA_AUDIO_SERVICE_TYPE,
                                                            sizeof(*ast));
    if (!ast)
        return AVERROR(ENOMEM);

    ac3info = avio_rb24(pb);
    bsmod = (ac3info >> 14) & 0x7;
    acmod = (ac3info >> 11) & 0x7;
    lfeon = (ac3info >> 10) & 0x1;
    st->codec->channels = ((int[]){2,1,2,3,3,4,4,5})[acmod] + lfeon;
    st->codec->channel_layout = avpriv_ac3_channel_layout_tab[acmod];
    if (lfeon)
        st->codec->channel_layout |= AV_CH_LOW_FREQUENCY;
    *ast = bsmod;
    if (st->codec->channels > 1 && bsmod == 0x7)
        *ast = AV_AUDIO_SERVICE_TYPE_KARAOKE;

    st->codec->audio_service_type = *ast;

    return 0;
}

static int mov_read_dec3(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    enum AVAudioServiceType *ast;
    int eac3info, acmod, lfeon, bsmod;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    ast = (enum AVAudioServiceType*)ff_stream_new_side_data(st, AV_PKT_DATA_AUDIO_SERVICE_TYPE,
                                                            sizeof(*ast));
    if (!ast)
        return AVERROR(ENOMEM);

    /* No need to parse fields for additional independent substreams and its
     * associated dependent substreams since libavcodec's E-AC-3 decoder
     * does not support them yet. */
    avio_rb16(pb); /* data_rate and num_ind_sub */
    eac3info = avio_rb24(pb);
    bsmod = (eac3info >> 12) & 0x1f;
    acmod = (eac3info >>  9) & 0x7;
    lfeon = (eac3info >>  8) & 0x1;
    st->codec->channel_layout = avpriv_ac3_channel_layout_tab[acmod];
    if (lfeon)
        st->codec->channel_layout |= AV_CH_LOW_FREQUENCY;
    st->codec->channels = av_get_channel_layout_nb_channels(st->codec->channel_layout);
    *ast = bsmod;
    if (st->codec->channels > 1 && bsmod == 0x7)
        *ast = AV_AUDIO_SERVICE_TYPE_KARAOKE;

    st->codec->audio_service_type = *ast;

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

static int mov_read_wfex(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((ret = ff_get_wav_header(c->fc, pb, st->codec, atom.size, 0)) < 0)
        av_log(c->fc, AV_LOG_WARNING, "get_wav_header failed\n");

    return ret;
}

static int mov_read_pasp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    const int num = avio_rb32(pb);
    const int den = avio_rb32(pb);
    AVStream *st;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((st->sample_aspect_ratio.den != 1 || st->sample_aspect_ratio.num) && // default
        (den != st->sample_aspect_ratio.den || num != st->sample_aspect_ratio.num)) {
        av_log(c->fc, AV_LOG_WARNING,
               "sample aspect ratio already set to %d:%d, ignoring 'pasp' atom (%d:%d)\n",
               st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
               num, den);
    } else if (den != 0) {
        av_reduce(&st->sample_aspect_ratio.num, &st->sample_aspect_ratio.den,
                  num, den, 32767);
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
    struct AVSHA *sha;
    int i;
    int ret = 0;
    uint8_t *activation_bytes = c->activation_bytes;
    uint8_t *fixed_key = c->audible_fixed_key;

    c->aax_mode = 1;

    sha = av_sha_alloc();
    if (!sha)
        return AVERROR(ENOMEM);
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

    av_log(c->fc, AV_LOG_INFO, "[aax] file checksum == "); // required by external tools
    for (i = 0; i < 20; i++)
        av_log(sha, AV_LOG_INFO, "%02x", file_checksum[i]);
    av_log(c->fc, AV_LOG_INFO, "\n");

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

    if (strcmp(type, "qt  "))
        c->isom = 1;
    av_log(c->fc, AV_LOG_DEBUG, "ISO: File Type Major Brand: %.4s\n",(char *)&type);
    av_dict_set(&c->fc->metadata, "major_brand", type, 0);
    minor_ver = avio_rb32(pb); /* minor version */
    av_dict_set_int(&c->fc->metadata, "minor_version", minor_ver, 0);

    comp_brand_size = atom.size - 8;
    if (comp_brand_size < 0)
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
    av_dict_set(&c->fc->metadata, "compatible_brands", comp_brands_str, 0);
    av_freep(&comp_brands_str);

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

static int mov_read_moof(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    if (!c->has_looked_for_mfra && c->use_mfra_for > 0) {
        c->has_looked_for_mfra = 1;
        if (pb->seekable) {
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
    return mov_read_default(c, pb, atom);
}

static void mov_metadata_creation_time(AVDictionary **metadata, int64_t time)
{
    char buffer[32];
    if (time) {
        struct tm *ptm, tmbuf;
        time_t timet;
        if(time >= 2082844800)
            time -= 2082844800;  /* seconds between 1904-01-01 and Epoch */
        timet = time;
        ptm = gmtime_r(&timet, &tmbuf);
        if (!ptm) return;
        if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", ptm))
            av_dict_set(metadata, "creation_time", buffer, 0);
    }
}

static int mov_read_mdhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int version;
    char language[4] = {0};
    unsigned lang;
    int64_t creation_time;

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
    if (version == 1) {
        creation_time = avio_rb64(pb);
        avio_rb64(pb);
    } else {
        creation_time = avio_rb32(pb);
        avio_rb32(pb); /* modification time */
    }
    mov_metadata_creation_time(&st->metadata, creation_time);

    sc->time_scale = avio_rb32(pb);
    st->duration = (version == 1) ? avio_rb64(pb) : avio_rb32(pb); /* duration */

    lang = avio_rb16(pb); /* language */
    if (ff_mov_lang_to_iso639(lang, language))
        av_dict_set(&st->metadata, "language", language, 0);
    avio_rb16(pb); /* quality */

    return 0;
}

static int mov_read_mvhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t creation_time;
    int version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    if (version == 1) {
        creation_time = avio_rb64(pb);
        avio_rb64(pb);
    } else {
        creation_time = avio_rb32(pb);
        avio_rb32(pb); /* modification time */
    }
    mov_metadata_creation_time(&c->fc->metadata, creation_time);
    c->time_scale = avio_rb32(pb); /* time scale */

    av_log(c->fc, AV_LOG_TRACE, "time scale = %i\n", c->time_scale);

    c->duration = (version == 1) ? avio_rb64(pb) : avio_rb32(pb); /* duration */
    // set the AVCodecContext duration because the duration of individual tracks
    // may be inaccurate
    if (c->time_scale > 0 && !c->trex_data)
        c->fc->duration = av_rescale(c->duration, AV_TIME_BASE, c->time_scale);
    avio_rb32(pb); /* preferred scale */

    avio_rb16(pb); /* preferred volume */

    avio_skip(pb, 10); /* reserved */

    avio_skip(pb, 36); /* display matrix */

    avio_rb32(pb); /* preview time */
    avio_rb32(pb); /* preview duration */
    avio_rb32(pb); /* poster time */
    avio_rb32(pb); /* selection time */
    avio_rb32(pb); /* selection duration */
    avio_rb32(pb); /* current time */
    avio_rb32(pb); /* next track ID */

    return 0;
}

static int mov_read_enda(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    int little_endian;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    little_endian = avio_rb16(pb) & 0xFF;
    av_log(c->fc, AV_LOG_TRACE, "enda %d\n", little_endian);
    if (little_endian == 1) {
        switch (st->codec->codec_id) {
        case AV_CODEC_ID_PCM_S24BE:
            st->codec->codec_id = AV_CODEC_ID_PCM_S24LE;
            break;
        case AV_CODEC_ID_PCM_S32BE:
            st->codec->codec_id = AV_CODEC_ID_PCM_S32LE;
            break;
        case AV_CODEC_ID_PCM_F32BE:
            st->codec->codec_id = AV_CODEC_ID_PCM_F32LE;
            break;
        case AV_CODEC_ID_PCM_F64BE:
            st->codec->codec_id = AV_CODEC_ID_PCM_F64LE;
            break;
        default:
            break;
        }
    }
    return 0;
}

static int mov_read_colr(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    char color_parameter_type[5] = { 0 };
    uint16_t color_primaries, color_trc, color_matrix;
    int ret;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams - 1];

    ret = ffio_read_size(pb, color_parameter_type, 4);
    if (ret < 0)
        return ret;
    if (strncmp(color_parameter_type, "nclx", 4) &&
        strncmp(color_parameter_type, "nclc", 4)) {
        av_log(c->fc, AV_LOG_WARNING, "unsupported color_parameter_type %s\n",
               color_parameter_type);
        return 0;
    }

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
            st->codec->color_range = AVCOL_RANGE_JPEG;
        else
            st->codec->color_range = AVCOL_RANGE_MPEG;
        /* 14496-12 references JPEG XR specs (rather than the more complete
         * 23001-8) so some adjusting is required */
        if (color_primaries >= AVCOL_PRI_FILM)
            color_primaries = AVCOL_PRI_UNSPECIFIED;
        if ((color_trc >= AVCOL_TRC_LINEAR &&
             color_trc <= AVCOL_TRC_LOG_SQRT) ||
            color_trc >= AVCOL_TRC_BT2020_10)
            color_trc = AVCOL_TRC_UNSPECIFIED;
        if (color_matrix >= AVCOL_SPC_BT2020_NCL)
            color_matrix = AVCOL_SPC_UNSPECIFIED;
        st->codec->color_primaries = color_primaries;
        st->codec->color_trc = color_trc;
        st->codec->colorspace = color_matrix;
    } else if (!strncmp(color_parameter_type, "nclc", 4)) {
        /* color primaries, Table 4-4 */
        switch (color_primaries) {
        case 1: st->codec->color_primaries = AVCOL_PRI_BT709; break;
        case 5: st->codec->color_primaries = AVCOL_PRI_SMPTE170M; break;
        case 6: st->codec->color_primaries = AVCOL_PRI_SMPTE240M; break;
        }
        /* color transfer, Table 4-5 */
        switch (color_trc) {
        case 1: st->codec->color_trc = AVCOL_TRC_BT709; break;
        case 7: st->codec->color_trc = AVCOL_TRC_SMPTE240M; break;
        }
        /* color matrix, Table 4-6 */
        switch (color_matrix) {
        case 1: st->codec->colorspace = AVCOL_SPC_BT709; break;
        case 6: st->codec->colorspace = AVCOL_SPC_BT470BG; break;
        case 7: st->codec->colorspace = AVCOL_SPC_SMPTE240M; break;
        }
    }
    av_log(c->fc, AV_LOG_TRACE, "\n");

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
        av_log(NULL, AV_LOG_ERROR, "Unknown MOV field order 0x%04x\n", mov_field_order);
    }
    st->codec->field_order = decoded_field_order;

    return 0;
}

static int mov_realloc_extradata(AVCodecContext *codec, MOVAtom atom)
{
    int err = 0;
    uint64_t size = (uint64_t)codec->extradata_size + atom.size + 8 + AV_INPUT_BUFFER_PADDING_SIZE;
    if (size > INT_MAX || (uint64_t)atom.size > INT_MAX)
        return AVERROR_INVALIDDATA;
    if ((err = av_reallocp(&codec->extradata, size)) < 0) {
        codec->extradata_size = 0;
        return err;
    }
    codec->extradata_size = size - AV_INPUT_BUFFER_PADDING_SIZE;
    return 0;
}

/* Read a whole atom into the extradata return the size of the atom read, possibly truncated if != atom.size */
static int64_t mov_read_atom_into_extradata(MOVContext *c, AVIOContext *pb, MOVAtom atom,
                                        AVCodecContext *codec, uint8_t *buf)
{
    int64_t result = atom.size;
    int err;

    AV_WB32(buf    , atom.size + 8);
    AV_WL32(buf + 4, atom.type);
    err = ffio_read_size(pb, buf + 8, atom.size);
    if (err < 0) {
        codec->extradata_size -= atom.size;
        return err;
    } else if (err < atom.size) {
        av_log(c->fc, AV_LOG_WARNING, "truncated extradata\n");
        codec->extradata_size -= atom.size - err;
        result = err;
    }
    memset(buf + 8 + err, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    return result;
}

/* FIXME modify qdm2/svq3/h264 decoders to take full atom as extradata */
static int mov_read_extradata(MOVContext *c, AVIOContext *pb, MOVAtom atom,
                              enum AVCodecID codec_id)
{
    AVStream *st;
    uint64_t original_size;
    int err;

    if (c->fc->nb_streams < 1) // will happen with jp2 files
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if (st->codec->codec_id != codec_id)
        return 0; /* unexpected codec_id - don't mess with extradata */

    original_size = st->codec->extradata_size;
    err = mov_realloc_extradata(st->codec, atom);
    if (err)
        return err;

    err =  mov_read_atom_into_extradata(c, pb, atom, st->codec,  st->codec->extradata + original_size);
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
    return mov_read_extradata(c, pb, atom, AV_CODEC_ID_AVS);
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
    if(ret == 0)
        ret = mov_read_extradata(c, pb, atom, AV_CODEC_ID_DNXHD);
    return ret;
}

static int mov_read_targa_y216(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret = mov_read_extradata(c, pb, atom, AV_CODEC_ID_TARGA_Y216);

    if (!ret && c->fc->nb_streams >= 1) {
        AVCodecContext *avctx = c->fc->streams[c->fc->nb_streams-1]->codec;
        if (avctx->extradata_size >= 40) {
            avctx->height = AV_RB16(&avctx->extradata[36]);
            avctx->width  = AV_RB16(&avctx->extradata[38]);
        }
    }
    return ret;
}

static int mov_read_ares(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    if (c->fc->nb_streams >= 1) {
        AVCodecContext *codec = c->fc->streams[c->fc->nb_streams-1]->codec;
        if (codec->codec_tag == MKTAG('A', 'V', 'i', 'n') &&
            codec->codec_id == AV_CODEC_ID_H264 &&
            atom.size > 11) {
            avio_skip(pb, 10);
            /* For AVID AVCI50, force width of 1440 to be able to select the correct SPS and PPS */
            if (avio_rb16(pb) == 0xd4d)
                codec->width = 1440;
            return 0;
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
        AVCodecContext *codec = c->fc->streams[c->fc->nb_streams-1]->codec;
        if (codec->codec_id == AV_CODEC_ID_H264)
            return 0;
        if (atom.size == 16) {
            original_size = codec->extradata_size;
            ret = mov_realloc_extradata(codec, atom);
            if (!ret) {
                length =  mov_read_atom_into_extradata(c, pb, atom, codec, codec->extradata + original_size);
                if (length == atom.size) {
                    const uint8_t range_value = codec->extradata[original_size + 19];
                    switch (range_value) {
                    case 1:
                        codec->color_range = AVCOL_RANGE_MPEG;
                        break;
                    case 2:
                        codec->color_range = AVCOL_RANGE_JPEG;
                        break;
                    default:
                        av_log(c, AV_LOG_WARNING, "ignored unknown aclr value (%d)\n", range_value);
                        break;
                    }
                    ff_dlog(c, "color_range: %d\n", codec->color_range);
                } else {
                  /* For some reason the whole atom was not added to the extradata */
                  av_log(c, AV_LOG_ERROR, "aclr not decoded - incomplete atom\n");
                }
            } else {
                av_log(c, AV_LOG_ERROR, "aclr not decoded - unable to add atom to extradata\n");
            }
        } else {
            av_log(c, AV_LOG_WARNING, "aclr not decoded - unexpected size %"PRId64"\n", atom.size);
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

    if (st->codec->codec_id == AV_CODEC_ID_QDM2 ||
        st->codec->codec_id == AV_CODEC_ID_QDMC ||
        st->codec->codec_id == AV_CODEC_ID_SPEEX) {
        // pass all frma atom to codec, needed at least for QDMC and QDM2
        av_freep(&st->codec->extradata);
        ret = ff_get_extradata(st->codec, pb, atom.size);
        if (ret < 0)
            return ret;
    } else if (atom.size > 8) { /* to read frma, esds atoms */
        if (st->codec->codec_id == AV_CODEC_ID_ALAC && atom.size >= 24) {
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
            } else if (!st->codec->extradata_size) {
#define ALAC_EXTRADATA_SIZE 36
                st->codec->extradata = av_mallocz(ALAC_EXTRADATA_SIZE + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!st->codec->extradata)
                    return AVERROR(ENOMEM);
                st->codec->extradata_size = ALAC_EXTRADATA_SIZE;
                AV_WB32(st->codec->extradata    , ALAC_EXTRADATA_SIZE);
                AV_WB32(st->codec->extradata + 4, MKTAG('a','l','a','c'));
                AV_WB64(st->codec->extradata + 12, buffer);
                avio_read(pb, st->codec->extradata + 20, 16);
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

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];

    if ((uint64_t)atom.size > (1<<30))
        return AVERROR_INVALIDDATA;

    if (atom.size >= 10) {
        // Broken files created by legacy versions of libavformat will
        // wrap a whole fiel atom inside of a glbl atom.
        unsigned size = avio_rb32(pb);
        unsigned type = avio_rl32(pb);
        avio_seek(pb, -8, SEEK_CUR);
        if (type == MKTAG('f','i','e','l') && size == atom.size)
            return mov_read_default(c, pb, atom);
    }
    if (st->codec->extradata_size > 1 && st->codec->extradata) {
        av_log(c, AV_LOG_WARNING, "ignoring multiple glbl\n");
        return 0;
    }
    av_freep(&st->codec->extradata);
    ret = ff_get_extradata(st->codec, pb, atom.size);
    if (ret < 0)
        return ret;

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
    av_freep(&st->codec->extradata);
    ret = ff_get_extradata(st->codec, pb, atom.size - 7);
    if (ret < 0)
        return ret;

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
    av_freep(&st->codec->extradata);
    ret = ff_get_extradata(st->codec, pb, atom.size - 40);
    if (ret < 0)
        return ret;

    return 0;
}

static int mov_read_stco(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    entries = avio_rb32(pb);

    if (!entries)
        return 0;

    if (sc->chunk_offsets)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STCO atom\n");
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
        for (i = 0; i < entries && !pb->eof_reached; i++)
            sc->chunk_offsets[i] = avio_rb64(pb);
    else
        return AVERROR_INVALIDDATA;

    sc->chunk_count = i;

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}

/**
 * Compute codec id for 'lpcm' tag.
 * See CoreAudioTypes and AudioStreamBasicDescription at Apple.
 */
enum AVCodecID ff_mov_get_lpcm_codec_id(int bps, int flags)
{
    /* lpcm flags:
     * 0x1 = float
     * 0x2 = big-endian
     * 0x4 = signed
     */
    return ff_get_pcm_codec_id(bps, flags & 1, flags & 2, flags & 4 ? -1 : 0);
}

static int mov_codec_id(AVStream *st, uint32_t format)
{
    int id = ff_codec_get_id(ff_codec_movaudio_tags, format);

    if (id <= 0 &&
        ((format & 0xFFFF) == 'm' + ('s' << 8) ||
         (format & 0xFFFF) == 'T' + ('S' << 8)))
        id = ff_codec_get_id(ff_codec_wav_tags, av_bswap32(format) & 0xFFFF);

    if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO && id > 0) {
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    } else if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO &&
               /* skip old asf mpeg4 tag */
               format && format != MKTAG('m','p','4','s')) {
        id = ff_codec_get_id(ff_codec_movvideo_tags, format);
        if (id <= 0)
            id = ff_codec_get_id(ff_codec_bmp_tags, format);
        if (id > 0)
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        else if (st->codec->codec_type == AVMEDIA_TYPE_DATA ||
                    (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE &&
                    st->codec->codec_id == AV_CODEC_ID_NONE)) {
            id = ff_codec_get_id(ff_codec_movsubtitle_tags, format);
            if (id > 0)
                st->codec->codec_type = AVMEDIA_TYPE_SUBTITLE;
        }
    }

    st->codec->codec_tag = format;

    return id;
}

static void mov_parse_stsd_video(MOVContext *c, AVIOContext *pb,
                                 AVStream *st, MOVStreamContext *sc)
{
    uint8_t codec_name[32];
    unsigned int color_depth, len, j;
    int color_greyscale;
    int color_table_id;

    avio_rb16(pb); /* version */
    avio_rb16(pb); /* revision level */
    avio_rb32(pb); /* vendor */
    avio_rb32(pb); /* temporal quality */
    avio_rb32(pb); /* spatial quality */

    st->codec->width  = avio_rb16(pb); /* width */
    st->codec->height = avio_rb16(pb); /* height */

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
    if (!memcmp(codec_name, "Planar Y'CbCr 8-bit 4:2:0", 25)) {
        st->codec->codec_tag = MKTAG('I', '4', '2', '0');
        st->codec->width &= ~1;
        st->codec->height &= ~1;
    }
    /* Flash Media Server uses tag H263 with Sorenson Spark */
    if (st->codec->codec_tag == MKTAG('H','2','6','3') &&
        !memcmp(codec_name, "Sorenson H263", 13))
        st->codec->codec_id = AV_CODEC_ID_FLV1;

    st->codec->bits_per_coded_sample = avio_rb16(pb); /* depth */
    color_table_id = avio_rb16(pb); /* colortable id */
    av_log(c->fc, AV_LOG_TRACE, "depth %d, ctab id %d\n",
            st->codec->bits_per_coded_sample, color_table_id);
    /* figure out the palette situation */
    color_depth     = st->codec->bits_per_coded_sample & 0x1F;
    color_greyscale = st->codec->bits_per_coded_sample & 0x20;
    /* Do not create a greyscale palette for cinepak */
    if (color_greyscale && st->codec->codec_id == AV_CODEC_ID_CINEPAK)
        return;

    /* if the depth is 2, 4, or 8 bpp, file is palettized */
    if ((color_depth == 2) || (color_depth == 4) || (color_depth == 8)) {
        /* for palette traversal */
        unsigned int color_start, color_count, color_end;
        unsigned int a, r, g, b;

        if (color_greyscale) {
            int color_index, color_dec;
            /* compute the greyscale palette */
            st->codec->bits_per_coded_sample = color_depth;
            color_count = 1 << color_depth;
            color_index = 255;
            color_dec   = 256 / (color_count - 1);
            for (j = 0; j < color_count; j++) {
                r = g = b = color_index;
                sc->palette[j] = (0xFFU << 24) | (r << 16) | (g << 8) | (b);
                color_index -= color_dec;
                if (color_index < 0)
                    color_index = 0;
            }
        } else if (color_table_id) {
            const uint8_t *color_table;
            /* if flag bit 3 is set, use the default palette */
            color_count = 1 << color_depth;
            if (color_depth == 2)
                color_table = ff_qt_default_palette_4;
            else if (color_depth == 4)
                color_table = ff_qt_default_palette_16;
            else
                color_table = ff_qt_default_palette_256;

            for (j = 0; j < color_count; j++) {
                r = color_table[j * 3 + 0];
                g = color_table[j * 3 + 1];
                b = color_table[j * 3 + 2];
                sc->palette[j] = (0xFFU << 24) | (r << 16) | (g << 8) | (b);
            }
        } else {
            /* load the palette from the file */
            color_start = avio_rb32(pb);
            color_count = avio_rb16(pb);
            color_end   = avio_rb16(pb);
            if ((color_start <= 255) && (color_end <= 255)) {
                for (j = color_start; j <= color_end; j++) {
                    /* each A, R, G, or B component is 16 bits;
                        * only use the top 8 bits */
                    a = avio_r8(pb);
                    avio_r8(pb);
                    r = avio_r8(pb);
                    avio_r8(pb);
                    g = avio_r8(pb);
                    avio_r8(pb);
                    b = avio_r8(pb);
                    avio_r8(pb);
                    sc->palette[j] = (a << 24 ) | (r << 16) | (g << 8) | (b);
                }
            }
        }
        sc->has_palette = 1;
    }
}

static void mov_parse_stsd_audio(MOVContext *c, AVIOContext *pb,
                                 AVStream *st, MOVStreamContext *sc)
{
    int bits_per_sample, flags;
    uint16_t version = avio_rb16(pb);
    AVDictionaryEntry *compatible_brands = av_dict_get(c->fc->metadata, "compatible_brands", NULL, AV_DICT_MATCH_CASE);

    avio_rb16(pb); /* revision level */
    avio_rb32(pb); /* vendor */

    st->codec->channels              = avio_rb16(pb); /* channel count */
    st->codec->bits_per_coded_sample = avio_rb16(pb); /* sample size */
    av_log(c->fc, AV_LOG_TRACE, "audio channels %d\n", st->codec->channels);

    sc->audio_cid = avio_rb16(pb);
    avio_rb16(pb); /* packet size = 0 */

    st->codec->sample_rate = ((avio_rb32(pb) >> 16));

    // Read QT version 1 fields. In version 0 these do not exist.
    av_log(c->fc, AV_LOG_TRACE, "version =%d, isom =%d\n", version, c->isom);
    if (!c->isom ||
        (compatible_brands && strstr(compatible_brands->value, "qt  "))) {

        if (version == 1) {
            sc->samples_per_frame = avio_rb32(pb);
            avio_rb32(pb); /* bytes per packet */
            sc->bytes_per_frame = avio_rb32(pb);
            avio_rb32(pb); /* bytes per sample */
        } else if (version == 2) {
            avio_rb32(pb); /* sizeof struct only */
            st->codec->sample_rate = av_int2double(avio_rb64(pb));
            st->codec->channels    = avio_rb32(pb);
            avio_rb32(pb); /* always 0x7F000000 */
            st->codec->bits_per_coded_sample = avio_rb32(pb);

            flags = avio_rb32(pb); /* lpcm format specific flag */
            sc->bytes_per_frame   = avio_rb32(pb);
            sc->samples_per_frame = avio_rb32(pb);
            if (st->codec->codec_tag == MKTAG('l','p','c','m'))
                st->codec->codec_id =
                    ff_mov_get_lpcm_codec_id(st->codec->bits_per_coded_sample,
                                             flags);
        }
        if (version == 0 || (version == 1 && sc->audio_cid != -2)) {
            /* can't correctly handle variable sized packet as audio unit */
            switch (st->codec->codec_id) {
            case AV_CODEC_ID_MP2:
            case AV_CODEC_ID_MP3:
                st->need_parsing = AVSTREAM_PARSE_FULL;
                break;
            }
        }
    }

    switch (st->codec->codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
        if (st->codec->bits_per_coded_sample == 16)
            st->codec->codec_id = AV_CODEC_ID_PCM_S16BE;
        break;
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
        if (st->codec->bits_per_coded_sample == 8)
            st->codec->codec_id = AV_CODEC_ID_PCM_S8;
        else if (st->codec->bits_per_coded_sample == 24)
            st->codec->codec_id =
                st->codec->codec_id == AV_CODEC_ID_PCM_S16BE ?
                AV_CODEC_ID_PCM_S24BE : AV_CODEC_ID_PCM_S24LE;
        else if (st->codec->bits_per_coded_sample == 32)
             st->codec->codec_id =
                st->codec->codec_id == AV_CODEC_ID_PCM_S16BE ?
                AV_CODEC_ID_PCM_S32BE : AV_CODEC_ID_PCM_S32LE;
        break;
    /* set values for old format before stsd version 1 appeared */
    case AV_CODEC_ID_MACE3:
        sc->samples_per_frame = 6;
        sc->bytes_per_frame   = 2 * st->codec->channels;
        break;
    case AV_CODEC_ID_MACE6:
        sc->samples_per_frame = 6;
        sc->bytes_per_frame   = 1 * st->codec->channels;
        break;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        sc->samples_per_frame = 64;
        sc->bytes_per_frame   = 34 * st->codec->channels;
        break;
    case AV_CODEC_ID_GSM:
        sc->samples_per_frame = 160;
        sc->bytes_per_frame   = 33;
        break;
    default:
        break;
    }

    bits_per_sample = av_get_bits_per_sample(st->codec->codec_id);
    if (bits_per_sample) {
        st->codec->bits_per_coded_sample = bits_per_sample;
        sc->sample_size = (bits_per_sample >> 3) * st->codec->channels;
    }
}

static void mov_parse_stsd_subtitle(MOVContext *c, AVIOContext *pb,
                                    AVStream *st, MOVStreamContext *sc,
                                    int64_t size)
{
    // ttxt stsd contains display flags, justification, background
    // color, fonts, and default styles, so fake an atom to read it
    MOVAtom fake_atom = { .size = size };
    // mp4s contains a regular esds atom
    if (st->codec->codec_tag != AV_RL32("mp4s"))
        mov_read_glbl(c, pb, fake_atom);
    st->codec->width  = sc->width;
    st->codec->height = sc->height;
}

static uint32_t yuv_to_rgba(uint32_t ycbcr)
{
    uint8_t r, g, b;
    int y, cb, cr;

    y  = (ycbcr >> 16) & 0xFF;
    cr = (ycbcr >> 8)  & 0xFF;
    cb =  ycbcr        & 0xFF;

    b = av_clip_uint8((1164 * (y - 16)                     + 2018 * (cb - 128)) / 1000);
    g = av_clip_uint8((1164 * (y - 16) -  813 * (cr - 128) -  391 * (cb - 128)) / 1000);
    r = av_clip_uint8((1164 * (y - 16) + 1596 * (cr - 128)                    ) / 1000);

    return (r << 16) | (g << 8) | b;
}

static int mov_rewrite_dvd_sub_extradata(AVStream *st)
{
    char buf[256] = {0};
    uint8_t *src = st->codec->extradata;
    int i;

    if (st->codec->extradata_size != 64)
        return 0;

    if (st->codec->width > 0 &&  st->codec->height > 0)
        snprintf(buf, sizeof(buf), "size: %dx%d\n",
                 st->codec->width, st->codec->height);
    av_strlcat(buf, "palette: ", sizeof(buf));

    for (i = 0; i < 16; i++) {
        uint32_t yuv = AV_RB32(src + i * 4);
        uint32_t rgba = yuv_to_rgba(yuv);

        av_strlcatf(buf, sizeof(buf), "%06"PRIx32"%s", rgba, i != 15 ? ", " : "");
    }

    if (av_strlcat(buf, "\n", sizeof(buf)) >= sizeof(buf))
        return 0;

    av_freep(&st->codec->extradata);
    st->codec->extradata_size = 0;
    st->codec->extradata = av_mallocz(strlen(buf) + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!st->codec->extradata)
        return AVERROR(ENOMEM);
    st->codec->extradata_size = strlen(buf);
    memcpy(st->codec->extradata, buf, st->codec->extradata_size);

    return 0;
}

static int mov_parse_stsd_data(MOVContext *c, AVIOContext *pb,
                                AVStream *st, MOVStreamContext *sc,
                                int64_t size)
{
    int ret;

    if (st->codec->codec_tag == MKTAG('t','m','c','d')) {
        if ((int)size != size)
            return AVERROR(ENOMEM);

        ret = ff_get_extradata(st->codec, pb, size);
        if (ret < 0)
            return ret;
        if (size > 16) {
            MOVStreamContext *tmcd_ctx = st->priv_data;
            int val;
            val = AV_RB32(st->codec->extradata + 4);
            tmcd_ctx->tmcd_flags = val;
            if (val & 1)
                st->codec->flags2 |= AV_CODEC_FLAG2_DROP_FRAME_TIMECODE;
            st->codec->time_base.den = st->codec->extradata[16]; /* number of frame */
            st->codec->time_base.num = 1;
            /* adjust for per frame dur in counter mode */
            if (tmcd_ctx->tmcd_flags & 0x0008) {
                int timescale = AV_RB32(st->codec->extradata + 8);
                int framedur = AV_RB32(st->codec->extradata + 12);
                st->codec->time_base.den *= timescale;
                st->codec->time_base.num *= framedur;
            }
            if (size > 30) {
                uint32_t len = AV_RB32(st->codec->extradata + 18); /* name atom length */
                uint32_t format = AV_RB32(st->codec->extradata + 22);
                if (format == AV_RB32("name") && (int64_t)size >= (int64_t)len + 18) {
                    uint16_t str_size = AV_RB16(st->codec->extradata + 26); /* string length */
                    if (str_size > 0 && size >= (int)str_size + 26) {
                        char *reel_name = av_malloc(str_size + 1);
                        if (!reel_name)
                            return AVERROR(ENOMEM);
                        memcpy(reel_name, st->codec->extradata + 30, str_size);
                        reel_name[str_size] = 0; /* Add null terminator */
                        /* don't add reel_name if emtpy string */
                        if (*reel_name == 0) {
                            av_free(reel_name);
                        } else {
                            av_dict_set(&st->metadata, "reel_name", reel_name,  AV_DICT_DONT_STRDUP_VAL);
                        }
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
    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
        !st->codec->sample_rate && sc->time_scale > 1)
        st->codec->sample_rate = sc->time_scale;

    /* special codec parameters handling */
    switch (st->codec->codec_id) {
#if CONFIG_DV_DEMUXER
    case AV_CODEC_ID_DVAUDIO:
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
        st->codec->codec_id    = AV_CODEC_ID_PCM_S16LE;
        break;
#endif
    /* no ifdef since parameters are always those */
    case AV_CODEC_ID_QCELP:
        st->codec->channels = 1;
        // force sample rate for qcelp when not stored in mov
        if (st->codec->codec_tag != MKTAG('Q','c','l','p'))
            st->codec->sample_rate = 8000;
        // FIXME: Why is the following needed for some files?
        sc->samples_per_frame = 160;
        if (!sc->bytes_per_frame)
            sc->bytes_per_frame = 35;
        break;
    case AV_CODEC_ID_AMR_NB:
        st->codec->channels    = 1;
        /* force sample rate for amr, stsd in 3gp does not store sample rate */
        st->codec->sample_rate = 8000;
        break;
    case AV_CODEC_ID_AMR_WB:
        st->codec->channels    = 1;
        st->codec->sample_rate = 16000;
        break;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP3:
        /* force type after stsd for m1a hdlr */
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        break;
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_ADPCM_MS:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ILBC:
    case AV_CODEC_ID_MACE3:
    case AV_CODEC_ID_MACE6:
    case AV_CODEC_ID_QDM2:
        st->codec->block_align = sc->bytes_per_frame;
        break;
    case AV_CODEC_ID_ALAC:
        if (st->codec->extradata_size == 36) {
            st->codec->channels    = AV_RB8 (st->codec->extradata + 21);
            st->codec->sample_rate = AV_RB32(st->codec->extradata + 32);
        }
        break;
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_VC1:
        st->need_parsing = AVSTREAM_PARSE_FULL;
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
    int video_codec_id = ff_codec_get_id(ff_codec_movvideo_tags, format);

    if (codec_tag &&
         (codec_tag != format &&
          (c->fc->video_codec_id ? video_codec_id != c->fc->video_codec_id
                                 : codec_tag != MKTAG('j','p','e','g')))) {
        /* Multiple fourcc, we skip JPEG. This is not correct, we should
         * export it as a separate AVStream but this needs a few changes
         * in the MOV demuxer, patch welcome. */

        av_log(c->fc, AV_LOG_WARNING, "multiple fourcc not supported\n");
        avio_skip(pb, size);
        return 1;
    }
    if ( codec_tag == AV_RL32("avc1") ||
         codec_tag == AV_RL32("hvc1") ||
         codec_tag == AV_RL32("hev1")
    )
        av_log(c->fc, AV_LOG_WARNING, "Concatenated H.264 or H.265 might not play correctly.\n");

    return 0;
}

int ff_mov_read_stsd_entries(MOVContext *c, AVIOContext *pb, int entries)
{
    AVStream *st;
    MOVStreamContext *sc;
    int pseudo_stream_id;

    if (c->fc->nb_streams < 1)
        return 0;
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
        int64_t size = avio_rb32(pb); /* size */
        uint32_t format = avio_rl32(pb); /* data format */

        if (size >= 16) {
            avio_rb32(pb); /* reserved */
            avio_rb16(pb); /* reserved */
            dref_id = avio_rb16(pb);
        }else if (size <= 7){
            av_log(c->fc, AV_LOG_ERROR, "invalid size %"PRId64" in stsd\n", size);
            return AVERROR_INVALIDDATA;
        }

        if (mov_skip_multiple_stsd(c, pb, st->codec->codec_tag, format,
                                   size - (avio_tell(pb) - start_pos)))
            continue;

        sc->pseudo_stream_id = st->codec->codec_tag ? -1 : pseudo_stream_id;
        sc->dref_id= dref_id;

        id = mov_codec_id(st, format);

        av_log(c->fc, AV_LOG_TRACE,
               "size=%"PRId64" 4CC= %c%c%c%c/0x%08x codec_type=%d\n", size,
                (format >> 0) & 0xff, (format >> 8) & 0xff, (format >> 16) & 0xff,
                (format >> 24) & 0xff, format, st->codec->codec_type);

        if (st->codec->codec_type==AVMEDIA_TYPE_VIDEO) {
            st->codec->codec_id = id;
            mov_parse_stsd_video(c, pb, st, sc);
        } else if (st->codec->codec_type==AVMEDIA_TYPE_AUDIO) {
            st->codec->codec_id = id;
            mov_parse_stsd_audio(c, pb, st, sc);
        } else if (st->codec->codec_type==AVMEDIA_TYPE_SUBTITLE){
            st->codec->codec_id = id;
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
    }

    if (pb->eof_reached)
        return AVERROR_EOF;

    return mov_finalize_stsd_codec(c, pb, st, sc);
}

static int mov_read_stsd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int entries;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    return ff_mov_read_stsd_entries(c, pb, entries);
}

static int mov_read_stsc(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "track[%i].stsc.entries = %i\n", c->fc->nb_streams-1, entries);

    if (!entries)
        return 0;
    if (sc->stsc_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STSC atom\n");
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

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}

static int mov_read_stps(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned i, entries;

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
        //av_log(c->fc, AV_LOG_TRACE, "stps %d\n", sc->stps_data[i]);
    }

    sc->stps_count = i;

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}

static int mov_read_stss(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */

    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "keyframe_count = %d\n", entries);

    if (!entries)
    {
        sc->keyframe_absent = 1;
        if (!st->need_parsing && st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
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
        //av_log(c->fc, AV_LOG_TRACE, "keyframes[]=%d\n", sc->keyframes[i]);
    }

    sc->keyframe_count = i;

    if (pb->eof_reached)
        return AVERROR_EOF;

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

    av_log(c->fc, AV_LOG_TRACE, "sample_size = %d sample_count = %d\n", sc->sample_size, entries);

    sc->sample_count = entries;
    if (sample_size)
        return 0;

    if (field_size != 4 && field_size != 8 && field_size != 16 && field_size != 32) {
        av_log(c->fc, AV_LOG_ERROR, "Invalid sample field size %d\n", field_size);
        return AVERROR_INVALIDDATA;
    }

    if (!entries)
        return 0;
    if (entries >= (UINT_MAX - 4) / field_size)
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
        return ret;
    }

    init_get_bits(&gb, buf, 8*num_bytes);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        sc->sample_sizes[i] = get_bits_long(&gb, field_size);
        sc->data_size += sc->sample_sizes[i];
    }

    sc->sample_count = i;

    av_free(buf);

    if (pb->eof_reached)
        return AVERROR_EOF;

    return 0;
}

static int mov_read_stts(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;
    int64_t duration=0;
    int64_t total_sample_count=0;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "track[%i].stts.entries = %i\n",
            c->fc->nb_streams-1, entries);

    if (sc->stts_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated STTS atom\n");
    av_free(sc->stts_data);
    sc->stts_count = 0;
    sc->stts_data = av_malloc_array(entries, sizeof(*sc->stts_data));
    if (!sc->stts_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        int sample_duration;
        int sample_count;

        sample_count=avio_rb32(pb);
        sample_duration = avio_rb32(pb);

        if (sample_count < 0) {
            av_log(c->fc, AV_LOG_ERROR, "Invalid sample_count=%d\n", sample_count);
            return AVERROR_INVALIDDATA;
        }
        sc->stts_data[i].count= sample_count;
        sc->stts_data[i].duration= sample_duration;

        av_log(c->fc, AV_LOG_TRACE, "sample_count=%d, sample_duration=%d\n",
                sample_count, sample_duration);

        if (   i+1 == entries
            && i
            && sample_count == 1
            && total_sample_count > 100
            && sample_duration/10 > duration / total_sample_count)
            sample_duration = duration / total_sample_count;
        duration+=(int64_t)sample_duration*sample_count;
        total_sample_count+=sample_count;
    }

    sc->stts_count = i;

    sc->duration_for_fps  += duration;
    sc->nb_frames_for_fps += total_sample_count;

    if (pb->eof_reached)
        return AVERROR_EOF;

    st->nb_frames= total_sample_count;
    if (duration)
        st->duration= duration;
    sc->track_end = duration;
    return 0;
}

static void mov_update_dts_shift(MOVStreamContext *sc, int duration)
{
    if (duration < 0) {
        sc->dts_shift = FFMAX(sc->dts_shift, -duration);
    }
}

static int mov_read_ctts(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    entries = avio_rb32(pb);

    av_log(c->fc, AV_LOG_TRACE, "track[%i].ctts.entries = %i\n", c->fc->nb_streams-1, entries);

    if (!entries)
        return 0;
    if (entries >= UINT_MAX / sizeof(*sc->ctts_data))
        return AVERROR_INVALIDDATA;
    av_freep(&sc->ctts_data);
    sc->ctts_data = av_realloc(NULL, entries * sizeof(*sc->ctts_data));
    if (!sc->ctts_data)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        int count    =avio_rb32(pb);
        int duration =avio_rb32(pb);

        sc->ctts_data[i].count   = count;
        sc->ctts_data[i].duration= duration;

        av_log(c->fc, AV_LOG_TRACE, "count=%d, duration=%d\n",
                count, duration);

        if (FFNABS(duration) < -(1<<28) && i+2<entries) {
            av_log(c->fc, AV_LOG_WARNING, "CTTS invalid\n");
            av_freep(&sc->ctts_data);
            sc->ctts_count = 0;
            return 0;
        }

        if (i+2<entries)
            mov_update_dts_shift(sc, duration);
    }

    sc->ctts_count = i;

    if (pb->eof_reached)
        return AVERROR_EOF;

    av_log(c->fc, AV_LOG_TRACE, "dts shift %d\n", sc->dts_shift);

    return 0;
}

static int mov_read_sbgp(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    unsigned int i, entries;
    uint8_t version;
    uint32_t grouping_type;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    grouping_type = avio_rl32(pb);
    if (grouping_type != MKTAG( 'r','a','p',' '))
        return 0; /* only support 'rap ' grouping */
    if (version == 1)
        avio_rb32(pb); /* grouping_type_parameter */

    entries = avio_rb32(pb);
    if (!entries)
        return 0;
    if (sc->rap_group)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated SBGP atom\n");
    av_free(sc->rap_group);
    sc->rap_group_count = 0;
    sc->rap_group = av_malloc_array(entries, sizeof(*sc->rap_group));
    if (!sc->rap_group)
        return AVERROR(ENOMEM);

    for (i = 0; i < entries && !pb->eof_reached; i++) {
        sc->rap_group[i].count = avio_rb32(pb); /* sample_count */
        sc->rap_group[i].index = avio_rb32(pb); /* group_description_index */
    }

    sc->rap_group_count = i;

    return pb->eof_reached ? AVERROR_EOF : 0;
}

static void mov_build_index(MOVContext *mov, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    int64_t current_offset;
    int64_t current_dts = 0;
    unsigned int stts_index = 0;
    unsigned int stsc_index = 0;
    unsigned int stss_index = 0;
    unsigned int stps_index = 0;
    unsigned int i, j;
    uint64_t stream_size = 0;

    if (sc->elst_count) {
        int i, edit_start_index = 0, unsupported = 0;
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
            } else
                unsupported = 1;
        }
        if (unsupported)
            av_log(mov->fc, AV_LOG_WARNING, "multiple edit list entries, "
                   "a/v desync might occur, patch welcome\n");

        /* adjust first dts according to edit list */
        if ((empty_duration || start_time) && mov->time_scale > 0) {
            if (empty_duration)
                empty_duration = av_rescale(empty_duration, sc->time_scale, mov->time_scale);
            sc->time_offset = start_time - empty_duration;
            current_dts = -sc->time_offset;
            if (sc->ctts_count>0 && sc->stts_count>0 &&
                sc->ctts_data[0].duration / FFMAX(sc->stts_data[0].duration, 1) > 16) {
                /* more than 16 frames delay, dts are likely wrong
                   this happens with files created by iMovie */
                sc->wrong_dts = 1;
                st->codec->has_b_frames = 1;
            }
        }
    }

    /* only use old uncompressed audio chunk demuxing when stts specifies it */
    if (!(st->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
          sc->stts_count == 1 && sc->stts_data[0].duration == 1)) {
        unsigned int current_sample = 0;
        unsigned int stts_sample = 0;
        unsigned int sample_size;
        unsigned int distance = 0;
        unsigned int rap_group_index = 0;
        unsigned int rap_group_sample = 0;
        int64_t last_dts = 0;
        int64_t dts_correction = 0;
        int rap_group_present = sc->rap_group_count && sc->rap_group;
        int key_off = (sc->keyframe_count && sc->keyframes[0] > 0) || (sc->stps_count && sc->stps_data[0] > 0);

        current_dts -= sc->dts_shift;
        last_dts     = current_dts;

        if (!sc->sample_count || st->nb_index_entries)
            return;
        if (sc->sample_count >= UINT_MAX / sizeof(*st->index_entries) - st->nb_index_entries)
            return;
        if (av_reallocp_array(&st->index_entries,
                              st->nb_index_entries + sc->sample_count,
                              sizeof(*st->index_entries)) < 0) {
            st->nb_index_entries = 0;
            return;
        }
        st->index_entries_allocated_size = (st->nb_index_entries + sc->sample_count) * sizeof(*st->index_entries);

        for (i = 0; i < sc->chunk_count; i++) {
            int64_t next_offset = i+1 < sc->chunk_count ? sc->chunk_offsets[i+1] : INT64_MAX;
            current_offset = sc->chunk_offsets[i];
            while (stsc_index + 1 < sc->stsc_count &&
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
                    && (st->codec->codec_type == AVMEDIA_TYPE_AUDIO || (i==0 && j==0)))
                     keyframe = 1;
                if (keyframe)
                    distance = 0;
                sample_size = sc->stsz_sample_size > 0 ? sc->stsz_sample_size : sc->sample_sizes[current_sample];
                if (sc->pseudo_stream_id == -1 ||
                   sc->stsc_data[stsc_index].id - 1 == sc->pseudo_stream_id) {
                    AVIndexEntry *e = &st->index_entries[st->nb_index_entries++];
                    e->pos = current_offset;
                    e->timestamp = current_dts;
                    e->size = sample_size;
                    e->min_distance = distance;
                    e->flags = keyframe ? AVINDEX_KEYFRAME : 0;
                    av_log(mov->fc, AV_LOG_TRACE, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", "
                            "size %d, distance %d, keyframe %d\n", st->index, current_sample,
                            current_offset, current_dts, sample_size, distance, keyframe);
                    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && st->nb_index_entries < 100)
                        ff_rfps_add_frame(mov->fc, st, current_dts);
                }

                current_offset += sample_size;
                stream_size += sample_size;

                /* A negative sample duration is invalid based on the spec,
                 * but some samples need it to correct the DTS. */
                if (sc->stts_data[stts_index].duration < 0) {
                    av_log(mov->fc, AV_LOG_WARNING,
                           "Invalid SampleDelta %d in STTS, at %d st:%d\n",
                           sc->stts_data[stts_index].duration, stts_index,
                           st->index);
                    dts_correction += sc->stts_data[stts_index].duration - 1;
                    sc->stts_data[stts_index].duration = 1;
                }
                current_dts += sc->stts_data[stts_index].duration;
                if (!dts_correction || current_dts + dts_correction > last_dts) {
                    current_dts += dts_correction;
                    dts_correction = 0;
                } else {
                    /* Avoid creating non-monotonous DTS */
                    dts_correction += current_dts - last_dts - 1;
                    current_dts = last_dts + 1;
                }
                last_dts = current_dts;
                distance++;
                stts_sample++;
                current_sample++;
                if (stts_index + 1 < sc->stts_count && stts_sample == sc->stts_data[stts_index].count) {
                    stts_sample = 0;
                    stts_index++;
                }
            }
        }
        if (st->duration > 0)
            st->codec->bit_rate = stream_size*8*sc->time_scale/st->duration;
    } else {
        unsigned chunk_samples, total = 0;

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

            if (i < sc->stsc_count - 1)
                chunk_count = sc->stsc_data[i+1].first - sc->stsc_data[i].first;
            else
                chunk_count = sc->chunk_count - (sc->stsc_data[i].first - 1);
            total += chunk_count * count;
        }

        av_log(mov->fc, AV_LOG_TRACE, "chunk count %d\n", total);
        if (total >= UINT_MAX / sizeof(*st->index_entries) - st->nb_index_entries)
            return;
        if (av_reallocp_array(&st->index_entries,
                              st->nb_index_entries + total,
                              sizeof(*st->index_entries)) < 0) {
            st->nb_index_entries = 0;
            return;
        }
        st->index_entries_allocated_size = (st->nb_index_entries + total) * sizeof(*st->index_entries);

        // populate index
        for (i = 0; i < sc->chunk_count; i++) {
            current_offset = sc->chunk_offsets[i];
            if (stsc_index + 1 < sc->stsc_count &&
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

                if (st->nb_index_entries >= total) {
                    av_log(mov->fc, AV_LOG_ERROR, "wrong chunk count %d\n", total);
                    return;
                }
                e = &st->index_entries[st->nb_index_entries++];
                e->pos = current_offset;
                e->timestamp = current_dts;
                e->size = size;
                e->min_distance = 0;
                e->flags = AVINDEX_KEYFRAME;
                av_log(mov->fc, AV_LOG_TRACE, "AVIndex stream %d, chunk %d, offset %"PRIx64", dts %"PRId64", "
                        "size %d, duration %d\n", st->index, i, current_offset, current_dts,
                        size, samples);

                current_offset += size;
                current_dts += samples;
                chunk_samples -= samples;
            }
        }
    }
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

static int mov_open_dref(MOVContext *c, AVIOContext **pb, const char *src, MOVDref *ref,
                         AVIOInterruptCB *int_cb)
{
    AVOpenCallback open_func = c->fc->open_cb;

    if (!open_func)
        open_func = ffio_open2_wrapper;

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
            if (!c->use_absolute_path && !c->fc->open_cb) {
                int same_origin = test_same_origin(src, filename);

                if (!same_origin) {
                    av_log(c->fc, AV_LOG_ERROR,
                        "Reference with mismatching origin, %s not tried for security reasons, "
                        "set demuxer option use_absolute_path to allow it anyway\n",
                        ref->path);
                    return AVERROR(ENOENT);
                }

                if(strstr(ref->path + l + 1, "..") ||
                   strstr(ref->path + l + 1, ":") ||
                   (ref->nlvl_from > 1 && same_origin < 0) ||
                   (filename[0] == '/' && src_path == src))
                    return AVERROR(ENOENT);
            }

            if (strlen(filename) + 1 == sizeof(filename))
                return AVERROR(ENOENT);
            if (!open_func(c->fc, pb, filename, AVIO_FLAG_READ, int_cb, NULL))
                return 0;
        }
    } else if (c->use_absolute_path) {
        av_log(c->fc, AV_LOG_WARNING, "Using absolute path on user request, "
               "this is a possible security issue\n");
        if (!open_func(c->fc, pb, ref->path, AVIO_FLAG_READ, int_cb, NULL))
            return 0;
    } else if (c->fc->open_cb) {
        if (!open_func(c->fc, pb, ref->path, AVIO_FLAG_READ, int_cb, NULL))
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

static int mov_read_trak(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    AVStream *st;
    MOVStreamContext *sc;
    int ret;

    st = avformat_new_stream(c->fc, NULL);
    if (!st) return AVERROR(ENOMEM);
    st->id = c->fc->nb_streams;
    sc = av_mallocz(sizeof(MOVStreamContext));
    if (!sc) return AVERROR(ENOMEM);

    st->priv_data = sc;
    st->codec->codec_type = AVMEDIA_TYPE_DATA;
    sc->ffindex = st->index;

    if ((ret = mov_read_default(c, pb, atom)) < 0)
        return ret;

    /* sanity checks */
    if (sc->chunk_count && (!sc->stts_count || !sc->stsc_count ||
                            (!sc->sample_size && !sc->sample_count))) {
        av_log(c->fc, AV_LOG_ERROR, "stream %d, missing mandatory atoms, broken header\n",
               st->index);
        return 0;
    }

    fix_timescale(c, sc);

    avpriv_set_pts_info(st, 64, 1, sc->time_scale);

    mov_build_index(c, st);

    if (sc->dref_id-1 < sc->drefs_count && sc->drefs[sc->dref_id-1].path) {
        MOVDref *dref = &sc->drefs[sc->dref_id - 1];
        if (mov_open_dref(c, &sc->pb, c->fc->filename, dref,
                          &c->fc->interrupt_callback) < 0)
            av_log(c->fc, AV_LOG_ERROR,
                   "stream %d, error opening alias: path='%s', dir='%s', "
                   "filename='%s', volume='%s', nlvl_from=%d, nlvl_to=%d\n",
                   st->index, dref->path, dref->dir, dref->filename,
                   dref->volume, dref->nlvl_from, dref->nlvl_to);
    } else {
        sc->pb = c->fc->pb;
        sc->pb_is_copied = 1;
    }

    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (!st->sample_aspect_ratio.num && st->codec->width && st->codec->height &&
            sc->height && sc->width &&
            (st->codec->width != sc->width || st->codec->height != sc->height)) {
            st->sample_aspect_ratio = av_d2q(((double)st->codec->height * sc->width) /
                                             ((double)st->codec->width * sc->height), INT_MAX);
        }

#if FF_API_R_FRAME_RATE
        if (sc->stts_count == 1 || (sc->stts_count == 2 && sc->stts_data[1].count == 1))
            av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den,
                      sc->time_scale, sc->stts_data[0].duration, INT_MAX);
#endif
    }

    // done for ai5q, ai52, ai55, ai1q, ai12 and ai15.
    if (!st->codec->extradata_size && st->codec->codec_id == AV_CODEC_ID_H264 &&
        TAG_IS_AVCI(st->codec->codec_tag)) {
        ret = ff_generate_avci_extradata(st);
        if (ret < 0)
            return ret;
    }

    switch (st->codec->codec_id) {
#if CONFIG_H261_DECODER
    case AV_CODEC_ID_H261:
#endif
#if CONFIG_H263_DECODER
    case AV_CODEC_ID_H263:
#endif
#if CONFIG_MPEG4_DECODER
    case AV_CODEC_ID_MPEG4:
#endif
        st->codec->width = 0; /* let decoder init width/height */
        st->codec->height= 0;
        break;
    }

    /* Do not need those anymore. */
    av_freep(&sc->chunk_offsets);
    av_freep(&sc->stsc_data);
    av_freep(&sc->sample_sizes);
    av_freep(&sc->keyframes);
    av_freep(&sc->stts_data);
    av_freep(&sc->stps_data);
    av_freep(&sc->elst_data);
    av_freep(&sc->rap_group);

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

static int mov_read_custom_2plus(MOVContext *c, AVIOContext *pb, int size)
{
    int64_t end = avio_tell(pb) + size;
    uint8_t *key = NULL, *val = NULL;
    int i;
    AVStream *st;
    MOVStreamContext *sc;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

    for (i = 0; i < 2; i++) {
        uint8_t **p;
        uint32_t len, tag;
        int ret;

        if (end - avio_tell(pb) <= 12)
            break;

        len = avio_rb32(pb);
        tag = avio_rl32(pb);
        avio_skip(pb, 4); // flags

        if (len < 12 || len - 12 > end - avio_tell(pb))
            break;
        len -= 12;

        if (tag == MKTAG('n', 'a', 'm', 'e'))
            p = &key;
        else if (tag == MKTAG('d', 'a', 't', 'a') && len > 4) {
            avio_skip(pb, 4);
            len -= 4;
            p = &val;
        } else
            break;

        *p = av_malloc(len + 1);
        if (!*p)
            break;
        ret = ffio_read_size(pb, *p, len);
        if (ret < 0) {
            av_freep(p);
            return ret;
        }
        (*p)[len] = 0;
    }

    if (key && val) {
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
    }

    avio_seek(pb, end, SEEK_SET);
    av_freep(&key);
    av_freep(&val);
    return 0;
}

static int mov_read_custom(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t end = avio_tell(pb) + atom.size;
    uint32_t tag, len;

    if (atom.size < 8)
        goto fail;

    len = avio_rb32(pb);
    tag = avio_rl32(pb);

    if (len > atom.size)
        goto fail;

    if (tag == MKTAG('m', 'e', 'a', 'n') && len > 12) {
        uint8_t domain[128];
        int domain_len;

        avio_skip(pb, 4); // flags
        len -= 12;

        domain_len = avio_get_str(pb, len, domain, sizeof(domain));
        avio_skip(pb, len - domain_len);
        return mov_read_custom_2plus(c, pb, end - avio_tell(pb));
    }

fail:
    av_log(c->fc, AV_LOG_VERBOSE,
           "Unhandled or malformed custom metadata of size %"PRId64"\n", atom.size);
    return 0;
}

static int mov_read_meta(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    while (atom.size > 8) {
        uint32_t tag = avio_rl32(pb);
        atom.size -= 4;
        if (tag == MKTAG('h','d','l','r')) {
            avio_seek(pb, -8, SEEK_CUR);
            atom.size += 8;
            return mov_read_default(c, pb, atom);
        }
    }
    return 0;
}

static int mov_read_tkhd(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int i;
    int width;
    int height;
    int display_matrix[3][3];
    AVStream *st;
    MOVStreamContext *sc;
    int version;
    int flags;

    if (c->fc->nb_streams < 1)
        return 0;
    st = c->fc->streams[c->fc->nb_streams-1];
    sc = st->priv_data;

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

    // save the matrix and add rotate metadata when it is not the default
    // identity
    if (display_matrix[0][0] != (1 << 16) ||
        display_matrix[1][1] != (1 << 16) ||
        display_matrix[2][2] != (1 << 30) ||
        display_matrix[0][1] || display_matrix[0][2] ||
        display_matrix[1][0] || display_matrix[1][2] ||
        display_matrix[2][0] || display_matrix[2][1]) {
        int i, j;
        double rotate;

        av_freep(&sc->display_matrix);
        sc->display_matrix = av_malloc(sizeof(int32_t) * 9);
        if (!sc->display_matrix)
            return AVERROR(ENOMEM);

        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                sc->display_matrix[i * 3 + j] = display_matrix[i][j];

        rotate = av_display_rotation_get(sc->display_matrix);
        if (!isnan(rotate)) {
            char rotate_buf[64];
            rotate = -rotate;
            if (rotate < 0) // for backward compatibility
                rotate += 360;
            snprintf(rotate_buf, sizeof(rotate_buf), "%g", rotate);
            av_dict_set(&st->metadata, "rotate", rotate_buf, 0);
        }
    }

    // transform the display width/height according to the matrix
    // to keep the same scale, use [width height 1<<16]
    if (width && height && sc->display_matrix) {
        double disp_transform[2];

#define SQR(a) ((a)*(double)(a))
        for (i = 0; i < 2; i++)
            disp_transform[i] = sqrt(SQR(display_matrix[i][0]) + SQR(display_matrix[i][1]));

        if (disp_transform[0] > 0       && disp_transform[1] > 0 &&
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
    MOVFragmentIndex* index = NULL;
    int flags, track_id, i, found = 0;

    avio_r8(pb); /* version */
    flags = avio_rb24(pb);

    track_id = avio_rb32(pb);
    if (!track_id)
        return AVERROR_INVALIDDATA;
    frag->track_id = track_id;
    for (i = 0; i < c->trex_count; i++)
        if (c->trex_data[i].track_id == frag->track_id) {
            trex = &c->trex_data[i];
            break;
        }
    if (!trex) {
        av_log(c->fc, AV_LOG_ERROR, "could not find corresponding trex\n");
        return AVERROR_INVALIDDATA;
    }

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
    frag->time     = AV_NOPTS_VALUE;
    for (i = 0; i < c->fragment_index_count; i++) {
        int j;
        MOVFragmentIndex* candidate = c->fragment_index_data[i];
        if (candidate->track_id == frag->track_id) {
            av_log(c->fc, AV_LOG_DEBUG,
                   "found fragment index for track %u\n", frag->track_id);
            index = candidate;
            for (j = index->current_item; j < index->item_count; j++) {
                if (frag->implicit_offset == index->items[j].moof_offset) {
                    av_log(c->fc, AV_LOG_DEBUG, "found fragment index entry "
                            "for track %u and moof_offset %"PRId64"\n",
                            frag->track_id, index->items[j].moof_offset);
                    frag->time = index->items[j].time;
                    index->current_item = j + 1;
                    found = 1;
                    break;
                }
            }
            if (found)
                break;
        }
    }
    if (index && !found) {
        av_log(c->fc, AV_LOG_DEBUG, "track %u has a fragment index but "
               "it doesn't have an (in-order) entry for moof_offset "
               "%"PRId64"\n", frag->track_id, frag->implicit_offset);
    }
    av_log(c->fc, AV_LOG_TRACE, "frag flags 0x%x\n", frag->flags);
    return 0;
}

static int mov_read_chap(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    c->chapter_track = avio_rb32(pb);
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

    for (i = 0; i < c->fc->nb_streams; i++) {
        if (c->fc->streams[i]->id == frag->track_id) {
            st = c->fc->streams[i];
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_ERROR, "could not find corresponding track id %d\n", frag->track_id);
        return AVERROR_INVALIDDATA;
    }
    sc = st->priv_data;
    if (sc->pseudo_stream_id + 1 != frag->stsd_id)
        return 0;
    version = avio_r8(pb);
    avio_rb24(pb); /* flags */
    if (version) {
        sc->track_end = avio_rb64(pb);
    } else {
        sc->track_end = avio_rb32(pb);
    }
    return 0;
}

static int mov_read_trun(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    MOVFragment *frag = &c->fragment;
    AVStream *st = NULL;
    MOVStreamContext *sc;
    MOVStts *ctts_data;
    uint64_t offset;
    int64_t dts;
    int data_offset = 0;
    unsigned entries, first_sample_flags = frag->flags;
    int flags, distance, i, found_keyframe = 0, err;

    for (i = 0; i < c->fc->nb_streams; i++) {
        if (c->fc->streams[i]->id == frag->track_id) {
            st = c->fc->streams[i];
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_ERROR, "could not find corresponding track id %d\n", frag->track_id);
        return AVERROR_INVALIDDATA;
    }
    sc = st->priv_data;
    if (sc->pseudo_stream_id+1 != frag->stsd_id && sc->pseudo_stream_id != -1)
        return 0;
    avio_r8(pb); /* version */
    flags = avio_rb24(pb);
    entries = avio_rb32(pb);
    av_log(c->fc, AV_LOG_TRACE, "flags 0x%x entries %d\n", flags, entries);

    /* Always assume the presence of composition time offsets.
     * Without this assumption, for instance, we cannot deal with a track in fragmented movies that meet the following.
     *  1) in the initial movie, there are no samples.
     *  2) in the first movie fragment, there is only one sample without composition time offset.
     *  3) in the subsequent movie fragments, there are samples with composition time offset. */
    if (!sc->ctts_count && sc->sample_count)
    {
        /* Complement ctts table if moov atom doesn't have ctts atom. */
        ctts_data = av_realloc(NULL, sizeof(*sc->ctts_data));
        if (!ctts_data)
            return AVERROR(ENOMEM);
        sc->ctts_data = ctts_data;
        sc->ctts_data[sc->ctts_count].count = sc->sample_count;
        sc->ctts_data[sc->ctts_count].duration = 0;
        sc->ctts_count++;
    }
    if ((uint64_t)entries+sc->ctts_count >= UINT_MAX/sizeof(*sc->ctts_data))
        return AVERROR_INVALIDDATA;
    if ((err = av_reallocp_array(&sc->ctts_data, entries + sc->ctts_count,
                                 sizeof(*sc->ctts_data))) < 0) {
        sc->ctts_count = 0;
        return err;
    }
    if (flags & MOV_TRUN_DATA_OFFSET)        data_offset        = avio_rb32(pb);
    if (flags & MOV_TRUN_FIRST_SAMPLE_FLAGS) first_sample_flags = avio_rb32(pb);
    dts    = sc->track_end - sc->time_offset;
    offset = frag->base_data_offset + data_offset;
    distance = 0;
    av_log(c->fc, AV_LOG_TRACE, "first sample flags 0x%x\n", first_sample_flags);
    for (i = 0; i < entries && !pb->eof_reached; i++) {
        unsigned sample_size = frag->size;
        int sample_flags = i ? frag->flags : first_sample_flags;
        unsigned sample_duration = frag->duration;
        int keyframe = 0;

        if (flags & MOV_TRUN_SAMPLE_DURATION) sample_duration = avio_rb32(pb);
        if (flags & MOV_TRUN_SAMPLE_SIZE)     sample_size     = avio_rb32(pb);
        if (flags & MOV_TRUN_SAMPLE_FLAGS)    sample_flags    = avio_rb32(pb);
        sc->ctts_data[sc->ctts_count].count = 1;
        sc->ctts_data[sc->ctts_count].duration = (flags & MOV_TRUN_SAMPLE_CTS) ?
                                                  avio_rb32(pb) : 0;
        mov_update_dts_shift(sc, sc->ctts_data[sc->ctts_count].duration);
        if (frag->time != AV_NOPTS_VALUE) {
            if (c->use_mfra_for == FF_MOV_FLAG_MFRA_PTS) {
                int64_t pts = frag->time;
                av_log(c->fc, AV_LOG_DEBUG, "found frag time %"PRId64
                        " sc->dts_shift %d ctts.duration %d"
                        " sc->time_offset %"PRId64" flags & MOV_TRUN_SAMPLE_CTS %d\n", pts,
                        sc->dts_shift, sc->ctts_data[sc->ctts_count].duration,
                        sc->time_offset, flags & MOV_TRUN_SAMPLE_CTS);
                dts = pts - sc->dts_shift;
                if (flags & MOV_TRUN_SAMPLE_CTS) {
                    dts -= sc->ctts_data[sc->ctts_count].duration;
                } else {
                    dts -= sc->time_offset;
                }
                av_log(c->fc, AV_LOG_DEBUG, "calculated into dts %"PRId64"\n", dts);
            } else {
                dts = frag->time;
                av_log(c->fc, AV_LOG_DEBUG, "found frag time %"PRId64
                        ", using it for dts\n", dts);
            }
            frag->time = AV_NOPTS_VALUE;
        }
        sc->ctts_count++;
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
            keyframe = 1;
        else if (!found_keyframe)
            keyframe = found_keyframe =
                !(sample_flags & (MOV_FRAG_SAMPLE_FLAG_IS_NON_SYNC |
                                  MOV_FRAG_SAMPLE_FLAG_DEPENDS_YES));
        if (keyframe)
            distance = 0;
        err = av_add_index_entry(st, offset, dts, sample_size, distance,
                                 keyframe ? AVINDEX_KEYFRAME : 0);
        if (err < 0) {
            av_log(c->fc, AV_LOG_ERROR, "Failed to add index entry\n");
        }
        av_log(c->fc, AV_LOG_TRACE, "AVIndex stream %d, sample %d, offset %"PRIx64", dts %"PRId64", "
                "size %d, distance %d, keyframe %d\n", st->index, sc->sample_count+i,
                offset, dts, sample_size, distance, keyframe);
        distance++;
        dts += sample_duration;
        offset += sample_size;
        sc->data_size += sample_size;
        sc->duration_for_fps += sample_duration;
        sc->nb_frames_for_fps ++;
    }

    if (pb->eof_reached)
        return AVERROR_EOF;

    frag->implicit_offset = offset;

    sc->track_end = dts + sc->time_offset;
    if (st->duration < sc->track_end)
        st->duration = sc->track_end;

    return 0;
}

static int mov_read_sidx(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int64_t offset = avio_tell(pb) + atom.size, pts;
    uint8_t version;
    unsigned i, track_id;
    AVStream *st = NULL;
    MOVStreamContext *sc;
    MOVFragmentIndex *index = NULL;
    MOVFragmentIndex **tmp;
    AVRational timescale;

    version = avio_r8(pb);
    if (version > 1) {
        avpriv_request_sample(c->fc, "sidx version %u", version);
        return AVERROR_PATCHWELCOME;
    }

    avio_rb24(pb); // flags

    track_id = avio_rb32(pb); // Reference ID
    for (i = 0; i < c->fc->nb_streams; i++) {
        if (c->fc->streams[i]->id == track_id) {
            st = c->fc->streams[i];
            break;
        }
    }
    if (!st) {
        av_log(c->fc, AV_LOG_ERROR, "could not find corresponding track id %d\n", track_id);
        return AVERROR_INVALIDDATA;
    }

    sc = st->priv_data;

    timescale = av_make_q(1, avio_rb32(pb));

    if (version == 0) {
        pts = avio_rb32(pb);
        offset += avio_rb32(pb);
    } else {
        pts = avio_rb64(pb);
        offset += avio_rb64(pb);
    }

    avio_rb16(pb); // reserved

    index = av_mallocz(sizeof(MOVFragmentIndex));
    if (!index)
        return AVERROR(ENOMEM);

    index->track_id = track_id;

    index->item_count = avio_rb16(pb);
    index->items = av_mallocz_array(index->item_count, sizeof(MOVFragmentIndexItem));

    if (!index->items) {
        av_freep(&index);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < index->item_count; i++) {
        uint32_t size = avio_rb32(pb);
        uint32_t duration = avio_rb32(pb);
        if (size & 0x80000000) {
            avpriv_request_sample(c->fc, "sidx reference_type 1");
            av_freep(&index->items);
            av_freep(&index);
            return AVERROR_PATCHWELCOME;
        }
        avio_rb32(pb); // sap_flags
        index->items[i].moof_offset = offset;
        index->items[i].time = av_rescale_q(pts, st->time_base, timescale);
        offset += size;
        pts += duration;
    }

    st->duration = sc->track_end = pts;

    tmp = av_realloc_array(c->fragment_index_data,
                           c->fragment_index_count + 1,
                           sizeof(MOVFragmentIndex*));
    if (!tmp) {
        av_freep(&index->items);
        av_freep(&index);
        return AVERROR(ENOMEM);
    }

    c->fragment_index_data = tmp;
    c->fragment_index_data[c->fragment_index_count++] = index;

    if (offset == avio_size(pb))
        c->fragment_index_complete = 1;

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
    AVIOContext ctx;
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

    if (uncompress (moov_data, (uLongf *) &moov_len, (const Bytef *)cmov_data, cmov_len) != Z_OK)
        goto free_and_return;
    if (ffio_init_context(&ctx, moov_data, moov_len, 0, NULL, NULL, NULL, NULL) != 0)
        goto free_and_return;
    ctx.seekable = AVIO_SEEKABLE_NORMAL;
    atom.type = MKTAG('m','o','o','v');
    atom.size = moov_len;
    ret = mov_read_default(c, &ctx, atom);
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

    if (c->fc->nb_streams < 1 || c->ignore_editlist)
        return 0;
    sc = c->fc->streams[c->fc->nb_streams-1]->priv_data;

    version = avio_r8(pb); /* version */
    avio_rb24(pb); /* flags */
    edit_count = avio_rb32(pb); /* entries */

    if (!edit_count)
        return 0;
    if (sc->elst_data)
        av_log(c->fc, AV_LOG_WARNING, "Duplicated ELST atom\n");
    av_free(sc->elst_data);
    sc->elst_count = 0;
    sc->elst_data = av_malloc_array(edit_count, sizeof(*sc->elst_data));
    if (!sc->elst_data)
        return AVERROR(ENOMEM);

    av_log(c->fc, AV_LOG_TRACE, "track[%i].edit_count = %i\n", c->fc->nb_streams-1, edit_count);
    for (i = 0; i < edit_count && !pb->eof_reached; i++) {
        MOVElst *e = &sc->elst_data[i];

        if (version == 1) {
            e->duration = avio_rb64(pb);
            e->time     = avio_rb64(pb);
        } else {
            e->duration = avio_rb32(pb); /* segment duration */
            e->time     = (int32_t)avio_rb32(pb); /* media time */
        }
        e->rate = avio_rb32(pb) / 65536.0;
        av_log(c->fc, AV_LOG_TRACE, "duration=%"PRId64" time=%"PRId64" rate=%f\n",
                e->duration, e->time, e->rate);
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

static int mov_read_uuid(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret;
    uint8_t uuid[16];
    static const uint8_t uuid_isml_manifest[] = {
        0xa5, 0xd4, 0x0b, 0x30, 0xe8, 0x14, 0x11, 0xdd,
        0xba, 0x2f, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66
    };

    if (atom.size < sizeof(uuid) || atom.size == INT64_MAX)
        return AVERROR_INVALIDDATA;

    ret = avio_read(pb, uuid, sizeof(uuid));
    if (ret < 0) {
        return ret;
    } else if (ret != sizeof(uuid)) {
        return AVERROR_INVALIDDATA;
    }
    if (!memcmp(uuid, uuid_isml_manifest, sizeof(uuid))) {
        uint8_t *buffer, *ptr;
        char *endptr;
        size_t len = atom.size - sizeof(uuid);

        if (len < 4) {
            return AVERROR_INVALIDDATA;
        }
        ret = avio_skip(pb, 4); // zeroes
        len -= 4;

        buffer = av_mallocz(len + 1);
        if (!buffer) {
            return AVERROR(ENOMEM);
        }
        ret = avio_read(pb, buffer, len);
        if (ret < 0) {
            av_free(buffer);
            return ret;
        } else if (ret != len) {
            av_free(buffer);
            return AVERROR_INVALIDDATA;
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
    }
    return 0;
}

static int mov_read_free(MOVContext *c, AVIOContext *pb, MOVAtom atom)
{
    int ret;
    uint8_t content[16];

    if (atom.size < 8)
        return 0;

    ret = avio_read(pb, content, FFMIN(sizeof(content), atom.size));
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

static const MOVParseTableEntry mov_default_parse_table[] = {
{ MKTAG('A','C','L','R'), mov_read_aclr },
{ MKTAG('A','P','R','G'), mov_read_avid },
{ MKTAG('A','A','L','P'), mov_read_avid },
{ MKTAG('A','R','E','S'), mov_read_ares },
{ MKTAG('a','v','s','s'), mov_read_avss },
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
{ MKTAG('w','i','d','e'), mov_read_wide }, /* place holder */
{ MKTAG('w','f','e','x'), mov_read_wfex },
{ MKTAG('c','m','o','v'), mov_read_cmov },
{ MKTAG('c','h','a','n'), mov_read_chan }, /* channel layout */
{ MKTAG('d','v','c','1'), mov_read_dvc1 },
{ MKTAG('s','b','g','p'), mov_read_sbgp },
{ MKTAG('h','v','c','C'), mov_read_glbl },
{ MKTAG('u','u','i','d'), mov_read_uuid },
{ MKTAG('C','i','n', 0x8e), mov_read_targa_y216 },
{ MKTAG('f','r','e','e'), mov_read_free },
{ MKTAG('-','-','-','-'), mov_read_custom },
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
    while (total_size + 8 <= atom.size && !avio_feof(pb)) {
        int (*parse)(MOVContext*, AVIOContext*, MOVAtom) = NULL;
        a.size = atom.size;
        a.type=0;
        if (atom.size >= 8) {
            a.size = avio_rb32(pb);
            a.type = avio_rl32(pb);
            if (a.type == MKTAG('f','r','e','e') &&
                a.size >= 8 &&
                c->moov_retry) {
                uint8_t buf[8];
                uint32_t *type = (uint32_t *)buf + 1;
                if (avio_read(pb, buf, 8) != 8)
                    return AVERROR_INVALIDDATA;
                avio_seek(pb, -8, SEEK_CUR);
                if (*type == MKTAG('m','v','h','d') ||
                    *type == MKTAG('c','m','o','v')) {
                    av_log(c->fc, AV_LOG_ERROR, "Detected moov in a free atom.\n");
                    a.type = MKTAG('m','o','o','v');
                }
            }
            if (atom.type != MKTAG('r','o','o','t') &&
                atom.type != MKTAG('m','o','o','v'))
            {
                if (a.type == MKTAG('t','r','a','k') || a.type == MKTAG('m','d','a','t'))
                {
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
        }
        av_log(c->fc, AV_LOG_TRACE, "type: %08x '%.4s' parent:'%.4s' sz: %"PRId64" %"PRId64" %"PRId64"\n",
                a.type, (char*)&a.type, (char*)&atom.type, a.size, total_size, atom.size);
        if (a.size == 0) {
            a.size = atom.size - total_size + 8;
        }
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
            if (c->found_moov && c->found_mdat &&
                ((!pb->seekable || c->fc->flags & AVFMT_FLAG_IGNIDX || c->fragment_index_complete) ||
                 start_pos + a.size == avio_size(pb))) {
                if (!pb->seekable || c->fc->flags & AVFMT_FLAG_IGNIDX || c->fragment_index_complete)
                    c->next_root_atom = start_pos + a.size;
                c->atom_depth --;
                return 0;
            }
            left = a.size - avio_tell(pb) + start_pos;
            if (left > 0) /* skip garbage at atom end */
                avio_skip(pb, left);
            else if (left < 0) {
                av_log(c->fc, AV_LOG_WARNING,
                       "overread end of atom '%.4s' by %"PRId64" bytes\n",
                       (char*)&a.type, -left);
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

static int mov_probe(AVProbeData *p)
{
    int64_t offset;
    uint32_t tag;
    int score = 0;
    int moov_offset = -1;

    /* check file header */
    offset = 0;
    for (;;) {
        /* ignore invalid offset */
        if ((offset + 8) > (unsigned int)p->buf_size)
            break;
        tag = AV_RL32(p->buf + offset + 4);
        switch(tag) {
        /* check for obvious tags */
        case MKTAG('m','o','o','v'):
            moov_offset = offset + 4;
        case MKTAG('m','d','a','t'):
        case MKTAG('p','n','o','t'): /* detect movs with preview pics like ew.mov and april.mov */
        case MKTAG('u','d','t','a'): /* Packet Video PVAuthor adds this and a lot of more junk */
        case MKTAG('f','t','y','p'):
            if (AV_RB32(p->buf+offset) < 8 &&
                (AV_RB32(p->buf+offset) != 1 ||
                 offset + 12 > (unsigned int)p->buf_size ||
                 AV_RB64(p->buf+offset + 8) == 0)) {
                score = FFMAX(score, AVPROBE_SCORE_EXTENSION);
            } else if (tag == MKTAG('f','t','y','p') &&
                       (   AV_RL32(p->buf + offset + 8) == MKTAG('j','p','2',' ')
                        || AV_RL32(p->buf + offset + 8) == MKTAG('j','p','x',' ')
                    )) {
                score = FFMAX(score, 5);
            } else {
                score = AVPROBE_SCORE_MAX;
            }
            offset = FFMAX(4, AV_RB32(p->buf+offset)) + offset;
            break;
        /* those are more common words, so rate then a bit less */
        case MKTAG('e','d','i','w'): /* xdcam files have reverted first tags */
        case MKTAG('w','i','d','e'):
        case MKTAG('f','r','e','e'):
        case MKTAG('j','u','n','k'):
        case MKTAG('p','i','c','t'):
            score  = FFMAX(score, AVPROBE_SCORE_MAX - 5);
            offset = FFMAX(4, AV_RB32(p->buf+offset)) + offset;
            break;
        case MKTAG(0x82,0x82,0x7f,0x7d):
        case MKTAG('s','k','i','p'):
        case MKTAG('u','u','i','d'):
        case MKTAG('p','r','f','l'):
            /* if we only find those cause probedata is too small at least rate them */
            score  = FFMAX(score, AVPROBE_SCORE_EXTENSION);
            offset = FFMAX(4, AV_RB32(p->buf+offset)) + offset;
            break;
        default:
            offset = FFMAX(4, AV_RB32(p->buf+offset)) + offset;
        }
    }
    if(score > AVPROBE_SCORE_MAX - 50 && moov_offset != -1) {
        /* moov atom in the header - we should make sure that this is not a
         * MOV-packed MPEG-PS */
        offset = moov_offset;

        while(offset < (p->buf_size - 16)){ /* Sufficient space */
               /* We found an actual hdlr atom */
            if(AV_RL32(p->buf + offset     ) == MKTAG('h','d','l','r') &&
               AV_RL32(p->buf + offset +  8) == MKTAG('m','h','l','r') &&
               AV_RL32(p->buf + offset + 12) == MKTAG('M','P','E','G')){
                av_log(NULL, AV_LOG_WARNING, "Found media data tag MPEG indicating this is a MOV-packed MPEG-PS.\n");
                /* We found a media handler reference atom describing an
                 * MPEG-PS-in-MOV, return a
                 * low score to force expanding the probe window until
                 * mpegps_probe finds what it needs */
                return 5;
            }else
                /* Keep looking */
                offset+=2;
        }
    }

    return score;
}

// must be done after parsing all trak because there's no order requirement
static void mov_read_chapters(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    AVStream *st = NULL;
    MOVStreamContext *sc;
    int64_t cur_pos;
    int i;

    for (i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == mov->chapter_track) {
            st = s->streams[i];
            break;
        }
    if (!st) {
        av_log(s, AV_LOG_ERROR, "Referenced QT chapter track not found\n");
        return;
    }

    st->discard = AVDISCARD_ALL;
    sc = st->priv_data;
    cur_pos = avio_tell(sc->pb);

    for (i = 0; i < st->nb_index_entries; i++) {
        AVIndexEntry *sample = &st->index_entries[i];
        int64_t end = i+1 < st->nb_index_entries ? st->index_entries[i+1].timestamp : st->duration;
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
finish:
    avio_seek(sc->pb, cur_pos, SEEK_SET);
}

static int parse_timecode_in_framenum_format(AVFormatContext *s, AVStream *st,
                                             uint32_t value, int flags)
{
    AVTimecode tc;
    char buf[AV_TIMECODE_STR_SIZE];
    AVRational rate = {st->codec->time_base.den,
                       st->codec->time_base.num};
    int ret = av_timecode_init(&tc, rate, flags, 0, s);
    if (ret < 0)
        return ret;
    av_dict_set(&st->metadata, "timecode",
                av_timecode_make_string(&tc, buf, value), 0);
    return 0;
}

static int mov_read_timecode_track(AVFormatContext *s, AVStream *st)
{
    MOVStreamContext *sc = st->priv_data;
    int flags = 0;
    int64_t cur_pos = avio_tell(sc->pb);
    uint32_t value;

    if (!st->nb_index_entries)
        return -1;

    avio_seek(sc->pb, st->index_entries->pos, SEEK_SET);
    value = avio_rb32(s->pb);

    if (sc->tmcd_flags & 0x0001) flags |= AV_TIMECODE_FLAG_DROPFRAME;
    if (sc->tmcd_flags & 0x0002) flags |= AV_TIMECODE_FLAG_24HOURSMAX;
    if (sc->tmcd_flags & 0x0004) flags |= AV_TIMECODE_FLAG_ALLOWNEGATIVE;

    /* Assume Counter flag is set to 1 in tmcd track (even though it is likely
     * not the case) and thus assume "frame number format" instead of QT one.
     * No sample with tmcd track can be found with a QT timecode at the moment,
     * despite what the tmcd track "suggests" (Counter flag set to 0 means QT
     * format). */
    parse_timecode_in_framenum_format(s, st, value, flags);

    avio_seek(sc->pb, cur_pos, SEEK_SET);
    return 0;
}

static int mov_read_close(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    int i, j;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;

        if (!sc)
            continue;

        av_freep(&sc->ctts_data);
        for (j = 0; j < sc->drefs_count; j++) {
            av_freep(&sc->drefs[j].path);
            av_freep(&sc->drefs[j].dir);
        }
        av_freep(&sc->drefs);

        sc->drefs_count = 0;

        if (!sc->pb_is_copied)
            avio_closep(&sc->pb);

        sc->pb = NULL;
        av_freep(&sc->chunk_offsets);
        av_freep(&sc->stsc_data);
        av_freep(&sc->sample_sizes);
        av_freep(&sc->keyframes);
        av_freep(&sc->stts_data);
        av_freep(&sc->stps_data);
        av_freep(&sc->elst_data);
        av_freep(&sc->rap_group);
        av_freep(&sc->display_matrix);
    }

    if (mov->dv_demux) {
        avformat_free_context(mov->dv_fctx);
        mov->dv_fctx = NULL;
    }

    av_freep(&mov->trex_data);
    av_freep(&mov->bitrates);

    for (i = 0; i < mov->fragment_index_count; i++) {
        MOVFragmentIndex* index = mov->fragment_index_data[i];
        av_freep(&index->items);
        av_freep(&mov->fragment_index_data[i]);
    }
    av_freep(&mov->fragment_index_data);

    av_freep(&mov->aes_decrypt);

    return 0;
}

static int tmcd_is_referenced(AVFormatContext *s, int tmcd_id)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;

        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
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

        if (st->codec->codec_tag  == MKTAG('t','m','c','d') &&
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
    MOVFragmentIndex* index = NULL;
    int version, fieldlength, i, j;
    int64_t pos = avio_tell(f);
    uint32_t size = avio_rb32(f);
    void *tmp;

    if (avio_rb32(f) != MKBETAG('t', 'f', 'r', 'a')) {
        return 1;
    }
    av_log(mov->fc, AV_LOG_VERBOSE, "found tfra\n");
    index = av_mallocz(sizeof(MOVFragmentIndex));
    if (!index) {
        return AVERROR(ENOMEM);
    }

    tmp = av_realloc_array(mov->fragment_index_data,
                           mov->fragment_index_count + 1,
                           sizeof(MOVFragmentIndex*));
    if (!tmp) {
        av_freep(&index);
        return AVERROR(ENOMEM);
    }
    mov->fragment_index_data = tmp;
    mov->fragment_index_data[mov->fragment_index_count++] = index;

    version = avio_r8(f);
    avio_rb24(f);
    index->track_id = avio_rb32(f);
    fieldlength = avio_rb32(f);
    index->item_count = avio_rb32(f);
    index->items = av_mallocz_array(
            index->item_count, sizeof(MOVFragmentIndexItem));
    if (!index->items) {
        index->item_count = 0;
        return AVERROR(ENOMEM);
    }
    for (i = 0; i < index->item_count; i++) {
        int64_t time, offset;
        if (version == 1) {
            time   = avio_rb64(f);
            offset = avio_rb64(f);
        } else {
            time   = avio_rb32(f);
            offset = avio_rb32(f);
        }
        index->items[i].time = time;
        index->items[i].moof_offset = offset;
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
    int32_t mfra_size;
    int ret = -1;
    if ((seek_ret = avio_seek(f, stream_size - 4, SEEK_SET)) < 0) {
        ret = seek_ret;
        goto fail;
    }
    mfra_size = avio_rb32(f);
    if (mfra_size < 0 || mfra_size > stream_size) {
        av_log(c->fc, AV_LOG_DEBUG, "doesn't look like mfra (unreasonable size)\n");
        goto fail;
    }
    if ((seek_ret = avio_seek(f, -mfra_size, SEEK_CUR)) < 0) {
        ret = seek_ret;
        goto fail;
    }
    if (avio_rb32(f) != mfra_size) {
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
fail:
    seek_ret = avio_seek(f, original_pos, SEEK_SET);
    if (seek_ret < 0) {
        av_log(c->fc, AV_LOG_ERROR,
               "failed to seek back after looking for mfra\n");
        ret = seek_ret;
    }
    return ret;
}

static int mov_read_header(AVFormatContext *s)
{
    MOVContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    int j, err;
    MOVAtom atom = { AV_RL32("root") };
    int i;

    mov->fc = s;
    /* .mov and .mp4 aren't streamable anyway (only progressive download if moov is before mdat) */
    if (pb->seekable)
        atom.size = avio_size(pb);
    else
        atom.size = INT64_MAX;

    /* check MOV header */
    do {
    if (mov->moov_retry)
        avio_seek(pb, 0, SEEK_SET);
    if ((err = mov_read_default(mov, pb, atom)) < 0) {
        av_log(s, AV_LOG_ERROR, "error reading header\n");
        mov_read_close(s);
        return err;
    }
    } while (pb->seekable && !mov->found_moov && !mov->moov_retry++);
    if (!mov->found_moov) {
        av_log(s, AV_LOG_ERROR, "moov atom not found\n");
        mov_read_close(s);
        return AVERROR_INVALIDDATA;
    }
    av_log(mov->fc, AV_LOG_TRACE, "on_parse_exit_offset=%"PRId64"\n", avio_tell(pb));

    if (pb->seekable) {
        if (mov->chapter_track > 0)
            mov_read_chapters(s);
        for (i = 0; i < s->nb_streams; i++)
            if (s->streams[i]->codec->codec_tag == AV_RL32("tmcd"))
                mov_read_timecode_track(s, s->streams[i]);
    }

    /* copy timecode metadata from tmcd tracks to the related video streams */
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;
        if (sc->timecode_track > 0) {
            AVDictionaryEntry *tcr;
            int tmcd_st_id = -1;

            for (j = 0; j < s->nb_streams; j++)
                if (s->streams[j]->id == sc->timecode_track)
                    tmcd_st_id = j;

            if (tmcd_st_id < 0 || tmcd_st_id == i)
                continue;
            tcr = av_dict_get(s->streams[tmcd_st_id]->metadata, "timecode", NULL, 0);
            if (tcr)
                av_dict_set(&st->metadata, "timecode", tcr->value, 0);
        }
    }
    export_orphan_timecode(s);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;
        fix_timescale(mov, sc);
        if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO && st->codec->codec_id == AV_CODEC_ID_AAC) {
            st->skip_samples = sc->start_pad;
        }
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && sc->nb_frames_for_fps > 0 && sc->duration_for_fps > 0)
            av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
                      sc->time_scale*(int64_t)sc->nb_frames_for_fps, sc->duration_for_fps, INT_MAX);
        if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (st->codec->width <= 0 || st->codec->height <= 0) {
                st->codec->width  = sc->width;
                st->codec->height = sc->height;
            }
            if (st->codec->codec_id == AV_CODEC_ID_DVD_SUBTITLE) {
                if ((err = mov_rewrite_dvd_sub_extradata(st)) < 0)
                    return err;
            }
        }
    }

    if (mov->trex_data) {
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            MOVStreamContext *sc = st->priv_data;
            if (st->duration > 0)
                st->codec->bit_rate = sc->data_size * 8 * sc->time_scale / st->duration;
        }
    }

    if (mov->use_mfra_for > 0) {
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            MOVStreamContext *sc = st->priv_data;
            if (sc->duration_for_fps > 0) {
                st->codec->bit_rate = sc->data_size * 8 * sc->time_scale /
                    sc->duration_for_fps;
            }
        }
    }

    for (i = 0; i < mov->bitrates_count && i < s->nb_streams; i++) {
        if (mov->bitrates[i]) {
            s->streams[i]->codec->bit_rate = mov->bitrates[i];
        }
    }

    ff_rfps_calculate(s);

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MOVStreamContext *sc = st->priv_data;

        switch (st->codec->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            err = ff_replaygain_export(st, s->metadata);
            if (err < 0) {
                mov_read_close(s);
                return err;
            }
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (sc->display_matrix) {
                AVPacketSideData *sd, *tmp;

                tmp = av_realloc_array(st->side_data,
                                       st->nb_side_data + 1, sizeof(*tmp));
                if (!tmp)
                    return AVERROR(ENOMEM);

                st->side_data = tmp;
                st->nb_side_data++;

                sd = &st->side_data[st->nb_side_data - 1];
                sd->type = AV_PKT_DATA_DISPLAYMATRIX;
                sd->size = sizeof(int32_t) * 9;
                sd->data = (uint8_t*)sc->display_matrix;
                sc->display_matrix = NULL;
            }
            break;
        }
    }
    ff_configure_buffers_for_index(s, AV_TIME_BASE);

    return 0;
}

static AVIndexEntry *mov_find_next_sample(AVFormatContext *s, AVStream **st)
{
    AVIndexEntry *sample = NULL;
    int64_t best_dts = INT64_MAX;
    int i;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *avst = s->streams[i];
        MOVStreamContext *msc = avst->priv_data;
        if (msc->pb && msc->current_sample < avst->nb_index_entries) {
            AVIndexEntry *current_sample = &avst->index_entries[msc->current_sample];
            int64_t dts = av_rescale(current_sample->timestamp, AV_TIME_BASE, msc->time_scale);
            av_log(s, AV_LOG_TRACE, "stream %d, sample %d, dts %"PRId64"\n", i, msc->current_sample, dts);
            if (!sample || (!s->pb->seekable && current_sample->pos < sample->pos) ||
                (s->pb->seekable &&
                 ((msc->pb != s->pb && dts < best_dts) || (msc->pb == s->pb &&
                 ((FFABS(best_dts - dts) <= AV_TIME_BASE && current_sample->pos < sample->pos) ||
                  (FFABS(best_dts - dts) > AV_TIME_BASE && dts < best_dts)))))) {
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

static int mov_switch_root(AVFormatContext *s, int64_t target)
{
    MOVContext *mov = s->priv_data;
    int i, j;
    int already_read = 0;

    if (avio_seek(s->pb, target, SEEK_SET) != target) {
        av_log(mov->fc, AV_LOG_ERROR, "root atom offset 0x%"PRIx64": partial file\n", target);
        return AVERROR_INVALIDDATA;
    }

    mov->next_root_atom = 0;

    for (i = 0; i < mov->fragment_index_count; i++) {
        MOVFragmentIndex *index = mov->fragment_index_data[i];
        int found = 0;
        for (j = 0; j < index->item_count; j++) {
            MOVFragmentIndexItem *item = &index->items[j];
            if (found) {
                mov->next_root_atom = item->moof_offset;
                break; // Advance to next index in outer loop
            } else if (item->moof_offset == target) {
                index->current_item = FFMIN(j, index->current_item);
                if (item->headers_read)
                    already_read = 1;
                item->headers_read = 1;
                found = 1;
            }
        }
        if (!found)
            index->current_item = 0;
    }

    if (already_read)
        return 0;

    mov->found_mdat = 0;

    if (mov_read_default(mov, s->pb, (MOVAtom){ AV_RL32("root"), INT64_MAX }) < 0 ||
        avio_feof(s->pb))
        return AVERROR_EOF;
    av_log(s, AV_LOG_TRACE, "read fragments, offset 0x%"PRIx64"\n", avio_tell(s->pb));

    return 1;
}

static int mov_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVContext *mov = s->priv_data;
    MOVStreamContext *sc;
    AVIndexEntry *sample;
    AVStream *st = NULL;
    int ret;
    mov->fc = s;
 retry:
    sample = mov_find_next_sample(s, &st);
    if (!sample || (mov->next_root_atom && sample->pos > mov->next_root_atom)) {
        if (!mov->next_root_atom)
            return AVERROR_EOF;
        if ((ret = mov_switch_root(s, mov->next_root_atom)) < 0)
            return ret;
        goto retry;
    }
    sc = st->priv_data;
    /* must be done just before reading, to avoid infinite loop on sample */
    sc->current_sample++;

    if (mov->next_root_atom) {
        sample->pos = FFMIN(sample->pos, mov->next_root_atom);
        sample->size = FFMIN(sample->size, (mov->next_root_atom - sample->pos));
    }

    if (st->discard != AVDISCARD_ALL) {
        int64_t ret64 = avio_seek(sc->pb, sample->pos, SEEK_SET);
        if (ret64 != sample->pos) {
            av_log(mov->fc, AV_LOG_ERROR, "stream %d, offset 0x%"PRIx64": partial file\n",
                   sc->ffindex, sample->pos);
            sc->current_sample -= should_retry(sc->pb, ret64);
            return AVERROR_INVALIDDATA;
        }
        ret = av_get_packet(sc->pb, pkt, sample->size);
        if (ret < 0) {
            sc->current_sample -= should_retry(sc->pb, ret);
            return ret;
        }
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
#if CONFIG_DV_DEMUXER
        if (mov->dv_demux && sc->dv_audio_container) {
            avpriv_dv_produce_packet(mov->dv_demux, pkt, pkt->data, pkt->size, pkt->pos);
            av_freep(&pkt->data);
            pkt->size = 0;
            ret = avpriv_dv_get_packet(mov->dv_demux, pkt);
            if (ret < 0)
                return ret;
        }
#endif
    }

    pkt->stream_index = sc->ffindex;
    pkt->dts = sample->timestamp;
    if (sc->ctts_data && sc->ctts_index < sc->ctts_count) {
        pkt->pts = pkt->dts + sc->dts_shift + sc->ctts_data[sc->ctts_index].duration;
        /* update ctts context */
        sc->ctts_sample++;
        if (sc->ctts_index < sc->ctts_count &&
            sc->ctts_data[sc->ctts_index].count == sc->ctts_sample) {
            sc->ctts_index++;
            sc->ctts_sample = 0;
        }
        if (sc->wrong_dts)
            pkt->dts = AV_NOPTS_VALUE;
    } else {
        int64_t next_dts = (sc->current_sample < st->nb_index_entries) ?
            st->index_entries[sc->current_sample].timestamp : st->duration;
        pkt->duration = next_dts - pkt->dts;
        pkt->pts = pkt->dts;
    }
    if (st->discard == AVDISCARD_ALL)
        goto retry;
    pkt->flags |= sample->flags & AVINDEX_KEYFRAME ? AV_PKT_FLAG_KEY : 0;
    pkt->pos = sample->pos;

    if (mov->aax_mode)
        aax_filter(pkt->data, pkt->size, mov);

    return 0;
}

static int mov_seek_fragment(AVFormatContext *s, AVStream *st, int64_t timestamp)
{
    MOVContext *mov = s->priv_data;
    int i, j;

    if (!mov->fragment_index_complete)
        return 0;

    for (i = 0; i < mov->fragment_index_count; i++) {
        if (mov->fragment_index_data[i]->track_id == st->id) {
            MOVFragmentIndex *index = index = mov->fragment_index_data[i];
            for (j = index->item_count - 1; j >= 0; j--) {
                if (index->items[j].time <= timestamp) {
                    if (index->items[j].headers_read)
                        return 0;

                    return mov_switch_root(s, index->items[j].moof_offset);
                }
            }
        }
    }

    return 0;
}

static int mov_seek_stream(AVFormatContext *s, AVStream *st, int64_t timestamp, int flags)
{
    MOVStreamContext *sc = st->priv_data;
    int sample, time_sample;
    int i;

    int ret = mov_seek_fragment(s, st, timestamp);
    if (ret < 0)
        return ret;

    sample = av_index_search_timestamp(st, timestamp, flags);
    av_log(s, AV_LOG_TRACE, "stream %d, timestamp %"PRId64", sample %d\n", st->index, timestamp, sample);
    if (sample < 0 && st->nb_index_entries && timestamp < st->index_entries[0].timestamp)
        sample = 0;
    if (sample < 0) /* not sure what to do */
        return AVERROR_INVALIDDATA;
    sc->current_sample = sample;
    av_log(s, AV_LOG_TRACE, "stream %d, found sample %d\n", st->index, sc->current_sample);
    /* adjust ctts index */
    if (sc->ctts_data) {
        time_sample = 0;
        for (i = 0; i < sc->ctts_count; i++) {
            int next = time_sample + sc->ctts_data[i].count;
            if (next > sc->current_sample) {
                sc->ctts_index = i;
                sc->ctts_sample = sc->current_sample - time_sample;
                break;
            }
            time_sample = next;
        }
    }
    return sample;
}

static int mov_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    MOVContext *mc = s->priv_data;
    AVStream *st;
    int sample;
    int i;

    if (stream_index >= s->nb_streams)
        return AVERROR_INVALIDDATA;

    st = s->streams[stream_index];
    sample = mov_seek_stream(s, st, sample_time, flags);
    if (sample < 0)
        return sample;

    if (mc->seek_individually) {
        /* adjust seek timestamp to found sample timestamp */
        int64_t seek_timestamp = st->index_entries[sample].timestamp;

        for (i = 0; i < s->nb_streams; i++) {
            int64_t timestamp;
            MOVStreamContext *sc = s->streams[i]->priv_data;
            st = s->streams[i];
            st->skip_samples = (sample_time <= 0) ? sc->start_pad : 0;

            if (stream_index == i)
                continue;

            timestamp = av_rescale_q(seek_timestamp, s->streams[stream_index]->time_base, st->time_base);
            mov_seek_stream(s, st, timestamp, flags);
        }
    } else {
        for (i = 0; i < s->nb_streams; i++) {
            MOVStreamContext *sc;
            st = s->streams[i];
            sc = st->priv_data;
            sc->current_sample = 0;
        }
        while (1) {
            MOVStreamContext *sc;
            AVIndexEntry *entry = mov_find_next_sample(s, &st);
            if (!entry)
                return AVERROR_INVALIDDATA;
            sc = st->priv_data;
            if (sc->ffindex == stream_index && sc->current_sample == sample)
                break;
            sc->current_sample++;
        }
    }
    return 0;
}

#define OFFSET(x) offsetof(MOVContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption mov_options[] = {
    {"use_absolute_path",
        "allow using absolute path when opening alias, this is a possible security issue",
        OFFSET(use_absolute_path), AV_OPT_TYPE_INT, {.i64 = 0},
        0, 1, FLAGS},
    {"seek_streams_individually",
        "Seek each stream individually to the to the closest point",
        OFFSET(seek_individually), AV_OPT_TYPE_INT, { .i64 = 1 },
        0, 1, FLAGS},
    {"ignore_editlist", "", OFFSET(ignore_editlist), AV_OPT_TYPE_INT, {.i64 = 0},
        0, 1, FLAGS},
    {"use_mfra_for",
        "use mfra for fragment timestamps",
        OFFSET(use_mfra_for), AV_OPT_TYPE_INT, {.i64 = FF_MOV_FLAG_MFRA_AUTO},
        -1, FF_MOV_FLAG_MFRA_PTS, FLAGS,
        "use_mfra_for"},
    {"auto", "auto", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_AUTO}, 0, 0,
        FLAGS, "use_mfra_for" },
    {"dts", "dts", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_DTS}, 0, 0,
        FLAGS, "use_mfra_for" },
    {"pts", "pts", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_MFRA_PTS}, 0, 0,
        FLAGS, "use_mfra_for" },
    { "export_all", "Export unrecognized metadata entries", OFFSET(export_all),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = FLAGS },
    { "export_xmp", "Export full XMP metadata", OFFSET(export_xmp),
        AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, .flags = FLAGS },
    { "activation_bytes", "Secret bytes for Audible AAX files", OFFSET(activation_bytes),
        AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_DECODING_PARAM },
    { "audible_fixed_key", // extracted from libAAX_SDK.so and AAXSDKWin.dll files!
        "Fixed key used for handling Audible AAX files", OFFSET(audible_fixed_key),
        AV_OPT_TYPE_BINARY, {.str="77214d4b196a87cd520045fd20a51d67"},
        .flags = AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass mov_class = {
    .class_name = "mov,mp4,m4a,3gp,3g2,mj2",
    .item_name  = av_default_item_name,
    .option     = mov_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_mov_demuxer = {
    .name           = "mov,mp4,m4a,3gp,3g2,mj2",
    .long_name      = NULL_IF_CONFIG_SMALL("QuickTime / MOV"),
    .priv_class     = &mov_class,
    .priv_data_size = sizeof(MOVContext),
    .extensions     = "mov,mp4,m4a,3gp,3g2,mj2",
    .read_probe     = mov_probe,
    .read_header    = mov_read_header,
    .read_packet    = mov_read_packet,
    .read_close     = mov_read_close,
    .read_seek      = mov_read_seek,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
