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

#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/timecode.h"
#include "libavutil/avassert.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/dnxhddata.h"
#include "audiointerleave.h"
#include "avformat.h"
#include "internal.h"
#include "mxf.h"
#include "config.h"

extern AVOutputFormat ff_mxf_d10_muxer;

#define EDIT_UNITS_PER_BODY 250
#define KAG_SIZE 512

typedef struct {
    int local_tag;
    UID uid;
} MXFLocalTagPair;

typedef struct {
    uint8_t flags;
    uint64_t offset;
    unsigned slice_offset; ///< offset of audio slice
    uint16_t temporal_ref;
} MXFIndexEntry;

typedef struct {
    AudioInterleaveContext aic;
    UID track_essence_element_key;
    int index;               ///< index in mxf_essence_container_uls table
    const UID *codec_ul;
    int order;               ///< interleaving order if dts are equal
    int interlaced;          ///< whether picture is interlaced
    int field_dominance;     ///< tff=1, bff=2
    int component_depth;
    int temporal_reordering;
    AVRational aspect_ratio; ///< display aspect ratio
    int closed_gop;          ///< gop is closed, used in mpeg-2 frame parsing
} MXFStreamContext;

typedef struct {
    UID container_ul;
    UID element_ul;
    UID codec_ul;
    void (*write_desc)(AVFormatContext *, AVStream *);
} MXFContainerEssenceEntry;

static const struct {
    enum AVCodecID id;
    int index;
} mxf_essence_mappings[] = {
    { AV_CODEC_ID_MPEG2VIDEO, 0 },
    { AV_CODEC_ID_PCM_S24LE,  1 },
    { AV_CODEC_ID_PCM_S16LE,  1 },
    { AV_CODEC_ID_DVVIDEO,   15 },
    { AV_CODEC_ID_DNXHD,     24 },
    { AV_CODEC_ID_NONE }
};

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_cdci_desc(AVFormatContext *s, AVStream *st);
static void mxf_write_generic_sound_desc(AVFormatContext *s, AVStream *st);

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
    // D-10 625/50 PAL 50mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x01 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x01,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // D-10 525/60 NTSC 50mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x02,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x02 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x02,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // D-10 625/50 PAL 40mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x03,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x03 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x03,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // D-10 525/60 NTSC 40mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x04,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x04 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x04,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // D-10 625/50 PAL 30mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x05,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x05 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x05,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // D-10 525/60 NTSC 30mb/s
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x06,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x05,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x01,0x02,0x01,0x06 },
      mxf_write_cdci_desc },
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x01,0x06,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x06,0x01,0x10,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x02,0x02,0x01,0x00,0x00,0x00,0x00 },
      mxf_write_generic_sound_desc },
    // DV Unknwon
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x7F,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x00,0x00,0x00 },
      mxf_write_cdci_desc },
    // DV25 525/60
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x40,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x01,0x00 },
      mxf_write_cdci_desc },
    // DV25 625/50
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x41,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x02,0x00 },
      mxf_write_cdci_desc },
    // DV50 525/60
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x50,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x03,0x00 },
      mxf_write_cdci_desc },
    // DV50 625/50
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x51,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x04,0x00 },
      mxf_write_cdci_desc },
    // DV100 1080/60
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x60,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x05,0x00 },
      mxf_write_cdci_desc },
    // DV100 1080/50
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x61,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x06,0x00 },
      mxf_write_cdci_desc },
    // DV100 720/60
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x62,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x07,0x00 },
      mxf_write_cdci_desc },
    // DV100 720/50
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x02,0x63,0x01 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x18,0x01,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x04,0x01,0x02,0x02,0x02,0x02,0x08,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080p 10bit high
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x01,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080p 8bit medium
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x03,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080p 8bit high
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x04,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080i 10bit high
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x07,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080i 8bit medium
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x08,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 1080i 8bit high
    { { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x01,0x02,0x01,0x01,0x0D,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x09,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 720p 10bit
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x10,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 720p 8bit high
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x11,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 720p 8bit medium
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x12,0x00,0x00 },
      mxf_write_cdci_desc },
    // DNxHD 720p 8bit low
    { { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x01,0x0d,0x01,0x03,0x01,0x02,0x11,0x01,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x01,0x02,0x01,0x01,0x0d,0x01,0x03,0x01,0x15,0x01,0x05,0x00 },
      { 0x06,0x0e,0x2b,0x34,0x04,0x01,0x01,0x0A,0x04,0x01,0x02,0x02,0x71,0x13,0x00,0x00 },
      mxf_write_cdci_desc },
    { { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
      NULL },
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
    uint64_t body_offset;
    uint32_t instance_number;
    uint8_t umid[16];        ///< unique material identifier
} MXFContext;

static const uint8_t uuid_base[]            = { 0xAD,0xAB,0x44,0x24,0x2f,0x25,0x4d,0xc7,0x92,0xff,0x29,0xbd };
static const uint8_t umid_ul[]              = { 0x06,0x0A,0x2B,0x34,0x01,0x01,0x01,0x05,0x01,0x01,0x0D,0x00,0x13 };

/**
 * complete key for operation pattern, partitions, and primer pack
 */
