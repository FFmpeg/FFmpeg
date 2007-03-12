/*
 * MXF demuxer.
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>.
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

//#define DEBUG

#include "avformat.h"
#include "aes.h"

typedef uint8_t UID[16];

enum MXFMetadataSetType {
    AnyType,
    MaterialPackage,
    SourcePackage,
    SourceClip,
    TimecodeComponent,
    Sequence,
    MultipleDescriptor,
    Descriptor,
    Track,
    EssenceContainerData,
    CryptoContext,
};

typedef struct MXFCryptoContext {
    UID uid;
    enum MXFMetadataSetType type;
    UID context_uid;
    UID source_container_ul;
} MXFCryptoContext;

typedef struct MXFStructuralComponent {
    UID uid;
    enum MXFMetadataSetType type;
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
} MXFSequence;

typedef struct MXFTrack {
    UID uid;
    enum MXFMetadataSetType type;
    MXFSequence *sequence; /* mandatory, and only one */
    UID sequence_ref;
    int track_id;
    uint8_t track_number[4];
    AVRational edit_rate;
} MXFTrack;

typedef struct MXFDescriptor {
    UID uid;
    enum MXFMetadataSetType type;
    UID essence_container_ul;
    UID essence_codec_ul;
    AVRational sample_rate;
    AVRational aspect_ratio;
    int width;
    int height;
    int channels;
    int bits_per_sample;
    UID *sub_descriptors_refs;
    int sub_descriptors_count;
    int linked_track_id;
    uint8_t *extradata;
    int extradata_size;
} MXFDescriptor;

typedef struct MXFPackage {
    UID uid;
    enum MXFMetadataSetType type;
    UID package_uid;
    UID *tracks_refs;
    int tracks_count;
    MXFDescriptor *descriptor; /* only one */
    UID descriptor_ref;
} MXFPackage;

typedef struct MXFEssenceContainerData {
    UID uid;
    enum MXFMetadataSetType type;
    UID linked_package_uid;
} MXFEssenceContainerData;

typedef struct {
    UID uid;
    enum MXFMetadataSetType type;
} MXFMetadataSet;

typedef struct MXFContext {
    UID *packages_refs;
    int packages_count;
    MXFMetadataSet **metadata_sets;
    int metadata_sets_count;
    const uint8_t *sync_key;
    AVFormatContext *fc;
    struct AVAES *aesc;
} MXFContext;

typedef struct KLVPacket {
    UID key;
    offset_t offset;
    uint64_t length;
} KLVPacket;

enum MXFWrappingScheme {
    Frame,
    Clip,
};

typedef struct MXFCodecUL {
    UID uid;
    enum CodecID id;
    enum MXFWrappingScheme wrapping;
} MXFCodecUL;

typedef struct MXFDataDefinitionUL {
    UID uid;
    enum CodecType type;
} MXFDataDefinitionUL;

typedef struct MXFMetadataReadTableEntry {
    const UID key;
    int (*read)();
    int ctx_size;
    enum MXFMetadataSetType type;
} MXFMetadataReadTableEntry;

/* partial keys to match */
static const uint8_t mxf_header_partition_pack_key[]       = { 0x06,0x0e,0x2b,0x34,0x02,0x05,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x02 };
static const uint8_t mxf_essence_element_key[]             = { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01 };
/* complete keys to match */
static const uint8_t mxf_encrypted_triplet_key[]           = { 0x06,0x0e,0x2b,0x34,0x02,0x04,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x7e,0x01,0x00 };
static const uint8_t mxf_encrypted_essence_container[]     = { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x0b,0x01,0x00 };

#define IS_KLV_KEY(x, y) (!memcmp(x, y, sizeof(y)))

#define PRINT_KEY(pc, s, x) dprintf(pc, "%s %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", s, \
                             (x)[0], (x)[1], (x)[2], (x)[3], (x)[4], (x)[5], (x)[6], (x)[7], (x)[8], (x)[9], (x)[10], (x)[11], (x)[12], (x)[13], (x)[14], (x)[15])

static int64_t klv_decode_ber_length(ByteIOContext *pb)
{
    uint64_t size = get_byte(pb);
    if (size & 0x80) { /* long form */
        int bytes_num = size & 0x7f;
        /* SMPTE 379M 5.3.4 guarantee that bytes_num must not exceed 8 bytes */
        if (bytes_num > 8)
            return -1;
        size = 0;
        while (bytes_num--)
            size = size << 8 | get_byte(pb);
    }
    return size;
}

