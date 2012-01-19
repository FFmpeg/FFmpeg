/*
 * MOV, 3GP, MP4 muxer
 * Copyright (c) 2003 Thomas Raivio
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>
 * Copyright (c) 2009 Baptiste Coudurier <baptiste dot coudurier at gmail dot com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "movenc.h"
#include "avformat.h"
#include "avio_internal.h"
#include "riff.h"
#include "avio.h"
#include "isom.h"
#include "avc.h"
#include "libavcodec/get_bits.h"
#include "libavcodec/put_bits.h"
#include "internal.h"
#include "libavutil/avstring.h"
#include "libavutil/intfloat.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "rtpenc.h"
#include "mov_chan.h"

#undef NDEBUG
#include <assert.h>

static const AVOption options[] = {
    { "movflags", "MOV muxer flags", offsetof(MOVMuxContext, flags), AV_OPT_TYPE_FLAGS, {.dbl = 0}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "rtphint", "Add RTP hint tracks", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_RTP_HINT}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "empty_moov", "Make the initial moov atom empty (not supported by QuickTime)", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_EMPTY_MOOV}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "frag_keyframe", "Fragment at video keyframes", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_FRAG_KEYFRAME}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "separate_moof", "Write separate moof/mdat atoms for each track", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_SEPARATE_MOOF}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "frag_custom", "Flush fragments on caller requests", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_FRAG_CUSTOM}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    { "isml", "Create a live smooth streaming feed (for pushing to a publishing point)", 0, AV_OPT_TYPE_CONST, {.dbl = FF_MOV_FLAG_ISML}, INT_MIN, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM, "movflags" },
    FF_RTP_FLAG_OPTS(MOVMuxContext, rtp_flags),
    { "skip_iods", "Skip writing iods atom.", offsetof(MOVMuxContext, iods_skip), AV_OPT_TYPE_INT, {.dbl = 0}, 0, 1, AV_OPT_FLAG_ENCODING_PARAM},
    { "iods_audio_profile", "iods audio profile atom.", offsetof(MOVMuxContext, iods_audio_profile), AV_OPT_TYPE_INT, {.dbl = -1}, -1, 255, AV_OPT_FLAG_ENCODING_PARAM},
    { "iods_video_profile", "iods video profile atom.", offsetof(MOVMuxContext, iods_video_profile), AV_OPT_TYPE_INT, {.dbl = -1}, -1, 255, AV_OPT_FLAG_ENCODING_PARAM},
    { "frag_duration", "Maximum fragment duration", offsetof(MOVMuxContext, max_fragment_duration), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "frag_size", "Maximum fragment size", offsetof(MOVMuxContext, max_fragment_size), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { "ism_lookahead", "Number of lookahead entries for ISM files", offsetof(MOVMuxContext, ism_lookahead), AV_OPT_TYPE_INT, {.dbl = 0}, 0, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM},
    { NULL },
};

#define MOV_CLASS(flavor)\
static const AVClass flavor ## _muxer_class = {\
    .class_name = #flavor " muxer",\
    .item_name  = av_default_item_name,\
    .option     = options,\
    .version    = LIBAVUTIL_VERSION_INT,\
};

//FIXME support 64 bit variant with wide placeholders
static int64_t updateSize(AVIOContext *pb, int64_t pos)
{
    int64_t curpos = avio_tell(pb);
    avio_seek(pb, pos, SEEK_SET);
    avio_wb32(pb, curpos - pos); /* rewrite size */
    avio_seek(pb, curpos, SEEK_SET);

    return curpos - pos;
}

/* Chunk offset atom */
static int mov_write_stco_tag(AVIOContext *pb, MOVTrack *track)
{
    int i;
    int mode64 = 0; //   use 32 bit size variant if possible
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    if (pos > UINT32_MAX) {
        mode64 = 1;
        ffio_wfourcc(pb, "co64");
    } else
        ffio_wfourcc(pb, "stco");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, track->entry); /* entry count */
    for (i=0; i<track->entry; i++) {
        if(mode64 == 1)
            avio_wb64(pb, track->cluster[i].pos);
        else
            avio_wb32(pb, track->cluster[i].pos);
    }
    return updateSize(pb, pos);
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

    for (i=0; i<track->entry; i++) {
        tst = track->cluster[i].size/track->cluster[i].entries;
        if(oldtst != -1 && tst != oldtst) {
            equalChunks = 0;
        }
        oldtst = tst;
        entries += track->cluster[i].entries;
    }
    if (equalChunks && track->entry) {
        int sSize = track->entry ? track->cluster[0].size/track->cluster[0].entries : 0;
        sSize = FFMAX(1, sSize); // adpcm mono case could make sSize == 0
        avio_wb32(pb, sSize); // sample size
        avio_wb32(pb, entries); // sample count
    }
    else {
        avio_wb32(pb, 0); // sample size
        avio_wb32(pb, entries); // sample count
        for (i=0; i<track->entry; i++) {
            for (j=0; j<track->cluster[i].entries; j++) {
                avio_wb32(pb, track->cluster[i].size /
                         track->cluster[i].entries);
            }
        }
    }
    return updateSize(pb, pos);
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
    avio_wb32(pb, track->entry); // entry count
    for (i=0; i<track->entry; i++) {
        if(oldval != track->cluster[i].samplesInChunk)
        {
            avio_wb32(pb, i+1); // first chunk
            avio_wb32(pb, track->cluster[i].samplesInChunk); // samples per chunk
            avio_wb32(pb, 0x1); // sample description index
            oldval = track->cluster[i].samplesInChunk;
            index++;
        }
    }
    curpos = avio_tell(pb);
    avio_seek(pb, entryPos, SEEK_SET);
    avio_wb32(pb, index); // rewrite size
    avio_seek(pb, curpos, SEEK_SET);

    return updateSize(pb, pos);
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
    for (i=0; i<track->entry; i++) {
        if (track->cluster[i].flags & flag) {
            avio_wb32(pb, i+1);
            index++;
        }
    }
    curpos = avio_tell(pb);
    avio_seek(pb, entryPos, SEEK_SET);
    avio_wb32(pb, index); // rewrite size
    avio_seek(pb, curpos, SEEK_SET);
    return updateSize(pb, pos);
}

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

static int mov_write_ac3_tag(AVIOContext *pb, MOVTrack *track)
{
    GetBitContext gbc;
    PutBitContext pbc;
    uint8_t buf[3];
    int fscod, bsid, bsmod, acmod, lfeon, frmsizecod;

    if (track->vosLen < 7)
        return -1;

    avio_wb32(pb, 11);
    ffio_wfourcc(pb, "dac3");

    init_get_bits(&gbc, track->vosData+4, (track->vosLen-4) * 8);
    fscod      = get_bits(&gbc, 2);
    frmsizecod = get_bits(&gbc, 6);
    bsid       = get_bits(&gbc, 5);
    bsmod      = get_bits(&gbc, 3);
    acmod      = get_bits(&gbc, 3);
    if (acmod == 2) {
        skip_bits(&gbc, 2); // dsurmod
    } else {
        if ((acmod & 1) && acmod != 1)
            skip_bits(&gbc, 2); // cmixlev
        if (acmod & 4)
            skip_bits(&gbc, 2); // surmixlev
    }
    lfeon = get_bits1(&gbc);

    init_put_bits(&pbc, buf, sizeof(buf));
    put_bits(&pbc, 2, fscod);
    put_bits(&pbc, 5, bsid);
    put_bits(&pbc, 3, bsmod);
    put_bits(&pbc, 3, acmod);
    put_bits(&pbc, 1, lfeon);
    put_bits(&pbc, 5, frmsizecod>>1); // bit_rate_code
    put_bits(&pbc, 5, 0); // reserved

    flush_put_bits(&pbc);
    avio_write(pb, buf, sizeof(buf));

    return 11;
}

/**
 * This function writes extradata "as is".
 * Extradata must be formatted like a valid atom (with size and tag).
 */
static int mov_write_extradata_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_write(pb, track->enc->extradata, track->enc->extradata_size);
    return track->enc->extradata_size;
}

static int mov_write_enda_tag(AVIOContext *pb)
{
    avio_wb32(pb, 10);
    ffio_wfourcc(pb, "enda");
    avio_wb16(pb, 1); /* little endian */
    return 10;
}

static void putDescr(AVIOContext *pb, int tag, unsigned int size)
{
    int i = 3;
    avio_w8(pb, tag);
    for(; i>0; i--)
        avio_w8(pb, (size>>(7*i)) | 0x80);
    avio_w8(pb, size & 0x7F);
}

static int mov_write_esds_tag(AVIOContext *pb, MOVTrack *track) // Basic
{
    int64_t pos = avio_tell(pb);
    int decoderSpecificInfoLen = track->vosLen ? 5+track->vosLen : 0;

    avio_wb32(pb, 0); // size
    ffio_wfourcc(pb, "esds");
    avio_wb32(pb, 0); // Version

    // ES descriptor
    putDescr(pb, 0x03, 3 + 5+13 + decoderSpecificInfoLen + 5+1);
    avio_wb16(pb, track->trackID);
    avio_w8(pb, 0x00); // flags (= no flags)

    // DecoderConfig descriptor
    putDescr(pb, 0x04, 13 + decoderSpecificInfoLen);

    // Object type indication
    if ((track->enc->codec_id == CODEC_ID_MP2 ||
         track->enc->codec_id == CODEC_ID_MP3) &&
        track->enc->sample_rate > 24000)
        avio_w8(pb, 0x6B); // 11172-3
    else
        avio_w8(pb, ff_codec_get_tag(ff_mp4_obj_type, track->enc->codec_id));

    // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
    // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
    if(track->enc->codec_type == AVMEDIA_TYPE_AUDIO)
        avio_w8(pb, 0x15); // flags (= Audiostream)
    else
        avio_w8(pb, 0x11); // flags (= Visualstream)

    avio_w8(pb,  track->enc->rc_buffer_size>>(3+16));      // Buffersize DB (24 bits)
    avio_wb16(pb, (track->enc->rc_buffer_size>>3)&0xFFFF); // Buffersize DB

    avio_wb32(pb, FFMAX(track->enc->bit_rate, track->enc->rc_max_rate)); // maxbitrate (FIXME should be max rate in any 1 sec window)
    if(track->enc->rc_max_rate != track->enc->rc_min_rate || track->enc->rc_min_rate==0)
        avio_wb32(pb, 0); // vbr
    else
        avio_wb32(pb, track->enc->rc_max_rate); // avg bitrate

    if (track->vosLen) {
        // DecoderSpecific info descriptor
        putDescr(pb, 0x05, track->vosLen);
        avio_write(pb, track->vosData, track->vosLen);
    }

    // SL descriptor
    putDescr(pb, 0x06, 1);
    avio_w8(pb, 0x02);
    return updateSize(pb, pos);
}

static int mov_pcm_le_gt16(enum CodecID codec_id)
{
    return codec_id == CODEC_ID_PCM_S24LE ||
           codec_id == CODEC_ID_PCM_S32LE ||
           codec_id == CODEC_ID_PCM_F32LE ||
           codec_id == CODEC_ID_PCM_F64LE;
}

static int mov_write_ms_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);
    avio_wl32(pb, track->tag); // store it byteswapped
    track->enc->codec_tag = av_bswap16(track->tag >> 16);
    ff_put_wav_header(pb, track->enc);
    return updateSize(pb, pos);
}

static int mov_write_chan_tag(AVIOContext *pb, MOVTrack *track)
{
    uint32_t layout_tag, bitmap;
    int64_t pos = avio_tell(pb);

    layout_tag = ff_mov_get_channel_layout_tag(track->enc->codec_id,
                                               track->enc->channel_layout,
                                               &bitmap);
    if (!layout_tag) {
        av_log(track->enc, AV_LOG_WARNING, "not writing 'chan' tag due to "
               "lack of channel information\n");
        return 0;
    }

    avio_wb32(pb, 0);           // Size
    ffio_wfourcc(pb, "chan");   // Type
    avio_w8(pb, 0);             // Version
    avio_wb24(pb, 0);           // Flags
    avio_wb32(pb, layout_tag);  // mChannelLayoutTag
    avio_wb32(pb, bitmap);      // mChannelBitmap
    avio_wb32(pb, 0);           // mNumberChannelDescriptions

    return updateSize(pb, pos);
}

