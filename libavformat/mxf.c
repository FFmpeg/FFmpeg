/*
 * MXF demuxer.
 * Copyright (c) 2006 SmartJog S.A., Baptiste Coudurier <baptiste dot coudurier at smartjog dot com>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
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
 * Material Package tracks does not contain Tracks numbers.
 * Search for Descriptors (Picture, Sound) which contains codec info and parameters.
 * Assign Descriptors to correct Tracks.
 *
 * Preliminary demuxer, only OP1A supported and some files might not work at all.
 */

//#define DEBUG

#include "avformat.h"
#include "dsputil.h"
#include "riff.h"

typedef DECLARE_ALIGNED_16(uint8_t, UID[16]);

typedef struct {
    AVStream *stream;
    UID track_uid;
    UID sequence_uid;
    int track_id;
    int track_number;
} MXFTrack;

typedef struct {
    UID essence_container;
    UID essence_compression;
    enum CodecType codec_type;
    AVRational sample_rate;
    AVRational aspect_ratio;
    int width;
    int height;
    int channels;
    int bits_per_sample;
    int block_align;
    int linked_track_id;
    int kind;
} MXFDescriptor;

typedef struct {
    AVFormatContext *fc;
    MXFTrack *tracks;
    MXFDescriptor *descriptors;
    int descriptors_count;
    int tracks_count;
} MXFContext;

typedef struct {
    UID key;
    offset_t offset;
    uint64_t length;
} KLVPacket;

static const UID mxf_metadata_source_package_key           = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x37, 0x00 };
static const UID mxf_metadata_sequence_key                 = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0F, 0x00 };
static const UID mxf_metadata_generic_sound_descriptor_key = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x42, 0x00 };
static const UID mxf_metadata_cdci_descriptor_key          = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x28, 0x00 };
static const UID mxf_metadata_mpegvideo_descriptor_key     = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x51, 0x00 };
static const UID mxf_metadata_wave_descriptor_key          = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x48, 0x00 };
static const UID mxf_metadata_track_key                    = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x53, 0x01, 0x01, 0x0d, 0x01, 0x01, 0x01, 0x01, 0x01, 0x3b, 0x00 };

/* partial keys to match */
static const uint8_t mxf_header_partition_pack_key[]       = { 0x06, 0x0e, 0x2b, 0x34, 0x02, 0x05, 0x01, 0x01, 0x0d, 0x01, 0x02, 0x01, 0x01, 0x02 };
static const uint8_t mxf_essence_element_key[]             = { 0x06, 0x0e, 0x2b, 0x34, 0x01, 0x02, 0x01 };

#define IS_KLV_KEY(x, y) (!memcmp(x, y, sizeof(y)))

#define PRINT_KEY(x) \
do { \
    int iterpk; \
    for (iterpk = 0; iterpk < 16; iterpk++) { \
        av_log(NULL, AV_LOG_DEBUG, "%02X ", x[iterpk]); \
    } \
    av_log(NULL, AV_LOG_DEBUG, "\n"); \
} while (0); \

static int64_t klv_decode_ber_length(ByteIOContext *pb)
{
    int64_t size = 0;
    uint8_t length = get_byte(pb);
    int type = length >> 7;

    if (type) { /* long form */
        int bytes_num = length & 0x7f;
        /* SMPTE 379M 5.3.4 guarantee that bytes_num must not exceed 8 bytes */
        if (bytes_num > 8)
            return -1;
        while (bytes_num--)
            size = size << 8 | get_byte(pb);
    } else {
        size = length & 0x7f;
    }
    return size;
}

static int klv_read_packet(KLVPacket *klv, ByteIOContext *pb)
{
    klv->offset = url_ftell(pb);
    get_buffer(pb, klv->key, 16);
    klv->length = klv_decode_ber_length(pb);
    if (klv->length == -1)
        return -1;
    else
        return 0;
}

static int mxf_get_stream_index(AVFormatContext *s, KLVPacket *klv)
{
    int id = BE_32(klv->key + 12); /* SMPTE 379M 7.3 */
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->id == id)
            return i;
    }
    return -1;
}

static int mxf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    KLVPacket klv;

    while (!url_feof(&s->pb)) {
        if (klv_read_packet(&klv, &s->pb) < 0)
            return -1;
        if (IS_KLV_KEY(klv.key, mxf_essence_element_key)) {
            av_get_packet(&s->pb, pkt, klv.length);
            pkt->stream_index = mxf_get_stream_index(s, &klv);
            if (pkt->stream_index == -1)
                return -1;
            return 0;
        } else
            url_fskip(&s->pb, klv.length);
    }
    return AVERROR_IO;
}