static int klv_read_packet(KLVPacket *klv, ByteIOContext *pb)
{
    klv->offset = url_ftell(pb);
    get_buffer(pb, klv->key, 16);
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
static int mxf_get_d10_aes3_packet(ByteIOContext *pb, AVStream *st, AVPacket *pkt, int64_t length)
{
    uint8_t buffer[61444];
    uint8_t *buf_ptr, *end_ptr, *data_ptr;

    if (length > 61444) /* worst case PAL 1920 samples 8 channels */
        return -1;
    get_buffer(pb, buffer, length);
    av_new_packet(pkt, length);
    data_ptr = pkt->data;
    end_ptr = buffer + length;
    buf_ptr = buffer + 4; /* skip SMPTE 331M header */
    for (; buf_ptr < end_ptr; buf_ptr += 4) {
        if (st->codec->bits_per_sample == 24) {
            data_ptr[0] = (buf_ptr[2] >> 4) | ((buf_ptr[3] & 0x0f) << 4);
            data_ptr[1] = (buf_ptr[1] >> 4) | ((buf_ptr[2] & 0x0f) << 4);
            data_ptr[2] = (buf_ptr[0] >> 4) | ((buf_ptr[1] & 0x0f) << 4);
            data_ptr += 3;
        } else {
            data_ptr[0] = (buf_ptr[2] >> 4) | ((buf_ptr[3] & 0x0f) << 4);
            data_ptr[1] = (buf_ptr[1] >> 4) | ((buf_ptr[2] & 0x0f) << 4);
            data_ptr += 2;
        }
    }
    pkt->size = data_ptr - pkt->data;
    return 0;
}

static int mxf_decrypt_triplet(AVFormatContext *s, AVPacket *pkt, KLVPacket *klv)
{
    static const uint8_t checkv[16] = {0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b, 0x43, 0x48, 0x55, 0x4b};
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t end = url_ftell(pb) + klv->length;
    uint64_t size;
    uint64_t orig_size;
    uint64_t plaintext_size;
    uint8_t ivec[16];
    uint8_t tmpbuf[16];
    int index;

    if (!mxf->aesc && s->key && s->keylen == 16) {
        mxf->aesc = av_malloc(av_aes_size);
        av_aes_init(mxf->aesc, s->key, 128, 1);
    }
    // crypto context
    url_fskip(pb, klv_decode_ber_length(pb));
    // plaintext offset
    klv_decode_ber_length(pb);
    plaintext_size = get_be64(pb);
    // source klv key
    klv_decode_ber_length(pb);
    get_buffer(pb, klv->key, 16);
    if (!IS_KLV_KEY(klv, mxf_essence_element_key)) goto err_out;
    index = mxf_get_stream_index(s, klv);
    if (index < 0) goto err_out;
    // source size
    klv_decode_ber_length(pb);
    orig_size = get_be64(pb);
    if (orig_size < plaintext_size) goto err_out;
    // enc. code
    size = klv_decode_ber_length(pb);
    if (size < 32 || size - 32 < orig_size) goto err_out;
    get_buffer(pb, ivec, 16);
    get_buffer(pb, tmpbuf, 16);
    if (mxf->aesc)
        av_aes_crypt(mxf->aesc, tmpbuf, tmpbuf, 1, ivec, 1);
    if (memcmp(tmpbuf, checkv, 16))
        av_log(s, AV_LOG_ERROR, "probably incorrect decryption key\n");
    size -= 32;
    av_get_packet(pb, pkt, size);
    size -= plaintext_size;
    if (mxf->aesc)
        av_aes_crypt(mxf->aesc, &pkt->data[plaintext_size],
                     &pkt->data[plaintext_size], size >> 4, ivec, 1);
    pkt->size = orig_size;
    pkt->stream_index = index;
    url_fskip(pb, end - url_ftell(pb));
    return 0;

err_out:
    url_fskip(pb, end - url_ftell(pb));
    return -1;
}

static int mxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    KLVPacket klv;

    while (!url_feof(&s->pb)) {
        if (klv_read_packet(&klv, &s->pb) < 0) {
            av_log(s, AV_LOG_ERROR, "error reading KLV packet\n");
            return -1;
        }
#ifdef DEBUG
        PRINT_KEY(s, "read packet", klv.key);
#endif
        if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key)) {
            int res = mxf_decrypt_triplet(s, pkt, &klv);
            mxf->sync_key = mxf_encrypted_triplet_key;
            if (res < 0) {
                av_log(s, AV_LOG_ERROR, "invalid encoded triplet\n");
                return -1;
            }
            return 0;
        }
        if (IS_KLV_KEY(klv.key, mxf_essence_element_key)) {
            int index = mxf_get_stream_index(s, &klv);
            if (index < 0) {
                av_log(s, AV_LOG_ERROR, "error getting stream index\n");
                url_fskip(&s->pb, klv.length);
                return -1;
            }
            /* check for 8 channels AES3 element */
            if (klv.key[12] == 0x06 && klv.key[13] == 0x01 && klv.key[14] == 0x10) {
                if (mxf_get_d10_aes3_packet(&s->pb, s->streams[index], pkt, klv.length) < 0) {
                    av_log(s, AV_LOG_ERROR, "error reading D-10 aes3 frame\n");
                    return -1;
                }
            } else
                av_get_packet(&s->pb, pkt, klv.length);
            pkt->stream_index = index;
            return 0;
        } else
            url_fskip(&s->pb, klv.length);
    }
    return AVERROR_IO;
}

