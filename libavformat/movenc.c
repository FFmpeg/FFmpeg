/*
 * MOV, 3GP, MP4 muxer
 * Copyright (c) 2003 Thomas Raivio
 * Copyright (c) 2004 Gildas Bazin <gbazin at videolan dot org>
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
#include "avformat.h"
#include "riff.h"
#include "avio.h"
#include "isom.h"
#include "avc.h"
#include "libavcodec/bitstream.h"

#undef NDEBUG
#include <assert.h>

#define MOV_INDEX_CLUSTER_SIZE 16384
#define globalTimescale 1000

#define MODE_MP4  0x01
#define MODE_MOV  0x02
#define MODE_3GP  0x04
#define MODE_PSP  0x08 // example working PSP command line:
// ffmpeg -i testinput.avi  -f psp -r 14.985 -s 320x240 -b 768 -ar 24000 -ab 32 M4V00001.MP4
#define MODE_3G2  0x10
#define MODE_IPOD 0x20

typedef struct MOVIentry {
    unsigned int flags, size;
    uint64_t     pos;
    unsigned int samplesInChunk;
    char         key_frame;
    unsigned int entries;
    int64_t      cts;
    int64_t      dts;
} MOVIentry;

typedef struct MOVIndex {
    int         mode;
    int         entry;
    long        timescale;
    long        time;
    int64_t     trackDuration;
    long        sampleCount;
    long        sampleSize;
    int         hasKeyframes;
    int         hasBframes;
    int         language;
    int         trackID;
    int         tag; ///< stsd fourcc
    AVCodecContext *enc;

    int         vosLen;
    uint8_t     *vosData;
    MOVIentry   *cluster;
    int         audio_vbr;
    int         height; ///< active picture (w/o VBI) height for D-10/IMX
} MOVTrack;

typedef struct MOVMuxContext {
    int     mode;
    int64_t time;
    int     nb_streams;
    int64_t mdat_pos;
    uint64_t mdat_size;
    long    timescale;
    MOVTrack tracks[MAX_STREAMS];
} MOVMuxContext;

//FIXME support 64 bit variant with wide placeholders
static int64_t updateSize(ByteIOContext *pb, int64_t pos)
{
    int64_t curpos = url_ftell(pb);
    url_fseek(pb, pos, SEEK_SET);
    put_be32(pb, curpos - pos); /* rewrite size */
    url_fseek(pb, curpos, SEEK_SET);

    return curpos - pos;
}

/* Chunk offset atom */
static int mov_write_stco_tag(ByteIOContext *pb, MOVTrack *track)
{
    int i;
    int mode64 = 0; //   use 32 bit size variant if possible
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    if (pos > UINT32_MAX) {
        mode64 = 1;
        put_tag(pb, "co64");
    } else
        put_tag(pb, "stco");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, track->entry); /* entry count */
    for (i=0; i<track->entry; i++) {
        if(mode64 == 1)
            put_be64(pb, track->cluster[i].pos);
        else
            put_be32(pb, track->cluster[i].pos);
    }
    return updateSize(pb, pos);
}

/* Sample size atom */
static int mov_write_stsz_tag(ByteIOContext *pb, MOVTrack *track)
{
    int equalChunks = 1;
    int i, j, entries = 0, tst = -1, oldtst = -1;

    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsz");
    put_be32(pb, 0); /* version & flags */

    for (i=0; i<track->entry; i++) {
        tst = track->cluster[i].size/track->cluster[i].entries;
        if(oldtst != -1 && tst != oldtst) {
            equalChunks = 0;
        }
        oldtst = tst;
        entries += track->cluster[i].entries;
    }
    if (equalChunks) {
        int sSize = track->cluster[0].size/track->cluster[0].entries;
        put_be32(pb, sSize); // sample size
        put_be32(pb, entries); // sample count
    }
    else {
        put_be32(pb, 0); // sample size
        put_be32(pb, entries); // sample count
        for (i=0; i<track->entry; i++) {
            for (j=0; j<track->cluster[i].entries; j++) {
                put_be32(pb, track->cluster[i].size /
                         track->cluster[i].entries);
            }
        }
    }
    return updateSize(pb, pos);
}

/* Sample to chunk atom */
static int mov_write_stsc_tag(ByteIOContext *pb, MOVTrack *track)
{
    int index = 0, oldval = -1, i;
    int64_t entryPos, curpos;

    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsc");
    put_be32(pb, 0); // version & flags
    entryPos = url_ftell(pb);
    put_be32(pb, track->entry); // entry count
    for (i=0; i<track->entry; i++) {
        if(oldval != track->cluster[i].samplesInChunk)
        {
            put_be32(pb, i+1); // first chunk
            put_be32(pb, track->cluster[i].samplesInChunk); // samples per chunk
            put_be32(pb, 0x1); // sample description index
            oldval = track->cluster[i].samplesInChunk;
            index++;
        }
    }
    curpos = url_ftell(pb);
    url_fseek(pb, entryPos, SEEK_SET);
    put_be32(pb, index); // rewrite size
    url_fseek(pb, curpos, SEEK_SET);

    return updateSize(pb, pos);
}

/* Sync sample atom */
static int mov_write_stss_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t curpos, entryPos;
    int i, index = 0;
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); // size
    put_tag(pb, "stss");
    put_be32(pb, 0); // version & flags
    entryPos = url_ftell(pb);
    put_be32(pb, track->entry); // entry count
    for (i=0; i<track->entry; i++) {
        if(track->cluster[i].key_frame == 1) {
            put_be32(pb, i+1);
            index++;
        }
    }
    curpos = url_ftell(pb);
    url_fseek(pb, entryPos, SEEK_SET);
    put_be32(pb, index); // rewrite size
    url_fseek(pb, curpos, SEEK_SET);
    return updateSize(pb, pos);
}

static int mov_write_amr_tag(ByteIOContext *pb, MOVTrack *track)
{
    put_be32(pb, 0x11); /* size */
    if (track->mode == MODE_MOV) put_tag(pb, "samr");
    else                         put_tag(pb, "damr");
    put_tag(pb, "FFMP");
    put_byte(pb, 0); /* decoder version */

    put_be16(pb, 0x81FF); /* Mode set (all modes for AMR_NB) */
    put_byte(pb, 0x00); /* Mode change period (no restriction) */
    put_byte(pb, 0x01); /* Frames per sample */
    return 0x11;
}

static int mov_write_ac3_tag(ByteIOContext *pb, MOVTrack *track)
{
    GetBitContext gbc;
    PutBitContext pbc;
    uint8_t buf[3];
    int fscod, bsid, bsmod, acmod, lfeon, frmsizecod;

    if (track->vosLen < 7)
        return -1;

    put_be32(pb, 11);
    put_tag(pb, "dac3");

    init_get_bits(&gbc, track->vosData+4, track->vosLen-4);
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
    put_buffer(pb, buf, sizeof(buf));

    return 11;
}

/**
 * This function writes extradata "as is".
 * Extradata must be formated like a valid atom (with size and tag)
 */
static int mov_write_extradata_tag(ByteIOContext *pb, MOVTrack *track)
{
    put_buffer(pb, track->enc->extradata, track->enc->extradata_size);
    return track->enc->extradata_size;
}

static int mov_write_enda_tag(ByteIOContext *pb)
{
    put_be32(pb, 10);
    put_tag(pb, "enda");
    put_be16(pb, 1); /* little endian */
    return 10;
}

static unsigned int descrLength(unsigned int len)
{
    int i;
    for(i=1; len>>(7*i); i++);
    return len + 1 + i;
}

static void putDescr(ByteIOContext *pb, int tag, unsigned int size)
{
    int i= descrLength(size) - size - 2;
    put_byte(pb, tag);
    for(; i>0; i--)
        put_byte(pb, (size>>(7*i)) | 0x80);
    put_byte(pb, size & 0x7F);
}