static int mxf_read_metadata_track(MXFContext *mxf, KLVPacket *klv)
{
    ByteIOContext *pb = &mxf->fc->pb;
    AVRational time_base = (AVRational){0, 0};
    uint8_t sequence_uid[16];
    uint8_t track_uid[16];
    int track_number = 0;
    int track_id = 0;
    int bytes_read = 0;
    int i;

    while (bytes_read < klv->length) {
        int tag = get_be16(pb);
        int size = get_be16(pb); /* SMPTE 336M Table 8 KLV specified length, 0x53 */

        switch (tag) {
        case 0x4801:
            track_id = get_be32(pb);
            break;
        case 0x4804:
            track_number = get_be32(pb);
            break;
        case 0x4B01:
            time_base.den = get_be32(pb);
            time_base.num = get_be32(pb);
            break;
        case 0x4803:
            get_buffer(pb, sequence_uid, 16);
            break;
        case 0x3C0A:
            get_buffer(pb, track_uid, 16);
            break;
        default:
            url_fskip(pb, size);
        }
        bytes_read += size + 4;
    }
    for (i = 0; i < mxf->tracks_count; i++)
        if (!memcmp(track_uid, mxf->tracks[i].track_uid, 16)) {
            mxf->tracks[i].track_id = track_id;
            mxf->tracks[i].track_number = track_number;
            mxf->tracks[i].stream->time_base = time_base;
            mxf->tracks[i].stream->id = track_number;
            memcpy(mxf->tracks[i].sequence_uid, sequence_uid, 16);
        }
    return bytes_read;
}

static int mxf_read_metadata_sequence(MXFContext *mxf, KLVPacket *klv)
{
    ByteIOContext *pb = &mxf->fc->pb;
    uint8_t sequence_uid[16];
    uint8_t data_definition[16];
    uint64_t duration = AV_NOPTS_VALUE;
    int bytes_read = 0;
    int i;

    while (bytes_read < klv->length) {
        int tag = get_be16(pb);
        int size = get_be16(pb); /* KLV specified by 0x53 */

        switch (tag) {
        case 0x3C0A:
            get_buffer(pb, sequence_uid, 16);
            break;
        case 0x0202:
            duration = get_be64(pb);
            break;
        case 0x0201:
            get_buffer(pb, data_definition, 16);
            break;
        default:
            url_fskip(pb, size);
        }
        bytes_read += size + 4;
    }

    for (i = 0; i < mxf->tracks_count; i++)
        if (!memcmp(sequence_uid, mxf->tracks[i].sequence_uid, 16)) {
            mxf->tracks[i].stream->start_time = 0;
            mxf->tracks[i].stream->duration = duration;
            if (data_definition[11] == 0x02 && data_definition[12] == 0x01)
                mxf->tracks[i].stream->codec->codec_type = CODEC_TYPE_VIDEO;
            else if (data_definition[11] == 0x02 && data_definition[12] == 0x02)
                mxf->tracks[i].stream->codec->codec_type = CODEC_TYPE_AUDIO;
            else if (data_definition[11] == 0x01) /* SMPTE 12M Time Code track */
                mxf->tracks[i].stream->codec->codec_type = CODEC_TYPE_DATA;
        }
    return bytes_read;
}

static int mxf_read_metadata_source_package(MXFContext *mxf, KLVPacket *klv)
{
    ByteIOContext *pb = &mxf->fc->pb;
    int tracks_count;
    int bytes_read = 0;
    int i;

    while (bytes_read < klv->length) {
        int tag = get_be16(pb);
        int size = get_be16(pb); /* KLV specified by 0x53 */

        switch (tag) {
        case 0x4403:
            tracks_count = get_be32(pb);
            if(tracks_count >= UINT_MAX / sizeof(*mxf->tracks) ||
               tracks_count >= UINT_MAX / sizeof(*mxf->descriptors))
                return -1;
            mxf->tracks_count += tracks_count; /* op2a contains multiple source packages */
            mxf->tracks = av_realloc(mxf->tracks, mxf->tracks_count * sizeof(*mxf->tracks));
            mxf->descriptors = av_realloc(mxf->descriptors, mxf->tracks_count * sizeof(*mxf->descriptors));
            url_fskip(pb, 4); /* useless size of objects, always 16 according to specs */
            for (i = mxf->tracks_count - tracks_count; i < mxf->tracks_count; i++) {
                mxf->tracks[i].stream = av_new_stream(mxf->fc, 0);
                get_buffer(pb, mxf->tracks[i].track_uid, 16);
            }
            break;
        default:
            url_fskip(pb, size);
        }
        bytes_read += size + 4;
    }
    return bytes_read;
}