static int mxf_add_metadata_set(MXFContext *mxf, void *metadata_set)
{
    mxf->metadata_sets = av_realloc(mxf->metadata_sets, (mxf->metadata_sets_count + 1) * sizeof(*mxf->metadata_sets));
    mxf->metadata_sets[mxf->metadata_sets_count] = metadata_set;
    mxf->metadata_sets_count++;
    return 0;
}

static int mxf_read_metadata_cryptographic_context(MXFCryptoContext *cryptocontext, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0xFFFE:
        get_buffer(pb, cryptocontext->context_uid, 16);
        break;
    case 0xFFFD:
        get_buffer(pb, cryptocontext->source_container_ul, 16);
        break;
    }
    return 0;
}

static int mxf_read_metadata_content_storage(MXFContext *mxf, ByteIOContext *pb, int tag)
{
    switch (tag) {
    case 0x1901:
        mxf->packages_count = get_be32(pb);
        if (mxf->packages_count >= UINT_MAX / sizeof(UID))
            return -1;
        mxf->packages_refs = av_malloc(mxf->packages_count * sizeof(UID));
        url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
        get_buffer(pb, (uint8_t *)mxf->packages_refs, mxf->packages_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_metadata_source_clip(MXFStructuralComponent *source_clip, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0x0202:
        source_clip->duration = get_be64(pb);
        break;
    case 0x1201:
        source_clip->start_position = get_be64(pb);
        break;
    case 0x1101:
        /* UMID, only get last 16 bytes */
        url_fskip(pb, 16);
        get_buffer(pb, source_clip->source_package_uid, 16);
        break;
    case 0x1102:
        source_clip->source_track_id = get_be32(pb);
        break;
    }
    return 0;
}

static int mxf_read_metadata_material_package(MXFPackage *package, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0x4403:
        package->tracks_count = get_be32(pb);
        if (package->tracks_count >= UINT_MAX / sizeof(UID))
            return -1;
        package->tracks_refs = av_malloc(package->tracks_count * sizeof(UID));
        url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
        get_buffer(pb, (uint8_t *)package->tracks_refs, package->tracks_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_metadata_track(MXFTrack *track, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0x4801:
        track->track_id = get_be32(pb);
        break;
    case 0x4804:
        get_buffer(pb, track->track_number, 4);
        break;
    case 0x4B01:
        track->edit_rate.den = get_be32(pb);
        track->edit_rate.num = get_be32(pb);
        break;
    case 0x4803:
        get_buffer(pb, track->sequence_ref, 16);
        break;
    }
    return 0;
}

static int mxf_read_metadata_sequence(MXFSequence *sequence, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0x0202:
        sequence->duration = get_be64(pb);
        break;
    case 0x0201:
        get_buffer(pb, sequence->data_definition_ul, 16);
        break;
    case 0x1001:
        sequence->structural_components_count = get_be32(pb);
        if (sequence->structural_components_count >= UINT_MAX / sizeof(UID))
            return -1;
        sequence->structural_components_refs = av_malloc(sequence->structural_components_count * sizeof(UID));
        url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
        get_buffer(pb, (uint8_t *)sequence->structural_components_refs, sequence->structural_components_count * sizeof(UID));
        break;
    }
    return 0;
}

static int mxf_read_metadata_source_package(MXFPackage *package, ByteIOContext *pb, int tag)
{
    switch(tag) {
    case 0x4403:
        package->tracks_count = get_be32(pb);
        if (package->tracks_count >= UINT_MAX / sizeof(UID))
            return -1;
        package->tracks_refs = av_malloc(package->tracks_count * sizeof(UID));
        url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
        get_buffer(pb, (uint8_t *)package->tracks_refs, package->tracks_count * sizeof(UID));
        break;
    case 0x4401:
        /* UMID, only get last 16 bytes */
        url_fskip(pb, 16);
        get_buffer(pb, package->package_uid, 16);
        break;
    case 0x4701:
        get_buffer(pb, package->descriptor_ref, 16);
        break;
    }
    return 0;
}

static void mxf_read_metadata_pixel_layout(ByteIOContext *pb, MXFDescriptor *descriptor)
{
    int code;

    do {
        code = get_byte(pb);
        dprintf(NULL, "pixel layout: code 0x%x\n", code);
        switch (code) {
        case 0x52: /* R */
            descriptor->bits_per_sample += get_byte(pb);
            break;
        case 0x47: /* G */
            descriptor->bits_per_sample += get_byte(pb);
            break;
        case 0x42: /* B */
            descriptor->bits_per_sample += get_byte(pb);
            break;
        default:
            get_byte(pb);
        }
    } while (code != 0); /* SMPTE 377M E.2.46 */
}

static int mxf_read_metadata_generic_descriptor(MXFDescriptor *descriptor, ByteIOContext *pb, int tag, int size)
{
    switch(tag) {
    case 0x3F01:
        descriptor->sub_descriptors_count = get_be32(pb);
        if (descriptor->sub_descriptors_count >= UINT_MAX / sizeof(UID))
            return -1;
        descriptor->sub_descriptors_refs = av_malloc(descriptor->sub_descriptors_count * sizeof(UID));
        url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
        get_buffer(pb, (uint8_t *)descriptor->sub_descriptors_refs, descriptor->sub_descriptors_count * sizeof(UID));
        break;
    case 0x3004:
        get_buffer(pb, descriptor->essence_container_ul, 16);
        break;
    case 0x3006:
        descriptor->linked_track_id = get_be32(pb);
        break;
    case 0x3201: /* PictureEssenceCoding */
        get_buffer(pb, descriptor->essence_codec_ul, 16);
        break;
    case 0x3203:
        descriptor->width = get_be32(pb);
        break;
    case 0x3202:
        descriptor->height = get_be32(pb);
        break;
    case 0x320E:
        descriptor->aspect_ratio.num = get_be32(pb);
        descriptor->aspect_ratio.den = get_be32(pb);
        break;
    case 0x3D03:
        descriptor->sample_rate.num = get_be32(pb);
        descriptor->sample_rate.den = get_be32(pb);
        break;
    case 0x3D06: /* SoundEssenceCompression */
        get_buffer(pb, descriptor->essence_codec_ul, 16);
        break;
    case 0x3D07:
        descriptor->channels = get_be32(pb);
        break;
    case 0x3D01:
        descriptor->bits_per_sample = get_be32(pb);
        break;
    case 0x3401:
        mxf_read_metadata_pixel_layout(pb, descriptor);
        break;
    case 0x8201: /* Private tag used by SONY C0023S01.mxf */
        descriptor->extradata = av_malloc(size);
        descriptor->extradata_size = size;
        get_buffer(pb, descriptor->extradata, size);
        break;
    }
    return 0;
}

/* SMPTE RP224 http://www.smpte-ra.org/mdd/index.html */
static const MXFDataDefinitionUL mxf_data_definition_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x01,0x03,0x02,0x02,0x01,0x00,0x00,0x00 }, CODEC_TYPE_VIDEO },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x01,0x03,0x02,0x02,0x02,0x00,0x00,0x00 }, CODEC_TYPE_AUDIO },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x05,0x01,0x03,0x02,0x02,0x02,0x02,0x00,0x00 }, CODEC_TYPE_AUDIO },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },  CODEC_TYPE_DATA },
};