static const uint8_t op1a_ul[]                     = { 0x06,0x0E,0x2B,0x34,0x04,0x01,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x01,0x09,0x00 };
static const uint8_t footer_partition_key[]        = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x04,0x04,0x00 }; // ClosedComplete
static const uint8_t primer_pack_key[]             = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x05,0x01,0x00 };
static const uint8_t index_table_segment_key[]     = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x02,0x01,0x01,0x10,0x01,0x00 };
static const uint8_t random_index_pack_key[]       = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x11,0x01,0x00 };
static const uint8_t header_open_partition_key[]   = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x01,0x00 }; // OpenIncomplete
static const uint8_t header_closed_partition_key[] = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x02,0x04,0x00 }; // ClosedComplete
static const uint8_t klv_fill_key[]                = { 0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x03,0x01,0x02,0x10,0x01,0x00,0x00,0x00 };
static const uint8_t body_partition_key[]          = { 0x06,0x0E,0x2B,0x34,0x02,0x05,0x01,0x01,0x0D,0x01,0x02,0x01,0x01,0x03,0x04,0x00 }; // ClosedComplete

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
    { 0x3004, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x06,0x01,0x01,0x04,0x01,0x02,0x00,0x00}}, /* Essence Container */
    // Generic Picture Essence Descriptor
    { 0x320C, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Frame Layout */
    { 0x320D, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x03,0x02,0x05,0x00,0x00,0x00}}, /* Video Line Map */
    { 0x3203, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x02,0x00,0x00,0x00}}, /* Stored Width */
    { 0x3202, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x02,0x01,0x00,0x00,0x00}}, /* Stored Height */
    { 0x3209, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0C,0x00,0x00,0x00}}, /* Display Width */
    { 0x3208, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x0B,0x00,0x00,0x00}}, /* Display Height */
    { 0x320E, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x01,0x01,0x01,0x00,0x00,0x00}}, /* Aspect Ratio */
    { 0x3201, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x06,0x01,0x00,0x00,0x00,0x00}}, /* Picture Essence Coding */
    { 0x3212, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x03,0x01,0x06,0x00,0x00,0x00}}, /* Field Dominance (Opt) */
    // CDCI Picture Essence Descriptor
    { 0x3301, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x02,0x04,0x01,0x05,0x03,0x0A,0x00,0x00,0x00}}, /* Component Depth */
    { 0x3302, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x01,0x04,0x01,0x05,0x01,0x05,0x00,0x00,0x00}}, /* Horizontal Subsampling */
    // Generic Sound Essence Descriptor
    { 0x3D02, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x04,0x04,0x02,0x03,0x01,0x04,0x00,0x00,0x00}}, /* Locked/Unlocked */
    { 0x3D03, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x01,0x01,0x01,0x00,0x00}}, /* Audio sampling rate */
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
    { 0x8007, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x01,0x06,0x02,0x01,0x0A,0x00,0x00}}, /* ProfileAndLevel */
    // Wave Audio Essence Descriptor
    { 0x3D09, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x03,0x05,0x00,0x00,0x00}}, /* Average Bytes Per Second */
    { 0x3D0A, {0x06,0x0E,0x2B,0x34,0x01,0x01,0x01,0x05,0x04,0x02,0x03,0x02,0x01,0x00,0x00,0x00}}, /* Block Align */
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
    int size;
    if (len < 128) {
        //short form
        avio_w8(pb, len);
        return 1;
    }

    size = (av_log2(len) >> 3) + 1;

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

static void mxf_write_primer_pack(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int local_tag_number, i = 0;

    local_tag_number = FF_ARRAY_ELEMS(mxf_local_tag_batch);

    avio_write(pb, primer_pack_key, 16);
    klv_encode_ber_length(pb, local_tag_number * 18 + 8);

    avio_wb32(pb, local_tag_number); // local_tag num
    avio_wb32(pb, 18); // item size, always 18 according to the specs

    for (i = 0; i < local_tag_number; i++) {
        avio_wb16(pb, mxf_local_tag_batch[i].local_tag);
        avio_write(pb, mxf_local_tag_batch[i].uid, 16);
    }
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

static void mxf_free(AVFormatContext *s)
{
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        av_freep(&st->priv_data);
    }
}

static const MXFCodecUL *mxf_get_data_definition_ul(int type)
{
    const MXFCodecUL *uls = ff_mxf_data_definition_uls;
    while (uls->uid[0]) {
        if (type == uls->id)
            break;
        uls++;
    }
    return uls;
}

static void mxf_write_essence_container_refs(AVFormatContext *s)
{
    MXFContext *c = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    mxf_write_refs_count(pb, c->essence_container_count);
    av_log(s,AV_LOG_DEBUG, "essence container count:%d\n", c->essence_container_count);
    for (i = 0; i < c->essence_container_count; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        avio_write(pb, mxf_essence_container_uls[sc->index].container_ul, 16);
    }
}

static void mxf_write_preface(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x012f00);
    PRINT_KEY(s, "preface key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 130 + 16 * mxf->essence_container_count);

    // write preface set uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, Preface, 0);
    PRINT_KEY(s, "preface uid", pb->buf_ptr - 16);

    // last modified date
    mxf_write_local_tag(pb, 8, 0x3B02);
    avio_wb64(pb, mxf->timestamp);

    // write version
    mxf_write_local_tag(pb, 2, 0x3B05);
    avio_wb16(pb, 258); // v1.2

    // write identification_refs
    mxf_write_local_tag(pb, 16 + 8, 0x3B06);
    mxf_write_refs_count(pb, 1);
    mxf_write_uuid(pb, Identification, 0);

    // write content_storage_refs
    mxf_write_local_tag(pb, 16, 0x3B03);
    mxf_write_uuid(pb, ContentStorage, 0);

    // operational pattern
    mxf_write_local_tag(pb, 16, 0x3B09);
    avio_write(pb, op1a_ul, 16);

    // write essence_container_refs
    mxf_write_local_tag(pb, 8 + 16 * mxf->essence_container_count, 0x3B0A);
    mxf_write_essence_container_refs(s);

    // write dm_scheme_refs
    mxf_write_local_tag(pb, 8, 0x3B0B);
    avio_wb64(pb, 0);
}

/*
 * Write a local tag containing an ascii string as utf-16
 */
static void mxf_write_local_tag_utf16(AVIOContext *pb, int tag, const char *value)
{
    int i, size = strlen(value);
    mxf_write_local_tag(pb, size*2, tag);
    for (i = 0; i < size; i++)
        avio_wb16(pb, value[i]);
}

static void mxf_write_identification(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
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
    avio_wb64(pb, mxf->timestamp);
}

static void mxf_write_content_storage(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;

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
    AVIOContext *pb = s->pb;
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
    avio_wb32(pb, st->index+2);

    // write track number
    mxf_write_local_tag(pb, 4, 0x4804);
    if (type == MaterialPackage)
        avio_wb32(pb, 0); // track number of material package is 0
    else
        avio_write(pb, sc->track_essence_element_key + 12, 4);

    mxf_write_local_tag(pb, 8, 0x4B01);
    avio_wb32(pb, mxf->time_base.den);
    avio_wb32(pb, mxf->time_base.num);

    // write origin
    mxf_write_local_tag(pb, 8, 0x4B02);
    avio_wb64(pb, 0);

    // write sequence refs
    mxf_write_local_tag(pb, 16, 0x4803);
    mxf_write_uuid(pb, type == MaterialPackage ? Sequence: Sequence + TypeBottom, st->index);
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
        const MXFCodecUL *data_def_ul = mxf_get_data_definition_ul(st->codec->codec_type);
        avio_write(pb, data_def_ul->uid, 16);
    }

    // write duration
    mxf_write_local_tag(pb, 8, 0x0202);
    avio_wb64(pb, mxf->duration);
}

