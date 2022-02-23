/*
 * Android MediaCodec Wrapper
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <jni.h>

#include "libavutil/avassert.h"
#include "libavutil/mem.h"
#include "libavutil/avstring.h"

#include "avcodec.h"
#include "ffjni.h"
#include "mediacodec_wrapper.h"

struct JNIAMediaCodecListFields {

    jclass mediacodec_list_class;
    jmethodID init_id;
    jmethodID find_decoder_for_format_id;

    jmethodID get_codec_count_id;
    jmethodID get_codec_info_at_id;

    jclass mediacodec_info_class;
    jmethodID get_name_id;
    jmethodID get_codec_capabilities_id;
    jmethodID get_supported_types_id;
    jmethodID is_encoder_id;
    jmethodID is_software_only_id;

    jclass codec_capabilities_class;
    jfieldID color_formats_id;
    jfieldID profile_levels_id;

    jclass codec_profile_level_class;
    jfieldID profile_id;
    jfieldID level_id;

    jfieldID avc_profile_baseline_id;
    jfieldID avc_profile_main_id;
    jfieldID avc_profile_extended_id;
    jfieldID avc_profile_high_id;
    jfieldID avc_profile_high10_id;
    jfieldID avc_profile_high422_id;
    jfieldID avc_profile_high444_id;

    jfieldID hevc_profile_main_id;
    jfieldID hevc_profile_main10_id;
    jfieldID hevc_profile_main10_hdr10_id;

};

static const struct FFJniField jni_amediacodeclist_mapping[] = {
    { "android/media/MediaCodecList", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, mediacodec_list_class), 1 },
        { "android/media/MediaCodecList", "<init>", "(I)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, init_id), 0 },
        { "android/media/MediaCodecList", "findDecoderForFormat", "(Landroid/media/MediaFormat;)Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, find_decoder_for_format_id), 0 },

        { "android/media/MediaCodecList", "getCodecCount", "()I", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecListFields, get_codec_count_id), 1 },
        { "android/media/MediaCodecList", "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecListFields, get_codec_info_at_id), 1 },

    { "android/media/MediaCodecInfo", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, mediacodec_info_class), 1 },
        { "android/media/MediaCodecInfo", "getName", "()Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, get_name_id), 1 },
        { "android/media/MediaCodecInfo", "getCapabilitiesForType", "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, get_codec_capabilities_id), 1 },
        { "android/media/MediaCodecInfo", "getSupportedTypes", "()[Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, get_supported_types_id), 1 },
        { "android/media/MediaCodecInfo", "isEncoder", "()Z", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, is_encoder_id), 1 },
        { "android/media/MediaCodecInfo", "isSoftwareOnly", "()Z", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, is_software_only_id), 0 },

    { "android/media/MediaCodecInfo$CodecCapabilities", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, codec_capabilities_class), 1 },
        { "android/media/MediaCodecInfo$CodecCapabilities", "colorFormats", "[I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecListFields, color_formats_id), 1 },
        { "android/media/MediaCodecInfo$CodecCapabilities", "profileLevels", "[Landroid/media/MediaCodecInfo$CodecProfileLevel;", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecListFields, profile_levels_id), 1 },

    { "android/media/MediaCodecInfo$CodecProfileLevel", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, codec_profile_level_class), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "profile", "I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecListFields, profile_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "level", "I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecListFields, level_id), 1 },

        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileBaseline", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_baseline_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileMain", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_main_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileExtended", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_extended_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileHigh", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_high_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileHigh10", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_high10_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileHigh422", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_high422_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "AVCProfileHigh444", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, avc_profile_high444_id), 1 },

        { "android/media/MediaCodecInfo$CodecProfileLevel", "HEVCProfileMain", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, hevc_profile_main_id), 0 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "HEVCProfileMain10", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, hevc_profile_main10_id), 0 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "HEVCProfileMain10HDR10", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecListFields, hevc_profile_main10_hdr10_id), 0 },

    { NULL }
};

struct JNIAMediaFormatFields {

    jclass mediaformat_class;

    jmethodID init_id;

    jmethodID contains_key_id;

    jmethodID get_integer_id;
    jmethodID get_long_id;
    jmethodID get_float_id;
    jmethodID get_bytebuffer_id;
    jmethodID get_string_id;

    jmethodID set_integer_id;
    jmethodID set_long_id;
    jmethodID set_float_id;
    jmethodID set_bytebuffer_id;
    jmethodID set_string_id;

    jmethodID to_string_id;

};

static const struct FFJniField jni_amediaformat_mapping[] = {
    { "android/media/MediaFormat", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaFormatFields, mediaformat_class), 1 },

        { "android/media/MediaFormat", "<init>", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, init_id), 1 },

        { "android/media/MediaFormat", "containsKey", "(Ljava/lang/String;)Z", FF_JNI_METHOD,offsetof(struct JNIAMediaFormatFields, contains_key_id), 1 },

        { "android/media/MediaFormat", "getInteger", "(Ljava/lang/String;)I", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, get_integer_id), 1 },
        { "android/media/MediaFormat", "getLong", "(Ljava/lang/String;)J", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, get_long_id), 1 },
        { "android/media/MediaFormat", "getFloat", "(Ljava/lang/String;)F", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, get_float_id), 1 },
        { "android/media/MediaFormat", "getByteBuffer", "(Ljava/lang/String;)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, get_bytebuffer_id), 1 },
        { "android/media/MediaFormat", "getString", "(Ljava/lang/String;)Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, get_string_id), 1 },

        { "android/media/MediaFormat", "setInteger", "(Ljava/lang/String;I)V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, set_integer_id), 1 },
        { "android/media/MediaFormat", "setLong", "(Ljava/lang/String;J)V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, set_long_id), 1 },
        { "android/media/MediaFormat", "setFloat", "(Ljava/lang/String;F)V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, set_float_id), 1 },
        { "android/media/MediaFormat", "setByteBuffer", "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, set_bytebuffer_id), 1 },
        { "android/media/MediaFormat", "setString", "(Ljava/lang/String;Ljava/lang/String;)V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, set_string_id), 1 },

        { "android/media/MediaFormat", "toString", "()Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, to_string_id), 1 },

    { NULL }
};

static const AVClass amediaformat_class = {
    .class_name = "amediaformat",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

struct FFAMediaFormat {

    const AVClass *class;
    struct JNIAMediaFormatFields jfields;
    jobject object;
};

struct JNIAMediaCodecFields {

    jclass mediacodec_class;

    jfieldID info_try_again_later_id;
    jfieldID info_output_buffers_changed_id;
    jfieldID info_output_format_changed_id;

    jfieldID buffer_flag_codec_config_id;
    jfieldID buffer_flag_end_of_stream_id;
    jfieldID buffer_flag_key_frame_id;

    jfieldID configure_flag_encode_id;

    jmethodID create_by_codec_name_id;
    jmethodID create_decoder_by_type_id;
    jmethodID create_encoder_by_type_id;

    jmethodID get_name_id;

    jmethodID configure_id;
    jmethodID start_id;
    jmethodID flush_id;
    jmethodID stop_id;
    jmethodID release_id;

    jmethodID get_output_format_id;

    jmethodID dequeue_input_buffer_id;
    jmethodID queue_input_buffer_id;
    jmethodID get_input_buffer_id;
    jmethodID get_input_buffers_id;

    jmethodID dequeue_output_buffer_id;
    jmethodID get_output_buffer_id;
    jmethodID get_output_buffers_id;
    jmethodID release_output_buffer_id;
    jmethodID release_output_buffer_at_time_id;

    jclass mediainfo_class;

    jmethodID init_id;

    jfieldID flags_id;
    jfieldID offset_id;
    jfieldID presentation_time_us_id;
    jfieldID size_id;

};

static const struct FFJniField jni_amediacodec_mapping[] = {
    { "android/media/MediaCodec", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecFields, mediacodec_class), 1 },

        { "android/media/MediaCodec", "INFO_TRY_AGAIN_LATER", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, info_try_again_later_id), 1 },
        { "android/media/MediaCodec", "INFO_OUTPUT_BUFFERS_CHANGED", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, info_output_buffers_changed_id), 1 },
        { "android/media/MediaCodec", "INFO_OUTPUT_FORMAT_CHANGED", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, info_output_format_changed_id), 1 },

        { "android/media/MediaCodec", "BUFFER_FLAG_CODEC_CONFIG", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, buffer_flag_codec_config_id), 1 },
        { "android/media/MediaCodec", "BUFFER_FLAG_END_OF_STREAM", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, buffer_flag_end_of_stream_id), 1 },
        { "android/media/MediaCodec", "BUFFER_FLAG_KEY_FRAME", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, buffer_flag_key_frame_id), 0 },

        { "android/media/MediaCodec", "CONFIGURE_FLAG_ENCODE", "I", FF_JNI_STATIC_FIELD, offsetof(struct JNIAMediaCodecFields, configure_flag_encode_id), 1 },

        { "android/media/MediaCodec", "createByCodecName", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecFields, create_by_codec_name_id), 1 },
        { "android/media/MediaCodec", "createDecoderByType", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecFields, create_decoder_by_type_id), 1 },
        { "android/media/MediaCodec", "createEncoderByType", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecFields, create_encoder_by_type_id), 1 },

        { "android/media/MediaCodec", "getName", "()Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_name_id), 1 },

        { "android/media/MediaCodec", "configure", "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, configure_id), 1 },
        { "android/media/MediaCodec", "start", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, start_id), 1 },
        { "android/media/MediaCodec", "flush", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, flush_id), 1 },
        { "android/media/MediaCodec", "stop", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, stop_id), 1 },
        { "android/media/MediaCodec", "release", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, release_id), 1 },

        { "android/media/MediaCodec", "getOutputFormat", "()Landroid/media/MediaFormat;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_output_format_id), 1 },

        { "android/media/MediaCodec", "dequeueInputBuffer", "(J)I", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, dequeue_input_buffer_id), 1 },
        { "android/media/MediaCodec", "queueInputBuffer", "(IIIJI)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, queue_input_buffer_id), 1 },
        { "android/media/MediaCodec", "getInputBuffer", "(I)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_input_buffer_id), 0 },
        { "android/media/MediaCodec", "getInputBuffers", "()[Ljava/nio/ByteBuffer;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_input_buffers_id), 1 },

        { "android/media/MediaCodec", "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, dequeue_output_buffer_id), 1 },
        { "android/media/MediaCodec", "getOutputBuffer", "(I)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_output_buffer_id), 0 },
        { "android/media/MediaCodec", "getOutputBuffers", "()[Ljava/nio/ByteBuffer;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, get_output_buffers_id), 1 },
        { "android/media/MediaCodec", "releaseOutputBuffer", "(IZ)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, release_output_buffer_id), 1 },
        { "android/media/MediaCodec", "releaseOutputBuffer", "(IJ)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, release_output_buffer_at_time_id), 0 },

    { "android/media/MediaCodec$BufferInfo", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecFields, mediainfo_class), 1 },

        { "android/media/MediaCodec.BufferInfo", "<init>", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecFields, init_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "flags", "I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecFields, flags_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "offset", "I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecFields, offset_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "presentationTimeUs", "J", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecFields, presentation_time_us_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "size", "I", FF_JNI_FIELD, offsetof(struct JNIAMediaCodecFields, size_id), 1 },

    { NULL }
};

static const AVClass amediacodec_class = {
    .class_name = "amediacodec",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

struct FFAMediaCodec {

    const AVClass *class;

    struct JNIAMediaCodecFields jfields;

    jobject object;
    jobject buffer_info;

    jobject input_buffers;
    jobject output_buffers;

    int INFO_TRY_AGAIN_LATER;
    int INFO_OUTPUT_BUFFERS_CHANGED;
    int INFO_OUTPUT_FORMAT_CHANGED;

    int BUFFER_FLAG_CODEC_CONFIG;
    int BUFFER_FLAG_END_OF_STREAM;
    int BUFFER_FLAG_KEY_FRAME;

    int CONFIGURE_FLAG_ENCODE;

    int has_get_i_o_buffer;
};

#define JNI_GET_ENV_OR_RETURN(env, log_ctx, ret) do {              \
    (env) = ff_jni_get_env(log_ctx);                               \
    if (!(env)) {                                                  \
        return ret;                                                \
    }                                                              \
} while (0)

#define JNI_GET_ENV_OR_RETURN_VOID(env, log_ctx) do {              \
    (env) = ff_jni_get_env(log_ctx);                               \
    if (!(env)) {                                                  \
        return;                                                    \
    }                                                              \
} while (0)

int ff_AMediaCodecProfile_getProfileFromAVCodecContext(AVCodecContext *avctx)
{
    int ret = -1;

    JNIEnv *env = NULL;
    struct JNIAMediaCodecListFields jfields = { 0 };
    jfieldID field_id = 0;

    JNI_GET_ENV_OR_RETURN(env, avctx, -1);

    if (ff_jni_init_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, avctx) < 0) {
        goto done;
    }

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        switch(avctx->profile) {
        case FF_PROFILE_H264_BASELINE:
        case FF_PROFILE_H264_CONSTRAINED_BASELINE:
            field_id = jfields.avc_profile_baseline_id;
            break;
        case FF_PROFILE_H264_MAIN:
            field_id = jfields.avc_profile_main_id;
            break;
        case FF_PROFILE_H264_EXTENDED:
            field_id = jfields.avc_profile_extended_id;
            break;
        case FF_PROFILE_H264_HIGH:
            field_id = jfields.avc_profile_high_id;
            break;
        case FF_PROFILE_H264_HIGH_10:
        case FF_PROFILE_H264_HIGH_10_INTRA:
            field_id = jfields.avc_profile_high10_id;
            break;
        case FF_PROFILE_H264_HIGH_422:
        case FF_PROFILE_H264_HIGH_422_INTRA:
            field_id = jfields.avc_profile_high422_id;
            break;
        case FF_PROFILE_H264_HIGH_444:
        case FF_PROFILE_H264_HIGH_444_INTRA:
        case FF_PROFILE_H264_HIGH_444_PREDICTIVE:
            field_id = jfields.avc_profile_high444_id;
            break;
        }
    } else if (avctx->codec_id == AV_CODEC_ID_HEVC) {
        switch (avctx->profile) {
        case FF_PROFILE_HEVC_MAIN:
        case FF_PROFILE_HEVC_MAIN_STILL_PICTURE:
            field_id = jfields.hevc_profile_main_id;
            break;
        case FF_PROFILE_HEVC_MAIN_10:
            field_id = jfields.hevc_profile_main10_id;
            break;
        }
    }

        if (field_id) {
            ret = (*env)->GetStaticIntField(env, jfields.codec_profile_level_class, field_id);
            if (ff_jni_exception_check(env, 1, avctx) < 0) {
                ret = -1;
                goto done;
            }
        }

done:
    ff_jni_reset_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, avctx);

    return ret;
}

char *ff_AMediaCodecList_getCodecNameByType(const char *mime, int profile, int encoder, void *log_ctx)
{
    int ret;
    int i;
    int codec_count;
    int found_codec = 0;
    char *name = NULL;
    char *supported_type = NULL;

    JNIEnv *env = NULL;
    struct JNIAMediaCodecListFields jfields = { 0 };
    struct JNIAMediaFormatFields mediaformat_jfields = { 0 };

    jobject codec_name = NULL;

    jobject info = NULL;
    jobject type = NULL;
    jobjectArray types = NULL;

    jobject capabilities = NULL;
    jobject profile_level = NULL;
    jobjectArray profile_levels = NULL;

    JNI_GET_ENV_OR_RETURN(env, log_ctx, NULL);

    if ((ret = ff_jni_init_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, log_ctx)) < 0) {
        goto done;
    }

    if ((ret = ff_jni_init_jfields(env, &mediaformat_jfields, jni_amediaformat_mapping, 0, log_ctx)) < 0) {
        goto done;
    }

    codec_count = (*env)->CallStaticIntMethod(env, jfields.mediacodec_list_class, jfields.get_codec_count_id);
    if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
        goto done;
    }

    for(i = 0; i < codec_count; i++) {
        int j;
        int type_count;
        int is_encoder;

        info = (*env)->CallStaticObjectMethod(env, jfields.mediacodec_list_class, jfields.get_codec_info_at_id, i);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        types = (*env)->CallObjectMethod(env, info, jfields.get_supported_types_id);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        is_encoder = (*env)->CallBooleanMethod(env, info, jfields.is_encoder_id);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        if (is_encoder != encoder) {
            goto done_with_info;
        }

        if (jfields.is_software_only_id) {
            int is_software_only = (*env)->CallBooleanMethod(env, info, jfields.is_software_only_id);
            if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                goto done;
            }

            if (is_software_only) {
                goto done_with_info;
            }
        }

        codec_name = (*env)->CallObjectMethod(env, info, jfields.get_name_id);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        name = ff_jni_jstring_to_utf_chars(env, codec_name, log_ctx);
        if (!name) {
            goto done;
        }

        if (codec_name) {
            (*env)->DeleteLocalRef(env, codec_name);
            codec_name = NULL;
        }

        /* Skip software decoders */
        if (
            strstr(name, "OMX.google") ||
            strstr(name, "OMX.ffmpeg") ||
            (strstr(name, "OMX.SEC") && strstr(name, ".sw.")) ||
            !strcmp(name, "OMX.qcom.video.decoder.hevcswvdec")) {
            goto done_with_info;
        }

        type_count = (*env)->GetArrayLength(env, types);
        for (j = 0; j < type_count; j++) {
            int k;
            int profile_count;

            type = (*env)->GetObjectArrayElement(env, types, j);
            if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                goto done;
            }

            supported_type = ff_jni_jstring_to_utf_chars(env, type, log_ctx);
            if (!supported_type) {
                goto done;
            }

            if (av_strcasecmp(supported_type, mime)) {
                goto done_with_type;
            }

            capabilities = (*env)->CallObjectMethod(env, info, jfields.get_codec_capabilities_id, type);
            if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                goto done;
            }

            profile_levels = (*env)->GetObjectField(env, capabilities, jfields.profile_levels_id);
            if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                goto done;
            }

            profile_count = (*env)->GetArrayLength(env, profile_levels);
            if (!profile_count) {
                found_codec = 1;
            }
            for (k = 0; k < profile_count; k++) {
                int supported_profile = 0;

                if (profile < 0) {
                    found_codec = 1;
                    break;
                }

                profile_level = (*env)->GetObjectArrayElement(env, profile_levels, k);
                if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                    goto done;
                }

                supported_profile = (*env)->GetIntField(env, profile_level, jfields.profile_id);
                if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                    goto done;
                }

                found_codec = profile == supported_profile;

                if (profile_level) {
                    (*env)->DeleteLocalRef(env, profile_level);
                    profile_level = NULL;
                }

                if (found_codec) {
                    break;
                }
            }

