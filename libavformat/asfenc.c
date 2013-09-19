/*
 * ASF muxer
 * Copyright (c) 2000, 2001 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "riff.h"
#include "asf.h"

#undef NDEBUG
#include <assert.h>


#define ASF_INDEXED_INTERVAL    10000000
#define ASF_INDEX_BLOCK         600

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

#define SINGLE_PAYLOAD_DATA_LENGTH                        \
    (PACKET_SIZE -                                        \
     PACKET_HEADER_MIN_SIZE -                             \
     PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD)

#define MULTI_PAYLOAD_CONSTANT                            \
    (PACKET_SIZE -                                        \
     PACKET_HEADER_MIN_SIZE -                             \
     1 -         /* Payload Flags */                      \
     2 * PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS)

#define DATA_HEADER_SIZE 50

typedef struct {
    uint32_t seqno;
    int is_streamed;
    ASFStream streams[128];              ///< it's max number and it's not that big
    /* non streamed additonnal info */
    uint64_t nb_packets;                 ///< how many packets are there in the file, invalid if broadcasting
    int64_t duration;                    ///< in 100ns units
    /* packet filling */
    unsigned char multi_payloads_present;
    int packet_size_left;
    int packet_timestamp_start;
    int packet_timestamp_end;
    unsigned int packet_nb_payloads;
    uint8_t packet_buf[PACKET_SIZE];
    AVIOContext pb;
    /* only for reading */
    uint64_t data_offset;                ///< beginning of the first data packet

    int64_t last_indexed_pts;
    ASFIndex *index_ptr;
    uint32_t nb_index_count;
    uint32_t nb_index_memory_alloc;
    uint16_t maximum_packet;
} ASFContext;

