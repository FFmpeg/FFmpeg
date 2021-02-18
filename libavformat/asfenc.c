/*
 * ASF muxer
 * Copyright (c) 2000, 2001 Fabrice Bellard
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

#include "libavutil/avassert.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "avlanguage.h"
#include "avio_internal.h"
#include "internal.h"
#include "riff.h"
#include "asf.h"

#define ASF_INDEXED_INTERVAL    10000000
#define ASF_INDEX_BLOCK         (1<<9)
#define ASF_PAYLOADS_PER_PACKET 63

#define ASF_PACKET_ERROR_CORRECTION_DATA_SIZE 0x2
#define ASF_PACKET_ERROR_CORRECTION_FLAGS          \
    (ASF_PACKET_FLAG_ERROR_CORRECTION_PRESENT |    \
     ASF_PACKET_ERROR_CORRECTION_DATA_SIZE)

#if (ASF_PACKET_ERROR_CORRECTION_FLAGS != 0)
#   define ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE 1
#else
#   define ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE 0
#endif

#define ASF_PPI_PROPERTY_FLAGS                                       \
    (ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_IS_BYTE           |    \
     ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_IS_DWORD |    \
     ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_IS_BYTE       |    \
     ASF_PL_FLAG_STREAM_NUMBER_LENGTH_FIELD_IS_BYTE)

#define ASF_PPI_LENGTH_TYPE_FLAGS 0

#define ASF_PAYLOAD_FLAGS ASF_PL_FLAG_PAYLOAD_LENGTH_FIELD_IS_WORD

#if (ASF_PPI_FLAG_SEQUENCE_FIELD_IS_BYTE == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_SEQUENCE_FIELD_SIZE))
#   define ASF_PPI_SEQUENCE_FIELD_SIZE 1
#endif
#if (ASF_PPI_FLAG_SEQUENCE_FIELD_IS_WORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_SEQUENCE_FIELD_SIZE))
#   define ASF_PPI_SEQUENCE_FIELD_SIZE 2
#endif
#if (ASF_PPI_FLAG_SEQUENCE_FIELD_IS_DWORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_SEQUENCE_FIELD_SIZE))
#   define ASF_PPI_SEQUENCE_FIELD_SIZE 4
#endif
#ifndef ASF_PPI_SEQUENCE_FIELD_SIZE
#   define ASF_PPI_SEQUENCE_FIELD_SIZE 0
#endif

#if (ASF_PPI_FLAG_PACKET_LENGTH_FIELD_IS_BYTE == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PACKET_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PACKET_LENGTH_FIELD_SIZE 1
#endif
#if (ASF_PPI_FLAG_PACKET_LENGTH_FIELD_IS_WORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PACKET_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PACKET_LENGTH_FIELD_SIZE 2
#endif
#if (ASF_PPI_FLAG_PACKET_LENGTH_FIELD_IS_DWORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PACKET_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PACKET_LENGTH_FIELD_SIZE 4
#endif
#ifndef ASF_PPI_PACKET_LENGTH_FIELD_SIZE
#   define ASF_PPI_PACKET_LENGTH_FIELD_SIZE 0
#endif

#if (ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_BYTE == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PADDING_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PADDING_LENGTH_FIELD_SIZE 1
#endif
#if (ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_WORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PADDING_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PADDING_LENGTH_FIELD_SIZE 2
#endif
#if (ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_DWORD == (ASF_PPI_LENGTH_TYPE_FLAGS & ASF_PPI_MASK_PADDING_LENGTH_FIELD_SIZE))
#   define ASF_PPI_PADDING_LENGTH_FIELD_SIZE 4
#endif
#ifndef ASF_PPI_PADDING_LENGTH_FIELD_SIZE
#   define ASF_PPI_PADDING_LENGTH_FIELD_SIZE 0
#endif

#if (ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_IS_BYTE == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_REPLICATED_DATA_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE 1
#endif
#if (ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_IS_WORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_REPLICATED_DATA_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE 2
#endif
#if (ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_IS_DWORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_REPLICATED_DATA_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE 4
#endif
#ifndef ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE
#   define ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE 0
#endif

#if (ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_IS_BYTE == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE 1
#endif
#if (ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_IS_WORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE 2
#endif
#if (ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_IS_DWORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE 4
#endif
#ifndef ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE
#   define ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE 0
#endif

#if (ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_IS_BYTE == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE 1
#endif
#if (ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_IS_WORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE 2
#endif
#if (ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_IS_DWORD == (ASF_PPI_PROPERTY_FLAGS & ASF_PL_MASK_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE 4
#endif
#ifndef ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE
#   define ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE 0
#endif

#if (ASF_PL_FLAG_PAYLOAD_LENGTH_FIELD_IS_BYTE == (ASF_PAYLOAD_FLAGS & ASF_PL_MASK_PAYLOAD_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_LENGTH_FIELD_SIZE 1
#endif
#if (ASF_PL_FLAG_PAYLOAD_LENGTH_FIELD_IS_WORD == (ASF_PAYLOAD_FLAGS & ASF_PL_MASK_PAYLOAD_LENGTH_FIELD_SIZE))
#   define ASF_PAYLOAD_LENGTH_FIELD_SIZE 2
#endif
#ifndef ASF_PAYLOAD_LENGTH_FIELD_SIZE
#   define ASF_PAYLOAD_LENGTH_FIELD_SIZE 0
#endif

#define PACKET_HEADER_MIN_SIZE \
    (ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE +       \
     ASF_PACKET_ERROR_CORRECTION_DATA_SIZE +              \
     1 +        /* Length Type Flags */                   \
     1 +        /* Property Flags */                      \
     ASF_PPI_PACKET_LENGTH_FIELD_SIZE +                   \
     ASF_PPI_SEQUENCE_FIELD_SIZE +                        \
     ASF_PPI_PADDING_LENGTH_FIELD_SIZE +                  \
     4 +        /* Send Time Field */                     \
     2)         /* Duration Field */