static int mov_write_esds_tag(ByteIOContext *pb, MOVTrack *track) // Basic
{
    int64_t pos = url_ftell(pb);
    int decoderSpecificInfoLen = track->vosLen ? descrLength(track->vosLen):0;

    put_be32(pb, 0); // size
    put_tag(pb, "esds");
    put_be32(pb, 0); // Version

    // ES descriptor
    putDescr(pb, 0x03, 3 + descrLength(13 + decoderSpecificInfoLen) +
             descrLength(1));
    put_be16(pb, track->trackID);
    put_byte(pb, 0x00); // flags (= no flags)

    // DecoderConfig descriptor
    putDescr(pb, 0x04, 13 + decoderSpecificInfoLen);

    // Object type indication
    if ((track->enc->codec_id == CODEC_ID_MP2 ||
         track->enc->codec_id == CODEC_ID_MP3) &&
        track->enc->sample_rate > 24000)
        put_byte(pb, 0x6B); // 11172-3
    else
        put_byte(pb, codec_get_tag(ff_mp4_obj_type, track->enc->codec_id));

    // the following fields is made of 6 bits to identify the streamtype (4 for video, 5 for audio)
    // plus 1 bit to indicate upstream and 1 bit set to 1 (reserved)
    if(track->enc->codec_type == CODEC_TYPE_AUDIO)
        put_byte(pb, 0x15); // flags (= Audiostream)
    else
        put_byte(pb, 0x11); // flags (= Visualstream)

    put_byte(pb,  track->enc->rc_buffer_size>>(3+16));    // Buffersize DB (24 bits)
    put_be16(pb, (track->enc->rc_buffer_size>>3)&0xFFFF); // Buffersize DB

    put_be32(pb, FFMAX(track->enc->bit_rate, track->enc->rc_max_rate)); // maxbitrate (FIXME should be max rate in any 1 sec window)
    if(track->enc->rc_max_rate != track->enc->rc_min_rate || track->enc->rc_min_rate==0)
        put_be32(pb, 0); // vbr
    else
        put_be32(pb, track->enc->rc_max_rate); // avg bitrate

    if (track->vosLen) {
        // DecoderSpecific info descriptor
        putDescr(pb, 0x05, track->vosLen);
        put_buffer(pb, track->vosData, track->vosLen);
    }

    // SL descriptor
    putDescr(pb, 0x06, 1);
    put_byte(pb, 0x02);
    return updateSize(pb, pos);
}

static int mov_write_wave_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);

    put_be32(pb, 0);     /* size */
    put_tag(pb, "wave");

    put_be32(pb, 12);    /* size */
    put_tag(pb, "frma");
    put_le32(pb, track->tag);

    if (track->enc->codec_id == CODEC_ID_AAC) {
        /* useless atom needed by mplayer, ipod, not needed by quicktime */
        put_be32(pb, 12); /* size */
        put_tag(pb, "mp4a");
        put_be32(pb, 0);
        mov_write_esds_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_PCM_S24LE ||
               track->enc->codec_id == CODEC_ID_PCM_S32LE) {
        mov_write_enda_tag(pb);
    } else if (track->enc->codec_id == CODEC_ID_AMR_NB) {
        mov_write_amr_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_AC3) {
        mov_write_ac3_tag(pb, track);
    } else if (track->enc->codec_id == CODEC_ID_ALAC) {
        mov_write_extradata_tag(pb, track);
    }

    put_be32(pb, 8);     /* size */
    put_be32(pb, 0);     /* null tag */

    return updateSize(pb, pos);
}

static int mov_write_glbl_tag(ByteIOContext *pb, MOVTrack *track)
{
    put_be32(pb, track->vosLen+8);
    put_tag(pb, "glbl");
    put_buffer(pb, track->vosData, track->vosLen);
    return 8+track->vosLen;
}

static int mov_write_audio_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    int version = track->mode == MODE_MOV &&
        (track->audio_vbr ||
         track->enc->codec_id == CODEC_ID_PCM_S32LE ||
         track->enc->codec_id == CODEC_ID_PCM_S24LE);

    put_be32(pb, 0); /* size */
    put_le32(pb, track->tag); // store it byteswapped
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index, XXX  == 1 */

    /* SoundDescription */
    put_be16(pb, version); /* Version */
    put_be16(pb, 0); /* Revision level */
    put_be32(pb, 0); /* Reserved */

    if (track->mode == MODE_MOV) {
        put_be16(pb, track->enc->channels);
        if (track->enc->codec_id == CODEC_ID_PCM_U8 ||
            track->enc->codec_id == CODEC_ID_PCM_S8)
            put_be16(pb, 8); /* bits per sample */
        else
            put_be16(pb, 16);
        put_be16(pb, track->audio_vbr ? -2 : 0); /* compression ID */
    } else { /* reserved for mp4/3gp */
        put_be16(pb, 2);
        put_be16(pb, 16);
        put_be16(pb, 0);
    }

    put_be16(pb, 0); /* packet size (= 0) */
    put_be16(pb, track->timescale); /* Time scale */
    put_be16(pb, 0); /* Reserved */

    if(version == 1) { /* SoundDescription V1 extended info */
        put_be32(pb, track->enc->frame_size); /* Samples per packet */
        put_be32(pb, track->sampleSize / track->enc->channels); /* Bytes per packet */
        put_be32(pb, track->sampleSize); /* Bytes per frame */
        put_be32(pb, 2); /* Bytes per sample */
    }

    if(track->mode == MODE_MOV &&
       (track->enc->codec_id == CODEC_ID_AAC ||
        track->enc->codec_id == CODEC_ID_AC3 ||
        track->enc->codec_id == CODEC_ID_AMR_NB ||
        track->enc->codec_id == CODEC_ID_PCM_S24LE ||
        track->enc->codec_id == CODEC_ID_PCM_S32LE ||
        track->enc->codec_id == CODEC_ID_ALAC))
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

static int mov_write_d263_tag(ByteIOContext *pb)
{
    put_be32(pb, 0xf); /* size */
    put_tag(pb, "d263");
    put_tag(pb, "FFMP");
    put_byte(pb, 0); /* decoder version */
    /* FIXME use AVCodecContext level/profile, when encoder will set values */
    put_byte(pb, 0xa); /* level */
    put_byte(pb, 0); /* profile */
    return 0xf;
}

/* TODO: No idea about these values */
static int mov_write_svq3_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x15);
    put_tag(pb, "SMI ");
    put_tag(pb, "SEQH");
    put_be32(pb, 0x5);
    put_be32(pb, 0xe2c0211d);
    put_be32(pb, 0xc0000000);
    put_byte(pb, 0);
    return 0x15;
}

static int mov_write_avcc_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);

    put_be32(pb, 0);
    put_tag(pb, "avcC");
    ff_isom_write_avcc(pb, track->vosData, track->vosLen);
    return updateSize(pb, pos);
}

