/*
 * AVCodecContext functions for libavcodec
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
 * AVCodecContext functions for libavcodec
 */

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/thread.h"
#include "avcodec.h"
#include "decode.h"
#include "encode.h"
#include "frame_thread_encoder.h"
#include "internal.h"
#include "thread.h"
#if CONFIG_ICONV
# include <iconv.h>
#endif

#include "libavutil/ffversion.h"
const char av_codec_ffversion[] = "FFmpeg version " FFMPEG_VERSION;

unsigned avcodec_version(void)
{
    av_assert0(AV_CODEC_ID_PCM_S8_PLANAR==65563);
    av_assert0(AV_CODEC_ID_ADPCM_G722==69660);
    av_assert0(AV_CODEC_ID_SRT==94216);
    av_assert0(LIBAVCODEC_VERSION_MICRO >= 100);

    return LIBAVCODEC_VERSION_INT;
}

const char *avcodec_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avcodec_license(void)
{
#define LICENSE_PREFIX "libavcodec license: "
    return &LICENSE_PREFIX FFMPEG_LICENSE[sizeof(LICENSE_PREFIX) - 1];
}

int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2), void *arg, int *ret, int count, int size)
{
    int i;

    for (i = 0; i < count; i++) {
        int r = func(c, (char *)arg + i * size);
        if (ret)
            ret[i] = r;
    }
    emms_c();
    return 0;
}

int avcodec_default_execute2(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2, int jobnr, int threadnr), void *arg, int *ret, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        int r = func(c, arg, i, 0);
        if (ret)
            ret[i] = r;
    }
    emms_c();
    return 0;
}

static AVMutex codec_mutex = AV_MUTEX_INITIALIZER;

static void lock_avcodec(const AVCodec *codec)
{
    if (!(codec->caps_internal & FF_CODEC_CAP_INIT_THREADSAFE) && codec->init)
        ff_mutex_lock(&codec_mutex);
}

static void unlock_avcodec(const AVCodec *codec)
{
    if (!(codec->caps_internal & FF_CODEC_CAP_INIT_THREADSAFE) && codec->init)
        ff_mutex_unlock(&codec_mutex);
}

#if FF_API_LOCKMGR
int av_lockmgr_register(int (*cb)(void **mutex, enum AVLockOp op))
{
    return 0;
}
#endif

static int64_t get_bit_rate(AVCodecContext *ctx)
{
    int64_t bit_rate;
    int bits_per_sample;

    switch (ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
        bit_rate = ctx->bit_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        bits_per_sample = av_get_bits_per_sample(ctx->codec_id);
        if (bits_per_sample) {
            bit_rate = ctx->sample_rate * (int64_t)ctx->channels;
            if (bit_rate > INT64_MAX / bits_per_sample) {
                bit_rate = 0;
            } else
                bit_rate *= bits_per_sample;
        } else
            bit_rate = ctx->bit_rate;
        break;
    default:
        bit_rate = 0;
        break;
    }
    return bit_rate;
}