// Replicated Data shall be at least 8 bytes long.
#define ASF_PAYLOAD_REPLICATED_DATA_LENGTH 0x08

#define PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD                \
    (1 +     /* Stream Number */                          \
     ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE +         \
     ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE +    \
     ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE +      \
     ASF_PAYLOAD_REPLICATED_DATA_LENGTH)

#define PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS             \
    (1 +        /* Stream Number */                       \
     ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE +         \
     ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE +    \
     ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE +      \
     ASF_PAYLOAD_REPLICATED_DATA_LENGTH +                 \
     ASF_PAYLOAD_LENGTH_FIELD_SIZE)

#define SINGLE_PAYLOAD_HEADERS                            \
    (PACKET_HEADER_MIN_SIZE +                             \
     PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD)

#define MULTI_PAYLOAD_HEADERS                             \
    (PACKET_HEADER_MIN_SIZE +                             \
     1 +         /* Payload Flags */                      \
     2 * PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS)

#define DATA_HEADER_SIZE 50

#define PACKET_SIZE_MAX 65536
#define PACKET_SIZE_MIN 100

typedef struct ASFPayload {
    uint8_t type;
    uint16_t size;
} ASFPayload;

typedef struct ASFStream {
    int num;
    unsigned char seq;
    /* use for reading */
    AVPacket pkt;
    int frag_offset;
    int packet_obj_size;
    int timestamp;
    int64_t duration;
    int skip_to_key;
    int pkt_clean;

    int ds_span;                /* descrambling  */
    int ds_packet_size;
    int ds_chunk_size;

    int64_t packet_pos;

    uint16_t stream_language_index;

    int      palette_changed;
    uint32_t palette[256];

    int payload_ext_ct;
    ASFPayload payload[8];
} ASFStream;

typedef struct ASFContext {
    AVClass *av_class;
    uint32_t seqno;
    int is_streamed;
    ASFStream streams[128];              ///< it's max number and it's not that big
    const char *languages[128];
    int nb_languages;
    int64_t creation_time;
    /* non-streamed additional info */
    uint64_t nb_packets;                 ///< how many packets are there in the file, invalid if broadcasting
    int64_t duration;                    ///< in 100ns units
    /* packet filling */
    unsigned char multi_payloads_present;
    int packet_size_left;
    int64_t packet_timestamp_start;
    int64_t packet_timestamp_end;
    unsigned int packet_nb_payloads;
    uint8_t packet_buf[PACKET_SIZE_MAX];
    AVIOContext pb;
    /* only for reading */
    uint64_t data_offset;                ///< beginning of the first data packet

    ASFIndex *index_ptr;
    uint32_t nb_index_memory_alloc;
    uint16_t maximum_packet;
    uint32_t next_packet_number;
    uint16_t next_packet_count;
    uint64_t next_packet_offset;
    int      next_start_sec;
    int      end_sec;
    int      packet_size;
} ASFContext;

static const AVCodecTag codec_asf_bmp_tags[] = {
    { AV_CODEC_ID_MPEG4,     MKTAG('M', '4', 'S', '2') },
    { AV_CODEC_ID_MPEG4,     MKTAG('M', 'P', '4', 'S') },
    { AV_CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') },
    { AV_CODEC_ID_NONE,      0 },
};

static const AVCodecTag *const asf_codec_tags[] = {
        codec_asf_bmp_tags, ff_codec_bmp_tags, ff_codec_wav_tags, NULL
};

#define PREROLL_TIME 3100

static void put_str16(AVIOContext *s, const char *tag)
{
    int len;
    uint8_t *pb;
    AVIOContext *dyn_buf;
    if (avio_open_dyn_buf(&dyn_buf) < 0)
        return;

    avio_put_str16le(dyn_buf, tag);
    len = avio_close_dyn_buf(dyn_buf, &pb);
    avio_wl16(s, len);
    avio_write(s, pb, len);
    av_freep(&pb);
}

static int64_t put_header(AVIOContext *pb, const ff_asf_guid *g)
{
    int64_t pos;

    pos = avio_tell(pb);
    ff_put_guid(pb, g);
    avio_wl64(pb, 24);
    return pos;
}

/* update header size */
static void end_header(AVIOContext *pb, int64_t pos)
{
    int64_t pos1;

    pos1 = avio_tell(pb);
    avio_seek(pb, pos + 16, SEEK_SET);
    avio_wl64(pb, pos1 - pos);
    avio_seek(pb, pos1, SEEK_SET);
}

/* write an asf chunk (only used in streaming case) */
static void put_chunk(AVFormatContext *s, int type,
                      int payload_length, int flags)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int length;

    length = payload_length + 8;
    avio_wl16(pb, type);
    avio_wl16(pb, length);      // size
    avio_wl32(pb, asf->seqno);  // sequence number
    avio_wl16(pb, flags);       // unknown bytes
    avio_wl16(pb, length);      // size_confirm
    asf->seqno++;
}