/* also used by all avid codecs (dv, imx, meridien) and their variants */
static int mov_write_avid_tag(ByteIOContext *pb, MOVTrack *track)
{
    int i;
    put_be32(pb, 24); /* size */
    put_tag(pb, "ACLR");
    put_tag(pb, "ACLR");
    put_tag(pb, "0001");
    put_be32(pb, 1); /* yuv 1 / rgb 2 ? */
    put_be32(pb, 0); /* unknown */

    put_be32(pb, 24); /* size */
    put_tag(pb, "APRG");
    put_tag(pb, "APRG");
    put_tag(pb, "0001");
    put_be32(pb, 1); /* unknown */
    put_be32(pb, 0); /* unknown */

    put_be32(pb, 120); /* size */
    put_tag(pb, "ARES");
    put_tag(pb, "ARES");
    put_tag(pb, "0001");
    put_be32(pb, AV_RB32(track->vosData + 0x28)); /* dnxhd cid, some id ? */
    put_be32(pb, track->enc->width);
    /* values below are based on samples created with quicktime and avid codecs */
    if (track->vosData[5] & 2) { // interlaced
        put_be32(pb, track->enc->height/2);
        put_be32(pb, 2); /* unknown */
        put_be32(pb, 0); /* unknown */
        put_be32(pb, 4); /* unknown */
    } else {
        put_be32(pb, track->enc->height);
        put_be32(pb, 1); /* unknown */
        put_be32(pb, 0); /* unknown */
        if (track->enc->height == 1080)
            put_be32(pb, 5); /* unknown */
        else
            put_be32(pb, 6); /* unknown */
    }
    /* padding */
    for (i = 0; i < 10; i++)
        put_be64(pb, 0);

    /* extra padding for stsd needed */
    put_be32(pb, 0);
    return 0;
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

static const AVCodecTag mov_pix_fmt_tags[] = {
    { PIX_FMT_YUYV422, MKTAG('y','u','v','s') },
    { PIX_FMT_UYVY422, MKTAG('2','v','u','y') },
    { PIX_FMT_BGR555,  MKTAG('r','a','w',' ') },
    { PIX_FMT_RGB24,   MKTAG('r','a','w',' ') },
    { PIX_FMT_BGR32_1, MKTAG('r','a','w',' ') },
};

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

static int mov_find_codec_tag(AVFormatContext *s, MOVTrack *track)
{
    int tag = track->enc->codec_tag;
    if (track->mode == MODE_MP4 || track->mode == MODE_PSP) {
        if (!codec_get_tag(ff_mp4_obj_type, track->enc->codec_id))
            return 0;
        if      (track->enc->codec_id == CODEC_ID_H264)      tag = MKTAG('a','v','c','1');
        else if (track->enc->codec_id == CODEC_ID_AC3)       tag = MKTAG('a','c','-','3');
        else if (track->enc->codec_id == CODEC_ID_DIRAC)     tag = MKTAG('d','r','a','c');
        else if (track->enc->codec_id == CODEC_ID_MOV_TEXT)  tag = MKTAG('t','x','3','g');
        else if (track->enc->codec_type == CODEC_TYPE_VIDEO) tag = MKTAG('m','p','4','v');
        else if (track->enc->codec_type == CODEC_TYPE_AUDIO) tag = MKTAG('m','p','4','a');
    } else if (track->mode == MODE_IPOD) {
        if (track->enc->codec_type == CODEC_TYPE_SUBTITLE &&
            (tag == MKTAG('t','x','3','g') ||
             tag == MKTAG('t','e','x','t')))
            track->tag = tag; // keep original tag
        else
            tag = codec_get_tag(codec_ipod_tags, track->enc->codec_id);
        if (!match_ext(s->filename, "m4a") && !match_ext(s->filename, "m4v"))
            av_log(s, AV_LOG_WARNING, "Warning, extension is not .m4a nor .m4v "
                   "Quicktime/Ipod might not play the file\n");
    } else if (track->mode & MODE_3GP) {
        tag = codec_get_tag(codec_3gp_tags, track->enc->codec_id);
    } else if (!tag || (track->enc->strict_std_compliance >= FF_COMPLIANCE_NORMAL &&
                        (tag == MKTAG('d','v','c','p') ||
                         track->enc->codec_id == CODEC_ID_RAWVIDEO))) {
        if (track->enc->codec_id == CODEC_ID_DVVIDEO) {
            if (track->enc->height == 480) /* NTSC */
                if  (track->enc->pix_fmt == PIX_FMT_YUV422P) tag = MKTAG('d','v','5','n');
                else                                         tag = MKTAG('d','v','c',' ');
            else if (track->enc->pix_fmt == PIX_FMT_YUV422P) tag = MKTAG('d','v','5','p');
            else if (track->enc->pix_fmt == PIX_FMT_YUV420P) tag = MKTAG('d','v','c','p');
            else                                             tag = MKTAG('d','v','p','p');
        } else if (track->enc->codec_id == CODEC_ID_RAWVIDEO) {
            tag = codec_get_tag(mov_pix_fmt_tags, track->enc->pix_fmt);
            if (!tag) // restore tag
                tag = track->enc->codec_tag;
        } else {
            if (track->enc->codec_type == CODEC_TYPE_VIDEO) {
                tag = codec_get_tag(codec_movvideo_tags, track->enc->codec_id);
                if (!tag) { // if no mac fcc found, try with Microsoft tags
                    tag = codec_get_tag(codec_bmp_tags, track->enc->codec_id);
                    if (tag)
                        av_log(s, AV_LOG_INFO, "Warning, using MS style video codec tag, "
                               "the file may be unplayable!\n");
                }
            } else if (track->enc->codec_type == CODEC_TYPE_AUDIO) {
                tag = codec_get_tag(codec_movaudio_tags, track->enc->codec_id);
                if (!tag) { // if no mac fcc found, try with Microsoft tags
                    int ms_tag = codec_get_tag(codec_wav_tags, track->enc->codec_id);
                    if (ms_tag) {
                        tag = MKTAG('m', 's', ((ms_tag >> 8) & 0xff), (ms_tag & 0xff));
                        av_log(s, AV_LOG_INFO, "Warning, using MS style audio codec tag, "
                               "the file may be unplayable!\n");
                    }
                }
            } else if (track->enc->codec_type == CODEC_TYPE_SUBTITLE) {
                tag = codec_get_tag(ff_codec_movsubtitle_tags, track->enc->codec_id);
            }
        }
    }
    return tag;
}

/** Write uuid atom.
 * Needed to make file play in iPods running newest firmware
 * goes after avcC atom in moov.trak.mdia.minf.stbl.stsd.avc1
 */
static int mov_write_uuid_tag_ipod(ByteIOContext *pb)
{
    put_be32(pb, 28);
    put_tag(pb, "uuid");
    put_be32(pb, 0x6b6840f2);
    put_be32(pb, 0x5f244fc5);
    put_be32(pb, 0xba39a51b);
    put_be32(pb, 0xcf0323f3);
    put_be32(pb, 0x0);
    return 28;
}

static int mov_write_subtitle_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0);    /* size */
    put_le32(pb, track->tag); // store it byteswapped
    put_be32(pb, 0);    /* Reserved */
    put_be16(pb, 0);    /* Reserved */
    put_be16(pb, 1);    /* Data-reference index */

    if (track->enc->extradata_size)
        put_buffer(pb, track->enc->extradata, track->enc->extradata_size);

    return updateSize(pb, pos);
}

static int mov_write_video_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    char compressor_name[32];

    put_be32(pb, 0); /* size */
    put_le32(pb, track->tag); // store it byteswapped
    put_be32(pb, 0); /* Reserved */
    put_be16(pb, 0); /* Reserved */
    put_be16(pb, 1); /* Data-reference index */

    put_be16(pb, 0); /* Codec stream version */
    put_be16(pb, 0); /* Codec stream revision (=0) */
    if (track->mode == MODE_MOV) {
        put_tag(pb, "FFMP"); /* Vendor */
        if(track->enc->codec_id == CODEC_ID_RAWVIDEO) {
            put_be32(pb, 0); /* Temporal Quality */
            put_be32(pb, 0x400); /* Spatial Quality = lossless*/
        } else {
            put_be32(pb, 0x200); /* Temporal Quality = normal */
            put_be32(pb, 0x200); /* Spatial Quality = normal */
        }
    } else {
        put_be32(pb, 0); /* Reserved */
        put_be32(pb, 0); /* Reserved */
        put_be32(pb, 0); /* Reserved */
    }
    put_be16(pb, track->enc->width); /* Video width */
    put_be16(pb, track->height); /* Video height */
    put_be32(pb, 0x00480000); /* Horizontal resolution 72dpi */
    put_be32(pb, 0x00480000); /* Vertical resolution 72dpi */
    put_be32(pb, 0); /* Data size (= 0) */
    put_be16(pb, 1); /* Frame count (= 1) */

    memset(compressor_name,0,32);
    /* FIXME not sure, ISO 14496-1 draft where it shall be set to 0 */
    if (track->mode == MODE_MOV && track->enc->codec && track->enc->codec->name)
        strncpy(compressor_name,track->enc->codec->name,31);
    put_byte(pb, strlen(compressor_name));
    put_buffer(pb, compressor_name, 31);

    if (track->mode == MODE_MOV && track->enc->bits_per_coded_sample)
        put_be16(pb, track->enc->bits_per_coded_sample);
    else
        put_be16(pb, 0x18); /* Reserved */
    put_be16(pb, 0xffff); /* Reserved */
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
    } else if(track->vosLen > 0)
        mov_write_glbl_tag(pb, track);

    return updateSize(pb, pos);
}