done_with_type:
            if (profile_levels) {
                (*env)->DeleteLocalRef(env, profile_levels);
                profile_levels = NULL;
            }

            if (capabilities) {
                (*env)->DeleteLocalRef(env, capabilities);
                capabilities = NULL;
            }

            if (type) {
                (*env)->DeleteLocalRef(env, type);
                type = NULL;
            }

            av_freep(&supported_type);

            if (found_codec) {
                break;
            }
        }

done_with_info:
        if (info) {
            (*env)->DeleteLocalRef(env, info);
            info = NULL;
        }

        if (types) {
            (*env)->DeleteLocalRef(env, types);
            types = NULL;
        }

        if (found_codec) {
            break;
        }

        av_freep(&name);
    }

done:
    if (codec_name) {
        (*env)->DeleteLocalRef(env, codec_name);
    }

    if (info) {
        (*env)->DeleteLocalRef(env, info);
    }

    if (type) {
        (*env)->DeleteLocalRef(env, type);
    }

    if (types) {
        (*env)->DeleteLocalRef(env, types);
    }

    if (capabilities) {
        (*env)->DeleteLocalRef(env, capabilities);
    }

    if (profile_level) {
        (*env)->DeleteLocalRef(env, profile_level);
    }

    if (profile_levels) {
        (*env)->DeleteLocalRef(env, profile_levels);
    }

    av_freep(&supported_type);

    ff_jni_reset_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, log_ctx);
    ff_jni_reset_jfields(env, &mediaformat_jfields, jni_amediaformat_mapping, 0, log_ctx);

    if (!found_codec) {
        av_freep(&name);
    }

    return name;
}

