/*
 * RIFF codec tags
 * Copyright (c) 2000 Fabrice Bellard.
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
#include "avcodec.h"
#include "riff.h"
#include "allformats.h" // for asf_muxer

/* Note: when encoding, the first matching tag is used, so order is
   important if multiple tags possible for a given codec. */
const AVCodecTag codec_bmp_tags[] = {
    { CODEC_ID_H264, MKTAG('H', '2', '6', '4') },
    { CODEC_ID_H264, MKTAG('h', '2', '6', '4') },
    { CODEC_ID_H264, MKTAG('X', '2', '6', '4') },
    { CODEC_ID_H264, MKTAG('x', '2', '6', '4') },
    { CODEC_ID_H264, MKTAG('a', 'v', 'c', '1') },
    { CODEC_ID_H264, MKTAG('V', 'S', 'S', 'H') },

    { CODEC_ID_H263, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('H', '2', '6', '3') },
    { CODEC_ID_H263I, MKTAG('I', '2', '6', '3') }, /* intel h263 */
    { CODEC_ID_H261, MKTAG('H', '2', '6', '1') },

    /* added based on MPlayer */
    { CODEC_ID_H263P, MKTAG('U', '2', '6', '3') },
    { CODEC_ID_H263P, MKTAG('v', 'i', 'v', '1') },

    { CODEC_ID_MPEG4, MKTAG('F', 'M', 'P', '4') },
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', 'X') },
    { CODEC_ID_MPEG4, MKTAG('D', 'X', '5', '0') },
    { CODEC_ID_MPEG4, MKTAG('X', 'V', 'I', 'D') },
    { CODEC_ID_MPEG4, MKTAG('M', 'P', '4', 'S') },
    { CODEC_ID_MPEG4, MKTAG('M', '4', 'S', '2') },
    { CODEC_ID_MPEG4, MKTAG(0x04, 0, 0, 0) }, /* some broken avi use this */

    /* added based on MPlayer */
    { CODEC_ID_MPEG4, MKTAG('D', 'I', 'V', '1') },
    { CODEC_ID_MPEG4, MKTAG('B', 'L', 'Z', '0') },
    { CODEC_ID_MPEG4, MKTAG('m', 'p', '4', 'v') },
    { CODEC_ID_MPEG4, MKTAG('U', 'M', 'P', '4') },
    { CODEC_ID_MPEG4, MKTAG('W', 'V', '1', 'F') },
    { CODEC_ID_MPEG4, MKTAG('S', 'E', 'D', 'G') },

    { CODEC_ID_MPEG4, MKTAG('R', 'M', 'P', '4') },

    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '3') }, /* default signature when using MSMPEG4 */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', '4', '3') },

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V3, MKTAG('M', 'P', 'G', '3') },
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '5') },
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '6') },
    { CODEC_ID_MSMPEG4V3, MKTAG('D', 'I', 'V', '4') },
    { CODEC_ID_MSMPEG4V3, MKTAG('A', 'P', '4', '1') },
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '1') },
    { CODEC_ID_MSMPEG4V3, MKTAG('C', 'O', 'L', '0') },

    { CODEC_ID_MSMPEG4V2, MKTAG('M', 'P', '4', '2') },

    /* added based on MPlayer */
    { CODEC_ID_MSMPEG4V2, MKTAG('D', 'I', 'V', '2') },

    { CODEC_ID_MSMPEG4V1, MKTAG('M', 'P', 'G', '4') },

    { CODEC_ID_WMV1, MKTAG('W', 'M', 'V', '1') },

    /* added based on MPlayer */
    { CODEC_ID_WMV2, MKTAG('W', 'M', 'V', '2') },
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'd') },
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 'h', 'd') },
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', 's', 'l') },
    { CODEC_ID_DVVIDEO, MKTAG('d', 'v', '2', '5') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '1') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('m', 'p', 'g', '2') },
    { CODEC_ID_MPEG2VIDEO, MKTAG('m', 'p', 'g', '2') },
    { CODEC_ID_MPEG2VIDEO, MKTAG('M', 'P', 'E', 'G') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('P', 'I', 'M', '1') },
    { CODEC_ID_MPEG1VIDEO, MKTAG('V', 'C', 'R', '2') },
    { CODEC_ID_MPEG1VIDEO, 0x10000001 },
    { CODEC_ID_MPEG2VIDEO, 0x10000002 },
    { CODEC_ID_MPEG2VIDEO, MKTAG('D', 'V', 'R', ' ') },
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG, MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_LJPEG, MKTAG('L', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG, MKTAG('J', 'P', 'G', 'L') }, /* Pegasus lossless JPEG */
    { CODEC_ID_MJPEG, MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - decoder */
    { CODEC_ID_MJPEG, MKTAG('j', 'p', 'e', 'g') },
    { CODEC_ID_MJPEG, MKTAG('I', 'J', 'P', 'G') },
    { CODEC_ID_MJPEG, MKTAG('A', 'V', 'R', 'n') },
    { CODEC_ID_JPEGLS, MKTAG('M', 'J', 'L', 'S') }, /* JPEG-LS custom FOURCC for avi - encoder */
    { CODEC_ID_HUFFYUV, MKTAG('H', 'F', 'Y', 'U') },
    { CODEC_ID_FFVHUFF, MKTAG('F', 'F', 'V', 'H') },
    { CODEC_ID_CYUV, MKTAG('C', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO, 0 },
    { CODEC_ID_RAWVIDEO, MKTAG('I', '4', '2', '0') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', 'U', 'Y', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', '4', '2', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', 'V', '1', '2') },
    { CODEC_ID_RAWVIDEO, MKTAG('U', 'Y', 'V', 'Y') },
    { CODEC_ID_RAWVIDEO, MKTAG('I', 'Y', 'U', 'V') },
    { CODEC_ID_RAWVIDEO, MKTAG('Y', '8', '0', '0') },
    { CODEC_ID_INDEO3, MKTAG('I', 'V', '3', '1') },
    { CODEC_ID_INDEO3, MKTAG('I', 'V', '3', '2') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '1') },
    { CODEC_ID_VP3, MKTAG('V', 'P', '3', '0') },
    { CODEC_ID_VP5, MKTAG('V', 'P', '5', '0') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '0') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '1') },
    { CODEC_ID_VP6, MKTAG('V', 'P', '6', '2') },
    { CODEC_ID_ASV1, MKTAG('A', 'S', 'V', '1') },
    { CODEC_ID_ASV2, MKTAG('A', 'S', 'V', '2') },
    { CODEC_ID_VCR1, MKTAG('V', 'C', 'R', '1') },
    { CODEC_ID_FFV1, MKTAG('F', 'F', 'V', '1') },
    { CODEC_ID_XAN_WC4, MKTAG('X', 'x', 'a', 'n') },
    { CODEC_ID_MSRLE, MKTAG('m', 'r', 'l', 'e') },
    { CODEC_ID_MSRLE, MKTAG(0x1, 0x0, 0x0, 0x0) },
    { CODEC_ID_MSVIDEO1, MKTAG('M', 'S', 'V', 'C') },
    { CODEC_ID_MSVIDEO1, MKTAG('m', 's', 'v', 'c') },
    { CODEC_ID_MSVIDEO1, MKTAG('C', 'R', 'A', 'M') },
    { CODEC_ID_MSVIDEO1, MKTAG('c', 'r', 'a', 'm') },
    { CODEC_ID_MSVIDEO1, MKTAG('W', 'H', 'A', 'M') },
    { CODEC_ID_MSVIDEO1, MKTAG('w', 'h', 'a', 'm') },
    { CODEC_ID_CINEPAK, MKTAG('c', 'v', 'i', 'd') },
    { CODEC_ID_TRUEMOTION1, MKTAG('D', 'U', 'C', 'K') },
    { CODEC_ID_MSZH, MKTAG('M', 'S', 'Z', 'H') },
    { CODEC_ID_ZLIB, MKTAG('Z', 'L', 'I', 'B') },
    { CODEC_ID_SNOW, MKTAG('S', 'N', 'O', 'W') },
    { CODEC_ID_4XM, MKTAG('4', 'X', 'M', 'V') },
    { CODEC_ID_FLV1, MKTAG('F', 'L', 'V', '1') },
    { CODEC_ID_FLASHSV, MKTAG('F', 'S', 'V', '1') },
    { CODEC_ID_VP6F, MKTAG('V', 'P', '6', 'F') },
    { CODEC_ID_SVQ1, MKTAG('s', 'v', 'q', '1') },
    { CODEC_ID_TSCC, MKTAG('t', 's', 'c', 'c') },
    { CODEC_ID_ULTI, MKTAG('U', 'L', 'T', 'I') },
    { CODEC_ID_VIXL, MKTAG('V', 'I', 'X', 'L') },
    { CODEC_ID_QPEG, MKTAG('Q', 'P', 'E', 'G') },
    { CODEC_ID_QPEG, MKTAG('Q', '1', '.', '0') },
    { CODEC_ID_QPEG, MKTAG('Q', '1', '.', '1') },
    { CODEC_ID_WMV3, MKTAG('W', 'M', 'V', '3') },
    { CODEC_ID_VC1, MKTAG('W', 'V', 'C', '1') },
    { CODEC_ID_VC1, MKTAG('W', 'M', 'V', 'A') },
    { CODEC_ID_LOCO, MKTAG('L', 'O', 'C', 'O') },
    { CODEC_ID_WNV1, MKTAG('W', 'N', 'V', '1') },
    { CODEC_ID_AASC, MKTAG('A', 'A', 'S', 'C') },
    { CODEC_ID_INDEO2, MKTAG('R', 'T', '2', '1') },
    { CODEC_ID_FRAPS, MKTAG('F', 'P', 'S', '1') },
    { CODEC_ID_THEORA, MKTAG('t', 'h', 'e', 'o') },
    { CODEC_ID_TRUEMOTION2, MKTAG('T', 'M', '2', '0') },
    { CODEC_ID_CSCD, MKTAG('C', 'S', 'C', 'D') },
    { CODEC_ID_ZMBV, MKTAG('Z', 'M', 'B', 'V') },
    { CODEC_ID_KMVC, MKTAG('K', 'M', 'V', 'C') },
    { CODEC_ID_CAVS, MKTAG('C', 'A', 'V', 'S') },
    { CODEC_ID_JPEG2000, MKTAG('M', 'J', '2', 'C') },
    { CODEC_ID_VMNC, MKTAG('V', 'M', 'n', 'c') },
    { CODEC_ID_TARGA, MKTAG('t', 'g', 'a', ' ') },
    { CODEC_ID_NONE, 0 },
};

