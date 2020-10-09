/*
 * MXF muxer
 * Copyright (c) 2008 GUCAS, Zhentan Feng <spyfeng at gmail dot com>
 * Copyright (c) 2008 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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
 * signal_standard, color_siting, store_user_comments, sample rate and klv_fill_key version
 * fixes sponsored by NOA GmbH
 */

/*
 * References
 * SMPTE 336M KLV Data Encoding Protocol Using Key-Length-Value
 * SMPTE 377M MXF File Format Specifications
 * SMPTE 379M MXF Generic Container
 * SMPTE 381M Mapping MPEG Streams into the MXF Generic Container
 * SMPTE 422M Mapping JPEG 2000 Codestreams into the MXF Generic Container
 * SMPTE RP210: SMPTE Metadata Dictionary
 * SMPTE RP224: Registry of SMPTE Universal Labels
 */

#include <inttypes.h>
#include <math.h>
#include <time.h>

#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/timecode.h"
#include "libavutil/avassert.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time_internal.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/dnxhddata.h"
#include "libavcodec/dv_profile.h"
#include "libavcodec/h264_ps.h"
#include "libavcodec/golomb.h"
#include "libavcodec/internal.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "avc.h"
#include "mxf.h"
#include "config.h"

extern AVOutputFormat ff_mxf_d10_muxer;
extern AVOutputFormat ff_mxf_opatom_muxer;

#define EDIT_UNITS_PER_BODY 250
#define KAG_SIZE 512

typedef struct MXFIndexEntry {
    uint64_t offset;
    unsigned slice_offset; ///< offset of audio slice
    uint16_t temporal_ref;
    uint8_t flags;
} MXFIndexEntry;

typedef struct MXFStreamContext {
    int64_t pkt_cnt;         ///< pkt counter for muxed packets
    UID track_essence_element_key;
    int index;               ///< index in mxf_essence_container_uls table
    const UID *codec_ul;
    const UID *container_ul;
    int order;               ///< interleaving order if dts are equal
    int interlaced;          ///< whether picture is interlaced
    int field_dominance;     ///< tff=1, bff=2
    int component_depth;
    int color_siting;
    int signal_standard;
    int h_chroma_sub_sample;
    int v_chroma_sub_sample;
    int temporal_reordering;
    AVRational aspect_ratio; ///< display aspect ratio
    int closed_gop;          ///< gop is closed, used in mpeg-2 frame parsing
    int video_bit_rate;
    int slice_offset;
    int frame_size;          ///< frame size in bytes
    int seq_closed_gop;      ///< all gops in sequence are closed, used in mpeg-2 descriptor
    int max_gop;             ///< maximum gop size, used by mpeg-2 descriptor
    int b_picture_count;     ///< maximum number of consecutive b pictures, used in mpeg-2 descriptor
    int low_delay;           ///< low delay, used in mpeg-2 descriptor
    int avc_intra;
} MXFStreamContext;

typedef struct MXFContainerEssenceEntry {
    UID container_ul;
    UID element_ul;
    UID codec_ul;
    void (*write_desc)(AVFormatContext *, AVStream *);
} MXFContainerEssenceEntry;

typedef struct MXFPackage {
    char *name;
    enum MXFMetadataSetType type;
    int instance;
    struct MXFPackage *ref;
} MXFPackage;

enum ULIndex {
    INDEX_MPEG2 = 0,
    INDEX_AES3,
    INDEX_WAV,
    INDEX_D10_VIDEO,
    INDEX_D10_AUDIO,
    INDEX_DV,
    INDEX_DNXHD,
    INDEX_JPEG2000,
    INDEX_H264,
    INDEX_S436M,
    INDEX_PRORES,
};

static const struct {
    enum AVCodecID id;
    enum ULIndex index;
} mxf_essence_mappings[] = {
    { AV_CODEC_ID_MPEG2VIDEO, INDEX_MPEG2 },
    { AV_CODEC_ID_PCM_S24LE,  INDEX_AES3 },
    { AV_CODEC_ID_PCM_S16LE,  INDEX_AES3 },
    { AV_CODEC_ID_DVVIDEO,    INDEX_DV },
    { AV_CODEC_ID_DNXHD,      INDEX_DNXHD },
    { AV_CODEC_ID_JPEG2000,   INDEX_JPEG2000 },
    { AV_CODEC_ID_H264,       INDEX_H264 },
    { AV_CODEC_ID_PRORES,     INDEX_PRORES },
    { AV_CODEC_ID_NONE }
};

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_h264_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_cdci_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_generic_sound_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_s436m_anc_desc(AVFormatContext *s, AVStream *st);

static const MXFContainerEssenceEntry mxf_essence_container_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x03,0x01,0x02,0x04,0x60,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x00,0x00,0x00 },
      mxf_write_mpegvideo_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x03,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x16,0x01,0x03,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_aes3_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x06,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x16,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_wav_desc },
    // D-10 Video
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x01 },
      mxf_write_cdci_desc },
    // D-10 Audio
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // DV
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x7F,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x00,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x01,0x00,0x00 },
      mxf_write_cdci_desc },
    // JPEG2000
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x0d,0x01,0x03,0x01,0x02,0x0c,0x01,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x08,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x07,0x04,0x01,0x02,0x02,0x03,0x01,0x01,0x00 },
      mxf_write_cdci_desc },
    // H.264
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x0D,0x01,0x03,0x01,0x02,0x10,0x60,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x00,0x00,0x00 },
      mxf_write_h264_desc },
    // S436M ANC
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x0D,0x01,0x03,0x01,0x02,0x0e,0x00,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x17,0x01,0x02,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x01,0x5C,0x00 },
      mxf_write_s436m_anc_desc },
    // ProRes
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x0d,0x01,0x03,0x01,0x02,0x1c,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x17,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x03,0x00 },
      mxf_write_cdci_desc },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      NULL },
};

static const UID mxf_d10_codec_uls[] = {
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x01 }, // D-10 625/50 PAL 50mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x02 }, // D-10 525/50 NTSC 50mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x03 }, // D-10 625/50 PAL 40mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x04 }, // D-10 525/50 NTSC 40mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x05 }, // D-10 625/50 PAL 30mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x06 }, // D-10 525/50 NTSC 30mb/s
};

static const UID mxf_d10_container_uls[] = {
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 }, // D-10 625/50 PAL 50mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x02,0x01 }, // D-10 525/50 NTSC 50mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x03,0x01 }, // D-10 625/50 PAL 40mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x04,0x01 }, // D-10 525/50 NTSC 40mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x05,0x01 }, // D-10 625/50 PAL 30mb/s
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x06,0x01 }, // D-10 525/50 NTSC 30mb/s
};

typedef struct MXFContext {
    AVClass *av_class;
    int64_t footer_partition_offset;
    int essence_container_count;
    AVRational time_base;
    int header_written;
    MXFIndexEntry *index_entries;
    unsigned edit_units_count;
    uint64_t timestamp;   ///< timestamp, as year(16),month(8),day(8),hour(8),minutes(8),msec/4(8)
    uint8_t slice_count;  ///< index slice count minus 1 (1 if no audio, 0 otherwise)
    int last_indexed_edit_unit;
    uint64_t *body_partition_offset;
    unsigned body_partitions_count;
    int last_key_index;  ///< index of last key frame
    uint64_t duration;
    AVTimecode tc;       ///< timecode context
    AVStream *timecode_track;
    int timecode_base;       ///< rounded time code base (25 or 30)
    int edit_unit_byte_count; ///< fixed edit unit byte count
    int content_package_rate; ///< content package rate in system element, see SMPTE 326M
    uint64_t body_offset;
    uint32_t instance_number;
    uint8_t umid[16];        ///< unique material identifier
    int channel_count;
    int signal_standard;
    uint32_t tagged_value_count;
    AVRational audio_edit_rate;
    int store_user_comments;
    int track_instance_count; // used to generate MXFTrack uuids
    int cbr_index;           ///< use a constant bitrate index
} MXFContext;

static const uint8_t uuid_base[]            = { 0xAD,0xAB,0x44,0x24,0x2f,0x25,0x4d,0xc7,0x92,0xff,0x29,0xbd };
static const uint8_t umid_ul[]              = { 0x06,0x0A,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x01,0x0D,0x00,0x13 };

/**
 * complete key for operation pattern, partitions, and primer pack
 */
static const uint8_t op1a_ul[]                     = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x01,0x09,0x00 };
static const uint8_t opatom_ul[]                   = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x02,0x0D,0x01,0x02,0x01,0x10,0x03,0x00,0x00 };
static const uint8_t footer_partition_key[]        = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }; // ClosedComplete
static const uint8_t primer_pack_key[]             = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x05,0x01,0x00 };
static const uint8_t index_table_segment_key[]     = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x10,0x01,0x00 };
static const uint8_t random_index_pack_key[]       = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x11,0x01,0x00 };
static const uint8_t header_open_partition_key[]   = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }; // OpenIncomplete
static const uint8_t header_closed_partition_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }; // ClosedComplete
static const uint8_t klv_fill_key[]                = { 0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x01,0x02,0x10,0x01,0x00,0x00,0x00 };
static const uint8_t body_partition_key[]          = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x03,0x04,0x00 }; // ClosedComplete

/**
 * partial key for header metadata
 */
static const uint8_t header_metadata_key[]  = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01 };
static const uint8_t multiple_desc_ul[]     = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x0D,0x01,0x03,0x01,0x02,0x7F,0x01,0x00 };

/**
 * SMPTE RP210 http://www.smpte-ra.org/mdd/index.html
 *             https://smpte-ra.org/sites/default/files/Labels.xml
 */
static const MXFLocalTagPair mxf_local_tag_batch[] = {
    // preface set
    { 0x3C0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x01,0x01,0x15,0x02,0x00,0x00,0x00,0x00}}, /* Instance UID */
    { 0x3B02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x04,0x00,0x00}}, /* Last Modified Date */
    { 0x3B05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x01,0x02,0x01,0x05,0x00,0x00,0x00}}, /* Version */
    { 0x3B07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x01,0x02,0x01,0x04,0x00,0x00,0x00}}, /* Object Model Version */
    { 0x3B06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x06,0x04,0x00,0x00}}, /* Identifications reference */
    { 0x3B03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x02,0x01,0x00,0x00}}, /* Content Storage reference */
    { 0x3B09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x03,0x00,0x00,0x00,0x00}}, /* Operational Pattern UL */
    { 0x3B0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x10,0x02,0x01,0x00,0x00}}, /* Essence Containers UL batch */
    { 0x3B0B, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x02,0x02,0x10,0x02,0x02,0x00,0x00}}, /* DM Schemes UL batch */
    // Identification
    { 0x3C09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x01,0x00,0x00,0x00}}, /* This Generation UID */
    { 0x3C01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x02,0x01,0x00,0x00}}, /* Company Name */
    { 0x3C02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x03,0x01,0x00,0x00}}, /* Product Name */
    { 0x3C03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x04,0x00,0x00,0x00}}, /* Product Version */
    { 0x3C04, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x05,0x01,0x00,0x00}}, /* Version String */
    { 0x3C05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x07,0x00,0x00,0x00}}, /* Product ID */
    { 0x3C06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x03,0x00,0x00}}, /* Modification Date */
    { 0x3C07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x0A,0x00,0x00,0x00}}, /* Toolkit Version */
    { 0x3C08, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x05,0x20,0x07,0x01,0x06,0x01,0x00,0x00}}, /* Platform */
    // Content Storage
    { 0x1901, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x05,0x01,0x00,0x00}}, /* Package strong reference batch */
    { 0x1902, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x05,0x02,0x00,0x00}}, /* Package strong reference batch */
    // Essence Container Data
    { 0x2701, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x06,0x01,0x00,0x00,0x00}}, /* Linked Package UID */
    { 0x3F07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x01,0x03,0x04,0x04,0x00,0x00,0x00,0x00}}, /* BodySID */
    // Package
    { 0x4401, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x01,0x01,0x15,0x10,0x00,0x00,0x00,0x00}}, /* Package UID */
    { 0x4405, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x01,0x03,0x00,0x00}}, /* Package Creation Date */
    { 0x4404, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x10,0x02,0x05,0x00,0x00}}, /* Package Modified Date */
    { 0x4402, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x01,0x03,0x03,0x02,0x01,0x00,0x00,0x00}}, /* Package Name */
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
    { 0x1201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x03,0x01,0x04,0x00,0x00}}, /* Start position */
    { 0x1101, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x03,0x01,0x00,0x00,0x00}}, /* SourcePackageID */
    { 0x1102, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x03,0x02,0x00,0x00,0x00}}, /* SourceTrackID */
    // Timecode Component
    { 0x1501, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x07,0x02,0x01,0x03,0x01,0x05,0x00,0x00}}, /* Start Time Code */
    { 0x1502, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x04,0x01,0x01,0x02,0x06,0x00,0x00}}, /* Rounded Time Code Base */
    { 0x1503, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x04,0x01,0x01,0x05,0x00,0x00,0x00}}, /* Drop Frame */
    // File Descriptor
    { 0x3F01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x06,0x01,0x01,0x04,0x06,0x0B,0x00,0x00}}, /* Sub Descriptors reference array */
    { 0x3006, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x06,0x01,0x01,0x03,0x05,0x00,0x00,0x00}}, /* Linked Track ID */
    { 0x3001, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x06,0x01,0x01,0x00,0x00,0x00,0x00}}, /* SampleRate */
    { 0x3002, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x06,0x01,0x02,0x00,0x00,0x00,0x00}}, /* ContainerDuration */
    { 0x3004, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x01,0x02,0x00,0x00}}, /* Essence Container */
    // Generic Picture Essence Descriptor
    { 0x320C, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Frame Layout */
    { 0x320D, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x03,0x02,0x05,0x00,0x00,0x00}}, /* Video Line Map */
    { 0x3203, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x02,0x00,0x00,0x00}}, /* Stored Width */
    { 0x3202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x01,0x00,0x00,0x00}}, /* Stored Height */
    { 0x3216, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x03,0x02,0x08,0x00,0x00,0x00}}, /* Stored F2 Offset */
    { 0x3205, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x08,0x00,0x00,0x00}}, /* Sampled Width */
    { 0x3204, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x07,0x00,0x00,0x00}}, /* Sampled Height */
    { 0x3206, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x09,0x00,0x00,0x00}}, /* Sampled X Offset */
    { 0x3207, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0A,0x00,0x00,0x00}}, /* Sampled Y Offset */
    { 0x3209, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0C,0x00,0x00,0x00}}, /* Display Width */
    { 0x3208, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0B,0x00,0x00,0x00}}, /* Display Height */
    { 0x320A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0D,0x00,0x00,0x00}}, /* Display X offset */
    { 0x320B, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0E,0x00,0x00,0x00}}, /* Presentation Y offset */
    { 0x3217, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x03,0x02,0x07,0x00,0x00,0x00}}, /* Display F2 offset */
    { 0x320E, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x01,0x01,0x01,0x00,0x00,0x00}}, /* Aspect Ratio */
    { 0x3210, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x02,0x01,0x01,0x01,0x02,0x00}}, /* Transfer characteristic */
    { 0x3213, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x18,0x01,0x02,0x00,0x00,0x00,0x00}}, /* Image Start Offset */
    { 0x3214, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x18,0x01,0x03,0x00,0x00,0x00,0x00}}, /* Image End Offset */
    { 0x3201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x06,0x01,0x00,0x00,0x00,0x00}}, /* Picture Essence Coding */
    { 0x3212, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x03,0x01,0x06,0x00,0x00,0x00}}, /* Field Dominance (Opt) */
    { 0x3215, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x05,0x01,0x13,0x00,0x00,0x00,0x00}}, /* Signal Standard */
    // CDCI Picture Essence Descriptor
    { 0x3301, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x05,0x03,0x0A,0x00,0x00,0x00}}, /* Component Depth */
    { 0x3302, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x05,0x00,0x00,0x00}}, /* Horizontal Subsampling */
    { 0x3308, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x05,0x01,0x10,0x00,0x00,0x00}}, /* Vertical Subsampling */
    { 0x3303, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x06,0x00,0x00,0x00}}, /* Color Siting */
    { 0x3307, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x18,0x01,0x04,0x00,0x00,0x00,0x00}}, /* Padding Bits */
    { 0x3304, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x03,0x03,0x00,0x00,0x00}}, /* Black Ref level */
    { 0x3305, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x03,0x04,0x00,0x00,0x00}}, /* White Ref level */
    { 0x3306, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x05,0x03,0x05,0x00,0x00,0x00}}, /* Color Range */
    // Generic Sound Essence Descriptor
    { 0x3D02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x02,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Locked/Unlocked */
    { 0x3D03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x01,0x01,0x01,0x00,0x00}}, /* Audio sampling rate */
    { 0x3D04, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x02,0x01,0x01,0x03,0x00,0x00,0x00}}, /* Audio Ref Level */
    { 0x3D07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x01,0x01,0x04,0x00,0x00,0x00}}, /* ChannelCount */
    { 0x3D01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x02,0x03,0x03,0x04,0x00,0x00,0x00}}, /* Quantization bits */
    { 0x3D06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x02,0x04,0x02,0x00,0x00,0x00,0x00}}, /* Sound Essence Compression */
    // Index Table Segment
    { 0x3F0B, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x05,0x30,0x04,0x06,0x00,0x00,0x00,0x00}}, /* Index Edit Rate */
    { 0x3F0C, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x07,0x02,0x01,0x03,0x01,0x0A,0x00,0x00}}, /* Index Start Position */
    { 0x3F0D, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x07,0x02,0x02,0x01,0x01,0x02,0x00,0x00}}, /* Index Duration */
    { 0x3F05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x06,0x02,0x01,0x00,0x00,0x00,0x00}}, /* Edit Unit Byte Count */
    { 0x3F06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x01,0x03,0x04,0x05,0x00,0x00,0x00,0x00}}, /* IndexSID */
    { 0x3F08, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x04,0x04,0x01,0x01,0x00,0x00,0x00}}, /* Slice Count */
    { 0x3F09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x04,0x04,0x01,0x06,0x00,0x00,0x00}}, /* Delta Entry Array */
    { 0x3F0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x04,0x04,0x02,0x05,0x00,0x00,0x00}}, /* Index Entry Array */
    // MPEG video Descriptor
    { 0x8000, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x0B,0x00,0x00}}, /* BitRate */
    { 0x8003, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x05,0x00,0x00}}, /* LowDelay */
    { 0x8004, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x06,0x00,0x00}}, /* ClosedGOP */
    { 0x8006, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x08,0x00,0x00}}, /* MaxGOP */
    { 0x8007, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x0A,0x00,0x00}}, /* ProfileAndLevel */
    { 0x8008, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x09,0x00,0x00}}, /* BPictureCount */
    // Wave Audio Essence Descriptor
    { 0x3D09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x03,0x05,0x00,0x00,0x00}}, /* Average Bytes Per Second */
    { 0x3D0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x02,0x01,0x00,0x00,0x00}}, /* Block Align */
};

