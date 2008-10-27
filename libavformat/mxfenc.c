/*
 * MXF muxer
 * Copyright (c) 2008 GUCAS, Zhentan Feng <spyfeng at gmail dot com>
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

/*
 * References
 * SMPTE 336M KLV Data Encoding Protocol Using Key-Length-Value
 * SMPTE 377M MXF File Format Specifications
 * SMPTE 379M MXF Generic Container
 * SMPTE 381M Mapping MPEG Streams into the MXF Generic Container
 * SMPTE RP210: SMPTE Metadata Dictionary
 * SMPTE RP224: Registry of SMPTE Universal Labels
 */

//#define DEBUG

#include "mxf.h"

typedef struct {
    int local_tag;
    UID uid;
} MXFLocalTagPair;

typedef struct {
    UID track_essence_element_key;
    int index; //<<< index in mxf_essence_container_uls table
    const UID *codec_ul;
    int64_t duration;
} MXFStreamContext;

typedef struct {
    UID container_ul;
    UID element_ul;
    UID codec_ul;
    enum CodecID id;
    void (*write_desc)();
} MXFContainerEssenceEntry;

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st);

static const MXFContainerEssenceEntry mxf_essence_container_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x60,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x00,0x00,0x00 },
      CODEC_ID_MPEG2VIDEO, mxf_write_mpegvideo_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x16,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      CODEC_ID_PCM_S16LE, mxf_write_wav_desc },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      CODEC_ID_NONE, NULL },
};

typedef struct MXFContext {
    int64_t footer_partition_offset;
    int essence_container_count;
    uint8_t essence_containers_indices[FF_ARRAY_ELEMS(mxf_essence_container_uls)];
} MXFContext;

static const uint8_t uuid_base[]            = { 0xAD,0xAB,0x44,0x24,0x2f,0x25,0x4d,0xc7,0x92,0xff,0x29,0xbd };
static const uint8_t umid_base[]            = { 0x06,0x0A,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x01,0x0D,0x00,0x13,0x00,0x00,0x00 };

/**
 * complete key for operation pattern, partitions, and primer pack
 */
static const uint8_t op1a_ul[]              = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x01,0x01,0x00 };
static const uint8_t footer_partition_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }; // ClosedComplete
static const uint8_t primer_pack_key[]      = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x05,0x01,0x00 };


static const uint8_t header_open_partition_key[]   = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }; // OpenIncomplete
static const uint8_t header_closed_partition_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }; // ClosedComplete

/**
 * partial key for header metadata
 */
static const uint8_t header_metadata_key[]  = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01 };

static const uint8_t multiple_desc_ul[] = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x0D,0x01,0x03,0x01,0x02,0x7F,0x01,0x00 };

/**
 * SMPTE RP210 http://www.smpte-ra.org/mdd/index.html
 */
