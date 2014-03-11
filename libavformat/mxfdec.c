/*
 * MXF demuxer.
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>
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
 * SMPTE 378M Operational Pattern 1a
 * SMPTE 379M MXF Generic Container
 * SMPTE 381M Mapping MPEG Streams into the MXF Generic Container
 * SMPTE 382M Mapping AES3 and Broadcast Wave Audio into the MXF Generic Container
 * SMPTE 383M Mapping DV-DIF Data to the MXF Generic Container
 *
 * Principle
 * Search for Track numbers which will identify essence element KLV packets.
 * Search for SourcePackage which define tracks which contains Track numbers.
 * Material Package contains tracks with reference to SourcePackage tracks.
 * Search for Descriptors (Picture, Sound) which contains codec info and parameters.
 * Assign Descriptors to correct Tracks.
 *
 * Metadata reading functions read Local Tags, get InstanceUID(0x3C0A) then add MetaDataSet to MXFContext.
 * Metadata parsing resolves Strong References to objects.
 *
 * Simple demuxer, only OP1A supported and some files might not work at all.
 * Only tracks with associated descriptors will be decoded. "Highly Desirable" SMPTE 377M D.1
 */

#include <inttypes.h>

#include "libavutil/aes.h"
#include "libavutil/avassert.h"
#include "libavutil/mathematics.h"
#include "libavcodec/bytestream.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/timecode.h"
#include "avformat.h"
#include "internal.h"
#include "mxf.h"

typedef enum {
    Header,
    BodyPartition,
    Footer
} MXFPartitionType;

typedef enum {
    OP1a = 1,
    OP1b,
    OP1c,
    OP2a,
    OP2b,
    OP2c,
    OP3a,
    OP3b,
    OP3c,
    OPAtom,
    OPSONYOpt,  /* FATE sample, violates the spec in places */
} MXFOP;

typedef struct {
    int closed;
    int complete;
    MXFPartitionType type;
    uint64_t previous_partition;
    int index_sid;
    int body_sid;
    int64_t this_partition;
    int64_t essence_offset;         ///< absolute offset of essence
    int64_t essence_length;
    int32_t kag_size;
    int64_t header_byte_count;
    int64_t index_byte_count;
    int pack_length;
} MXFPartition;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID source_container_ul;
} MXFCryptoContext;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID source_package_uid;
    UID data_definition_ul;
    int64_t duration;
    int64_t start_position;
    int source_track_id;
} MXFStructuralComponent;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID data_definition_ul;
    UID *structural_components_refs;
    int structural_components_count;
    int64_t duration;
} MXFSequence;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    int drop_frame;
    int start_frame;
    struct AVRational rate;
    AVTimecode tc;
} MXFTimecodeComponent;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    MXFSequence *sequence; /* mandatory, and only one */
    UID sequence_ref;
    int track_id;
    uint8_t track_number[4];
    AVRational edit_rate;
    int intra_only;
    uint64_t sample_count;
    int64_t original_duration; /* st->duration in SampleRate/EditRate units */
} MXFTrack;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID essence_container_ul;
    UID essence_codec_ul;
    AVRational sample_rate;
    AVRational aspect_ratio;
    int width;
    int height; /* Field height, not frame height */
    int frame_layout; /* See MXFFrameLayout enum */
#define MXF_TFF 1
#define MXF_BFF 2
    int field_dominance;
    int channels;
    int bits_per_sample;
    unsigned int component_depth;
    unsigned int horiz_subsampling;
    unsigned int vert_subsampling;
    UID *sub_descriptors_refs;
    int sub_descriptors_count;
    int linked_track_id;
    uint8_t *extradata;
    int extradata_size;
    enum AVPixelFormat pix_fmt;
} MXFDescriptor;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    int edit_unit_byte_count;
    int index_sid;
    int body_sid;
    AVRational index_edit_rate;
    uint64_t index_start_position;
    uint64_t index_duration;
    int8_t *temporal_offset_entries;
    int *flag_entries;
    uint64_t *stream_offset_entries;
    int nb_index_entries;
} MXFIndexTableSegment;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID package_uid;
    UID *tracks_refs;
    int tracks_count;
    MXFDescriptor *descriptor; /* only one */
    UID descriptor_ref;
} MXFPackage;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
} MXFMetadataSet;

/* decoded index table */
typedef struct {
    int index_sid;
    int body_sid;
    int nb_ptses;               /* number of PTSes or total duration of index */
    int64_t first_dts;          /* DTS = EditUnit + first_dts */
    int64_t *ptses;             /* maps EditUnit -> PTS */
    int nb_segments;
    MXFIndexTableSegment **segments;    /* sorted by IndexStartPosition */
    AVIndexEntry *fake_index;   /* used for calling ff_index_search_timestamp() */
} MXFIndexTable;

typedef struct {
    MXFPartition *partitions;
    unsigned partitions_count;
    MXFOP op;
    UID *packages_refs;
    int packages_count;
    MXFMetadataSet **metadata_sets;
    int metadata_sets_count;
    AVFormatContext *fc;
    struct AVAES *aesc;
    uint8_t *local_tags;
    int local_tags_count;
    uint64_t last_partition;
    uint64_t footer_partition;
    KLVPacket current_klv_data;
    int current_klv_index;
    int run_in;
    MXFPartition *current_partition;
    int parsing_backward;
    int64_t last_forward_tell;
    int last_forward_partition;
    int current_edit_unit;
    int nb_index_tables;
    MXFIndexTable *index_tables;
    int edit_units_per_packet;      ///< how many edit units to read at a time (PCM, OPAtom)
} MXFContext;

enum MXFWrappingScheme {
    Frame,
    Clip,
};

/* NOTE: klv_offset is not set (-1) for local keys */
typedef int MXFMetadataReadFunc(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset);

typedef struct {
    const UID key;
    MXFMetadataReadFunc *read;
    int ctx_size;
    enum MXFMetadataSetType type;
} MXFMetadataReadTableEntry;

static int mxf_read_close(AVFormatContext *s);

/* partial keys to match */
static const uint8_t mxf_header_partition_pack_key[]       = { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02 };
static const uint8_t mxf_essence_element_key[]             = { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01 };
static const uint8_t mxf_avid_essence_element_key[]        = { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0e,0x04,0x03,0x01 };
static const uint8_t mxf_system_item_key[]                 = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x03,0x01,0x04 };
static const uint8_t mxf_klv_key[]                         = { 0x06,0x0e,0x2b,0x34 };
/* complete keys to match */
static const uint8_t mxf_crypto_source_container_ul[]      = { 0x06,0x0e,0x2b,0x34,0x01,0x01,0x01,0x09,0x06,0x01,0x01,0x02,0x02,0x00,0x00,0x00 };
static const uint8_t mxf_encrypted_triplet_key[]           = { 0x06,0x0e,0x2b,0x34,0x02,0x04,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x7e,0x01,0x00 };
static const uint8_t mxf_encrypted_essence_container[]     = { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x0b,0x01,0x00 };
static const uint8_t mxf_random_index_pack_key[]           = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x11,0x01,0x00 };
static const uint8_t mxf_sony_mpeg4_extradata[]            = { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0e,0x06,0x06,0x02,0x02,0x01,0x00,0x00 };

#define IS_KLV_KEY(x, y) (!memcmp(x, y, sizeof(y)))

static int64_t klv_decode_ber_length(AVIOContext *pb)
{
    uint64_t size = avio_r8(pb);
    if (size & 0x80) { /* long form */
        int bytes_num = size & 0x7f;
        /* SMPTE 379M 5.3.4 guarantee that bytes_num must not exceed 8 bytes */
        if (bytes_num > 8)
            return AVERROR_INVALIDDATA;
        size = 0;
        while (bytes_num--)
            size = size << 8 | avio_r8(pb);
    }
    return size;
}

static int mxf_read_sync(AVIOContext *pb, const uint8_t *key, unsigned size)
{
    int i, b;
    for (i = 0; i < size && !url_feof(pb); i++) {
        b = avio_r8(pb);
        if (b == key[0])
            i = 0;
        else if (b != key[i])
            i = -1;
    }
    return i == size;
}

static int klv_read_packet(KLVPacket *klv, AVIOContext *pb)
{
    if (!mxf_read_sync(pb, mxf_klv_key, 4))
        return AVERROR_INVALIDDATA;
    klv->offset = avio_tell(pb) - 4;
    memcpy(klv->key, mxf_klv_key, 4);
    avio_read(pb, klv->key + 4, 12);
    klv->length = klv_decode_ber_length(pb);
    return klv->length == -1 ? -1 : 0;
}

static int mxf_get_stream_index(AVFormatContext *s, KLVPacket *klv)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        MXFTrack *track = s->streams[i]->priv_data;
        /* SMPTE 379M 7.3 */
        if (!memcmp(klv->key + sizeof(mxf_essence_element_key), track->track_number, sizeof(track->track_number)))
            return i;
    }
    /* return 0 if only one stream, for OP Atom files with 0 as track number */
    return s->nb_streams == 1 ? 0 : -1;
}

/* XXX: use AVBitStreamFilter */
static int mxf_get_d10_aes3_packet(AVIOContext *pb, AVStream *st, AVPacket *pkt, int64_t length)
{
    const uint8_t *buf_ptr, *end_ptr;
    uint8_t *data_ptr;
    int i;

    if (length > 61444) /* worst case PAL 1920 samples 8 channels */
        return AVERROR_INVALIDDATA;
    length = av_get_packet(pb, pkt, length);
    if (length < 0)
        return length;
    data_ptr = pkt->data;
    end_ptr = pkt->data + length;
    buf_ptr = pkt->data + 4; /* skip SMPTE 331M header */
    for (; end_ptr - buf_ptr >= st->codec->channels * 4; ) {
        for (i = 0; i < st->codec->channels; i++) {
            uint32_t sample = bytestream_get_le32(&buf_ptr);
            if (st->codec->bits_per_coded_sample == 24)
                bytestream_put_le24(&data_ptr, (sample >> 4) & 0xffffff);
            else
                bytestream_put_le16(&data_ptr, (sample >> 12) & 0xffff);
        }
        buf_ptr += 32 - st->codec->channels*4; // always 8 channels stored SMPTE 331M
    }
    av_shrink_packet(pkt, data_ptr - pkt->data);
    return 0;
}

static int mxf_decrypt_triplet(AVFormatContext *s, AVPacket *pkt, KLVPacket *klv)
{
    static const uint8_t checkv[16] = {0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b};
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t end = avio_tell(pb) + klv->length;
    int64_t size;
    uint64_t orig_size;
    uint64_t plaintext_size;
    uint8_t ivec[16];
    uint8_t tmpbuf[16];
    int index;

    if (!mxf->aesc && s->key && s->keylen == 16) {
        mxf->aesc = av_aes_alloc();
        if (!mxf->aesc)
            return AVERROR(ENOMEM);
        av_aes_init(mxf->aesc, s->key, 128, 1);
    }
    // crypto context
    avio_skip(pb, klv_decode_ber_length(pb));
    // plaintext offset
    klv_decode_ber_length(pb);
    plaintext_size = avio_rb64(pb);
    // source klv key
    klv_decode_ber_length(pb);
    avio_read(pb, klv->key, 16);
    if (!IS_KLV_KEY(klv, mxf_essence_element_key))
        return AVERROR_INVALIDDATA;
    index = mxf_get_stream_index(s, klv);
    if (index < 0)
        return AVERROR_INVALIDDATA;
    // source size
    klv_decode_ber_length(pb);
    orig_size = avio_rb64(pb);
    if (orig_size < plaintext_size)
        return AVERROR_INVALIDDATA;
    // enc. code
    size = klv_decode_ber_length(pb);
    if (size < 32 || size - 32 < orig_size)
        return AVERROR_INVALIDDATA;
    avio_read(pb, ivec, 16);
    avio_read(pb, tmpbuf, 16);
    if (mxf->aesc)
        av_aes_crypt(mxf->aesc, tmpbuf, tmpbuf, 1, ivec, 1);
    if (memcmp(tmpbuf, checkv, 16))
        av_log(s, AV_LOG_ERROR, "probably incorrect decryption key\n");
    size -= 32;
    size = av_get_packet(pb, pkt, size);
    if (size < 0)
        return size;
    else if (size < plaintext_size)
        return AVERROR_INVALIDDATA;
    size -= plaintext_size;
    if (mxf->aesc)
        av_aes_crypt(mxf->aesc, &pkt->data[plaintext_size],
                     &pkt->data[plaintext_size], size >> 4, ivec, 1);
    av_shrink_packet(pkt, orig_size);
    pkt->stream_index = index;
    avio_skip(pb, end - avio_tell(pb));
    return 0;
}