const AVCodecTag codec_wav_tags[] = {
    { CODEC_ID_MP2, 0x50 },
    { CODEC_ID_MP3, 0x55 },
    { CODEC_ID_AC3, 0x2000 },
    { CODEC_ID_DTS, 0x2001 },
    { CODEC_ID_PCM_S16LE, 0x01 },
    { CODEC_ID_PCM_U8, 0x01 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_S24LE, 0x01 },
    { CODEC_ID_PCM_S32LE, 0x01 },
    { CODEC_ID_PCM_ALAW, 0x06 },
    { CODEC_ID_PCM_MULAW, 0x07 },
    { CODEC_ID_ADPCM_MS, 0x02 },
    { CODEC_ID_ADPCM_IMA_WAV, 0x11 },
    { CODEC_ID_ADPCM_YAMAHA, 0x20 },
    { CODEC_ID_ADPCM_G726, 0x45 },
    { CODEC_ID_ADPCM_IMA_DK4, 0x61 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_DK3, 0x62 },  /* rogue format number */
    { CODEC_ID_WMAV1, 0x160 },
    { CODEC_ID_WMAV2, 0x161 },
    { CODEC_ID_AAC, 0x706d },
    { CODEC_ID_AAC, 0xff },
    { CODEC_ID_VORBIS, ('V'<<8)+'o' }, //HACK/FIXME, does vorbis in WAV/AVI have an (in)official id?
    { CODEC_ID_SONIC, 0x2048 },
    { CODEC_ID_SONIC_LS, 0x2048 },
    { CODEC_ID_ADPCM_CT, 0x200 },
    { CODEC_ID_ADPCM_SWF, ('S'<<8)+'F' },
    { CODEC_ID_TRUESPEECH, 0x22 },
    { CODEC_ID_FLAC, 0xF1AC },
    { CODEC_ID_IMC, 0x401 },
    { CODEC_ID_GSM_MS, 0x31 },

    /* FIXME: All of the IDs below are not 16 bit and thus illegal. */
    // for NuppelVideo (nuv.c)
    { CODEC_ID_PCM_S16LE, MKTAG('R', 'A', 'W', 'A') },
    { CODEC_ID_MP3, MKTAG('L', 'A', 'M', 'E') },
    { CODEC_ID_MP3, MKTAG('M', 'P', '3', ' ') },
    { 0, 0 },
};

