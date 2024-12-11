/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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

/**
 * @file
 * utils.
 */

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixfmt.h"
#include "avcodec.h"
#include "codec.h"
#include "codec_desc.h"
#include "codec_internal.h"
#include "codec_par.h"
#include "decode.h"
#include "hwconfig.h"
#include "libavutil/refstruct.h"
#include "thread.h"
#include "threadframe.h"
#include "internal.h"
#include "put_bits.h"
#include "startcode.h"
#include <stdlib.h>
#include <limits.h>

void av_fast_padded_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    av_fast_mallocz(p, size, min_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (*p)
        memset(*p + min_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
}

void av_fast_padded_mallocz(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    av_fast_malloc(p, size, min_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (*p)
        memset(*p, 0, min_size + AV_INPUT_BUFFER_PADDING_SIZE);
}

int av_codec_is_encoder(const AVCodec *avcodec)
{
    const FFCodec *const codec = ffcodec(avcodec);
    return codec && (codec->cb_type == FF_CODEC_CB_TYPE_ENCODE     ||
                     codec->cb_type == FF_CODEC_CB_TYPE_ENCODE_SUB ||
                     codec->cb_type == FF_CODEC_CB_TYPE_RECEIVE_PACKET);
}

int av_codec_is_decoder(const AVCodec *avcodec)
{
    const FFCodec *const codec = ffcodec(avcodec);
    return codec && (codec->cb_type == FF_CODEC_CB_TYPE_DECODE     ||
                     codec->cb_type == FF_CODEC_CB_TYPE_DECODE_SUB ||
                     codec->cb_type == FF_CODEC_CB_TYPE_RECEIVE_FRAME);
}

int ff_set_dimensions(AVCodecContext *s, int width, int height)
{
    int ret = av_image_check_size2(width, height, s->max_pixels, AV_PIX_FMT_NONE, 0, s);

    if (ret < 0)
        width = height = 0;

    s->coded_width  = width;
    s->coded_height = height;
    s->width        = AV_CEIL_RSHIFT(width,  s->lowres);
    s->height       = AV_CEIL_RSHIFT(height, s->lowres);

    return ret;
}

int ff_set_sar(AVCodecContext *avctx, AVRational sar)
{
    int ret = av_image_check_sar(avctx->width, avctx->height, sar);

    if (ret < 0) {
        av_log(avctx, AV_LOG_WARNING, "ignoring invalid SAR: %d/%d\n",
               sar.num, sar.den);
        avctx->sample_aspect_ratio = (AVRational){ 0, 1 };
        return ret;
    } else {
        avctx->sample_aspect_ratio = sar;
    }
    return 0;
}

int ff_side_data_update_matrix_encoding(AVFrame *frame,
                                        enum AVMatrixEncoding matrix_encoding)
{
    AVFrameSideData *side_data;
    enum AVMatrixEncoding *data;

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_MATRIXENCODING);
    if (!side_data)
        side_data = av_frame_new_side_data(frame, AV_FRAME_DATA_MATRIXENCODING,
                                           sizeof(enum AVMatrixEncoding));

    if (!side_data)
        return AVERROR(ENOMEM);

    data  = (enum AVMatrixEncoding*)side_data->data;
    *data = matrix_encoding;

    return 0;
}

void avcodec_align_dimensions2(AVCodecContext *s, int *width, int *height,
                               int linesize_align[AV_NUM_DATA_POINTERS])
{
    int i;
    int w_align = 1;
    int h_align = 1;
    AVPixFmtDescriptor const *desc = av_pix_fmt_desc_get(s->pix_fmt);

    if (desc) {
        w_align = 1 << desc->log2_chroma_w;
        h_align = 1 << desc->log2_chroma_h;
    }

    switch (s->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_YVYU422:
    case AV_PIX_FMT_UYVY422:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_YUV420P9LE:
    case AV_PIX_FMT_YUV420P9BE:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV420P10BE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV420P12BE:
    case AV_PIX_FMT_YUV420P14LE:
    case AV_PIX_FMT_YUV420P14BE:
    case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV420P16BE:
    case AV_PIX_FMT_YUVA420P9LE:
    case AV_PIX_FMT_YUVA420P9BE:
    case AV_PIX_FMT_YUVA420P10LE:
    case AV_PIX_FMT_YUVA420P10BE:
    case AV_PIX_FMT_YUVA420P16LE:
    case AV_PIX_FMT_YUVA420P16BE:
    case AV_PIX_FMT_YUV422P9LE:
    case AV_PIX_FMT_YUV422P9BE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV422P10BE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV422P12BE:
    case AV_PIX_FMT_YUV422P14LE:
    case AV_PIX_FMT_YUV422P14BE:
    case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV422P16BE:
    case AV_PIX_FMT_YUVA422P9LE:
    case AV_PIX_FMT_YUVA422P9BE:
    case AV_PIX_FMT_YUVA422P10LE:
    case AV_PIX_FMT_YUVA422P10BE:
    case AV_PIX_FMT_YUVA422P12LE:
    case AV_PIX_FMT_YUVA422P12BE:
    case AV_PIX_FMT_YUVA422P16LE:
    case AV_PIX_FMT_YUVA422P16BE:
    case AV_PIX_FMT_YUV440P10LE:
    case AV_PIX_FMT_YUV440P10BE:
    case AV_PIX_FMT_YUV440P12LE:
    case AV_PIX_FMT_YUV440P12BE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUV444P9BE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV444P10BE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV444P12BE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV444P14BE:
    case AV_PIX_FMT_YUV444P16LE:
    case AV_PIX_FMT_YUV444P16BE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUVA444P9BE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUVA444P10BE:
    case AV_PIX_FMT_YUVA444P12LE:
    case AV_PIX_FMT_YUVA444P12BE:
    case AV_PIX_FMT_YUVA444P16LE:
    case AV_PIX_FMT_YUVA444P16BE:
    case AV_PIX_FMT_GBRP9LE:
    case AV_PIX_FMT_GBRP9BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP14LE:
    case AV_PIX_FMT_GBRP14BE:
    case AV_PIX_FMT_GBRP16LE:
    case AV_PIX_FMT_GBRP16BE:
    case AV_PIX_FMT_GBRAP12LE:
    case AV_PIX_FMT_GBRAP12BE:
    case AV_PIX_FMT_GBRAP16LE:
    case AV_PIX_FMT_GBRAP16BE:
        w_align = 16; //FIXME assume 16 pixel per macroblock
        h_align = 16 * 2; // interlaced needs 2 macroblocks height
        if (s->codec_id == AV_CODEC_ID_BINKVIDEO)
            w_align = 16*2;
        break;
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUVJ411P:
    case AV_PIX_FMT_UYYVYY411:
        w_align = 32;
        h_align = 16 * 2;
        break;
    case AV_PIX_FMT_YUV410P:
        if (s->codec_id == AV_CODEC_ID_SVQ1) {
            w_align = 64;
            h_align = 64;
        } else if (s->codec_id == AV_CODEC_ID_SNOW) {
            w_align = 16;
            h_align = 16;
        }
        break;
    case AV_PIX_FMT_RGB555:
        if (s->codec_id == AV_CODEC_ID_RPZA) {
            w_align = 4;
            h_align = 4;
        }
        if (s->codec_id == AV_CODEC_ID_INTERPLAY_VIDEO) {
            w_align = 8;
            h_align = 8;
        }
        break;
    case AV_PIX_FMT_PAL8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB8:
        if (s->codec_id == AV_CODEC_ID_SMC ||
            s->codec_id == AV_CODEC_ID_CINEPAK) {
            w_align = 4;
            h_align = 4;
        }
        if (s->codec_id == AV_CODEC_ID_JV ||
            s->codec_id == AV_CODEC_ID_ARGO ||
            s->codec_id == AV_CODEC_ID_INTERPLAY_VIDEO) {
            w_align = 8;
            h_align = 8;
        }
        if (s->codec_id == AV_CODEC_ID_MJPEG   ||
            s->codec_id == AV_CODEC_ID_MJPEGB  ||
            s->codec_id == AV_CODEC_ID_LJPEG   ||
            s->codec_id == AV_CODEC_ID_SMVJPEG ||
            s->codec_id == AV_CODEC_ID_AMV     ||
            s->codec_id == AV_CODEC_ID_SP5X    ||
            s->codec_id == AV_CODEC_ID_JPEGLS) {
            w_align =   8;
            h_align = 2*8;
        }
        break;
    case AV_PIX_FMT_BGR24:
        if ((s->codec_id == AV_CODEC_ID_MSZH) ||
            (s->codec_id == AV_CODEC_ID_ZLIB)) {
            w_align = 4;
            h_align = 4;
        }
        break;
    case AV_PIX_FMT_RGB24:
        if (s->codec_id == AV_CODEC_ID_CINEPAK) {
            w_align = 4;
            h_align = 4;
        }
        break;
    case AV_PIX_FMT_BGR0:
        if (s->codec_id == AV_CODEC_ID_ARGO) {
            w_align = 8;
            h_align = 8;
        }
        break;
    default:
        break;
    }

    if (s->codec_id == AV_CODEC_ID_IFF_ILBM) {
        w_align = FFMAX(w_align, 16);
    }

    *width  = FFALIGN(*width, w_align);
    *height = FFALIGN(*height, h_align);
    if (s->codec_id == AV_CODEC_ID_H264 || s->lowres ||
        s->codec_id == AV_CODEC_ID_VC1  || s->codec_id == AV_CODEC_ID_WMV3 ||
        s->codec_id == AV_CODEC_ID_VP5  || s->codec_id == AV_CODEC_ID_VP6 ||
        s->codec_id == AV_CODEC_ID_VP6F || s->codec_id == AV_CODEC_ID_VP6A
    ) {
        // some of the optimized chroma MC reads one line too much
        // which is also done in mpeg decoders with lowres > 0
        *height += 2;

        // H.264 uses edge emulation for out of frame motion vectors, for this
        // it requires a temporary area large enough to hold a 21x21 block,
        // increasing witdth ensure that the temporary area is large enough,
        // the next rounded up width is 32
        *width = FFMAX(*width, 32);
    }
    if (s->codec_id == AV_CODEC_ID_SVQ3) {
        *width = FFMAX(*width, 32);
    }

    for (i = 0; i < 4; i++)
        linesize_align[i] = STRIDE_ALIGN;
}

void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->pix_fmt);
    int chroma_shift = desc->log2_chroma_w;
    int linesize_align[AV_NUM_DATA_POINTERS];
    int align;

    avcodec_align_dimensions2(s, width, height, linesize_align);
    align               = FFMAX(linesize_align[0], linesize_align[3]);
    linesize_align[1] <<= chroma_shift;
    linesize_align[2] <<= chroma_shift;
    align               = FFMAX3(align, linesize_align[1], linesize_align[2]);
    *width              = FFALIGN(*width, align);
}