static int mxf_read_primer_pack(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    int item_num = avio_rb32(pb);
    int item_len = avio_rb32(pb);

    if (item_len != 18) {
        avpriv_request_sample(pb, "Primer pack item length %d", item_len);
        return AVERROR_PATCHWELCOME;
    }
    if (item_num > 65536) {
        av_log(mxf->fc, AV_LOG_ERROR, "item_num %d is too large\n", item_num);
        return AVERROR_INVALIDDATA;
    }
    mxf->local_tags = av_calloc(item_num, item_len);
    if (!mxf->local_tags)
        return AVERROR(ENOMEM);
    mxf->local_tags_count = item_num;
    avio_read(pb, mxf->local_tags, item_num*item_len);
    return 0;
}

static int mxf_read_partition_pack(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    MXFPartition *partition, *tmp_part;
    UID op;
    uint64_t footer_partition;
    uint32_t nb_essence_containers;

    tmp_part = av_realloc_array(mxf->partitions, mxf->partitions_count + 1, sizeof(*mxf->partitions));
    if (!tmp_part)
        return AVERROR(ENOMEM);
    mxf->partitions = tmp_part;

    if (mxf->parsing_backward) {
        /* insert the new partition pack in the middle
         * this makes the entries in mxf->partitions sorted by offset */
        memmove(&mxf->partitions[mxf->last_forward_partition+1],
                &mxf->partitions[mxf->last_forward_partition],
                (mxf->partitions_count - mxf->last_forward_partition)*sizeof(*mxf->partitions));
        partition = mxf->current_partition = &mxf->partitions[mxf->last_forward_partition];
    } else {
        mxf->last_forward_partition++;
        partition = mxf->current_partition = &mxf->partitions[mxf->partitions_count];
    }

    memset(partition, 0, sizeof(*partition));
    mxf->partitions_count++;
    partition->pack_length = avio_tell(pb) - klv_offset + size;

    switch(uid[13]) {
    case 2:
        partition->type = Header;
        break;
    case 3:
        partition->type = BodyPartition;
        break;
    case 4:
        partition->type = Footer;
        break;
    default:
        av_log(mxf->fc, AV_LOG_ERROR, "unknown partition type %i\n", uid[13]);
        return AVERROR_INVALIDDATA;
    }

    /* consider both footers to be closed (there is only Footer and CompleteFooter) */
    partition->closed = partition->type == Footer || !(uid[14] & 1);
    partition->complete = uid[14] > 2;
    avio_skip(pb, 4);
    partition->kag_size = avio_rb32(pb);
    partition->this_partition = avio_rb64(pb);
    partition->previous_partition = avio_rb64(pb);
    footer_partition = avio_rb64(pb);
    partition->header_byte_count = avio_rb64(pb);
    partition->index_byte_count = avio_rb64(pb);
    partition->index_sid = avio_rb32(pb);
    avio_skip(pb, 8);
    partition->body_sid = avio_rb32(pb);
    if (avio_read(pb, op, sizeof(UID)) != sizeof(UID)) {
        av_log(mxf->fc, AV_LOG_ERROR, "Failed reading UID\n");
        return AVERROR_INVALIDDATA;
    }
    nb_essence_containers = avio_rb32(pb);

    /* some files don'thave FooterPartition set in every partition */
    if (footer_partition) {
        if (mxf->footer_partition && mxf->footer_partition != footer_partition) {
            av_log(mxf->fc, AV_LOG_ERROR,
                   "inconsistent FooterPartition value: %"PRIu64" != %"PRIu64"\n",
                   mxf->footer_partition, footer_partition);
        } else {
            mxf->footer_partition = footer_partition;
        }
    }

    av_dlog(mxf->fc,
            "PartitionPack: ThisPartition = 0x%"PRIX64
            ", PreviousPartition = 0x%"PRIX64", "
            "FooterPartition = 0x%"PRIX64", IndexSID = %i, BodySID = %i\n",
            partition->this_partition,
            partition->previous_partition, footer_partition,
            partition->index_sid, partition->body_sid);

    /* sanity check PreviousPartition if set */
    if (partition->previous_partition &&
        mxf->run_in + partition->previous_partition >= klv_offset) {
        av_log(mxf->fc, AV_LOG_ERROR,
               "PreviousPartition points to this partition or forward\n");
        return AVERROR_INVALIDDATA;
    }

    if      (op[12] == 1 && op[13] == 1) mxf->op = OP1a;
    else if (op[12] == 1 && op[13] == 2) mxf->op = OP1b;
    else if (op[12] == 1 && op[13] == 3) mxf->op = OP1c;
    else if (op[12] == 2 && op[13] == 1) mxf->op = OP2a;
    else if (op[12] == 2 && op[13] == 2) mxf->op = OP2b;
    else if (op[12] == 2 && op[13] == 3) mxf->op = OP2c;
    else if (op[12] == 3 && op[13] == 1) mxf->op = OP3a;
    else if (op[12] == 3 && op[13] == 2) mxf->op = OP3b;
    else if (op[12] == 3 && op[13] == 3) mxf->op = OP3c;
    else if (op[12] == 64&& op[13] == 1) mxf->op = OPSONYOpt;
    else if (op[12] == 0x10) {
        /* SMPTE 390m: "There shall be exactly one essence container"
         * The following block deals with files that violate this, namely:
         * 2011_DCPTEST_24FPS.V.mxf - two ECs, OP1a
         * abcdefghiv016f56415e.mxf - zero ECs, OPAtom, output by Avid AirSpeed */
        if (nb_essence_containers != 1) {
            MXFOP op = nb_essence_containers ? OP1a : OPAtom;

            /* only nag once */
            if (!mxf->op)
                av_log(mxf->fc, AV_LOG_WARNING,
                       "\"OPAtom\" with %"PRIu32" ECs - assuming %s\n",
                       nb_essence_containers,
                       op == OP1a ? "OP1a" : "OPAtom");

            mxf->op = op;
        } else
            mxf->op = OPAtom;
    } else {
        av_log(mxf->fc, AV_LOG_ERROR, "unknown operational pattern: %02xh %02xh - guessing OP1a\n", op[12], op[13]);
        mxf->op = OP1a;
    }

    if (partition->kag_size <= 0 || partition->kag_size > (1 << 20)) {
        av_log(mxf->fc, AV_LOG_WARNING, "invalid KAGSize %"PRId32" - guessing ",
               partition->kag_size);

        if (mxf->op == OPSONYOpt)
            partition->kag_size = 512;
        else
            partition->kag_size = 1;

        av_log(mxf->fc, AV_LOG_WARNING, "%"PRId32"\n", partition->kag_size);
    }

    return 0;
}

static int mxf_add_metadata_set(MXFContext *mxf, void *metadata_set)
{
    MXFMetadataSet **tmp;

    tmp = av_realloc_array(mxf->metadata_sets, mxf->metadata_sets_count + 1, sizeof(*mxf->metadata_sets));
    if (!tmp)
        return AVERROR(ENOMEM);
    mxf->metadata_sets = tmp;
    mxf->metadata_sets[mxf->metadata_sets_count] = metadata_set;
    mxf->metadata_sets_count++;
    return 0;
}

static int mxf_read_cryptographic_context(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFCryptoContext *cryptocontext = arg;
    if (size != 16)
        return AVERROR_INVALIDDATA;
    if (IS_KLV_KEY(uid, mxf_crypto_source_container_ul))
        avio_read(pb, cryptocontext->source_container_ul, 16);
    return 0;
}