static int mov_write_wave_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);     /* size */
    ffio_wfourcc(pb, "wave");

    avio_wb32(pb, 12);    /* size */
    ffio_wfourcc(pb, "frma");
    avio_wl32(pb, track->tag);

    if (track->enc->codec_id == CODEC_ID_AAC) {
        /* useless atom needed by mplayer, ipod, not needed by quicktime */
        avio_wb32(pb, 12); /* size */
        ffio_wfourcc(pb, "mp4a");
        avio_wb32(pb, 0);
        mov_write_esds_tag(pb, track);
    } else if (mov_pcm_le_gt16(track->enc->codec_id)) {
        mov_write_enda_tag(pb);
    } else if (track->enc->codec_id == CODEC_ID_AMR_NB) {
        mov_write_amr_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_AC3) {
        mov_write_chan_tag(pb, track);
        mov_write_ac3_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_ALAC) {
        mov_write_extradata_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_ADPCM_MS ||
               track->enc->codec_id == CODEC_ID_ADPCM_IMA_WAV) {
        mov_write_ms_tag(pb, track);
    }

    avio_wb32(pb, 8);     /* size */
    avio_wb32(pb, 0);     /* null tag */

    return updateSize(pb, pos);
}

static int mov_write_glbl_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, track->vosLen+8);
    ffio_wfourcc(pb, "glbl");
    avio_write(pb, track->vosData, track->vosLen);
    return 8+track->vosLen;
}

/**
 * Compute flags for 'lpcm' tag.
 * See CoreAudioTypes and AudioStreamBasicDescription at Apple.
 */
static int mov_get_lpcm_flags(enum CodecID codec_id)
{
    switch (codec_id) {
    case CODEC_ID_PCM_F32BE:
    case CODEC_ID_PCM_F64BE:
        return 11;
    case CODEC_ID_PCM_F32LE:
    case CODEC_ID_PCM_F64LE:
        return 9;
    case CODEC_ID_PCM_U8:
        return 10;
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_S24BE:
    case CODEC_ID_PCM_S32BE:
        return 14;
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S24LE:
    case CODEC_ID_PCM_S32LE:
        return 12;
    default:
        return 0;
    }
}

static int mov_write_audio_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int version = 0;
    uint32_t tag = track->tag;

    if (track->mode == MODE_MOV) {
        if (mov_get_lpcm_flags(track->enc->codec_id))
            tag = AV_RL32("lpcm");
        version = 2;
    }

    avio_wb32(pb, 0); /* size */
    avio_wl32(pb, tag); // store it byteswapped
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
        avio_wb64(pb, av_double2int(track->enc->sample_rate));
        avio_wb32(pb, track->enc->channels);
        avio_wb32(pb, 0x7F000000);
        avio_wb32(pb, av_get_bits_per_sample(track->enc->codec_id));
        avio_wb32(pb, mov_get_lpcm_flags(track->enc->codec_id));
        avio_wb32(pb, track->sampleSize);
        avio_wb32(pb, track->audio_vbr ? track->enc->frame_size : 1);
    } else {
        /* reserved for mp4/3gp */
        avio_wb16(pb, 2);
        avio_wb16(pb, 16);
        avio_wb16(pb, 0);

        avio_wb16(pb, 0); /* packet size (= 0) */
        avio_wb16(pb, track->enc->sample_rate);
        avio_wb16(pb, 0); /* Reserved */
    }

    if(track->mode == MODE_MOV &&
       (track->enc->codec_id == CODEC_ID_AAC ||
        track->enc->codec_id == CODEC_ID_AC3 ||
        track->enc->codec_id == CODEC_ID_AMR_NB ||
        track->enc->codec_id == CODEC_ID_ALAC ||
        track->enc->codec_id == CODEC_ID_ADPCM_MS ||
        track->enc->codec_id == CODEC_ID_ADPCM_IMA_WAV ||
        mov_pcm_le_gt16(track->enc->codec_id)))
        mov_write_wave_tag(pb, track);
    else if(track->tag == MKTAG('m','p','4','a'))
        mov_write_esds_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_AMR_NB)
        mov_write_amr_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_AC3)
        mov_write_ac3_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_ALAC)
        mov_write_extradata_tag(pb, track);
    else if(track->vosLen > 0)
        mov_write_glbl_tag(pb, track);

    return updateSize(pb, pos);
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

/* TODO: No idea about these values */
static int mov_write_svq3_tag(AVIOContext *pb)
{
    avio_wb32(pb, 0x15);
    ffio_wfourcc(pb, "SMI ");
    ffio_wfourcc(pb, "SEQH");
    avio_wb32(pb, 0x5);
    avio_wb32(pb, 0xe2c0211d);
    avio_wb32(pb, 0xc0000000);
    avio_w8(pb, 0);
    return 0x15;
}

static int mov_write_avcc_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "avcC");
    ff_isom_write_avcc(pb, track->vosData, track->vosLen);
    return updateSize(pb, pos);
}

/* also used by all avid codecs (dv, imx, meridien) and their variants */
static int mov_write_avid_tag(AVIOContext *pb, MOVTrack *track)
{
    int i;
    avio_wb32(pb, 24); /* size */
    ffio_wfourcc(pb, "ACLR");
    ffio_wfourcc(pb, "ACLR");
    ffio_wfourcc(pb, "0001");
    avio_wb32(pb, 2); /* yuv range: full 1 / normal 2 */
    avio_wb32(pb, 0); /* unknown */

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
    avio_wb32(pb, AV_RB32(track->vosData + 0x28)); /* dnxhd cid, some id ? */
    avio_wb32(pb, track->enc->width);
    /* values below are based on samples created with quicktime and avid codecs */
    if (track->vosData[5] & 2) { // interlaced
        avio_wb32(pb, track->enc->height/2);
        avio_wb32(pb, 2); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        avio_wb32(pb, 4); /* unknown */
    } else {
        avio_wb32(pb, track->enc->height);
        avio_wb32(pb, 1); /* unknown */
        avio_wb32(pb, 0); /* unknown */
        if (track->enc->height == 1080)
            avio_wb32(pb, 5); /* unknown */
        else
            avio_wb32(pb, 6); /* unknown */
    }
    /* padding */
    for (i = 0; i < 10; i++)
        avio_wb64(pb, 0);

    /* extra padding for stsd needed */
    avio_wb32(pb, 0);
    return 0;
}

static int mp4_get_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;

    if (!ff_codec_get_tag(ff_mp4_obj_type, track->enc->codec_id))
        return 0;

    if      (track->enc->codec_id == CODEC_ID_H264)      tag = MKTAG('a','v','c','1');
    else if (track->enc->codec_id == CODEC_ID_AC3)       tag = MKTAG('a','c','-','3');
    else if (track->enc->codec_id == CODEC_ID_DIRAC)     tag = MKTAG('d','r','a','c');
    else if (track->enc->codec_id == CODEC_ID_MOV_TEXT)  tag = MKTAG('t','x','3','g');
    else if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO) tag = MKTAG('m','p','4','v');
    else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO) tag = MKTAG('m','p','4','a');

    return tag;
}

static const AVCodecTag codec_ipod_tags[] = {
    { CODEC_ID_H264,   MKTAG('a','v','c','1') },
    { CODEC_ID_MPEG4,  MKTAG('m','p','4','v') },
    { CODEC_ID_AAC,    MKTAG('m','p','4','a') },
    { CODEC_ID_ALAC,   MKTAG('a','l','a','c') },
    { CODEC_ID_AC3,    MKTAG('a','c','-','3') },
    { CODEC_ID_MOV_TEXT, MKTAG('t','x','3','g') },
    { CODEC_ID_MOV_TEXT, MKTAG('t','e','x','t') },
    { CODEC_ID_NONE, 0 },
};

static int ipod_get_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;

    // keep original tag for subs, ipod supports both formats
    if (!(track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE &&
        (tag == MKTAG('t','x','3','g') ||
         tag == MKTAG('t','e','x','t'))))
        tag = ff_codec_get_tag(codec_ipod_tags, track->enc->codec_id);

    if (!av_match_ext(s->filename, "m4a") && !av_match_ext(s->filename, "m4v"))
        av_log(s, AV_LOG_WARNING, "Warning, extension is not .m4a nor .m4v "
               "Quicktime/Ipod might not play the file\n");

    return tag;
}

static int mov_get_dv_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag;

    if (track->enc->width == 720) /* SD */
        if (track->enc->height == 480) /* NTSC */
            if  (track->enc->pix_fmt == PIX_FMT_YUV422P) tag = MKTAG('d','v','5','n');
            else                                         tag = MKTAG('d','v','c',' ');
        else if (track->enc->pix_fmt == PIX_FMT_YUV422P) tag = MKTAG('d','v','5','p');
        else if (track->enc->pix_fmt == PIX_FMT_YUV420P) tag = MKTAG('d','v','c','p');
        else                                             tag = MKTAG('d','v','p','p');
    else if (track->enc->height == 720) /* HD 720 line */
        if  (track->enc->time_base.den == 50)            tag = MKTAG('d','v','h','q');
        else                                             tag = MKTAG('d','v','h','p');
    else if (track->enc->height == 1080) /* HD 1080 line */
        if  (track->enc->time_base.den == 25)            tag = MKTAG('d','v','h','5');
        else                                             tag = MKTAG('d','v','h','6');
    else {
        av_log(s, AV_LOG_ERROR, "unsupported height for dv codec\n");
        return 0;
    }

    return tag;
}

static const struct {
    enum PixelFormat pix_fmt;
    uint32_t tag;
    unsigned bps;
} mov_pix_fmt_tags[] = {
    { PIX_FMT_YUYV422, MKTAG('y','u','v','s'),  0 },
    { PIX_FMT_UYVY422, MKTAG('2','v','u','y'),  0 },
    { PIX_FMT_RGB555BE,MKTAG('r','a','w',' '), 16 },
    { PIX_FMT_RGB555LE,MKTAG('L','5','5','5'), 16 },
    { PIX_FMT_RGB565LE,MKTAG('L','5','6','5'), 16 },
    { PIX_FMT_RGB565BE,MKTAG('B','5','6','5'), 16 },
    { PIX_FMT_GRAY16BE,MKTAG('b','1','6','g'), 16 },
    { PIX_FMT_RGB24,   MKTAG('r','a','w',' '), 24 },
    { PIX_FMT_BGR24,   MKTAG('2','4','B','G'), 24 },
    { PIX_FMT_ARGB,    MKTAG('r','a','w',' '), 32 },
    { PIX_FMT_BGRA,    MKTAG('B','G','R','A'), 32 },
    { PIX_FMT_RGBA,    MKTAG('R','G','B','A'), 32 },
    { PIX_FMT_ABGR,    MKTAG('A','B','G','R'), 32 },
    { PIX_FMT_RGB48BE, MKTAG('b','4','8','r'), 48 },
};

static int mov_get_rawvideo_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(mov_pix_fmt_tags); i++) {
        if (track->enc->pix_fmt == mov_pix_fmt_tags[i].pix_fmt) {
            tag = mov_pix_fmt_tags[i].tag;
            track->enc->bits_per_coded_sample = mov_pix_fmt_tags[i].bps;
            break;
        }
    }

    return tag;
}

static int mov_get_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;

    if (!tag || (track->enc->strict_std_compliance >= FF_COMPLIANCE_NORMAL &&
                 (track->enc->codec_id == CODEC_ID_DVVIDEO ||
                  track->enc->codec_id == CODEC_ID_RAWVIDEO ||
                  track->enc->codec_id == CODEC_ID_H263 ||
                  av_get_bits_per_sample(track->enc->codec_id)))) { // pcm audio
        if (track->enc->codec_id == CODEC_ID_DVVIDEO)
            tag = mov_get_dv_codec_tag(s, track);
        else if (track->enc->codec_id == CODEC_ID_RAWVIDEO)
            tag = mov_get_rawvideo_codec_tag(s, track);
        else if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            tag = ff_codec_get_tag(codec_movvideo_tags, track->enc->codec_id);
            if (!tag) { // if no mac fcc found, try with Microsoft tags
                tag = ff_codec_get_tag(ff_codec_bmp_tags, track->enc->codec_id);
                if (tag)
                    av_log(s, AV_LOG_WARNING, "Using MS style video codec tag, "
                           "the file may be unplayable!\n");
            }
        } else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            tag = ff_codec_get_tag(codec_movaudio_tags, track->enc->codec_id);
            if (!tag) { // if no mac fcc found, try with Microsoft tags
                int ms_tag = ff_codec_get_tag(ff_codec_wav_tags, track->enc->codec_id);
                if (ms_tag) {
                    tag = MKTAG('m', 's', ((ms_tag >> 8) & 0xff), (ms_tag & 0xff));
                    av_log(s, AV_LOG_WARNING, "Using MS style audio codec tag, "
                           "the file may be unplayable!\n");
                }
            }
        } else if (track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE)
            tag = ff_codec_get_tag(ff_codec_movsubtitle_tags, track->enc->codec_id);
    }

    return tag;
}