static const AVCodecTag codec_asf_bmp_tags[] = {
    { AV_CODEC_ID_MPEG4,     MKTAG('M', 'P', '4', 'S') },
    { AV_CODEC_ID_MPEG4,     MKTAG('M', '4', 'S', '2') },
    { AV_CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') },
    { AV_CODEC_ID_NONE,      0 },
};

#define PREROLL_TIME 3100

static void put_guid(AVIOContext *s, const ff_asf_guid *g)
{
    assert(sizeof(*g) == 16);
    avio_write(s, *g, sizeof(*g));
}

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
    put_guid(pb, g);
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

/* convert from unix to windows time */
static int64_t unix_to_file_time(int ti)
{
    int64_t t;

    t  = ti * INT64_C(10000000);
    t += INT64_C(116444736000000000);
    return t;
}

static int32_t get_send_time(ASFContext *asf, int64_t pres_time, uint64_t *offset)
{
    int i;
    int32_t send_time = 0;
    *offset = asf->data_offset + DATA_HEADER_SIZE;
    for (i = 0; i < asf->nb_index_count; i++) {
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

    put_guid(pb, &ff_asf_reserved_4);  // ASF spec mandates this reserved value
    avio_wl32(pb, s->nb_chapters);     // markers count
    avio_wl16(pb, 0);                  // ASF spec mandates 0 for this
    avio_wl16(pb, 0);                  // name length 0, no name given

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *c = s->chapters[i];
        AVDictionaryEntry *t = av_dict_get(c->metadata, "title", NULL, 0);
        int64_t pres_time = av_rescale_q(c->start, c->time_base, scale);
        uint64_t offset;
        int32_t send_time = get_send_time(asf, pres_time, &offset);
        int len = 0;
        uint8_t *buf;
        AVIOContext *dyn_buf;
        if (t) {
            if (avio_open_dyn_buf(&dyn_buf) < 0)
                return AVERROR(ENOMEM);
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
    int header_size, n, extra_size, extra_size2, wav_extra_size, file_time;
    int has_title;
    int metadata_count;
    AVCodecContext *enc;
    int64_t header_offset, cur_pos, hpos;
    int bit_rate;
    int64_t duration;

    ff_metadata_conv(&s->metadata, ff_asf_metadata_conv, NULL);

    tags[0] = av_dict_get(s->metadata, "title", NULL, 0);
    tags[1] = av_dict_get(s->metadata, "author", NULL, 0);
    tags[2] = av_dict_get(s->metadata, "copyright", NULL, 0);
    tags[3] = av_dict_get(s->metadata, "comment", NULL, 0);
    tags[4] = av_dict_get(s->metadata, "rating", NULL, 0);

    duration       = asf->duration + PREROLL_TIME * 10000;
    has_title      = tags[0] || tags[1] || tags[2] || tags[3] || tags[4];
    metadata_count = av_dict_count(s->metadata);

    bit_rate = 0;
    for (n = 0; n < s->nb_streams; n++) {
        enc = s->streams[n]->codec;

        avpriv_set_pts_info(s->streams[n], 32, 1, 1000); /* 32 bit pts in ms */

        bit_rate += enc->bit_rate;
    }

    if (asf->is_streamed) {
        put_chunk(s, 0x4824, 0, 0xc00); /* start of stream (length will be patched later) */
    }

    put_guid(pb, &ff_asf_header);
    avio_wl64(pb, -1); /* header length, will be patched after */
    avio_wl32(pb, 3 + has_title + !!metadata_count + s->nb_streams); /* number of chunks in header */
    avio_w8(pb, 1); /* ??? */
    avio_w8(pb, 2); /* ??? */

    /* file header */
    header_offset = avio_tell(pb);
    hpos          = put_header(pb, &ff_asf_file_header);
    put_guid(pb, &ff_asf_my_guid);
    avio_wl64(pb, file_size);
    file_time = 0;
    avio_wl64(pb, unix_to_file_time(file_time));
    avio_wl64(pb, asf->nb_packets); /* number of packets */
    avio_wl64(pb, duration); /* end time stamp (in 100ns units) */
    avio_wl64(pb, asf->duration); /* duration (in 100ns units) */
    avio_wl64(pb, PREROLL_TIME); /* start time stamp */
    avio_wl32(pb, (asf->is_streamed || !pb->seekable) ? 3 : 2);  /* ??? */
    avio_wl32(pb, s->packet_size); /* packet size */
    avio_wl32(pb, s->packet_size); /* packet size */
    avio_wl32(pb, bit_rate); /* Nominal data rate in bps */
    end_header(pb, hpos);

    /* unknown headers */
    hpos = put_header(pb, &ff_asf_head1_guid);
    put_guid(pb, &ff_asf_head2_guid);
    avio_wl32(pb, 6);
    avio_wl16(pb, 0);
    end_header(pb, hpos);

    /* title and other infos */
    if (has_title) {
        int len;
        uint8_t *buf;
        AVIOContext *dyn_buf;

        if (avio_open_dyn_buf(&dyn_buf) < 0)
            return AVERROR(ENOMEM);

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
        if (ret = asf_write_markers(s))
            return ret;
    }
    /* stream headers */
    for (n = 0; n < s->nb_streams; n++) {
        int64_t es_pos;
        //        ASFStream *stream = &asf->streams[n];

        enc                 = s->streams[n]->codec;
        asf->streams[n].num = n + 1;
        asf->streams[n].seq = 0;

        switch (enc->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            wav_extra_size = 0;
            extra_size     = 18 + wav_extra_size;
            extra_size2    = 8;
            break;
        default:
        case AVMEDIA_TYPE_VIDEO:
            wav_extra_size = enc->extradata_size;
            extra_size     = 0x33 + wav_extra_size;
            extra_size2    = 0;
            break;
        }

        hpos = put_header(pb, &ff_asf_stream_header);
        if (enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            put_guid(pb, &ff_asf_audio_stream);
            put_guid(pb, &ff_asf_audio_conceal_spread);
        } else {
            put_guid(pb, &ff_asf_video_stream);
            put_guid(pb, &ff_asf_video_conceal_none);
        }
        avio_wl64(pb, 0); /* ??? */
        es_pos = avio_tell(pb);
        avio_wl32(pb, extra_size); /* wav header len */
        avio_wl32(pb, extra_size2); /* additional data len */
        avio_wl16(pb, n + 1); /* stream number */
        avio_wl32(pb, 0); /* ??? */

        if (enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            /* WAVEFORMATEX header */
            int wavsize = ff_put_wav_header(pb, enc);

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
            if (enc->codec_id == AV_CODEC_ID_ADPCM_G726 || !enc->block_align) {
                avio_wl16(pb, 0x0190);
                avio_wl16(pb, 0x0190);
            } else {
                avio_wl16(pb, enc->block_align);
                avio_wl16(pb, enc->block_align);
            }
            avio_wl16(pb, 0x01);
            avio_w8(pb, 0x00);
        } else {
            avio_wl32(pb, enc->width);
            avio_wl32(pb, enc->height);
            avio_w8(pb, 2); /* ??? */
            avio_wl16(pb, 40 + enc->extradata_size); /* size */

            /* BITMAPINFOHEADER header */
            ff_put_bmp_header(pb, enc, ff_codec_bmp_tags, 1);
        }
        end_header(pb, hpos);
    }

    /* media comments */

    hpos = put_header(pb, &ff_asf_codec_comment_header);
    put_guid(pb, &ff_asf_codec_comment1_header);
    avio_wl32(pb, s->nb_streams);
    for (n = 0; n < s->nb_streams; n++) {
        AVCodec *p;
        const char *desc;
        int len;
        uint8_t *buf;
        AVIOContext *dyn_buf;

        enc = s->streams[n]->codec;
        p   = avcodec_find_encoder(enc->codec_id);

        if (enc->codec_type == AVMEDIA_TYPE_AUDIO)
            avio_wl16(pb, 2);
        else if (enc->codec_type == AVMEDIA_TYPE_VIDEO)
            avio_wl16(pb, 1);
        else
            avio_wl16(pb, -1);

        if (enc->codec_id == AV_CODEC_ID_WMAV2)
            desc = "Windows Media Audio V8";
        else
            desc = p ? p->name : enc->codec_name;

        if (avio_open_dyn_buf(&dyn_buf) < 0)
            return AVERROR(ENOMEM);

        avio_put_str16le(dyn_buf, desc);
        len = avio_close_dyn_buf(dyn_buf, &buf);
        avio_wl16(pb, len / 2); // "number of characters" = length in bytes / 2

        avio_write(pb, buf, len);
        av_freep(&buf);

        avio_wl16(pb, 0); /* no parameters */

        /* id */
        if (enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            avio_wl16(pb, 2);
            avio_wl16(pb, enc->codec_tag);
        } else {
            avio_wl16(pb, 4);
            avio_wl32(pb, enc->codec_tag);
        }
        if (!enc->codec_tag)
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
    put_guid(pb, &ff_asf_data_header);
    avio_wl64(pb, data_chunk_size);
    put_guid(pb, &ff_asf_my_guid);
    avio_wl64(pb, asf->nb_packets); /* nb packets */
    avio_w8(pb, 1); /* ??? */
    avio_w8(pb, 1); /* ??? */
    return 0;
}

static int asf_write_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    s->packet_size  = PACKET_SIZE;
    asf->nb_packets = 0;

    asf->last_indexed_pts      = 0;
    asf->index_ptr             = av_malloc(sizeof(ASFIndex) * ASF_INDEX_BLOCK);
    asf->nb_index_memory_alloc = ASF_INDEX_BLOCK;
    asf->nb_index_count        = 0;
    asf->maximum_packet        = 0;

    /* the data-chunk-size has to be 50 (DATA_HEADER_SIZE), which is
     * data_size - asf->data_offset at the moment this function is done.
     * It is needed to use asf as a streamable format. */
    if (asf_write_header1(s, 0, DATA_HEADER_SIZE) < 0) {
        //av_free(asf);
        return -1;
    }

    avio_flush(s->pb);

    asf->packet_nb_payloads     = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end   = -1;
    ffio_init_context(&asf->pb, asf->packet_buf, s->packet_size, 1,
                      NULL, NULL, NULL, NULL);

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
    assert(padsize >= 0);

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

    assert(asf->packet_timestamp_end >= asf->packet_timestamp_start);

    if (asf->is_streamed)
        put_chunk(s, 0x4424, s->packet_size, 0);

    packet_hdr_size = put_payload_parsing_info(s,
                                               asf->packet_timestamp_start,
                                               asf->packet_timestamp_end -
                                               asf->packet_timestamp_start,
                                               asf->packet_nb_payloads,
                                               asf->packet_size_left);

    packet_filled_size = PACKET_SIZE - asf->packet_size_left;
    assert(packet_hdr_size <= asf->packet_size_left);
    memset(asf->packet_buf + packet_filled_size, 0, asf->packet_size_left);

    avio_write(s->pb, asf->packet_buf, s->packet_size - packet_hdr_size);

    avio_flush(s->pb);
    asf->nb_packets++;
    asf->packet_nb_payloads     = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end   = -1;
    ffio_init_context(&asf->pb, asf->packet_buf, s->packet_size, 1,
                      NULL, NULL, NULL, NULL);
}

static void put_payload_header(AVFormatContext *s, ASFStream *stream,
                               int presentation_time, int m_obj_size,
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
    avio_wl32(pb, presentation_time); // Replicated Data - Presentation Time

    if (asf->multi_payloads_present) {
        avio_wl16(pb, payload_len);   // payload length
    }
}

static void put_frame(AVFormatContext *s, ASFStream *stream, AVStream *avst,
                      int timestamp, const uint8_t *buf,
                      int m_obj_size, int flags)
{
    ASFContext *asf = s->priv_data;
    int m_obj_offset, payload_len, frag_len1;

    m_obj_offset = 0;
    while (m_obj_offset < m_obj_size) {
        payload_len = m_obj_size - m_obj_offset;
        if (asf->packet_timestamp_start == -1) {
            asf->multi_payloads_present = (payload_len < MULTI_PAYLOAD_CONSTANT);

            asf->packet_size_left = PACKET_SIZE;
            if (asf->multi_payloads_present) {
                frag_len1 = MULTI_PAYLOAD_CONSTANT - 1;
            } else {
                frag_len1 = SINGLE_PAYLOAD_DATA_LENGTH;
            }
            asf->packet_timestamp_start = timestamp;
        } else {
            // multi payloads
            frag_len1 = asf->packet_size_left -
                        PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS -
                        PACKET_HEADER_MIN_SIZE - 1;

            if (frag_len1 < payload_len &&
                avst->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
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
    }
    stream->seq++;
}

static int asf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ASFContext *asf = s->priv_data;
    AVIOContext *pb = s->pb;
    ASFStream *stream;
    int64_t duration;
    AVCodecContext *codec;
    int64_t packet_st, pts;
    int start_sec, i;
    int flags = pkt->flags;
    uint64_t offset = avio_tell(pb);

    codec  = s->streams[pkt->stream_index]->codec;
    stream = &asf->streams[pkt->stream_index];

    if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
        flags &= ~AV_PKT_FLAG_KEY;

    pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
    assert(pts != AV_NOPTS_VALUE);
    duration      = pts * 10000;
    asf->duration = FFMAX(asf->duration, duration + pkt->duration * 10000);

    packet_st = asf->nb_packets;
    put_frame(s, stream, s->streams[pkt->stream_index],
              pkt->dts, pkt->data, pkt->size, flags);

    /* check index */
    if ((!asf->is_streamed) && (flags & AV_PKT_FLAG_KEY)) {
        start_sec = (int)(duration / INT64_C(10000000));
        if (start_sec != (int)(asf->last_indexed_pts / INT64_C(10000000))) {
            for (i = asf->nb_index_count; i < start_sec; i++) {
                if (i >= asf->nb_index_memory_alloc) {
                    int err;
                    asf->nb_index_memory_alloc += ASF_INDEX_BLOCK;
                    if ((err = av_reallocp_array(&asf->index_ptr,
                                                asf->nb_index_memory_alloc,
                                                sizeof(*asf->index_ptr))) < 0) {
                       asf->nb_index_memory_alloc = 0;
                       return err;
                   }
                }
                // store
                asf->index_ptr[i].packet_number = (uint32_t)packet_st;
                asf->index_ptr[i].packet_count  = (uint16_t)(asf->nb_packets - packet_st);
                asf->index_ptr[i].send_time     = start_sec * INT64_C(10000000);
                asf->index_ptr[i].offset        = offset;
                asf->maximum_packet             = FFMAX(asf->maximum_packet,
                                                        (uint16_t)(asf->nb_packets - packet_st));
            }
            asf->nb_index_count   = start_sec;
            asf->last_indexed_pts = duration;
        }
    }
    return 0;
}

static int asf_write_index(AVFormatContext *s, ASFIndex *index,
                           uint16_t max, uint32_t count)
{
    AVIOContext *pb = s->pb;
    int i;

    put_guid(pb, &ff_asf_simple_index_header);
    avio_wl64(pb, 24 + 16 + 8 + 4 + 4 + (4 + 2) * count);
    put_guid(pb, &ff_asf_my_guid);
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

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    /* write index */
    data_size = avio_tell(s->pb);
    if ((!asf->is_streamed) && (asf->nb_index_count != 0))
        asf_write_index(s, asf->index_ptr, asf->maximum_packet, asf->nb_index_count);
    avio_flush(s->pb);

    if (asf->is_streamed || !s->pb->seekable) {
        put_chunk(s, 0x4524, 0, 0); /* end of stream */
    } else {
        /* rewrite an updated header */
        file_size = avio_tell(s->pb);
        avio_seek(s->pb, 0, SEEK_SET);
        asf_write_header1(s, file_size, data_size - asf->data_offset);
    }

    av_free(asf->index_ptr);
    return 0;
}

#if CONFIG_ASF_MUXER
AVOutputFormat ff_asf_muxer = {
    .name           = "asf",
    .long_name      = NULL_IF_CONFIG_SMALL("ASF (Advanced / Active Streaming Format)"),
    .mime_type      = "video/x-ms-asf",
    .extensions     = "asf,wmv,wma",
    .priv_data_size = sizeof(ASFContext),
    .audio_codec    = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_MP2,
    .video_codec    = AV_CODEC_ID_MSMPEG4V3,
    .write_header   = asf_write_header,
    .write_packet   = asf_write_packet,
    .write_trailer  = asf_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
    .codec_tag      = (const AVCodecTag * const []) {
        codec_asf_bmp_tags, ff_codec_bmp_tags, ff_codec_wav_tags, 0
    },
};
#endif /* CONFIG_ASF_MUXER */

#if CONFIG_ASF_STREAM_MUXER
AVOutputFormat ff_asf_stream_muxer = {
    .name           = "asf_stream",
    .long_name      = NULL_IF_CONFIG_SMALL("ASF (Advanced / Active Streaming Format)"),
    .mime_type      = "video/x-ms-asf",
    .extensions     = "asf,wmv,wma",
    .priv_data_size = sizeof(ASFContext),
    .audio_codec    = CONFIG_LIBMP3LAME ? AV_CODEC_ID_MP3 : AV_CODEC_ID_MP2,
    .video_codec    = AV_CODEC_ID_MSMPEG4V3,
    .write_header   = asf_write_stream_header,
    .write_packet   = asf_write_packet,
    .write_trailer  = asf_write_trailer,
    .flags          = AVFMT_GLOBALHEADER,
    .codec_tag      = (const AVCodecTag * const []) {
        codec_asf_bmp_tags, ff_codec_bmp_tags, ff_codec_wav_tags, 0
    },
};
#endif /* CONFIG_ASF_STREAM_MUXER */