static const MXFLocalTagPair mxf_avc_subdescriptor_local_tags[] = {
    { 0x8100, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x09,0x06,0x01,0x01,0x04,0x06,0x10,0x00,0x00}}, /* SubDescriptors */
    { 0x8200, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x0E,0x04,0x01,0x06,0x06,0x01,0x0E,0x00,0x00}}, /* AVC Decoding Delay */
    { 0x8201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x0E,0x04,0x01,0x06,0x06,0x01,0x0A,0x00,0x00}}, /* AVC Profile */
    { 0x8202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x0E,0x04,0x01,0x06,0x06,0x01,0x0D,0x00,0x00}}, /* AVC Level */
};

static const MXFLocalTagPair mxf_user_comments_local_tag[] = {
    { 0x4406, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x02,0x01,0x02,0x0C,0x00,0x00,0x00}}, /* User Comments */
    { 0x5001, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x02,0x01,0x02,0x09,0x01,0x00,0x00}}, /* Name */
    { 0x5003, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x03,0x02,0x01,0x02,0x0A,0x01,0x00,0x00}}, /* Value */
};

static void mxf_write_uuid(AVIOContext *pb, enum MXFMetadataSetType type, int value)
{
    avio_write(pb, uuid_base, 12);
    avio_wb16(pb, type);
    avio_wb16(pb, value);
}

static void mxf_write_umid(AVFormatContext *s, int type)
{
    MXFContext *mxf = s->priv_data;
    avio_write(s->pb, umid_ul, 13);
    avio_wb24(s->pb, mxf->instance_number);
    avio_write(s->pb, mxf->umid, 15);
    avio_w8(s->pb, type);
}

static void mxf_write_refs_count(AVIOContext *pb, int ref_count)
{
    avio_wb32(pb, ref_count);
    avio_wb32(pb, 16);
}

static int klv_ber_length(uint64_t len)
{
    if (len < 128)
        return 1;
    else
        return (av_log2(len) >> 3) + 2;
}

static int klv_encode_ber_length(AVIOContext *pb, uint64_t len)
{
    // Determine the best BER size
    int size = klv_ber_length(len);
    if (size == 1) {
        //short form
        avio_w8(pb, len);
        return 1;
    }

    size --;
    // long form
    avio_w8(pb, 0x80 + size);
    while(size) {
        size--;
        avio_w8(pb, len >> 8 * size & 0xff);
    }
    return 0;
}

static void klv_encode_ber4_length(AVIOContext *pb, int len)
{
    avio_w8(pb, 0x80 + 3);
    avio_wb24(pb, len);
}

static void klv_encode_ber9_length(AVIOContext *pb, uint64_t len)
{
    avio_w8(pb, 0x80 + 8);
    avio_wb64(pb, len);
}

/*
 * Get essence container ul index
 */
static int mxf_get_essence_container_ul_index(enum AVCodecID id)
{
    int i;
    for (i = 0; mxf_essence_mappings[i].id; i++)
        if (mxf_essence_mappings[i].id == id)
            return mxf_essence_mappings[i].index;
    return -1;
}

static void mxf_write_local_tags(AVIOContext *pb, const MXFLocalTagPair *local_tags, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        avio_wb16(pb, local_tags[i].local_tag);
        avio_write(pb, local_tags[i].uid, 16);
    }
}

static void mxf_write_primer_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int local_tag_number, i = 0;
    int avc_tags_count = 0;
    int mastering_tags_count = 0;

    local_tag_number = FF_ARRAY_ELEMS(mxf_local_tag_batch);
    local_tag_number += mxf->store_user_comments * FF_ARRAY_ELEMS(mxf_user_comments_local_tag);

    for (i = 0; i < s->nb_streams; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_H264 && !sc->avc_intra) {
            avc_tags_count = FF_ARRAY_ELEMS(mxf_avc_subdescriptor_local_tags);
        }
        if (av_stream_get_side_data(s->streams[i], AV_PKT_DATA_MASTERING_DISPLAY_METADATA, NULL)) {
            mastering_tags_count = FF_ARRAY_ELEMS(ff_mxf_mastering_display_local_tags);
        }
    }

    local_tag_number += avc_tags_count;
    local_tag_number += mastering_tags_count;

    avio_write(pb, primer_pack_key, 16);
    klv_encode_ber_length(pb, local_tag_number * 18 + 8);

    avio_wb32(pb, local_tag_number); // local_tag num
    avio_wb32(pb, 18); // item size, always 18 according to the specs

    for (i = 0; i < FF_ARRAY_ELEMS(mxf_local_tag_batch); i++) {
        avio_wb16(pb, mxf_local_tag_batch[i].local_tag);
        avio_write(pb, mxf_local_tag_batch[i].uid, 16);
    }
    if (mxf->store_user_comments)
        for (i = 0; i < FF_ARRAY_ELEMS(mxf_user_comments_local_tag); i++) {
            avio_wb16(pb, mxf_user_comments_local_tag[i].local_tag);
            avio_write(pb, mxf_user_comments_local_tag[i].uid, 16);
        }
    if (avc_tags_count > 0)
        mxf_write_local_tags(pb, mxf_avc_subdescriptor_local_tags, avc_tags_count);
    if (mastering_tags_count > 0)
        mxf_write_local_tags(pb, ff_mxf_mastering_display_local_tags, mastering_tags_count);
}

static void mxf_write_local_tag(AVIOContext *pb, int size, int tag)
{
    avio_wb16(pb, tag);
    avio_wb16(pb, size);
}

static void mxf_write_metadata_key(AVIOContext *pb, unsigned int value)
{
    avio_write(pb, header_metadata_key, 13);
    avio_wb24(pb, value);
}

static const MXFCodecUL *mxf_get_codec_ul_by_id(const MXFCodecUL *uls, int id)
{
    while (uls->uid[0]) {
        if (id == uls->id)
            break;
        uls++;
    }
    return uls;
}

//one EC -> one descriptor. N ECs -> MultipleDescriptor + N descriptors
#define DESCRIPTOR_COUNT(essence_container_count) \
    (essence_container_count > 1 ? essence_container_count + 1 : essence_container_count)

static void mxf_write_essence_container_refs(AVFormatContext *s)
{
    MXFContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    mxf_write_refs_count(pb, DESCRIPTOR_COUNT(c->essence_container_count));
    av_log(s,AV_LOG_DEBUG, "essence container count:%d\n", c->essence_container_count);
    for (i = 0; i < s->nb_streams; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        // check first track of essence container type and only write it once
        if (sc->track_essence_element_key[15] != 0)
            continue;
        avio_write(pb, *sc->container_ul, 16);
        if (c->essence_container_count == 1)
            break;
    }

    if (c->essence_container_count > 1)
        avio_write(pb, multiple_desc_ul, 16);
}

static void mxf_write_preface(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012f00);
    PRINT_KEY(s, "preface key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 138 + 16LL * DESCRIPTOR_COUNT(mxf->essence_container_count));

    // write preface set uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Preface, 0);
    PRINT_KEY(s, "preface uid", pb->buf_ptr - 16);

    // last modified date
    mxf_write_local_tag(pb, 8, 0x3B02);
    avio_wb64(pb, mxf->timestamp);

    // write version
    mxf_write_local_tag(pb, 2, 0x3B05);
    avio_wb16(pb, 259); // v1.3

    // Object Model Version
    mxf_write_local_tag(pb, 4, 0x3B07);
    avio_wb32(pb, 1);

    // write identification_refs
    mxf_write_local_tag(pb, 16 + 8, 0x3B06);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, Identification, 0);

    // write content_storage_refs
    mxf_write_local_tag(pb, 16, 0x3B03);
    mxf_write_uuid(pb, ContentStorage, 0);

    // operational pattern
    mxf_write_local_tag(pb, 16, 0x3B09);
    if (s->oformat == &ff_mxf_opatom_muxer)
        avio_write(pb, opatom_ul, 16);
    else
        avio_write(pb, op1a_ul, 16);

    // write essence_container_refs
    mxf_write_local_tag(pb, 8 + 16LL * DESCRIPTOR_COUNT(mxf->essence_container_count), 0x3B0A);
    mxf_write_essence_container_refs(s);

    // write dm_scheme_refs
    mxf_write_local_tag(pb, 8, 0x3B0B);
    avio_wb64(pb, 0);
}

/*
 * Returns the length of the UTF-16 string, in 16-bit characters, that would result
 * from decoding the utf-8 string.
 */
static uint64_t mxf_utf16len(const char *utf8_str)
{
    const uint8_t *q = utf8_str;
    uint64_t size = 0;
    while (*q) {
        uint32_t ch;
        GET_UTF8(ch, *q++, goto invalid;)
        if (ch < 0x10000)
            size++;
        else
            size += 2;
        continue;
invalid:
        av_log(NULL, AV_LOG_ERROR, "Invalid UTF8 sequence in mxf_utf16len\n\n");
    }
    size += 1;
    return size;
}

/*
 * Returns the calculated length a local tag containing an utf-8 string as utf-16
 */
static int mxf_utf16_local_tag_length(const char *utf8_str)
{
    uint64_t size;

    if (!utf8_str)
        return 0;

    size = mxf_utf16len(utf8_str);
    if (size >= UINT16_MAX/2) {
        av_log(NULL, AV_LOG_ERROR, "utf16 local tag size %"PRIx64" invalid (too large), ignoring\n", size);
        return 0;
    }

    return 4 + size * 2;
}

/*
 * Write a local tag containing an utf-8 string as utf-16
 */
static void mxf_write_local_tag_utf16(AVIOContext *pb, int tag, const char *value)
{
    uint64_t size = mxf_utf16len(value);

    if (size >= UINT16_MAX/2) {
        av_log(NULL, AV_LOG_ERROR, "utf16 local tag size %"PRIx64" invalid (too large), ignoring\n", size);
        return;
    }

    mxf_write_local_tag(pb, size*2, tag);
    avio_put_str16be(pb, value);
}

static void store_version(AVFormatContext *s){
    AVIOContext *pb = s->pb;

    if (s->flags & AVFMT_FLAG_BITEXACT) {
        avio_wb16(pb, 0); // major
        avio_wb16(pb, 0); // minor
        avio_wb16(pb, 0); // tertiary
    } else {
        avio_wb16(pb, LIBAVFORMAT_VERSION_MAJOR); // major
        avio_wb16(pb, LIBAVFORMAT_VERSION_MINOR); // minor
        avio_wb16(pb, LIBAVFORMAT_VERSION_MICRO); // tertiary
    }
    avio_wb16(pb, 0); // patch
    avio_wb16(pb, 0); // release
}

static void mxf_write_identification(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    const char *company = "FFmpeg";
    const char *product = s->oformat != &ff_mxf_opatom_muxer ? "OP1a Muxer" : "OPAtom Muxer";
    const char *version;
    int length;

    mxf_write_metadata_key(pb, 0x013000);
    PRINT_KEY(s, "identification key", pb->buf_ptr - 16);

    version = s->flags & AVFMT_FLAG_BITEXACT ?
        "0.0.0" : AV_STRINGIFY(LIBAVFORMAT_VERSION);
    length = 100 +mxf_utf16_local_tag_length(company) +
                  mxf_utf16_local_tag_length(product) +
                  mxf_utf16_local_tag_length(version);
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

    mxf_write_local_tag(pb, 10, 0x3C03); // Product Version
    store_version(s);

    mxf_write_local_tag_utf16(pb, 0x3C04, version); // Version String

    // write product uid
    mxf_write_local_tag(pb, 16, 0x3C05);
    mxf_write_uuid(pb, Identification, 2);

    // modification date
    mxf_write_local_tag(pb, 8, 0x3C06);
    avio_wb64(pb, mxf->timestamp);

    mxf_write_local_tag(pb, 10, 0x3C07); // Toolkit Version
    store_version(s);
}