/* convert from av time to windows time */
static int64_t unix_to_file_time(int64_t ti)
{
    int64_t t;

    t  = ti * INT64_C(10);
    t += INT64_C(116444736000000000);
    return t;
}

static int32_t get_send_time(ASFContext *asf, int64_t pres_time, uint64_t *offset)
{
    int i;
    int32_t send_time = 0;
    *offset = asf->data_offset + DATA_HEADER_SIZE;
    for (i = 0; i < asf->next_start_sec; i++) {
        if (pres_time <= asf->index_ptr[i].send_time)
            break;
        send_time = asf->index_ptr[i].send_time;
        *offset   = asf->index_ptr[i].offset;
    }

    return send_time / 10000;
}

static int asf_write_markers(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;
    AVRational scale = {1, 10000000};
    int64_t hpos = put_header(pb, &ff_asf_marker_header);

    ff_put_guid(pb, &ff_asf_reserved_4);// ASF spec mandates this reserved value
    avio_wl32(pb, s->nb_chapters);     // markers count
    avio_wl16(pb, 0);                  // ASF spec mandates 0 for this
    avio_wl16(pb, 0);                  // name length 0, no name given

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *c = s->chapters[i];
        AVDictionaryEntry *t = av_dict_get(c->metadata, "title", NULL, 0);
        int64_t pres_time = av_rescale_q(c->start, c->time_base, scale);
        uint64_t offset;
        int32_t send_time = get_send_time(asf, pres_time, &offset);
        int len = 0, ret;
        uint8_t *buf;
        AVIOContext *dyn_buf;
        if (t) {
            if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
                return ret;
            avio_put_str16le(dyn_buf, t->value);
            len = avio_close_dyn_buf(dyn_buf, &buf);
        }
        avio_wl64(pb, offset);            // offset of the packet with send_time
        avio_wl64(pb, pres_time + PREROLL_TIME * 10000); // presentation time
        avio_wl16(pb, 12 + len);          // entry length
        avio_wl32(pb, send_time);         // send time
        avio_wl32(pb, 0);                 // flags, should be 0
        avio_wl32(pb, len / 2);           // marker desc length in WCHARS!
        if (t) {
            avio_write(pb, buf, len);     // marker desc
            av_freep(&buf);
        }
    }
    end_header(pb, hpos);
    return 0;
}

