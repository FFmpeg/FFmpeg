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
 * References
 * SMPTE 336M KLV Data Encoding Protocol Using Key-Length-Value
 * SMPTE 377M MXF File Format Specifications
 * SMPTE 379M MXF Generic Container
 * SMPTE 381M Mapping MPEG Streams into the MXF Generic Container
 * SMPTE RP210: SMPTE Metadata Dictionary
 * SMPTE RP224: Registry of SMPTE Universal Labels
 */

//#define DEBUG

#include <math.h>
#include <time.h>

#include "libavutil/fifo.h"
#include "mxf.h"

static const int NTSC_samples_per_frame[] = { 1602, 1601, 1602, 1601, 1602, 0 };
static const int PAL_samples_per_frame[]  = { 1920, 0 };

#define MXF_INDEX_CLUSTER_SIZE 4096

typedef struct {
    AVFifoBuffer fifo;
    unsigned fifo_size;           ///< current fifo size allocated
    uint64_t dts;                 ///< current dts
    int sample_size;              ///< size of one sample all channels included
    const int *samples_per_frame; ///< must be 0 terminated
    const int *samples;           ///< current samples per frame, pointer to samples_per_frame
    AVRational time_base;         ///< time base of output audio packets
} AudioInterleaveContext;

typedef struct {
    int local_tag;
    UID uid;
} MXFLocalTagPair;

typedef struct {
    uint8_t flags;
    uint64_t offset;
    unsigned slice_offset[17]; // one video, 16 audio
} MXFIndexEntry;

typedef struct {
    AudioInterleaveContext aic;
    UID track_essence_element_key;
    int index;            ///< index in mxf_essence_container_uls table
    const UID *codec_ul;
    int64_t duration;
    int order;            ///< interleaving order if dts are equal
    int interlaced;       ///< wether picture is interlaced
    int temporal_reordering;
} MXFStreamContext;

typedef struct {
    UID container_ul;
    UID element_ul;
    UID codec_ul;
    void (*write_desc)();
} MXFContainerEssenceEntry;

static const struct {
    enum CodecID id;
    int index;
} mxf_essence_mappings[] = {
    { CODEC_ID_MPEG2VIDEO, 0 },
    { CODEC_ID_PCM_S24LE,  1 },
    { CODEC_ID_PCM_S16LE,  1 },
    { 0 }
};

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st);

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
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      NULL },
};

typedef struct MXFContext {
    int64_t footer_partition_offset;
    int essence_container_count;
    uint8_t essence_containers_indices[FF_ARRAY_ELEMS(mxf_essence_container_uls)];
    AVRational time_base;
    int header_written;
    MXFIndexEntry *index_entries;
    unsigned edit_units_count;
    int edit_unit_start;  ///< index of the stream starting edit unit
    uint64_t timestamp;   ///< timestamp, as year(16),month(8),day(8),hour(8),minutes(8),msec/4(8)
} MXFContext;

static const uint8_t uuid_base[]            = { 0xAD,0xAB,0x44,0x24,0x2f,0x25,0x4d,0xc7,0x92,0xff,0x29,0xbd };
static const uint8_t umid_base[]            = { 0x06,0x0A,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x01,0x0D,0x00,0x13,0x00,0x00,0x00 };

/**
 * complete key for operation pattern, partitions, and primer pack
 */
static const uint8_t op1a_ul[]                     = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x01,0x01,0x00 };
static const uint8_t footer_partition_key[]        = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }; // ClosedComplete
static const uint8_t primer_pack_key[]             = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x05,0x01,0x00 };
static const uint8_t index_table_segment_key[]     = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x10,0x01,0x00 };
static const uint8_t random_index_pack_key[]       = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x11,0x01,0x00 };
static const uint8_t header_open_partition_key[]   = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }; // OpenIncomplete
static const uint8_t header_closed_partition_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }; // ClosedComplete

/**
 * partial key for header metadata
 */