static const MXFLocalTagPair mxf_local_tag_batch[] = {
    // preface set
    { 0x3C0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x01,0x01,0x15,0x02,0x00,0x00,0x00,0x00}}, /* Instance UID */
    { 0x3B02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x04,0x00,0x00}}, /* Last Modified Date */
    { 0x3B05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x01,0x02,0x01,0x05,0x00,0x00,0x00}}, /* Version */
    { 0x3B06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x06,0x04,0x00,0x00}}, /* Identifications reference */
    { 0x3B03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x02,0x01,0x00,0x00}}, /* Content Storage reference */
    { 0x3B09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x03,0x00,0x00,0x00,0x00}}, /* Operational Pattern UL */
    { 0x3B0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x10,0x02,0x01,0x00,0x00}}, /* Essence Containers UL batch */
    { 0x3B0B, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x10,0x02,0x02,0x00,0x00}}, /* DM Schemes UL batch */
    // Identification
    { 0x3C09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x01,0x00,0x00,0x00}}, /* This Generation UID */
    { 0x3C01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x02,0x01,0x00,0x00}}, /* Company Name */
    { 0x3C02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x03,0x01,0x00,0x00}}, /* Product Name */
    { 0x3C04, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x05,0x01,0x00,0x00}}, /* Version String */
    { 0x3C05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x07,0x00,0x00,0x00}}, /* Product ID */
    { 0x3C06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x03,0x00,0x00}}, /* Modification Date */
    // Content Storage
    { 0x1901, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x05,0x01,0x00,0x00}}, /* Package strong reference batch */
    // Essence Container Data
    { 0x2701, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x06,0x01,0x00,0x00,0x00}}, /* Linked Package UID */
    { 0x3F07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x01,0x03,0x04,0x04,0x00,0x00,0x00,0x00}}, /* BodySID */
    // Package
    { 0x4401, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x01,0x01,0x15,0x10,0x00,0x00,0x00,0x00}}, /* Package UID */
    { 0x4405, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x01,0x03,0x00,0x00}}, /* Package Creation Date */
    { 0x4404, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x05,0x00,0x00}}, /* Package Modified Date */
    { 0x4403, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x06,0x05,0x00,0x00}}, /* Tracks Strong reference array */
    { 0x4701, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x02,0x03,0x00,0x00}}, /* Descriptor */
    // Track
    { 0x4801, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x01,0x07,0x01,0x01,0x00,0x00,0x00,0x00}}, /* Track ID */
    { 0x4804, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x01,0x04,0x01,0x03,0x00,0x00,0x00,0x00}}, /* Track Number */
    { 0x4B01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x30,0x04,0x05,0x00,0x00,0x00,0x00}}, /* Edit Rate */
    { 0x4B02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x03,0x01,0x03,0x00,0x00}}, /* Origin */
    { 0x4803, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x02,0x04,0x00,0x00}}, /* Sequence reference */
    // Sequence
    { 0x0201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x07,0x01,0x00,0x00,0x00,0x00,0x00}}, /* Data Definition UL */
    { 0x0202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x02,0x01,0x01,0x03,0x00,0x00}}, /* Duration */
    { 0x1001, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x06,0x09,0x00,0x00}}, /* Structural Components reference array */
    // Source Clip
    { 0x1201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x07,0x02,0x01,0x03,0x01,0x04,0x00,0x00}}, /* Start position */
    { 0x1101, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x03,0x01,0x00,0x00,0x00}}, /* SourcePackageID */
    { 0x1102, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x03,0x02,0x00,0x00,0x00}}, /* SourceTrackID */
    // File Descriptor
    { 0x3F01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x06,0x01,0x01,0x04,0x06,0x0B,0x00,0x00}}, /* Sub Descriptors reference array */
    { 0x3006, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x06,0x01,0x01,0x03,0x05,0x00,0x00,0x00}}, /* Linked Track ID */
    { 0x3001, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x06,0x01,0x01,0x00,0x00,0x00,0x00}}, /* SampleRate */
    { 0x3004, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x01,0x02,0x00,0x00}}, /* Essence Container */
    // Generic Picture Essence Descriptor
    { 0x3203, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x02,0x00,0x00,0x00}}, /* Stored Width */
    { 0x3202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x01,0x00,0x00,0x00}}, /* Stored Height */
    { 0x320E, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x01,0x01,0x01,0x00,0x00,0x00}}, /* Aspect Ratio */
    { 0x3201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x06,0x01,0x00,0x00,0x00,0x00}}, /* Picture Essence Coding */
    // Generic Sound Essence Descriptor
    { 0x3D03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x01,0x01,0x01,0x00,0x00}}, /* Audio sampling rate */
    { 0x3D07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x01,0x01,0x04,0x00,0x00,0x00}}, /* ChannelCount */
    { 0x3D01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x02,0x03,0x03,0x04,0x00,0x00,0x00}}, /* Quantization bits */
    { 0x3D06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x02,0x04,0x02,0x00,0x00,0x00,0x00}}, /* Sound Essence Compression */
};

static void mxf_write_uuid(ByteIOContext *pb, enum MXFMetadataSetType type, int value)
{
    put_buffer(pb, uuid_base, 12);
    put_be16(pb, type);
    put_be16(pb, value);
}

static void mxf_write_umid(ByteIOContext *pb, enum MXFMetadataSetType type, int value)
{
    put_buffer(pb, umid_base, 16);
    mxf_write_uuid(pb, type, value);
}

static void mxf_write_refs_count(ByteIOContext *pb, int ref_count)
{
    put_be32(pb, ref_count);
    put_be32(pb, 16);
}

