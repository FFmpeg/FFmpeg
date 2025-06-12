/*
 * MOV, 3GP, MP4 muxer
 * Copyright (c) 2003 Thomas Raivio
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
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

#include "config_components.h"

#include <stdint.h>
#include <inttypes.h>

#include "movenc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "dovi_isom.h"
#include "riff.h"
#include "avio.h"
#include "iamf_writer.h"
#include "isom.h"
#include "av1.h"
#include "avc.h"
#include "evc.h"
#include "libavcodec/ac3_parser_internal.h"
#include "libavcodec/dnxhddata.h"
#include "libavcodec/flac.h"
#include "libavcodec/get_bits.h"

#include "libavcodec/internal.h"
#include "libavcodec/put_bits.h"
#include "libavcodec/vc1_common.h"
#include "libavcodec/raw.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/csp.h"
#include "libavutil/intfloat.h"
#include "libavutil/mathematics.h"
#include "libavutil/libm.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timecode.h"
#include "libavutil/dovi_meta.h"
#include "libavutil/uuid.h"
#include "hevc.h"
#include "rtpenc.h"
#include "nal.h"
#include "mov_chan.h"
#include "movenc_ttml.h"
#include "mux.h"
#include "rawutils.h"
#include "ttmlenc.h"
#include "version.h"
#include "vpcc.h"
#include "vvc.h"

static const AVOption options[] = {
    { "brand",    "Override major brand", offsetof(MOVMuxContext, major_brand),   AV_OPT_TYPE_STRING, {.str = NULL}, .flags = AV_OPT_FLAG_ENCODING_PARAM },
    { "empty_hdlr_name", "write zero-length name string in hdlr atoms within mdia and minf atoms", offsetof(MOVMuxContext, empty_hdlr_name), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "encryption_key", "The media encryption key (hex)", offsetof(MOVMuxContext, encryption_key), AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_ENCODING_PARAM },
    { "encryption_kid", "The media encryption key identifier (hex)", offsetof(MOVMuxContext, encryption_kid), AV_OPT_TYPE_BINARY, .flags = AV_OPT_FLAG_ENCODING_PARAM },
    { "encryption_scheme",    "Configures the encryption scheme, allowed values are none, cenc-aes-ctr", offsetof(MOVMuxContext, encryption_scheme_str),   AV_OPT_TYPE_STRING, {.str = NULL}, .flags = AV_OPT_FLAG_ENCODING_PARAM },
    { "frag_duration", "Maximum fragment duration", offsetof(MOVMuxContext, max_fragment_duration), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "frag_interleave", "Interleave samples within fragments (max number of consecutive samples, lower is tighter interleaving, but with more overhead)", offsetof(MOVMuxContext, frag_interleave), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "frag_size", "Maximum fragment size", offsetof(MOVMuxContext, max_fragment_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "fragment_index", "Fragment number of the next fragment", offsetof(MOVMuxContext, fragments), AV_OPT_TYPE_INT, {.i64 = 1}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "iods_audio_profile", "iods audio profile atom.", offsetof(MOVMuxContext, iods_audio_profile), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 255, AV_OPT_FLAG_ENCODING_PARAM},
    { "iods_video_profile", "iods video profile atom.", offsetof(MOVMuxContext, iods_video_profile), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 255, AV_OPT_FLAG_ENCODING_PARAM},
    { "ism_lookahead", "Number of lookahead entries for ISM files", offsetof(MOVMuxContext, ism_lookahead), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 255, AV_OPT_FLAG_ENCODING_PARAM},
    { "movflags", "MOV muxer flags", offsetof(MOVMuxContext, flags), AV_OPT_TYPE_FLAGS, {.i64 = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "cmaf", "Write CMAF compatible fragmented MP4", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_CMAF}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "dash", "Write DASH compatible fragmented MP4", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_DASH}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "default_base_moof", "Set the default-base-is-moof flag in tfhd atoms", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_DEFAULT_BASE_MOOF}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "delay_moov", "Delay writing the initial moov until the first fragment is cut, or until the first fragment flush", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_DELAY_MOOV}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "disable_chpl", "Disable Nero chapter atom", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_DISABLE_CHPL}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "empty_moov", "Make the initial moov atom empty", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_EMPTY_MOOV}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "faststart", "Run a second pass to put the index (moov atom) at the beginning of the file", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_FASTSTART}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "frag_custom", "Flush fragments on caller requests", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_FRAG_CUSTOM}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "frag_discont", "Signal that the next fragment is discontinuous from earlier ones", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_FRAG_DISCONT}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "frag_every_frame", "Fragment at every frame", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_FRAG_EVERY_FRAME}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "frag_keyframe", "Fragment at video keyframes", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_FRAG_KEYFRAME}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "global_sidx", "Write a global sidx index at the start of the file", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_GLOBAL_SIDX}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "isml", "Create a live smooth streaming feed (for pushing to a publishing point)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_ISML}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "moov_size", "maximum moov size so it can be placed at the begin", offsetof(MOVMuxContext, reserved_moov_size), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = 0 },
      { "negative_cts_offsets", "Use negative CTS offsets (reducing the need for edit lists)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "omit_tfhd_offset", "Omit the base data offset in tfhd atoms", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_OMIT_TFHD_OFFSET}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "prefer_icc", "If writing colr atom prioritise usage of ICC profile if it exists in stream packet side data", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_PREFER_ICC}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "rtphint", "Add RTP hint tracks", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_RTP_HINT}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "separate_moof", "Write separate moof/mdat atoms for each track", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_SEPARATE_MOOF}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "skip_sidx", "Skip writing of sidx atom", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_SKIP_SIDX}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "skip_trailer", "Skip writing the mfra/tfra/mfro trailer for fragmented files", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_SKIP_TRAILER}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "use_metadata_tags", "Use mdta atom for metadata.", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_USE_MDTA}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "write_colr", "Write colr atom even if the color info is unspecified (Experimental, may be renamed or changed, do not use from scripts)", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_WRITE_COLR}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "write_gama", "Write deprecated gama atom", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_WRITE_GAMA}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
      { "hybrid_fragmented", "For recoverability, write a fragmented file that is converted to non-fragmented at the end.", 0, AV_OPT_TYPE_CONST, {.i64 = FF_MOV_FLAG_HYBRID_FRAGMENTED}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = "movflags" },
    { "min_frag_duration", "Minimum fragment duration", offsetof(MOVMuxContext, min_fragment_duration), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "mov_gamma", "gamma value for gama atom", offsetof(MOVMuxContext, gamma), AV_OPT_TYPE_FLOAT, {.dbl = 0.0 }, 0.0, 10, AV_OPT_FLAG_ENCODING_PARAM},
    { "movie_timescale", "set movie timescale", offsetof(MOVMuxContext, movie_timescale), AV_OPT_TYPE_INT, {.i64 = MOV_TIMESCALE}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    FF_RTP_FLAG_OPTS(MOVMuxContext, rtp_flags),
    { "skip_iods", "Skip writing iods atom.", offsetof(MOVMuxContext, iods_skip), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "use_editlist", "use edit list", offsetof(MOVMuxContext, use_editlist), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "use_stream_ids_as_track_ids", "use stream ids as track ids", offsetof(MOVMuxContext, use_stream_ids_as_track_ids), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "video_track_timescale", "set timescale of all video tracks", offsetof(MOVMuxContext, video_track_timescale), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "write_btrt", "force or disable writing btrt", offsetof(MOVMuxContext, write_btrt), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "write_prft", "Write producer reference time box with specified time source", offsetof(MOVMuxContext, write_prft), AV_OPT_TYPE_INT, {.i64 = MOV_PRFT_NONE}, 0, MOV_PRFT_NB-1, AV_OPT_FLAG_ENCODING_PARAM, .unit = "prft"},
      { "pts", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MOV_PRFT_SRC_PTS}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, .unit = "prft"},
      { "wallclock", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = MOV_PRFT_SRC_WALLCLOCK}, 0, 0, AV_OPT_FLAG_ENCODING_PARAM, .unit = "prft"},
    { "write_tmcd", "force or disable writing tmcd", offsetof(MOVMuxContext, write_tmcd), AV_OPT_TYPE_BOOL, {.i64 = -1}, -1, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

static const AVClass mov_isobmff_muxer_class = {
    .class_name = "mov/mp4/tgp/psp/tg2/ipod/ismv/f4v muxer",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int get_moov_size(AVFormatContext *s);
static int mov_write_single_packet(AVFormatContext *s, AVPacket *pkt);

static int utf8len(const uint8_t *b)
{
    int len = 0;
    int val;
    while (*b) {
        GET_UTF8(val, *b++, return -1;)
        len++;
    }
    return len;
}

//FIXME support 64 bit variant with wide placeholders
static int64_t update_size(AVIOContext *pb, int64_t pos)
{
    int64_t curpos = avio_tell(pb);
    avio_seek(pb, pos, SEEK_SET);
    avio_wb32(pb, curpos - pos); /* rewrite size */
    avio_seek(pb, curpos, SEEK_SET);

    return curpos - pos;
}

static int co64_required(const MOVTrack *track)
{
    if (track->entry > 0 && track->cluster[track->entry - 1].pos + track->data_offset > UINT32_MAX)
        return 1;
    return 0;
}

static int is_cover_image(const AVStream *st)
{
    /* Eg. AV_DISPOSITION_ATTACHED_PIC | AV_DISPOSITION_TIMED_THUMBNAILS
     * is encoded as sparse video track */
    return st && st->disposition == AV_DISPOSITION_ATTACHED_PIC;
}

static int rtp_hinting_needed(const AVStream *st)
{
    /* Add hint tracks for each real audio and video stream */
    if (is_cover_image(st))
        return 0;
    return st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
           st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
}

/* Chunk offset atom */
static int mov_write_stco_tag(AVIOContext *pb, MOVTrack *track)
{
    int i;
    int mode64 = co64_required(track); // use 32 bit size variant if possible
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    if (mode64)
        ffio_wfourcc(pb, "co64");
    else
        ffio_wfourcc(pb, "stco");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, track->chunkCount); /* entry count */
    for (i = 0; i < track->entry; i++) {
        if (!track->cluster[i].chunkNum)
            continue;
        if (mode64 == 1)
            avio_wb64(pb, track->cluster[i].pos + track->data_offset);
        else
            avio_wb32(pb, track->cluster[i].pos + track->data_offset);
    }
    return update_size(pb, pos);
}

/* Sample size atom */
static int mov_write_stsz_tag(AVIOContext *pb, MOVTrack *track)
{
    int equalChunks = 1;
    int i, j, entries = 0, tst = -1, oldtst = -1;

    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stsz");
    avio_wb32(pb, 0); /* version & flags */

    for (i = 0; i < track->entry; i++) {
        tst = track->cluster[i].size / track->cluster[i].entries;
        if (oldtst != -1 && tst != oldtst)
            equalChunks = 0;
        oldtst = tst;
        entries += track->cluster[i].entries;
    }
    if (equalChunks && track->entry) {
        int sSize = track->entry ? track->cluster[0].size / track->cluster[0].entries : 0;
        sSize = FFMAX(1, sSize); // adpcm mono case could make sSize == 0
        avio_wb32(pb, sSize); // sample size
        avio_wb32(pb, entries); // sample count
    } else {
        avio_wb32(pb, 0); // sample size
        avio_wb32(pb, entries); // sample count
        for (i = 0; i < track->entry; i++) {
            for (j = 0; j < track->cluster[i].entries; j++) {
                avio_wb32(pb, track->cluster[i].size /
                          track->cluster[i].entries);
            }
        }
    }
    return update_size(pb, pos);
}

/* Sample to chunk atom */
static int mov_write_stsc_tag(AVIOContext *pb, MOVTrack *track)
{
    int index = 0, oldval = -1, i;
    int64_t entryPos, curpos;

    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stsc");
    avio_wb32(pb, 0); // version & flags
    entryPos = avio_tell(pb);
    avio_wb32(pb, track->chunkCount); // entry count
    for (i = 0; i < track->entry; i++) {
        if (oldval != track->cluster[i].samples_in_chunk && track->cluster[i].chunkNum) {
            avio_wb32(pb, track->cluster[i].chunkNum); // first chunk
            avio_wb32(pb, track->cluster[i].samples_in_chunk); // samples per chunk
            avio_wb32(pb, 0x1); // sample description index
            oldval = track->cluster[i].samples_in_chunk;
            index++;
        }
    }
    curpos = avio_tell(pb);
    avio_seek(pb, entryPos, SEEK_SET);
    avio_wb32(pb, index); // rewrite size
    avio_seek(pb, curpos, SEEK_SET);

    return update_size(pb, pos);
}

/* Sync sample atom */
static int mov_write_stss_tag(AVIOContext *pb, MOVTrack *track, uint32_t flag)
{
    int64_t curpos, entryPos;
    int i, index = 0;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); // size
    ffio_wfourcc(pb, flag == MOV_SYNC_SAMPLE ? "stss" : "stps");
    avio_wb32(pb, 0); // version & flags
    entryPos = avio_tell(pb);
    avio_wb32(pb, track->entry); // entry count
    for (i = 0; i < track->entry; i++) {
        if (track->cluster[i].flags & flag) {
            avio_wb32(pb, i + 1);
            index++;
        }
    }
    curpos = avio_tell(pb);
    avio_seek(pb, entryPos, SEEK_SET);
    avio_wb32(pb, index); // rewrite size
    avio_seek(pb, curpos, SEEK_SET);
    return update_size(pb, pos);
}

/* Sample dependency atom */
static int mov_write_sdtp_tag(AVIOContext *pb, MOVTrack *track)
{
    int i;
    uint8_t leading, dependent, reference, redundancy;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); // size
    ffio_wfourcc(pb, "sdtp");
    avio_wb32(pb, 0); // version & flags
    for (i = 0; i < track->entry; i++) {
        dependent = MOV_SAMPLE_DEPENDENCY_YES;
        leading = reference = redundancy = MOV_SAMPLE_DEPENDENCY_UNKNOWN;
        if (track->cluster[i].flags & MOV_DISPOSABLE_SAMPLE) {
            reference = MOV_SAMPLE_DEPENDENCY_NO;
        }
        if (track->cluster[i].flags & MOV_SYNC_SAMPLE) {
            dependent = MOV_SAMPLE_DEPENDENCY_NO;
        }
        avio_w8(pb, (leading << 6)   | (dependent << 4) |
                    (reference << 2) | redundancy);
    }
    return update_size(pb, pos);
}

#if CONFIG_IAMFENC
static int mov_write_iacb_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    AVIOContext *dyn_bc;
    int64_t pos = avio_tell(pb);
    uint8_t *dyn_buf = NULL;
    int dyn_size;
    int ret = avio_open_dyn_buf(&dyn_bc);
    if (ret < 0)
        return ret;

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "iacb");
    avio_w8(pb, 1); // configurationVersion

    ret = ff_iamf_write_descriptors(track->iamf, dyn_bc, s);
    if (ret < 0)
        return ret;

    dyn_size = avio_close_dyn_buf(dyn_bc, &dyn_buf);
    ffio_write_leb(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);

    return update_size(pb, pos);
}
#endif

static int mov_write_amr_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, 0x11); /* size */
    if (track->mode == MODE_MOV) ffio_wfourcc(pb, "samr");
    else                         ffio_wfourcc(pb, "damr");
    ffio_wfourcc(pb, "FFMP");
    avio_w8(pb, 0); /* decoder version */

    avio_wb16(pb, 0x81FF); /* Mode set (all modes for AMR_NB) */
    avio_w8(pb, 0x00); /* Mode change period (no restriction) */
    avio_w8(pb, 0x01); /* Frames per sample */
    return 0x11;
}

struct eac3_info {
    AVPacket *pkt;
    uint8_t ec3_done;
    uint8_t num_blocks;

    /* Layout of the EC3SpecificBox */
    /* maximum bitrate */
    uint16_t data_rate;
    int8_t   ac3_bit_rate_code;
    /* number of independent substreams */
    uint8_t  num_ind_sub;
    struct {
        /* sample rate code (see ff_ac3_sample_rate_tab) 2 bits */
        uint8_t fscod;
        /* bit stream identification 5 bits */
        uint8_t bsid;
        /* one bit reserved */
        /* audio service mixing (not supported yet) 1 bit */
        /* bit stream mode 3 bits */
        uint8_t bsmod;
        /* audio coding mode 3 bits */
        uint8_t acmod;
        /* sub woofer on 1 bit */
        uint8_t lfeon;
        /* 3 bits reserved */
        /* number of dependent substreams associated with this substream 4 bits */
        uint8_t num_dep_sub;
        /* channel locations of the dependent substream(s), if any, 9 bits */
        uint16_t chan_loc;
        /* if there is no dependent substream, then one bit reserved instead */
    } substream[1]; /* TODO: support 8 independent substreams */
    /* indicates the decoding complexity, 8 bits */
    uint8_t complexity_index_type_a;
};

static int mov_write_ac3_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    struct eac3_info *info = track->eac3_priv;
    PutBitContext pbc;
    uint8_t buf[3];

    if (!info || !info->ec3_done) {
        av_log(s, AV_LOG_ERROR,
               "Cannot write moov atom before AC3 packets."
               " Set the delay_moov flag to fix this.\n");
        return AVERROR(EINVAL);
    }

    if (info->substream[0].bsid > 8) {
        av_log(s, AV_LOG_ERROR,
               "RealAudio AC-3/DolbyNet with bsid %d is not defined by the "
               "ISOBMFF specification in ETSI TS 102 366!\n",
               info->substream[0].bsid);
        return AVERROR(EINVAL);
    }

    if (info->ac3_bit_rate_code < 0) {
        av_log(s, AV_LOG_ERROR,
               "No valid AC3 bit rate code for data rate of %d!\n",
               info->data_rate);
        return AVERROR(EINVAL);
    }

    avio_wb32(pb, 11);
    ffio_wfourcc(pb, "dac3");

    init_put_bits(&pbc, buf, sizeof(buf));
    put_bits(&pbc, 2, info->substream[0].fscod);
    put_bits(&pbc, 5, info->substream[0].bsid);
    put_bits(&pbc, 3, info->substream[0].bsmod);
    put_bits(&pbc, 3, info->substream[0].acmod);
    put_bits(&pbc, 1, info->substream[0].lfeon);
    put_bits(&pbc, 5, info->ac3_bit_rate_code); // bit_rate_code
    put_bits(&pbc, 5, 0); // reserved

    flush_put_bits(&pbc);
    avio_write(pb, buf, sizeof(buf));

    return 11;
}

static int handle_eac3(MOVMuxContext *mov, AVPacket *pkt, MOVTrack *track)
{
    AC3HeaderInfo *hdr = NULL;
    struct eac3_info *info;
    int num_blocks, ret;

    if (!track->eac3_priv) {
        if (!(track->eac3_priv = av_mallocz(sizeof(*info))))
            return AVERROR(ENOMEM);

        ((struct eac3_info *)track->eac3_priv)->ac3_bit_rate_code = -1;
    }
    info = track->eac3_priv;

    if (!info->pkt && !(info->pkt = av_packet_alloc()))
        return AVERROR(ENOMEM);

    if ((ret = avpriv_ac3_parse_header(&hdr, pkt->data, pkt->size)) < 0) {
        if (ret == AVERROR(ENOMEM))
            goto end;

        /* drop the packets until we see a good one */
        if (!track->entry) {
            av_log(mov->fc, AV_LOG_WARNING, "Dropping invalid packet from start of the stream\n");
            ret = 0;
        } else
            ret = AVERROR_INVALIDDATA;
        goto end;
    }

    info->data_rate = FFMAX(info->data_rate, hdr->bit_rate / 1000);
    info->ac3_bit_rate_code = FFMAX(info->ac3_bit_rate_code,
                                    hdr->ac3_bit_rate_code);
    info->complexity_index_type_a = hdr->complexity_index_type_a;

    num_blocks = hdr->num_blocks;

    if (!info->ec3_done) {
        /* AC-3 substream must be the first one */
        if (hdr->bitstream_id <= 10 && hdr->substreamid != 0) {
            ret = AVERROR(EINVAL);
            goto end;
        }

        /* this should always be the case, given that our AC-3 parser
         * concatenates dependent frames to their independent parent */
        if (hdr->frame_type == EAC3_FRAME_TYPE_INDEPENDENT ||
            hdr->frame_type == EAC3_FRAME_TYPE_AC3_CONVERT) {
            /* substream ids must be incremental */
            if (hdr->substreamid > info->num_ind_sub + 1) {
                ret = AVERROR(EINVAL);
                goto end;
            }

            if (hdr->substreamid == info->num_ind_sub + 1) {
                //info->num_ind_sub++;
                avpriv_request_sample(mov->fc, "Multiple independent substreams");
                ret = AVERROR_PATCHWELCOME;
                goto end;
            } else if (hdr->substreamid < info->num_ind_sub ||
                       hdr->substreamid == 0 && info->substream[0].bsid) {
                info->ec3_done = 1;
                goto concatenate;
            }
        } else {
            if (hdr->substreamid != 0) {
                avpriv_request_sample(mov->fc, "Multiple non EAC3 independent substreams");
                ret = AVERROR_PATCHWELCOME;
                goto end;
            }
        }

        /* fill the info needed for the "dec3" atom */
        info->substream[hdr->substreamid].fscod = hdr->sr_code;
        info->substream[hdr->substreamid].bsid  = hdr->bitstream_id;
        info->substream[hdr->substreamid].bsmod = hdr->bitstream_mode;
        info->substream[hdr->substreamid].acmod = hdr->channel_mode;
        info->substream[hdr->substreamid].lfeon = hdr->lfe_on;

        if (track->par->codec_id == AV_CODEC_ID_AC3) {
            // with AC-3 we only require the information of a single packet,
            // so we can finish as soon as the basic values of the bit stream
            // have been set to the track's informational structure.
            info->ec3_done = 1;
            goto concatenate;
        }

        /* Parse dependent substream(s), if any */
        if (pkt->size != hdr->frame_size) {
            int cumul_size = hdr->frame_size;
            int parent = hdr->substreamid;

            while (cumul_size != pkt->size) {
                ret = avpriv_ac3_parse_header(&hdr, pkt->data + cumul_size, pkt->size - cumul_size);
                if (ret < 0)
                    goto end;
                if (hdr->frame_type != EAC3_FRAME_TYPE_DEPENDENT) {
                    ret = AVERROR(EINVAL);
                    goto end;
                }
                info->substream[parent].num_dep_sub++;
                ret /= 8;

                /* get the dependent stream channel map, if exists */
                if (hdr->channel_map_present)
                    info->substream[parent].chan_loc |= (hdr->channel_map >> 5) & 0x1f;
                else
                    info->substream[parent].chan_loc |= hdr->channel_mode;
                cumul_size += hdr->frame_size;
            }
        }
    }

concatenate:
    if (!info->num_blocks && num_blocks == 6) {
        ret = pkt->size;
        goto end;
    }
    else if (info->num_blocks + num_blocks > 6) {
        ret = AVERROR_INVALIDDATA;
        goto end;
    }

    if (!info->num_blocks) {
        ret = av_packet_ref(info->pkt, pkt);
        if (!ret)
            info->num_blocks = num_blocks;
        goto end;
    } else {
        if ((ret = av_grow_packet(info->pkt, pkt->size)) < 0)
            goto end;
        memcpy(info->pkt->data + info->pkt->size - pkt->size, pkt->data, pkt->size);
        info->num_blocks += num_blocks;
        info->pkt->duration += pkt->duration;
        if (info->num_blocks != 6)
            goto end;
        av_packet_unref(pkt);
        av_packet_move_ref(pkt, info->pkt);
        info->num_blocks = 0;
    }
    ret = pkt->size;

end:
    av_free(hdr);

    return ret;
}

static int mov_write_eac3_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    PutBitContext pbc;
    uint8_t *buf;
    struct eac3_info *info;
    int size, i;

    if (!track->eac3_priv) {
        av_log(s, AV_LOG_ERROR,
               "Cannot write moov atom before EAC3 packets parsed.\n");
        return AVERROR(EINVAL);
    }

    info = track->eac3_priv;
    size = 2 + (4 * (info->num_ind_sub + 1)) + (2 * !!info->complexity_index_type_a);
    buf = av_malloc(size);
    if (!buf) {
        return AVERROR(ENOMEM);
    }

    init_put_bits(&pbc, buf, size);
    put_bits(&pbc, 13, info->data_rate);
    put_bits(&pbc,  3, info->num_ind_sub);
    for (i = 0; i <= info->num_ind_sub; i++) {
        put_bits(&pbc, 2, info->substream[i].fscod);
        put_bits(&pbc, 5, info->substream[i].bsid);
        put_bits(&pbc, 1, 0); /* reserved */
        put_bits(&pbc, 1, 0); /* asvc */
        put_bits(&pbc, 3, info->substream[i].bsmod);
        put_bits(&pbc, 3, info->substream[i].acmod);
        put_bits(&pbc, 1, info->substream[i].lfeon);
        put_bits(&pbc, 3, 0); /* reserved */
        put_bits(&pbc, 4, info->substream[i].num_dep_sub);
        if (!info->substream[i].num_dep_sub) {
            put_bits(&pbc, 1, 0); /* reserved */
        } else {
            put_bits(&pbc, 9, info->substream[i].chan_loc);
        }
    }
    if (info->complexity_index_type_a) {
        put_bits(&pbc, 7, 0); /* reserved */
        put_bits(&pbc, 1, 1); // flag_eac3_extension_type_a
        put_bits(&pbc, 8, info->complexity_index_type_a);
    }
    flush_put_bits(&pbc);
    size = put_bytes_output(&pbc);

    avio_wb32(pb, size + 8);
    ffio_wfourcc(pb, "dec3");
    avio_write(pb, buf, size);

    av_free(buf);

    return size;
}

/**
 * This function writes extradata "as is".
 * Extradata must be formatted like a valid atom (with size and tag).
 */
static int mov_write_extradata_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_write(pb, track->par->extradata, track->par->extradata_size);
    return track->par->extradata_size;
}

static int mov_write_enda_tag(AVIOContext *pb)
{
    avio_wb32(pb, 10);
    ffio_wfourcc(pb, "enda");
    avio_wb16(pb, 1); /* little endian */
    return 10;
}

static int mov_write_enda_tag_be(AVIOContext *pb)
{
    avio_wb32(pb, 10);
    ffio_wfourcc(pb, "enda");
    avio_wb16(pb, 0); /* big endian */
    return 10;
}

static void put_descr(AVIOContext *pb, int tag, unsigned int size)
{
    int i = 3;
    avio_w8(pb, tag);
    for (; i > 0; i--)
        avio_w8(pb, (size >> (7 * i)) | 0x80);
    avio_w8(pb, size & 0x7F);
}

static unsigned compute_avg_bitrate(MOVTrack *track)
{
    uint64_t size = 0;
    int i;
    if (!track->track_duration)
        return 0;
    for (i = 0; i < track->entry; i++)
        size += track->cluster[i].size;
    return size * 8 * track->timescale / track->track_duration;
}

struct mpeg4_bit_rate_values {
    uint32_t buffer_size;  ///< Size of the decoding buffer for the elementary stream in bytes.
    uint32_t max_bit_rate; ///< Maximum rate in bits/second over any window of one second.
    uint32_t avg_bit_rate; ///< Average rate in bits/second over the entire presentation.
};

static struct mpeg4_bit_rate_values calculate_mpeg4_bit_rates(MOVTrack *track)
{
    const AVPacketSideData *sd = track->st ?
        av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                track->st->codecpar->nb_coded_side_data,
                                AV_PKT_DATA_CPB_PROPERTIES) : NULL;
    AVCPBProperties *props = sd ? (AVCPBProperties *)sd->data : NULL;
    struct mpeg4_bit_rate_values bit_rates = { 0 };

    bit_rates.avg_bit_rate = compute_avg_bitrate(track);
    if (!bit_rates.avg_bit_rate) {
        // if the average bit rate cannot be calculated at this point, such as
        // in the case of fragmented MP4, utilize the following values as
        // fall-back in priority order:
        //
        // 1. average bit rate property
        // 2. bit rate (usually average over the whole clip)
        // 3. maximum bit rate property

        if (props && props->avg_bitrate) {
            bit_rates.avg_bit_rate = props->avg_bitrate;
        } else if (track->par->bit_rate) {
            bit_rates.avg_bit_rate = track->par->bit_rate;
        } else if (props && props->max_bitrate) {
            bit_rates.avg_bit_rate = props->max_bitrate;
        }
    }

    // (FIXME should be max rate in any 1 sec window)
    bit_rates.max_bit_rate = FFMAX(track->par->bit_rate,
                                   bit_rates.avg_bit_rate);

    // utilize values from properties if we have them available
    if (props) {
        // no avg_bitrate signals that the track is VBR
        if (!props->avg_bitrate)
            bit_rates.avg_bit_rate = props->avg_bitrate;
        bit_rates.max_bit_rate = FFMAX(bit_rates.max_bit_rate,
                                       props->max_bitrate);
        bit_rates.buffer_size = props->buffer_size / 8;
    }

    return bit_rates;
}

static int mov_write_esds_tag(AVIOContext *pb, MOVTrack *track) // Basic
{
    struct mpeg4_bit_rate_values bit_rates = calculate_mpeg4_bit_rates(track);
    int64_t pos = avio_tell(pb);
    int decoder_specific_info_len = track->vos_len ? 5 + track->vos_len : 0;

    avio_wb32(pb, 0); // size
    ffio_wfourcc(pb, "esds");
    avio_wb32(pb, 0); // Version

    // ES descriptor
    put_descr(pb, 0x03, 3 + 5+13 + decoder_specific_info_len + 5+1);
    avio_wb16(pb, track->track_id);
    avio_w8(pb, 0x00); // flags (= no flags)

    // DecoderConfig descriptor
    put_descr(pb, 0x04, 13 + decoder_specific_info_len);

    // Object type indication
    if ((track->par->codec_id == AV_CODEC_ID_MP2 ||
         track->par->codec_id == AV_CODEC_ID_MP3) &&
        track->par->sample_rate > 24000)
        avio_w8(pb, 0x6B); // 11172-3
    else
        avio_w8(pb, ff_codec_get_tag(ff_mp4_obj_type, track->par->codec_id));

    // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
    // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
    if (track->par->codec_id == AV_CODEC_ID_DVD_SUBTITLE)
        avio_w8(pb, (0x38 << 2) | 1); // flags (= NeroSubpicStream)
    else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO)
        avio_w8(pb, 0x15); // flags (= Audiostream)
    else
        avio_w8(pb, 0x11); // flags (= Visualstream)

    avio_wb24(pb, bit_rates.buffer_size); // Buffersize DB
    avio_wb32(pb, bit_rates.max_bit_rate); // maxbitrate
    avio_wb32(pb, bit_rates.avg_bit_rate);

    if (track->vos_len) {
        // DecoderSpecific info descriptor
        put_descr(pb, 0x05, track->vos_len);
        avio_write(pb, track->vos_data, track->vos_len);
    }

    // SL descriptor
    put_descr(pb, 0x06, 1);
    avio_w8(pb, 0x02);
    return update_size(pb, pos);
}

static int mov_pcm_le_gt16(enum AVCodecID codec_id)
{
    return codec_id == AV_CODEC_ID_PCM_S24LE ||
           codec_id == AV_CODEC_ID_PCM_S32LE ||
           codec_id == AV_CODEC_ID_PCM_F32LE ||
           codec_id == AV_CODEC_ID_PCM_F64LE;
}

static int mov_pcm_be_gt16(enum AVCodecID codec_id)
{
    return codec_id == AV_CODEC_ID_PCM_S24BE ||
           codec_id == AV_CODEC_ID_PCM_S32BE ||
           codec_id == AV_CODEC_ID_PCM_F32BE ||
           codec_id == AV_CODEC_ID_PCM_F64BE;
}

static int mov_write_ms_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int ret;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);
    avio_wl32(pb, track->tag); // store it byteswapped
    track->par->codec_tag = av_bswap16(track->tag >> 16);
    if ((ret = ff_put_wav_header(s, pb, track->par, 0)) < 0)
        return ret;
    return update_size(pb, pos);
}

static int mov_write_wfex_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int ret;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "wfex");
    if ((ret = ff_put_wav_header(s, pb, track->st->codecpar, FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX)) < 0)
        return ret;
    return update_size(pb, pos);
}

static int mov_write_dfla_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "dfLa");
    avio_w8(pb, 0); /* version */
    avio_wb24(pb, 0); /* flags */

    /* Expect the encoder to pass a METADATA_BLOCK_TYPE_STREAMINFO. */
    if (track->par->extradata_size != FLAC_STREAMINFO_SIZE)
        return AVERROR_INVALIDDATA;

    /* TODO: Write other METADATA_BLOCK_TYPEs if the encoder makes them available. */
    avio_w8(pb, 1 << 7 | FLAC_METADATA_TYPE_STREAMINFO); /* LastMetadataBlockFlag << 7 | BlockType */
    avio_wb24(pb, track->par->extradata_size); /* Length */
    avio_write(pb, track->par->extradata, track->par->extradata_size); /* BlockData[Length] */

    return update_size(pb, pos);
}

static int mov_write_dops_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int channels, channel_map;
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "dOps");
    avio_w8(pb, 0); /* Version */
    if (track->par->extradata_size < 19) {
        av_log(s, AV_LOG_ERROR, "invalid extradata size\n");
        return AVERROR_INVALIDDATA;
    }
    /* extradata contains an Ogg OpusHead, other than byte-ordering and
       OpusHead's preceeding magic/version, OpusSpecificBox is currently
       identical. */
    channels = AV_RB8(track->par->extradata + 9);
    channel_map = AV_RB8(track->par->extradata + 18);

    avio_w8(pb, channels); /* OuputChannelCount */
    avio_wb16(pb, AV_RL16(track->par->extradata + 10)); /* PreSkip */
    avio_wb32(pb, AV_RL32(track->par->extradata + 12)); /* InputSampleRate */
    avio_wb16(pb, AV_RL16(track->par->extradata + 16)); /* OutputGain */
    avio_w8(pb, channel_map); /* ChannelMappingFamily */
    /* Write the rest of the header out without byte-swapping. */
    if (channel_map) {
        if (track->par->extradata_size < 21 + channels) {
            av_log(s, AV_LOG_ERROR, "invalid extradata size\n");
            return AVERROR_INVALIDDATA;
        }
        avio_write(pb, track->par->extradata + 19, 2 + channels); /* ChannelMappingTable */
    }

    return update_size(pb, pos);
}

static int mov_write_dmlp_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int length;
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "dmlp");

    if (track->vos_len < 20) {
        av_log(s, AV_LOG_ERROR,
               "Cannot write moov atom before TrueHD packets."
               " Set the delay_moov flag to fix this.\n");
        return AVERROR(EINVAL);
    }

    length = (AV_RB16(track->vos_data) & 0xFFF) * 2;
    if (length < 20 || length > track->vos_len)
        return AVERROR_INVALIDDATA;

    // Only TrueHD is supported
    if (AV_RB32(track->vos_data + 4) != 0xF8726FBA)
        return AVERROR_INVALIDDATA;

    avio_wb32(pb, AV_RB32(track->vos_data + 8)); /* format_info */
    avio_wb16(pb, AV_RB16(track->vos_data + 18) << 1); /* peak_data_rate */
    avio_wb32(pb, 0); /* reserved */

    return update_size(pb, pos);
}

static int mov_write_SA3D_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    const AVDictionaryEntry *str = av_dict_get(track->st->metadata, "SA3D", NULL, 0);
    AVChannelLayout ch_layout = { 0 };
    int64_t pos;
    int ambisonic_order, ambi_channels, non_diegetic_channels;
    int i, ret;

    if (!str)
        return 0;

    ret = av_channel_layout_from_string(&ch_layout, str->value);
    if (ret < 0) {
        if (ret == AVERROR(EINVAL)) {
invalid:
            av_log(s, AV_LOG_ERROR, "Invalid SA3D layout: \"%s\"\n", str->value);
            ret = 0;
        }
        av_channel_layout_uninit(&ch_layout);
        return ret;
    }

    if (track->st->codecpar->ch_layout.nb_channels != ch_layout.nb_channels)
        goto invalid;

    ambisonic_order = av_channel_layout_ambisonic_order(&ch_layout);
    if (ambisonic_order < 0)
        goto invalid;

    ambi_channels = (ambisonic_order + 1LL) * (ambisonic_order + 1LL);
    non_diegetic_channels = ch_layout.nb_channels - ambi_channels;
    if (non_diegetic_channels &&
        (non_diegetic_channels != 2 ||
         av_channel_layout_subset(&ch_layout, AV_CH_LAYOUT_STEREO) != AV_CH_LAYOUT_STEREO))
        goto invalid;

    av_log(s, AV_LOG_VERBOSE, "Inserting SA3D box with layout: \"%s\"\n", str->value);

    pos = avio_tell(pb);

    avio_wb32(pb, 0); // Size
    ffio_wfourcc(pb, "SA3D");
    avio_w8(pb, 0); // version
    avio_w8(pb, (!!non_diegetic_channels) << 7); // head_locked_stereo and ambisonic_type
    avio_wb32(pb, ambisonic_order); // ambisonic_order
    avio_w8(pb, 0); // ambisonic_channel_ordering
    avio_w8(pb, 0); // ambisonic_normalization
    avio_wb32(pb, ch_layout.nb_channels); // num_channels
    for (i = 0; i < ambi_channels; i++)
        avio_wb32(pb, av_channel_layout_channel_from_index(&ch_layout, i) - AV_CHAN_AMBISONIC_BASE);
    for (; i < ch_layout.nb_channels; i++)
        avio_wb32(pb, av_channel_layout_channel_from_index(&ch_layout, i) + ambi_channels);

    av_channel_layout_uninit(&ch_layout);

    return update_size(pb, pos);
}

static int mov_write_chan_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    uint32_t layout_tag, bitmap, *channel_desc;
    int64_t pos = avio_tell(pb);
    int num_desc, ret;

    if (track->multichannel_as_mono)
        return 0;

    ret = ff_mov_get_channel_layout_tag(track->par, &layout_tag,
                                        &bitmap, &channel_desc);

    if (ret < 0) {
        if (ret == AVERROR(ENOSYS)) {
            av_log(s, AV_LOG_WARNING, "not writing 'chan' tag due to "
                                      "lack of channel information\n");
            ret = 0;
        }

        return ret;
    }

    if (layout_tag == MOV_CH_LAYOUT_MONO && track->mono_as_fc > 0) {
        av_assert0(!channel_desc);
        channel_desc = av_malloc(sizeof(*channel_desc));
        if (!channel_desc)
            return AVERROR(ENOMEM);

        layout_tag = 0;
        bitmap = 0;
        *channel_desc = 3; // channel label "Center"
    }

    num_desc = layout_tag ? 0 : track->par->ch_layout.nb_channels;

    avio_wb32(pb, 0);           // Size
    ffio_wfourcc(pb, "chan");   // Type
    avio_w8(pb, 0);             // Version
    avio_wb24(pb, 0);           // Flags
    avio_wb32(pb, layout_tag);  // mChannelLayoutTag
    avio_wb32(pb, bitmap);      // mChannelBitmap
    avio_wb32(pb, num_desc);    // mNumberChannelDescriptions

    for (int i = 0; i < num_desc; i++) {
        avio_wb32(pb, channel_desc[i]); // mChannelLabel
        avio_wb32(pb, 0);               // mChannelFlags
        avio_wl32(pb, 0);               // mCoordinates[0]
        avio_wl32(pb, 0);               // mCoordinates[1]
        avio_wl32(pb, 0);               // mCoordinates[2]
    }

    av_free(channel_desc);

    return update_size(pb, pos);
}

static int mov_write_wave_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);     /* size */
    ffio_wfourcc(pb, "wave");

    if (track->par->codec_id != AV_CODEC_ID_QDM2) {
        avio_wb32(pb, 12);    /* size */
        ffio_wfourcc(pb, "frma");
        avio_wl32(pb, track->tag);
    }

    if (track->par->codec_id == AV_CODEC_ID_AAC) {
        /* useless atom needed by mplayer, ipod, not needed by quicktime */
        avio_wb32(pb, 12); /* size */
        ffio_wfourcc(pb, "mp4a");
        avio_wb32(pb, 0);
        mov_write_esds_tag(pb, track);
    } else if (mov_pcm_le_gt16(track->par->codec_id))  {
        mov_write_enda_tag(pb);
    } else if (mov_pcm_be_gt16(track->par->codec_id))  {
        mov_write_enda_tag_be(pb);
    } else if (track->par->codec_id == AV_CODEC_ID_AMR_NB) {
        mov_write_amr_tag(pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_AC3) {
        mov_write_ac3_tag(s, pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_EAC3) {
        mov_write_eac3_tag(s, pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_ALAC ||
               track->par->codec_id == AV_CODEC_ID_QDM2) {
        mov_write_extradata_tag(pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_ADPCM_MS ||
               track->par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV) {
        mov_write_ms_tag(s, pb, track);
    }

    avio_wb32(pb, 8);     /* size */
    avio_wb32(pb, 0);     /* null tag */

    return update_size(pb, pos);
}

static int mov_write_dvc1_structs(MOVTrack *track, uint8_t *buf)
{
    uint8_t *unescaped;
    const uint8_t *start, *next, *end = track->vos_data + track->vos_len;
    int unescaped_size, seq_found = 0;
    int level = 0, interlace = 0;
    int packet_seq   = track->vc1_info.packet_seq;
    int packet_entry = track->vc1_info.packet_entry;
    int slices       = track->vc1_info.slices;
    PutBitContext pbc;

    if (track->start_dts == AV_NOPTS_VALUE) {
        /* No packets written yet, vc1_info isn't authoritative yet. */
        /* Assume inline sequence and entry headers. */
        packet_seq = packet_entry = 1;
        av_log(NULL, AV_LOG_WARNING,
               "moov atom written before any packets, unable to write correct "
               "dvc1 atom. Set the delay_moov flag to fix this.\n");
    }

    unescaped = av_mallocz(track->vos_len + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!unescaped)
        return AVERROR(ENOMEM);
    start = find_next_marker(track->vos_data, end);
    for (next = start; next < end; start = next) {
        GetBitContext gb;
        int size;
        next = find_next_marker(start + 4, end);
        size = next - start - 4;
        if (size <= 0)
            continue;
        unescaped_size = vc1_unescape_buffer(start + 4, size, unescaped);
        init_get_bits(&gb, unescaped, 8 * unescaped_size);
        if (AV_RB32(start) == VC1_CODE_SEQHDR) {
            int profile = get_bits(&gb, 2);
            if (profile != PROFILE_ADVANCED) {
                av_free(unescaped);
                return AVERROR(ENOSYS);
            }
            seq_found = 1;
            level = get_bits(&gb, 3);
            /* chromaformat, frmrtq_postproc, bitrtq_postproc, postprocflag,
             * width, height */
            skip_bits_long(&gb, 2 + 3 + 5 + 1 + 2*12);
            skip_bits(&gb, 1); /* broadcast */
            interlace = get_bits1(&gb);
            skip_bits(&gb, 4); /* tfcntrflag, finterpflag, reserved, psf */
        }
    }
    if (!seq_found) {
        av_free(unescaped);
        return AVERROR(ENOSYS);
    }

    init_put_bits(&pbc, buf, 7);
    /* VC1DecSpecStruc */
    put_bits(&pbc, 4, 12); /* profile - advanced */
    put_bits(&pbc, 3, level);
    put_bits(&pbc, 1, 0); /* reserved */
    /* VC1AdvDecSpecStruc */
    put_bits(&pbc, 3, level);
    put_bits(&pbc, 1, 0); /* cbr */
    put_bits(&pbc, 6, 0); /* reserved */
    put_bits(&pbc, 1, !interlace); /* no interlace */
    put_bits(&pbc, 1, !packet_seq); /* no multiple seq */
    put_bits(&pbc, 1, !packet_entry); /* no multiple entry */
    put_bits(&pbc, 1, !slices); /* no slice code */
    put_bits(&pbc, 1, 0); /* no bframe */
    put_bits(&pbc, 1, 0); /* reserved */

    /* framerate */
    if (track->st->avg_frame_rate.num > 0 && track->st->avg_frame_rate.den > 0)
        put_bits32(&pbc, track->st->avg_frame_rate.num / track->st->avg_frame_rate.den);
    else
        put_bits32(&pbc, 0xffffffff);

    flush_put_bits(&pbc);

    av_free(unescaped);

    return 0;
}

static int mov_write_dvc1_tag(AVIOContext *pb, MOVTrack *track)
{
    uint8_t buf[7] = { 0 };
    int ret;

    if ((ret = mov_write_dvc1_structs(track, buf)) < 0)
        return ret;

    avio_wb32(pb, track->vos_len + 8 + sizeof(buf));
    ffio_wfourcc(pb, "dvc1");
    avio_write(pb, buf, sizeof(buf));
    avio_write(pb, track->vos_data, track->vos_len);

    return 0;
}

static int mov_write_glbl_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, track->vos_len + 8);
    ffio_wfourcc(pb, "glbl");
    avio_write(pb, track->vos_data, track->vos_len);
    return 8 + track->vos_len;
}

/**
 * Compute flags for 'lpcm' tag.
 * See CoreAudioTypes and AudioStreamBasicDescription at Apple.
 */
static int mov_get_lpcm_flags(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F64BE:
        return 11;
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F64LE:
        return 9;
    case AV_CODEC_ID_PCM_U8:
        return 10;
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S32BE:
        return 14;
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S32LE:
        return 12;
    default:
        return 0;
    }
}

static int get_cluster_duration(MOVTrack *track, int cluster_idx)
{
    int64_t next_dts;

    if (cluster_idx >= track->entry)
        return 0;

    if (cluster_idx + 1 == track->entry)
        next_dts = track->track_duration + track->start_dts;
    else
        next_dts = track->cluster[cluster_idx + 1].dts;

    next_dts -= track->cluster[cluster_idx].dts;

    av_assert0(next_dts >= 0);
    av_assert0(next_dts <= INT_MAX);

    return next_dts;
}

static int get_samples_per_packet(MOVTrack *track)
{
    int i, first_duration;

    /* use 1 for raw PCM */
    if (!track->audio_vbr)
        return 1;

    /* check to see if duration is constant for all clusters */
    if (!track->entry)
        return 0;
    first_duration = get_cluster_duration(track, 0);
    for (i = 1; i < track->entry; i++) {
        if (get_cluster_duration(track, i) != first_duration)
            return 0;
    }
    return first_duration;
}

static int mov_write_btrt_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    struct mpeg4_bit_rate_values bit_rates = calculate_mpeg4_bit_rates(track);
    if (!bit_rates.max_bit_rate && !bit_rates.avg_bit_rate &&
        !bit_rates.buffer_size)
        // no useful data to be written, skip
        return 0;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "btrt");

    avio_wb32(pb, bit_rates.buffer_size);
    avio_wb32(pb, bit_rates.max_bit_rate);
    avio_wb32(pb, bit_rates.avg_bit_rate);

    return update_size(pb, pos);
}

static int mov_write_chnl_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int config = 0;
    int ret;
    uint8_t *speaker_pos = NULL;
    const AVChannelLayout *layout = &track->par->ch_layout;

    ret = ff_mov_get_channel_config_from_layout(layout, &config);
    if (ret || !config) {
        config = 0;
        speaker_pos = av_malloc(layout->nb_channels);
        if (!speaker_pos)
            return AVERROR(ENOMEM);
        ret = ff_mov_get_channel_positions_from_layout(layout,
                speaker_pos, layout->nb_channels);
        if (ret) {
            char buf[128] = {0};

            av_freep(&speaker_pos);
            av_channel_layout_describe(layout, buf, sizeof(buf));
            av_log(s, AV_LOG_ERROR, "unsupported channel layout %s\n", buf);
            return ret;
        }
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "chnl");
    avio_wb32(pb, 0); /* version & flags */

    avio_w8(pb, 1); /* stream_structure */
    avio_w8(pb, config);
    if (config) {
        avio_wb64(pb, 0);
    } else {
        avio_write(pb, speaker_pos, layout->nb_channels);
        av_freep(&speaker_pos);
    }

    return update_size(pb, pos);
}

static int mov_write_pcmc_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int format_flags;
    int sample_size;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "pcmC");
    avio_wb32(pb, 0); /* version & flags */

    /* 0x01: indicates little-endian format */
    format_flags = (track->par->codec_id == AV_CODEC_ID_PCM_F32LE ||
                    track->par->codec_id == AV_CODEC_ID_PCM_F64LE ||
                    track->par->codec_id == AV_CODEC_ID_PCM_S16LE ||
                    track->par->codec_id == AV_CODEC_ID_PCM_S24LE ||
                    track->par->codec_id == AV_CODEC_ID_PCM_S32LE);
    avio_w8(pb, format_flags);
    sample_size = track->par->bits_per_raw_sample;
    if (!sample_size)
        sample_size = av_get_exact_bits_per_sample(track->par->codec_id);
    av_assert0(sample_size);
    avio_w8(pb, sample_size);

    return update_size(pb, pos);
}

static int mov_write_audio_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int version = 0;
    uint32_t tag = track->tag;
    int ret = 0;

    if (track->mode == MODE_MOV) {
        if (track->timescale > UINT16_MAX || !track->par->ch_layout.nb_channels) {
            if (mov_get_lpcm_flags(track->par->codec_id))
                tag = AV_RL32("lpcm");
            version = 2;
        } else if (track->audio_vbr || mov_pcm_le_gt16(track->par->codec_id) ||
                   mov_pcm_be_gt16(track->par->codec_id) ||
                   track->par->codec_id == AV_CODEC_ID_ADPCM_MS ||
                   track->par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
                   track->par->codec_id == AV_CODEC_ID_QDM2) {
            version = 1;
        }
    }

    avio_wb32(pb, 0); /* size */
    if (mov->encryption_scheme != MOV_ENC_NONE) {
        ffio_wfourcc(pb, "enca");
    } else {
        avio_wl32(pb, tag); // store it byteswapped
    }
    avio_wb32(pb, 0); /* Reserved */
    avio_wb16(pb, 0); /* Reserved */
    avio_wb16(pb, 1); /* Data-reference index, XXX  == 1 */

    /* SoundDescription */
    avio_wb16(pb, version); /* Version */
    avio_wb16(pb, 0); /* Revision level */
    avio_wb32(pb, 0); /* Reserved */

    if (version == 2) {
        avio_wb16(pb, 3);
        avio_wb16(pb, 16);
        avio_wb16(pb, 0xfffe);
        avio_wb16(pb, 0);
        avio_wb32(pb, 0x00010000);
        avio_wb32(pb, 72);
        avio_wb64(pb, av_double2int(track->par->sample_rate));
        avio_wb32(pb, track->par->ch_layout.nb_channels);
        avio_wb32(pb, 0x7F000000);
        avio_wb32(pb, av_get_bits_per_sample(track->par->codec_id));
        avio_wb32(pb, mov_get_lpcm_flags(track->par->codec_id));
        avio_wb32(pb, track->sample_size);
        avio_wb32(pb, get_samples_per_packet(track));
    } else {
        if (track->mode == MODE_MOV) {
            avio_wb16(pb, track->par->ch_layout.nb_channels);
            if (track->par->codec_id == AV_CODEC_ID_PCM_U8 ||
                track->par->codec_id == AV_CODEC_ID_PCM_S8)
                avio_wb16(pb, 8); /* bits per sample */
            else if (track->par->codec_id == AV_CODEC_ID_ADPCM_G726)
                avio_wb16(pb, track->par->bits_per_coded_sample);
            else
                avio_wb16(pb, 16);
            avio_wb16(pb, track->audio_vbr ? -2 : 0); /* compression ID */
        } else { /* reserved for mp4/3gp */
            avio_wb16(pb, track->tag == MKTAG('i', 'a', 'm', 'f') ?
                      0 : track->par->ch_layout.nb_channels);
            if (track->par->codec_id == AV_CODEC_ID_FLAC ||
                track->par->codec_id == AV_CODEC_ID_ALAC) {
                avio_wb16(pb, track->par->bits_per_raw_sample);
            } else {
                avio_wb16(pb, 16);
            }
            avio_wb16(pb, 0);
        }

        avio_wb16(pb, 0); /* packet size (= 0) */
        if (track->tag == MKTAG('i','a','m','f'))
            avio_wb16(pb, 0); /* samplerate must be 0 for IAMF */
        else if (track->par->codec_id == AV_CODEC_ID_OPUS)
            avio_wb16(pb, 48000);
        else if (track->par->codec_id == AV_CODEC_ID_TRUEHD)
            avio_wb32(pb, track->par->sample_rate);
        else
            avio_wb16(pb, track->par->sample_rate <= UINT16_MAX ?
                          track->par->sample_rate : 0);

        if (track->par->codec_id != AV_CODEC_ID_TRUEHD)
            avio_wb16(pb, 0); /* Reserved */
    }

    if (version == 1) { /* SoundDescription V1 extended info */
        if (mov_pcm_le_gt16(track->par->codec_id) ||
            mov_pcm_be_gt16(track->par->codec_id))
            avio_wb32(pb, 1); /*  must be 1 for  uncompressed formats */
        else
            avio_wb32(pb, track->par->frame_size); /* Samples per packet */
        avio_wb32(pb, track->sample_size / track->par->ch_layout.nb_channels); /* Bytes per packet */
        avio_wb32(pb, track->sample_size); /* Bytes per frame */
        avio_wb32(pb, 2); /* Bytes per sample */
    }

    if (track->mode == MODE_MOV &&
        (track->par->codec_id == AV_CODEC_ID_AAC           ||
         track->par->codec_id == AV_CODEC_ID_AC3           ||
         track->par->codec_id == AV_CODEC_ID_EAC3          ||
         track->par->codec_id == AV_CODEC_ID_AMR_NB        ||
         track->par->codec_id == AV_CODEC_ID_ALAC          ||
         track->par->codec_id == AV_CODEC_ID_ADPCM_MS      ||
         track->par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
         track->par->codec_id == AV_CODEC_ID_QDM2          ||
         (mov_pcm_le_gt16(track->par->codec_id) && version==1) ||
         (mov_pcm_be_gt16(track->par->codec_id) && version==1)))
        ret = mov_write_wave_tag(s, pb, track);
    else if (track->tag == MKTAG('m','p','4','a'))
        ret = mov_write_esds_tag(pb, track);
#if CONFIG_IAMFENC
    else if (track->tag == MKTAG('i','a','m','f'))
        ret = mov_write_iacb_tag(mov->fc, pb, track);
#endif
    else if (track->par->codec_id == AV_CODEC_ID_AMR_NB)
        ret = mov_write_amr_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_AC3)
        ret = mov_write_ac3_tag(s, pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_EAC3)
        ret = mov_write_eac3_tag(s, pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_ALAC)
        ret = mov_write_extradata_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_WMAPRO)
        ret = mov_write_wfex_tag(s, pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_FLAC)
        ret = mov_write_dfla_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_OPUS)
        ret = mov_write_dops_tag(s, pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_TRUEHD)
        ret = mov_write_dmlp_tag(s, pb, track);
    else if (tag == MOV_MP4_IPCM_TAG || tag == MOV_MP4_FPCM_TAG) {
        if (track->par->ch_layout.nb_channels > 1)
            ret = mov_write_chnl_tag(s, pb, track);
        if (ret < 0)
            return ret;
        ret = mov_write_pcmc_tag(s, pb, track);
    } else if (track->vos_len > 0)
        ret = mov_write_glbl_tag(pb, track);

    if (ret < 0)
        return ret;

    if (track->mode == MODE_MP4 && track->par->codec_type == AVMEDIA_TYPE_AUDIO
            && ((ret = mov_write_SA3D_tag(s, pb, track)) < 0)) {
        return ret;
    }

    if (track->mode == MODE_MOV && track->par->codec_type == AVMEDIA_TYPE_AUDIO
            && ((ret = mov_write_chan_tag(s, pb, track)) < 0)) {
        return ret;
    }

    if (mov->encryption_scheme != MOV_ENC_NONE
            && ((ret = ff_mov_cenc_write_sinf_tag(track, pb, mov->encryption_kid)) < 0)) {
        return ret;
    }

    if (mov->write_btrt &&
            ((ret = mov_write_btrt_tag(pb, track)) < 0))
        return ret;

    ret = update_size(pb, pos);
    return ret;
}

static int mov_write_d263_tag(AVIOContext *pb)
{
    avio_wb32(pb, 0xf); /* size */
    ffio_wfourcc(pb, "d263");
    ffio_wfourcc(pb, "FFMP");
    avio_w8(pb, 0); /* decoder version */
    /* FIXME use AVCodecContext level/profile, when encoder will set values */
    avio_w8(pb, 0xa); /* level */
    avio_w8(pb, 0); /* profile */
    return 0xf;
}

static int mov_write_av1c_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "av1C");
    ff_isom_write_av1c(pb, track->vos_data, track->vos_len, track->mode != MODE_AVIF);
    return update_size(pb, pos);
}

static int mov_write_avcc_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "avcC");
    ff_isom_write_avcc(pb, track->vos_data, track->vos_len);
    return update_size(pb, pos);
}

/* AVS3 Intelligent Media Coding
 * Information Technology - Intelligent Media Coding
 * Part 6: Intelligent Media Format
 */
static int mov_write_av3c(AVIOContext *pb, const uint8_t *data, int len)
{
    if (len < 4)
        return AVERROR_INVALIDDATA;

    if (data[0] == 1) {
        // In Avs3DecoderConfigurationRecord format
        avio_write(pb, data, len);
        return 0;
    }

    avio_w8(pb, 1);             // version
    avio_wb16(pb, len);         // sequence_header_length
    avio_write(pb, data, len);  // sequence_header
    avio_w8(pb, 0xFC);          // Only support library_dependency_idc = 0

    return 0;
}

static int mov_write_av3c_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "av3c");
    mov_write_av3c(pb, track->vos_data, track->vos_len);
    return update_size(pb, pos);
}

static int mov_write_vpcc_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "vpcC");
    ff_isom_write_vpcc(s, pb, track->vos_data, track->vos_len, track->par);
    return update_size(pb, pos);
}

static int mov_write_hvcc_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "hvcC");
    if (track->tag == MKTAG('h','v','c','1'))
        ff_isom_write_hvcc(pb, track->vos_data, track->vos_len, 1, s);
    else
        ff_isom_write_hvcc(pb, track->vos_data, track->vos_len, 0, s);
    return update_size(pb, pos);
}

static int mov_write_lhvc_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int ret;

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "lhvC");
    if (track->tag == MKTAG('h','v','c','1'))
        ret = ff_isom_write_lhvc(pb, track->vos_data, track->vos_len, 1, s);
    else
        ret = ff_isom_write_lhvc(pb, track->vos_data, track->vos_len, 0, s);

    if (ret < 0) {
        avio_seek(pb, pos, SEEK_SET);
        return ret;
    }

    return update_size(pb, pos);
}

static int mov_write_evcc_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "evcC");

    if (track->tag == MKTAG('e','v','c','1'))
        ff_isom_write_evcc(pb, track->vos_data, track->vos_len, 1);
    else
        ff_isom_write_evcc(pb, track->vos_data, track->vos_len, 0);

    return update_size(pb, pos);
}

static int mov_write_vvcc_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "vvcC");

    avio_w8  (pb, 0); /* version */
    avio_wb24(pb, 0); /* flags */

    if (track->tag == MKTAG('v','v','c','1'))
        ff_isom_write_vvcc(pb, track->vos_data, track->vos_len, 1);
    else
        ff_isom_write_vvcc(pb, track->vos_data, track->vos_len, 0);
    return update_size(pb, pos);
}

/* also used by all avid codecs (dv, imx, meridien) and their variants */
static int mov_write_avid_tag(AVIOContext *pb, MOVTrack *track)
{
    int interlaced;
    int cid;
    int display_width = track->par->width;

    if (track->vos_data && track->vos_len > 0x29) {
        if (ff_dnxhd_parse_header_prefix(track->vos_data) != 0) {
            /* looks like a DNxHD bit stream */
            interlaced = (track->vos_data[5] & 2);
            cid = AV_RB32(track->vos_data + 0x28);
        } else {
            av_log(NULL, AV_LOG_WARNING, "Could not locate DNxHD bit stream in vos_data\n");
            return 0;
        }
    } else {
        av_log(NULL, AV_LOG_WARNING, "Could not locate DNxHD bit stream, vos_data too small\n");
        return 0;
    }

    avio_wb32(pb, 24); /* size */
    ffio_wfourcc(pb, "ACLR");
    ffio_wfourcc(pb, "ACLR");
    ffio_wfourcc(pb, "0001");
    if (track->par->color_range == AVCOL_RANGE_MPEG || /* Legal range (16-235) */
        track->par->color_range == AVCOL_RANGE_UNSPECIFIED) {
        avio_wb32(pb, 1); /* Corresponds to 709 in official encoder */
    } else { /* Full range (0-255) */
        avio_wb32(pb, 2); /* Corresponds to RGB in official encoder */
    }
    avio_wb32(pb, 0); /* unknown */

    if (track->tag == MKTAG('A','V','d','h')) {
        avio_wb32(pb, 32);
        ffio_wfourcc(pb, "ADHR");
        ffio_wfourcc(pb, "0001");
        avio_wb32(pb, cid);
        avio_wb32(pb, 0); /* unknown */
        avio_wb32(pb, 1); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        return 0;
    }

    avio_wb32(pb, 24); /* size */
    ffio_wfourcc(pb, "APRG");
    ffio_wfourcc(pb, "APRG");
    ffio_wfourcc(pb, "0001");
    avio_wb32(pb, 1); /* unknown */
    avio_wb32(pb, 0); /* unknown */

    avio_wb32(pb, 120); /* size */
    ffio_wfourcc(pb, "ARES");
    ffio_wfourcc(pb, "ARES");
    ffio_wfourcc(pb, "0001");
    avio_wb32(pb, cid); /* dnxhd cid, some id ? */
    if (   track->par->sample_aspect_ratio.num > 0
        && track->par->sample_aspect_ratio.den > 0)
        display_width = display_width * track->par->sample_aspect_ratio.num / track->par->sample_aspect_ratio.den;
    avio_wb32(pb, display_width);
    /* values below are based on samples created with quicktime and avid codecs */
    if (interlaced) {
        avio_wb32(pb, track->par->height / 2);
        avio_wb32(pb, 2); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        avio_wb32(pb, 4); /* unknown */
    } else {
        avio_wb32(pb, track->par->height);
        avio_wb32(pb, 1); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        if (track->par->height == 1080)
            avio_wb32(pb, 5); /* unknown */
        else
            avio_wb32(pb, 6); /* unknown */
    }
    /* padding */
    ffio_fill(pb, 0, 10 * 8);

    return 0;
}

static int mov_write_dpxe_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, 12);
    ffio_wfourcc(pb, "DpxE");
    if (track->par->extradata_size >= 12 &&
        !memcmp(&track->par->extradata[4], "DpxE", 4)) {
        avio_wb32(pb, track->par->extradata[11]);
    } else {
        avio_wb32(pb, 1);
    }
    return 0;
}

static int mov_get_dv_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag;

    if (track->par->width == 720) { /* SD */
        if (track->par->height == 480) { /* NTSC */
            if  (track->par->format == AV_PIX_FMT_YUV422P) tag = MKTAG('d','v','5','n');
            else                                            tag = MKTAG('d','v','c',' ');
       }else if (track->par->format == AV_PIX_FMT_YUV422P) tag = MKTAG('d','v','5','p');
        else if (track->par->format == AV_PIX_FMT_YUV420P) tag = MKTAG('d','v','c','p');
        else                                                tag = MKTAG('d','v','p','p');
    } else if (track->par->height == 720) { /* HD 720 line */
        if  (track->st->time_base.den == 50)                tag = MKTAG('d','v','h','q');
        else                                                tag = MKTAG('d','v','h','p');
    } else if (track->par->height == 1080) { /* HD 1080 line */
        if  (track->st->time_base.den == 25)                tag = MKTAG('d','v','h','5');
        else                                                tag = MKTAG('d','v','h','6');
    } else {
        av_log(s, AV_LOG_ERROR, "unsupported height for dv codec\n");
        return 0;
    }

    return tag;
}

static int defined_frame_rate(AVFormatContext *s, AVStream *st)
{
    AVRational rational_framerate = st->avg_frame_rate;
    int rate = 0;
    if (rational_framerate.den != 0)
        rate = av_q2d(rational_framerate);
    return rate;
}

static int mov_get_mpeg2_xdcam_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->par->codec_tag;
    int interlaced = track->par->field_order > AV_FIELD_PROGRESSIVE;
    AVStream *st = track->st;
    int rate = defined_frame_rate(s, st);

    if (!tag)
        tag = MKTAG('m', '2', 'v', '1'); //fallback tag

    if (track->par->format == AV_PIX_FMT_YUV420P) {
        if (track->par->width == 1280 && track->par->height == 720) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('x','d','v','4');
                else if (rate == 25) tag = MKTAG('x','d','v','5');
                else if (rate == 30) tag = MKTAG('x','d','v','1');
                else if (rate == 50) tag = MKTAG('x','d','v','a');
                else if (rate == 60) tag = MKTAG('x','d','v','9');
            }
        } else if (track->par->width == 1440 && track->par->height == 1080) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('x','d','v','6');
                else if (rate == 25) tag = MKTAG('x','d','v','7');
                else if (rate == 30) tag = MKTAG('x','d','v','8');
            } else {
                if      (rate == 25) tag = MKTAG('x','d','v','3');
                else if (rate == 30) tag = MKTAG('x','d','v','2');
            }
        } else if (track->par->width == 1920 && track->par->height == 1080) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('x','d','v','d');
                else if (rate == 25) tag = MKTAG('x','d','v','e');
                else if (rate == 30) tag = MKTAG('x','d','v','f');
            } else {
                if      (rate == 25) tag = MKTAG('x','d','v','c');
                else if (rate == 30) tag = MKTAG('x','d','v','b');
            }
        }
    } else if (track->par->format == AV_PIX_FMT_YUV422P) {
        if (track->par->width == 1280 && track->par->height == 720) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('x','d','5','4');
                else if (rate == 25) tag = MKTAG('x','d','5','5');
                else if (rate == 30) tag = MKTAG('x','d','5','1');
                else if (rate == 50) tag = MKTAG('x','d','5','a');
                else if (rate == 60) tag = MKTAG('x','d','5','9');
            }
        } else if (track->par->width == 1920 && track->par->height == 1080) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('x','d','5','d');
                else if (rate == 25) tag = MKTAG('x','d','5','e');
                else if (rate == 30) tag = MKTAG('x','d','5','f');
            } else {
                if      (rate == 25) tag = MKTAG('x','d','5','c');
                else if (rate == 30) tag = MKTAG('x','d','5','b');
            }
        }
    }

    return tag;
}

static int mov_get_h264_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->par->codec_tag;
    int interlaced = track->par->field_order > AV_FIELD_PROGRESSIVE;
    AVStream *st = track->st;
    int rate = defined_frame_rate(s, st);

    if (!tag)
        tag = MKTAG('a', 'v', 'c', 'i'); //fallback tag

    if (track->par->format == AV_PIX_FMT_YUV420P10) {
        if (track->par->width == 960 && track->par->height == 720) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('a','i','5','p');
                else if (rate == 25) tag = MKTAG('a','i','5','q');
                else if (rate == 30) tag = MKTAG('a','i','5','p');
                else if (rate == 50) tag = MKTAG('a','i','5','q');
                else if (rate == 60) tag = MKTAG('a','i','5','p');
            }
        } else if (track->par->width == 1440 && track->par->height == 1080) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('a','i','5','3');
                else if (rate == 25) tag = MKTAG('a','i','5','2');
                else if (rate == 30) tag = MKTAG('a','i','5','3');
            } else {
                if      (rate == 50) tag = MKTAG('a','i','5','5');
                else if (rate == 60) tag = MKTAG('a','i','5','6');
            }
        }
    } else if (track->par->format == AV_PIX_FMT_YUV422P10) {
        if (track->par->width == 1280 && track->par->height == 720) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('a','i','1','p');
                else if (rate == 25) tag = MKTAG('a','i','1','q');
                else if (rate == 30) tag = MKTAG('a','i','1','p');
                else if (rate == 50) tag = MKTAG('a','i','1','q');
                else if (rate == 60) tag = MKTAG('a','i','1','p');
            }
        } else if (track->par->width == 1920 && track->par->height == 1080) {
            if (!interlaced) {
                if      (rate == 24) tag = MKTAG('a','i','1','3');
                else if (rate == 25) tag = MKTAG('a','i','1','2');
                else if (rate == 30) tag = MKTAG('a','i','1','3');
            } else {
                if      (rate == 25) tag = MKTAG('a','i','1','5');
                else if (rate == 50) tag = MKTAG('a','i','1','5');
                else if (rate == 60) tag = MKTAG('a','i','1','6');
            }
        } else if (   track->par->width == 4096 && track->par->height == 2160
                   || track->par->width == 3840 && track->par->height == 2160
                   || track->par->width == 2048 && track->par->height == 1080) {
            tag = MKTAG('a','i','v','x');
        }
    }

    return tag;
}

static int mov_get_evc_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->par->codec_tag;

    if (!tag)
        tag = MKTAG('e', 'v', 'c', '1');

    return tag;
}

static const struct {
    enum AVPixelFormat pix_fmt;
    uint32_t tag;
    unsigned bps;
} mov_pix_fmt_tags[] = {
    { AV_PIX_FMT_YUYV422, MKTAG('y','u','v','2'),  0 },
    { AV_PIX_FMT_YUYV422, MKTAG('y','u','v','s'),  0 },
    { AV_PIX_FMT_UYVY422, MKTAG('2','v','u','y'),  0 },
    { AV_PIX_FMT_VYU444,  MKTAG('v','3','0','8'),  0 },
    { AV_PIX_FMT_UYVA,    MKTAG('v','4','0','8'),  0 },
    { AV_PIX_FMT_V30XLE,  MKTAG('v','4','1','0'),  0 },
    { AV_PIX_FMT_RGB555BE,MKTAG('r','a','w',' '), 16 },
    { AV_PIX_FMT_RGB555LE,MKTAG('L','5','5','5'), 16 },
    { AV_PIX_FMT_RGB565LE,MKTAG('L','5','6','5'), 16 },
    { AV_PIX_FMT_RGB565BE,MKTAG('B','5','6','5'), 16 },
    { AV_PIX_FMT_GRAY16BE,MKTAG('b','1','6','g'), 16 },
    { AV_PIX_FMT_RGB24,   MKTAG('r','a','w',' '), 24 },
    { AV_PIX_FMT_BGR24,   MKTAG('2','4','B','G'), 24 },
    { AV_PIX_FMT_ARGB,    MKTAG('r','a','w',' '), 32 },
    { AV_PIX_FMT_BGRA,    MKTAG('B','G','R','A'), 32 },
    { AV_PIX_FMT_RGBA,    MKTAG('R','G','B','A'), 32 },
    { AV_PIX_FMT_ABGR,    MKTAG('A','B','G','R'), 32 },
    { AV_PIX_FMT_RGB48BE, MKTAG('b','4','8','r'), 48 },
};

static int mov_get_dnxhd_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = MKTAG('A','V','d','n');
    if (track->par->profile != AV_PROFILE_UNKNOWN &&
        track->par->profile != AV_PROFILE_DNXHD)
        tag = MKTAG('A','V','d','h');
    return tag;
}

static int mov_get_rawvideo_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->par->codec_tag;
    int i;
    enum AVPixelFormat pix_fmt;

    for (i = 0; i < FF_ARRAY_ELEMS(mov_pix_fmt_tags); i++) {
        if (track->par->format == mov_pix_fmt_tags[i].pix_fmt) {
            tag = mov_pix_fmt_tags[i].tag;
            track->par->bits_per_coded_sample = mov_pix_fmt_tags[i].bps;
            if (track->par->codec_tag == mov_pix_fmt_tags[i].tag)
                break;
        }
    }

    pix_fmt = avpriv_pix_fmt_find(PIX_FMT_LIST_MOV,
                                  track->par->bits_per_coded_sample);
    if (tag == MKTAG('r','a','w',' ') &&
        track->par->format != pix_fmt &&
        track->par->format != AV_PIX_FMT_GRAY8 &&
        track->par->format != AV_PIX_FMT_NONE)
        av_log(s, AV_LOG_ERROR, "%s rawvideo cannot be written to mov, output file will be unreadable\n",
               av_get_pix_fmt_name(track->par->format));
    return tag;
}

static unsigned int mov_get_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    unsigned int tag = track->par->codec_tag;

    // "rtp " is used to distinguish internally created RTP-hint tracks
    // (with rtp_ctx) from other tracks.
    if (tag == MKTAG('r','t','p',' '))
        tag = 0;
    if (!tag || (s->strict_std_compliance >= FF_COMPLIANCE_NORMAL &&
                 (track->par->codec_id == AV_CODEC_ID_DVVIDEO ||
                  track->par->codec_id == AV_CODEC_ID_RAWVIDEO ||
                  track->par->codec_id == AV_CODEC_ID_H263 ||
                  track->par->codec_id == AV_CODEC_ID_H264 ||
                  track->par->codec_id == AV_CODEC_ID_DNXHD ||
                  track->par->codec_id == AV_CODEC_ID_MPEG2VIDEO ||
                  av_get_bits_per_sample(track->par->codec_id)))) { // pcm audio
        if (track->par->codec_id == AV_CODEC_ID_DVVIDEO)
            tag = mov_get_dv_codec_tag(s, track);
        else if (track->par->codec_id == AV_CODEC_ID_RAWVIDEO)
            tag = mov_get_rawvideo_codec_tag(s, track);
        else if (track->par->codec_id == AV_CODEC_ID_MPEG2VIDEO)
            tag = mov_get_mpeg2_xdcam_codec_tag(s, track);
        else if (track->par->codec_id == AV_CODEC_ID_H264)
            tag = mov_get_h264_codec_tag(s, track);
        else if (track->par->codec_id == AV_CODEC_ID_EVC)
            tag = mov_get_evc_codec_tag(s, track);
        else if (track->par->codec_id == AV_CODEC_ID_DNXHD)
            tag = mov_get_dnxhd_codec_tag(s, track);
        else if (track->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            tag = ff_codec_get_tag(ff_codec_movvideo_tags, track->par->codec_id);
            if (!tag) { // if no mac fcc found, try with Microsoft tags
                tag = ff_codec_get_tag(ff_codec_bmp_tags, track->par->codec_id);
                if (tag)
                    av_log(s, AV_LOG_WARNING, "Using MS style video codec tag, "
                           "the file may be unplayable!\n");
            }
        } else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO) {
            tag = ff_codec_get_tag(ff_codec_movaudio_tags, track->par->codec_id);
            if (!tag) { // if no mac fcc found, try with Microsoft tags
                int ms_tag = ff_codec_get_tag(ff_codec_wav_tags, track->par->codec_id);
                if (ms_tag) {
                    tag = MKTAG('m', 's', ((ms_tag >> 8) & 0xff), (ms_tag & 0xff));
                    av_log(s, AV_LOG_WARNING, "Using MS style audio codec tag, "
                           "the file may be unplayable!\n");
                }
            }
        } else if (track->par->codec_type == AVMEDIA_TYPE_SUBTITLE)
            tag = ff_codec_get_tag(ff_codec_movsubtitle_tags, track->par->codec_id);
    }

    return tag;
}

static const AVCodecTag codec_cover_image_tags[] = {
    { AV_CODEC_ID_MJPEG,  0xD },
    { AV_CODEC_ID_PNG,    0xE },
    { AV_CODEC_ID_BMP,    0x1B },
    { AV_CODEC_ID_NONE, 0 },
};

static unsigned int validate_codec_tag(const AVCodecTag *const *tags,
                                       unsigned int tag, int codec_id)
{
    int i;

    /**
     * Check that tag + id is in the table
     */
    for (i = 0; tags && tags[i]; i++) {
        const AVCodecTag *codec_tags = tags[i];
        while (codec_tags->id != AV_CODEC_ID_NONE) {
            if (ff_toupper4(codec_tags->tag) == ff_toupper4(tag) &&
                codec_tags->id == codec_id)
                return codec_tags->tag;
            codec_tags++;
        }
    }
    return 0;
}

static unsigned int mov_find_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    if (is_cover_image(track->st))
        return ff_codec_get_tag(codec_cover_image_tags, track->par->codec_id);

    if (track->mode == MODE_IPOD)
        if (!av_match_ext(s->url, "m4a") &&
            !av_match_ext(s->url, "m4v") &&
            !av_match_ext(s->url, "m4b"))
            av_log(s, AV_LOG_WARNING, "Warning, extension is not .m4a nor .m4v "
                   "Quicktime/Ipod might not play the file\n");

    if (track->mode == MODE_MOV) {
        return mov_get_codec_tag(s, track);
    } else
        return validate_codec_tag(s->oformat->codec_tag, track->par->codec_tag,
                                  track->par->codec_id);
}

/** Write uuid atom.
 * Needed to make file play in iPods running newest firmware
 * goes after avcC atom in moov.trak.mdia.minf.stbl.stsd.avc1
 */
static int mov_write_uuid_tag_ipod(AVIOContext *pb)
{
    avio_wb32(pb, 28);
    ffio_wfourcc(pb, "uuid");
    avio_wb32(pb, 0x6b6840f2);
    avio_wb32(pb, 0x5f244fc5);
    avio_wb32(pb, 0xba39a51b);
    avio_wb32(pb, 0xcf0323f3);
    avio_wb32(pb, 0x0);
    return 28;
}

static const uint16_t fiel_data[] = {
    0x0000, 0x0100, 0x0201, 0x0206, 0x0209, 0x020e
};

static int mov_write_fiel_tag(AVIOContext *pb, MOVTrack *track, int field_order)
{
    unsigned mov_field_order = 0;
    if (field_order < FF_ARRAY_ELEMS(fiel_data))
        mov_field_order = fiel_data[field_order];
    else
        return 0;
    avio_wb32(pb, 10);
    ffio_wfourcc(pb, "fiel");
    avio_wb16(pb, mov_field_order);
    return 10;
}

static int mov_write_subtitle_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    MOVMuxContext *mov = s->priv_data;
    int ret = AVERROR_BUG;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);    /* size */
    avio_wl32(pb, track->tag); // store it byteswapped
    avio_wb32(pb, 0);    /* Reserved */
    avio_wb16(pb, 0);    /* Reserved */
    avio_wb16(pb, 1);    /* Data-reference index */

    if (track->par->codec_id == AV_CODEC_ID_DVD_SUBTITLE)
        mov_write_esds_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_TTML) {
        switch (track->par->codec_tag) {
        case MOV_ISMV_TTML_TAG:
            // ISMV dfxp requires no extradata.
            break;
        case MOV_MP4_TTML_TAG:
            // As specified in 14496-30, XMLSubtitleSampleEntry
            // Namespace
            avio_put_str(pb, "http://www.w3.org/ns/ttml");
            // Empty schema_location
            avio_w8(pb, 0);
            // Empty auxiliary_mime_types
            avio_w8(pb, 0);
            break;
        default:
            av_log(NULL, AV_LOG_ERROR,
                   "Unknown codec tag '%s' utilized for TTML stream with "
                   "index %d (track id %d)!\n",
                   av_fourcc2str(track->par->codec_tag), track->st->index,
                   track->track_id);
            return AVERROR(EINVAL);
        }
    } else if (track->par->extradata_size)
        avio_write(pb, track->par->extradata, track->par->extradata_size);

    if (mov->write_btrt &&
            ((ret = mov_write_btrt_tag(pb, track)) < 0))
        return ret;

    return update_size(pb, pos);
}

static int mov_write_st3d_tag(AVFormatContext *s, AVIOContext *pb, AVStereo3D *stereo_3d)
{
    int8_t stereo_mode;

    if (stereo_3d->flags != 0) {
        av_log(s, AV_LOG_WARNING, "Unsupported stereo_3d flags %x. st3d not written.\n", stereo_3d->flags);
        return 0;
    }

    switch (stereo_3d->type) {
    case AV_STEREO3D_2D:
        stereo_mode = 0;
        break;
    case AV_STEREO3D_TOPBOTTOM:
        stereo_mode = 1;
        break;
    case AV_STEREO3D_SIDEBYSIDE:
        stereo_mode = 2;
        break;
    default:
        av_log(s, AV_LOG_WARNING, "Unsupported stereo_3d type %s. st3d not written.\n", av_stereo3d_type_name(stereo_3d->type));
        return 0;
    }
    avio_wb32(pb, 13); /* size */
    ffio_wfourcc(pb, "st3d");
    avio_wb32(pb, 0); /* version = 0 & flags = 0 */
    avio_w8(pb, stereo_mode);
    return 13;
}

static int mov_write_sv3d_tag(AVFormatContext *s, AVIOContext *pb, AVSphericalMapping *spherical_mapping)
{
    int64_t sv3d_pos, svhd_pos, proj_pos;
    const char* metadata_source = s->flags & AVFMT_FLAG_BITEXACT ? "Lavf" : LIBAVFORMAT_IDENT;

    if (spherical_mapping->projection != AV_SPHERICAL_EQUIRECTANGULAR &&
        spherical_mapping->projection != AV_SPHERICAL_EQUIRECTANGULAR_TILE &&
        spherical_mapping->projection != AV_SPHERICAL_CUBEMAP) {
        av_log(s, AV_LOG_WARNING, "Unsupported projection %d. sv3d not written.\n", spherical_mapping->projection);
        return 0;
    }

    sv3d_pos = avio_tell(pb);
    avio_wb32(pb, 0);  /* size */
    ffio_wfourcc(pb, "sv3d");

    svhd_pos = avio_tell(pb);
    avio_wb32(pb, 0);  /* size */
    ffio_wfourcc(pb, "svhd");
    avio_wb32(pb, 0); /* version = 0 & flags = 0 */
    avio_put_str(pb, metadata_source);
    update_size(pb, svhd_pos);

    proj_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "proj");

    avio_wb32(pb, 24); /* size */
    ffio_wfourcc(pb, "prhd");
    avio_wb32(pb, 0); /* version = 0 & flags = 0 */
    avio_wb32(pb, spherical_mapping->yaw);
    avio_wb32(pb, spherical_mapping->pitch);
    avio_wb32(pb, spherical_mapping->roll);

    switch (spherical_mapping->projection) {
    case AV_SPHERICAL_EQUIRECTANGULAR:
    case AV_SPHERICAL_EQUIRECTANGULAR_TILE:
        avio_wb32(pb, 28);    /* size */
        ffio_wfourcc(pb, "equi");
        avio_wb32(pb, 0); /* version = 0 & flags = 0 */
        avio_wb32(pb, spherical_mapping->bound_top);
        avio_wb32(pb, spherical_mapping->bound_bottom);
        avio_wb32(pb, spherical_mapping->bound_left);
        avio_wb32(pb, spherical_mapping->bound_right);
        break;
    case AV_SPHERICAL_CUBEMAP:
        avio_wb32(pb, 20);    /* size */
        ffio_wfourcc(pb, "cbmp");
        avio_wb32(pb, 0); /* version = 0 & flags = 0 */
        avio_wb32(pb, 0); /* layout */
        avio_wb32(pb, spherical_mapping->padding); /* padding */
        break;
    }
    update_size(pb, proj_pos);

    return update_size(pb, sv3d_pos);
}

static inline int64_t rescale_rational(AVRational q, int b)
{
    return av_rescale(q.num, b, q.den);
}

static void mov_write_hfov_tag(AVFormatContext *s, AVIOContext *pb,
                              const AVStereo3D *stereo3d)
{
    if (!stereo3d->horizontal_field_of_view.num)
        return;

    avio_wb32(pb, 12); /* size */
    ffio_wfourcc(pb, "hfov");
    avio_wb32(pb, rescale_rational(stereo3d->horizontal_field_of_view, 1000));
}

static void mov_write_vexu_proj_tag(AVFormatContext *s, AVIOContext *pb,
                                    const AVSphericalMapping *spherical_mapping)
{
    avio_wb32(pb, 24); /* size */
    ffio_wfourcc(pb, "proj");
    avio_wb32(pb, 16); /* size */
    ffio_wfourcc(pb, "prji");
    avio_wb32(pb, 0); /* version + flags */

    switch (spherical_mapping->projection) {
    case AV_SPHERICAL_RECTILINEAR:
        ffio_wfourcc(pb, "rect");
        break;
    case AV_SPHERICAL_EQUIRECTANGULAR:
        ffio_wfourcc(pb, "equi");
        break;
    case AV_SPHERICAL_HALF_EQUIRECTANGULAR:
        ffio_wfourcc(pb, "hequ");
        break;
    case AV_SPHERICAL_FISHEYE:
        ffio_wfourcc(pb, "fish");
        break;
    default:
        av_assert0(0);
    }
}

static int mov_write_eyes_tag(AVFormatContext *s, AVIOContext *pb,
                               const AVStereo3D *stereo3d)
{
    int64_t pos = avio_tell(pb);
    int view = 0;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "eyes");

    // stri is mandatory
    avio_wb32(pb, 13); /* size */
    ffio_wfourcc(pb, "stri");
    avio_wb32(pb, 0); /* version + flags */
    switch (stereo3d->view) {
    case AV_STEREO3D_VIEW_LEFT:
        view |= 1 << 0;
        break;
    case AV_STEREO3D_VIEW_RIGHT:
        view |= 1 << 1;
        break;
    case AV_STEREO3D_VIEW_PACKED:
        view |= (1 << 0) | (1 << 1);
        break;
    }
    view |= !!(stereo3d->flags & AV_STEREO3D_FLAG_INVERT) << 3;
    avio_w8(pb, view);

    // hero is optional
    if (stereo3d->primary_eye != AV_PRIMARY_EYE_NONE) {
        avio_wb32(pb, 13); /* size */
        ffio_wfourcc(pb, "hero");
        avio_wb32(pb, 0); /* version + flags */
        avio_w8(pb, stereo3d->primary_eye);
    }

    // it's not clear if cams is mandatory or optional
    if (stereo3d->baseline) {
        avio_wb32(pb, 24); /* size */
        ffio_wfourcc(pb, "cams");
        avio_wb32(pb, 16); /* size */
        ffio_wfourcc(pb, "blin");
        avio_wb32(pb, 0); /* version + flags */
        avio_wb32(pb, stereo3d->baseline);
    }

    // it's not clear if cmfy is mandatory or optional
    if (stereo3d->horizontal_disparity_adjustment.num) {
        avio_wb32(pb, 24); /* size */
        ffio_wfourcc(pb, "cmfy");
        avio_wb32(pb, 16); /* size */
        ffio_wfourcc(pb, "dadj");
        avio_wb32(pb, 0); /* version + flags */
        avio_wb32(pb, rescale_rational(stereo3d->horizontal_disparity_adjustment, 10000));
    }

    return update_size(pb, pos);
}

static int mov_write_vexu_tag(AVFormatContext *s, AVIOContext *pb,
                              const AVStereo3D *stereo3d,
                              const AVSphericalMapping *spherical_mapping)
{
    int64_t pos;

    if (spherical_mapping &&
        spherical_mapping->projection != AV_SPHERICAL_RECTILINEAR &&
        spherical_mapping->projection != AV_SPHERICAL_EQUIRECTANGULAR &&
        spherical_mapping->projection != AV_SPHERICAL_HALF_EQUIRECTANGULAR &&
        spherical_mapping->projection != AV_SPHERICAL_FISHEYE) {
        av_log(s, AV_LOG_WARNING, "Unsupported projection %d. proj not written.\n",
               spherical_mapping->projection);
        spherical_mapping = NULL;
    }

    if (stereo3d && (stereo3d->type == AV_STEREO3D_2D ||
        (!(stereo3d->flags & AV_STEREO3D_FLAG_INVERT) &&
           stereo3d->view == AV_STEREO3D_VIEW_UNSPEC &&
           stereo3d->primary_eye == AV_PRIMARY_EYE_NONE &&
          !stereo3d->baseline &&
          !stereo3d->horizontal_disparity_adjustment.num))) {
        av_log(s, AV_LOG_WARNING, "Unsupported stereo 3d metadata. eyes not written.\n");
        stereo3d = NULL;
    }

    if (!spherical_mapping && !stereo3d)
        return 0;

    pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "vexu");

    if (spherical_mapping)
        mov_write_vexu_proj_tag(s, pb, spherical_mapping);

    if (stereo3d)
        mov_write_eyes_tag(s, pb, stereo3d);

    return update_size(pb, pos);
}

static int mov_write_dvcc_dvvc_tag(AVFormatContext *s, AVIOContext *pb, AVDOVIDecoderConfigurationRecord *dovi)
{
    uint8_t buf[ISOM_DVCC_DVVC_SIZE];

    avio_wb32(pb, 32); /* size = 8 + 24 */
    if (dovi->dv_profile > 10)
        ffio_wfourcc(pb, "dvwC");
    else if (dovi->dv_profile > 7)
        ffio_wfourcc(pb, "dvvC");
    else
        ffio_wfourcc(pb, "dvcC");

    ff_isom_put_dvcc_dvvc(s, buf, dovi);
    avio_write(pb, buf, sizeof(buf));

    return 32; /* 8 + 24 */
}

static int mov_write_clap_tag(AVIOContext *pb, MOVTrack *track,
                              uint32_t top, uint32_t bottom,
                              uint32_t left, uint32_t right)
{
    uint32_t cropped_width  = track->par->width - left - right;
    uint32_t cropped_height = track->height - top - bottom;
    AVRational horizOff =
        av_sub_q((AVRational) { track->par->width - cropped_width, 2 },
                 (AVRational) { left, 1 });
    AVRational vertOff =
        av_sub_q((AVRational) { track->height - cropped_height, 2 },
                 (AVRational) { top, 1 });

    avio_wb32(pb, 40);
    ffio_wfourcc(pb, "clap");
    avio_wb32(pb, cropped_width); /* apertureWidthN */
    avio_wb32(pb, 1); /* apertureWidthD */
    avio_wb32(pb, cropped_height); /* apertureHeightN */
    avio_wb32(pb, 1); /* apertureHeightD */

    avio_wb32(pb, -horizOff.num);
    avio_wb32(pb, horizOff.den);
    avio_wb32(pb, -vertOff.num);
    avio_wb32(pb, vertOff.den);

    return 40;
}

static int mov_write_pasp_tag(AVIOContext *pb, MOVTrack *track)
{
    AVRational sar;
    av_reduce(&sar.num, &sar.den, track->par->sample_aspect_ratio.num,
              track->par->sample_aspect_ratio.den, INT_MAX);

    avio_wb32(pb, 16);
    ffio_wfourcc(pb, "pasp");
    avio_wb32(pb, sar.num);
    avio_wb32(pb, sar.den);
    return 16;
}

static int mov_write_gama_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track, double gamma)
{
    uint32_t gama = 0;
    if (gamma <= 0.0)
        gamma = av_csp_approximate_trc_gamma(track->par->color_trc);
    av_log(s, AV_LOG_DEBUG, "gamma value %g\n", gamma);

    if (gamma > 1e-6) {
        gama = (uint32_t)lrint((double)(1<<16) * gamma);
        av_log(s, AV_LOG_DEBUG, "writing gama value %"PRId32"\n", gama);

        av_assert0(track->mode == MODE_MOV);
        avio_wb32(pb, 12);
        ffio_wfourcc(pb, "gama");
        avio_wb32(pb, gama);
        return 12;
    } else {
        av_log(s, AV_LOG_WARNING, "gamma value unknown, unable to write gama atom\n");
    }
    return 0;
}

static int mov_write_colr_tag(AVIOContext *pb, MOVTrack *track, int prefer_icc)
{
    int64_t pos = avio_tell(pb);

    // Ref (MOV): https://developer.apple.com/library/mac/technotes/tn2162/_index.html#//apple_ref/doc/uid/DTS40013070-CH1-TNTAG9
    // Ref (MP4): ISO/IEC 14496-12:2012

    if (prefer_icc) {
        const AVPacketSideData *sd = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                                             track->st->codecpar->nb_coded_side_data,
                                                             AV_PKT_DATA_ICC_PROFILE);

        if (sd) {
            avio_wb32(pb, 12 + sd->size);
            ffio_wfourcc(pb, "colr");
            ffio_wfourcc(pb, "prof");
            avio_write(pb, sd->data, sd->size);
            return 12 + sd->size;
        }
        else {
            av_log(NULL, AV_LOG_INFO, "no ICC profile found, will write nclx/nclc colour info instead\n");
        }
    }

    /* We should only ever be called for MOV, MP4 and AVIF. */
    av_assert0(track->mode == MODE_MOV || track->mode == MODE_MP4 ||
               track->mode == MODE_AVIF);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "colr");
    if (track->mode == MODE_MP4 || track->mode == MODE_AVIF)
        ffio_wfourcc(pb, "nclx");
    else
        ffio_wfourcc(pb, "nclc");
    // Do not try to guess the color info if it is AVCOL_PRI_UNSPECIFIED.
    // e.g., Dolby Vision for Apple devices should be set to AVCOL_PRI_UNSPECIFIED. See
    // https://developer.apple.com/av-foundation/High-Dynamic-Range-Metadata-for-Apple-Devices.pdf
    avio_wb16(pb, track->par->color_primaries);
    avio_wb16(pb, track->par->color_trc);
    avio_wb16(pb, track->par->color_space);
    if (track->mode == MODE_MP4 || track->mode == MODE_AVIF) {
        int full_range = track->par->color_range == AVCOL_RANGE_JPEG;
        avio_w8(pb, full_range << 7);
    }

    return update_size(pb, pos);
}

static int mov_write_clli_tag(AVIOContext *pb, MOVTrack *track)
{
    const AVPacketSideData *side_data;
    const AVContentLightMetadata *content_light_metadata;

    side_data = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                        track->st->codecpar->nb_coded_side_data,
                                        AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
    if (!side_data) {
        return 0;
    }
    content_light_metadata = (const AVContentLightMetadata*)side_data->data;

    avio_wb32(pb, 12); // size
    ffio_wfourcc(pb, "clli");
    avio_wb16(pb, content_light_metadata->MaxCLL);
    avio_wb16(pb, content_light_metadata->MaxFALL);
    return 12;
}

static int mov_write_mdcv_tag(AVIOContext *pb, MOVTrack *track)
{
    const int chroma_den = 50000;
    const int luma_den = 10000;
    const AVPacketSideData *side_data;
    const AVMasteringDisplayMetadata *metadata = NULL;

    side_data = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                        track->st->codecpar->nb_coded_side_data,
                                        AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
    if (side_data)
        metadata = (const AVMasteringDisplayMetadata*)side_data->data;
    if (!metadata || !metadata->has_primaries || !metadata->has_luminance) {
        return 0;
    }

    avio_wb32(pb, 32); // size
    ffio_wfourcc(pb, "mdcv");
    avio_wb16(pb, rescale_rational(metadata->display_primaries[1][0], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->display_primaries[1][1], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->display_primaries[2][0], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->display_primaries[2][1], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->display_primaries[0][0], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->display_primaries[0][1], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->white_point[0], chroma_den));
    avio_wb16(pb, rescale_rational(metadata->white_point[1], chroma_den));
    avio_wb32(pb, rescale_rational(metadata->max_luminance, luma_den));
    avio_wb32(pb, rescale_rational(metadata->min_luminance, luma_den));
    return 32;
}

static int mov_write_amve_tag(AVIOContext *pb, MOVTrack *track)
{
    const int illuminance_den = 10000;
    const int ambient_den = 50000;
    const AVPacketSideData *side_data;
    const AVAmbientViewingEnvironment *ambient;


    side_data = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                        track->st->codecpar->nb_coded_side_data,
                                        AV_PKT_DATA_AMBIENT_VIEWING_ENVIRONMENT);

    if (!side_data)
        return 0;

    ambient = (const AVAmbientViewingEnvironment*)side_data->data;
    if (!ambient || !ambient->ambient_illuminance.num)
        return 0;

    avio_wb32(pb, 16); // size
    ffio_wfourcc(pb, "amve");
    avio_wb32(pb, rescale_rational(ambient->ambient_illuminance, illuminance_den));
    avio_wb16(pb, rescale_rational(ambient->ambient_light_x, ambient_den));
    avio_wb16(pb, rescale_rational(ambient->ambient_light_y, ambient_den));
    return 16;
}

static void find_compressor(char * compressor_name, int len, MOVTrack *track)
{
    AVDictionaryEntry *encoder;
    int xdcam_res =  (track->par->width == 1280 && track->par->height == 720)
                  || (track->par->width == 1440 && track->par->height == 1080)
                  || (track->par->width == 1920 && track->par->height == 1080);

    if ((track->mode == MODE_AVIF ||
         track->mode == MODE_MOV ||
         track->mode == MODE_MP4) &&
        (encoder = av_dict_get(track->st->metadata, "encoder", NULL, 0))) {
        av_strlcpy(compressor_name, encoder->value, 32);
    } else if (track->par->codec_id == AV_CODEC_ID_MPEG2VIDEO && xdcam_res) {
        int interlaced = track->par->field_order > AV_FIELD_PROGRESSIVE;
        AVStream *st = track->st;
        int rate = defined_frame_rate(NULL, st);
        av_strlcatf(compressor_name, len, "XDCAM");
        if (track->par->format == AV_PIX_FMT_YUV422P) {
            av_strlcatf(compressor_name, len, " HD422");
        } else if(track->par->width == 1440) {
            av_strlcatf(compressor_name, len, " HD");
        } else
            av_strlcatf(compressor_name, len, " EX");

        av_strlcatf(compressor_name, len, " %d%c", track->par->height, interlaced ? 'i' : 'p');

        av_strlcatf(compressor_name, len, "%d", rate * (interlaced + 1));
    }
}

static int mov_write_ccst_tag(AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);
    // Write sane defaults:
    // all_ref_pics_intra = 0 : all samples can use any type of reference.
    // intra_pred_used = 1 : intra prediction may or may not be used.
    // max_ref_per_pic = 15 : reserved value to indicate that any number of
    //                        reference images can be used.
    uint8_t ccstValue = (0 << 7) |  /* all_ref_pics_intra */
                        (1 << 6) |  /* intra_pred_used */
                        (15 << 2);  /* max_ref_per_pic */
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ccst");
    avio_wb32(pb, 0); /* Version & flags */
    avio_w8(pb, ccstValue);
    avio_wb24(pb, 0);  /* reserved */
    return update_size(pb, pos);
}

static int mov_write_aux_tag(AVIOContext *pb, const char *aux_type)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, aux_type);
    avio_wb32(pb, 0); /* Version & flags */
    avio_write(pb, "urn:mpeg:mpegB:cicp:systems:auxiliary:alpha\0", 44);
    return update_size(pb, pos);
}

static int mov_write_video_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov, MOVTrack *track)
{
    int ret = AVERROR_BUG;
    int64_t pos = avio_tell(pb);
    const AVPacketSideData *sd;
    char compressor_name[32] = { 0 };
    int avid = 0;

    int uncompressed_ycbcr = ((track->par->codec_id == AV_CODEC_ID_RAWVIDEO && track->par->format == AV_PIX_FMT_UYVY422)
                           || (track->par->codec_id == AV_CODEC_ID_RAWVIDEO && track->par->format == AV_PIX_FMT_YUYV422)
                           || (track->par->codec_id == AV_CODEC_ID_RAWVIDEO && track->par->format == AV_PIX_FMT_VYU444)
                           || (track->par->codec_id == AV_CODEC_ID_RAWVIDEO && track->par->format == AV_PIX_FMT_UYVA)
                           || (track->par->codec_id == AV_CODEC_ID_RAWVIDEO && track->par->format == AV_PIX_FMT_V30XLE)
#if FF_API_V408_CODECID
                           ||  track->par->codec_id == AV_CODEC_ID_V308
                           ||  track->par->codec_id == AV_CODEC_ID_V408
                           ||  track->par->codec_id == AV_CODEC_ID_V410
#endif
                           ||  track->par->codec_id == AV_CODEC_ID_V210);

    avio_wb32(pb, 0); /* size */
    if (mov->encryption_scheme != MOV_ENC_NONE) {
        ffio_wfourcc(pb, "encv");
    } else {
        avio_wl32(pb, track->tag); // store it byteswapped
    }
    avio_wb32(pb, 0); /* Reserved */
    avio_wb16(pb, 0); /* Reserved */
    avio_wb16(pb, 1); /* Data-reference index */

    if (uncompressed_ycbcr) {
        avio_wb16(pb, 2); /* Codec stream version */
    } else {
        avio_wb16(pb, 0); /* Codec stream version */
    }
    avio_wb16(pb, 0); /* Codec stream revision (=0) */
    if (track->mode == MODE_MOV) {
        ffio_wfourcc(pb, "FFMP"); /* Vendor */
        if (track->par->codec_id == AV_CODEC_ID_RAWVIDEO || uncompressed_ycbcr) {
            avio_wb32(pb, 0); /* Temporal Quality */
            avio_wb32(pb, 0x400); /* Spatial Quality = lossless*/
        } else {
            avio_wb32(pb, 0x200); /* Temporal Quality = normal */
            avio_wb32(pb, 0x200); /* Spatial Quality = normal */
        }
    } else {
        ffio_fill(pb, 0, 3 * 4); /* Reserved */
    }
    avio_wb16(pb, track->par->width); /* Video width */
    avio_wb16(pb, track->height); /* Video height */
    avio_wb32(pb, 0x00480000); /* Horizontal resolution 72dpi */
    avio_wb32(pb, 0x00480000); /* Vertical resolution 72dpi */
    avio_wb32(pb, 0); /* Data size (= 0) */
    avio_wb16(pb, 1); /* Frame count (= 1) */

    find_compressor(compressor_name, 32, track);
    avio_w8(pb, strlen(compressor_name));
    avio_write(pb, compressor_name, 31);

    if (track->mode == MODE_MOV &&
       (track->par->codec_id == AV_CODEC_ID_V410 || track->par->codec_id == AV_CODEC_ID_V210))
        avio_wb16(pb, 0x18);
    else if (track->mode == MODE_MOV && track->par->bits_per_coded_sample)
        avio_wb16(pb, track->par->bits_per_coded_sample |
                  (track->par->format == AV_PIX_FMT_GRAY8 ? 0x20 : 0));
    else
        avio_wb16(pb, 0x18); /* Reserved */

    if (track->mode == MODE_MOV && track->par->format == AV_PIX_FMT_PAL8) {
        int pal_size, i;
        avio_wb16(pb, 0);             /* Color table ID */
        avio_wb32(pb, 0);             /* Color table seed */
        avio_wb16(pb, 0x8000);        /* Color table flags */
        if (track->par->bits_per_coded_sample < 0 || track->par->bits_per_coded_sample > 8)
            return AVERROR(EINVAL);
        pal_size = 1 << track->par->bits_per_coded_sample;
        avio_wb16(pb, pal_size - 1);  /* Color table size (zero-relative) */
        for (i = 0; i < pal_size; i++) {
            uint32_t rgb = track->palette[i];
            uint16_t r = (rgb >> 16) & 0xff;
            uint16_t g = (rgb >> 8)  & 0xff;
            uint16_t b = rgb         & 0xff;
            avio_wb16(pb, 0);
            avio_wb16(pb, (r << 8) | r);
            avio_wb16(pb, (g << 8) | g);
            avio_wb16(pb, (b << 8) | b);
        }
    } else
        avio_wb16(pb, 0xffff); /* Reserved */

    if (track->tag == MKTAG('m','p','4','v'))
        mov_write_esds_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_H263)
        mov_write_d263_tag(pb);
    else if (track->par->codec_id == AV_CODEC_ID_AVUI ||
            track->par->codec_id == AV_CODEC_ID_SVQ3) {
        mov_write_extradata_tag(pb, track);
        avio_wb32(pb, 0);
    } else if (track->par->codec_id == AV_CODEC_ID_DNXHD) {
        mov_write_avid_tag(pb, track);
        avid = 1;
    } else if (track->par->codec_id == AV_CODEC_ID_HEVC) {
        mov_write_hvcc_tag(mov->fc, pb, track);
        if (track->st->disposition & AV_DISPOSITION_MULTILAYER) {
            ret = mov_write_lhvc_tag(mov->fc, pb, track);
            if (ret < 0)
                av_log(mov->fc, AV_LOG_WARNING, "Not writing 'lhvC' atom for multilayer stream.\n");
        }
    } else if (track->par->codec_id == AV_CODEC_ID_VVC)
        mov_write_vvcc_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_H264 && !TAG_IS_AVCI(track->tag)) {
        mov_write_avcc_tag(pb, track);
        if (track->mode == MODE_IPOD)
            mov_write_uuid_tag_ipod(pb);
    }
    else if (track->par->codec_id ==AV_CODEC_ID_EVC) {
        mov_write_evcc_tag(pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_VP9) {
        mov_write_vpcc_tag(mov->fc, pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_AV1) {
        mov_write_av1c_tag(pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_VC1 && track->vos_len > 0)
        mov_write_dvc1_tag(pb, track);
    else if (track->par->codec_id == AV_CODEC_ID_VP6F ||
             track->par->codec_id == AV_CODEC_ID_VP6A) {
        /* Don't write any potential extradata here - the cropping
         * is signalled via the normal width/height fields. */
    } else if (track->par->codec_id == AV_CODEC_ID_R10K) {
        if (track->par->codec_tag == MKTAG('R','1','0','k'))
            mov_write_dpxe_tag(pb, track);
    } else if (track->par->codec_id == AV_CODEC_ID_AVS3) {
        mov_write_av3c_tag(pb, track);
    } else if (track->vos_len > 0)
        mov_write_glbl_tag(pb, track);

    if (track->par->codec_id != AV_CODEC_ID_H264 &&
        track->par->codec_id != AV_CODEC_ID_MPEG4 &&
        track->par->codec_id != AV_CODEC_ID_DNXHD) {
        int field_order = track->par->field_order;

        if (field_order != AV_FIELD_UNKNOWN)
            mov_write_fiel_tag(pb, track, field_order);
    }

    if (mov->flags & FF_MOV_FLAG_WRITE_GAMA) {
        if (track->mode == MODE_MOV)
            mov_write_gama_tag(s, pb, track, mov->gamma);
        else
            av_log(mov->fc, AV_LOG_WARNING, "Not writing 'gama' atom. Format is not MOV.\n");
    }
    if (track->mode == MODE_MOV || track->mode == MODE_MP4 || track->mode == MODE_AVIF) {
        int has_color_info = track->par->color_primaries != AVCOL_PRI_UNSPECIFIED &&
                             track->par->color_trc != AVCOL_TRC_UNSPECIFIED &&
                             track->par->color_space != AVCOL_SPC_UNSPECIFIED;
        if (has_color_info || mov->flags & FF_MOV_FLAG_WRITE_COLR ||
            av_packet_side_data_get(track->st->codecpar->coded_side_data,  track->st->codecpar->nb_coded_side_data,
                                    AV_PKT_DATA_ICC_PROFILE)) {
            int prefer_icc = mov->flags & FF_MOV_FLAG_PREFER_ICC || !has_color_info;
            mov_write_colr_tag(pb, track, prefer_icc);
        }
    } else if (mov->flags & FF_MOV_FLAG_WRITE_COLR) {
        av_log(mov->fc, AV_LOG_WARNING, "Not writing 'colr' atom. Format is not MOV or MP4 or AVIF.\n");
    }

    if (track->mode == MODE_MOV || track->mode == MODE_MP4) {
        mov_write_clli_tag(pb, track);
        mov_write_mdcv_tag(pb, track);
        mov_write_amve_tag(pb, track);
    }

    if (track->mode == MODE_MP4 && mov->fc->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
        const AVPacketSideData *stereo_3d = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                                                    track->st->codecpar->nb_coded_side_data,
                                                                    AV_PKT_DATA_STEREO3D);
        const AVPacketSideData *spherical_mapping = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                                                            track->st->codecpar->nb_coded_side_data,
                                                                            AV_PKT_DATA_SPHERICAL);
        if (stereo_3d)
            mov_write_st3d_tag(s, pb, (AVStereo3D*)stereo_3d->data);
        if (spherical_mapping)
            mov_write_sv3d_tag(mov->fc, pb, (AVSphericalMapping*)spherical_mapping->data);
    }

    if (track->mode == MODE_MOV || (track->mode == MODE_MP4 &&
                                    mov->fc->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)) {
        const AVStereo3D *stereo3d = NULL;
        const AVSphericalMapping *spherical_mapping = NULL;

        sd = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                     track->st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_STEREO3D);
        if (sd)
            stereo3d = (AVStereo3D *)sd->data;

        sd = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                     track->st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_SPHERICAL);
        if (sd)
            spherical_mapping = (AVSphericalMapping *)sd->data;

        if (stereo3d || spherical_mapping)
            mov_write_vexu_tag(s, pb, stereo3d, spherical_mapping);
        if (stereo3d)
            mov_write_hfov_tag(s, pb, stereo3d);
    }

    if (track->mode == MODE_MP4) {
        const AVPacketSideData *dovi = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                                               track->st->codecpar->nb_coded_side_data,
                                                               AV_PKT_DATA_DOVI_CONF);
        if (dovi && mov->fc->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL) {
            mov_write_dvcc_dvvc_tag(s, pb, (AVDOVIDecoderConfigurationRecord *)dovi->data);
        } else if (dovi) {
            av_log(mov->fc, AV_LOG_WARNING, "Not writing 'dvcC'/'dvvC' box. Requires -strict unofficial.\n");
        }
    }

    if (track->par->sample_aspect_ratio.den && track->par->sample_aspect_ratio.num) {
        mov_write_pasp_tag(pb, track);
    }

    sd = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                 track->st->codecpar->nb_coded_side_data,
                                 AV_PKT_DATA_FRAME_CROPPING);
    if (sd && sd->size >= sizeof(uint32_t) * 4) {
        uint64_t top    = AV_RL32(sd->data +  0);
        uint64_t bottom = AV_RL32(sd->data +  4);
        uint64_t left   = AV_RL32(sd->data +  8);
        uint64_t right  = AV_RL32(sd->data + 12);

        if ((left + right) >= track->par->width ||
            (top + bottom) >= track->height) {
            av_log(s, AV_LOG_ERROR, "Invalid cropping dimensions in stream side data\n");
            return AVERROR(EINVAL);
        }
        if (top || bottom || left || right)
            mov_write_clap_tag(pb, track, top, bottom, left, right);
    } else if (uncompressed_ycbcr)
        mov_write_clap_tag(pb, track, 0, 0, 0, 0);

    if (mov->encryption_scheme != MOV_ENC_NONE) {
        ff_mov_cenc_write_sinf_tag(track, pb, mov->encryption_kid);
    }

    if (mov->write_btrt &&
            ((ret = mov_write_btrt_tag(pb, track)) < 0))
        return ret;

    /* extra padding for avid stsd */
    /* https://developer.apple.com/library/mac/documentation/QuickTime/QTFF/QTFFChap2/qtff2.html#//apple_ref/doc/uid/TP40000939-CH204-61112 */
    if (avid)
        avio_wb32(pb, 0);

    if (track->mode == MODE_AVIF) {
        mov_write_ccst_tag(pb);
        if (mov->nb_streams > 0 && track == &mov->tracks[1])
            mov_write_aux_tag(pb, "auxi");
    }

    return update_size(pb, pos);
}

static int mov_write_rtp_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "rtp ");
    avio_wb32(pb, 0); /* Reserved */
    avio_wb16(pb, 0); /* Reserved */
    avio_wb16(pb, 1); /* Data-reference index */

    avio_wb16(pb, 1); /* Hint track version */
    avio_wb16(pb, 1); /* Highest compatible version */
    avio_wb32(pb, track->max_packet_size); /* Max packet size */

    avio_wb32(pb, 12); /* size */
    ffio_wfourcc(pb, "tims");
    avio_wb32(pb, track->timescale);

    return update_size(pb, pos);
}

static int mov_write_source_reference_tag(AVIOContext *pb, MOVTrack *track, const char *reel_name)
{
    uint64_t str_size =strlen(reel_name);
    int64_t pos = avio_tell(pb);

    if (str_size >= UINT16_MAX){
        av_log(NULL, AV_LOG_ERROR, "reel_name length %"PRIu64" is too large\n", str_size);
        avio_wb16(pb, 0);
        return AVERROR(EINVAL);
    }

    avio_wb32(pb, 0);                              /* size */
    ffio_wfourcc(pb, "name");                      /* Data format */
    avio_wb16(pb, str_size);                       /* string size */
    avio_wb16(pb, track->language);                /* langcode */
    avio_write(pb, reel_name, str_size);           /* reel name */
    return update_size(pb,pos);
}

static int mov_write_tmcd_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
#if 1
    int frame_duration;
    int nb_frames;
    AVDictionaryEntry *t = NULL;

    if (!track->st->avg_frame_rate.num || !track->st->avg_frame_rate.den) {
        av_log(NULL, AV_LOG_ERROR, "avg_frame_rate not set for tmcd track.\n");
        return AVERROR(EINVAL);
    } else {
        frame_duration = av_rescale(track->timescale, track->st->avg_frame_rate.den, track->st->avg_frame_rate.num);
        nb_frames      = ROUNDED_DIV(track->st->avg_frame_rate.num, track->st->avg_frame_rate.den);
    }

    if (nb_frames > 255) {
        av_log(NULL, AV_LOG_ERROR, "fps %d is too large\n", nb_frames);
        return AVERROR(EINVAL);
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "tmcd");               /* Data format */
    avio_wb32(pb, 0);                       /* Reserved */
    avio_wb32(pb, 1);                       /* Data reference index */
    avio_wb32(pb, 0);                       /* Flags */
    avio_wb32(pb, track->timecode_flags);   /* Flags (timecode) */
    avio_wb32(pb, track->timescale);        /* Timescale */
    avio_wb32(pb, frame_duration);          /* Frame duration */
    avio_w8(pb, nb_frames);                 /* Number of frames */
    avio_w8(pb, 0);                         /* Reserved */

    t = av_dict_get(track->st->metadata, "reel_name", NULL, 0);
    if (t && utf8len(t->value) && track->mode != MODE_MP4)
        mov_write_source_reference_tag(pb, track, t->value);
    else
        avio_wb16(pb, 0); /* zero size */
#else

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "tmcd");               /* Data format */
    avio_wb32(pb, 0);                       /* Reserved */
    avio_wb32(pb, 1);                       /* Data reference index */
    if (track->par->extradata_size)
        avio_write(pb, track->par->extradata, track->par->extradata_size);
#endif
    return update_size(pb, pos);
}

static int mov_write_gpmd_tag(AVIOContext *pb, const MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "gpmd");
    avio_wb32(pb, 0); /* Reserved */
    avio_wb16(pb, 0); /* Reserved */
    avio_wb16(pb, 1); /* Data-reference index */
    avio_wb32(pb, 0); /* Reserved */
    return update_size(pb, pos);
}

static int mov_write_stsd_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int ret = 0;
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stsd");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, 1); /* entry count */
    if (track->par->codec_type == AVMEDIA_TYPE_VIDEO)
        ret = mov_write_video_tag(s, pb, mov, track);
    else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO)
        ret = mov_write_audio_tag(s, pb, mov, track);
    else if (track->par->codec_type == AVMEDIA_TYPE_SUBTITLE)
        ret = mov_write_subtitle_tag(s, pb, track);
    else if (track->par->codec_tag == MKTAG('r','t','p',' '))
        ret = mov_write_rtp_tag(pb, track);
    else if (track->par->codec_tag == MKTAG('t','m','c','d'))
        ret = mov_write_tmcd_tag(pb, track);
    else if (track->par->codec_tag == MKTAG('g','p','m','d'))
        ret = mov_write_gpmd_tag(pb, track);

    if (ret < 0)
        return ret;

    return update_size(pb, pos);
}

static int mov_write_ctts_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    MOVMuxContext *mov = s->priv_data;
    MOVCtts *ctts_entries;
    uint32_t entries = 0;
    uint32_t atom_size;
    int i;

    ctts_entries = av_malloc_array((track->entry + 1), sizeof(*ctts_entries)); /* worst case */
    if (!ctts_entries)
        return AVERROR(ENOMEM);
    ctts_entries[0].count = 1;
    ctts_entries[0].offset = track->cluster[0].cts;
    for (i = 1; i < track->entry; i++) {
        if (track->cluster[i].cts == ctts_entries[entries].offset) {
            ctts_entries[entries].count++; /* compress */
        } else {
            entries++;
            ctts_entries[entries].offset = track->cluster[i].cts;
            ctts_entries[entries].count = 1;
        }
    }
    entries++; /* last one */
    atom_size = 16 + (entries * 8);
    avio_wb32(pb, atom_size); /* size */
    ffio_wfourcc(pb, "ctts");
    if (mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
        avio_w8(pb, 1); /* version */
    else
        avio_w8(pb, 0); /* version */
    avio_wb24(pb, 0); /* flags */
    avio_wb32(pb, entries); /* entry count */
    for (i = 0; i < entries; i++) {
        avio_wb32(pb, ctts_entries[i].count);
        avio_wb32(pb, ctts_entries[i].offset);
    }
    av_free(ctts_entries);
    return atom_size;
}

/* Time to sample atom */
static int mov_write_stts_tag(AVIOContext *pb, MOVTrack *track)
{
    MOVStts *stts_entries = NULL;
    uint32_t entries = -1;
    uint32_t atom_size;
    int i;

    if (track->par->codec_type == AVMEDIA_TYPE_AUDIO && !track->audio_vbr) {
        stts_entries = av_malloc(sizeof(*stts_entries)); /* one entry */
        if (!stts_entries)
            return AVERROR(ENOMEM);
        stts_entries[0].count = track->sample_count;
        stts_entries[0].duration = 1;
        entries = 1;
    } else {
        if (track->entry) {
            stts_entries = av_malloc_array(track->entry, sizeof(*stts_entries)); /* worst case */
            if (!stts_entries)
                return AVERROR(ENOMEM);
        }
        for (i = 0; i < track->entry; i++) {
            int duration = get_cluster_duration(track, i);
#if CONFIG_IAMFENC
            if (track->iamf && track->par->codec_id == AV_CODEC_ID_OPUS)
                duration = av_rescale(duration, 48000, track->par->sample_rate);
#endif
            if (i && duration == stts_entries[entries].duration) {
                stts_entries[entries].count++; /* compress */
            } else {
                entries++;
                stts_entries[entries].duration = duration;
                stts_entries[entries].count = 1;
            }
        }
        entries++; /* last one */
    }
    atom_size = 16 + (entries * 8);
    avio_wb32(pb, atom_size); /* size */
    ffio_wfourcc(pb, "stts");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, entries); /* entry count */
    for (i = 0; i < entries; i++) {
        avio_wb32(pb, stts_entries[i].count);
        avio_wb32(pb, stts_entries[i].duration);
    }
    av_free(stts_entries);
    return atom_size;
}

static int mov_write_dref_tag(AVIOContext *pb)
{
    avio_wb32(pb, 28); /* size */
    ffio_wfourcc(pb, "dref");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, 1); /* entry count */

    avio_wb32(pb, 0xc); /* size */
    //FIXME add the alis and rsrc atom
    ffio_wfourcc(pb, "url ");
    avio_wb32(pb, 1); /* version & flags */

    return 28;
}

static int mov_preroll_write_stbl_atoms(AVIOContext *pb, MOVTrack *track)
{
    struct sgpd_entry {
        int count;
        int16_t roll_distance;
        int group_description_index;
    };

    struct sgpd_entry *sgpd_entries = NULL;
    int entries = -1;
    int group = 0;
    int i, j;

    const int OPUS_SEEK_PREROLL_MS = 80;
    int roll_samples = av_rescale_q(OPUS_SEEK_PREROLL_MS,
                                    (AVRational){1, 1000},
                                    (AVRational){1, 48000});

    if (!track->entry)
        return 0;

    sgpd_entries = av_malloc_array(track->entry, sizeof(*sgpd_entries));
    if (!sgpd_entries)
        return AVERROR(ENOMEM);

    av_assert0(track->par->codec_id == AV_CODEC_ID_OPUS || track->par->codec_id == AV_CODEC_ID_AAC);

    if (track->par->codec_id == AV_CODEC_ID_OPUS) {
        for (i = 0; i < track->entry; i++) {
            int roll_samples_remaining = roll_samples;
            int distance = 0;
            for (j = i - 1; j >= 0; j--) {
                roll_samples_remaining -= get_cluster_duration(track, j);
                distance++;
                if (roll_samples_remaining <= 0)
                    break;
            }
            /* We don't have enough preceeding samples to compute a valid
               roll_distance here, so this sample can't be independently
               decoded. */
            if (roll_samples_remaining > 0)
                distance = 0;
            /* Verify distance is a maximum of 32 (2.5ms) packets. */
            if (distance > 32)
                return AVERROR_INVALIDDATA;
            if (i && distance == sgpd_entries[entries].roll_distance) {
                sgpd_entries[entries].count++;
            } else {
                entries++;
                sgpd_entries[entries].count = 1;
                sgpd_entries[entries].roll_distance = distance;
                sgpd_entries[entries].group_description_index = distance ? ++group : 0;
            }
        }
    } else {
        entries++;
        sgpd_entries[entries].count = track->sample_count;
        sgpd_entries[entries].roll_distance = 1;
        sgpd_entries[entries].group_description_index = ++group;
    }
    entries++;

    if (!group) {
        av_free(sgpd_entries);
        return 0;
    }

    /* Write sgpd tag */
    avio_wb32(pb, 24 + (group * 2)); /* size */
    ffio_wfourcc(pb, "sgpd");
    avio_wb32(pb, 1 << 24); /* fullbox */
    ffio_wfourcc(pb, "roll");
    avio_wb32(pb, 2); /* default_length */
    avio_wb32(pb, group); /* entry_count */
    for (i = 0; i < entries; i++) {
        if (sgpd_entries[i].group_description_index) {
            avio_wb16(pb, -sgpd_entries[i].roll_distance); /* roll_distance */
        }
    }

    /* Write sbgp tag */
    avio_wb32(pb, 20 + (entries * 8)); /* size */
    ffio_wfourcc(pb, "sbgp");
    avio_wb32(pb, 0); /* fullbox */
    ffio_wfourcc(pb, "roll");
    avio_wb32(pb, entries); /* entry_count */
    for (i = 0; i < entries; i++) {
        avio_wb32(pb, sgpd_entries[i].count); /* sample_count */
        avio_wb32(pb, sgpd_entries[i].group_description_index); /* group_description_index */
    }

    av_free(sgpd_entries);
    return 0;
}

static int mov_write_stbl_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int ret = 0;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stbl");
    if ((ret = mov_write_stsd_tag(s, pb, mov, track)) < 0)
        return ret;
    mov_write_stts_tag(pb, track);
    if ((track->par->codec_type == AVMEDIA_TYPE_VIDEO ||
         track->par->codec_id == AV_CODEC_ID_TRUEHD ||
         track->par->codec_id == AV_CODEC_ID_MPEGH_3D_AUDIO ||
         (track->par->codec_id == AV_CODEC_ID_AAC && track->par->profile == AV_PROFILE_AAC_USAC) ||
         track->par->codec_tag == MKTAG('r','t','p',' ')) &&
        track->has_keyframes && track->has_keyframes < track->entry)
        mov_write_stss_tag(pb, track, MOV_SYNC_SAMPLE);
    if (track->par->codec_type == AVMEDIA_TYPE_VIDEO && track->has_disposable)
        mov_write_sdtp_tag(pb, track);
    if (track->mode == MODE_MOV && track->flags & MOV_TRACK_STPS)
        mov_write_stss_tag(pb, track, MOV_PARTIAL_SYNC_SAMPLE);
    if (track->par->codec_type == AVMEDIA_TYPE_VIDEO &&
        track->flags & MOV_TRACK_CTTS && track->entry) {

        if ((ret = mov_write_ctts_tag(s, pb, track)) < 0)
            return ret;
    }
    mov_write_stsc_tag(pb, track);
    mov_write_stsz_tag(pb, track);
    mov_write_stco_tag(pb, track);
    if (track->cenc.aes_ctr && !(mov->flags & FF_MOV_FLAG_FRAGMENT)) {
        ff_mov_cenc_write_stbl_atoms(&track->cenc, pb, 0);
    }
    if (track->par->codec_id == AV_CODEC_ID_OPUS || track->par->codec_id == AV_CODEC_ID_AAC) {
        mov_preroll_write_stbl_atoms(pb, track);
    }
    return update_size(pb, pos);
}

static int mov_write_dinf_tag(AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "dinf");
    mov_write_dref_tag(pb);
    return update_size(pb, pos);
}

static int mov_write_nmhd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 12);
    ffio_wfourcc(pb, "nmhd");
    avio_wb32(pb, 0);
    return 12;
}

static int mov_write_sthd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 12);
    ffio_wfourcc(pb, "sthd");
    avio_wb32(pb, 0);
    return 12;
}

static int mov_write_tcmi_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    const char *font = "Lucida Grande";
    avio_wb32(pb, 0);                   /* size */
    ffio_wfourcc(pb, "tcmi");           /* timecode media information atom */
    avio_wb32(pb, 0);                   /* version & flags */
    avio_wb16(pb, 0);                   /* text font */
    avio_wb16(pb, 0);                   /* text face */
    avio_wb16(pb, 12);                  /* text size */
    avio_wb16(pb, 0);                   /* (unknown, not in the QT specs...) */
    avio_wb16(pb, 0x0000);              /* text color (red) */
    avio_wb16(pb, 0x0000);              /* text color (green) */
    avio_wb16(pb, 0x0000);              /* text color (blue) */
    avio_wb16(pb, 0xffff);              /* background color (red) */
    avio_wb16(pb, 0xffff);              /* background color (green) */
    avio_wb16(pb, 0xffff);              /* background color (blue) */
    avio_w8(pb, strlen(font));          /* font len (part of the pascal string) */
    avio_write(pb, font, strlen(font)); /* font name */
    return update_size(pb, pos);
}

static int mov_write_gmhd_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);      /* size */
    ffio_wfourcc(pb, "gmhd");
    avio_wb32(pb, 0x18);   /* gmin size */
    ffio_wfourcc(pb, "gmin");/* generic media info */
    avio_wb32(pb, 0);      /* version & flags */
    avio_wb16(pb, 0x40);   /* graphics mode = */
    avio_wb16(pb, 0x8000); /* opColor (r?) */
    avio_wb16(pb, 0x8000); /* opColor (g?) */
    avio_wb16(pb, 0x8000); /* opColor (b?) */
    avio_wb16(pb, 0);      /* balance */
    avio_wb16(pb, 0);      /* reserved */

    /*
     * This special text atom is required for
     * Apple Quicktime chapters. The contents
     * don't appear to be documented, so the
     * bytes are copied verbatim.
     */
    if (track->tag != MKTAG('c','6','0','8')) {
        avio_wb32(pb, 0x2C);   /* size */
        ffio_wfourcc(pb, "text");
        avio_wb16(pb, 0x01);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x01);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x00);
        avio_wb32(pb, 0x00004000);
        avio_wb16(pb, 0x0000);
    }

    if (track->par->codec_tag == MKTAG('t','m','c','d')) {
        int64_t tmcd_pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        ffio_wfourcc(pb, "tmcd");
        mov_write_tcmi_tag(pb, track);
        update_size(pb, tmcd_pos);
    } else if (track->par->codec_tag == MKTAG('g','p','m','d')) {
        int64_t gpmd_pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        ffio_wfourcc(pb, "gpmd");
        avio_wb32(pb, 0); /* version */
        update_size(pb, gpmd_pos);
    }
    return update_size(pb, pos);
}

static int mov_write_smhd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 16); /* size */
    ffio_wfourcc(pb, "smhd");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb16(pb, 0); /* reserved (balance, normally = 0) */
    avio_wb16(pb, 0); /* reserved */
    return 16;
}

static int mov_write_vmhd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 0x14); /* size (always 0x14) */
    ffio_wfourcc(pb, "vmhd");
    avio_wb32(pb, 0x01); /* version & flags */
    avio_wb64(pb, 0); /* reserved (graphics mode = copy) */
    return 0x14;
}

static int is_clcp_track(MOVTrack *track)
{
    return track->tag == MKTAG('c','7','0','8') ||
           track->tag == MKTAG('c','6','0','8');
}

static int mov_write_hdlr_tag(AVFormatContext *s, AVIOContext *pb, MOVTrack *track)
{
    MOVMuxContext *mov = s->priv_data;
    const char *hdlr, *descr = NULL, *hdlr_type = NULL;
    int64_t pos = avio_tell(pb);
    size_t descr_len;

    hdlr      = "dhlr";
    hdlr_type = "url ";
    descr     = "DataHandler";

    if (track) {
        hdlr = (track->mode == MODE_MOV) ? "mhlr" : "\0\0\0\0";
        if (track->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (track->mode == MODE_AVIF) {
                hdlr_type = (track == &mov->tracks[0]) ? "pict" : "auxv";
                descr     = "PictureHandler";
            } else {
                hdlr_type = "vide";
                descr     = "VideoHandler";
            }
        } else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO) {
            hdlr_type = "soun";
            descr     = "SoundHandler";
        } else if (track->par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (is_clcp_track(track)) {
                hdlr_type = "clcp";
                descr = "ClosedCaptionHandler";
            } else {
                if (track->tag == MKTAG('t','x','3','g')) {
                    hdlr_type = "sbtl";
                } else if (track->tag == MKTAG('m','p','4','s')) {
                    hdlr_type = "subp";
                } else if (track->tag == MOV_MP4_TTML_TAG) {
                    hdlr_type = "subt";
                } else {
                    hdlr_type = "text";
                }
                descr = "SubtitleHandler";
            }
        } else if (track->par->codec_tag == MKTAG('r','t','p',' ')) {
            hdlr_type = "hint";
            descr     = "HintHandler";
        } else if (track->par->codec_tag == MKTAG('t','m','c','d')) {
            hdlr_type = "tmcd";
            descr = "TimeCodeHandler";
        } else if (track->par->codec_tag == MKTAG('g','p','m','d')) {
            hdlr_type = "meta";
            descr = "GoPro MET"; // GoPro Metadata
        } else {
            av_log(s, AV_LOG_WARNING,
                   "Unknown hdlr_type for %s, writing dummy values\n",
                   av_fourcc2str(track->par->codec_tag));
        }
        if (track->st) {
            // hdlr.name is used by some players to identify the content title
            // of the track. So if an alternate handler description is
            // specified, use it.
            AVDictionaryEntry *t;
            t = av_dict_get(track->st->metadata, "handler_name", NULL, 0);
            if (t && utf8len(t->value))
                descr = t->value;
        }
    }

    if (mov->empty_hdlr_name) /* expressly allowed by QTFF and not prohibited in ISO 14496-12 8.4.3.3 */
        descr = "";

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "hdlr");
    avio_wb32(pb, 0); /* Version & flags */
    avio_write(pb, hdlr, 4); /* handler */
    ffio_wfourcc(pb, hdlr_type); /* handler type */
    avio_wb32(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */
    descr_len = strlen(descr);
    if (!track || track->mode == MODE_MOV)
        avio_w8(pb, descr_len); /* pascal string */
    avio_write(pb, descr, descr_len); /* handler description */
    if (track && track->mode != MODE_MOV)
        avio_w8(pb, 0); /* c string */
    return update_size(pb, pos);
}

static int mov_write_pitm_tag(AVIOContext *pb, int item_id)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "pitm");
    avio_wb32(pb, 0); /* Version & flags */
    avio_wb16(pb, item_id); /* item_id */
    return update_size(pb, pos);
}

static int mov_write_iloc_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "iloc");
    avio_wb32(pb, 0); /* Version & flags */
    avio_w8(pb, (4 << 4) + 4); /* offset_size(4) and length_size(4) */
    avio_w8(pb, 0); /* base_offset_size(4) and reserved(4) */
    avio_wb16(pb, mov->nb_streams); /* item_count */

    for (int i = 0; i < mov->nb_streams; i++) {
        avio_wb16(pb, i + 1); /* item_id */
        avio_wb16(pb, 0); /* data_reference_index */
        avio_wb16(pb, 1); /* extent_count */
        mov->avif_extent_pos[i] = avio_tell(pb);
        avio_wb32(pb, 0); /* extent_offset (written later) */
        // For animated AVIF, we simply write the first packet's size.
        avio_wb32(pb, mov->avif_extent_length[i]); /* extent_length */
    }

    return update_size(pb, pos);
}

static int mov_write_iinf_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t iinf_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "iinf");
    avio_wb32(pb, 0); /* Version & flags */
    avio_wb16(pb, mov->nb_streams); /* entry_count */

    for (int i = 0; i < mov->nb_streams; i++) {
        int64_t infe_pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        ffio_wfourcc(pb, "infe");
        avio_w8(pb, 0x2); /* Version */
        avio_wb24(pb, 0); /* flags */
        avio_wb16(pb, i + 1); /* item_id */
        avio_wb16(pb, 0); /* item_protection_index */
        avio_write(pb, "av01", 4); /* item_type */
        avio_write(pb, !i ? "Color\0" : "Alpha\0", 6); /* item_name */
        update_size(pb, infe_pos);
    }

    return update_size(pb, iinf_pos);
}


static int mov_write_iref_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t auxl_pos;
    int64_t iref_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "iref");
    avio_wb32(pb, 0); /* Version & flags */

    auxl_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "auxl");
    avio_wb16(pb, 2); /* from_item_ID */
    avio_wb16(pb, 1); /* reference_count */
    avio_wb16(pb, 1); /* to_item_ID */
    update_size(pb, auxl_pos);

    return update_size(pb, iref_pos);
}

static int mov_write_ispe_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s,
                              int stream_index)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ispe");
    avio_wb32(pb, 0); /* Version & flags */
    avio_wb32(pb, s->streams[stream_index]->codecpar->width); /* image_width */
    avio_wb32(pb, s->streams[stream_index]->codecpar->height); /* image_height */
    return update_size(pb, pos);
}

static int mov_write_pixi_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s,
                              int stream_index)
{
    int64_t pos = avio_tell(pb);
    const AVPixFmtDescriptor *pixdesc =
        av_pix_fmt_desc_get(s->streams[stream_index]->codecpar->format);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "pixi");
    avio_wb32(pb, 0); /* Version & flags */
    avio_w8(pb, pixdesc->nb_components); /* num_channels */
    for (int i = 0; i < pixdesc->nb_components; ++i) {
      avio_w8(pb, pixdesc->comp[i].depth); /* bits_per_channel */
    }
    return update_size(pb, pos);
}

static int mov_write_ipco_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ipco");
    for (int i = 0; i < mov->nb_streams; i++) {
        mov_write_ispe_tag(pb, mov, s, i);
        mov_write_pixi_tag(pb, mov, s, i);
        mov_write_av1c_tag(pb, &mov->tracks[i]);
        if (!i)
            mov_write_colr_tag(pb, &mov->tracks[0], 0);
        else
            mov_write_aux_tag(pb, "auxC");
    }
    return update_size(pb, pos);
}

static int mov_write_ipma_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ipma");
    avio_wb32(pb, 0); /* Version & flags */
    avio_wb32(pb, mov->nb_streams); /* entry_count */

    for (int i = 0, index = 1; i < mov->nb_streams; i++) {
        avio_wb16(pb, i + 1); /* item_ID */
        avio_w8(pb, 4); /* association_count */

        // ispe association.
        avio_w8(pb, index++); /* essential and property_index */
        // pixi association.
        avio_w8(pb, index++); /* essential and property_index */
        // av1C association.
        avio_w8(pb, 0x80 | index++); /* essential and property_index */
        // colr/auxC association.
        avio_w8(pb, index++); /* essential and property_index */
    }
    return update_size(pb, pos);
}

static int mov_write_iprp_tag(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "iprp");
    mov_write_ipco_tag(pb, mov, s);
    mov_write_ipma_tag(pb, mov, s);
    return update_size(pb, pos);
}

static int mov_write_hmhd_tag(AVIOContext *pb)
{
    /* This atom must be present, but leaving the values at zero
     * seems harmless. */
    avio_wb32(pb, 28); /* size */
    ffio_wfourcc(pb, "hmhd");
    avio_wb32(pb, 0); /* version, flags */
    avio_wb16(pb, 0); /* maxPDUsize */
    avio_wb16(pb, 0); /* avgPDUsize */
    avio_wb32(pb, 0); /* maxbitrate */
    avio_wb32(pb, 0); /* avgbitrate */
    avio_wb32(pb, 0); /* reserved */
    return 28;
}

static int mov_write_minf_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int ret;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "minf");
    if (track->par->codec_type == AVMEDIA_TYPE_VIDEO)
        mov_write_vmhd_tag(pb);
    else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO)
        mov_write_smhd_tag(pb);
    else if (track->par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        if (track->tag == MKTAG('t','e','x','t') || is_clcp_track(track)) {
            mov_write_gmhd_tag(pb, track);
        } else if (track->tag == MOV_MP4_TTML_TAG) {
            mov_write_sthd_tag(pb);
        } else {
            mov_write_nmhd_tag(pb);
        }
    } else if (track->tag == MKTAG('r','t','p',' ')) {
        mov_write_hmhd_tag(pb);
    } else if (track->tag == MKTAG('t','m','c','d')) {
        if (track->mode != MODE_MOV)
            mov_write_nmhd_tag(pb);
        else
            mov_write_gmhd_tag(pb, track);
    } else if (track->tag == MKTAG('g','p','m','d')) {
        mov_write_gmhd_tag(pb, track);
    }
    if (track->mode == MODE_MOV) /* ISO 14496-12 8.4.3.1 specifies hdlr only within mdia or meta boxes */
        mov_write_hdlr_tag(s, pb, NULL);
    mov_write_dinf_tag(pb);
    if ((ret = mov_write_stbl_tag(s, pb, mov, track)) < 0)
        return ret;
    return update_size(pb, pos);
}

static void get_pts_range(MOVMuxContext *mov, MOVTrack *track,
                          int64_t *start, int64_t *end)
{
    if (track->tag == MKTAG('t','m','c','d') && mov->nb_meta_tmcd) {
        // tmcd tracks gets track_duration set in mov_write_moov_tag from
        // another track's duration, while the end_pts may be left at zero.
        // Calculate the pts duration for that track instead.
        get_pts_range(mov, &mov->tracks[track->src_track], start, end);
        *start = av_rescale(*start, track->timescale,
                            mov->tracks[track->src_track].timescale);
        *end   = av_rescale(*end, track->timescale,
                            mov->tracks[track->src_track].timescale);
        return;
    }
    if (track->end_pts != AV_NOPTS_VALUE &&
        track->start_dts != AV_NOPTS_VALUE &&
        track->start_cts != AV_NOPTS_VALUE) {
        *start = track->start_dts + track->start_cts;
        *end   = track->end_pts;
        return;
    }
    *start = 0;
    *end   = track->track_duration;
}

static int64_t calc_samples_pts_duration(MOVMuxContext *mov, MOVTrack *track)
{
    int64_t start, end;
    get_pts_range(mov, track, &start, &end);
    return end - start;
}

// Calculate the actual duration of the track, after edits.
// If it starts with a pts < 0, that is removed by the edit list.
// If it starts with a pts > 0, the edit list adds a delay before that.
// Thus, with edit lists enabled, the post-edit output of the file is
// starting with pts=0.
static int64_t calc_pts_duration(MOVMuxContext *mov, MOVTrack *track)
{
    int64_t start, end;
    get_pts_range(mov, track, &start, &end);
    if (mov->use_editlist != 0)
        start = 0;
    return end - start;
}

static int mov_mdhd_mvhd_tkhd_version(MOVMuxContext *mov, MOVTrack *track, int64_t duration)
{
    if (track && track->mode == MODE_ISM)
        return 1;
    if (duration < INT32_MAX)
        return 0;
    return 1;
}

static int mov_write_mdhd_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track)
{
    int64_t duration = calc_samples_pts_duration(mov, track);
    int version = mov_mdhd_mvhd_tkhd_version(mov, track, duration);

    (version == 1) ? avio_wb32(pb, 44) : avio_wb32(pb, 32); /* size */
    ffio_wfourcc(pb, "mdhd");
    avio_w8(pb, version);
    avio_wb24(pb, 0); /* flags */
    if (version == 1) {
        avio_wb64(pb, track->time);
        avio_wb64(pb, track->time);
    } else {
        avio_wb32(pb, track->time); /* creation time */
        avio_wb32(pb, track->time); /* modification time */
    }
    avio_wb32(pb, track->timescale); /* time scale (sample rate for audio) */
    if (!track->entry && mov->mode == MODE_ISM)
        (version == 1) ? avio_wb64(pb, UINT64_C(0xffffffffffffffff)) : avio_wb32(pb, 0xffffffff);
    else if (!track->entry)
        (version == 1) ? avio_wb64(pb, 0) : avio_wb32(pb, 0);
    else
        (version == 1) ? avio_wb64(pb, duration) : avio_wb32(pb, duration); /* duration */
    avio_wb16(pb, track->language); /* language */
    avio_wb16(pb, 0); /* reserved (quality) */

    if (version != 0 && track->mode == MODE_MOV) {
        av_log(NULL, AV_LOG_ERROR,
               "FATAL error, file duration too long for timebase, this file will not be\n"
               "playable with QuickTime. Choose a different timebase with "
               "-video_track_timescale or a different container format\n");
    }

    return 32;
}

static int mov_write_mdia_tag(AVFormatContext *s, AVIOContext *pb,
                              MOVMuxContext *mov, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int ret;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "mdia");
    mov_write_mdhd_tag(pb, mov, track);
    mov_write_hdlr_tag(s, pb, track);
    if ((ret = mov_write_minf_tag(s, pb, mov, track)) < 0)
        return ret;
    return update_size(pb, pos);
}

/* transformation matrix
     |a  b  u|
     |c  d  v|
     |tx ty w| */
static void write_matrix(AVIOContext *pb, int16_t a, int16_t b, int16_t c,
                         int16_t d, int16_t tx, int16_t ty)
{
    avio_wb32(pb, a << 16);  /* 16.16 format */
    avio_wb32(pb, b << 16);  /* 16.16 format */
    avio_wb32(pb, 0);        /* u in 2.30 format */
    avio_wb32(pb, c << 16);  /* 16.16 format */
    avio_wb32(pb, d << 16);  /* 16.16 format */
    avio_wb32(pb, 0);        /* v in 2.30 format */
    avio_wb32(pb, tx << 16); /* 16.16 format */
    avio_wb32(pb, ty << 16); /* 16.16 format */
    avio_wb32(pb, 1 << 30);  /* w in 2.30 format */
}

static int mov_write_tkhd_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, AVStream *st)
{
    int64_t duration = av_rescale_rnd(calc_pts_duration(mov, track),
                                      mov->movie_timescale, track->timescale,
                                      AV_ROUND_UP);
    int version;
    int flags   = MOV_TKHD_FLAG_IN_MOVIE;
    int group   = 0;

    uint32_t *display_matrix = NULL;
    int       i;

    if (mov->mode == MODE_AVIF)
        if (!mov->avif_loop_count)
            duration = INT64_MAX;
        else
            duration *= mov->avif_loop_count;

    if (st) {
        const AVPacketSideData *sd;
        if (mov->per_stream_grouping)
            group = st->index;
        else
            group = st->codecpar->codec_type;

        sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                     st->codecpar->nb_coded_side_data,
                                     AV_PKT_DATA_DISPLAYMATRIX);
        if (sd && sd->size == 9 * sizeof(*display_matrix))
            display_matrix = (uint32_t *)sd->data;
    }

    if (track->flags & MOV_TRACK_ENABLED)
        flags |= MOV_TKHD_FLAG_ENABLED;

    version = mov_mdhd_mvhd_tkhd_version(mov, track, duration);

    (version == 1) ? avio_wb32(pb, 104) : avio_wb32(pb, 92); /* size */
    ffio_wfourcc(pb, "tkhd");
    avio_w8(pb, version);
    avio_wb24(pb, flags);
    if (version == 1) {
        avio_wb64(pb, track->time);
        avio_wb64(pb, track->time);
    } else {
        avio_wb32(pb, track->time); /* creation time */
        avio_wb32(pb, track->time); /* modification time */
    }
    avio_wb32(pb, track->track_id); /* track-id */
    avio_wb32(pb, 0); /* reserved */
    if (!track->entry && mov->mode == MODE_ISM)
        (version == 1) ? avio_wb64(pb, UINT64_C(0xffffffffffffffff)) : avio_wb32(pb, 0xffffffff);
    else if (!track->entry)
        (version == 1) ? avio_wb64(pb, 0) : avio_wb32(pb, 0);
    else
        (version == 1) ? avio_wb64(pb, duration) : avio_wb32(pb, duration);

    avio_wb32(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */
    avio_wb16(pb, 0); /* layer */
    avio_wb16(pb, group); /* alternate group) */
    /* Volume, only for audio */
    if (track->par->codec_type == AVMEDIA_TYPE_AUDIO)
        avio_wb16(pb, 0x0100);
    else
        avio_wb16(pb, 0);
    avio_wb16(pb, 0); /* reserved */

    /* Matrix structure */
    if (display_matrix) {
        for (i = 0; i < 9; i++)
            avio_wb32(pb, display_matrix[i]);
    } else {
        write_matrix(pb,  1,  0,  0,  1, 0, 0);
    }
    /* Track width and height, for visual only */
    if (st && (track->par->codec_type == AVMEDIA_TYPE_VIDEO ||
               track->par->codec_type == AVMEDIA_TYPE_SUBTITLE)) {
        int64_t track_width_1616;
        if (track->mode == MODE_MOV || track->mode == MODE_AVIF) {
            track_width_1616 = track->par->width * 0x10000ULL;
        } else {
            track_width_1616 = av_rescale(st->sample_aspect_ratio.num,
                                                  track->par->width * 0x10000LL,
                                                  st->sample_aspect_ratio.den);
            if (!track_width_1616 ||
                track->height != track->par->height ||
                track_width_1616 > UINT32_MAX)
                track_width_1616 = track->par->width * 0x10000ULL;
        }
        if (track_width_1616 > UINT32_MAX) {
            av_log(mov->fc, AV_LOG_WARNING, "track width is too large\n");
            track_width_1616 = 0;
        }
        avio_wb32(pb, track_width_1616);
        if (track->height > 0xFFFF) {
            av_log(mov->fc, AV_LOG_WARNING, "track height is too large\n");
            avio_wb32(pb, 0);
        } else
            avio_wb32(pb, track->height * 0x10000U);
    } else {
        avio_wb32(pb, 0);
        avio_wb32(pb, 0);
    }
    return 0x5c;
}

static int mov_write_tapt_tag(AVIOContext *pb, MOVTrack *track)
{
    int32_t width = av_rescale(track->par->sample_aspect_ratio.num, track->par->width,
                               track->par->sample_aspect_ratio.den);

    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "tapt");

    avio_wb32(pb, 20);
    ffio_wfourcc(pb, "clef");
    avio_wb32(pb, 0);
    avio_wb32(pb, width << 16);
    avio_wb32(pb, track->par->height << 16);

    avio_wb32(pb, 20);
    ffio_wfourcc(pb, "prof");
    avio_wb32(pb, 0);
    avio_wb32(pb, width << 16);
    avio_wb32(pb, track->par->height << 16);

    avio_wb32(pb, 20);
    ffio_wfourcc(pb, "enof");
    avio_wb32(pb, 0);
    avio_wb32(pb, track->par->width << 16);
    avio_wb32(pb, track->par->height << 16);

    return update_size(pb, pos);
}

// This box is written in the following cases:
//   * Seems important for the psp playback. Without it the movie seems to hang.
//   * Used for specifying the looping behavior of animated AVIF (as specified
//   in Section 9.6 of the HEIF specification ISO/IEC 23008-12).
static int mov_write_edts_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track)
{
    int64_t duration = av_rescale_rnd(calc_samples_pts_duration(mov, track),
                                      mov->movie_timescale, track->timescale,
                                      AV_ROUND_UP);
    int version = duration < INT32_MAX ? 0 : 1;
    int entry_size, entry_count, size;
    int64_t delay, start_ct = track->start_cts;
    int64_t start_dts = track->start_dts;
    int flags = 0;

    if (track->entry) {
        if (start_dts != track->cluster[0].dts || (start_ct != track->cluster[0].cts && track->cluster[0].dts >= 0)) {

            av_log(mov->fc, AV_LOG_DEBUG,
                   "EDTS using dts:%"PRId64" cts:%d instead of dts:%"PRId64" cts:%"PRId64" tid:%d\n",
                   track->cluster[0].dts, track->cluster[0].cts,
                   start_dts, start_ct, track->track_id);
            start_dts = track->cluster[0].dts;
            start_ct  = track->cluster[0].cts;
        }
    }

    delay = av_rescale_rnd(start_dts + start_ct, mov->movie_timescale,
                           track->timescale, AV_ROUND_DOWN);

    if (mov->mode == MODE_AVIF) {
        delay = 0;
        // Section 9.6.3 of ISO/IEC 23008-12: flags specifies repetition of the
        // edit list as follows: (flags & 1) equal to 0 specifies that the edit
        // list is not repeated, while (flags & 1) equal to 1 specifies that the
        // edit list is repeated.
        flags = mov->avif_loop_count != 1;
        start_ct = 0;
    }

    version |= delay < INT32_MAX ? 0 : 1;

    entry_size = (version == 1) ? 20 : 12;
    entry_count = 1 + (delay > 0);
    size = 24 + entry_count * entry_size;

    /* write the atom data */
    avio_wb32(pb, size);
    ffio_wfourcc(pb, "edts");
    avio_wb32(pb, size - 8);
    ffio_wfourcc(pb, "elst");
    avio_w8(pb, version);
    avio_wb24(pb, flags); /* flags */

    avio_wb32(pb, entry_count);
    if (delay > 0) { /* add an empty edit to delay presentation */
        /* In the positive delay case, the delay includes the cts
         * offset, and the second edit list entry below trims out
         * the same amount from the actual content. This makes sure
         * that the offset last sample is included in the edit
         * list duration as well. */
        if (version == 1) {
            avio_wb64(pb, delay);
            avio_wb64(pb, -1);
        } else {
            avio_wb32(pb, delay);
            avio_wb32(pb, -1);
        }
        avio_wb32(pb, 0x00010000);
    } else if (mov->mode != MODE_AVIF) {
        /* Avoid accidentally ending up with start_ct = -1 which has got a
         * special meaning. Normally start_ct should end up positive or zero
         * here, but use FFMIN in case dts is a small positive integer
         * rounded to 0 when represented in movie timescale units. */
        av_assert0(av_rescale_rnd(start_dts, mov->movie_timescale, track->timescale, AV_ROUND_DOWN) <= 0);
        start_ct  = -FFMIN(start_dts, 0);

#if CONFIG_IAMFENC
        if (track->iamf && track->par->codec_id == AV_CODEC_ID_OPUS)
            start_ct = av_rescale(start_ct, 48000, track->par->sample_rate);
#endif
        /* Note, this delay is calculated from the pts of the first sample,
         * ensuring that we don't reduce the duration for cases with
         * dts<0 pts=0. */
        duration += delay;
    }

    /* For fragmented files, we don't know the full length yet. Setting
     * duration to 0 allows us to only specify the offset, including
     * the rest of the content (from all future fragments) without specifying
     * an explicit duration.
     *
     * For hybrid_fragmented during mov_write_trailer (mov->moov_written != 0),
     * don't reset duration to zero.
     */
    if (mov->flags & FF_MOV_FLAG_FRAGMENT &&
        !(mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED && mov->moov_written))
        duration = 0;

    /* duration */
    if (version == 1) {
        avio_wb64(pb, duration);
        avio_wb64(pb, start_ct);
    } else {
        avio_wb32(pb, duration);
        avio_wb32(pb, start_ct);
    }
    avio_wb32(pb, 0x00010000);
    return size;
}

static int mov_write_tref_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, 20);   // size
    ffio_wfourcc(pb, "tref");
    avio_wb32(pb, 12);   // size (subatom)
    avio_wl32(pb, track->tref_tag);
    avio_wb32(pb, track->tref_id);
    return 20;
}

// goes at the end of each track!  ... Critical for PSP playback ("Incompatible data" without it)
static int mov_write_uuid_tag_psp(AVIOContext *pb, MOVTrack *mov)
{
    avio_wb32(pb, 0x34); /* size ... reports as 28 in mp4box! */
    ffio_wfourcc(pb, "uuid");
    ffio_wfourcc(pb, "USMT");
    avio_wb32(pb, 0x21d24fce);
    avio_wb32(pb, 0xbb88695c);
    avio_wb32(pb, 0xfac9c740);
    avio_wb32(pb, 0x1c);     // another size here!
    ffio_wfourcc(pb, "MTDT");
    avio_wb32(pb, 0x00010012);
    avio_wb32(pb, 0x0a);
    avio_wb32(pb, 0x55c40000);
    avio_wb32(pb, 0x1);
    avio_wb32(pb, 0x0);
    return 0x34;
}

static int mov_write_udta_sdp(AVIOContext *pb, MOVTrack *track)
{
    AVFormatContext *ctx = track->rtp_ctx;
    char buf[1000] = "";
    int len;

    ff_sdp_write_media(buf, sizeof(buf), ctx->streams[0], track->src_track,
                       NULL, NULL, 0, 0, ctx);
    av_strlcatf(buf, sizeof(buf), "a=control:streamid=%d\r\n", track->track_id);
    len = strlen(buf);

    avio_wb32(pb, len + 24);
    ffio_wfourcc(pb, "udta");
    avio_wb32(pb, len + 16);
    ffio_wfourcc(pb, "hnti");
    avio_wb32(pb, len + 8);
    ffio_wfourcc(pb, "sdp ");
    avio_write(pb, buf, len);
    return len + 24;
}

static int mov_write_track_metadata(AVIOContext *pb, AVStream *st,
                                    const char *tag, const char *str)
{
    int64_t pos = avio_tell(pb);
    AVDictionaryEntry *t = av_dict_get(st->metadata, str, NULL, 0);
    if (!t || !utf8len(t->value))
        return 0;

    avio_wb32(pb, 0);   /* size */
    ffio_wfourcc(pb, tag); /* type */
    avio_write(pb, t->value, strlen(t->value)); /* UTF8 string value */
    return update_size(pb, pos);
}

static int mov_write_track_kind(AVIOContext *pb, const char *scheme_uri,
                                const char *value)
{
    int64_t pos = avio_tell(pb);

    /* Box|FullBox basics */
    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, (const unsigned char *)"kind");
    avio_w8(pb, 0);   /* version = 0 */
    avio_wb24(pb, 0); /* flags = 0 */

    /* Required null-terminated scheme URI */
    avio_write(pb, (const unsigned char *)scheme_uri,
               strlen(scheme_uri));
    avio_w8(pb, 0);

    /* Optional value string */
    if (value && value[0])
        avio_write(pb, (const unsigned char *)value,
                   strlen(value));

    avio_w8(pb, 0);

    return update_size(pb, pos);
}

static int mov_write_track_kinds(AVIOContext *pb, AVStream *st)
{
    int ret = AVERROR_BUG;

    for (int i = 0; ff_mov_track_kind_table[i].scheme_uri; i++) {
        const struct MP4TrackKindMapping map = ff_mov_track_kind_table[i];

        for (int j = 0; map.value_maps[j].disposition; j++) {
            const struct MP4TrackKindValueMapping value_map = map.value_maps[j];
            if (!(st->disposition & value_map.disposition))
                continue;

            if ((ret = mov_write_track_kind(pb, map.scheme_uri, value_map.value)) < 0)
                return ret;
        }
    }

    return 0;
}

static int mov_write_track_udta_tag(AVIOContext *pb, MOVMuxContext *mov,
                                    AVStream *st)
{
    AVIOContext *pb_buf;
    int ret, size;
    uint8_t *buf;

    if (!st)
        return 0;

    ret = avio_open_dyn_buf(&pb_buf);
    if (ret < 0)
        return ret;

    if (mov->mode & (MODE_MP4|MODE_MOV))
        mov_write_track_metadata(pb_buf, st, "name", "title");

    if (mov->mode & MODE_MP4) {
        if ((ret = mov_write_track_kinds(pb_buf, st)) < 0)
            return ret;
    }

    if ((size = avio_get_dyn_buf(pb_buf, &buf)) > 0) {
        avio_wb32(pb, size + 8);
        ffio_wfourcc(pb, "udta");
        avio_write(pb, buf, size);
    }
    ffio_free_dyn_buf(&pb_buf);

    return 0;
}

static int mov_write_trak_tag(AVFormatContext *s, AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, AVStream *st)
{
    int64_t pos = avio_tell(pb);
    int entry_backup = track->entry;
    int chunk_backup = track->chunkCount;
    int ret;

    /* If we want to have an empty moov, but some samples already have been
     * buffered (delay_moov), pretend that no samples have been written yet. */
    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV)
        track->chunkCount = track->entry = 0;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "trak");
    mov_write_tkhd_tag(pb, mov, track, st);

    av_assert2(mov->use_editlist >= 0);

    if (track->start_dts != AV_NOPTS_VALUE) {
        if (mov->use_editlist)
            mov_write_edts_tag(pb, mov, track);  // PSP Movies and several other cases require edts box
        else if ((track->entry && track->cluster[0].dts) || track->mode == MODE_PSP || is_clcp_track(track))
            av_log(mov->fc, AV_LOG_WARNING,
                   "Not writing any edit list even though one would have been required\n");
    }

    if (mov->is_animated_avif)
        mov_write_edts_tag(pb, mov, track);

    if (track->tref_tag)
        mov_write_tref_tag(pb, track);

    if ((ret = mov_write_mdia_tag(s, pb, mov, track)) < 0)
        return ret;
    if (track->mode == MODE_PSP)
        mov_write_uuid_tag_psp(pb, track); // PSP Movies require this uuid box
    if (track->tag == MKTAG('r','t','p',' '))
        mov_write_udta_sdp(pb, track);
    if (track->mode == MODE_MOV) {
        if (track->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            double sample_aspect_ratio = av_q2d(st->sample_aspect_ratio);
            if (st->sample_aspect_ratio.num && 1.0 != sample_aspect_ratio) {
                mov_write_tapt_tag(pb, track);
            }
        }
        if (is_clcp_track(track) && st->sample_aspect_ratio.num) {
            mov_write_tapt_tag(pb, track);
        }
    }
    mov_write_track_udta_tag(pb, mov, st);
    track->entry = entry_backup;
    track->chunkCount = chunk_backup;
    return update_size(pb, pos);
}

static int mov_write_iods_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int i, has_audio = 0, has_video = 0;
    int64_t pos = avio_tell(pb);
    int audio_profile = mov->iods_audio_profile;
    int video_profile = mov->iods_video_profile;
    for (i = 0; i < mov->nb_tracks; i++) {
        if (mov->tracks[i].entry > 0 || mov->flags & FF_MOV_FLAG_EMPTY_MOOV) {
            has_audio |= mov->tracks[i].par->codec_type == AVMEDIA_TYPE_AUDIO;
            has_video |= mov->tracks[i].par->codec_type == AVMEDIA_TYPE_VIDEO;
        }
    }
    if (audio_profile < 0)
        audio_profile = 0xFF - has_audio;
    if (video_profile < 0)
        video_profile = 0xFF - has_video;
    avio_wb32(pb, 0x0); /* size */
    ffio_wfourcc(pb, "iods");
    avio_wb32(pb, 0);    /* version & flags */
    put_descr(pb, 0x10, 7);
    avio_wb16(pb, 0x004f);
    avio_w8(pb, 0xff);
    avio_w8(pb, 0xff);
    avio_w8(pb, audio_profile);
    avio_w8(pb, video_profile);
    avio_w8(pb, 0xff);
    return update_size(pb, pos);
}

static int mov_write_trex_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, 0x20); /* size */
    ffio_wfourcc(pb, "trex");
    avio_wb32(pb, 0);   /* version & flags */
    avio_wb32(pb, track->track_id); /* track ID */
    avio_wb32(pb, 1);   /* default sample description index */
    avio_wb32(pb, 0);   /* default sample duration */
    avio_wb32(pb, 0);   /* default sample size */
    avio_wb32(pb, 0);   /* default sample flags */
    return 0;
}

static int mov_write_mvex_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int64_t pos = avio_tell(pb);
    int i;
    avio_wb32(pb, 0x0); /* size */
    ffio_wfourcc(pb, "mvex");
    for (i = 0; i < mov->nb_tracks; i++)
        mov_write_trex_tag(pb, &mov->tracks[i]);
    return update_size(pb, pos);
}

static int mov_write_mvhd_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int max_track_id = 1, i;
    int64_t max_track_len = 0;
    int version;
    int timescale;

    for (i = 0; i < mov->nb_tracks; i++) {
        if (mov->tracks[i].entry > 0 && mov->tracks[i].timescale) {
            int64_t max_track_len_temp = av_rescale_rnd(
                                                calc_pts_duration(mov, &mov->tracks[i]),
                                                mov->movie_timescale,
                                                mov->tracks[i].timescale,
                                                AV_ROUND_UP);
            if (max_track_len < max_track_len_temp)
                max_track_len = max_track_len_temp;
            if (max_track_id < mov->tracks[i].track_id)
                max_track_id = mov->tracks[i].track_id;
        }
    }
    /* If using delay_moov, make sure the output is the same as if no
     * samples had been written yet. */
    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV) {
        max_track_len = 0;
        max_track_id  = 1;
    }

    version = mov_mdhd_mvhd_tkhd_version(mov, NULL, max_track_len);
    avio_wb32(pb, version == 1 ? 120 : 108); /* size */

    ffio_wfourcc(pb, "mvhd");
    avio_w8(pb, version);
    avio_wb24(pb, 0); /* flags */
    if (version == 1) {
        avio_wb64(pb, mov->time);
        avio_wb64(pb, mov->time);
    } else {
        avio_wb32(pb, mov->time); /* creation time */
        avio_wb32(pb, mov->time); /* modification time */
    }

    timescale = mov->movie_timescale;
    if (mov->mode == MODE_AVIF && !timescale)
        timescale = mov->tracks[0].timescale;

    avio_wb32(pb, timescale);
    (version == 1) ? avio_wb64(pb, max_track_len) : avio_wb32(pb, max_track_len); /* duration of longest track */

    avio_wb32(pb, 0x00010000); /* reserved (preferred rate) 1.0 = normal */
    avio_wb16(pb, 0x0100); /* reserved (preferred volume) 1.0 = normal */
    ffio_fill(pb, 0, 2 + 2 * 4); /* reserved */

    /* Matrix structure */
    write_matrix(pb, 1, 0, 0, 1, 0, 0);

    avio_wb32(pb, 0); /* reserved (preview time) */
    avio_wb32(pb, 0); /* reserved (preview duration) */
    avio_wb32(pb, 0); /* reserved (poster time) */
    avio_wb32(pb, 0); /* reserved (selection time) */
    avio_wb32(pb, 0); /* reserved (selection duration) */
    avio_wb32(pb, 0); /* reserved (current time) */
    avio_wb32(pb, max_track_id + 1); /* Next track id */
    return 0x6c;
}

static int mov_write_itunes_hdlr_tag(AVIOContext *pb, MOVMuxContext *mov,
                                     AVFormatContext *s)
{
    avio_wb32(pb, 33); /* size */
    ffio_wfourcc(pb, "hdlr");
    avio_wb32(pb, 0);
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "mdir");
    ffio_wfourcc(pb, "appl");
    avio_wb32(pb, 0);
    avio_wb32(pb, 0);
    avio_w8(pb, 0);
    return 33;
}

/* helper function to write a data tag with the specified string as data */
static int mov_write_string_data_tag(AVIOContext *pb, const char *data, int lang, int long_style)
{
    size_t data_len = strlen(data);
    if (long_style) {
        int size = 16 + data_len;
        avio_wb32(pb, size); /* size */
        ffio_wfourcc(pb, "data");
        avio_wb32(pb, 1);
        avio_wb32(pb, 0);
        avio_write(pb, data, data_len);
        return size;
    } else {
        avio_wb16(pb, data_len); /* string length */
        if (!lang)
            lang = ff_mov_iso639_to_lang("und", 1);
        avio_wb16(pb, lang);
        avio_write(pb, data, data_len);
        return data_len + 4;
    }
}

static int mov_write_string_tag(AVIOContext *pb, const char *name,
                                const char *value, int lang, int long_style)
{
    int size = 0;
    if (value && value[0]) {
        int64_t pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        ffio_wfourcc(pb, name);
        mov_write_string_data_tag(pb, value, lang, long_style);
        size = update_size(pb, pos);
    }
    return size;
}

static AVDictionaryEntry *get_metadata_lang(AVFormatContext *s,
                                            const char *tag, int *lang)
{
    int l, len, len2;
    AVDictionaryEntry *t, *t2 = NULL;
    char tag2[16];

    *lang = 0;

    if (!(t = av_dict_get(s->metadata, tag, NULL, 0)))
        return NULL;

    len = strlen(t->key);
    snprintf(tag2, sizeof(tag2), "%s-", tag);
    while ((t2 = av_dict_get(s->metadata, tag2, t2, AV_DICT_IGNORE_SUFFIX))) {
        len2 = strlen(t2->key);
        if (len2 == len + 4 && !strcmp(t->value, t2->value)
            && (l = ff_mov_iso639_to_lang(&t2->key[len2 - 3], 1)) >= 0) {
            *lang = l;
            return t;
        }
    }
    return t;
}

static int mov_write_string_metadata(AVFormatContext *s, AVIOContext *pb,
                                     const char *name, const char *tag,
                                     int long_style)
{
    int lang;
    AVDictionaryEntry *t = get_metadata_lang(s, tag, &lang);
    if (!t)
        return 0;
    return mov_write_string_tag(pb, name, t->value, lang, long_style);
}

/* iTunes bpm number */
static int mov_write_tmpo_tag(AVIOContext *pb, AVFormatContext *s)
{
    AVDictionaryEntry *t = av_dict_get(s->metadata, "tmpo", NULL, 0);
    int size = 0, tmpo = t ? atoi(t->value) : 0;
    if (tmpo) {
        size = 26;
        avio_wb32(pb, size);
        ffio_wfourcc(pb, "tmpo");
        avio_wb32(pb, size-8); /* size */
        ffio_wfourcc(pb, "data");
        avio_wb32(pb, 0x15);  //type specifier
        avio_wb32(pb, 0);
        avio_wb16(pb, tmpo);        // data
    }
    return size;
}

/* 3GPP TS 26.244 */
static int mov_write_loci_tag(AVFormatContext *s, AVIOContext *pb)
{
    int lang;
    int64_t pos = avio_tell(pb);
    double latitude, longitude, altitude;
    int32_t latitude_fix, longitude_fix, altitude_fix;
    AVDictionaryEntry *t = get_metadata_lang(s, "location", &lang);
    const char *ptr, *place = "";
    char *end;
    static const char *astronomical_body = "earth";
    if (!t)
        return 0;

    ptr = t->value;
    latitude = strtod(ptr, &end);
    if (end == ptr) {
        av_log(s, AV_LOG_WARNING, "malformed location metadata\n");
        return 0;
    }
    ptr = end;
    longitude = strtod(ptr, &end);
    if (end == ptr) {
        av_log(s, AV_LOG_WARNING, "malformed location metadata\n");
        return 0;
    }
    ptr = end;
    altitude = strtod(ptr, &end);
    /* If no altitude was present, the default 0 should be fine */
    if (*end == '/')
        place = end + 1;

    latitude_fix  = (int32_t) ((1 << 16) * latitude);
    longitude_fix = (int32_t) ((1 << 16) * longitude);
    altitude_fix  = (int32_t) ((1 << 16) * altitude);

    avio_wb32(pb, 0);         /* size */
    ffio_wfourcc(pb, "loci"); /* type */
    avio_wb32(pb, 0);         /* version + flags */
    avio_wb16(pb, lang);
    avio_write(pb, place, strlen(place) + 1);
    avio_w8(pb, 0);           /* role of place (0 == shooting location, 1 == real location, 2 == fictional location) */
    avio_wb32(pb, longitude_fix);
    avio_wb32(pb, latitude_fix);
    avio_wb32(pb, altitude_fix);
    avio_write(pb, astronomical_body, strlen(astronomical_body) + 1);
    avio_w8(pb, 0);           /* additional notes, null terminated string */

    return update_size(pb, pos);
}

/* iTunes track or disc number */
static int mov_write_trkn_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s, int disc)
{
    AVDictionaryEntry *t = av_dict_get(s->metadata,
                                       disc ? "disc" : "track",
                                       NULL, 0);
    int size = 0, track = t ? atoi(t->value) : 0;
    if (track) {
        int tracks = 0;
        char *slash = strchr(t->value, '/');
        if (slash)
            tracks = atoi(slash + 1);
        avio_wb32(pb, 32); /* size */
        ffio_wfourcc(pb, disc ? "disk" : "trkn");
        avio_wb32(pb, 24); /* size */
        ffio_wfourcc(pb, "data");
        avio_wb32(pb, 0);        // 8 bytes empty
        avio_wb32(pb, 0);
        avio_wb16(pb, 0);        // empty
        avio_wb16(pb, track);    // track / disc number
        avio_wb16(pb, tracks);   // total track / disc number
        avio_wb16(pb, 0);        // empty
        size = 32;
    }
    return size;
}

static int mov_write_int8_metadata(AVFormatContext *s, AVIOContext *pb,
                                   const char *name, const char *tag,
                                   int len)
{
    AVDictionaryEntry *t = NULL;
    uint8_t num;
    int size = 24 + len;

    if (len != 1 && len != 4)
        return -1;

    if (!(t = av_dict_get(s->metadata, tag, NULL, 0)))
        return 0;
    num = atoi(t->value);

    avio_wb32(pb, size);
    ffio_wfourcc(pb, name);
    avio_wb32(pb, size - 8);
    ffio_wfourcc(pb, "data");
    avio_wb32(pb, 0x15);
    avio_wb32(pb, 0);
    if (len==4) avio_wb32(pb, num);
    else        avio_w8 (pb, num);

    return size;
}

static int mov_write_covr(AVIOContext *pb, AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int64_t pos = 0;

    for (int i = 0; i < mov->nb_streams; i++) {
        MOVTrack *trk = &mov->tracks[i];

        if (!is_cover_image(trk->st) || trk->cover_image->size <= 0)
            continue;

        if (!pos) {
            pos = avio_tell(pb);
            avio_wb32(pb, 0);
            ffio_wfourcc(pb, "covr");
        }
        avio_wb32(pb, 16 + trk->cover_image->size);
        ffio_wfourcc(pb, "data");
        avio_wb32(pb, trk->tag);
        avio_wb32(pb , 0);
        avio_write(pb, trk->cover_image->data, trk->cover_image->size);
    }

    return pos ? update_size(pb, pos) : 0;
}

/* iTunes meta data list */
static int mov_write_ilst_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ilst");
    mov_write_string_metadata(s, pb, "\251nam", "title"    , 1);
    mov_write_string_metadata(s, pb, "\251ART", "artist"   , 1);
    mov_write_string_metadata(s, pb, "aART", "album_artist", 1);
    mov_write_string_metadata(s, pb, "\251wrt", "composer" , 1);
    mov_write_string_metadata(s, pb, "\251alb", "album"    , 1);
    mov_write_string_metadata(s, pb, "\251day", "date"     , 1);
    if (!mov_write_string_metadata(s, pb, "\251too", "encoding_tool", 1)) {
        if (!(s->flags & AVFMT_FLAG_BITEXACT))
            mov_write_string_tag(pb, "\251too", LIBAVFORMAT_IDENT, 0, 1);
    }
    mov_write_string_metadata(s, pb, "\251cmt", "comment"  , 1);
    mov_write_string_metadata(s, pb, "\251gen", "genre"    , 1);
    mov_write_string_metadata(s, pb, "cprt",    "copyright", 1);
    mov_write_string_metadata(s, pb, "\251grp", "grouping" , 1);
    mov_write_string_metadata(s, pb, "\251lyr", "lyrics"   , 1);
    mov_write_string_metadata(s, pb, "desc",    "description",1);
    mov_write_string_metadata(s, pb, "ldes",    "synopsis" , 1);
    mov_write_string_metadata(s, pb, "tvsh",    "show"     , 1);
    mov_write_string_metadata(s, pb, "tven",    "episode_id",1);
    mov_write_string_metadata(s, pb, "tvnn",    "network"  , 1);
    mov_write_string_metadata(s, pb, "keyw",    "keywords"  , 1);
    mov_write_int8_metadata  (s, pb, "tves",    "episode_sort",4);
    mov_write_int8_metadata  (s, pb, "tvsn",    "season_number",4);
    mov_write_int8_metadata  (s, pb, "stik",    "media_type",1);
    mov_write_int8_metadata  (s, pb, "hdvd",    "hd_video",  1);
    mov_write_int8_metadata  (s, pb, "pgap",    "gapless_playback",1);
    mov_write_int8_metadata  (s, pb, "cpil",    "compilation", 1);
    mov_write_covr(pb, s);
    mov_write_trkn_tag(pb, mov, s, 0); // track number
    mov_write_trkn_tag(pb, mov, s, 1); // disc number
    mov_write_tmpo_tag(pb, s);
    return update_size(pb, pos);
}

static int mov_write_mdta_hdlr_tag(AVIOContext *pb, MOVMuxContext *mov,
                                   AVFormatContext *s)
{
    avio_wb32(pb, 33); /* size */
    ffio_wfourcc(pb, "hdlr");
    avio_wb32(pb, 0);
    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "mdta");
    avio_wb32(pb, 0);
    avio_wb32(pb, 0);
    avio_wb32(pb, 0);
    avio_w8(pb, 0);
    return 33;
}

static int mov_write_mdta_keys_tag(AVIOContext *pb, MOVMuxContext *mov,
                                   AVFormatContext *s)
{
    const AVDictionaryEntry *t = NULL;
    int64_t pos = avio_tell(pb);
    int64_t curpos, entry_pos;
    int count = 0;

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "keys");
    avio_wb32(pb, 0);
    entry_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* entry count */

    while (t = av_dict_iterate(s->metadata, t)) {
        size_t key_len = strlen(t->key);
        avio_wb32(pb, key_len + 8);
        ffio_wfourcc(pb, "mdta");
        avio_write(pb, t->key, key_len);
        count += 1;
    }
    curpos = avio_tell(pb);
    avio_seek(pb, entry_pos, SEEK_SET);
    avio_wb32(pb, count); // rewrite entry count
    avio_seek(pb, curpos, SEEK_SET);

    return update_size(pb, pos);
}

static int mov_write_mdta_ilst_tag(AVIOContext *pb, MOVMuxContext *mov,
                                   AVFormatContext *s)
{
    const AVDictionaryEntry *t = NULL;
    int64_t pos = avio_tell(pb);
    int count = 1; /* keys are 1-index based */

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ilst");

    while (t = av_dict_iterate(s->metadata, t)) {
        int64_t entry_pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        avio_wb32(pb, count); /* key */
        mov_write_string_data_tag(pb, t->value, 0, 1);
        update_size(pb, entry_pos);
        count += 1;
    }
    return update_size(pb, pos);
}

/* meta data tags */
static int mov_write_meta_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int size = 0;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "meta");
    avio_wb32(pb, 0);
    if (mov->flags & FF_MOV_FLAG_USE_MDTA) {
        mov_write_mdta_hdlr_tag(pb, mov, s);
        mov_write_mdta_keys_tag(pb, mov, s);
        mov_write_mdta_ilst_tag(pb, mov, s);
    } else if (mov->mode == MODE_AVIF) {
        mov_write_hdlr_tag(s, pb, &mov->tracks[0]);
        // We always write the primary item id as 1 since only one track is
        // supported for AVIF.
        mov_write_pitm_tag(pb, 1);
        mov_write_iloc_tag(pb, mov, s);
        mov_write_iinf_tag(pb, mov, s);
        if (mov->nb_streams > 1)
            mov_write_iref_tag(pb, mov, s);
        mov_write_iprp_tag(pb, mov, s);
    } else {
        /* iTunes metadata tag */
        mov_write_itunes_hdlr_tag(pb, mov, s);
        mov_write_ilst_tag(pb, mov, s);
    }
    size = update_size(pb, pos);
    return size;
}

static int mov_write_raw_metadata_tag(AVFormatContext *s, AVIOContext *pb,
                                      const char *name, const char *key)
{
    int len;
    AVDictionaryEntry *t;

    if (!(t = av_dict_get(s->metadata, key, NULL, 0)))
        return 0;

    len = strlen(t->value);
    if (len > 0) {
        int size = len + 8;
        avio_wb32(pb, size);
        ffio_wfourcc(pb, name);
        avio_write(pb, t->value, len);
        return size;
    }
    return 0;
}

static int ascii_to_wc(AVIOContext *pb, const uint8_t *b)
{
    int val;
    while (*b) {
        GET_UTF8(val, *b++, return -1;)
        avio_wb16(pb, val);
    }
    avio_wb16(pb, 0x00);
    return 0;
}

static uint16_t language_code(const char *str)
{
    return (((str[0] - 0x60) & 0x1F) << 10) +
           (((str[1] - 0x60) & 0x1F) <<  5) +
           (( str[2] - 0x60) & 0x1F);
}

static int mov_write_3gp_udta_tag(AVIOContext *pb, AVFormatContext *s,
                                  const char *tag, const char *str)
{
    int64_t pos = avio_tell(pb);
    AVDictionaryEntry *t = av_dict_get(s->metadata, str, NULL, 0);
    if (!t || !utf8len(t->value))
        return 0;
    avio_wb32(pb, 0);   /* size */
    ffio_wfourcc(pb, tag); /* type */
    avio_wb32(pb, 0);   /* version + flags */
    if (!strcmp(tag, "yrrc"))
        avio_wb16(pb, atoi(t->value));
    else {
        avio_wb16(pb, language_code("eng")); /* language */
        avio_write(pb, t->value, strlen(t->value) + 1); /* UTF8 string value */
        if (!strcmp(tag, "albm") &&
            (t = av_dict_get(s->metadata, "track", NULL, 0)))
            avio_w8(pb, atoi(t->value));
    }
    return update_size(pb, pos);
}

static int mov_write_chpl_tag(AVIOContext *pb, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    int i, nb_chapters = FFMIN(s->nb_chapters, 255);

    avio_wb32(pb, 0);            // size
    ffio_wfourcc(pb, "chpl");
    avio_wb32(pb, 0x01000000);   // version + flags
    avio_wb32(pb, 0);            // unknown
    avio_w8(pb, nb_chapters);

    for (i = 0; i < nb_chapters; i++) {
        AVChapter *c = s->chapters[i];
        AVDictionaryEntry *t;
        avio_wb64(pb, av_rescale_q(c->start, c->time_base, (AVRational){1,10000000}));

        if ((t = av_dict_get(c->metadata, "title", NULL, 0))) {
            int len = FFMIN(strlen(t->value), 255);
            avio_w8(pb, len);
            avio_write(pb, t->value, len);
        } else
            avio_w8(pb, 0);
    }
    return update_size(pb, pos);
}

static int mov_write_udta_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    AVIOContext *pb_buf;
    int ret, size;
    uint8_t *buf;

    ret = avio_open_dyn_buf(&pb_buf);
    if (ret < 0)
        return ret;

    if (mov->mode & MODE_3GP) {
        mov_write_3gp_udta_tag(pb_buf, s, "perf", "artist");
        mov_write_3gp_udta_tag(pb_buf, s, "titl", "title");
        mov_write_3gp_udta_tag(pb_buf, s, "auth", "author");
        mov_write_3gp_udta_tag(pb_buf, s, "gnre", "genre");
        mov_write_3gp_udta_tag(pb_buf, s, "dscp", "comment");
        mov_write_3gp_udta_tag(pb_buf, s, "albm", "album");
        mov_write_3gp_udta_tag(pb_buf, s, "cprt", "copyright");
        mov_write_3gp_udta_tag(pb_buf, s, "yrrc", "date");
        mov_write_loci_tag(s, pb_buf);
    } else if (mov->mode == MODE_MOV && !(mov->flags & FF_MOV_FLAG_USE_MDTA)) { // the title field breaks gtkpod with mp4 and my suspicion is that stuff is not valid in mp4
        mov_write_string_metadata(s, pb_buf, "\251ART", "artist",      0);
        mov_write_string_metadata(s, pb_buf, "\251nam", "title",       0);
        mov_write_string_metadata(s, pb_buf, "\251aut", "author",      0);
        mov_write_string_metadata(s, pb_buf, "\251alb", "album",       0);
        mov_write_string_metadata(s, pb_buf, "\251day", "date",        0);
        mov_write_string_metadata(s, pb_buf, "\251swr", "encoder",     0);
        // currently ignored by mov.c
        mov_write_string_metadata(s, pb_buf, "\251des", "comment",     0);
        // add support for libquicktime, this atom is also actually read by mov.c
        mov_write_string_metadata(s, pb_buf, "\251cmt", "comment",     0);
        mov_write_string_metadata(s, pb_buf, "\251gen", "genre",       0);
        mov_write_string_metadata(s, pb_buf, "\251cpy", "copyright",   0);
        mov_write_string_metadata(s, pb_buf, "\251mak", "make",        0);
        mov_write_string_metadata(s, pb_buf, "\251mod", "model",       0);
        mov_write_string_metadata(s, pb_buf, "\251xyz", "location",    0);
        mov_write_string_metadata(s, pb_buf, "\251key", "keywords",    0);
        mov_write_raw_metadata_tag(s, pb_buf, "XMP_", "xmp");
    } else {
        /* iTunes meta data */
        mov_write_meta_tag(pb_buf, mov, s);
        mov_write_loci_tag(s, pb_buf);
    }

    if (s->nb_chapters && !(mov->flags & FF_MOV_FLAG_DISABLE_CHPL))
        mov_write_chpl_tag(pb_buf, s);

    if ((size = avio_get_dyn_buf(pb_buf, &buf)) > 0) {
        avio_wb32(pb, size + 8);
        ffio_wfourcc(pb, "udta");
        avio_write(pb, buf, size);
    }
    ffio_free_dyn_buf(&pb_buf);

    return 0;
}

static void mov_write_psp_udta_tag(AVIOContext *pb,
                                   const char *str, const char *lang, int type)
{
    int len = utf8len(str) + 1;
    if (len <= 0)
        return;
    avio_wb16(pb, len * 2 + 10);        /* size */
    avio_wb32(pb, type);                /* type */
    avio_wb16(pb, language_code(lang)); /* language */
    avio_wb16(pb, 0x01);                /* ? */
    ascii_to_wc(pb, str);
}

static int mov_write_uuidusmt_tag(AVIOContext *pb, AVFormatContext *s)
{
    AVDictionaryEntry *title = av_dict_get(s->metadata, "title", NULL, 0);
    int64_t pos, pos2;

    if (title) {
        pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size placeholder*/
        ffio_wfourcc(pb, "uuid");
        ffio_wfourcc(pb, "USMT");
        avio_wb32(pb, 0x21d24fce); /* 96 bit UUID */
        avio_wb32(pb, 0xbb88695c);
        avio_wb32(pb, 0xfac9c740);

        pos2 = avio_tell(pb);
        avio_wb32(pb, 0); /* size placeholder*/
        ffio_wfourcc(pb, "MTDT");
        avio_wb16(pb, 4);

        // ?
        avio_wb16(pb, 0x0C);                 /* size */
        avio_wb32(pb, 0x0B);                 /* type */
        avio_wb16(pb, language_code("und")); /* language */
        avio_wb16(pb, 0x0);                  /* ? */
        avio_wb16(pb, 0x021C);               /* data */

        if (!(s->flags & AVFMT_FLAG_BITEXACT))
            mov_write_psp_udta_tag(pb, LIBAVFORMAT_IDENT,      "eng", 0x04);
        mov_write_psp_udta_tag(pb, title->value,          "eng", 0x01);
        mov_write_psp_udta_tag(pb, "2006/04/01 11:11:11", "und", 0x03);

        update_size(pb, pos2);
        return update_size(pb, pos);
    }

    return 0;
}

static int mov_write_pssh_tag(AVIOContext *pb, AVStream *st)
{
    AVEncryptionInitInfo *info;
    const AVPacketSideData *sd = av_packet_side_data_get(st->codecpar->coded_side_data,
                                                         st->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_ENCRYPTION_INIT_INFO);
    if (!sd)
        return 0;

    info = av_encryption_init_info_get_side_data(sd->data, sd->size);
    for (AVEncryptionInitInfo *copy = info; copy; copy = copy->next) {
        int64_t pos;

        if (!copy->data_size && !copy->num_key_ids)
            continue;

        pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size placeholder */
        ffio_wfourcc(pb, "pssh");
        avio_w8(pb, 1); /* version */
        avio_wb24(pb, 0);
        for (int i = 0; i < copy->system_id_size; i++)
            avio_w8(pb, copy->system_id[i]);
        avio_wb32(pb, copy->num_key_ids);
        for (int i = 0; i < copy->num_key_ids; i++)
            for (int j = 0; j < copy->key_id_size; j++)
                avio_w8(pb, copy->key_ids[i][j]);
        avio_wb32(pb, copy->data_size);
        avio_write(pb, copy->data, copy->data_size);
        update_size(pb, pos);
    }

    av_encryption_init_info_free(info);

    return 0;
}

static void build_chunks(MOVTrack *trk)
{
    int i;
    MOVIentry *chunk = &trk->cluster[0];
    uint64_t chunkSize = chunk->size;
    chunk->chunkNum = 1;
    if (trk->chunkCount)
        return;
    trk->chunkCount = 1;
    for (i = 1; i<trk->entry; i++){
        if (chunk->pos + chunkSize == trk->cluster[i].pos &&
            chunkSize + trk->cluster[i].size < (1<<20)){
            chunkSize             += trk->cluster[i].size;
            chunk->samples_in_chunk += trk->cluster[i].entries;
        } else {
            trk->cluster[i].chunkNum = chunk->chunkNum+1;
            chunk=&trk->cluster[i];
            chunkSize = chunk->size;
            trk->chunkCount++;
        }
    }
}

/**
 * Assign track ids. If option "use_stream_ids_as_track_ids" is set,
 * the stream ids are used as track ids.
 *
 * This assumes mov->tracks and s->streams are in the same order and
 * there are no gaps in either of them (so mov->tracks[n] refers to
 * s->streams[n]).
 *
 * As an exception, there can be more entries in
 * s->streams than in mov->tracks, in which case new track ids are
 * generated (starting after the largest found stream id).
 */
static int mov_setup_track_ids(MOVMuxContext *mov, AVFormatContext *s)
{
    int i;

    if (mov->track_ids_ok)
        return 0;

    if (mov->use_stream_ids_as_track_ids) {
        int next_generated_track_id = 0;
        for (i = 0; i < mov->nb_streams; i++) {
            AVStream *st = mov->tracks[i].st;
            if (st->id > next_generated_track_id)
                next_generated_track_id = st->id;
        }

        for (i = 0; i < mov->nb_tracks; i++) {
            if (mov->tracks[i].entry <= 0 && !(mov->flags & FF_MOV_FLAG_FRAGMENT))
                continue;

            mov->tracks[i].track_id = i >= mov->nb_streams ? ++next_generated_track_id : mov->tracks[i].st->id;
        }
    } else {
        int last_track_id = 0;
        for (i = 0; i < mov->nb_tracks; i++) {
            if (mov->tracks[i].entry <= 0 && !(mov->flags & FF_MOV_FLAG_FRAGMENT))
                continue;

            last_track_id =
            mov->tracks[i].track_id = (mov->tracks[i].st
                                       ? FFMAX(mov->tracks[i].st->index, last_track_id)
                                       : FFMAX(i, last_track_id)) + 1;
        }
    }

    mov->track_ids_ok = 1;

    return 0;
}

static int mov_write_moov_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int i;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size placeholder*/
    ffio_wfourcc(pb, "moov");

    mov_setup_track_ids(mov, s);

    for (i = 0; i < mov->nb_tracks; i++) {
        if (mov->tracks[i].entry <= 0 && !(mov->flags & FF_MOV_FLAG_FRAGMENT))
            continue;

        mov->tracks[i].time     = mov->time;

        if (mov->tracks[i].entry)
            build_chunks(&mov->tracks[i]);
    }

    if (mov->chapter_track)
        for (i = 0; i < mov->nb_streams; i++) {
            mov->tracks[i].tref_tag = MKTAG('c','h','a','p');
            mov->tracks[i].tref_id  = mov->tracks[mov->chapter_track].track_id;
        }
    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (track->tag == MKTAG('r','t','p',' ')) {
            track->tref_tag = MKTAG('h','i','n','t');
            track->tref_id = mov->tracks[track->src_track].track_id;
        } else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO) {
            const AVPacketSideData *sd = av_packet_side_data_get(track->st->codecpar->coded_side_data,
                                                                 track->st->codecpar->nb_coded_side_data,
                                                                 AV_PKT_DATA_FALLBACK_TRACK );
            if (sd && sd->size == sizeof(int)) {
                int *fallback = (int *)sd->data;
                if (*fallback >= 0 && *fallback < mov->nb_tracks) {
                    track->tref_tag = MKTAG('f','a','l','l');
                    track->tref_id = mov->tracks[*fallback].track_id;
                }
            }
        }
    }
    for (i = 0; i < mov->nb_tracks; i++) {
        if (mov->tracks[i].tag == MKTAG('t','m','c','d')) {
            int src_trk = mov->tracks[i].src_track;
            mov->tracks[src_trk].tref_tag = mov->tracks[i].tag;
            mov->tracks[src_trk].tref_id  = mov->tracks[i].track_id;
            //src_trk may have a different timescale than the tmcd track
            mov->tracks[i].track_duration = av_rescale(mov->tracks[src_trk].track_duration,
                                                       mov->tracks[i].timescale,
                                                       mov->tracks[src_trk].timescale);
        }
    }

    mov_write_mvhd_tag(pb, mov);
    if (mov->mode != MODE_MOV && mov->mode != MODE_AVIF && !mov->iods_skip)
        mov_write_iods_tag(pb, mov);
    for (i = 0; i < mov->nb_tracks; i++) {
        if (mov->tracks[i].entry > 0 || mov->flags & FF_MOV_FLAG_FRAGMENT ||
            mov->mode == MODE_AVIF) {
            int ret = mov_write_trak_tag(s, pb, mov, &(mov->tracks[i]), i < mov->nb_streams ? mov->tracks[i].st : NULL);
            if (ret < 0)
                return ret;
        }
    }
    if (mov->flags & FF_MOV_FLAG_FRAGMENT)
        mov_write_mvex_tag(pb, mov); /* QuickTime requires trak to precede this */

    if (mov->mode == MODE_PSP)
        mov_write_uuidusmt_tag(pb, s);
    else if (mov->mode != MODE_AVIF)
        mov_write_udta_tag(pb, mov, s);
    for (i = 0; i < mov->nb_streams; i++)
        mov_write_pssh_tag(pb, mov->tracks[i].st);

    return update_size(pb, pos);
}

static void param_write_int(AVIOContext *pb, const char *name, int value)
{
    avio_printf(pb, "<param name=\"%s\" value=\"%d\" valuetype=\"data\"/>\n", name, value);
}

static void param_write_string(AVIOContext *pb, const char *name, const char *value)
{
    avio_printf(pb, "<param name=\"%s\" value=\"%s\" valuetype=\"data\"/>\n", name, value);
}

static void param_write_hex(AVIOContext *pb, const char *name, const uint8_t *value, int len)
{
    char buf[150];
    len = FFMIN(sizeof(buf) / 2 - 1, len);
    ff_data_to_hex(buf, value, len, 0);
    avio_printf(pb, "<param name=\"%s\" value=\"%s\" valuetype=\"data\"/>\n", name, buf);
}

static int mov_write_isml_manifest(AVIOContext *pb, MOVMuxContext *mov, AVFormatContext *s)
{
    int64_t pos = avio_tell(pb);
    int i;

    static const AVUUID uuid = {
        0xa5, 0xd4, 0x0b, 0x30, 0xe8, 0x14, 0x11, 0xdd,
        0xba, 0x2f, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66
    };

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, AV_UUID_LEN);
    avio_wb32(pb, 0);

    avio_printf(pb, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    avio_printf(pb, "<smil xmlns=\"http://www.w3.org/2001/SMIL20/Language\">\n");
    avio_printf(pb, "<head>\n");
    if (!(mov->fc->flags & AVFMT_FLAG_BITEXACT))
        avio_printf(pb, "<meta name=\"creator\" content=\"%s\" />\n",
                    LIBAVFORMAT_IDENT);
    avio_printf(pb, "</head>\n");
    avio_printf(pb, "<body>\n");
    avio_printf(pb, "<switch>\n");

    mov_setup_track_ids(mov, s);

    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        struct mpeg4_bit_rate_values bit_rates =
            calculate_mpeg4_bit_rates(track);
        const char *type;
        int track_id = track->track_id;
        char track_name_buf[32] = { 0 };

        AVStream *st = track->st;
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL,0);

        if (track->par->codec_type == AVMEDIA_TYPE_VIDEO && !is_cover_image(st)) {
            type = "video";
        } else if (track->par->codec_type == AVMEDIA_TYPE_AUDIO) {
            type = "audio";
        } else {
            continue;
        }

        avio_printf(pb, "<%s systemBitrate=\"%"PRIu32"\">\n", type,
                    bit_rates.avg_bit_rate);
        param_write_int(pb, "systemBitrate", bit_rates.avg_bit_rate);
        param_write_int(pb, "trackID", track_id);
        param_write_string(pb, "systemLanguage", lang ? lang->value : "und");

        /* Build track name piece by piece: */
        /* 1. track type */
        av_strlcat(track_name_buf, type, sizeof(track_name_buf));
        /* 2. track language, if available */
        if (lang)
            av_strlcatf(track_name_buf, sizeof(track_name_buf),
                        "_%s", lang->value);
        /* 3. special type suffix */
        /* "_cc" = closed captions, "_ad" = audio_description */
        if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
            av_strlcat(track_name_buf, "_cc", sizeof(track_name_buf));
        else if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
            av_strlcat(track_name_buf, "_ad", sizeof(track_name_buf));

        param_write_string(pb, "trackName", track_name_buf);

        if (track->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (track->par->codec_id == AV_CODEC_ID_H264) {
                uint8_t *ptr;
                int size = track->par->extradata_size;
                if (!ff_avc_write_annexb_extradata(track->par->extradata, &ptr,
                                                   &size)) {
                    param_write_hex(pb, "CodecPrivateData",
                                    ptr ? ptr : track->par->extradata,
                                    size);
                    av_free(ptr);
                }
                param_write_string(pb, "FourCC", "H264");
            } else if (track->par->codec_id == AV_CODEC_ID_VC1) {
                param_write_string(pb, "FourCC", "WVC1");
                param_write_hex(pb, "CodecPrivateData", track->par->extradata,
                                track->par->extradata_size);
            }
            param_write_int(pb, "MaxWidth", track->par->width);
            param_write_int(pb, "MaxHeight", track->par->height);
            param_write_int(pb, "DisplayWidth", track->par->width);
            param_write_int(pb, "DisplayHeight", track->par->height);
        } else {
            if (track->par->codec_id == AV_CODEC_ID_AAC) {
                switch (track->par->profile) {
                case AV_PROFILE_AAC_HE_V2:
                    param_write_string(pb, "FourCC", "AACP");
                    break;
                case AV_PROFILE_AAC_HE:
                    param_write_string(pb, "FourCC", "AACH");
                    break;
                default:
                    param_write_string(pb, "FourCC", "AACL");
                }
            } else if (track->par->codec_id == AV_CODEC_ID_WMAPRO) {
                param_write_string(pb, "FourCC", "WMAP");
            }
            param_write_hex(pb, "CodecPrivateData", track->par->extradata,
                            track->par->extradata_size);
            param_write_int(pb, "AudioTag", ff_codec_get_tag(ff_codec_wav_tags,
                                                             track->par->codec_id));
            param_write_int(pb, "Channels", track->par->ch_layout.nb_channels);
            param_write_int(pb, "SamplingRate", track->tag == MKTAG('i','a','m','f') ?
                                            0 : track->par->sample_rate);
            param_write_int(pb, "BitsPerSample", 16);
            param_write_int(pb, "PacketSize", track->par->block_align ?
                                              track->par->block_align : 4);
        }
        avio_printf(pb, "</%s>\n", type);
    }
    avio_printf(pb, "</switch>\n");
    avio_printf(pb, "</body>\n");
    avio_printf(pb, "</smil>\n");

    return update_size(pb, pos);
}

static int mov_write_mfhd_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    avio_wb32(pb, 16);
    ffio_wfourcc(pb, "mfhd");
    avio_wb32(pb, 0);
    avio_wb32(pb, mov->fragments);
    return 0;
}

static uint32_t get_sample_flags(MOVTrack *track, MOVIentry *entry)
{
    return entry->flags & MOV_SYNC_SAMPLE ? MOV_FRAG_SAMPLE_FLAG_DEPENDS_NO :
           (MOV_FRAG_SAMPLE_FLAG_DEPENDS_YES | MOV_FRAG_SAMPLE_FLAG_IS_NON_SYNC);
}

static int mov_write_tfhd_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int64_t moof_offset)
{
    int64_t pos = avio_tell(pb);
    uint32_t flags = MOV_TFHD_DEFAULT_SIZE | MOV_TFHD_DEFAULT_DURATION |
                     MOV_TFHD_BASE_DATA_OFFSET;
    if (!track->entry) {
        flags |= MOV_TFHD_DURATION_IS_EMPTY;
    } else {
        flags |= MOV_TFHD_DEFAULT_FLAGS;
    }
    if (mov->flags & FF_MOV_FLAG_OMIT_TFHD_OFFSET)
        flags &= ~MOV_TFHD_BASE_DATA_OFFSET;
    if (mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF) {
        flags &= ~MOV_TFHD_BASE_DATA_OFFSET;
        flags |= MOV_TFHD_DEFAULT_BASE_IS_MOOF;
    }
    /* CMAF requires all values to be explicit in tfhd atoms */
    if (mov->flags & FF_MOV_FLAG_CMAF)
        flags |= MOV_TFHD_STSD_ID;

    /* Don't set a default sample size, the silverlight player refuses
     * to play files with that set. Don't set a default sample duration,
     * WMP freaks out if it is set. Don't set a base data offset, PIFF
     * file format says it MUST NOT be set. */
    if (track->mode == MODE_ISM)
        flags &= ~(MOV_TFHD_DEFAULT_SIZE | MOV_TFHD_DEFAULT_DURATION |
                   MOV_TFHD_BASE_DATA_OFFSET | MOV_TFHD_STSD_ID);

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "tfhd");
    avio_w8(pb, 0); /* version */
    avio_wb24(pb, flags);

    avio_wb32(pb, track->track_id); /* track-id */
    if (flags & MOV_TFHD_BASE_DATA_OFFSET)
        avio_wb64(pb, moof_offset);
    if (flags & MOV_TFHD_STSD_ID) {
        avio_wb32(pb, 1);
    }
    if (flags & MOV_TFHD_DEFAULT_DURATION) {
        track->default_duration = get_cluster_duration(track, 0);
        avio_wb32(pb, track->default_duration);
    }
    if (flags & MOV_TFHD_DEFAULT_SIZE) {
        track->default_size = track->entry ? track->cluster[0].size : 1;
        avio_wb32(pb, track->default_size);
    } else
        track->default_size = -1;

    if (flags & MOV_TFHD_DEFAULT_FLAGS) {
        /* Set the default flags based on the second sample, if available.
         * If the first sample is different, that can be signaled via a separate field. */
        if (track->entry > 1)
            track->default_sample_flags = get_sample_flags(track, &track->cluster[1]);
        else
            track->default_sample_flags =
                track->par->codec_type == AVMEDIA_TYPE_VIDEO ?
                (MOV_FRAG_SAMPLE_FLAG_DEPENDS_YES | MOV_FRAG_SAMPLE_FLAG_IS_NON_SYNC) :
                MOV_FRAG_SAMPLE_FLAG_DEPENDS_NO;
        avio_wb32(pb, track->default_sample_flags);
    }

    return update_size(pb, pos);
}

static int mov_write_trun_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int moof_size,
                              int first, int end)
{
    int64_t pos = avio_tell(pb);
    uint32_t flags = MOV_TRUN_DATA_OFFSET;
    int i;

    for (i = first; i < end; i++) {
        if (get_cluster_duration(track, i) != track->default_duration)
            flags |= MOV_TRUN_SAMPLE_DURATION;
        if (track->cluster[i].size != track->default_size)
            flags |= MOV_TRUN_SAMPLE_SIZE;
        if (i > first && get_sample_flags(track, &track->cluster[i]) != track->default_sample_flags)
            flags |= MOV_TRUN_SAMPLE_FLAGS;
    }
    if (!(flags & MOV_TRUN_SAMPLE_FLAGS) && track->entry > first &&
         get_sample_flags(track, &track->cluster[first]) != track->default_sample_flags)
        flags |= MOV_TRUN_FIRST_SAMPLE_FLAGS;
    if (track->flags & MOV_TRACK_CTTS)
        flags |= MOV_TRUN_SAMPLE_CTS;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "trun");
    if (mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
        avio_w8(pb, 1); /* version */
    else
        avio_w8(pb, 0); /* version */
    avio_wb24(pb, flags);

    avio_wb32(pb, end - first); /* sample count */
    if (mov->flags & FF_MOV_FLAG_OMIT_TFHD_OFFSET &&
        !(mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF) &&
        !mov->first_trun)
        avio_wb32(pb, 0); /* Later tracks follow immediately after the previous one */
    else
        avio_wb32(pb, moof_size + 8 + track->data_offset +
                      track->cluster[first].pos); /* data offset */
    if (flags & MOV_TRUN_FIRST_SAMPLE_FLAGS)
        avio_wb32(pb, get_sample_flags(track, &track->cluster[first]));

    for (i = first; i < end; i++) {
        if (flags & MOV_TRUN_SAMPLE_DURATION)
            avio_wb32(pb, get_cluster_duration(track, i));
        if (flags & MOV_TRUN_SAMPLE_SIZE)
            avio_wb32(pb, track->cluster[i].size);
        if (flags & MOV_TRUN_SAMPLE_FLAGS)
            avio_wb32(pb, get_sample_flags(track, &track->cluster[i]));
        if (flags & MOV_TRUN_SAMPLE_CTS)
            avio_wb32(pb, track->cluster[i].cts);
    }

    mov->first_trun = 0;
    return update_size(pb, pos);
}

static int mov_write_tfxd_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    static const uint8_t uuid[] = {
        0x6d, 0x1d, 0x9b, 0x05, 0x42, 0xd5, 0x44, 0xe6,
        0x80, 0xe2, 0x14, 0x1d, 0xaf, 0xf7, 0x57, 0xb2
    };

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, AV_UUID_LEN);
    avio_w8(pb, 1);
    avio_wb24(pb, 0);
    avio_wb64(pb, track->cluster[0].dts + track->cluster[0].cts);
    avio_wb64(pb, track->end_pts -
                  (track->cluster[0].dts + track->cluster[0].cts));

    return update_size(pb, pos);
}

static int mov_write_tfrf_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int entry)
{
    int n = track->nb_frag_info - 1 - entry, i;
    int size = 8 + 16 + 4 + 1 + 16*n;
    static const uint8_t uuid[] = {
        0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
        0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f
    };

    if (entry < 0)
        return 0;

    avio_seek(pb, track->frag_info[entry].tfrf_offset, SEEK_SET);
    avio_wb32(pb, size);
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, AV_UUID_LEN);
    avio_w8(pb, 1);
    avio_wb24(pb, 0);
    avio_w8(pb, n);
    for (i = 0; i < n; i++) {
        int index = entry + 1 + i;
        avio_wb64(pb, track->frag_info[index].time);
        avio_wb64(pb, track->frag_info[index].duration);
    }
    if (n < mov->ism_lookahead) {
        int free_size = 16 * (mov->ism_lookahead - n);
        avio_wb32(pb, free_size);
        ffio_wfourcc(pb, "free");
        ffio_fill(pb, 0, free_size - 8);
    }

    return 0;
}

static int mov_write_tfrf_tags(AVIOContext *pb, MOVMuxContext *mov,
                               MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int i;
    for (i = 0; i < mov->ism_lookahead; i++) {
        /* Update the tfrf tag for the last ism_lookahead fragments,
         * nb_frag_info - 1 is the next fragment to be written. */
        mov_write_tfrf_tag(pb, mov, track, track->nb_frag_info - 2 - i);
    }
    avio_seek(pb, pos, SEEK_SET);
    return 0;
}

static int mov_add_tfra_entries(AVIOContext *pb, MOVMuxContext *mov, int tracks,
                                int size)
{
    int i;
    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        MOVFragmentInfo *info;
        if ((tracks >= 0 && i != tracks) || !track->entry)
            continue;
        track->nb_frag_info++;
        if (track->nb_frag_info >= track->frag_info_capacity) {
            unsigned new_capacity = track->nb_frag_info + MOV_FRAG_INFO_ALLOC_INCREMENT;
            if (av_reallocp_array(&track->frag_info,
                                  new_capacity,
                                  sizeof(*track->frag_info)))
                return AVERROR(ENOMEM);
            track->frag_info_capacity = new_capacity;
        }
        info = &track->frag_info[track->nb_frag_info - 1];
        info->offset   = avio_tell(pb);
        info->size     = size;
        // Try to recreate the original pts for the first packet
        // from the fields we have stored
        info->time     = track->cluster[0].dts + track->cluster[0].cts;
        info->duration = track->end_pts -
                         (track->cluster[0].dts + track->cluster[0].cts);
        // If the pts is less than zero, we will have trimmed
        // away parts of the media track using an edit list,
        // and the corresponding start presentation time is zero.
        if (info->time < 0) {
            info->duration += info->time;
            info->time = 0;
        }
        info->tfrf_offset = 0;
        mov_write_tfrf_tags(pb, mov, track);
    }
    return 0;
}

static void mov_prune_frag_info(MOVMuxContext *mov, int tracks, int max)
{
    int i;
    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if ((tracks >= 0 && i != tracks) || !track->entry)
            continue;
        if (track->nb_frag_info > max) {
            memmove(track->frag_info, track->frag_info + (track->nb_frag_info - max), max * sizeof(*track->frag_info));
            track->nb_frag_info = max;
        }
    }
}

static int mov_write_tfdt_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "tfdt");
    avio_w8(pb, 1); /* version */
    avio_wb24(pb, 0);
    avio_wb64(pb, track->cluster[0].dts - track->start_dts);
    return update_size(pb, pos);
}

static int mov_write_traf_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int64_t moof_offset,
                              int moof_size)
{
    int64_t pos = avio_tell(pb);
    int i, start = 0;
    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "traf");

    mov_write_tfhd_tag(pb, mov, track, moof_offset);
    if (mov->mode != MODE_ISM)
        mov_write_tfdt_tag(pb, track);
    for (i = 1; i < track->entry; i++) {
        if (track->cluster[i].pos != track->cluster[i - 1].pos + track->cluster[i - 1].size) {
            mov_write_trun_tag(pb, mov, track, moof_size, start, i);
            start = i;
        }
    }
    mov_write_trun_tag(pb, mov, track, moof_size, start, track->entry);
    if (mov->mode == MODE_ISM) {
        mov_write_tfxd_tag(pb, track);

        if (mov->ism_lookahead) {
            int size = 16 + 4 + 1 + 16 * mov->ism_lookahead;

            if (track->nb_frag_info > 0) {
                MOVFragmentInfo *info = &track->frag_info[track->nb_frag_info - 1];
                if (!info->tfrf_offset)
                    info->tfrf_offset = avio_tell(pb);
            }
            avio_wb32(pb, 8 + size);
            ffio_wfourcc(pb, "free");
            ffio_fill(pb, 0, size);
        }
    }

    if (track->cenc.aes_ctr && (mov->flags & FF_MOV_FLAG_FRAGMENT))
        ff_mov_cenc_write_stbl_atoms(&track->cenc, pb, moof_offset);

    return update_size(pb, pos);
}

static int mov_write_moof_tag_internal(AVIOContext *pb, MOVMuxContext *mov,
                                       int tracks, int moof_size)
{
    int64_t pos = avio_tell(pb);
    int i;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "moof");
    mov->first_trun = 1;

    mov_write_mfhd_tag(pb, mov);
    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (tracks >= 0 && i != tracks)
            continue;
        if (!track->entry)
            continue;
        if (track->cenc.aes_ctr && (mov->flags & FF_MOV_FLAG_FRAGMENT))
            mov_write_pssh_tag(pb, track->st);
        mov_write_traf_tag(pb, mov, track, pos, moof_size);
    }

    return update_size(pb, pos);
}

static int mov_write_sidx_tag(AVIOContext *pb,
                              MOVTrack *track, int ref_size, int total_sidx_size)
{
    int64_t pos = avio_tell(pb), offset_pos, end_pos;
    int64_t presentation_time, duration, offset;
    unsigned starts_with_SAP;
    int i, entries;

    if (track->entry) {
        entries = 1;
        presentation_time = track->cluster[0].dts + track->cluster[0].cts -
                            track->start_dts - track->start_cts;
        duration = track->end_pts -
                   (track->cluster[0].dts + track->cluster[0].cts);
        starts_with_SAP = track->cluster[0].flags & MOV_SYNC_SAMPLE;

        // pts<0 should be cut away using edts
        if (presentation_time < 0) {
            duration += presentation_time;
            presentation_time = 0;
        }
    } else {
        entries = track->nb_frag_info;
        if (entries <= 0)
            return 0;
        presentation_time = track->frag_info[0].time;
        /* presentation_time <= 0 is handled by mov_add_tfra_entries() */
        if (presentation_time > 0)
            presentation_time -= track->start_dts + track->start_cts;
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "sidx");
    avio_w8(pb, 1); /* version */
    avio_wb24(pb, 0);
    avio_wb32(pb, track->track_id); /* reference_ID */
    avio_wb32(pb, track->timescale); /* timescale */
    avio_wb64(pb, presentation_time); /* earliest_presentation_time */
    offset_pos = avio_tell(pb);
    avio_wb64(pb, 0); /* first_offset (offset to referenced moof) */
    avio_wb16(pb, 0); /* reserved */

    avio_wb16(pb, entries); /* reference_count */
    for (i = 0; i < entries; i++) {
        if (!track->entry) {
            if (i > 1 && track->frag_info[i].offset != track->frag_info[i - 1].offset + track->frag_info[i - 1].size) {
               av_log(NULL, AV_LOG_ERROR, "Non-consecutive fragments, writing incorrect sidx\n");
            }
            duration = track->frag_info[i].duration;
            ref_size = track->frag_info[i].size;
            starts_with_SAP = 1;
        }
        avio_wb32(pb, (0 << 31) | (ref_size & 0x7fffffff)); /* reference_type (0 = media) | referenced_size */
        avio_wb32(pb, duration); /* subsegment_duration */
        avio_wb32(pb, (starts_with_SAP << 31) | (0 << 28) | 0); /* starts_with_SAP | SAP_type | SAP_delta_time */
    }

    end_pos = avio_tell(pb);
    offset = pos + total_sidx_size - end_pos;
    avio_seek(pb, offset_pos, SEEK_SET);
    avio_wb64(pb, offset);
    avio_seek(pb, end_pos, SEEK_SET);
    return update_size(pb, pos);
}

static int mov_write_sidx_tags(AVIOContext *pb, MOVMuxContext *mov,
                               int tracks, int ref_size)
{
    int i, round, ret;
    AVIOContext *avio_buf;
    int total_size = 0;
    for (round = 0; round < 2; round++) {
        // First run one round to calculate the total size of all
        // sidx atoms.
        // This would be much simpler if we'd only write one sidx
        // atom, for the first track in the moof.
        if (round == 0) {
            if ((ret = ffio_open_null_buf(&avio_buf)) < 0)
                return ret;
        } else {
            avio_buf = pb;
        }
        for (i = 0; i < mov->nb_tracks; i++) {
            MOVTrack *track = &mov->tracks[i];
            if (tracks >= 0 && i != tracks)
                continue;
            // When writing a sidx for the full file, entry is 0, but
            // we want to include all tracks. ref_size is 0 in this case,
            // since we read it from frag_info instead.
            if (!track->entry && ref_size > 0)
                continue;
            total_size -= mov_write_sidx_tag(avio_buf, track, ref_size,
                                             total_size);
        }
        if (round == 0)
            total_size = ffio_close_null_buf(avio_buf);
    }
    return 0;
}

static int mov_write_prft_tag(AVIOContext *pb, MOVMuxContext *mov, int tracks)
{
    int64_t pos = avio_tell(pb), pts_us, ntp_ts;
    MOVTrack *first_track;
    int flags = 24;

    /* PRFT should be associated with at most one track. So, choosing only the
     * first track. */
    if (tracks > 0)
        return 0;
    first_track = &(mov->tracks[0]);

    if (!first_track->entry) {
        av_log(mov->fc, AV_LOG_WARNING, "Unable to write PRFT, no entries in the track\n");
        return 0;
    }

    if (first_track->cluster[0].pts == AV_NOPTS_VALUE) {
        av_log(mov->fc, AV_LOG_WARNING, "Unable to write PRFT, first PTS is invalid\n");
        return 0;
    }

    if (mov->write_prft == MOV_PRFT_SRC_WALLCLOCK) {
        if (first_track->cluster[0].prft.wallclock) {
            /* Round the NTP time to whole milliseconds. */
            ntp_ts = ff_get_formatted_ntp_time((first_track->cluster[0].prft.wallclock / 1000) * 1000 +
                                               NTP_OFFSET_US);
            flags = first_track->cluster[0].prft.flags;
        } else
            ntp_ts = ff_get_formatted_ntp_time(ff_ntp_time());
    } else if (mov->write_prft == MOV_PRFT_SRC_PTS) {
        pts_us = av_rescale_q(first_track->cluster[0].pts,
                              first_track->st->time_base, AV_TIME_BASE_Q);
        ntp_ts = ff_get_formatted_ntp_time(pts_us + NTP_OFFSET_US);
    } else {
        av_log(mov->fc, AV_LOG_WARNING, "Unsupported PRFT box configuration: %d\n",
               mov->write_prft);
        return 0;
    }

    avio_wb32(pb, 0);                           // Size place holder
    ffio_wfourcc(pb, "prft");                   // Type
    avio_w8(pb, 1);                             // Version
    avio_wb24(pb, flags);                       // Flags
    avio_wb32(pb, first_track->track_id);       // reference track ID
    avio_wb64(pb, ntp_ts);                      // NTP time stamp
    avio_wb64(pb, first_track->cluster[0].pts); //media time
    return update_size(pb, pos);
}

static int mov_write_moof_tag(AVIOContext *pb, MOVMuxContext *mov, int tracks,
                              int64_t mdat_size)
{
    AVIOContext *avio_buf;
    int ret, moof_size;

    if ((ret = ffio_open_null_buf(&avio_buf)) < 0)
        return ret;
    mov_write_moof_tag_internal(avio_buf, mov, tracks, 0);
    moof_size = ffio_close_null_buf(avio_buf);

    if (mov->flags & FF_MOV_FLAG_DASH &&
        !(mov->flags & (FF_MOV_FLAG_GLOBAL_SIDX | FF_MOV_FLAG_SKIP_SIDX)))
        mov_write_sidx_tags(pb, mov, tracks, moof_size + 8 + mdat_size);

    if (mov->write_prft > MOV_PRFT_NONE && mov->write_prft < MOV_PRFT_NB)
        mov_write_prft_tag(pb, mov, tracks);

    if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX ||
        !(mov->flags & FF_MOV_FLAG_SKIP_TRAILER) ||
        mov->ism_lookahead) {
        if ((ret = mov_add_tfra_entries(pb, mov, tracks, moof_size + 8 + mdat_size)) < 0)
            return ret;
        if (!(mov->flags & FF_MOV_FLAG_GLOBAL_SIDX) &&
            mov->flags & FF_MOV_FLAG_SKIP_TRAILER) {
            mov_prune_frag_info(mov, tracks, mov->ism_lookahead + 1);
        }
    }

    return mov_write_moof_tag_internal(pb, mov, tracks, moof_size);
}

static int mov_write_tfra_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int i;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "tfra");
    avio_w8(pb, 1); /* version */
    avio_wb24(pb, 0);

    avio_wb32(pb, track->track_id);
    avio_wb32(pb, 0); /* length of traf/trun/sample num */
    avio_wb32(pb, track->nb_frag_info);
    for (i = 0; i < track->nb_frag_info; i++) {
        avio_wb64(pb, track->frag_info[i].time);
        avio_wb64(pb, track->frag_info[i].offset + track->data_offset);
        avio_w8(pb, 1); /* traf number */
        avio_w8(pb, 1); /* trun number */
        avio_w8(pb, 1); /* sample number */
    }

    return update_size(pb, pos);
}

static int mov_write_mfra_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    AVIOContext *mfra_pb;
    int i, ret, sz;
    uint8_t *buf;

    ret = avio_open_dyn_buf(&mfra_pb);
    if (ret < 0)
        return ret;

    avio_wb32(mfra_pb, 0); /* size placeholder */
    ffio_wfourcc(mfra_pb, "mfra");
    /* An empty mfra atom is enough to indicate to the publishing point that
     * the stream has ended. */
    if (mov->flags & FF_MOV_FLAG_ISML)
        goto done_mfra;

    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (track->nb_frag_info)
            mov_write_tfra_tag(mfra_pb, track);
    }

    avio_wb32(mfra_pb, 16);
    ffio_wfourcc(mfra_pb, "mfro");
    avio_wb32(mfra_pb, 0); /* version + flags */
    avio_wb32(mfra_pb, avio_tell(mfra_pb) + 4);

done_mfra:

    sz  = update_size(mfra_pb, 0);
    ret = avio_get_dyn_buf(mfra_pb, &buf);
    avio_write(pb, buf, ret);
    ffio_free_dyn_buf(&mfra_pb);

    return sz;
}

static int mov_write_mdat_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    avio_wb32(pb, 8);    // placeholder for extended size field (64 bit)
    ffio_wfourcc(pb, mov->mode == MODE_MOV ? "wide" : "free");

    mov->mdat_pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size placeholder*/
    ffio_wfourcc(pb, "mdat");
    return 0;
}

static void mov_write_ftyp_tag_internal(AVIOContext *pb, AVFormatContext *s,
                                        int has_h264, int has_video, int write_minor)
{
    MOVMuxContext *mov = s->priv_data;
    int minor = 0x200;

    if (mov->major_brand && strlen(mov->major_brand) >= 4)
        ffio_wfourcc(pb, mov->major_brand);
    else if (mov->mode == MODE_3GP) {
        ffio_wfourcc(pb, has_h264 ? "3gp6"  : "3gp4");
        minor =     has_h264 ?   0x100 :   0x200;
    } else if (mov->mode == MODE_AVIF) {
        ffio_wfourcc(pb, mov->is_animated_avif ? "avis" : "avif");
        minor = 0;
    } else if (mov->mode & MODE_3G2) {
        ffio_wfourcc(pb, has_h264 ? "3g2b"  : "3g2a");
        minor =     has_h264 ? 0x20000 : 0x10000;
    } else if (mov->mode == MODE_PSP)
        ffio_wfourcc(pb, "MSNV");
    else if (mov->mode == MODE_MP4 && mov->flags & FF_MOV_FLAG_FRAGMENT &&
                                      mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
        ffio_wfourcc(pb, "iso6"); // Required when using signed CTS offsets in trun boxes
    else if (mov->mode == MODE_MP4 && mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF)
        ffio_wfourcc(pb, "iso5"); // Required when using default-base-is-moof
    else if (mov->mode == MODE_MP4 && mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
        ffio_wfourcc(pb, "iso4");
    else if (mov->mode == MODE_MP4)
        ffio_wfourcc(pb, "isom");
    else if (mov->mode == MODE_IPOD)
        ffio_wfourcc(pb, has_video ? "M4V ":"M4A ");
    else if (mov->mode == MODE_ISM)
        ffio_wfourcc(pb, "isml");
    else if (mov->mode == MODE_F4V)
        ffio_wfourcc(pb, "f4v ");
    else
        ffio_wfourcc(pb, "qt  ");

    if (write_minor)
        avio_wb32(pb, minor);
}

static int mov_write_ftyp_tag(AVIOContext *pb, AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int64_t pos = avio_tell(pb);
    int has_h264 = 0, has_av1 = 0, has_video = 0, has_dolby = 0, has_id3 = 0;
    int has_iamf = 0;

#if CONFIG_IAMFENC
    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];

        if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT ||
            stg->type == AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION) {
            has_iamf = 1;
            break;
        }
    }
#endif
    for (int i = 0; i < mov->nb_streams; i++) {
        AVStream *st = mov->tracks[i].st;
        if (is_cover_image(st))
            continue;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            has_video = 1;
        if (st->codecpar->codec_id == AV_CODEC_ID_H264)
            has_h264 = 1;
        if (st->codecpar->codec_id == AV_CODEC_ID_AV1)
            has_av1 = 1;
        if (st->codecpar->codec_id == AV_CODEC_ID_AC3 ||
            st->codecpar->codec_id == AV_CODEC_ID_EAC3 ||
            st->codecpar->codec_id == AV_CODEC_ID_TRUEHD ||
            av_packet_side_data_get(st->codecpar->coded_side_data,
                                    st->codecpar->nb_coded_side_data,
                                    AV_PKT_DATA_DOVI_CONF))
            has_dolby = 1;
        if (st->codecpar->codec_id == AV_CODEC_ID_TIMED_ID3)
            has_id3 = 1;
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ftyp");

    // Write major brand
    mov_write_ftyp_tag_internal(pb, s, has_h264, has_video, 1);
    // Write the major brand as the first compatible brand as well
    mov_write_ftyp_tag_internal(pb, s, has_h264, has_video, 0);

    // Write compatible brands, ensuring that we don't write the major brand as a
    // compatible brand a second time.
    if (mov->mode == MODE_ISM) {
        ffio_wfourcc(pb, "piff");
    } else if (mov->mode == MODE_AVIF) {
        const AVPixFmtDescriptor *pix_fmt_desc =
            av_pix_fmt_desc_get(s->streams[0]->codecpar->format);
        const int depth = pix_fmt_desc->comp[0].depth;
        if (mov->is_animated_avif) {
            // For animated AVIF, major brand is "avis". Add "avif" as a
            // compatible brand.
            ffio_wfourcc(pb, "avif");
            ffio_wfourcc(pb, "msf1");
            ffio_wfourcc(pb, "iso8");
        }
        ffio_wfourcc(pb, "mif1");
        ffio_wfourcc(pb, "miaf");
        if (depth == 8 || depth == 10) {
            // MA1B and MA1A brands are based on AV1 profile. Short hand for
            // computing that is based on chroma subsampling type. 420 chroma
            // subsampling is MA1B.  444 chroma subsampling is MA1A.
            if (!pix_fmt_desc->log2_chroma_w && !pix_fmt_desc->log2_chroma_h) {
                // 444 chroma subsampling.
                ffio_wfourcc(pb, "MA1A");
            } else {
                // 420 chroma subsampling.
                ffio_wfourcc(pb, "MA1B");
            }
        }
    } else if (mov->mode != MODE_MOV) {
        // We add tfdt atoms when fragmenting, signal this with the iso6 compatible
        // brand, if not already the major brand. This is compatible with users that
        // don't understand tfdt.
        if (mov->mode == MODE_MP4) {
            if (mov->flags & FF_MOV_FLAG_CMAF)
                ffio_wfourcc(pb, "cmfc");
            if (mov->flags & FF_MOV_FLAG_FRAGMENT && !(mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS))
                ffio_wfourcc(pb, "iso6");
            if (has_av1)
                ffio_wfourcc(pb, "av01");
            if (has_dolby)
                ffio_wfourcc(pb, "dby1");
            if (has_iamf)
                ffio_wfourcc(pb, "iamf");
        } else {
            if (mov->flags & FF_MOV_FLAG_FRAGMENT)
                ffio_wfourcc(pb, "iso6");
            if (mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF)
                ffio_wfourcc(pb, "iso5");
            else if (mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
                ffio_wfourcc(pb, "iso4");
        }
        // Brands prior to iso5 can't be signaled when using default-base-is-moof
        if (!(mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF)) {
            // write isom for mp4 only if it it's not the major brand already.
            if (mov->mode != MODE_MP4 || mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS)
                ffio_wfourcc(pb, "isom");
            ffio_wfourcc(pb, "iso2");
            if (has_h264)
                ffio_wfourcc(pb, "avc1");
        }
    }

    if (mov->mode == MODE_MP4)
        ffio_wfourcc(pb, "mp41");

    if (mov->flags & FF_MOV_FLAG_DASH && mov->flags & FF_MOV_FLAG_GLOBAL_SIDX)
        ffio_wfourcc(pb, "dash");

    if (has_id3)
        ffio_wfourcc(pb, "aid3");

    return update_size(pb, pos);
}

static int mov_write_uuidprof_tag(AVIOContext *pb, AVFormatContext *s)
{
    AVStream       *video_st    = s->streams[0];
    AVCodecParameters *video_par = s->streams[0]->codecpar;
    AVCodecParameters *audio_par = s->streams[1]->codecpar;
    int audio_rate = audio_par->sample_rate;
    int64_t frame_rate = video_st->avg_frame_rate.den ?
                        (video_st->avg_frame_rate.num * 0x10000LL) / video_st->avg_frame_rate.den :
                        0;
    int audio_kbitrate = audio_par->bit_rate / 1000;
    int video_kbitrate = FFMIN(video_par->bit_rate / 1000, 800 - audio_kbitrate);

    if (frame_rate < 0 || frame_rate > INT32_MAX) {
        av_log(s, AV_LOG_ERROR, "Frame rate %f outside supported range\n", frame_rate / (double)0x10000);
        return AVERROR(EINVAL);
    }

    avio_wb32(pb, 0x94); /* size */
    ffio_wfourcc(pb, "uuid");
    ffio_wfourcc(pb, "PROF");

    avio_wb32(pb, 0x21d24fce); /* 96 bit UUID */
    avio_wb32(pb, 0xbb88695c);
    avio_wb32(pb, 0xfac9c740);

    avio_wb32(pb, 0x0);  /* ? */
    avio_wb32(pb, 0x3);  /* 3 sections ? */

    avio_wb32(pb, 0x14); /* size */
    ffio_wfourcc(pb, "FPRF");
    avio_wb32(pb, 0x0);  /* ? */
    avio_wb32(pb, 0x0);  /* ? */
    avio_wb32(pb, 0x0);  /* ? */

    avio_wb32(pb, 0x2c);  /* size */
    ffio_wfourcc(pb, "APRF"); /* audio */
    avio_wb32(pb, 0x0);
    avio_wb32(pb, 0x2);   /* TrackID */
    ffio_wfourcc(pb, "mp4a");
    avio_wb32(pb, 0x20f);
    avio_wb32(pb, 0x0);
    avio_wb32(pb, audio_kbitrate);
    avio_wb32(pb, audio_kbitrate);
    avio_wb32(pb, audio_rate);
    avio_wb32(pb, audio_par->ch_layout.nb_channels);

    avio_wb32(pb, 0x34);  /* size */
    ffio_wfourcc(pb, "VPRF");   /* video */
    avio_wb32(pb, 0x0);
    avio_wb32(pb, 0x1);    /* TrackID */
    if (video_par->codec_id == AV_CODEC_ID_H264) {
        ffio_wfourcc(pb, "avc1");
        avio_wb16(pb, 0x014D);
        avio_wb16(pb, 0x0015);
    } else {
        ffio_wfourcc(pb, "mp4v");
        avio_wb16(pb, 0x0000);
        avio_wb16(pb, 0x0103);
    }
    avio_wb32(pb, 0x0);
    avio_wb32(pb, video_kbitrate);
    avio_wb32(pb, video_kbitrate);
    avio_wb32(pb, frame_rate);
    avio_wb32(pb, frame_rate);
    avio_wb16(pb, video_par->width);
    avio_wb16(pb, video_par->height);
    avio_wb32(pb, 0x010001); /* ? */

    return 0;
}

static int mov_write_identification(AVIOContext *pb, AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int i;

    mov_write_ftyp_tag(pb,s);
    if (mov->mode == MODE_PSP) {
        int video_streams_nb = 0, audio_streams_nb = 0, other_streams_nb = 0;
        for (i = 0; i < mov->nb_streams; i++) {
            AVStream *st = mov->tracks[i].st;
            if (is_cover_image(st))
                continue;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                video_streams_nb++;
            else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                audio_streams_nb++;
            else
                other_streams_nb++;
            }

        if (video_streams_nb != 1 || audio_streams_nb != 1 || other_streams_nb) {
            av_log(s, AV_LOG_ERROR, "PSP mode need one video and one audio stream\n");
            return AVERROR(EINVAL);
        }
        return mov_write_uuidprof_tag(pb, s);
    }
    return 0;
}

static int mov_parse_mpeg2_frame(AVPacket *pkt, uint32_t *flags)
{
    uint32_t c = -1;
    int i, closed_gop = 0;

    for (i = 0; i < pkt->size - 4; i++) {
        c = (c << 8) + pkt->data[i];
        if (c == 0x1b8) { // gop
            closed_gop = pkt->data[i + 4] >> 6 & 0x01;
        } else if (c == 0x100) { // pic
            int temp_ref = (pkt->data[i + 1] << 2) | (pkt->data[i + 2] >> 6);
            if (!temp_ref || closed_gop) // I picture is not reordered
                *flags = MOV_SYNC_SAMPLE;
            else
                *flags = MOV_PARTIAL_SYNC_SAMPLE;
            break;
        }
    }
    return 0;
}

static void mov_parse_vc1_frame(AVPacket *pkt, MOVTrack *trk)
{
    const uint8_t *start, *next, *end = pkt->data + pkt->size;
    int seq = 0, entry = 0;
    int key = pkt->flags & AV_PKT_FLAG_KEY;
    start = find_next_marker(pkt->data, end);
    for (next = start; next < end; start = next) {
        next = find_next_marker(start + 4, end);
        switch (AV_RB32(start)) {
        case VC1_CODE_SEQHDR:
            seq = 1;
            break;
        case VC1_CODE_ENTRYPOINT:
            entry = 1;
            break;
        case VC1_CODE_SLICE:
            trk->vc1_info.slices = 1;
            break;
        }
    }
    if (!trk->entry && trk->vc1_info.first_packet_seen)
        trk->vc1_info.first_frag_written = 1;
    if (!trk->entry && !trk->vc1_info.first_frag_written) {
        /* First packet in first fragment */
        trk->vc1_info.first_packet_seq   = seq;
        trk->vc1_info.first_packet_entry = entry;
        trk->vc1_info.first_packet_seen  = 1;
    } else if ((seq && !trk->vc1_info.packet_seq) ||
               (entry && !trk->vc1_info.packet_entry)) {
        int i;
        for (i = 0; i < trk->entry; i++)
            trk->cluster[i].flags &= ~MOV_SYNC_SAMPLE;
        trk->has_keyframes = 0;
        if (seq)
            trk->vc1_info.packet_seq = 1;
        if (entry)
            trk->vc1_info.packet_entry = 1;
        if (!trk->vc1_info.first_frag_written) {
            /* First fragment */
            if ((!seq   || trk->vc1_info.first_packet_seq) &&
                (!entry || trk->vc1_info.first_packet_entry)) {
                /* First packet had the same headers as this one, readd the
                 * sync sample flag. */
                trk->cluster[0].flags |= MOV_SYNC_SAMPLE;
                trk->has_keyframes = 1;
            }
        }
    }
    if (trk->vc1_info.packet_seq && trk->vc1_info.packet_entry)
        key = seq && entry;
    else if (trk->vc1_info.packet_seq)
        key = seq;
    else if (trk->vc1_info.packet_entry)
        key = entry;
    if (key) {
        trk->cluster[trk->entry].flags |= MOV_SYNC_SAMPLE;
        trk->has_keyframes++;
    }
}

static void mov_parse_truehd_frame(AVPacket *pkt, MOVTrack *trk)
{
    int length;

    if (pkt->size < 8)
        return;

    length = (AV_RB16(pkt->data) & 0xFFF) * 2;
    if (length < 8 || length > pkt->size)
        return;

    if (AV_RB32(pkt->data + 4) == 0xF8726FBA) {
        trk->cluster[trk->entry].flags |= MOV_SYNC_SAMPLE;
        trk->has_keyframes++;
    }

    return;
}

static int mov_flush_fragment_interleaving(AVFormatContext *s, MOVTrack *track)
{
    MOVMuxContext *mov = s->priv_data;
    int ret, buf_size;
    uint8_t *buf;
    int i, offset;

    if (!track->mdat_buf)
        return 0;
    if (!mov->mdat_buf) {
        if ((ret = avio_open_dyn_buf(&mov->mdat_buf)) < 0)
            return ret;
    }
    buf_size = avio_get_dyn_buf(track->mdat_buf, &buf);

    offset = avio_tell(mov->mdat_buf);
    avio_write(mov->mdat_buf, buf, buf_size);
    ffio_free_dyn_buf(&track->mdat_buf);

    for (i = track->entries_flushed; i < track->entry; i++)
        track->cluster[i].pos += offset;
    track->entries_flushed = track->entry;
    return 0;
}

static int mov_write_squashed_packet(AVFormatContext *s, MOVTrack *track)
{
    MOVMuxContext *mov = s->priv_data;
    AVPacket *squashed_packet = mov->pkt;
    int ret = AVERROR_BUG;

    switch (track->st->codecpar->codec_id) {
    case AV_CODEC_ID_TTML: {
        int had_packets = !!track->squashed_packet_queue.head;

        if ((ret = ff_mov_generate_squashed_ttml_packet(s, track, squashed_packet)) < 0) {
            goto finish_squash;
        }

        // We have generated a padding packet (no actual input packets in
        // queue) and its duration is zero. Skipping writing it.
        if (!had_packets && squashed_packet->duration == 0) {
            goto finish_squash;
        }

        track->end_reliable = 1;
        break;
    }
    default:
        ret = AVERROR(EINVAL);
        goto finish_squash;
    }

    squashed_packet->stream_index = track->st->index;

    ret = mov_write_single_packet(s, squashed_packet);

finish_squash:
    av_packet_unref(squashed_packet);

    return ret;
}

static int mov_write_squashed_packets(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;

    for (int i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        int ret = AVERROR_BUG;

        if (track->squash_fragment_samples_to_one && !track->entry) {
            if ((ret = mov_write_squashed_packet(s, track)) < 0) {
                av_log(s, AV_LOG_ERROR,
                       "Failed to write squashed packet for %s stream with "
                       "index %d and track id %d. Error: %s\n",
                       avcodec_get_name(track->st->codecpar->codec_id),
                       track->st->index, track->track_id,
                       av_err2str(ret));
                return ret;
            }
        }
    }

    return 0;
}

static int mov_finish_fragment(MOVMuxContext *mov, MOVTrack *track,
                               int64_t ref_pos)
{
    int i;
    if (!track->entry)
        return 0;
    if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED) {
        for (i = 0; i < track->entry; i++)
            track->cluster[i].pos += ref_pos + track->data_offset;
        if (track->cluster_written == 0 && !(mov->flags & FF_MOV_FLAG_EMPTY_MOOV)) {
            // First flush. If this was a case of not using empty moov, reset chunking.
            for (i = 0; i < track->entry; i++) {
                track->cluster[i].chunkNum = 0;
                track->cluster[i].samples_in_chunk = track->cluster[i].entries;
            }
        }
        if (av_reallocp_array(&track->cluster_written,
                              track->entry_written + track->entry,
                              sizeof(*track->cluster)))
            return AVERROR(ENOMEM);
        memcpy(&track->cluster_written[track->entry_written],
               track->cluster, track->entry * sizeof(*track->cluster));
        track->entry_written += track->entry;
    }
    track->entry = 0;
    track->entries_flushed = 0;
    track->end_reliable = 0;
    return 0;
}

static int mov_flush_fragment(AVFormatContext *s, int force)
{
    MOVMuxContext *mov = s->priv_data;
    int i, first_track = -1;
    int64_t mdat_size = 0, mdat_start = 0;
    int ret;
    int has_video = 0, starts_with_key = 0, first_video_track = 1;

    if (!(mov->flags & FF_MOV_FLAG_FRAGMENT))
        return 0;

    // Check if we have any tracks that require squashing.
    // In that case, we'll have to write the packet here.
    if ((ret = mov_write_squashed_packets(s)) < 0)
        return ret;

    // Try to fill in the duration of the last packet in each stream
    // from queued packets in the interleave queues. If the flushing
    // of fragments was triggered automatically by an AVPacket, we
    // already have reliable info for the end of that track, but other
    // tracks may need to be filled in.
    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (!track->end_reliable) {
            const AVPacket *pkt = ff_interleaved_peek(s, i);
            if (pkt) {
                int64_t offset, dts, pts;
                ff_get_muxer_ts_offset(s, i, &offset);
                pts = pkt->pts + offset;
                dts = pkt->dts + offset;
                if (track->dts_shift != AV_NOPTS_VALUE)
                    dts += track->dts_shift;
                track->track_duration = dts - track->start_dts;
                if (pts != AV_NOPTS_VALUE)
                    track->end_pts = pts;
                else
                    track->end_pts = dts;
            }
        }
    }

    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (track->entry <= 1)
            continue;
        // Sample durations are calculated as the diff of dts values,
        // but for the last sample in a fragment, we don't know the dts
        // of the first sample in the next fragment, so we have to rely
        // on what was set as duration in the AVPacket. Not all callers
        // set this though, so we might want to replace it with an
        // estimate if it currently is zero.
        if (get_cluster_duration(track, track->entry - 1) != 0)
            continue;
        // Use the duration (i.e. dts diff) of the second last sample for
        // the last one. This is a wild guess (and fatal if it turns out
        // to be too long), but probably the best we can do - having a zero
        // duration is bad as well.
        track->track_duration += get_cluster_duration(track, track->entry - 2);
        track->end_pts        += get_cluster_duration(track, track->entry - 2);
        if (!mov->missing_duration_warned) {
            av_log(s, AV_LOG_WARNING,
                   "Estimating the duration of the last packet in a "
                   "fragment, consider setting the duration field in "
                   "AVPacket instead.\n");
            mov->missing_duration_warned = 1;
        }
    }

    if (!mov->moov_written) {
        int64_t pos = avio_tell(s->pb);
        uint8_t *buf;
        int buf_size, moov_size;

        for (i = 0; i < mov->nb_tracks; i++)
            if (!mov->tracks[i].entry && !is_cover_image(mov->tracks[i].st))
                break;
        /* Don't write the initial moov unless all tracks have data */
        if (i < mov->nb_tracks && !force)
            return 0;

        moov_size = get_moov_size(s);
        for (i = 0; i < mov->nb_tracks; i++)
            mov->tracks[i].data_offset = pos + moov_size + 8;

        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_HEADER);
        if (mov->flags & FF_MOV_FLAG_DELAY_MOOV)
            mov_write_identification(s->pb, s);
        if ((ret = mov_write_moov_tag(s->pb, mov, s)) < 0)
            return ret;

        if (mov->flags & FF_MOV_FLAG_DELAY_MOOV) {
            if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX)
                mov->reserved_header_pos = avio_tell(s->pb);
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
            mov->moov_written = 1;
            return 0;
        }

        buf_size = avio_get_dyn_buf(mov->mdat_buf, &buf);
        avio_wb32(s->pb, buf_size + 8);
        ffio_wfourcc(s->pb, "mdat");
        avio_write(s->pb, buf, buf_size);
        ffio_free_dyn_buf(&mov->mdat_buf);

        if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX)
            mov->reserved_header_pos = avio_tell(s->pb);

        mov->moov_written = 1;
        mov->mdat_size = 0;
        for (i = 0; i < mov->nb_tracks; i++)
            mov_finish_fragment(mov, &mov->tracks[i], 0);
        avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
        return 0;
    }

    if (mov->frag_interleave) {
        for (i = 0; i < mov->nb_tracks; i++) {
            MOVTrack *track = &mov->tracks[i];
            int ret;
            if ((ret = mov_flush_fragment_interleaving(s, track)) < 0)
                return ret;
        }

        if (!mov->mdat_buf)
            return 0;
        mdat_size = avio_tell(mov->mdat_buf);
    }

    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (mov->flags & FF_MOV_FLAG_SEPARATE_MOOF || mov->frag_interleave)
            track->data_offset = 0;
        else
            track->data_offset = mdat_size;
        if (track->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            has_video = 1;
            if (first_video_track) {
                if (track->entry)
                    starts_with_key = track->cluster[0].flags & MOV_SYNC_SAMPLE;
                first_video_track = 0;
            }
        }
        if (!track->entry)
            continue;
        if (track->mdat_buf)
            mdat_size += avio_tell(track->mdat_buf);
        if (first_track < 0)
            first_track = i;
    }

    if (!mdat_size)
        return 0;

    avio_write_marker(s->pb,
                      av_rescale(mov->tracks[first_track].cluster[0].dts, AV_TIME_BASE, mov->tracks[first_track].timescale),
                      (has_video ? starts_with_key : mov->tracks[first_track].cluster[0].flags & MOV_SYNC_SAMPLE) ? AVIO_DATA_MARKER_SYNC_POINT : AVIO_DATA_MARKER_BOUNDARY_POINT);

    for (i = first_track; i < mov->nb_tracks; i++) {
        MOVTrack *track = &mov->tracks[i];
        int buf_size, write_moof = 1, moof_tracks = -1;
        uint8_t *buf;

        if (!track->entry)
            continue;
        if (mov->flags & FF_MOV_FLAG_SEPARATE_MOOF) {
            mdat_size = avio_tell(track->mdat_buf);
            moof_tracks = i;
        } else {
            write_moof = i == first_track;
        }

        if (write_moof) {
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);

            mov_write_moof_tag(s->pb, mov, moof_tracks, mdat_size);
            mov->fragments++;

            avio_wb32(s->pb, mdat_size + 8);
            ffio_wfourcc(s->pb, "mdat");
            mdat_start = avio_tell(s->pb);
        }

        mov_finish_fragment(mov, &mov->tracks[i], mdat_start);
        if (!mov->frag_interleave) {
            if (!track->mdat_buf)
                continue;
            buf_size = avio_close_dyn_buf(track->mdat_buf, &buf);
            track->mdat_buf = NULL;
        } else {
            if (!mov->mdat_buf)
                continue;
            buf_size = avio_close_dyn_buf(mov->mdat_buf, &buf);
            mov->mdat_buf = NULL;
        }

        avio_write(s->pb, buf, buf_size);
        av_free(buf);
    }

    mov->mdat_size = 0;

    avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_FLUSH_POINT);
    return 0;
}

static int mov_auto_flush_fragment(AVFormatContext *s, int force)
{
    MOVMuxContext *mov = s->priv_data;
    int had_moov = mov->moov_written;
    int ret = mov_flush_fragment(s, force);
    if (ret < 0)
        return ret;
    // If using delay_moov, the first flush only wrote the moov,
    // not the actual moof+mdat pair, thus flush once again.
    if (!had_moov && mov->flags & FF_MOV_FLAG_DELAY_MOOV)
        ret = mov_flush_fragment(s, force);
    return ret;
}

static int check_pkt(AVFormatContext *s, MOVTrack *trk, AVPacket *pkt)
{
    int64_t ref;
    uint64_t duration;

    if (trk->entry) {
        ref = trk->cluster[trk->entry - 1].dts;
    } else if (   trk->start_dts != AV_NOPTS_VALUE
               && !trk->frag_discont) {
        ref = trk->start_dts + trk->track_duration;
    } else
        ref = pkt->dts; // Skip tests for the first packet

    if (trk->dts_shift != AV_NOPTS_VALUE) {
        /* With negative CTS offsets we have set an offset to the DTS,
         * reverse this for the check. */
        ref -= trk->dts_shift;
    }

    duration = pkt->dts - ref;
    if (pkt->dts < ref || duration >= INT_MAX) {
        av_log(s, AV_LOG_WARNING, "Packet duration: %"PRId64" / dts: %"PRId64" in stream %d is out of range\n",
               duration, pkt->dts, pkt->stream_index);

        pkt->dts = ref + 1;
        pkt->pts = AV_NOPTS_VALUE;
    }

    if (pkt->duration < 0 || pkt->duration > INT_MAX) {
        av_log(s, AV_LOG_ERROR, "Application provided duration: %"PRId64" in stream %d is invalid\n", pkt->duration, pkt->stream_index);
        return AVERROR(EINVAL);
    }
    return 0;
}

int ff_mov_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    MOVTrack *trk;
    AVCodecParameters *par;
    AVProducerReferenceTime *prft;
    unsigned int samples_in_chunk = 0;
    int size = pkt->size, ret = 0, offset = 0;
    size_t prft_size;
    uint8_t *reformatted_data = NULL;

    if (pkt->stream_index < s->nb_streams)
        trk = s->streams[pkt->stream_index]->priv_data;
    else // Timecode or chapter
        trk = &mov->tracks[pkt->stream_index];
    par = trk->par;

    ret = check_pkt(s, trk, pkt);
    if (ret < 0)
        return ret;

    if (pkt->pts != AV_NOPTS_VALUE &&
        (uint64_t)pkt->dts - pkt->pts != (int32_t)((uint64_t)pkt->dts - pkt->pts)) {
        av_log(s, AV_LOG_WARNING, "pts/dts pair unsupported\n");
        return AVERROR_PATCHWELCOME;
    }

    if (mov->flags & FF_MOV_FLAG_FRAGMENT || mov->mode == MODE_AVIF) {
        int ret;
        if (mov->moov_written || mov->flags & FF_MOV_FLAG_EMPTY_MOOV) {
            if (mov->frag_interleave && mov->fragments > 0) {
                if (trk->entry - trk->entries_flushed >= mov->frag_interleave) {
                    if ((ret = mov_flush_fragment_interleaving(s, trk)) < 0)
                        return ret;
                }
            }

            if (!trk->mdat_buf) {
                if ((ret = avio_open_dyn_buf(&trk->mdat_buf)) < 0)
                    return ret;
            }
            pb = trk->mdat_buf;
        } else {
            if (!mov->mdat_buf) {
                if ((ret = avio_open_dyn_buf(&mov->mdat_buf)) < 0)
                    return ret;
            }
            pb = mov->mdat_buf;
        }
    }

    if (par->codec_id == AV_CODEC_ID_AMR_NB) {
        /* We must find out how many AMR blocks there are in one packet */
        static const uint16_t packed_size[16] =
            {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 1};
        int len = 0;

        while (len < size && samples_in_chunk < 100) {
            len += packed_size[(pkt->data[len] >> 3) & 0x0F];
            samples_in_chunk++;
        }
        if (samples_in_chunk > 1) {
            av_log(s, AV_LOG_ERROR, "fatal error, input is not a single packet, implement a AVParser for it\n");
            return -1;
        }
    } else if (par->codec_id == AV_CODEC_ID_ADPCM_MS ||
               par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV) {
        samples_in_chunk = trk->par->frame_size;
    } else if (trk->sample_size)
        samples_in_chunk = size / trk->sample_size;
    else
        samples_in_chunk = 1;

    if (samples_in_chunk < 1) {
        av_log(s, AV_LOG_ERROR, "fatal error, input packet contains no samples\n");
        return AVERROR_PATCHWELCOME;
    }

    /* copy extradata if it exists */
    if (trk->vos_len == 0 && par->extradata_size > 0 &&
        !TAG_IS_AVCI(trk->tag) &&
        (par->codec_id != AV_CODEC_ID_DNXHD)) {
        trk->vos_len  = par->extradata_size;
        trk->vos_data = av_malloc(trk->vos_len + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!trk->vos_data) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        memcpy(trk->vos_data, par->extradata, trk->vos_len);
        memset(trk->vos_data + trk->vos_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }

    if ((par->codec_id == AV_CODEC_ID_DNXHD ||
         par->codec_id == AV_CODEC_ID_H264 ||
         par->codec_id == AV_CODEC_ID_HEVC ||
         par->codec_id == AV_CODEC_ID_VVC ||
         par->codec_id == AV_CODEC_ID_VP9 ||
         par->codec_id == AV_CODEC_ID_EVC ||
         par->codec_id == AV_CODEC_ID_TRUEHD) && !trk->vos_len &&
         !TAG_IS_AVCI(trk->tag)) {
        /* copy frame to create needed atoms */
        trk->vos_len  = size;
        trk->vos_data = av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!trk->vos_data) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        memcpy(trk->vos_data, pkt->data, size);
        memset(trk->vos_data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    }

    if (par->codec_id == AV_CODEC_ID_AAC && pkt->size > 2 &&
        (AV_RB16(pkt->data) & 0xfff0) == 0xfff0) {
        if (!trk->st->nb_frames) {
            av_log(s, AV_LOG_ERROR, "Malformed AAC bitstream detected: "
                   "use the audio bitstream filter 'aac_adtstoasc' to fix it "
                   "('-bsf:a aac_adtstoasc' option with ffmpeg)\n");
            return -1;
        }
        av_log(s, AV_LOG_WARNING, "aac bitstream error\n");
    }
    if (par->codec_id == AV_CODEC_ID_H264 && trk->vos_len > 0 && *(uint8_t *)trk->vos_data != 1 && !TAG_IS_AVCI(trk->tag)) {
        /* from x264 or from bytestream H.264 */
        /* NAL reformatting needed */
        if (trk->hint_track >= 0 && trk->hint_track < mov->nb_tracks) {
            ret = ff_nal_parse_units_buf(pkt->data, &reformatted_data,
                                             &size);
            if (ret < 0)
                return ret;
            avio_write(pb, reformatted_data, size);
        } else {
            if (trk->cenc.aes_ctr) {
                size = ff_mov_cenc_avc_parse_nal_units(&trk->cenc, pb, pkt->data, size);
                if (size < 0) {
                    ret = size;
                    goto err;
                }
            } else {
                size = ff_nal_parse_units(pb, pkt->data, pkt->size);
            }
        }
    } else if (par->codec_id == AV_CODEC_ID_HEVC && trk->vos_len > 6 &&
               (AV_RB24(trk->vos_data) == 1 || AV_RB32(trk->vos_data) == 1)) {
        /* extradata is Annex B, assume the bitstream is too and convert it */
        int filter_ps = (trk->tag == MKTAG('h','v','c','1'));
        if (trk->hint_track >= 0 && trk->hint_track < mov->nb_tracks) {
            ret = ff_hevc_annexb2mp4_buf(pkt->data, &reformatted_data,
                                         &size, filter_ps, NULL);
            if (ret < 0)
                return ret;
            avio_write(pb, reformatted_data, size);
        } else {
            if (trk->cenc.aes_ctr) {
                size = ff_mov_cenc_avc_parse_nal_units(&trk->cenc, pb, pkt->data, size);
                if (size < 0) {
                    ret = size;
                    goto err;
                }
            } else {
                size = ff_hevc_annexb2mp4(pb, pkt->data, pkt->size, filter_ps, NULL);
            }
        }
    } else if (par->codec_id == AV_CODEC_ID_VVC && trk->vos_len > 6 &&
             (AV_RB24(trk->vos_data) == 1 || AV_RB32(trk->vos_data) == 1)) {
        /* extradata is Annex B, assume the bitstream is too and convert it */
        if (trk->hint_track >= 0 && trk->hint_track < mov->nb_tracks) {
            ret = ff_vvc_annexb2mp4_buf(pkt->data, &reformatted_data,
                                        &size, 0, NULL);
            if (ret < 0)
                return ret;
            avio_write(pb, reformatted_data, size);
        } else {
            size = ff_vvc_annexb2mp4(pb, pkt->data, pkt->size, 0, NULL);
        }
    } else if (par->codec_id == AV_CODEC_ID_AV1 && !trk->cenc.aes_ctr) {
        if (trk->hint_track >= 0 && trk->hint_track < mov->nb_tracks) {
            ret = ff_av1_filter_obus_buf(pkt->data, &reformatted_data,
                                         &size, &offset);
            if (ret < 0)
                return ret;
            avio_write(pb, reformatted_data, size);
        } else {
            size = ff_av1_filter_obus(pb, pkt->data, pkt->size);
            if (trk->mode == MODE_AVIF && !mov->avif_extent_length[pkt->stream_index]) {
                mov->avif_extent_length[pkt->stream_index] = size;
            }
        }

    } else if (par->codec_id == AV_CODEC_ID_AC3 ||
               par->codec_id == AV_CODEC_ID_EAC3) {
        size = handle_eac3(mov, pkt, trk);
        if (size < 0)
            return size;
        else if (!size)
            goto end;
        avio_write(pb, pkt->data, size);
    } else if (par->codec_id == AV_CODEC_ID_EIA_608) {
        size = 8;

        for (int i = 0; i < pkt->size; i += 3) {
            if (pkt->data[i] == 0xFC) {
                size += 2;
            }
        }
        avio_wb32(pb, size);
        ffio_wfourcc(pb, "cdat");
        for (int i = 0; i < pkt->size; i += 3) {
            if (pkt->data[i] == 0xFC) {
                avio_w8(pb, pkt->data[i + 1]);
                avio_w8(pb, pkt->data[i + 2]);
            }
        }
    } else {
        if (trk->cenc.aes_ctr) {
            if (par->codec_id == AV_CODEC_ID_H264 && par->extradata_size > 4) {
                int nal_size_length = (par->extradata[4] & 0x3) + 1;
                ret = ff_mov_cenc_avc_write_nal_units(s, &trk->cenc, nal_size_length, pb, pkt->data, size);
            } else if(par->codec_id == AV_CODEC_ID_HEVC && par->extradata_size > 21) {
                int nal_size_length = (par->extradata[21] & 0x3) + 1;
                ret = ff_mov_cenc_avc_write_nal_units(s, &trk->cenc, nal_size_length, pb, pkt->data, size);
            } else if(par->codec_id == AV_CODEC_ID_VVC) {
                ret = AVERROR_PATCHWELCOME;
            } else if(par->codec_id == AV_CODEC_ID_AV1) {
                av_assert0(size == pkt->size);
                ret = ff_mov_cenc_av1_write_obus(s, &trk->cenc, pb, pkt);
                if (ret > 0) {
                    size = ret;
                    ret = 0;
                }
            } else {
                ret = ff_mov_cenc_write_packet(&trk->cenc, pb, pkt->data, size);
            }

            if (ret) {
                goto err;
            }
        } else {
            avio_write(pb, pkt->data, size);
        }
    }

    if (trk->entry >= trk->cluster_capacity) {
        unsigned new_capacity = trk->entry + MOV_INDEX_CLUSTER_SIZE;
        void *cluster = av_realloc_array(trk->cluster, new_capacity, sizeof(*trk->cluster));
        if (!cluster) {
            ret = AVERROR(ENOMEM);
            goto err;
        }
        trk->cluster          = cluster;
        trk->cluster_capacity = new_capacity;
    }

    trk->cluster[trk->entry].pos              = avio_tell(pb) - size;
    trk->cluster[trk->entry].samples_in_chunk = samples_in_chunk;
    trk->cluster[trk->entry].chunkNum         = 0;
    trk->cluster[trk->entry].size             = size;
    trk->cluster[trk->entry].entries          = samples_in_chunk;
    trk->cluster[trk->entry].dts              = pkt->dts;
    trk->cluster[trk->entry].pts              = pkt->pts;
    if (!trk->squash_fragment_samples_to_one &&
        !trk->entry && trk->start_dts != AV_NOPTS_VALUE) {
        if (!trk->frag_discont) {
            /* First packet of a new fragment. We already wrote the duration
             * of the last packet of the previous fragment based on track_duration,
             * which might not exactly match our dts. Therefore adjust the dts
             * of this packet to be what the previous packets duration implies. */
            trk->cluster[trk->entry].dts = trk->start_dts + trk->track_duration;
            /* We also may have written the pts and the corresponding duration
             * in sidx/tfrf/tfxd tags; make sure the sidx pts and duration match up with
             * the next fragment. This means the cts of the first sample must
             * be the same in all fragments, unless end_pts was updated by
             * the packet causing the fragment to be written. */
            if ((mov->flags & FF_MOV_FLAG_DASH &&
                !(mov->flags & (FF_MOV_FLAG_GLOBAL_SIDX | FF_MOV_FLAG_SKIP_SIDX))) ||
                mov->mode == MODE_ISM)
                pkt->pts = pkt->dts + trk->end_pts - trk->cluster[trk->entry].dts;
        } else {
            /* New fragment, but discontinuous from previous fragments.
             * Pretend the duration sum of the earlier fragments is
             * pkt->dts - trk->start_dts. */
            trk->end_pts = AV_NOPTS_VALUE;
            trk->frag_discont = 0;
        }
    }

    if (!trk->entry && trk->start_dts == AV_NOPTS_VALUE && !mov->use_editlist &&
        s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO) {
        /* Not using edit lists and shifting the first track to start from zero.
         * If the other streams start from a later timestamp, we won't be able
         * to signal the difference in starting time without an edit list.
         * Thus move the timestamp for this first sample to 0, increasing
         * its duration instead. */
        trk->cluster[trk->entry].dts = trk->start_dts = 0;
    }
    if (trk->start_dts == AV_NOPTS_VALUE) {
        trk->start_dts = pkt->dts;
        if (trk->frag_discont) {
            if (mov->use_editlist) {
                /* Pretend the whole stream started at pts=0, with earlier fragments
                 * already written. If the stream started at pts=0, the duration sum
                 * of earlier fragments would have been pkt->pts. */
                trk->start_dts  = pkt->dts - pkt->pts;
            } else {
                /* Pretend the whole stream started at dts=0, with earlier fragments
                 * already written, with a duration summing up to pkt->dts. */
                trk->start_dts  = 0;
            }
            trk->frag_discont = 0;
        } else if (pkt->dts && mov->moov_written)
            av_log(s, AV_LOG_WARNING,
                   "Track %d starts with a nonzero dts %"PRId64", while the moov "
                   "already has been written. Set the delay_moov flag to handle "
                   "this case.\n",
                   pkt->stream_index, pkt->dts);
    }
    trk->track_duration = pkt->dts - trk->start_dts + pkt->duration;
    trk->last_sample_is_subtitle_end = 0;

    if (pkt->pts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_WARNING, "pts has no value\n");
        pkt->pts = pkt->dts;
    }
    if (pkt->dts != pkt->pts)
        trk->flags |= MOV_TRACK_CTTS;
    trk->cluster[trk->entry].cts   = pkt->pts - pkt->dts;
    trk->cluster[trk->entry].flags = 0;
    if (trk->start_cts == AV_NOPTS_VALUE || (pkt->dts <= 0 && trk->start_cts > pkt->pts - pkt->dts))
        trk->start_cts = pkt->pts - pkt->dts;
    if (trk->end_pts == AV_NOPTS_VALUE)
        trk->end_pts = trk->cluster[trk->entry].dts +
                       trk->cluster[trk->entry].cts + pkt->duration;
    else
        trk->end_pts = FFMAX(trk->end_pts, trk->cluster[trk->entry].dts +
                                           trk->cluster[trk->entry].cts +
                                           pkt->duration);

    if (par->codec_id == AV_CODEC_ID_VC1) {
        mov_parse_vc1_frame(pkt, trk);
    } else if (par->codec_id == AV_CODEC_ID_TRUEHD) {
        mov_parse_truehd_frame(pkt, trk);
    } else if (pkt->flags & AV_PKT_FLAG_KEY) {
        if (mov->mode == MODE_MOV && par->codec_id == AV_CODEC_ID_MPEG2VIDEO &&
            trk->entry > 0) { // force sync sample for the first key frame
            mov_parse_mpeg2_frame(pkt, &trk->cluster[trk->entry].flags);
            if (trk->cluster[trk->entry].flags & MOV_PARTIAL_SYNC_SAMPLE)
                trk->flags |= MOV_TRACK_STPS;
        } else {
            trk->cluster[trk->entry].flags = MOV_SYNC_SAMPLE;
        }
        if (trk->cluster[trk->entry].flags & MOV_SYNC_SAMPLE)
            trk->has_keyframes++;
    }
    if (pkt->flags & AV_PKT_FLAG_DISPOSABLE) {
        trk->cluster[trk->entry].flags |= MOV_DISPOSABLE_SAMPLE;
        trk->has_disposable++;
    }

    prft = (AVProducerReferenceTime *)av_packet_get_side_data(pkt, AV_PKT_DATA_PRFT, &prft_size);
    if (prft && prft_size == sizeof(AVProducerReferenceTime))
        memcpy(&trk->cluster[trk->entry].prft, prft, prft_size);
    else
        memset(&trk->cluster[trk->entry].prft, 0, sizeof(AVProducerReferenceTime));

    trk->entry++;
    trk->sample_count += samples_in_chunk;
    mov->mdat_size    += size;

    if (trk->hint_track >= 0 && trk->hint_track < mov->nb_tracks)
        ff_mov_add_hinted_packet(s, pkt, trk->hint_track, trk->entry,
                                 reformatted_data ? reformatted_data + offset
                                                  : NULL, size);

end:
err:

    if (pkt->data != reformatted_data)
        av_free(reformatted_data);
    return ret;
}

static int mov_write_single_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *trk = s->streams[pkt->stream_index]->priv_data;
    AVCodecParameters *par = trk->par;
    int64_t frag_duration = 0;
    int size = pkt->size;

    int ret = check_pkt(s, trk, pkt);
    if (ret < 0)
        return ret;

    if (mov->flags & FF_MOV_FLAG_FRAG_DISCONT) {
        for (int i = 0; i < mov->nb_streams; i++)
            mov->tracks[i].frag_discont = 1;
        mov->flags &= ~FF_MOV_FLAG_FRAG_DISCONT;
    }

    if (mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS) {
        if (trk->dts_shift == AV_NOPTS_VALUE)
            trk->dts_shift = pkt->pts - pkt->dts;
        pkt->dts += trk->dts_shift;
    }

    if (trk->par->codec_id == AV_CODEC_ID_MP4ALS ||
            trk->par->codec_id == AV_CODEC_ID_AAC ||
            trk->par->codec_id == AV_CODEC_ID_AV1 ||
            trk->par->codec_id == AV_CODEC_ID_FLAC) {
        size_t side_size;
        uint8_t *side = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA, &side_size);
        if (side && side_size > 0 && (side_size != par->extradata_size || memcmp(side, par->extradata, side_size))) {
            void *newextra = av_mallocz(side_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!newextra)
                return AVERROR(ENOMEM);
            av_free(par->extradata);
            par->extradata = newextra;
            memcpy(par->extradata, side, side_size);
            par->extradata_size = side_size;
            if (!pkt->size) // Flush packet
                mov->need_rewrite_extradata = 1;
        }
    }

    if (!pkt->size) {
        if (trk->start_dts == AV_NOPTS_VALUE && trk->frag_discont) {
            trk->start_dts = pkt->dts;
            if (pkt->pts != AV_NOPTS_VALUE)
                trk->start_cts = pkt->pts - pkt->dts;
            else
                trk->start_cts = 0;
        }

        return 0;             /* Discard 0 sized packets */
    }

    if (trk->entry && pkt->stream_index < mov->nb_streams)
        frag_duration = av_rescale_q(pkt->dts - trk->cluster[0].dts,
                s->streams[pkt->stream_index]->time_base,
                AV_TIME_BASE_Q);
    if ((mov->max_fragment_duration &&
                frag_duration >= mov->max_fragment_duration) ||
            (mov->max_fragment_size && mov->mdat_size + size >= mov->max_fragment_size) ||
            (mov->flags & FF_MOV_FLAG_FRAG_KEYFRAME &&
             par->codec_type == AVMEDIA_TYPE_VIDEO &&
             trk->entry && pkt->flags & AV_PKT_FLAG_KEY) ||
            (mov->flags & FF_MOV_FLAG_FRAG_EVERY_FRAME)) {
        if (frag_duration >= mov->min_fragment_duration) {
            if (trk->entry) {
                // Set the duration of this track to line up with the next
                // sample in this track. This avoids relying on AVPacket
                // duration, but only helps for this particular track, not
                // for the other ones that are flushed at the same time.
                //
                // If we have trk->entry == 0, no fragment will be written
                // for this track, and we can't adjust the track end here.
                trk->track_duration = pkt->dts - trk->start_dts;
                if (pkt->pts != AV_NOPTS_VALUE)
                    trk->end_pts = pkt->pts;
                else
                    trk->end_pts = pkt->dts;
                trk->end_reliable = 1;
            }
            mov_auto_flush_fragment(s, 0);
        }
    }

    return ff_mov_write_packet(s, pkt);
}

static int mov_write_subtitle_end_packet(AVFormatContext *s,
                                         int stream_index,
                                         int64_t dts) {
    MOVMuxContext *mov = s->priv_data;
    AVPacket *end = mov->pkt;
    uint8_t data[2] = {0};
    int ret;

    end->size = sizeof(data);
    end->data = data;
    end->pts = dts;
    end->dts = dts;
    end->duration = 0;
    end->stream_index = stream_index;

    ret = mov_write_single_packet(s, end);
    av_packet_unref(end);

    return ret;
}

#if CONFIG_IAMFENC
static int mov_build_iamf_packet(AVFormatContext *s, MOVTrack *trk, AVPacket *pkt)
{
    uint8_t *data;
    int ret;

    if (pkt->stream_index == trk->first_iamf_idx) {
        ret = ff_iamf_write_parameter_blocks(trk->iamf, trk->iamf_buf, pkt, s);
        if (ret < 0)
            return ret;
    }

    ret = ff_iamf_write_audio_frame(trk->iamf, trk->iamf_buf,
                                    s->streams[pkt->stream_index]->id, pkt);
    if (ret < 0)
        return ret;

    if (pkt->stream_index != trk->last_iamf_idx)
        return AVERROR(EAGAIN);

    ret = avio_close_dyn_buf(trk->iamf_buf, &data);
    trk->iamf_buf = NULL;
    if (!ret) {
        if (pkt->size) {
            // Either all or none of the packets for a single
            // IA Sample may be empty.
            av_log(s, AV_LOG_ERROR, "Unexpected packet from "
                                     "stream #%d\n", pkt->stream_index);
            ret = AVERROR_INVALIDDATA;
        }
        av_free(data);
        return ret;
    }

    av_buffer_unref(&pkt->buf);
    pkt->buf = av_buffer_create(data, ret, NULL, NULL, 0);
    if (!pkt->buf) {
        av_free(data);
        return AVERROR(ENOMEM);
    }
    pkt->data = data;
    pkt->size = ret;
    pkt->stream_index = trk->first_iamf_idx;

    return avio_open_dyn_buf(&trk->iamf_buf);
}
#endif

static int mov_write_emsg_tag(AVIOContext *pb, AVStream *st, AVPacket *pkt)
{
    int64_t pos = avio_tell(pb);
    const char *scheme_id_uri = "https://aomedia.org/emsg/ID3";
    const char *value = "";

    av_assert0(st->time_base.num == 1);

    avio_write_marker(pb,
                      av_rescale_q(pkt->pts, st->time_base, AV_TIME_BASE_Q),
                      AVIO_DATA_MARKER_BOUNDARY_POINT);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "emsg");
    avio_w8(pb, 1); /* version */
    avio_wb24(pb, 0);
    avio_wb32(pb, st->time_base.den); /* timescale */
    avio_wb64(pb, pkt->pts); /* presentation_time */
    avio_wb32(pb, 0xFFFFFFFFU); /* event_duration */
    avio_wb32(pb, 0); /* id */
    /* null terminated UTF8 strings */
    avio_write(pb, scheme_id_uri, strlen(scheme_id_uri) + 1);
    avio_write(pb, value, strlen(value) + 1);
    avio_write(pb, pkt->data, pkt->size);

    return update_size(pb, pos);
}

static int mov_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *trk;

    if (!pkt) {
        mov_flush_fragment(s, 1);
        return 1;
    }

    if (s->streams[pkt->stream_index]->codecpar->codec_id == AV_CODEC_ID_TIMED_ID3) {
        mov_write_emsg_tag(s->pb, s->streams[pkt->stream_index], pkt);
        return 0;
    }

    trk = s->streams[pkt->stream_index]->priv_data;

#if CONFIG_IAMFENC
    if (trk->iamf) {
        int ret = mov_build_iamf_packet(s, trk, pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN))
                return 0;
            av_log(s, AV_LOG_ERROR, "Error assembling an IAMF packet "
                                    "for stream #%d\n", trk->st->index);
            return ret;
        }
    }
#endif

    if (is_cover_image(trk->st)) {
        int ret;

        if (trk->st->nb_frames >= 1) {
            if (trk->st->nb_frames == 1)
                av_log(s, AV_LOG_WARNING, "Got more than one picture in stream %d,"
                       " ignoring.\n", pkt->stream_index);
            return 0;
        }

        if ((ret = av_packet_ref(trk->cover_image, pkt)) < 0)
            return ret;

        return 0;
    } else {
        int i;

        if (!pkt->size)
            return mov_write_single_packet(s, pkt); /* Passthrough. */

        /*
         * Subtitles require special handling.
         *
         * 1) For full complaince, every track must have a sample at
         * dts == 0, which is rarely true for subtitles. So, as soon
         * as we see any packet with dts > 0, write an empty subtitle
         * at dts == 0 for any subtitle track with no samples in it.
         *
         * 2) For each subtitle track, check if the current packet's
         * dts is past the duration of the last subtitle sample. If
         * so, we now need to write an end sample for that subtitle.
         *
         * This must be done conditionally to allow for subtitles that
         * immediately replace each other, in which case an end sample
         * is not needed, and is, in fact, actively harmful.
         *
         * 3) See mov_write_trailer for how the final end sample is
         * handled.
         */
        for (i = 0; i < mov->nb_tracks; i++) {
            MOVTrack *trk = &mov->tracks[i];
            int ret;

            if (trk->par->codec_id == AV_CODEC_ID_MOV_TEXT &&
                trk->track_duration < pkt->dts &&
                (trk->entry == 0 || !trk->last_sample_is_subtitle_end)) {
                ret = mov_write_subtitle_end_packet(s, i, trk->track_duration);
                if (ret < 0) return ret;
                trk->last_sample_is_subtitle_end = 1;
            }
        }

        if (trk->squash_fragment_samples_to_one) {
            /*
             * If the track has to have its samples squashed into one sample,
             * we just take it into the track's queue.
             * This will then be utilized as the samples get written in either
             * mov_flush_fragment or when the mux is finalized in
             * mov_write_trailer.
             */
            int ret = AVERROR_BUG;

            if (pkt->pts == AV_NOPTS_VALUE) {
                av_log(s, AV_LOG_ERROR,
                       "Packets without a valid presentation timestamp are "
                       "not supported with packet squashing!\n");
                return AVERROR(EINVAL);
            }

            /* The following will reset pkt and is only allowed to be used
             * because we return immediately. afterwards. */
            if ((ret = avpriv_packet_list_put(&trk->squashed_packet_queue,
                                              pkt, NULL, 0)) < 0) {
                return ret;
            }

            return 0;
        }


        if (trk->mode == MODE_MOV && trk->par->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVPacket *opkt = pkt;
            int reshuffle_ret, ret;
            if (trk->is_unaligned_qt_rgb) {
                int64_t bpc = trk->par->bits_per_coded_sample != 15 ? trk->par->bits_per_coded_sample : 16;
                int expected_stride = ((trk->par->width * bpc + 15) >> 4)*2;
                reshuffle_ret = ff_reshuffle_raw_rgb(s, &pkt, trk->par, expected_stride);
                if (reshuffle_ret < 0)
                    return reshuffle_ret;
            } else
                reshuffle_ret = 0;
            if (trk->par->format == AV_PIX_FMT_PAL8 && !trk->pal_done) {
                ret = ff_get_packet_palette(s, opkt, reshuffle_ret, trk->palette);
                if (ret < 0)
                    goto fail;
                if (ret)
                    trk->pal_done++;
            } else if (trk->par->codec_id == AV_CODEC_ID_RAWVIDEO &&
                       (trk->par->format == AV_PIX_FMT_GRAY8 ||
                       trk->par->format == AV_PIX_FMT_MONOBLACK)) {
                ret = av_packet_make_writable(pkt);
                if (ret < 0)
                    goto fail;
                for (i = 0; i < pkt->size; i++)
                    pkt->data[i] = ~pkt->data[i];
            }
            if (reshuffle_ret) {
                ret = mov_write_single_packet(s, pkt);
fail:
                if (reshuffle_ret)
                    av_packet_free(&pkt);
                return ret;
            }
        }

        return mov_write_single_packet(s, pkt);
    }
}

// QuickTime chapters involve an additional text track with the chapter names
// as samples, and a tref pointing from the other tracks to the chapter one.
static int mov_create_chapter_track(AVFormatContext *s, int tracknum)
{
    static const uint8_t stub_header[] = {
        // TextSampleEntry
        0x00, 0x00, 0x00, 0x01, // displayFlags
        0x00, 0x00,             // horizontal + vertical justification
        0x00, 0x00, 0x00, 0x00, // bgColourRed/Green/Blue/Alpha
        // BoxRecord
        0x00, 0x00, 0x00, 0x00, // defTextBoxTop/Left
        0x00, 0x00, 0x00, 0x00, // defTextBoxBottom/Right
        // StyleRecord
        0x00, 0x00, 0x00, 0x00, // startChar + endChar
        0x00, 0x01,             // fontID
        0x00, 0x00,             // fontStyleFlags + fontSize
        0x00, 0x00, 0x00, 0x00, // fgColourRed/Green/Blue/Alpha
        // FontTableBox
        0x00, 0x00, 0x00, 0x0D, // box size
        'f', 't', 'a', 'b',     // box atom name
        0x00, 0x01,             // entry count
        // FontRecord
        0x00, 0x01,             // font ID
        0x00,                   // font name length
    };
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *track = &mov->tracks[tracknum];
    AVPacket *pkt = mov->pkt;
    int i, len;
    int ret;

    track->mode = mov->mode;
    track->tag = MKTAG('t','e','x','t');
    track->timescale = mov->movie_timescale;
    track->par = avcodec_parameters_alloc();
    if (!track->par)
        return AVERROR(ENOMEM);
    track->par->codec_type = AVMEDIA_TYPE_SUBTITLE;
    ret = ff_alloc_extradata(track->par, sizeof(stub_header));
    if (ret < 0)
        return ret;
    memcpy(track->par->extradata, stub_header, sizeof(stub_header));

    pkt->stream_index = tracknum;
    pkt->flags = AV_PKT_FLAG_KEY;

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *c = s->chapters[i];
        AVDictionaryEntry *t;

        int64_t end = av_rescale_q(c->end, c->time_base, (AVRational){1,mov->movie_timescale});
        pkt->pts = pkt->dts = av_rescale_q(c->start, c->time_base, (AVRational){1,mov->movie_timescale});
        pkt->duration = end - pkt->dts;

        if ((t = av_dict_get(c->metadata, "title", NULL, 0))) {
            static const char encd[12] = {
                0x00, 0x00, 0x00, 0x0C,
                'e',  'n',  'c',  'd',
                0x00, 0x00, 0x01, 0x00 };
            len      = strlen(t->value);
            pkt->size = len + 2 + 12;
            pkt->data = av_malloc(pkt->size);
            if (!pkt->data) {
                av_packet_unref(pkt);
                return AVERROR(ENOMEM);
            }
            AV_WB16(pkt->data, len);
            memcpy(pkt->data + 2, t->value, len);
            memcpy(pkt->data + len + 2, encd, sizeof(encd));
            ff_mov_write_packet(s, pkt);
            av_freep(&pkt->data);
        }
    }

    av_packet_unref(mov->pkt);

    return 0;
}


static int mov_check_timecode_track(AVFormatContext *s, AVTimecode *tc, AVStream *src_st, const char *tcstr)
{
    int ret;

    /* compute the frame number */
    ret = av_timecode_init_from_string(tc, src_st->avg_frame_rate, tcstr, s);
    return ret;
}

static int mov_create_timecode_track(AVFormatContext *s, int index, int src_index, AVTimecode tc)
{
    MOVMuxContext *mov  = s->priv_data;
    MOVTrack *track     = &mov->tracks[index];
    AVStream *src_st    = mov->tracks[src_index].st;
    uint8_t data[4];
    AVPacket *pkt = mov->pkt;
    AVRational rate = src_st->avg_frame_rate;
    int ret;

    /* tmcd track based on video stream */
    track->mode      = mov->mode;
    track->tag       = MKTAG('t','m','c','d');
    track->src_track = src_index;
    track->timescale = mov->tracks[src_index].timescale;
    if (tc.flags & AV_TIMECODE_FLAG_DROPFRAME)
        track->timecode_flags |= MOV_TIMECODE_FLAG_DROPFRAME;

    /* set st to src_st for metadata access*/
    track->st = src_st;

    /* encode context: tmcd data stream */
    track->par = avcodec_parameters_alloc();
    if (!track->par)
        return AVERROR(ENOMEM);
    track->par->codec_type = AVMEDIA_TYPE_DATA;
    track->par->codec_tag  = track->tag;
    track->st->avg_frame_rate = rate;

    /* the tmcd track just contains one packet with the frame number */
    pkt->data = data;
    pkt->stream_index = index;
    pkt->flags = AV_PKT_FLAG_KEY;
    pkt->pts = pkt->dts = av_rescale_q(tc.start, av_inv_q(rate), (AVRational){1,mov->movie_timescale});
    pkt->size = 4;
    AV_WB32(pkt->data, tc.start);
    ret = ff_mov_write_packet(s, pkt);
    av_packet_unref(pkt);
    return ret;
}

/*
 * st->disposition controls the "enabled" flag in the tkhd tag.
 * QuickTime will not play a track if it is not enabled.  So make sure
 * that one track of each type (audio, video, subtitle) is enabled.
 *
 * Subtitles are special.  For audio and video, setting "enabled" also
 * makes the track "default" (i.e. it is rendered when played). For
 * subtitles, an "enabled" subtitle is not rendered by default, but
 * if no subtitle is enabled, the subtitle menu in QuickTime will be
 * empty!
 */
static void enable_tracks(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int i;
    int enabled[AVMEDIA_TYPE_NB];
    int first[AVMEDIA_TYPE_NB];

    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        enabled[i] = 0;
        first[i] = -1;
    }

    for (i = 0; i < mov->nb_streams; i++) {
        AVStream *st = mov->tracks[i].st;

        if (st->codecpar->codec_type <= AVMEDIA_TYPE_UNKNOWN ||
            st->codecpar->codec_type >= AVMEDIA_TYPE_NB ||
            is_cover_image(st))
            continue;

        if (first[st->codecpar->codec_type] < 0)
            first[st->codecpar->codec_type] = i;
        if (st->disposition & AV_DISPOSITION_DEFAULT) {
            mov->tracks[i].flags |= MOV_TRACK_ENABLED;
            enabled[st->codecpar->codec_type]++;
        }
    }

    for (i = 0; i < AVMEDIA_TYPE_NB; i++) {
        switch (i) {
        case AVMEDIA_TYPE_VIDEO:
        case AVMEDIA_TYPE_AUDIO:
        case AVMEDIA_TYPE_SUBTITLE:
            if (enabled[i] > 1)
                mov->per_stream_grouping = 1;
            if (!enabled[i] && first[i] >= 0)
                mov->tracks[first[i]].flags |= MOV_TRACK_ENABLED;
            break;
        }
    }
}

static void mov_free(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;

    for (int i = 0; i < s->nb_streams; i++)
        s->streams[i]->priv_data = NULL;

    if (!mov->tracks)
        return;

    if (mov->chapter_track) {
        avcodec_parameters_free(&mov->tracks[mov->chapter_track].par);
    }

    for (int i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *const track = &mov->tracks[i];

        if (track->tag == MKTAG('r','t','p',' '))
            ff_mov_close_hinting(track);
        else if (track->tag == MKTAG('t','m','c','d') && mov->nb_meta_tmcd)
            av_freep(&track->par);
        av_freep(&track->cluster);
        av_freep(&track->cluster_written);
        av_freep(&track->frag_info);
        av_packet_free(&track->cover_image);

        if (track->eac3_priv) {
            struct eac3_info *info = track->eac3_priv;
            av_packet_free(&info->pkt);
            av_freep(&track->eac3_priv);
        }
        if (track->vos_len)
            av_freep(&track->vos_data);

        ff_mov_cenc_free(&track->cenc);
        ffio_free_dyn_buf(&track->mdat_buf);

#if CONFIG_IAMFENC
        ffio_free_dyn_buf(&track->iamf_buf);
        if (track->iamf)
            ff_iamf_uninit_context(track->iamf);
        av_freep(&track->iamf);
#endif

        avpriv_packet_list_free(&track->squashed_packet_queue);
    }

    av_freep(&mov->tracks);
    ffio_free_dyn_buf(&mov->mdat_buf);
}

static uint32_t rgb_to_yuv(uint32_t rgb)
{
    uint8_t r, g, b;
    int y, cb, cr;

    r = (rgb >> 16) & 0xFF;
    g = (rgb >>  8) & 0xFF;
    b = (rgb      ) & 0xFF;

    y  = av_clip_uint8(( 16000 +  257 * r + 504 * g +  98 * b)/1000);
    cb = av_clip_uint8((128000 -  148 * r - 291 * g + 439 * b)/1000);
    cr = av_clip_uint8((128000 +  439 * r - 368 * g -  71 * b)/1000);

    return (y << 16) | (cr << 8) | cb;
}

static int mov_create_dvd_sub_decoder_specific_info(MOVTrack *track,
                                                    AVStream *st)
{
    int i, width = 720, height = 480;
    int have_palette = 0, have_size = 0;
    uint32_t palette[16];
    char *cur = st->codecpar->extradata;

    while (cur && *cur) {
        if (strncmp("palette:", cur, 8) == 0) {
            int i, count;
            count = sscanf(cur + 8,
                "%06"PRIx32", %06"PRIx32", %06"PRIx32", %06"PRIx32", "
                "%06"PRIx32", %06"PRIx32", %06"PRIx32", %06"PRIx32", "
                "%06"PRIx32", %06"PRIx32", %06"PRIx32", %06"PRIx32", "
                "%06"PRIx32", %06"PRIx32", %06"PRIx32", %06"PRIx32"",
                &palette[ 0], &palette[ 1], &palette[ 2], &palette[ 3],
                &palette[ 4], &palette[ 5], &palette[ 6], &palette[ 7],
                &palette[ 8], &palette[ 9], &palette[10], &palette[11],
                &palette[12], &palette[13], &palette[14], &palette[15]);

            for (i = 0; i < count; i++) {
                palette[i] = rgb_to_yuv(palette[i]);
            }
            have_palette = 1;
        } else if (!strncmp("size:", cur, 5)) {
            sscanf(cur + 5, "%dx%d", &width, &height);
            have_size = 1;
        }
        if (have_palette && have_size)
            break;
        cur += strcspn(cur, "\n\r");
        cur += strspn(cur, "\n\r");
    }
    if (have_palette) {
        track->vos_data = av_malloc(16*4 + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!track->vos_data)
            return AVERROR(ENOMEM);
        for (i = 0; i < 16; i++) {
            AV_WB32(track->vos_data + i * 4, palette[i]);
        }
        memset(track->vos_data + 16*4, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        track->vos_len = 16 * 4;
    }
    st->codecpar->width = width;
    st->codecpar->height = track->height = height;

    return 0;
}

#if CONFIG_IAMFENC
static int mov_init_iamf_track(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *track;
    IAMFContext *iamf;
    int first_iamf_idx = INT_MAX, last_iamf_idx = 0;
    int nb_audio_elements = 0, nb_mix_presentations = 0;
    int ret;

    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];

        if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            nb_audio_elements++;
        if (stg->type == AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION)
            nb_mix_presentations++;
    }

    if (!nb_audio_elements && !nb_mix_presentations)
        return 0;

    if (nb_audio_elements < 1 || nb_audio_elements > 2 || nb_mix_presentations < 1) {
        av_log(s, AV_LOG_ERROR, "There must be >= 1 and <= 2 IAMF_AUDIO_ELEMENT and at least "
                                "one IAMF_MIX_PRESENTATION stream groups to write a IMAF track\n");
        return AVERROR(EINVAL);
    }

    iamf = av_mallocz(sizeof(*iamf));
    if (!iamf)
        return AVERROR(ENOMEM);


    for (int i = 0; i < s->nb_stream_groups; i++) {
        const AVStreamGroup *stg = s->stream_groups[i];
        switch(stg->type) {
        case AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT:
            for (int j = 0; j < stg->nb_streams; j++) {
                first_iamf_idx = FFMIN(stg->streams[j]->index, first_iamf_idx);
                last_iamf_idx  = FFMAX(stg->streams[j]->index, last_iamf_idx);
            }

            ret = ff_iamf_add_audio_element(iamf, stg, s);
            break;
        case AV_STREAM_GROUP_PARAMS_IAMF_MIX_PRESENTATION:
            ret = ff_iamf_add_mix_presentation(iamf, stg, s);
            break;
        default:
            av_assert0(0);
        }
        if (ret < 0)
            return ret;
    }

    track = &mov->tracks[first_iamf_idx];
    track->iamf = iamf;
    track->first_iamf_idx = first_iamf_idx;
    track->last_iamf_idx = last_iamf_idx;
    track->tag = MKTAG('i','a','m','f');

    for (int i = 0; i < s->nb_stream_groups; i++) {
        AVStreamGroup *stg = s->stream_groups[i];
        if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            continue;
        for (int j = 0; j < stg->nb_streams; j++)
            stg->streams[j]->priv_data = track;
    }

    ret = avio_open_dyn_buf(&track->iamf_buf);
    if (ret < 0)
        return ret;

    return 0;
}
#endif

static int mov_init(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int has_iamf = 0;
    int i, ret;

    mov->fc = s;
    mov->pkt = ffformatcontext(s)->pkt;

    /* Default mode == MP4 */
    mov->mode = MODE_MP4;

#define IS_MODE(muxer, config) (CONFIG_ ## config ## _MUXER && !strcmp(#muxer, s->oformat->name))
    if      (IS_MODE(3gp,   TGP)) mov->mode = MODE_3GP;
    else if (IS_MODE(3g2,   TG2)) mov->mode = MODE_3GP|MODE_3G2;
    else if (IS_MODE(mov,   MOV)) mov->mode = MODE_MOV;
    else if (IS_MODE(psp,   PSP)) mov->mode = MODE_PSP;
    else if (IS_MODE(ipod, IPOD)) mov->mode = MODE_IPOD;
    else if (IS_MODE(ismv, ISMV)) mov->mode = MODE_ISM;
    else if (IS_MODE(f4v,   F4V)) mov->mode = MODE_F4V;
    else if (IS_MODE(avif, AVIF)) mov->mode = MODE_AVIF;
#undef IS_MODE

    if (mov->flags & FF_MOV_FLAG_DELAY_MOOV)
        mov->flags |= FF_MOV_FLAG_EMPTY_MOOV;

    if (mov->mode == MODE_AVIF)
        mov->flags |= FF_MOV_FLAG_DELAY_MOOV;

    /* Set the FRAGMENT flag if any of the fragmentation methods are
     * enabled. */
    if (mov->max_fragment_duration || mov->max_fragment_size ||
        mov->flags & (FF_MOV_FLAG_EMPTY_MOOV |
                      FF_MOV_FLAG_FRAG_KEYFRAME |
                      FF_MOV_FLAG_FRAG_CUSTOM |
                      FF_MOV_FLAG_FRAG_EVERY_FRAME))
        mov->flags |= FF_MOV_FLAG_FRAGMENT;

    if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED &&
        mov->flags & FF_MOV_FLAG_FASTSTART) {
        av_log(s, AV_LOG_ERROR, "Setting both hybrid_fragmented and faststart is not supported.\n");
        return AVERROR(EINVAL);
    }

    /* Set other implicit flags immediately */
    if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED)
        mov->flags |= FF_MOV_FLAG_FRAGMENT;

    if (mov->mode == MODE_ISM)
        mov->flags |= FF_MOV_FLAG_EMPTY_MOOV | FF_MOV_FLAG_SEPARATE_MOOF |
                      FF_MOV_FLAG_FRAGMENT | FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS;
    if (mov->flags & FF_MOV_FLAG_DASH)
        mov->flags |= FF_MOV_FLAG_FRAGMENT | FF_MOV_FLAG_EMPTY_MOOV |
                      FF_MOV_FLAG_DEFAULT_BASE_MOOF;
    if (mov->flags & FF_MOV_FLAG_CMAF)
        mov->flags |= FF_MOV_FLAG_FRAGMENT | FF_MOV_FLAG_EMPTY_MOOV |
                      FF_MOV_FLAG_DEFAULT_BASE_MOOF | FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS;

    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV && s->flags & AVFMT_FLAG_AUTO_BSF) {
        av_log(s, AV_LOG_VERBOSE, "Empty MOOV enabled; disabling automatic bitstream filtering\n");
        s->flags &= ~AVFMT_FLAG_AUTO_BSF;
    }

    if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX && mov->flags & FF_MOV_FLAG_SKIP_SIDX) {
        av_log(s, AV_LOG_WARNING, "Global SIDX enabled; Ignoring skip_sidx option\n");
        mov->flags &= ~FF_MOV_FLAG_SKIP_SIDX;
    }

    if (mov->flags & FF_MOV_FLAG_FASTSTART) {
        mov->reserved_moov_size = -1;
    }

    if (mov->use_editlist < 0) {
        mov->use_editlist = 1;
        if (mov->flags & FF_MOV_FLAG_FRAGMENT &&
            !(mov->flags & FF_MOV_FLAG_DELAY_MOOV)) {
            // If we can avoid needing an edit list by shifting the
            // tracks, prefer that over (trying to) write edit lists
            // in fragmented output.
            if (s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_AUTO ||
                s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_MAKE_ZERO)
                mov->use_editlist = 0;
        }
    }
    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV &&
        !(mov->flags & FF_MOV_FLAG_DELAY_MOOV) && mov->use_editlist)
        av_log(s, AV_LOG_WARNING, "No meaningful edit list will be written when using empty_moov without delay_moov\n");

    if (!mov->use_editlist && s->avoid_negative_ts == AVFMT_AVOID_NEG_TS_AUTO &&
        !(mov->flags & FF_MOV_FLAG_NEGATIVE_CTS_OFFSETS))
        s->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_ZERO;

    /* Clear the omit_tfhd_offset flag if default_base_moof is set;
     * if the latter is set that's enough and omit_tfhd_offset doesn't
     * add anything extra on top of that. */
    if (mov->flags & FF_MOV_FLAG_OMIT_TFHD_OFFSET &&
        mov->flags & FF_MOV_FLAG_DEFAULT_BASE_MOOF)
        mov->flags &= ~FF_MOV_FLAG_OMIT_TFHD_OFFSET;

    if (mov->frag_interleave &&
        mov->flags & (FF_MOV_FLAG_OMIT_TFHD_OFFSET | FF_MOV_FLAG_SEPARATE_MOOF)) {
        av_log(s, AV_LOG_ERROR,
               "Sample interleaving in fragments is mutually exclusive with "
               "omit_tfhd_offset and separate_moof\n");
        return AVERROR(EINVAL);
    }

    /* Non-seekable output is ok if using fragmentation. If ism_lookahead
     * is enabled, we don't support non-seekable output at all. */
    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL) &&
        (!(mov->flags & FF_MOV_FLAG_FRAGMENT) || mov->ism_lookahead ||
         mov->mode == MODE_AVIF)) {
        av_log(s, AV_LOG_ERROR, "muxer does not support non seekable output\n");
        return AVERROR(EINVAL);
    }

    /* AVIF output must have at most two video streams (one for YUV and one for
     * alpha). */
    if (mov->mode == MODE_AVIF) {
        if (s->nb_streams > 2) {
            av_log(s, AV_LOG_ERROR, "AVIF output requires exactly one or two streams\n");
            return AVERROR(EINVAL);
        }
        if (s->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            (s->nb_streams > 1 && s->streams[1]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)) {
            av_log(s, AV_LOG_ERROR, "AVIF output supports only video streams\n");
            return AVERROR(EINVAL);
        }
        if (s->nb_streams > 1) {
            const AVPixFmtDescriptor *pixdesc =
                av_pix_fmt_desc_get(s->streams[1]->codecpar->format);
            if (pixdesc->nb_components != 1) {
                av_log(s, AV_LOG_ERROR, "Second stream for AVIF (alpha) output must have exactly one plane\n");
                return AVERROR(EINVAL);
            }
        }
        s->streams[0]->disposition |= AV_DISPOSITION_DEFAULT;
    }

#if CONFIG_IAMFENC
    for (i = 0; i < s->nb_stream_groups; i++) {
        AVStreamGroup *stg = s->stream_groups[i];

        if (stg->type != AV_STREAM_GROUP_PARAMS_IAMF_AUDIO_ELEMENT)
            continue;

        for (int j = 0; j < stg->nb_streams; j++) {
            AVStream *st = stg->streams[j];

            if (st->priv_data) {
                av_log(s, AV_LOG_ERROR, "Stream %d is present in more than one Stream Group of type "
                                        "IAMF Audio Element\n", j);
                return AVERROR(EINVAL);
            }
            st->priv_data = st;
        }
        has_iamf = 1;

        if (!mov->nb_tracks) // We support one track for the entire IAMF structure
            mov->nb_tracks++;
    }
#endif

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->priv_data)
            continue;
        // Don't produce a track in the output file for timed ID3 streams.
        if (st->codecpar->codec_id == AV_CODEC_ID_TIMED_ID3) {
            // Leave priv_data set to NULL for these AVStreams that don't
            // have a corresponding track.
            continue;
        }
        st->priv_data = st;
        mov->nb_tracks++;
    }

    mov->nb_streams = mov->nb_tracks;

    if (mov->mode & (MODE_MP4|MODE_MOV|MODE_IPOD) && s->nb_chapters)
        mov->chapter_track = mov->nb_tracks++;

    if (mov->flags & FF_MOV_FLAG_RTP_HINT) {
        for (i = 0; i < s->nb_streams; i++)
            if (rtp_hinting_needed(s->streams[i]))
                mov->nb_tracks++;
    }

    if (mov->write_btrt < 0) {
        mov->write_btrt = mov->mode == MODE_MP4;
    }

    if (   mov->write_tmcd == -1 && (mov->mode == MODE_MOV || mov->mode == MODE_MP4)
        || mov->write_tmcd == 1) {
        AVDictionaryEntry *global_tcr = av_dict_get(s->metadata, "timecode",
                                                    NULL, 0);

        /* +1 tmcd track for each video stream with a timecode */
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            AVDictionaryEntry *t = global_tcr;
            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                (t || (t=av_dict_get(st->metadata, "timecode", NULL, 0)))) {
                AVTimecode tc;
                ret = mov_check_timecode_track(s, &tc, st, t->value);
                if (ret >= 0)
                    mov->nb_meta_tmcd++;
            }
        }

        /* check if there is already a tmcd track to remux */
        if (mov->nb_meta_tmcd) {
            for (i = 0; i < s->nb_streams; i++) {
                AVStream *st = s->streams[i];
                if (st->codecpar->codec_tag == MKTAG('t','m','c','d')) {
                    av_log(s, AV_LOG_WARNING, "You requested a copy of the original timecode track "
                           "so timecode metadata are now ignored\n");
                    mov->nb_meta_tmcd = 0;
                }
            }
        }

        mov->nb_tracks += mov->nb_meta_tmcd;
    }

    // Reserve an extra stream for chapters for the case where chapters
    // are written in the trailer
    mov->tracks = av_calloc(mov->nb_tracks + 1, sizeof(*mov->tracks));
    if (!mov->tracks)
        return AVERROR(ENOMEM);

    if (mov->encryption_scheme_str != NULL && strcmp(mov->encryption_scheme_str, "none") != 0) {
        if (strcmp(mov->encryption_scheme_str, "cenc-aes-ctr") == 0) {
            mov->encryption_scheme = MOV_ENC_CENC_AES_CTR;

            if (mov->encryption_key_len != AES_CTR_KEY_SIZE) {
                av_log(s, AV_LOG_ERROR, "Invalid encryption key len %d expected %d\n",
                    mov->encryption_key_len, AES_CTR_KEY_SIZE);
                return AVERROR(EINVAL);
            }

            if (mov->encryption_kid_len != CENC_KID_SIZE) {
                av_log(s, AV_LOG_ERROR, "Invalid encryption kid len %d expected %d\n",
                    mov->encryption_kid_len, CENC_KID_SIZE);
                return AVERROR(EINVAL);
            }
        } else {
            av_log(s, AV_LOG_ERROR, "unsupported encryption scheme %s\n",
                mov->encryption_scheme_str);
            return AVERROR(EINVAL);
        }
    }

#if CONFIG_IAMFENC
    ret = mov_init_iamf_track(s);
    if (ret < 0)
        return ret;
#endif

    for (int j = 0, i = 0; j < s->nb_streams; j++) {
        AVStream *st = s->streams[j];

        if (st != st->priv_data) {
            if (has_iamf)
                i += has_iamf--;
            continue;
        }
        st->priv_data = &mov->tracks[i++];
    }

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st= s->streams[i];
        MOVTrack *track = st->priv_data;
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL,0);

        if (!track)
            continue;

        if (!track->st) {
            track->st  = st;
            track->par = st->codecpar;
        }
        track->language = ff_mov_iso639_to_lang(lang?lang->value:"und", mov->mode!=MODE_MOV);
        if (track->language < 0)
            track->language = 32767;  // Unspecified Macintosh language code
        track->mode = mov->mode;
        if (!track->tag)
            track->tag  = mov_find_codec_tag(s, track);
        if (!track->tag) {
            av_log(s, AV_LOG_ERROR, "Could not find tag for codec %s in stream #%d, "
                   "codec not currently supported in container\n",
                   avcodec_get_name(st->codecpar->codec_id), i);
            return AVERROR(EINVAL);
        }
        /* If hinting of this track is enabled by a later hint track,
         * this is updated. */
        track->hint_track = -1;
        track->start_dts  = AV_NOPTS_VALUE;
        track->start_cts  = AV_NOPTS_VALUE;
        track->end_pts    = AV_NOPTS_VALUE;
        track->dts_shift  = AV_NOPTS_VALUE;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (track->tag == MKTAG('m','x','3','p') || track->tag == MKTAG('m','x','3','n') ||
                track->tag == MKTAG('m','x','4','p') || track->tag == MKTAG('m','x','4','n') ||
                track->tag == MKTAG('m','x','5','p') || track->tag == MKTAG('m','x','5','n')) {
                if (st->codecpar->width != 720 || (st->codecpar->height != 608 && st->codecpar->height != 512)) {
                    av_log(s, AV_LOG_ERROR, "D-10/IMX must use 720x608 or 720x512 video resolution\n");
                    return AVERROR(EINVAL);
                }
                track->height = track->tag >> 24 == 'n' ? 486 : 576;
            }
            if (mov->video_track_timescale) {
                track->timescale = mov->video_track_timescale;
                if (mov->mode == MODE_ISM && mov->video_track_timescale != 10000000)
                    av_log(s, AV_LOG_WARNING, "Warning: some tools, like mp4split, assume a timescale of 10000000 for ISMV.\n");
            } else {
                track->timescale = st->time_base.den;
                while(track->timescale < 10000)
                    track->timescale *= 2;
            }
            if (st->codecpar->width > 65535 || st->codecpar->height > 65535) {
                av_log(s, AV_LOG_ERROR, "Resolution %dx%d too large for mov/mp4\n", st->codecpar->width, st->codecpar->height);
                return AVERROR(EINVAL);
            }
            if (track->mode == MODE_MOV && track->timescale > 100000)
                av_log(s, AV_LOG_WARNING,
                       "WARNING codec timebase is very high. If duration is too long,\n"
                       "file may not be playable by quicktime. Specify a shorter timebase\n"
                       "or choose different container.\n");
            if (track->mode == MODE_MOV &&
                track->par->codec_id == AV_CODEC_ID_RAWVIDEO &&
                track->tag == MKTAG('r','a','w',' ')) {
                enum AVPixelFormat pix_fmt = track->par->format;
                if (pix_fmt == AV_PIX_FMT_NONE && track->par->bits_per_coded_sample == 1)
                    pix_fmt = AV_PIX_FMT_MONOWHITE;
                track->is_unaligned_qt_rgb =
                        pix_fmt == AV_PIX_FMT_RGB24 ||
                        pix_fmt == AV_PIX_FMT_BGR24 ||
                        pix_fmt == AV_PIX_FMT_PAL8 ||
                        pix_fmt == AV_PIX_FMT_GRAY8 ||
                        pix_fmt == AV_PIX_FMT_MONOWHITE ||
                        pix_fmt == AV_PIX_FMT_MONOBLACK;
            }
            if (track->par->codec_id == AV_CODEC_ID_VP9 && track->mode != MODE_MP4) {
                av_log(s, AV_LOG_ERROR, "%s only supported in MP4.\n", avcodec_get_name(track->par->codec_id));
                return AVERROR(EINVAL);
            } else if (track->par->codec_id == AV_CODEC_ID_AV1 &&
                       track->mode != MODE_MP4 && track->mode != MODE_AVIF) {
                av_log(s, AV_LOG_ERROR, "%s only supported in MP4 and AVIF.\n", avcodec_get_name(track->par->codec_id));
                return AVERROR(EINVAL);
            } else if (track->par->codec_id == AV_CODEC_ID_VP8) {
                /* altref frames handling is not defined in the spec as of version v1.0,
                 * so just forbid muxing VP8 streams altogether until a new version does */
                av_log(s, AV_LOG_ERROR, "VP8 muxing is currently not supported.\n");
                return AVERROR_PATCHWELCOME;
            }
            if (is_cover_image(st)) {
                track->cover_image = av_packet_alloc();
                if (!track->cover_image)
                    return AVERROR(ENOMEM);
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            track->timescale = st->codecpar->sample_rate;
            if (!st->codecpar->frame_size && !av_get_bits_per_sample(st->codecpar->codec_id)) {
                av_log(s, AV_LOG_WARNING, "track %d: codec frame size is not set\n", i);
                track->audio_vbr = 1;
            }else if (st->codecpar->codec_id == AV_CODEC_ID_ADPCM_MS ||
                     st->codecpar->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV ||
                     st->codecpar->codec_id == AV_CODEC_ID_ILBC){
                if (!st->codecpar->block_align) {
                    av_log(s, AV_LOG_ERROR, "track %d: codec block align is not set for adpcm\n", i);
                    return AVERROR(EINVAL);
                }
                track->sample_size = st->codecpar->block_align;
            }else if (st->codecpar->frame_size > 1){ /* assume compressed audio */
                track->audio_vbr = 1;
            }else{
                track->sample_size = (av_get_bits_per_sample(st->codecpar->codec_id) >> 3) *
                                     st->codecpar->ch_layout.nb_channels;
            }
            if (st->codecpar->codec_id == AV_CODEC_ID_ILBC ||
                st->codecpar->codec_id == AV_CODEC_ID_ADPCM_IMA_QT) {
                track->audio_vbr = 1;
            }
            if (track->mode != MODE_MOV &&
                track->par->codec_id == AV_CODEC_ID_MP3 && track->timescale < 16000) {
                if (s->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
                    av_log(s, AV_LOG_ERROR, "track %d: muxing mp3 at %dhz is not standard, to mux anyway set strict to -1\n",
                        i, track->par->sample_rate);
                    return AVERROR(EINVAL);
                } else {
                    av_log(s, AV_LOG_WARNING, "track %d: muxing mp3 at %dhz is not standard in MP4\n",
                           i, track->par->sample_rate);
                }
            }
            if (track->par->codec_id == AV_CODEC_ID_FLAC ||
                track->par->codec_id == AV_CODEC_ID_TRUEHD ||
                track->par->codec_id == AV_CODEC_ID_OPUS) {
                if (track->mode != MODE_MP4) {
                    av_log(s, AV_LOG_ERROR, "%s only supported in MP4.\n", avcodec_get_name(track->par->codec_id));
                    return AVERROR(EINVAL);
                }
                if (track->par->codec_id == AV_CODEC_ID_TRUEHD &&
                    s->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
                    av_log(s, AV_LOG_ERROR,
                           "%s in MP4 support is experimental, add "
                           "'-strict %d' if you want to use it.\n",
                           avcodec_get_name(track->par->codec_id), FF_COMPLIANCE_EXPERIMENTAL);
                    return AVERROR_EXPERIMENTAL;
                }
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            track->timescale = st->time_base.den;

            if (track->par->codec_id == AV_CODEC_ID_TTML) {
                /* 14496-30 requires us to use a single sample per fragment
                   for TTML, for which we define a per-track flag.

                   We set the flag in case we are receiving TTML paragraphs
                   from the input, in other words in case we are not doing
                   stream copy. */
                track->squash_fragment_samples_to_one =
                    ff_is_ttml_stream_paragraph_based(track->par);

                if (mov->flags & FF_MOV_FLAG_FRAGMENT &&
                    track->squash_fragment_samples_to_one) {
                    av_log(s, AV_LOG_ERROR,
                           "Fragmentation is not currently supported for "
                           "TTML in MP4/ISMV (track synchronization between "
                           "subtitles and other media is not yet implemented)!\n");
                    return AVERROR_PATCHWELCOME;
                }

                if (track->mode != MODE_ISM &&
                    track->par->codec_tag == MOV_ISMV_TTML_TAG &&
                    s->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
                    av_log(s, AV_LOG_ERROR,
                           "ISMV style TTML support with the 'dfxp' tag in "
                           "non-ISMV formats is not officially supported. Add "
                           "'-strict unofficial' if you want to use it.\n");
                    return AVERROR_EXPERIMENTAL;
                }
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            track->timescale = st->time_base.den;
        } else {
            track->timescale = mov->movie_timescale;
        }
        if (!track->height)
            track->height = st->codecpar->height;
        /* The Protected Interoperable File Format (PIFF) standard, used by ISMV recommends but
           doesn't mandate a track timescale of 10,000,000. The muxer allows a custom timescale
           for video tracks, so if user-set, it isn't overwritten */
        if (mov->mode == MODE_ISM &&
            (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
            (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !mov->video_track_timescale))) {
             track->timescale = 10000000;
        }

        avpriv_set_pts_info(st, 64, 1, track->timescale);

        if (mov->encryption_scheme == MOV_ENC_CENC_AES_CTR) {
            ret = ff_mov_cenc_init(&track->cenc, mov->encryption_key,
                (track->par->codec_id == AV_CODEC_ID_H264 || track->par->codec_id == AV_CODEC_ID_HEVC ||
                 track->par->codec_id == AV_CODEC_ID_VVC || track->par->codec_id == AV_CODEC_ID_AV1),
                 track->par->codec_id, s->flags & AVFMT_FLAG_BITEXACT);
            if (ret)
                return ret;
        }
    }

    enable_tracks(s);
    return 0;
}

static int mov_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    MOVMuxContext *mov = s->priv_data;
    int ret, hint_track = 0, tmcd_track = 0, nb_tracks = mov->nb_streams;

    if (mov->mode & (MODE_MP4|MODE_MOV|MODE_IPOD) && s->nb_chapters)
        nb_tracks++;

    if (mov->flags & FF_MOV_FLAG_RTP_HINT) {
        hint_track = nb_tracks;
        for (int i = 0; i < mov->nb_streams; i++) {
            if (rtp_hinting_needed(mov->tracks[i].st))
                nb_tracks++;
        }
    }

    if (mov->nb_meta_tmcd)
        tmcd_track = nb_tracks;

    for (int i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        AVStream *st = track->st;

        /* copy extradata if it exists */
        if (st->codecpar->extradata_size) {
            if (st->codecpar->codec_id == AV_CODEC_ID_DVD_SUBTITLE)
                mov_create_dvd_sub_decoder_specific_info(track, st);
            else if (!TAG_IS_AVCI(track->tag) && st->codecpar->codec_id != AV_CODEC_ID_DNXHD) {
                track->vos_len  = st->codecpar->extradata_size;
                track->vos_data = av_malloc(track->vos_len + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!track->vos_data) {
                    return AVERROR(ENOMEM);
                }
                memcpy(track->vos_data, st->codecpar->extradata, track->vos_len);
                memset(track->vos_data + track->vos_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            }
        }

        if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO ||
            av_channel_layout_compare(&track->par->ch_layout,
                                      &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO))
            continue;

        for (int j = 0; j < mov->nb_streams; j++) {
            AVStream *stj= mov->tracks[j].st;
            MOVTrack *trackj= &mov->tracks[j];
            if (j == i)
                continue;

            if (stj->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                (trackj->par->ch_layout.nb_channels != 1 ||
                 !av_channel_layout_compare(&trackj->par->ch_layout,
                                            &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO))
            )
                track->mono_as_fc = -1;

            if (stj->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                av_channel_layout_compare(&trackj->par->ch_layout,
                                          &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO) &&
                trackj->par->ch_layout.nb_channels == 1 && track->mono_as_fc >= 0
            )
                track->mono_as_fc++;

            if (stj->codecpar->codec_type != AVMEDIA_TYPE_AUDIO ||
                av_channel_layout_compare(&trackj->par->ch_layout,
                                          &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO) ||
                trackj->language != track->language ||
                trackj->tag != track->tag
            )
                continue;
            track->multichannel_as_mono++;
        }
    }

    if (!(mov->flags & FF_MOV_FLAG_DELAY_MOOV)) {
        if ((ret = mov_write_identification(pb, s)) < 0)
            return ret;
    }

    if (mov->reserved_moov_size){
        mov->reserved_header_pos = avio_tell(pb);
        if (mov->reserved_moov_size > 0)
            avio_skip(pb, mov->reserved_moov_size);
    }

    if (mov->flags & FF_MOV_FLAG_FRAGMENT) {
        /* If no fragmentation options have been set, set a default. */
        if (!(mov->flags & (FF_MOV_FLAG_FRAG_KEYFRAME |
                            FF_MOV_FLAG_FRAG_CUSTOM |
                            FF_MOV_FLAG_FRAG_EVERY_FRAME)) &&
            !mov->max_fragment_duration && !mov->max_fragment_size)
            mov->flags |= FF_MOV_FLAG_FRAG_KEYFRAME;
        if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED) {
            avio_wb32(pb, 8); // placeholder for extended size field (64 bit)
            ffio_wfourcc(pb, mov->mode == MODE_MOV ? "wide" : "free");
            mov->mdat_pos = avio_tell(pb);
        }
    } else if (mov->mode != MODE_AVIF) {
        if (mov->flags & FF_MOV_FLAG_FASTSTART)
            mov->reserved_header_pos = avio_tell(pb);
        mov_write_mdat_tag(pb, mov);
    }

    ff_parse_creation_time_metadata(s, &mov->time, 1);
    if (mov->time)
        mov->time += 0x7C25B080; // 1970 based -> 1904 based

    if (mov->chapter_track)
        if ((ret = mov_create_chapter_track(s, mov->chapter_track)) < 0)
            return ret;

    if (mov->flags & FF_MOV_FLAG_RTP_HINT) {
        for (int i = 0; i < mov->nb_streams; i++) {
            if (rtp_hinting_needed(mov->tracks[i].st)) {
                if ((ret = ff_mov_init_hinting(s, hint_track, i)) < 0)
                    return ret;
                hint_track++;
            }
        }
    }

    if (mov->nb_meta_tmcd) {
        const AVDictionaryEntry *t, *global_tcr = av_dict_get(s->metadata,
                                                              "timecode", NULL, 0);
        /* Initialize the tmcd tracks */
        for (int i = 0; i < mov->nb_streams; i++) {
            AVStream *st = mov->tracks[i].st;
            t = global_tcr;

            if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                AVTimecode tc;
                if (!t)
                    t = av_dict_get(st->metadata, "timecode", NULL, 0);
                if (!t)
                    continue;
                if (mov_check_timecode_track(s, &tc, st, t->value) < 0)
                    continue;
                if ((ret = mov_create_timecode_track(s, tmcd_track, i, tc)) < 0)
                    return ret;
                tmcd_track++;
            }
        }
    }

    avio_flush(pb);

    if (mov->flags & FF_MOV_FLAG_ISML)
        mov_write_isml_manifest(pb, mov, s);

    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV &&
        !(mov->flags & FF_MOV_FLAG_DELAY_MOOV)) {
        if ((ret = mov_write_moov_tag(pb, mov, s)) < 0)
            return ret;
        mov->moov_written = 1;
        if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX)
            mov->reserved_header_pos = avio_tell(pb);
    }

    return 0;
}

static int get_moov_size(AVFormatContext *s)
{
    int ret;
    AVIOContext *moov_buf;
    MOVMuxContext *mov = s->priv_data;

    if ((ret = ffio_open_null_buf(&moov_buf)) < 0)
        return ret;
    if ((ret = mov_write_moov_tag(moov_buf, mov, s)) < 0)
        return ret;
    return ffio_close_null_buf(moov_buf);
}

static int get_sidx_size(AVFormatContext *s)
{
    int ret;
    AVIOContext *buf;
    MOVMuxContext *mov = s->priv_data;

    if ((ret = ffio_open_null_buf(&buf)) < 0)
        return ret;
    mov_write_sidx_tags(buf, mov, -1, 0);
    return ffio_close_null_buf(buf);
}

/*
 * This function gets the moov size if moved to the top of the file: the chunk
 * offset table can switch between stco (32-bit entries) to co64 (64-bit
 * entries) when the moov is moved to the beginning, so the size of the moov
 * would change. It also updates the chunk offset tables.
 */
static int compute_moov_size(AVFormatContext *s)
{
    int i, moov_size, moov_size2;
    MOVMuxContext *mov = s->priv_data;

    moov_size = get_moov_size(s);
    if (moov_size < 0)
        return moov_size;

    for (i = 0; i < mov->nb_tracks; i++)
        mov->tracks[i].data_offset += moov_size;

    moov_size2 = get_moov_size(s);
    if (moov_size2 < 0)
        return moov_size2;

    /* if the size changed, we just switched from stco to co64 and need to
     * update the offsets */
    if (moov_size2 != moov_size)
        for (i = 0; i < mov->nb_tracks; i++)
            mov->tracks[i].data_offset += moov_size2 - moov_size;

    return moov_size2;
}

static int compute_sidx_size(AVFormatContext *s)
{
    int i, sidx_size;
    MOVMuxContext *mov = s->priv_data;

    sidx_size = get_sidx_size(s);
    if (sidx_size < 0)
        return sidx_size;

    for (i = 0; i < mov->nb_tracks; i++)
        mov->tracks[i].data_offset += sidx_size;

    return sidx_size;
}

static int shift_data(AVFormatContext *s)
{
    int moov_size;
    MOVMuxContext *mov = s->priv_data;

    if (mov->flags & FF_MOV_FLAG_FRAGMENT)
        moov_size = compute_sidx_size(s);
    else
        moov_size = compute_moov_size(s);
    if (moov_size < 0)
        return moov_size;

    return ff_format_shift_data(s, mov->reserved_header_pos, moov_size);
}

static int mov_write_trailer(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    int res = 0;
    int i;
    int64_t moov_pos;

    if (mov->need_rewrite_extradata) {
        for (i = 0; i < mov->nb_streams; i++) {
            MOVTrack *track = &mov->tracks[i];
            AVCodecParameters *par = track->par;

            track->vos_len  = par->extradata_size;
            av_freep(&track->vos_data);
            track->vos_data = av_malloc(track->vos_len + AV_INPUT_BUFFER_PADDING_SIZE);
            if (!track->vos_data)
                return AVERROR(ENOMEM);
            memcpy(track->vos_data, par->extradata, track->vos_len);
            memset(track->vos_data + track->vos_len, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        }
        mov->need_rewrite_extradata = 0;
    }

    /*
     * Before actually writing the trailer, make sure that there are no
     * dangling subtitles, that need a terminating sample.
     */
    for (i = 0; i < mov->nb_tracks; i++) {
        MOVTrack *trk = &mov->tracks[i];
        if (trk->par->codec_id == AV_CODEC_ID_MOV_TEXT &&
            !trk->last_sample_is_subtitle_end) {
            mov_write_subtitle_end_packet(s, i, trk->track_duration);
            trk->last_sample_is_subtitle_end = 1;
        }
    }

    // Check if we have any tracks that require squashing.
    // In that case, we'll have to write the packet here.
    if ((res = mov_write_squashed_packets(s)) < 0)
        return res;

    // If there were no chapters when the header was written, but there
    // are chapters now, write them in the trailer.  This only works
    // when we are not doing fragments.
    if (!mov->chapter_track && !(mov->flags & FF_MOV_FLAG_FRAGMENT)) {
        if (mov->mode & (MODE_MP4|MODE_MOV|MODE_IPOD) && s->nb_chapters) {
            mov->chapter_track = mov->nb_tracks++;
            if ((res = mov_create_chapter_track(s, mov->chapter_track)) < 0)
                return res;
        }
    }

    if (!(mov->flags & FF_MOV_FLAG_FRAGMENT) ||
        mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED) {
        if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED) {
            mov_flush_fragment(s, 1);
            mov->mdat_size = avio_tell(pb) - mov->mdat_pos - 8;
            for (i = 0; i < mov->nb_tracks; i++) {
                MOVTrack *track = &mov->tracks[i];
                track->data_offset = 0;
                av_free(track->cluster);
                track->cluster = track->cluster_written;
                track->entry   = track->entry_written;
                track->cluster_written = NULL;
                track->entry_written   = 0;
                track->chunkCount = 0; // Force build_chunks to rebuild the list of chunks
            }
            // Clear the empty_moov flag, as we do want the moov to include
            // all the samples at this point.
            mov->flags &= ~FF_MOV_FLAG_EMPTY_MOOV;
        }

        moov_pos = avio_tell(pb);

        /* Write size of mdat tag */
        if (mov->mdat_size + 8 <= UINT32_MAX) {
            avio_seek(pb, mov->mdat_pos, SEEK_SET);
            avio_wb32(pb, mov->mdat_size + 8);
            if (mov->flags & FF_MOV_FLAG_HYBRID_FRAGMENTED)
                ffio_wfourcc(pb, "mdat"); // overwrite the original moov into a mdat
        } else {
            /* overwrite 'wide' placeholder atom */
            avio_seek(pb, mov->mdat_pos - 8, SEEK_SET);
            /* special value: real atom size will be 64 bit value after
             * tag field */
            avio_wb32(pb, 1);
            ffio_wfourcc(pb, "mdat");
            avio_wb64(pb, mov->mdat_size + 16);
        }
        avio_seek(pb, mov->reserved_moov_size > 0 ? mov->reserved_header_pos : moov_pos, SEEK_SET);

        if (mov->flags & FF_MOV_FLAG_FASTSTART) {
            av_log(s, AV_LOG_INFO, "Starting second pass: moving the moov atom to the beginning of the file\n");
            res = shift_data(s);
            if (res < 0)
                return res;
            avio_seek(pb, mov->reserved_header_pos, SEEK_SET);
            if ((res = mov_write_moov_tag(pb, mov, s)) < 0)
                return res;
        } else if (mov->reserved_moov_size > 0) {
            int64_t size;
            if ((res = mov_write_moov_tag(pb, mov, s)) < 0)
                return res;
            size = mov->reserved_moov_size - (avio_tell(pb) - mov->reserved_header_pos);
            if (size < 8){
                av_log(s, AV_LOG_ERROR, "reserved_moov_size is too small, needed %"PRId64" additional\n", 8-size);
                return AVERROR(EINVAL);
            }
            avio_wb32(pb, size);
            ffio_wfourcc(pb, "free");
            ffio_fill(pb, 0, size - 8);
            avio_seek(pb, moov_pos, SEEK_SET);
        } else {
            if ((res = mov_write_moov_tag(pb, mov, s)) < 0)
                return res;
        }
        res = 0;
    } else {
        mov_auto_flush_fragment(s, 1);
        for (i = 0; i < mov->nb_tracks; i++)
           mov->tracks[i].data_offset = 0;
        if (mov->flags & FF_MOV_FLAG_GLOBAL_SIDX) {
            int64_t end;
            av_log(s, AV_LOG_INFO, "Starting second pass: inserting sidx atoms\n");
            res = shift_data(s);
            if (res < 0)
                return res;
            end = avio_tell(pb);
            avio_seek(pb, mov->reserved_header_pos, SEEK_SET);
            mov_write_sidx_tags(pb, mov, -1, 0);
            avio_seek(pb, end, SEEK_SET);
        }
        if (!(mov->flags & FF_MOV_FLAG_SKIP_TRAILER)) {
            avio_write_marker(s->pb, AV_NOPTS_VALUE, AVIO_DATA_MARKER_TRAILER);
            res = mov_write_mfra_tag(pb, mov);
            if (res < 0)
                return res;
        }
    }

    return res;
}

static int mov_check_bitstream(AVFormatContext *s, AVStream *st,
                               const AVPacket *pkt)
{
    int ret = 1;

    if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
        if (pkt->size > 2 && (AV_RB16(pkt->data) & 0xfff0) == 0xfff0)
            ret = ff_stream_add_bitstream_filter(st, "aac_adtstoasc", NULL);
    } else if (st->codecpar->codec_id == AV_CODEC_ID_VP9) {
        ret = ff_stream_add_bitstream_filter(st, "vp9_superframe", NULL);
    }

    return ret;
}

#if CONFIG_AVIF_MUXER
static int avif_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    MOVMuxContext *mov = s->priv_data;
    int64_t pos_backup, extent_offsets[2];
    uint8_t *buf;
    int buf_size, moov_size;

    if (mov->moov_written) return 0;

    mov->is_animated_avif = s->streams[0]->nb_frames > 1;
    if (mov->is_animated_avif && mov->nb_streams > 1) {
        // For animated avif with alpha channel, we need to write a tref tag
        // with type "auxl".
        mov->tracks[1].tref_tag = MKTAG('a', 'u', 'x', 'l');
        mov->tracks[1].tref_id = 1;
    }
    mov_write_identification(pb, s);
    mov_write_meta_tag(pb, mov, s);

    moov_size = get_moov_size(s);
    for (int i = 0; i < mov->nb_tracks; i++)
        mov->tracks[i].data_offset = avio_tell(pb) + moov_size + 8;

    if (mov->is_animated_avif) {
        int ret;
        if ((ret = mov_write_moov_tag(pb, mov, s)) < 0)
            return ret;
    }

    buf_size = avio_get_dyn_buf(mov->mdat_buf, &buf);
    avio_wb32(pb, buf_size + 8);
    ffio_wfourcc(pb, "mdat");

    // The offset for the YUV planes is the starting position of mdat.
    extent_offsets[0] = avio_tell(pb);
    // The offset for alpha plane is YUV offset + YUV size.
    extent_offsets[1] = extent_offsets[0] + mov->avif_extent_length[0];

    avio_write(pb, buf, buf_size);

    // write extent offsets.
    pos_backup = avio_tell(pb);
    for (int i = 0; i < mov->nb_streams; i++) {
        if (extent_offsets[i] != (uint32_t)extent_offsets[i]) {
            av_log(s, AV_LOG_ERROR, "extent offset does not fit in 32 bits\n");
            return AVERROR_INVALIDDATA;
        }
        avio_seek(pb, mov->avif_extent_pos[i], SEEK_SET);
        avio_wb32(pb, extent_offsets[i]); /* rewrite offset */
    }
    avio_seek(pb, pos_backup, SEEK_SET);

    return 0;
}
#endif

#if CONFIG_TGP_MUXER || CONFIG_TG2_MUXER
static const AVCodecTag codec_3gp_tags[] = {
    { AV_CODEC_ID_H263,     MKTAG('s','2','6','3') },
    { AV_CODEC_ID_H264,     MKTAG('a','v','c','1') },
    { AV_CODEC_ID_MPEG4,    MKTAG('m','p','4','v') },
    { AV_CODEC_ID_AAC,      MKTAG('m','p','4','a') },
    { AV_CODEC_ID_AMR_NB,   MKTAG('s','a','m','r') },
    { AV_CODEC_ID_AMR_WB,   MKTAG('s','a','w','b') },
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t','x','3','g') },
    { AV_CODEC_ID_NONE, 0 },
};
static const AVCodecTag *const codec_3gp_tags_list[] = { codec_3gp_tags, NULL };
#endif

static const AVCodecTag codec_mp4_tags[] = {
    { AV_CODEC_ID_MPEG4,           MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_H264,            MKTAG('a', 'v', 'c', '1') },
    { AV_CODEC_ID_H264,            MKTAG('a', 'v', 'c', '3') },
    { AV_CODEC_ID_HEVC,            MKTAG('h', 'e', 'v', '1') },
    { AV_CODEC_ID_HEVC,            MKTAG('h', 'v', 'c', '1') },
    { AV_CODEC_ID_HEVC,            MKTAG('d', 'v', 'h', '1') },
    { AV_CODEC_ID_VVC,             MKTAG('v', 'v', 'c', '1') },
    { AV_CODEC_ID_VVC,             MKTAG('v', 'v', 'i', '1') },
    { AV_CODEC_ID_EVC,             MKTAG('e', 'v', 'c', '1') },
    { AV_CODEC_ID_MPEG2VIDEO,      MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_MPEG1VIDEO,      MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_MJPEG,           MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_PNG,             MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_JPEG2000,        MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_VC1,             MKTAG('v', 'c', '-', '1') },
    { AV_CODEC_ID_DIRAC,           MKTAG('d', 'r', 'a', 'c') },
    { AV_CODEC_ID_TSCC2,           MKTAG('m', 'p', '4', 'v') },
    { AV_CODEC_ID_VP9,             MKTAG('v', 'p', '0', '9') },
    { AV_CODEC_ID_AV1,             MKTAG('a', 'v', '0', '1') },
    { AV_CODEC_ID_AAC,             MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_ALAC,            MKTAG('a', 'l', 'a', 'c') },
    { AV_CODEC_ID_MP4ALS,          MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_MP3,             MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_MP2,             MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_AC3,             MKTAG('a', 'c', '-', '3') },
    { AV_CODEC_ID_EAC3,            MKTAG('e', 'c', '-', '3') },
    { AV_CODEC_ID_DTS,             MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_TRUEHD,          MKTAG('m', 'l', 'p', 'a') },
    { AV_CODEC_ID_FLAC,            MKTAG('f', 'L', 'a', 'C') },
    { AV_CODEC_ID_OPUS,            MKTAG('O', 'p', 'u', 's') },
    { AV_CODEC_ID_VORBIS,          MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_QCELP,           MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_EVRC,            MKTAG('m', 'p', '4', 'a') },
    { AV_CODEC_ID_DVD_SUBTITLE,    MKTAG('m', 'p', '4', 's') },
    { AV_CODEC_ID_MOV_TEXT,        MKTAG('t', 'x', '3', 'g') },
    { AV_CODEC_ID_BIN_DATA,        MKTAG('g', 'p', 'm', 'd') },
    { AV_CODEC_ID_MPEGH_3D_AUDIO,  MKTAG('m', 'h', 'm', '1') },
    { AV_CODEC_ID_TTML,            MOV_MP4_TTML_TAG          },
    { AV_CODEC_ID_TTML,            MOV_ISMV_TTML_TAG         },
    { AV_CODEC_ID_FFV1,            MKTAG('F', 'F', 'V', '1') },

    /* ISO/IEC 23003-5 integer formats */
    { AV_CODEC_ID_PCM_S16BE,       MOV_MP4_IPCM_TAG          },
    { AV_CODEC_ID_PCM_S16LE,       MOV_MP4_IPCM_TAG          },
    { AV_CODEC_ID_PCM_S24BE,       MOV_MP4_IPCM_TAG          },
    { AV_CODEC_ID_PCM_S24LE,       MOV_MP4_IPCM_TAG          },
    { AV_CODEC_ID_PCM_S32BE,       MOV_MP4_IPCM_TAG          },
    { AV_CODEC_ID_PCM_S32LE,       MOV_MP4_IPCM_TAG          },
    /* ISO/IEC 23003-5 floating-point formats */
    { AV_CODEC_ID_PCM_F32BE,       MOV_MP4_FPCM_TAG          },
    { AV_CODEC_ID_PCM_F32LE,       MOV_MP4_FPCM_TAG          },
    { AV_CODEC_ID_PCM_F64BE,       MOV_MP4_FPCM_TAG          },
    { AV_CODEC_ID_PCM_F64LE,       MOV_MP4_FPCM_TAG          },

    { AV_CODEC_ID_AVS3,            MKTAG('a', 'v', 's', '3') },

    { AV_CODEC_ID_NONE,               0 },
};
#if CONFIG_MP4_MUXER || CONFIG_PSP_MUXER
static const AVCodecTag *const mp4_codec_tags_list[] = { codec_mp4_tags, NULL };
#endif

static const AVCodecTag codec_ism_tags[] = {
    { AV_CODEC_ID_WMAPRO      , MKTAG('w', 'm', 'a', ' ') },
    { AV_CODEC_ID_TTML        , MOV_ISMV_TTML_TAG         },
    { AV_CODEC_ID_NONE        ,    0 },
};

static const AVCodecTag codec_ipod_tags[] = {
    { AV_CODEC_ID_H264,     MKTAG('a','v','c','1') },
    { AV_CODEC_ID_MPEG4,    MKTAG('m','p','4','v') },
    { AV_CODEC_ID_AAC,      MKTAG('m','p','4','a') },
    { AV_CODEC_ID_ALAC,     MKTAG('a','l','a','c') },
    { AV_CODEC_ID_AC3,      MKTAG('a','c','-','3') },
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t','x','3','g') },
    { AV_CODEC_ID_MOV_TEXT, MKTAG('t','e','x','t') },
    { AV_CODEC_ID_NONE, 0 },
};

static const AVCodecTag codec_f4v_tags[] = {
    { AV_CODEC_ID_MP3,    MKTAG('.','m','p','3') },
    { AV_CODEC_ID_AAC,    MKTAG('m','p','4','a') },
    { AV_CODEC_ID_H264,   MKTAG('a','v','c','1') },
    { AV_CODEC_ID_VP6A,   MKTAG('V','P','6','A') },
    { AV_CODEC_ID_VP6F,   MKTAG('V','P','6','F') },
    { AV_CODEC_ID_NONE, 0 },
};

#if CONFIG_AVIF_MUXER

static const AVOption avif_options[] = {
    { "movie_timescale", "set movie timescale", offsetof(MOVMuxContext, movie_timescale), AV_OPT_TYPE_INT, {.i64 = MOV_TIMESCALE}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "loop", "Number of times to loop animated AVIF: 0 - infinite loop", offsetof(MOVMuxContext, avif_loop_count), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, .unit = 0 },
    { NULL },
};
static const AVCodecTag codec_avif_tags[] = {
    { AV_CODEC_ID_AV1,     MKTAG('a','v','0','1') },
    { AV_CODEC_ID_NONE, 0 },
};
static const AVCodecTag *const codec_avif_tags_list[] = { codec_avif_tags, NULL };

static const AVClass mov_avif_muxer_class = {
    .class_name = "avif muxer",
    .item_name  = av_default_item_name,
    .option     = avif_options,
    .version    = LIBAVUTIL_VERSION_INT,
};
#endif

#if CONFIG_MOV_MUXER
const FFOutputFormat ff_mov_muxer = {
    .p.name            = "mov",
    .p.long_name       = NULL_IF_CONFIG_SMALL("QuickTime / MOV"),
    .p.extensions      = "mov",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = CONFIG_LIBX264_ENCODER ?
                         AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG4,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE | AVFMT_VARIABLE_FPS,
    .p.codec_tag       = (const AVCodecTag* const []){
        ff_codec_movvideo_tags, ff_codec_movaudio_tags, ff_codec_movsubtitle_tags, 0
    },
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_TGP_MUXER
const FFOutputFormat ff_tgp_muxer = {
    .p.name            = "3gp",
    .p.long_name       = NULL_IF_CONFIG_SMALL("3GP (3GPP file format)"),
    .p.extensions      = "3gp",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AMR_NB,
    .p.video_codec     = AV_CODEC_ID_H263,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE,
    .p.codec_tag       = codec_3gp_tags_list,
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_MP4_MUXER
const FFOutputFormat ff_mp4_muxer = {
    .p.name            = "mp4",
    .p.long_name       = NULL_IF_CONFIG_SMALL("MP4 (MPEG-4 Part 14)"),
    .p.mime_type       = "video/mp4",
    .p.extensions      = "mp4",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = CONFIG_LIBX264_ENCODER ?
                         AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG4,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE | AVFMT_VARIABLE_FPS,
    .p.codec_tag       = mp4_codec_tags_list,
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_PSP_MUXER
const FFOutputFormat ff_psp_muxer = {
    .p.name            = "psp",
    .p.long_name       = NULL_IF_CONFIG_SMALL("PSP MP4 (MPEG-4 Part 14)"),
    .p.extensions      = "mp4,psp",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = CONFIG_LIBX264_ENCODER ?
                         AV_CODEC_ID_H264 : AV_CODEC_ID_MPEG4,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE,
    .p.codec_tag       = mp4_codec_tags_list,
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_TG2_MUXER
const FFOutputFormat ff_tg2_muxer = {
    .p.name            = "3g2",
    .p.long_name       = NULL_IF_CONFIG_SMALL("3GP2 (3GPP2 file format)"),
    .p.extensions      = "3g2",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AMR_NB,
    .p.video_codec     = AV_CODEC_ID_H263,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE,
    .p.codec_tag       = codec_3gp_tags_list,
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_IPOD_MUXER
const FFOutputFormat ff_ipod_muxer = {
    .p.name            = "ipod",
    .p.long_name       = NULL_IF_CONFIG_SMALL("iPod H.264 MP4 (MPEG-4 Part 14)"),
    .p.mime_type       = "video/mp4",
    .p.extensions      = "m4v,m4a,m4b",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = AV_CODEC_ID_H264,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE,
    .p.codec_tag       = (const AVCodecTag* const []){ codec_ipod_tags, 0 },
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_ISMV_MUXER
const FFOutputFormat ff_ismv_muxer = {
    .p.name            = "ismv",
    .p.long_name       = NULL_IF_CONFIG_SMALL("ISMV/ISMA (Smooth Streaming)"),
    .p.mime_type       = "video/mp4",
    .p.extensions      = "ismv,isma",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = AV_CODEC_ID_H264,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER | AVFMT_TS_NEGATIVE,
    .p.codec_tag       = (const AVCodecTag* const []){
        codec_mp4_tags, codec_ism_tags, 0 },
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_F4V_MUXER
const FFOutputFormat ff_f4v_muxer = {
    .p.name            = "f4v",
    .p.long_name       = NULL_IF_CONFIG_SMALL("F4V Adobe Flash Video"),
    .p.mime_type       = "application/f4v",
    .p.extensions      = "f4v",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.audio_codec     = AV_CODEC_ID_AAC,
    .p.video_codec     = AV_CODEC_ID_H264,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER,
    .p.codec_tag       = (const AVCodecTag* const []){ codec_f4v_tags, 0 },
    .check_bitstream   = mov_check_bitstream,
    .p.priv_class      = &mov_isobmff_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
#if CONFIG_AVIF_MUXER
const FFOutputFormat ff_avif_muxer = {
    .p.name            = "avif",
    .p.long_name       = NULL_IF_CONFIG_SMALL("AVIF"),
    .p.mime_type       = "image/avif",
    .p.extensions      = "avif",
    .priv_data_size    = sizeof(MOVMuxContext),
    .p.video_codec     = AV_CODEC_ID_AV1,
    .init              = mov_init,
    .write_header      = mov_write_header,
    .write_packet      = mov_write_packet,
    .write_trailer     = avif_write_trailer,
    .deinit            = mov_free,
    .p.flags           = AVFMT_GLOBALHEADER,
    .p.codec_tag       = codec_avif_tags_list,
    .p.priv_class      = &mov_avif_muxer_class,
    .flags_internal    = FF_OFMT_FLAG_ALLOW_FLUSH,
};
#endif