/* write the header (used two times if non streamed) */
static int asf_write_header1(AVFormatContext *s, int64_t file_size,
                             int64_t data_chunk_size)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVDictionaryEntry *tags[5];
    int header_size, n, extra_size, extra_size2, wav_extra_size;
    int has_title, has_aspect_ratio = 0;
    int metadata_count;
    AVCodecParameters *par;
    int64_t header_offset, cur_pos, hpos;
    int bit_rate;
    int64_t duration;
    int audio_language_counts[128] = { 0 };

    ff_metadata_conv(&s->metadata, ff_asf_metadata_conv, NULL);

    tags[0] = av_dict_get(s->metadata, "title", NULL, 0);
    tags[1] = av_dict_get(s->metadata, "author", NULL, 0);
    tags[2] = av_dict_get(s->metadata, "copyright", NULL, 0);
    tags[3] = av_dict_get(s->metadata, "comment", NULL, 0);
    tags[4] = av_dict_get(s->metadata, "rating", NULL, 0);

    duration       = asf->duration + PREROLL_TIME * 10000;
    has_title      = tags[0] || tags[1] || tags[2] || tags[3] || tags[4];

    if (!file_size) {
        if (ff_parse_creation_time_metadata(s, &asf->creation_time, 0) != 0)
            av_dict_set(&s->metadata, "creation_time", NULL, 0);
    }

    metadata_count = av_dict_count(s->metadata);

    bit_rate = 0;
    for (n = 0; n < s->nb_streams; n++) {
        AVDictionaryEntry *entry;
        par = s->streams[n]->codecpar;

        avpriv_set_pts_info(s->streams[n], 32, 1, 1000); /* 32 bit pts in ms */

        bit_rate += par->bit_rate;
        if (   par->codec_type == AVMEDIA_TYPE_VIDEO
            && par->sample_aspect_ratio.num > 0
            && par->sample_aspect_ratio.den > 0)
            has_aspect_ratio++;

        entry = av_dict_get(s->streams[n]->metadata, "language", NULL, 0);
        if (entry) {
            const char *iso6391lang = ff_convert_lang_to(entry->value, AV_LANG_ISO639_1);
            if (iso6391lang) {
                int i;
                for (i = 0; i < asf->nb_languages; i++) {
                    if (!strcmp(asf->languages[i], iso6391lang)) {
                        asf->streams[n].stream_language_index = i;
                        break;
                    }
                }
                if (i >= asf->nb_languages) {
                    asf->languages[asf->nb_languages] = iso6391lang;
                    asf->streams[n].stream_language_index = asf->nb_languages;
                    asf->nb_languages++;
                }
                if (par->codec_type == AVMEDIA_TYPE_AUDIO)
                    audio_language_counts[asf->streams[n].stream_language_index]++;
            }
        } else {
            asf->streams[n].stream_language_index = 128;
        }
    }

    if (asf->is_streamed) {
        put_chunk(s, 0x4824, 0, 0xc00); /* start of stream (length will be patched later) */
    }

    ff_put_guid(pb, &ff_asf_header);
    avio_wl64(pb, -1); /* header length, will be patched after */
    avio_wl32(pb, 3 + has_title + !!metadata_count + s->nb_streams); /* number of chunks in header */
    avio_w8(pb, 1); /* ??? */
    avio_w8(pb, 2); /* ??? */

    /* file header */
    header_offset = avio_tell(pb);
    hpos          = put_header(pb, &ff_asf_file_header);
    ff_put_guid(pb, &ff_asf_my_guid);
    avio_wl64(pb, file_size);
    avio_wl64(pb, unix_to_file_time(asf->creation_time));
    avio_wl64(pb, asf->nb_packets); /* number of packets */
    avio_wl64(pb, duration); /* end time stamp (in 100ns units) */
    avio_wl64(pb, asf->duration); /* duration (in 100ns units) */
    avio_wl64(pb, PREROLL_TIME); /* start time stamp */
    avio_wl32(pb, (asf->is_streamed || !(pb->seekable & AVIO_SEEKABLE_NORMAL)) ? 3 : 2);  /* ??? */
    avio_wl32(pb, s->packet_size); /* packet size */
    avio_wl32(pb, s->packet_size); /* packet size */
    avio_wl32(pb, bit_rate ? bit_rate : -1); /* Maximum data rate in bps */
    end_header(pb, hpos);

    /* header_extension */
    hpos = put_header(pb, &ff_asf_head1_guid);
    ff_put_guid(pb, &ff_asf_head2_guid);
    avio_wl16(pb, 6);
    avio_wl32(pb, 0); /* length, to be filled later */
    if (asf->nb_languages) {
        int64_t hpos2;
        int i;
        int nb_audio_languages = 0;

        hpos2 = put_header(pb, &ff_asf_language_guid);
        avio_wl16(pb, asf->nb_languages);
        for (i = 0; i < asf->nb_languages; i++) {
            avio_w8(pb, 6);
            avio_put_str16le(pb, asf->languages[i]);
        }
        end_header(pb, hpos2);

        for (i = 0; i < asf->nb_languages; i++)
            if (audio_language_counts[i])
                nb_audio_languages++;

        if (nb_audio_languages > 1) {
            hpos2 = put_header(pb, &ff_asf_group_mutual_exclusion_object);
            ff_put_guid(pb, &ff_asf_mutex_language);
            avio_wl16(pb, nb_audio_languages);
            for (i = 0; i < asf->nb_languages; i++) {
                if (audio_language_counts[i]) {
                    avio_wl16(pb, audio_language_counts[i]);
                    for (n = 0; n < s->nb_streams; n++)
                        if (asf->streams[n].stream_language_index == i && s->streams[n]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                            avio_wl16(pb, n + 1);
                }
            }
            end_header(pb, hpos2);
        }

        for (n = 0; n < s->nb_streams; n++) {
            int64_t es_pos;
            if (asf->streams[n].stream_language_index > 127)
                continue;
            es_pos = put_header(pb, &ff_asf_extended_stream_properties_object);
            avio_wl64(pb, 0); /* start time */
            avio_wl64(pb, 0); /* end time */
            avio_wl32(pb, s->streams[n]->codecpar->bit_rate); /* data bitrate bps */
            avio_wl32(pb, 5000); /* buffer size ms */
            avio_wl32(pb, 0); /* initial buffer fullness */
            avio_wl32(pb, s->streams[n]->codecpar->bit_rate); /* peak data bitrate */
            avio_wl32(pb, 5000); /* maximum buffer size ms */
            avio_wl32(pb, 0); /* max initial buffer fullness */
            avio_wl32(pb, 0); /* max object size */
            avio_wl32(pb, (!asf->is_streamed && (pb->seekable & AVIO_SEEKABLE_NORMAL)) << 1); /* flags - seekable */
            avio_wl16(pb, n + 1); /* stream number */
            avio_wl16(pb, asf->streams[n].stream_language_index); /* language id index */
            avio_wl64(pb, 0); /* avg time per frame */
            avio_wl16(pb, 0); /* stream name count */
            avio_wl16(pb, 0); /* payload extension system count */
            end_header(pb, es_pos);
        }
    }
    if (has_aspect_ratio) {
        int64_t hpos2;
        hpos2 = put_header(pb, &ff_asf_metadata_header);
        avio_wl16(pb, 2 * has_aspect_ratio);
        for (n = 0; n < s->nb_streams; n++) {
            par = s->streams[n]->codecpar;
            if (   par->codec_type == AVMEDIA_TYPE_VIDEO
                && par->sample_aspect_ratio.num > 0
                && par->sample_aspect_ratio.den > 0) {
                AVRational sar = par->sample_aspect_ratio;
                avio_wl16(pb, 0);
                // the stream number is set like this below
                avio_wl16(pb, n + 1);
                avio_wl16(pb, 26); // name_len
                avio_wl16(pb,  3); // value_type
                avio_wl32(pb,  4); // value_len
                avio_put_str16le(pb, "AspectRatioX");
                avio_wl32(pb, sar.num);
                avio_wl16(pb, 0);
                // the stream number is set like this below
                avio_wl16(pb, n + 1);
                avio_wl16(pb, 26); // name_len
                avio_wl16(pb,  3); // value_type
                avio_wl32(pb,  4); // value_len
                avio_put_str16le(pb, "AspectRatioY");
                avio_wl32(pb, sar.den);
            }
        }
        end_header(pb, hpos2);
    }
    {
        int64_t pos1;
        pos1 = avio_tell(pb);
        avio_seek(pb, hpos + 42, SEEK_SET);
        avio_wl32(pb, pos1 - hpos - 46);
        avio_seek(pb, pos1, SEEK_SET);
    }
    end_header(pb, hpos);

    /* title and other info */
    if (has_title) {
        int len, ret;
        uint8_t *buf;
        AVIOContext *dyn_buf;

        if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
            return ret;

        hpos = put_header(pb, &ff_asf_comment_header);

        for (n = 0; n < FF_ARRAY_ELEMS(tags); n++) {
            len = tags[n] ? avio_put_str16le(dyn_buf, tags[n]->value) : 0;
            avio_wl16(pb, len);
        }
        len = avio_close_dyn_buf(dyn_buf, &buf);
        avio_write(pb, buf, len);
        av_freep(&buf);
        end_header(pb, hpos);
    }
    if (metadata_count) {
        AVDictionaryEntry *tag = NULL;
        hpos = put_header(pb, &ff_asf_extended_content_header);
        avio_wl16(pb, metadata_count);
        while ((tag = av_dict_get(s->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            put_str16(pb, tag->key);
            avio_wl16(pb, 0);
            put_str16(pb, tag->value);
        }
        end_header(pb, hpos);
    }
    /* chapters using ASF markers */
    if (!asf->is_streamed && s->nb_chapters) {
        int ret;
        if ((ret = asf_write_markers(s)) < 0)
            return ret;
    }
    /* stream headers */
    for (n = 0; n < s->nb_streams; n++) {
        int64_t es_pos;
        //        ASFStream *stream = &asf->streams[n];

        par                 = s->streams[n]->codecpar;
        asf->streams[n].num = n + 1;
        asf->streams[n].seq = 1;

        switch (par->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            wav_extra_size = 0;
            extra_size     = 18 + wav_extra_size;
            extra_size2    = 8;
            break;
        default:
        case AVMEDIA_TYPE_VIDEO:
            wav_extra_size = par->extradata_size;
            extra_size     = 0x33 + wav_extra_size;
            extra_size2    = 0;
            break;
        }

        hpos = put_header(pb, &ff_asf_stream_header);
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            ff_put_guid(pb, &ff_asf_audio_stream);
            ff_put_guid(pb, &ff_asf_audio_conceal_spread);
        } else {
            ff_put_guid(pb, &ff_asf_video_stream);
            ff_put_guid(pb, &ff_asf_video_conceal_none);
        }
        avio_wl64(pb, 0); /* ??? */
        es_pos = avio_tell(pb);
        avio_wl32(pb, extra_size); /* wav header len */
        avio_wl32(pb, extra_size2); /* additional data len */
        avio_wl16(pb, n + 1); /* stream number */
        avio_wl32(pb, 0); /* ??? */

        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* WAVEFORMATEX header */
            int wavsize = ff_put_wav_header(s, pb, par, FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX);

            if (wavsize < 0)
                return -1;
            if (wavsize != extra_size) {
                cur_pos = avio_tell(pb);
                avio_seek(pb, es_pos, SEEK_SET);
                avio_wl32(pb, wavsize); /* wav header len */
                avio_seek(pb, cur_pos, SEEK_SET);
            }
            /* ERROR Correction */
            avio_w8(pb, 0x01);
            if (par->codec_id == AV_CODEC_ID_ADPCM_G726 || !par->block_align) {
                avio_wl16(pb, 0x0190);
                avio_wl16(pb, 0x0190);
            } else {
                avio_wl16(pb, par->block_align);
                avio_wl16(pb, par->block_align);
            }
            avio_wl16(pb, 0x01);
            avio_w8(pb, 0x00);
        } else {
            avio_wl32(pb, par->width);
            avio_wl32(pb, par->height);
            avio_w8(pb, 2); /* ??? */
            avio_wl16(pb, 40 + par->extradata_size); /* size */

            /* BITMAPINFOHEADER header */
            ff_put_bmp_header(pb, par, 1, 0, 0);
        }
        end_header(pb, hpos);
    }

    /* media comments */

    hpos = put_header(pb, &ff_asf_codec_comment_header);
    ff_put_guid(pb, &ff_asf_codec_comment1_header);
    avio_wl32(pb, s->nb_streams);
    for (n = 0; n < s->nb_streams; n++) {
        const AVCodecDescriptor *codec_desc;
        const char *desc;

        par  = s->streams[n]->codecpar;
        codec_desc = avcodec_descriptor_get(par->codec_id);

        if (par->codec_type == AVMEDIA_TYPE_AUDIO)
            avio_wl16(pb, 2);
        else if (par->codec_type == AVMEDIA_TYPE_VIDEO)
            avio_wl16(pb, 1);
        else
            avio_wl16(pb, -1);

        if (par->codec_id == AV_CODEC_ID_WMAV2)
            desc = "Windows Media Audio V8";
        else
            desc = codec_desc ? codec_desc->name : NULL;

        if (desc) {
            AVIOContext *dyn_buf;
            uint8_t *buf;
            int len, ret;

            if ((ret = avio_open_dyn_buf(&dyn_buf)) < 0)
                return ret;

            avio_put_str16le(dyn_buf, desc);
            len = avio_close_dyn_buf(dyn_buf, &buf);
            avio_wl16(pb, len / 2); // "number of characters" = length in bytes / 2

            avio_write(pb, buf, len);
            av_freep(&buf);
        } else
            avio_wl16(pb, 0);

        avio_wl16(pb, 0); /* no parameters */

        /* id */
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            avio_wl16(pb, 2);
            avio_wl16(pb, par->codec_tag);
        } else {
            avio_wl16(pb, 4);
            avio_wl32(pb, par->codec_tag);
        }
        if (!par->codec_tag)
            return -1;
    }
    end_header(pb, hpos);

    /* patch the header size fields */

    cur_pos     = avio_tell(pb);
    header_size = cur_pos - header_offset;
    if (asf->is_streamed) {
        header_size += 8 + 30 + DATA_HEADER_SIZE;

        avio_seek(pb, header_offset - 10 - 30, SEEK_SET);
        avio_wl16(pb, header_size);
        avio_seek(pb, header_offset - 2 - 30, SEEK_SET);
        avio_wl16(pb, header_size);

        header_size -= 8 + 30 + DATA_HEADER_SIZE;
    }
    header_size += 24 + 6;
    avio_seek(pb, header_offset - 14, SEEK_SET);
    avio_wl64(pb, header_size);
    avio_seek(pb, cur_pos, SEEK_SET);

    /* movie chunk, followed by packets of packet_size */
    asf->data_offset = cur_pos;
    ff_put_guid(pb, &ff_asf_data_header);
    avio_wl64(pb, data_chunk_size);
    ff_put_guid(pb, &ff_asf_my_guid);
    avio_wl64(pb, asf->nb_packets); /* nb packets */
    avio_w8(pb, 1); /* ??? */
    avio_w8(pb, 1); /* ??? */
    return 0;
}