static void mxf_write_sequence(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    enum MXFMetadataSetType component;

    mxf_write_metadata_key(pb, 0x010f00);
    PRINT_KEY(s, "sequence key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 80);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? Sequence: Sequence + TypeBottom, st->index);

    PRINT_KEY(s, "sequence uid", pb->buf_ptr - 16);
    mxf_write_common_fields(s, st);

    // write structural component
    mxf_write_local_tag(pb, 16 + 8, 0x1001);
    mxf_write_refs_count(pb, 1);
    if (st == mxf->timecode_track)
        component = TimecodeComponent;
    else
        component = SourceClip;
    if (type == SourcePackage)
        component += TypeBottom;
    mxf_write_uuid(pb, component, st->index);
}

static void mxf_write_timecode_component(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;

    mxf_write_metadata_key(pb, 0x011400);
    klv_encode_ber_length(pb, 75);

    // UID
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? TimecodeComponent :
                   TimecodeComponent + TypeBottom, st->index);

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

static void mxf_write_structural_component(AVFormatContext *s, AVStream *st, enum MXFMetadataSetType type)
{
    AVIOContext *pb = s->pb;
    int i;

    mxf_write_metadata_key(pb, 0x011100);
    PRINT_KEY(s, "sturctural component key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 108);

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type == MaterialPackage ? SourceClip: SourceClip + TypeBottom, st->index);

    PRINT_KEY(s, "structural component uid", pb->buf_ptr - 16);
    mxf_write_common_fields(s, st);

    // write start_position
    mxf_write_local_tag(pb, 8, 0x1201);
    avio_wb64(pb, 0);

    // write source package uid, end of the reference
    mxf_write_local_tag(pb, 32, 0x1101);
    if (type == SourcePackage) {
        for (i = 0; i < 4; i++)
            avio_wb64(pb, 0);
    } else
        mxf_write_umid(s, 1);

    // write source track id
    mxf_write_local_tag(pb, 4, 0x1102);
    if (type == SourcePackage)
        avio_wb32(pb, 0);
    else
        avio_wb32(pb, st->index+2);
}

static void mxf_write_multi_descriptor(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    const uint8_t *ul;
    int i;

    mxf_write_metadata_key(pb, 0x014400);
    PRINT_KEY(s, "multiple descriptor key", pb->buf_ptr - 16);
    klv_encode_ber_length(pb, 64 + 16 * s->nb_streams);

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
        ul = mxf_essence_container_uls[sc->index].container_ul;
    }
    avio_write(pb, ul, 16);

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
    AVIOContext *pb = s->pb;

    avio_write(pb, key, 16);
    klv_encode_ber4_length(pb, size+20+8+12+20);

    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, SubDescriptor, st->index);

    mxf_write_local_tag(pb, 4, 0x3006);
    avio_wb32(pb, st->index+2);

    mxf_write_local_tag(pb, 8, 0x3001);
    avio_wb32(pb, mxf->time_base.den);
    avio_wb32(pb, mxf->time_base.num);

    mxf_write_local_tag(pb, 16, 0x3004);
    avio_write(pb, mxf_essence_container_uls[sc->index].container_ul, 16);
}

static const UID mxf_mpegvideo_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x51,0x00 };
static const UID mxf_wav_descriptor_key       = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x48,0x00 };
static const UID mxf_aes3_descriptor_key      = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0d,0x01,0x01,0x01,0x01,0x01,0x47,0x00 };
static const UID mxf_cdci_descriptor_key      = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x28,0x00 };
static const UID mxf_generic_sound_descriptor_key = { 0x06,0x0E,0x2B,0x34,0x02,0x53,0x01,0x01,0x0D,0x01,0x01,0x01,0x01,0x01,0x42,0x00 };

static void mxf_write_cdci_common(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    MXFStreamContext *sc = st->priv_data;
    AVIOContext *pb = s->pb;
    int stored_height = (st->codec->height+15)/16*16;
    int display_height;
    int f1, f2;
    unsigned desc_size = size+8+8+8+8+8+8+5+16+sc->interlaced*4+12+20;
    if (sc->interlaced && sc->field_dominance)
        desc_size += 5;

    mxf_write_generic_desc(s, st, key, desc_size);

    mxf_write_local_tag(pb, 4, 0x3203);
    avio_wb32(pb, st->codec->width);

    mxf_write_local_tag(pb, 4, 0x3202);
    avio_wb32(pb, stored_height>>sc->interlaced);

    mxf_write_local_tag(pb, 4, 0x3209);
    avio_wb32(pb, st->codec->width);

    if (st->codec->height == 608) // PAL + VBI
        display_height = 576;
    else if (st->codec->height == 512)  // NTSC + VBI
        display_height = 486;
    else
        display_height = st->codec->height;

    mxf_write_local_tag(pb, 4, 0x3208);
    avio_wb32(pb, display_height>>sc->interlaced);

    // component depth
    mxf_write_local_tag(pb, 4, 0x3301);
    avio_wb32(pb, sc->component_depth);

    // horizontal subsampling
    mxf_write_local_tag(pb, 4, 0x3302);
    avio_wb32(pb, 2);

    // frame layout
    mxf_write_local_tag(pb, 1, 0x320C);
    avio_w8(pb, sc->interlaced);

    // video line map
    switch (st->codec->height) {
    case  576: f1 = 23; f2 = st->codec->codec_id == AV_CODEC_ID_DVVIDEO ? 335 : 336; break;
    case  608: f1 =  7; f2 = 320; break;
    case  480: f1 = 20; f2 = st->codec->codec_id == AV_CODEC_ID_DVVIDEO ? 285 : 283; break;
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
    avio_wb32(pb, sc->interlaced ? 2 : 1);
    avio_wb32(pb, 4);
    avio_wb32(pb, f1);
    if (sc->interlaced)
        avio_wb32(pb, f2);

    mxf_write_local_tag(pb, 8, 0x320E);
    avio_wb32(pb, sc->aspect_ratio.num);
    avio_wb32(pb, sc->aspect_ratio.den);

    mxf_write_local_tag(pb, 16, 0x3201);
    avio_write(pb, *sc->codec_ul, 16);

    if (sc->interlaced && sc->field_dominance) {
        mxf_write_local_tag(pb, 1, 0x3212);
        avio_w8(pb, sc->field_dominance);
    }

}

static void mxf_write_cdci_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_cdci_common(s, st, mxf_cdci_descriptor_key, 0);
}