static int mov_write_stsd_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stsd");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */
    if (track->enc->codec_type == CODEC_TYPE_VIDEO)
        mov_write_video_tag(pb, track);
    else if (track->enc->codec_type == CODEC_TYPE_AUDIO)
        mov_write_audio_tag(pb, track);
    else if (track->enc->codec_type == CODEC_TYPE_SUBTITLE)
        mov_write_subtitle_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_ctts_tag(ByteIOContext *pb, MOVTrack *track)
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
    put_be32(pb, atom_size); /* size */
    put_tag(pb, "ctts");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, entries); /* entry count */
    for (i=0; i<entries; i++) {
        put_be32(pb, ctts_entries[i].count);
        put_be32(pb, ctts_entries[i].duration);
    }
    av_free(ctts_entries);
    return atom_size;
}

/* Time to sample atom */
static int mov_write_stts_tag(ByteIOContext *pb, MOVTrack *track)
{
    MOVStts *stts_entries;
    uint32_t entries = -1;
    uint32_t atom_size;
    int i;

    if (track->enc->codec_type == CODEC_TYPE_AUDIO && !track->audio_vbr) {
        stts_entries = av_malloc(sizeof(*stts_entries)); /* one entry */
        stts_entries[0].count = track->sampleCount;
        stts_entries[0].duration = 1;
        entries = 1;
    } else {
        stts_entries = av_malloc(track->entry * sizeof(*stts_entries)); /* worst case */
        for (i=0; i<track->entry; i++) {
            int64_t duration = i + 1 == track->entry ?
                track->trackDuration - track->cluster[i].dts + track->cluster[0].dts : /* readjusting */
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
    put_be32(pb, atom_size); /* size */
    put_tag(pb, "stts");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, entries); /* entry count */
    for (i=0; i<entries; i++) {
        put_be32(pb, stts_entries[i].count);
        put_be32(pb, stts_entries[i].duration);
    }
    av_free(stts_entries);
    return atom_size;
}

static int mov_write_dref_tag(ByteIOContext *pb)
{
    put_be32(pb, 28); /* size */
    put_tag(pb, "dref");
    put_be32(pb, 0); /* version & flags */
    put_be32(pb, 1); /* entry count */

    put_be32(pb, 0xc); /* size */
    put_tag(pb, "url ");
    put_be32(pb, 1); /* version & flags */

    return 28;
}

static int mov_write_stbl_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "stbl");
    mov_write_stsd_tag(pb, track);
    mov_write_stts_tag(pb, track);
    if (track->enc->codec_type == CODEC_TYPE_VIDEO &&
        track->hasKeyframes && track->hasKeyframes < track->entry)
        mov_write_stss_tag(pb, track);
    if (track->enc->codec_type == CODEC_TYPE_VIDEO &&
        track->hasBframes)
        mov_write_ctts_tag(pb, track);
    mov_write_stsc_tag(pb, track);
    mov_write_stsz_tag(pb, track);
    mov_write_stco_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_dinf_tag(ByteIOContext *pb)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "dinf");
    mov_write_dref_tag(pb);
    return updateSize(pb, pos);
}

static int mov_write_nmhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 12);
    put_tag(pb, "nmhd");
    put_be32(pb, 0);
    return 12;
}

static int mov_write_gmhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x20);   /* size */
    put_tag(pb, "gmhd");
    put_be32(pb, 0x18);   /* gmin size */
    put_tag(pb, "gmin");  /* generic media info */
    put_be32(pb, 0);      /* version & flags */
    put_be16(pb, 0x40);   /* graphics mode = */
    put_be16(pb, 0x8000); /* opColor (r?) */
    put_be16(pb, 0x8000); /* opColor (g?) */
    put_be16(pb, 0x8000); /* opColor (b?) */
    put_be16(pb, 0);      /* balance */
    put_be16(pb, 0);      /* reserved */
    return 0x20;
}

static int mov_write_smhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 16); /* size */
    put_tag(pb, "smhd");
    put_be32(pb, 0); /* version & flags */
    put_be16(pb, 0); /* reserved (balance, normally = 0) */
    put_be16(pb, 0); /* reserved */
    return 16;
}

static int mov_write_vmhd_tag(ByteIOContext *pb)
{
    put_be32(pb, 0x14); /* size (always 0x14) */
    put_tag(pb, "vmhd");
    put_be32(pb, 0x01); /* version & flags */
    put_be64(pb, 0); /* reserved (graphics mode = copy) */
    return 0x14;
}

static int mov_write_hdlr_tag(ByteIOContext *pb, MOVTrack *track)
{
    const char *hdlr, *descr = NULL, *hdlr_type = NULL;
    int64_t pos = url_ftell(pb);

    if (!track) { /* no media --> data handler */
        hdlr = "dhlr";
        hdlr_type = "url ";
        descr = "DataHandler";
    } else {
        hdlr = (track->mode == MODE_MOV) ? "mhlr" : "\0\0\0\0";
        if (track->enc->codec_type == CODEC_TYPE_VIDEO) {
            hdlr_type = "vide";
            descr = "VideoHandler";
        } else if (track->enc->codec_type == CODEC_TYPE_AUDIO) {
            hdlr_type = "soun";
            descr = "SoundHandler";
        } else if (track->enc->codec_type == CODEC_TYPE_SUBTITLE) {
            if (track->tag == MKTAG('t','x','3','g')) hdlr_type = "sbtl";
            else                                      hdlr_type = "text";
            descr = "SubtitleHandler";
        }
    }

    put_be32(pb, 0); /* size */
    put_tag(pb, "hdlr");
    put_be32(pb, 0); /* Version & flags */
    put_buffer(pb, hdlr, 4); /* handler */
    put_tag(pb, hdlr_type); /* handler type */
    put_be32(pb ,0); /* reserved */
    put_be32(pb ,0); /* reserved */
    put_be32(pb ,0); /* reserved */
    put_byte(pb, strlen(descr)); /* string counter */
    put_buffer(pb, descr, strlen(descr)); /* handler description */
    return updateSize(pb, pos);
}

static int mov_write_minf_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "minf");
    if(track->enc->codec_type == CODEC_TYPE_VIDEO)
        mov_write_vmhd_tag(pb);
    else if (track->enc->codec_type == CODEC_TYPE_AUDIO)
        mov_write_smhd_tag(pb);
    else if (track->enc->codec_type == CODEC_TYPE_SUBTITLE) {
        if (track->tag == MKTAG('t','e','x','t')) mov_write_gmhd_tag(pb);
        else                                      mov_write_nmhd_tag(pb);
    }
    if (track->mode == MODE_MOV) /* FIXME: Why do it for MODE_MOV only ? */
        mov_write_hdlr_tag(pb, NULL);
    mov_write_dinf_tag(pb);
    mov_write_stbl_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_mdhd_tag(ByteIOContext *pb, MOVTrack *track)
{
    int version = track->trackDuration < INT32_MAX ? 0 : 1;

    (version == 1) ? put_be32(pb, 44) : put_be32(pb, 32); /* size */
    put_tag(pb, "mdhd");
    put_byte(pb, version);
    put_be24(pb, 0); /* flags */
    if (version == 1) {
        put_be64(pb, track->time);
        put_be64(pb, track->time);
    } else {
        put_be32(pb, track->time); /* creation time */
        put_be32(pb, track->time); /* modification time */
    }
    put_be32(pb, track->timescale); /* time scale (sample rate for audio) */
    (version == 1) ? put_be64(pb, track->trackDuration) : put_be32(pb, track->trackDuration); /* duration */
    put_be16(pb, track->language); /* language */
    put_be16(pb, 0); /* reserved (quality) */

    if(version!=0 && track->mode == MODE_MOV){
        av_log(NULL, AV_LOG_ERROR,
            "FATAL error, file duration too long for timebase, this file will not be\n"
            "playable with quicktime. Choose a different timebase or a different\n"
            "container format\n");
    }

    return 32;
}