FFAMediaFormat *ff_AMediaFormat_new(void)
{
    JNIEnv *env = NULL;
    FFAMediaFormat *format = NULL;
    jobject object = NULL;

    format = av_mallocz(sizeof(FFAMediaFormat));
    if (!format) {
        return NULL;
    }
    format->class = &amediaformat_class;

    env = ff_jni_get_env(format);
    if (!env) {
        av_freep(&format);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format) < 0) {
        goto fail;
    }

    object = (*env)->NewObject(env, format->jfields.mediaformat_class, format->jfields.init_id);
    if (!object) {
        goto fail;
    }

    format->object = (*env)->NewGlobalRef(env, object);
    if (!format->object) {
        goto fail;
    }

fail:
    if (object) {
        (*env)->DeleteLocalRef(env, object);
    }

    if (!format->object) {
        ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);
        av_freep(&format);
    }

    return format;
}

static FFAMediaFormat *ff_AMediaFormat_newFromObject(void *object)
{
    JNIEnv *env = NULL;
    FFAMediaFormat *format = NULL;

    format = av_mallocz(sizeof(FFAMediaFormat));
    if (!format) {
        return NULL;
    }
    format->class = &amediaformat_class;

    env = ff_jni_get_env(format);
    if (!env) {
        av_freep(&format);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format) < 0) {
        goto fail;
    }

    format->object = (*env)->NewGlobalRef(env, object);
    if (!format->object) {
        goto fail;
    }

    return format;