int avcodec_fill_audio_frame(AVFrame *frame, int nb_channels,
                             enum AVSampleFormat sample_fmt, const uint8_t *buf,
                             int buf_size, int align)
{
    int ch, planar, needed_size, ret = 0;

    needed_size = av_samples_get_buffer_size(NULL, nb_channels,
                                             frame->nb_samples, sample_fmt,
                                             align);
    if (buf_size < needed_size)
        return AVERROR(EINVAL);

    planar = av_sample_fmt_is_planar(sample_fmt);
    if (planar && nb_channels > AV_NUM_DATA_POINTERS) {
        if (!FF_ALLOCZ_TYPED_ARRAY(frame->extended_data, nb_channels))
            return AVERROR(ENOMEM);
    } else {
        frame->extended_data = frame->data;
    }

    if ((ret = av_samples_fill_arrays(frame->extended_data, &frame->linesize[0],
                                      (uint8_t *)(intptr_t)buf, nb_channels, frame->nb_samples,
                                      sample_fmt, align)) < 0) {
        if (frame->extended_data != frame->data)
            av_freep(&frame->extended_data);
        return ret;
    }
    if (frame->extended_data != frame->data) {
        for (ch = 0; ch < AV_NUM_DATA_POINTERS; ch++)
            frame->data[ch] = frame->extended_data[ch];
    }

    return ret;
}