static const AVCodecTag codec_3gp_tags[] = {
    { CODEC_ID_H263,   MKTAG('s','2','6','3') },
    { CODEC_ID_H264,   MKTAG('a','v','c','1') },
    { CODEC_ID_MPEG4,  MKTAG('m','p','4','v') },
    { CODEC_ID_AAC,    MKTAG('m','p','4','a') },
    { CODEC_ID_AMR_NB, MKTAG('s','a','m','r') },
    { CODEC_ID_AMR_WB, MKTAG('s','a','w','b') },
    { CODEC_ID_MOV_TEXT, MKTAG('t','x','3','g') },
    { CODEC_ID_NONE, 0 },
};

static int mov_find_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;

    if (track->mode == MODE_MP4 || track->mode == MODE_PSP || track->mode == MODE_ISM)
        tag = mp4_get_codec_tag(s, track);
    else if (track->mode == MODE_IPOD)
        tag = ipod_get_codec_tag(s, track);
    else if (track->mode & MODE_3GP)
        tag = ff_codec_get_tag(codec_3gp_tags, track->enc->codec_id);
    else
        tag = mov_get_codec_tag(s, track);

    return tag;
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

static int mov_write_fiel_tag(AVIOContext *pb, MOVTrack *track)
{
    unsigned mov_field_order = 0;
    if (track->enc->field_order < FF_ARRAY_ELEMS(fiel_data))
        mov_field_order = fiel_data[track->enc->field_order];
    else
        return 0;
    avio_wb32(pb, 10);
    ffio_wfourcc(pb, "fiel");
    avio_wb16(pb, mov_field_order);
    return 10;
}

static int mov_write_subtitle_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0);    /* size */
    avio_wl32(pb, track->tag); // store it byteswapped
    avio_wb32(pb, 0);    /* Reserved */
    avio_wb16(pb, 0);    /* Reserved */
    avio_wb16(pb, 1);    /* Data-reference index */

    if (track->enc->extradata_size)
        avio_write(pb, track->enc->extradata, track->enc->extradata_size);

    return updateSize(pb, pos);
}

static int mov_write_pasp_tag(AVIOContext *pb, MOVTrack *track)
{
    AVRational sar;
    av_reduce(&sar.num, &sar.den, track->enc->sample_aspect_ratio.num,
              track->enc->sample_aspect_ratio.den, INT_MAX);

    avio_wb32(pb, 16);
    ffio_wfourcc(pb, "pasp");
    avio_wb32(pb, sar.num);
    avio_wb32(pb, sar.den);
    return 16;
}

static int mov_write_video_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    char compressor_name[32];

    avio_wb32(pb, 0); /* size */
    avio_wl32(pb, track->tag); // store it byteswapped
    avio_wb32(pb, 0); /* Reserved */
    avio_wb16(pb, 0); /* Reserved */
    avio_wb16(pb, 1); /* Data-reference index */

    avio_wb16(pb, 0); /* Codec stream version */
    avio_wb16(pb, 0); /* Codec stream revision (=0) */
    if (track->mode == MODE_MOV) {
        ffio_wfourcc(pb, "FFMP"); /* Vendor */
        if(track->enc->codec_id == CODEC_ID_RAWVIDEO) {
            avio_wb32(pb, 0); /* Temporal Quality */
            avio_wb32(pb, 0x400); /* Spatial Quality = lossless*/
        } else {
            avio_wb32(pb, 0x200); /* Temporal Quality = normal */
            avio_wb32(pb, 0x200); /* Spatial Quality = normal */
        }
    } else {
        avio_wb32(pb, 0); /* Reserved */
        avio_wb32(pb, 0); /* Reserved */
        avio_wb32(pb, 0); /* Reserved */
    }
    avio_wb16(pb, track->enc->width); /* Video width */
    avio_wb16(pb, track->height); /* Video height */
    avio_wb32(pb, 0x00480000); /* Horizontal resolution 72dpi */
    avio_wb32(pb, 0x00480000); /* Vertical resolution 72dpi */
    avio_wb32(pb, 0); /* Data size (= 0) */
    avio_wb16(pb, 1); /* Frame count (= 1) */

    memset(compressor_name,0,32);
    /* FIXME not sure, ISO 14496-1 draft where it shall be set to 0 */
    if (track->mode == MODE_MOV && track->enc->codec && track->enc->codec->name)
        av_strlcpy(compressor_name,track->enc->codec->name,32);
    avio_w8(pb, strlen(compressor_name));
    avio_write(pb, compressor_name, 31);

    if (track->mode == MODE_MOV && track->enc->bits_per_coded_sample)
        avio_wb16(pb, track->enc->bits_per_coded_sample);
    else
        avio_wb16(pb, 0x18); /* Reserved */
    avio_wb16(pb, 0xffff); /* Reserved */
    if(track->tag == MKTAG('m','p','4','v'))
        mov_write_esds_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_H263)
        mov_write_d263_tag(pb);
    else if(track->enc->codec_id == CODEC_ID_SVQ3)
        mov_write_svq3_tag(pb);
    else if(track->enc->codec_id == CODEC_ID_DNXHD)
        mov_write_avid_tag(pb, track);
    else if(track->enc->codec_id == CODEC_ID_H264) {
        mov_write_avcc_tag(pb, track);
        if(track->mode == MODE_IPOD)
            mov_write_uuid_tag_ipod(pb);
    } else if (track->enc->field_order != AV_FIELD_UNKNOWN)
        mov_write_fiel_tag(pb, track);
    else if(track->vosLen > 0)
        mov_write_glbl_tag(pb, track);

    if (track->enc->sample_aspect_ratio.den && track->enc->sample_aspect_ratio.num &&
        track->enc->sample_aspect_ratio.den != track->enc->sample_aspect_ratio.num) {
        mov_write_pasp_tag(pb, track);
    }

    return updateSize(pb, pos);
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

    return updateSize(pb, pos);
}

static int mov_write_stsd_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stsd");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, 1); /* entry count */
    if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO)
        mov_write_video_tag(pb, track);
    else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO)
        mov_write_audio_tag(pb, track);
    else if (track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE)
        mov_write_subtitle_tag(pb, track);
    else if (track->enc->codec_tag == MKTAG('r','t','p',' '))
        mov_write_rtp_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_ctts_tag(AVIOContext *pb, MOVTrack *track)
{
    MOVStts *ctts_entries;
    uint32_t entries = 0;
    uint32_t atom_size;
    int i;

    ctts_entries = av_malloc((track->entry + 1) * sizeof(*ctts_entries)); /* worst case */
    ctts_entries[0].count = 1;
    ctts_entries[0].duration = track->cluster[0].cts;
    for (i=1; i<track->entry; i++) {
        if (track->cluster[i].cts == ctts_entries[entries].duration) {
            ctts_entries[entries].count++; /* compress */
        } else {
            entries++;
            ctts_entries[entries].duration = track->cluster[i].cts;
            ctts_entries[entries].count = 1;
        }
    }
    entries++; /* last one */
    atom_size = 16 + (entries * 8);
    avio_wb32(pb, atom_size); /* size */
    ffio_wfourcc(pb, "ctts");
    avio_wb32(pb, 0); /* version & flags */
    avio_wb32(pb, entries); /* entry count */
    for (i=0; i<entries; i++) {
        avio_wb32(pb, ctts_entries[i].count);
        avio_wb32(pb, ctts_entries[i].duration);
    }
    av_free(ctts_entries);
    return atom_size;
}

/* Time to sample atom */
static int mov_write_stts_tag(AVIOContext *pb, MOVTrack *track)
{
    MOVStts *stts_entries;
    uint32_t entries = -1;
    uint32_t atom_size;
    int i;

    if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO && !track->audio_vbr) {
        stts_entries = av_malloc(sizeof(*stts_entries)); /* one entry */
        stts_entries[0].count = track->sampleCount;
        stts_entries[0].duration = 1;
        entries = 1;
    } else {
        stts_entries = track->entry ?
                       av_malloc(track->entry * sizeof(*stts_entries)) : /* worst case */
                       NULL;
        for (i=0; i<track->entry; i++) {
            int64_t duration = i + 1 == track->entry ?
                track->trackDuration - track->cluster[i].dts + track->start_dts : /* readjusting */
                track->cluster[i+1].dts - track->cluster[i].dts;
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
    for (i=0; i<entries; i++) {
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
    ffio_wfourcc(pb, "url ");
    avio_wb32(pb, 1); /* version & flags */

    return 28;
}

static int mov_write_stbl_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "stbl");
    mov_write_stsd_tag(pb, track);
    mov_write_stts_tag(pb, track);
    if ((track->enc->codec_type == AVMEDIA_TYPE_VIDEO ||
         track->enc->codec_tag == MKTAG('r','t','p',' ')) &&
        track->hasKeyframes && track->hasKeyframes < track->entry)
        mov_write_stss_tag(pb, track, MOV_SYNC_SAMPLE);
    if (track->mode == MODE_MOV && track->flags & MOV_TRACK_STPS)
        mov_write_stss_tag(pb, track, MOV_PARTIAL_SYNC_SAMPLE);
    if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO &&
        track->flags & MOV_TRACK_CTTS)
        mov_write_ctts_tag(pb, track);
    mov_write_stsc_tag(pb, track);
    mov_write_stsz_tag(pb, track);
    mov_write_stco_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_dinf_tag(AVIOContext *pb)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "dinf");
    mov_write_dref_tag(pb);
    return updateSize(pb, pos);
}

static int mov_write_nmhd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 12);
    ffio_wfourcc(pb, "nmhd");
    avio_wb32(pb, 0);
    return 12;
}

static int mov_write_gmhd_tag(AVIOContext *pb)
{
    avio_wb32(pb, 0x20);   /* size */
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
    return 0x20;
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

static int mov_write_hdlr_tag(AVIOContext *pb, MOVTrack *track)
{
    const char *hdlr, *descr = NULL, *hdlr_type = NULL;
    int64_t pos = avio_tell(pb);

    if (!track) { /* no media --> data handler */
        hdlr = "dhlr";
        hdlr_type = "url ";
        descr = "DataHandler";
    } else {
        hdlr = (track->mode == MODE_MOV) ? "mhlr" : "\0\0\0\0";
        if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            hdlr_type = "vide";
            descr = "VideoHandler";
        } else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            hdlr_type = "soun";
            descr = "SoundHandler";
        } else if (track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (track->tag == MKTAG('t','x','3','g')) hdlr_type = "sbtl";
            else                                      hdlr_type = "text";
            descr = "SubtitleHandler";
        } else if (track->enc->codec_tag == MKTAG('r','t','p',' ')) {
            hdlr_type = "hint";
            descr = "HintHandler";
        }
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "hdlr");
    avio_wb32(pb, 0); /* Version & flags */
    avio_write(pb, hdlr, 4); /* handler */
    ffio_wfourcc(pb, hdlr_type); /* handler type */
    avio_wb32(pb ,0); /* reserved */
    avio_wb32(pb ,0); /* reserved */
    avio_wb32(pb ,0); /* reserved */
    if (!track || track->mode == MODE_MOV)
        avio_w8(pb, strlen(descr)); /* pascal string */
    avio_write(pb, descr, strlen(descr)); /* handler description */
    if (track && track->mode != MODE_MOV)
        avio_w8(pb, 0); /* c string */
    return updateSize(pb, pos);
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

static int mov_write_minf_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "minf");
    if(track->enc->codec_type == AVMEDIA_TYPE_VIDEO)
        mov_write_vmhd_tag(pb);
    else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO)
        mov_write_smhd_tag(pb);
    else if (track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        if (track->tag == MKTAG('t','e','x','t')) mov_write_gmhd_tag(pb);
        else                                      mov_write_nmhd_tag(pb);
    } else if (track->tag == MKTAG('r','t','p',' ')) {
        mov_write_hmhd_tag(pb);
    }
    if (track->mode == MODE_MOV) /* FIXME: Why do it for MODE_MOV only ? */
        mov_write_hdlr_tag(pb, NULL);
    mov_write_dinf_tag(pb);
    mov_write_stbl_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_mdhd_tag(AVIOContext *pb, MOVTrack *track)
{
    int version = track->trackDuration < INT32_MAX ? 0 : 1;

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
    (version == 1) ? avio_wb64(pb, track->trackDuration) : avio_wb32(pb, track->trackDuration); /* duration */
    avio_wb16(pb, track->language); /* language */
    avio_wb16(pb, 0); /* reserved (quality) */

    if(version!=0 && track->mode == MODE_MOV){
        av_log(NULL, AV_LOG_ERROR,
            "FATAL error, file duration too long for timebase, this file will not be\n"
            "playable with quicktime. Choose a different timebase or a different\n"
            "container format\n");
    }

    return 32;
}