static int klv_encode_ber_length(ByteIOContext *pb, uint64_t len)
{
    // Determine the best BER size
    int size;
    if (len < 128) {
        //short form
        put_byte(pb, len);
        return 1;
    }

    size = (av_log2(len) >> 3) + 1;

    // long form
    put_byte(pb, 0x80 + size);
    while(size) {
        size --;
        put_byte(pb, len >> 8 * size & 0xff);
    }
    return 0;
}

/*
 * Get essence container ul index
 */
static int mxf_get_essence_container_ul_index(enum CodecID id)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(mxf_essence_container_uls); i++)
        if (mxf_essence_container_uls[i].id == id)
            return i;
    return -1;
}

static void mxf_write_primer_pack(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    int local_tag_number, i = 0;

    local_tag_number = FF_ARRAY_ELEMS(mxf_local_tag_batch);

    put_buffer(pb, primer_pack_key, 16);
    klv_encode_ber_length(pb, local_tag_number * 18 + 8);

    put_be32(pb, local_tag_number); // local_tag num
    put_be32(pb, 18); // item size, always 18 according to the specs

    for (i = 0; i < local_tag_number; i++) {
        put_be16(pb, mxf_local_tag_batch[i].local_tag);
        put_buffer(pb, mxf_local_tag_batch[i].uid, 16);
    }
}

static void mxf_write_local_tag(ByteIOContext *pb, int size, int tag)
{
    put_be16(pb, tag);
    put_be16(pb, size);
}

static void mxf_write_metadata_key(ByteIOContext *pb, unsigned int value)
{
    put_buffer(pb, header_metadata_key, 13);
    put_be24(pb, value);
}

static void mxf_free(AVFormatContext *s)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        av_freep(&st->priv_data);
    }
}

static const MXFDataDefinitionUL *mxf_get_data_definition_ul(enum CodecType type)
{
    const MXFDataDefinitionUL *uls = ff_mxf_data_definition_uls;
    while (uls->type != CODEC_TYPE_DATA) {
        if (type == uls->type)
            break;
        uls++;
    }
    return uls;
}

static void mxf_write_essence_container_refs(AVFormatContext *s)
{
    MXFContext *c = s->priv_data;
    ByteIOContext *pb = s->pb;
    int i;

    mxf_write_refs_count(pb, c->essence_container_count);
    av_log(s,AV_LOG_DEBUG, "essence container count:%d\n", c->essence_container_count);
    for (i = 0; i < c->essence_container_count; i++) {
        put_buffer(pb, mxf_essence_container_uls[c->essence_containers_indices[i]].container_ul, 16);
        PRINT_KEY(s, "essence container ul:\n", mxf_essence_container_uls[c->essence_containers_indices[i]].container_ul);
    }
}

static void mxf_write_preface(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012f00);
    PRINT_KEY(s, "preface key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 130 + 16 * mxf->essence_container_count);

    // write preface set uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Preface, 0);
    PRINT_KEY(s, "preface uid", pb->buf_ptr - 16);

    // write create date as unknown
    mxf_write_local_tag(pb, 8, 0x3B02);
    put_be64(pb, 0);

    // write version
    mxf_write_local_tag(pb, 2, 0x3B05);
    put_be16(pb, 1);

    // write identification_refs
    mxf_write_local_tag(pb, 16 + 8, 0x3B06);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, Identification, 0);

    // write content_storage_refs
    mxf_write_local_tag(pb, 16, 0x3B03);
    mxf_write_uuid(pb, ContentStorage, 0);

    mxf_write_local_tag(pb, 16, 0x3B09);
    put_buffer(pb, op1a_ul, 16);

    // write essence_container_refs
    mxf_write_local_tag(pb, 8 + 16 * mxf->essence_container_count, 0x3B0A);
    mxf_write_essence_container_refs(s);

    // write dm_scheme_refs
    mxf_write_local_tag(pb, 8, 0x3B0B);
    put_be64(pb, 0);
}

/*
 * Write a local tag containing an ascii string as utf-16
 */
static void mxf_write_local_tag_utf16(ByteIOContext *pb, int tag, const char *value)
{
    int i, size = strlen(value);
    mxf_write_local_tag(pb, size*2, tag);
    for (i = 0; i < size; i++)
        put_be16(pb, value[i]);
}