static const uint8_t header_metadata_key[]  = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01 };
static const uint8_t multiple_desc_ul[]     = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x03,0x0D,0x01,0x03,0x01,0x02,0x7F,0x01,0x00 };

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
    { 0x1902, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x05,0x02,0x00,0x00}}, /* Package strong reference batch */
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
    { 0x320C, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Frame Layout */
    { 0x320D, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x03,0x02,0x05,0x00,0x00,0x00}}, /* Video Line Map */
    { 0x3203, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x02,0x00,0x00,0x00}}, /* Stored Width */
    { 0x3202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x01,0x00,0x00,0x00}}, /* Stored Height */
    { 0x3209, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0C,0x00,0x00,0x00}}, /* Display Width */
    { 0x3208, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0B,0x00,0x00,0x00}}, /* Display Height */
    { 0x320E, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x01,0x01,0x01,0x00,0x00,0x00}}, /* Aspect Ratio */
    { 0x3201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x06,0x01,0x00,0x00,0x00,0x00}}, /* Picture Essence Coding */
    // Generic Sound Essence Descriptor
    { 0x3D02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Locked/Unlocked */
    { 0x3D03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x01,0x01,0x01,0x00,0x00}}, /* Audio sampling rate */
    { 0x3D07, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x01,0x01,0x04,0x00,0x00,0x00}}, /* ChannelCount */
    { 0x3D01, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x02,0x03,0x03,0x04,0x00,0x00,0x00}}, /* Quantization bits */
    { 0x3D06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x02,0x04,0x02,0x00,0x00,0x00,0x00}}, /* Sound Essence Compression */
    // Index Table Segment
    { 0x3F0B, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x05,0x30,0x04,0x06,0x00,0x00,0x00,0x00}}, /* Index Edit Rate */
    { 0x3F0C, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x07,0x02,0x01,0x03,0x01,0x0A,0x00,0x00}}, /* Index Start Position */
    { 0x3F0D, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x07,0x02,0x02,0x01,0x01,0x02,0x00,0x00}}, /* Index Duration */
    { 0x3F05, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x06,0x02,0x01,0x00,0x00,0x00,0x00,0x00}}, /* Edit Unit Byte Count */
    { 0x3F06, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x01,0x03,0x04,0x05,0x00,0x00,0x00,0x00}}, /* IndexSID */
    { 0x3F08, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x04,0x04,0x01,0x01,0x00,0x00,0x00}}, /* Slice Count */
    { 0x3F09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x04,0x04,0x01,0x06,0x00,0x00,0x00}}, /* Delta Entry Array */
    { 0x3F0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x04,0x04,0x02,0x05,0x00,0x00,0x00}}, /* Index Entry Array */
    // MPEG video Descriptor
    { 0x8000, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x0B,0x00,0x00}}, /* BitRate */
    // Wave Audio Essence Descriptor
    { 0x3D09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x03,0x05,0x00,0x00,0x00}}, /* Average Bytes Per Second */
    { 0x3D0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x02,0x01,0x00,0x00,0x00}}, /* Block Align */
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
        size--;
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
    for (i = 0; mxf_essence_mappings[i].id; i++)
        if (mxf_essence_mappings[i].id == id)
            return mxf_essence_mappings[i].index;
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

    // last modified date
    mxf_write_local_tag(pb, 8, 0x3B02);
    put_be64(pb, mxf->timestamp);

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
    MXFContext *mxf = s->priv_data;
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

    // modification date
    mxf_write_local_tag(pb, 8, 0x3C06);
    put_be64(pb, mxf->timestamp);
}