static int mxf_read_content_storage(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    switch (tag) {
    case 0x1901:
        mxf->packages_count = avio_rb32(pb);
        mxf->packages_refs = av_calloc(mxf->packages_count, sizeof(UID));
        if (!mxf->packages_refs)
            return AVERROR(ENOMEM);
        avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
        avio_read(pb, (uint8_t *)mxf->packages_refs, mxf->packages_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_source_clip(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFStructuralComponent *source_clip = arg;
    switch(tag) {
    case 0x0202:
        source_clip->duration = avio_rb64(pb);
        break;
    case 0x1201:
        source_clip->start_position = avio_rb64(pb);
        break;
    case 0x1101:
        /* UMID, only get last 16 bytes */
        avio_skip(pb, 16);
        avio_read(pb, source_clip->source_package_uid, 16);
        break;
    case 0x1102:
        source_clip->source_track_id = avio_rb32(pb);
        break;
    }
    return 0;
}

static int mxf_read_material_package(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFPackage *package = arg;
    switch(tag) {
    case 0x4403:
        package->tracks_count = avio_rb32(pb);
        package->tracks_refs = av_calloc(package->tracks_count, sizeof(UID));
        if (!package->tracks_refs)
            return AVERROR(ENOMEM);
        avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
        avio_read(pb, (uint8_t *)package->tracks_refs, package->tracks_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_timecode_component(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFTimecodeComponent *mxf_timecode = arg;
    switch(tag) {
    case 0x1501:
        mxf_timecode->start_frame = avio_rb64(pb);
        break;
    case 0x1502:
        mxf_timecode->rate = (AVRational){avio_rb16(pb), 1};
        break;
    case 0x1503:
        mxf_timecode->drop_frame = avio_r8(pb);
        break;
    }
    return 0;
}

static int mxf_read_track(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFTrack *track = arg;
    switch(tag) {
    case 0x4801:
        track->track_id = avio_rb32(pb);
        break;
    case 0x4804:
        avio_read(pb, track->track_number, 4);
        break;
    case 0x4B01:
        track->edit_rate.num = avio_rb32(pb);
        track->edit_rate.den = avio_rb32(pb);
        break;
    case 0x4803:
        avio_read(pb, track->sequence_ref, 16);
        break;
    }
    return 0;
}

static int mxf_read_sequence(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFSequence *sequence = arg;
    switch(tag) {
    case 0x0202:
        sequence->duration = avio_rb64(pb);
        break;
    case 0x0201:
        avio_read(pb, sequence->data_definition_ul, 16);
        break;
    case 0x1001:
        sequence->structural_components_count = avio_rb32(pb);
        sequence->structural_components_refs = av_calloc(sequence->structural_components_count, sizeof(UID));
        if (!sequence->structural_components_refs)
            return AVERROR(ENOMEM);
        avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
        avio_read(pb, (uint8_t *)sequence->structural_components_refs, sequence->structural_components_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_source_package(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFPackage *package = arg;
    switch(tag) {
    case 0x4403:
        package->tracks_count = avio_rb32(pb);
        package->tracks_refs = av_calloc(package->tracks_count, sizeof(UID));
        if (!package->tracks_refs)
            return AVERROR(ENOMEM);
        avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
        avio_read(pb, (uint8_t *)package->tracks_refs, package->tracks_count * sizeof(UID));
        break;
    case 0x4401:
        /* UMID, only get last 16 bytes */
        avio_skip(pb, 16);
        avio_read(pb, package->package_uid, 16);
        break;
    case 0x4701:
        avio_read(pb, package->descriptor_ref, 16);
        break;
    }
    return 0;
}

static int mxf_read_index_entry_array(AVIOContext *pb, MXFIndexTableSegment *segment)
{
    int i, length;

    segment->nb_index_entries = avio_rb32(pb);

    length = avio_rb32(pb);

    if (!(segment->temporal_offset_entries=av_calloc(segment->nb_index_entries, sizeof(*segment->temporal_offset_entries))) ||
        !(segment->flag_entries          = av_calloc(segment->nb_index_entries, sizeof(*segment->flag_entries))) ||
        !(segment->stream_offset_entries = av_calloc(segment->nb_index_entries, sizeof(*segment->stream_offset_entries))))
        return AVERROR(ENOMEM);

    for (i = 0; i < segment->nb_index_entries; i++) {
        segment->temporal_offset_entries[i] = avio_r8(pb);
        avio_r8(pb);                                        /* KeyFrameOffset */
        segment->flag_entries[i] = avio_r8(pb);
        segment->stream_offset_entries[i] = avio_rb64(pb);
        avio_skip(pb, length - 11);
    }
    return 0;
}

static int mxf_read_index_table_segment(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFIndexTableSegment *segment = arg;
    switch(tag) {
    case 0x3F05:
        segment->edit_unit_byte_count = avio_rb32(pb);
        av_dlog(NULL, "EditUnitByteCount %d\n", segment->edit_unit_byte_count);
        break;
    case 0x3F06:
        segment->index_sid = avio_rb32(pb);
        av_dlog(NULL, "IndexSID %d\n", segment->index_sid);
        break;
    case 0x3F07:
        segment->body_sid = avio_rb32(pb);
        av_dlog(NULL, "BodySID %d\n", segment->body_sid);
        break;
    case 0x3F0A:
        av_dlog(NULL, "IndexEntryArray found\n");
        return mxf_read_index_entry_array(pb, segment);
    case 0x3F0B:
        segment->index_edit_rate.num = avio_rb32(pb);
        segment->index_edit_rate.den = avio_rb32(pb);
        av_dlog(NULL, "IndexEditRate %d/%d\n", segment->index_edit_rate.num,
                segment->index_edit_rate.den);
        break;
    case 0x3F0C:
        segment->index_start_position = avio_rb64(pb);
        av_dlog(NULL, "IndexStartPosition %"PRId64"\n", segment->index_start_position);
        break;
    case 0x3F0D:
        segment->index_duration = avio_rb64(pb);
        av_dlog(NULL, "IndexDuration %"PRId64"\n", segment->index_duration);
        break;
    }
    return 0;
}

static void mxf_read_pixel_layout(AVIOContext *pb, MXFDescriptor *descriptor)
{
    int code, value, ofs = 0;
    char layout[16] = {0}; /* not for printing, may end up not terminated on purpose */

    do {
        code = avio_r8(pb);
        value = avio_r8(pb);
        av_dlog(NULL, "pixel layout: code %#x\n", code);

        if (ofs <= 14) {
            layout[ofs++] = code;
            layout[ofs++] = value;
        } else
            break;  /* don't read byte by byte on sneaky files filled with lots of non-zeroes */
    } while (code != 0); /* SMPTE 377M E.2.46 */

    ff_mxf_decode_pixel_layout(layout, &descriptor->pix_fmt);
}

static int mxf_read_generic_descriptor(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFDescriptor *descriptor = arg;
    descriptor->pix_fmt = AV_PIX_FMT_NONE;
    switch(tag) {
    case 0x3F01:
        descriptor->sub_descriptors_count = avio_rb32(pb);
        descriptor->sub_descriptors_refs = av_calloc(descriptor->sub_descriptors_count, sizeof(UID));
        if (!descriptor->sub_descriptors_refs)
            return AVERROR(ENOMEM);
        avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
        avio_read(pb, (uint8_t *)descriptor->sub_descriptors_refs, descriptor->sub_descriptors_count * sizeof(UID));
        break;
    case 0x3004:
        avio_read(pb, descriptor->essence_container_ul, 16);
        break;
    case 0x3006:
        descriptor->linked_track_id = avio_rb32(pb);
        break;
    case 0x3201: /* PictureEssenceCoding */
        avio_read(pb, descriptor->essence_codec_ul, 16);
        break;
    case 0x3203:
        descriptor->width = avio_rb32(pb);
        break;
    case 0x3202:
        descriptor->height = avio_rb32(pb);
        break;
    case 0x320C:
        descriptor->frame_layout = avio_r8(pb);
        break;
    case 0x320E:
        descriptor->aspect_ratio.num = avio_rb32(pb);
        descriptor->aspect_ratio.den = avio_rb32(pb);
        break;
    case 0x3212:
        descriptor->field_dominance = avio_r8(pb);
        break;
    case 0x3301:
        descriptor->component_depth = avio_rb32(pb);
        break;
    case 0x3302:
        descriptor->horiz_subsampling = avio_rb32(pb);
        break;
    case 0x3308:
        descriptor->vert_subsampling = avio_rb32(pb);
        break;
    case 0x3D03:
        descriptor->sample_rate.num = avio_rb32(pb);
        descriptor->sample_rate.den = avio_rb32(pb);
        break;
    case 0x3D06: /* SoundEssenceCompression */
        avio_read(pb, descriptor->essence_codec_ul, 16);
        break;
    case 0x3D07:
        descriptor->channels = avio_rb32(pb);
        break;
    case 0x3D01:
        descriptor->bits_per_sample = avio_rb32(pb);
        break;
    case 0x3401:
        mxf_read_pixel_layout(pb, descriptor);
        break;
    default:
        /* Private uid used by SONY C0023S01.mxf */
        if (IS_KLV_KEY(uid, mxf_sony_mpeg4_extradata)) {
            if (descriptor->extradata)
                av_log(NULL, AV_LOG_WARNING, "Duplicate sony_mpeg4_extradata\n");
            av_free(descriptor->extradata);
            descriptor->extradata_size = 0;
            descriptor->extradata = av_malloc(size);
            if (!descriptor->extradata)
                return AVERROR(ENOMEM);
            descriptor->extradata_size = size;
            avio_read(pb, descriptor->extradata, size);
        }
        break;
    }
    return 0;
}

/*
 * Match an uid independently of the version byte and up to len common bytes
 * Returns: boolean
 */
static int mxf_match_uid(const UID key, const UID uid, int len)
{
    int i;
    for (i = 0; i < len; i++) {
        if (i != 7 && key[i] != uid[i])
            return 0;
    }
    return 1;
}

static const MXFCodecUL *mxf_get_codec_ul(const MXFCodecUL *uls, UID *uid)
{
    while (uls->uid[0]) {
        if(mxf_match_uid(uls->uid, *uid, uls->matching_len))
            break;
        uls++;
    }
    return uls;
}

static void *mxf_resolve_strong_ref(MXFContext *mxf, UID *strong_ref, enum MXFMetadataSetType type)
{
    int i;

    if (!strong_ref)
        return NULL;
    for (i = 0; i < mxf->metadata_sets_count; i++) {
        if (!memcmp(*strong_ref, mxf->metadata_sets[i]->uid, 16) &&
            (type == AnyType || mxf->metadata_sets[i]->type == type)) {
            return mxf->metadata_sets[i];
        }
    }
    return NULL;
}

static const MXFCodecUL mxf_picture_essence_container_uls[] = {
    // video essence container uls
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x60,0x01 }, 14, AV_CODEC_ID_MPEG2VIDEO }, /* MPEG-ES Frame wrapped */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x41,0x01 }, 14,    AV_CODEC_ID_DVVIDEO }, /* DV 625 25mbps */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x05,0x00,0x00 }, 14,   AV_CODEC_ID_RAWVIDEO }, /* Uncompressed Picture */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,      AV_CODEC_ID_NONE },
};

/* EC ULs for intra-only formats */
static const MXFCodecUL mxf_intra_only_essence_container_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x00,0x00 }, 14, AV_CODEC_ID_MPEG2VIDEO }, /* MXF-GC SMPTE D-10 Mappings */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,       AV_CODEC_ID_NONE },
};

/* intra-only PictureEssenceCoding ULs, where no corresponding EC UL exists */
static const MXFCodecUL mxf_intra_only_picture_essence_coding_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x00,0x00 }, 14,       AV_CODEC_ID_H264 }, /* H.264/MPEG-4 AVC Intra Profiles */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x07,0x04,0x01,0x02,0x02,0x03,0x01,0x01,0x00 }, 14,   AV_CODEC_ID_JPEG2000 }, /* JPEG2000 Codestream */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,       AV_CODEC_ID_NONE },
};

static const MXFCodecUL mxf_sound_essence_container_uls[] = {
    // sound essence container uls
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x01,0x00 }, 14, AV_CODEC_ID_PCM_S16LE }, /* BWF Frame wrapped */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x40,0x01 }, 14,       AV_CODEC_ID_MP2 }, /* MPEG-ES Frame wrapped, 0x40 ??? stream id */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 }, 14, AV_CODEC_ID_PCM_S16LE }, /* D-10 Mapping 50Mbps PAL Extended Template */
    { { 0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0xFF,0x4B,0x46,0x41,0x41,0x00,0x0D,0x4D,0x4F }, 14, AV_CODEC_ID_PCM_S16LE }, /* 0001GL00.MXF.A1.mxf_opatom.mxf */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,      AV_CODEC_ID_NONE },
};

static int mxf_get_sorted_table_segments(MXFContext *mxf, int *nb_sorted_segments, MXFIndexTableSegment ***sorted_segments)
{
    int i, j, nb_segments = 0;
    MXFIndexTableSegment **unsorted_segments;
    int last_body_sid = -1, last_index_sid = -1, last_index_start = -1;

    /* count number of segments, allocate arrays and copy unsorted segments */
    for (i = 0; i < mxf->metadata_sets_count; i++)
        if (mxf->metadata_sets[i]->type == IndexTableSegment)
            nb_segments++;

    if (!nb_segments)
        return AVERROR_INVALIDDATA;

    if (!(unsorted_segments = av_calloc(nb_segments, sizeof(*unsorted_segments))) ||
        !(*sorted_segments  = av_calloc(nb_segments, sizeof(**sorted_segments)))) {
        av_freep(sorted_segments);
        av_free(unsorted_segments);
        return AVERROR(ENOMEM);
    }

    for (i = j = 0; i < mxf->metadata_sets_count; i++)
        if (mxf->metadata_sets[i]->type == IndexTableSegment)
            unsorted_segments[j++] = (MXFIndexTableSegment*)mxf->metadata_sets[i];

    *nb_sorted_segments = 0;

    /* sort segments by {BodySID, IndexSID, IndexStartPosition}, remove duplicates while we're at it */
    for (i = 0; i < nb_segments; i++) {
        int best = -1, best_body_sid = -1, best_index_sid = -1, best_index_start = -1;
        uint64_t best_index_duration = 0;

        for (j = 0; j < nb_segments; j++) {
            MXFIndexTableSegment *s = unsorted_segments[j];

            /* Require larger BosySID, IndexSID or IndexStartPosition then the previous entry. This removes duplicates.
             * We want the smallest values for the keys than what we currently have, unless this is the first such entry this time around.
             * If we come across an entry with the same IndexStartPosition but larger IndexDuration, then we'll prefer it over the one we currently have.
             */
            if ((i == 0     || s->body_sid > last_body_sid || s->index_sid > last_index_sid || s->index_start_position > last_index_start) &&
                (best == -1 || s->body_sid < best_body_sid || s->index_sid < best_index_sid || s->index_start_position < best_index_start ||
                (s->index_start_position == best_index_start && s->index_duration > best_index_duration))) {
                best             = j;
                best_body_sid    = s->body_sid;
                best_index_sid   = s->index_sid;
                best_index_start = s->index_start_position;
                best_index_duration = s->index_duration;
            }
        }

        /* no suitable entry found -> we're done */
        if (best == -1)
            break;

        (*sorted_segments)[(*nb_sorted_segments)++] = unsorted_segments[best];
        last_body_sid    = best_body_sid;
        last_index_sid   = best_index_sid;
        last_index_start = best_index_start;
    }

    av_free(unsorted_segments);

    return 0;
}

/**
 * Computes the absolute file offset of the given essence container offset
 */