static void mxf_write_content_storage(AVFormatContext *s, MXFPackage *packages, int package_count)
{
    AVIOContext *pb = s->pb;
    int i;

    mxf_write_metadata_key(pb, 0x011800);
    PRINT_KEY(s, "content storage key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 60 + (16 * package_count));

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, ContentStorage, 0);
    PRINT_KEY(s, "content storage uid", pb->buf_ptr - 16);

    // write package reference
    mxf_write_local_tag(pb, 16 * package_count + 8, 0x1901);
    mxf_write_refs_count(pb, package_count);
    for (i = 0; i < package_count; i++) {
        mxf_write_uuid(pb, packages[i].type, packages[i].instance);
    }

    // write essence container data
    mxf_write_local_tag(pb, 8 + 16, 0x1902);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, EssenceContainerData, 0);
}

static void mxf_write_track(AVFormatContext *s, AVStream *st, MXFPackage *package)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    MXFStreamContext *sc = st->priv_data;

    mxf_write_metadata_key(pb, 0x013b00);
    PRINT_KEY(s, "track key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 80);

    // write track uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Track, mxf->track_instance_count);
    PRINT_KEY(s, "track uid", pb->buf_ptr - 16);

    // write track id
    mxf_write_local_tag(pb, 4, 0x4801);
    avio_wb32(pb, st->index+2);

    // write track number
    mxf_write_local_tag(pb, 4, 0x4804);
    if (package->type == MaterialPackage)
        avio_wb32(pb, 0); // track number of material package is 0
    else
        avio_write(pb, sc->track_essence_element_key + 12, 4);

    // write edit rate
    mxf_write_local_tag(pb, 8, 0x4B01);

    if (st == mxf->timecode_track && s->oformat == &ff_mxf_opatom_muxer) {
        avio_wb32(pb, mxf->tc.rate.num);
        avio_wb32(pb, mxf->tc.rate.den);
    } else {
        avio_wb32(pb, mxf->time_base.den);
        avio_wb32(pb, mxf->time_base.num);
    }

    // write origin
    mxf_write_local_tag(pb, 8, 0x4B02);
    avio_wb64(pb, 0);

    // write sequence refs
    mxf_write_local_tag(pb, 16, 0x4803);
    mxf_write_uuid(pb, Sequence, mxf->track_instance_count);
}

static const uint8_t smpte_12m_timecode_track_data_ul[] = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x01,0x03,0x02,0x01,0x01,0x00,0x00,0x00 };

static void mxf_write_common_fields(AVFormatContext *s, AVStream *st)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    // find data define uls
    mxf_write_local_tag(pb, 16, 0x0201);
    if (st == mxf->timecode_track)
        avio_write(pb, smpte_12m_timecode_track_data_ul, 16);
    else {
        const MXFCodecUL *data_def_ul = mxf_get_codec_ul_by_id(ff_mxf_data_definition_uls, st->codecpar->codec_type);
        avio_write(pb, data_def_ul->uid, 16);
    }

    // write duration
    mxf_write_local_tag(pb, 8, 0x0202);

    if (st != mxf->timecode_track && s->oformat == &ff_mxf_opatom_muxer && st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        avio_wb64(pb, mxf->body_offset / mxf->edit_unit_byte_count);
    } else {
        avio_wb64(pb, mxf->duration);
    }
}

static void mxf_write_sequence(AVFormatContext *s, AVStream *st, MXFPackage *package)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    enum MXFMetadataSetType component;

    mxf_write_metadata_key(pb, 0x010f00);
    PRINT_KEY(s, "sequence key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 80);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Sequence, mxf->track_instance_count);

    PRINT_KEY(s, "sequence uid", pb->buf_ptr - 16);
    mxf_write_common_fields(s, st);

    // write structural component
    mxf_write_local_tag(pb, 16 + 8, 0x1001);
    mxf_write_refs_count(pb, 1);
    if (st == mxf->timecode_track)
        component = TimecodeComponent;
    else
        component = SourceClip;

    mxf_write_uuid(pb, component, mxf->track_instance_count);
}

static void mxf_write_timecode_component(AVFormatContext *s, AVStream *st, MXFPackage *package)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x011400);
    klv_encode_ber_length(pb, 75);

    // UID
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, TimecodeComponent, mxf->track_instance_count);

    mxf_write_common_fields(s, st);

    // Start Time Code
    mxf_write_local_tag(pb, 8, 0x1501);
    avio_wb64(pb, mxf->tc.start);

    // Rounded Time Code Base
    mxf_write_local_tag(pb, 2, 0x1502);
    avio_wb16(pb, mxf->timecode_base);

    // Drop Frame
    mxf_write_local_tag(pb, 1, 0x1503);
    avio_w8(pb, !!(mxf->tc.flags & AV_TIMECODE_FLAG_DROPFRAME));
}

static void mxf_write_structural_component(AVFormatContext *s, AVStream *st, MXFPackage *package)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    mxf_write_metadata_key(pb, 0x011100);
    PRINT_KEY(s, "sturctural component key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 108);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, SourceClip, mxf->track_instance_count);

    PRINT_KEY(s, "structural component uid", pb->buf_ptr - 16);
    mxf_write_common_fields(s, st);

    // write start_position
    mxf_write_local_tag(pb, 8, 0x1201);
    avio_wb64(pb, 0);

    // write source package uid, end of the reference
    mxf_write_local_tag(pb, 32, 0x1101);
    if (!package->ref) {
        for (i = 0; i < 4; i++)
            avio_wb64(pb, 0);
    } else
        mxf_write_umid(s, package->ref->instance);

    // write source track id
    mxf_write_local_tag(pb, 4, 0x1102);
    if (package->type == SourcePackage && !package->ref)
        avio_wb32(pb, 0);
    else
        avio_wb32(pb, st->index+2);
}

static void mxf_write_tape_descriptor(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012e00);
    PRINT_KEY(s, "tape descriptor key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 20);
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, TapeDescriptor, 0);
    PRINT_KEY(s, "tape_desc uid", pb->buf_ptr - 16);
}


static void mxf_write_multi_descriptor(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    const uint8_t *ul;
    int i;

    mxf_write_metadata_key(pb, 0x014400);
    PRINT_KEY(s, "multiple descriptor key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 64 + 16LL * s->nb_streams);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, MultipleDescriptor, 0);
    PRINT_KEY(s, "multi_desc uid", pb->buf_ptr - 16);

    // write sample rate
    mxf_write_local_tag(pb, 8, 0x3001);
    avio_wb32(pb, mxf->time_base.den);
    avio_wb32(pb, mxf->time_base.num);

    // write essence container ul
    mxf_write_local_tag(pb, 16, 0x3004);
    if (mxf->essence_container_count > 1)
        ul = multiple_desc_ul;
    else {
        MXFStreamContext *sc = s->streams[0]->priv_data;
        ul = *sc->container_ul;
    }
    avio_write(pb, ul, 16);

    // write sub descriptor refs
    mxf_write_local_tag(pb, s->nb_streams * 16 + 8, 0x3F01);
    mxf_write_refs_count(pb, s->nb_streams);
    for (i = 0; i < s->nb_streams; i++)
        mxf_write_uuid(pb, SubDescriptor, i);
}

static int64_t mxf_write_generic_desc(AVFormatContext *s, AVStream *st, const UID key)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;

    avio_write(pb, key, 16);
    klv_encode_ber4_length(pb, 0);
    pos = avio_tell(pb);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, SubDescriptor, st->index);

    mxf_write_local_tag(pb, 4, 0x3006);
    avio_wb32(pb, st->index+2);

    mxf_write_local_tag(pb, 8, 0x3001);
    if (s->oformat == &ff_mxf_d10_muxer) {
        avio_wb32(pb, mxf->time_base.den);
        avio_wb32(pb, mxf->time_base.num);
    } else {
        if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S16LE ||
            st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE) {
            avio_wb32(pb, st->codecpar->sample_rate);
            avio_wb32(pb, 1);
        } else {
            avio_wb32(pb, mxf->time_base.den);
            avio_wb32(pb, mxf->time_base.num);
        }
    }

    mxf_write_local_tag(pb, 16, 0x3004);
    avio_write(pb, *sc->container_ul, 16);

    return pos;
}

static const UID mxf_s436m_anc_descriptor_key = { 0x06,0x0e,0x2b,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x5c,0x00 };
static const UID mxf_mpegvideo_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 };
static const UID mxf_wav_descriptor_key       = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 };
static const UID mxf_aes3_descriptor_key      = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 };
static const UID mxf_cdci_descriptor_key      = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x28,0x00 };
static const UID mxf_generic_sound_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x42,0x00 };

static const UID mxf_avc_subdescriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x6E,0x00 };

static inline uint16_t rescale_mastering_chroma(AVRational q)
{
    return av_clip_uint16(av_rescale(q.num, FF_MXF_MASTERING_CHROMA_DEN, q.den));
}

static inline uint32_t rescale_mastering_luma(AVRational q)
{
    return av_rescale(q.num, FF_MXF_MASTERING_LUMA_DEN, q.den);
}

static int64_t mxf_write_cdci_common(AVFormatContext *s, AVStream *st, const UID key)
{
    MXFStreamContext *sc = st->priv_data;
    AVIOContext *pb = s->pb;
    int stored_width = 0;
    int stored_height = (st->codecpar->height+15)/16*16;
    int display_height;
    int f1, f2;
    const MXFCodecUL *color_primaries_ul;
    const MXFCodecUL *color_trc_ul;
    const MXFCodecUL *color_space_ul;
    int64_t pos = mxf_write_generic_desc(s, st, key);
    uint8_t *side_data;

    color_primaries_ul = mxf_get_codec_ul_by_id(ff_mxf_color_primaries_uls, st->codecpar->color_primaries);
    color_trc_ul       = mxf_get_codec_ul_by_id(ff_mxf_color_trc_uls, st->codecpar->color_trc);
    color_space_ul     = mxf_get_codec_ul_by_id(ff_mxf_color_space_uls, st->codecpar->color_space);

    if (st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO) {
        if (st->codecpar->height == 1080)
            stored_width = 1920;
        else if (st->codecpar->height == 720)
            stored_width = 1280;
    }
    if (!stored_width)
        stored_width = (st->codecpar->width+15)/16*16;

    mxf_write_local_tag(pb, 4, 0x3203);
    avio_wb32(pb, stored_width);

    mxf_write_local_tag(pb, 4, 0x3202);
    avio_wb32(pb, stored_height>>sc->interlaced);

    if (s->oformat == &ff_mxf_d10_muxer) {
        //Stored F2 Offset
        mxf_write_local_tag(pb, 4, 0x3216);
        avio_wb32(pb, 0);

        //Image Start Offset
        mxf_write_local_tag(pb, 4, 0x3213);
        avio_wb32(pb, 0);

        //Image End Offset
        mxf_write_local_tag(pb, 4, 0x3214);
        avio_wb32(pb, 0);
    }

    //Sampled width
    mxf_write_local_tag(pb, 4, 0x3205);
    avio_wb32(pb, stored_width);

    //Samples height
    mxf_write_local_tag(pb, 4, 0x3204);
    avio_wb32(pb, st->codecpar->height>>sc->interlaced);

    //Sampled X Offset
    mxf_write_local_tag(pb, 4, 0x3206);
    avio_wb32(pb, 0);

    //Sampled Y Offset
    mxf_write_local_tag(pb, 4, 0x3207);
    avio_wb32(pb, 0);

    mxf_write_local_tag(pb, 4, 0x3209);
    avio_wb32(pb, stored_width);

    if (st->codecpar->height == 608) // PAL + VBI
        display_height = 576;
    else if (st->codecpar->height == 512)  // NTSC + VBI
        display_height = 486;
    else
        display_height = st->codecpar->height;

    mxf_write_local_tag(pb, 4, 0x3208);
    avio_wb32(pb, display_height>>sc->interlaced);

    // display X offset
    mxf_write_local_tag(pb, 4, 0x320A);
    avio_wb32(pb, 0);

    // display Y offset
    mxf_write_local_tag(pb, 4, 0x320B);
    avio_wb32(pb, (st->codecpar->height - display_height)>>sc->interlaced);

    if (sc->interlaced) {
        //Display F2 Offset
        mxf_write_local_tag(pb, 4, 0x3217);
        avio_wb32(pb, -((st->codecpar->height - display_height)&1));
    }

    // component depth
    mxf_write_local_tag(pb, 4, 0x3301);
    avio_wb32(pb, sc->component_depth);

    // horizontal subsampling
    mxf_write_local_tag(pb, 4, 0x3302);
    avio_wb32(pb, sc->h_chroma_sub_sample);

    // vertical subsampling
    mxf_write_local_tag(pb, 4, 0x3308);
    avio_wb32(pb, sc->v_chroma_sub_sample);

    // color siting
    mxf_write_local_tag(pb, 1, 0x3303);
    avio_w8(pb, sc->color_siting);

    // Padding Bits
    mxf_write_local_tag(pb, 2, 0x3307);
    avio_wb16(pb, 0);

    if (st->codecpar->color_range != AVCOL_RANGE_UNSPECIFIED) {
        int black = 0,
            white = (1<<sc->component_depth) - 1,
            color = (1<<sc->component_depth);
        if (st->codecpar->color_range == AVCOL_RANGE_MPEG) {
            black = 1   << (sc->component_depth - 4);
            white = 235 << (sc->component_depth - 8);
            color = (14 << (sc->component_depth - 4)) + 1;
        }
        mxf_write_local_tag(pb, 4, 0x3304);
        avio_wb32(pb, black);
        mxf_write_local_tag(pb, 4, 0x3305);
        avio_wb32(pb, white);
        mxf_write_local_tag(pb, 4, 0x3306);
        avio_wb32(pb, color);
    }

    if (sc->signal_standard) {
        mxf_write_local_tag(pb, 1, 0x3215);
        avio_w8(pb, sc->signal_standard);
    }

    // frame layout
    mxf_write_local_tag(pb, 1, 0x320C);
    avio_w8(pb, sc->interlaced);

    // video line map
    switch (st->codecpar->height) {
    case  576: f1 = 23; f2 = st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO ? 335 : 336; break;
    case  608: f1 =  7; f2 = 320; break;
    case  480: f1 = 20; f2 = st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO ? 285 : 283; break;
    case  512: f1 =  7; f2 = 270; break;
    case  720: f1 = 26; f2 =   0; break; // progressive
    case 1080: f1 = 21; f2 = 584; break;
    default:   f1 =  0; f2 =   0; break;
    }

    if (!sc->interlaced && f2) {
        f2  = 0;
        f1 *= 2;
    }


    mxf_write_local_tag(pb, 16, 0x320D);
    avio_wb32(pb, 2);
    avio_wb32(pb, 4);
    avio_wb32(pb, f1);
    avio_wb32(pb, f2);

    mxf_write_local_tag(pb, 8, 0x320E);
    avio_wb32(pb, sc->aspect_ratio.num);
    avio_wb32(pb, sc->aspect_ratio.den);

    if (color_primaries_ul->uid[0]) {
        mxf_write_local_tag(pb, 16, 0x3219);
        avio_write(pb, color_primaries_ul->uid, 16);
    };

    if (color_trc_ul->uid[0]) {
        mxf_write_local_tag(pb, 16, 0x3210);
        avio_write(pb, color_trc_ul->uid, 16);
    };

    if (color_space_ul->uid[0]) {
        mxf_write_local_tag(pb, 16, 0x321A);
        avio_write(pb, color_space_ul->uid, 16);
    };

    mxf_write_local_tag(pb, 16, 0x3201);
    avio_write(pb, *sc->codec_ul, 16);

    // Mastering Display metadata
    side_data = av_stream_get_side_data(st, AV_PKT_DATA_MASTERING_DISPLAY_METADATA, NULL);
    if (side_data) {
        const AVMasteringDisplayMetadata *metadata = (const AVMasteringDisplayMetadata*)side_data;
        if (metadata->has_primaries) {
            mxf_write_local_tag(pb, 12, ff_mxf_mastering_display_local_tags[0].local_tag);
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[0][0]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[0][1]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[1][0]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[1][1]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[2][0]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->display_primaries[2][1]));
            mxf_write_local_tag(pb, 4, ff_mxf_mastering_display_local_tags[1].local_tag);
            avio_wb16(pb, rescale_mastering_chroma(metadata->white_point[0]));
            avio_wb16(pb, rescale_mastering_chroma(metadata->white_point[1]));
        } else {
            av_log(NULL, AV_LOG_VERBOSE, "Not writing mastering display primaries. Missing data.\n");
        }
        if (metadata->has_luminance) {
            mxf_write_local_tag(pb, 4, ff_mxf_mastering_display_local_tags[2].local_tag);
            avio_wb32(pb, rescale_mastering_luma(metadata->max_luminance));
            mxf_write_local_tag(pb, 4, ff_mxf_mastering_display_local_tags[3].local_tag);
            avio_wb32(pb, rescale_mastering_luma(metadata->min_luminance));
        } else {
            av_log(NULL, AV_LOG_VERBOSE, "Not writing mastering display luminances. Missing data.\n");
        }
    }

    if (sc->interlaced && sc->field_dominance) {
        mxf_write_local_tag(pb, 1, 0x3212);
        avio_w8(pb, sc->field_dominance);
    }

    if (st->codecpar->codec_id == AV_CODEC_ID_H264 && !sc->avc_intra) {
        // write avc sub descriptor ref
        mxf_write_local_tag(pb, 8 + 16, 0x8100);
        mxf_write_refs_count(pb, 1);
        mxf_write_uuid(pb, AVCSubDescriptor, 0);
    }

    return pos;
}