static int asf_write_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    s->packet_size  = asf->packet_size;
    s->max_interleave_delta = 0;
    asf->nb_packets = 0;

    if (s->nb_streams > 127) {
        av_log(s, AV_LOG_ERROR, "ASF can only handle 127 streams\n");
        return AVERROR(EINVAL);
    }

    asf->index_ptr             = av_malloc(sizeof(ASFIndex) * ASF_INDEX_BLOCK);
    if (!asf->index_ptr)
        return AVERROR(ENOMEM);
    asf->nb_index_memory_alloc = ASF_INDEX_BLOCK;
    asf->maximum_packet        = 0;

    /* the data-chunk-size has to be 50 (DATA_HEADER_SIZE), which is
     * data_size - asf->data_offset at the moment this function is done.
     * It is needed to use asf as a streamable format. */
    if (asf_write_header1(s, 0, DATA_HEADER_SIZE) < 0) {
        //av_free(asf);
        av_freep(&asf->index_ptr);
        return -1;
    }

    asf->packet_nb_payloads     = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end   = -1;
    ffio_init_context(&asf->pb, asf->packet_buf, s->packet_size, 1,
                      NULL, NULL, NULL, NULL);

    if (s->avoid_negative_ts < 0)
        s->avoid_negative_ts = 1;

    return 0;
}