static void mxf_write_mpegvideo_desc(AVFormatContext *s, AVStream *st)
{
    AVIOContext *pb = s->pb;
    int profile_and_level = (st->codec->profile<<4) | st->codec->level;

    mxf_write_cdci_common(s, st, mxf_mpegvideo_descriptor_key, 8+5);

    // bit rate
    mxf_write_local_tag(pb, 4, 0x8000);
    avio_wb32(pb, st->codec->bit_rate);

    // profile and level
    mxf_write_local_tag(pb, 1, 0x8007);
    if (!st->codec->profile)
        profile_and_level |= 0x80; // escape bit
    avio_w8(pb, profile_and_level);
}

static void mxf_write_generic_sound_common(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    AVIOContext *pb = s->pb;

    mxf_write_generic_desc(s, st, key, size+5+12+8+8);

    // audio locked
    mxf_write_local_tag(pb, 1, 0x3D02);
    avio_w8(pb, 1);

    // write audio sampling rate
    mxf_write_local_tag(pb, 8, 0x3D03);
    avio_wb32(pb, st->codec->sample_rate);
    avio_wb32(pb, 1);

    mxf_write_local_tag(pb, 4, 0x3D07);
    avio_wb32(pb, st->codec->channels);

    mxf_write_local_tag(pb, 4, 0x3D01);
    avio_wb32(pb, av_get_bits_per_sample(st->codec->codec_id));
}

static void mxf_write_wav_common(AVFormatContext *s, AVStream *st, const UID key, unsigned size)
{
    AVIOContext *pb = s->pb;

    mxf_write_generic_sound_common(s, st, key, size+6+8);

    mxf_write_local_tag(pb, 2, 0x3D0A);
    avio_wb16(pb, st->codec->block_align);

    // avg bytes per sec
    mxf_write_local_tag(pb, 4, 0x3D09);
    avio_wb32(pb, st->codec->block_align*st->codec->sample_rate);
}

static void mxf_write_wav_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_wav_common(s, st, mxf_wav_descriptor_key, 0);
}

static void mxf_write_aes3_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_wav_common(s, st, mxf_aes3_descriptor_key, 0);
}

static void mxf_write_generic_sound_desc(AVFormatContext *s, AVStream *st)
{
    mxf_write_generic_sound_common(s, st, mxf_generic_sound_descriptor_key, 0);
}

static void mxf_write_package(AVFormatContext *s, enum MXFMetadataSetType type)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, track_count = s->nb_streams+1;

    if (type == MaterialPackage) {
        mxf_write_metadata_key(pb, 0x013600);
        PRINT_KEY(s, "Material Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 92 + 16*track_count);
    } else {
        mxf_write_metadata_key(pb, 0x013700);
        PRINT_KEY(s, "Source Package key", pb->buf_ptr - 16);
        klv_encode_ber_length(pb, 112 + 16*track_count); // 20 bytes length for descriptor reference
    }

    // write uid
    mxf_write_local_tag(pb, 16, 0x3C0A);
    mxf_write_uuid(pb, type, 0);
    av_log(s,AV_LOG_DEBUG, "package type:%d\n", type);
    PRINT_KEY(s, "package uid", pb->buf_ptr - 16);

    // write package umid
    mxf_write_local_tag(pb, 32, 0x4401);
    mxf_write_umid(s, type == SourcePackage);
    PRINT_KEY(s, "package umid second part", pb->buf_ptr - 16);

    // package creation date
    mxf_write_local_tag(pb, 8, 0x4405);
    avio_wb64(pb, mxf->timestamp);

    // package modified date
    mxf_write_local_tag(pb, 8, 0x4404);
    avio_wb64(pb, mxf->timestamp);

    // write track refs
    mxf_write_local_tag(pb, track_count*16 + 8, 0x4403);
    mxf_write_refs_count(pb, track_count);
    mxf_write_uuid(pb, type == MaterialPackage ? Track :
                   Track + TypeBottom, -1); // timecode track
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

    // write timecode track
    mxf_write_track(s, mxf->timecode_track, type);
    mxf_write_sequence(s, mxf->timecode_track, type);
    mxf_write_timecode_component(s, mxf->timecode_track, type);

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
    mxf_write_preface(s);
    mxf_write_identification(s);
    mxf_write_content_storage(s);
    mxf_write_package(s, MaterialPackage);
    mxf_write_package(s, SourcePackage);
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

    av_log(s, AV_LOG_DEBUG, "edit units count %d\n", mxf->edit_units_count);

    if (!mxf->edit_units_count && !mxf->edit_unit_byte_count)
        return;

    avio_write(pb, index_table_segment_key, 16);

    if (mxf->edit_unit_byte_count) {
        klv_encode_ber_length(pb, 80);
    } else {
        klv_encode_ber_length(pb, 85 + 12+(s->nb_streams+1)*6 +
                              12+mxf->edit_units_count*(11+mxf->slice_count*4));
    }

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

    if (!mxf->edit_unit_byte_count) {
        // real slice count - 1
        mxf_write_local_tag(pb, 1, 0x3F08);
        avio_w8(pb, mxf->slice_count);

        // delta entry array
        mxf_write_local_tag(pb, 8 + (s->nb_streams+1)*6, 0x3F09);
        avio_wb32(pb, s->nb_streams+1); // num of entries
        avio_wb32(pb, 6);               // size of one entry
        // write system item delta entry
        avio_w8(pb, 0);
        avio_w8(pb, 0); // slice entry
        avio_wb32(pb, 0); // element delta
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            MXFStreamContext *sc = st->priv_data;
            avio_w8(pb, sc->temporal_reordering);
            if (sc->temporal_reordering)
                temporal_reordering = 1;
            if (i == 0) { // video track
                avio_w8(pb, 0); // slice number
                avio_wb32(pb, KAG_SIZE); // system item size including klv fill
            } else { // audio track
                unsigned audio_frame_size = sc->aic.samples[0]*sc->aic.sample_size;
                audio_frame_size += klv_fill_size(audio_frame_size);
                avio_w8(pb, 1);
                avio_wb32(pb, (i-1)*audio_frame_size); // element delta
            }
        }

        mxf_write_local_tag(pb, 8 + mxf->edit_units_count*(11+mxf->slice_count*4), 0x3F0A);
        avio_wb32(pb, mxf->edit_units_count);  // num of entries
        avio_wb32(pb, 11+mxf->slice_count*4);  // size of one entry

        for (i = 0; i < mxf->edit_units_count; i++) {
            int temporal_offset = 0;

            if (!(mxf->index_entries[i].flags & 0x33)) { // I frame
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
                avio_w8(pb, mxf->last_key_index - i);
            } else {
                avio_w8(pb, key_index - i); // key frame offset
                if ((mxf->index_entries[i].flags & 0x20) == 0x20) // only forward
                    mxf->last_key_index = key_index;
            }

            if (!(mxf->index_entries[i].flags & 0x33) && // I frame
                mxf->index_entries[i].flags & 0x40 && !temporal_offset)
                mxf->index_entries[i].flags |= 0x80; // random access
            avio_w8(pb, mxf->index_entries[i].flags);
            // stream offset
            avio_wb64(pb, mxf->index_entries[i].offset);
            if (s->nb_streams > 1)
                avio_wb32(pb, mxf->index_entries[i].slice_offset);
        }

        mxf->last_key_index = key_index - mxf->edit_units_count;
        mxf->last_indexed_edit_unit += mxf->edit_units_count;
        mxf->edit_units_count = 0;
    }
}