static void mxf_update_klv_size(AVIOContext *pb, int64_t pos)
{
    int64_t cur_pos = avio_tell(pb);
    int size = cur_pos - pos;
    avio_seek(pb, pos - 4, SEEK_SET);
    klv_encode_ber4_length(pb, size);
    avio_seek(pb, cur_pos, SEEK_SET);
}

static void mxf_write_avc_subdesc(AVFormatContext *s, AVStream *st)
{
    AVIOContext *pb = s->pb;
    int64_t pos;

    avio_write(pb, mxf_avc_subdescriptor_key, 16);
    klv_encode_ber4_length(pb, 0);
    pos = avio_tell(pb);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, AVCSubDescriptor, 0);

    mxf_write_local_tag(pb, 1, 0x8200);
    avio_w8(pb, 0xFF); // AVC Decoding Delay, unknown

    mxf_write_local_tag(pb, 1, 0x8201);
    avio_w8(pb, st->codecpar->profile); // AVC Profile

    mxf_write_local_tag(pb, 1, 0x8202);
    avio_w8(pb, st->codecpar->level); // AVC Level

    mxf_update_klv_size(s->pb, pos);
}

static void mxf_write_cdci_desc(AVFormatContext *s, AVStream *st)
{
    int64_t pos = mxf_write_cdci_common(s, st, mxf_cdci_descriptor_key);
    mxf_update_klv_size(s->pb, pos);

    if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        mxf_write_avc_subdesc(s, st);
    }
}

static void mxf_write_h264_desc(AVFormatContext *s, AVStream *st)
{
    MXFStreamContext *sc = st->priv_data;
    if (sc->avc_intra) {
        mxf_write_mpegvideo_desc(s, st);
    } else {
        int64_t pos = mxf_write_cdci_common(s, st, mxf_cdci_descriptor_key);
        mxf_update_klv_size(s->pb, pos);
        mxf_write_avc_subdesc(s, st);
    }
}

static void mxf_write_s436m_anc_desc(AVFormatContext *s, AVStream *st)
{
    int64_t pos = mxf_write_generic_desc(s, st, mxf_s436m_anc_descriptor_key);
    mxf_update_klv_size(s->pb, pos);
}

static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st)
{
    AVIOContext *pb = s->pb;
    MXFStreamContext *sc = st->priv_data;
    int profile_and_level = (st->codecpar->profile<<4) | st->codecpar->level;
    int64_t pos = mxf_write_cdci_common(s, st, mxf_mpegvideo_descriptor_key);

    if (st->codecpar->codec_id != AV_CODEC_ID_H264) {
        // bit rate
        mxf_write_local_tag(pb, 4, 0x8000);
        avio_wb32(pb, sc->video_bit_rate);

        // profile and level
        mxf_write_local_tag(pb, 1, 0x8007);
        if (!st->codecpar->profile)
            profile_and_level |= 0x80; // escape bit
        avio_w8(pb, profile_and_level);

        // low delay
        mxf_write_local_tag(pb, 1, 0x8003);
        avio_w8(pb, sc->low_delay);

        // closed gop
        mxf_write_local_tag(pb, 1, 0x8004);
        avio_w8(pb, sc->seq_closed_gop);

        // max gop
        mxf_write_local_tag(pb, 2, 0x8006);
        avio_wb16(pb, sc->max_gop);

        // b picture count
        mxf_write_local_tag(pb, 2, 0x8008);
        avio_wb16(pb, sc->b_picture_count);
    }

    mxf_update_klv_size(pb, pos);
}

static int64_t mxf_write_generic_sound_common(AVFormatContext *s, AVStream *st, const UID key)
{
    AVIOContext *pb = s->pb;
    MXFContext *mxf = s->priv_data;
    int show_warnings = !mxf->footer_partition_offset;
    int64_t pos = mxf_write_generic_desc(s, st, key);

    if (s->oformat == &ff_mxf_opatom_muxer) {
        mxf_write_local_tag(pb, 8, 0x3002);
        avio_wb64(pb, mxf->body_offset / mxf->edit_unit_byte_count);
    }

    // audio locked
    mxf_write_local_tag(pb, 1, 0x3D02);
    avio_w8(pb, 1);

    // write audio sampling rate
    mxf_write_local_tag(pb, 8, 0x3D03);
    avio_wb32(pb, st->codecpar->sample_rate);
    avio_wb32(pb, 1);

    if (s->oformat == &ff_mxf_d10_muxer) {
        mxf_write_local_tag(pb, 1, 0x3D04);
        avio_w8(pb, 0);
    }

    mxf_write_local_tag(pb, 4, 0x3D07);
    if (mxf->channel_count == -1) {
        if (show_warnings && (s->oformat == &ff_mxf_d10_muxer) && (st->codecpar->channels != 4) && (st->codecpar->channels != 8))
            av_log(s, AV_LOG_WARNING, "the number of audio channels shall be 4 or 8 : the output will not comply to MXF D-10 specs, use -d10_channelcount to fix this\n");
        avio_wb32(pb, st->codecpar->channels);
    } else if (s->oformat == &ff_mxf_d10_muxer) {
        if (show_warnings && (mxf->channel_count < st->codecpar->channels))
            av_log(s, AV_LOG_WARNING, "d10_channelcount < actual number of audio channels : some channels will be discarded\n");
        if (show_warnings && (mxf->channel_count != 4) && (mxf->channel_count != 8))
            av_log(s, AV_LOG_WARNING, "d10_channelcount shall be set to 4 or 8 : the output will not comply to MXF D-10 specs\n");
        avio_wb32(pb, mxf->channel_count);
    } else {
        avio_wb32(pb, st->codecpar->channels);
    }

    mxf_write_local_tag(pb, 4, 0x3D01);
    avio_wb32(pb, av_get_bits_per_sample(st->codecpar->codec_id));

    return pos;
}

static int64_t mxf_write_wav_common(AVFormatContext *s, AVStream *st, const UID key)
{
    AVIOContext *pb = s->pb;
    int64_t pos = mxf_write_generic_sound_common(s, st, key);

    mxf_write_local_tag(pb, 2, 0x3D0A);
    avio_wb16(pb, st->codecpar->block_align);

    // avg bytes per sec
    mxf_write_local_tag(pb, 4, 0x3D09);
    avio_wb32(pb, st->codecpar->block_align*st->codecpar->sample_rate);

    return pos;
}

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st)
{
    int64_t pos = mxf_write_wav_common(s, st, mxf_wav_descriptor_key);
    mxf_update_klv_size(s->pb, pos);
}

static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st)
{
    int64_t pos = mxf_write_wav_common(s, st, mxf_aes3_descriptor_key);
    mxf_update_klv_size(s->pb, pos);
}

static void mxf_write_generic_sound_desc(AVFormatContext *s, AVStream *st)
{
    int64_t pos = mxf_write_generic_sound_common(s, st, mxf_generic_sound_descriptor_key);
    mxf_update_klv_size(s->pb, pos);
}

static const uint8_t mxf_indirect_value_utf16le[] = { 0x4c,0x00,0x02,0x10,0x01,0x00,0x00,0x00,0x00,0x06,0x0e,0x2b,0x34,0x01,0x04,0x01,0x01 };

static int mxf_write_tagged_value(AVFormatContext *s, const char* name, const char* value)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int name_size = mxf_utf16_local_tag_length(name);
    int indirect_value_size = 13 + mxf_utf16_local_tag_length(value);

    if (!name_size || indirect_value_size == 13)
        return 1;

    mxf_write_metadata_key(pb, 0x013f00);
    klv_encode_ber_length(pb, 24 + name_size + indirect_value_size);

    // write instance UID
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, TaggedValue, mxf->tagged_value_count);

    // write name
    mxf_write_local_tag_utf16(pb, 0x5001, name); // Name

    // write indirect value
    mxf_write_local_tag(pb, indirect_value_size, 0x5003);
    avio_write(pb, mxf_indirect_value_utf16le, 17);
    avio_put_str16le(pb, value);

    mxf->tagged_value_count++;
    return 0;
}

static int mxf_write_user_comments(AVFormatContext *s, const AVDictionary *m)
{
    MXFContext *mxf = s->priv_data;
    AVDictionaryEntry *t = NULL;
    int count = 0;

    while ((t = av_dict_get(m, "comment_", t, AV_DICT_IGNORE_SUFFIX))) {
        if (mxf->tagged_value_count >= UINT16_MAX) {
            av_log(s, AV_LOG_ERROR, "too many tagged values, ignoring remaining\n");
            return count;
        }

        if (mxf_write_tagged_value(s, t->key + 8, t->value) == 0)
            count++;
    }
    return count;
}

static void mxf_write_package(AVFormatContext *s, MXFPackage *package)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, track_count = s->nb_streams+1;
    int name_size = mxf_utf16_local_tag_length(package->name);
    int user_comment_count = 0;

    if (package->type == MaterialPackage) {
        if (mxf->store_user_comments)
            user_comment_count = mxf_write_user_comments(s, s->metadata);
        mxf_write_metadata_key(pb, 0x013600);
        PRINT_KEY(s, "Material Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 92 + name_size + (16*track_count) + (16*user_comment_count) + 12LL*mxf->store_user_comments);
    } else {
        mxf_write_metadata_key(pb, 0x013700);
        PRINT_KEY(s, "Source Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 112 + name_size + (16*track_count) + 12LL*mxf->store_user_comments); // 20 bytes length for descriptor reference
    }

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, package->type, package->instance);
    av_log(s, AV_LOG_DEBUG, "package type:%d\n", package->type);
    PRINT_KEY(s, "package uid", pb->buf_ptr - 16);

    // write package umid
    mxf_write_local_tag(pb, 32, 0x4401);
    mxf_write_umid(s, package->instance);
    PRINT_KEY(s, "package umid second part", pb->buf_ptr - 16);

    // package name
    if (name_size)
        mxf_write_local_tag_utf16(pb, 0x4402, package->name);

    // package creation date
    mxf_write_local_tag(pb, 8, 0x4405);
    avio_wb64(pb, mxf->timestamp);

    // package modified date
    mxf_write_local_tag(pb, 8, 0x4404);
    avio_wb64(pb, mxf->timestamp);

    // write track refs
    mxf_write_local_tag(pb, track_count*16 + 8, 0x4403);
    mxf_write_refs_count(pb, track_count);
    // these are the uuids of the tracks the will be written in mxf_write_track
    for (i = 0; i < track_count; i++)
        mxf_write_uuid(pb, Track,  mxf->track_instance_count + i);

    // write user comment refs
    if (mxf->store_user_comments) {
        mxf_write_local_tag(pb, user_comment_count*16 + 8, 0x4406);
        mxf_write_refs_count(pb, user_comment_count);
        for (i = 0; i < user_comment_count; i++)
            mxf_write_uuid(pb, TaggedValue, mxf->tagged_value_count - user_comment_count + i);
    }

    // write multiple descriptor reference
    if (package->type == SourcePackage && package->instance == 1) {
        mxf_write_local_tag(pb, 16, 0x4701);
        if (s->nb_streams > 1) {
            mxf_write_uuid(pb, MultipleDescriptor, 0);
            mxf_write_multi_descriptor(s);
        } else
            mxf_write_uuid(pb, SubDescriptor, 0);
    } else if (package->type == SourcePackage && package->instance == 2) {
        mxf_write_local_tag(pb, 16, 0x4701);
        mxf_write_uuid(pb, TapeDescriptor, 0);
        mxf_write_tape_descriptor(s);
    }

    /*
     * for every 1 track in a package there is 1 sequence and 1 component.
     * all 3 of these elements share the same instance number for generating
     * there instance uuids. mxf->track_instance_count stores this value.
     * mxf->track_instance_count is incremented after a group of all 3 of
     * these elements are written.
     */

    // write timecode track
    mxf_write_track(s, mxf->timecode_track, package);
    mxf_write_sequence(s, mxf->timecode_track, package);
    mxf_write_timecode_component(s, mxf->timecode_track, package);
    mxf->track_instance_count++;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        mxf_write_track(s, st, package);
        mxf_write_sequence(s, st, package);
        mxf_write_structural_component(s, st, package);
        mxf->track_instance_count++;

        if (package->type == SourcePackage && package->instance == 1) {
            MXFStreamContext *sc = st->priv_data;
            mxf_essence_container_uls[sc->index].write_desc(s, st);
        }
    }
}