static void mxf_write_content_storage(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x011800);
    PRINT_KEY(s, "content storage key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 92);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, ContentStorage, 0);
    PRINT_KEY(s, "content storage uid", pb->buf_ptr - 16);

    // write package reference
    mxf_write_local_tag(pb, 16 * 2 + 8, 0x1901);
    mxf_write_refs_count(pb, 2);
    mxf_write_uuid(pb, MaterialPackage, 0);
    mxf_write_uuid(pb, SourcePackage, 0);

    // write essence container data
    mxf_write_local_tag(pb, 8 + 16, 0x1902);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, EssenceContainerData, 0);
}

static void mxf_write_track(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    MXFContext *mxf = s->priv_data;
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
    put_be32(pb, mxf->time_base.den);
    put_be32(pb, mxf->time_base.num);

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
    MXFContext *mxf = s->priv_data;
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
    put_be32(pb, mxf->time_base.den);
    put_be32(pb, mxf->time_base.num);

    // write essence container ul
    mxf_write_local_tag(pb, 16, 0x3004);
    put_buffer(pb, multiple_desc_ul, 16);

    // write sub descriptor refs
    mxf_write_local_tag(pb, s->nb_streams * 16 + 8, 0x3F01);
    mxf_write_refs_count(pb, s->nb_streams);
    for (i = 0; i < s->nb_streams; i++)
        mxf_write_uuid(pb, SubDescriptor, i);
}

static void mxf_write_generic_desc(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    ByteIOContext *pb = s->pb;

    put_buffer(pb, key, 16);
    klv_encode_ber_length(pb, size);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, SubDescriptor, st->index);

    mxf_write_local_tag(pb, 4, 0x3006);
    put_be32(pb, st->index);

    mxf_write_local_tag(pb, 8, 0x3001);
    put_be32(pb, mxf->time_base.den);
    put_be32(pb, mxf->time_base.num);

    mxf_write_local_tag(pb, 16, 0x3004);
    put_buffer(pb, mxf_essence_container_uls[sc->index].container_ul, 16);
}

static const UID mxf_mpegvideo_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 };
static const UID mxf_wav_descriptor_key       = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 };
static const UID mxf_aes3_descriptor_key      = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 };

static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st)
{
    MXFStreamContext *sc = st->priv_data;
    ByteIOContext *pb = s->pb;
    int stored_height = (st->codec->height+15)/16*16;
    AVRational dar;
    int f1, f2;

    mxf_write_generic_desc(s, st, mxf_mpegvideo_descriptor_key, 153+sc->interlaced*4);

    mxf_write_local_tag(pb, 4, 0x3203);
    put_be32(pb, st->codec->width);

    mxf_write_local_tag(pb, 4, 0x3202);
    put_be32(pb, stored_height>>sc->interlaced);

    mxf_write_local_tag(pb, 4, 0x3209);
    put_be32(pb, st->codec->width);

    mxf_write_local_tag(pb, 4, 0x3208);
    put_be32(pb, st->codec->height>>sc->interlaced);

    // bit rate
    mxf_write_local_tag(pb, 4, 0x8000);
    put_be32(pb, st->codec->bit_rate);

    // frame layout
    mxf_write_local_tag(pb, 1, 0x320C);
    put_byte(pb, sc->interlaced);

    // video line map
    switch (st->codec->height) {
    case  576: f1 = 23; f2 = 336; break;
    case  608: f1 =  7; f2 = 320; break;
    case  480: f1 = 20; f2 = 283; break;
    case  512: f1 =  7; f2 = 270; break;
    case  720: f1 = 26; f2 =   0; break; // progressive
    case 1080: f1 = 21; f2 = 584; break;
    default:   f1 =  0; f2 =   0; break;
    }

    if (!sc->interlaced) {
        f2  = 0;
        f1 *= 2;
    }

    mxf_write_local_tag(pb, 12+sc->interlaced*4, 0x320D);
    put_be32(pb, sc->interlaced ? 2 : 1);
    put_be32(pb, 4);
    put_be32(pb, f1);
    if (sc->interlaced)
        put_be32(pb, f2);

    av_reduce(&dar.num, &dar.den,
              st->codec->width*st->codec->sample_aspect_ratio.num,
              st->codec->height*st->codec->sample_aspect_ratio.den,
              1024*1024);

    mxf_write_local_tag(pb, 8, 0x320E);
    put_be32(pb, dar.num);
    put_be32(pb, dar.den);

    mxf_write_local_tag(pb, 16, 0x3201);
    put_buffer(pb, *sc->codec_ul, 16);
}