static void mxf_write_identification(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    const char *company = "FFmpeg";
    const char *product = "OP1a Muxer";
    const char *version;
    int length;

    mxf_write_metadata_key(pb, 0x013000);
    PRINT_KEY(s, "identification key", pb->buf_ptr - 16);

    version = s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT ?
        "0.0.0" : AV_STRINGIFY(LIBAVFORMAT_VERSION);
    length = 84 + (strlen(company)+strlen(product)+strlen(version))*2; // utf-16
    klv_encode_ber_length(pb, length);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Identification, 0);
    PRINT_KEY(s, "identification uid", pb->buf_ptr - 16);

    // write generation uid
    mxf_write_local_tag(pb, 16, 0x3C09);
    mxf_write_uuid(pb, Identification, 1);

    mxf_write_local_tag_utf16(pb, 0x3C01, company); // Company Name
    mxf_write_local_tag_utf16(pb, 0x3C02, product); // Product Name
    mxf_write_local_tag_utf16(pb, 0x3C04, version); // Version String

    // write product uid
    mxf_write_local_tag(pb, 16, 0x3C05);
    mxf_write_uuid(pb, Identification, 2);

    // write modified date
    mxf_write_local_tag(pb, 8, 0x3C06);
    put_be64(pb, 0);
}

static void mxf_write_content_storage(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x011800);
    PRINT_KEY(s, "content storage key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 64);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, ContentStorage, 0);
    PRINT_KEY(s, "content storage uid", pb->buf_ptr - 16);

    // write package reference
    mxf_write_local_tag(pb, 16 * 2 + 8, 0x1901);
    mxf_write_refs_count(pb, 2);
    mxf_write_uuid(pb, MaterialPackage, 0);
    mxf_write_uuid(pb, SourcePackage, 0);
}

static void mxf_write_track(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    ByteIOContext *pb = s->pb;
    MXFStreamContext *sc = st->priv_data;

    mxf_write_metadata_key(pb, 0x013b00);
    PRINT_KEY(s, "track key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 80);

    // write track uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? Track : Track + TypeBottom, st->index);
    PRINT_KEY(s, "track uid", pb->buf_ptr - 16);

    // write track id
    mxf_write_local_tag(pb, 4, 0x4801);
    put_be32(pb, st->index);

    // write track number
    mxf_write_local_tag(pb, 4, 0x4804);
    if (type == MaterialPackage)
        put_be32(pb, 0); // track number of material package is 0
    else
        put_buffer(pb, sc->track_essence_element_key + 12, 4);

    mxf_write_local_tag(pb, 8, 0x4B01);
    put_be32(pb, st->time_base.den);
    put_be32(pb, st->time_base.num);

    // write origin
    mxf_write_local_tag(pb, 8, 0x4B02);
    put_be64(pb, 0);

    // write sequence refs
    mxf_write_local_tag(pb, 16, 0x4803);
    mxf_write_uuid(pb, type == MaterialPackage ? Sequence: Sequence + TypeBottom, st->index);
}

static void mxf_write_common_fields(ByteIOContext *pb, AVStream *st)
{
    const MXFDataDefinitionUL *data_def_ul = mxf_get_data_definition_ul(st->codec->codec_type);
    MXFStreamContext *sc = st->priv_data;

    // find data define uls
    mxf_write_local_tag(pb, 16, 0x0201);
    put_buffer(pb, data_def_ul->uid, 16);

    // write duration
    mxf_write_local_tag(pb, 8, 0x0202);
    put_be64(pb, sc->duration);
}

static void mxf_write_sequence(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    ByteIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x010f00);
    PRINT_KEY(s, "sequence key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 80);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? Sequence: Sequence + TypeBottom, st->index);

    PRINT_KEY(s, "sequence uid", pb->buf_ptr - 16);
    mxf_write_common_fields(pb, st);

    // write structural component
    mxf_write_local_tag(pb, 16 + 8, 0x1001);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, type == MaterialPackage ? SourceClip: SourceClip + TypeBottom, st->index);
}