static int mov_write_mdia_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "mdia");
    mov_write_mdhd_tag(pb, track);
    mov_write_hdlr_tag(pb, track);
    mov_write_minf_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_tkhd_tag(AVIOContext *pb, MOVTrack *track, AVStream *st)
{
    int64_t duration = av_rescale_rnd(track->trackDuration, MOV_TIMESCALE,
                                      track->timescale, AV_ROUND_UP);
    int version = duration < INT32_MAX ? 0 : 1;

    (version == 1) ? avio_wb32(pb, 104) : avio_wb32(pb, 92); /* size */
    ffio_wfourcc(pb, "tkhd");
    avio_w8(pb, version);
    avio_wb24(pb, 0xf); /* flags (track enabled) */
    if (version == 1) {
        avio_wb64(pb, track->time);
        avio_wb64(pb, track->time);
    } else {
        avio_wb32(pb, track->time); /* creation time */
        avio_wb32(pb, track->time); /* modification time */
    }
    avio_wb32(pb, track->trackID); /* track-id */
    avio_wb32(pb, 0); /* reserved */
    (version == 1) ? avio_wb64(pb, duration) : avio_wb32(pb, duration);

    avio_wb32(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */
    avio_wb16(pb, 0); /* layer */
    avio_wb16(pb, st ? st->codec->codec_type : 0); /* alternate group) */
    /* Volume, only for audio */
    if(track->enc->codec_type == AVMEDIA_TYPE_AUDIO)
        avio_wb16(pb, 0x0100);
    else
        avio_wb16(pb, 0);
    avio_wb16(pb, 0); /* reserved */

    /* Matrix structure */
    avio_wb32(pb, 0x00010000); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x00010000); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x40000000); /* reserved */

    /* Track width and height, for visual only */
    if(st && (track->enc->codec_type == AVMEDIA_TYPE_VIDEO ||
              track->enc->codec_type == AVMEDIA_TYPE_SUBTITLE)) {
        if(track->mode == MODE_MOV) {
            avio_wb32(pb, track->enc->width << 16);
            avio_wb32(pb, track->height << 16);
        } else {
            double sample_aspect_ratio = av_q2d(st->sample_aspect_ratio);
            if(!sample_aspect_ratio || track->height != track->enc->height)
                sample_aspect_ratio = 1;
            avio_wb32(pb, sample_aspect_ratio * track->enc->width*0x10000);
            avio_wb32(pb, track->height*0x10000);
        }
    }
    else {
        avio_wb32(pb, 0);
        avio_wb32(pb, 0);
    }
    return 0x5c;
}

static int mov_write_tapt_tag(AVIOContext *pb, MOVTrack *track)
{
    int32_t width = av_rescale(track->enc->sample_aspect_ratio.num, track->enc->width,
                               track->enc->sample_aspect_ratio.den);

    int64_t pos = avio_tell(pb);

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "tapt");

    avio_wb32(pb, 20);
    ffio_wfourcc(pb, "clef");
    avio_wb32(pb, 0);
    avio_wb32(pb, width << 16);
    avio_wb32(pb, track->enc->height << 16);

    avio_wb32(pb, 20);
    ffio_wfourcc(pb, "enof");
    avio_wb32(pb, 0);
    avio_wb32(pb, track->enc->width << 16);
    avio_wb32(pb, track->enc->height << 16);

    return updateSize(pb, pos);
}

// This box seems important for the psp playback ... without it the movie seems to hang
static int mov_write_edts_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t duration = av_rescale_rnd(track->trackDuration, MOV_TIMESCALE,
                                      track->timescale, AV_ROUND_UP);
    int version = duration < INT32_MAX ? 0 : 1;
    int entry_size, entry_count, size;
    int64_t delay, start_ct = track->cluster[0].cts;
    delay = av_rescale_rnd(track->cluster[0].dts + start_ct, MOV_TIMESCALE,
                           track->timescale, AV_ROUND_DOWN);
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
    avio_wb24(pb, 0); /* flags */

    avio_wb32(pb, entry_count);
    if (delay > 0) { /* add an empty edit to delay presentation */
        if (version == 1) {
            avio_wb64(pb, delay);
            avio_wb64(pb, -1);
        } else {
            avio_wb32(pb, delay);
            avio_wb32(pb, -1);
        }
        avio_wb32(pb, 0x00010000);
    }

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

static int mov_write_udta_sdp(AVIOContext *pb, AVFormatContext *ctx, int index)
{
    char buf[1000] = "";
    int len;

    ff_sdp_write_media(buf, sizeof(buf), ctx->streams[0]->codec, NULL, NULL, 0, 0, ctx);
    av_strlcatf(buf, sizeof(buf), "a=control:streamid=%d\r\n", index);
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

static int mov_write_trak_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, AVStream *st)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "trak");
    mov_write_tkhd_tag(pb, track, st);
    if (track->mode == MODE_PSP || track->flags & MOV_TRACK_CTTS ||
        (track->entry && track->cluster[0].dts)) {
        if (!(mov->flags & FF_MOV_FLAG_FRAGMENT))
            mov_write_edts_tag(pb, track);  // PSP Movies require edts box
    }
    if (track->tref_tag)
        mov_write_tref_tag(pb, track);
    mov_write_mdia_tag(pb, track);
    if (track->mode == MODE_PSP)
        mov_write_uuid_tag_psp(pb,track);  // PSP Movies require this uuid box
    if (track->tag == MKTAG('r','t','p',' '))
        mov_write_udta_sdp(pb, track->rtp_ctx, track->trackID);
    if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO && track->mode == MODE_MOV) {
        double sample_aspect_ratio = av_q2d(st->sample_aspect_ratio);
        if (0.0 != sample_aspect_ratio && 1.0 != sample_aspect_ratio)
            mov_write_tapt_tag(pb, track);
    };
    return updateSize(pb, pos);
}

static int mov_write_iods_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int i, has_audio = 0, has_video = 0;
    int64_t pos = avio_tell(pb);
    int audio_profile = mov->iods_audio_profile;
    int video_profile = mov->iods_video_profile;
    for (i = 0; i < mov->nb_streams; i++) {
        if(mov->tracks[i].entry > 0) {
            has_audio |= mov->tracks[i].enc->codec_type == AVMEDIA_TYPE_AUDIO;
            has_video |= mov->tracks[i].enc->codec_type == AVMEDIA_TYPE_VIDEO;
        }
    }
    if (audio_profile < 0)
        audio_profile = 0xFF - has_audio;
    if (video_profile < 0)
        video_profile = 0xFF - has_video;
    avio_wb32(pb, 0x0); /* size */
    ffio_wfourcc(pb, "iods");
    avio_wb32(pb, 0);    /* version & flags */
    putDescr(pb, 0x10, 7);
    avio_wb16(pb, 0x004f);
    avio_w8(pb, 0xff);
    avio_w8(pb, 0xff);
    avio_w8(pb, audio_profile);
    avio_w8(pb, video_profile);
    avio_w8(pb, 0xff);
    return updateSize(pb, pos);
}

static int mov_write_trex_tag(AVIOContext *pb, MOVTrack *track)
{
    avio_wb32(pb, 0x20); /* size */
    ffio_wfourcc(pb, "trex");
    avio_wb32(pb, 0);   /* version & flags */
    avio_wb32(pb, track->trackID); /* track ID */
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
    for (i = 0; i < mov->nb_streams; i++)
        mov_write_trex_tag(pb, &mov->tracks[i]);
    return updateSize(pb, pos);
}

static int mov_write_mvhd_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int maxTrackID = 1, i;
    int64_t maxTrackLenTemp, maxTrackLen = 0;
    int version;

    for (i=0; i<mov->nb_streams; i++) {
        if(mov->tracks[i].entry > 0) {
            maxTrackLenTemp = av_rescale_rnd(mov->tracks[i].trackDuration,
                                             MOV_TIMESCALE,
                                             mov->tracks[i].timescale,
                                             AV_ROUND_UP);
            if(maxTrackLen < maxTrackLenTemp)
                maxTrackLen = maxTrackLenTemp;
            if(maxTrackID < mov->tracks[i].trackID)
                maxTrackID = mov->tracks[i].trackID;
        }
    }

    version = maxTrackLen < UINT32_MAX ? 0 : 1;
    (version == 1) ? avio_wb32(pb, 120) : avio_wb32(pb, 108); /* size */
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
    avio_wb32(pb, MOV_TIMESCALE);
    (version == 1) ? avio_wb64(pb, maxTrackLen) : avio_wb32(pb, maxTrackLen); /* duration of longest track */

    avio_wb32(pb, 0x00010000); /* reserved (preferred rate) 1.0 = normal */
    avio_wb16(pb, 0x0100); /* reserved (preferred volume) 1.0 = normal */
    avio_wb16(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */
    avio_wb32(pb, 0); /* reserved */

    /* Matrix structure */
    avio_wb32(pb, 0x00010000); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x00010000); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x0); /* reserved */
    avio_wb32(pb, 0x40000000); /* reserved */

    avio_wb32(pb, 0); /* reserved (preview time) */
    avio_wb32(pb, 0); /* reserved (preview duration) */
    avio_wb32(pb, 0); /* reserved (poster time) */
    avio_wb32(pb, 0); /* reserved (selection time) */
    avio_wb32(pb, 0); /* reserved (selection duration) */
    avio_wb32(pb, 0); /* reserved (current time) */
    avio_wb32(pb, maxTrackID+1); /* Next track id */
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
    if(long_style){
        int size = 16 + strlen(data);
        avio_wb32(pb, size); /* size */
        ffio_wfourcc(pb, "data");
        avio_wb32(pb, 1);
        avio_wb32(pb, 0);
        avio_write(pb, data, strlen(data));
        return size;
    }else{
        if (!lang)
            lang = ff_mov_iso639_to_lang("und", 1);
        avio_wb16(pb, strlen(data)); /* string length */
        avio_wb16(pb, lang);
        avio_write(pb, data, strlen(data));
        return strlen(data) + 4;
    }
}

static int mov_write_string_tag(AVIOContext *pb, const char *name, const char *value, int lang, int long_style){
    int size = 0;
    if (value && value[0]) {
        int64_t pos = avio_tell(pb);
        avio_wb32(pb, 0); /* size */
        ffio_wfourcc(pb, name);
        mov_write_string_data_tag(pb, value, lang, long_style);
        size= updateSize(pb, pos);
    }
    return size;
}

static int mov_write_string_metadata(AVFormatContext *s, AVIOContext *pb,
                                     const char *name, const char *tag,
                                     int long_style)
{
    int l, lang = 0, len, len2;
    AVDictionaryEntry *t, *t2 = NULL;
    char tag2[16];

    if (!(t = av_dict_get(s->metadata, tag, NULL, 0)))
        return 0;

    len = strlen(t->key);
    snprintf(tag2, sizeof(tag2), "%s-", tag);
    while ((t2 = av_dict_get(s->metadata, tag2, t2, AV_DICT_IGNORE_SUFFIX))) {
        len2 = strlen(t2->key);
        if (len2 == len+4 && !strcmp(t->value, t2->value)
            && (l=ff_mov_iso639_to_lang(&t2->key[len2-3], 1)) >= 0) {
            lang = l;
            break;
        }
    }
    return mov_write_string_tag(pb, name, t->value, lang, long_style);
}

