/*
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

#include "avassert.h"
#include "buffer.h"
#include "common.h"
#include "dict.h"
#include "frame.h"
#include "mem.h"
#include "side_data.h"

static const AVSideDataDescriptor sd_props[] = {
    [AV_FRAME_DATA_PANSCAN]                     = { "AVPanScan",                                    AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_A53_CC]                      = { "ATSC A53 Part 4 Closed Captions" },
    [AV_FRAME_DATA_MATRIXENCODING]              = { "AVMatrixEncoding",                             AV_SIDE_DATA_PROP_CHANNEL_DEPENDENT },
    [AV_FRAME_DATA_DOWNMIX_INFO]                = { "Metadata relevant to a downmix procedure",     AV_SIDE_DATA_PROP_CHANNEL_DEPENDENT },
    [AV_FRAME_DATA_AFD]                         = { "Active format description" },
    [AV_FRAME_DATA_MOTION_VECTORS]              = { "Motion vectors",                               AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_SKIP_SAMPLES]                = { "Skip samples" },
    [AV_FRAME_DATA_GOP_TIMECODE]                = { "GOP timecode" },
    [AV_FRAME_DATA_S12M_TIMECODE]               = { "SMPTE 12-1 timecode" },
    [AV_FRAME_DATA_DYNAMIC_HDR_PLUS]            = { "HDR Dynamic Metadata SMPTE2094-40 (HDR10+)",   AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_DYNAMIC_HDR_VIVID]           = { "HDR Dynamic Metadata CUVA 005.1 2021 (Vivid)", AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_REGIONS_OF_INTEREST]         = { "Regions Of Interest",                          AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_VIDEO_ENC_PARAMS]            = { "Video encoding parameters" },
    [AV_FRAME_DATA_FILM_GRAIN_PARAMS]           = { "Film grain parameters" },
    [AV_FRAME_DATA_DETECTION_BBOXES]            = { "Bounding boxes for object detection and classification", AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_DOVI_RPU_BUFFER]             = { "Dolby Vision RPU Data",                        AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_DOVI_METADATA]               = { "Dolby Vision Metadata",                        AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_LCEVC]                       = { "LCEVC NAL data",                               AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_VIEW_ID]                     = { "View ID" },
    [AV_FRAME_DATA_STEREO3D]                    = { "Stereo 3D",                                    AV_SIDE_DATA_PROP_GLOBAL },
    [AV_FRAME_DATA_REPLAYGAIN]                  = { "AVReplayGain",                                 AV_SIDE_DATA_PROP_GLOBAL },
    [AV_FRAME_DATA_DISPLAYMATRIX]               = { "3x3 displaymatrix",                            AV_SIDE_DATA_PROP_GLOBAL },
    [AV_FRAME_DATA_AUDIO_SERVICE_TYPE]          = { "Audio service type",                           AV_SIDE_DATA_PROP_GLOBAL },
    [AV_FRAME_DATA_MASTERING_DISPLAY_METADATA]  = { "Mastering display metadata",                   AV_SIDE_DATA_PROP_GLOBAL | AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_CONTENT_LIGHT_LEVEL]         = { "Content light level metadata",                 AV_SIDE_DATA_PROP_GLOBAL | AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_AMBIENT_VIEWING_ENVIRONMENT] = { "Ambient viewing environment",                  AV_SIDE_DATA_PROP_GLOBAL },
    [AV_FRAME_DATA_SPHERICAL]                   = { "Spherical Mapping",                            AV_SIDE_DATA_PROP_GLOBAL | AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
    [AV_FRAME_DATA_ICC_PROFILE]                 = { "ICC profile",                                  AV_SIDE_DATA_PROP_GLOBAL | AV_SIDE_DATA_PROP_COLOR_DEPENDENT },
    [AV_FRAME_DATA_SEI_UNREGISTERED]            = { "H.26[45] User Data Unregistered SEI message",  AV_SIDE_DATA_PROP_MULTI },
    [AV_FRAME_DATA_VIDEO_HINT]                  = { "Encoding video hint",                          AV_SIDE_DATA_PROP_SIZE_DEPENDENT },
};

const AVSideDataDescriptor *av_frame_side_data_desc(enum AVFrameSideDataType type)
{
    unsigned t = type;
    if (t < FF_ARRAY_ELEMS(sd_props) && sd_props[t].name)
        return &sd_props[t];
    return NULL;
}

const char *av_frame_side_data_name(enum AVFrameSideDataType type)
{
    const AVSideDataDescriptor *desc = av_frame_side_data_desc(type);
    return desc ? desc->name : NULL;
}

static void free_side_data_entry(AVFrameSideData **ptr_sd)
{
    AVFrameSideData *sd = *ptr_sd;

    av_buffer_unref(&sd->buf);
    av_dict_free(&sd->metadata);
    av_freep(ptr_sd);
}

static void remove_side_data_by_entry(AVFrameSideData ***sd, int *nb_sd,
                                      const AVFrameSideData *target)
{
    for (int i = *nb_sd - 1; i >= 0; i--) {
        AVFrameSideData *entry = ((*sd)[i]);
        if (entry != target)
            continue;

        free_side_data_entry(&entry);

        ((*sd)[i]) = ((*sd)[*nb_sd - 1]);
        (*nb_sd)--;

        return;
    }
}

void av_frame_side_data_remove(AVFrameSideData ***sd, int *nb_sd,
                               enum AVFrameSideDataType type)
{
    for (int i = *nb_sd - 1; i >= 0; i--) {
        AVFrameSideData *entry = ((*sd)[i]);
        if (entry->type != type)
            continue;

        free_side_data_entry(&entry);

        ((*sd)[i]) = ((*sd)[*nb_sd - 1]);
        (*nb_sd)--;
    }
}

void av_frame_side_data_remove_by_props(AVFrameSideData ***sd, int *nb_sd,
                                        int props)
{
    for (int i = *nb_sd - 1; i >= 0; i--) {
        AVFrameSideData *entry = ((*sd)[i]);
        const AVSideDataDescriptor *desc = av_frame_side_data_desc(entry->type);
        if (!desc || !(desc->props & props))
            continue;

        free_side_data_entry(&entry);

        ((*sd)[i]) = ((*sd)[*nb_sd - 1]);
        (*nb_sd)--;
    }
}

void av_frame_side_data_free(AVFrameSideData ***sd, int *nb_sd)
{
    for (int i = 0; i < *nb_sd; i++)
        free_side_data_entry(&((*sd)[i]));
    *nb_sd = 0;

    av_freep(sd);
}

static AVFrameSideData *add_side_data_from_buf_ext(AVFrameSideData ***sd,
                                                   int *nb_sd,
                                                   enum AVFrameSideDataType type,
                                                   AVBufferRef *buf, uint8_t *data,
                                                   size_t size)
{
    AVFrameSideData *ret, **tmp;

    // *nb_sd + 1 needs to fit into an int and a size_t.
    if ((unsigned)*nb_sd >= FFMIN(INT_MAX, SIZE_MAX))
        return NULL;

    tmp = av_realloc_array(*sd, sizeof(**sd), *nb_sd + 1);
    if (!tmp)
        return NULL;
    *sd = tmp;

    ret = av_mallocz(sizeof(*ret));
    if (!ret)
        return NULL;

    ret->buf = buf;
    ret->data = data;
    ret->size = size;
    ret->type = type;

    (*sd)[(*nb_sd)++] = ret;

    return ret;
}

AVFrameSideData *ff_frame_side_data_add_from_buf(AVFrameSideData ***sd,
                                                 int *nb_sd,
                                                 enum AVFrameSideDataType type,
                                                 AVBufferRef *buf)
{
    if (!buf)
        return NULL;

    return add_side_data_from_buf_ext(sd, nb_sd, type, buf, buf->data, buf->size);
}

static AVFrameSideData *replace_side_data_from_buf(AVFrameSideData *dst,
                                                   AVBufferRef *buf, int flags)
{
    if (!(flags & AV_FRAME_SIDE_DATA_FLAG_REPLACE))
        return NULL;

    av_dict_free(&dst->metadata);
    av_buffer_unref(&dst->buf);
    dst->buf  = buf;
    dst->data = buf->data;
    dst->size = buf->size;
    return dst;
}

AVFrameSideData *av_frame_side_data_new(AVFrameSideData ***sd, int *nb_sd,
                                        enum AVFrameSideDataType type,
                                        size_t size, unsigned int flags)
{
    const AVSideDataDescriptor *desc = av_frame_side_data_desc(type);
    AVBufferRef     *buf = av_buffer_alloc(size);
    AVFrameSideData *ret = NULL;

    if (flags & AV_FRAME_SIDE_DATA_FLAG_UNIQUE)
        av_frame_side_data_remove(sd, nb_sd, type);
    if ((!desc || !(desc->props & AV_SIDE_DATA_PROP_MULTI)) &&
        (ret = (AVFrameSideData *)av_frame_side_data_get(*sd, *nb_sd, type))) {
        ret = replace_side_data_from_buf(ret, buf, flags);
        if (!ret)
            av_buffer_unref(&buf);
        return ret;
    }

    ret = ff_frame_side_data_add_from_buf(sd, nb_sd, type, buf);
    if (!ret)
        av_buffer_unref(&buf);

    return ret;
}

AVFrameSideData *av_frame_side_data_add(AVFrameSideData ***sd, int *nb_sd,
                                        enum AVFrameSideDataType type,
                                        AVBufferRef **pbuf, unsigned int flags)
{
    const AVSideDataDescriptor *desc = av_frame_side_data_desc(type);
    AVFrameSideData *sd_dst  = NULL;
    AVBufferRef *buf = *pbuf;

    if ((flags & AV_FRAME_SIDE_DATA_FLAG_NEW_REF) && !(buf = av_buffer_ref(*pbuf)))
        return NULL;
    if (flags & AV_FRAME_SIDE_DATA_FLAG_UNIQUE)
        av_frame_side_data_remove(sd, nb_sd, type);
    if ((!desc || !(desc->props & AV_SIDE_DATA_PROP_MULTI)) &&
        (sd_dst = (AVFrameSideData *)av_frame_side_data_get(*sd, *nb_sd, type))) {
        sd_dst = replace_side_data_from_buf(sd_dst, buf, flags);
    } else
        sd_dst = ff_frame_side_data_add_from_buf(sd, nb_sd, type, buf);

    if (sd_dst && !(flags & AV_FRAME_SIDE_DATA_FLAG_NEW_REF))
        *pbuf = NULL;
    else if (!sd_dst && (flags & AV_FRAME_SIDE_DATA_FLAG_NEW_REF))
        av_buffer_unref(&buf);
    return sd_dst;
}

int av_frame_side_data_clone(AVFrameSideData ***sd, int *nb_sd,
                             const AVFrameSideData *src, unsigned int flags)
{
    const AVSideDataDescriptor *desc;
    AVBufferRef     *buf    = NULL;
    AVFrameSideData *sd_dst = NULL;
    int              ret    = AVERROR_BUG;

    if (!sd || !src || !nb_sd || (*nb_sd && !*sd))
        return AVERROR(EINVAL);

    desc = av_frame_side_data_desc(src->type);
    if (flags & AV_FRAME_SIDE_DATA_FLAG_UNIQUE)
        av_frame_side_data_remove(sd, nb_sd, src->type);
    if ((!desc || !(desc->props & AV_SIDE_DATA_PROP_MULTI)) &&
        (sd_dst = (AVFrameSideData *)av_frame_side_data_get(*sd, *nb_sd, src->type))) {
        AVDictionary *dict = NULL;

        if (!(flags & AV_FRAME_SIDE_DATA_FLAG_REPLACE))
            return AVERROR(EEXIST);

        ret = av_dict_copy(&dict, src->metadata, 0);
        if (ret < 0)
            return ret;

        ret = av_buffer_replace(&sd_dst->buf, src->buf);
        if (ret < 0) {
            av_dict_free(&dict);
            return ret;
        }

        av_dict_free(&sd_dst->metadata);
        sd_dst->metadata = dict;
        sd_dst->data     = src->data;
        sd_dst->size     = src->size;
        return 0;
    }

    buf = av_buffer_ref(src->buf);
    if (!buf)
        return AVERROR(ENOMEM);

    sd_dst = add_side_data_from_buf_ext(sd, nb_sd, src->type, buf,
                                        src->data, src->size);
    if (!sd_dst) {
        av_buffer_unref(&buf);
        return AVERROR(ENOMEM);
    }

    ret = av_dict_copy(&sd_dst->metadata, src->metadata, 0);
    if (ret < 0) {
        remove_side_data_by_entry(sd, nb_sd, sd_dst);
        return ret;
    }

    return 0;
}

const AVFrameSideData *av_frame_side_data_get_c(const AVFrameSideData * const *sd,
                                                const int nb_sd,
                                                enum AVFrameSideDataType type)
{
    for (int i = 0; i < nb_sd; i++) {
        if (sd[i]->type == type)
            return sd[i];
    }
    return NULL;
}