static void mxf_write_structural_component(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    ByteIOContext *pb = s->pb;
    int i;

    mxf_write_metadata_key(pb, 0x011100);
    PRINT_KEY(s, "sturctural component key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 108);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? SourceClip: SourceClip + TypeBottom, st->index);

    PRINT_KEY(s, "structural component uid", pb->buf_ptr - 16);
    mxf_write_common_fields(pb, st);

    // write start_position
    mxf_write_local_tag(pb, 8, 0x1201);
    put_be64(pb, 0);

    // write source package uid, end of the reference
    mxf_write_local_tag(pb, 32, 0x1101);
    if (type == SourcePackage) {
        for (i = 0; i < 4; i++)
            put_be64(pb, 0);
    } else
        mxf_write_umid(pb, SourcePackage, 0);

    // write source track id
    mxf_write_local_tag(pb, 4, 0x1102);
    if (type == SourcePackage)
        put_be32(pb, 0);
    else
        put_be32(pb, st->index);
}

static void mxf_write_multi_descriptor(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    int i;

    mxf_write_metadata_key(pb, 0x014400);
    PRINT_KEY(s, "multiple descriptor key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 64 + 16 * s->nb_streams);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, MultipleDescriptor, 0);
    PRINT_KEY(s, "multi_desc uid", pb->buf_ptr - 16);

    // write sample rate
    mxf_write_local_tag(pb, 8, 0x3001);
    put_be32(pb, s->streams[0]->time_base.den);
    put_be32(pb, s->streams[0]->time_base.num);

    // write essence container ul
    mxf_write_local_tag(pb, 16, 0x3004);
    put_buffer(pb, multiple_desc_ul, 16);

    // write sub descriptor refs
    mxf_write_local_tag(pb, s->nb_streams * 16 + 8, 0x3F01);
    mxf_write_refs_count(pb, s->nb_streams);
    for (i = 0; i < s->nb_streams; i++)
        mxf_write_uuid(pb, SubDescriptor, i);
}

static void mxf_write_generic_desc(ByteIOContext *pb, AVStream *st, const UID key)
{
    MXFStreamContext *sc = st->priv_data;

    put_buffer(pb, key, 16);
    klv_encode_ber_length(pb, 108);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, SubDescriptor, st->index);

    mxf_write_local_tag(pb, 4, 0x3006);
    put_be32(pb, st->index);

    mxf_write_local_tag(pb, 8, 0x3001);
    put_be32(pb, st->time_base.den);
    put_be32(pb, st->time_base.num);

    mxf_write_local_tag(pb, 16, 0x3004);
    put_buffer(pb, mxf_essence_container_uls[sc->index].container_ul, 16);

    mxf_write_local_tag(pb, 16, 0x3201);
    put_buffer(pb, *sc->codec_ul, 16);
}

static const UID mxf_mpegvideo_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 };
static const UID mxf_wav_descriptor_key       = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 };

static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st)
{
    ByteIOContext *pb = s->pb;

    mxf_write_generic_desc(pb, st, mxf_mpegvideo_descriptor_key);

    mxf_write_local_tag(pb, 4, 0x3203);
    put_be32(pb, st->codec->width);

    mxf_write_local_tag(pb, 4, 0x3202);
    put_be32(pb, st->codec->height);

    mxf_write_local_tag(pb, 8, 0x320E);
    put_be32(pb, st->codec->height * st->sample_aspect_ratio.den);
    put_be32(pb, st->codec->width  * st->sample_aspect_ratio.num);
}

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st)
{
    ByteIOContext *pb = s->pb;

    mxf_write_generic_desc(pb, st, mxf_wav_descriptor_key);

    // write audio sampling rate
    mxf_write_local_tag(pb, 8, 0x3D03);
    put_be32(pb, st->codec->sample_rate);
    put_be32(pb, 1);

    mxf_write_local_tag(pb, 4, 0x3D07);
    put_be32(pb, st->codec->channels);

    mxf_write_local_tag(pb, 4, 0x3D01);
    put_be32(pb, st->codec->bits_per_coded_sample);
}