/* iTunes track number */
static int mov_write_trkn_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    AVDictionaryEntry *t = av_dict_get(s->metadata, "track", NULL, 0);
    int size = 0, track = t ? atoi(t->value) : 0;
    if (track) {
        avio_wb32(pb, 32); /* size */
        ffio_wfourcc(pb, "trkn");
            avio_wb32(pb, 24); /* size */
            ffio_wfourcc(pb, "data");
            avio_wb32(pb, 0);        // 8 bytes empty
            avio_wb32(pb, 0);
            avio_wb16(pb, 0);        // empty
            avio_wb16(pb, track);    // track number
            avio_wb16(pb, 0);        // total track number
            avio_wb16(pb, 0);        // empty
        size = 32;
    }
    return size;
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
    mov_write_string_tag(pb, "\251too", LIBAVFORMAT_IDENT, 0, 1);
    mov_write_string_metadata(s, pb, "\251cmt", "comment"  , 1);
    mov_write_string_metadata(s, pb, "\251gen", "genre"    , 1);
    mov_write_string_metadata(s, pb, "\251cpy", "copyright", 1);
    mov_write_string_metadata(s, pb, "\251grp", "grouping" , 1);
    mov_write_string_metadata(s, pb, "\251lyr", "lyrics"   , 1);
    mov_write_string_metadata(s, pb, "desc",    "description",1);
    mov_write_string_metadata(s, pb, "ldes",    "synopsis" , 1);
    mov_write_string_metadata(s, pb, "tvsh",    "show"     , 1);
    mov_write_string_metadata(s, pb, "tven",    "episode_id",1);
    mov_write_string_metadata(s, pb, "tvnn",    "network"  , 1);
    mov_write_trkn_tag(pb, mov, s);
    return updateSize(pb, pos);
}

/* iTunes meta data tag */
static int mov_write_meta_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int size = 0;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "meta");
    avio_wb32(pb, 0);
    mov_write_itunes_hdlr_tag(pb, mov, s);
    mov_write_ilst_tag(pb, mov, s);
    size = updateSize(pb, pos);
    return size;
}

static int utf8len(const uint8_t *b)
{
    int len=0;
    int val;
    while(*b){
        GET_UTF8(val, *b++, return -1;)
        len++;
    }
    return len;
}

static int ascii_to_wc(AVIOContext *pb, const uint8_t *b)
{
    int val;
    while(*b){
        GET_UTF8(val, *b++, return -1;)
        avio_wb16(pb, val);
    }
    avio_wb16(pb, 0x00);
    return 0;
}

static uint16_t language_code(const char *str)
{
    return (((str[0]-0x60) & 0x1F) << 10) + (((str[1]-0x60) & 0x1F) << 5) + ((str[2]-0x60) & 0x1F);
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
        avio_write(pb, t->value, strlen(t->value)+1); /* UTF8 string value */
        if (!strcmp(tag, "albm") &&
            (t = av_dict_get(s->metadata, "track", NULL, 0)))
            avio_w8(pb, atoi(t->value));
    }
    return updateSize(pb, pos);
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
    return updateSize(pb, pos);
}

static int mov_write_udta_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    AVIOContext *pb_buf;
    int i, ret, size;
    uint8_t *buf;

    for (i = 0; i < s->nb_streams; i++)
        if (mov->tracks[i].enc->flags & CODEC_FLAG_BITEXACT) {
            return 0;
        }

    ret = avio_open_dyn_buf(&pb_buf);
    if(ret < 0)
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
        } else if (mov->mode == MODE_MOV) { // the title field breaks gtkpod with mp4 and my suspicion is that stuff is not valid in mp4
            mov_write_string_metadata(s, pb_buf, "\251ART", "artist"     , 0);
            mov_write_string_metadata(s, pb_buf, "\251nam", "title"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251aut", "author"     , 0);
            mov_write_string_metadata(s, pb_buf, "\251alb", "album"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251day", "date"       , 0);
            mov_write_string_metadata(s, pb_buf, "\251swr", "encoder"    , 0);
            mov_write_string_metadata(s, pb_buf, "\251des", "comment"    , 0);
            mov_write_string_metadata(s, pb_buf, "\251gen", "genre"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251cpy", "copyright"  , 0);
        } else {
            /* iTunes meta data */
            mov_write_meta_tag(pb_buf, mov, s);
        }

        if (s->nb_chapters)
            mov_write_chpl_tag(pb_buf, s);

    if ((size = avio_close_dyn_buf(pb_buf, &buf)) > 0) {
        avio_wb32(pb, size+8);
        ffio_wfourcc(pb, "udta");
        avio_write(pb, buf, size);
    }
    av_free(buf);

    return 0;
}

static void mov_write_psp_udta_tag(AVIOContext *pb,
                                  const char *str, const char *lang, int type)
{
    int len = utf8len(str)+1;
    if(len<=0)
        return;
    avio_wb16(pb, len*2+10);            /* size */
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

        mov_write_psp_udta_tag(pb, LIBAVCODEC_IDENT,      "eng", 0x04);
        mov_write_psp_udta_tag(pb, title->value,          "eng", 0x01);
//        snprintf(dt,32,"%04d/%02d/%02d %02d:%02d:%02d",t_st->tm_year+1900,t_st->tm_mon+1,t_st->tm_mday,t_st->tm_hour,t_st->tm_min,t_st->tm_sec);
        mov_write_psp_udta_tag(pb, "2006/04/01 11:11:11", "und", 0x03);

        updateSize(pb, pos2);
        return updateSize(pb, pos);
    }

    return 0;
}

static int mov_write_moov_tag(AVIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int i;
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size placeholder*/
    ffio_wfourcc(pb, "moov");

    for (i=0; i<mov->nb_streams; i++) {
        if (mov->tracks[i].entry <= 0 && !(mov->flags & FF_MOV_FLAG_FRAGMENT))
            continue;

        mov->tracks[i].time = mov->time;
        mov->tracks[i].trackID = i+1;
    }

    if (mov->chapter_track)
        for (i=0; i<s->nb_streams; i++) {
            mov->tracks[i].tref_tag = MKTAG('c','h','a','p');
            mov->tracks[i].tref_id = mov->tracks[mov->chapter_track].trackID;
        }
    for (i = 0; i < mov->nb_streams; i++) {
        if (mov->tracks[i].tag == MKTAG('r','t','p',' ')) {
            mov->tracks[i].tref_tag = MKTAG('h','i','n','t');
            mov->tracks[i].tref_id =
                mov->tracks[mov->tracks[i].src_track].trackID;
        }
    }

    mov_write_mvhd_tag(pb, mov);
    if (mov->mode != MODE_MOV && !mov->iods_skip)
        mov_write_iods_tag(pb, mov);
    for (i=0; i<mov->nb_streams; i++) {
        if (mov->tracks[i].entry > 0 || mov->flags & FF_MOV_FLAG_FRAGMENT) {
            mov_write_trak_tag(pb, mov, &(mov->tracks[i]), i < s->nb_streams ? s->streams[i] : NULL);
        }
    }
    if (mov->flags & FF_MOV_FLAG_FRAGMENT)
        mov_write_mvex_tag(pb, mov); /* QuickTime requires trak to precede this */

    if (mov->mode == MODE_PSP)
        mov_write_uuidusmt_tag(pb, s);
    else
        mov_write_udta_tag(pb, mov, s);

    return updateSize(pb, pos);
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
    len = FFMIN(sizeof(buf)/2 - 1, len);
    ff_data_to_hex(buf, value, len, 0);
    buf[2*len] = '\0';
    avio_printf(pb, "<param name=\"%s\" value=\"%s\" valuetype=\"data\"/>\n", name, buf);
}

static void write_h264_extradata(AVIOContext *pb, AVCodecContext *enc)
{
    uint16_t sps_size, pps_size, len;
    char buf[150];
    sps_size = AV_RB16(&enc->extradata[6]);
    if (11 + sps_size > enc->extradata_size)
        return;
    pps_size = AV_RB16(&enc->extradata[9 + sps_size]);
    if (11 + sps_size + pps_size > enc->extradata_size)
        return;
    len = FFMIN(sizeof(buf)/2 - 1, sps_size);
    ff_data_to_hex(buf, &enc->extradata[8], len, 0);
    buf[2*len] = '\0';
    avio_printf(pb, "<param name=\"CodecPrivateData\" value=\"00000001%s", buf);
    len = FFMIN(sizeof(buf)/2 - 1, pps_size);
    ff_data_to_hex(buf, &enc->extradata[11 + sps_size], len, 0);
    buf[2*len] = '\0';
    avio_printf(pb, "00000001%s\" valuetype=\"data\"/>\n", buf);
}

static int mov_write_isml_manifest(AVIOContext *pb, MOVMuxContext *mov)
{
    int64_t pos = avio_tell(pb);
    int i;
    const uint8_t uuid[] = {
        0xa5, 0xd4, 0x0b, 0x30, 0xe8, 0x14, 0x11, 0xdd,
        0xba, 0x2f, 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66
    };

    avio_wb32(pb, 0);
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, sizeof(uuid));
    avio_wb32(pb, 0);

    avio_printf(pb, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    avio_printf(pb, "<smil xmlns=\"http://www.w3.org/2001/SMIL20/Language\">\n");
    avio_printf(pb, "<head>\n");
    avio_printf(pb, "<meta name=\"creator\" content=\"%s\" />\n",
                    LIBAVFORMAT_IDENT);
    avio_printf(pb, "</head>\n");
    avio_printf(pb, "<body>\n");
    avio_printf(pb, "<switch>\n");
    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        const char *type;
        /* track->trackID is initialized in write_moov, and thus isn't known
         * here yet */
        int track_id = i + 1;

        if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            type = "video";
        } else if (track->enc->codec_type == AVMEDIA_TYPE_AUDIO) {
            type = "audio";
        } else {
            continue;
        }
        avio_printf(pb, "<%s systemBitrate=\"%d\">\n", type,
                                                       track->enc->bit_rate);
        param_write_int(pb, "systemBitrate", track->enc->bit_rate);
        param_write_int(pb, "trackID", track_id);
        if (track->enc->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (track->enc->codec_id == CODEC_ID_H264 &&
                track->enc->extradata_size >= 11 &&
                track->enc->extradata[0] == 1) {
                write_h264_extradata(pb, track->enc);
            } else {
                param_write_hex(pb, "CodecPrivateData", track->enc->extradata,
                                track->enc->extradata_size);
            }
            if (track->enc->codec_id == CODEC_ID_H264) {
                param_write_string(pb, "FourCC", "H264");
            } else if (track->enc->codec_id == CODEC_ID_VC1) {
                param_write_string(pb, "FourCC", "WVC1");
            }
            param_write_int(pb, "MaxWidth", track->enc->width);
            param_write_int(pb, "MaxHeight", track->enc->height);
            param_write_int(pb, "DisplayWidth", track->enc->width);
            param_write_int(pb, "DisplayHeight", track->enc->height);
        } else {
            if (track->enc->codec_id == CODEC_ID_AAC) {
                param_write_string(pb, "FourCC", "AACL");
            } else if (track->enc->codec_id == CODEC_ID_WMAPRO) {
                param_write_string(pb, "FourCC", "WMAP");
            }
            param_write_hex(pb, "CodecPrivateData", track->enc->extradata,
                            track->enc->extradata_size);
            param_write_int(pb, "AudioTag", ff_codec_get_tag(ff_codec_wav_tags,
                                                track->enc->codec_id));
            param_write_int(pb, "Channels", track->enc->channels);
            param_write_int(pb, "SamplingRate", track->enc->sample_rate);
            param_write_int(pb, "BitsPerSample", 16);
            param_write_int(pb, "PacketSize", track->enc->block_align ?
                                              track->enc->block_align : 4);
        }
        avio_printf(pb, "</%s>\n", type);
    }
    avio_printf(pb, "</switch>\n");
    avio_printf(pb, "</body>\n");
    avio_printf(pb, "</smil>\n");

    return updateSize(pb, pos);
}

static int mov_write_mfhd_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    avio_wb32(pb, 16);
    ffio_wfourcc(pb, "mfhd");
    avio_wb32(pb, 0);
    avio_wb32(pb, mov->fragments);
    return 0;
}

static int mov_write_tfhd_tag(AVIOContext *pb, MOVTrack *track,
                              int64_t moof_offset)
{
    int64_t pos = avio_tell(pb);
    /* default-sample-size + default-sample-duration + base-data-offset */
    uint32_t flags = 0x19;
    if (!track->entry) {
        flags |= 0x010000; /* duration-is-empty */
    } else {
        flags |= 0x20; /* default-sample-flags-present */
    }

    /* Don't set a default sample size when creating data for silverlight,
     * the player refuses to play files with that set. */
    if (track->mode == MODE_ISM)
        flags &= ~0x10;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "tfhd");
    avio_w8(pb, 0); /* version */
    avio_wb24(pb, flags);

    avio_wb32(pb, track->trackID); /* track-id */
    if (flags & 0x01)
        avio_wb64(pb, moof_offset);
    if (flags & 0x08) {
        track->default_duration = track->audio_vbr ? track->enc->frame_size : 1;
        avio_wb32(pb, track->default_duration);
    }
    if (flags & 0x10) {
        track->default_size = track->entry ? track->cluster[0].size : 1;
        avio_wb32(pb, track->default_size);
    } else
        track->default_size = -1;

    if (flags & 0x20) {
        track->default_sample_flags =
            track->enc->codec_type == AVMEDIA_TYPE_VIDEO ?
            0x01010000 : 0x02000000;
        avio_wb32(pb, track->default_sample_flags);
    }

    return updateSize(pb, pos);
}