unsigned int codec_get_tag(const AVCodecTag *tags, int id)
{
    while (tags->id != CODEC_ID_NONE) {
        if (tags->id == id)
            return tags->tag;
        tags++;
    }
    return 0;
}

enum CodecID codec_get_id(const AVCodecTag *tags, unsigned int tag)
{
    while (tags->id != CODEC_ID_NONE) {
        if(   toupper((tag >> 0)&0xFF) == toupper((tags->tag >> 0)&0xFF)
           && toupper((tag >> 8)&0xFF) == toupper((tags->tag >> 8)&0xFF)
           && toupper((tag >>16)&0xFF) == toupper((tags->tag >>16)&0xFF)
           && toupper((tag >>24)&0xFF) == toupper((tags->tag >>24)&0xFF))
            return tags->id;
        tags++;
    }
    return CODEC_ID_NONE;
}

unsigned int av_codec_get_tag(const AVCodecTag *tags[4], enum CodecID id)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        int tag= codec_get_tag(tags[i], id);
        if(tag) return tag;
    }
    return 0;
}

enum CodecID av_codec_get_id(const AVCodecTag *tags[4], unsigned int tag)
{
    int i;
    for(i=0; tags && tags[i]; i++){
        enum CodecID id= codec_get_id(tags[i], tag);
        if(id!=CODEC_ID_NONE) return id;
    }
    return CODEC_ID_NONE;
}