static int mxf_absolute_bodysid_offset(MXFContext *mxf, int body_sid, int64_t offset, int64_t *offset_out)
{
    int x;
    int64_t offset_in = offset;     /* for logging */

    for (x = 0; x < mxf->partitions_count; x++) {
        MXFPartition *p = &mxf->partitions[x];

        if (p->body_sid != body_sid)
            continue;

        if (offset < p->essence_length || !p->essence_length) {
            *offset_out = p->essence_offset + offset;
            return 0;
        }

        offset -= p->essence_length;
    }

    av_log(mxf->fc, AV_LOG_ERROR,
           "failed to find absolute offset of %"PRIX64" in BodySID %i - partial file?\n",
           offset_in, body_sid);

    return AVERROR_INVALIDDATA;
}

/**
 * Returns the end position of the essence container with given BodySID, or zero if unknown
 */
static int64_t mxf_essence_container_end(MXFContext *mxf, int body_sid)
{
    int x;
    int64_t ret = 0;

    for (x = 0; x < mxf->partitions_count; x++) {
        MXFPartition *p = &mxf->partitions[x];

        if (p->body_sid != body_sid)
            continue;

        if (!p->essence_length)
            return 0;

        ret = p->essence_offset + p->essence_length;
    }

    return ret;
}

/* EditUnit -> absolute offset */
static int mxf_edit_unit_absolute_offset(MXFContext *mxf, MXFIndexTable *index_table, int64_t edit_unit, int64_t *edit_unit_out, int64_t *offset_out, int nag)
{
    int i;
    int64_t offset_temp = 0;

    for (i = 0; i < index_table->nb_segments; i++) {
        MXFIndexTableSegment *s = index_table->segments[i];

        edit_unit = FFMAX(edit_unit, s->index_start_position);  /* clamp if trying to seek before start */

        if (edit_unit < s->index_start_position + s->index_duration) {
            int64_t index = edit_unit - s->index_start_position;

            if (s->edit_unit_byte_count)
                offset_temp += s->edit_unit_byte_count * index;
            else if (s->nb_index_entries) {
                if (s->nb_index_entries == 2 * s->index_duration + 1)
                    index *= 2;     /* Avid index */

                if (index < 0 || index >= s->nb_index_entries) {
                    av_log(mxf->fc, AV_LOG_ERROR, "IndexSID %i segment at %"PRId64" IndexEntryArray too small\n",
                           index_table->index_sid, s->index_start_position);
                    return AVERROR_INVALIDDATA;
                }

                offset_temp = s->stream_offset_entries[index];
            } else {
                av_log(mxf->fc, AV_LOG_ERROR, "IndexSID %i segment at %"PRId64" missing EditUnitByteCount and IndexEntryArray\n",
                       index_table->index_sid, s->index_start_position);
                return AVERROR_INVALIDDATA;
            }

            if (edit_unit_out)
                *edit_unit_out = edit_unit;

            return mxf_absolute_bodysid_offset(mxf, index_table->body_sid, offset_temp, offset_out);
        } else {
            /* EditUnitByteCount == 0 for VBR indexes, which is fine since they use explicit StreamOffsets */
            offset_temp += s->edit_unit_byte_count * s->index_duration;
        }
    }

    if (nag)
        av_log(mxf->fc, AV_LOG_ERROR, "failed to map EditUnit %"PRId64" in IndexSID %i to an offset\n", edit_unit, index_table->index_sid);

    return AVERROR_INVALIDDATA;
}

static int mxf_compute_ptses_fake_index(MXFContext *mxf, MXFIndexTable *index_table)
{
    int i, j, x;
    int8_t max_temporal_offset = -128;

    /* first compute how many entries we have */
    for (i = 0; i < index_table->nb_segments; i++) {
        MXFIndexTableSegment *s = index_table->segments[i];

        if (!s->nb_index_entries) {
            index_table->nb_ptses = 0;
            return 0;                               /* no TemporalOffsets */
        }

        index_table->nb_ptses += s->index_duration;
    }

    /* paranoid check */
    if (index_table->nb_ptses <= 0)
        return 0;

    if (!(index_table->ptses      = av_calloc(index_table->nb_ptses, sizeof(int64_t))) ||
        !(index_table->fake_index = av_calloc(index_table->nb_ptses, sizeof(AVIndexEntry)))) {
        av_freep(&index_table->ptses);
        return AVERROR(ENOMEM);
    }

    /* we may have a few bad TemporalOffsets
     * make sure the corresponding PTSes don't have the bogus value 0 */
    for (x = 0; x < index_table->nb_ptses; x++)
        index_table->ptses[x] = AV_NOPTS_VALUE;

    /**
     * We have this:
     *
     * x  TemporalOffset
     * 0:  0
     * 1:  1
     * 2:  1
     * 3: -2
     * 4:  1
     * 5:  1
     * 6: -2
     *
     * We want to transform it into this:
     *
     * x  DTS PTS
     * 0: -1   0
     * 1:  0   3
     * 2:  1   1
     * 3:  2   2
     * 4:  3   6
     * 5:  4   4
     * 6:  5   5
     *
     * We do this by bucket sorting x by x+TemporalOffset[x] into mxf->ptses,
     * then settings mxf->first_dts = -max(TemporalOffset[x]).
     * The latter makes DTS <= PTS.
     */
    for (i = x = 0; i < index_table->nb_segments; i++) {
        MXFIndexTableSegment *s = index_table->segments[i];
        int index_delta = 1;
        int n = s->nb_index_entries;

        if (s->nb_index_entries == 2 * s->index_duration + 1) {
            index_delta = 2;    /* Avid index */
            /* ignore the last entry - it's the size of the essence container */
            n--;
        }

        for (j = 0; j < n; j += index_delta, x++) {
            int offset = s->temporal_offset_entries[j] / index_delta;
            int index  = x + offset;

            if (x >= index_table->nb_ptses) {
                av_log(mxf->fc, AV_LOG_ERROR,
                       "x >= nb_ptses - IndexEntryCount %i < IndexDuration %"PRId64"?\n",
                       s->nb_index_entries, s->index_duration);
                break;
            }

            index_table->fake_index[x].timestamp = x;
            index_table->fake_index[x].flags = !(s->flag_entries[j] & 0x30) ? AVINDEX_KEYFRAME : 0;

            if (index < 0 || index >= index_table->nb_ptses) {
                av_log(mxf->fc, AV_LOG_ERROR,
                       "index entry %i + TemporalOffset %i = %i, which is out of bounds\n",
                       x, offset, index);
                continue;
            }

            index_table->ptses[index] = x;
            max_temporal_offset = FFMAX(max_temporal_offset, offset);
        }
    }

    index_table->first_dts = -max_temporal_offset;

    return 0;
}

/**
 * Sorts and collects index table segments into index tables.
 * Also computes PTSes if possible.
 */
static int mxf_compute_index_tables(MXFContext *mxf)
{
    int i, j, k, ret, nb_sorted_segments;
    MXFIndexTableSegment **sorted_segments = NULL;

    if ((ret = mxf_get_sorted_table_segments(mxf, &nb_sorted_segments, &sorted_segments)) ||
        nb_sorted_segments <= 0) {
        av_log(mxf->fc, AV_LOG_WARNING, "broken or empty index\n");
        return 0;
    }

    /* sanity check and count unique BodySIDs/IndexSIDs */
    for (i = 0; i < nb_sorted_segments; i++) {
        if (i == 0 || sorted_segments[i-1]->index_sid != sorted_segments[i]->index_sid)
            mxf->nb_index_tables++;
        else if (sorted_segments[i-1]->body_sid != sorted_segments[i]->body_sid) {
            av_log(mxf->fc, AV_LOG_ERROR, "found inconsistent BodySID\n");
            ret = AVERROR_INVALIDDATA;
            goto finish_decoding_index;
        }
    }

    mxf->index_tables = av_mallocz_array(mxf->nb_index_tables,
                                         sizeof(*mxf->index_tables));
    if (!mxf->index_tables) {
        av_log(mxf->fc, AV_LOG_ERROR, "failed to allocate index tables\n");
        ret = AVERROR(ENOMEM);
        goto finish_decoding_index;
    }

    /* distribute sorted segments to index tables */
    for (i = j = 0; i < nb_sorted_segments; i++) {
        if (i != 0 && sorted_segments[i-1]->index_sid != sorted_segments[i]->index_sid) {
            /* next IndexSID */
            j++;
        }

        mxf->index_tables[j].nb_segments++;
    }

    for (i = j = 0; j < mxf->nb_index_tables; i += mxf->index_tables[j++].nb_segments) {
        MXFIndexTable *t = &mxf->index_tables[j];

        t->segments = av_mallocz_array(t->nb_segments,
                                       sizeof(*t->segments));

        if (!t->segments) {
            av_log(mxf->fc, AV_LOG_ERROR, "failed to allocate IndexTableSegment"
                   " pointer array\n");
            ret = AVERROR(ENOMEM);
            goto finish_decoding_index;
        }

        if (sorted_segments[i]->index_start_position)
            av_log(mxf->fc, AV_LOG_WARNING, "IndexSID %i starts at EditUnit %"PRId64" - seeking may not work as expected\n",
                   sorted_segments[i]->index_sid, sorted_segments[i]->index_start_position);

        memcpy(t->segments, &sorted_segments[i], t->nb_segments * sizeof(MXFIndexTableSegment*));
        t->index_sid = sorted_segments[i]->index_sid;
        t->body_sid = sorted_segments[i]->body_sid;

        if ((ret = mxf_compute_ptses_fake_index(mxf, t)) < 0)
            goto finish_decoding_index;

        /* fix zero IndexDurations */
        for (k = 0; k < t->nb_segments; k++) {
            if (t->segments[k]->index_duration)
                continue;

            if (t->nb_segments > 1)
                av_log(mxf->fc, AV_LOG_WARNING, "IndexSID %i segment %i has zero IndexDuration and there's more than one segment\n",
                       t->index_sid, k);

            if (mxf->fc->nb_streams <= 0) {
                av_log(mxf->fc, AV_LOG_WARNING, "no streams?\n");
                break;
            }

            /* assume the first stream's duration is reasonable
             * leave index_duration = 0 on further segments in case we have any (unlikely)
             */
            t->segments[k]->index_duration = mxf->fc->streams[0]->duration;
            break;
        }
    }

    ret = 0;
finish_decoding_index:
    av_free(sorted_segments);
    return ret;
}

static int mxf_is_intra_only(MXFDescriptor *descriptor)
{
    return mxf_get_codec_ul(mxf_intra_only_essence_container_uls,
                            &descriptor->essence_container_ul)->id != AV_CODEC_ID_NONE ||
           mxf_get_codec_ul(mxf_intra_only_picture_essence_coding_uls,
                            &descriptor->essence_codec_ul)->id     != AV_CODEC_ID_NONE;
}

static int mxf_add_timecode_metadata(AVDictionary **pm, const char *key, AVTimecode *tc)
{
    char buf[AV_TIMECODE_STR_SIZE];
    av_dict_set(pm, key, av_timecode_make_string(tc, buf, 0), 0);

    return 0;
}