static uint32_t get_sample_flags(MOVTrack *track, MOVIentry *entry)
{
    return entry->flags & MOV_SYNC_SAMPLE ? 0x02000000 : 0x01010000;
}

static int mov_write_trun_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    uint32_t flags = 1; /* data-offset-present */
    int i;

    for (i = 0; i < track->entry; i++) {
        int64_t duration = i + 1 == track->entry ?
            track->trackDuration - track->cluster[i].dts + track->start_dts :
            track->cluster[i + 1].dts - track->cluster[i].dts;
        if (duration != track->default_duration)
            flags |= 0x100; /* sample-duration-present */
        if (track->cluster[i].size != track->default_size)
            flags |= 0x200; /* sample-size-present */
        if (i > 0 && get_sample_flags(track, &track->cluster[i]) != track->default_sample_flags)
            flags |= 0x400; /* sample-flags-present */
    }
    if (!(flags & 0x400))
        flags |= 0x4; /* first-sample-flags-present */
    if (track->flags & MOV_TRACK_CTTS)
        flags |= 0x800; /* sample-composition-time-offsets-present */

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "trun");
    avio_w8(pb, 0); /* version */
    avio_wb24(pb, flags);

    avio_wb32(pb, track->entry); /* sample count */
    track->moof_size_offset = avio_tell(pb);
    avio_wb32(pb, 0); /* data offset */
    if (flags & 0x4) /* first sample flags */
        avio_wb32(pb, get_sample_flags(track, &track->cluster[0]));

    for (i = 0; i < track->entry; i++) {
        int64_t duration = i + 1 == track->entry ?
            track->trackDuration - track->cluster[i].dts + track->start_dts :
            track->cluster[i + 1].dts - track->cluster[i].dts;
        if (flags & 0x100)
            avio_wb32(pb, duration);
        if (flags & 0x200)
            avio_wb32(pb, track->cluster[i].size);
        if (flags & 0x400)
            avio_wb32(pb, get_sample_flags(track, &track->cluster[i]));
        if (flags & 0x800)
            avio_wb32(pb, track->cluster[i].cts);
    }

    return updateSize(pb, pos);
}

static int mov_write_tfxd_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    const uint8_t uuid[] = {
        0x6d, 0x1d, 0x9b, 0x05, 0x42, 0xd5, 0x44, 0xe6,
        0x80, 0xe2, 0x14, 0x1d, 0xaf, 0xf7, 0x57, 0xb2
    };

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, sizeof(uuid));
    avio_w8(pb, 1);
    avio_wb24(pb, 0);
    avio_wb64(pb, track->frag_start);
    avio_wb64(pb, track->start_dts + track->trackDuration -
                  track->cluster[0].dts);

    return updateSize(pb, pos);
}

static int mov_write_tfrf_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int entry)
{
    int n = track->nb_frag_info - 1 - entry, i;
    int size = 8 + 16 + 4 + 1 + 16*n;
    const uint8_t uuid[] = {
        0xd4, 0x80, 0x7e, 0xf2, 0xca, 0x39, 0x46, 0x95,
        0x8e, 0x54, 0x26, 0xcb, 0x9e, 0x46, 0xa7, 0x9f
    };

    if (entry < 0)
        return 0;

    avio_seek(pb, track->frag_info[entry].tfrf_offset, SEEK_SET);
    avio_wb32(pb, size);
    ffio_wfourcc(pb, "uuid");
    avio_write(pb, uuid, sizeof(uuid));
    avio_w8(pb, 1);
    avio_wb24(pb, 0);
    avio_w8(pb, n);
    for (i = 0; i < n; i++) {
        int index = entry + 1 + i;
        avio_wb64(pb, track->frag_info[index].time);
        avio_wb64(pb, track->frag_info[index].duration);
    }
    if (n < mov->ism_lookahead) {
        int free_size = 16*(mov->ism_lookahead - n);
        avio_wb32(pb, free_size);
        ffio_wfourcc(pb, "free");
        for (i = 0; i < free_size - 8; i++)
            avio_w8(pb, 0);
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

static int mov_write_traf_tag(AVIOContext *pb, MOVMuxContext *mov,
                              MOVTrack *track, int64_t moof_offset)
{
    int64_t pos = avio_tell(pb);
    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "traf");

    mov_write_tfhd_tag(pb, track, moof_offset);
    mov_write_trun_tag(pb, track);
    if (mov->mode == MODE_ISM) {
        mov_write_tfxd_tag(pb, track);

        if (mov->ism_lookahead) {
            int i, size = 16 + 4 + 1 + 16*mov->ism_lookahead;

            track->tfrf_offset = avio_tell(pb);
            avio_wb32(pb, 8 + size);
            ffio_wfourcc(pb, "free");
            for (i = 0; i < size; i++)
                avio_w8(pb, 0);
        }
    }

    return updateSize(pb, pos);
}

static int mov_write_moof_tag(AVIOContext *pb, MOVMuxContext *mov, int tracks)
{
    int64_t pos = avio_tell(pb), end;
    int i, moof_size;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "moof");

    mov_write_mfhd_tag(pb, mov);
    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (tracks >= 0 && i != tracks)
            continue;
        if (!track->entry)
            continue;
        mov_write_traf_tag(pb, mov, track, pos);
    }

    end = avio_tell(pb);
    moof_size = end - pos;
    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (tracks >= 0 && i != tracks)
            continue;
        if (!track->entry)
            continue;
        avio_seek(pb, mov->tracks[i].moof_size_offset, SEEK_SET);
        avio_wb32(pb, moof_size + 8 + mov->tracks[i].data_offset);
    }
    avio_seek(pb, end, SEEK_SET);

    return updateSize(pb, pos);
}

static int mov_write_tfra_tag(AVIOContext *pb, MOVTrack *track)
{
    int64_t pos = avio_tell(pb);
    int i;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "tfra");
    avio_w8(pb, 1); /* version */
    avio_wb24(pb, 0);

    avio_wb32(pb, track->trackID);
    avio_wb32(pb, 0); /* length of traf/trun/sample num */
    avio_wb32(pb, track->nb_frag_info);
    for (i = 0; i < track->nb_frag_info; i++) {
        avio_wb64(pb, track->frag_info[i].time);
        avio_wb64(pb, track->frag_info[i].offset);
        avio_w8(pb, 1); /* traf number */
        avio_w8(pb, 1); /* trun number */
        avio_w8(pb, 1); /* sample number */
    }

    return updateSize(pb, pos);
}

static int mov_write_mfra_tag(AVIOContext *pb, MOVMuxContext *mov)
{
    int64_t pos = avio_tell(pb);
    int i;

    avio_wb32(pb, 0); /* size placeholder */
    ffio_wfourcc(pb, "mfra");
    /* An empty mfra atom is enough to indicate to the publishing point that
     * the stream has ended. */
    if (mov->flags & FF_MOV_FLAG_ISML)
        return updateSize(pb, pos);

    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (track->nb_frag_info)
            mov_write_tfra_tag(pb, track);
    }

    avio_wb32(pb, 16);
    ffio_wfourcc(pb, "mfro");
    avio_wb32(pb, 0); /* version + flags */
    avio_wb32(pb, avio_tell(pb) + 4 - pos);

    return updateSize(pb, pos);
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

/* TODO: This needs to be more general */
static int mov_write_ftyp_tag(AVIOContext *pb, AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int64_t pos = avio_tell(pb);
    int has_h264 = 0, has_video = 0;
    int minor = 0x200;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            has_video = 1;
        if (st->codec->codec_id == CODEC_ID_H264)
            has_h264 = 1;
    }

    avio_wb32(pb, 0); /* size */
    ffio_wfourcc(pb, "ftyp");

    if (mov->mode == MODE_3GP) {
        ffio_wfourcc(pb, has_h264 ? "3gp6"  : "3gp4");
        minor =     has_h264 ?   0x100 :   0x200;
    } else if (mov->mode & MODE_3G2) {
        ffio_wfourcc(pb, has_h264 ? "3g2b"  : "3g2a");
        minor =     has_h264 ? 0x20000 : 0x10000;
    }else if (mov->mode == MODE_PSP)
        ffio_wfourcc(pb, "MSNV");
    else if (mov->mode == MODE_MP4)
        ffio_wfourcc(pb, "isom");
    else if (mov->mode == MODE_IPOD)
        ffio_wfourcc(pb, has_video ? "M4V ":"M4A ");
    else if (mov->mode == MODE_ISM)
        ffio_wfourcc(pb, "isml");
    else
        ffio_wfourcc(pb, "qt  ");

    avio_wb32(pb, minor);

    if(mov->mode == MODE_MOV)
        ffio_wfourcc(pb, "qt  ");
    else if (mov->mode == MODE_ISM) {
        ffio_wfourcc(pb, "piff");
        ffio_wfourcc(pb, "iso2");
    } else {
        ffio_wfourcc(pb, "isom");
        ffio_wfourcc(pb, "iso2");
        if(has_h264)
            ffio_wfourcc(pb, "avc1");
    }

    if (mov->mode == MODE_3GP)
        ffio_wfourcc(pb, has_h264 ? "3gp6":"3gp4");
    else if (mov->mode & MODE_3G2)
        ffio_wfourcc(pb, has_h264 ? "3g2b":"3g2a");
    else if (mov->mode == MODE_PSP)
        ffio_wfourcc(pb, "MSNV");
    else if (mov->mode == MODE_MP4)
        ffio_wfourcc(pb, "mp41");
    return updateSize(pb, pos);
}

