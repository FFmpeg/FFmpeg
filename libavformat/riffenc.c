/*
 * RIFF muxing functions
 * Copyright (c) 2000 Fabrice Bellard
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

#include "libavutil/channel_layout.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "avio_internal.h"
#include "riff.h"

int64_t ff_start_tag(AVIOContext *pb, const char *tag)
{
    ffio_wfourcc(pb, tag);
    avio_wl32(pb, -1);
    return avio_tell(pb);
}

void ff_end_tag(AVIOContext *pb, int64_t start)
{
    int64_t pos;

    av_assert0((start&1) == 0);

    pos = avio_tell(pb);
    if (pos & 1)
        avio_w8(pb, 0);
    avio_seek(pb, start - 4, SEEK_SET);
    avio_wl32(pb, (uint32_t)(pos - start));
    avio_seek(pb, FFALIGN(pos, 2), SEEK_SET);
}

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int ff_put_wav_header(AVFormatContext *s, AVIOContext *pb,
                      AVCodecParameters *par, int flags)
{
    int bps, blkalign, bytespersec, frame_size;
    int hdrsize;
    int64_t hdrstart = avio_tell(pb);
    int waveformatextensible;
    uint8_t temp[256];
    uint8_t *riff_extradata       = temp;
    uint8_t *riff_extradata_start = temp;

    if (!par->codec_tag || par->codec_tag > 0xffff)
        return -1;

    if (par->codec_id == AV_CODEC_ID_ADPCM_SWF && par->block_align == 0) {
        av_log(s, AV_LOG_ERROR, "%s can only be written to WAVE with a constant frame size\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    /* We use the known constant frame size for the codec if known, otherwise
     * fall back on using AVCodecParameters.frame_size, which is not as reliable
     * for indicating packet duration. */
    frame_size = av_get_audio_frame_duration2(par, par->block_align);

    waveformatextensible = (par->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                            av_channel_layout_compare(&par->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO) &&
                            av_channel_layout_compare(&par->ch_layout, &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO)) ||
                           par->sample_rate > 48000 ||
                           par->codec_id == AV_CODEC_ID_EAC3 || par->codec_id == AV_CODEC_ID_DFPWM ||
                           (av_get_bits_per_sample(par->codec_id) > 16 && par->codec_tag != 0x0003);

    if (waveformatextensible)
        avio_wl16(pb, 0xfffe);
    else
        avio_wl16(pb, par->codec_tag);

    avio_wl16(pb, par->ch_layout.nb_channels);
    avio_wl32(pb, par->sample_rate);
    if (par->codec_id == AV_CODEC_ID_ATRAC3 ||
        par->codec_id == AV_CODEC_ID_G723_1 ||
        par->codec_id == AV_CODEC_ID_G728   ||
        par->codec_id == AV_CODEC_ID_MP2    ||
        par->codec_id == AV_CODEC_ID_MP3    ||
        par->codec_id == AV_CODEC_ID_GSM_MS) {
        bps = 0;
    } else {
        if (!(bps = av_get_bits_per_sample(par->codec_id))) {
            if (par->bits_per_coded_sample)
                bps = par->bits_per_coded_sample;
            else
                bps = 16;  // default to 16
        }
    }
    if (bps != par->bits_per_coded_sample && par->bits_per_coded_sample) {
        av_log(s, AV_LOG_WARNING,
               "requested bits_per_coded_sample (%d) "
               "and actually stored (%d) differ\n",
               par->bits_per_coded_sample, bps);
    }

    if (par->codec_id == AV_CODEC_ID_MP2) {
        blkalign = (144 * par->bit_rate - 1)/par->sample_rate + 1;
    } else if (par->codec_id == AV_CODEC_ID_MP3) {
        blkalign = 576 * (par->sample_rate <= (24000 + 32000)/2 ? 1 : 2);
    } else if (par->codec_id == AV_CODEC_ID_AC3) {
        blkalign = 3840;                /* maximum bytes per frame */
    } else if (par->codec_id == AV_CODEC_ID_AAC) {
        blkalign = 768 * par->ch_layout.nb_channels; /* maximum bytes per frame */
    } else if (par->codec_id == AV_CODEC_ID_G723_1) {
        blkalign = 24;
    } else if (par->block_align != 0) { /* specified by the codec */
        blkalign = par->block_align;
    } else
        blkalign = bps * par->ch_layout.nb_channels / av_gcd(8, bps);
    if (par->codec_id == AV_CODEC_ID_PCM_U8 ||
        par->codec_id == AV_CODEC_ID_PCM_S24LE ||
        par->codec_id == AV_CODEC_ID_PCM_S32LE ||
        par->codec_id == AV_CODEC_ID_PCM_F32LE ||
        par->codec_id == AV_CODEC_ID_PCM_F64LE ||
        par->codec_id == AV_CODEC_ID_PCM_S16LE) {
        bytespersec = par->sample_rate * blkalign;
    } else if (par->codec_id == AV_CODEC_ID_G723_1) {
        bytespersec = 800;
    } else {
        bytespersec = par->bit_rate / 8;
    }
    avio_wl32(pb, bytespersec); /* bytes per second */
    avio_wl16(pb, blkalign);    /* block align */
    avio_wl16(pb, bps);         /* bits per sample */
    if (par->codec_id == AV_CODEC_ID_MP3) {
        bytestream_put_le16(&riff_extradata, 1);    /* wID */
        bytestream_put_le32(&riff_extradata, 2);    /* fdwFlags */
        bytestream_put_le16(&riff_extradata, 1152); /* nBlockSize */
        bytestream_put_le16(&riff_extradata, 1);    /* nFramesPerBlock */
        bytestream_put_le16(&riff_extradata, 1393); /* nCodecDelay */
    } else if (par->codec_id == AV_CODEC_ID_MP2) {
        /* fwHeadLayer */
        bytestream_put_le16(&riff_extradata, 2);
        /* dwHeadBitrate */
        bytestream_put_le32(&riff_extradata, par->bit_rate);
        /* fwHeadMode */
        bytestream_put_le16(&riff_extradata, par->ch_layout.nb_channels == 2 ? 1 : 8);
        /* fwHeadModeExt */
        bytestream_put_le16(&riff_extradata, 0);
        /* wHeadEmphasis */
        bytestream_put_le16(&riff_extradata, 1);
        /* fwHeadFlags */
        bytestream_put_le16(&riff_extradata, 16);
        /* dwPTSLow */
        bytestream_put_le32(&riff_extradata, 0);
        /* dwPTSHigh */
        bytestream_put_le32(&riff_extradata, 0);
    } else if (par->codec_id == AV_CODEC_ID_G723_1) {
        bytestream_put_le32(&riff_extradata, 0x9ace0002); /* extradata needed for msacm g723.1 codec */
        bytestream_put_le32(&riff_extradata, 0xaea2f732);
        bytestream_put_le16(&riff_extradata, 0xacde);
    } else if (par->codec_id == AV_CODEC_ID_GSM_MS ||
               par->codec_id == AV_CODEC_ID_ADPCM_IMA_WAV) {
        /* wSamplesPerBlock */
        bytestream_put_le16(&riff_extradata, frame_size);
    } else if (par->extradata_size) {
        riff_extradata_start = par->extradata;
        riff_extradata       = par->extradata + par->extradata_size;
    }
    /* write WAVEFORMATEXTENSIBLE extensions */
    if (waveformatextensible) {
        int write_channel_mask = !(flags & FF_PUT_WAV_HEADER_SKIP_CHANNELMASK) &&
                                 (s->strict_std_compliance < FF_COMPLIANCE_NORMAL ||
                                  par->ch_layout.u.mask < 0x40000);
        /* 22 is WAVEFORMATEXTENSIBLE size */
        avio_wl16(pb, riff_extradata - riff_extradata_start + 22);
        /* ValidBitsPerSample || SamplesPerBlock || Reserved */
        avio_wl16(pb, bps);
        /* dwChannelMask */
        avio_wl32(pb, write_channel_mask ? par->ch_layout.u.mask : 0);
        /* GUID + next 3 */
        if (par->codec_id == AV_CODEC_ID_EAC3 || par->codec_id == AV_CODEC_ID_DFPWM) {
            ff_put_guid(pb, ff_get_codec_guid(par->codec_id, ff_codec_wav_guids));
        } else {
            avio_wl32(pb, par->codec_tag);
            avio_wl32(pb, 0x00100000);
            avio_wl32(pb, 0xAA000080);
            avio_wl32(pb, 0x719B3800);
        }
    } else if ((flags & FF_PUT_WAV_HEADER_FORCE_WAVEFORMATEX) ||
               par->codec_tag != 0x0001 /* PCM */ ||
               riff_extradata - riff_extradata_start) {
        /* WAVEFORMATEX */
        avio_wl16(pb, riff_extradata - riff_extradata_start); /* cbSize */
    } /* else PCMWAVEFORMAT */
    avio_write(pb, riff_extradata_start, riff_extradata - riff_extradata_start);
    hdrsize = avio_tell(pb) - hdrstart;
    if (hdrsize & 1) {
        hdrsize++;
        avio_w8(pb, 0);
    }

    return hdrsize;
}

