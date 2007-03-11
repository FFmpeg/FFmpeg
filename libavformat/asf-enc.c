/*
 * Adaptive stream format muxer
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "riff.h"
#include "asf.h"

#undef NDEBUG
#include <assert.h>


#define ASF_INDEXED_INTERVAL    10000000
#define ASF_INDEX_BLOCK         600

#define ASF_PACKET_ERROR_CORRECTION_DATA_SIZE 0x2
#define ASF_PACKET_ERROR_CORRECTION_FLAGS (\
                ASF_PACKET_FLAG_ERROR_CORRECTION_PRESENT | \
                ASF_PACKET_ERROR_CORRECTION_DATA_SIZE\
                )

#if (ASF_PACKET_ERROR_CORRECTION_FLAGS != 0)
#   define ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE 1
#else
#   define ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE 0
#endif

#define ASF_PPI_PROPERTY_FLAGS (\
                ASF_PL_FLAG_REPLICATED_DATA_LENGTH_FIELD_IS_BYTE | \
                ASF_PL_FLAG_OFFSET_INTO_MEDIA_OBJECT_LENGTH_FIELD_IS_DWORD | \
                ASF_PL_FLAG_MEDIA_OBJECT_NUMBER_LENGTH_FIELD_IS_BYTE | \
                ASF_PL_FLAG_STREAM_NUMBER_LENGTH_FIELD_IS_BYTE \
                )

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

#define PACKET_HEADER_MIN_SIZE (\
                ASF_PACKET_ERROR_CORRECTION_FLAGS_FIELD_SIZE + \
                ASF_PACKET_ERROR_CORRECTION_DATA_SIZE + \
                1 + /*Length Type Flags*/ \
                1 + /*Property Flags*/ \
                ASF_PPI_PACKET_LENGTH_FIELD_SIZE + \
                ASF_PPI_SEQUENCE_FIELD_SIZE + \
                ASF_PPI_PADDING_LENGTH_FIELD_SIZE + \
                4 + /*Send Time Field*/ \
                2   /*Duration Field*/ \
                )


// Replicated Data shall be at least 8 bytes long.
#define ASF_PAYLOAD_REPLICATED_DATA_LENGTH 0x08

#define PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD (\
                1 + /*Stream Number*/ \
                ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE + \
                ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE + \
                ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE + \
                ASF_PAYLOAD_REPLICATED_DATA_LENGTH \
                )

#define PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS (\
                1 + /*Stream Number*/ \
                ASF_PAYLOAD_MEDIA_OBJECT_NUMBER_FIELD_SIZE + \
                ASF_PAYLOAD_OFFSET_INTO_MEDIA_OBJECT_FIELD_SIZE + \
                ASF_PAYLOAD_REPLICATED_DATA_LENGTH_FIELD_SIZE + \
                ASF_PAYLOAD_REPLICATED_DATA_LENGTH + \
                ASF_PAYLOAD_LENGTH_FIELD_SIZE \
                )

#define SINGLE_PAYLOAD_DATA_LENGTH (\
                PACKET_SIZE - \
                PACKET_HEADER_MIN_SIZE - \
                PAYLOAD_HEADER_SIZE_SINGLE_PAYLOAD \
                )

#define MULTI_PAYLOAD_CONSTANT (\
                PACKET_SIZE - \
                PACKET_HEADER_MIN_SIZE - \
                1 - /*Payload Flags*/ \
                2*PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS \
                )

static const AVCodecTag codec_asf_bmp_tags[] = {
    { CODEC_ID_MPEG4, MKTAG('M', 'P', '4', 'S') },
    { CODEC_ID_MPEG4, MKTAG('M', '4', 'S', '2') },
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') },
    { CODEC_ID_NONE, 0 },
};

#define PREROLL_TIME 3100

static void put_guid(ByteIOContext *s, const GUID *g)
{
    assert(sizeof(*g) == 16);
    put_buffer(s, g, sizeof(*g));
}

static void put_str16_nolen(ByteIOContext *s, const char *tag);
static void put_str16(ByteIOContext *s, const char *tag)
{
    put_le16(s,strlen(tag) + 1);
    put_str16_nolen(s, tag);
}