static void mxf_write_klv_fill(AVFormatContext *s)
{
    unsigned pad = klv_fill_size(avio_tell(s->pb));
    if (pad) {
        avio_write(s->pb, klv_fill_key, 16);
        pad -= 16 + 4;
        klv_encode_ber4_length(s->pb, pad);
        for (; pad; pad--)
            avio_w8(s->pb, 0);
        av_assert1(!(avio_tell(s->pb) & (KAG_SIZE-1)));
    }
}

static void mxf_write_partition(AVFormatContext *s, int bodysid,
                                int indexsid,
                                const uint8_t *key, int write_metadata)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t header_byte_count_offset;
    unsigned index_byte_count = 0;
    uint64_t partition_offset = avio_tell(pb);

    if (!mxf->edit_unit_byte_count && mxf->edit_units_count)
        index_byte_count = 85 + 12+(s->nb_streams+1)*6 +
            12+mxf->edit_units_count*(11+mxf->slice_count*4);
    else if (mxf->edit_unit_byte_count && indexsid)
        index_byte_count = 80;

    if (index_byte_count) {
        // add encoded ber length
        index_byte_count += 16 + klv_ber_length(index_byte_count);
        index_byte_count += klv_fill_size(index_byte_count);
    }

    if (!memcmp(key, body_partition_key, 16)) {
        mxf->body_partition_offset =
            av_realloc(mxf->body_partition_offset,
                       (mxf->body_partitions_count+1)*
                       sizeof(*mxf->body_partition_offset));
        mxf->body_partition_offset[mxf->body_partitions_count++] = partition_offset;
    }

    // write klv
    avio_write(pb, key, 16);
    klv_encode_ber_length(pb, 88 + 16 * mxf->essence_container_count);

    // write partition value
    avio_wb16(pb, 1); // majorVersion
    avio_wb16(pb, 2); // minorVersion
    avio_wb32(pb, KAG_SIZE); // KAGSize

    avio_wb64(pb, partition_offset); // ThisPartition

    if (!memcmp(key, body_partition_key, 16) && mxf->body_partitions_count > 1)
        avio_wb64(pb, mxf->body_partition_offset[mxf->body_partitions_count-2]); // PreviousPartition
    else if (!memcmp(key, footer_partition_key, 16) && mxf->body_partitions_count)
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
    if (bodysid && mxf->edit_units_count && mxf->body_partitions_count) {
        avio_wb64(pb, mxf->body_offset);
    } else
        avio_wb64(pb, 0);

    avio_wb32(pb, bodysid); // bodySID

    // operational pattern
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
        mxf_write_header_metadata_sets(s);
        pos = avio_tell(s->pb);
        header_byte_count = pos - start + klv_fill_size(pos);

        // update header_byte_count
        avio_seek(pb, header_byte_count_offset, SEEK_SET);
        avio_wb64(pb, header_byte_count);
        avio_seek(pb, pos, SEEK_SET);
    }

    avio_flush(pb);
}

static int mxf_parse_dnxhd_frame(AVFormatContext *s, AVStream *st,
AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    int i, cid;
    uint8_t* header_cid;
    unsigned int frame_size = 0;

    if (mxf->header_written)
        return 1;

    if (pkt->size < 43)
        return -1;

    header_cid = pkt->data + 0x28;
    cid = header_cid[0] << 24 | header_cid[1] << 16 | header_cid[2] << 8 | header_cid[3];

    if ((i = ff_dnxhd_get_cid_table(cid)) < 0)
        return -1;

    switch (cid) {
    case 1235:
        sc->index = 24;
        sc->component_depth = 10;
        break;
    case 1237:
        sc->index = 25;
        break;
    case 1238:
        sc->index = 26;
        break;
    case 1241:
        sc->index = 27;
        sc->component_depth = 10;
        break;
    case 1242:
        sc->index = 28;
        break;
    case 1243:
        sc->index = 29;
        break;
    case 1250:
        sc->index = 30;
        sc->component_depth = 10;
        break;
    case 1251:
        sc->index = 31;
        break;
    case 1252:
        sc->index = 32;
        break;
    case 1253:
        sc->index = 33;
        break;
    default:
        return -1;
    }

    frame_size = ff_dnxhd_cid_table[i].frame_size;
    sc->codec_ul = &mxf_essence_container_uls[sc->index].codec_ul;
    sc->aspect_ratio = (AVRational){ 16, 9 };

    mxf->edit_unit_byte_count = KAG_SIZE;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = st->priv_data;
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            mxf->edit_unit_byte_count += 16 + 4 + sc->aic.samples[0]*sc->aic.sample_size;
            mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
        } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            mxf->edit_unit_byte_count += 16 + 4 + frame_size;
            mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
        }
    }

    return 1;
}