fail:
    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    av_freep(&format);

    return NULL;
}

int ff_AMediaFormat_delete(FFAMediaFormat* format)
{
    int ret = 0;

    JNIEnv *env = NULL;

    if (!format) {
        return 0;
    }

    JNI_GET_ENV_OR_RETURN(env, format, AVERROR_EXTERNAL);

    (*env)->DeleteGlobalRef(env, format->object);
    format->object = NULL;

    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    av_freep(&format);

    return ret;
}

char* ff_AMediaFormat_toString(FFAMediaFormat* format)
{
    char *ret = NULL;

    JNIEnv *env = NULL;
    jstring description = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, NULL);

    description = (*env)->CallObjectMethod(env, format->object, format->jfields.to_string_id);
    if (ff_jni_exception_check(env, 1, NULL) < 0) {
        goto fail;
    }

    ret = ff_jni_jstring_to_utf_chars(env, description, format);
fail:
    if (description) {
        (*env)->DeleteLocalRef(env, description);
    }

    return ret;
}

int ff_AMediaFormat_getInt32(FFAMediaFormat* format, const char *name, int32_t *out)
{
    int ret = 1;

    JNIEnv *env = NULL;
    jstring key = NULL;
    jboolean contains_key;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        ret = 0;
        goto fail;
    }

    contains_key = (*env)->CallBooleanMethod(env, format->object, format->jfields.contains_key_id, key);
    if (!contains_key || (ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    *out = (*env)->CallIntMethod(env, format->object, format->jfields.get_integer_id, key);
    if ((ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    ret = 1;
fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    return ret;
}

int ff_AMediaFormat_getInt64(FFAMediaFormat* format, const char *name, int64_t *out)
{
    int ret = 1;

    JNIEnv *env = NULL;
    jstring key = NULL;
    jboolean contains_key;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        ret = 0;
        goto fail;
    }

    contains_key = (*env)->CallBooleanMethod(env, format->object, format->jfields.contains_key_id, key);
    if (!contains_key || (ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    *out = (*env)->CallLongMethod(env, format->object, format->jfields.get_long_id, key);
    if ((ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    ret = 1;
fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    return ret;
}

int ff_AMediaFormat_getFloat(FFAMediaFormat* format, const char *name, float *out)
{
    int ret = 1;

    JNIEnv *env = NULL;
    jstring key = NULL;
    jboolean contains_key;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        ret = 0;
        goto fail;
    }

    contains_key = (*env)->CallBooleanMethod(env, format->object, format->jfields.contains_key_id, key);
    if (!contains_key || (ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    *out = (*env)->CallFloatMethod(env, format->object, format->jfields.get_float_id, key);
    if ((ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    ret = 1;
fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    return ret;
}

int ff_AMediaFormat_getBuffer(FFAMediaFormat* format, const char *name, void** data, size_t *size)
{
    int ret = 1;

    JNIEnv *env = NULL;
    jstring key = NULL;
    jboolean contains_key;
    jobject result = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        ret = 0;
        goto fail;
    }

    contains_key = (*env)->CallBooleanMethod(env, format->object, format->jfields.contains_key_id, key);
    if (!contains_key || (ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    result = (*env)->CallObjectMethod(env, format->object, format->jfields.get_bytebuffer_id, key);
    if ((ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    *data = (*env)->GetDirectBufferAddress(env, result);
    *size = (*env)->GetDirectBufferCapacity(env, result);

    if (*data && *size) {
        void *src = *data;
        *data = av_malloc(*size);
        if (!*data) {
            ret = 0;
            goto fail;
        }

        memcpy(*data, src, *size);
    }

    ret = 1;
fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }

    return ret;
}

int ff_AMediaFormat_getString(FFAMediaFormat* format, const char *name, const char **out)
{
    int ret = 1;

    JNIEnv *env = NULL;
    jstring key = NULL;
    jboolean contains_key;
    jstring result = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN(env, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        ret = 0;
        goto fail;
    }

    contains_key = (*env)->CallBooleanMethod(env, format->object, format->jfields.contains_key_id, key);
    if (!contains_key || (ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    result = (*env)->CallObjectMethod(env, format->object, format->jfields.get_string_id, key);
    if ((ret = ff_jni_exception_check(env, 1, format)) < 0) {
        ret = 0;
        goto fail;
    }

    *out = ff_jni_jstring_to_utf_chars(env, result, format);
    if (!*out) {
        ret = 0;
        goto fail;
    }

    ret = 1;
fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    if (result) {
        (*env)->DeleteLocalRef(env, result);
    }

    return ret;
}

void ff_AMediaFormat_setInt32(FFAMediaFormat* format, const char* name, int32_t value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN_VOID(env, format);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        goto fail;
    }

    (*env)->CallVoidMethod(env, format->object, format->jfields.set_integer_id, key, value);
    if (ff_jni_exception_check(env, 1, format) < 0) {
        goto fail;
    }

fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }
}

void ff_AMediaFormat_setInt64(FFAMediaFormat* format, const char* name, int64_t value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN_VOID(env, format);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        goto fail;
    }

    (*env)->CallVoidMethod(env, format->object, format->jfields.set_long_id, key, value);
    if (ff_jni_exception_check(env, 1, format) < 0) {
        goto fail;
    }

fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }
}

void ff_AMediaFormat_setFloat(FFAMediaFormat* format, const char* name, float value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN_VOID(env, format);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        goto fail;
    }

    (*env)->CallVoidMethod(env, format->object, format->jfields.set_float_id, key, value);
    if (ff_jni_exception_check(env, 1, format) < 0) {
        goto fail;
    }

fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }
}

void ff_AMediaFormat_setString(FFAMediaFormat* format, const char* name, const char* value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    jstring string = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN_VOID(env, format);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        goto fail;
    }

    string = ff_jni_utf_chars_to_jstring(env, value, format);
    if (!string) {
        goto fail;
    }

    (*env)->CallVoidMethod(env, format->object, format->jfields.set_string_id, key, string);
    if (ff_jni_exception_check(env, 1, format) < 0) {
        goto fail;
    }

fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    if (string) {
        (*env)->DeleteLocalRef(env, string);
    }
}

void ff_AMediaFormat_setBuffer(FFAMediaFormat* format, const char* name, void* data, size_t size)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    jobject buffer = NULL;
    void *buffer_data = NULL;

    av_assert0(format != NULL);

    JNI_GET_ENV_OR_RETURN_VOID(env, format);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
        goto fail;
    }

    if (!data || !size) {
        goto fail;
    }

    buffer_data = av_malloc(size);
    if (!buffer_data) {
        goto fail;
    }

    memcpy(buffer_data, data, size);

    buffer = (*env)->NewDirectByteBuffer(env, buffer_data, size);
    if (!buffer) {
        goto fail;
    }

    (*env)->CallVoidMethod(env, format->object, format->jfields.set_bytebuffer_id, key, buffer);
    if (ff_jni_exception_check(env, 1, format) < 0) {
        goto fail;
    }

fail:
    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    if (buffer) {
        (*env)->DeleteLocalRef(env, buffer);
    }
}

static int codec_init_static_fields(FFAMediaCodec *codec)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    codec->INFO_TRY_AGAIN_LATER = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.info_try_again_later_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    codec->BUFFER_FLAG_CODEC_CONFIG = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.buffer_flag_codec_config_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    codec->BUFFER_FLAG_END_OF_STREAM = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.buffer_flag_end_of_stream_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    if (codec->jfields.buffer_flag_key_frame_id) {
        codec->BUFFER_FLAG_KEY_FRAME = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.buffer_flag_key_frame_id);
        if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
            goto fail;
        }
    }

    codec->CONFIGURE_FLAG_ENCODE = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.configure_flag_encode_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    codec->INFO_TRY_AGAIN_LATER = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.info_try_again_later_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    codec->INFO_OUTPUT_BUFFERS_CHANGED = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.info_output_buffers_changed_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

    codec->INFO_OUTPUT_FORMAT_CHANGED = (*env)->GetStaticIntField(env, codec->jfields.mediacodec_class, codec->jfields.info_output_format_changed_id);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        goto fail;
    }

fail:

    return ret;
}