static int mxf_write_essence_container_data(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012300);
    klv_encode_ber_length(pb, 72);

    mxf_write_local_tag(pb, 16, 0x3C0A); // Instance UID
    mxf_write_uuid(pb, EssenceContainerData, 0);

    mxf_write_local_tag(pb, 32, 0x2701); // Linked Package UID
    mxf_write_umid(s, 1);

    mxf_write_local_tag(pb, 4, 0x3F07); // BodySID
    avio_wb32(pb, 1);

    mxf_write_local_tag(pb, 4, 0x3F06); // IndexSID
    avio_wb32(pb, 2);

    return 0;
}

static int mxf_write_header_metadata_sets(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVDictionaryEntry *entry = NULL;
    AVStream *st = NULL;
    int i;
    MXFPackage packages[3] = {{0}};
    int package_count = 2;
    packages[0].type = MaterialPackage;
    packages[1].type = SourcePackage;
    packages[1].instance = 1;
    packages[0].ref = &packages[1];


    if (entry = av_dict_get(s->metadata, "material_package_name", NULL, 0))
       packages[0].name = entry->value;

    if (entry = av_dict_get(s->metadata, "file_package_name", NULL, 0)) {
        packages[1].name = entry->value;
    } else {
        /* check if any of the streams contain a file_package_name */
        for (i = 0; i < s->nb_streams; i++) {
            st = s->streams[i];
            if (entry = av_dict_get(st->metadata, "file_package_name", NULL, 0)) {
                packages[1].name = entry->value;
                break;
            }
        }
    }

    entry = av_dict_get(s->metadata, "reel_name", NULL, 0);
    if (entry) {
        packages[2].name = entry->value;
        packages[2].type = SourcePackage;
        packages[2].instance = 2;
        packages[1].ref = &packages[2];
        package_count = 3;
    }

    mxf_write_preface(s);
    mxf_write_identification(s);
    mxf_write_content_storage(s, packages, package_count);
    mxf->track_instance_count = 0;
    for (i = 0; i < package_count; i++)
        mxf_write_package(s, &packages[i]);
    mxf_write_essence_container_data(s);
    return 0;
}

static unsigned klv_fill_size(uint64_t size)
{
    unsigned pad = KAG_SIZE - (size & (KAG_SIZE-1));
    if (pad < 20) // smallest fill item possible
        return pad + KAG_SIZE;
    else
        return pad & (KAG_SIZE-1);
}

static void mxf_write_index_table_segment(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, j, temporal_reordering = 0;
    int key_index = mxf->last_key_index;
    int prev_non_b_picture = 0;
    int audio_frame_size = 0;
    int64_t pos;

    av_log(s, AV_LOG_DEBUG, "edit units count %d\n", mxf->edit_units_count);

    if (!mxf->edit_units_count && !mxf->edit_unit_byte_count)
        return;

    avio_write(pb, index_table_segment_key, 16);

    klv_encode_ber4_length(pb, 0);
    pos = avio_tell(pb);

    // instance id
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, IndexTableSegment, 0);

    // index edit rate
    mxf_write_local_tag(pb, 8, 0x3F0B);
    avio_wb32(pb, mxf->time_base.den);
    avio_wb32(pb, mxf->time_base.num);

    // index start position
    mxf_write_local_tag(pb, 8, 0x3F0C);
    avio_wb64(pb, mxf->last_indexed_edit_unit);

    // index duration
    mxf_write_local_tag(pb, 8, 0x3F0D);
    if (mxf->edit_unit_byte_count)
        avio_wb64(pb, 0); // index table covers whole container
    else
        avio_wb64(pb, mxf->edit_units_count);

    // edit unit byte count
    mxf_write_local_tag(pb, 4, 0x3F05);
    avio_wb32(pb, mxf->edit_unit_byte_count);

    // index sid
    mxf_write_local_tag(pb, 4, 0x3F06);
    avio_wb32(pb, 2);

    // body sid
    mxf_write_local_tag(pb, 4, 0x3F07);
    avio_wb32(pb, 1);

    // real slice count - 1
    mxf_write_local_tag(pb, 1, 0x3F08);
    avio_w8(pb, !mxf->edit_unit_byte_count); // only one slice for CBR

    // delta entry array
    mxf_write_local_tag(pb, 8 + (s->nb_streams+1)*6, 0x3F09);
    avio_wb32(pb, s->nb_streams+1); // num of entries
    avio_wb32(pb, 6);               // size of one entry
    // write system item delta entry
    avio_w8(pb, 0);
    avio_w8(pb, 0); // slice entry
    avio_wb32(pb, 0); // element delta
    // write each stream delta entry
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = st->priv_data;
        avio_w8(pb, sc->temporal_reordering);
        if (sc->temporal_reordering)
            temporal_reordering = 1;
        if (mxf->edit_unit_byte_count) {
            avio_w8(pb, 0); // slice number
            avio_wb32(pb, sc->slice_offset);
        } else if (i == 0) { // video track
            avio_w8(pb, 0); // slice number
            avio_wb32(pb, KAG_SIZE); // system item size including klv fill
        } else { // audio or data track
            if (!audio_frame_size) {
                audio_frame_size = sc->frame_size;
                audio_frame_size += klv_fill_size(audio_frame_size);
            }
            avio_w8(pb, 1);
            avio_wb32(pb, (i-1)*audio_frame_size); // element delta
        }
    }

    if (!mxf->edit_unit_byte_count) {
        MXFStreamContext *sc = s->streams[0]->priv_data;
        mxf_write_local_tag(pb, 8 + mxf->edit_units_count*15, 0x3F0A);
        avio_wb32(pb, mxf->edit_units_count);  // num of entries
        avio_wb32(pb, 15);  // size of one entry

        for (i = 0; i < mxf->edit_units_count; i++) {
            int temporal_offset = 0;

            if (!(mxf->index_entries[i].flags & 0x33)) { // I-frame
                sc->max_gop = FFMAX(sc->max_gop, i - mxf->last_key_index);
                mxf->last_key_index = key_index;
                key_index = i;
            }

            if (temporal_reordering) {
                int pic_num_in_gop = i - key_index;
                if (pic_num_in_gop != mxf->index_entries[i].temporal_ref) {
                    for (j = key_index; j < mxf->edit_units_count; j++) {
                        if (pic_num_in_gop == mxf->index_entries[j].temporal_ref)
                            break;
                    }
                    if (j == mxf->edit_units_count)
                        av_log(s, AV_LOG_WARNING, "missing frames\n");
                    temporal_offset = j - key_index - pic_num_in_gop;
                }
            }
            avio_w8(pb, temporal_offset);

            if ((mxf->index_entries[i].flags & 0x30) == 0x30) { // back and forward prediction
                sc->b_picture_count = FFMAX(sc->b_picture_count, i - prev_non_b_picture);
                avio_w8(pb, mxf->last_key_index - i);
            } else {
                avio_w8(pb, key_index - i); // key frame offset
                if ((mxf->index_entries[i].flags & 0x20) == 0x20) // only forward
                    mxf->last_key_index = key_index;
                prev_non_b_picture = i;
            }

            if (!(mxf->index_entries[i].flags & 0x33) && // I-frame
                mxf->index_entries[i].flags & 0x40 && !temporal_offset)
                mxf->index_entries[i].flags |= 0x80; // random access
            avio_w8(pb, mxf->index_entries[i].flags);
            // stream offset
            avio_wb64(pb, mxf->index_entries[i].offset);
            if (s->nb_streams > 1)
                avio_wb32(pb, mxf->index_entries[i].slice_offset);
            else
                avio_wb32(pb, 0);
        }

        mxf->last_key_index = key_index - mxf->edit_units_count;
        mxf->last_indexed_edit_unit += mxf->edit_units_count;
        mxf->edit_units_count = 0;
    }

    mxf_update_klv_size(pb, pos);
}

static void mxf_write_klv_fill(AVFormatContext *s)
{
    unsigned pad = klv_fill_size(avio_tell(s->pb));
    if (pad) {
        avio_write(s->pb, klv_fill_key, 16);
        pad -= 16 + 4;
        klv_encode_ber4_length(s->pb, pad);
        ffio_fill(s->pb, 0, pad);
        av_assert1(!(avio_tell(s->pb) & (KAG_SIZE-1)));
    }
}

static int mxf_write_partition(AVFormatContext *s, int bodysid,
                                int indexsid,
                                const uint8_t *key, int write_metadata)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t header_byte_count_offset;
    unsigned index_byte_count = 0;
    uint64_t partition_offset = avio_tell(pb);
    int err;

    if (!mxf->edit_unit_byte_count && mxf->edit_units_count)
        index_byte_count = 85 + 12+(s->nb_streams+1)*6 +
            12+mxf->edit_units_count*15;
    else if (mxf->edit_unit_byte_count && indexsid)
        index_byte_count = 80;

    if (index_byte_count) {
        index_byte_count += 16 + 4; // add encoded ber4 length
        index_byte_count += klv_fill_size(index_byte_count);
    }

    if (key && !memcmp(key, body_partition_key, 16)) {
        if ((err = av_reallocp_array(&mxf->body_partition_offset, mxf->body_partitions_count + 1,
                                     sizeof(*mxf->body_partition_offset))) < 0) {
            mxf->body_partitions_count = 0;
            return err;
        }
        mxf->body_partition_offset[mxf->body_partitions_count++] = partition_offset;
    }

    // write klv
    if (key)
        avio_write(pb, key, 16);
    else
        avio_write(pb, body_partition_key, 16);

    klv_encode_ber4_length(pb, 88 + 16LL * DESCRIPTOR_COUNT(mxf->essence_container_count));

    // write partition value
    avio_wb16(pb, 1); // majorVersion
    avio_wb16(pb, 3); // minorVersion
    avio_wb32(pb, KAG_SIZE); // KAGSize

    avio_wb64(pb, partition_offset); // ThisPartition

    if (key && !memcmp(key, body_partition_key, 16) && mxf->body_partitions_count > 1)
        avio_wb64(pb, mxf->body_partition_offset[mxf->body_partitions_count-2]); // PreviousPartition
    else if (key && !memcmp(key, footer_partition_key, 16) && mxf->body_partitions_count)
        avio_wb64(pb, mxf->body_partition_offset[mxf->body_partitions_count-1]); // PreviousPartition
    else
        avio_wb64(pb, 0);

    avio_wb64(pb, mxf->footer_partition_offset); // footerPartition

    // set offset
    header_byte_count_offset = avio_tell(pb);
    avio_wb64(pb, 0); // headerByteCount, update later

    // indexTable
    avio_wb64(pb, index_byte_count); // indexByteCount
    avio_wb32(pb, index_byte_count ? indexsid : 0); // indexSID

    // BodyOffset
    if (bodysid && mxf->edit_units_count && mxf->body_partitions_count && s->oformat != &ff_mxf_opatom_muxer)
        avio_wb64(pb, mxf->body_offset);
    else
        avio_wb64(pb, 0);

    avio_wb32(pb, bodysid); // bodySID

    // operational pattern
    if (s->oformat == &ff_mxf_opatom_muxer)
        avio_write(pb, opatom_ul, 16);
    else
        avio_write(pb, op1a_ul, 16);

    // essence container
    mxf_write_essence_container_refs(s);

    if (write_metadata) {
        // mark the start of the headermetadata and calculate metadata size
        int64_t pos, start;
        unsigned header_byte_count;

        mxf_write_klv_fill(s);
        start = avio_tell(s->pb);
        mxf_write_primer_pack(s);
        mxf_write_klv_fill(s);
        mxf_write_header_metadata_sets(s);
        pos = avio_tell(s->pb);
        header_byte_count = pos - start + klv_fill_size(pos);

        // update header_byte_count
        avio_seek(pb, header_byte_count_offset, SEEK_SET);
        avio_wb64(pb, header_byte_count);
        avio_seek(pb, pos, SEEK_SET);
    }

    if(key)
        avio_write_marker(pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);

    return 0;
}

static const struct {
    int profile;
    UID codec_ul;
} mxf_prores_codec_uls[] = {
    { FF_PROFILE_PRORES_PROXY,    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x01,0x00 } },
    { FF_PROFILE_PRORES_LT,       { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x02,0x00 } },
    { FF_PROFILE_PRORES_STANDARD, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x03,0x00 } },
    { FF_PROFILE_PRORES_HQ,       { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x04,0x00 } },
    { FF_PROFILE_PRORES_4444,     { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x05,0x00 } },
    { FF_PROFILE_PRORES_XQ,       { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x03,0x06,0x06,0x00 } },
};

static int mxf_parse_prores_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    int i, profile;

    if (mxf->header_written)
        return 1;

    sc->codec_ul = NULL;
    profile = st->codecpar->profile;
    for (i = 0; i < FF_ARRAY_ELEMS(mxf_prores_codec_uls); i++) {
        if (profile == mxf_prores_codec_uls[i].profile) {
            sc->codec_ul = &mxf_prores_codec_uls[i].codec_ul;
            break;
        }
    }
    if (!sc->codec_ul)
        return 0;

    sc->frame_size = pkt->size;

    return 1;
}

static const struct {
    int cid;
    UID codec_ul;
} mxf_dnxhd_codec_uls[] = {
    { 1235, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x01,0x00,0x00 } }, // 1080p 10bit HIGH
    { 1237, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x03,0x00,0x00 } }, // 1080p 8bit MED
    { 1238, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x04,0x00,0x00 } }, // 1080p 8bit HIGH
    { 1241, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x07,0x00,0x00 } }, // 1080i 10bit HIGH
    { 1242, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x08,0x00,0x00 } }, // 1080i 8bit MED
    { 1243, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x09,0x00,0x00 } }, // 1080i 8bit HIGH
    { 1244, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x0a,0x00,0x00 } }, // 1080i 8bit TR
    { 1250, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x10,0x00,0x00 } }, // 720p 10bit
    { 1251, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x11,0x00,0x00 } }, // 720p 8bit HIGH
    { 1252, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x12,0x00,0x00 } }, // 720p 8bit MED
    { 1253, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x13,0x00,0x00 } }, // 720p 8bit LOW
    { 1256, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x16,0x00,0x00 } }, // 1080p 10bit 444
    { 1258, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x18,0x00,0x00 } }, // 720p 8bit TR
    { 1259, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x19,0x00,0x00 } }, // 1080p 8bit TR
    { 1260, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x1a,0x00,0x00 } }, // 1080i 8bit TR MBAFF
    { 1270, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x24,0x00,0x00 } }, // DNXHR 444
    { 1271, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x25,0x00,0x00 } }, // DNXHR HQX
    { 1272, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x26,0x00,0x00 } }, // DNXHR HQ
    { 1273, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x27,0x00,0x00 } }, // DNXHR SQ
    { 1274, { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x71,0x28,0x00,0x00 } }, // DNXHR LB
};