static int mxf_parse_structural_metadata(MXFContext *mxf)
{
    MXFPackage *material_package = NULL;
    MXFPackage *temp_package = NULL;
    int i, j, k, ret;

    av_dlog(mxf->fc, "metadata sets count %d\n", mxf->metadata_sets_count);
    /* TODO: handle multiple material packages (OP3x) */
    for (i = 0; i < mxf->packages_count; i++) {
        material_package = mxf_resolve_strong_ref(mxf, &mxf->packages_refs[i], MaterialPackage);
        if (material_package) break;
    }
    if (!material_package) {
        av_log(mxf->fc, AV_LOG_ERROR, "no material package found\n");
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < material_package->tracks_count; i++) {
        MXFPackage *source_package = NULL;
        MXFTrack *material_track = NULL;
        MXFTrack *source_track = NULL;
        MXFTrack *temp_track = NULL;
        MXFDescriptor *descriptor = NULL;
        MXFStructuralComponent *component = NULL;
        MXFTimecodeComponent *mxf_tc = NULL;
        UID *essence_container_ul = NULL;
        const MXFCodecUL *codec_ul = NULL;
        const MXFCodecUL *container_ul = NULL;
        const MXFCodecUL *pix_fmt_ul = NULL;
        AVStream *st;
        AVTimecode tc;
        int flags;

        if (!(material_track = mxf_resolve_strong_ref(mxf, &material_package->tracks_refs[i], Track))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve material track strong ref\n");
            continue;
        }

        if ((component = mxf_resolve_strong_ref(mxf, &material_track->sequence_ref, TimecodeComponent))) {
            mxf_tc = (MXFTimecodeComponent*)component;
            flags = mxf_tc->drop_frame == 1 ? AV_TIMECODE_FLAG_DROPFRAME : 0;
            if (av_timecode_init(&tc, mxf_tc->rate, flags, mxf_tc->start_frame, mxf->fc) == 0) {
                mxf_add_timecode_metadata(&mxf->fc->metadata, "timecode", &tc);
            }
        }

        if (!(material_track->sequence = mxf_resolve_strong_ref(mxf, &material_track->sequence_ref, Sequence))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve material track sequence strong ref\n");
            continue;
        }

        for (j = 0; j < material_track->sequence->structural_components_count; j++) {
            component = mxf_resolve_strong_ref(mxf, &material_track->sequence->structural_components_refs[j], TimecodeComponent);
            if (!component)
                continue;

            mxf_tc = (MXFTimecodeComponent*)component;
            flags = mxf_tc->drop_frame == 1 ? AV_TIMECODE_FLAG_DROPFRAME : 0;
            if (av_timecode_init(&tc, mxf_tc->rate, flags, mxf_tc->start_frame, mxf->fc) == 0) {
                mxf_add_timecode_metadata(&mxf->fc->metadata, "timecode", &tc);
                break;
            }
        }

        /* TODO: handle multiple source clips */
        for (j = 0; j < material_track->sequence->structural_components_count; j++) {
            component = mxf_resolve_strong_ref(mxf, &material_track->sequence->structural_components_refs[j], SourceClip);
            if (!component)
                continue;

            for (k = 0; k < mxf->packages_count; k++) {
                temp_package = mxf_resolve_strong_ref(mxf, &mxf->packages_refs[k], SourcePackage);
                if (!temp_package)
                    continue;
                if (!memcmp(temp_package->package_uid, component->source_package_uid, 16)) {
                    source_package = temp_package;
                    break;
                }
            }
            if (!source_package) {
                av_dlog(mxf->fc, "material track %d: no corresponding source package found\n", material_track->track_id);
                break;
            }
            for (k = 0; k < source_package->tracks_count; k++) {
                if (!(temp_track = mxf_resolve_strong_ref(mxf, &source_package->tracks_refs[k], Track))) {
                    av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track strong ref\n");
                    ret = AVERROR_INVALIDDATA;
                    goto fail_and_free;
                }
                if (temp_track->track_id == component->source_track_id) {
                    source_track = temp_track;
                    break;
                }
            }
            if (!source_track) {
                av_log(mxf->fc, AV_LOG_ERROR, "material track %d: no corresponding source track found\n", material_track->track_id);
                break;
            }
        }
        if (!source_track || !component)
            continue;

        if (!(source_track->sequence = mxf_resolve_strong_ref(mxf, &source_track->sequence_ref, Sequence))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track sequence strong ref\n");
            ret = AVERROR_INVALIDDATA;
            goto fail_and_free;
        }

        /* 0001GL00.MXF.A1.mxf_opatom.mxf has the same SourcePackageID as 0001GL.MXF.V1.mxf_opatom.mxf
         * This would result in both files appearing to have two streams. Work around this by sanity checking DataDefinition */
        if (memcmp(material_track->sequence->data_definition_ul, source_track->sequence->data_definition_ul, 16)) {
            av_log(mxf->fc, AV_LOG_ERROR, "material track %d: DataDefinition mismatch\n", material_track->track_id);
            continue;
        }

        st = avformat_new_stream(mxf->fc, NULL);
        if (!st) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not allocate stream\n");
            ret = AVERROR(ENOMEM);
            goto fail_and_free;
        }
        st->id = source_track->track_id;
        st->priv_data = source_track;
        source_track->original_duration = st->duration = component->duration;
        if (st->duration == -1)
            st->duration = AV_NOPTS_VALUE;
        st->start_time = component->start_position;
        if (material_track->edit_rate.num <= 0 ||
            material_track->edit_rate.den <= 0) {
            av_log(mxf->fc, AV_LOG_WARNING,
                   "Invalid edit rate (%d/%d) found on stream #%d, "
                   "defaulting to 25/1\n",
                   material_track->edit_rate.num,
                   material_track->edit_rate.den, st->index);
            material_track->edit_rate = (AVRational){25, 1};
        }
        avpriv_set_pts_info(st, 64, material_track->edit_rate.den, material_track->edit_rate.num);

        /* ensure SourceTrack EditRate == MaterialTrack EditRate since only
         * the former is accessible via st->priv_data */
        source_track->edit_rate = material_track->edit_rate;

        PRINT_KEY(mxf->fc, "data definition   ul", source_track->sequence->data_definition_ul);
        codec_ul = mxf_get_codec_ul(ff_mxf_data_definition_uls, &source_track->sequence->data_definition_ul);
        st->codec->codec_type = codec_ul->id;

        source_package->descriptor = mxf_resolve_strong_ref(mxf, &source_package->descriptor_ref, AnyType);
        if (source_package->descriptor) {
            if (source_package->descriptor->type == MultipleDescriptor) {
                for (j = 0; j < source_package->descriptor->sub_descriptors_count; j++) {
                    MXFDescriptor *sub_descriptor = mxf_resolve_strong_ref(mxf, &source_package->descriptor->sub_descriptors_refs[j], Descriptor);

                    if (!sub_descriptor) {
                        av_log(mxf->fc, AV_LOG_ERROR, "could not resolve sub descriptor strong ref\n");
                        continue;
                    }
                    if (sub_descriptor->linked_track_id == source_track->track_id) {
                        descriptor = sub_descriptor;
                        break;
                    }
                }
            } else if (source_package->descriptor->type == Descriptor)
                descriptor = source_package->descriptor;
        }
        if (!descriptor) {
            av_log(mxf->fc, AV_LOG_INFO, "source track %d: stream %d, no descriptor found\n", source_track->track_id, st->index);
            continue;
        }
        PRINT_KEY(mxf->fc, "essence codec     ul", descriptor->essence_codec_ul);
        PRINT_KEY(mxf->fc, "essence container ul", descriptor->essence_container_ul);
        essence_container_ul = &descriptor->essence_container_ul;
        /* HACK: replacing the original key with mxf_encrypted_essence_container
         * is not allowed according to s429-6, try to find correct information anyway */
        if (IS_KLV_KEY(essence_container_ul, mxf_encrypted_essence_container)) {
            av_log(mxf->fc, AV_LOG_INFO, "broken encrypted mxf file\n");
            for (k = 0; k < mxf->metadata_sets_count; k++) {
                MXFMetadataSet *metadata = mxf->metadata_sets[k];
                if (metadata->type == CryptoContext) {
                    essence_container_ul = &((MXFCryptoContext *)metadata)->source_container_ul;
                    break;
                }
            }
        }

        /* TODO: drop PictureEssenceCoding and SoundEssenceCompression, only check EssenceContainer */
        codec_ul = mxf_get_codec_ul(ff_mxf_codec_uls, &descriptor->essence_codec_ul);
        st->codec->codec_id = (enum AVCodecID)codec_ul->id;
        av_log(mxf->fc, AV_LOG_VERBOSE, "%s: Universal Label: ",
               avcodec_get_name(st->codec->codec_id));
        for (k = 0; k < 16; k++) {
            av_log(mxf->fc, AV_LOG_VERBOSE, "%.2x",
                   descriptor->essence_codec_ul[k]);
            if (!(k+1 & 19) || k == 5)
                av_log(mxf->fc, AV_LOG_VERBOSE, ".");
        }
        av_log(mxf->fc, AV_LOG_VERBOSE, "\n");

        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            source_track->intra_only = mxf_is_intra_only(descriptor);
            container_ul = mxf_get_codec_ul(mxf_picture_essence_container_uls, essence_container_ul);
            if (st->codec->codec_id == AV_CODEC_ID_NONE)
                st->codec->codec_id = container_ul->id;
            st->codec->width = descriptor->width;
            st->codec->height = descriptor->height; /* Field height, not frame height */
            switch (descriptor->frame_layout) {
                case SegmentedFrame:
                    /* This one is a weird layout I don't fully understand. */
                    av_log(mxf->fc, AV_LOG_INFO, "SegmentedFrame layout isn't currently supported\n");
                    break;
                case FullFrame:
                    st->codec->field_order = AV_FIELD_PROGRESSIVE;
                    break;
                case OneField:
                    /* Every other line is stored and needs to be duplicated. */
                    av_log(mxf->fc, AV_LOG_INFO, "OneField frame layout isn't currently supported\n");
                    break; /* The correct thing to do here is fall through, but by breaking we might be
                              able to decode some streams at half the vertical resolution, rather than not al all.
                              It's also for compatibility with the old behavior. */
                case MixedFields:
                    break;
                case SeparateFields:
                    switch (descriptor->field_dominance) {
                    case MXF_TFF:
                        st->codec->field_order = AV_FIELD_TT;
                        break;
                    case MXF_BFF:
                        st->codec->field_order = AV_FIELD_BB;
                        break;
                    default:
                        avpriv_request_sample(mxf->fc,
                                              "Field dominance %d support",
                                              descriptor->field_dominance);
                        break;
                    }
                    /* Turn field height into frame height. */
                    st->codec->height *= 2;
                    break;
                default:
                    av_log(mxf->fc, AV_LOG_INFO, "Unknown frame layout type: %d\n", descriptor->frame_layout);
            }
            if (st->codec->codec_id == AV_CODEC_ID_RAWVIDEO) {
                st->codec->pix_fmt = descriptor->pix_fmt;
                if (st->codec->pix_fmt == AV_PIX_FMT_NONE) {
                    pix_fmt_ul = mxf_get_codec_ul(ff_mxf_pixel_format_uls,
                                                  &descriptor->essence_codec_ul);
                    st->codec->pix_fmt = (enum AVPixelFormat)pix_fmt_ul->id;
                    if (st->codec->pix_fmt == AV_PIX_FMT_NONE) {
                        /* support files created before RP224v10 by defaulting to UYVY422
                           if subsampling is 4:2:2 and component depth is 8-bit */
                        if (descriptor->horiz_subsampling == 2 &&
                            descriptor->vert_subsampling == 1 &&
                            descriptor->component_depth == 8) {
                            st->codec->pix_fmt = AV_PIX_FMT_UYVY422;
                        }
                    }
                }
            }
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
        } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            container_ul = mxf_get_codec_ul(mxf_sound_essence_container_uls, essence_container_ul);
            /* Only overwrite existing codec ID if it is unset or A-law, which is the default according to SMPTE RP 224. */
            if (st->codec->codec_id == AV_CODEC_ID_NONE || (st->codec->codec_id == AV_CODEC_ID_PCM_ALAW && (enum AVCodecID)container_ul->id != AV_CODEC_ID_NONE))
                st->codec->codec_id = (enum AVCodecID)container_ul->id;
            st->codec->channels = descriptor->channels;
            st->codec->bits_per_coded_sample = descriptor->bits_per_sample;

            if (descriptor->sample_rate.den > 0) {
                st->codec->sample_rate = descriptor->sample_rate.num / descriptor->sample_rate.den;
                avpriv_set_pts_info(st, 64, descriptor->sample_rate.den, descriptor->sample_rate.num);
            } else {
                av_log(mxf->fc, AV_LOG_WARNING, "invalid sample rate (%d/%d) "
                       "found for stream #%d, time base forced to 1/48000\n",
                       descriptor->sample_rate.num, descriptor->sample_rate.den,
                       st->index);
                avpriv_set_pts_info(st, 64, 1, 48000);
            }

            /* if duration is set, rescale it from EditRate to SampleRate */
            if (st->duration != AV_NOPTS_VALUE)
                st->duration = av_rescale_q(st->duration,
                                            av_inv_q(material_track->edit_rate),
                                            st->time_base);

            /* TODO: implement AV_CODEC_ID_RAWAUDIO */
            if (st->codec->codec_id == AV_CODEC_ID_PCM_S16LE) {
                if (descriptor->bits_per_sample > 16 && descriptor->bits_per_sample <= 24)
                    st->codec->codec_id = AV_CODEC_ID_PCM_S24LE;
                else if (descriptor->bits_per_sample == 32)
                    st->codec->codec_id = AV_CODEC_ID_PCM_S32LE;
            } else if (st->codec->codec_id == AV_CODEC_ID_PCM_S16BE) {
                if (descriptor->bits_per_sample > 16 && descriptor->bits_per_sample <= 24)
                    st->codec->codec_id = AV_CODEC_ID_PCM_S24BE;
                else if (descriptor->bits_per_sample == 32)
                    st->codec->codec_id = AV_CODEC_ID_PCM_S32BE;
            } else if (st->codec->codec_id == AV_CODEC_ID_MP2) {
                st->need_parsing = AVSTREAM_PARSE_FULL;
            }
        }
        if (descriptor->extradata) {
            if (!ff_alloc_extradata(st->codec, descriptor->extradata_size)) {
                memcpy(st->codec->extradata, descriptor->extradata, descriptor->extradata_size);
            }
        } else if (st->codec->codec_id == AV_CODEC_ID_H264) {
            ret = ff_generate_avci_extradata(st);
            if (ret < 0)
                return ret;
        }
        if (st->codec->codec_type != AVMEDIA_TYPE_DATA && (*essence_container_ul)[15] > 0x01) {
            /* TODO: decode timestamps */
            st->need_parsing = AVSTREAM_PARSE_TIMESTAMPS;
        }
    }

    ret = 0;
