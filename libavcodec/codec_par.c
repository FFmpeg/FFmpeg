/*
 * AVCodecParameters functions for libavcodec
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
 * AVCodecParameters functions for libavcodec.
 */

#include <string.h>
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_par.h"
#include "packet.h"

static void codec_parameters_reset(AVCodecParameters *par)
{
    av_freep(&par->extradata);
    av_channel_layout_uninit(&par->ch_layout);
    av_packet_side_data_free(&par->coded_side_data, &par->nb_coded_side_data);

    memset(par, 0, sizeof(*par));

    par->codec_type          = AVMEDIA_TYPE_UNKNOWN;
    par->codec_id            = AV_CODEC_ID_NONE;
    par->format              = -1;
    par->ch_layout.order     = AV_CHANNEL_ORDER_UNSPEC;
    par->field_order         = AV_FIELD_UNKNOWN;
    par->color_range         = AVCOL_RANGE_UNSPECIFIED;
    par->color_primaries     = AVCOL_PRI_UNSPECIFIED;
    par->color_trc           = AVCOL_TRC_UNSPECIFIED;
    par->color_space         = AVCOL_SPC_UNSPECIFIED;
    par->chroma_location     = AVCHROMA_LOC_UNSPECIFIED;
    par->sample_aspect_ratio = (AVRational){ 0, 1 };
    par->framerate           = (AVRational){ 0, 1 };
    par->profile             = AV_PROFILE_UNKNOWN;
    par->level               = AV_LEVEL_UNKNOWN;
}

AVCodecParameters *avcodec_parameters_alloc(void)
{
    AVCodecParameters *par = av_mallocz(sizeof(*par));

    if (!par)
        return NULL;
    codec_parameters_reset(par);
    return par;
}

void avcodec_parameters_free(AVCodecParameters **ppar)
{
    AVCodecParameters *par = *ppar;

    if (!par)
        return;
    codec_parameters_reset(par);

    av_freep(ppar);
}

static int codec_parameters_copy_side_data(AVPacketSideData **pdst, int *pnb_dst,
                                           const AVPacketSideData *src, int nb_src)
{
    AVPacketSideData *dst;
    int nb_dst = *pnb_dst;

    if (!src)
        return 0;

    *pdst = dst = av_calloc(nb_src, sizeof(*dst));
    if (!dst)
        return AVERROR(ENOMEM);

    for (int i = 0; i < nb_src; i++) {
        const AVPacketSideData *src_sd = &src[i];
        AVPacketSideData *dst_sd = &dst[i];

        dst_sd->data = av_memdup(src_sd->data, src_sd->size);
        if (!dst_sd->data)
            return AVERROR(ENOMEM);

        dst_sd->type = src_sd->type;
        dst_sd->size = src_sd->size;
        *pnb_dst = ++nb_dst;
    }

    return 0;
}

int avcodec_parameters_copy(AVCodecParameters *dst, const AVCodecParameters *src)
{
    int ret;

    codec_parameters_reset(dst);
    memcpy(dst, src, sizeof(*dst));

    dst->ch_layout      = (AVChannelLayout){0};
    dst->extradata      = NULL;
    dst->extradata_size = 0;
    dst->coded_side_data      = NULL;
    dst->nb_coded_side_data   = 0;
    if (src->extradata) {
        dst->extradata = av_mallocz(src->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!dst->extradata)
            return AVERROR(ENOMEM);
        memcpy(dst->extradata, src->extradata, src->extradata_size);
        dst->extradata_size = src->extradata_size;
    }
    ret = codec_parameters_copy_side_data(&dst->coded_side_data, &dst->nb_coded_side_data,
                                           src->coded_side_data,  src->nb_coded_side_data);
    if (ret < 0)
        return ret;

    ret = av_channel_layout_copy(&dst->ch_layout, &src->ch_layout);
    if (ret < 0)
        return ret;

    return 0;
}