static const MXFCodecUL mxf_codec_uls[] = {
    /* PictureEssenceCoding */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x02,0x02,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* 422P@ML I-Frame */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x04,0x02,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* 422P@HL I-Frame */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x04,0x03,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* 422P@HL Long GoP */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x01,0x11,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* MP@ML Long GoP */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x02,0x03,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* 422P@ML Long GoP */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x03,0x03,0x00 }, CODEC_ID_MPEG2VIDEO, Frame }, /* MP@HL Long GoP */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x20,0x02,0x03 },      CODEC_ID_MPEG4, Frame }, /* XDCAM proxy_pal030926.mxf */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x20,0x02,0x04 },      CODEC_ID_MPEG4, Frame }, /* XDCAM Proxy C0023S01.mxf */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x05 }, CODEC_ID_MPEG2VIDEO, Frame }, /* D-10 30Mbps PAL */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x01 }, CODEC_ID_MPEG2VIDEO, Frame }, /* D-10 50Mbps PAL */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x04,0x00 },    CODEC_ID_DVVIDEO, Frame }, /* DVCPRO50 PAL */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x02,0x00 },    CODEC_ID_DVVIDEO, Frame }, /* DVCPRO25 PAL */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x01,0x02,0x00 },    CODEC_ID_DVVIDEO, Frame }, /* DV25 IEC PAL */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x07,0x04,0x01,0x02,0x02,0x03,0x01,0x01,0x00 },   CODEC_ID_JPEG2000, Frame }, /* JPEG2000 Codestream */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x01,0x7F,0x00,0x00,0x00 },   CODEC_ID_RAWVIDEO, Frame }, /* Uncompressed */
    /* SoundEssenceCompression */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },  CODEC_ID_PCM_S16LE, Frame }, /* Uncompressed */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x7F,0x00,0x00,0x00 },  CODEC_ID_PCM_S16LE, Frame },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x07,0x04,0x02,0x02,0x01,0x7E,0x00,0x00,0x00 },  CODEC_ID_PCM_S16BE, Frame }, /* From Omneon MXF file */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x02,0x03,0x01,0x01,0x00 },   CODEC_ID_PCM_ALAW, Frame },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x04,0x04,0x02,0x02,0x02,0x03,0x01,0x01,0x00 },   CODEC_ID_PCM_ALAW, Frame }, /* XDCAM Proxy C0023S01.mxf */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x02,0x03,0x02,0x01,0x00 },        CODEC_ID_AC3, Frame },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x02,0x03,0x02,0x05,0x00 },        CODEC_ID_MP2, Frame }, /* MP2 or MP3 */
  //{ { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x02,0x03,0x02,0x1C,0x00 },    CODEC_ID_DOLBY_E, Frame }, /* Dolby-E */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },       CODEC_ID_NONE, Frame },
};