int avpriv_codec_get_cap_skip_frame_fill_param(const AVCodec *codec){
    return !!(ffcodec(codec)->caps_internal & FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM);
}

const char *avcodec_get_name(enum AVCodecID id)
{
    const AVCodecDescriptor *cd;
    const AVCodec *codec;

    if (id == AV_CODEC_ID_NONE)
        return "none";
    cd = avcodec_descriptor_get(id);
    if (cd)
        return cd->name;
    av_log(NULL, AV_LOG_WARNING, "Codec 0x%x is not in the full list.\n", id);
    codec = avcodec_find_decoder(id);
    if (codec)
        return codec->name;
    codec = avcodec_find_encoder(id);
    if (codec)
        return codec->name;
    return "unknown_codec";
}

const char *av_get_profile_name(const AVCodec *codec, int profile)
{
    const AVProfile *p;
    if (profile == AV_PROFILE_UNKNOWN || !codec->profiles)
        return NULL;

    for (p = codec->profiles; p->profile != AV_PROFILE_UNKNOWN; p++)
        if (p->profile == profile)
            return p->name;

    return NULL;
}

const char *avcodec_profile_name(enum AVCodecID codec_id, int profile)
{
    const AVCodecDescriptor *desc = avcodec_descriptor_get(codec_id);
    const AVProfile *p;

    if (profile == AV_PROFILE_UNKNOWN || !desc || !desc->profiles)
        return NULL;

    for (p = desc->profiles; p->profile != AV_PROFILE_UNKNOWN; p++)
        if (p->profile == profile)
            return p->name;

    return NULL;
}