static void put_str16_nolen(ByteIOContext *s, const char *tag)
{
    int c;

    do{
        c = (uint8_t)*tag++;
        put_le16(s, c);
    }while(c);
}

static int64_t put_header(ByteIOContext *pb, const GUID *g)
{
    int64_t pos;

    pos = url_ftell(pb);
    put_guid(pb, g);
    put_le64(pb, 24);
    return pos;
}

/* update header size */
static void end_header(ByteIOContext *pb, int64_t pos)
{
    int64_t pos1;

    pos1 = url_ftell(pb);
    url_fseek(pb, pos + 16, SEEK_SET);
    put_le64(pb, pos1 - pos);
    url_fseek(pb, pos1, SEEK_SET);
}

/* write an asf chunk (only used in streaming case) */
static void put_chunk(AVFormatContext *s, int type, int payload_length, int flags)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int length;

    length = payload_length + 8;
    put_le16(pb, type);
    put_le16(pb, length);    //size
    put_le32(pb, asf->seqno);//sequence number
    put_le16(pb, flags); /* unknown bytes */
    put_le16(pb, length);    //size_confirm
    asf->seqno++;
}

/* convert from unix to windows time */
static int64_t unix_to_file_time(int ti)
{
    int64_t t;

    t = ti * INT64_C(10000000);
    t += INT64_C(116444736000000000);
    return t;
}