int avcodec_parameters_from_context(AVCodecParameters *par,
                                    const AVCodecContext *codec)
{
    int ret;

    codec_parameters_reset(par);

    par->codec_type = codec->codec_type;
    par->codec_id   = codec->codec_id;
    par->codec_tag  = codec->codec_tag;

    par->bit_rate              = codec->bit_rate;
    par->bits_per_coded_sample = codec->bits_per_coded_sample;
    par->bits_per_raw_sample   = codec->bits_per_raw_sample;
    par->profile               = codec->profile;
    par->level                 = codec->level;

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        par->format              = codec->pix_fmt;
        par->width               = codec->width;
        par->height              = codec->height;
        par->field_order         = codec->field_order;
        par->color_range         = codec->color_range;
        par->color_primaries     = codec->color_primaries;
        par->color_trc           = codec->color_trc;
        par->color_space         = codec->colorspace;
        par->chroma_location     = codec->chroma_sample_location;
        par->sample_aspect_ratio = codec->sample_aspect_ratio;
        par->video_delay         = codec->has_b_frames;
        par->framerate           = codec->framerate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        par->format           = codec->sample_fmt;
        ret = av_channel_layout_copy(&par->ch_layout, &codec->ch_layout);
        if (ret < 0)
            return ret;
        par->sample_rate      = codec->sample_rate;
        par->block_align      = codec->block_align;
        par->frame_size       = codec->frame_size;
        par->initial_padding  = codec->initial_padding;
        par->trailing_padding = codec->trailing_padding;
        par->seek_preroll     = codec->seek_preroll;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        par->width  = codec->width;
        par->height = codec->height;
        break;
    }

    if (codec->extradata) {
        par->extradata = av_mallocz(codec->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!par->extradata)
            return AVERROR(ENOMEM);
        memcpy(par->extradata, codec->extradata, codec->extradata_size);
        par->extradata_size = codec->extradata_size;
    }

    ret = codec_parameters_copy_side_data(&par->coded_side_data, &par->nb_coded_side_data,
                                          codec->coded_side_data, codec->nb_coded_side_data);
    if (ret < 0)
        return ret;

    return 0;
}

int avcodec_parameters_to_context(AVCodecContext *codec,
                                  const AVCodecParameters *par)
{
    int ret;

    codec->codec_type = par->codec_type;
    codec->codec_id   = par->codec_id;
    codec->codec_tag  = par->codec_tag;

    codec->bit_rate              = par->bit_rate;
    codec->bits_per_coded_sample = par->bits_per_coded_sample;
    codec->bits_per_raw_sample   = par->bits_per_raw_sample;
    codec->profile               = par->profile;
    codec->level                 = par->level;

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        codec->pix_fmt                = par->format;
        codec->width                  = par->width;
        codec->height                 = par->height;
        codec->field_order            = par->field_order;
        codec->color_range            = par->color_range;
        codec->color_primaries        = par->color_primaries;
        codec->color_trc              = par->color_trc;
        codec->colorspace             = par->color_space;
        codec->chroma_sample_location = par->chroma_location;
        codec->sample_aspect_ratio    = par->sample_aspect_ratio;
        codec->has_b_frames           = par->video_delay;
        codec->framerate              = par->framerate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        codec->sample_fmt       = par->format;
        ret = av_channel_layout_copy(&codec->ch_layout, &par->ch_layout);
        if (ret < 0)
            return ret;
        codec->sample_rate      = par->sample_rate;
        codec->block_align      = par->block_align;
        codec->frame_size       = par->frame_size;
        codec->delay            =
        codec->initial_padding  = par->initial_padding;
        codec->trailing_padding = par->trailing_padding;
        codec->seek_preroll     = par->seek_preroll;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        codec->width  = par->width;
        codec->height = par->height;
        break;
    }

    av_freep(&codec->extradata);
    if (par->extradata) {
        codec->extradata = av_mallocz(par->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!codec->extradata)
            return AVERROR(ENOMEM);
        memcpy(codec->extradata, par->extradata, par->extradata_size);
        codec->extradata_size = par->extradata_size;
    }

    av_packet_side_data_free(&codec->coded_side_data, &codec->nb_coded_side_data);
    ret = codec_parameters_copy_side_data(&codec->coded_side_data, &codec->nb_coded_side_data,
                                          par->coded_side_data, par->nb_coded_side_data);
    if (ret < 0)
        return ret;

    return 0;
}