int av_get_exact_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_DFPWM:
        return 1;
    case AV_CODEC_ID_8SVX_EXP:
    case AV_CODEC_ID_8SVX_FIB:
    case AV_CODEC_ID_ADPCM_ARGO:
    case AV_CODEC_ID_ADPCM_CT:
    case AV_CODEC_ID_ADPCM_IMA_ALP:
    case AV_CODEC_ID_ADPCM_IMA_AMV:
    case AV_CODEC_ID_ADPCM_IMA_APC:
    case AV_CODEC_ID_ADPCM_IMA_APM:
    case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
    case AV_CODEC_ID_ADPCM_IMA_OKI:
    case AV_CODEC_ID_ADPCM_IMA_WS:
    case AV_CODEC_ID_ADPCM_IMA_SSI:
    case AV_CODEC_ID_ADPCM_G722:
    case AV_CODEC_ID_ADPCM_YAMAHA:
    case AV_CODEC_ID_ADPCM_AICA:
        return 4;
    case AV_CODEC_ID_DSD_LSBF:
    case AV_CODEC_ID_DSD_MSBF:
    case AV_CODEC_ID_DSD_LSBF_PLANAR:
    case AV_CODEC_ID_DSD_MSBF_PLANAR:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_VIDC:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S8_PLANAR:
    case AV_CODEC_ID_PCM_SGA:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_SDX2_DPCM:
    case AV_CODEC_ID_CBD2_DPCM:
    case AV_CODEC_ID_DERF_DPCM:
    case AV_CODEC_ID_WADY_DPCM:
        return 8;
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16BE_PLANAR:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16LE_PLANAR:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U16LE:
        return 16;
    case AV_CODEC_ID_PCM_S24DAUD:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24LE_PLANAR:
    case AV_CODEC_ID_PCM_U24BE:
    case AV_CODEC_ID_PCM_U24LE:
        return 24;
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32LE_PLANAR:
    case AV_CODEC_ID_PCM_U32BE:
    case AV_CODEC_ID_PCM_U32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F24LE:
    case AV_CODEC_ID_PCM_F16LE:
        return 32;
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_F64LE:
    case AV_CODEC_ID_PCM_S64BE:
    case AV_CODEC_ID_PCM_S64LE:
        return 64;
    default:
        return 0;
    }
}

enum AVCodecID av_get_pcm_codec(enum AVSampleFormat fmt, int be)
{
    static const enum AVCodecID map[][2] = {
        [AV_SAMPLE_FMT_U8  ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16 ] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32 ] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_FLT ] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBL ] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
        [AV_SAMPLE_FMT_U8P ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16P] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32P] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_S64P] = { AV_CODEC_ID_PCM_S64LE, AV_CODEC_ID_PCM_S64BE },
        [AV_SAMPLE_FMT_FLTP] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBLP] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
    };
    if (fmt < 0 || fmt >= FF_ARRAY_ELEMS(map))
        return AV_CODEC_ID_NONE;
    if (be < 0 || be > 1)
        be = AV_NE(1, 0);
    return map[fmt][be];
}

int av_get_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_DFPWM:
        return 1;
    case AV_CODEC_ID_ADPCM_SBPRO_2:
        return 2;
    case AV_CODEC_ID_ADPCM_SBPRO_3:
        return 3;
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ADPCM_IMA_XBOX:
    case AV_CODEC_ID_ADPCM_IMA_QT:
    case AV_CODEC_ID_ADPCM_SWF:
    case AV_CODEC_ID_ADPCM_MS:
        return 4;
    default:
        return av_get_exact_bits_per_sample(codec_id);
    }
}

