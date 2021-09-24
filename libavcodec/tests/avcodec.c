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

#include "libavcodec/codec.h"
#include "libavcodec/codec_desc.h"

static const char *get_type_string(enum AVMediaType type)
{
    const char *ret = av_get_media_type_string(type);
    return ret ? ret : "unknown";
}

#define AV_LOG(...) av_log(NULL, AV_LOG_FATAL, __VA_ARGS__)
#define ERR_INTERNAL(msg, ...)                                  \
do {                                                            \
    AV_LOG(msg, codec->name __VA_ARGS__);                       \
    ret = 1;                                                    \
} while (0)
#define ERR(msg)           ERR_INTERNAL(msg, )
#define ERR_EXT(msg, ...)  ERR_INTERNAL(msg, , __VA_ARGS__)

int main(void){
    void *iter = NULL;
    const AVCodec *codec = NULL;
    int ret = 0;

    while (codec = av_codec_iterate(&iter)) {
        const AVCodecDescriptor *desc;

        if (!codec->name) {
            AV_LOG("Codec for format %s has no name\n",
                   avcodec_get_name(codec->id));
            ret = 1;
            continue;
        }
        if (codec->type != AVMEDIA_TYPE_VIDEO &&
            codec->type != AVMEDIA_TYPE_AUDIO &&
            codec->type != AVMEDIA_TYPE_SUBTITLE)
            ERR_EXT("Codec %s has unsupported type %s\n",
                    get_type_string(codec->type));
        if (codec->type != AVMEDIA_TYPE_AUDIO) {
            if (codec->channel_layouts || codec->sample_fmts ||
                codec->supported_samplerates)
                ERR("Non-audio codec %s has audio-only fields set\n");
        }
        if (codec->type != AVMEDIA_TYPE_VIDEO) {
            if (codec->pix_fmts || codec->supported_framerates)
                ERR("Non-video codec %s has video-only fields set\n");
        }

        if (av_codec_is_encoder(codec)) {
            if (codec->type == AVMEDIA_TYPE_AUDIO) {
                if (!codec->sample_fmts) {
                    av_log(NULL, AV_LOG_FATAL, "Encoder %s is missing the sample_fmts field\n", codec->name);
                    ret = 1;
                }
            }
        }
        if (!(desc = avcodec_descriptor_get(codec->id))) {
            ERR("Codec %s lacks a corresponding descriptor\n");
        } else if (desc->type != codec->type)
            ERR_EXT("The type of AVCodec %s and its AVCodecDescriptor differ: "
                    "%s vs %s\n",
                    get_type_string(codec->type), get_type_string(desc->type));
    }
    return ret;
}