static int mxf_parse_dv_frame(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    MXFStreamContext *sc = st->priv_data;
    uint8_t *vs_pack, *vsc_pack;
    int i, ul_index, frame_size, stype, pal;

    if (mxf->header_written)
        return 1;

    // Check for minimal frame size
    if (pkt->size < 120000)
        return -1;

    vs_pack  = pkt->data + 80*5 + 48;
    vsc_pack = pkt->data + 80*5 + 53;
    stype    = vs_pack[3] & 0x1f;
    pal      = (vs_pack[3] >> 5) & 0x1;

    if ((vs_pack[2] & 0x07) == 0x02)
        sc->aspect_ratio = (AVRational){ 16, 9 };
    else
        sc->aspect_ratio = (AVRational){ 4, 3 };

    sc->interlaced = (vsc_pack[3] >> 4) & 0x01;
    // TODO: fix dv encoder to set proper FF/FS value in VSC pack
    // and set field dominance accordingly
    // av_log(s, AV_LOG_DEBUG, "DV vsc pack ff/ss = %x\n", vsc_pack[2] >> 6);

    switch (stype) {
    case 0x18: // DV100 720p
        ul_index = 6 + pal;
        frame_size = pal ? 288000 : 240000;
        if (sc->interlaced) {
            av_log(s, AV_LOG_ERROR, "source marked as interlaced but codec profile is progressive\n");
            sc->interlaced = 0;
        }
        break;
    case 0x14: // DV100 1080i
        ul_index = 4 + pal;
        frame_size = pal ? 576000 : 480000;
        break;
    case 0x04: // DV50
        ul_index = 2 + pal;
        frame_size = pal ? 288000 : 240000;
        break;
    default: // DV25
        ul_index = 0 + pal;
        frame_size = pal ? 144000 : 120000;
    }

    sc->index = ul_index + 16;
    sc->codec_ul =  &mxf_essence_container_uls[sc->index].codec_ul;

    mxf->edit_unit_byte_count = KAG_SIZE;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = st->priv_data;
        if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            mxf->edit_unit_byte_count += 16 + 4 + sc->aic.samples[0]*sc->aic.sample_size;
            mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
        } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            mxf->edit_unit_byte_count += 16 + 4 + frame_size;
            mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
        }
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

