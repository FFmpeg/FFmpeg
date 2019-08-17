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
#include "libavutil/parseutils.h"
#include "libavutil/timecode.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include "mxf.h"

#define MXF_MAX_CHUNK_SIZE (32 << 20)

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

typedef enum {
    UnknownWrapped = 0,
    FrameWrapped,
    ClipWrapped,
} MXFWrappingScheme;

typedef struct MXFPartition {
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
    int64_t pack_ofs;               ///< absolute offset of pack in file, including run-in
    int64_t body_offset;
    KLVPacket first_essence_klv;
} MXFPartition;

typedef struct MXFCryptoContext {
    UID uid;
    enum MXFMetadataSetType type;
    UID source_container_ul;
} MXFCryptoContext;

typedef struct MXFStructuralComponent {
    UID uid;
    enum MXFMetadataSetType type;
    UID source_package_ul;
    UID source_package_uid;
    UID data_definition_ul;
    int64_t duration;
    int64_t start_position;
    int source_track_id;
} MXFStructuralComponent;

typedef struct MXFSequence {
    UID uid;
    enum MXFMetadataSetType type;
    UID data_definition_ul;
    UID *structural_components_refs;
    int structural_components_count;
    int64_t duration;
    uint8_t origin;
} MXFSequence;

typedef struct MXFTrack {
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
    UID input_segment_ref;
} MXFPulldownComponent;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    UID *structural_components_refs;
    int structural_components_count;
    int64_t duration;
} MXFEssenceGroup;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    char *name;
    char *value;
} MXFTaggedValue;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
    MXFSequence *sequence; /* mandatory, and only one */
    UID sequence_ref;
    int track_id;
    char *name;
    uint8_t track_number[4];
    AVRational edit_rate;
    int intra_only;
    uint64_t sample_count;
    int64_t original_duration; /* st->duration in SampleRate/EditRate units */
    int index_sid;
    int body_sid;
    MXFWrappingScheme wrapping;
    int edit_units_per_packet; /* how many edit units to read at a time (PCM, ClipWrapped) */
} MXFTrack;