/* BITMAPINFOHEADER header */
void ff_put_bmp_header(AVIOContext *pb, AVCodecParameters *par,
                       int for_asf, int ignore_extradata, int rgb_frame_is_flipped)
{
    int flipped_extradata = (par->extradata_size >= 9 &&
                            !memcmp(par->extradata + par->extradata_size - 9, "BottomUp", 9));
    int keep_height = flipped_extradata || rgb_frame_is_flipped;
    int extradata_size = par->extradata_size - 9*flipped_extradata;
    enum AVPixelFormat pix_fmt = par->format;
    int pal_avi;

    if (pix_fmt == AV_PIX_FMT_NONE && par->bits_per_coded_sample == 1)
        pix_fmt = AV_PIX_FMT_MONOWHITE;
    pal_avi = !for_asf &&
              (pix_fmt == AV_PIX_FMT_PAL8 ||
               pix_fmt == AV_PIX_FMT_MONOWHITE ||
               pix_fmt == AV_PIX_FMT_MONOBLACK);

    /* Size (not including the size of the color table or color masks) */
    avio_wl32(pb, 40 + (ignore_extradata || pal_avi ? 0 : extradata_size));
    avio_wl32(pb, par->width);
    //We always store RGB TopDown
    avio_wl32(pb, par->codec_tag || keep_height ? par->height : -par->height);
    /* planes */
    avio_wl16(pb, 1);
    /* depth */
    avio_wl16(pb, par->bits_per_coded_sample ? par->bits_per_coded_sample : 24);
    /* compression type */
    // MSRLE compatibility with Media Player 3.1 and Windows 95
    avio_wl32(pb, par->codec_id == AV_CODEC_ID_MSRLE ? 1 : par->codec_tag);
    avio_wl32(pb, (par->width * par->height * (par->bits_per_coded_sample ? par->bits_per_coded_sample : 24)+7) / 8);
    avio_wl32(pb, 0);
    avio_wl32(pb, 0);
    /* Number of color indices in the color table that are used.
     * A value of 0 means 2^biBitCount indices, but this doesn't work
     * with Windows Media Player and files containing xxpc chunks. */
    // MSRLE on Windows 95 requires a zero here
    avio_wl32(pb, pal_avi && par->codec_id != AV_CODEC_ID_MSRLE ? 1 << par->bits_per_coded_sample : 0);
    avio_wl32(pb, 0);

    if (!ignore_extradata) {
        if (par->extradata_size) {
            avio_write(pb, par->extradata, extradata_size);
            if (!for_asf && extradata_size & 1)
                avio_w8(pb, 0);
        } else if (pal_avi) {
            int i;
            for (i = 0; i < 1 << par->bits_per_coded_sample; i++) {
                /* Initialize 1 bpp palette to black & white */
                if (i == 0 && pix_fmt == AV_PIX_FMT_MONOWHITE)
                    avio_wl32(pb, 0xffffff);
                else if (i == 1 && pix_fmt == AV_PIX_FMT_MONOBLACK)
                    avio_wl32(pb, 0xffffff);
                else
                    avio_wl32(pb, 0);
            }
        }
    }
}