/* write the header (used two times if non streamed) */
static int asf_write_header1(AVFormatContext *s, int64_t file_size, int64_t data_chunk_size)
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int header_size, n, extra_size, extra_size2, wav_extra_size, file_time;
    int has_title;
    AVCodecContext *enc;
    int64_t header_offset, cur_pos, hpos;
    int bit_rate;
    int64_t duration;

    duration = asf->duration + PREROLL_TIME * 10000;
    has_title = (s->title[0] || s->author[0] || s->copyright[0] || s->comment[0]);

    bit_rate = 0;
    for(n=0;n<s->nb_streams;n++) {
        enc = s->streams[n]->codec;

        av_set_pts_info(s->streams[n], 32, 1, 1000); /* 32 bit pts in ms */

        bit_rate += enc->bit_rate;
    }

    if (asf->is_streamed) {
        put_chunk(s, 0x4824, 0, 0xc00); /* start of stream (length will be patched later) */
    }

    put_guid(pb, &asf_header);
    put_le64(pb, -1); /* header length, will be patched after */
    put_le32(pb, 3 + has_title + s->nb_streams); /* number of chunks in header */
    put_byte(pb, 1); /* ??? */
    put_byte(pb, 2); /* ??? */

    /* file header */
    header_offset = url_ftell(pb);
    hpos = put_header(pb, &file_header);
    put_guid(pb, &my_guid);
    put_le64(pb, file_size);
    file_time = 0;
    put_le64(pb, unix_to_file_time(file_time));
    put_le64(pb, asf->nb_packets); /* number of packets */
    put_le64(pb, duration); /* end time stamp (in 100ns units) */
    put_le64(pb, asf->duration); /* duration (in 100ns units) */
    put_le64(pb, PREROLL_TIME); /* start time stamp */
    put_le32(pb, (asf->is_streamed || url_is_streamed(pb)) ? 3 : 2); /* ??? */
    put_le32(pb, asf->packet_size); /* packet size */
    put_le32(pb, asf->packet_size); /* packet size */
    put_le32(pb, bit_rate); /* Nominal data rate in bps */
    end_header(pb, hpos);

    /* unknown headers */
    hpos = put_header(pb, &head1_guid);
    put_guid(pb, &head2_guid);
    put_le32(pb, 6);
    put_le16(pb, 0);
    end_header(pb, hpos);

    /* title and other infos */
    if (has_title) {
        hpos = put_header(pb, &comment_header);
        if ( s->title[0]     ) { put_le16(pb, 2 * (strlen(s->title    ) + 1)); } else { put_le16(pb, 0); }
        if ( s->author[0]    ) { put_le16(pb, 2 * (strlen(s->author   ) + 1)); } else { put_le16(pb, 0); }
        if ( s->copyright[0] ) { put_le16(pb, 2 * (strlen(s->copyright) + 1)); } else { put_le16(pb, 0); }
        if ( s->comment[0]   ) { put_le16(pb, 2 * (strlen(s->comment  ) + 1)); } else { put_le16(pb, 0); }
        put_le16(pb, 0);
        if ( s->title[0]     ) put_str16_nolen(pb, s->title);
        if ( s->author[0]    ) put_str16_nolen(pb, s->author);
        if ( s->copyright[0] ) put_str16_nolen(pb, s->copyright);
        if ( s->comment[0]   ) put_str16_nolen(pb, s->comment);
        end_header(pb, hpos);
    }

    /* stream headers */
    for(n=0;n<s->nb_streams;n++) {
        int64_t es_pos;
        //        ASFStream *stream = &asf->streams[n];

        enc = s->streams[n]->codec;
        asf->streams[n].num = n + 1;
        asf->streams[n].seq = 0;


        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            wav_extra_size = 0;
            extra_size = 18 + wav_extra_size;
            extra_size2 = 8;
            break;
        default:
        case CODEC_TYPE_VIDEO:
            wav_extra_size = enc->extradata_size;
            extra_size = 0x33 + wav_extra_size;
            extra_size2 = 0;
            break;
        }

        hpos = put_header(pb, &stream_header);
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            put_guid(pb, &audio_stream);
            put_guid(pb, &audio_conceal_spread);
        } else {
            put_guid(pb, &video_stream);
            put_guid(pb, &video_conceal_none);
        }
        put_le64(pb, 0); /* ??? */
        es_pos = url_ftell(pb);
        put_le32(pb, extra_size); /* wav header len */
        put_le32(pb, extra_size2); /* additional data len */
        put_le16(pb, n + 1); /* stream number */
        put_le32(pb, 0); /* ??? */

        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            /* WAVEFORMATEX header */
            int wavsize = put_wav_header(pb, enc);
            if ((enc->codec_id != CODEC_ID_MP3) && (enc->codec_id != CODEC_ID_MP2) && (enc->codec_id != CODEC_ID_ADPCM_IMA_WAV) && (enc->extradata_size==0)) {
                wavsize += 2;
                put_le16(pb, 0);
            }

            if (wavsize < 0)
                return -1;
            if (wavsize != extra_size) {
                cur_pos = url_ftell(pb);
                url_fseek(pb, es_pos, SEEK_SET);
                put_le32(pb, wavsize); /* wav header len */
                url_fseek(pb, cur_pos, SEEK_SET);
            }
            /* ERROR Correction */
            put_byte(pb, 0x01);
            if(enc->codec_id == CODEC_ID_ADPCM_G726 || !enc->block_align){
                put_le16(pb, 0x0190);
                put_le16(pb, 0x0190);
            }else{
                put_le16(pb, enc->block_align);
                put_le16(pb, enc->block_align);
            }
            put_le16(pb, 0x01);
            put_byte(pb, 0x00);
        } else {
            put_le32(pb, enc->width);
            put_le32(pb, enc->height);
            put_byte(pb, 2); /* ??? */
            put_le16(pb, 40 + enc->extradata_size); /* size */

            /* BITMAPINFOHEADER header */
            put_bmp_header(pb, enc, codec_bmp_tags, 1);
        }
        end_header(pb, hpos);
    }

    /* media comments */

    hpos = put_header(pb, &codec_comment_header);
    put_guid(pb, &codec_comment1_header);
    put_le32(pb, s->nb_streams);
    for(n=0;n<s->nb_streams;n++) {
        AVCodec *p;

        enc = s->streams[n]->codec;
        p = avcodec_find_encoder(enc->codec_id);

        if(enc->codec_type == CODEC_TYPE_AUDIO)
            put_le16(pb, 2);
        else if(enc->codec_type == CODEC_TYPE_VIDEO)
            put_le16(pb, 1);
        else
            put_le16(pb, -1);

        if(enc->codec_id == CODEC_ID_WMAV2)
            put_str16(pb, "Windows Media Audio V8");
        else
            put_str16(pb, p ? p->name : enc->codec_name);
        put_le16(pb, 0); /* no parameters */


        /* id */
        if (enc->codec_type == CODEC_TYPE_AUDIO) {
            put_le16(pb, 2);
            put_le16(pb, enc->codec_tag);
        } else {
            put_le16(pb, 4);
            put_le32(pb, enc->codec_tag);
        }
        if(!enc->codec_tag)
            return -1;
    }
    end_header(pb, hpos);

    /* patch the header size fields */

    cur_pos = url_ftell(pb);
    header_size = cur_pos - header_offset;
    if (asf->is_streamed) {
        header_size += 8 + 30 + 50;

        url_fseek(pb, header_offset - 10 - 30, SEEK_SET);
        put_le16(pb, header_size);
        url_fseek(pb, header_offset - 2 - 30, SEEK_SET);
        put_le16(pb, header_size);

        header_size -= 8 + 30 + 50;
    }
    header_size += 24 + 6;
    url_fseek(pb, header_offset - 14, SEEK_SET);
    put_le64(pb, header_size);
    url_fseek(pb, cur_pos, SEEK_SET);

    /* movie chunk, followed by packets of packet_size */
    asf->data_offset = cur_pos;
    put_guid(pb, &data_header);
    put_le64(pb, data_chunk_size);
    put_guid(pb, &my_guid);
    put_le64(pb, asf->nb_packets); /* nb packets */
    put_byte(pb, 1); /* ??? */
    put_byte(pb, 1); /* ??? */
    return 0;
}

