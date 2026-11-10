/*
 * Super Audio CD (SACD) demuxer
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
 * Super Audio CD (SACD) demuxer
 *
 * A disc holds a Master TOC and up to two Audio Areas, a multi channel one
 * and a 2-channel stereo one, as a rule two mixes of the same tracks. Each
 * area has an Area TOC in front of and behind its Track Area. The Track Area
 * is a run of Audio Sectors, whose headers cut it into the packets of
 * Multiplexed Frames of 1/75 s, which carry either plain DSD or one DST
 * frame.
 *
 * Each area is a program with one stream here. A file has one list of
 * chapters, which are the tracks of the first area.
 *
 * The structure and field names are the ones of the Super Audio CD System
 * Description, as far as they are public.
 *
 *   Master TOC                              EP 0956557 B1
 *   Audio Area, Audio Sector, Time Code     US 6522695 B1
 *   Audio_Header, Packet_Info               US 6661762 B1
 *   Frame_Info                              US 7028152 B2
 *   frame formats with 14 and 16 sectors    US 6275457 B1
 *   Track_List_2                            EP 0987709 A2
 *   Access_List                             US 6643228 B1
 *   channel order, pauses                   Philips, DSDIFF 1.5
 *   character sets, text items              Sonic Studio, SACD Text Overview
 */

#include "config.h"

#include <inttypes.h>
#include <string.h>
#if CONFIG_ICONV
#include <iconv.h>
#endif

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"

#include "avformat.h"
#include "avio_internal.h"
#include "demux.h"
#include "internal.h"

#define SECTOR_SIZE          2048
/* An image may also keep the whole data frame of each sector, with the ID,
 * IED and CPR_MAI in front of the data and the EDC behind it (ECMA-267, 16). */
#define RAW_SECTOR_SIZE      2064
#define RAW_SECTOR_HEADER      12

#define MASTER_TOC_LSN        510

#define FRAMES_PER_SECOND      75
#define BITS_PER_SAMPLE         8
/* DSD has samples of one bit at 64 * 44100 Hz */
#define SAMPLE_RATE       (64 * 44100 / BITS_PER_SAMPLE)
#define FRAME_SAMPLES     (SAMPLE_RATE / FRAMES_PER_SECOND)

#define MAX_FRAME_SECTORS      16
#define MAX_PACKET_SIZE   (SECTOR_SIZE - 3)
#define MAX_FRAME_SIZE    (MAX_FRAME_SECTORS * MAX_PACKET_SIZE)

#define MAX_TRACKS            255
#define MAX_TRACK_TEXT_SECTORS 32

static const struct {
    int toc_address;            /* offset of the address in Master_TOC_0 */
    const char *signature;
    const char *name;
} areas[] = {
    { 0x48, "MULCHTOC", "Multi Channel"    },
    { 0x40, "TWOCHTOC", "2-Channel Stereo" },
};

enum {
    FRAME_FORMAT_DST         = 0,
    FRAME_FORMAT_DSD_3_IN_14 = 2,
    FRAME_FORMAT_DSD_3_IN_16 = 3,
};

#define DATA_TYPE_AUDIO         2

typedef struct SACDArea {
    int frame_format;
    uint32_t start;             /* first sector of the Track Area */
    uint32_t end;               /* last sector of the Track Area */
    int nb_frames;              /* play time of the Track Area */
    int first_frame;            /* time code of the start of the first track */

    uint8_t *access_list;       /* entries of 5 bytes */
    int access_step;            /* frames per entry */

    int64_t lsn;                /* next sector to read */
    int frame;                  /* frames before this time code are dropped */
    int idle;                   /* not read, its stream is discarded */
    int nb_packets;             /* packets of the sector read last */
    int packet;                 /* the next one of them */
    int info_pos;               /* offset of the next Frame_Info */
    int data_pos;               /* offset of the data of the next packet */
    uint8_t sector[SECTOR_SIZE];
} SACDArea;

typedef struct SACDContext {
    int sector_size;            /* bytes per sector in the image */
    int sector_offset;          /* offset of the data in a sector */

    int64_t pos;                /* next frame, counted from the first track */
    uint8_t frame[MAX_FRAME_SIZE];
} SACDContext;

