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

#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"

#include "ffmpeg.h"

static enum AVPixelFormat get_format(AVCodecContext *s, const enum AVPixelFormat *pix_fmts)
{
    InputStream *ist = s->opaque;
    const enum AVPixelFormat *p;
    int ret;

    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(*p);
        const AVCodecHWConfig  *config = NULL;
        int i;

        if (!(desc->flags & AV_PIX_FMT_FLAG_HWACCEL))
            break;

        if (ist->hwaccel_id == HWACCEL_GENERIC ||
            ist->hwaccel_id == HWACCEL_AUTO) {
            for (i = 0;; i++) {
                config = avcodec_get_hw_config(s->codec, i);
                if (!config)
                    break;
                if (!(config->methods &
                      AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    continue;
                if (config->pix_fmt == *p)
                    break;
            }
        }
        if (config && config->device_type == ist->hwaccel_device_type) {
            ret = hwaccel_decode_init(s);
            if (ret < 0) {
                if (ist->hwaccel_id == HWACCEL_GENERIC) {
                    av_log(NULL, AV_LOG_FATAL,
                           "%s hwaccel requested for input stream #%d:%d, "
                           "but cannot be initialized.\n",
                           av_hwdevice_get_type_name(config->device_type),
                           ist->file_index, ist->st->index);
                    return AV_PIX_FMT_NONE;
                }
                continue;
            }

            ist->hwaccel_pix_fmt = *p;
            break;
        }
    }

    return *p;
}

int dec_open(InputStream *ist)
{
    const AVCodec *codec = ist->dec;
    int ret;

    if (!codec) {
        av_log(ist, AV_LOG_ERROR,
               "Decoding requested, but no decoder found for: %s\n",
                avcodec_get_name(ist->dec_ctx->codec_id));
        return AVERROR(EINVAL);
    }

    ist->dec_ctx->opaque                = ist;
    ist->dec_ctx->get_format            = get_format;

    if (ist->dec_ctx->codec_id == AV_CODEC_ID_DVB_SUBTITLE &&
       (ist->decoding_needed & DECODING_FOR_OST)) {
        av_dict_set(&ist->decoder_opts, "compute_edt", "1", AV_DICT_DONT_OVERWRITE);
        if (ist->decoding_needed & DECODING_FOR_FILTER)
            av_log(NULL, AV_LOG_WARNING, "Warning using DVB subtitles for filtering and output at the same time is not fully supported, also see -compute_edt [0|1]\n");
    }

    /* Useful for subtitles retiming by lavf (FIXME), skipping samples in
     * audio, and video decoders such as cuvid or mediacodec */
    ist->dec_ctx->pkt_timebase = ist->st->time_base;

    if (!av_dict_get(ist->decoder_opts, "threads", NULL, 0))
        av_dict_set(&ist->decoder_opts, "threads", "auto", 0);
    /* Attached pics are sparse, therefore we would not want to delay their decoding till EOF. */
    if (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC)
        av_dict_set(&ist->decoder_opts, "threads", "1", 0);

    ret = hw_device_setup_for_decode(ist);
    if (ret < 0) {
        av_log(ist, AV_LOG_ERROR,
               "Hardware device setup failed for decoder: %s\n",
               av_err2str(ret));
        return ret;
    }

    if ((ret = avcodec_open2(ist->dec_ctx, codec, &ist->decoder_opts)) < 0) {
        if (ret == AVERROR_EXPERIMENTAL)
            exit_program(1);

        av_log(ist, AV_LOG_ERROR, "Error while opening decoder: %s\n",
               av_err2str(ret));
        return ret;
    }
    assert_avoptions(ist->decoder_opts);

    return 0;
}