static int get_audio_frame_duration(enum AVCodecID id, int sr, int ch, int ba,
                                    uint32_t tag, int bits_per_coded_sample, int64_t bitrate,
                                    uint8_t * extradata, int frame_size, int frame_bytes)
{
    int bps = av_get_exact_bits_per_sample(id);
    int framecount = (ba > 0 && frame_bytes / ba > 0) ? frame_bytes / ba : 1;

    /* codecs with an exact constant bits per sample */
    if (bps > 0 && ch > 0 && frame_bytes > 0 && ch < 32768 && bps < 32768)
        return (frame_bytes * 8LL) / (bps * ch);
    bps = bits_per_coded_sample;

    /* codecs with a fixed packet duration */
    switch (id) {
    case AV_CODEC_ID_ADPCM_ADX:    return   32;
    case AV_CODEC_ID_ADPCM_IMA_QT: return   64;
    case AV_CODEC_ID_ADPCM_EA_XAS: return  128;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_EVRC:
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_QCELP:
    case AV_CODEC_ID_RA_288:       return  160;
    case AV_CODEC_ID_AMR_WB:
    case AV_CODEC_ID_GSM_MS:       return  320;
    case AV_CODEC_ID_MP1:          return  384;
    case AV_CODEC_ID_ATRAC1:       return  512;
    case AV_CODEC_ID_ATRAC9:
    case AV_CODEC_ID_ATRAC3:
        if (framecount > INT_MAX/1024)
            return 0;
        return 1024 * framecount;
    case AV_CODEC_ID_ATRAC3P:      return 2048;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MUSEPACK7:    return 1152;
    case AV_CODEC_ID_AC3:          return 1536;
    case AV_CODEC_ID_FTR:          return 1024;
    }

    if (sr > 0) {
        /* calc from sample rate */
        if (id == AV_CODEC_ID_TTA)
            return 256ll * sr / 245;
        else if (id == AV_CODEC_ID_DST)
            return 588ll * sr / 44100;
        else if (id == AV_CODEC_ID_BINKAUDIO_DCT) {
            if (sr / 22050 > 22)
                return 0;
            return (480 << (sr / 22050));
        }

        if (id == AV_CODEC_ID_MP3)
            return sr <= 24000 ? 576 : 1152;
    }

    if (ba > 0) {
        /* calc from block_align */
        if (id == AV_CODEC_ID_SIPR) {
            switch (ba) {
            case 20: return 160;
            case 19: return 144;
            case 29: return 288;
            case 37: return 480;
            }
        } else if (id == AV_CODEC_ID_ILBC) {
            switch (ba) {
            case 38: return 160;
            case 50: return 240;
            }
        }
    }

    if (frame_bytes > 0) {
        /* calc from frame_bytes only */
        if (id == AV_CODEC_ID_TRUESPEECH)
            return 240 * (frame_bytes / 32);
        if (id == AV_CODEC_ID_NELLYMOSER)
            return 256 * (frame_bytes / 64);
        if (id == AV_CODEC_ID_RA_144)
            return 160 * (frame_bytes / 20);
        if (id == AV_CODEC_ID_APTX)
            return 4 * (frame_bytes / 4);
        if (id == AV_CODEC_ID_APTX_HD)
            return 4 * (frame_bytes / 6);

        if (bps > 0) {
            /* calc from frame_bytes and bits_per_coded_sample */
            if (id == AV_CODEC_ID_ADPCM_G726 || id == AV_CODEC_ID_ADPCM_G726LE)
                return frame_bytes * 8 / bps;
        }

        if (ch > 0 && ch < INT_MAX/16) {
            /* calc from frame_bytes and channels */
            switch (id) {
            case AV_CODEC_ID_FASTAUDIO:
                return frame_bytes / (40 * ch) * 256;
            case AV_CODEC_ID_ADPCM_IMA_MOFLEX:
                return (frame_bytes - 4 * ch) / (128 * ch) * 256;
            case AV_CODEC_ID_ADPCM_AFC:
                return frame_bytes / (9 * ch) * 16;
            case AV_CODEC_ID_ADPCM_PSX:
            case AV_CODEC_ID_ADPCM_DTK:
                frame_bytes /= 16 * ch;
                if (frame_bytes > INT_MAX / 28)
                    return 0;
                return frame_bytes * 28;
            case AV_CODEC_ID_ADPCM_4XM:
            case AV_CODEC_ID_ADPCM_IMA_ACORN:
            case AV_CODEC_ID_ADPCM_IMA_DAT4:
            case AV_CODEC_ID_ADPCM_IMA_ISS:
                return (frame_bytes - 4 * ch) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
                return (frame_bytes - 4) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_AMV:
                return (frame_bytes - 8) * 2;
            case AV_CODEC_ID_ADPCM_THP:
            case AV_CODEC_ID_ADPCM_THP_LE:
                if (extradata)
                    return frame_bytes * 14LL / (8 * ch);
                break;
            case AV_CODEC_ID_ADPCM_XA:
                return (frame_bytes / 128) * 224 / ch;
            case AV_CODEC_ID_INTERPLAY_DPCM:
                return (frame_bytes - 6 - ch) / ch;
            case AV_CODEC_ID_ROQ_DPCM:
                return (frame_bytes - 8) / ch;
            case AV_CODEC_ID_XAN_DPCM:
                return (frame_bytes - 2 * ch) / ch;
            case AV_CODEC_ID_MACE3:
                return 3 * frame_bytes / ch;
            case AV_CODEC_ID_MACE6:
                return 6 * frame_bytes / ch;
            case AV_CODEC_ID_PCM_LXF:
                return 2 * (frame_bytes / (5 * ch));
            case AV_CODEC_ID_IAC:
            case AV_CODEC_ID_IMC:
                return 4 * frame_bytes / ch;
            }

            if (tag) {
                /* calc from frame_bytes, channels, and codec_tag */
                if (id == AV_CODEC_ID_SOL_DPCM) {
                    if (tag == 3)
                        return frame_bytes / ch;
                    else
                        return frame_bytes * 2 / ch;
                }
            }

            if (ba > 0) {
                /* calc from frame_bytes, channels, and block_align */
                int blocks = frame_bytes / ba;
                int64_t tmp = 0;
                switch (id) {
                case AV_CODEC_ID_ADPCM_IMA_XBOX:
                    if (bps != 4)
                        return 0;
                    tmp = blocks * ((ba - 4 * ch) / (bps * ch) * 8);
                    break;
                case AV_CODEC_ID_ADPCM_IMA_WAV:
                    if (bps < 2 || bps > 5)
                        return 0;
                    tmp = blocks * (1LL + (ba - 4 * ch) / (bps * ch) * 8LL);
                    break;
                case AV_CODEC_ID_ADPCM_IMA_DK3:
                    tmp = blocks * (((ba - 16LL) * 2 / 3 * 4) / ch);
                    break;
                case AV_CODEC_ID_ADPCM_IMA_DK4:
                    tmp = blocks * (1 + (ba - 4LL * ch) * 2 / ch);
                    break;
                case AV_CODEC_ID_ADPCM_IMA_RAD:
                    tmp = blocks * ((ba - 4LL * ch) * 2 / ch);
                    break;
                case AV_CODEC_ID_ADPCM_MS:
                    tmp = blocks * (2 + (ba - 7LL * ch) * 2LL / ch);
                    break;
                case AV_CODEC_ID_ADPCM_MTAF:
                    tmp = blocks * (ba - 16LL) * 2 / ch;
                    break;
                case AV_CODEC_ID_ADPCM_XMD:
                    tmp = blocks * 32;
                    break;
                }
                if (tmp) {
                    if (tmp != (int)tmp)
                        return 0;
                    return tmp;
                }
            }

            if (bps > 0) {
                /* calc from frame_bytes, channels, and bits_per_coded_sample */
                switch (id) {
                case AV_CODEC_ID_PCM_DVD:
                    if(bps<4 || frame_bytes<3)
                        return 0;
                    return 2 * ((frame_bytes - 3) / ((bps * 2 / 8) * ch));
                case AV_CODEC_ID_PCM_BLURAY:
                    if(bps<4 || frame_bytes<4)
                        return 0;
                    return (frame_bytes - 4) / ((FFALIGN(ch, 2) * bps) / 8);
                case AV_CODEC_ID_S302M:
                    return 2 * (frame_bytes / ((bps + 4) / 4)) / ch;
                }
            }
        }
    }

    /* Fall back on using frame_size */
    if (frame_size > 1 && frame_bytes)
        return frame_size;

    //For WMA we currently have no other means to calculate duration thus we
    //do it here by assuming CBR, which is true for all known cases.
    if (bitrate > 0 && frame_bytes > 0 && sr > 0 && ba > 1) {
        if (id == AV_CODEC_ID_WMAV1 || id == AV_CODEC_ID_WMAV2)
            return  (frame_bytes * 8LL * sr) / bitrate;
    }

    return 0;
}