static int sacd_probe(const AVProbeData *p)
{
    static const int offsets[] = {
        MASTER_TOC_LSN * SECTOR_SIZE,
        MASTER_TOC_LSN * RAW_SECTOR_SIZE + RAW_SECTOR_HEADER,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(offsets); i++)
        if (p->buf_size >= offsets[i] + 8 &&
            !memcmp(p->buf + offsets[i], "SACDMTOC", 8))
            return AVPROBE_SCORE_MAX;
    return 0;
}

static int read_sectors(AVFormatContext *s, int64_t lsn, uint8_t *buf, int nb)
{
    SACDContext *c = s->priv_data;

    for (int i = 0; i < nb; i++) {
        int64_t pos = (lsn + i) * c->sector_size + c->sector_offset;
        int64_t ret = avio_seek(s->pb, pos, SEEK_SET);
        if (ret < 0)
            return ret;
        ret = ffio_read_size(s->pb, buf + i * SECTOR_SIZE, SECTOR_SIZE);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int time_code(const uint8_t *tc)
{
    if (tc[1] >= 60 || tc[2] >= FRAMES_PER_SECOND)
        return -1;
    return (tc[0] * 60 + tc[1]) * FRAMES_PER_SECOND + tc[2];
}

/* The part of ISO 8859 whose right half the ISO 2022 escape sequence
 * ESC - F designates, by its final byte F (ISO-IR). */
static const uint8_t iso8859_parts[] = {
    ['A'] = 1,  ['B'] = 2,  ['C'] = 3,  ['D'] = 4,  ['L'] = 5,  ['G'] = 6,
    ['F'] = 7,  ['H'] = 8,  ['M'] = 9,  ['V'] = 10, ['T'] = 11, ['Y'] = 13,
    ['_'] = 14, ['b'] = 15, ['f'] = 16,
};

static int put_iso8859(AVBPrint *bp, int part, const uint8_t *str, int len)
{
    if (part == 1) {
        for (int i = 0; i < len; i++) {
            uint8_t tmp;
            PUT_UTF8(str[i], tmp, av_bprint_chars(bp, tmp, 1);)
        }
        return 0;
    }

#if CONFIG_ICONV
    char name[16];
    snprintf(name, sizeof(name), "ISO-8859-%d", part);
    iconv_t cd = iconv_open("UTF-8", name);
    if (cd == (iconv_t)-1)
        return AVERROR(ENOSYS);

    char *in = (char *)str;
    size_t in_size = len;
    while (in_size) {
        char buf[256];
        char *out = buf;
        size_t out_size = sizeof(buf);
        if (iconv(cd, &in, &in_size, &out, &out_size) == (size_t)-1 &&
            errno != E2BIG)
            break;
        av_bprint_append_data(bp, buf, out - buf);
    }
    iconv_close(cd);
    return in_size ? AVERROR_INVALIDDATA : 0;
#else
    return AVERROR(ENOSYS);
#endif
}

/* The text is ISO 8859-1, ends at a NUL or with its field and may be padded
 * with spaces. In character set 7 an ISO 2022 escape sequence may put the
 * right half of another part of ISO 8859 into the upper half of the code. */
static void set_string(AVFormatContext *s, AVDictionary **m, const char *key,
                       const uint8_t *str, int max_len, int flags)
{
    enum { ESC = 0x1b };
    AVBPrint bp;
    int len = 0, part = 1;

    for (int i = 0; i < max_len && str[i]; i++)
        if (str[i] != ' ')
            len = i + 1;

    av_bprint_init(&bp, len + 1, AV_BPRINT_SIZE_UNLIMITED);
    while (len) {
        int n;

        if (*str == ESC) {
            part = 0;
            if (len > 2 && str[1] == '-' &&
                str[2] < FF_ARRAY_ELEMS(iso8859_parts))
                part = iso8859_parts[str[2]];
            if (!part) {
                avpriv_request_sample(s, "Text in a character set other than "
                                      "ISO 8859");
                goto end;
            }
            n = 3;
        } else {
            const uint8_t *esc = memchr(str, ESC, len);
            n = esc ? esc - str : len;
            if (put_iso8859(&bp, part, str, n) < 0) {
                av_log(s, AV_LOG_WARNING,
                       "Cannot convert text from ISO 8859-%d\n", part);
                goto end;
            }
        }
        str += n;
        len -= n;
    }
    if (bp.len && av_bprint_is_complete(&bp))
        av_dict_set(m, key, bp.str, flags);
end:
    av_bprint_finalize(&bp, NULL);
}

static void set_text(AVFormatContext *s, AVDictionary **m, const char *key,
                     const uint8_t *buf, int size, int offset, int flags)
{
    if (offset && offset < size)
        set_string(s, m, key, buf + offset, size - offset, flags);
}

/* The General Genre Table has the genre codes of CD-TEXT. */
static const char *const genres[] = {
    [ 2] = "Adult Contemporary",
    [ 3] = "Alternative Rock",
    [ 4] = "Children's Music",
    [ 5] = "Classical",
    [ 6] = "Contemporary Christian",
    [ 7] = "Country",
    [ 8] = "Dance",
    [ 9] = "Easy Listening",
    [10] = "Erotic",
    [11] = "Folk",
    [12] = "Gospel",
    [13] = "Hip Hop",
    [14] = "Jazz",
    [15] = "Latin",
    [16] = "Musical",
    [17] = "New Age",
    [18] = "Opera",
    [19] = "Operetta",
    [20] = "Pop Music",
    [21] = "Rap",
    [22] = "Reggae",
    [23] = "Rock Music",
    [24] = "Rhythm & Blues",
    [25] = "Sound Effects",
    [26] = "Sound Track",
    [27] = "Spoken Word",
    [28] = "World Music",
    [29] = "Blues",
};

static void set_genre(AVDictionary **m, const uint8_t *genre)
{
    enum { GENERAL_GENRE_TABLE = 1 };
    unsigned index = AV_RB16(genre + 2);

    if (genre[0] == GENERAL_GENRE_TABLE && index < FF_ARRAY_ELEMS(genres) &&
        genres[index])
        av_dict_set(m, "genre", genres[index], AV_DICT_DONT_OVERWRITE);
}

static int text_supported(AVFormatContext *s, const uint8_t *text_channels)
{
    enum {
        CHARSET_ISO646        = 1,
        CHARSET_ISO8859_1     = 2,
        CHARSET_ISO8859_1_ESC = 7,
    };
    int charset = text_channels[8 + 2];

    if (!text_channels[0])
        return 0;
    if (charset == CHARSET_ISO646 || charset == CHARSET_ISO8859_1 ||
        charset == CHARSET_ISO8859_1_ESC)
        return 1;
    avpriv_request_sample(s, "Text in character set %d", charset);
    return 0;
}

static int read_master_toc(AVFormatContext *s, uint32_t *toc_lsn)
{
    /* the texts of the disc, then those of the album, which only fill in
     * what the disc does not have */
    static const struct {
        int offset;
        const char *key;
    } texts[] = {
        { 0x20, "title"        },
        { 0x22, "artist"       },
        { 0x24, "publisher"    },
        { 0x26, "copyright"    },
        { 0x10, "album"        },
        { 0x12, "album_artist" },
        { 0x14, "publisher"    },
        { 0x16, "copyright"    },
    };
    SACDContext *c = s->priv_data;
    uint8_t buf[SECTOR_SIZE];
    char str[32];
    int ret;

    c->sector_size = SECTOR_SIZE;
    if ((ret = read_sectors(s, MASTER_TOC_LSN, buf, 1)) < 0)
        return ret;
    if (memcmp(buf, "SACDMTOC", 8)) {
        c->sector_size   = RAW_SECTOR_SIZE;
        c->sector_offset = RAW_SECTOR_HEADER;
        if ((ret = read_sectors(s, MASTER_TOC_LSN, buf, 1)) < 0)
            return ret;
        if (memcmp(buf, "SACDMTOC", 8))
            return AVERROR_INVALIDDATA;
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(areas); i++)
        toc_lsn[i] = AV_RB32(buf + areas[i].toc_address);

    if (AV_RB16(buf + 0x10) && AV_RB16(buf + 0x12)) {
        snprintf(str, sizeof(str), "%d/%d", AV_RB16(buf + 0x12),
                 AV_RB16(buf + 0x10));
        av_dict_set(&s->metadata, "disc", str, 0);
    }
    set_string(s, &s->metadata, "album_catalog_number", buf + 0x18, 16, 0);
    set_string(s, &s->metadata, "catalog_number",       buf + 0x58, 16, 0);
    for (int i = 0; i < 4; i++)
        set_genre(&s->metadata, buf + 0x68 + 4 * i);
    if (AV_RB16(buf + 0x78) && buf[0x7a] && buf[0x7b]) {
        snprintf(str, sizeof(str), "%04d-%02d-%02d", AV_RB16(buf + 0x78),
                 buf[0x7a], buf[0x7b]);
        av_dict_set(&s->metadata, "date", str, 0);
    }

    /* only the first Text Channel is read, its texts are in the next sector */
    if (!text_supported(s, buf + 0x80))
        return 0;
    if ((ret = read_sectors(s, MASTER_TOC_LSN + 1, buf, 1)) < 0)
        return ret;
    if (memcmp(buf, "SACDText", 8))
        return 0;
    for (int i = 0; i < FF_ARRAY_ELEMS(texts); i++)
        set_text(s, &s->metadata, texts[i].key, buf, sizeof(buf),
                 AV_RB16(buf + texts[i].offset), AV_DICT_DONT_OVERWRITE);

    return 0;
}

static void read_track_text(AVFormatContext *s, AVDictionary **m,
                            const uint8_t *buf, int size, int track)
{
    static const char *const keys[] = {
        [1] = "title",
        [2] = "artist",
        [3] = "songwriter",
        [4] = "composer",
        [5] = "arranger",
        [6] = "comment",
    };
    int pos = AV_RB16(buf + 8 + 2 * track);
    int nb_items;

    /* the items of a track follow their count, an item is its type, one more
     * byte and the text, padded to a multiple of 4 bytes */
    if (!pos || pos + 4 > size)
        return;
    nb_items = buf[pos];
    pos     += 4;
    for (int i = 0; i < nb_items && pos + 2 < size; i++) {
        int type = buf[pos];
        if (type < FF_ARRAY_ELEMS(keys) && keys[type])
            set_string(s, m, keys[type], buf + pos + 2, size - pos - 2, 0);
        pos += 2 + av_strnlen((const char *)buf + pos + 2, size - pos - 2) + 1;
        pos  = FFALIGN(pos, 4);
    }
}

static int read_access_list(AVFormatContext *s, SACDArea *a, int64_t lsn)
{
    uint8_t *buf;
    int nb_entries, nb_sectors, ret;

    buf = av_malloc(SECTOR_SIZE);
    if (!buf)
        return AVERROR(ENOMEM);
    if ((ret = read_sectors(s, lsn, buf, 1)) < 0)
        goto end;

    nb_entries     = AV_RB16(buf + 8);
    a->access_step = buf[10];
    if (memcmp(buf, "SACD_ACC", 8) || !a->access_step ||
        nb_entries != (a->nb_frames - 1) / a->access_step + 1) {
        av_log(s, AV_LOG_WARNING, "No valid Access List, cannot seek\n");
        goto end;
    }

    nb_sectors = (16 + 5 * nb_entries + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if ((ret = av_reallocp(&buf, nb_sectors * SECTOR_SIZE)) < 0)
        return ret;
    if ((ret = read_sectors(s, lsn + 1, buf + SECTOR_SIZE, nb_sectors - 1)) < 0)
        goto end;

    memmove(buf, buf + 16, 5 * nb_entries);
    a->access_list = buf;
    return 0;

end:
    av_free(buf);
    return ret;
}

static void seek_frame(AVFormatContext *s, AVStream *st, int64_t pos)
{
    SACDArea *a = st->priv_data;
    int frame   = a->first_frame + FFMIN(pos, a->nb_frames - a->first_frame);
    int64_t lsn = a->start;

    a->frame = frame;
    /* nothing is left of the area past its end */
    if (frame == a->nb_frames)
        return;

    if (a->frame_format == FRAME_FORMAT_DSD_3_IN_14) {
        lsn += frame / 3 * 14;
    } else if (a->frame_format == FRAME_FORMAT_DSD_3_IN_16) {
        lsn += frame / 3 * 16;
    } else if (a->access_list) {
        /* An entry has flags and the sector in which the first frame of its
         * interval starts. The frames after it are interpolated up to the
         * next entry, which lands at most Access_Margin sectors too far.
         * With the Sub_Access flag there is no margin to go by. */
        const uint8_t *entry = a->access_list + 5 * (frame / a->access_step);
        int flags            = AV_RB16(entry);
        int64_t start        = AV_RB24(entry + 2);
        int64_t next, nb;

        if (frame / a->access_step < (a->nb_frames - 1) / a->access_step) {
            next = AV_RB24(entry + 5 + 2);
            nb   = a->access_step;
        } else {
            next = a->end;
            nb   = (a->nb_frames - 1) % a->access_step + 1;
        }
        lsn = start;
        if (!(flags & 0x8000) && next > start)
            lsn = FFMAX(start, start + frame % a->access_step *
                                       (next - start) / nb - (flags & 0x7fff));
        lsn = FFMAX(lsn, a->start);
    }

    av_log(s, AV_LOG_TRACE, "Stream %d goes on with frame %d from sector "
           "%"PRId64"\n", st->index, frame, lsn);

    a->lsn        = lsn;
    a->nb_packets = a->packet = 0;
}

static int read_area_toc(AVFormatContext *s, int area, int64_t toc_lsn,
                         int64_t other_toc_lsn)
{
    /* the channels are in the order of standard layouts */
    static const AVChannelLayout layouts[] = {
        [2] = AV_CHANNEL_LAYOUT_STEREO,
        [5] = AV_CHANNEL_LAYOUT_5POINT0,
        [6] = AV_CHANNEL_LAYOUT_5POINT1,
    };
    uint8_t buf[2 * SECTOR_SIZE], tracks[SECTOR_SIZE];
    uint8_t *track_text = NULL;
    int toc_size, nb_channels, nb_tracks, has_text;
    int track_text_pos, track_text_size = 0, access_list_pos;
    AVProgram *program;
    AVStream *st;
    SACDArea *a;
    int ret;

    if ((ret = read_sectors(s, toc_lsn, buf, 1)) < 0)
        return ret;
    if (memcmp(buf, areas[area].signature, 8))
        return AVERROR_INVALIDDATA;

    a = av_mallocz(sizeof(*a));
    if (!a)
        return AVERROR(ENOMEM);
    st = avformat_new_stream(s, NULL);
    if (!st) {
        av_free(a);
        return AVERROR(ENOMEM);
    }
    st->priv_data = a;

    program = av_new_program(s, area + 1);
    if (!program)
        return AVERROR(ENOMEM);
    av_program_add_stream_index(s, program->id, st->index);
    av_dict_set(&program->metadata, "title", areas[area].name, 0);

    toc_size        = AV_RB16(buf + 0x0a);
    a->frame_format = buf[0x15] & 0xf;
    nb_channels     = buf[0x20];
    a->nb_frames    = time_code(buf + 0x40);
    /* the tracks of the first area are the chapters, those of the other one
     * are left out, as a file has only one list of chapters */
    nb_tracks       = s->nb_streams > 1 ? 0 : buf[0x45];
    a->start        = AV_RB32(buf + 0x48);
    a->end          = AV_RB32(buf + 0x4c);
    has_text        = text_supported(s, buf + 0x50);
    track_text_pos  = AV_RB16(buf + 0x80);
    access_list_pos = AV_RB16(buf + 0x84);

    if (buf[0x14] != 4) {
        avpriv_request_sample(s, "Sampling frequency code %d", buf[0x14]);
        return AVERROR_PATCHWELCOME;
    }
    if (a->frame_format != FRAME_FORMAT_DST &&
        a->frame_format != FRAME_FORMAT_DSD_3_IN_14 &&
        a->frame_format != FRAME_FORMAT_DSD_3_IN_16) {
        avpriv_request_sample(s, "Frame format %d", a->frame_format);
        return AVERROR_PATCHWELCOME;
    }
    if (nb_channels >= FF_ARRAY_ELEMS(layouts) ||
        !layouts[nb_channels].nb_channels ||
        (nb_channels != 2 && a->frame_format != FRAME_FORMAT_DST) ||
        a->start > a->end)
        return AVERROR_INVALIDDATA;

    if (has_text) {
        set_text(s, &program->metadata, "description", buf, SECTOR_SIZE,
                 AV_RB16(buf + 0x90), 0);
        set_text(s, &program->metadata, "copyright", buf, SECTOR_SIZE,
                 AV_RB16(buf + 0x92), 0);
    }

    if ((ret = read_sectors(s, toc_lsn + 2, tracks, 1)) < 0)
        return ret;
    if (memcmp(tracks, "SACDTRL2", 8))
        return AVERROR_INVALIDDATA;
    /* the first track has to start within the play time, which also turns
     * down an area that has none */
    a->first_frame = time_code(tracks + 8);
    if (a->first_frame < 0 || a->first_frame >= a->nb_frames)
        return AVERROR_INVALIDDATA;

    if (nb_tracks) {
        /* the ISRCs and the genres of the tracks */
        if ((ret = read_sectors(s, toc_lsn + 3, buf, 2)) < 0)
            return ret;
        if (memcmp(buf, "SACD_IGL", 8))
            return AVERROR_INVALIDDATA;
    }

    uint8_t other_isrc[2 * SECTOR_SIZE];
    if (nb_tracks && other_toc_lsn &&
        (ret = read_sectors(s, other_toc_lsn + 3, other_isrc, 2)) < 0)
        return ret;

    /* the lists are read in their order on the disc, which also works with
     * an input that cannot seek back */
    if (a->frame_format == FRAME_FORMAT_DST &&
        (ret = read_access_list(s, a, toc_lsn + access_list_pos)) < 0)
        return ret;

    if (nb_tracks && has_text && track_text_pos && track_text_pos < toc_size) {
        track_text_size = FFMIN(toc_size - track_text_pos,
                                MAX_TRACK_TEXT_SECTORS) * SECTOR_SIZE;
        track_text      = av_malloc(track_text_size);
        if (!track_text)
            return AVERROR(ENOMEM);
        ret = read_sectors(s, toc_lsn + track_text_pos, track_text,
                           track_text_size / SECTOR_SIZE);
        if (ret < 0)
            goto end;
        if (memcmp(track_text, "SACDTTxt", 8))
            track_text_size = 0;
    }

    for (int i = 0; i < nb_tracks; i++) {
        int start  = time_code(tracks + 8 + 4 * i);
        int length = time_code(tracks + 8 + 4 * MAX_TRACKS + 4 * i);
        AVChapter *chapter;

        if (start < a->first_frame || length < 0)
            continue;
        chapter = avpriv_new_chapter(s, i + 1,
                                     (AVRational){ 1, FRAMES_PER_SECOND },
                                     start - a->first_frame,
                                     start + length - a->first_frame, NULL);
        if (!chapter) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        if (track_text_size)
            read_track_text(s, &chapter->metadata, track_text,
                            track_text_size, i);
        /* the mixes are recordings of their own, their ISRCs may differ */
        if (!other_toc_lsn ||
            !memcmp(buf + 8 + 12 * i, other_isrc + 8 + 12 * i, 12))
            set_string(s, &chapter->metadata, "isrc", buf + 8 + 12 * i, 12, 0);
        set_genre(&chapter->metadata, buf + 8 + 12 * MAX_TRACKS + 4 + 4 * i);
    }

    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->format      = AV_SAMPLE_FMT_DSD;
    st->codecpar->sample_rate = SAMPLE_RATE;
    st->codecpar->ch_layout   = layouts[nb_channels];
    if (a->frame_format == FRAME_FORMAT_DST) {
        st->codecpar->codec_id = AV_CODEC_ID_DST;
        st->codecpar->bit_rate = av_rescale(a->end - a->start + 1LL,
                                            SECTOR_SIZE * 8 * FRAMES_PER_SECOND,
                                            a->nb_frames);
    } else {
        st->codecpar->codec_id    = AV_CODEC_ID_DSD_MSBF;
        st->codecpar->block_align = nb_channels;
        st->codecpar->bit_rate    = nb_channels * BITS_PER_SAMPLE * SAMPLE_RATE;
    }
    avpriv_set_pts_info(st, 64, 1, SAMPLE_RATE);
    st->start_time = 0;
    st->duration   = (a->nb_frames - a->first_frame) * (int64_t)FRAME_SAMPLES;

    seek_frame(s, st, 0);
    ret = 0;

end:
    av_free(track_text);
    return ret;
}

static int sacd_read_header(AVFormatContext *s)
{
    uint32_t toc_lsn[FF_ARRAY_ELEMS(areas)];
    int ret;

    if ((ret = read_master_toc(s, toc_lsn)) < 0)
        return ret;

    for (int i = 0; i < FF_ARRAY_ELEMS(areas); i++) {
        if (!toc_lsn[i])
            continue;
        ret = read_area_toc(s, i, toc_lsn[i], toc_lsn[!i]);
        if (ret < 0)
            return ret;
    }

    return s->nb_streams ? 0 : AVERROR_INVALIDDATA;
}

/* A sector starts with the number of its packets and of the frames that
 * start in it, and a flag for DST. Then come a Packet_Info per packet, with
 * a flag for the start of a frame, the type of the data and its length, a
 * Frame_Info per frame start, with a fourth byte for DST, and the data. */
static int read_audio_header(SACDArea *a)
{
    const uint8_t *buf = a->sector;
    int dst         = a->frame_format == FRAME_FORMAT_DST;
    int nb_packets  = buf[0] >> 5;
    int nb_starts   = buf[0] >> 2 & 7;
    int size        = 1 + 2 * nb_packets + (dst ? 4 : 3) * nb_starts;

    if (!nb_packets || (buf[0] & 1) != dst)
        return AVERROR_INVALIDDATA;

    a->info_pos = 1 + 2 * nb_packets;
    a->data_pos = size;
    for (int i = 0; i < nb_packets; i++) {
        int info = AV_RB16(buf + 1 + 2 * i);
        if (!(info & 0x7ff))
            return AVERROR_INVALIDDATA;
        nb_starts -= info >> 15;
        size      += info & 0x7ff;
    }
    if (nb_starts || size > SECTOR_SIZE)
        return AVERROR_INVALIDDATA;

    a->nb_packets = nb_packets;
    a->packet     = 0;
    return 0;
}

static int read_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    SACDContext *c = s->priv_data;
    SACDArea *a    = st->priv_data;
    /* size of the frame collected so far, negative while there is none */
    int size = -1;
    int frame = 0;
    int64_t pos = 0;
    /* a run of invalid sectors is reported once */
    int invalid = 0;
    int ret;

    for (;;) {
        int info, length;

        if (a->packet == a->nb_packets) {
            if (a->lsn > a->end)
                break;
            if ((ret = read_sectors(s, a->lsn, a->sector, 1)) < 0)
                return ret;
            a->lsn++;
            if (read_audio_header(a) < 0) {
                if (!invalid)
                    av_log(s, AV_LOG_WARNING, "Invalid Audio Sector "
                           "%"PRId64"\n", a->lsn - 1);
                invalid = 1;
                size    = -1;
                continue;
            }
            invalid = 0;
        }

        info   = AV_RB16(a->sector + 1 + 2 * a->packet);
        length = info & 0x7ff;

        if (info & 0x8000) {
            /* a frame lasts up to the start of the next one, and a frame
             * start that no audio packet follows is padding */
            if (size > 0)
                break;

            frame        = time_code(a->sector + a->info_pos);
            a->info_pos += a->frame_format == FRAME_FORMAT_DST ? 4 : 3;
            size         = -1;
            if (frame >= a->frame) {
                pos  = (a->lsn - 1) * c->sector_size + c->sector_offset;
                size = 0;
            }
        }

        if (size >= 0 && (info >> 11 & 7) == DATA_TYPE_AUDIO) {
            if (size + length > MAX_FRAME_SIZE) {
                av_log(s, AV_LOG_WARNING, "Frame at sector %"PRId64" is too "
                       "large\n", a->lsn - 1);
                size = -1;
            } else {
                memcpy(c->frame + size, a->sector + a->data_pos, length);
                size += length;
            }
        }

        a->data_pos += length;
        a->packet++;
    }

    if (size <= 0)
        return AVERROR_EOF;

    if ((ret = av_new_packet(pkt, size)) < 0)
        return ret;
    memcpy(pkt->data, c->frame, size);
    pkt->stream_index = st->index;
    pkt->pos          = pos;
    pkt->pts          = (frame - (int64_t)a->first_frame) * FRAME_SAMPLES;
    pkt->duration     = FRAME_SAMPLES;

    a->frame = frame + 1;
    c->pos   = a->frame - a->first_frame;
    return 0;
}

static int sacd_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SACDContext *c = s->priv_data;

    for (;;) {
        AVStream *st = NULL;
        SACDArea *a  = NULL;
        int ret;

        /* the areas are read in the order of time */
        for (unsigned i = 0; i < s->nb_streams; i++) {
            AVStream *cur = s->streams[i];
            SACDArea *b   = cur->priv_data;

            if (cur->discard >= AVDISCARD_ALL) {
                b->idle = 1;
                continue;
            }
            /* an area that was not read goes on where the other one is */
            if (b->idle)
                seek_frame(s, cur, c->pos);
            b->idle = 0;

            if (b->frame < b->nb_frames &&
                (!a || b->frame - b->first_frame < a->frame - a->first_frame)) {
                st = cur;
                a  = b;
            }
        }
        if (!a)
            return AVERROR_EOF;

        ret = read_frame(s, st, pkt);
        if (ret != AVERROR_EOF)
            return ret;
        /* this area has ended, go on with the other one */
        a->frame = a->nb_frames;
    }
}

static int sacd_read_seek(AVFormatContext *s, int stream_index,
                          int64_t timestamp, int flags)
{
    SACDContext *c = s->priv_data;

    for (unsigned i = 0; i < s->nb_streams; i++) {
        const SACDArea *a = s->streams[i]->priv_data;
        if (s->streams[i]->discard < AVDISCARD_ALL &&
            a->frame_format == FRAME_FORMAT_DST && !a->access_list)
            return AVERROR(ENOSYS);
    }

    if (!(flags & AVSEEK_FLAG_BACKWARD))
        timestamp = av_sat_add64(timestamp, FRAME_SAMPLES - 1);
    c->pos = FFMAX(timestamp / FRAME_SAMPLES, 0);
    if (flags & AVSEEK_FLAG_BACKWARD) {
        const SACDArea *a = s->streams[stream_index]->priv_data;
        c->pos = FFMIN(c->pos, a->nb_frames - a->first_frame - 1);
    }
    for (unsigned i = 0; i < s->nb_streams; i++)
        seek_frame(s, s->streams[i], c->pos);
    return 0;
}

static int sacd_read_close(AVFormatContext *s)
{
    for (unsigned i = 0; i < s->nb_streams; i++) {
        SACDArea *a = s->streams[i]->priv_data;
        av_freep(&a->access_list);
    }
    return 0;
}

const FFInputFormat ff_sacd_demuxer = {
    .p.name         = "sacd",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Super Audio CD (SACD)"),
    .p.flags        = AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK,
    .priv_data_size = sizeof(SACDContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = sacd_probe,
    .read_header    = sacd_read_header,
    .read_packet    = sacd_read_packet,
    .read_seek      = sacd_read_seek,
    .read_close     = sacd_read_close,
};