unsigned int codec_get_bmp_tag(int id)
{
    return codec_get_tag(codec_bmp_tags, id);
}

unsigned int codec_get_wav_tag(int id)
{
    return codec_get_tag(codec_wav_tags, id);
}

enum CodecID codec_get_bmp_id(unsigned int tag)
{
    return codec_get_id(codec_bmp_tags, tag);
}

enum CodecID codec_get_wav_id(unsigned int tag)
{
    return codec_get_id(codec_wav_tags, tag);
}

#ifdef CONFIG_MUXERS
offset_t start_tag(ByteIOContext *pb, const char *tag)
{
    put_tag(pb, tag);
    put_le32(pb, 0);
    return url_ftell(pb);
}

void end_tag(ByteIOContext *pb, offset_t start)
{
    offset_t pos;

    pos = url_ftell(pb);
    url_fseek(pb, start - 4, SEEK_SET);
    put_le32(pb, (uint32_t)(pos - start));
    url_fseek(pb, pos, SEEK_SET);
}

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int put_wav_header(ByteIOContext *pb, AVCodecContext *enc)
{
    int bps, blkalign, bytespersec;
    int hdrsize = 18;

    if(!enc->codec_tag || enc->codec_tag > 0xffff)
        return -1;

    put_le16(pb, enc->codec_tag);
    put_le16(pb, enc->channels);
    put_le32(pb, enc->sample_rate);
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_ALAW ||
        enc->codec_id == CODEC_ID_PCM_MULAW) {
        bps = 8;
    } else if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3 || enc->codec_id == CODEC_ID_GSM_MS) {
        bps = 0;
    } else if (enc->codec_id == CODEC_ID_ADPCM_IMA_WAV || enc->codec_id == CODEC_ID_ADPCM_MS || enc->codec_id == CODEC_ID_ADPCM_G726 || enc->codec_id == CODEC_ID_ADPCM_YAMAHA) { //
        bps = 4;
    } else if (enc->codec_id == CODEC_ID_PCM_S24LE) {
        bps = 24;
    } else if (enc->codec_id == CODEC_ID_PCM_S32LE) {
        bps = 32;
    } else {
        bps = 16;
    }
    if(bps != enc->bits_per_sample && enc->bits_per_sample){
        av_log(enc, AV_LOG_WARNING, "requested bits_per_sample (%d) and actually stored (%d) differ\n", enc->bits_per_sample, bps);
    }

    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3 || enc->codec_id == CODEC_ID_GSM_MS) {
        blkalign = enc->frame_size; //this is wrong, but seems many demuxers dont work if this is set correctly
        //blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else if (enc->codec_id == CODEC_ID_ADPCM_G726) { //
        blkalign = 1;
    } else if (enc->block_align != 0) { /* specified by the codec */
        blkalign = enc->block_align;
    } else
        blkalign = enc->channels*bps >> 3;
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_S24LE ||
        enc->codec_id == CODEC_ID_PCM_S32LE ||
        enc->codec_id == CODEC_ID_PCM_S16LE) {
        bytespersec = enc->sample_rate * blkalign;
    } else {
        bytespersec = enc->bit_rate / 8;
    }
    put_le32(pb, bytespersec); /* bytes per second */
    put_le16(pb, blkalign); /* block align */
    put_le16(pb, bps); /* bits per sample */
    if (enc->codec_id == CODEC_ID_MP3) {
        put_le16(pb, 12); /* wav_extra_size */
        hdrsize += 12;
        put_le16(pb, 1); /* wID */
        put_le32(pb, 2); /* fdwFlags */
        put_le16(pb, 1152); /* nBlockSize */
        put_le16(pb, 1); /* nFramesPerBlock */
        put_le16(pb, 1393); /* nCodecDelay */
    } else if (enc->codec_id == CODEC_ID_MP2) {
        put_le16(pb, 22); /* wav_extra_size */
        hdrsize += 22;
        put_le16(pb, 2);  /* fwHeadLayer */
        put_le32(pb, enc->bit_rate); /* dwHeadBitrate */
        put_le16(pb, enc->channels == 2 ? 1 : 8); /* fwHeadMode */
        put_le16(pb, 0);  /* fwHeadModeExt */
        put_le16(pb, 1);  /* wHeadEmphasis */
        put_le16(pb, 16); /* fwHeadFlags */
        put_le32(pb, 0);  /* dwPTSLow */
        put_le32(pb, 0);  /* dwPTSHigh */
    } else if (enc->codec_id == CODEC_ID_GSM_MS) {
        put_le16(pb, 2); /* wav_extra_size */
        hdrsize += 2;
        put_le16(pb, enc->frame_size); /* wSamplesPerBlock */
    } else if (enc->codec_id == CODEC_ID_ADPCM_IMA_WAV) {
        put_le16(pb, 2); /* wav_extra_size */
        hdrsize += 2;
        put_le16(pb, enc->frame_size); /* wSamplesPerBlock */
    } else if(enc->extradata_size){
        put_le16(pb, enc->extradata_size);
        put_buffer(pb, enc->extradata, enc->extradata_size);
        hdrsize += enc->extradata_size;
        if(hdrsize&1){
            hdrsize++;
            put_byte(pb, 0);
        }
    } else {
        hdrsize -= 2;
    }

    return hdrsize;
}