int av_get_audio_frame_duration(AVCodecContext *avctx, int frame_bytes)
{
   int channels = avctx->ch_layout.nb_channels;
   int duration;

    duration = get_audio_frame_duration(avctx->codec_id, avctx->sample_rate,
                                    channels, avctx->block_align,
                                    avctx->codec_tag, avctx->bits_per_coded_sample,
                                    avctx->bit_rate, avctx->extradata, avctx->frame_size,
                                    frame_bytes);
    return FFMAX(0, duration);
}

int av_get_audio_frame_duration2(AVCodecParameters *par, int frame_bytes)
{
   int channels = par->ch_layout.nb_channels;
   int duration;

    duration = get_audio_frame_duration(par->codec_id, par->sample_rate,
                                    channels, par->block_align,
                                    par->codec_tag, par->bits_per_coded_sample,
                                    par->bit_rate, par->extradata, par->frame_size,
                                    frame_bytes);
    return FFMAX(0, duration);
}

unsigned int av_xiphlacing(unsigned char *s, unsigned int v)
{
    unsigned int n = 0;

    while (v >= 0xff) {
        *s++ = 0xff;
        v -= 0xff;
        n++;
    }
    *s = v;
    n++;
    return n;
}

int ff_match_2uint16(const uint16_t(*tab)[2], int size, int a, int b)
{
    int i;
    for (i = 0; i < size && !(tab[i][0] == a && tab[i][1] == b); i++) ;
    return i;
}