#define CREATE_CODEC_BY_NAME   0
#define CREATE_DECODER_BY_TYPE 1
#define CREATE_ENCODER_BY_TYPE 2

static inline FFAMediaCodec *codec_create(int method, const char *arg)
{
    int ret = -1;
    JNIEnv *env = NULL;
    FFAMediaCodec *codec = NULL;
    jstring jarg = NULL;
    jobject object = NULL;
    jobject buffer_info = NULL;
    jmethodID create_id = NULL;

    codec = av_mallocz(sizeof(FFAMediaCodec));
    if (!codec) {
        return NULL;
    }
    codec->class = &amediacodec_class;

    env = ff_jni_get_env(codec);
    if (!env) {
        av_freep(&codec);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec) < 0) {
        goto fail;
    }

    jarg = ff_jni_utf_chars_to_jstring(env, arg, codec);
    if (!jarg) {
        goto fail;
    }

    switch (method) {
    case CREATE_CODEC_BY_NAME:   create_id = codec->jfields.create_by_codec_name_id;   break;
    case CREATE_DECODER_BY_TYPE: create_id = codec->jfields.create_decoder_by_type_id; break;
    case CREATE_ENCODER_BY_TYPE: create_id = codec->jfields.create_encoder_by_type_id; break;
    default:
        av_assert0(0);
    }

    object = (*env)->CallStaticObjectMethod(env,
                                            codec->jfields.mediacodec_class,
                                            create_id,
                                            jarg);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    codec->object = (*env)->NewGlobalRef(env, object);
    if (!codec->object) {
        goto fail;
    }

    if (codec_init_static_fields(codec) < 0) {
        goto fail;
    }

    if (codec->jfields.get_input_buffer_id && codec->jfields.get_output_buffer_id) {
        codec->has_get_i_o_buffer = 1;
    }

    buffer_info = (*env)->NewObject(env, codec->jfields.mediainfo_class, codec->jfields.init_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    codec->buffer_info = (*env)->NewGlobalRef(env, buffer_info);
    if (!codec->buffer_info) {
        goto fail;
    }

    ret = 0;
fail:
    if (jarg) {
        (*env)->DeleteLocalRef(env, jarg);
    }

    if (object) {
        (*env)->DeleteLocalRef(env, object);
    }

    if (buffer_info) {
        (*env)->DeleteLocalRef(env, buffer_info);
    }

    if (ret < 0) {
        if (codec->object) {
            (*env)->DeleteGlobalRef(env, codec->object);
        }

        if (codec->buffer_info) {
            (*env)->DeleteGlobalRef(env, codec->buffer_info);
        }

        ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);
        av_freep(&codec);
    }

    return codec;
}