static const MXFCodecUL mxf_picture_essence_container_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x60,0x01 }, CODEC_ID_MPEG2VIDEO, Frame }, /* MPEG-ES Frame wrapped */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0xe0,0x02 }, CODEC_ID_MPEG2VIDEO,  Clip }, /* MPEG-ES Clip wrapped, 0xe0 MPV stream id */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x04,0x61,0x07 }, CODEC_ID_MPEG2VIDEO,  Clip }, /* MPEG-ES Custom wrapped, 0x61 ??? stream id */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },       CODEC_ID_NONE, Frame },
};

static const MXFCodecUL mxf_sound_essence_container_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x01,0x00 },  CODEC_ID_PCM_S16LE, Frame }, /* BWF Frame wrapped */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x03,0x00 },  CODEC_ID_PCM_S16LE, Frame }, /* AES Frame wrapped */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x40,0x01 },        CODEC_ID_MP2, Frame }, /* MPEG-ES Frame wrapped, 0x40 ??? stream id */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0xc0,0x01 },        CODEC_ID_MP2, Frame }, /* MPEG-ES Frame wrapped, 0xc0 MPA stream id */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0xc0,0x02 },        CODEC_ID_MP2,  Clip }, /* MPEG-ES Clip wrapped, 0xc0 MPA stream id */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x05,0x01 },  CODEC_ID_PCM_S16BE, Frame }, /* D-10 Mapping 30Mbps PAL Extended Template */
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 },  CODEC_ID_PCM_S16BE, Frame }, /* D-10 Mapping 50Mbps PAL Extended Template */
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },       CODEC_ID_NONE, Frame },
};