int attribute_align_arg avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)
{
    int ret = 0;
    int codec_init_ok = 0;
    AVDictionary *tmp = NULL;
    AVCodecInternal *avci;

    if (avcodec_is_open(avctx))
        return 0;

    if (!codec && !avctx->codec) {
        av_log(avctx, AV_LOG_ERROR, "No codec provided to avcodec_open2()\n");
        return AVERROR(EINVAL);
    }
    if (codec && avctx->codec && codec != avctx->codec) {
        av_log(avctx, AV_LOG_ERROR, "This AVCodecContext was allocated for %s, "
                                    "but %s passed to avcodec_open2()\n", avctx->codec->name, codec->name);
        return AVERROR(EINVAL);
    }
    if (!codec)
        codec = avctx->codec;

    if (avctx->extradata_size < 0 || avctx->extradata_size >= FF_MAX_EXTRADATA_SIZE)
        return AVERROR(EINVAL);

    if (options)
        av_dict_copy(&tmp, *options, 0);

    lock_avcodec(codec);

    avci = av_mallocz(sizeof(*avci));
    if (!avci) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    avctx->internal = avci;

#if FF_API_OLD_ENCDEC
    avci->to_free = av_frame_alloc();
    avci->compat_decode_frame = av_frame_alloc();
    avci->compat_encode_packet = av_packet_alloc();
    if (!avci->to_free || !avci->compat_decode_frame || !avci->compat_encode_packet) {
        ret = AVERROR(ENOMEM);
        goto free_and_end;
    }
#endif
    avci->buffer_frame = av_frame_alloc();
    avci->buffer_pkt = av_packet_alloc();
    avci->es.in_frame = av_frame_alloc();
    avci->ds.in_pkt = av_packet_alloc();
    avci->last_pkt_props = av_packet_alloc();
    avci->pkt_props = av_fifo_alloc(sizeof(*avci->last_pkt_props));
    if (!avci->buffer_frame || !avci->buffer_pkt          ||
        !avci->es.in_frame  || !avci->ds.in_pkt           ||
        !avci->last_pkt_props || !avci->pkt_props) {
        ret = AVERROR(ENOMEM);
        goto free_and_end;
    }

    avci->skip_samples_multiplier = 1;

    if (codec->priv_data_size > 0) {
        if (!avctx->priv_data) {
            avctx->priv_data = av_mallocz(codec->priv_data_size);
            if (!avctx->priv_data) {
                ret = AVERROR(ENOMEM);
                goto free_and_end;
            }
            if (codec->priv_class) {
                *(const AVClass **)avctx->priv_data = codec->priv_class;
                av_opt_set_defaults(avctx->priv_data);
            }
        }
        if (codec->priv_class && (ret = av_opt_set_dict(avctx->priv_data, &tmp)) < 0)
            goto free_and_end;
    } else {
        avctx->priv_data = NULL;
    }
    if ((ret = av_opt_set_dict(avctx, &tmp)) < 0)
        goto free_and_end;

    if (avctx->codec_whitelist && av_match_list(codec->name, avctx->codec_whitelist, ',') <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Codec (%s) not on whitelist \'%s\'\n", codec->name, avctx->codec_whitelist);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    // only call ff_set_dimensions() for non H.264/VP6F/DXV codecs so as not to overwrite previously setup dimensions
    if (!(avctx->coded_width && avctx->coded_height && avctx->width && avctx->height &&
          (avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_VP6F || avctx->codec_id == AV_CODEC_ID_DXV))) {
        if (avctx->coded_width && avctx->coded_height)
            ret = ff_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
        else if (avctx->width && avctx->height)
            ret = ff_set_dimensions(avctx, avctx->width, avctx->height);
        if (ret < 0)
            goto free_and_end;
    }

    if ((avctx->coded_width || avctx->coded_height || avctx->width || avctx->height)
        && (  av_image_check_size2(avctx->coded_width, avctx->coded_height, avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx) < 0
           || av_image_check_size2(avctx->width,       avctx->height,       avctx->max_pixels, AV_PIX_FMT_NONE, 0, avctx) < 0)) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring invalid width/height values\n");
        ff_set_dimensions(avctx, 0, 0);
    }

    if (avctx->width > 0 && avctx->height > 0) {
        if (av_image_check_sar(avctx->width, avctx->height,
                               avctx->sample_aspect_ratio) < 0) {
            av_log(avctx, AV_LOG_WARNING, "ignoring invalid SAR: %u/%u\n",
                   avctx->sample_aspect_ratio.num,
                   avctx->sample_aspect_ratio.den);
            avctx->sample_aspect_ratio = (AVRational){ 0, 1 };
        }
    }

    if (avctx->channels > FF_SANE_NB_CHANNELS || avctx->channels < 0) {
        av_log(avctx, AV_LOG_ERROR, "Too many or invalid channels: %d\n", avctx->channels);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    if (av_codec_is_decoder(codec) &&
        codec->type == AVMEDIA_TYPE_AUDIO &&
        !(codec->capabilities & AV_CODEC_CAP_CHANNEL_CONF) &&
        avctx->channels == 0) {
        av_log(avctx, AV_LOG_ERROR, "Decoder requires channel count but channels not set\n");
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    if (avctx->sample_rate < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid sample rate: %d\n", avctx->sample_rate);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    if (avctx->block_align < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid block align: %d\n", avctx->block_align);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    if ((avctx->codec_type == AVMEDIA_TYPE_UNKNOWN || avctx->codec_type == codec->type) &&
        avctx->codec_id == AV_CODEC_ID_NONE) {
        avctx->codec_type = codec->type;
        avctx->codec_id   = codec->id;
    }
    if (avctx->codec_id != codec->id || (avctx->codec_type != codec->type
                                         && avctx->codec_type != AVMEDIA_TYPE_ATTACHMENT)) {
        av_log(avctx, AV_LOG_ERROR, "Codec type or id mismatches\n");
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    avctx->frame_number = 0;
    avctx->codec_descriptor = avcodec_descriptor_get(avctx->codec_id);

    if ((avctx->codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        const char *codec_string = av_codec_is_encoder(codec) ? "encoder" : "decoder";
        const AVCodec *codec2;
        av_log(avctx, AV_LOG_ERROR,
               "The %s '%s' is experimental but experimental codecs are not enabled, "
               "add '-strict %d' if you want to use it.\n",
               codec_string, codec->name, FF_COMPLIANCE_EXPERIMENTAL);
        codec2 = av_codec_is_encoder(codec) ? avcodec_find_encoder(codec->id) : avcodec_find_decoder(codec->id);
        if (!(codec2->capabilities & AV_CODEC_CAP_EXPERIMENTAL))
            av_log(avctx, AV_LOG_ERROR, "Alternatively use the non experimental %s '%s'.\n",
                codec_string, codec2->name);
        ret = AVERROR_EXPERIMENTAL;
        goto free_and_end;
    }

    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO &&
        (!avctx->time_base.num || !avctx->time_base.den)) {
        avctx->time_base.num = 1;
        avctx->time_base.den = avctx->sample_rate;
    }

    if (av_codec_is_encoder(avctx->codec))
        ret = ff_encode_preinit(avctx);
    else
        ret = ff_decode_preinit(avctx);
    if (ret < 0)
        goto free_and_end;

    if (!HAVE_THREADS)
        av_log(avctx, AV_LOG_WARNING, "Warning: not compiled with thread support, using thread emulation\n");

    if (CONFIG_FRAME_THREAD_ENCODER && av_codec_is_encoder(avctx->codec)) {
        unlock_avcodec(codec); //we will instantiate a few encoders thus kick the counter to prevent false detection of a problem
        ret = ff_frame_thread_encoder_init(avctx, options ? *options : NULL);
        lock_avcodec(codec);
        if (ret < 0)
            goto free_and_end;
    }

    if (HAVE_THREADS
        && !(avci->frame_thread_encoder && (avctx->active_thread_type&FF_THREAD_FRAME))) {
        ret = ff_thread_init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }
    if (!HAVE_THREADS && !(codec->caps_internal & FF_CODEC_CAP_AUTO_THREADS))
        avctx->thread_count = 1;

    if (   avctx->codec->init && (!(avctx->active_thread_type&FF_THREAD_FRAME)
        || avci->frame_thread_encoder)) {
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            codec_init_ok = -1;
            goto free_and_end;
        }
        codec_init_ok = 1;
    }

    ret=0;

    if (av_codec_is_decoder(avctx->codec)) {
        if (!avctx->bit_rate)
            avctx->bit_rate = get_bit_rate(avctx);
        /* validate channel layout from the decoder */
        if (avctx->channel_layout) {
            int channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
            if (!avctx->channels)
                avctx->channels = channels;
            else if (channels != avctx->channels) {
                char buf[512];
                av_get_channel_layout_string(buf, sizeof(buf), -1, avctx->channel_layout);
                av_log(avctx, AV_LOG_WARNING,
                       "Channel layout '%s' with %d channels does not match specified number of channels %d: "
                       "ignoring specified channel layout\n",
                       buf, channels, avctx->channels);
                avctx->channel_layout = 0;
            }
        }
        if (avctx->channels && avctx->channels < 0 ||
            avctx->channels > FF_SANE_NB_CHANNELS) {
            ret = AVERROR(EINVAL);
            goto free_and_end;
        }
        if (avctx->bits_per_coded_sample < 0) {
            ret = AVERROR(EINVAL);
            goto free_and_end;
        }
        if (avctx->sub_charenc) {
            if (avctx->codec_type != AVMEDIA_TYPE_SUBTITLE) {
                av_log(avctx, AV_LOG_ERROR, "Character encoding is only "
                       "supported with subtitles codecs\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            } else if (avctx->codec_descriptor->props & AV_CODEC_PROP_BITMAP_SUB) {
                av_log(avctx, AV_LOG_WARNING, "Codec '%s' is bitmap-based, "
                       "subtitles character encoding will be ignored\n",
                       avctx->codec_descriptor->name);
                avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_DO_NOTHING;
            } else {
                /* input character encoding is set for a text based subtitle
                 * codec at this point */
                if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_AUTOMATIC)
                    avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_PRE_DECODER;

                if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_PRE_DECODER) {
#if CONFIG_ICONV
                    iconv_t cd = iconv_open("UTF-8", avctx->sub_charenc);
                    if (cd == (iconv_t)-1) {
                        ret = AVERROR(errno);
                        av_log(avctx, AV_LOG_ERROR, "Unable to open iconv context "
                               "with input character encoding \"%s\"\n", avctx->sub_charenc);
                        goto free_and_end;
                    }
                    iconv_close(cd);
#else
                    av_log(avctx, AV_LOG_ERROR, "Character encoding subtitles "
                           "conversion needs a libavcodec built with iconv support "
                           "for this codec\n");
                    ret = AVERROR(ENOSYS);
                    goto free_and_end;
#endif
                }
            }
        }

#if FF_API_AVCTX_TIMEBASE
        if (avctx->framerate.num > 0 && avctx->framerate.den > 0)
            avctx->time_base = av_inv_q(av_mul_q(avctx->framerate, (AVRational){avctx->ticks_per_frame, 1}));
#endif
    }
    if (codec->priv_data_size > 0 && avctx->priv_data && codec->priv_class) {
        av_assert0(*(const AVClass **)avctx->priv_data == codec->priv_class);
    }

end:
    unlock_avcodec(codec);
    if (options) {
        av_dict_free(options);
        *options = tmp;
    }

    return ret;
free_and_end:
    if (avctx->codec && avctx->codec->close &&
        (codec_init_ok > 0 || (codec_init_ok < 0 &&
         avctx->codec->caps_internal & FF_CODEC_CAP_INIT_CLEANUP)))
        avctx->codec->close(avctx);

    if (HAVE_THREADS && avci->thread_ctx)
        ff_thread_free(avctx);

    if (codec->priv_class && avctx->priv_data)
        av_opt_free(avctx->priv_data);
    av_opt_free(avctx);

    if (av_codec_is_encoder(avctx->codec)) {
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        av_frame_free(&avctx->coded_frame);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        av_freep(&avctx->extradata);
        avctx->extradata_size = 0;
    }

    av_dict_free(&tmp);
    av_freep(&avctx->priv_data);
    av_freep(&avctx->subtitle_header);

#if FF_API_OLD_ENCDEC
    av_frame_free(&avci->to_free);
    av_frame_free(&avci->compat_decode_frame);
    av_packet_free(&avci->compat_encode_packet);
#endif
    av_frame_free(&avci->buffer_frame);
    av_packet_free(&avci->buffer_pkt);
    av_packet_free(&avci->last_pkt_props);
    av_fifo_freep(&avci->pkt_props);

    av_packet_free(&avci->ds.in_pkt);
    av_frame_free(&avci->es.in_frame);
    av_bsf_free(&avci->bsf);

    av_buffer_unref(&avci->pool);
    av_freep(&avci);
    avctx->internal = NULL;
    avctx->codec = NULL;
    goto end;
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    AVCodecInternal *avci = avctx->internal;

    if (av_codec_is_encoder(avctx->codec)) {
        int caps = avctx->codec->capabilities;

        if (!(caps & AV_CODEC_CAP_ENCODER_FLUSH)) {
            // Only encoders that explicitly declare support for it can be
            // flushed. Otherwise, this is a no-op.
            av_log(avctx, AV_LOG_WARNING, "Ignoring attempt to flush encoder "
                   "that doesn't support it\n");
            return;
        }

        // We haven't implemented flushing for frame-threaded encoders.
        av_assert0(!(caps & AV_CODEC_CAP_FRAME_THREADS));
    }

    avci->draining      = 0;
    avci->draining_done = 0;
    avci->nb_draining_errors = 0;
    av_frame_unref(avci->buffer_frame);
#if FF_API_OLD_ENCDEC
    av_frame_unref(avci->compat_decode_frame);
    av_packet_unref(avci->compat_encode_packet);
#endif
    av_packet_unref(avci->buffer_pkt);

    av_packet_unref(avci->last_pkt_props);
    while (av_fifo_size(avci->pkt_props) >= sizeof(*avci->last_pkt_props)) {
        av_fifo_generic_read(avci->pkt_props,
                             avci->last_pkt_props, sizeof(*avci->last_pkt_props),
                             NULL);
        av_packet_unref(avci->last_pkt_props);
    }
    av_fifo_reset(avci->pkt_props);

    av_frame_unref(avci->es.in_frame);
    av_packet_unref(avci->ds.in_pkt);

    if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
        ff_thread_flush(avctx);
    else if (avctx->codec->flush)
        avctx->codec->flush(avctx);

    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if (av_codec_is_decoder(avctx->codec))
        av_bsf_flush(avci->bsf);

#if FF_API_OLD_ENCDEC
FF_DISABLE_DEPRECATION_WARNINGS
    if (!avctx->refcounted_frames)
        av_frame_unref(avci->to_free);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
}

void avsubtitle_free(AVSubtitle *sub)
{
    int i;

    for (i = 0; i < sub->num_rects; i++) {
        av_freep(&sub->rects[i]->data[0]);
        av_freep(&sub->rects[i]->data[1]);
        av_freep(&sub->rects[i]->data[2]);
        av_freep(&sub->rects[i]->data[3]);
        av_freep(&sub->rects[i]->text);
        av_freep(&sub->rects[i]->ass);
        av_freep(&sub->rects[i]);
    }

    av_freep(&sub->rects);

    memset(sub, 0, sizeof(*sub));
}

av_cold int avcodec_close(AVCodecContext *avctx)
{
    int i;

    if (!avctx)
        return 0;

    if (avcodec_is_open(avctx)) {
        if (CONFIG_FRAME_THREAD_ENCODER &&
            avctx->internal->frame_thread_encoder && avctx->thread_count > 1) {
            ff_frame_thread_encoder_free(avctx);
        }
        if (HAVE_THREADS && avctx->internal->thread_ctx)
            ff_thread_free(avctx);
        if (avctx->codec && avctx->codec->close)
            avctx->codec->close(avctx);
        avctx->internal->byte_buffer_size = 0;
        av_freep(&avctx->internal->byte_buffer);
#if FF_API_OLD_ENCDEC
        av_frame_free(&avctx->internal->to_free);
        av_frame_free(&avctx->internal->compat_decode_frame);
        av_packet_free(&avctx->internal->compat_encode_packet);
#endif
        av_frame_free(&avctx->internal->buffer_frame);
        av_packet_free(&avctx->internal->buffer_pkt);
        av_packet_unref(avctx->internal->last_pkt_props);
        while (av_fifo_size(avctx->internal->pkt_props) >=
               sizeof(*avctx->internal->last_pkt_props)) {
            av_fifo_generic_read(avctx->internal->pkt_props,
                                 avctx->internal->last_pkt_props,
                                 sizeof(*avctx->internal->last_pkt_props),
                                 NULL);
            av_packet_unref(avctx->internal->last_pkt_props);
        }
        av_packet_free(&avctx->internal->last_pkt_props);
        av_fifo_freep(&avctx->internal->pkt_props);

        av_packet_free(&avctx->internal->ds.in_pkt);
        av_frame_free(&avctx->internal->es.in_frame);

        av_buffer_unref(&avctx->internal->pool);

        if (avctx->hwaccel && avctx->hwaccel->uninit)
            avctx->hwaccel->uninit(avctx);
        av_freep(&avctx->internal->hwaccel_priv_data);

        av_bsf_free(&avctx->internal->bsf);

        av_freep(&avctx->internal);
    }

    for (i = 0; i < avctx->nb_coded_side_data; i++)
        av_freep(&avctx->coded_side_data[i].data);
    av_freep(&avctx->coded_side_data);
    avctx->nb_coded_side_data = 0;

    av_buffer_unref(&avctx->hw_frames_ctx);
    av_buffer_unref(&avctx->hw_device_ctx);

    if (avctx->priv_data && avctx->codec && avctx->codec->priv_class)
        av_opt_free(avctx->priv_data);
    av_opt_free(avctx);
    av_freep(&avctx->priv_data);
    if (av_codec_is_encoder(avctx->codec)) {
        av_freep(&avctx->extradata);
#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
        av_frame_free(&avctx->coded_frame);
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    }
    avctx->codec = NULL;
    avctx->active_thread_type = 0;

    return 0;
}

static const char *unknown_if_null(const char *str)
{
    return str ? str : "unknown";
}

void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode)
{
    const char *codec_type;
    const char *codec_name;
    const char *profile = NULL;
    int64_t bitrate;
    int new_line = 0;
    AVRational display_aspect_ratio;
    const char *separator = enc->dump_separator ? (const char *)enc->dump_separator : ", ";
    const char *str;

    if (!buf || buf_size <= 0)
        return;
    codec_type = av_get_media_type_string(enc->codec_type);
    codec_name = avcodec_get_name(enc->codec_id);
    profile = avcodec_profile_name(enc->codec_id, enc->profile);

    snprintf(buf, buf_size, "%s: %s", codec_type ? codec_type : "unknown",
             codec_name);
    buf[0] ^= 'a' ^ 'A'; /* first letter in uppercase */

    if (enc->codec && strcmp(enc->codec->name, codec_name))
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s)", enc->codec->name);

    if (profile)
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s)", profile);
    if (   enc->codec_type == AVMEDIA_TYPE_VIDEO
        && av_log_get_level() >= AV_LOG_VERBOSE
        && enc->refs)
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", %d reference frame%s",
                 enc->refs, enc->refs > 1 ? "s" : "");

    if (enc->codec_tag)
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s / 0x%04X)",
                 av_fourcc2str(enc->codec_tag), enc->codec_tag);

    switch (enc->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        {
            char detail[256] = "(";

            av_strlcat(buf, separator, buf_size);

            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     "%s", enc->pix_fmt == AV_PIX_FMT_NONE ? "none" :
                     unknown_if_null(av_get_pix_fmt_name(enc->pix_fmt)));
            if (enc->bits_per_raw_sample && enc->pix_fmt != AV_PIX_FMT_NONE &&
                enc->bits_per_raw_sample < av_pix_fmt_desc_get(enc->pix_fmt)->comp[0].depth)
                av_strlcatf(detail, sizeof(detail), "%d bpc, ", enc->bits_per_raw_sample);
            if (enc->color_range != AVCOL_RANGE_UNSPECIFIED &&
                (str = av_color_range_name(enc->color_range)))
                av_strlcatf(detail, sizeof(detail), "%s, ", str);

            if (enc->colorspace != AVCOL_SPC_UNSPECIFIED ||
                enc->color_primaries != AVCOL_PRI_UNSPECIFIED ||
                enc->color_trc != AVCOL_TRC_UNSPECIFIED) {
                const char *col = unknown_if_null(av_color_space_name(enc->colorspace));
                const char *pri = unknown_if_null(av_color_primaries_name(enc->color_primaries));
                const char *trc = unknown_if_null(av_color_transfer_name(enc->color_trc));
                if (strcmp(col, pri) || strcmp(col, trc)) {
                    new_line = 1;
                    av_strlcatf(detail, sizeof(detail), "%s/%s/%s, ",
                                col, pri, trc);
                } else
                    av_strlcatf(detail, sizeof(detail), "%s, ", col);
            }

            if (enc->field_order != AV_FIELD_UNKNOWN) {
                const char *field_order = "progressive";
                if (enc->field_order == AV_FIELD_TT)
                    field_order = "top first";
                else if (enc->field_order == AV_FIELD_BB)
                    field_order = "bottom first";
                else if (enc->field_order == AV_FIELD_TB)
                    field_order = "top coded first (swapped)";
                else if (enc->field_order == AV_FIELD_BT)
                    field_order = "bottom coded first (swapped)";

                av_strlcatf(detail, sizeof(detail), "%s, ", field_order);
            }

            if (av_log_get_level() >= AV_LOG_VERBOSE &&
                enc->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED &&
                (str = av_chroma_location_name(enc->chroma_sample_location)))
                av_strlcatf(detail, sizeof(detail), "%s, ", str);

            if (strlen(detail) > 1) {
                detail[strlen(detail) - 2] = 0;
                av_strlcatf(buf, buf_size, "%s)", detail);
            }
        }

        if (enc->width) {
            av_strlcat(buf, new_line ? separator : ", ", buf_size);

            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     "%dx%d",
                     enc->width, enc->height);

            if (av_log_get_level() >= AV_LOG_VERBOSE &&
                (enc->width != enc->coded_width ||
                 enc->height != enc->coded_height))
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         " (%dx%d)", enc->coded_width, enc->coded_height);

            if (enc->sample_aspect_ratio.num) {
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          enc->width * (int64_t)enc->sample_aspect_ratio.num,
                          enc->height * (int64_t)enc->sample_aspect_ratio.den,
                          1024 * 1024);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         " [SAR %d:%d DAR %d:%d]",
                         enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den,
                         display_aspect_ratio.num, display_aspect_ratio.den);
            }
            if (av_log_get_level() >= AV_LOG_DEBUG) {
                int g = av_gcd(enc->time_base.num, enc->time_base.den);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", %d/%d",
                         enc->time_base.num / g, enc->time_base.den / g);
            }
        }
        if (encode) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", q=%d-%d", enc->qmin, enc->qmax);
        } else {
            if (enc->properties & FF_CODEC_PROPERTY_CLOSED_CAPTIONS)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", Closed Captions");
            if (enc->properties & FF_CODEC_PROPERTY_LOSSLESS)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", lossless");
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        av_strlcat(buf, separator, buf_size);

        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     "%d Hz, ", enc->sample_rate);
        }
        av_get_channel_layout_string(buf + strlen(buf), buf_size - strlen(buf), enc->channels, enc->channel_layout);
        if (enc->sample_fmt != AV_SAMPLE_FMT_NONE &&
            (str = av_get_sample_fmt_name(enc->sample_fmt))) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s", str);
        }
        if (   enc->bits_per_raw_sample > 0
            && enc->bits_per_raw_sample != av_get_bytes_per_sample(enc->sample_fmt) * 8)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     " (%d bit)", enc->bits_per_raw_sample);
        if (av_log_get_level() >= AV_LOG_VERBOSE) {
            if (enc->initial_padding)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", delay %d", enc->initial_padding);
            if (enc->trailing_padding)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", padding %d", enc->trailing_padding);
        }
        break;
    case AVMEDIA_TYPE_DATA:
        if (av_log_get_level() >= AV_LOG_DEBUG) {
            int g = av_gcd(enc->time_base.num, enc->time_base.den);
            if (g)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", %d/%d",
                         enc->time_base.num / g, enc->time_base.den / g);
        }
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (enc->width)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d", enc->width, enc->height);
        break;
    default:
        return;
    }
    if (encode) {
        if (enc->flags & AV_CODEC_FLAG_PASS1)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 1");
        if (enc->flags & AV_CODEC_FLAG_PASS2)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 2");
    }
    bitrate = get_bit_rate(enc);
    if (bitrate != 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", %"PRId64" kb/s", bitrate / 1000);
    } else if (enc->rc_max_rate > 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", max. %"PRId64" kb/s", enc->rc_max_rate / 1000);
    }
}

int avcodec_is_open(AVCodecContext *s)
{
    return !!s->internal;
}