static int mov_write_mdia_tag(ByteIOContext *pb, MOVTrack *track)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "mdia");
    mov_write_mdhd_tag(pb, track);
    mov_write_hdlr_tag(pb, track);
    mov_write_minf_tag(pb, track);
    return updateSize(pb, pos);
}

static int mov_write_tkhd_tag(ByteIOContext *pb, MOVTrack *track, AVStream *st)
{
    int64_t duration = av_rescale_rnd(track->trackDuration, globalTimescale, track->timescale, AV_ROUND_UP);
    int version = duration < INT32_MAX ? 0 : 1;

    (version == 1) ? put_be32(pb, 104) : put_be32(pb, 92); /* size */
    put_tag(pb, "tkhd");
    put_byte(pb, version);
    put_be24(pb, 0xf); /* flags (track enabled) */
    if (version == 1) {
        put_be64(pb, track->time);
        put_be64(pb, track->time);
    } else {
        put_be32(pb, track->time); /* creation time */
        put_be32(pb, track->time); /* modification time */
    }
    put_be32(pb, track->trackID); /* track-id */
    put_be32(pb, 0); /* reserved */
    (version == 1) ? put_be64(pb, duration) : put_be32(pb, duration);

    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0x0); /* reserved (Layer & Alternate group) */
    /* Volume, only for audio */
    if(track->enc->codec_type == CODEC_TYPE_AUDIO)
        put_be16(pb, 0x0100);
    else
        put_be16(pb, 0);
    put_be16(pb, 0); /* reserved */

    /* Matrix structure */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x40000000); /* reserved */

    /* Track width and height, for visual only */
    if(track->enc->codec_type == CODEC_TYPE_VIDEO ||
       track->enc->codec_type == CODEC_TYPE_SUBTITLE) {
        double sample_aspect_ratio = av_q2d(st->sample_aspect_ratio);
        if(!sample_aspect_ratio || track->height != track->enc->height)
            sample_aspect_ratio = 1;
        put_be32(pb, sample_aspect_ratio * track->enc->width*0x10000);
        put_be32(pb, track->height*0x10000);
    }
    else {
        put_be32(pb, 0);
        put_be32(pb, 0);
    }
    return 0x5c;
}

// This box seems important for the psp playback ... without it the movie seems to hang
static int mov_write_edts_tag(ByteIOContext *pb, MOVTrack *track)
{
    put_be32(pb, 0x24); /* size  */
    put_tag(pb, "edts");
    put_be32(pb, 0x1c); /* size  */
    put_tag(pb, "elst");
    put_be32(pb, 0x0);
    put_be32(pb, 0x1);

    put_be32(pb, av_rescale_rnd(track->trackDuration, globalTimescale, track->timescale, AV_ROUND_UP)); /* duration   ... doesn't seem to effect psp */

    put_be32(pb, track->cluster[0].cts); /* first pts is cts since dts is 0 */
    put_be32(pb, 0x00010000);
    return 0x24;
}

// goes at the end of each track!  ... Critical for PSP playback ("Incompatible data" without it)
static int mov_write_uuid_tag_psp(ByteIOContext *pb, MOVTrack *mov)
{
    put_be32(pb, 0x34); /* size ... reports as 28 in mp4box! */
    put_tag(pb, "uuid");
    put_tag(pb, "USMT");
    put_be32(pb, 0x21d24fce);
    put_be32(pb, 0xbb88695c);
    put_be32(pb, 0xfac9c740);
    put_be32(pb, 0x1c);     // another size here!
    put_tag(pb, "MTDT");
    put_be32(pb, 0x00010012);
    put_be32(pb, 0x0a);
    put_be32(pb, 0x55c40000);
    put_be32(pb, 0x1);
    put_be32(pb, 0x0);
    return 0x34;
}

static int mov_write_trak_tag(ByteIOContext *pb, MOVTrack *track, AVStream *st)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "trak");
    mov_write_tkhd_tag(pb, track, st);
    if (track->mode == MODE_PSP || track->hasBframes)
        mov_write_edts_tag(pb, track);  // PSP Movies require edts box
    mov_write_mdia_tag(pb, track);
    if (track->mode == MODE_PSP)
        mov_write_uuid_tag_psp(pb,track);  // PSP Movies require this uuid box
    return updateSize(pb, pos);
}

#if 0
/* TODO: Not sorted out, but not necessary either */
static int mov_write_iods_tag(ByteIOContext *pb, MOVMuxContext *mov)
{
    put_be32(pb, 0x15); /* size */
    put_tag(pb, "iods");
    put_be32(pb, 0);    /* version & flags */
    put_be16(pb, 0x1007);
    put_byte(pb, 0);
    put_be16(pb, 0x4fff);
    put_be16(pb, 0xfffe);
    put_be16(pb, 0x01ff);
    return 0x15;
}
#endif

static int mov_write_mvhd_tag(ByteIOContext *pb, MOVMuxContext *mov)
{
    int maxTrackID = 1, i;
    int64_t maxTrackLenTemp, maxTrackLen = 0;
    int version;

    for (i=0; i<mov->nb_streams; i++) {
        if(mov->tracks[i].entry > 0) {
            maxTrackLenTemp = av_rescale_rnd(mov->tracks[i].trackDuration, globalTimescale, mov->tracks[i].timescale, AV_ROUND_UP);
            if(maxTrackLen < maxTrackLenTemp)
                maxTrackLen = maxTrackLenTemp;
            if(maxTrackID < mov->tracks[i].trackID)
                maxTrackID = mov->tracks[i].trackID;
        }
    }

    version = maxTrackLen < UINT32_MAX ? 0 : 1;
    (version == 1) ? put_be32(pb, 120) : put_be32(pb, 108); /* size */
    put_tag(pb, "mvhd");
    put_byte(pb, version);
    put_be24(pb, 0); /* flags */
    if (version == 1) {
        put_be64(pb, mov->time);
        put_be64(pb, mov->time);
    } else {
        put_be32(pb, mov->time); /* creation time */
        put_be32(pb, mov->time); /* modification time */
    }
    put_be32(pb, mov->timescale); /* timescale */
    (version == 1) ? put_be64(pb, maxTrackLen) : put_be32(pb, maxTrackLen); /* duration of longest track */

    put_be32(pb, 0x00010000); /* reserved (preferred rate) 1.0 = normal */
    put_be16(pb, 0x0100); /* reserved (preferred volume) 1.0 = normal */
    put_be16(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */
    put_be32(pb, 0); /* reserved */

    /* Matrix structure */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x00010000); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x0); /* reserved */
    put_be32(pb, 0x40000000); /* reserved */

    put_be32(pb, 0); /* reserved (preview time) */
    put_be32(pb, 0); /* reserved (preview duration) */
    put_be32(pb, 0); /* reserved (poster time) */
    put_be32(pb, 0); /* reserved (selection time) */
    put_be32(pb, 0); /* reserved (selection duration) */
    put_be32(pb, 0); /* reserved (current time) */
    put_be32(pb, maxTrackID+1); /* Next track id */
    return 0x6c;
}

static int mov_write_itunes_hdlr_tag(ByteIOContext *pb, MOVMuxContext *mov,
                                     AVFormatContext *s)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "hdlr");
    put_be32(pb, 0);
    put_be32(pb, 0);
    put_tag(pb, "mdir");
    put_tag(pb, "appl");
    put_be32(pb, 0);
    put_be32(pb, 0);
    put_be16(pb, 0);
    return updateSize(pb, pos);
}

/* helper function to write a data tag with the specified string as data */
static int mov_write_string_data_tag(ByteIOContext *pb, const char *data, int lang, int long_style)
{
    if(long_style){
        int64_t pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "data");
        put_be32(pb, 1);
        put_be32(pb, 0);
        put_buffer(pb, data, strlen(data));
        return updateSize(pb, pos);
    }else{
        put_be16(pb, strlen(data)); /* string length */
        put_be16(pb, lang);
        put_buffer(pb, data, strlen(data));
        return strlen(data) + 4;
    }
}