static int asf_write_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    asf->packet_size = PACKET_SIZE;
    asf->nb_packets = 0;

    asf->last_indexed_pts = 0;
    asf->index_ptr = (ASFIndex*)av_malloc( sizeof(ASFIndex) * ASF_INDEX_BLOCK );
    asf->nb_index_memory_alloc = ASF_INDEX_BLOCK;
    asf->nb_index_count = 0;
    asf->maximum_packet = 0;

    if (asf_write_header1(s, 0, 0) < 0) {
        //av_free(asf);
        return -1;
    }

    put_flush_packet(&s->pb);

    asf->packet_nb_payloads = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size, 1,
                  NULL, NULL, NULL, NULL);

    return 0;
}

static int asf_write_stream_header(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;

    asf->is_streamed = 1;

    return asf_write_header(s);
}

static int put_payload_parsing_info(
                                AVFormatContext *s,
                                unsigned int    sendtime,
                                unsigned int    duration,
                                int             nb_payloads,
                                int             padsize
            )
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int ppi_size, i;
    int64_t start= url_ftell(pb);

    int iLengthTypeFlags = ASF_PPI_LENGTH_TYPE_FLAGS;

    padsize -= PACKET_HEADER_MIN_SIZE;
    if(asf->multi_payloads_present)
        padsize--;
    assert(padsize>=0);

    put_byte(pb, ASF_PACKET_ERROR_CORRECTION_FLAGS);
    for (i = 0; i < ASF_PACKET_ERROR_CORRECTION_DATA_SIZE; i++){
        put_byte(pb, 0x0);
    }

    if (asf->multi_payloads_present)
        iLengthTypeFlags |= ASF_PPI_FLAG_MULTIPLE_PAYLOADS_PRESENT;

    if (padsize > 0) {
        if (padsize < 256)
            iLengthTypeFlags |= ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_BYTE;
        else
            iLengthTypeFlags |= ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_WORD;
    }
    put_byte(pb, iLengthTypeFlags);

    put_byte(pb, ASF_PPI_PROPERTY_FLAGS);

    if (iLengthTypeFlags & ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_WORD)
        put_le16(pb, padsize - 2);
    if (iLengthTypeFlags & ASF_PPI_FLAG_PADDING_LENGTH_FIELD_IS_BYTE)
        put_byte(pb, padsize - 1);

    put_le32(pb, sendtime);
    put_le16(pb, duration);
    if (asf->multi_payloads_present)
        put_byte(pb, nb_payloads | ASF_PAYLOAD_FLAGS);

    ppi_size = url_ftell(pb) - start;

    return ppi_size;
}