typedef struct MXFDescriptor {
    UID uid;
    enum MXFMetadataSetType type;
    UID essence_container_ul;
    UID essence_codec_ul;
    UID codec_ul;
    AVRational sample_rate;
    AVRational aspect_ratio;
    int width;
    int height; /* Field height, not frame height */
    int frame_layout; /* See MXFFrameLayout enum */
    int video_line_map[2];
#define MXF_FIELD_DOMINANCE_DEFAULT 0
#define MXF_FIELD_DOMINANCE_FF 1 /* coded first, displayed first */
#define MXF_FIELD_DOMINANCE_FL 2 /* coded first, displayed last */
    int field_dominance;
    int channels;
    int bits_per_sample;
    int64_t duration; /* ContainerDuration optional property */
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

typedef struct MXFIndexTableSegment {
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

typedef struct MXFPackage {
    UID uid;
    enum MXFMetadataSetType type;
    UID package_uid;
    UID package_ul;
    UID *tracks_refs;
    int tracks_count;
    MXFDescriptor *descriptor; /* only one */
    UID descriptor_ref;
    char *name;
    UID *comment_refs;
    int comment_count;
} MXFPackage;

typedef struct MXFEssenceContainerData {
    UID uid;
    enum MXFMetadataSetType type;
    UID package_uid;
    UID package_ul;
    int index_sid;
    int body_sid;
} MXFEssenceContainerData;

typedef struct MXFMetadataSet {
    UID uid;
    enum MXFMetadataSetType type;
} MXFMetadataSet;

/* decoded index table */
typedef struct MXFIndexTable {
    int index_sid;
    int body_sid;
    int nb_ptses;               /* number of PTSes or total duration of index */
    int64_t first_dts;          /* DTS = EditUnit + first_dts */
    int64_t *ptses;             /* maps EditUnit -> PTS */
    int nb_segments;
    MXFIndexTableSegment **segments;    /* sorted by IndexStartPosition */
    AVIndexEntry *fake_index;   /* used for calling ff_index_search_timestamp() */
    int8_t *offsets;            /* temporal offsets for display order to stored order conversion */
} MXFIndexTable;

typedef struct MXFContext {
    const AVClass *class;     /**< Class for private options. */
    MXFPartition *partitions;
    unsigned partitions_count;
    MXFOP op;
    UID *packages_refs;
    int packages_count;
    UID *essence_container_data_refs;
    int essence_container_data_count;
    MXFMetadataSet **metadata_sets;
    int metadata_sets_count;
    AVFormatContext *fc;
    struct AVAES *aesc;
    uint8_t *local_tags;
    int local_tags_count;
    uint64_t footer_partition;
    KLVPacket current_klv_data;
    int run_in;
    MXFPartition *current_partition;
    int parsing_backward;
    int64_t last_forward_tell;
    int last_forward_partition;
    int nb_index_tables;
    MXFIndexTable *index_tables;
    int eia608_extract;
} MXFContext;

/* NOTE: klv_offset is not set (-1) for local keys */
typedef int MXFMetadataReadFunc(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset);

typedef struct MXFMetadataReadTableEntry {
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
static const uint8_t mxf_canopus_essence_element_key[]     = { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x0a,0x0e,0x0f,0x03,0x01 };
static const uint8_t mxf_system_item_key_cp[]              = { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x03,0x01,0x04 };
static const uint8_t mxf_system_item_key_gc[]              = { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x03,0x01,0x14 };
static const uint8_t mxf_klv_key[]                         = { 0x06,0x0e,0x2b,0x34 };
/* complete keys to match */
static const uint8_t mxf_crypto_source_container_ul[]      = { 0x06,0x0e,0x2b,0x34,0x01,0x01,0x01,0x09,0x06,0x01,0x01,0x02,0x02,0x00,0x00,0x00 };
static const uint8_t mxf_encrypted_triplet_key[]           = { 0x06,0x0e,0x2b,0x34,0x02,0x04,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x7e,0x01,0x00 };
static const uint8_t mxf_encrypted_essence_container[]     = { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x0b,0x01,0x00 };
static const uint8_t mxf_random_index_pack_key[]           = { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x11,0x01,0x00 };
static const uint8_t mxf_sony_mpeg4_extradata[]            = { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0e,0x06,0x06,0x02,0x02,0x01,0x00,0x00 };
static const uint8_t mxf_avid_project_name[]               = { 0xa5,0xfb,0x7b,0x25,0xf6,0x15,0x94,0xb9,0x62,0xfc,0x37,0x17,0x49,0x2d,0x42,0xbf };
static const uint8_t mxf_jp2k_rsiz[]                       = { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x01,0x00 };
static const uint8_t mxf_indirect_value_utf16le[]          = { 0x4c,0x00,0x02,0x10,0x01,0x00,0x00,0x00,0x00,0x06,0x0e,0x2b,0x34,0x01,0x04,0x01,0x01 };
static const uint8_t mxf_indirect_value_utf16be[]          = { 0x42,0x01,0x10,0x02,0x00,0x00,0x00,0x00,0x00,0x06,0x0e,0x2b,0x34,0x01,0x04,0x01,0x01 };

#define IS_KLV_KEY(x, y) (!memcmp(x, y, sizeof(y)))

static void mxf_free_metadataset(MXFMetadataSet **ctx, int freectx)
{
    MXFIndexTableSegment *seg;
    switch ((*ctx)->type) {
    case Descriptor:
        av_freep(&((MXFDescriptor *)*ctx)->extradata);
        break;
    case MultipleDescriptor:
        av_freep(&((MXFDescriptor *)*ctx)->sub_descriptors_refs);
        break;
    case Sequence:
        av_freep(&((MXFSequence *)*ctx)->structural_components_refs);
        break;
    case EssenceGroup:
        av_freep(&((MXFEssenceGroup *)*ctx)->structural_components_refs);
        break;
    case SourcePackage:
    case MaterialPackage:
        av_freep(&((MXFPackage *)*ctx)->tracks_refs);
        av_freep(&((MXFPackage *)*ctx)->name);
        av_freep(&((MXFPackage *)*ctx)->comment_refs);
        break;
    case TaggedValue:
        av_freep(&((MXFTaggedValue *)*ctx)->name);
        av_freep(&((MXFTaggedValue *)*ctx)->value);
        break;
    case Track:
        av_freep(&((MXFTrack *)*ctx)->name);
        break;
    case IndexTableSegment:
        seg = (MXFIndexTableSegment *)*ctx;
        av_freep(&seg->temporal_offset_entries);
        av_freep(&seg->flag_entries);
        av_freep(&seg->stream_offset_entries);
    default:
        break;
    }
    if (freectx)
    av_freep(ctx);
}

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
    if (size > INT64_MAX)
        return AVERROR_INVALIDDATA;
    return size;
}

static int mxf_read_sync(AVIOContext *pb, const uint8_t *key, unsigned size)
{
    int i, b;
    for (i = 0; i < size && !avio_feof(pb); i++) {
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
    int64_t length, pos;
    if (!mxf_read_sync(pb, mxf_klv_key, 4))
        return AVERROR_INVALIDDATA;
    klv->offset = avio_tell(pb) - 4;
    memcpy(klv->key, mxf_klv_key, 4);
    avio_read(pb, klv->key + 4, 12);
    length = klv_decode_ber_length(pb);
    if (length < 0)
        return length;
    klv->length = length;
    pos = avio_tell(pb);
    if (pos > INT64_MAX - length)
        return AVERROR_INVALIDDATA;
    klv->next_klv = pos + length;
    return 0;
}

static int mxf_get_stream_index(AVFormatContext *s, KLVPacket *klv, int body_sid)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        MXFTrack *track = s->streams[i]->priv_data;
        /* SMPTE 379M 7.3 */
        if (track && (!body_sid || !track->body_sid || track->body_sid == body_sid) && !memcmp(klv->key + sizeof(mxf_essence_element_key), track->track_number, sizeof(track->track_number)))
            return i;
    }
    /* return 0 if only one stream, for OP Atom files with 0 as track number */
    return s->nb_streams == 1 && s->streams[0]->priv_data ? 0 : -1;
}

static int find_body_sid_by_absolute_offset(MXFContext *mxf, int64_t offset)
{
    // we look for partition where the offset is placed
    int a, b, m;
    int64_t pack_ofs;

    a = -1;
    b = mxf->partitions_count;

    while (b - a > 1) {
        m = (a + b) >> 1;
        pack_ofs = mxf->partitions[m].pack_ofs;
        if (pack_ofs <= offset)
            a = m;
        else
            b = m;
    }

    if (a == -1)
        return 0;
    return mxf->partitions[a].body_sid;
}

static int mxf_get_eia608_packet(AVFormatContext *s, AVStream *st, AVPacket *pkt, int64_t length)
{
    int count = avio_rb16(s->pb);
    int cdp_identifier, cdp_length, cdp_footer_id, ccdata_id, cc_count;
    int line_num, sample_coding, sample_count;
    int did, sdid, data_length;
    int i, ret;

    if (count != 1)
        av_log(s, AV_LOG_WARNING, "unsupported multiple ANC packets (%d) per KLV packet\n", count);

    for (i = 0; i < count; i++) {
        if (length < 6) {
            av_log(s, AV_LOG_ERROR, "error reading s436m packet %"PRId64"\n", length);
            return AVERROR_INVALIDDATA;
        }
        line_num = avio_rb16(s->pb);
        avio_r8(s->pb); // wrapping type
        sample_coding = avio_r8(s->pb);
        sample_count = avio_rb16(s->pb);
        length -= 6 + 8 + sample_count;
        if (line_num != 9 && line_num != 11)
            continue;
        if (sample_coding == 7 || sample_coding == 8 || sample_coding == 9) {
            av_log(s, AV_LOG_WARNING, "unsupported s436m 10 bit sample coding\n");
            continue;
        }
        if (length < 0)
            return AVERROR_INVALIDDATA;

        avio_rb32(s->pb); // array count
        avio_rb32(s->pb); // array elem size
        did = avio_r8(s->pb);
        sdid = avio_r8(s->pb);
        data_length = avio_r8(s->pb);
        if (did != 0x61 || sdid != 1) {
            av_log(s, AV_LOG_WARNING, "unsupported did or sdid: %x %x\n", did, sdid);
            continue;
        }
        cdp_identifier = avio_rb16(s->pb); // cdp id
        if (cdp_identifier != 0x9669) {
            av_log(s, AV_LOG_ERROR, "wrong cdp identifier %x\n", cdp_identifier);
            return AVERROR_INVALIDDATA;
        }
        cdp_length = avio_r8(s->pb);
        avio_r8(s->pb); // cdp_frame_rate
        avio_r8(s->pb); // cdp_flags
        avio_rb16(s->pb); // cdp_hdr_sequence_cntr
        ccdata_id = avio_r8(s->pb); // ccdata_id
        if (ccdata_id != 0x72) {
            av_log(s, AV_LOG_ERROR, "wrong cdp data section %x\n", ccdata_id);
            return AVERROR_INVALIDDATA;
        }
        cc_count = avio_r8(s->pb) & 0x1f;
        ret = av_get_packet(s->pb, pkt, cc_count * 3);
        if (ret < 0)
            return ret;
        if (cdp_length - 9 - 4 <  cc_count * 3) {
            av_log(s, AV_LOG_ERROR, "wrong cdp size %d cc count %d\n", cdp_length, cc_count);
            return AVERROR_INVALIDDATA;
        }
        avio_skip(s->pb, data_length - 9 - 4 - cc_count * 3);
        cdp_footer_id = avio_r8(s->pb);
        if (cdp_footer_id != 0x74) {
            av_log(s, AV_LOG_ERROR, "wrong cdp footer section %x\n", cdp_footer_id);
            return AVERROR_INVALIDDATA;
        }
        avio_rb16(s->pb); // cdp_ftr_sequence_cntr
        avio_r8(s->pb); // packet_checksum
        break;
    }

    return 0;
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
    for (; end_ptr - buf_ptr >= st->codecpar->channels * 4; ) {
        for (i = 0; i < st->codecpar->channels; i++) {
            uint32_t sample = bytestream_get_le32(&buf_ptr);
            if (st->codecpar->bits_per_coded_sample == 24)
                bytestream_put_le24(&data_ptr, (sample >> 4) & 0xffffff);
            else
                bytestream_put_le16(&data_ptr, (sample >> 12) & 0xffff);
        }
        buf_ptr += 32 - st->codecpar->channels*4; // always 8 channels stored SMPTE 331M
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
    int body_sid;

    if (!mxf->aesc && s->key && s->keylen == 16) {
        mxf->aesc = av_aes_alloc();
        if (!mxf->aesc)
            return AVERROR(ENOMEM);
        av_aes_init(mxf->aesc, s->key, 128, 1);
    }
    // crypto context
    size = klv_decode_ber_length(pb);
    if (size < 0)
        return size;
    avio_skip(pb, size);
    // plaintext offset
    klv_decode_ber_length(pb);
    plaintext_size = avio_rb64(pb);
    // source klv key
    klv_decode_ber_length(pb);
    avio_read(pb, klv->key, 16);
    if (!IS_KLV_KEY(klv, mxf_essence_element_key))
        return AVERROR_INVALIDDATA;

    body_sid = find_body_sid_by_absolute_offset(mxf, klv->offset);
    index = mxf_get_stream_index(s, klv, body_sid);
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
    if (item_num > 65536 || item_num < 0) {
        av_log(mxf->fc, AV_LOG_ERROR, "item_num %d is too large\n", item_num);
        return AVERROR_INVALIDDATA;
    }
    if (mxf->local_tags)
        av_log(mxf->fc, AV_LOG_VERBOSE, "Multiple primer packs\n");
    av_free(mxf->local_tags);
    mxf->local_tags_count = 0;
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
    AVFormatContext *s = mxf->fc;
    MXFPartition *partition, *tmp_part;
    UID op;
    uint64_t footer_partition;
    uint32_t nb_essence_containers;

    if (mxf->partitions_count >= INT_MAX / 2)
        return AVERROR_INVALIDDATA;

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
    partition->pack_ofs    = klv_offset;

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
    partition->body_offset = avio_rb64(pb);
    partition->body_sid = avio_rb32(pb);
    if (avio_read(pb, op, sizeof(UID)) != sizeof(UID)) {
        av_log(mxf->fc, AV_LOG_ERROR, "Failed reading UID\n");
        return AVERROR_INVALIDDATA;
    }
    nb_essence_containers = avio_rb32(pb);

    if (partition->type == Header) {
        char str[36];
        snprintf(str, sizeof(str), "%08x.%08x.%08x.%08x", AV_RB32(&op[0]), AV_RB32(&op[4]), AV_RB32(&op[8]), AV_RB32(&op[12]));
        av_dict_set(&s->metadata, "operational_pattern_ul", str, 0);
    }

    if (partition->this_partition &&
        partition->previous_partition == partition->this_partition) {
        av_log(mxf->fc, AV_LOG_ERROR,
               "PreviousPartition equal to ThisPartition %"PRIx64"\n",
               partition->previous_partition);
        /* override with the actual previous partition offset */
        if (!mxf->parsing_backward && mxf->last_forward_partition > 1) {
            MXFPartition *prev =
                mxf->partitions + mxf->last_forward_partition - 2;
            partition->previous_partition = prev->this_partition;
        }
        /* if no previous body partition are found point to the header
         * partition */
        if (partition->previous_partition == partition->this_partition)
            partition->previous_partition = 0;
        av_log(mxf->fc, AV_LOG_ERROR,
               "Overriding PreviousPartition with %"PRIx64"\n",
               partition->previous_partition);
    }

    /* some files don't have FooterPartition set in every partition */
    if (footer_partition) {
        if (mxf->footer_partition && mxf->footer_partition != footer_partition) {
            av_log(mxf->fc, AV_LOG_ERROR,
                   "inconsistent FooterPartition value: %"PRIu64" != %"PRIu64"\n",
                   mxf->footer_partition, footer_partition);
        } else {
            mxf->footer_partition = footer_partition;
        }
    }

    av_log(mxf->fc, AV_LOG_TRACE,
            "PartitionPack: ThisPartition = 0x%"PRIX64
            ", PreviousPartition = 0x%"PRIX64", "
            "FooterPartition = 0x%"PRIX64", IndexSID = %i, BodySID = %i\n",
            partition->this_partition,
            partition->previous_partition, footer_partition,
            partition->index_sid, partition->body_sid);

    /* sanity check PreviousPartition if set */
    //NOTE: this isn't actually enough, see mxf_seek_to_previous_partition()
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

static int mxf_read_strong_ref_array(AVIOContext *pb, UID **refs, int *count)
{
    *count = avio_rb32(pb);
    *refs = av_calloc(*count, sizeof(UID));
    if (!*refs) {
        *count = 0;
        return AVERROR(ENOMEM);
    }
    avio_skip(pb, 4); /* useless size of objects, always 16 according to specs */
    avio_read(pb, (uint8_t *)*refs, *count * sizeof(UID));
    return 0;
}

static inline int mxf_read_utf16_string(AVIOContext *pb, int size, char** str, int be)
{
    int ret;
    size_t buf_size;

    if (size < 0 || size > INT_MAX/2)
        return AVERROR(EINVAL);

    buf_size = size + size / 2 + 1;
    *str = av_malloc(buf_size);
    if (!*str)
        return AVERROR(ENOMEM);

    if (be)
        ret = avio_get_str16be(pb, size, *str, buf_size);
    else
        ret = avio_get_str16le(pb, size, *str, buf_size);

    if (ret < 0) {
        av_freep(str);
        return ret;
    }

    return ret;
}

#define READ_STR16(type, big_endian)                                               \
static int mxf_read_utf16 ## type ##_string(AVIOContext *pb, int size, char** str) \
{                                                                                  \
return mxf_read_utf16_string(pb, size, str, big_endian);                           \
}
READ_STR16(be, 1)
READ_STR16(le, 0)
#undef READ_STR16

static int mxf_read_content_storage(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    switch (tag) {
    case 0x1901:
        if (mxf->packages_refs)
            av_log(mxf->fc, AV_LOG_VERBOSE, "Multiple packages_refs\n");
        av_free(mxf->packages_refs);
        return mxf_read_strong_ref_array(pb, &mxf->packages_refs, &mxf->packages_count);
    case 0x1902:
        av_free(mxf->essence_container_data_refs);
        return mxf_read_strong_ref_array(pb, &mxf->essence_container_data_refs, &mxf->essence_container_data_count);
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
        avio_read(pb, source_clip->source_package_ul, 16);
        avio_read(pb, source_clip->source_package_uid, 16);
        break;
    case 0x1102:
        source_clip->source_track_id = avio_rb32(pb);
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

static int mxf_read_pulldown_component(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFPulldownComponent *mxf_pulldown = arg;
    switch(tag) {
    case 0x0d01:
        avio_read(pb, mxf_pulldown->input_segment_ref, 16);
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
    case 0x4802:
        mxf_read_utf16be_string(pb, size, &track->name);
        break;
    case 0x4b01:
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
        case 0x4b02:
        sequence->origin = avio_r8(pb);
        break;
    case 0x1001:
        return mxf_read_strong_ref_array(pb, &sequence->structural_components_refs,
                                             &sequence->structural_components_count);
    }
    return 0;
}

static int mxf_read_essence_group(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFEssenceGroup *essence_group = arg;
    switch (tag) {
    case 0x0202:
        essence_group->duration = avio_rb64(pb);
        break;
    case 0x0501:
        return mxf_read_strong_ref_array(pb, &essence_group->structural_components_refs,
                                             &essence_group->structural_components_count);
    }
    return 0;
}

static int mxf_read_package(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFPackage *package = arg;
    switch(tag) {
    case 0x4403:
        return mxf_read_strong_ref_array(pb, &package->tracks_refs,
                                             &package->tracks_count);
    case 0x4401:
        /* UMID */
        avio_read(pb, package->package_ul, 16);
        avio_read(pb, package->package_uid, 16);
        break;
    case 0x4701:
        avio_read(pb, package->descriptor_ref, 16);
        break;
    case 0x4402:
        return mxf_read_utf16be_string(pb, size, &package->name);
    case 0x4406:
        return mxf_read_strong_ref_array(pb, &package->comment_refs,
                                             &package->comment_count);
    }
    return 0;
}

static int mxf_read_essence_container_data(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFEssenceContainerData *essence_data = arg;
    switch(tag) {
        case 0x2701:
            /* linked package umid UMID */
            avio_read(pb, essence_data->package_ul, 16);
            avio_read(pb, essence_data->package_uid, 16);
            break;
        case 0x3f06:
            essence_data->index_sid = avio_rb32(pb);
            break;
        case 0x3f07:
            essence_data->body_sid = avio_rb32(pb);
            break;
    }
    return 0;
}

static int mxf_read_index_entry_array(AVIOContext *pb, MXFIndexTableSegment *segment)
{
    int i, length;

    segment->nb_index_entries = avio_rb32(pb);

    length = avio_rb32(pb);
    if(segment->nb_index_entries && length < 11)
        return AVERROR_INVALIDDATA;

    if (!(segment->temporal_offset_entries=av_calloc(segment->nb_index_entries, sizeof(*segment->temporal_offset_entries))) ||
        !(segment->flag_entries          = av_calloc(segment->nb_index_entries, sizeof(*segment->flag_entries))) ||
        !(segment->stream_offset_entries = av_calloc(segment->nb_index_entries, sizeof(*segment->stream_offset_entries)))) {
        av_freep(&segment->temporal_offset_entries);
        av_freep(&segment->flag_entries);
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < segment->nb_index_entries; i++) {
        if(avio_feof(pb))
            return AVERROR_INVALIDDATA;
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
        av_log(NULL, AV_LOG_TRACE, "EditUnitByteCount %d\n", segment->edit_unit_byte_count);
        break;
    case 0x3F06:
        segment->index_sid = avio_rb32(pb);
        av_log(NULL, AV_LOG_TRACE, "IndexSID %d\n", segment->index_sid);
        break;
    case 0x3F07:
        segment->body_sid = avio_rb32(pb);
        av_log(NULL, AV_LOG_TRACE, "BodySID %d\n", segment->body_sid);
        break;
    case 0x3F0A:
        av_log(NULL, AV_LOG_TRACE, "IndexEntryArray found\n");
        return mxf_read_index_entry_array(pb, segment);
    case 0x3F0B:
        segment->index_edit_rate.num = avio_rb32(pb);
        segment->index_edit_rate.den = avio_rb32(pb);
        av_log(NULL, AV_LOG_TRACE, "IndexEditRate %d/%d\n", segment->index_edit_rate.num,
                segment->index_edit_rate.den);
        break;
    case 0x3F0C:
        segment->index_start_position = avio_rb64(pb);
        av_log(NULL, AV_LOG_TRACE, "IndexStartPosition %"PRId64"\n", segment->index_start_position);
        break;
    case 0x3F0D:
        segment->index_duration = avio_rb64(pb);
        av_log(NULL, AV_LOG_TRACE, "IndexDuration %"PRId64"\n", segment->index_duration);
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
        av_log(NULL, AV_LOG_TRACE, "pixel layout: code %#x\n", code);

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
    int entry_count, entry_size;

    switch(tag) {
    case 0x3F01:
        return mxf_read_strong_ref_array(pb, &descriptor->sub_descriptors_refs,
                                             &descriptor->sub_descriptors_count);
    case 0x3002: /* ContainerDuration */
        descriptor->duration = avio_rb64(pb);
        break;
    case 0x3004:
        avio_read(pb, descriptor->essence_container_ul, 16);
        break;
    case 0x3005:
        avio_read(pb, descriptor->codec_ul, 16);
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
    case 0x320D:
        entry_count = avio_rb32(pb);
        entry_size = avio_rb32(pb);
        if (entry_size == 4) {
            if (entry_count > 0)
                descriptor->video_line_map[0] = avio_rb32(pb);
            else
                descriptor->video_line_map[0] = 0;
            if (entry_count > 1)
                descriptor->video_line_map[1] = avio_rb32(pb);
            else
                descriptor->video_line_map[1] = 0;
        } else
            av_log(NULL, AV_LOG_WARNING, "VideoLineMap element size %d currently not supported\n", entry_size);
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
        if (IS_KLV_KEY(uid, mxf_jp2k_rsiz)) {
            uint32_t rsiz = avio_rb16(pb);
            if (rsiz == FF_PROFILE_JPEG2000_DCINEMA_2K ||
                rsiz == FF_PROFILE_JPEG2000_DCINEMA_4K)
                descriptor->pix_fmt = AV_PIX_FMT_XYZ12;
        }
        break;
    }
    return 0;
}

static int mxf_read_indirect_value(void *arg, AVIOContext *pb, int size)
{
    MXFTaggedValue *tagged_value = arg;
    uint8_t key[17];

    if (size <= 17)
        return 0;

    avio_read(pb, key, 17);
    /* TODO: handle other types of of indirect values */
    if (memcmp(key, mxf_indirect_value_utf16le, 17) == 0) {
        return mxf_read_utf16le_string(pb, size - 17, &tagged_value->value);
    } else if (memcmp(key, mxf_indirect_value_utf16be, 17) == 0) {
        return mxf_read_utf16be_string(pb, size - 17, &tagged_value->value);
    }
    return 0;
}

static int mxf_read_tagged_value(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFTaggedValue *tagged_value = arg;
    switch (tag){
    case 0x5001:
        return mxf_read_utf16be_string(pb, size, &tagged_value->name);
    case 0x5003:
        return mxf_read_indirect_value(tagged_value, pb, size);
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
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x0c,0x01,0x00 }, 14,   AV_CODEC_ID_JPEG2000, NULL, 14 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x10,0x60,0x01 }, 14,       AV_CODEC_ID_H264, NULL, 15 }, /* H.264 */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x11,0x01,0x00 }, 14,      AV_CODEC_ID_DNXHD, NULL, 14 }, /* VC-3 */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x12,0x01,0x00 }, 14,        AV_CODEC_ID_VC1, NULL, 14 }, /* VC-1 */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x14,0x01,0x00 }, 14,       AV_CODEC_ID_TIFF, NULL, 14 }, /* TIFF */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x15,0x01,0x00 }, 14,      AV_CODEC_ID_DIRAC, NULL, 14 }, /* VC-2 */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x1b,0x01,0x00 }, 14,       AV_CODEC_ID_CFHD, NULL, 14 }, /* VC-5 */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x1c,0x01,0x00 }, 14,     AV_CODEC_ID_PRORES, NULL, 14 }, /* ProRes */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x04,0x60,0x01 }, 14, AV_CODEC_ID_MPEG2VIDEO, NULL, 15 }, /* MPEG-ES */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x01,0x04,0x01 }, 14, AV_CODEC_ID_MPEG2VIDEO, NULL, 15, D10D11Wrap }, /* SMPTE D-10 mapping */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x02,0x41,0x01 }, 14,    AV_CODEC_ID_DVVIDEO, NULL, 15 }, /* DV 625 25mbps */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x05,0x00,0x00 }, 14,   AV_CODEC_ID_RAWVIDEO, NULL, 15, RawVWrap }, /* uncompressed picture */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0a,0x0e,0x0f,0x03,0x01,0x02,0x20,0x01,0x01 }, 15,     AV_CODEC_ID_HQ_HQA },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0a,0x0e,0x0f,0x03,0x01,0x02,0x20,0x02,0x01 }, 15,        AV_CODEC_ID_HQX },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0a,0x0e,0x15,0x00,0x04,0x02,0x10,0x00,0x01 }, 16,       AV_CODEC_ID_HEVC, NULL, 15 }, /* Canon XF-HEVC */
    { { 0x06,0x0e,0x2b,0x34,0x01,0x01,0x01,0xff,0x4b,0x46,0x41,0x41,0x00,0x0d,0x4d,0x4f }, 14,   AV_CODEC_ID_RAWVIDEO }, /* Legacy ?? Uncompressed Picture */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,      AV_CODEC_ID_NONE },
};