fail_and_free:
    return ret;
}

static int mxf_read_utf16_string(AVIOContext *pb, int size, char** str)
{
    int ret;
    size_t buf_size;

    if (size < 0)
        return AVERROR(EINVAL);

    buf_size = size + size / 2 + 1;
    *str = av_malloc(buf_size);
    if (!*str)
        return AVERROR(ENOMEM);

    if ((ret = avio_get_str16be(pb, size, *str, buf_size)) < 0) {
        av_freep(str);
        return ret;
    }

    return ret;
}

static int mxf_uid_to_str(UID uid, char **str)
{
    int i;
    char *p;
    p = *str = av_mallocz(sizeof(UID) * 2 + 4 + 1);
    if (!p)
        return AVERROR(ENOMEM);
    for (i = 0; i < sizeof(UID); i++) {
        snprintf(p, 2 + 1, "%.2x", uid[i]);
        p += 2;
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            snprintf(p, 1 + 1, "-");
            p++;
        }
    }
    return 0;
}

static int mxf_timestamp_to_str(uint64_t timestamp, char **str)
{
    struct tm time = { 0 };
    time.tm_year = (timestamp >> 48) - 1900;
    time.tm_mon  = (timestamp >> 40 & 0xFF) - 1;
    time.tm_mday = (timestamp >> 32 & 0xFF);
    time.tm_hour = (timestamp >> 24 & 0xFF);
    time.tm_min  = (timestamp >> 16 & 0xFF);
    time.tm_sec  = (timestamp >> 8  & 0xFF);

    /* msvcrt versions of strftime calls the invalid parameter handler
     * (aborting the process if one isn't set) if the parameters are out
     * of range. */
    time.tm_mon  = av_clip(time.tm_mon,  0, 11);
    time.tm_mday = av_clip(time.tm_mday, 1, 31);
    time.tm_hour = av_clip(time.tm_hour, 0, 23);
    time.tm_min  = av_clip(time.tm_min,  0, 59);
    time.tm_sec  = av_clip(time.tm_sec,  0, 59);

    *str = av_mallocz(32);
    if (!*str)
        return AVERROR(ENOMEM);
    strftime(*str, 32, "%Y-%m-%d %H:%M:%S", &time);

    return 0;
}

#define SET_STR_METADATA(pb, name, str) do { \
    if ((ret = mxf_read_utf16_string(pb, size, &str)) < 0) \
        return ret; \
    av_dict_set(&s->metadata, name, str, AV_DICT_DONT_STRDUP_VAL); \
} while (0)

#define SET_UID_METADATA(pb, name, var, str) do { \
    avio_read(pb, var, 16); \
    if ((ret = mxf_uid_to_str(var, &str)) < 0) \
        return ret; \
    av_dict_set(&s->metadata, name, str, AV_DICT_DONT_STRDUP_VAL); \
} while (0)

#define SET_TS_METADATA(pb, name, var, str) do { \
    var = avio_rb64(pb); \
    if ((ret = mxf_timestamp_to_str(var, &str)) < 0) \
        return ret; \
    av_dict_set(&s->metadata, name, str, AV_DICT_DONT_STRDUP_VAL); \
} while (0)

static int mxf_read_identification_metadata(void *arg, AVIOContext *pb, int tag, int size, UID _uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    AVFormatContext *s = mxf->fc;
    int ret;
    UID uid = { 0 };
    char *str = NULL;
    uint64_t ts;
    switch (tag) {
    case 0x3C01:
        SET_STR_METADATA(pb, "company_name", str);
        break;
    case 0x3C02:
        SET_STR_METADATA(pb, "product_name", str);
        break;
    case 0x3C04:
        SET_STR_METADATA(pb, "product_version", str);
        break;
    case 0x3C05:
        SET_UID_METADATA(pb, "product_uid", uid, str);
        break;
    case 0x3C06:
        SET_TS_METADATA(pb, "modification_date", ts, str);
        break;
    case 0x3C08:
        SET_STR_METADATA(pb, "application_platform", str);
        break;
    case 0x3C09:
        SET_UID_METADATA(pb, "generation_uid", uid, str);
        break;
    case 0x3C0A:
        SET_UID_METADATA(pb, "uid", uid, str);
        break;
    }
    return 0;
}

static const MXFMetadataReadTableEntry mxf_metadata_read_table[] = {
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x05,0x01,0x00 }, mxf_read_primer_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x03,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x01,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x03,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x04,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x30,0x00 }, mxf_read_identification_metadata },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x18,0x00 }, mxf_read_content_storage, 0, AnyType },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x37,0x00 }, mxf_read_source_package, sizeof(MXFPackage), SourcePackage },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x36,0x00 }, mxf_read_material_package, sizeof(MXFPackage), MaterialPackage },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x0F,0x00 }, mxf_read_sequence, sizeof(MXFSequence), Sequence },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x11,0x00 }, mxf_read_source_clip, sizeof(MXFStructuralComponent), SourceClip },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x44,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), MultipleDescriptor },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x42,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Generic Sound */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x28,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* CDCI */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x29,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* RGBA */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* MPEG 2 Video */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Wave */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* AES3 */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3A,0x00 }, mxf_read_track, sizeof(MXFTrack), Track }, /* Static Track */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3B,0x00 }, mxf_read_track, sizeof(MXFTrack), Track }, /* Generic Track */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x14,0x00 }, mxf_read_timecode_component, sizeof(MXFTimecodeComponent), TimecodeComponent },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x04,0x01,0x02,0x02,0x00,0x00 }, mxf_read_cryptographic_context, sizeof(MXFCryptoContext), CryptoContext },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x10,0x01,0x00 }, mxf_read_index_table_segment, sizeof(MXFIndexTableSegment), IndexTableSegment },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, NULL, 0, AnyType },
};

static int mxf_read_local_tags(MXFContext *mxf, KLVPacket *klv, MXFMetadataReadFunc *read_child, int ctx_size, enum MXFMetadataSetType type)
{
    AVIOContext *pb = mxf->fc->pb;
    MXFMetadataSet *ctx = ctx_size ? av_mallocz(ctx_size) : mxf;
    uint64_t klv_end = avio_tell(pb) + klv->length;

    if (!ctx)
        return AVERROR(ENOMEM);
    while (avio_tell(pb) + 4 < klv_end && !url_feof(pb)) {
        int ret;
        int tag = avio_rb16(pb);
        int size = avio_rb16(pb); /* KLV specified by 0x53 */
        uint64_t next = avio_tell(pb) + size;
        UID uid = {0};

        av_dlog(mxf->fc, "local tag %#04x size %d\n", tag, size);
        if (!size) { /* ignore empty tag, needed for some files with empty UMID tag */
            av_log(mxf->fc, AV_LOG_ERROR, "local tag %#04x with 0 size\n", tag);
            continue;
        }
        if (tag > 0x7FFF) { /* dynamic tag */
            int i;
            for (i = 0; i < mxf->local_tags_count; i++) {
                int local_tag = AV_RB16(mxf->local_tags+i*18);
                if (local_tag == tag) {
                    memcpy(uid, mxf->local_tags+i*18+2, 16);
                    av_dlog(mxf->fc, "local tag %#04x\n", local_tag);
                    PRINT_KEY(mxf->fc, "uid", uid);
                }
            }
        }
        if (ctx_size && tag == 0x3C0A)
            avio_read(pb, ctx->uid, 16);
        else if ((ret = read_child(ctx, pb, tag, size, uid, -1)) < 0)
            return ret;

        /* Accept the 64k local set limit being exceeded (Avid). Don't accept
         * it extending past the end of the KLV though (zzuf5.mxf). */
        if (avio_tell(pb) > klv_end) {
            if (ctx_size)
                av_free(ctx);

            av_log(mxf->fc, AV_LOG_ERROR,
                   "local tag %#04x extends past end of local set @ %#"PRIx64"\n",
                   tag, klv->offset);
            return AVERROR_INVALIDDATA;
        } else if (avio_tell(pb) <= next)   /* only seek forward, else this can loop for a long time */
            avio_seek(pb, next, SEEK_SET);
    }
    if (ctx_size) ctx->type = type;
    return ctx_size ? mxf_add_metadata_set(mxf, ctx) : 0;
}

/**
 * Seeks to the previous partition, if possible
 * @return <= 0 if we should stop parsing, > 0 if we should keep going
 */
static int mxf_seek_to_previous_partition(MXFContext *mxf)
{
    AVIOContext *pb = mxf->fc->pb;

    if (!mxf->current_partition ||
        mxf->run_in + mxf->current_partition->previous_partition <= mxf->last_forward_tell)
        return 0;   /* we've parsed all partitions */

    /* seek to previous partition */
    avio_seek(pb, mxf->run_in + mxf->current_partition->previous_partition, SEEK_SET);
    mxf->current_partition = NULL;

    av_dlog(mxf->fc, "seeking to previous partition\n");

    return 1;
}

/**
 * Called when essence is encountered
 * @return <= 0 if we should stop parsing, > 0 if we should keep going
 */
static int mxf_parse_handle_essence(MXFContext *mxf)
{
    AVIOContext *pb = mxf->fc->pb;
    int64_t ret;

    if (mxf->parsing_backward) {
        return mxf_seek_to_previous_partition(mxf);
    } else {
        uint64_t offset = mxf->footer_partition ? mxf->footer_partition
                                                : mxf->last_partition;

        if (!offset) {
            av_dlog(mxf->fc, "no last partition\n");
            return 0;
        }

        av_dlog(mxf->fc, "seeking to last partition\n");

        /* remember where we were so we don't end up seeking further back than this */
        mxf->last_forward_tell = avio_tell(pb);

        if (!pb->seekable) {
            av_log(mxf->fc, AV_LOG_INFO, "file is not seekable - not parsing last partition\n");
            return -1;
        }

        /* seek to last partition and parse backward */
        if ((ret = avio_seek(pb, mxf->run_in + offset, SEEK_SET)) < 0) {
            av_log(mxf->fc, AV_LOG_ERROR,
                   "failed to seek to last partition @ 0x%" PRIx64
                   " (%"PRId64") - partial file?\n",
                   mxf->run_in + offset, ret);
            return ret;
        }

        mxf->current_partition = NULL;
        mxf->parsing_backward = 1;
    }

    return 1;
}

/**
 * Called when the next partition or EOF is encountered
 * @return <= 0 if we should stop parsing, > 0 if we should keep going
 */
static int mxf_parse_handle_partition_or_eof(MXFContext *mxf)
{
    return mxf->parsing_backward ? mxf_seek_to_previous_partition(mxf) : 1;
}