const AVCodecHWConfig *avcodec_get_hw_config(const AVCodec *avcodec, int index)
{
    const FFCodec *const codec = ffcodec(avcodec);
    int i;
    if (!codec->hw_configs || index < 0)
        return NULL;
    for (i = 0; i <= index; i++)
        if (!codec->hw_configs[i])
            return NULL;
    return &codec->hw_configs[index]->public;
}

int ff_thread_ref_frame(ThreadFrame *dst, const ThreadFrame *src)
{
    int ret;

    dst->owner[0] = src->owner[0];
    dst->owner[1] = src->owner[1];

    ret = av_frame_ref(dst->f, src->f);
    if (ret < 0)
        return ret;

    av_assert0(!dst->progress);

    if (src->progress)
        dst->progress = av_refstruct_ref(src->progress);

    return 0;
}

int ff_thread_replace_frame(ThreadFrame *dst, const ThreadFrame *src)
{
    int ret;

    dst->owner[0] = src->owner[0];
    dst->owner[1] = src->owner[1];

    ret = av_frame_replace(dst->f, src->f);
    if (ret < 0)
        return ret;

    av_refstruct_replace(&dst->progress, src->progress);

    return 0;
}

#if !HAVE_THREADS

int ff_thread_get_buffer(AVCodecContext *avctx, AVFrame *f, int flags)
{
    return ff_get_buffer(avctx, f, flags);
}

int ff_thread_get_ext_buffer(AVCodecContext *avctx, ThreadFrame *f, int flags)
{
    f->owner[0] = f->owner[1] = avctx;
    return ff_get_buffer(avctx, f->f, flags);
}

void ff_thread_release_ext_buffer(ThreadFrame *f)
{
    f->owner[0] = f->owner[1] = NULL;
    if (f->f)
        av_frame_unref(f->f);
}

void ff_thread_finish_setup(AVCodecContext *avctx)
{
}

void ff_thread_report_progress(ThreadFrame *f, int progress, int field)
{
}

void ff_thread_await_progress(const ThreadFrame *f, int progress, int field)
{
}

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    return 1;
}
#endif