static int mxf_parse_dnxhd_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    int i, cid, frame_size = 0;

    if (mxf->header_written)
        return 1;

    if (pkt->size < 43)
        return 0;

    sc->codec_ul = NULL;
    cid = AV_RB32(pkt->data + 0x28);
    for (i = 0; i < FF_ARRAY_ELEMS(mxf_dnxhd_codec_uls); i++) {
        if (cid == mxf_dnxhd_codec_uls[i].cid) {
            sc->codec_ul = &mxf_dnxhd_codec_uls[i].codec_ul;
            break;
        }
    }
    if (!sc->codec_ul)
        return 0;

    sc->component_depth = 0;
    switch (pkt->data[0x21] >> 5) {
    case 1: sc->component_depth = 8; break;
    case 2: sc->component_depth = 10; break;
    case 3: sc->component_depth = 12; break;
    }
    if (!sc->component_depth)
        return 0;

    if ((frame_size = avpriv_dnxhd_get_frame_size(cid)) == DNXHD_VARIABLE) {
        frame_size = avpriv_dnxhd_get_hr_frame_size(cid, st->codecpar->width, st->codecpar->height);
    }
    if (frame_size < 0)
        return 0;

    if ((sc->interlaced = avpriv_dnxhd_get_interlaced(cid)) < 0)
        return 0;

    if (cid >= 1270) { // RI raster
        av_reduce(&sc->aspect_ratio.num, &sc->aspect_ratio.den,
                  st->codecpar->width, st->codecpar->height,
                  INT_MAX);
    } else {
        sc->aspect_ratio = (AVRational){ 16, 9 };
    }

    sc->frame_size = pkt->size;

    return 1;
}

static const struct {
    const UID container_ul;
    const UID codec_ul;
} mxf_dv_uls[] = {
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x01,0x01 }, // IEC DV25 525/60
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x01,0x01,0x00 } },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x02,0x01 }, // IEC DV25 626/50
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x01,0x02,0x00 } },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x40,0x01 }, // DV25 525/60
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x01,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x41,0x01 }, // DV25 625/50
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x02,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x50,0x01 }, // DV50 525/60
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x03,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x51,0x01 }, // DV50 625/50
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x04,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x60,0x01 }, // DV100 1080/60
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x05,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x61,0x01 }, // DV100 1080/50
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x06,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x62,0x01 }, // DV100 720/60
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x07,0x00 }, },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x63,0x01 }, // DV100 720/50
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x08,0x00 }, },
};

static int mxf_parse_dv_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    uint8_t *vs_pack, *vsc_pack;
    int apt, ul_index, stype, pal;

    if (mxf->header_written)
        return 1;

    // Check for minimal frame size
    if (pkt->size < 120000)
        return -1;

    apt      = pkt->data[4] & 0x7;
    vs_pack  = pkt->data + 80*5 + 48;
    vsc_pack = pkt->data + 80*5 + 53;
    stype    = vs_pack[3] & 0x1f;
    pal      = (vs_pack[3] >> 5) & 0x1;

    if ((vsc_pack[2] & 0x07) == 0x02) {
        sc->aspect_ratio = (AVRational){ 16, 9 };
    } else {
        sc->aspect_ratio = (AVRational){ 4, 3 };
    }

    sc->interlaced = (vsc_pack[3] >> 4) & 0x01;
    // TODO: fix dv encoder to set proper FF/FS value in VSC pack
    // and set field dominance accordingly
    // av_log(s, AV_LOG_DEBUG, "DV vsc pack ff/ss = %x\n", vsc_pack[2] >> 6);

    switch (stype) {
    case 0x18: // DV100 720p
        ul_index = 8+pal;
        if (sc->interlaced) {
            av_log(s, AV_LOG_ERROR, "source marked as interlaced but codec profile is progressive\n");
            sc->interlaced = 0;
        }
        break;
    case 0x14: // DV100 1080i
        ul_index = 6+pal;
        break;
    case 0x04: // DV50
        ul_index = 4+pal;
        break;
    default: // DV25
        if (!apt) { // IEC
            ul_index = 0+pal;
        } else {
            ul_index = 2+pal;
        }
    }

    sc->container_ul = &mxf_dv_uls[ul_index].container_ul;
    sc->codec_ul = &mxf_dv_uls[ul_index].codec_ul;

    sc->frame_size = pkt->size;

    return 1;
}

static const struct {
    UID uid;
    int frame_size;
    int profile;
    uint8_t interlaced;
    int intra_only; // 1 or 0 when there are separate UIDs for Long GOP and Intra, -1 when Intra/LGOP detection can be ignored
} mxf_h264_codec_uls[] = {
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x11,0x01 },      0,  66, 0, -1 }, // AVC Baseline
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x20,0x01 },      0,  77, 0, -1 }, // AVC Main
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x30,0x01 },      0,  88, 0, -1 }, // AVC Extended
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x40,0x01 },      0, 100, 0, -1 }, // AVC High
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x50,0x01 },      0, 110, 0,  0 }, // AVC High 10
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x60,0x01 },      0, 122, 0,  0 }, // AVC High 422
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x31,0x70,0x01 },      0, 244, 0,  0 }, // AVC High 444
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x20,0x01 },      0, 110, 0,  1 }, // AVC High 10 Intra
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x01 }, 232960, 110, 1,  1 }, // AVC High 10 Intra RP2027 Class 50 1080/59.94i
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x02 }, 281088, 110, 1,  1 }, // AVC High 10 Intra RP2027 Class 50 1080/50i
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x03 }, 232960, 110, 0,  1 }, // AVC High 10 Intra RP2027 Class 50 1080/29.97p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x04 }, 281088, 110, 0,  1 }, // AVC High 10 Intra RP2027 Class 50 1080/25p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x08 }, 116736, 110, 0,  1 }, // AVC High 10 Intra RP2027 Class 50 720/59.94p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x21,0x09 }, 140800, 110, 0,  1 }, // AVC High 10 Intra RP2027 Class 50 720/50p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x30,0x01 },      0, 122, 0,  1 }, // AVC High 422 Intra
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x01 }, 472576, 122, 1,  1 }, // AVC High 422 Intra RP2027 Class 100 1080/59.94i
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x02 }, 568832, 122, 1,  1 }, // AVC High 422 Intra RP2027 Class 100 1080/50i
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x03 }, 472576, 122, 0,  1 }, // AVC High 422 Intra RP2027 Class 100 1080/29.97p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x04 }, 568832, 122, 0,  1 }, // AVC High 422 Intra RP2027 Class 100 1080/25p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x08 }, 236544, 122, 0,  1 }, // AVC High 422 Intra RP2027 Class 100 720/59.94p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0a,0x04,0x01,0x02,0x02,0x01,0x32,0x31,0x09 }, 284672, 122, 0,  1 }, // AVC High 422 Intra RP2027 Class 100 720/50p
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x01,0x32,0x40,0x01 },      0, 244, 0,  1 }, // AVC High 444 Intra
    {{ 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0d,0x04,0x01,0x02,0x02,0x01,0x32,0x50,0x01 },      0,  44, 0, -1 }, // AVC CAVLC 444
};

static int mxf_parse_h264_frame(AVFormatContext *s, AVStream *st,
                                AVPacket *pkt, MXFIndexEntry *e)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    H264SPS seq, *const sps = &seq;
    GetBitContext gb;
    const uint8_t *buf = pkt->data;
    const uint8_t *buf_end = pkt->data + pkt->size;
    const uint8_t *nal_end;
    uint32_t state = -1;
    int extra_size = 512; // support AVC Intra files without SPS/PPS header
    int i, frame_size, slice_type, has_sps = 0, intra_only = 0, ret;

    for (;;) {
        buf = avpriv_find_start_code(buf, buf_end, &state);
        if (buf >= buf_end)
            break;

        switch (state & 0x1f) {
        case H264_NAL_SPS:
            e->flags |= 0x40;

            if (mxf->header_written)
                break;

            nal_end = ff_avc_find_startcode(buf, buf_end);
            ret = ff_avc_decode_sps(sps, buf, nal_end - buf);
            if (ret < 0) {
                av_log(s, AV_LOG_ERROR, "error parsing sps\n");
                return 0;
            }
            has_sps = 1;

            sc->aspect_ratio.num = st->codecpar->width * sps->sar.num;
            sc->aspect_ratio.den = st->codecpar->height * sps->sar.den;
            av_reduce(&sc->aspect_ratio.num, &sc->aspect_ratio.den,
                      sc->aspect_ratio.num, sc->aspect_ratio.den, 1024*1024);
            intra_only = (sps->constraint_set_flags >> 3) & 1;
            sc->interlaced = !sps->frame_mbs_only_flag;
            sc->component_depth = sps->bit_depth_luma;

            buf = nal_end;
            break;
        case H264_NAL_PPS:
            if (e->flags & 0x40) { // sequence header present
                e->flags |= 0x80; // random access
                extra_size = 0;
            }
            break;
        case H264_NAL_IDR_SLICE:
            e->flags |= 0x04; // IDR Picture
            buf = buf_end;
            break;
        case H264_NAL_SLICE:
            init_get_bits8(&gb, buf, buf_end - buf);
            get_ue_golomb_long(&gb); // skip first_mb_in_slice
            slice_type = get_ue_golomb_31(&gb);
            switch (slice_type % 5) {
            case 0:
                e->flags |= 0x20; // P Picture
                e->flags |= 0x06; // P Picture
                break;
            case 1:
                e->flags |= 0x30; // B Picture
                e->flags |= 0x03; // non-referenced B Picture
                break;
            }
            buf = buf_end;
            break;
        default:
            break;
        }
    }

    if (mxf->header_written)
        return 1;

    if (!has_sps)
        sc->interlaced = st->codecpar->field_order != AV_FIELD_PROGRESSIVE ? 1 : 0;
    sc->codec_ul = NULL;
    frame_size = pkt->size + extra_size;

    for (i = 0; i < FF_ARRAY_ELEMS(mxf_h264_codec_uls); i++) {
        if (frame_size == mxf_h264_codec_uls[i].frame_size && sc->interlaced == mxf_h264_codec_uls[i].interlaced) {
            sc->codec_ul = &mxf_h264_codec_uls[i].uid;
            sc->component_depth = 10; // AVC Intra is always 10 Bit
            sc->aspect_ratio = (AVRational){ 16, 9 }; // 16:9 is mandatory for broadcast HD
            st->codecpar->profile = mxf_h264_codec_uls[i].profile;
            sc->avc_intra = 1;
            mxf->cbr_index = 1;
            sc->frame_size = pkt->size;
            if (sc->interlaced)
                sc->field_dominance = 1; // top field first is mandatory for AVC Intra
            break;
        } else if (has_sps && mxf_h264_codec_uls[i].frame_size == 0 &&
                   mxf_h264_codec_uls[i].profile == sps->profile_idc &&
                   (mxf_h264_codec_uls[i].intra_only < 0 ||
                    mxf_h264_codec_uls[i].intra_only == intra_only)) {
            sc->codec_ul = &mxf_h264_codec_uls[i].uid;
            st->codecpar->profile = sps->profile_idc;
            st->codecpar->level = sps->level_idc;
            // continue to check for avc intra
        }
    }

    if (!sc->codec_ul) {
        av_log(s, AV_LOG_ERROR, "h264 profile not supported\n");
        return 0;
    }

    return 1;
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
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x05,0x02,0x00 }, // MP@H-14 I-Frame
    { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x04,0x01,0x02,0x02,0x01,0x05,0x03,0x00 }, // MP@H-14 Long GOP
};

static const UID *mxf_get_mpeg2_codec_ul(AVCodecParameters *par)
{
    int long_gop = 1;

    if (par->profile == 4) { // Main
        if (par->level == 8) // Main
            return &mxf_mpeg2_codec_uls[0+long_gop];
        else if (par->level == 4) // High
            return &mxf_mpeg2_codec_uls[4+long_gop];
        else if (par->level == 6) // High 14
            return &mxf_mpeg2_codec_uls[8+long_gop];
    } else if (par->profile == 0) { // 422
        if (par->level == 5) // Main
            return &mxf_mpeg2_codec_uls[2+long_gop];
        else if (par->level == 2) // High
            return &mxf_mpeg2_codec_uls[6+long_gop];
    }
    return NULL;
}

static int mxf_parse_mpeg2_frame(AVFormatContext *s, AVStream *st,
                                 AVPacket *pkt, MXFIndexEntry *e)
{
    MXFStreamContext *sc = st->priv_data;
    uint32_t c = -1;
    int i;

    for(i = 0; i < pkt->size - 4; i++) {
        c = (c<<8) + pkt->data[i];
        if (c == 0x1b5) {
            if ((pkt->data[i+1] & 0xf0) == 0x10) { // seq ext
                st->codecpar->profile = pkt->data[i+1] & 0x07;
                st->codecpar->level   = pkt->data[i+2] >> 4;
                sc->low_delay = pkt->data[i+6] >> 7;
            } else if (i + 5 < pkt->size && (pkt->data[i+1] & 0xf0) == 0x80) { // pict coding ext
                sc->interlaced = !(pkt->data[i+5] & 0x80); // progressive frame
                if (sc->interlaced)
                    sc->field_dominance = 1 + !(pkt->data[i+4] & 0x80); // top field first
                break;
            }
        } else if (c == 0x1b8) { // gop
            if (pkt->data[i+4]>>6 & 0x01) { // closed
                if (sc->seq_closed_gop == -1)
                    sc->seq_closed_gop = 1;
                sc->closed_gop = 1;
                if (e->flags & 0x40) // sequence header present
                    e->flags |= 0x80; // random access
            } else {
                sc->seq_closed_gop = 0;
                sc->closed_gop = 0;
            }
        } else if (c == 0x1b3) { // seq
            e->flags |= 0x40;
            switch ((pkt->data[i+4]>>4) & 0xf) {
            case 2:  sc->aspect_ratio = (AVRational){  4,  3}; break;
            case 3:  sc->aspect_ratio = (AVRational){ 16,  9}; break;
            case 4:  sc->aspect_ratio = (AVRational){221,100}; break;
            default:
                av_reduce(&sc->aspect_ratio.num, &sc->aspect_ratio.den,
                          st->codecpar->width, st->codecpar->height, 1024*1024);
            }
        } else if (c == 0x100) { // pic
            int pict_type = (pkt->data[i+2]>>3) & 0x07;
            e->temporal_ref = (pkt->data[i+1]<<2) | (pkt->data[i+2]>>6);
            if (pict_type == 2) { // P-frame
                e->flags |= 0x22;
                sc->closed_gop = 0; // reset closed GOP, don't matter anymore
            } else if (pict_type == 3) { // B-frame
                if (sc->closed_gop)
                    e->flags |= 0x13; // only backward prediction
                else
                    e->flags |= 0x33;
                sc->temporal_reordering = -1;
            } else if (!pict_type) {
                av_log(s, AV_LOG_ERROR, "error parsing mpeg2 frame\n");
                return 0;
            }
        }
    }
    if (s->oformat != &ff_mxf_d10_muxer)
        sc->codec_ul = mxf_get_mpeg2_codec_ul(st->codecpar);
    return !!sc->codec_ul;
}

static uint64_t mxf_parse_timestamp(int64_t timestamp64)
{
    time_t timestamp = timestamp64 / 1000000;
    struct tm tmbuf;
    struct tm *time = gmtime_r(&timestamp, &tmbuf);
    if (!time)
        return 0;
    return (uint64_t)(time->tm_year+1900) << 48 |
           (uint64_t)(time->tm_mon+1)     << 40 |
           (uint64_t) time->tm_mday       << 32 |
                      time->tm_hour       << 24 |
                      time->tm_min        << 16 |
                      time->tm_sec        << 8  |
                      (timestamp64 % 1000000) / 4000;
}