static void mxf_write_generic_sound_desc(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    ByteIOContext *pb = s->pb;

    mxf_write_generic_desc(s, st, key, size);

    // audio locked
    mxf_write_local_tag(pb, 1, 0x3D02);
    put_byte(pb, 1);

    // write audio sampling rate
    mxf_write_local_tag(pb, 8, 0x3D03);
    put_be32(pb, st->codec->sample_rate);
    put_be32(pb, 1);

    mxf_write_local_tag(pb, 4, 0x3D07);
    put_be32(pb, st->codec->channels);

    mxf_write_local_tag(pb, 4, 0x3D01);
    put_be32(pb, av_get_bits_per_sample(st->codec->codec_id));
}

static void mxf_write_wav_common_desc(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    ByteIOContext *pb = s->pb;

    mxf_write_generic_sound_desc(s, st, key, size);

    mxf_write_local_tag(pb, 2, 0x3D0A);
    put_be16(pb, st->codec->block_align);

    // avg bytes per sec
    mxf_write_local_tag(pb, 4, 0x3D09);
    put_be32(pb, st->codec->block_align*st->codec->sample_rate);
}

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_wav_common_desc(s, st, mxf_wav_descriptor_key, 107);
}

static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_wav_common_desc(s, st, mxf_aes3_descriptor_key, 107);
}

static void mxf_write_package(AVFormatContext *s, enum MXFMetadataSetType type)
{
    MXFContext *mxf = s->priv_data;
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

    // package creation date
    mxf_write_local_tag(pb, 8, 0x4405);
    put_be64(pb, mxf->timestamp);

    // package modified date
    mxf_write_local_tag(pb, 8, 0x4404);
    put_be64(pb, mxf->timestamp);

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

static int mxf_write_essence_container_data(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012300);
    klv_encode_ber_length(pb, 72);

    mxf_write_local_tag(pb, 16, 0x3C0A); // Instance UID
    mxf_write_uuid(pb, EssenceContainerData, 0);

    mxf_write_local_tag(pb, 32, 0x2701); // Linked Package UID
    mxf_write_umid(pb, SourcePackage, 0);

    mxf_write_local_tag(pb, 4, 0x3F07); // BodySID
    put_be32(pb, 1);

    mxf_write_local_tag(pb, 4, 0x3F06); // IndexSID
    put_be32(pb, 2);

    return 0;
}

static int mxf_write_header_metadata_sets(AVFormatContext *s)
{
    mxf_write_preface(s);
    mxf_write_identification(s);
    mxf_write_content_storage(s);
    mxf_write_package(s, MaterialPackage);
    mxf_write_package(s, SourcePackage);
    mxf_write_essence_container_data(s);
    return 0;
}