static int asf_write_stream_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    asf->is_streamed = 1;

    return asf_write_header(s);
}

static int put_payload_parsing_info(AVFormatContext *s,
                                    unsigned sendtime, unsigned duration,
                                    int nb_payloads, int padsize)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    int ppi_size, i;
    int64_t start = avio_tell(pb);

    int iLengthTypeFlags = ASF_PPI_LENGTH_TYPE_FLAGS;

    padsize -= PACKET_HEADER_MIN_SIZE;
    if (asf->multi_payloads_present)
        padsize--;
    av_assert0(padsize >= 0);

    avio_w8(pb, ASF_PACKET_ERROR_CORRECTION_FLAGS);
    for (i = 0; i < ASF_PACKET_ERROR_CORRECTION_DATA_SIZE; i++)
        avio_w8(pb, 0x0);

    if (asf->multi_payloads_present)
        iLengthTypeFlags |= ASF_PPI_FLAG_MULTIPLE_PAYLOADS_PRESENT;

    if (padsize > 0) {
        if (padsize < 256)
            iLengthTypeFlags |= ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_BYTE;
        else
            iLengthTypeFlags |= ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_WORD;
    }
    avio_w8(pb, iLengthTypeFlags);

    avio_w8(pb, ASF_PPI_PROPERTY_FLAGS);

    if (iLengthTypeFlags & ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_WORD)
        avio_wl16(pb, padsize - 2);
    if (iLengthTypeFlags & ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_BYTE)
        avio_w8(pb, padsize - 1);

    avio_wl32(pb, sendtime);
    avio_wl16(pb, duration);
    if (asf->multi_payloads_present)
        avio_w8(pb, nb_payloads | ASF_PAYLOAD_FLAGS);

    ppi_size = avio_tell(pb) - start;

    return ppi_size;
}

static void flush_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int packet_hdr_size, packet_filled_size;

    av_assert0(asf->packet_timestamp_end >= asf->packet_timestamp_start);

    if (asf->is_streamed)
        put_chunk(s, 0x4424, s->packet_size, 0);

    packet_hdr_size = put_payload_parsing_info(s,
                                               asf->packet_timestamp_start,
                                               asf->packet_timestamp_end - asf->packet_timestamp_start,
                                               asf->packet_nb_payloads,
                                               asf->packet_size_left);

    packet_filled_size = asf->packet_size - asf->packet_size_left;
    av_assert0(packet_hdr_size <= asf->packet_size_left);
    memset(asf->packet_buf + packet_filled_size, 0, asf->packet_size_left);

    avio_write(s->pb, asf->packet_buf, s->packet_size - packet_hdr_size);

    avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);

    asf->nb_packets++;
    asf->packet_nb_payloads     = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end   = -1;
    ffio_init_context(&asf->pb, asf->packet_buf, s->packet_size, 1,
                      NULL, NULL, NULL, NULL);
}