void ff_parse_specific_params(AVStream *st, int *au_rate,
                              int *au_ssize, int *au_scale)
{
    AVCodecParameters *par = st->codecpar;
    int gcd;
    int audio_frame_size;

    audio_frame_size = av_get_audio_frame_duration2(par, 0);
    if (!audio_frame_size)
        audio_frame_size = par->frame_size;

    *au_ssize = par->block_align;
    if (audio_frame_size && par->sample_rate) {
        *au_scale = audio_frame_size;
        *au_rate  = par->sample_rate;
    } else if (par->codec_type == AVMEDIA_TYPE_VIDEO ||
               par->codec_type == AVMEDIA_TYPE_DATA ||
               par->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        *au_scale = st->time_base.num;
        *au_rate  = st->time_base.den;
    } else {
        *au_scale = par->block_align ? par->block_align * 8 : 8;
        *au_rate  = par->bit_rate ? par->bit_rate :
                    8 * par->sample_rate;
    }
    gcd        = av_gcd(*au_scale, *au_rate);
    *au_scale /= gcd;
    *au_rate  /= gcd;
}

void ff_riff_write_info_tag(AVIOContext *pb, const char *tag, const char *str)
{
    size_t len = strlen(str);
    if (len > 0 && len < UINT32_MAX) {
        len++;
        ffio_wfourcc(pb, tag);
        avio_wl32(pb, len);
        avio_put_str(pb, str);
        if (len & 1)
            avio_w8(pb, 0);
    }
}