static int mxf_write_index_table_segment(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    int i, j, k, ret;
    int temporal_reordering = 0;
    int last_key_index = 0, key_index = 0;

    av_log(s, AV_LOG_DEBUG, "edit units count %d\n", mxf->edit_units_count);

    put_buffer(pb, index_table_segment_key, 16);
    ret = klv_encode_ber_length(pb, 109 + s->nb_streams*6 +
                                mxf->edit_units_count*(11+(s->nb_streams-1)*4));

    // instance id
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, IndexTableSegment, 0);

    // index edit rate
    mxf_write_local_tag(pb, 8, 0x3F0B);
    put_be32(pb, mxf->time_base.num);
    put_be32(pb, mxf->time_base.den);

    // index start position
    mxf_write_local_tag(pb, 8, 0x3F0C);
    put_be64(pb, 0);

    // index duration
    mxf_write_local_tag(pb, 8, 0x3F0D);
    put_be64(pb, mxf->edit_units_count);

    // edit unit byte count
    mxf_write_local_tag(pb, 4, 0x3F05);
    put_be32(pb, 0);

    // index sid
    mxf_write_local_tag(pb, 4, 0x3F06);
    put_be32(pb, 2);

    // body sid
    mxf_write_local_tag(pb, 4, 0x3F07);
    put_be32(pb, 1);

    // slice count - 1
    mxf_write_local_tag(pb, 1, 0x3F08);
    put_byte(pb, s->nb_streams-1);

    // delta entry array
    mxf_write_local_tag(pb, 8 + s->nb_streams*6, 0x3F09);
    put_be32(pb, s->nb_streams); // num of entries
    put_be32(pb, 6);             // size of one entry
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = st->priv_data;
        put_byte(pb, sc->temporal_reordering);
        if (sc->temporal_reordering)
            temporal_reordering = 1;
        put_byte(pb, i);
        put_be32(pb, 0); // element delta
    }

    mxf_write_local_tag(pb, 8 + mxf->edit_units_count*(11+(s->nb_streams-1)*4), 0x3F0A);
    put_be32(pb, mxf->edit_units_count);  // num of entries
    put_be32(pb, 11+(s->nb_streams-1)*4); // size of one entry
    for (i = 0; i < mxf->edit_units_count; i++) {
        if (temporal_reordering) {
            int temporal_offset = 0;
            for (j = i+1; j < mxf->edit_units_count; j++) {
                temporal_offset++;
                if (mxf->index_entries[j].flags & 0x10) { // backward prediction
                    // next is not b, so is reordered
                    if (!(mxf->index_entries[i+1].flags & 0x10)) {
                        if ((mxf->index_entries[i].flags & 0x11) == 0) // i frame
                            temporal_offset = 0;
                        else
                            temporal_offset = -temporal_offset;
                    }
                    break;
                }
            }
            put_byte(pb, temporal_offset);
        } else
            put_byte(pb, 0);
        if (!(mxf->index_entries[i].flags & 0x33)) { // I frame
            last_key_index = key_index;
            key_index = i;
        }
        if (mxf->index_entries[i].flags & 0x10 && // backward prediction
            !(mxf->index_entries[key_index].flags & 0x80)) { // open gop
            put_byte(pb, last_key_index - i);
        } else {
            put_byte(pb, key_index - i); // key frame offset
            if ((mxf->index_entries[i].flags & 0x20) == 0x20) // only forward
                last_key_index = key_index;
        }
        put_byte(pb, mxf->index_entries[i].flags);
        // stream offset
        put_be64(pb, mxf->index_entries[i].offset - mxf->index_entries[0].offset);
        for (k = 0; k < s->nb_streams; k++) {
            if (mxf->index_entries[i].slice_offset[k])
                put_be32(pb, mxf->index_entries[i].slice_offset[k]);
        }
    }

    return ret;
}

static void mxf_write_partition(AVFormatContext *s, int bodysid,
                                int indexsid, unsigned index_byte_count,
                                const uint8_t *key, int write_metadata)
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

    // indexTable
    put_be64(pb, index_byte_count); // indexByteCount
    put_be32(pb, indexsid); // indexSID
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