static const MXFCodecUL *mxf_get_codec_ul(const MXFCodecUL *uls, UID *uid)
{
    while (uls->id != CODEC_ID_NONE) {
        if(!memcmp(uls->uid, *uid, 16))
            break;
        uls++;
    }
    return uls;
}

static enum CodecType mxf_get_codec_type(const MXFDataDefinitionUL *uls, UID *uid)
{
    while (uls->type != CODEC_TYPE_DATA) {
        if(!memcmp(uls->uid, *uid, 16))
            break;
        uls++;
    }
    return uls->type;
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

static int mxf_parse_structural_metadata(MXFContext *mxf)
{
    MXFPackage *material_package = NULL;
    MXFPackage *temp_package = NULL;
    int i, j, k;

    dprintf(mxf->fc, "metadata sets count %d\n", mxf->metadata_sets_count);
    /* TODO: handle multiple material packages (OP3x) */
    for (i = 0; i < mxf->packages_count; i++) {
        material_package = mxf_resolve_strong_ref(mxf, &mxf->packages_refs[i], MaterialPackage);
        if (material_package) break;
    }
    if (!material_package) {
        av_log(mxf->fc, AV_LOG_ERROR, "no material package found\n");
        return -1;
    }

    for (i = 0; i < material_package->tracks_count; i++) {
        MXFPackage *source_package = NULL;
        MXFTrack *material_track = NULL;
        MXFTrack *source_track = NULL;
        MXFTrack *temp_track = NULL;
        MXFDescriptor *descriptor = NULL;
        MXFStructuralComponent *component = NULL;
        UID *essence_container_ul = NULL;
        const MXFCodecUL *codec_ul = NULL;
        const MXFCodecUL *container_ul = NULL;
        AVStream *st;

        if (!(material_track = mxf_resolve_strong_ref(mxf, &material_package->tracks_refs[i], Track))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve material track strong ref\n");
            continue;
        }

        if (!(material_track->sequence = mxf_resolve_strong_ref(mxf, &material_track->sequence_ref, Sequence))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve material track sequence strong ref\n");
            return -1;
        }

        /* TODO: handle multiple source clips */
        for (j = 0; j < material_track->sequence->structural_components_count; j++) {
            /* TODO: handle timecode component */
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
                av_log(mxf->fc, AV_LOG_ERROR, "material track %d: no corresponding source package found\n", material_track->track_id);
                break;
            }
            for (k = 0; k < source_package->tracks_count; k++) {
                if (!(temp_track = mxf_resolve_strong_ref(mxf, &source_package->tracks_refs[k], Track))) {
                    av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track strong ref\n");
                    return -1;
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
        if (!source_track)
            continue;

        st = av_new_stream(mxf->fc, source_track->track_id);
        st->priv_data = source_track;
        st->duration = component->duration;
        if (st->duration == -1)
            st->duration = AV_NOPTS_VALUE;
        st->start_time = component->start_position;
        av_set_pts_info(st, 64, material_track->edit_rate.num, material_track->edit_rate.den);

        if (!(source_track->sequence = mxf_resolve_strong_ref(mxf, &source_track->sequence_ref, Sequence))) {
            av_log(mxf->fc, AV_LOG_ERROR, "could not resolve source track sequence strong ref\n");
            return -1;
        }

#ifdef DEBUG
        PRINT_KEY(mxf->fc, "data definition   ul", source_track->sequence->data_definition_ul);
#endif
        st->codec->codec_type = mxf_get_codec_type(mxf_data_definition_uls, &source_track->sequence->data_definition_ul);

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
#ifdef DEBUG
        PRINT_KEY(mxf->fc, "essence codec     ul", descriptor->essence_codec_ul);
        PRINT_KEY(mxf->fc, "essence container ul", descriptor->essence_container_ul);
#endif
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
        codec_ul = mxf_get_codec_ul(mxf_codec_uls, &descriptor->essence_codec_ul);
        st->codec->codec_id = codec_ul->id;
        if (descriptor->extradata) {
            st->codec->extradata = descriptor->extradata;
            st->codec->extradata_size = descriptor->extradata_size;
        }
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            container_ul = mxf_get_codec_ul(mxf_picture_essence_container_uls, essence_container_ul);
            if (st->codec->codec_id == CODEC_ID_NONE)
                st->codec->codec_id = container_ul->id;
            st->codec->width = descriptor->width;
            st->codec->height = descriptor->height;
            st->codec->bits_per_sample = descriptor->bits_per_sample; /* Uncompressed */
            st->need_parsing = 2; /* only parse headers */
        } else if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            container_ul = mxf_get_codec_ul(mxf_sound_essence_container_uls, essence_container_ul);
            if (st->codec->codec_id == CODEC_ID_NONE)
                st->codec->codec_id = container_ul->id;
            st->codec->channels = descriptor->channels;
            st->codec->bits_per_sample = descriptor->bits_per_sample;
            st->codec->sample_rate = descriptor->sample_rate.num / descriptor->sample_rate.den;
            /* TODO: implement CODEC_ID_RAWAUDIO */
            if (st->codec->codec_id == CODEC_ID_PCM_S16LE) {
                if (descriptor->bits_per_sample == 24)
                    st->codec->codec_id = CODEC_ID_PCM_S24LE;
                else if (descriptor->bits_per_sample == 32)
                    st->codec->codec_id = CODEC_ID_PCM_S32LE;
            } else if (st->codec->codec_id == CODEC_ID_PCM_S16BE) {
                if (descriptor->bits_per_sample == 24)
                    st->codec->codec_id = CODEC_ID_PCM_S24BE;
                else if (descriptor->bits_per_sample == 32)
                    st->codec->codec_id = CODEC_ID_PCM_S32BE;
                if (descriptor->essence_container_ul[13] == 0x01) /* D-10 Mapping */
                    st->codec->channels = 8; /* force channels to 8 */
            } else if (st->codec->codec_id == CODEC_ID_MP2) {
                st->need_parsing = 1;
            }
        }
        if (container_ul && container_ul->wrapping == Clip) {
            dprintf(mxf->fc, "stream %d: clip wrapped essence\n", st->index);
            st->need_parsing = 1;
        }
    }
    return 0;
}

static const MXFMetadataReadTableEntry mxf_metadata_read_table[] = {
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x18,0x00 }, mxf_read_metadata_content_storage, 0, AnyType },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x37,0x00 }, mxf_read_metadata_source_package, sizeof(MXFPackage), SourcePackage },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x36,0x00 }, mxf_read_metadata_material_package, sizeof(MXFPackage), MaterialPackage },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x0F,0x00 }, mxf_read_metadata_sequence, sizeof(MXFSequence), Sequence },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x11,0x00 }, mxf_read_metadata_source_clip, sizeof(MXFStructuralComponent), SourceClip },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x44,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), MultipleDescriptor },
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x42,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Generic Sound */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x28,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* CDCI */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x29,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* RGBA */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* MPEG 2 Video */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* Wave */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 }, mxf_read_metadata_generic_descriptor, sizeof(MXFDescriptor), Descriptor }, /* AES3 */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3A,0x00 }, mxf_read_metadata_track, sizeof(MXFTrack), Track }, /* Static Track */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x3B,0x00 }, mxf_read_metadata_track, sizeof(MXFTrack), Track }, /* Generic Track */
    { { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x04,0x01,0x02,0x02,0x00,0x00 }, mxf_read_metadata_cryptographic_context, sizeof(MXFCryptoContext), CryptoContext },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 }, NULL, 0, AnyType },
};