/* EC ULs for intra-only formats */
static const MXFCodecUL mxf_intra_only_essence_container_uls[] = {
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x01,0x00,0x00 }, 14, AV_CODEC_ID_MPEG2VIDEO }, /* MXF-GC SMPTE D-10 mappings */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,       AV_CODEC_ID_NONE },
};

/* intra-only PictureEssenceCoding ULs, where no corresponding EC UL exists */
static const MXFCodecUL mxf_intra_only_picture_essence_coding_uls[] = {
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x00,0x00 }, 14,       AV_CODEC_ID_H264 }, /* H.264/MPEG-4 AVC Intra Profiles */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x04,0x01,0x02,0x02,0x03,0x01,0x01,0x00 }, 14,   AV_CODEC_ID_JPEG2000 }, /* JPEG 2000 code stream */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,       AV_CODEC_ID_NONE },
};

/* actual coded width for AVC-Intra to allow selecting correct SPS/PPS */
static const MXFCodecUL mxf_intra_only_picture_coded_width[] = {
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x01 }, 16, 1440 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x02 }, 16, 1440 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x03 }, 16, 1440 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x04 }, 16, 1440 },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,    0 },
};

static const MXFCodecUL mxf_sound_essence_container_uls[] = {
    // sound essence container uls
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x06,0x01,0x00 }, 14, AV_CODEC_ID_PCM_S16LE, NULL, 14, RawAWrap }, /* BWF */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x02,0x0d,0x01,0x03,0x01,0x02,0x04,0x40,0x01 }, 14,       AV_CODEC_ID_MP2, NULL, 15 }, /* MPEG-ES */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x01,0x01,0x01 }, 14, AV_CODEC_ID_PCM_S16LE, NULL, 13 }, /* D-10 Mapping 50Mbps PAL Extended Template */
    { { 0x06,0x0e,0x2b,0x34,0x01,0x01,0x01,0xff,0x4b,0x46,0x41,0x41,0x00,0x0d,0x4d,0x4F }, 14, AV_CODEC_ID_PCM_S16LE }, /* 0001GL00.MXF.A1.mxf_opatom.mxf */
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x03,0x04,0x02,0x02,0x02,0x03,0x03,0x01,0x00 }, 14,       AV_CODEC_ID_AAC }, /* MPEG-2 AAC ADTS (legacy) */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0,      AV_CODEC_ID_NONE },
};

static const MXFCodecUL mxf_data_essence_container_uls[] = {
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x09,0x0d,0x01,0x03,0x01,0x02,0x0d,0x00,0x00 }, 16, AV_CODEC_ID_NONE,      "vbi_smpte_436M", 11 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x09,0x0d,0x01,0x03,0x01,0x02,0x0e,0x00,0x00 }, 16, AV_CODEC_ID_NONE, "vbi_vanc_smpte_436M", 11 },
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x09,0x0d,0x01,0x03,0x01,0x02,0x13,0x01,0x01 }, 16, AV_CODEC_ID_TTML },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  0, AV_CODEC_ID_NONE },
};