static int mxf_read_metadata_descriptor(MXFContext *mxf, KLVPacket *klv)
{
    ByteIOContext *pb = &mxf->fc->pb;
    MXFDescriptor *desc;
    int bytes_read = 0;

    if (mxf->descriptors_count == mxf->tracks_count)
        return -1;
    desc = &mxf->descriptors[mxf->descriptors_count++];
    desc->kind = klv->key[14];
    desc->linked_track_id = -1;

    while (bytes_read < klv->length) {
        int tag = get_be16(pb);
        int size = get_be16(pb); /* KLV specified by 0x53 */

        switch (tag) {
        case 0x3004:
            get_buffer(pb, desc->essence_container, 16);
            break;
        case 0x3006:
            desc->linked_track_id = get_be32(pb);
            break;
        case 0x3201: /* PictureEssenceCoding */
            desc->codec_type = CODEC_TYPE_VIDEO;
            get_buffer(pb, desc->essence_compression, 16);
            break;
        case 0x3203:
            desc->width = get_be32(pb);
            break;
        case 0x3202:
            desc->height = get_be32(pb);
            break;
        case 0x320E:
            desc->aspect_ratio.num = get_be32(pb);
            desc->aspect_ratio.den = get_be32(pb);
            break;
        case 0x3D0A:
            desc->block_align = get_be16(pb);
            break;
        case 0x3D03:
            desc->sample_rate.num = get_be32(pb);
            desc->sample_rate.den = get_be32(pb);
            break;
        case 0x3D06: /* SoundEssenceCompression */
            desc->codec_type = CODEC_TYPE_AUDIO;
            get_buffer(pb, desc->essence_compression, 16);
            break;
        case 0x3D07:
            desc->channels = get_be32(pb);
            break;
        case 0x3D01:
            desc->bits_per_sample = get_be32(pb);
            break;
        default:
            url_fskip(pb, size);
        }
        bytes_read += size + 4;
    }
    return bytes_read;
}

/* SMPTE RP224 http://www.smpte-ra.org/mdd/index.html */
static const CodecTag mxf_sound_essence_labels[] = {
    { CODEC_ID_PCM_S16LE, 0x01000000 },/* Uncompressed Sound Coding */
    { CODEC_ID_PCM_S16LE, 0x017F0000 },/* Uncompressed Sound Coding */
    { CODEC_ID_PCM_S16BE, 0x017E0000 },/* Uncompressed Sound Coding Big Endian*/
    { CODEC_ID_PCM_ALAW,  0x02030101 },
    { CODEC_ID_AC3,       0x02030201 },
  //{ CODEC_ID_MP1,       0x02030104 },
    { CODEC_ID_MP2,       0x02030105 },/* MP2 or MP3 */
  //{ CODEC_ID_MP2,       0x02030106 },/* MPEG-2 Layer 1 */
  //{ CODEC_ID_???,       0x0203010C },/* Dolby E */
  //{ CODEC_ID_???,       0x02030301 },/* MPEG-2 AAC */
    { 0, 0 },
};

static const CodecTag mxf_picture_essence_labels[] = {
    { CODEC_ID_RAWVIDEO,   0x0100 },
    { CODEC_ID_MPEG2VIDEO, 0x0201 },
    { CODEC_ID_DVVIDEO,    0x0202 },
  //{ CODEC_ID_???,        0x0207 },/* D-11 HDCAM */
    { 0, 0 },
};

static const CodecTag mxf_container_picture_labels[] = {
    { CODEC_ID_MPEG2VIDEO, 0x0201 }, /* D-10 Mapping */
    { CODEC_ID_DVVIDEO,    0x0202 }, /* DV Mapping */
  //{ CODEC_ID_???,        0x0203 }, /* HDCAM D-11 Mapping */
    { CODEC_ID_MPEG2VIDEO, 0x0204 }, /* MPEG ES Mapping */
};