static int mxf_read_sync(ByteIOContext *pb, const uint8_t *key, unsigned size)
{
    int i, b;
    for (i = 0; i < size && !url_feof(pb); i++) {
        b = get_byte(pb);
        if (b == key[0])
            i = 0;
        else if (b != key[i])
            i = -1;
    }
    return i == size;
}

static int mxf_read_local_tags(MXFContext *mxf, KLVPacket *klv, int (*read_child)(), int ctx_size, enum MXFMetadataSetType type)
{
    ByteIOContext *pb = &mxf->fc->pb;
    MXFMetadataSet *ctx = ctx_size ? av_mallocz(ctx_size) : mxf;
    uint64_t klv_end= url_ftell(pb) + klv->length;

    while (url_ftell(pb) + 4 < klv_end) {
        int tag = get_be16(pb);
        int size = get_be16(pb); /* KLV specified by 0x53 */
        uint64_t next= url_ftell(pb) + size;

        if (!size) { /* ignore empty tag, needed for some files with empty UMID tag */
            av_log(mxf->fc, AV_LOG_ERROR, "local tag 0x%04X with 0 size\n", tag);
            continue;
        }
        if(ctx_size && tag == 0x3C0A)
            get_buffer(pb, ctx->uid, 16);
        else
            read_child(ctx, pb, tag, size);

        url_fseek(pb, next, SEEK_SET);
    }
    if (ctx_size) ctx->type = type;
    return ctx_size ? mxf_add_metadata_set(mxf, ctx) : 0;
}