static void flush_packet(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int packet_hdr_size, packet_filled_size;

    if (asf->is_streamed) {
        put_chunk(s, 0x4424, asf->packet_size, 0);
    }

    packet_hdr_size = put_payload_parsing_info(
                            s,
                            asf->packet_timestamp_start,
                            asf->packet_timestamp_end - asf->packet_timestamp_start,
                            asf->packet_nb_payloads,
                            asf->packet_size_left
                        );

    packet_filled_size = PACKET_SIZE - asf->packet_size_left;
    assert(packet_hdr_size <= asf->packet_size_left);
    memset(asf->packet_buf + packet_filled_size, 0, asf->packet_size_left);

    put_buffer(&s->pb, asf->packet_buf, asf->packet_size - packet_hdr_size);

    put_flush_packet(&s->pb);
    asf->nb_packets++;
    asf->packet_nb_payloads = 0;
    asf->packet_timestamp_start = -1;
    asf->packet_timestamp_end = -1;
    init_put_byte(&asf->pb, asf->packet_buf, asf->packet_size, 1,
                  NULL, NULL, NULL, NULL);
}

static void put_payload_header(
                                AVFormatContext *s,
                                ASFStream       *stream,
                                int             presentation_time,
                                int             m_obj_size,
                                int             m_obj_offset,
                                int             payload_len,
                                int             flags
            )
{
    ASFContext *asf = s->priv_data;
    ByteIOContext *pb = &asf->pb;
    int val;

    val = stream->num;
    if (flags & PKT_FLAG_KEY)
        val |= ASF_PL_FLAG_KEY_FRAME;
    put_byte(pb, val);

    put_byte(pb, stream->seq);  //Media object number
    put_le32(pb, m_obj_offset); //Offset Into Media Object

    // Replicated Data shall be at least 8 bytes long.
    // The first 4 bytes of data shall contain the
    // Size of the Media Object that the payload belongs to.
    // The next 4 bytes of data shall contain the
    // Presentation Time for the media object that the payload belongs to.
    put_byte(pb, ASF_PAYLOAD_REPLICATED_DATA_LENGTH);

    put_le32(pb, m_obj_size);       //Replicated Data - Media Object Size
    put_le32(pb, presentation_time);//Replicated Data - Presentation Time

    if (asf->multi_payloads_present){
        put_le16(pb, payload_len);   //payload length
    }
}