static MXFWrappingScheme mxf_get_wrapping_kind(UID *essence_container_ul)
{
    int val;
    const MXFCodecUL *codec_ul;

    codec_ul = mxf_get_codec_ul(mxf_picture_essence_container_uls, essence_container_ul);
    if (!codec_ul->uid[0])
        codec_ul = mxf_get_codec_ul(mxf_sound_essence_container_uls, essence_container_ul);
    if (!codec_ul->uid[0])
        codec_ul = mxf_get_codec_ul(mxf_data_essence_container_uls, essence_container_ul);
    if (!codec_ul->uid[0] || !codec_ul->wrapping_indicator_pos)
        return UnknownWrapped;

    val = (*essence_container_ul)[codec_ul->wrapping_indicator_pos];
    switch (codec_ul->wrapping_indicator_type) {
        case RawVWrap:
            val = val % 4;
            break;
        case RawAWrap:
            if (val == 0x03 || val == 0x04)
                val -= 0x02;
            break;
        case D10D11Wrap:
            if (val == 0x02)
                val = 0x01;
            break;
    }
    if (val == 0x01)
        return FrameWrapped;
    if (val == 0x02)
        return ClipWrapped;
    return UnknownWrapped;
}

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

    for (i = nb_segments = 0; i < mxf->metadata_sets_count; i++) {
        if (mxf->metadata_sets[i]->type == IndexTableSegment) {
            MXFIndexTableSegment *s = (MXFIndexTableSegment*)mxf->metadata_sets[i];
            if (s->edit_unit_byte_count || s->nb_index_entries)
                unsorted_segments[nb_segments++] = s;
            else
                av_log(mxf->fc, AV_LOG_WARNING, "IndexSID %i segment at %"PRId64" missing EditUnitByteCount and IndexEntryArray\n",
                       s->index_sid, s->index_start_position);
        }
    }

    if (!nb_segments) {
        av_freep(sorted_segments);
        av_free(unsorted_segments);
        return AVERROR_INVALIDDATA;
    }

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
            if ((i == 0 ||
                 s->body_sid >  last_body_sid ||
                 s->body_sid == last_body_sid && s->index_sid >  last_index_sid ||
                 s->body_sid == last_body_sid && s->index_sid == last_index_sid && s->index_start_position > last_index_start) &&
                (best == -1 ||
                 s->body_sid <  best_body_sid ||
                 s->body_sid == best_body_sid && s->index_sid <  best_index_sid ||
                 s->body_sid == best_body_sid && s->index_sid == best_index_sid && s->index_start_position <  best_index_start ||
                 s->body_sid == best_body_sid && s->index_sid == best_index_sid && s->index_start_position == best_index_start && s->index_duration > best_index_duration)) {
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
static int mxf_absolute_bodysid_offset(MXFContext *mxf, int body_sid, int64_t offset, int64_t *offset_out, MXFPartition **partition_out)
{
    MXFPartition *last_p = NULL;
    int a, b, m, m0;

    if (offset < 0)
        return AVERROR(EINVAL);

    a = -1;
    b = mxf->partitions_count;

    while (b - a > 1) {
        m0 = m = (a + b) >> 1;

        while (m < b && mxf->partitions[m].body_sid != body_sid)
            m++;

        if (m < b && mxf->partitions[m].body_offset <= offset)
            a = m;
        else
            b = m0;
    }

    if (a >= 0)
        last_p = &mxf->partitions[a];

    if (last_p && (!last_p->essence_length || last_p->essence_length > (offset - last_p->body_offset))) {
        *offset_out = last_p->essence_offset + (offset - last_p->body_offset);
        if (partition_out)
            *partition_out = last_p;
        return 0;
    }

    av_log(mxf->fc, AV_LOG_ERROR,
           "failed to find absolute offset of %"PRIX64" in BodySID %i - partial file?\n",
           offset, body_sid);

    return AVERROR_INVALIDDATA;
}

/**
 * Returns the end position of the essence container with given BodySID, or zero if unknown
 */
static int64_t mxf_essence_container_end(MXFContext *mxf, int body_sid)
{
    for (int x = mxf->partitions_count - 1; x >= 0; x--) {
        MXFPartition *p = &mxf->partitions[x];

        if (p->body_sid != body_sid)
            continue;

        if (!p->essence_length)
            return 0;

        return p->essence_offset + p->essence_length;
    }

    return 0;
}

/* EditUnit -> absolute offset */
static int mxf_edit_unit_absolute_offset(MXFContext *mxf, MXFIndexTable *index_table, int64_t edit_unit, AVRational edit_rate, int64_t *edit_unit_out, int64_t *offset_out, MXFPartition **partition_out, int nag)
{
    int i;
    int64_t offset_temp = 0;

    edit_unit = av_rescale_q(edit_unit, index_table->segments[0]->index_edit_rate, edit_rate);

    for (i = 0; i < index_table->nb_segments; i++) {
        MXFIndexTableSegment *s = index_table->segments[i];

        edit_unit = FFMAX(edit_unit, s->index_start_position);  /* clamp if trying to seek before start */

        if (edit_unit < s->index_start_position + s->index_duration) {
            int64_t index = edit_unit - s->index_start_position;

            if (s->edit_unit_byte_count)
                offset_temp += s->edit_unit_byte_count * index;
            else {
                if (s->nb_index_entries == 2 * s->index_duration + 1)
                    index *= 2;     /* Avid index */

                if (index < 0 || index >= s->nb_index_entries) {
                    av_log(mxf->fc, AV_LOG_ERROR, "IndexSID %i segment at %"PRId64" IndexEntryArray too small\n",
                           index_table->index_sid, s->index_start_position);
                    return AVERROR_INVALIDDATA;
                }

                offset_temp = s->stream_offset_entries[index];
            }

            if (edit_unit_out)
                *edit_unit_out = av_rescale_q(edit_unit, edit_rate, s->index_edit_rate);

            return mxf_absolute_bodysid_offset(mxf, index_table->body_sid, offset_temp, offset_out, partition_out);
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
    uint8_t *flags;

    /* first compute how many entries we have */
    for (i = 0; i < index_table->nb_segments; i++) {
        MXFIndexTableSegment *s = index_table->segments[i];

        if (!s->nb_index_entries) {
            index_table->nb_ptses = 0;
            return 0;                               /* no TemporalOffsets */
        }

        if (s->index_duration > INT_MAX - index_table->nb_ptses) {
            index_table->nb_ptses = 0;
            av_log(mxf->fc, AV_LOG_ERROR, "ignoring IndexSID %d, duration is too large\n", s->index_sid);
            return 0;
        }

        index_table->nb_ptses += s->index_duration;
    }

    /* paranoid check */
    if (index_table->nb_ptses <= 0)
        return 0;

    if (!(index_table->ptses      = av_calloc(index_table->nb_ptses, sizeof(int64_t))) ||
        !(index_table->fake_index = av_calloc(index_table->nb_ptses, sizeof(AVIndexEntry))) ||
        !(index_table->offsets    = av_calloc(index_table->nb_ptses, sizeof(int8_t))) ||
        !(flags                   = av_calloc(index_table->nb_ptses, sizeof(uint8_t)))) {
        av_freep(&index_table->ptses);
        av_freep(&index_table->fake_index);
        av_freep(&index_table->offsets);
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

            flags[x] = !(s->flag_entries[j] & 0x30) ? AVINDEX_KEYFRAME : 0;

            if (index < 0 || index >= index_table->nb_ptses) {
                av_log(mxf->fc, AV_LOG_ERROR,
                       "index entry %i + TemporalOffset %i = %i, which is out of bounds\n",
                       x, offset, index);
                continue;
            }

            index_table->offsets[x] = offset;
            index_table->ptses[index] = x;
            max_temporal_offset = FFMAX(max_temporal_offset, offset);
        }
    }

    /* calculate the fake index table in display order */
    for (x = 0; x < index_table->nb_ptses; x++) {
        index_table->fake_index[x].timestamp = x;
        if (index_table->ptses[x] != AV_NOPTS_VALUE)
            index_table->fake_index[index_table->ptses[x]].flags = flags[x];
    }
    av_freep(&flags);

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
        MXFTrack *mxf_track = NULL;

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

        for (k = 0; k < mxf->fc->nb_streams; k++) {
            MXFTrack *track = mxf->fc->streams[k]->priv_data;
            if (track && track->index_sid == t->index_sid) {
                mxf_track = track;
                break;
            }
        }

        /* fix zero IndexDurations */
        for (k = 0; k < t->nb_segments; k++) {
            if (!t->segments[k]->index_edit_rate.num || !t->segments[k]->index_edit_rate.den) {
                av_log(mxf->fc, AV_LOG_WARNING, "IndexSID %i segment %i has invalid IndexEditRate\n",
                       t->index_sid, k);
                if (mxf_track)
                    t->segments[k]->index_edit_rate = mxf_track->edit_rate;
            }

            if (t->segments[k]->index_duration)
                continue;

            if (t->nb_segments > 1)
                av_log(mxf->fc, AV_LOG_WARNING, "IndexSID %i segment %i has zero IndexDuration and there's more than one segment\n",
                       t->index_sid, k);

            if (!mxf_track) {
                av_log(mxf->fc, AV_LOG_WARNING, "no streams?\n");
                break;
            }

            /* assume the first stream's duration is reasonable
             * leave index_duration = 0 on further segments in case we have any (unlikely)
             */
            t->segments[k]->index_duration = mxf_track->original_duration;
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

static int mxf_umid_to_str(UID ul, UID uid, char **str)
{
    int i;
    char *p;
    p = *str = av_mallocz(sizeof(UID) * 4 + 2 + 1);
    if (!p)
        return AVERROR(ENOMEM);
    snprintf(p, 2 + 1, "0x");
    p += 2;
    for (i = 0; i < sizeof(UID); i++) {
        snprintf(p, 2 + 1, "%.2X", ul[i]);
        p += 2;

    }
    for (i = 0; i < sizeof(UID); i++) {
        snprintf(p, 2 + 1, "%.2X", uid[i]);
        p += 2;
    }
    return 0;
}

static int mxf_add_umid_metadata(AVDictionary **pm, const char *key, MXFPackage* package)
{
    char *str;
    int ret;
    if (!package)
        return 0;
    if ((ret = mxf_umid_to_str(package->package_ul, package->package_uid, &str)) < 0)
        return ret;
    av_dict_set(pm, key, str, AV_DICT_DONT_STRDUP_VAL);
    return 0;
}

static int mxf_add_timecode_metadata(AVDictionary **pm, const char *key, AVTimecode *tc)
{
    char buf[AV_TIMECODE_STR_SIZE];
    av_dict_set(pm, key, av_timecode_make_string(tc, buf, 0), 0);

    return 0;
}

static MXFTimecodeComponent* mxf_resolve_timecode_component(MXFContext *mxf, UID *strong_ref)
{
    MXFStructuralComponent *component = NULL;
    MXFPulldownComponent *pulldown = NULL;

    component = mxf_resolve_strong_ref(mxf, strong_ref, AnyType);
    if (!component)
        return NULL;

    switch (component->type) {
    case TimecodeComponent:
        return (MXFTimecodeComponent*)component;
    case PulldownComponent: /* timcode component may be located on a pulldown component */
        pulldown = (MXFPulldownComponent*)component;
        return mxf_resolve_strong_ref(mxf, &pulldown->input_segment_ref, TimecodeComponent);
    default:
        break;
    }
    return NULL;
}

static MXFPackage* mxf_resolve_source_package(MXFContext *mxf, UID package_ul, UID package_uid)
{
    MXFPackage *package = NULL;
    int i;

    for (i = 0; i < mxf->packages_count; i++) {
        package = mxf_resolve_strong_ref(mxf, &mxf->packages_refs[i], SourcePackage);
        if (!package)
            continue;

        if (!memcmp(package->package_ul, package_ul, 16) && !memcmp(package->package_uid, package_uid, 16))
            return package;
    }
    return NULL;
}

static MXFDescriptor* mxf_resolve_multidescriptor(MXFContext *mxf, MXFDescriptor *descriptor, int track_id)
{
    MXFDescriptor *sub_descriptor = NULL;
    int i;

    if (!descriptor)
        return NULL;

    if (descriptor->type == MultipleDescriptor) {
        for (i = 0; i < descriptor->sub_descriptors_count; i++) {
            sub_descriptor = mxf_resolve_strong_ref(mxf, &descriptor->sub_descriptors_refs[i], Descriptor);

            if (!sub_descriptor) {
                av_log(mxf->fc, AV_LOG_ERROR, "could not resolve sub descriptor strong ref\n");
                continue;
            }
            if (sub_descriptor->linked_track_id == track_id) {
                return sub_descriptor;
            }
        }
    } else if (descriptor->type == Descriptor)
        return descriptor;

    return NULL;
}

static MXFStructuralComponent* mxf_resolve_essence_group_choice(MXFContext *mxf, MXFEssenceGroup *essence_group)
{
    MXFStructuralComponent *component = NULL;
    MXFPackage *package = NULL;
    MXFDescriptor *descriptor = NULL;
    int i;

    if (!essence_group || !essence_group->structural_components_count)
        return NULL;

    /* essence groups contains multiple representations of the same media,
       this return the first components with a valid Descriptor typically index 0 */
    for (i =0; i < essence_group->structural_components_count; i++){
        component = mxf_resolve_strong_ref(mxf, &essence_group->structural_components_refs[i], SourceClip);
        if (!component)
            continue;

        if (!(package = mxf_resolve_source_package(mxf, component->source_package_ul, component->source_package_uid)))
            continue;

        descriptor = mxf_resolve_strong_ref(mxf, &package->descriptor_ref, Descriptor);
        if (descriptor)
            return component;
    }
    return NULL;
}

static MXFStructuralComponent* mxf_resolve_sourceclip(MXFContext *mxf, UID *strong_ref)
{
    MXFStructuralComponent *component = NULL;

    component = mxf_resolve_strong_ref(mxf, strong_ref, AnyType);
    if (!component)
        return NULL;
    switch (component->type) {
        case SourceClip:
            return component;
        case EssenceGroup:
            return mxf_resolve_essence_group_choice(mxf, (MXFEssenceGroup*) component);
        default:
            break;
    }
    return NULL;
}

static int mxf_parse_package_comments(MXFContext *mxf, AVDictionary **pm, MXFPackage *package)
{
    MXFTaggedValue *tag;
    int size, i;
    char *key = NULL;

    for (i = 0; i < package->comment_count; i++) {
        tag = mxf_resolve_strong_ref(mxf, &package->comment_refs[i], TaggedValue);
        if (!tag || !tag->name || !tag->value)
            continue;

        size = strlen(tag->name) + 8 + 1;
        key = av_mallocz(size);
        if (!key)
            return AVERROR(ENOMEM);

        snprintf(key, size, "comment_%s", tag->name);
        av_dict_set(pm, key, tag->value, AV_DICT_DONT_STRDUP_KEY);
    }
    return 0;
}

static int mxf_parse_physical_source_package(MXFContext *mxf, MXFTrack *source_track, AVStream *st)
{
    MXFPackage *physical_package = NULL;
    MXFTrack *physical_track = NULL;
    MXFStructuralComponent *sourceclip = NULL;
    MXFTimecodeComponent *mxf_tc = NULL;
    int i, j, k;
    AVTimecode tc;
    int flags;
    int64_t start_position;

    for (i = 0; i < source_track->sequence->structural_components_count; i++) {
        sourceclip = mxf_resolve_strong_ref(mxf, &source_track->sequence->structural_components_refs[i], SourceClip);
        if (!sourceclip)
            continue;

        if (!(physical_package = mxf_resolve_source_package(mxf, sourceclip->source_package_ul, sourceclip->source_package_uid)))
            break;

        mxf_add_umid_metadata(&st->metadata, "reel_umid", physical_package);

        /* the name of physical source package is name of the reel or tape */
        if (physical_package->name && physical_package->name[0])
            av_dict_set(&st->metadata, "reel_name", physical_package->name, 0);

        /* the source timecode is calculated by adding the start_position of the sourceclip from the file source package track
         * to the start_frame of the timecode component located on one of the tracks of the physical source package.
         */
        for (j = 0; j < physical_package->tracks_count; j++) {
            if (!(physical_track = mxf_resolve_strong_ref(mxf, &physical_package->tracks_refs[j], Track))) {
                av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track strong ref\n");
                continue;
            }

            if (!(physical_track->sequence = mxf_resolve_strong_ref(mxf, &physical_track->sequence_ref, Sequence))) {
                av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track sequence strong ref\n");
                continue;
            }

            if (physical_track->edit_rate.num <= 0 ||
                physical_track->edit_rate.den <= 0) {
                av_log(mxf->fc, AV_LOG_WARNING,
                       "Invalid edit rate (%d/%d) found on structural"
                       " component #%d, defaulting to 25/1\n",
                       physical_track->edit_rate.num,
                       physical_track->edit_rate.den, i);
                physical_track->edit_rate = (AVRational){25, 1};
            }

            for (k = 0; k < physical_track->sequence->structural_components_count; k++) {
                if (!(mxf_tc = mxf_resolve_timecode_component(mxf, &physical_track->sequence->structural_components_refs[k])))
                    continue;

                flags = mxf_tc->drop_frame == 1 ? AV_TIMECODE_FLAG_DROPFRAME : 0;
                /* scale sourceclip start_position to match physical track edit rate */
                start_position = av_rescale_q(sourceclip->start_position,
                                              physical_track->edit_rate,
                                              source_track->edit_rate);

                if (av_timecode_init(&tc, mxf_tc->rate, flags, start_position + mxf_tc->start_frame, mxf->fc) == 0) {
                    mxf_add_timecode_metadata(&st->metadata, "timecode", &tc);
                    return 0;
                }
            }
        }
    }

    return 0;
}

static int mxf_add_metadata_stream(MXFContext *mxf, MXFTrack *track)
{
    MXFStructuralComponent *component = NULL;
    const MXFCodecUL *codec_ul = NULL;
    MXFPackage tmp_package;
    AVStream *st;
    int j;

    for (j = 0; j < track->sequence->structural_components_count; j++) {
        component = mxf_resolve_sourceclip(mxf, &track->sequence->structural_components_refs[j]);
        if (!component)
            continue;
        break;
    }
    if (!component)
        return 0;

    st = avformat_new_stream(mxf->fc, NULL);
    if (!st) {
        av_log(mxf->fc, AV_LOG_ERROR, "could not allocate metadata stream\n");
        return AVERROR(ENOMEM);
    }

    st->codecpar->codec_type = AVMEDIA_TYPE_DATA;
    st->codecpar->codec_id = AV_CODEC_ID_NONE;
    st->id = track->track_id;

    memcpy(&tmp_package.package_ul, component->source_package_ul, 16);
    memcpy(&tmp_package.package_uid, component->source_package_uid, 16);
    mxf_add_umid_metadata(&st->metadata, "file_package_umid", &tmp_package);
    if (track->name && track->name[0])
        av_dict_set(&st->metadata, "track_name", track->name, 0);

    codec_ul = mxf_get_codec_ul(ff_mxf_data_definition_uls, &track->sequence->data_definition_ul);
    av_dict_set(&st->metadata, "data_type", av_get_media_type_string(codec_ul->id), 0);
    return 0;
}

static int mxf_parse_structural_metadata(MXFContext *mxf)
{
    MXFPackage *material_package = NULL;
    int i, j, k, ret;

    av_log(mxf->fc, AV_LOG_TRACE, "metadata sets count %d\n", mxf->metadata_sets_count);
    /* TODO: handle multiple material packages (OP3x) */
    for (i = 0; i < mxf->packages_count; i++) {
        material_package = mxf_resolve_strong_ref(mxf, &mxf->packages_refs[i], MaterialPackage);
        if (material_package) break;
    }
    if (!material_package) {
        av_log(mxf->fc, AV_LOG_ERROR, "no material package found\n");
        return AVERROR_INVALIDDATA;
    }

    mxf_add_umid_metadata(&mxf->fc->metadata, "material_package_umid", material_package);
    if (material_package->name && material_package->name[0])
        av_dict_set(&mxf->fc->metadata, "material_package_name", material_package->name, 0);
    mxf_parse_package_comments(mxf, &mxf->fc->metadata, material_package);

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

        /* TODO: handle multiple source clips, only finds first valid source clip */
        if(material_track->sequence->structural_components_count > 1)
            av_log(mxf->fc, AV_LOG_WARNING, "material track %d: has %d components\n",
                       material_track->track_id, material_track->sequence->structural_components_count);

        for (j = 0; j < material_track->sequence->structural_components_count; j++) {
            component = mxf_resolve_sourceclip(mxf, &material_track->sequence->structural_components_refs[j]);
            if (!component)
                continue;

            source_package = mxf_resolve_source_package(mxf, component->source_package_ul, component->source_package_uid);
            if (!source_package) {
                av_log(mxf->fc, AV_LOG_TRACE, "material track %d: no corresponding source package found\n", material_track->track_id);
                continue;
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

            for (k = 0; k < mxf->essence_container_data_count; k++) {
                MXFEssenceContainerData *essence_data;

                if (!(essence_data = mxf_resolve_strong_ref(mxf, &mxf->essence_container_data_refs[k], EssenceContainerData))) {
                    av_log(mxf->fc, AV_LOG_TRACE, "could not resolve essence container data strong ref\n");
                    continue;
                }
                if (!memcmp(component->source_package_ul, essence_data->package_ul, sizeof(UID)) && !memcmp(component->source_package_uid, essence_data->package_uid, sizeof(UID))) {
                    source_track->body_sid = essence_data->body_sid;
                    source_track->index_sid = essence_data->index_sid;
                    break;
                }
            }

            if(source_track && component)
                break;
        }
        if (!source_track || !component || !source_package) {
            if((ret = mxf_add_metadata_stream(mxf, material_track)))
                goto fail_and_free;
            continue;
        }

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
        st->id = material_track->track_id;
        st->priv_data = source_track;

        source_package->descriptor = mxf_resolve_strong_ref(mxf, &source_package->descriptor_ref, AnyType);
        descriptor = mxf_resolve_multidescriptor(mxf, source_package->descriptor, source_track->track_id);

        /* A SourceClip from a EssenceGroup may only be a single frame of essence data. The clips duration is then how many
         * frames its suppose to repeat for. Descriptor->duration, if present, contains the real duration of the essence data */
        if (descriptor && descriptor->duration != AV_NOPTS_VALUE)
            source_track->original_duration = st->duration = FFMIN(descriptor->duration, component->duration);
        else
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
        st->codecpar->codec_type = codec_ul->id;

        if (!descriptor) {
            av_log(mxf->fc, AV_LOG_INFO, "source track %d: stream %d, no descriptor found\n", source_track->track_id, st->index);
            continue;
        }
        PRINT_KEY(mxf->fc, "essence codec     ul", descriptor->essence_codec_ul);
        PRINT_KEY(mxf->fc, "essence container ul", descriptor->essence_container_ul);
        essence_container_ul = &descriptor->essence_container_ul;
        source_track->wrapping = (mxf->op == OPAtom) ? ClipWrapped : mxf_get_wrapping_kind(essence_container_ul);
        if (source_track->wrapping == UnknownWrapped)
            av_log(mxf->fc, AV_LOG_INFO, "wrapping of stream %d is unknown\n", st->index);
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
        st->codecpar->codec_id = (enum AVCodecID)codec_ul->id;
        if (st->codecpar->codec_id == AV_CODEC_ID_NONE) {
            codec_ul = mxf_get_codec_ul(ff_mxf_codec_uls, &descriptor->codec_ul);
            st->codecpar->codec_id = (enum AVCodecID)codec_ul->id;
        }

        av_log(mxf->fc, AV_LOG_VERBOSE, "%s: Universal Label: ",
               avcodec_get_name(st->codecpar->codec_id));
        for (k = 0; k < 16; k++) {
            av_log(mxf->fc, AV_LOG_VERBOSE, "%.2x",
                   descriptor->essence_codec_ul[k]);
            if (!(k+1 & 19) || k == 5)
                av_log(mxf->fc, AV_LOG_VERBOSE, ".");
        }
        av_log(mxf->fc, AV_LOG_VERBOSE, "\n");

        mxf_add_umid_metadata(&st->metadata, "file_package_umid", source_package);
        if (source_package->name && source_package->name[0])
            av_dict_set(&st->metadata, "file_package_name", source_package->name, 0);
        if (material_track->name && material_track->name[0])
            av_dict_set(&st->metadata, "track_name", material_track->name, 0);

        mxf_parse_physical_source_package(mxf, source_track, st);

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            source_track->intra_only = mxf_is_intra_only(descriptor);
            container_ul = mxf_get_codec_ul(mxf_picture_essence_container_uls, essence_container_ul);
            if (st->codecpar->codec_id == AV_CODEC_ID_NONE)
                st->codecpar->codec_id = container_ul->id;
            st->codecpar->width = descriptor->width;
            st->codecpar->height = descriptor->height; /* Field height, not frame height */
            switch (descriptor->frame_layout) {
                case FullFrame:
                    st->codecpar->field_order = AV_FIELD_PROGRESSIVE;
                    break;
                case OneField:
                    /* Every other line is stored and needs to be duplicated. */
                    av_log(mxf->fc, AV_LOG_INFO, "OneField frame layout isn't currently supported\n");
                    break; /* The correct thing to do here is fall through, but by breaking we might be
                              able to decode some streams at half the vertical resolution, rather than not al all.
                              It's also for compatibility with the old behavior. */
                case MixedFields:
                    break;
                case SegmentedFrame:
                    st->codecpar->field_order = AV_FIELD_PROGRESSIVE;
                case SeparateFields:
                    av_log(mxf->fc, AV_LOG_DEBUG, "video_line_map: (%d, %d), field_dominance: %d\n",
                           descriptor->video_line_map[0], descriptor->video_line_map[1],
                           descriptor->field_dominance);
                    if ((descriptor->video_line_map[0] > 0) && (descriptor->video_line_map[1] > 0)) {
                        /* Detect coded field order from VideoLineMap:
                         *  (even, even) => bottom field coded first
                         *  (even, odd)  => top field coded first
                         *  (odd, even)  => top field coded first
                         *  (odd, odd)   => bottom field coded first
                         */
                        if ((descriptor->video_line_map[0] + descriptor->video_line_map[1]) % 2) {
                            switch (descriptor->field_dominance) {
                                case MXF_FIELD_DOMINANCE_DEFAULT:
                                case MXF_FIELD_DOMINANCE_FF:
                                    st->codecpar->field_order = AV_FIELD_TT;
                                    break;
                                case MXF_FIELD_DOMINANCE_FL:
                                    st->codecpar->field_order = AV_FIELD_TB;
                                    break;
                                default:
                                    avpriv_request_sample(mxf->fc,
                                                          "Field dominance %d support",
                                                          descriptor->field_dominance);
                            }
                        } else {
                            switch (descriptor->field_dominance) {
                                case MXF_FIELD_DOMINANCE_DEFAULT:
                                case MXF_FIELD_DOMINANCE_FF:
                                    st->codecpar->field_order = AV_FIELD_BB;
                                    break;
                                case MXF_FIELD_DOMINANCE_FL:
                                    st->codecpar->field_order = AV_FIELD_BT;
                                    break;
                                default:
                                    avpriv_request_sample(mxf->fc,
                                                          "Field dominance %d support",
                                                          descriptor->field_dominance);
                            }
                        }
                    }
                    /* Turn field height into frame height. */
                    st->codecpar->height *= 2;
                    break;
                default:
                    av_log(mxf->fc, AV_LOG_INFO, "Unknown frame layout type: %d\n", descriptor->frame_layout);
            }

            if (st->codecpar->codec_id == AV_CODEC_ID_PRORES) {
                switch (descriptor->essence_codec_ul[14]) {
                case 1: st->codecpar->codec_tag = MKTAG('a','p','c','o'); break;
                case 2: st->codecpar->codec_tag = MKTAG('a','p','c','s'); break;
                case 3: st->codecpar->codec_tag = MKTAG('a','p','c','n'); break;
                case 4: st->codecpar->codec_tag = MKTAG('a','p','c','h'); break;
                case 5: st->codecpar->codec_tag = MKTAG('a','p','4','h'); break;
                case 6: st->codecpar->codec_tag = MKTAG('a','p','4','x'); break;
                }
            }

            if (st->codecpar->codec_id == AV_CODEC_ID_RAWVIDEO) {
                st->codecpar->format = descriptor->pix_fmt;
                if (st->codecpar->format == AV_PIX_FMT_NONE) {
                    pix_fmt_ul = mxf_get_codec_ul(ff_mxf_pixel_format_uls,
                                                  &descriptor->essence_codec_ul);
                    st->codecpar->format = (enum AVPixelFormat)pix_fmt_ul->id;
                    if (st->codecpar->format== AV_PIX_FMT_NONE) {
                        st->codecpar->codec_tag = mxf_get_codec_ul(ff_mxf_codec_tag_uls,
                                                                   &descriptor->essence_codec_ul)->id;
                        if (!st->codecpar->codec_tag) {
                            /* support files created before RP224v10 by defaulting to UYVY422
                               if subsampling is 4:2:2 and component depth is 8-bit */
                            if (descriptor->horiz_subsampling == 2 &&
                                descriptor->vert_subsampling == 1 &&
                                descriptor->component_depth == 8) {
                                st->codecpar->format = AV_PIX_FMT_UYVY422;
                            }
                        }
                    }
                }
            }
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
            if (material_track->sequence->origin) {
                av_dict_set_int(&st->metadata, "material_track_origin", material_track->sequence->origin, 0);
            }
            if (source_track->sequence->origin) {
                av_dict_set_int(&st->metadata, "source_track_origin", source_track->sequence->origin, 0);
            }
            if (descriptor->aspect_ratio.num && descriptor->aspect_ratio.den)
                st->display_aspect_ratio = descriptor->aspect_ratio;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            container_ul = mxf_get_codec_ul(mxf_sound_essence_container_uls, essence_container_ul);
            /* Only overwrite existing codec ID if it is unset or A-law, which is the default according to SMPTE RP 224. */
            if (st->codecpar->codec_id == AV_CODEC_ID_NONE || (st->codecpar->codec_id == AV_CODEC_ID_PCM_ALAW && (enum AVCodecID)container_ul->id != AV_CODEC_ID_NONE))
                st->codecpar->codec_id = (enum AVCodecID)container_ul->id;
            st->codecpar->channels = descriptor->channels;

            if (descriptor->sample_rate.den > 0) {
                st->codecpar->sample_rate = descriptor->sample_rate.num / descriptor->sample_rate.den;
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
            if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE) {
                if (descriptor->bits_per_sample > 16 && descriptor->bits_per_sample <= 24)
                    st->codecpar->codec_id = AV_CODEC_ID_PCM_S24LE;
                else if (descriptor->bits_per_sample == 32)
                    st->codecpar->codec_id = AV_CODEC_ID_PCM_S32LE;
            } else if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S16BE) {
                if (descriptor->bits_per_sample > 16 && descriptor->bits_per_sample <= 24)
                    st->codecpar->codec_id = AV_CODEC_ID_PCM_S24BE;
                else if (descriptor->bits_per_sample == 32)
                    st->codecpar->codec_id = AV_CODEC_ID_PCM_S32BE;
            } else if (st->codecpar->codec_id == AV_CODEC_ID_MP2) {
                st->need_parsing = AVSTREAM_PARSE_FULL;
            }
            st->codecpar->bits_per_coded_sample = av_get_bits_per_sample(st->codecpar->codec_id);
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            enum AVMediaType type;
            container_ul = mxf_get_codec_ul(mxf_data_essence_container_uls, essence_container_ul);
            if (st->codecpar->codec_id == AV_CODEC_ID_NONE)
                st->codecpar->codec_id = container_ul->id;
            type = avcodec_get_type(st->codecpar->codec_id);
            if (type == AVMEDIA_TYPE_SUBTITLE)
                st->codecpar->codec_type = type;
            if (container_ul->desc)
                av_dict_set(&st->metadata, "data_type", container_ul->desc, 0);
            if (mxf->eia608_extract &&
                !strcmp(container_ul->desc, "vbi_vanc_smpte_436M")) {
                st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
                st->codecpar->codec_id = AV_CODEC_ID_EIA_608;
            }
        }
        if (descriptor->extradata) {
            if (!ff_alloc_extradata(st->codecpar, descriptor->extradata_size)) {
                memcpy(st->codecpar->extradata, descriptor->extradata, descriptor->extradata_size);
            }
        } else if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
            int coded_width = mxf_get_codec_ul(mxf_intra_only_picture_coded_width,
                                               &descriptor->essence_codec_ul)->id;
            if (coded_width)
                st->codecpar->width = coded_width;
            ret = ff_generate_avci_extradata(st);
            if (ret < 0)
                return ret;
        }
        if (st->codecpar->codec_type != AVMEDIA_TYPE_DATA && source_track->wrapping != FrameWrapped) {
            /* TODO: decode timestamps */
            st->need_parsing = AVSTREAM_PARSE_TIMESTAMPS;
        }
    }

    for (int i = 0; i < mxf->fc->nb_streams; i++) {
        MXFTrack *track1 = mxf->fc->streams[i]->priv_data;
        if (track1 && track1->body_sid) {
            for (int j = i + 1; j < mxf->fc->nb_streams; j++) {
                MXFTrack *track2 = mxf->fc->streams[j]->priv_data;
                if (track2 && track1->body_sid == track2->body_sid && track1->wrapping != track2->wrapping) {
                    if (track1->wrapping == UnknownWrapped)
                        track1->wrapping = track2->wrapping;
                    else if (track2->wrapping == UnknownWrapped)
                        track2->wrapping = track1->wrapping;
                    else
                        av_log(mxf->fc, AV_LOG_ERROR, "stream %d and stream %d have the same BodySID (%d) "
                                                      "with different wrapping\n", i, j, track1->body_sid);
                }
            }
        }
    }

    ret = 0;
fail_and_free:
    return ret;
}

static int64_t mxf_timestamp_to_int64(uint64_t timestamp)
{
    struct tm time = { 0 };
    int msecs;
    time.tm_year = (timestamp >> 48) - 1900;
    time.tm_mon  = (timestamp >> 40 & 0xFF) - 1;
    time.tm_mday = (timestamp >> 32 & 0xFF);
    time.tm_hour = (timestamp >> 24 & 0xFF);
    time.tm_min  = (timestamp >> 16 & 0xFF);
    time.tm_sec  = (timestamp >> 8  & 0xFF);
    msecs        = (timestamp & 0xFF) * 4;

    /* Clip values for legacy reasons. Maybe we should return error instead? */
    time.tm_mon  = av_clip(time.tm_mon,  0, 11);
    time.tm_mday = av_clip(time.tm_mday, 1, 31);
    time.tm_hour = av_clip(time.tm_hour, 0, 23);
    time.tm_min  = av_clip(time.tm_min,  0, 59);
    time.tm_sec  = av_clip(time.tm_sec,  0, 59);
    msecs        = av_clip(msecs, 0, 999);

    return (int64_t)av_timegm(&time) * 1000000 + msecs * 1000;
}

#define SET_STR_METADATA(pb, name, str) do { \
    if ((ret = mxf_read_utf16be_string(pb, size, &str)) < 0) \
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
    if (var && (ret = avpriv_dict_set_timestamp(&s->metadata, name, mxf_timestamp_to_int64(var))) < 0) \
        return ret; \
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

static int mxf_read_preface_metadata(void *arg, AVIOContext *pb, int tag, int size, UID uid, int64_t klv_offset)
{
    MXFContext *mxf = arg;
    AVFormatContext *s = mxf->fc;
    int ret;
    char *str = NULL;

    if (tag >= 0x8000 && (IS_KLV_KEY(uid, mxf_avid_project_name))) {
        SET_STR_METADATA(pb, "project_name", str);
    }
    return 0;
}

static const MXFMetadataReadTableEntry mxf_metadata_read_table[] = {
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x05,0x01,0x00 }, mxf_read_primer_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x03,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x01,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x03,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x03,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x04,0x02,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }, mxf_read_partition_pack },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x2f,0x00 }, mxf_read_preface_metadata },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x30,0x00 }, mxf_read_identification_metadata },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x18,0x00 }, mxf_read_content_storage, 0, AnyType },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x37,0x00 }, mxf_read_package, sizeof(MXFPackage), SourcePackage },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x36,0x00 }, mxf_read_package, sizeof(MXFPackage), MaterialPackage },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x0f,0x00 }, mxf_read_sequence, sizeof(MXFSequence), Sequence },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x05,0x00 }, mxf_read_essence_group, sizeof(MXFEssenceGroup), EssenceGroup},
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x11,0x00 }, mxf_read_source_clip, sizeof(MXFStructuralComponent), SourceClip },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3f,0x00 }, mxf_read_tagged_value, sizeof(MXFTaggedValue), TaggedValue },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x44,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), MultipleDescriptor },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x42,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Generic Sound */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x28,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* CDCI */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x29,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* RGBA */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Wave */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* AES3 */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* MPEG2VideoDescriptor */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x5b,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* VBI - SMPTE 436M */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x5c,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* VANC/VBI - SMPTE 436M */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x5e,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* MPEG2AudioDescriptor */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x64,0x00 }, mxf_read_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* DC Timed Text Descriptor */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3A,0x00 }, mxf_read_track, sizeof(MXFTrack), Track }, /* Static Track */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3B,0x00 }, mxf_read_track, sizeof(MXFTrack), Track }, /* Generic Track */
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x14,0x00 }, mxf_read_timecode_component, sizeof(MXFTimecodeComponent), TimecodeComponent },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x0c,0x00 }, mxf_read_pulldown_component, sizeof(MXFPulldownComponent), PulldownComponent },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x04,0x01,0x02,0x02,0x00,0x00 }, mxf_read_cryptographic_context, sizeof(MXFCryptoContext), CryptoContext },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x10,0x01,0x00 }, mxf_read_index_table_segment, sizeof(MXFIndexTableSegment), IndexTableSegment },
    { { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x23,0x00 }, mxf_read_essence_container_data, sizeof(MXFEssenceContainerData), EssenceContainerData },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, NULL, 0, AnyType },
};