static void mxf_write_package(AVFormatContext *s, enum MXFMetadataSetType type)
{
    ByteIOContext *pb = s->pb;
    int i;

    if (type == MaterialPackage) {
        mxf_write_metadata_key(pb, 0x013600);
        PRINT_KEY(s, "Material Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 92 + 16 * s->nb_streams);
    } else {
        mxf_write_metadata_key(pb, 0x013700);
        PRINT_KEY(s, "Source Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 112 + 16 * s->nb_streams); // 20 bytes length for descriptor reference
    }

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type, 0);
    av_log(s,AV_LOG_DEBUG, "package type:%d\n", type);
    PRINT_KEY(s, "package uid", pb->buf_ptr - 16);

    // write package umid
    mxf_write_local_tag(pb, 32, 0x4401);
    mxf_write_umid(pb, type, 0);
    PRINT_KEY(s, "package umid second part", pb->buf_ptr - 16);

    // write create date
    mxf_write_local_tag(pb, 8, 0x4405);
    put_be64(pb, 0);

    // write modified date
    mxf_write_local_tag(pb, 8, 0x4404);
    put_be64(pb, 0);

    // write track refs
    mxf_write_local_tag(pb, s->nb_streams * 16 + 8, 0x4403);
    mxf_write_refs_count(pb, s->nb_streams);
    for (i = 0; i < s->nb_streams; i++)
        mxf_write_uuid(pb, type == MaterialPackage ? Track : Track + TypeBottom, i);

    // write multiple descriptor reference
    if (type == SourcePackage) {
        mxf_write_local_tag(pb, 16, 0x4701);
        if (s->nb_streams > 1) {
            mxf_write_uuid(pb, MultipleDescriptor, 0);
            mxf_write_multi_descriptor(s);
        } else
            mxf_write_uuid(pb, SubDescriptor, 0);
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        mxf_write_track(s, st, type);
        mxf_write_sequence(s, st, type);
        mxf_write_structural_component(s, st, type);

        if (type == SourcePackage) {
            MXFStreamContext *sc = st->priv_data;
            mxf_essence_container_uls[sc->index].write_desc(s, st);
        }
    }
}

static int mxf_write_header_metadata_sets(AVFormatContext *s)
{
    mxf_write_preface(s);
    mxf_write_identification(s);
    mxf_write_content_storage(s);
    mxf_write_package(s, MaterialPackage);
    mxf_write_package(s, SourcePackage);
    return 0;
}

static void mxf_write_partition(AVFormatContext *s, int bodysid, const uint8_t *key, int write_metadata)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t header_byte_count_offset;

    // write klv
    put_buffer(pb, key, 16);

    klv_encode_ber_length(pb, 88 + 16 * mxf->essence_container_count);

    // write partition value
    put_be16(pb, 1); // majorVersion
    put_be16(pb, 2); // minorVersion
    put_be32(pb, 1); // kagSize

    put_be64(pb, url_ftell(pb) - 25); // thisPartition
    put_be64(pb, 0); // previousPartition

    put_be64(pb, mxf->footer_partition_offset); // footerPartition

    // set offset
    header_byte_count_offset = url_ftell(pb);
    put_be64(pb, 0); // headerByteCount, update later

    // no indexTable
    put_be64(pb, 0); // indexByteCount
    put_be32(pb, 0); // indexSID
    put_be64(pb, 0); // bodyOffset

    put_be32(pb, bodysid); // bodySID
    put_buffer(pb, op1a_ul, 16); // operational pattern

    // essence container
    mxf_write_essence_container_refs(s);

    if (write_metadata) {
        // mark the start of the headermetadata and calculate metadata size
        int64_t pos, start = url_ftell(s->pb);
        mxf_write_primer_pack(s);
        mxf_write_header_metadata_sets(s);
        pos = url_ftell(s->pb);
        // update header_byte_count
        url_fseek(pb, header_byte_count_offset, SEEK_SET);
        put_be64(pb, pos - start);
        url_fseek(pb, pos, SEEK_SET);
    }

    put_flush_packet(pb);
}

static const UID mxf_mpeg2_codec_uls[] = {
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x01,0x10,0x00 }, // MP-ML I-Frame
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x01,0x11,0x00 }, // MP-ML Long GOP
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x02,0x02,0x00 }, // 422P-ML I-Frame
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x02,0x03,0x00 }, // 422P-ML Long GOP
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x03,0x02,0x00 }, // MP-HL I-Frame
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x03,0x03,0x00 }, // MP-HL Long GOP
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x04,0x02,0x00 }, // 422P-HL I-Frame
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x04,0x03,0x00 }, // 422P-HL Long GOP
};

