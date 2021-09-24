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

#include "libavutil/opt.h"
#include "libavcodec/codec.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/internal.h"

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

static int priv_data_size_wrong(const AVCodec *codec)
{
    if (codec->priv_data_size < 0 ||
        codec->priv_class && codec->priv_data_size < sizeof(AVClass*))
        return 1;
    if (!codec->priv_class || !codec->priv_class->option)
        return 0;
    for (const AVOption *opt = codec->priv_class->option; opt->name; opt++) {
        if (opt->offset >= codec->priv_data_size ||
            opt->type == AV_OPT_TYPE_CONST && opt->offset != 0 ||
            opt->type != AV_OPT_TYPE_CONST && (opt->offset < sizeof(AVClass*) || opt->offset < 0)) {
            AV_LOG("Option %s offset %d nonsensical\n",
                   opt->name, opt->offset);
            return 1;
        }
    }
    return 0;
}

int main(void){
    void *iter = NULL;
    const AVCodec *codec = NULL;
    int ret = 0;

    while (codec = av_codec_iterate(&iter)) {
        const AVCodecDescriptor *desc;
        int is_decoder, is_encoder;

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
            if (codec->capabilities & (AV_CODEC_CAP_SMALL_LAST_FRAME |
                                       AV_CODEC_CAP_CHANNEL_CONF     |
                                       AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
                ERR("Non-audio codec %s has audio-only capabilities set\n");
        }
        if (codec->type != AVMEDIA_TYPE_VIDEO) {
            if (codec->pix_fmts || codec->supported_framerates)
                ERR("Non-video codec %s has video-only fields set\n");
            if (codec->caps_internal & FF_CODEC_CAP_EXPORTS_CROPPING)
                ERR("Non-video codec %s exports cropping\n");
        }
        if (codec->caps_internal  & FF_CODEC_CAP_SLICE_THREAD_HAS_MF &&
            !(codec->capabilities & AV_CODEC_CAP_SLICE_THREADS))
            ERR("Codec %s wants mainfunction despite not being "
                "slice-threading capable");
        if (codec->caps_internal  & FF_CODEC_CAP_AUTO_THREADS &&
            !(codec->capabilities & (AV_CODEC_CAP_FRAME_THREADS |
                                     AV_CODEC_CAP_SLICE_THREADS |
                                     AV_CODEC_CAP_OTHER_THREADS)))
            ERR("Codec %s has private-only threading support\n");

        is_decoder = av_codec_is_decoder(codec);
        is_encoder = av_codec_is_encoder(codec);
        if (!!is_decoder + !!is_encoder != 1) {
            ERR("Codec %s is decoder and encoder or neither.\n");
            continue;
        }
        if (is_encoder) {
            if (codec->type == AVMEDIA_TYPE_SUBTITLE ^ !!codec->encode_sub)
                ERR("Encoder %s is both subtitle encoder and not subtitle encoder.");
            if (!!codec->encode_sub + !!codec->encode2 + !!codec->receive_packet != 1)
                ERR("Encoder %s does not implement exactly one encode API.\n");
            if (codec->update_thread_context || codec->update_thread_context_for_user || codec->bsfs)
                ERR("Encoder %s has decoder-only thread functions or bsf.\n");
            if (codec->type == AVMEDIA_TYPE_AUDIO) {
                if (!codec->sample_fmts) {
                    av_log(NULL, AV_LOG_FATAL, "Encoder %s is missing the sample_fmts field\n", codec->name);
                    ret = 1;
                }
            }
            if (codec->caps_internal & (FF_CODEC_CAP_ALLOCATE_PROGRESS |
                                        FF_CODEC_CAP_SETS_PKT_DTS |
                                        FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM |
                                        FF_CODEC_CAP_EXPORTS_CROPPING |
                                        FF_CODEC_CAP_SETS_FRAME_PROPS) ||
                codec->capabilities  & (AV_CODEC_CAP_AVOID_PROBING |
                                        AV_CODEC_CAP_CHANNEL_CONF  |
                                        AV_CODEC_CAP_DRAW_HORIZ_BAND |
                                        AV_CODEC_CAP_SUBFRAMES))
                ERR("Encoder %s has decoder-only capabilities set\n");
            if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS &&
                codec->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)
                ERR("Frame-threaded encoder %s claims to support flushing\n");
        } else {
            if (codec->type == AVMEDIA_TYPE_SUBTITLE && !codec->decode)
                ERR("Subtitle decoder %s does not implement decode callback\n");
            if (codec->type == AVMEDIA_TYPE_SUBTITLE && codec->bsfs)
                ERR("Automatic bitstream filtering unsupported for subtitles; "
                    "yet decoder %s has it set\n");
            if (!!codec->decode + !!codec->receive_frame != 1)
                ERR("Decoder %s does not implement exactly one decode API.\n");
            if (codec->capabilities & (AV_CODEC_CAP_SMALL_LAST_FRAME    |
                                       AV_CODEC_CAP_VARIABLE_FRAME_SIZE |
                                       AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE |
                                       AV_CODEC_CAP_ENCODER_FLUSH))
                ERR("Decoder %s has encoder-only capabilities\n");
            if (codec->caps_internal & FF_CODEC_CAP_ALLOCATE_PROGRESS &&
                !(codec->capabilities & AV_CODEC_CAP_FRAME_THREADS))
                ERR("Decoder %s wants allocated progress without supporting"
                    "frame threads\n");
        }
        if (priv_data_size_wrong(codec))
            ERR_EXT("Private context of codec %s is impossibly-sized (size %d).",
                    codec->priv_data_size);
        if (!(desc = avcodec_descriptor_get(codec->id))) {
            ERR("Codec %s lacks a corresponding descriptor\n");
        } else if (desc->type != codec->type)
            ERR_EXT("The type of AVCodec %s and its AVCodecDescriptor differ: "
                    "%s vs %s\n",
                    get_type_string(codec->type), get_type_string(desc->type));
    }
    return ret;
}