static int mxf_metadataset_init(MXFMetadataSet *ctx, enum MXFMetadataSetType type)
{
    switch (type){
    case MultipleDescriptor:
    case Descriptor:
        ((MXFDescriptor*)ctx)->pix_fmt = AV_PIX_FMT_NONE;
        ((MXFDescriptor*)ctx)->duration = AV_NOPTS_VALUE;
        break;
    default:
        break;
    }
    return 0;
}

static int mxf_read_local_tags(MXFContext *mxf, KLVPacket *klv, MXFMetadataReadFunc *read_child, int ctx_size, enum MXFMetadataSetType type)
{
    AVIOContext *pb = mxf->fc->pb;
    MXFMetadataSet *ctx = ctx_size ? av_mallocz(ctx_size) : mxf;
    uint64_t klv_end = avio_tell(pb) + klv->length;

    if (!ctx)
        return AVERROR(ENOMEM);
    mxf_metadataset_init(ctx, type);
    while (avio_tell(pb) + 4 < klv_end && !avio_feof(pb)) {
        int ret;
        int tag = avio_rb16(pb);
        int size = avio_rb16(pb); /* KLV specified by 0x53 */
        uint64_t next = avio_tell(pb) + size;
        UID uid = {0};

        av_log(mxf->fc, AV_LOG_TRACE, "local tag %#04x size %d\n", tag, size);
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
                    av_log(mxf->fc, AV_LOG_TRACE, "local tag %#04x\n", local_tag);
                    PRINT_KEY(mxf->fc, "uid", uid);
                }
            }
        }
        if (ctx_size && tag == 0x3C0A) {
            avio_read(pb, ctx->uid, 16);
        } else if ((ret = read_child(ctx, pb, tag, size, uid, -1)) < 0) {
            if (ctx_size)
                mxf_free_metadataset(&ctx, 1);
            return ret;
        }

        /* Accept the 64k local set limit being exceeded (Avid). Don't accept
         * it extending past the end of the KLV though (zzuf5.mxf). */
        if (avio_tell(pb) > klv_end) {
            if (ctx_size) {
                ctx->type = type;
                mxf_free_metadataset(&ctx, 1);
            }

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
 * Matches any partition pack key, in other words:
 * - HeaderPartition
 * - BodyPartition
 * - FooterPartition
 * @return non-zero if the key is a partition pack key, zero otherwise
 */
static int mxf_is_partition_pack_key(UID key)
{
    //NOTE: this is a little lax since it doesn't constraint key[14]
    return !memcmp(key, mxf_header_partition_pack_key, 13) &&
            key[13] >= 2 && key[13] <= 4;
}

/**
 * Parses a metadata KLV
 * @return <0 on error, 0 otherwise
 */
static int mxf_parse_klv(MXFContext *mxf, KLVPacket klv, MXFMetadataReadFunc *read,
                                     int ctx_size, enum MXFMetadataSetType type)
{
    AVFormatContext *s = mxf->fc;
    int res;
    if (klv.key[5] == 0x53) {
        res = mxf_read_local_tags(mxf, &klv, read, ctx_size, type);
    } else {
        uint64_t next = avio_tell(s->pb) + klv.length;
        res = read(mxf, s->pb, 0, klv.length, klv.key, klv.offset);

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
    return 0;
}

/**
 * Seeks to the previous partition and parses it, if possible
 * @return <= 0 if we should stop parsing, > 0 if we should keep going
 */
static int mxf_seek_to_previous_partition(MXFContext *mxf)
{
    AVIOContext *pb = mxf->fc->pb;
    KLVPacket klv;
    int64_t current_partition_ofs;
    int ret;

    if (!mxf->current_partition ||
        mxf->run_in + mxf->current_partition->previous_partition <= mxf->last_forward_tell)
        return 0;   /* we've parsed all partitions */

    /* seek to previous partition */
    current_partition_ofs = mxf->current_partition->pack_ofs;   //includes run-in
    avio_seek(pb, mxf->run_in + mxf->current_partition->previous_partition, SEEK_SET);
    mxf->current_partition = NULL;

    av_log(mxf->fc, AV_LOG_TRACE, "seeking to previous partition\n");

    /* Make sure this is actually a PartitionPack, and if so parse it.
     * See deadlock2.mxf
     */
    if ((ret = klv_read_packet(&klv, pb)) < 0) {
        av_log(mxf->fc, AV_LOG_ERROR, "failed to read PartitionPack KLV\n");
        return ret;
    }

    if (!mxf_is_partition_pack_key(klv.key)) {
        av_log(mxf->fc, AV_LOG_ERROR, "PreviousPartition @ %" PRIx64 " isn't a PartitionPack\n", klv.offset);
        return AVERROR_INVALIDDATA;
    }

    /* We can't just check ofs >= current_partition_ofs because PreviousPartition
     * can point to just before the current partition, causing klv_read_packet()
     * to sync back up to it. See deadlock3.mxf
     */
    if (klv.offset >= current_partition_ofs) {
        av_log(mxf->fc, AV_LOG_ERROR, "PreviousPartition for PartitionPack @ %"
               PRIx64 " indirectly points to itself\n", current_partition_ofs);
        return AVERROR_INVALIDDATA;
    }

    if ((ret = mxf_parse_klv(mxf, klv, mxf_read_partition_pack, 0, 0)) < 0)
        return ret;

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
        if (!mxf->footer_partition) {
            av_log(mxf->fc, AV_LOG_TRACE, "no FooterPartition\n");
            return 0;
        }

        av_log(mxf->fc, AV_LOG_TRACE, "seeking to FooterPartition\n");

        /* remember where we were so we don't end up seeking further back than this */
        mxf->last_forward_tell = avio_tell(pb);

        if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            av_log(mxf->fc, AV_LOG_INFO, "file is not seekable - not parsing FooterPartition\n");
            return -1;
        }

        /* seek to FooterPartition and parse backward */
        if ((ret = avio_seek(pb, mxf->run_in + mxf->footer_partition, SEEK_SET)) < 0) {
            av_log(mxf->fc, AV_LOG_ERROR,
                   "failed to seek to FooterPartition @ 0x%" PRIx64
                   " (%"PRId64") - partial file?\n",
                   mxf->run_in + mxf->footer_partition, ret);
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

static MXFWrappingScheme mxf_get_wrapping_by_body_sid(AVFormatContext *s, int body_sid)
{
    for (int i = 0; i < s->nb_streams; i++) {
        MXFTrack *track = s->streams[i]->priv_data;
        if (track && track->body_sid == body_sid && track->wrapping != UnknownWrapped)
            return track->wrapping;
    }
    return UnknownWrapped;
}

/**
 * Figures out the proper offset and length of the essence container in each partition
 */
static void mxf_compute_essence_containers(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int x;

    for (x = 0; x < mxf->partitions_count; x++) {
        MXFPartition *p = &mxf->partitions[x];
        MXFWrappingScheme wrapping;

        if (!p->body_sid)
            continue;       /* BodySID == 0 -> no essence */

        /* for clip wrapped essences we point essence_offset after the KL (usually klv.offset + 20 or 25)
         * otherwise we point essence_offset at the key of the first essence KLV.
         */

        wrapping = (mxf->op == OPAtom) ? ClipWrapped : mxf_get_wrapping_by_body_sid(s, p->body_sid);

        if (wrapping == ClipWrapped) {
            p->essence_offset = p->first_essence_klv.next_klv - p->first_essence_klv.length;
            p->essence_length = p->first_essence_klv.length;
        } else {
            p->essence_offset = p->first_essence_klv.offset;

            /* essence container spans to the next partition */
            if (x < mxf->partitions_count - 1)
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
}

static int is_pcm(enum AVCodecID codec_id)
{
    /* we only care about "normal" PCM codecs until we get samples */
    return codec_id >= AV_CODEC_ID_PCM_S16LE && codec_id < AV_CODEC_ID_PCM_S24DAUD;
}

static MXFIndexTable *mxf_find_index_table(MXFContext *mxf, int index_sid)
{
    int i;
    for (i = 0; i < mxf->nb_index_tables; i++)
        if (mxf->index_tables[i].index_sid == index_sid)
            return &mxf->index_tables[i];
    return NULL;
}

/**
 * Deal with the case where for some audio atoms EditUnitByteCount is
 * very small (2, 4..). In those cases we should read more than one
 * sample per call to mxf_read_packet().
 */
static void mxf_compute_edit_units_per_packet(MXFContext *mxf, AVStream *st)
{
    MXFTrack *track = st->priv_data;
    MXFIndexTable *t;

    if (!track)
        return;
    track->edit_units_per_packet = 1;
    if (track->wrapping != ClipWrapped)
        return;

    t = mxf_find_index_table(mxf, track->index_sid);

    /* expect PCM with exactly one index table segment and a small (< 32) EUBC */
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO         ||
        !is_pcm(st->codecpar->codec_id)                        ||
        !t                                                     ||
        t->nb_segments != 1                                    ||
        t->segments[0]->edit_unit_byte_count >= 32)
        return;

    /* arbitrarily default to 48 kHz PAL audio frame size */
    /* TODO: We could compute this from the ratio between the audio
     *       and video edit rates for 48 kHz NTSC we could use the
     *       1802-1802-1802-1802-1801 pattern. */
    track->edit_units_per_packet = FFMAX(1, track->edit_rate.num / track->edit_rate.den / 25);
}

/**
 * Deal with the case where ClipWrapped essences does not have any IndexTableSegments.
 */
static int mxf_handle_missing_index_segment(MXFContext *mxf, AVStream *st)
{
    MXFTrack *track = st->priv_data;
    MXFIndexTableSegment *segment = NULL;
    MXFPartition *p = NULL;
    int essence_partition_count = 0;
    int edit_unit_byte_count = 0;
    int i, ret;

    if (!track || track->wrapping != ClipWrapped)
        return 0;

    /* check if track already has an IndexTableSegment */
    for (i = 0; i < mxf->metadata_sets_count; i++) {
        if (mxf->metadata_sets[i]->type == IndexTableSegment) {
            MXFIndexTableSegment *s = (MXFIndexTableSegment*)mxf->metadata_sets[i];
            if (s->body_sid == track->body_sid)
                return 0;
        }
    }

    /* find the essence partition */
    for (i = 0; i < mxf->partitions_count; i++) {
        /* BodySID == 0 -> no essence */
        if (mxf->partitions[i].body_sid != track->body_sid)
            continue;

        p = &mxf->partitions[i];
        essence_partition_count++;
    }

    /* only handle files with a single essence partition */
    if (essence_partition_count != 1)
        return 0;

    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && is_pcm(st->codecpar->codec_id)) {
        edit_unit_byte_count = (av_get_bits_per_sample(st->codecpar->codec_id) * st->codecpar->channels) >> 3;
    } else if (st->duration > 0 && p->first_essence_klv.length > 0 && p->first_essence_klv.length % st->duration == 0) {
        edit_unit_byte_count = p->first_essence_klv.length / st->duration;
    }

    if (edit_unit_byte_count <= 0)
        return 0;

    av_log(mxf->fc, AV_LOG_WARNING, "guessing index for stream %d using edit unit byte count %d\n", st->index, edit_unit_byte_count);

    if (!(segment = av_mallocz(sizeof(*segment))))
        return AVERROR(ENOMEM);

    if ((ret = mxf_add_metadata_set(mxf, segment))) {
        mxf_free_metadataset((MXFMetadataSet**)&segment, 1);
        return ret;
    }

    /* Make sure we have nonzero unique index_sid, body_sid will be ok, because
     * using the same SID for index is forbidden in MXF. */
    if (!track->index_sid)
        track->index_sid = track->body_sid;

    segment->type = IndexTableSegment;
    /* stream will be treated as small EditUnitByteCount */
    segment->edit_unit_byte_count = edit_unit_byte_count;
    segment->index_start_position = 0;
    segment->index_duration = st->duration;
    segment->index_edit_rate = av_inv_q(st->time_base);
    segment->index_sid = track->index_sid;
    segment->body_sid = p->body_sid;
    return 0;
}

static void mxf_read_random_index_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    uint32_t length;
    int64_t file_size, max_rip_length, min_rip_length;
    KLVPacket klv;

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return;

    file_size = avio_size(s->pb);

    /* S377m says to check the RIP length for "silly" values, without defining "silly".
     * The limit below assumes a file with nothing but partition packs and a RIP.
     * Before changing this, consider that a muxer may place each sample in its own partition.
     *
     * 105 is the size of the smallest possible PartitionPack
     * 12 is the size of each RIP entry
     * 28 is the size of the RIP header and footer, assuming an 8-byte BER
     */
    max_rip_length = ((file_size - mxf->run_in) / 105) * 12 + 28;
    max_rip_length = FFMIN(max_rip_length, INT_MAX); //2 GiB and up is also silly

    /* We're only interested in RIPs with at least two entries.. */
    min_rip_length = 16+1+24+4;

    /* See S377m section 11 */
    avio_seek(s->pb, file_size - 4, SEEK_SET);
    length = avio_rb32(s->pb);

    if (length < min_rip_length || length > max_rip_length)
        goto end;
    avio_seek(s->pb, file_size - length, SEEK_SET);
    if (klv_read_packet(&klv, s->pb) < 0 ||
        !IS_KLV_KEY(klv.key, mxf_random_index_pack_key))
        goto end;
    if (klv.next_klv != file_size || klv.length <= 4 || (klv.length - 4) % 12) {
        av_log(s, AV_LOG_WARNING, "Invalid RIP KLV length\n");
        goto end;
    }

    avio_skip(s->pb, klv.length - 12);
    mxf->footer_partition = avio_rb64(s->pb);

    /* sanity check */
    if (mxf->run_in + mxf->footer_partition >= file_size) {
        av_log(s, AV_LOG_WARNING, "bad FooterPartition in RIP - ignoring\n");
        mxf->footer_partition = 0;
    }

end:
    avio_seek(s->pb, mxf->run_in, SEEK_SET);
}

static int mxf_read_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    KLVPacket klv;
    int64_t essence_offset = 0;
    int ret;

    mxf->last_forward_tell = INT64_MAX;

    if (!mxf_read_sync(s->pb, mxf_header_partition_pack_key, 14)) {
        av_log(s, AV_LOG_ERROR, "could not find header partition pack key\n");
        return AVERROR_INVALIDDATA;
    }
    avio_seek(s->pb, -14, SEEK_CUR);
    mxf->fc = s;
    mxf->run_in = avio_tell(s->pb);

    mxf_read_random_index_pack(s);

    while (!avio_feof(s->pb)) {
        const MXFMetadataReadTableEntry *metadata;

        if (klv_read_packet(&klv, s->pb) < 0) {
            /* EOF - seek to previous partition or stop */
            if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
                break;
            else
                continue;
        }

        PRINT_KEY(s, "read header", klv.key);
        av_log(s, AV_LOG_TRACE, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
        if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key) ||
            IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_canopus_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_avid_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_system_item_key_cp) ||
            IS_KLV_KEY(klv.key, mxf_system_item_key_gc)) {

            if (!mxf->current_partition) {
                av_log(mxf->fc, AV_LOG_ERROR, "found essence prior to first PartitionPack\n");
                return AVERROR_INVALIDDATA;
            }

            if (!mxf->current_partition->first_essence_klv.offset)
                mxf->current_partition->first_essence_klv = klv;

            if (!essence_offset)
                essence_offset = klv.offset;

            /* seek to footer, previous partition or stop */
            if (mxf_parse_handle_essence(mxf) <= 0)
                break;
            continue;
        } else if (mxf_is_partition_pack_key(klv.key) && mxf->current_partition) {
            /* next partition pack - keep going, seek to previous partition or stop */
            if(mxf_parse_handle_partition_or_eof(mxf) <= 0)
                break;
            else if (mxf->parsing_backward)
                continue;
            /* we're still parsing forward. proceed to parsing this partition pack */
        }

        for (metadata = mxf_metadata_read_table; metadata->read; metadata++) {
            if (IS_KLV_KEY(klv.key, metadata->key)) {
                if ((ret = mxf_parse_klv(mxf, klv, metadata->read, metadata->ctx_size, metadata->type)) < 0)
                    goto fail;
                break;
            }
        }
        if (!metadata->read) {
            av_log(s, AV_LOG_VERBOSE, "Dark key " PRIxUID "\n",
                            UID_ARG(klv.key));
            avio_skip(s->pb, klv.length);
        }
    }
    /* FIXME avoid seek */
    if (!essence_offset)  {
        av_log(s, AV_LOG_ERROR, "no essence\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }
    avio_seek(s->pb, essence_offset, SEEK_SET);

    /* we need to do this before computing the index tables
     * to be able to fill in zero IndexDurations with st->duration */
    if ((ret = mxf_parse_structural_metadata(mxf)) < 0)
        goto fail;

    for (int i = 0; i < s->nb_streams; i++)
        mxf_handle_missing_index_segment(mxf, s->streams[i]);

    if ((ret = mxf_compute_index_tables(mxf)) < 0)
        goto fail;

    if (mxf->nb_index_tables > 1) {
        /* TODO: look up which IndexSID to use via EssenceContainerData */
        av_log(mxf->fc, AV_LOG_INFO, "got %i index tables - only the first one (IndexSID %i) will be used\n",
               mxf->nb_index_tables, mxf->index_tables[0].index_sid);
    } else if (mxf->nb_index_tables == 0 && mxf->op == OPAtom && (s->error_recognition & AV_EF_EXPLODE)) {
        av_log(mxf->fc, AV_LOG_ERROR, "cannot demux OPAtom without an index\n");
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    mxf_compute_essence_containers(s);

    for (int i = 0; i < s->nb_streams; i++)
        mxf_compute_edit_units_per_packet(mxf, s->streams[i]);

    return 0;
fail:
    mxf_read_close(s);

    return ret;
}

/* Get the edit unit of the next packet from current_offset in a track. The returned edit unit can be original_duration as well! */
static int mxf_get_next_track_edit_unit(MXFContext *mxf, MXFTrack *track, int64_t current_offset, int64_t *edit_unit_out)
{
    int64_t a, b, m, offset;
    MXFIndexTable *t = mxf_find_index_table(mxf, track->index_sid);

    if (!t || track->original_duration <= 0)
        return -1;

    a = -1;
    b = track->original_duration;

    while (b - a > 1) {
        m = (a + b) >> 1;
        if (mxf_edit_unit_absolute_offset(mxf, t, m, track->edit_rate, NULL, &offset, NULL, 0) < 0)
            return -1;
        if (offset < current_offset)
            a = m;
        else
            b = m;
    }

    *edit_unit_out = b;

    return 0;
}

static int64_t mxf_compute_sample_count(MXFContext *mxf, AVStream *st,
                                        int64_t edit_unit)
{
    int i, total = 0, size = 0;
    MXFTrack *track = st->priv_data;
    AVRational time_base = av_inv_q(track->edit_rate);
    AVRational sample_rate = av_inv_q(st->time_base);
    const MXFSamplesPerFrame *spf = NULL;
    int64_t sample_count;

    // For non-audio sample_count equals current edit unit
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
        return edit_unit;

    if ((sample_rate.num / sample_rate.den) == 48000)
        spf = ff_mxf_get_samples_per_frame(mxf->fc, time_base);
    if (!spf) {
        int remainder = (sample_rate.num * time_base.num) %
                        (time_base.den * sample_rate.den);
        if (remainder)
            av_log(mxf->fc, AV_LOG_WARNING,
                   "seeking detected on stream #%d with time base (%d/%d) and "
                   "sample rate (%d/%d), audio pts won't be accurate.\n",
                   st->index, time_base.num, time_base.den,
                   sample_rate.num, sample_rate.den);
        return av_rescale_q(edit_unit, sample_rate, track->edit_rate);
    }

    while (spf->samples_per_frame[size]) {
        total += spf->samples_per_frame[size];
        size++;
    }

    av_assert2(size);

    sample_count = (edit_unit / size) * (uint64_t)total;
    for (i = 0; i < edit_unit % size; i++) {
        sample_count += spf->samples_per_frame[i];
    }

    return sample_count;
}

/**
 * Make sure track->sample_count is correct based on what offset we're currently at.
 * Also determine the next edit unit (or packet) offset.
 * @return next_ofs if OK, <0 on error
 */
static int64_t mxf_set_current_edit_unit(MXFContext *mxf, AVStream *st, int64_t current_offset, int resync)
{
    int64_t next_ofs = -1;
    MXFTrack *track = st->priv_data;
    int64_t edit_unit = av_rescale_q(track->sample_count, st->time_base, av_inv_q(track->edit_rate));
    int64_t new_edit_unit;
    MXFIndexTable *t = mxf_find_index_table(mxf, track->index_sid);

    if (!t || track->wrapping == UnknownWrapped)
        return -1;

    if (mxf_edit_unit_absolute_offset(mxf, t, edit_unit + track->edit_units_per_packet, track->edit_rate, NULL, &next_ofs, NULL, 0) < 0 &&
        (next_ofs = mxf_essence_container_end(mxf, t->body_sid)) <= 0) {
        av_log(mxf->fc, AV_LOG_ERROR, "unable to compute the size of the last packet\n");
        return -1;
    }

    /* check if the next edit unit offset (next_ofs) starts ahead of current_offset */
    if (next_ofs > current_offset)
        return next_ofs;

    if (!resync) {
        av_log(mxf->fc, AV_LOG_ERROR, "cannot find current edit unit for stream %d, invalid index?\n", st->index);
        return -1;
    }

    if (mxf_get_next_track_edit_unit(mxf, track, current_offset + 1, &new_edit_unit) < 0 || new_edit_unit <= 0) {
        av_log(mxf->fc, AV_LOG_ERROR, "failed to find next track edit unit in stream %d\n", st->index);
        return -1;
    }

    new_edit_unit--;
    track->sample_count = mxf_compute_sample_count(mxf, st, new_edit_unit);
    av_log(mxf->fc, AV_LOG_WARNING, "edit unit sync lost on stream %d, jumping from %"PRId64" to %"PRId64"\n", st->index, edit_unit, new_edit_unit);

    return mxf_set_current_edit_unit(mxf, st, current_offset, 0);
}

static int mxf_set_audio_pts(MXFContext *mxf, AVCodecParameters *par,
                             AVPacket *pkt)
{
    AVStream *st = mxf->fc->streams[pkt->stream_index];
    MXFTrack *track = st->priv_data;
    int64_t bits_per_sample = par->bits_per_coded_sample;

    if (!bits_per_sample)
        bits_per_sample = av_get_bits_per_sample(par->codec_id);

    pkt->pts = track->sample_count;

    if (   par->channels <= 0
        || bits_per_sample <= 0
        || par->channels * (int64_t)bits_per_sample < 8)
        track->sample_count = mxf_compute_sample_count(mxf, st, av_rescale_q(track->sample_count, st->time_base, av_inv_q(track->edit_rate)) + 1);
    else
        track->sample_count += pkt->size / (par->channels * (int64_t)bits_per_sample / 8);

    return 0;
}

static int mxf_set_pts(MXFContext *mxf, AVStream *st, AVPacket *pkt)
{
    AVCodecParameters *par = st->codecpar;
    MXFTrack *track = st->priv_data;

    if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
        /* see if we have an index table to derive timestamps from */
        MXFIndexTable *t = mxf_find_index_table(mxf, track->index_sid);

        if (t && track->sample_count < t->nb_ptses) {
            pkt->dts = track->sample_count + t->first_dts;
            pkt->pts = t->ptses[track->sample_count];
        } else if (track->intra_only) {
            /* intra-only -> PTS = EditUnit.
             * let utils.c figure out DTS since it can be < PTS if low_delay = 0 (Sony IMX30) */
            pkt->pts = track->sample_count;
        }
        track->sample_count++;
    } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
        int ret = mxf_set_audio_pts(mxf, par, pkt);
        if (ret < 0)
            return ret;
    } else if (track) {
        pkt->dts = pkt->pts = track->sample_count;
        pkt->duration = 1;
        track->sample_count++;
    }
    return 0;
}