/**
 * Figures out the proper offset and length of the essence container in each partition
 */
static void mxf_compute_essence_containers(MXFContext *mxf)
{
    int x;

    /* everything is already correct */
    if (mxf->op == OPAtom)
        return;

    for (x = 0; x < mxf->partitions_count; x++) {
        MXFPartition *p = &mxf->partitions[x];

        if (!p->body_sid)
            continue;       /* BodySID == 0 -> no essence */

        if (x >= mxf->partitions_count - 1)
            break;          /* last partition - can't compute length (and we don't need to) */

        /* essence container spans to the next partition */
        p->essence_length = mxf->partitions[x+1].this_partition - p->essence_offset;

        if (p->essence_length < 0) {
            /* next ThisPartition < essence_offset */
            p->essence_length = 0;
            av_log(mxf->fc, AV_LOG_ERROR,
                   "partition %i: bad ThisPartition = %"PRIX64"\n",
                   x+1, mxf->partitions[x+1].this_partition);
        }
    }
}

static int64_t round_to_kag(int64_t position, int kag_size)
{
    /* TODO: account for run-in? the spec isn't clear whether KAG should account for it */
    /* NOTE: kag_size may be any integer between 1 - 2^10 */
    int64_t ret = (position / kag_size) * kag_size;
    return ret == position ? ret : ret + kag_size;
}

static int is_pcm(enum AVCodecID codec_id)
{
    /* we only care about "normal" PCM codecs until we get samples */
    return codec_id >= AV_CODEC_ID_PCM_S16LE && codec_id < AV_CODEC_ID_PCM_S24DAUD;
}

/**
 * Deal with the case where for some audio atoms EditUnitByteCount is
 * very small (2, 4..). In those cases we should read more than one
 * sample per call to mxf_read_packet().
 */
static void mxf_handle_small_eubc(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;

    /* assuming non-OPAtom == frame wrapped
     * no sane writer would wrap 2 byte PCM packets with 20 byte headers.. */
    if (mxf->op != OPAtom)
        return;

    /* expect PCM with exactly one index table segment and a small (< 32) EUBC */
    if (s->nb_streams != 1                                     ||
        s->streams[0]->codec->codec_type != AVMEDIA_TYPE_AUDIO ||
        !is_pcm(s->streams[0]->codec->codec_id)                ||
        mxf->nb_index_tables != 1                              ||
        mxf->index_tables[0].nb_segments != 1                  ||
        mxf->index_tables[0].segments[0]->edit_unit_byte_count >= 32)
        return;

    /* arbitrarily default to 48 kHz PAL audio frame size */
    /* TODO: We could compute this from the ratio between the audio
     *       and video edit rates for 48 kHz NTSC we could use the
     *       1802-1802-1802-1802-1801 pattern. */
    mxf->edit_units_per_packet = 1920;
}

static void mxf_read_random_index_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    uint32_t length;
    int64_t file_size;
    KLVPacket klv;

    if (!s->pb->seekable)
        return;

    file_size = avio_size(s->pb);
    avio_seek(s->pb, file_size - 4, SEEK_SET);
    length = avio_rb32(s->pb);
    if (length <= 32 || length >= FFMIN(file_size, INT_MAX))
        goto end;
    avio_seek(s->pb, file_size - length, SEEK_SET);
    if (klv_read_packet(&klv, s->pb) < 0 ||
        !IS_KLV_KEY(klv.key, mxf_random_index_pack_key) ||
        klv.length != length - 20)
        goto end;

    avio_skip(s->pb, klv.length - 12);
    mxf->last_partition = avio_rb64(s->pb);

end:
    avio_seek(s->pb, mxf->run_in, SEEK_SET);
}

static int mxf_read_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    KLVPacket klv;
    int64_t essence_offset = 0;
    int64_t last_pos = -1;
    uint64_t last_pos_index = 1;
    int ret;

    mxf->last_forward_tell = INT64_MAX;
    mxf->edit_units_per_packet = 1;

    if (!mxf_read_sync(s->pb, mxf_header_partition_pack_key, 14)) {
        av_log(s, AV_LOG_ERROR, "could not find header partition pack key\n");
        return AVERROR_INVALIDDATA;
    }
    avio_seek(s->pb, -14, SEEK_CUR);
    mxf->fc = s;
    mxf->run_in = avio_tell(s->pb);

    mxf_read_random_index_pack(s);

    while (!url_feof(s->pb)) {
        const MXFMetadataReadTableEntry *metadata;
        if (avio_tell(s->pb) == last_pos) {
            av_log(mxf->fc, AV_LOG_ERROR, "MXF structure loop detected\n");
            return AVERROR_INVALIDDATA;
        }
        if ((1ULL<<61) % last_pos_index++ == 0)
            last_pos = avio_tell(s->pb);
        if (klv_read_packet(&klv, s->pb) < 0) {
            /* EOF - seek to previous partition or stop */
            if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
                break;
            else
                continue;
        }

        PRINT_KEY(s, "read header", klv.key);
        av_dlog(s, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
        if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key) ||
            IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_avid_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_system_item_key)) {

            if (!mxf->current_partition) {
                av_log(mxf->fc, AV_LOG_ERROR, "found essence prior to first PartitionPack\n");
                return AVERROR_INVALIDDATA;
            }

            if (!mxf->current_partition->essence_offset) {
                /* for OP1a we compute essence_offset
                 * for OPAtom we point essence_offset after the KL (usually op1a_essence_offset + 20 or 25)
                 * TODO: for OP1a we could eliminate this entire if statement, always stopping parsing at op1a_essence_offset
                 *       for OPAtom we still need the actual essence_offset though (the KL's length can vary)
                 */
                int64_t op1a_essence_offset =
                    round_to_kag(mxf->current_partition->this_partition +
                                 mxf->current_partition->pack_length,       mxf->current_partition->kag_size) +
                    round_to_kag(mxf->current_partition->header_byte_count, mxf->current_partition->kag_size) +
                    round_to_kag(mxf->current_partition->index_byte_count,  mxf->current_partition->kag_size);

                if (mxf->op == OPAtom) {
                    /* point essence_offset to the actual data
                    * OPAtom has all the essence in one big KLV
                    */
                    mxf->current_partition->essence_offset = avio_tell(s->pb);
                    mxf->current_partition->essence_length = klv.length;
                } else {
                    /* NOTE: op1a_essence_offset may be less than to klv.offset (C0023S01.mxf)  */
                    mxf->current_partition->essence_offset = op1a_essence_offset;
                }
            }

            if (!essence_offset)
                essence_offset = klv.offset;

            /* seek to footer, previous partition or stop */
            if (mxf_parse_handle_essence(mxf) <= 0)
                break;
            continue;
        } else if (!memcmp(klv.key, mxf_header_partition_pack_key, 13) &&
                   klv.key[13] >= 2 && klv.key[13] <= 4 && mxf->current_partition) {
            /* next partition pack - keep going, seek to previous partition or stop */
            if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
                break;
            else if (mxf->parsing_backward)
                continue;
            /* we're still parsing forward. proceed to parsing this partition pack */
        }

        for (metadata = mxf_metadata_read_table; metadata->read; metadata++) {
            if (IS_KLV_KEY(klv.key, metadata->key)) {
                int res;
                if (klv.key[5] == 0x53) {
                    res = mxf_read_local_tags(mxf, &klv, metadata->read, metadata->ctx_size, metadata->type);
                } else {
                    uint64_t next = avio_tell(s->pb) + klv.length;
                    res = metadata->read(mxf, s->pb, 0, klv.length, klv.key, klv.offset);

                    /* only seek forward, else this can loop for a long time */
                    if (avio_tell(s->pb) > next) {
                        av_log(s, AV_LOG_ERROR, "read past end of KLV @ %#"PRIx64"\n",
                               klv.offset);
                        return AVERROR_INVALIDDATA;
                    }

                    avio_seek(s->pb, next, SEEK_SET);
                }
                if (res < 0) {
                    av_log(s, AV_LOG_ERROR, "error reading header metadata\n");
                    return res;
                }
                break;
            }
        }
        if (!metadata->read)
            avio_skip(s->pb, klv.length);
    }
    /* FIXME avoid seek */
    if (!essence_offset)  {
        av_log(s, AV_LOG_ERROR, "no essence\n");
        return AVERROR_INVALIDDATA;
    }
    avio_seek(s->pb, essence_offset, SEEK_SET);

    mxf_compute_essence_containers(mxf);

    /* we need to do this before computing the index tables
     * to be able to fill in zero IndexDurations with st->duration */
    if ((ret = mxf_parse_structural_metadata(mxf)) < 0)
        goto fail;

    if ((ret = mxf_compute_index_tables(mxf)) < 0)
        goto fail;

    if (mxf->nb_index_tables > 1) {
        /* TODO: look up which IndexSID to use via EssenceContainerData */
        av_log(mxf->fc, AV_LOG_INFO, "got %i index tables - only the first one (IndexSID %i) will be used\n",
               mxf->nb_index_tables, mxf->index_tables[0].index_sid);
    } else if (mxf->nb_index_tables == 0 && mxf->op == OPAtom) {
        av_log(mxf->fc, AV_LOG_ERROR, "cannot demux OPAtom without an index\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    mxf_handle_small_eubc(s);

    return 0;
fail:
    mxf_read_close(s);

    return ret;
}

/**
 * Sets mxf->current_edit_unit based on what offset we're currently at.
 * @return next_ofs if OK, <0 on error
 */
static int64_t mxf_set_current_edit_unit(MXFContext *mxf, int64_t current_offset)
{
    int64_t last_ofs = -1, next_ofs = -1;
    MXFIndexTable *t = &mxf->index_tables[0];

    /* this is called from the OP1a demuxing logic, which means there
     * may be no index tables */
    if (mxf->nb_index_tables <= 0)
        return -1;

    /* find mxf->current_edit_unit so that the next edit unit starts ahead of current_offset */
    while (mxf->current_edit_unit >= 0) {
        if (mxf_edit_unit_absolute_offset(mxf, t, mxf->current_edit_unit + 1, NULL, &next_ofs, 0) < 0)
            return -1;

        if (next_ofs <= last_ofs) {
            /* large next_ofs didn't change or current_edit_unit wrapped
             * around this fixes the infinite loop on zzuf3.mxf */
            av_log(mxf->fc, AV_LOG_ERROR,
                   "next_ofs didn't change. not deriving packet timestamps\n");
            return -1;
        }

        if (next_ofs > current_offset)
            break;

        last_ofs = next_ofs;
        mxf->current_edit_unit++;
    }

    /* not checking mxf->current_edit_unit >= t->nb_ptses here since CBR files may lack IndexEntryArrays */
    if (mxf->current_edit_unit < 0)
        return -1;

    return next_ofs;
}

static int mxf_compute_sample_count(MXFContext *mxf, int stream_index,
                                    uint64_t *sample_count)
{
    int i, total = 0, size = 0;
    AVStream *st = mxf->fc->streams[stream_index];
    MXFTrack *track = st->priv_data;
    AVRational time_base = av_inv_q(track->edit_rate);
    AVRational sample_rate = av_inv_q(st->time_base);
    const MXFSamplesPerFrame *spf = NULL;

    if ((sample_rate.num / sample_rate.den) == 48000)
        spf = ff_mxf_get_samples_per_frame(mxf->fc, time_base);
    if (!spf) {
        int remainder = (sample_rate.num * time_base.num) %
                        (time_base.den * sample_rate.den);
        *sample_count = av_q2d(av_mul_q((AVRational){mxf->current_edit_unit, 1},
                                        av_mul_q(sample_rate, time_base)));
        if (remainder)
            av_log(mxf->fc, AV_LOG_WARNING,
                   "seeking detected on stream #%d with time base (%d/%d) and "
                   "sample rate (%d/%d), audio pts won't be accurate.\n",
                   stream_index, time_base.num, time_base.den,
                   sample_rate.num, sample_rate.den);
        return 0;
    }

    while (spf->samples_per_frame[size]) {
        total += spf->samples_per_frame[size];
        size++;
    }

    av_assert2(size);

    *sample_count = (mxf->current_edit_unit / size) * (uint64_t)total;
    for (i = 0; i < mxf->current_edit_unit % size; i++) {
        *sample_count += spf->samples_per_frame[i];
    }

    return 0;
}

static int mxf_set_audio_pts(MXFContext *mxf, AVCodecContext *codec,
                             AVPacket *pkt)
{
    MXFTrack *track = mxf->fc->streams[pkt->stream_index]->priv_data;
    int64_t bits_per_sample = av_get_bits_per_sample(codec->codec_id);

    pkt->pts = track->sample_count;

    if (   codec->channels <= 0
        || bits_per_sample <= 0
        || codec->channels * (int64_t)bits_per_sample < 8)
        return AVERROR(EINVAL);
    track->sample_count += pkt->size / (codec->channels * (int64_t)bits_per_sample / 8);
    return 0;
}

static int mxf_read_packet_old(AVFormatContext *s, AVPacket *pkt)
{
    KLVPacket klv;
    MXFContext *mxf = s->priv_data;
    int ret;

    while ((ret = klv_read_packet(&klv, s->pb)) == 0) {
        PRINT_KEY(s, "read packet", klv.key);
        av_dlog(s, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
        if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key)) {
            ret = mxf_decrypt_triplet(s, pkt, &klv);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "invalid encoded triplet\n");
                return ret;
            }
            return 0;
        }
        if (IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_avid_essence_element_key)) {
            int index = mxf_get_stream_index(s, &klv);
            int64_t next_ofs, next_klv;
            AVStream *st;
            MXFTrack *track;
            AVCodecContext *codec;

            if (index < 0) {
                av_log(s, AV_LOG_ERROR,
                       "error getting stream index %"PRIu32"\n",
                       AV_RB32(klv.key + 12));
                goto skip;
            }

            st = s->streams[index];
            track = st->priv_data;

            if (s->streams[index]->discard == AVDISCARD_ALL)
                goto skip;

            next_klv = avio_tell(s->pb) + klv.length;
            next_ofs = mxf_set_current_edit_unit(mxf, klv.offset);

            if (next_ofs >= 0 && next_klv > next_ofs) {
                /* if this check is hit then it's possible OPAtom was treated as OP1a
                 * truncate the packet since it's probably very large (>2 GiB is common) */
                avpriv_request_sample(s,
                                      "OPAtom misinterpreted as OP1a?"
                                      "KLV for edit unit %i extending into "
                                      "next edit unit",
                                      mxf->current_edit_unit);
                klv.length = next_ofs - avio_tell(s->pb);
            }

            /* check for 8 channels AES3 element */
            if (klv.key[12] == 0x06 && klv.key[13] == 0x01 && klv.key[14] == 0x10) {
                ret = mxf_get_d10_aes3_packet(s->pb, s->streams[index],
                                              pkt, klv.length);
                if (ret < 0) {
                    av_log(s, AV_LOG_ERROR, "error reading D-10 aes3 frame\n");
                    return ret;
                }
            } else {
                ret = av_get_packet(s->pb, pkt, klv.length);
                if (ret < 0)
                    return ret;
            }
            pkt->stream_index = index;
            pkt->pos = klv.offset;

            codec = s->streams[index]->codec;

            if (codec->codec_type == AVMEDIA_TYPE_VIDEO && next_ofs >= 0) {
                /* mxf->current_edit_unit good - see if we have an
                 * index table to derive timestamps from */
                MXFIndexTable *t = &mxf->index_tables[0];

                if (mxf->nb_index_tables >= 1 && mxf->current_edit_unit < t->nb_ptses) {
                    pkt->dts = mxf->current_edit_unit + t->first_dts;
                    pkt->pts = t->ptses[mxf->current_edit_unit];
                } else if (track->intra_only) {
                    /* intra-only -> PTS = EditUnit.
                     * let utils.c figure out DTS since it can be < PTS if low_delay = 0 (Sony IMX30) */
                    pkt->pts = mxf->current_edit_unit;
                }
            } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                ret = mxf_set_audio_pts(mxf, codec, pkt);
                if (ret < 0)
                    return ret;
            }

            /* seek for truncated packets */
            avio_seek(s->pb, next_klv, SEEK_SET);

            return 0;
        } else
        skip:
            avio_skip(s->pb, klv.length);
    }
    return url_feof(s->pb) ? AVERROR_EOF : ret;
}