static int mxf_parse_mpeg2_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    uint32_t c = -1;
    int i;

    mxf->index_entries[mxf->edit_units_count].flags = 0;

    for(i = 0; i < pkt->size - 4; i++) {
        c = (c<<8) + pkt->data[i];
        if (c == 0x1B5) {
            if (i + 2 < pkt->size && (pkt->data[i+1] & 0xf0) == 0x10) { // seq ext
                st->codec->profile = pkt->data[i+1] & 0x07;
                st->codec->level   = pkt->data[i+2] >> 4;
            } else if (i + 5 < pkt->size && (pkt->data[i+1] & 0xf0) == 0x80) { // pict coding ext
                sc->interlaced = !(pkt->data[i+5] & 0x80); // progressive frame
                break;
            }
        } else if (c == 0x1b8) { // gop
            if (i + 4 < pkt->size && pkt->data[i+4]>>6 & 0x01) // closed
                mxf->index_entries[mxf->edit_units_count].flags |= 0x80; // random access
        } else if (c == 0x1b3) { // seq
            mxf->index_entries[mxf->edit_units_count].flags |= 0x40;
        } else if (c == 0x100) { // pic
            int pict_type = (pkt->data[i+2]>>3) & 0x07;
            if (pict_type == 2) { // P frame
                mxf->index_entries[mxf->edit_units_count].flags |= 0x22;
                st->codec->gop_size = 1;
            } else if (pict_type == 3) { // B frame
                mxf->index_entries[mxf->edit_units_count].flags |= 0x33;
                sc->temporal_reordering = -1;
            } else if (!pict_type) {
                av_log(s, AV_LOG_ERROR, "error parsing mpeg2 frame\n");
                return 0;
            }
        }
    }
    sc->codec_ul = mxf_get_mpeg2_codec_ul(st->codec);
    return !!sc->codec_ul;
}

static int ff_audio_interleave_init(AVFormatContext *s,
                                    const int *samples_per_frame,
                                    AVRational time_base)
{
    int i;

    if (!samples_per_frame)
        return -1;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AudioInterleaveContext *aic = st->priv_data;

        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            aic->sample_size = (st->codec->channels *
                                av_get_bits_per_sample(st->codec->codec_id)) / 8;
            if (!aic->sample_size) {
                av_log(s, AV_LOG_ERROR, "could not compute sample size\n");
                return -1;
            }
            aic->samples_per_frame = samples_per_frame;
            aic->samples = aic->samples_per_frame;
            aic->time_base = time_base;

            av_fifo_init(&aic->fifo, 100 * *aic->samples);
        }
    }

    return 0;
}

static void ff_audio_interleave_close(AVFormatContext *s)
{
    int i;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        AudioInterleaveContext *aic = st->priv_data;

        if (st->codec->codec_type == CODEC_TYPE_AUDIO)
            av_fifo_free(&aic->fifo);
    }
}

static uint64_t mxf_parse_timestamp(time_t timestamp)
{
    struct tm *time = localtime(&timestamp);
    return (uint64_t)(time->tm_year+1900) << 48 |
           (uint64_t)(time->tm_mon+1)     << 40 |
           (uint64_t) time->tm_mday       << 32 |
                      time->tm_hour       << 24 |
                      time->tm_min        << 16 |
                      time->tm_sec        << 8;
}

static int mxf_write_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i;
    uint8_t present[FF_ARRAY_ELEMS(mxf_essence_container_uls)] = {0};
    const int *samples_per_frame = NULL;

    if (s->nb_streams > 17) {
        av_log(s, AV_LOG_ERROR, "error, mxf muxer supports 17 tracks maximum\n");
        return -1;
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;

        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            if (fabs(av_q2d(st->codec->time_base) - 1/25.0) < 0.0001) {
                samples_per_frame = PAL_samples_per_frame;
                mxf->time_base = (AVRational){ 1, 25 };
            } else if (fabs(av_q2d(st->codec->time_base) - 1001/30000.0) < 0.0001) {
                samples_per_frame = NTSC_samples_per_frame;
                mxf->time_base = (AVRational){ 1001, 30000 };
            } else {
                av_log(s, AV_LOG_ERROR, "unsupported video frame rate\n");
                return -1;
            }
            mxf->edit_unit_start = st->index;
            av_set_pts_info(st, 64, mxf->time_base.num, mxf->time_base.den);
        } else if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            if (st->codec->sample_rate != 48000) {
                av_log(s, AV_LOG_ERROR, "only 48khz is implemented\n");
                return -1;
            }
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
        }
        sc->duration = -1;

        sc->index = mxf_get_essence_container_ul_index(st->codec->codec_id);
        if (sc->index == -1) {
            av_log(s, AV_LOG_ERROR, "track %d: could not find essence container ul, "
                   "codec not currently supported in container\n", i);
            return -1;
        }

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

    for (i = 0; i < s->nb_streams; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        // update element count
        sc->track_essence_element_key[13] = present[sc->index];
        sc->order = AV_RB32(sc->track_essence_element_key+12);
    }

    mxf->timestamp = mxf_parse_timestamp(s->timestamp);

    if (!samples_per_frame)
        samples_per_frame = PAL_samples_per_frame;

    if (ff_audio_interleave_init(s, samples_per_frame, mxf->time_base) < 0)
        return -1;

    return 0;
}