static const char riff_tags[][5] = {
    "IARL", "IART", "IAS1", "IAS2", "IAS3", "IAS4", "IAS5", "IAS6", "IAS7",
    "IAS8", "IAS9", "ICMS", "ICMT", "ICOP", "ICRD", "ICRP", "IDIM", "IDPI",
    "IENG", "IGNR", "IKEY", "ILGT", "ILNG", "IMED", "INAM", "IPLT", "IPRD",
    "IPRT", "ITRK", "ISBJ", "ISFT", "ISHP", "ISMP", "ISRC", "ISRF", "ITCH",
    { 0 }
};

static int riff_has_valid_tags(AVFormatContext *s)
{
    int i;

    for (i = 0; *riff_tags[i]; i++)
        if (av_dict_get(s->metadata, riff_tags[i], NULL, AV_DICT_MATCH_CASE))
            return 1;

    return 0;
}

void ff_riff_write_info(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    int i;
    int64_t list_pos;
    AVDictionaryEntry *t = NULL;

    ff_metadata_conv(&s->metadata, ff_riff_info_conv, NULL);

    /* writing empty LIST is not nice and may cause problems */
    if (!riff_has_valid_tags(s))
        return;

    list_pos = ff_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "INFO");
    for (i = 0; *riff_tags[i]; i++)
        if ((t = av_dict_get(s->metadata, riff_tags[i],
                             NULL, AV_DICT_MATCH_CASE)))
            ff_riff_write_info_tag(s->pb, t->key, t->value);
    ff_end_tag(pb, list_pos);
}

void ff_put_guid(AVIOContext *s, const ff_asf_guid *g)
{
    av_assert0(sizeof(*g) == 16);
    avio_write(s, *g, sizeof(*g));
}

const ff_asf_guid *ff_get_codec_guid(enum AVCodecID id, const AVCodecGuid *av_guid)
{
    int i;
    for (i = 0; av_guid[i].id != AV_CODEC_ID_NONE; i++) {
        if (id == av_guid[i].id)
            return &(av_guid[i].guid);
    }
    return NULL;
}