static void put_frame(
                    AVFormatContext *s,
                    ASFStream       *stream,
                    AVStream        *avst,
                    int             timestamp,
                    const uint8_t   *buf,
                    int             m_obj_size,
                    int             flags
                )
{
    ASFContext *asf = s->priv_data;
    int m_obj_offset, payload_len, frag_len1;

    m_obj_offset = 0;
    while (m_obj_offset < m_obj_size) {
        payload_len = m_obj_size - m_obj_offset;
        if (asf->packet_timestamp_start == -1) {
            asf->multi_payloads_present = (payload_len < MULTI_PAYLOAD_CONSTANT);

            asf->packet_size_left = PACKET_SIZE;
            if (asf->multi_payloads_present){
                frag_len1 = MULTI_PAYLOAD_CONSTANT - 1;
            }
            else {
                frag_len1 = SINGLE_PAYLOAD_DATA_LENGTH;
            }
            asf->packet_timestamp_start = timestamp;
        }
        else {
            // multi payloads
            frag_len1 = asf->packet_size_left - PAYLOAD_HEADER_SIZE_MULTIPLE_PAYLOADS - PACKET_HEADER_MIN_SIZE - 1;

            asf->packet_timestamp_start = timestamp;

            if(frag_len1 < payload_len && avst->codec->codec_type == CODEC_TYPE_AUDIO){
                flush_packet(s);
                continue;
            }
        }
        if (frag_len1 > 0) {
            if (payload_len > frag_len1)
                payload_len = frag_len1;
            else if (payload_len == (frag_len1 - 1))
                payload_len = frag_len1 - 2;  //additional byte need to put padding length

            put_payload_header(s, stream, timestamp+PREROLL_TIME, m_obj_size, m_obj_offset, payload_len, flags);
            put_buffer(&asf->pb, buf, payload_len);

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
        buf += payload_len;

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
    ASFStream *stream;
    int64_t duration;
    AVCodecContext *codec;
    int64_t packet_st,pts;
    int start_sec,i;
    int flags= pkt->flags;

    codec = s->streams[pkt->stream_index]->codec;
    stream = &asf->streams[pkt->stream_index];

    if(codec->codec_type == CODEC_TYPE_AUDIO)
        flags &= ~PKT_FLAG_KEY;

    //XXX /FIXME use duration from AVPacket (quick hack by)
    pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
    assert(pts != AV_NOPTS_VALUE);
    duration = pts * 10000;
    asf->duration= FFMAX(asf->duration, duration);

    packet_st = asf->nb_packets;
    put_frame(s, stream, s->streams[pkt->stream_index], pkt->dts, pkt->data, pkt->size, flags);

    /* check index */
    if ((!asf->is_streamed) && (flags & PKT_FLAG_KEY)) {
        start_sec = (int)(duration / INT64_C(10000000));
        if (start_sec != (int)(asf->last_indexed_pts / INT64_C(10000000))) {
            for(i=asf->nb_index_count;i<start_sec;i++) {
                if (i>=asf->nb_index_memory_alloc) {
                    asf->nb_index_memory_alloc += ASF_INDEX_BLOCK;
                    asf->index_ptr = (ASFIndex*)av_realloc( asf->index_ptr, sizeof(ASFIndex) * asf->nb_index_memory_alloc );
                }
                // store
                asf->index_ptr[i].packet_number = (uint32_t)packet_st;
                asf->index_ptr[i].packet_count  = (uint16_t)(asf->nb_packets-packet_st);
                asf->maximum_packet = FFMAX(asf->maximum_packet, (uint16_t)(asf->nb_packets-packet_st));
            }
            asf->nb_index_count = start_sec;
            asf->last_indexed_pts = duration;
        }
    }
    return 0;
}

//
static int asf_write_index(AVFormatContext *s, ASFIndex *index, uint16_t max, uint32_t count)
{
    ByteIOContext *pb = &s->pb;
    int i;

    put_guid(pb, &simple_index_header);
    put_le64(pb, 24 + 16 + 8 + 4 + 4 + (4 + 2)*count);
    put_guid(pb, &my_guid);
    put_le64(pb, ASF_INDEXED_INTERVAL);
    put_le32(pb, max);
    put_le32(pb, count);
    for(i=0; i<count; i++) {
        put_le32(pb, index[i].packet_number);
        put_le16(pb, index[i].packet_count);
    }

    return 0;
}

static int asf_write_trailer(AVFormatContext *s)
{
    ASFContext *asf = s->priv_data;
    int64_t file_size,data_size;

    /* flush the current packet */
    if (asf->pb.buf_ptr > asf->pb.buffer)
        flush_packet(s);

    /* write index */
    data_size = url_ftell(&s->pb);
    if ((!asf->is_streamed) && (asf->nb_index_count != 0)) {
        asf_write_index(s, asf->index_ptr, asf->maximum_packet, asf->nb_index_count);
    }
    put_flush_packet(&s->pb);

    if (asf->is_streamed || url_is_streamed(&s->pb)) {
        put_chunk(s, 0x4524, 0, 0); /* end of stream */
    } else {
        /* rewrite an updated header */
        file_size = url_ftell(&s->pb);
        url_fseek(&s->pb, 0, SEEK_SET);
        asf_write_header1(s, file_size, data_size - asf->data_offset);
    }

    put_flush_packet(&s->pb);
    av_free(asf->index_ptr);
    return 0;
}

#ifdef CONFIG_ASF_MUXER
AVOutputFormat asf_muxer = {
    "asf",
    "asf format",
    "video/x-ms-asf",
    "asf,wmv,wma",
    sizeof(ASFContext),
#ifdef CONFIG_LIBMP3LAME
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MSMPEG4V3,
    asf_write_header,
    asf_write_packet,
    asf_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag= (const AVCodecTag*[]){codec_asf_bmp_tags, codec_bmp_tags, codec_wav_tags, 0},
};
#endif

#ifdef CONFIG_ASF_STREAM_MUXER
AVOutputFormat asf_stream_muxer = {
    "asf_stream",
    "asf format",
    "video/x-ms-asf",
    "asf,wmv,wma",
    sizeof(ASFContext),
#ifdef CONFIG_LIBMP3LAME
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MSMPEG4V3,
    asf_write_stream_header,
    asf_write_packet,
    asf_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag= (const AVCodecTag*[]){codec_asf_bmp_tags, codec_bmp_tags, codec_wav_tags, 0},
};
#endif //CONFIG_ASF_STREAM_MUXER