static const CodecTag mxf_container_sound_labels[] = {
  //{ CODEC_ID_PCM_S16??,  0x0201 }, /* D-10 Mapping */
    { CODEC_ID_MP2,        0x0204 }, /* MPEG ES Mapping */
    { CODEC_ID_PCM_S16LE,  0x0206 }, /* AES BWF Mapping */
    { CODEC_ID_PCM_ALAW,   0x020A },
    { 0, 0 },
};

static void mxf_resolve_track_descriptor(MXFContext *mxf)
{
    uint32_t container_label;
    uint32_t essence_label;
    int i, j;

    for (i = 0; i < mxf->descriptors_count; i++) {
        for (j = 0; j < mxf->tracks_count; j++) {
            AVStream *st = mxf->tracks[j].stream;
            MXFDescriptor *desc = &mxf->descriptors[i];

            if ((desc->linked_track_id == -1 && st->codec->codec_type == desc->codec_type)
                || desc->linked_track_id == mxf->tracks[j].track_id) {
                if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
                    st->codec->channels = desc->channels;
                    st->codec->bits_per_sample = desc->bits_per_sample;
                    st->codec->block_align = desc->block_align;
                    st->codec->sample_rate = desc->sample_rate.num / desc->sample_rate.den;

                    container_label = BE_16(desc->essence_container + 12);
                    essence_label = BE_32(desc->essence_compression + 11);
                    st->codec->codec_id = codec_get_id(mxf_sound_essence_labels, essence_label);
                    if (st->codec->codec_id == CODEC_ID_PCM_S16LE) {
                        if (desc->bits_per_sample == 24)
                            st->codec->codec_id = CODEC_ID_PCM_S24LE;
                        else if (desc->bits_per_sample == 32)
                            st->codec->codec_id = CODEC_ID_PCM_S32LE;
                    }
                    if (st->codec->codec_id == CODEC_ID_PCM_S16BE) {
                        if (desc->bits_per_sample == 24)
                            st->codec->codec_id = CODEC_ID_PCM_S24BE;
                        else if (desc->bits_per_sample == 32)
                            st->codec->codec_id = CODEC_ID_PCM_S32BE;
                    }
                    if (!st->codec->codec_id)
                        st->codec->codec_id = codec_get_id(mxf_container_sound_labels, container_label);

                } else if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
                    st->codec->width = desc->width;
                    st->codec->height = desc->height;

                    container_label = BE_16(desc->essence_container + 12);
                    essence_label = BE_16(desc->essence_compression + 11);
                    st->codec->codec_id = codec_get_id(mxf_picture_essence_labels, essence_label);
                    if (!st->codec->codec_id)
                        st->codec->codec_id = codec_get_id(mxf_container_picture_labels, container_label);
                }
            }
        }
    }
}

static int mxf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MXFContext *mxf = s->priv_data;
    KLVPacket klv;
    int ret = 0;

    mxf->fc = s;
    while (!url_feof(&s->pb)) {
        if (klv_read_packet(&klv, &s->pb) < 0)
            return -1;
        if (IS_KLV_KEY(klv.key, mxf_metadata_track_key))
            ret = mxf_read_metadata_track(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_source_package_key))
            ret = mxf_read_metadata_source_package(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_sequence_key))
            ret = mxf_read_metadata_sequence(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_wave_descriptor_key))
            ret = mxf_read_metadata_descriptor(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_mpegvideo_descriptor_key))
            ret = mxf_read_metadata_descriptor(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_cdci_descriptor_key))
            ret = mxf_read_metadata_descriptor(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_metadata_generic_sound_descriptor_key))
            ret = mxf_read_metadata_descriptor(mxf, &klv);
        else if (IS_KLV_KEY(klv.key, mxf_essence_element_key)) {
            /* FIXME avoid seek */
            url_fseek(&s->pb, klv.offset, SEEK_SET);
            break;
        } else
            url_fskip(&s->pb, klv.length);
        if (ret < 0)
            return ret;
    }
    mxf_resolve_track_descriptor(mxf);
    return 0;
}

static int mxf_read_close(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;

    av_freep(&mxf->tracks);
    av_freep(&mxf->descriptors);
    return 0;
}

static int mxf_probe(AVProbeData *p) {
    /* KLV packet describing MXF header partition pack */
    if (p->buf_size < sizeof(mxf_header_partition_pack_key))
        return 0;

    if (IS_KLV_KEY(p->buf, mxf_header_partition_pack_key))
        return AVPROBE_SCORE_MAX;
    else
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
    NULL,
};