static const UID *mxf_get_mpeg2_codec_ul(AVCodecContext *avctx)
{
    if (avctx->profile == 4) { // Main
        if (avctx->level == 8) // Main
            return avctx->gop_size ?
                &mxf_mpeg2_codec_uls[1] :
                &mxf_mpeg2_codec_uls[0];
        else if (avctx->level == 4) // High
            return avctx->gop_size ?
                &mxf_mpeg2_codec_uls[5] :
                &mxf_mpeg2_codec_uls[4];
    } else if (avctx->profile == 0) { // 422
        if (avctx->level == 5) // Main
            return avctx->gop_size ?
                &mxf_mpeg2_codec_uls[3] :
                &mxf_mpeg2_codec_uls[2];
        else if (avctx->level == 2) // High
            return avctx->gop_size ?
                &mxf_mpeg2_codec_uls[7] :
                &mxf_mpeg2_codec_uls[6];
    }
    return NULL;
}

static int mxf_write_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i;
    uint8_t present[FF_ARRAY_ELEMS(mxf_essence_container_uls)] = {0};

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;
        // set pts information
        if (st->codec->codec_type == CODEC_TYPE_VIDEO)
            av_set_pts_info(st, 64, 1, st->codec->time_base.den);
        else if (st->codec->codec_type == CODEC_TYPE_AUDIO)
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
        sc->duration = -1;

        sc->index = mxf_get_essence_container_ul_index(st->codec->codec_id);
        if (sc->index == -1) {
            av_log(s, AV_LOG_ERROR, "track %d: could not find essence container ul, "
                   "codec not currently supported in container\n", i);
            return -1;
        }

        if (st->codec->codec_id == CODEC_ID_MPEG2VIDEO) {
            if (st->codec->profile == FF_PROFILE_UNKNOWN ||
                st->codec->level == FF_LEVEL_UNKNOWN) {
                av_log(s, AV_LOG_ERROR, "track %d: profile and level must be set for mpeg-2\n", i);
                return -1;
            }
            sc->codec_ul = mxf_get_mpeg2_codec_ul(st->codec);
            if (!sc->codec_ul) {
                av_log(s, AV_LOG_ERROR, "track %d: could not find codec ul for mpeg-2, "
                       "unsupported profile/level\n", i);
                return -1;
            }
        } else
            sc->codec_ul = &mxf_essence_container_uls[sc->index].codec_ul;

        if (!present[sc->index]) {
            mxf->essence_containers_indices[mxf->essence_container_count++] = sc->index;
            present[sc->index] = 1;
        } else
            present[sc->index]++;
        memcpy(sc->track_essence_element_key, mxf_essence_container_uls[sc->index].element_ul, 15);
        sc->track_essence_element_key[15] = present[sc->index];
        PRINT_KEY(s, "track essence element key", sc->track_essence_element_key);
    }

    mxf_write_partition(s, 1, header_open_partition_key, 1);

    return 0;
}

static int mxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    MXFStreamContext *sc = st->priv_data;

    put_buffer(pb, sc->track_essence_element_key, 16); // write key
    klv_encode_ber_length(pb, pkt->size); // write length
    put_buffer(pb, pkt->data, pkt->size); // write value

    sc->duration = FFMAX(pkt->pts + pkt->duration, sc->duration);

    put_flush_packet(pb);
    return 0;
}

static int mxf_write_footer(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;

    mxf->footer_partition_offset = url_ftell(pb);
    mxf_write_partition(s, 0, footer_partition_key, 0);
    if (!url_is_streamed(s->pb)) {
        url_fseek(pb, 0, SEEK_SET);
        mxf_write_partition(s, 1, header_closed_partition_key, 1);
    }
    mxf_free(s);
    return 0;
}

AVOutputFormat mxf_muxer = {
    "mxf",
    NULL_IF_CONFIG_SMALL("Material eXchange Format"),
    NULL,
    "mxf",
    sizeof(MXFContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_MPEG2VIDEO,
    mxf_write_header,
    mxf_write_packet,
    mxf_write_footer,
};