static void mov_write_uuidprof_tag(AVIOContext *pb, AVFormatContext *s)
{
    AVCodecContext *VideoCodec = s->streams[0]->codec;
    AVCodecContext *AudioCodec = s->streams[1]->codec;
    int AudioRate = AudioCodec->sample_rate;
    int FrameRate = ((VideoCodec->time_base.den) * (0x10000))/ (VideoCodec->time_base.num);
    int audio_kbitrate= AudioCodec->bit_rate / 1000;
    int video_kbitrate= FFMIN(VideoCodec->bit_rate / 1000, 800 - audio_kbitrate);

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
    ffio_wfourcc(pb, "APRF");/* audio */
    avio_wb32(pb, 0x0);
    avio_wb32(pb, 0x2);   /* TrackID */
    ffio_wfourcc(pb, "mp4a");
    avio_wb32(pb, 0x20f);
    avio_wb32(pb, 0x0);
    avio_wb32(pb, audio_kbitrate);
    avio_wb32(pb, audio_kbitrate);
    avio_wb32(pb, AudioRate);
    avio_wb32(pb, AudioCodec->channels);

    avio_wb32(pb, 0x34);  /* size */
    ffio_wfourcc(pb, "VPRF");   /* video */
    avio_wb32(pb, 0x0);
    avio_wb32(pb, 0x1);    /* TrackID */
    if (VideoCodec->codec_id == CODEC_ID_H264) {
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
    avio_wb32(pb, FrameRate);
    avio_wb32(pb, FrameRate);
    avio_wb16(pb, VideoCodec->width);
    avio_wb16(pb, VideoCodec->height);
    avio_wb32(pb, 0x010001); /* ? */
}

static int mov_parse_mpeg2_frame(AVPacket *pkt, uint32_t *flags)
{
    uint32_t c = -1;
    int i, closed_gop = 0;

    for (i = 0; i < pkt->size - 4; i++) {
        c = (c<<8) + pkt->data[i];
        if (c == 0x1b8) { // gop
            closed_gop = pkt->data[i+4]>>6 & 0x01;
        } else if (c == 0x100) { // pic
            int temp_ref = (pkt->data[i+1]<<2) | (pkt->data[i+2]>>6);
            if (!temp_ref || closed_gop) // I picture is not reordered
                *flags = MOV_SYNC_SAMPLE;
            else
                *flags = MOV_PARTIAL_SYNC_SAMPLE;
            break;
        }
    }
    return 0;
}

static int mov_flush_fragment(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int i, first_track = -1;
    int64_t mdat_size = 0;

    if (!(mov->flags & FF_MOV_FLAG_FRAGMENT))
        return 0;

    if (!(mov->flags & FF_MOV_FLAG_EMPTY_MOOV) && mov->fragments == 0) {
        int64_t pos = avio_tell(s->pb);

        for (i = 0; i < mov->nb_streams; i++)
            if (!mov->tracks[i].entry)
                break;
        /* Don't write the initial moov unless all tracks have data */
        if (i < mov->nb_streams)
            return 0;
        avio_seek(s->pb, mov->mdat_pos, SEEK_SET);
        avio_wb32(s->pb, mov->mdat_size + 8);
        avio_seek(s->pb, pos, SEEK_SET);
        mov_write_moov_tag(s->pb, mov, s);
        mov->fragments++;
        mov->mdat_size = 0;
        for (i = 0; i < mov->nb_streams; i++) {
            if (mov->tracks[i].entry)
                mov->tracks[i].frag_start += mov->tracks[i].start_dts +
                                             mov->tracks[i].trackDuration -
                                             mov->tracks[i].cluster[0].dts;
            mov->tracks[i].entry = 0;
        }
        avio_flush(s->pb);
        return 0;
    }

    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        if (mov->flags & FF_MOV_FLAG_SEPARATE_MOOF)
            track->data_offset = 0;
        else
            track->data_offset = mdat_size;
        if (!track->mdat_buf)
            continue;
        mdat_size += avio_tell(track->mdat_buf);
        if (first_track < 0)
            first_track = i;
    }

    if (!mdat_size)
        return 0;

    for (i = 0; i < mov->nb_streams; i++) {
        MOVTrack *track = &mov->tracks[i];
        int buf_size, write_moof = 1, moof_tracks = -1;
        uint8_t *buf;
        int64_t duration = 0;

        if (track->entry)
            duration = track->start_dts + track->trackDuration -
                       track->cluster[0].dts;
        if (mov->flags & FF_MOV_FLAG_SEPARATE_MOOF) {
            if (!track->mdat_buf)
                continue;
            mdat_size = avio_tell(track->mdat_buf);
            moof_tracks = i;
        } else {
            write_moof = i == first_track;
        }

        if (write_moof) {
            MOVFragmentInfo *info;
            avio_flush(s->pb);
            track->nb_frag_info++;
            track->frag_info = av_realloc(track->frag_info,
                                          sizeof(*track->frag_info) *
                                          track->nb_frag_info);
            info = &track->frag_info[track->nb_frag_info - 1];
            info->offset   = avio_tell(s->pb);
            info->time     = mov->tracks[i].frag_start;
            info->duration = duration;
            mov_write_tfrf_tags(s->pb, mov, track);

            mov_write_moof_tag(s->pb, mov, moof_tracks);
            info->tfrf_offset = track->tfrf_offset;
            mov->fragments++;

            avio_wb32(s->pb, mdat_size + 8);
            ffio_wfourcc(s->pb, "mdat");
        }

        if (track->entry)
            track->frag_start += duration;
        track->entry = 0;
        if (!track->mdat_buf)
            continue;
        buf_size = avio_close_dyn_buf(track->mdat_buf, &buf);
        track->mdat_buf = NULL;

        avio_write(s->pb, buf, buf_size);
        av_free(buf);
    }

    mov->mdat_size = 0;

    avio_flush(s->pb);
    return 0;
}

static int mov_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    MOVTrack *trk = &mov->tracks[pkt->stream_index];
    AVCodecContext *enc = trk->enc;
    unsigned int samplesInChunk = 0;
    int size= pkt->size;
    uint8_t *reformatted_data = NULL;

    if (!s->pb->seekable && !(mov->flags & FF_MOV_FLAG_EMPTY_MOOV))
        return 0; /* Can't handle that */

    if (!size) return 0; /* Discard 0 sized packets */

    if ((mov->max_fragment_duration && trk->entry &&
         av_rescale_q(pkt->dts - trk->cluster[0].dts,
                      s->streams[pkt->stream_index]->time_base,
                      AV_TIME_BASE_Q) >= mov->max_fragment_duration) ||
         (mov->max_fragment_size && mov->mdat_size + size >= mov->max_fragment_size) ||
         (mov->flags & FF_MOV_FLAG_FRAG_KEYFRAME &&
          enc->codec_type == AVMEDIA_TYPE_VIDEO &&
          trk->entry && pkt->flags & AV_PKT_FLAG_KEY)) {
        mov_flush_fragment(s);
    }

    if (mov->flags & FF_MOV_FLAG_FRAGMENT && mov->fragments > 0) {
        if (!trk->mdat_buf) {
            int ret;
            if ((ret = avio_open_dyn_buf(&trk->mdat_buf)) < 0)
                return ret;
        }
        pb = trk->mdat_buf;
    }

    if (enc->codec_id == CODEC_ID_AMR_NB) {
        /* We must find out how many AMR blocks there are in one packet */
        static uint16_t packed_size[16] =
            {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 1};
        int len = 0;

        while (len < size && samplesInChunk < 100) {
            len += packed_size[(pkt->data[len] >> 3) & 0x0F];
            samplesInChunk++;
        }
        if(samplesInChunk > 1){
            av_log(s, AV_LOG_ERROR, "fatal error, input is not a single packet, implement a AVParser for it\n");
            return -1;
        }
    } else if (trk->sampleSize)
        samplesInChunk = size/trk->sampleSize;
    else
        samplesInChunk = 1;

    /* copy extradata if it exists */
    if (trk->vosLen == 0 && enc->extradata_size > 0) {
        trk->vosLen = enc->extradata_size;
        trk->vosData = av_malloc(trk->vosLen);
        memcpy(trk->vosData, enc->extradata, trk->vosLen);
    }

    if (enc->codec_id == CODEC_ID_H264 && trk->vosLen > 0 && *(uint8_t *)trk->vosData != 1) {
        /* from x264 or from bytestream h264 */
        /* nal reformating needed */
        if (trk->hint_track >= 0 && trk->hint_track < mov->nb_streams) {
            ff_avc_parse_nal_units_buf(pkt->data, &reformatted_data,
                                       &size);
            avio_write(pb, reformatted_data, size);
        } else {
            size = ff_avc_parse_nal_units(pb, pkt->data, pkt->size);
        }
    } else {
        avio_write(pb, pkt->data, size);
    }

    if ((enc->codec_id == CODEC_ID_DNXHD ||
         enc->codec_id == CODEC_ID_AC3) && !trk->vosLen) {
        /* copy frame to create needed atoms */
        trk->vosLen = size;
        trk->vosData = av_malloc(size);
        if (!trk->vosData)
            return AVERROR(ENOMEM);
        memcpy(trk->vosData, pkt->data, size);
    }

    if (!(trk->entry % MOV_INDEX_CLUSTER_SIZE)) {
        trk->cluster = av_realloc(trk->cluster, (trk->entry + MOV_INDEX_CLUSTER_SIZE) * sizeof(*trk->cluster));
        if (!trk->cluster)
            return -1;
    }

    trk->cluster[trk->entry].pos = avio_tell(pb) - size;
    trk->cluster[trk->entry].samplesInChunk = samplesInChunk;
    trk->cluster[trk->entry].size = size;
    trk->cluster[trk->entry].entries = samplesInChunk;
    trk->cluster[trk->entry].dts = pkt->dts;
    if (!trk->entry && trk->start_dts != AV_NOPTS_VALUE) {
        /* First packet of a new fragment. We already wrote the duration
         * of the last packet of the previous fragment based on trackDuration,
         * which might not exactly match our dts. Therefore adjust the dts
         * of this packet to be what the previous packets duration implies. */
        trk->cluster[trk->entry].dts = trk->start_dts + trk->trackDuration;
    }
    if (trk->start_dts == AV_NOPTS_VALUE)
        trk->start_dts = pkt->dts;
    trk->trackDuration = pkt->dts - trk->start_dts + pkt->duration;

    if (pkt->pts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_WARNING, "pts has no value\n");
        pkt->pts = pkt->dts;
    }
    if (pkt->dts != pkt->pts)
        trk->flags |= MOV_TRACK_CTTS;
    trk->cluster[trk->entry].cts = pkt->pts - pkt->dts;
    trk->cluster[trk->entry].flags = 0;
    if (pkt->flags & AV_PKT_FLAG_KEY) {
        if (mov->mode == MODE_MOV && enc->codec_id == CODEC_ID_MPEG2VIDEO &&
            trk->entry > 0) { // force sync sample for the first key frame
            mov_parse_mpeg2_frame(pkt, &trk->cluster[trk->entry].flags);
            if (trk->cluster[trk->entry].flags & MOV_PARTIAL_SYNC_SAMPLE)
                trk->flags |= MOV_TRACK_STPS;
        } else {
            trk->cluster[trk->entry].flags = MOV_SYNC_SAMPLE;
        }
        if (trk->cluster[trk->entry].flags & MOV_SYNC_SAMPLE)
            trk->hasKeyframes++;
    }
    trk->entry++;
    trk->sampleCount += samplesInChunk;
    mov->mdat_size += size;

    avio_flush(pb);

    if (trk->hint_track >= 0 && trk->hint_track < mov->nb_streams)
        ff_mov_add_hinted_packet(s, pkt, trk->hint_track, trk->entry,
                                 reformatted_data, size);
    av_free(reformatted_data);
    return 0;
}

int ff_mov_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (!pkt) {
        mov_flush_fragment(s);
        return 1;
    } else {
        return mov_write_packet_internal(s, pkt);
    }
}

// QuickTime chapters involve an additional text track with the chapter names
// as samples, and a tref pointing from the other tracks to the chapter one.
static void mov_create_chapter_track(AVFormatContext *s, int tracknum)
{
    MOVMuxContext *mov = s->priv_data;
    MOVTrack *track = &mov->tracks[tracknum];
    AVPacket pkt = { .stream_index = tracknum, .flags = AV_PKT_FLAG_KEY };
    int i, len;

    track->mode = mov->mode;
    track->tag = MKTAG('t','e','x','t');
    track->timescale = MOV_TIMESCALE;
    track->enc = avcodec_alloc_context3(NULL);
    track->enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    for (i = 0; i < s->nb_chapters; i++) {
        AVChapter *c = s->chapters[i];
        AVDictionaryEntry *t;

        int64_t end = av_rescale_q(c->end, c->time_base, (AVRational){1,MOV_TIMESCALE});
        pkt.pts = pkt.dts = av_rescale_q(c->start, c->time_base, (AVRational){1,MOV_TIMESCALE});
        pkt.duration = end - pkt.dts;

        if ((t = av_dict_get(c->metadata, "title", NULL, 0))) {
            len = strlen(t->value);
            pkt.size = len+2;
            pkt.data = av_malloc(pkt.size);
            AV_WB16(pkt.data, len);
            memcpy(pkt.data+2, t->value, len);
            ff_mov_write_packet(s, &pkt);
            av_freep(&pkt.data);
        }
    }
}