static int mov_write_string_tag(ByteIOContext *pb, const char *name, const char *value, int lang, int long_style){
    int size = 0;
    if (value && value[0]) {
        int64_t pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, name);
        mov_write_string_data_tag(pb, value, lang, long_style);
        size= updateSize(pb, pos);
    }
    return size;
}

static int mov_write_string_metadata(AVFormatContext *s, ByteIOContext *pb,
                                     const char *name, const char *tag,
                                     int long_style)
{
    int l, lang = 0, len, len2;
    AVMetadataTag *t, *t2 = NULL;
    char tag2[16];

    if (!(t = av_metadata_get(s->metadata, tag, NULL, 0)))
        return 0;

    len = strlen(t->key);
    snprintf(tag2, sizeof(tag2), "%s-", tag);
    while ((t2 = av_metadata_get(s->metadata, tag2, t2, AV_METADATA_IGNORE_SUFFIX))) {
        len2 = strlen(t2->key);
        if (len2 == len+4 && !strcmp(t->value, t2->value)
            && (l=ff_mov_iso639_to_lang(&t2->key[len2-3], 0)) >= 0) {
            lang = l;
            break;
        }
    }
    return mov_write_string_tag(pb, name, t->value, lang, long_style);
}

/* iTunes track number */
static int mov_write_trkn_tag(ByteIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    AVMetadataTag *t = av_metadata_get(s->metadata, "track", NULL, 0);
    int size = 0, track = t ? atoi(t->value) : 0;
    if (track) {
        int64_t pos = url_ftell(pb);
        put_be32(pb, 0); /* size */
        put_tag(pb, "trkn");
        {
            int64_t pos = url_ftell(pb);
            put_be32(pb, 0); /* size */
            put_tag(pb, "data");
            put_be32(pb, 0);        // 8 bytes empty
            put_be32(pb, 0);
            put_be16(pb, 0);        // empty
            put_be16(pb, track);    // track number
            put_be16(pb, 0);        // total track number
            put_be16(pb, 0);        // empty
            updateSize(pb, pos);
        }
        size = updateSize(pb, pos);
    }
    return size;
}

/* iTunes meta data list */
static int mov_write_ilst_tag(ByteIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "ilst");
    mov_write_string_metadata(s, pb, "\251nam", "title"    , 1);
    mov_write_string_metadata(s, pb, "\251ART", "author"   , 1);
    mov_write_string_metadata(s, pb, "\251wrt", "author"   , 1);
    mov_write_string_metadata(s, pb, "\251alb", "album"    , 1);
    mov_write_string_metadata(s, pb, "\251day", "year"     , 1);
    mov_write_string_tag(pb, "\251too", LIBAVFORMAT_IDENT, 0, 1);
    mov_write_string_metadata(s, pb, "\251cmt", "comment"  , 1);
    mov_write_string_metadata(s, pb, "\251gen", "genre"    , 1);
    mov_write_string_metadata(s, pb, "\251cpy", "copyright", 1);
    mov_write_trkn_tag(pb, mov, s);
    return updateSize(pb, pos);
}

/* iTunes meta data tag */
static int mov_write_meta_tag(ByteIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int size = 0;
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size */
    put_tag(pb, "meta");
    put_be32(pb, 0);
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

static int ascii_to_wc(ByteIOContext *pb, const uint8_t *b)
{
    int val;
    while(*b){
        GET_UTF8(val, *b++, return -1;)
        put_be16(pb, val);
    }
    put_be16(pb, 0x00);
    return 0;
}

static uint16_t language_code(const char *str)
{
    return (((str[0]-0x60) & 0x1F) << 10) + (((str[1]-0x60) & 0x1F) << 5) + ((str[2]-0x60) & 0x1F);
}

static int mov_write_3gp_udta_tag(ByteIOContext *pb, AVFormatContext *s,
                                  const char *tag, const char *str)
{
    int64_t pos = url_ftell(pb);
    AVMetadataTag *t = av_metadata_get(s->metadata, str, NULL, 0);
    if (!t || !utf8len(t->value))
        return 0;
    put_be32(pb, 0);   /* size */
    put_tag (pb, tag); /* type */
    put_be32(pb, 0);   /* version + flags */
    if (!strcmp(tag, "yrrc"))
        put_be16(pb, atoi(t->value));
    else {
        put_be16(pb, language_code("eng")); /* language */
        ascii_to_wc(pb, t->value);
        if (!strcmp(tag, "albm") &&
            (t = av_metadata_get(s->metadata, "year", NULL, 0)))
            put_byte(pb, atoi(t->value));
    }
    return updateSize(pb, pos);
}

static int mov_write_udta_tag(ByteIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    ByteIOContext *pb_buf;
    int i, ret, size;
    uint8_t *buf;

    for (i = 0; i < s->nb_streams; i++)
        if (mov->tracks[i].enc->flags & CODEC_FLAG_BITEXACT) {
            return 0;
        }

    ret = url_open_dyn_buf(&pb_buf);
    if(ret < 0)
        return ret;

        if (mov->mode & MODE_3GP) {
            mov_write_3gp_udta_tag(pb_buf, s, "titl", "title");
            mov_write_3gp_udta_tag(pb_buf, s, "auth", "author");
            mov_write_3gp_udta_tag(pb_buf, s, "gnre", "genre");
            mov_write_3gp_udta_tag(pb_buf, s, "dscp", "comment");
            mov_write_3gp_udta_tag(pb_buf, s, "albm", "album");
            mov_write_3gp_udta_tag(pb_buf, s, "cprt", "copyright");
            mov_write_3gp_udta_tag(pb_buf, s, "yrrc", "year");
        } else if (mov->mode == MODE_MOV) { // the title field breaks gtkpod with mp4 and my suspicion is that stuff is not valid in mp4
            mov_write_string_metadata(s, pb_buf, "\251nam", "title"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251aut", "author"     , 0);
            mov_write_string_metadata(s, pb_buf, "\251alb", "album"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251day", "year"       , 0);
            mov_write_string_tag(pb_buf, "\251enc", LIBAVFORMAT_IDENT, 0, 0);
            mov_write_string_metadata(s, pb_buf, "\251des", "comment"    , 0);
            mov_write_string_metadata(s, pb_buf, "\251gen", "genre"      , 0);
            mov_write_string_metadata(s, pb_buf, "\251cpy", "copyright"  , 0);
        } else {
            /* iTunes meta data */
            mov_write_meta_tag(pb_buf, mov, s);
        }

    if ((size = url_close_dyn_buf(pb_buf, &buf)) > 0) {
        put_be32(pb, size+8);
        put_tag(pb, "udta");
        put_buffer(pb, buf, size);
        av_free(buf);
    }

    return 0;
}

static void mov_write_psp_udta_tag(ByteIOContext *pb,
                                  const char *str, const char *lang, int type)
{
    int len = utf8len(str)+1;
    if(len<=0)
        return;
    put_be16(pb, len*2+10);            /* size */
    put_be32(pb, type);                /* type */
    put_be16(pb, language_code(lang)); /* language */
    put_be16(pb, 0x01);                /* ? */
    ascii_to_wc(pb, str);
}

static int mov_write_uuidusmt_tag(ByteIOContext *pb, AVFormatContext *s)
{
    AVMetadataTag *title = av_metadata_get(s->metadata, "title", NULL, 0);
    int64_t pos, pos2;

    if (title) {
        pos = url_ftell(pb);
        put_be32(pb, 0); /* size placeholder*/
        put_tag(pb, "uuid");
        put_tag(pb, "USMT");
        put_be32(pb, 0x21d24fce); /* 96 bit UUID */
        put_be32(pb, 0xbb88695c);
        put_be32(pb, 0xfac9c740);

        pos2 = url_ftell(pb);
        put_be32(pb, 0); /* size placeholder*/
        put_tag(pb, "MTDT");
        put_be16(pb, 4);

        // ?
        put_be16(pb, 0x0C);                 /* size */
        put_be32(pb, 0x0B);                 /* type */
        put_be16(pb, language_code("und")); /* language */
        put_be16(pb, 0x0);                  /* ? */
        put_be16(pb, 0x021C);               /* data */

        mov_write_psp_udta_tag(pb, LIBAVCODEC_IDENT,      "eng", 0x04);
        mov_write_psp_udta_tag(pb, title->value,          "eng", 0x01);
//        snprintf(dt,32,"%04d/%02d/%02d %02d:%02d:%02d",t_st->tm_year+1900,t_st->tm_mon+1,t_st->tm_mday,t_st->tm_hour,t_st->tm_min,t_st->tm_sec);
        mov_write_psp_udta_tag(pb, "2006/04/01 11:11:11", "und", 0x03);

        updateSize(pb, pos2);
        return updateSize(pb, pos);
    }

    return 0;
}

static int mov_write_moov_tag(ByteIOContext *pb, MOVMuxContext *mov,
                              AVFormatContext *s)
{
    int i;
    int64_t pos = url_ftell(pb);
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "moov");
    mov->timescale = globalTimescale;

    for (i=0; i<mov->nb_streams; i++) {
        if(mov->tracks[i].entry <= 0) continue;

        mov->tracks[i].time = mov->time;
        mov->tracks[i].trackID = i+1;
    }

    mov_write_mvhd_tag(pb, mov);
    //mov_write_iods_tag(pb, mov);
    for (i=0; i<mov->nb_streams; i++) {
        if(mov->tracks[i].entry > 0) {
            mov_write_trak_tag(pb, &(mov->tracks[i]), s->streams[i]);
        }
    }

    if (mov->mode == MODE_PSP)
        mov_write_uuidusmt_tag(pb, s);
    else
        mov_write_udta_tag(pb, mov, s);

    return updateSize(pb, pos);
}

static int mov_write_mdat_tag(ByteIOContext *pb, MOVMuxContext *mov)
{
    put_be32(pb, 8);    // placeholder for extended size field (64 bit)
    put_tag(pb, mov->mode == MODE_MOV ? "wide" : "free");

    mov->mdat_pos = url_ftell(pb);
    put_be32(pb, 0); /* size placeholder*/
    put_tag(pb, "mdat");
    return 0;
}

/* TODO: This needs to be more general */
static int mov_write_ftyp_tag(ByteIOContext *pb, AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    int64_t pos = url_ftell(pb);
    int has_h264 = 0, has_video = 0;
    int minor = 0x200;
    int i;

    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO)
            has_video = 1;
        if (st->codec->codec_id == CODEC_ID_H264)
            has_h264 = 1;
    }

    put_be32(pb, 0); /* size */
    put_tag(pb, "ftyp");

    if (mov->mode == MODE_3GP) {
        put_tag(pb, has_h264 ? "3gp6"  : "3gp4");
        minor =     has_h264 ?   0x100 :   0x200;
    } else if (mov->mode & MODE_3G2) {
        put_tag(pb, has_h264 ? "3g2b"  : "3g2a");
        minor =     has_h264 ? 0x20000 : 0x10000;
    }else if (mov->mode == MODE_PSP)
        put_tag(pb, "MSNV");
    else if (mov->mode == MODE_MP4)
        put_tag(pb, "isom");
    else if (mov->mode == MODE_IPOD)
        put_tag(pb, has_video ? "M4V ":"M4A ");
    else
        put_tag(pb, "qt  ");

    put_be32(pb, minor);

    if(mov->mode == MODE_MOV)
        put_tag(pb, "qt  ");
    else{
        put_tag(pb, "isom");
        put_tag(pb, "iso2");
        if(has_h264)
            put_tag(pb, "avc1");
    }

    if (mov->mode == MODE_3GP)
        put_tag(pb, has_h264 ? "3gp6":"3gp4");
    else if (mov->mode & MODE_3G2)
        put_tag(pb, has_h264 ? "3g2b":"3g2a");
    else if (mov->mode == MODE_PSP)
        put_tag(pb, "MSNV");
    else if (mov->mode == MODE_MP4)
        put_tag(pb, "mp41");
    return updateSize(pb, pos);
}