static int mxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    KLVPacket klv;
    MXFContext *mxf = s->priv_data;
    int ret;

    while (1) {
        int64_t max_data_size;
        int64_t pos = avio_tell(s->pb);

        if (pos < mxf->current_klv_data.next_klv - mxf->current_klv_data.length || pos >= mxf->current_klv_data.next_klv) {
            mxf->current_klv_data = (KLVPacket){{0}};
            ret = klv_read_packet(&klv, s->pb);
            if (ret < 0)
                break;
            max_data_size = klv.length;
            pos = klv.next_klv - klv.length;
            PRINT_KEY(s, "read packet", klv.key);
            av_log(s, AV_LOG_TRACE, "size %"PRIu64" offset %#"PRIx64"\n", klv.length, klv.offset);
            if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key)) {
                ret = mxf_decrypt_triplet(s, pkt, &klv);
                if (ret < 0) {
                    av_log(s, AV_LOG_ERROR, "invalid encoded triplet\n");
                    return ret;
                }
                return 0;
            }
        } else {
            klv = mxf->current_klv_data;
            max_data_size = klv.next_klv - pos;
        }
        if (IS_KLV_KEY(klv.key, mxf_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_canopus_essence_element_key) ||
            IS_KLV_KEY(klv.key, mxf_avid_essence_element_key)) {
            int body_sid = find_body_sid_by_absolute_offset(mxf, klv.offset);
            int index = mxf_get_stream_index(s, &klv, body_sid);
            int64_t next_ofs;
            AVStream *st;
            MXFTrack *track;

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

            next_ofs = mxf_set_current_edit_unit(mxf, st, pos, 1);

            if (track->wrapping != FrameWrapped) {
                int64_t size;

                if (next_ofs <= 0) {
                    // If we have no way to packetize the data, then return it in chunks...
                    if (klv.next_klv - klv.length == pos && max_data_size > MXF_MAX_CHUNK_SIZE) {
                        st->need_parsing = AVSTREAM_PARSE_FULL;
                        avpriv_request_sample(s, "Huge KLV without proper index in non-frame wrapped essence");
                    }
                    size = FFMIN(max_data_size, MXF_MAX_CHUNK_SIZE);
                } else {
                    if ((size = next_ofs - pos) <= 0) {
                        av_log(s, AV_LOG_ERROR, "bad size: %"PRId64"\n", size);
                        mxf->current_klv_data = (KLVPacket){{0}};
                        return AVERROR_INVALIDDATA;
                    }
                    // We must not overread, because the next edit unit might be in another KLV
                    if (size > max_data_size)
                        size = max_data_size;
                }

                mxf->current_klv_data = klv;
                klv.offset = pos;
                klv.length = size;
                klv.next_klv = klv.offset + klv.length;
            }

            /* check for 8 channels AES3 element */
            if (klv.key[12] == 0x06 && klv.key[13] == 0x01 && klv.key[14] == 0x10) {
                ret = mxf_get_d10_aes3_packet(s->pb, s->streams[index],
                                              pkt, klv.length);
                if (ret < 0) {
                    av_log(s, AV_LOG_ERROR, "error reading D-10 aes3 frame\n");
                    mxf->current_klv_data = (KLVPacket){{0}};
                    return ret;
                }
            } else if (mxf->eia608_extract &&
                       s->streams[index]->codecpar->codec_id == AV_CODEC_ID_EIA_608) {
                ret = mxf_get_eia608_packet(s, s->streams[index], pkt, klv.length);
                if (ret < 0) {
                    mxf->current_klv_data = (KLVPacket){{0}};
                    return ret;
                }
            } else {
                ret = av_get_packet(s->pb, pkt, klv.length);
                if (ret < 0) {
                    mxf->current_klv_data = (KLVPacket){{0}};
                    return ret;
                }
            }
            pkt->stream_index = index;
            pkt->pos = klv.offset;

            ret = mxf_set_pts(mxf, st, pkt);
            if (ret < 0) {
                mxf->current_klv_data = (KLVPacket){{0}};
                return ret;
            }

            /* seek for truncated packets */
            avio_seek(s->pb, klv.next_klv, SEEK_SET);

            return 0;
        } else {
        skip:
            avio_skip(s->pb, max_data_size);
            mxf->current_klv_data = (KLVPacket){{0}};
        }
    }
    return avio_feof(s->pb) ? AVERROR_EOF : ret;
}