const uint8_t *avpriv_find_start_code(const uint8_t *restrict p,
                                      const uint8_t *end,
                                      uint32_t *restrict state)
{
    int i;

    av_assert0(p <= end);
    if (p >= end)
        return end;

    for (i = 0; i < 3; i++) {
        uint32_t tmp = *state << 8;
        *state = tmp + *(p++);
        if (tmp == 0x100 || p == end)
            return p;
    }

    while (p < end) {
        if      (p[-1] > 1      ) p += 3;
        else if (p[-2]          ) p += 2;
        else if (p[-3]|(p[-1]-1)) p++;
        else {
            p++;
            break;
        }
    }

    p = FFMIN(p, end) - 4;
    *state = AV_RB32(p);

    return p + 4;
}

AVCPBProperties *av_cpb_properties_alloc(size_t *size)
{
    AVCPBProperties *props = av_mallocz(sizeof(AVCPBProperties));
    if (!props)
        return NULL;

    if (size)
        *size = sizeof(*props);

    props->vbv_delay = UINT64_MAX;

    return props;
}

static unsigned bcd2uint(uint8_t bcd)
{
    unsigned low  = bcd & 0xf;
    unsigned high = bcd >> 4;
    if (low > 9 || high > 9)
        return 0;
    return low + 10*high;
}

int ff_alloc_timecode_sei(const AVFrame *frame, AVRational rate, size_t prefix_len,
                     void **data, size_t *sei_size)
{
    AVFrameSideData *sd = NULL;
    uint8_t *sei_data;
    PutBitContext pb;
    uint32_t *tc;
    int m;

    if (frame)
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_S12M_TIMECODE);

    if (!sd) {
        *data = NULL;
        return 0;
    }
    tc =  (uint32_t*)sd->data;
    m  = tc[0] & 3;

    *sei_size = sizeof(uint32_t) * 4;
    *data = av_mallocz(*sei_size + prefix_len);
    if (!*data)
        return AVERROR(ENOMEM);
    sei_data = (uint8_t*)*data + prefix_len;

    init_put_bits(&pb, sei_data, *sei_size);
    put_bits(&pb, 2, m); // num_clock_ts

    for (int j = 1; j <= m; j++) {
        uint32_t tcsmpte = tc[j];
        unsigned hh   = bcd2uint(tcsmpte     & 0x3f);    // 6-bit hours
        unsigned mm   = bcd2uint(tcsmpte>>8  & 0x7f);    // 7-bit minutes
        unsigned ss   = bcd2uint(tcsmpte>>16 & 0x7f);    // 7-bit seconds
        unsigned ff   = bcd2uint(tcsmpte>>24 & 0x3f);    // 6-bit frames
        unsigned drop = tcsmpte & 1<<30 && !0;  // 1-bit drop if not arbitrary bit

        /* Calculate frame number of HEVC by SMPTE ST 12-1:2014 Sec 12.2 if rate > 30FPS */
        if (av_cmp_q(rate, (AVRational) {30, 1}) == 1) {
            unsigned pc;
            ff *= 2;
            if (av_cmp_q(rate, (AVRational) {50, 1}) == 0)
                pc = !!(tcsmpte & 1 << 7);
            else
                pc = !!(tcsmpte & 1 << 23);
            ff = (ff + pc) & 0x7f;
        }

        put_bits(&pb, 1, 1); // clock_timestamp_flag
        put_bits(&pb, 1, 1); // units_field_based_flag
        put_bits(&pb, 5, 0); // counting_type
        put_bits(&pb, 1, 1); // full_timestamp_flag
        put_bits(&pb, 1, 0); // discontinuity_flag
        put_bits(&pb, 1, drop);
        put_bits(&pb, 9, ff);
        put_bits(&pb, 6, ss);
        put_bits(&pb, 6, mm);
        put_bits(&pb, 5, hh);
        put_bits(&pb, 5, 0);
    }
    flush_put_bits(&pb);

    return 0;
}

int64_t ff_guess_coded_bitrate(AVCodecContext *avctx)
{
    AVRational framerate = avctx->framerate;
    int bits_per_coded_sample = avctx->bits_per_coded_sample;
    int64_t bitrate;

    if (!(framerate.num && framerate.den))
        framerate = av_inv_q(avctx->time_base);
    if (!(framerate.num && framerate.den))
        return 0;

    if (!bits_per_coded_sample) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(avctx->pix_fmt);
        bits_per_coded_sample = av_get_bits_per_pixel(desc);
    }
    bitrate = (int64_t)bits_per_coded_sample * avctx->width * avctx->height *
              framerate.num / framerate.den;

    return bitrate;
}