/* BITMAPINFOHEADER header */
void put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const AVCodecTag *tags, int for_asf)
{
    put_le32(pb, 40 + enc->extradata_size); /* size */
    put_le32(pb, enc->width);
    put_le32(pb, enc->height);
    put_le16(pb, 1); /* planes */

    put_le16(pb, enc->bits_per_sample ? enc->bits_per_sample : 24); /* depth */
    /* compression type */
    put_le32(pb, enc->codec_tag);
    put_le32(pb, enc->width * enc->height * 3);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);
    put_le32(pb, 0);

    put_buffer(pb, enc->extradata, enc->extradata_size);

    if (enc->extradata_size & 1)
        put_byte(pb, 0);
}
#endif //CONFIG_MUXERS

#ifdef CONFIG_DEMUXERS
/* We could be given one of the three possible structures here:
 * WAVEFORMAT, PCMWAVEFORMAT or WAVEFORMATEX. Each structure
 * is an expansion of the previous one with the fields added
 * at the bottom. PCMWAVEFORMAT adds 'WORD wBitsPerSample' and
 * WAVEFORMATEX adds 'WORD  cbSize' and basically makes itself
 * an openended structure.
 */
void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size)
{
    int id;

    id = get_le16(pb);
    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->codec_tag = id;
    codec->channels = get_le16(pb);
    codec->sample_rate = get_le32(pb);
    codec->bit_rate = get_le32(pb) * 8;
    codec->block_align = get_le16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_sample = 8;
    }else
        codec->bits_per_sample = get_le16(pb);
    if (size >= 18) {  /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = get_le16(pb); /* cbSize */
        size -= 18;
        cbSize = FFMIN(size, cbSize);
        if (cbSize >= 22 && id == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            codec->bits_per_sample = get_le16(pb);
            get_le32(pb); /* dwChannelMask */
            id = get_le32(pb); /* 4 first bytes of GUID */
            url_fskip(pb, 12); /* skip end of GUID */
            cbSize -= 22;
            size -= 22;
        }
        codec->extradata_size = cbSize;
        if (cbSize > 0) {
            codec->extradata = av_mallocz(codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            get_buffer(pb, codec->extradata, codec->extradata_size);
            size -= cbSize;
        }

        /* It is possible for the chunk to contain garbage at the end */
        if (size > 0)
            url_fskip(pb, size);
    }
    codec->codec_id = wav_codec_get_id(id, codec->bits_per_sample);
}