static int mxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    MXFStreamContext *sc = st->priv_data;

    if (!(mxf->edit_units_count % MXF_INDEX_CLUSTER_SIZE)) {
        mxf->index_entries = av_realloc(mxf->index_entries,
            (mxf->edit_units_count + MXF_INDEX_CLUSTER_SIZE)*
             sizeof(*mxf->index_entries));
        if (!mxf->index_entries) {
            av_log(s, AV_LOG_ERROR, "could not allocate index entries\n");
            return -1;
        }
    }

    if (st->codec->codec_id == CODEC_ID_MPEG2VIDEO) {
        if (!mxf_parse_mpeg2_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get mpeg2 profile and level\n");
            return -1;
        }
    }

    if (!mxf->header_written) {
        mxf_write_partition(s, 1, 0, 0, header_open_partition_key, 1);
        mxf->header_written = 1;
    }

    if (st->index == mxf->edit_unit_start) {
        mxf->index_entries[mxf->edit_units_count].offset = url_ftell(pb);
        mxf->index_entries[mxf->edit_units_count].slice_offset[st->index] = 0;
        mxf->edit_units_count++;
    } else {
        mxf->index_entries[mxf->edit_units_count-1].slice_offset[st->index] =
            url_ftell(pb) - mxf->index_entries[mxf->edit_units_count-1].offset;
    }

    put_buffer(pb, sc->track_essence_element_key, 16); // write key
    klv_encode_ber_length(pb, pkt->size); // write length
    put_buffer(pb, pkt->data, pkt->size); // write value

    sc->duration = FFMAX(pkt->pts + pkt->duration, sc->duration);

    put_flush_packet(pb);
    return 0;
}

static void mxf_write_random_index_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    uint64_t pos = url_ftell(pb);

    put_buffer(pb, random_index_pack_key, 16);
    klv_encode_ber_length(pb, 28);

    put_be32(pb, 1); // BodySID of header partition
    put_be64(pb, 0); // offset of header partition

    put_be32(pb, 0); // BodySID of footer partition
    put_be64(pb, mxf->footer_partition_offset);

    put_be32(pb, url_ftell(pb) - pos + 4);
}

static int mxf_write_footer(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned index_byte_count =
        109 + s->nb_streams*6 +
        mxf->edit_units_count*(11+(s->nb_streams-1)*4);

    // add encoded ber length
    index_byte_count += 16 +
        (index_byte_count < 128 ? 1 : (av_log2(index_byte_count) >> 3) + 2);

    mxf->footer_partition_offset = url_ftell(pb);
    mxf_write_partition(s, 0, 2, index_byte_count, footer_partition_key, 0);

    mxf_write_index_table_segment(s);

    mxf_write_random_index_pack(s);

    if (!url_is_streamed(s->pb)) {
        url_fseek(pb, 0, SEEK_SET);
        mxf_write_partition(s, 1, 0, 0, header_closed_partition_key, 1);
    }

    ff_audio_interleave_close(s);

    mxf_free(s);
    return 0;
}