static void mxf_gen_umid(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    uint32_t seed = av_get_random_seed();
    uint64_t umid = seed + 0x5294713400000000LL;

    AV_WB64(mxf->umid  , umid);
    AV_WB64(mxf->umid+8, umid>>8);

    mxf->instance_number = seed & 0xFFFFFF;
}

static int mxf_init_timecode(AVFormatContext *s, AVStream *st, AVRational tbc)
{
    MXFContext *mxf = s->priv_data;
    AVDictionaryEntry *tcr = av_dict_get(s->metadata, "timecode", NULL, 0);

    if (!ff_mxf_get_content_package_rate(tbc)) {
        if (s->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
            av_log(s, AV_LOG_ERROR, "Unsupported frame rate %d/%d. Set -strict option to 'unofficial' or lower in order to allow it!\n", tbc.den, tbc.num);
            return AVERROR(EINVAL);
        } else {
            av_log(s, AV_LOG_WARNING, "Unofficial frame rate %d/%d.\n", tbc.den, tbc.num);
        }
    }

    mxf->timecode_base = (tbc.den + tbc.num/2) / tbc.num;
    if (!tcr)
        tcr = av_dict_get(st->metadata, "timecode", NULL, 0);

    if (tcr)
        return av_timecode_init_from_string(&mxf->tc, av_inv_q(tbc), tcr->value, s);
    else
        return av_timecode_init(&mxf->tc, av_inv_q(tbc), 0, 0, s);
}

static int mxf_write_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i, ret;
    uint8_t present[FF_ARRAY_ELEMS(mxf_essence_container_uls)] = {0};
    int64_t timestamp = 0;

    if (!s->nb_streams)
        return -1;

    if (s->oformat == &ff_mxf_opatom_muxer && s->nb_streams !=1) {
        av_log(s, AV_LOG_ERROR, "there must be exactly one stream for mxf opatom\n");
        return -1;
    }

    if (!av_dict_get(s->metadata, "comment_", NULL, AV_DICT_IGNORE_SUFFIX))
        mxf->store_user_comments = 0;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;
        sc->index = -1;

        if (((i == 0) ^ (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)) && s->oformat != &ff_mxf_opatom_muxer) {
            av_log(s, AV_LOG_ERROR, "there must be exactly one video stream and it must be the first one\n");
            return -1;
        }

        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(st->codecpar->format);
            // TODO: should be avg_frame_rate
            AVRational tbc = st->time_base;
            // Default component depth to 8
            sc->component_depth = 8;
            sc->h_chroma_sub_sample = 2;
            sc->v_chroma_sub_sample = 2;
            sc->color_siting = 0xFF;

            if (st->codecpar->sample_aspect_ratio.num && st->codecpar->sample_aspect_ratio.den) {
                sc->aspect_ratio = av_mul_q(st->codecpar->sample_aspect_ratio,
                                            av_make_q(st->codecpar->width, st->codecpar->height));
            }

            if (pix_desc) {
                sc->component_depth     = pix_desc->comp[0].depth;
                sc->h_chroma_sub_sample = 1 << pix_desc->log2_chroma_w;
                sc->v_chroma_sub_sample = 1 << pix_desc->log2_chroma_h;
            }
            switch (ff_choose_chroma_location(s, st)) {
            case AVCHROMA_LOC_TOPLEFT: sc->color_siting = 0; break;
            case AVCHROMA_LOC_LEFT:    sc->color_siting = 6; break;
            case AVCHROMA_LOC_TOP:     sc->color_siting = 1; break;
            case AVCHROMA_LOC_CENTER:  sc->color_siting = 3; break;
            }

            mxf->content_package_rate = ff_mxf_get_content_package_rate(tbc);
            mxf->time_base = tbc;
            avpriv_set_pts_info(st, 64, mxf->time_base.num, mxf->time_base.den);
            if((ret = mxf_init_timecode(s, st, tbc)) < 0)
                return ret;

            if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                sc->seq_closed_gop = -1; // unknown yet
            }

            sc->video_bit_rate = st->codecpar->bit_rate;

            if (s->oformat == &ff_mxf_d10_muxer ||
                st->codecpar->codec_id == AV_CODEC_ID_DNXHD ||
                st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO)
                mxf->cbr_index = 1;

            if (s->oformat == &ff_mxf_d10_muxer) {
                int ntsc = mxf->time_base.den != 25;
                int ul_index;

                if (st->codecpar->codec_id != AV_CODEC_ID_MPEG2VIDEO) {
                    av_log(s, AV_LOG_ERROR, "error MXF D-10 only support MPEG-2 Video\n");
                    return AVERROR(EINVAL);
                }
                if ((sc->video_bit_rate == 50000000) && (mxf->time_base.den == 25)) {
                    ul_index = 0;
                } else if ((sc->video_bit_rate == 49999840 || sc->video_bit_rate == 50000000) && ntsc) {
                    ul_index = 1;
                } else if (sc->video_bit_rate == 40000000) {
                    ul_index = 2+ntsc;
                } else if (sc->video_bit_rate == 30000000) {
                    ul_index = 4+ntsc;
                } else {
                    av_log(s, AV_LOG_ERROR, "error MXF D-10 only support 30/40/50 mbit/s\n");
                    return -1;
                }

                sc->codec_ul = &mxf_d10_codec_uls[ul_index];
                sc->container_ul = &mxf_d10_container_uls[ul_index];
                sc->index = INDEX_D10_VIDEO;
                sc->signal_standard = 1;
                sc->color_siting = 0;
                sc->frame_size = (int64_t)sc->video_bit_rate *
                    mxf->time_base.num / (8*mxf->time_base.den);
            }
            if (mxf->signal_standard >= 0)
                sc->signal_standard = mxf->signal_standard;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            char bsf_arg[32];
            if (st->codecpar->sample_rate != 48000) {
                av_log(s, AV_LOG_ERROR, "only 48khz is implemented\n");
                return -1;
            }
            avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
            if (s->oformat == &ff_mxf_d10_muxer) {
                if (st->index != 1) {
                    av_log(s, AV_LOG_ERROR, "MXF D-10 only support one audio track\n");
                    return -1;
                }
                if (st->codecpar->codec_id != AV_CODEC_ID_PCM_S16LE &&
                    st->codecpar->codec_id != AV_CODEC_ID_PCM_S24LE) {
                    av_log(s, AV_LOG_ERROR, "MXF D-10 only support 16 or 24 bits le audio\n");
                }
                sc->index = INDEX_D10_AUDIO;
                sc->container_ul = ((MXFStreamContext*)s->streams[0]->priv_data)->container_ul;
                sc->frame_size = 4 + 8 * av_rescale_rnd(st->codecpar->sample_rate, mxf->time_base.num, mxf->time_base.den, AV_ROUND_UP) * 4;
            } else if (s->oformat == &ff_mxf_opatom_muxer) {
                AVRational tbc = av_inv_q(mxf->audio_edit_rate);

                if (st->codecpar->codec_id != AV_CODEC_ID_PCM_S16LE &&
                    st->codecpar->codec_id != AV_CODEC_ID_PCM_S24LE) {
                    av_log(s, AV_LOG_ERROR, "Only pcm_s16le and pcm_s24le audio codecs are implemented\n");
                    return AVERROR_PATCHWELCOME;
                }
                if (st->codecpar->channels != 1) {
                    av_log(s, AV_LOG_ERROR, "MXF OPAtom only supports single channel audio\n");
                    return AVERROR(EINVAL);
                }

                mxf->time_base = st->time_base;
                if((ret = mxf_init_timecode(s, st, tbc)) < 0)
                    return ret;

                mxf->edit_unit_byte_count = (av_get_bits_per_sample(st->codecpar->codec_id) * st->codecpar->channels) >> 3;
                sc->index = INDEX_WAV;
            } else {
                mxf->slice_count = 1;
                sc->frame_size = st->codecpar->channels *
                                 av_rescale_rnd(st->codecpar->sample_rate, mxf->time_base.num, mxf->time_base.den, AV_ROUND_UP) *
                                 av_get_bits_per_sample(st->codecpar->codec_id) / 8;
            }
            snprintf(bsf_arg, sizeof(bsf_arg), "r=%d/%d", mxf->tc.rate.num, mxf->tc.rate.den);
            ret = ff_stream_add_bitstream_filter(st, "pcm_rechunk", bsf_arg);
            if (ret < 0)
                return ret;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            AVDictionaryEntry *e = av_dict_get(st->metadata, "data_type", NULL, 0);
            if (e && !strcmp(e->value, "vbi_vanc_smpte_436M")) {
                sc->index = INDEX_S436M;
            } else {
                av_log(s, AV_LOG_ERROR, "track %d: unsupported data type\n", i);
                return -1;
            }
            if (st->index != s->nb_streams - 1) {
                av_log(s, AV_LOG_ERROR, "data track must be placed last\n");
                return -1;
            }
        }

        if (sc->index == -1) {
            sc->index = mxf_get_essence_container_ul_index(st->codecpar->codec_id);
            if (sc->index == -1) {
                av_log(s, AV_LOG_ERROR, "track %d: could not find essence container ul, "
                       "codec not currently supported in container\n", i);
                return -1;
            }
        }

        if (!sc->codec_ul)
            sc->codec_ul = &mxf_essence_container_uls[sc->index].codec_ul;
        if (!sc->container_ul)
            sc->container_ul = &mxf_essence_container_uls[sc->index].container_ul;

        memcpy(sc->track_essence_element_key, mxf_essence_container_uls[sc->index].element_ul, 15);
        sc->track_essence_element_key[15] = present[sc->index];
        PRINT_KEY(s, "track essence element key", sc->track_essence_element_key);

        if (!present[sc->index])
            mxf->essence_container_count++;
        present[sc->index]++;
    }

    if (s->oformat == &ff_mxf_d10_muxer || s->oformat == &ff_mxf_opatom_muxer) {
        mxf->essence_container_count = 1;
    }

    if (!(s->flags & AVFMT_FLAG_BITEXACT))
        mxf_gen_umid(s);

    for (i = 0; i < s->nb_streams; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        // update element count
        sc->track_essence_element_key[13] = present[sc->index];
        if (!memcmp(sc->track_essence_element_key, mxf_essence_container_uls[INDEX_DV].element_ul, 13)) // DV
            sc->order = (0x15 << 24) | AV_RB32(sc->track_essence_element_key+13);
        else
            sc->order = AV_RB32(sc->track_essence_element_key+12);
    }

    if (ff_parse_creation_time_metadata(s, &timestamp, 0) > 0)
        mxf->timestamp = mxf_parse_timestamp(timestamp);
    mxf->duration = -1;

    mxf->timecode_track = av_mallocz(sizeof(*mxf->timecode_track));
    if (!mxf->timecode_track)
        return AVERROR(ENOMEM);
    mxf->timecode_track->priv_data = av_mallocz(sizeof(MXFStreamContext));
    if (!mxf->timecode_track->priv_data)
        return AVERROR(ENOMEM);
    mxf->timecode_track->index = -1;

    return 0;
}

static const uint8_t system_metadata_pack_key[]        = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x03,0x01,0x04,0x01,0x01,0x00 };
static const uint8_t system_metadata_package_set_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x43,0x01,0x01,0x0D,0x01,0x03,0x01,0x04,0x01,0x02,0x01 };

static void mxf_write_system_item(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned frame;
    uint32_t time_code;
    int i, system_item_bitmap = 0x58; // UL, user date/time stamp, picture present

    frame = mxf->last_indexed_edit_unit + mxf->edit_units_count;

    // write system metadata pack
    avio_write(pb, system_metadata_pack_key, 16);
    klv_encode_ber4_length(pb, 57);

    for (i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            system_item_bitmap |= 0x4;
        else if (s->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_DATA)
            system_item_bitmap |= 0x2;
    }
    avio_w8(pb, system_item_bitmap);
    avio_w8(pb, mxf->content_package_rate); // content package rate
    avio_w8(pb, 0x00); // content package type
    avio_wb16(pb, 0x00); // channel handle
    avio_wb16(pb, frame & 0xFFFF); // continuity count, supposed to overflow
    if (mxf->essence_container_count > 1)
        avio_write(pb, multiple_desc_ul, 16);
    else {
        MXFStreamContext *sc = s->streams[0]->priv_data;
        avio_write(pb, *sc->container_ul, 16);
    }
    avio_w8(pb, 0);
    avio_wb64(pb, 0);
    avio_wb64(pb, 0); // creation date/time stamp

    avio_w8(pb, 0x81); // SMPTE 12M time code
    time_code = av_timecode_get_smpte_from_framenum(&mxf->tc, frame);
    avio_wb32(pb, time_code);
    avio_wb32(pb, 0); // binary group data
    avio_wb64(pb, 0);

    // write system metadata package set
    avio_write(pb, system_metadata_package_set_key, 16);
    klv_encode_ber4_length(pb, 35);
    avio_w8(pb, 0x83); // UMID
    avio_wb16(pb, 0x20);
    mxf_write_umid(s, 1);
}

static void mxf_write_d10_audio_packet(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int frame_size = pkt->size / st->codecpar->block_align;
    uint8_t *samples = pkt->data;
    uint8_t *end = pkt->data + pkt->size;
    int i;

    klv_encode_ber4_length(pb, 4 + frame_size*4*8);

    avio_w8(pb, (frame_size == 1920 ? 0 : (mxf->edit_units_count-1) % 5 + 1));
    avio_wl16(pb, frame_size);
    avio_w8(pb, (1<<st->codecpar->channels)-1);

    while (samples < end) {
        for (i = 0; i < st->codecpar->channels; i++) {
            uint32_t sample;
            if (st->codecpar->codec_id == AV_CODEC_ID_PCM_S24LE) {
                sample = AV_RL24(samples)<< 4;
                samples += 3;
            } else {
                sample = AV_RL16(samples)<<12;
                samples += 2;
            }
            avio_wl32(pb, sample | i);
        }
        for (; i < 8; i++)
            avio_wl32(pb, i);
    }
}

static int mxf_write_opatom_body_partition(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    MXFStreamContext *sc = st->priv_data;
    const uint8_t *key = NULL;

    int err;

    if (!mxf->header_written)
        key = body_partition_key;

    if ((err = mxf_write_partition(s, 1, 0, key, 0)) < 0)
        return err;
    mxf_write_klv_fill(s);
    avio_write(pb, sc->track_essence_element_key, 16);
    klv_encode_ber9_length(pb, mxf->body_offset);
    return 0;
}

static int mxf_write_opatom_packet(AVFormatContext *s, AVPacket *pkt, MXFIndexEntry *ie)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    int err;

    if (!mxf->header_written) {
        if ((err = mxf_write_partition(s, 0, 0, header_open_partition_key, 1)) < 0)
            return err;
        mxf_write_klv_fill(s);

        if ((err = mxf_write_opatom_body_partition(s)) < 0)
            return err;
        mxf->header_written = 1;
    }

    if (!mxf->edit_unit_byte_count) {
        mxf->index_entries[mxf->edit_units_count].offset = mxf->body_offset;
        mxf->index_entries[mxf->edit_units_count].flags = ie->flags;
        mxf->index_entries[mxf->edit_units_count].temporal_ref = ie->temporal_ref;
    }
    mxf->edit_units_count++;
    avio_write(pb, pkt->data, pkt->size);
    mxf->body_offset += pkt->size;

    return 0;
}