static void mov_write_uuidprof_tag(ByteIOContext *pb, AVFormatContext *s)
{
    AVCodecContext *VideoCodec = s->streams[0]->codec;
    AVCodecContext *AudioCodec = s->streams[1]->codec;
    int AudioRate = AudioCodec->sample_rate;
    int FrameRate = ((VideoCodec->time_base.den) * (0x10000))/ (VideoCodec->time_base.num);
    int audio_kbitrate= AudioCodec->bit_rate / 1000;
    int video_kbitrate= FFMIN(VideoCodec->bit_rate / 1000, 800 - audio_kbitrate);

    put_be32(pb, 0x94); /* size */
    put_tag(pb, "uuid");
    put_tag(pb, "PROF");

    put_be32(pb, 0x21d24fce); /* 96 bit UUID */
    put_be32(pb, 0xbb88695c);
    put_be32(pb, 0xfac9c740);

    put_be32(pb, 0x0);  /* ? */
    put_be32(pb, 0x3);  /* 3 sections ? */

    put_be32(pb, 0x14); /* size */
    put_tag(pb, "FPRF");
    put_be32(pb, 0x0);  /* ? */
    put_be32(pb, 0x0);  /* ? */
    put_be32(pb, 0x0);  /* ? */

    put_be32(pb, 0x2c);  /* size */
    put_tag(pb, "APRF");   /* audio */
    put_be32(pb, 0x0);
    put_be32(pb, 0x2);   /* TrackID */
    put_tag(pb, "mp4a");
    put_be32(pb, 0x20f);
    put_be32(pb, 0x0);
    put_be32(pb, audio_kbitrate);
    put_be32(pb, audio_kbitrate);
    put_be32(pb, AudioRate);
    put_be32(pb, AudioCodec->channels);

    put_be32(pb, 0x34);  /* size */
    put_tag(pb, "VPRF");   /* video */
    put_be32(pb, 0x0);
    put_be32(pb, 0x1);    /* TrackID */
    if (VideoCodec->codec_id == CODEC_ID_H264) {
        put_tag(pb, "avc1");
        put_be16(pb, 0x014D);
        put_be16(pb, 0x0015);
    } else {
        put_tag(pb, "mp4v");
        put_be16(pb, 0x0000);
        put_be16(pb, 0x0103);
    }
    put_be32(pb, 0x0);
    put_be32(pb, video_kbitrate);
    put_be32(pb, video_kbitrate);
    put_be32(pb, FrameRate);
    put_be32(pb, FrameRate);
    put_be16(pb, VideoCodec->width);
    put_be16(pb, VideoCodec->height);
    put_be32(pb, 0x010001); /* ? */
}