#define DECLARE_FF_AMEDIACODEC_CREATE_FUNC(name, method) \
FFAMediaCodec *ff_AMediaCodec_##name(const char *arg)    \
{                                                        \
    return codec_create(method, arg);                    \
}                                                        \

DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createCodecByName,   CREATE_CODEC_BY_NAME)
DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createDecoderByType, CREATE_DECODER_BY_TYPE)
DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createEncoderByType, CREATE_ENCODER_BY_TYPE)

int ff_AMediaCodec_delete(FFAMediaCodec* codec)
{
    int ret = 0;

    JNIEnv *env = NULL;

    if (!codec) {
        return 0;
    }

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
    }

    (*env)->DeleteGlobalRef(env, codec->input_buffers);
    codec->input_buffers = NULL;

    (*env)->DeleteGlobalRef(env, codec->output_buffers);
    codec->output_buffers = NULL;

    (*env)->DeleteGlobalRef(env, codec->object);
    codec->object = NULL;

    (*env)->DeleteGlobalRef(env, codec->buffer_info);
    codec->buffer_info = NULL;

    ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);

    av_freep(&codec);

    return ret;
}

char *ff_AMediaCodec_getName(FFAMediaCodec *codec)
{
    char *ret = NULL;
    JNIEnv *env = NULL;
    jobject *name = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, NULL);

    name = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_name_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    ret = ff_jni_jstring_to_utf_chars(env, name, codec);