static int mxf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MXFContext *mxf = s->priv_data;
    KLVPacket klv;

    mxf->sync_key = mxf_essence_element_key;
    if (!mxf_read_sync(&s->pb, mxf_header_partition_pack_key, 14)) {
        av_log(s, AV_LOG_ERROR, "could not find header partition pack key\n");
        return -1;
    }
    url_fseek(&s->pb, -14, SEEK_CUR);
    mxf->fc = s;
    while (!url_feof(&s->pb)) {
        const MXFMetadataReadTableEntry *metadata;

        if (klv_read_packet(&klv, &s->pb) < 0) {
            av_log(s, AV_LOG_ERROR, "error reading KLV packet\n");
            return -1;
        }
#ifdef DEBUG
        PRINT_KEY(s, "read header", klv.key);
#endif
        if (IS_KLV_KEY(klv.key, mxf_encrypted_triplet_key) ||
            IS_KLV_KEY(klv.key, mxf_essence_element_key)) {
            /* FIXME avoid seek */
            url_fseek(&s->pb, klv.offset, SEEK_SET);
            break;
        }

        for (metadata = mxf_metadata_read_table; metadata->read; metadata++) {
            if (IS_KLV_KEY(klv.key, metadata->key)) {
                if (mxf_read_local_tags(mxf, &klv, metadata->read, metadata->ctx_size, metadata->type) < 0) {
                    av_log(s, AV_LOG_ERROR, "error reading header metadata\n");
                    return -1;
                }
                break;
            }
        }
        if (!metadata->read)
            url_fskip(&s->pb, klv.length);
    }
    return mxf_parse_structural_metadata(mxf);
}

static int mxf_read_close(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i;

    av_freep(&mxf->packages_refs);
    for (i = 0; i < mxf->metadata_sets_count; i++) {
        switch (mxf->metadata_sets[i]->type) {
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
        default:
            break;
        }
        av_freep(&mxf->metadata_sets[i]);
    }
    av_freep(&mxf->metadata_sets);
    av_freep(&mxf->aesc);
    return 0;
}

static int mxf_probe(AVProbeData *p) {
    uint8_t *bufp = p->buf;
    uint8_t *end = p->buf + p->buf_size;

    if (p->buf_size < sizeof(mxf_header_partition_pack_key))
        return 0;

    /* Must skip Run-In Sequence and search for MXF header partition pack key SMPTE 377M 5.5 */
    end -= sizeof(mxf_header_partition_pack_key);
    for (; bufp < end; bufp++) {
        if (IS_KLV_KEY(bufp, mxf_header_partition_pack_key))
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

/* rudimentary binary seek */
/* XXX: use MXF Index */
static int mxf_read_seek(AVFormatContext *s, int stream_index, int64_t sample_time, int flags)
{
    MXFContext *mxf = s->priv_data;
    AVStream *st = s->streams[stream_index];
    int64_t seconds;

    if (!s->bit_rate)
        return -1;
    if (sample_time < 0)
        sample_time = 0;
    seconds = av_rescale(sample_time, st->time_base.num, st->time_base.den);
    url_fseek(&s->pb, (s->bit_rate * seconds) >> 3, SEEK_SET);
    if (!mxf_read_sync(&s->pb, mxf->sync_key, 12))
        return -1;

    /* found KLV key */
    url_fseek(&s->pb, -12, SEEK_CUR);
    av_update_cur_dts(s, st, sample_time);
    return 0;
}

AVInputFormat mxf_demuxer = {
    "mxf",
    "MXF format",
    sizeof(MXFContext),
    mxf_probe,
    mxf_read_header,
    mxf_read_packet,
    mxf_read_close,
    mxf_read_seek,
};