static int mxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    int ret, size;
    int64_t ret64, pos, next_pos;
    AVStream *st;
    MXFIndexTable *t;
    int edit_units;

    if (mxf->op != OPAtom)
        return mxf_read_packet_old(s, pkt);

    /* OPAtom - clip wrapped demuxing */
    /* NOTE: mxf_read_header() makes sure nb_index_tables > 0 for OPAtom */
    st = s->streams[0];
    t = &mxf->index_tables[0];

    if (mxf->current_edit_unit >= st->duration)
        return AVERROR_EOF;

    edit_units = FFMIN(mxf->edit_units_per_packet, st->duration - mxf->current_edit_unit);

    if ((ret = mxf_edit_unit_absolute_offset(mxf, t, mxf->current_edit_unit, NULL, &pos, 1)) < 0)
        return ret;

    /* compute size by finding the next edit unit or the end of the essence container
     * not pretty, but it works */
    if ((ret = mxf_edit_unit_absolute_offset(mxf, t, mxf->current_edit_unit + edit_units, NULL, &next_pos, 0)) < 0 &&
        (next_pos = mxf_essence_container_end(mxf, t->body_sid)) <= 0) {
        av_log(s, AV_LOG_ERROR, "unable to compute the size of the last packet\n");
        return AVERROR_INVALIDDATA;
    }

    if ((size = next_pos - pos) <= 0) {
        av_log(s, AV_LOG_ERROR, "bad size: %i\n", size);
        return AVERROR_INVALIDDATA;
    }

    if ((ret64 = avio_seek(s->pb, pos, SEEK_SET)) < 0)
        return ret64;

    if ((size = av_get_packet(s->pb, pkt, size)) < 0)
        return size;

    pkt->stream_index = 0;

    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && t->ptses &&
        mxf->current_edit_unit >= 0 && mxf->current_edit_unit < t->nb_ptses) {
        pkt->dts = mxf->current_edit_unit + t->first_dts;
        pkt->pts = t->ptses[mxf->current_edit_unit];
    } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        int ret = mxf_set_audio_pts(mxf, st->codec, pkt);
        if (ret < 0)
            return ret;
    }

    mxf->current_edit_unit += edit_units;

    return 0;
}

static int mxf_read_close(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    MXFIndexTableSegment *seg;
    int i;

    av_freep(&mxf->packages_refs);

    for (i = 0; i < s->nb_streams; i++)
        s->streams[i]->priv_data = NULL;

    for (i = 0; i < mxf->metadata_sets_count; i++) {
        switch (mxf->metadata_sets[i]->type) {
        case Descriptor:
            av_freep(&((MXFDescriptor *)mxf->metadata_sets[i])->extradata);
            break;
        case MultipleDescriptor:
            av_freep(&((MXFDescriptor *)mxf->metadata_sets[i])->sub_descriptors_refs);
            break;
        case Sequence:
            av_freep(&((MXFSequence *)mxf->metadata_sets[i])->structural_components_refs);
            break;
        case SourcePackage:
        case MaterialPackage:
            av_freep(&((MXFPackage *)mxf->metadata_sets[i])->tracks_refs);
            break;
        case IndexTableSegment:
            seg = (MXFIndexTableSegment *)mxf->metadata_sets[i];
            av_freep(&seg->temporal_offset_entries);
            av_freep(&seg->flag_entries);
            av_freep(&seg->stream_offset_entries);
            break;
        default:
            break;
        }
        av_freep(&mxf->metadata_sets[i]);
    }
    av_freep(&mxf->partitions);
    av_freep(&mxf->metadata_sets);
    av_freep(&mxf->aesc);
    av_freep(&mxf->local_tags);

    if (mxf->index_tables) {
        for (i = 0; i < mxf->nb_index_tables; i++) {
            av_freep(&mxf->index_tables[i].segments);
            av_freep(&mxf->index_tables[i].ptses);
            av_freep(&mxf->index_tables[i].fake_index);
        }
    }
    av_freep(&mxf->index_tables);

    return 0;
}

static int mxf_probe(AVProbeData *p) {
    const uint8_t *bufp = p->buf;
    const uint8_t *end = p->buf + p->buf_size;

    if (p->buf_size < sizeof(mxf_header_partition_pack_key))
        return 0;

    /* Must skip Run-In Sequence and search for MXF header partition pack key SMPTE 377M 5.5 */
    end -= sizeof(mxf_header_partition_pack_key);

    for (; bufp < end;) {
        if (!((bufp[13] - 1) & 0xF2)){
            if (AV_RN32(bufp   ) == AV_RN32(mxf_header_partition_pack_key   ) &&
                AV_RN32(bufp+ 4) == AV_RN32(mxf_header_partition_pack_key+ 4) &&
                AV_RN32(bufp+ 8) == AV_RN32(mxf_header_partition_pack_key+ 8) &&
                AV_RN16(bufp+12) == AV_RN16(mxf_header_partition_pack_key+12))
                return AVPROBE_SCORE_MAX;
            bufp ++;
        } else
            bufp += 10;
    }

    return 0;
}

/* rudimentary byte seek */
/* XXX: use MXF Index */
static int mxf_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    AVStream *st = s->streams[stream_index];
    int64_t seconds;
    MXFContext* mxf = s->priv_data;
    int64_t seekpos;
    int i, ret;
    MXFIndexTable *t;
    MXFTrack *source_track = st->priv_data;

    /* if audio then truncate sample_time to EditRate */
    if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        sample_time = av_rescale_q(sample_time, st->time_base,
                                   av_inv_q(source_track->edit_rate));

    if (mxf->nb_index_tables <= 0) {
    if (!s->bit_rate)
        return AVERROR_INVALIDDATA;
    if (sample_time < 0)
        sample_time = 0;
    seconds = av_rescale(sample_time, st->time_base.num, st->time_base.den);

    seekpos = avio_seek(s->pb, (s->bit_rate * seconds) >> 3, SEEK_SET);
    if (seekpos < 0)
        return seekpos;

    ff_update_cur_dts(s, st, sample_time);
    mxf->current_edit_unit = sample_time;
    } else {
        t = &mxf->index_tables[0];

        /* clamp above zero, else ff_index_search_timestamp() returns negative
         * this also means we allow seeking before the start */
        sample_time = FFMAX(sample_time, 0);

        if (t->fake_index) {
            /* behave as if we have a proper index */
            if ((sample_time = ff_index_search_timestamp(t->fake_index, t->nb_ptses, sample_time, flags)) < 0)
                return sample_time;
        } else {
            /* no IndexEntryArray (one or more CBR segments)
             * make sure we don't seek past the end */
            sample_time = FFMIN(sample_time, source_track->original_duration - 1);
        }

        if ((ret = mxf_edit_unit_absolute_offset(mxf, t, sample_time, &sample_time, &seekpos, 1)) < 0)
            return ret;

        ff_update_cur_dts(s, st, sample_time);
        mxf->current_edit_unit = sample_time;
        avio_seek(s->pb, seekpos, SEEK_SET);
    }

    // Update all tracks sample count
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *cur_st = s->streams[i];
        MXFTrack *cur_track = cur_st->priv_data;
        uint64_t current_sample_count = 0;
        if (cur_st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            ret = mxf_compute_sample_count(mxf, i, &current_sample_count);
            if (ret < 0)
                return ret;

            cur_track->sample_count = current_sample_count;
        }
    }
    return 0;
}

AVInputFormat ff_mxf_demuxer = {
    .name           = "mxf",
    .long_name      = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format)"),
    .priv_data_size = sizeof(MXFContext),
    .read_probe     = mxf_probe,
    .read_header    = mxf_read_header,
    .read_packet    = mxf_read_packet,
    .read_close     = mxf_read_close,
    .read_seek      = mxf_read_seek,
};