fail:
    if (name) {
        (*env)->DeleteLocalRef(env, name);
    }

    return ret;
}

int ff_AMediaCodec_configure(FFAMediaCodec* codec, const FFAMediaFormat* format, void* surface, void *crypto, uint32_t flags)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.configure_id, format->object, surface, NULL, flags);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_start(FFAMediaCodec* codec)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.start_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_stop(FFAMediaCodec* codec)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.stop_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_flush(FFAMediaCodec* codec)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.flush_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_releaseOutputBuffer(FFAMediaCodec* codec, size_t idx, int render)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_id, (jint)idx, (jboolean)render);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_releaseOutputBufferAtTime(FFAMediaCodec *codec, size_t idx, int64_t timestampNs)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_at_time_id, (jint)idx, (jlong)timestampNs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

ssize_t ff_AMediaCodec_dequeueInputBuffer(FFAMediaCodec* codec, int64_t timeoutUs)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    ret = (*env)->CallIntMethod(env, codec->object, codec->jfields.dequeue_input_buffer_id, timeoutUs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

int ff_AMediaCodec_queueInputBuffer(FFAMediaCodec* codec, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.queue_input_buffer_id, (jint)idx, (jint)offset, (jint)size, time, flags);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

ssize_t ff_AMediaCodec_dequeueOutputBuffer(FFAMediaCodec* codec, FFAMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    int ret = 0;
    JNIEnv *env = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    ret = (*env)->CallIntMethod(env, codec->object, codec->jfields.dequeue_output_buffer_id, codec->buffer_info, timeoutUs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    info->flags = (*env)->GetIntField(env, codec->buffer_info, codec->jfields.flags_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    info->offset = (*env)->GetIntField(env, codec->buffer_info, codec->jfields.offset_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    info->presentationTimeUs = (*env)->GetLongField(env, codec->buffer_info, codec->jfields.presentation_time_us_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    info->size = (*env)->GetIntField(env, codec->buffer_info, codec->jfields.size_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    return ret;
}

uint8_t* ff_AMediaCodec_getInputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    JNIEnv *env = NULL;

    jobject buffer = NULL;
    jobject input_buffers = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, NULL);

    if (codec->has_get_i_o_buffer) {
        buffer = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_input_buffer_id, (jint)idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    } else {
        if (!codec->input_buffers) {
            input_buffers = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_input_buffers_id);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }

            codec->input_buffers = (*env)->NewGlobalRef(env, input_buffers);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }
        }

        buffer = (*env)->GetObjectArrayElement(env, codec->input_buffers, idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    }

    ret = (*env)->GetDirectBufferAddress(env, buffer);
    *out_size = (*env)->GetDirectBufferCapacity(env, buffer);
fail:
    if (buffer) {
        (*env)->DeleteLocalRef(env, buffer);
    }

    if (input_buffers) {
        (*env)->DeleteLocalRef(env, input_buffers);
    }

    return ret;
}

uint8_t* ff_AMediaCodec_getOutputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    JNIEnv *env = NULL;

    jobject buffer = NULL;
    jobject output_buffers = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, NULL);

    if (codec->has_get_i_o_buffer) {
        buffer = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_buffer_id, (jint)idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    } else {
        if (!codec->output_buffers) {
            output_buffers = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_buffers_id);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }

            codec->output_buffers = (*env)->NewGlobalRef(env, output_buffers);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }
        }

        buffer = (*env)->GetObjectArrayElement(env, codec->output_buffers, idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    }

    ret = (*env)->GetDirectBufferAddress(env, buffer);
    *out_size = (*env)->GetDirectBufferCapacity(env, buffer);