static int mxf_read_close(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i;

    av_freep(&mxf->packages_refs);
    av_freep(&mxf->essence_container_data_refs);

    for (i = 0; i < s->nb_streams; i++)
        s->streams[i]->priv_data = NULL;

    for (i = 0; i < mxf->metadata_sets_count; i++) {
        mxf_free_metadataset(mxf->metadata_sets + i, 1);
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
            av_freep(&mxf->index_tables[i].offsets);
        }
    }
    av_freep(&mxf->index_tables);

    return 0;
}

static int mxf_probe(const AVProbeData *p) {
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

    if (!source_track)
        return 0;

    /* if audio then truncate sample_time to EditRate */
    if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
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
        mxf->current_klv_data = (KLVPacket){{0}};
    } else {
        MXFPartition *partition;

        t = &mxf->index_tables[0];
        if (t->index_sid != source_track->index_sid) {
            /* If the first index table does not belong to the stream, then find a stream which does belong to the index table */
            for (i = 0; i < s->nb_streams; i++) {
                MXFTrack *new_source_track = s->streams[i]->priv_data;
                if (new_source_track && new_source_track->index_sid == t->index_sid) {
                    sample_time = av_rescale_q(sample_time, new_source_track->edit_rate, source_track->edit_rate);
                    source_track = new_source_track;
                    st = s->streams[i];
                    break;
                }
            }
            if (i == s->nb_streams)
                return AVERROR_INVALIDDATA;
        }

        /* clamp above zero, else ff_index_search_timestamp() returns negative
         * this also means we allow seeking before the start */
        sample_time = FFMAX(sample_time, 0);

        if (t->fake_index) {
            /* The first frames may not be keyframes in presentation order, so
             * we have to advance the target to be able to find the first
             * keyframe backwards... */
            if (!(flags & AVSEEK_FLAG_ANY) &&
                (flags & AVSEEK_FLAG_BACKWARD) &&
                t->ptses[0] != AV_NOPTS_VALUE &&
                sample_time < t->ptses[0] &&
                (t->fake_index[t->ptses[0]].flags & AVINDEX_KEYFRAME))
                sample_time = t->ptses[0];

            /* behave as if we have a proper index */
            if ((sample_time = ff_index_search_timestamp(t->fake_index, t->nb_ptses, sample_time, flags)) < 0)
                return sample_time;
            /* get the stored order index from the display order index */
            sample_time += t->offsets[sample_time];
        } else {
            /* no IndexEntryArray (one or more CBR segments)
             * make sure we don't seek past the end */
            sample_time = FFMIN(sample_time, source_track->original_duration - 1);
        }

        if (source_track->wrapping == UnknownWrapped)
            av_log(mxf->fc, AV_LOG_WARNING, "attempted seek in an UnknownWrapped essence\n");

        if ((ret = mxf_edit_unit_absolute_offset(mxf, t, sample_time, source_track->edit_rate, &sample_time, &seekpos, &partition, 1)) < 0)
            return ret;

        ff_update_cur_dts(s, st, sample_time);
        if (source_track->wrapping == ClipWrapped) {
            KLVPacket klv = partition->first_essence_klv;
            if (seekpos < klv.next_klv - klv.length || seekpos >= klv.next_klv) {
                av_log(mxf->fc, AV_LOG_ERROR, "attempted seek out of clip wrapped KLV\n");
                return AVERROR_INVALIDDATA;
            }
            mxf->current_klv_data = klv;
        } else {
            mxf->current_klv_data = (KLVPacket){{0}};
        }
        avio_seek(s->pb, seekpos, SEEK_SET);
    }

    // Update all tracks sample count
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *cur_st = s->streams[i];
        MXFTrack *cur_track = cur_st->priv_data;
        if (cur_track) {
            int64_t track_edit_unit = sample_time;
            if (st != cur_st)
                mxf_get_next_track_edit_unit(mxf, cur_track, seekpos, &track_edit_unit);
            cur_track->sample_count = mxf_compute_sample_count(mxf, cur_st, track_edit_unit);
        }
    }
    return 0;
}

static const AVOption options[] = {
    { "eia608_extract", "extract eia 608 captions from s436m track",
      offsetof(MXFContext, eia608_extract), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1,
      AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "mxf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

AVInputFormat ff_mxf_demuxer = {
    .name           = "mxf",
    .long_name      = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format)"),
    .flags          = AVFMT_SEEK_TO_PTS,
    .priv_data_size = sizeof(MXFContext),
    .read_probe     = mxf_probe,
    .read_header    = mxf_read_header,
    .read_packet    = mxf_read_packet,
    .read_close     = mxf_read_close,
    .read_seek      = mxf_read_seek,
    .priv_class     = &demuxer_class,
};