static void put_payload_header(AVFormatContext *s, ASFStream *stream,
                               int64_t presentation_time, int m_obj_size,
                               int m_obj_offset, int payload_len, int flags)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = &asf->pb;
    int val;

    val = stream->num;
    if (flags & AV_PKT_FLAG_KEY)
        val |= ASF_PL_FLAG_KEY_FRAME;
    avio_w8(pb, val);

    avio_w8(pb, stream->seq);     // Media object number
    avio_wl32(pb, m_obj_offset);  // Offset Into Media Object

    // Replicated Data shall be at least 8 bytes long.
    // The first 4 bytes of data shall contain the
    // Size of the Media Object that the payload belongs to.
    // The next 4 bytes of data shall contain the
    // Presentation Time for the media object that the payload belongs to.
    avio_w8(pb, ASF_PAYLOAD_REPLICATED_DATA_LENGTH);

    avio_wl32(pb, m_obj_size);        // Replicated Data - Media Object Size
    avio_wl32(pb, (uint32_t) presentation_time); // Replicated Data - Presentation Time

    if (asf->multi_payloads_present) {
        avio_wl16(pb, payload_len);   // payload length
    }
}

static void put_frame(AVFormatContext *s, ASFStream *stream, AVStream *avst,
                      int64_t timestamp, const uint8_t *buf,
                      int m_obj_size, int flags)
{
    ASFContext *asf = s->priv_data;
    int m_obj_offset, payload_len, frag_len1;

    m_obj_offset = 0;
    while (m_obj_offset < m_obj_size) {
        payload_len = m_obj_size - m_obj_offset;
        if (asf->packet_timestamp_start == -1) {
            const int multi_payload_constant = (asf->packet_size - MULTI_PAYLOAD_HEADERS);
            asf->multi_payloads_present = (payload_len < multi_payload_constant);

            asf->packet_size_left = asf->packet_size;
            if (asf->multi_payloads_present) {
                frag_len1 = multi_payload_constant - 1;
            } else {
                frag_len1 = asf->packet_size - SINGLE_PAYLOAD_HEADERS;
            }
            asf->packet_timestamp_start = timestamp;
        } else {
            // multi payloads
            frag_len1 = asf->packet_size_left -
                        PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS -
                        PACKET_HEADER_MIN_SIZE - 1;

            if (frag_len1 < payload_len &&
                avst->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                flush_packet(s);
                continue;
            }
            if (asf->packet_timestamp_start > INT64_MAX - UINT16_MAX ||
                timestamp > asf->packet_timestamp_start + UINT16_MAX) {
                flush_packet(s);
                continue;
            }
        }
        if (frag_len1 > 0) {
            if (payload_len > frag_len1)
                payload_len = frag_len1;
            else if (payload_len == (frag_len1 - 1))
                payload_len = frag_len1 - 2;  // additional byte need to put padding length

            put_payload_header(s, stream, timestamp + PREROLL_TIME,
                               m_obj_size, m_obj_offset, payload_len, flags);
            avio_write(&asf->pb, buf, payload_len);

            if (asf->multi_payloads_present)
                asf->packet_size_left -= (payload_len + PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS);
            else
                asf->packet_size_left -= (payload_len + PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD);
            asf->packet_timestamp_end = timestamp;

            asf->packet_nb_payloads++;
        } else {
            payload_len = 0;
        }
        m_obj_offset += payload_len;
        buf          += payload_len;

        if (!asf->multi_payloads_present)
            flush_packet(s);
        else if (asf->packet_size_left <= (PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS + PACKET_HEADER_MIN_SIZE + 1))
            flush_packet(s);
        else if (asf->packet_nb_payloads == ASF_PAYLOADS_PER_PACKET)
            flush_packet(s);
    }
    stream->seq++;
}

static int update_index(AVFormatContext *s, int start_sec,
                         uint32_t packet_number, uint16_t packet_count,
                         uint64_t packet_offset)
{
    ASFContext *asf = s->priv_data;

    if (start_sec > asf->next_start_sec) {
        int i;

        if (!asf->next_start_sec) {
            asf->next_packet_number = packet_number;
            asf->next_packet_count  = packet_count;
            asf->next_packet_offset = packet_offset;
        }

        if (start_sec > asf->nb_index_memory_alloc) {
            int err;
            asf->nb_index_memory_alloc = (start_sec + ASF_INDEX_BLOCK) & ~(ASF_INDEX_BLOCK - 1);
            if ((err = av_reallocp_array(&asf->index_ptr,
                                         asf->nb_index_memory_alloc,
                                         sizeof(*asf->index_ptr))) < 0) {
                asf->nb_index_memory_alloc = 0;
                return err;
            }
        }
        for (i = asf->next_start_sec; i < start_sec; i++) {
            asf->index_ptr[i].packet_number = asf->next_packet_number;
            asf->index_ptr[i].packet_count  = asf->next_packet_count;
            asf->index_ptr[i].send_time     = asf->next_start_sec * INT64_C(10000000);
            asf->index_ptr[i].offset        = asf->next_packet_offset;

        }
    }
    asf->maximum_packet     = FFMAX(asf->maximum_packet, packet_count);
    asf->next_packet_number = packet_number;
    asf->next_packet_count  = packet_count;
    asf->next_packet_offset = packet_offset;
    asf->next_start_sec     = start_sec;

    return 0;
}