fail:
    if (buffer) {
        (*env)->DeleteLocalRef(env, buffer);
    }

    if (output_buffers) {
        (*env)->DeleteLocalRef(env, output_buffers);
    }

    return ret;
}

FFAMediaFormat* ff_AMediaCodec_getOutputFormat(FFAMediaCodec* codec)
{
    FFAMediaFormat *ret = NULL;
    JNIEnv *env = NULL;

    jobject mediaformat = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, NULL);

    mediaformat = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_format_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    ret = ff_AMediaFormat_newFromObject(mediaformat);
fail:
    if (mediaformat) {
        (*env)->DeleteLocalRef(env, mediaformat);
    }

    return ret;
}

int ff_AMediaCodec_infoTryAgainLater(FFAMediaCodec *codec, ssize_t idx)
{
    return idx == codec->INFO_TRY_AGAIN_LATER;
}

int ff_AMediaCodec_infoOutputBuffersChanged(FFAMediaCodec *codec, ssize_t idx)
{
    return idx == codec->INFO_OUTPUT_BUFFERS_CHANGED;
}

int ff_AMediaCodec_infoOutputFormatChanged(FFAMediaCodec *codec, ssize_t idx)
{
    return idx == codec->INFO_OUTPUT_FORMAT_CHANGED;
}

int ff_AMediaCodec_getBufferFlagCodecConfig(FFAMediaCodec *codec)
{
    return codec->BUFFER_FLAG_CODEC_CONFIG;
}

int ff_AMediaCodec_getBufferFlagEndOfStream(FFAMediaCodec *codec)
{
    return codec->BUFFER_FLAG_END_OF_STREAM;
}

int ff_AMediaCodec_getBufferFlagKeyFrame(FFAMediaCodec *codec)
{
    return codec->BUFFER_FLAG_KEY_FRAME;
}

int ff_AMediaCodec_getConfigureFlagEncode(FFAMediaCodec *codec)
{
    return codec->CONFIGURE_FLAG_ENCODE;
}

int ff_AMediaCodec_cleanOutputBuffers(FFAMediaCodec *codec)
{
    int ret = 0;

    if (!codec->has_get_i_o_buffer) {
        if (codec->output_buffers) {
            JNIEnv *env = NULL;

            env = ff_jni_get_env(codec);
            if (!env) {
                ret = AVERROR_EXTERNAL;
                goto fail;
            }

            (*env)->DeleteGlobalRef(env, codec->output_buffers);
            codec->output_buffers = NULL;
        }
    }

fail:
    return ret;
}

int ff_Build_SDK_INT(AVCodecContext *avctx)
{
    int ret = -1;
    JNIEnv *env = NULL;
    jclass versionClass;
    jfieldID sdkIntFieldID;
    JNI_GET_ENV_OR_RETURN(env, avctx, -1);

    versionClass = (*env)->FindClass(env, "android/os/Build$VERSION");
    sdkIntFieldID = (*env)->GetStaticFieldID(env, versionClass, "SDK_INT", "I");
    ret = (*env)->GetStaticIntField(env, versionClass, sdkIntFieldID);
    (*env)->DeleteLocalRef(env, versionClass);
    return ret;
}