static int mov_write_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    MOVMuxContext *mov = s->priv_data;
    AVDictionaryEntry *t;
    int i, hint_track = 0;

    /* Non-seekable output is ok if EMPTY_MOOV is set, or if using the ismv
     * format (which sets EMPTY_MOOV later in this function). If ism_lookahead
     * is enabled, we don't support non-seekable output at all. */
    if (!s->pb->seekable &&
        ((!(mov->flags & FF_MOV_FLAG_EMPTY_MOOV) &&
          !(s->oformat && !strcmp(s->oformat->name, "ismv")))
         || mov->ism_lookahead)) {
        av_log(s, AV_LOG_ERROR, "muxer does not support non seekable output\n");
        return -1;
    }

    /* Default mode == MP4 */
    mov->mode = MODE_MP4;

    if (s->oformat != NULL) {
        if (!strcmp("3gp", s->oformat->name)) mov->mode = MODE_3GP;
        else if (!strcmp("3g2", s->oformat->name)) mov->mode = MODE_3GP|MODE_3G2;
        else if (!strcmp("mov", s->oformat->name)) mov->mode = MODE_MOV;
        else if (!strcmp("psp", s->oformat->name)) mov->mode = MODE_PSP;
        else if (!strcmp("ipod",s->oformat->name)) mov->mode = MODE_IPOD;
        else if (!strcmp("ismv",s->oformat->name)) mov->mode = MODE_ISM;

        mov_write_ftyp_tag(pb,s);
        if (mov->mode == MODE_PSP) {
            if (s->nb_streams != 2) {
                av_log(s, AV_LOG_ERROR, "PSP mode need one video and one audio stream\n");
                return -1;
            }
            mov_write_uuidprof_tag(pb,s);
        }
    }

    mov->nb_streams = s->nb_streams;
    if (mov->mode & (MODE_MOV|MODE_IPOD) && s->nb_chapters)
        mov->chapter_track = mov->nb_streams++;

    if (mov->flags & FF_MOV_FLAG_RTP_HINT) {
        /* Add hint tracks for each audio and video stream */
        hint_track = mov->nb_streams;
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
                st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                mov->nb_streams++;
            }
        }
    }

    mov->tracks = av_mallocz(mov->nb_streams*sizeof(*mov->tracks));
    if (!mov->tracks)
        return AVERROR(ENOMEM);

    for(i=0; i<s->nb_streams; i++){
        AVStream *st= s->streams[i];
        MOVTrack *track= &mov->tracks[i];
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL,0);

        track->enc = st->codec;
        track->language = ff_mov_iso639_to_lang(lang?lang->value:"und", mov->mode!=MODE_MOV);
        if (track->language < 0)
            track->language = 0;
        track->mode = mov->mode;
        track->tag = mov_find_codec_tag(s, track);
        if (!track->tag) {
            av_log(s, AV_LOG_ERROR, "track %d: could not find tag, "
                   "codec not currently supported in container\n", i);
            goto error;
        }
        /* If hinting of this track is enabled by a later hint track,
         * this is updated. */
        track->hint_track = -1;
        track->start_dts = AV_NOPTS_VALUE;
        if(st->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            if (track->tag == MKTAG('m','x','3','p') || track->tag == MKTAG('m','x','3','n') ||
                track->tag == MKTAG('m','x','4','p') || track->tag == MKTAG('m','x','4','n') ||
                track->tag == MKTAG('m','x','5','p') || track->tag == MKTAG('m','x','5','n')) {
                if (st->codec->width != 720 || (st->codec->height != 608 && st->codec->height != 512)) {
                    av_log(s, AV_LOG_ERROR, "D-10/IMX must use 720x608 or 720x512 video resolution\n");
                    goto error;
                }
                track->height = track->tag>>24 == 'n' ? 486 : 576;
            }
            track->timescale = st->codec->time_base.den;
            if (track->mode == MODE_MOV && track->timescale > 100000)
                av_log(s, AV_LOG_WARNING,
                       "WARNING codec timebase is very high. If duration is too long,\n"
                       "file may not be playable by quicktime. Specify a shorter timebase\n"
                       "or choose different container.\n");
        }else if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO){
            track->timescale = st->codec->sample_rate;
            /* set sampleSize for PCM and ADPCM */
            if (av_get_bits_per_sample(st->codec->codec_id)) {
                if (!st->codec->block_align) {
                    av_log(s, AV_LOG_ERROR, "track %d: codec block align is not set\n", i);
                    goto error;
                }
                track->sampleSize = st->codec->block_align;
            }
            /* set audio_vbr for compressed audio */
            if (av_get_bits_per_sample(st->codec->codec_id) < 8) {
                if (!st->codec->frame_size && track->mode == MODE_MOV) {
                    av_log(s, AV_LOG_ERROR, "track %d: codec frame size is not set\n", i);
                    goto error;
                }
                track->audio_vbr = 1;
            }
            if (track->mode != MODE_MOV) {
                if (track->timescale > UINT16_MAX) {
                    av_log(s, AV_LOG_ERROR, "track %d: output format does not support "
                           "sample rate %dhz\n", i, track->timescale);
                    goto error;
                }
                if (track->enc->codec_id == CODEC_ID_MP3 && track->timescale < 16000) {
                    av_log(s, AV_LOG_ERROR, "track %d: muxing mp3 at %dhz is not supported\n",
                           i, track->enc->sample_rate);
                    goto error;
                }
            }
        }else if(st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE){
            track->timescale = st->codec->time_base.den;
        }
        if (!track->height)
            track->height = st->codec->height;
        /* The ism specific timescale isn't mandatory, but is assumed by
         * some tools, such as mp4split. */
        if (mov->mode == MODE_ISM)
            track->timescale = 10000000;

        avpriv_set_pts_info(st, 64, 1, track->timescale);

        /* copy extradata if it exists */
        if (st->codec->extradata_size) {
            track->vosLen  = st->codec->extradata_size;
            track->vosData = av_malloc(track->vosLen);
            memcpy(track->vosData, st->codec->extradata, track->vosLen);
        }
    }

    if (mov->mode == MODE_ISM) {
        /* If no fragmentation options have been set, set a default. */
        if (!(mov->flags & (FF_MOV_FLAG_FRAG_KEYFRAME |
                            FF_MOV_FLAG_FRAG_CUSTOM)) &&
            !mov->max_fragment_duration && !mov->max_fragment_size)
            mov->max_fragment_duration = 5000000;
        mov->flags |= FF_MOV_FLAG_EMPTY_MOOV | FF_MOV_FLAG_SEPARATE_MOOF;
    }

    /* Set the FRAGMENT flag if any of the fragmentation methods are
     * enabled. */
    if (mov->max_fragment_duration || mov->max_fragment_size ||
        mov->flags & (FF_MOV_FLAG_EMPTY_MOOV |
                      FF_MOV_FLAG_FRAG_KEYFRAME |
                      FF_MOV_FLAG_FRAG_CUSTOM))
        mov->flags |= FF_MOV_FLAG_FRAGMENT;

    if (!(mov->flags & FF_MOV_FLAG_EMPTY_MOOV))
        mov_write_mdat_tag(pb, mov);

    if (t = av_dict_get(s->metadata, "creation_time", NULL, 0))
        mov->time = ff_iso8601_to_unix_time(t->value);
    if (mov->time)
        mov->time += 0x7C25B080; // 1970 based -> 1904 based

    if (mov->chapter_track)
        mov_create_chapter_track(s, mov->chapter_track);

    if (mov->flags & FF_MOV_FLAG_RTP_HINT) {
        /* Initialize the hint tracks for each audio and video stream */
        for (i = 0; i < s->nb_streams; i++) {
            AVStream *st = s->streams[i];
            if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
                st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
                ff_mov_init_hinting(s, hint_track, i);
                hint_track++;
            }
        }
    }

    avio_flush(pb);

    if (mov->flags & FF_MOV_FLAG_ISML)
        mov_write_isml_manifest(pb, mov);

    if (mov->flags & FF_MOV_FLAG_EMPTY_MOOV) {
        mov_write_moov_tag(pb, mov, s);
        mov->fragments++;
    }

    return 0;
 error:
    av_freep(&mov->tracks);
    return -1;
}

static int mov_write_trailer(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    AVIOContext *pb = s->pb;
    int res = 0;
    int i;

    int64_t moov_pos = avio_tell(pb);

    if (!(mov->flags & FF_MOV_FLAG_FRAGMENT)) {
        /* Write size of mdat tag */
        if (mov->mdat_size + 8 <= UINT32_MAX) {
            avio_seek(pb, mov->mdat_pos, SEEK_SET);
            avio_wb32(pb, mov->mdat_size + 8);
        } else {
            /* overwrite 'wide' placeholder atom */
            avio_seek(pb, mov->mdat_pos - 8, SEEK_SET);
            /* special value: real atom size will be 64 bit value after
             * tag field */
            avio_wb32(pb, 1);
            ffio_wfourcc(pb, "mdat");
            avio_wb64(pb, mov->mdat_size + 16);
        }
        avio_seek(pb, moov_pos, SEEK_SET);

        mov_write_moov_tag(pb, mov, s);
    } else {
        mov_flush_fragment(s);
        mov_write_mfra_tag(pb, mov);
    }

    if (mov->chapter_track)
        av_freep(&mov->tracks[mov->chapter_track].enc);

    for (i=0; i<mov->nb_streams; i++) {
        if (mov->tracks[i].tag == MKTAG('r','t','p',' '))
            ff_mov_close_hinting(&mov->tracks[i]);
        av_freep(&mov->tracks[i].cluster);
        av_freep(&mov->tracks[i].frag_info);

        if(mov->tracks[i].vosLen) av_free(mov->tracks[i].vosData);

    }

    avio_flush(pb);

    av_freep(&mov->tracks);

    return res;
}

#if CONFIG_MOV_MUXER
MOV_CLASS(mov)
AVOutputFormat ff_mov_muxer = {
    .name              = "mov",
    .long_name         = NULL_IF_CONFIG_SMALL("MOV format"),
    .extensions        = "mov",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AAC,
#if CONFIG_LIBX264_ENCODER
    .video_codec       = CODEC_ID_H264,
#else
    .video_codec       = CODEC_ID_MPEG4,
#endif
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){codec_movvideo_tags, codec_movaudio_tags, 0},
    .priv_class = &mov_muxer_class,
};
#endif
#if CONFIG_TGP_MUXER
MOV_CLASS(tgp)
AVOutputFormat ff_tgp_muxer = {
    .name              = "3gp",
    .long_name         = NULL_IF_CONFIG_SMALL("3GP format"),
    .extensions        = "3gp",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AMR_NB,
    .video_codec       = CODEC_ID_H263,
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){codec_3gp_tags, 0},
    .priv_class = &tgp_muxer_class,
};
#endif
#if CONFIG_MP4_MUXER
MOV_CLASS(mp4)
AVOutputFormat ff_mp4_muxer = {
    .name              = "mp4",
    .long_name         = NULL_IF_CONFIG_SMALL("MP4 format"),
    .mime_type         = "application/mp4",
    .extensions        = "mp4",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AAC,
#if CONFIG_LIBX264_ENCODER
    .video_codec       = CODEC_ID_H264,
#else
    .video_codec       = CODEC_ID_MPEG4,
#endif
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){ff_mp4_obj_type, 0},
    .priv_class = &mp4_muxer_class,
};
#endif
#if CONFIG_PSP_MUXER
MOV_CLASS(psp)
AVOutputFormat ff_psp_muxer = {
    .name              = "psp",
    .long_name         = NULL_IF_CONFIG_SMALL("PSP MP4 format"),
    .extensions        = "mp4,psp",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AAC,
#if CONFIG_LIBX264_ENCODER
    .video_codec       = CODEC_ID_H264,
#else
    .video_codec       = CODEC_ID_MPEG4,
#endif
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){ff_mp4_obj_type, 0},
    .priv_class = &psp_muxer_class,
};
#endif
#if CONFIG_TG2_MUXER
MOV_CLASS(tg2)
AVOutputFormat ff_tg2_muxer = {
    .name              = "3g2",
    .long_name         = NULL_IF_CONFIG_SMALL("3GP2 format"),
    .extensions        = "3g2",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AMR_NB,
    .video_codec       = CODEC_ID_H263,
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){codec_3gp_tags, 0},
    .priv_class = &tg2_muxer_class,
};
#endif
#if CONFIG_IPOD_MUXER
MOV_CLASS(ipod)
AVOutputFormat ff_ipod_muxer = {
    .name              = "ipod",
    .long_name         = NULL_IF_CONFIG_SMALL("iPod H.264 MP4 format"),
    .mime_type         = "application/mp4",
    .extensions        = "m4v,m4a",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AAC,
    .video_codec       = CODEC_ID_H264,
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag = (const AVCodecTag* const []){codec_ipod_tags, 0},
    .priv_class = &ipod_muxer_class,
};
#endif
#if CONFIG_ISMV_MUXER
MOV_CLASS(ismv)
AVOutputFormat ff_ismv_muxer = {
    .name              = "ismv",
    .long_name         = NULL_IF_CONFIG_SMALL("ISMV/ISMA (Smooth Streaming) format"),
    .mime_type         = "application/mp4",
    .extensions        = "ismv,isma",
    .priv_data_size    = sizeof(MOVMuxContext),
    .audio_codec       = CODEC_ID_AAC,
    .video_codec       = CODEC_ID_H264,
    .write_header      = mov_write_header,
    .write_packet      = ff_mov_write_packet,
    .write_trailer     = mov_write_trailer,
    .flags             = AVFMT_GLOBALHEADER | AVFMT_ALLOW_FLUSH,
    .codec_tag         = (const AVCodecTag* const []){ff_mp4_obj_type, 0},
    .priv_class        = &ismv_muxer_class,
};
#endif