static const UID *mxf_get_mpeg2_codec_ul(AVCodecContext *avctx)
{
    int long_gop = avctx->gop_size > 1 || avctx->has_b_frames;

    if (avctx->profile == 4) { // Main
        if (avctx->level == 8) // Main
            return &mxf_mpeg2_codec_uls[0+long_gop];
        else if (avctx->level == 4) // High
            return &mxf_mpeg2_codec_uls[4+long_gop];
        else if (avctx->level == 6) // High 14
            return &mxf_mpeg2_codec_uls[8+long_gop];
    } else if (avctx->profile == 0) { // 422
        if (avctx->level == 5) // Main
            return &mxf_mpeg2_codec_uls[2+long_gop];
        else if (avctx->level == 2) // High
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
                st->codec->profile = pkt->data[i+1] & 0x07;
                st->codec->level   = pkt->data[i+2] >> 4;
            } else if (i + 5 < pkt->size && (pkt->data[i+1] & 0xf0) == 0x80) { // pict coding ext
                sc->interlaced = !(pkt->data[i+5] & 0x80); // progressive frame
                if (sc->interlaced)
                    sc->field_dominance = 1 + !(pkt->data[i+4] & 0x80); // top field first
                break;
            }
        } else if (c == 0x1b8) { // gop
            if (pkt->data[i+4]>>6 & 0x01) { // closed
                sc->closed_gop = 1;
                if (e->flags & 0x40) // sequence header present
                    e->flags |= 0x80; // random access
            }
        } else if (c == 0x1b3) { // seq
            e->flags |= 0x40;
            switch ((pkt->data[i+4]>>4) & 0xf) {
            case 2:  sc->aspect_ratio = (AVRational){  4,  3}; break;
            case 3:  sc->aspect_ratio = (AVRational){ 16,  9}; break;
            case 4:  sc->aspect_ratio = (AVRational){221,100}; break;
            default:
                av_reduce(&sc->aspect_ratio.num, &sc->aspect_ratio.den,
                          st->codec->width, st->codec->height, 1024*1024);
            }
        } else if (c == 0x100) { // pic
            int pict_type = (pkt->data[i+2]>>3) & 0x07;
            e->temporal_ref = (pkt->data[i+1]<<2) | (pkt->data[i+2]>>6);
            if (pict_type == 2) { // P frame
                e->flags |= 0x22;
                sc->closed_gop = 0; // reset closed gop, don't matter anymore
            } else if (pict_type == 3) { // B frame
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
        sc->codec_ul = mxf_get_mpeg2_codec_ul(st->codec);
    return !!sc->codec_ul;
}

static uint64_t mxf_parse_timestamp(time_t timestamp)
{
    struct tm *time = gmtime(&timestamp);
    if (!time)
        return 0;
    return (uint64_t)(time->tm_year+1900) << 48 |
           (uint64_t)(time->tm_mon+1)     << 40 |
           (uint64_t) time->tm_mday       << 32 |
                      time->tm_hour       << 24 |
                      time->tm_min        << 16 |
                      time->tm_sec        << 8;
}

static void mxf_gen_umid(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    uint32_t seed = av_get_random_seed();
    uint64_t umid = seed + 0x5294713400000000LL;

    AV_WB64(mxf->umid  , umid);
    AV_WB64(mxf->umid+8, umid>>8);

    mxf->instance_number = seed;
}

static int mxf_write_header(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    int i, ret;
    uint8_t present[FF_ARRAY_ELEMS(mxf_essence_container_uls)] = {0};
    const MXFSamplesPerFrame *spf = NULL;
    AVDictionaryEntry *t;
    int64_t timestamp = 0;
    AVDictionaryEntry *tcr = av_dict_get(s->metadata, "timecode", NULL, 0);

    if (!s->nb_streams)
        return -1;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        MXFStreamContext *sc = av_mallocz(sizeof(*sc));
        if (!sc)
            return AVERROR(ENOMEM);
        st->priv_data = sc;

        if ((i == 0) ^ (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)) {
            av_log(s, AV_LOG_ERROR, "there must be exactly one video stream and it must be the first one\n");
            return -1;
        }

        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVRational rate, tbc = st->codec->time_base;
            // Default component depth to 8
            sc->component_depth = 8;
            mxf->timecode_base = (tbc.den + tbc.num/2) / tbc.num;
            spf = ff_mxf_get_samples_per_frame(s, tbc);
            if (!spf) {
                av_log(s, AV_LOG_ERROR, "Unsupported video frame rate %d/%d\n",
                       tbc.den, tbc.num);
                return AVERROR(EINVAL);
            }
            mxf->time_base = spf->time_base;
            rate = av_inv_q(mxf->time_base);
            avpriv_set_pts_info(st, 64, mxf->time_base.num, mxf->time_base.den);
            if (!tcr)
                tcr = av_dict_get(st->metadata, "timecode", NULL, 0);
            if (tcr)
                ret = av_timecode_init_from_string(&mxf->tc, rate, tcr->value, s);
            else
                ret = av_timecode_init(&mxf->tc, rate, 0, 0, s);
            if (ret < 0)
                return ret;
            if (s->oformat == &ff_mxf_d10_muxer) {
                if (st->codec->bit_rate == 50000000)
                    if (mxf->time_base.den == 25) sc->index = 3;
                    else                          sc->index = 5;
                else if (st->codec->bit_rate == 40000000)
                    if (mxf->time_base.den == 25) sc->index = 7;
                    else                          sc->index = 9;
                else if (st->codec->bit_rate == 30000000)
                    if (mxf->time_base.den == 25) sc->index = 11;
                    else                          sc->index = 13;
                else {
                    av_log(s, AV_LOG_ERROR, "error MXF D-10 only support 30/40/50 mbit/s\n");
                    return -1;
                }

                mxf->edit_unit_byte_count = KAG_SIZE; // system element
                mxf->edit_unit_byte_count += 16 + 4 + (uint64_t)st->codec->bit_rate *
                    mxf->time_base.num / (8*mxf->time_base.den);
                mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
                mxf->edit_unit_byte_count += 16 + 4 + 4 + spf->samples_per_frame[0]*8*4;
                mxf->edit_unit_byte_count += klv_fill_size(mxf->edit_unit_byte_count);
            }
        } else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (st->codec->sample_rate != 48000) {
                av_log(s, AV_LOG_ERROR, "only 48khz is implemented\n");
                return -1;
            }
            avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);
            if (s->oformat == &ff_mxf_d10_muxer) {
                if (st->index != 1) {
                    av_log(s, AV_LOG_ERROR, "MXF D-10 only support one audio track\n");
                    return -1;
                }
                if (st->codec->codec_id != AV_CODEC_ID_PCM_S16LE &&
                    st->codec->codec_id != AV_CODEC_ID_PCM_S24LE) {
                    av_log(s, AV_LOG_ERROR, "MXF D-10 only support 16 or 24 bits le audio\n");
                }
                sc->index = ((MXFStreamContext*)s->streams[0]->priv_data)->index + 1;
            } else
            mxf->slice_count = 1;
        }

        if (!sc->index) {
            sc->index = mxf_get_essence_container_ul_index(st->codec->codec_id);
            if (sc->index == -1) {
                av_log(s, AV_LOG_ERROR, "track %d: could not find essence container ul, "
                       "codec not currently supported in container\n", i);
                return -1;
            }
        }

        sc->codec_ul = &mxf_essence_container_uls[sc->index].codec_ul;

        memcpy(sc->track_essence_element_key, mxf_essence_container_uls[sc->index].element_ul, 15);
        sc->track_essence_element_key[15] = present[sc->index];
        PRINT_KEY(s, "track essence element key", sc->track_essence_element_key);

        if (!present[sc->index])
            mxf->essence_container_count++;
        present[sc->index]++;
    }

    if (s->oformat == &ff_mxf_d10_muxer) {
        mxf->essence_container_count = 1;
    }

    if (!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
        mxf_gen_umid(s);

    for (i = 0; i < s->nb_streams; i++) {
        MXFStreamContext *sc = s->streams[i]->priv_data;
        // update element count
        sc->track_essence_element_key[13] = present[sc->index];
        if (!memcmp(sc->track_essence_element_key, mxf_essence_container_uls[15].element_ul, 13)) // DV
            sc->order = (0x15 << 24) | AV_RB32(sc->track_essence_element_key+13);
        else
            sc->order = AV_RB32(sc->track_essence_element_key+12);
    }

    if (t = av_dict_get(s->metadata, "creation_time", NULL, 0))
        timestamp = ff_iso8601_to_unix_time(t->value);
    if (timestamp)
        mxf->timestamp = mxf_parse_timestamp(timestamp);
    mxf->duration = -1;

    mxf->timecode_track = av_mallocz(sizeof(*mxf->timecode_track));
    if (!mxf->timecode_track)
        return AVERROR(ENOMEM);
    mxf->timecode_track->priv_data = av_mallocz(sizeof(MXFStreamContext));
    if (!mxf->timecode_track->priv_data)
        return AVERROR(ENOMEM);
    mxf->timecode_track->index = -1;

    if (!spf)
        spf = ff_mxf_get_samples_per_frame(s, (AVRational){ 1, 25 });

    if (ff_audio_interleave_init(s, spf->samples_per_frame, mxf->time_base) < 0)
        return -1;

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

    frame = mxf->last_indexed_edit_unit + mxf->edit_units_count;

    // write system metadata pack
    avio_write(pb, system_metadata_pack_key, 16);
    klv_encode_ber4_length(pb, 57);
    avio_w8(pb, 0x5c); // UL, user date/time stamp, picture and sound item present
    avio_w8(pb, 0x04); // content package rate
    avio_w8(pb, 0x00); // content package type
    avio_wb16(pb, 0x00); // channel handle
    avio_wb16(pb, mxf->tc.start + frame); // continuity count
    if (mxf->essence_container_count > 1)
        avio_write(pb, multiple_desc_ul, 16);
    else {
        MXFStreamContext *sc = s->streams[0]->priv_data;
        avio_write(pb, mxf_essence_container_uls[sc->index].container_ul, 16);
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

static void mxf_write_d10_video_packet(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int packet_size = (uint64_t)st->codec->bit_rate*mxf->time_base.num /
        (8*mxf->time_base.den); // frame size
    int pad;

    packet_size += 16 + 4;
    packet_size += klv_fill_size(packet_size);

    klv_encode_ber4_length(pb, pkt->size);
    avio_write(pb, pkt->data, pkt->size);

    // ensure CBR muxing by padding to correct video frame size
    pad = packet_size - pkt->size - 16 - 4;
    if (pad > 20) {
        avio_write(s->pb, klv_fill_key, 16);
        pad -= 16 + 4;
        klv_encode_ber4_length(s->pb, pad);
        for (; pad; pad--)
            avio_w8(s->pb, 0);
        av_assert1(!(avio_tell(s->pb) & (KAG_SIZE-1)));
    } else {
        av_log(s, AV_LOG_WARNING, "cannot fill d-10 video packet\n");
        for (; pad > 0; pad--)
            avio_w8(s->pb, 0);
    }
}

static void mxf_write_d10_audio_packet(AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    int frame_size = pkt->size / st->codec->block_align;
    uint8_t *samples = pkt->data;
    uint8_t *end = pkt->data + pkt->size;
    int i;

    klv_encode_ber4_length(pb, 4 + frame_size*4*8);

    avio_w8(pb, (frame_size == 1920 ? 0 : (mxf->edit_units_count-1) % 5 + 1));
    avio_wl16(pb, frame_size);
    avio_w8(pb, (1<<st->codec->channels)-1);

    while (samples < end) {
        for (i = 0; i < st->codec->channels; i++) {
            uint32_t sample;
            if (st->codec->codec_id == AV_CODEC_ID_PCM_S24LE) {
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

static int mxf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    MXFStreamContext *sc = st->priv_data;
    MXFIndexEntry ie = {0};

    if (!mxf->edit_unit_byte_count && !(mxf->edit_units_count % EDIT_UNITS_PER_BODY)) {
        mxf->index_entries = av_realloc(mxf->index_entries,
            (mxf->edit_units_count + EDIT_UNITS_PER_BODY)*sizeof(*mxf->index_entries));
        if (!mxf->index_entries) {
            av_log(s, AV_LOG_ERROR, "could not allocate index entries\n");
            return -1;
        }
    }

    if (st->codec->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
        if (!mxf_parse_mpeg2_frame(s, st, pkt, &ie)) {
            av_log(s, AV_LOG_ERROR, "could not get mpeg2 profile and level\n");
            return -1;
        }
    } else if (st->codec->codec_id == AV_CODEC_ID_DNXHD) {
        if (!mxf_parse_dnxhd_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get dnxhd profile\n");
            return -1;
        }
    } else if (st->codec->codec_id == AV_CODEC_ID_DVVIDEO) {
        if (!mxf_parse_dv_frame(s, st, pkt)) {
            av_log(s, AV_LOG_ERROR, "could not get dv profile\n");
            return -1;
        }
    }

    if (!mxf->header_written) {
        if (mxf->edit_unit_byte_count) {
            mxf_write_partition(s, 1, 2, header_open_partition_key, 1);
            mxf_write_klv_fill(s);
            mxf_write_index_table_segment(s);
        } else {
            mxf_write_partition(s, 0, 0, header_open_partition_key, 1);
        }
        mxf->header_written = 1;
    }

    if (st->index == 0) {
        if (!mxf->edit_unit_byte_count &&
            (!mxf->edit_units_count || mxf->edit_units_count > EDIT_UNITS_PER_BODY) &&
            !(ie.flags & 0x33)) { // I frame, Gop start
            mxf_write_klv_fill(s);
            mxf_write_partition(s, 1, 2, body_partition_key, 0);

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
        mxf->index_entries[mxf->edit_units_count-1].slice_offset =
            mxf->body_offset - mxf->index_entries[mxf->edit_units_count-1].offset;
    }

    mxf_write_klv_fill(s);
    avio_write(pb, sc->track_essence_element_key, 16); // write key
    if (s->oformat == &ff_mxf_d10_muxer) {
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            mxf_write_d10_video_packet(s, st, pkt);
        else
            mxf_write_d10_audio_packet(s, st, pkt);
    } else {
        klv_encode_ber4_length(pb, pkt->size); // write length
        avio_write(pb, pkt->data, pkt->size);
        mxf->body_offset += 16+4+pkt->size + klv_fill_size(16+4+pkt->size);
    }

    avio_flush(pb);

    return 0;
}

static void mxf_write_random_index_pack(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVIOContext *pb = s->pb;
    uint64_t pos = avio_tell(pb);
    int i;

    avio_write(pb, random_index_pack_key, 16);
    klv_encode_ber_length(pb, 28 + 12*mxf->body_partitions_count);

    if (mxf->edit_unit_byte_count)
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

    mxf->duration = mxf->last_indexed_edit_unit + mxf->edit_units_count;

    mxf_write_klv_fill(s);
    mxf->footer_partition_offset = avio_tell(pb);
    if (mxf->edit_unit_byte_count) { // no need to repeat index
        mxf_write_partition(s, 0, 0, footer_partition_key, 0);
    } else {
        mxf_write_partition(s, 0, 2, footer_partition_key, 0);

        mxf_write_klv_fill(s);
        mxf_write_index_table_segment(s);
    }

    mxf_write_klv_fill(s);
    mxf_write_random_index_pack(s);

    if (s->pb->seekable) {
        avio_seek(pb, 0, SEEK_SET);
        if (mxf->edit_unit_byte_count) {
            mxf_write_partition(s, 1, 2, header_closed_partition_key, 1);
            mxf_write_klv_fill(s);
            mxf_write_index_table_segment(s);
        } else {
            mxf_write_partition(s, 0, 0, header_closed_partition_key, 1);
        }
    }

    ff_audio_interleave_close(s);

    av_freep(&mxf->index_entries);
    av_freep(&mxf->body_partition_offset);
    av_freep(&mxf->timecode_track->priv_data);
    av_freep(&mxf->timecode_track);

    mxf_free(s);

    return 0;
}

static int mxf_interleave_get_packet(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    int i, stream_count = 0;

    for (i = 0; i < s->nb_streams; i++)
        stream_count += !!s->streams[i]->last_in_packet_buffer;

    if (stream_count && (s->nb_streams == stream_count || flush)) {
        AVPacketList *pktl = s->packet_buffer;
        if (s->nb_streams != stream_count) {
            AVPacketList *last = NULL;
            // find last packet in edit unit
            while (pktl) {
                if (!stream_count || pktl->pkt.stream_index == 0)
                    break;
                last = pktl;
                pktl = pktl->next;
                stream_count--;
            }
            // purge packet queue
            while (pktl) {
                AVPacketList *next = pktl->next;

                if(s->streams[pktl->pkt.stream_index]->last_in_packet_buffer == pktl)
                    s->streams[pktl->pkt.stream_index]->last_in_packet_buffer= NULL;
                av_free_packet(&pktl->pkt);
                av_freep(&pktl);
                pktl = next;
            }
            if (last)
                last->next = NULL;
            else {
                s->packet_buffer = NULL;
                s->packet_buffer_end= NULL;
                goto out;
            }
            pktl = s->packet_buffer;
        }

        *out = pktl->pkt;
        av_dlog(s, "out st:%d dts:%lld\n", (*out).stream_index, (*out).dts);
        s->packet_buffer = pktl->next;
        if(s->streams[pktl->pkt.stream_index]->last_in_packet_buffer == pktl)
            s->streams[pktl->pkt.stream_index]->last_in_packet_buffer= NULL;
        if(!s->packet_buffer)
            s->packet_buffer_end= NULL;
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
    return ff_audio_rechunk_interleave(s, out, pkt, flush,
                               mxf_interleave_get_packet, mxf_compare_timestamps);
}

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
    .flags             = AVFMT_NOTIMESTAMPS,
    .interleave_packet = mxf_interleave,
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
    .flags             = AVFMT_NOTIMESTAMPS,
    .interleave_packet = mxf_interleave,
};