static int mxf_interleave_new_audio_packet(AVFormatContext *s, AVPacket *pkt,
                                           int stream_index, int flush)
{
    AVStream *st = s->streams[stream_index];
    AudioInterleaveContext *aic = st->priv_data;

    int size = FFMIN(av_fifo_size(&aic->fifo), *aic->samples * aic->sample_size);
    if (!size || (!flush && size == av_fifo_size(&aic->fifo)))
        return 0;

    av_new_packet(pkt, size);
    av_fifo_read(&aic->fifo, pkt->data, size);

    pkt->dts = pkt->pts = aic->dts;
    pkt->duration = av_rescale_q(*aic->samples, st->time_base, aic->time_base);
    pkt->stream_index = stream_index;
    aic->dts += pkt->duration;

    aic->samples++;
    if (!*aic->samples)
        aic->samples = aic->samples_per_frame;

    return size;
}

static int mxf_interleave_get_packet(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    AVPacketList *pktl;
    int stream_count = 0;
    int streams[MAX_STREAMS];

    memset(streams, 0, sizeof(streams));
    pktl = s->packet_buffer;
    while (pktl) {
        //av_log(s, AV_LOG_DEBUG, "show st:%d dts:%lld\n", pktl->pkt.stream_index, pktl->pkt.dts);
        if (!streams[pktl->pkt.stream_index])
            stream_count++;
        streams[pktl->pkt.stream_index]++;
        pktl = pktl->next;
    }

    if (stream_count && (s->nb_streams == stream_count || flush)) {
        pktl = s->packet_buffer;
        if (s->nb_streams != stream_count) {
            MXFContext *mxf = s->priv_data;
            AVPacketList *first = NULL;
            // find first packet in edit unit
            while (pktl) {
                AVStream *st = s->streams[pktl->pkt.stream_index];
                if (st->index == mxf->edit_unit_start)
                    break;
                else if (!first)
                    first = pktl;
                pktl = pktl->next;
            }
            // purge packet queue
            while (pktl) {
                AVPacketList *next = pktl->next;
                av_free_packet(&pktl->pkt);
                av_freep(&pktl);
                pktl = next;
            }
            if (!first)
                goto out;
            pktl = first;
        }

        *out = pktl->pkt;
        //av_log(s, AV_LOG_DEBUG, "out st:%d dts:%lld\n", (*out).stream_index, (*out).dts);
        s->packet_buffer = pktl->next;
        av_freep(&pktl);
        return 1;
    } else {
    out:
        av_init_packet(out);
        return 0;
    }
}

static int mxf_compare_timestamps(AVFormatContext *s, AVPacket *next, AVPacket *pkt)
{
    MXFStreamContext *sc  = s->streams[pkt ->stream_index]->priv_data;
    MXFStreamContext *sc2 = s->streams[next->stream_index]->priv_data;

    return next->dts > pkt->dts ||
        (next->dts == pkt->dts && sc->order < sc2->order);
}

static int mxf_interleave(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    int i;

    if (pkt) {
        AVStream *st = s->streams[pkt->stream_index];
        AudioInterleaveContext *aic = st->priv_data;
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            av_fifo_generic_write(&aic->fifo, pkt->data, pkt->size, NULL);
        } else {
            // rewrite pts and dts to be decoded time line position
            pkt->pts = pkt->dts = aic->dts;
            aic->dts += pkt->duration;
            ff_interleave_add_packet(s, pkt, mxf_compare_timestamps);
        }
        pkt = NULL;
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
            AVPacket new_pkt;
            while (mxf_interleave_new_audio_packet(s, &new_pkt, i, flush))
                ff_interleave_add_packet(s, &new_pkt, mxf_compare_timestamps);
        }
    }

    return mxf_interleave_get_packet(s, out, pkt, flush);
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
    AVFMT_NOTIMESTAMPS,
    NULL,
    mxf_interleave,
};