static int asf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    ASFStream *stream;
    AVCodecParameters *par;
    uint32_t packet_number;
    int64_t pts;
    int start_sec;
    int flags = pkt->flags;
    int ret;
    uint64_t offset = avio_tell(pb);

    par  = s->streams[pkt->stream_index]->codecpar;
    stream = &asf->streams[pkt->stream_index];

    if (par->codec_type == AVMEDIA_TYPE_AUDIO)
        flags &= ~AV_PKT_FLAG_KEY;

    pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
    av_assert0(pts != AV_NOPTS_VALUE);
    if (   pts < - PREROLL_TIME
        || pts > (INT_MAX-3)/10000LL * ASF_INDEXED_INTERVAL - PREROLL_TIME) {
        av_log(s, AV_LOG_ERROR, "input pts %"PRId64" is invalid\n", pts);
        return AVERROR(EINVAL);
    }
    pts *= 10000;
    asf->duration = FFMAX(asf->duration, pts + pkt->duration * 10000);

    packet_number = asf->nb_packets;
    put_frame(s, stream, s->streams[pkt->stream_index],
              pkt->dts, pkt->data, pkt->size, flags);

    start_sec = (int)((PREROLL_TIME * 10000 + pts + ASF_INDEXED_INTERVAL - 1)
              / ASF_INDEXED_INTERVAL);

    /* check index */
    if ((!asf->is_streamed) && (flags & AV_PKT_FLAG_KEY)) {
        uint16_t packet_count = asf->nb_packets - packet_number;
        ret = update_index(s, start_sec, packet_number, packet_count, offset);
        if (ret < 0)
            return ret;
    }
    asf->end_sec = start_sec;

    return 0;
}

static int asf_write_index(AVFormatContext *s, const ASFIndex *index,
                           uint16_t max, uint32_t count)
{
    AVIOContext *pb = s->pb;
    int i;

    ff_put_guid(pb, &ff_asf_simple_index_header);
    avio_wl64(pb, 24 + 16 + 8 + 4 + 4 + (4 + 2) * count);
    ff_put_guid(pb, &ff_asf_my_guid);
    avio_wl64(pb, ASF_INDEXED_INTERVAL);
    avio_wl32(pb, max);
    avio_wl32(pb, count);
    for (i = 0; i < count; i++) {
        avio_wl32(pb, index[i].packet_number);
        avio_wl16(pb, index[i].packet_count);
    }

    return 0;
}

static int asf_write_trailer(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int64_t file_size, data_size;
    int ret;

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    /* write index */
    data_size = avio_tell(s->pb);
    if (!asf->is_streamed && asf->next_start_sec) {
        if ((ret = update_index(s, asf->end_sec + 1, 0, 0, 0)) < 0)
            return ret;
        asf_write_index(s, asf->index_ptr, asf->maximum_packet, asf->next_start_sec);
    }

    if (asf->is_streamed || !(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        put_chunk(s, 0x4524, 0, 0); /* end of stream */
    } else {
        /* rewrite an updated header */
        file_size = avio_tell(s->pb);
        avio_seek(s->pb, 0, SEEK_SET);
        asf_write_header1(s, file_size, data_size - asf->data_offset);
    }

    av_freep(&asf->index_ptr);
    return 0;
}

static const AVOption asf_options[] = {
    { "packet_size", "Packet size", offsetof(ASFContext, packet_size), AV_OPT_TYPE_INT, {.i64 = 3200}, PACKET_SIZE_MIN, PACKET_SIZE_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL },
};

#if CONFIG_ASF_MUXER
static const AVClass asf_muxer_class = {
    .class_name     = "ASF muxer",
    .item_name      = av_default_item_name,
    .option         = asf_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_asf_muxer = {
    .name           = "asf",
    .long_name      = NULL_IF_CONFIG_SMALL("ASF (Advanced / Active Streaming Format)"),
    .mime_type      = "video/x-ms-asf",
    .extensions     = "asf,wmv,wma",
    .priv_data_size = sizeof(ASFContext),
    .audio_codec    = AV_CODEC_ID_WMAV2,
    .video_codec    = AV_CODEC_ID_MSMPEG4V3,
    .write_header   = asf_write_header,
    .write_packet   = asf_write_packet,
    .write_trailer  = asf_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
    .codec_tag      = asf_codec_tags,
    .priv_class        = &asf_muxer_class,
};
#endif /* CONFIG_ASF_MUXER */

#if CONFIG_ASF_STREAM_MUXER
static const AVClass asf_stream_muxer_class = {
    .class_name     = "ASF stream muxer",
    .item_name      = av_default_item_name,
    .option         = asf_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_asf_stream_muxer = {
    .name           = "asf_stream",
    .long_name      = NULL_IF_CONFIG_SMALL("ASF (Advanced / Active Streaming Format)"),
    .mime_type      = "video/x-ms-asf",
    .extensions     = "asf,wmv,wma",
    .priv_data_size = sizeof(ASFContext),
    .audio_codec    = AV_CODEC_ID_WMAV2,
    .video_codec    = AV_CODEC_ID_MSMPEG4V3,
    .write_header   = asf_write_stream_header,
    .write_packet   = asf_write_packet,
    .write_trailer  = asf_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
    .codec_tag      = asf_codec_tags,
    .priv_class        = &asf_stream_muxer_class,
};
#endif /* CONFIG_ASF_STREAM_MUXER */