static int mov_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    MOVMuxContext *mov = s->priv_data;
    int i;

    if (url_is_streamed(s->pb)) {
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

        mov_write_ftyp_tag(pb,s);
        if (mov->mode == MODE_PSP) {
            if (s->nb_streams != 2) {
                av_log(s, AV_LOG_ERROR, "PSP mode need one video and one audio stream\n");
                return -1;
            }
            mov_write_uuidprof_tag(pb,s);
        }
    }

    for(i=0; i<s->nb_streams; i++){
        AVStream *st= s->streams[i];
        MOVTrack *track= &mov->tracks[i];
        AVMetadataTag *lang = av_metadata_get(st->metadata, "language", NULL,0);

        track->enc = st->codec;
        track->language = ff_mov_iso639_to_lang(lang?lang->value:"und", mov->mode!=MODE_MOV);
        if (track->language < 0)
            track->language = 0;
        track->mode = mov->mode;
        track->tag = mov_find_codec_tag(s, track);
        if (!track->tag) {
            av_log(s, AV_LOG_ERROR, "track %d: could not find tag, "
                   "codec not currently supported in container\n", i);
            return -1;
        }
        if(st->codec->codec_type == CODEC_TYPE_VIDEO){
            if (track->tag == MKTAG('m','x','3','p') || track->tag == MKTAG('m','x','3','n') ||
                track->tag == MKTAG('m','x','4','p') || track->tag == MKTAG('m','x','4','n') ||
                track->tag == MKTAG('m','x','5','p') || track->tag == MKTAG('m','x','5','n')) {
                if (st->codec->width != 720 || (st->codec->height != 608 && st->codec->height != 512)) {
                    av_log(s, AV_LOG_ERROR, "D-10/IMX must use 720x608 or 720x512 video resolution\n");
                    return -1;
                }
                track->height = track->tag>>24 == 'n' ? 486 : 576;
            }
            track->timescale = st->codec->time_base.den;
            av_set_pts_info(st, 64, 1, st->codec->time_base.den);
            if (track->mode == MODE_MOV && track->timescale > 100000)
                av_log(s, AV_LOG_WARNING,
                       "WARNING codec timebase is very high. If duration is too long,\n"
                       "file may not be playable by quicktime. Specify a shorter timebase\n"
                       "or choose different container.\n");
        }else if(st->codec->codec_type == CODEC_TYPE_AUDIO){
            track->timescale = st->codec->sample_rate;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            if(!st->codec->frame_size && !av_get_bits_per_sample(st->codec->codec_id)) {
                av_log(s, AV_LOG_ERROR, "track %d: codec frame size is not set\n", i);
                return -1;
            }else if(st->codec->frame_size > 1){ /* assume compressed audio */
                track->audio_vbr = 1;
            }else{
                st->codec->frame_size = 1;
                track->sampleSize = (av_get_bits_per_sample(st->codec->codec_id) >> 3) * st->codec->channels;
            }
            if(track->mode != MODE_MOV &&
               track->enc->codec_id == CODEC_ID_MP3 && track->enc->sample_rate < 16000){
                av_log(s, AV_LOG_ERROR, "track %d: muxing mp3 at %dhz is not supported\n",
                       i, track->enc->sample_rate);
                return -1;
            }
        }else if(st->codec->codec_type == CODEC_TYPE_SUBTITLE){
            track->timescale = st->codec->time_base.den;
            av_set_pts_info(st, 64, 1, st->codec->time_base.den);
        }
        if (!track->height)
            track->height = st->codec->height;
    }

    mov_write_mdat_tag(pb, mov);
    mov->time = s->timestamp + 0x7C25B080; //1970 based -> 1904 based
    mov->nb_streams = s->nb_streams;

    put_flush_packet(pb);

    return 0;
}

static int mov_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    MOVMuxContext *mov = s->priv_data;
    ByteIOContext *pb = s->pb;
    MOVTrack *trk = &mov->tracks[pkt->stream_index];
    AVCodecContext *enc = trk->enc;
    unsigned int samplesInChunk = 0;
    int size= pkt->size;

    if (url_is_streamed(s->pb)) return 0; /* Can't handle that */
    if (!size) return 0; /* Discard 0 sized packets */

    if (enc->codec_id == CODEC_ID_AMR_NB) {
        /* We must find out how many AMR blocks there are in one packet */
        static uint16_t packed_size[16] =
            {13, 14, 16, 18, 20, 21, 27, 32, 6, 0, 0, 0, 0, 0, 0, 0};
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
        size = ff_avc_parse_nal_units(pb, pkt->data, pkt->size);
    } else {
        put_buffer(pb, pkt->data, size);
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

    trk->cluster[trk->entry].pos = url_ftell(pb) - size;
    trk->cluster[trk->entry].samplesInChunk = samplesInChunk;
    trk->cluster[trk->entry].size = size;
    trk->cluster[trk->entry].entries = samplesInChunk;
    trk->cluster[trk->entry].dts = pkt->dts;
    trk->trackDuration = pkt->dts - trk->cluster[0].dts + pkt->duration;

    if (pkt->pts == AV_NOPTS_VALUE) {
        av_log(s, AV_LOG_WARNING, "pts has no value\n");
        pkt->pts = pkt->dts;
    }
    if (pkt->dts != pkt->pts)
        trk->hasBframes = 1;
    trk->cluster[trk->entry].cts = pkt->pts - pkt->dts;
    trk->cluster[trk->entry].key_frame = !!(pkt->flags & PKT_FLAG_KEY);
    if(trk->cluster[trk->entry].key_frame)
        trk->hasKeyframes++;
    trk->entry++;
    trk->sampleCount += samplesInChunk;
    mov->mdat_size += size;

    put_flush_packet(pb);
    return 0;
}

static int mov_write_trailer(AVFormatContext *s)
{
    MOVMuxContext *mov = s->priv_data;
    ByteIOContext *pb = s->pb;
    int res = 0;
    int i;

    int64_t moov_pos = url_ftell(pb);

    /* Write size of mdat tag */
    if (mov->mdat_size+8 <= UINT32_MAX) {
        url_fseek(pb, mov->mdat_pos, SEEK_SET);
        put_be32(pb, mov->mdat_size+8);
    } else {
        /* overwrite 'wide' placeholder atom */
        url_fseek(pb, mov->mdat_pos - 8, SEEK_SET);
        put_be32(pb, 1); /* special value: real atom size will be 64 bit value after tag field */
        put_tag(pb, "mdat");
        put_be64(pb, mov->mdat_size+16);
    }
    url_fseek(pb, moov_pos, SEEK_SET);

    mov_write_moov_tag(pb, mov, s);

    for (i=0; i<mov->nb_streams; i++) {
        av_freep(&mov->tracks[i].cluster);

        if(mov->tracks[i].vosLen) av_free(mov->tracks[i].vosData);

    }

    put_flush_packet(pb);

    return res;
}

#if CONFIG_MOV_MUXER
AVOutputFormat mov_muxer = {
    "mov",
    NULL_IF_CONFIG_SMALL("MOV format"),
    NULL,
    "mov",
    sizeof(MOVMuxContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
    .codec_tag = (const AVCodecTag* const []){codec_movvideo_tags, codec_movaudio_tags, 0},
};
#endif
#if CONFIG_TGP_MUXER
AVOutputFormat tgp_muxer = {
    "3gp",
    NULL_IF_CONFIG_SMALL("3GP format"),
    NULL,
    "3gp",
    sizeof(MOVMuxContext),
    CODEC_ID_AMR_NB,
    CODEC_ID_H263,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag = (const AVCodecTag* const []){codec_3gp_tags, 0},
};
#endif
#if CONFIG_MP4_MUXER
AVOutputFormat mp4_muxer = {
    "mp4",
    NULL_IF_CONFIG_SMALL("MP4 format"),
    "application/mp4",
    "mp4",
    sizeof(MOVMuxContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
    .codec_tag = (const AVCodecTag* const []){ff_mp4_obj_type, 0},
};
#endif
#if CONFIG_PSP_MUXER
AVOutputFormat psp_muxer = {
    "psp",
    NULL_IF_CONFIG_SMALL("PSP MP4 format"),
    NULL,
    "mp4,psp",
    sizeof(MOVMuxContext),
    CODEC_ID_AAC,
    CODEC_ID_MPEG4,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag = (const AVCodecTag* const []){ff_mp4_obj_type, 0},
};
#endif
#if CONFIG_TG2_MUXER
AVOutputFormat tg2_muxer = {
    "3g2",
    NULL_IF_CONFIG_SMALL("3GP2 format"),
    NULL,
    "3g2",
    sizeof(MOVMuxContext),
    CODEC_ID_AMR_NB,
    CODEC_ID_H263,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag = (const AVCodecTag* const []){codec_3gp_tags, 0},
};
#endif
#if CONFIG_IPOD_MUXER
AVOutputFormat ipod_muxer = {
    "ipod",
    NULL_IF_CONFIG_SMALL("iPod H.264 MP4 format"),
    "application/mp4",
    "m4v,m4a",
    sizeof(MOVMuxContext),
    CODEC_ID_AAC,
    CODEC_ID_H264,
    mov_write_header,
    mov_write_packet,
    mov_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
    .codec_tag = (const AVCodecTag* const []){codec_ipod_tags, 0},
};
#endif