int wav_codec_get_id(unsigned int tag, int bps)
{
    int id;
    id = codec_get_id(codec_wav_tags, tag);
    if (id <= 0)
        return id;
    /* handle specific u8 codec */
    if (id == CODEC_ID_PCM_S16LE && bps == 8)
        id = CODEC_ID_PCM_U8;
    if (id == CODEC_ID_PCM_S16LE && bps == 24)
        id = CODEC_ID_PCM_S24LE;
    if (id == CODEC_ID_PCM_S16LE && bps == 32)
        id = CODEC_ID_PCM_S32LE;
    return id;
}
#endif // CONFIG_DEMUXERS

void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale)
{
    int gcd;

    *au_ssize= stream->block_align;
    if(stream->frame_size && stream->sample_rate){
        *au_scale=stream->frame_size;
        *au_rate= stream->sample_rate;
    }else if(stream->codec_type == CODEC_TYPE_VIDEO){
        *au_scale= stream->time_base.num;
        *au_rate = stream->time_base.den;
    }else{
        *au_scale= stream->block_align ? stream->block_align*8 : 8;
        *au_rate = stream->bit_rate;
    }
    gcd= ff_gcd(*au_scale, *au_rate);
    *au_scale /= gcd;
    *au_rate /= gcd;
}