static void mxf_compute_edit_unit_byte_count(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i;

    if (s->oformat == &ff_mxf_opatom_muxer) {
        MXFStreamContext *sc = s->streams[0]->priv_data;
        mxf->edit_unit_byte_count = sc->frame_size;
        return;
    }

    mxf->edit_unit_byte_count = KAG_SIZE; // system element
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = st->priv_data;
        sc->slice_offset = mxf->edit_unit_byte_count;
        mxf->edit_unit_byte_count += 16 + 4 + sc->frame_size;
        mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
    }
}

static int mxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    MXFStreamContext *sc = st->priv_data;
    MXFIndexEntry ie = {0};
    int err;

    if (!mxf->cbr_index && !mxf->edit_unit_byte_count && !(mxf->edit_units_count % EDIT_UNITS_PER_BODY)) {
        if ((err = av_reallocp_array(&mxf->index_entries, mxf->edit_units_count
                                     + EDIT_UNITS_PER_BODY, sizeof(*mxf->index_entries))) < 0) {
            mxf->edit_units_count = 0;
            av_log(s, AV_LOG_ERROR, "could not allocate index entries\n");
            return err;
        }
    }

    if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        if (!mxf_parse_mpeg2_frame(s, st, pkt, &ie)) {
            av_log(s, AV_LOG_ERROR, "could not get mpeg2 profile and level\n");
            return -1;
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_DNXHD) {
        if (!mxf_parse_dnxhd_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get dnxhd profile\n");
            return -1;
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_PRORES) {
        if (!mxf_parse_prores_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get prores profile\n");
            return -1;
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_DVVIDEO) {
        if (!mxf_parse_dv_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get dv profile\n");
            return -1;
        }
    } else if (st->codecpar->codec_id == AV_CODEC_ID_H264) {
        if (!mxf_parse_h264_frame(s, st, pkt, &ie)) {
            av_log(s, AV_LOG_ERROR, "could not get h264 profile\n");
            return -1;
        }
    }

    if (mxf->cbr_index) {
        if (pkt->size != sc->frame_size && st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            av_log(s, AV_LOG_ERROR, "track %d: frame size does not match index unit size, %d != %d\n",
                   st->index, pkt->size, sc->frame_size);
            return -1;
        }
        if (!mxf->header_written)
            mxf_compute_edit_unit_byte_count(s);
    }

    if (s->oformat == &ff_mxf_opatom_muxer)
        return mxf_write_opatom_packet(s, pkt, &ie);

    if (!mxf->header_written) {
        if (mxf->edit_unit_byte_count) {
            if ((err = mxf_write_partition(s, 1, 2, header_open_partition_key, 1)) < 0)
                return err;
            mxf_write_klv_fill(s);
            mxf_write_index_table_segment(s);
        } else {
            if ((err = mxf_write_partition(s, 0, 0, header_open_partition_key, 1)) < 0)
                return err;
        }
        mxf->header_written = 1;
    }

    if (st->index == 0) {
        if (!mxf->edit_unit_byte_count &&
            (!mxf->edit_units_count || mxf->edit_units_count > EDIT_UNITS_PER_BODY) &&
            !(ie.flags & 0x33)) { // I-frame, GOP start
            mxf_write_klv_fill(s);
            if ((err = mxf_write_partition(s, 1, 2, body_partition_key, 0)) < 0)
                return err;
            mxf_write_klv_fill(s);
            mxf_write_index_table_segment(s);
        }

        mxf_write_klv_fill(s);
        mxf_write_system_item(s);

        if (!mxf->edit_unit_byte_count) {
            mxf->index_entries[mxf->edit_units_count].offset = mxf->body_offset;
            mxf->index_entries[mxf->edit_units_count].flags = ie.flags;
            mxf->index_entries[mxf->edit_units_count].temporal_ref = ie.temporal_ref;
            mxf->body_offset += KAG_SIZE; // size of system element
        }
        mxf->edit_units_count++;
    } else if (!mxf->edit_unit_byte_count && st->index == 1) {
        if (!mxf->edit_units_count) {
            av_log(s, AV_LOG_ERROR, "No packets in first stream\n");
            return AVERROR_PATCHWELCOME;
        }
        mxf->index_entries[mxf->edit_units_count-1].slice_offset =
            mxf->body_offset - mxf->index_entries[mxf->edit_units_count-1].offset;
    }

    mxf_write_klv_fill(s);
    avio_write(pb, sc->track_essence_element_key, 16); // write key
    if (s->oformat == &ff_mxf_d10_muxer &&
        st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        mxf_write_d10_audio_packet(s, st, pkt);
    } else {
        klv_encode_ber4_length(pb, pkt->size); // write length
        avio_write(pb, pkt->data, pkt->size);
        mxf->body_offset += 16+4+pkt->size + klv_fill_size(16+4+pkt->size);
    }

    return 0;
}

static void mxf_write_random_index_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t pos = avio_tell(pb);
    int i;

    avio_write(pb, random_index_pack_key, 16);
    klv_encode_ber_length(pb, 28 + 12LL*mxf->body_partitions_count);

    if (mxf->edit_unit_byte_count && s->oformat != &ff_mxf_opatom_muxer)
        avio_wb32(pb, 1); // BodySID of header partition
    else
        avio_wb32(pb, 0);
    avio_wb64(pb, 0); // offset of header partition

    for (i = 0; i < mxf->body_partitions_count; i++) {
        avio_wb32(pb, 1); // BodySID
        avio_wb64(pb, mxf->body_partition_offset[i]);
    }

    avio_wb32(pb, 0); // BodySID of footer partition
    avio_wb64(pb, mxf->footer_partition_offset);

    avio_wb32(pb, avio_tell(pb) - pos + 4);
}

static int mxf_write_footer(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, err;

    if (!mxf->header_written ||
        (s->oformat == &ff_mxf_opatom_muxer && !mxf->body_partition_offset)) {
        /* reason could be invalid options/not supported codec/out of memory */
        return AVERROR_UNKNOWN;
    }

    mxf->duration = mxf->last_indexed_edit_unit + mxf->edit_units_count;

    mxf_write_klv_fill(s);
    mxf->footer_partition_offset = avio_tell(pb);
    if (mxf->edit_unit_byte_count && s->oformat != &ff_mxf_opatom_muxer) { // no need to repeat index
        if ((err = mxf_write_partition(s, 0, 0, footer_partition_key, 0)) < 0)
            return err;
    } else {
        if ((err = mxf_write_partition(s, 0, 2, footer_partition_key, 0)) < 0)
            return err;
        mxf_write_klv_fill(s);
        mxf_write_index_table_segment(s);
    }

    mxf_write_klv_fill(s);
    mxf_write_random_index_pack(s);

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        if (s->oformat == &ff_mxf_opatom_muxer) {
            /* rewrite body partition to update lengths */
            avio_seek(pb, mxf->body_partition_offset[0], SEEK_SET);
            if ((err = mxf_write_opatom_body_partition(s)) < 0)
                return err;
        }

        avio_seek(pb, 0, SEEK_SET);
        if (mxf->edit_unit_byte_count && s->oformat != &ff_mxf_opatom_muxer) {
            if ((err = mxf_write_partition(s, 1, 2, header_closed_partition_key, 1)) < 0)
                return err;
            mxf_write_klv_fill(s);
            mxf_write_index_table_segment(s);
        } else {
            if ((err = mxf_write_partition(s, 0, 0, header_closed_partition_key, 1)) < 0)
                return err;
        }
        // update footer partition offset
        for (i = 0; i < mxf->body_partitions_count; i++) {
            avio_seek(pb, mxf->body_partition_offset[i]+44, SEEK_SET);
            avio_wb64(pb, mxf->footer_partition_offset);
        }
    }

    return 0;
}

static void mxf_deinit(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;

    av_freep(&mxf->index_entries);
    av_freep(&mxf->body_partition_offset);
    if (mxf->timecode_track) {
        av_freep(&mxf->timecode_track->priv_data);
        av_freep(&mxf->timecode_track);
    }
}

static int mxf_interleave_get_packet(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    int i, stream_count = 0;

    for (i = 0; i < s->nb_streams; i++)
        stream_count += !!s->streams[i]->internal->last_in_packet_buffer;

    if (stream_count && (s->nb_streams == stream_count || flush)) {
        AVPacketList *pktl = s->internal->packet_buffer;
        if (s->nb_streams != stream_count) {
            AVPacketList *last = NULL;
            // find last packet in edit unit
            while (pktl) {
                if (!stream_count || pktl->pkt.stream_index == 0)
                    break;
                // update last packet in packet buffer
                if (s->streams[pktl->pkt.stream_index]->internal->last_in_packet_buffer != pktl)
                    s->streams[pktl->pkt.stream_index]->internal->last_in_packet_buffer = pktl;
                last = pktl;
                pktl = pktl->next;
                stream_count--;
            }
            // purge packet queue
            while (pktl) {
                AVPacketList *next = pktl->next;
                av_packet_unref(&pktl->pkt);
                av_freep(&pktl);
                pktl = next;
            }
            if (last)
                last->next = NULL;
            else {
                s->internal->packet_buffer = NULL;
                s->internal->packet_buffer_end= NULL;
                goto out;
            }
            pktl = s->internal->packet_buffer;
        }

        *out = pktl->pkt;
        av_log(s, AV_LOG_TRACE, "out st:%d dts:%"PRId64"\n", (*out).stream_index, (*out).dts);
        s->internal->packet_buffer = pktl->next;
        if(s->streams[pktl->pkt.stream_index]->internal->last_in_packet_buffer == pktl)
            s->streams[pktl->pkt.stream_index]->internal->last_in_packet_buffer= NULL;
        if(!s->internal->packet_buffer)
            s->internal->packet_buffer_end= NULL;
        av_freep(&pktl);
        return 1;
    } else {
    out:
        return 0;
    }
}

static int mxf_compare_timestamps(AVFormatContext *s, const AVPacket *next,
                                                      const AVPacket *pkt)
{
    MXFStreamContext *sc  = s->streams[pkt ->stream_index]->priv_data;
    MXFStreamContext *sc2 = s->streams[next->stream_index]->priv_data;

    return next->dts > pkt->dts ||
        (next->dts == pkt->dts && sc->order < sc2->order);
}

static int mxf_interleave(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    int ret;
    if (pkt) {
        MXFStreamContext *sc = s->streams[pkt->stream_index]->priv_data;
        pkt->pts = pkt->dts = sc->pkt_cnt++;
        if ((ret = ff_interleave_add_packet(s, pkt, mxf_compare_timestamps)) < 0)
            return ret;
    }
    return mxf_interleave_get_packet(s, out, NULL, flush);
}

#define MXF_COMMON_OPTIONS \
    { "signal_standard", "Force/set Signal Standard",\
      offsetof(MXFContext, signal_standard), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "bt601", "ITU-R BT.601 and BT.656, also SMPTE 125M (525 and 625 line interlaced)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 1}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "bt1358", "ITU-R BT.1358 and ITU-R BT.799-3, also SMPTE 293M (525 and 625 line progressive)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 2}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "smpte347m", "SMPTE 347M (540 Mbps mappings)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 3}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "smpte274m", "SMPTE 274M (1125 line)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 4}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "smpte296m", "SMPTE 296M (750 line progressive)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 5}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "smpte349m", "SMPTE 349M (1485 Mbps mappings)",\
      0, AV_OPT_TYPE_CONST, {.i64 = 6}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},\
    { "smpte428", "SMPTE 428-1 DCDM",\
      0, AV_OPT_TYPE_CONST, {.i64 = 7}, -1, 7, AV_OPT_FLAG_ENCODING_PARAM, "signal_standard"},



static const AVOption mxf_options[] = {
    MXF_COMMON_OPTIONS
    { "store_user_comments", "",
      offsetof(MXFContext, store_user_comments), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mxf_muxer_class = {
    .class_name     = "MXF muxer",
    .item_name      = av_default_item_name,
    .option         = mxf_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static const AVOption d10_options[] = {
    { "d10_channelcount", "Force/set channelcount in generic sound essence descriptor",
      offsetof(MXFContext, channel_count), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 8, AV_OPT_FLAG_ENCODING_PARAM},
    MXF_COMMON_OPTIONS
    { "store_user_comments", "",
      offsetof(MXFContext, store_user_comments), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mxf_d10_muxer_class = {
    .class_name     = "MXF-D10 muxer",
    .item_name      = av_default_item_name,
    .option         = d10_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static const AVOption opatom_options[] = {
    { "mxf_audio_edit_rate", "Audio edit rate for timecode",
        offsetof(MXFContext, audio_edit_rate), AV_OPT_TYPE_RATIONAL, {.dbl=25}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    MXF_COMMON_OPTIONS
    { "store_user_comments", "",
      offsetof(MXFContext, store_user_comments), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mxf_opatom_muxer_class = {
    .class_name     = "MXF-OPAtom muxer",
    .item_name      = av_default_item_name,
    .option         = opatom_options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVOutputFormat ff_mxf_muxer = {
    .name              = "mxf",
    .long_name         = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format)"),
    .mime_type         = "application/mxf",
    .extensions        = "mxf",
    .priv_data_size    = sizeof(MXFContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mxf_write_header,
    .write_packet      = mxf_write_packet,
    .write_trailer     = mxf_write_footer,
    .deinit            = mxf_deinit,
    .flags             = AVFMT_NOTIMESTAMPS,
    .interleave_packet = mxf_interleave,
    .priv_class        = &mxf_muxer_class,
};

AVOutputFormat ff_mxf_d10_muxer = {
    .name              = "mxf_d10",
    .long_name         = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format) D-10 Mapping"),
    .mime_type         = "application/mxf",
    .priv_data_size    = sizeof(MXFContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_MPEG2VIDEO,
    .write_header      = mxf_write_header,
    .write_packet      = mxf_write_packet,
    .write_trailer     = mxf_write_footer,
    .deinit            = mxf_deinit,
    .flags             = AVFMT_NOTIMESTAMPS,
    .interleave_packet = mxf_interleave,
    .priv_class        = &mxf_d10_muxer_class,
};

AVOutputFormat ff_mxf_opatom_muxer = {
    .name              = "mxf_opatom",
    .long_name         = NULL_IF_CONFIG_SMALL("MXF (Material eXchange Format) Operational Pattern Atom"),
    .mime_type         = "application/mxf",
    .extensions        = "mxf",
    .priv_data_size    = sizeof(MXFContext),
    .audio_codec       = AV_CODEC_ID_PCM_S16LE,
    .video_codec       = AV_CODEC_ID_DNXHD,
    .write_header      = mxf_write_header,
    .write_packet      = mxf_write_packet,
    .write_trailer     = mxf_write_footer,
    .deinit            = mxf_deinit,
    .flags             = AVFMT_NOTIMESTAMPS,
    .interleave_packet = mxf_interleave,
    .priv_class        = &mxf_opatom_muxer_class,
};
