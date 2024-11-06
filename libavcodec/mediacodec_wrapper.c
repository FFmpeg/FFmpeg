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

#include <dlfcn.h>
#include <jni.h>
#include <stdbool.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaCodec.h>
#include <android/native_window_jni.h>

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
};

#define OFFSET(x) offsetof(struct JNIAMediaCodecListFields, x)
static const struct FFJniField jni_amediacodeclist_mapping[] = {
    { "android/media/MediaCodecList", NULL, NULL, FF_JNI_CLASS, OFFSET(mediacodec_list_class), 1 },
        { "android/media/MediaCodecList", "<init>", "(I)V", FF_JNI_METHOD, OFFSET(init_id), 0 },
        { "android/media/MediaCodecList", "findDecoderForFormat", "(Landroid/media/MediaFormat;)Ljava/lang/String;", FF_JNI_METHOD, OFFSET(find_decoder_for_format_id), 0 },

        { "android/media/MediaCodecList", "getCodecCount", "()I", FF_JNI_STATIC_METHOD, OFFSET(get_codec_count_id), 1 },
        { "android/media/MediaCodecList", "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;", FF_JNI_STATIC_METHOD, OFFSET(get_codec_info_at_id), 1 },

    { "android/media/MediaCodecInfo", NULL, NULL, FF_JNI_CLASS, OFFSET(mediacodec_info_class), 1 },
        { "android/media/MediaCodecInfo", "getName", "()Ljava/lang/String;", FF_JNI_METHOD, OFFSET(get_name_id), 1 },
        { "android/media/MediaCodecInfo", "getCapabilitiesForType", "(Ljava/lang/String;)Landroid/media/MediaCodecInfo$CodecCapabilities;", FF_JNI_METHOD, OFFSET(get_codec_capabilities_id), 1 },
        { "android/media/MediaCodecInfo", "getSupportedTypes", "()[Ljava/lang/String;", FF_JNI_METHOD, OFFSET(get_supported_types_id), 1 },
        { "android/media/MediaCodecInfo", "isEncoder", "()Z", FF_JNI_METHOD, OFFSET(is_encoder_id), 1 },
        { "android/media/MediaCodecInfo", "isSoftwareOnly", "()Z", FF_JNI_METHOD, OFFSET(is_software_only_id), 0 },

    { "android/media/MediaCodecInfo$CodecCapabilities", NULL, NULL, FF_JNI_CLASS, OFFSET(codec_capabilities_class), 1 },
        { "android/media/MediaCodecInfo$CodecCapabilities", "colorFormats", "[I", FF_JNI_FIELD, OFFSET(color_formats_id), 1 },
        { "android/media/MediaCodecInfo$CodecCapabilities", "profileLevels", "[Landroid/media/MediaCodecInfo$CodecProfileLevel;", FF_JNI_FIELD, OFFSET(profile_levels_id), 1 },

    { "android/media/MediaCodecInfo$CodecProfileLevel", NULL, NULL, FF_JNI_CLASS, OFFSET(codec_profile_level_class), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "profile", "I", FF_JNI_FIELD, OFFSET(profile_id), 1 },
        { "android/media/MediaCodecInfo$CodecProfileLevel", "level", "I", FF_JNI_FIELD, OFFSET(level_id), 1 },

    { NULL }
};
#undef OFFSET

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

#define OFFSET(x) offsetof(struct JNIAMediaFormatFields, x)
static const struct FFJniField jni_amediaformat_mapping[] = {
    { "android/media/MediaFormat", NULL, NULL, FF_JNI_CLASS, OFFSET(mediaformat_class), 1 },

        { "android/media/MediaFormat", "<init>", "()V", FF_JNI_METHOD, OFFSET(init_id), 1 },

        { "android/media/MediaFormat", "containsKey", "(Ljava/lang/String;)Z", FF_JNI_METHOD, OFFSET(contains_key_id), 1 },

        { "android/media/MediaFormat", "getInteger", "(Ljava/lang/String;)I", FF_JNI_METHOD, OFFSET(get_integer_id), 1 },
        { "android/media/MediaFormat", "getLong", "(Ljava/lang/String;)J", FF_JNI_METHOD, OFFSET(get_long_id), 1 },
        { "android/media/MediaFormat", "getFloat", "(Ljava/lang/String;)F", FF_JNI_METHOD, OFFSET(get_float_id), 1 },
        { "android/media/MediaFormat", "getByteBuffer", "(Ljava/lang/String;)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, OFFSET(get_bytebuffer_id), 1 },
        { "android/media/MediaFormat", "getString", "(Ljava/lang/String;)Ljava/lang/String;", FF_JNI_METHOD, OFFSET(get_string_id), 1 },

        { "android/media/MediaFormat", "setInteger", "(Ljava/lang/String;I)V", FF_JNI_METHOD, OFFSET(set_integer_id), 1 },
        { "android/media/MediaFormat", "setLong", "(Ljava/lang/String;J)V", FF_JNI_METHOD, OFFSET(set_long_id), 1 },
        { "android/media/MediaFormat", "setFloat", "(Ljava/lang/String;F)V", FF_JNI_METHOD, OFFSET(set_float_id), 1 },
        { "android/media/MediaFormat", "setByteBuffer", "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V", FF_JNI_METHOD, OFFSET(set_bytebuffer_id), 1 },
        { "android/media/MediaFormat", "setString", "(Ljava/lang/String;Ljava/lang/String;)V", FF_JNI_METHOD, OFFSET(set_string_id), 1 },

        { "android/media/MediaFormat", "toString", "()Ljava/lang/String;", FF_JNI_METHOD, OFFSET(to_string_id), 1 },

    { NULL }
};
#undef OFFSET

static const AVClass amediaformat_class = {
    .class_name = "amediaformat",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

typedef struct FFAMediaFormatJni {
    FFAMediaFormat api;

    struct JNIAMediaFormatFields jfields;
    jobject object;
} FFAMediaFormatJni;

static const FFAMediaFormat media_format_jni;

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

    jmethodID set_input_surface_id;
    jmethodID signal_end_of_input_stream_id;

    jclass mediainfo_class;

    jmethodID init_id;

    jfieldID flags_id;
    jfieldID offset_id;
    jfieldID presentation_time_us_id;
    jfieldID size_id;

};

#define OFFSET(x) offsetof(struct JNIAMediaCodecFields, x)
static const struct FFJniField jni_amediacodec_mapping[] = {
    { "android/media/MediaCodec", NULL, NULL, FF_JNI_CLASS, OFFSET(mediacodec_class), 1 },

        { "android/media/MediaCodec", "INFO_TRY_AGAIN_LATER", "I", FF_JNI_STATIC_FIELD, OFFSET(info_try_again_later_id), 1 },
        { "android/media/MediaCodec", "INFO_OUTPUT_BUFFERS_CHANGED", "I", FF_JNI_STATIC_FIELD, OFFSET(info_output_buffers_changed_id), 1 },
        { "android/media/MediaCodec", "INFO_OUTPUT_FORMAT_CHANGED", "I", FF_JNI_STATIC_FIELD, OFFSET(info_output_format_changed_id), 1 },

        { "android/media/MediaCodec", "BUFFER_FLAG_CODEC_CONFIG", "I", FF_JNI_STATIC_FIELD, OFFSET(buffer_flag_codec_config_id), 1 },
        { "android/media/MediaCodec", "BUFFER_FLAG_END_OF_STREAM", "I", FF_JNI_STATIC_FIELD, OFFSET(buffer_flag_end_of_stream_id), 1 },
        { "android/media/MediaCodec", "BUFFER_FLAG_KEY_FRAME", "I", FF_JNI_STATIC_FIELD, OFFSET(buffer_flag_key_frame_id), 0 },

        { "android/media/MediaCodec", "CONFIGURE_FLAG_ENCODE", "I", FF_JNI_STATIC_FIELD, OFFSET(configure_flag_encode_id), 1 },

        { "android/media/MediaCodec", "createByCodecName", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, OFFSET(create_by_codec_name_id), 1 },
        { "android/media/MediaCodec", "createDecoderByType", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, OFFSET(create_decoder_by_type_id), 1 },
        { "android/media/MediaCodec", "createEncoderByType", "(Ljava/lang/String;)Landroid/media/MediaCodec;", FF_JNI_STATIC_METHOD, OFFSET(create_encoder_by_type_id), 1 },

        { "android/media/MediaCodec", "getName", "()Ljava/lang/String;", FF_JNI_METHOD, OFFSET(get_name_id), 1 },

        { "android/media/MediaCodec", "configure", "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V", FF_JNI_METHOD, OFFSET(configure_id), 1 },
        { "android/media/MediaCodec", "start", "()V", FF_JNI_METHOD, OFFSET(start_id), 1 },
        { "android/media/MediaCodec", "flush", "()V", FF_JNI_METHOD, OFFSET(flush_id), 1 },
        { "android/media/MediaCodec", "stop", "()V", FF_JNI_METHOD, OFFSET(stop_id), 1 },
        { "android/media/MediaCodec", "release", "()V", FF_JNI_METHOD, OFFSET(release_id), 1 },

        { "android/media/MediaCodec", "getOutputFormat", "()Landroid/media/MediaFormat;", FF_JNI_METHOD, OFFSET(get_output_format_id), 1 },

        { "android/media/MediaCodec", "dequeueInputBuffer", "(J)I", FF_JNI_METHOD, OFFSET(dequeue_input_buffer_id), 1 },
        { "android/media/MediaCodec", "queueInputBuffer", "(IIIJI)V", FF_JNI_METHOD, OFFSET(queue_input_buffer_id), 1 },
        { "android/media/MediaCodec", "getInputBuffer", "(I)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, OFFSET(get_input_buffer_id), 0 },
        { "android/media/MediaCodec", "getInputBuffers", "()[Ljava/nio/ByteBuffer;", FF_JNI_METHOD, OFFSET(get_input_buffers_id), 1 },

        { "android/media/MediaCodec", "dequeueOutputBuffer", "(Landroid/media/MediaCodec$BufferInfo;J)I", FF_JNI_METHOD, OFFSET(dequeue_output_buffer_id), 1 },
        { "android/media/MediaCodec", "getOutputBuffer", "(I)Ljava/nio/ByteBuffer;", FF_JNI_METHOD, OFFSET(get_output_buffer_id), 0 },
        { "android/media/MediaCodec", "getOutputBuffers", "()[Ljava/nio/ByteBuffer;", FF_JNI_METHOD, OFFSET(get_output_buffers_id), 1 },
        { "android/media/MediaCodec", "releaseOutputBuffer", "(IZ)V", FF_JNI_METHOD, OFFSET(release_output_buffer_id), 1 },
        { "android/media/MediaCodec", "releaseOutputBuffer", "(IJ)V", FF_JNI_METHOD, OFFSET(release_output_buffer_at_time_id), 0 },

        { "android/media/MediaCodec", "setInputSurface", "(Landroid/view/Surface;)V", FF_JNI_METHOD, OFFSET(set_input_surface_id), 0 },
        { "android/media/MediaCodec", "signalEndOfInputStream", "()V", FF_JNI_METHOD, OFFSET(signal_end_of_input_stream_id), 0 },

    { "android/media/MediaCodec$BufferInfo", NULL, NULL, FF_JNI_CLASS, OFFSET(mediainfo_class), 1 },

        { "android/media/MediaCodec.BufferInfo", "<init>", "()V", FF_JNI_METHOD, OFFSET(init_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "flags", "I", FF_JNI_FIELD, OFFSET(flags_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "offset", "I", FF_JNI_FIELD, OFFSET(offset_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "presentationTimeUs", "J", FF_JNI_FIELD, OFFSET(presentation_time_us_id), 1 },
        { "android/media/MediaCodec.BufferInfo", "size", "I", FF_JNI_FIELD, OFFSET(size_id), 1 },

    { NULL }
};
#undef OFFSET

static const AVClass amediacodec_class = {
    .class_name = "amediacodec",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

typedef struct FFAMediaCodecJni {
    FFAMediaCodec api;

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
} FFAMediaCodecJni;

static const FFAMediaCodec media_codec_jni;

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
    // Copy and modified from MediaCodecInfo.java
    static const int AVCProfileBaseline = 0x01;
    static const int AVCProfileMain     = 0x02;
    static const int AVCProfileExtended = 0x04;
    static const int AVCProfileHigh     = 0x08;
    static const int AVCProfileHigh10   = 0x10;
    static const int AVCProfileHigh422  = 0x20;
    static const int AVCProfileHigh444  = 0x40;
    static const int AVCProfileConstrainedBaseline = 0x10000;
    static const int AVCProfileConstrainedHigh     = 0x80000;

    static const int HEVCProfileMain        = 0x01;
    static const int HEVCProfileMain10      = 0x02;
    static const int HEVCProfileMainStill   = 0x04;
    static const int HEVCProfileMain10HDR10 = 0x1000;
    static const int HEVCProfileMain10HDR10Plus = 0x2000;

    static const int VP9Profile0 = 0x01;
    static const int VP9Profile1 = 0x02;
    static const int VP9Profile2 = 0x04;
    static const int VP9Profile3 = 0x08;
    static const int VP9Profile2HDR = 0x1000;
    static const int VP9Profile3HDR = 0x2000;
    static const int VP9Profile2HDR10Plus = 0x4000;
    static const int VP9Profile3HDR10Plus = 0x8000;

    static const int MPEG4ProfileSimple           = 0x01;
    static const int MPEG4ProfileSimpleScalable   = 0x02;
    static const int MPEG4ProfileCore             = 0x04;
    static const int MPEG4ProfileMain             = 0x08;
    static const int MPEG4ProfileNbit             = 0x10;
    static const int MPEG4ProfileScalableTexture  = 0x20;
    static const int MPEG4ProfileSimpleFBA        = 0x80;
    static const int MPEG4ProfileSimpleFace       = 0x40;
    static const int MPEG4ProfileBasicAnimated    = 0x100;
    static const int MPEG4ProfileHybrid           = 0x200;
    static const int MPEG4ProfileAdvancedRealTime = 0x400;
    static const int MPEG4ProfileCoreScalable     = 0x800;
    static const int MPEG4ProfileAdvancedCoding   = 0x1000;
    static const int MPEG4ProfileAdvancedCore     = 0x2000;
    static const int MPEG4ProfileAdvancedScalable = 0x4000;
    static const int MPEG4ProfileAdvancedSimple   = 0x8000;


    static const int AV1ProfileMain8  = 0x1;
    static const int AV1ProfileMain10 = 0x2;
    static const int AV1ProfileMain10HDR10     = 0x1000;
    static const int AV1ProfileMain10HDR10Plus = 0x2000;

    // Unused yet.
    (void)AVCProfileConstrainedHigh;
    (void)HEVCProfileMain10HDR10;
    (void)HEVCProfileMain10HDR10Plus;
    (void)VP9Profile2HDR;
    (void)VP9Profile3HDR;
    (void)VP9Profile2HDR10Plus;
    (void)VP9Profile3HDR10Plus;
    (void)MPEG4ProfileSimpleFace;
    (void)AV1ProfileMain10;
    (void)AV1ProfileMain10HDR10;
    (void)AV1ProfileMain10HDR10Plus;

    if (avctx->codec_id == AV_CODEC_ID_H264) {
        switch(avctx->profile) {
        case AV_PROFILE_H264_BASELINE:
            return AVCProfileBaseline;
        case AV_PROFILE_H264_CONSTRAINED_BASELINE:
            return AVCProfileConstrainedBaseline;
        case AV_PROFILE_H264_MAIN:
            return AVCProfileMain;
            break;
        case AV_PROFILE_H264_EXTENDED:
            return AVCProfileExtended;
        case AV_PROFILE_H264_HIGH:
            return AVCProfileHigh;
        case AV_PROFILE_H264_HIGH_10:
        case AV_PROFILE_H264_HIGH_10_INTRA:
            return AVCProfileHigh10;
        case AV_PROFILE_H264_HIGH_422:
        case AV_PROFILE_H264_HIGH_422_INTRA:
            return AVCProfileHigh422;
        case AV_PROFILE_H264_HIGH_444:
        case AV_PROFILE_H264_HIGH_444_INTRA:
        case AV_PROFILE_H264_HIGH_444_PREDICTIVE:
            return AVCProfileHigh444;
        }
    } else if (avctx->codec_id == AV_CODEC_ID_HEVC) {
        switch (avctx->profile) {
        case AV_PROFILE_HEVC_MAIN:
            return HEVCProfileMain;
        case AV_PROFILE_HEVC_MAIN_STILL_PICTURE:
            return HEVCProfileMainStill;
        case AV_PROFILE_HEVC_MAIN_10:
            return HEVCProfileMain10;
        }
    } else if (avctx->codec_id == AV_CODEC_ID_VP9) {
        switch (avctx->profile) {
        case AV_PROFILE_VP9_0:
            return VP9Profile0;
        case AV_PROFILE_VP9_1:
            return VP9Profile1;
        case AV_PROFILE_VP9_2:
            return VP9Profile2;
         case AV_PROFILE_VP9_3:
            return VP9Profile3;
        }
    } else if(avctx->codec_id == AV_CODEC_ID_MPEG4) {
        switch (avctx->profile)
        {
        case AV_PROFILE_MPEG4_SIMPLE:
            return MPEG4ProfileSimple;
        case AV_PROFILE_MPEG4_SIMPLE_SCALABLE:
            return MPEG4ProfileSimpleScalable;
        case AV_PROFILE_MPEG4_CORE:
            return MPEG4ProfileCore;
        case AV_PROFILE_MPEG4_MAIN:
            return MPEG4ProfileMain;
        case AV_PROFILE_MPEG4_N_BIT:
            return MPEG4ProfileNbit;
        case AV_PROFILE_MPEG4_SCALABLE_TEXTURE:
            return MPEG4ProfileScalableTexture;
        case AV_PROFILE_MPEG4_SIMPLE_FACE_ANIMATION:
            return MPEG4ProfileSimpleFBA;
        case AV_PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE:
            return MPEG4ProfileBasicAnimated;
        case AV_PROFILE_MPEG4_HYBRID:
            return MPEG4ProfileHybrid;
        case AV_PROFILE_MPEG4_ADVANCED_REAL_TIME:
            return MPEG4ProfileAdvancedRealTime;
        case AV_PROFILE_MPEG4_CORE_SCALABLE:
            return MPEG4ProfileCoreScalable;
        case AV_PROFILE_MPEG4_ADVANCED_CODING:
            return MPEG4ProfileAdvancedCoding;
        case AV_PROFILE_MPEG4_ADVANCED_CORE:
            return MPEG4ProfileAdvancedCore;
        case AV_PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE:
            return MPEG4ProfileAdvancedScalable;
        case AV_PROFILE_MPEG4_ADVANCED_SIMPLE:
            return MPEG4ProfileAdvancedSimple;
        case AV_PROFILE_MPEG4_SIMPLE_STUDIO:
            // Studio profiles are not supported by mediacodec.
        default:
            break;
        }
    } else if(avctx->codec_id == AV_CODEC_ID_AV1) {
        switch (avctx->profile)
        {
        case AV_PROFILE_AV1_MAIN:
            return AV1ProfileMain8;
        case AV_PROFILE_AV1_HIGH:
        case AV_PROFILE_AV1_PROFESSIONAL:
        default:
            break;
        }
    }

    return -1;
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

        (*env)->DeleteLocalRef(env, codec_name);
        codec_name = NULL;

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

                (*env)->DeleteLocalRef(env, profile_level);
                profile_level = NULL;

                if (found_codec) {
                    break;
                }
            }

done_with_type:
            (*env)->DeleteLocalRef(env, profile_levels);
            profile_levels = NULL;

            (*env)->DeleteLocalRef(env, capabilities);
            capabilities = NULL;

            (*env)->DeleteLocalRef(env, type);
            type = NULL;

            av_freep(&supported_type);

            if (found_codec) {
                break;
            }
        }

done_with_info:
        (*env)->DeleteLocalRef(env, info);
        info = NULL;

        (*env)->DeleteLocalRef(env, types);
        types = NULL;

        if (found_codec) {
            break;
        }

        av_freep(&name);
    }

done:
    (*env)->DeleteLocalRef(env, codec_name);
    (*env)->DeleteLocalRef(env, info);
    (*env)->DeleteLocalRef(env, type);
    (*env)->DeleteLocalRef(env, types);
    (*env)->DeleteLocalRef(env, capabilities);
    (*env)->DeleteLocalRef(env, profile_level);
    (*env)->DeleteLocalRef(env, profile_levels);

    av_freep(&supported_type);

    ff_jni_reset_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, log_ctx);
    ff_jni_reset_jfields(env, &mediaformat_jfields, jni_amediaformat_mapping, 0, log_ctx);

    if (!found_codec) {
        av_freep(&name);
    }

    return name;
}

static FFAMediaFormat *mediaformat_jni_new(void)
{
    JNIEnv *env = NULL;
    FFAMediaFormatJni *format = NULL;
    jobject object = NULL;

    format = av_mallocz(sizeof(*format));
    if (!format) {
        return NULL;
    }
    format->api = media_format_jni;

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
    (*env)->DeleteLocalRef(env, object);

    if (!format->object) {
        ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);
        av_freep(&format);
    }

    return (FFAMediaFormat *)format;
}

static FFAMediaFormat *mediaformat_jni_newFromObject(void *object)
{
    JNIEnv *env = NULL;
    FFAMediaFormatJni *format = NULL;

    format = av_mallocz(sizeof(*format));
    if (!format) {
        return NULL;
    }
    format->api = media_format_jni;

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

    return (FFAMediaFormat *)format;
fail:
    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    av_freep(&format);

    return NULL;
}

static int mediaformat_jni_delete(FFAMediaFormat* ctx)
{
    int ret = 0;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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

static char* mediaformat_jni_toString(FFAMediaFormat* ctx)
{
    char *ret = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, description);

    return ret;
}

static int mediaformat_jni_getInt32(FFAMediaFormat* ctx, const char *name, int32_t *out)
{
    int ret = 1;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, key);

    return ret;
}

static int mediaformat_jni_getInt64(FFAMediaFormat* ctx, const char *name, int64_t *out)
{
    int ret = 1;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, key);

    return ret;
}

static int mediaformat_jni_getFloat(FFAMediaFormat* ctx, const char *name, float *out)
{
    int ret = 1;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, key);

    return ret;
}

static int mediaformat_jni_getBuffer(FFAMediaFormat* ctx, const char *name, void** data, size_t *size)
{
    int ret = 1;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, key);
    (*env)->DeleteLocalRef(env, result);

    return ret;
}

static int mediaformat_jni_getString(FFAMediaFormat* ctx, const char *name, const char **out)
{
    int ret = 1;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;
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
    (*env)->DeleteLocalRef(env, key);
    (*env)->DeleteLocalRef(env, result);

    return ret;
}

static void mediaformat_jni_setInt32(FFAMediaFormat* ctx, const char* name, int32_t value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;

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
    (*env)->DeleteLocalRef(env, key);
}

static void mediaformat_jni_setInt64(FFAMediaFormat* ctx, const char* name, int64_t value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;

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
    (*env)->DeleteLocalRef(env, key);
}

static void mediaformat_jni_setFloat(FFAMediaFormat* ctx, const char* name, float value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;

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
    (*env)->DeleteLocalRef(env, key);
}

static void mediaformat_jni_setString(FFAMediaFormat* ctx, const char* name, const char* value)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    jstring string = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;

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
    (*env)->DeleteLocalRef(env, key);
    (*env)->DeleteLocalRef(env, string);
}

static void mediaformat_jni_setBuffer(FFAMediaFormat* ctx, const char* name, void* data, size_t size)
{
    JNIEnv *env = NULL;
    jstring key = NULL;
    jobject buffer = NULL;
    void *buffer_data = NULL;
    FFAMediaFormatJni *format = (FFAMediaFormatJni *)ctx;

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
    (*env)->DeleteLocalRef(env, key);
    (*env)->DeleteLocalRef(env, buffer);
}

static int codec_init_static_fields(FFAMediaCodecJni *codec)
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
    FFAMediaCodecJni *codec = NULL;
    jstring jarg = NULL;
    jobject object = NULL;
    jobject buffer_info = NULL;
    jmethodID create_id = NULL;

    codec = av_mallocz(sizeof(*codec));
    if (!codec) {
        return NULL;
    }
    codec->api = media_codec_jni;

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
    (*env)->DeleteLocalRef(env, jarg);
    (*env)->DeleteLocalRef(env, object);
    (*env)->DeleteLocalRef(env, buffer_info);

    if (ret < 0) {
        (*env)->DeleteGlobalRef(env, codec->object);
        (*env)->DeleteGlobalRef(env, codec->buffer_info);

        ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);
        av_freep(&codec);
    }

    return (FFAMediaCodec *)codec;
}

#define DECLARE_FF_AMEDIACODEC_CREATE_FUNC(name, method) \
static FFAMediaCodec *mediacodec_jni_##name(const char *arg)    \
{                                                        \
    return codec_create(method, arg);                    \
}                                                        \

DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createCodecByName,   CREATE_CODEC_BY_NAME)
DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createDecoderByType, CREATE_DECODER_BY_TYPE)
DECLARE_FF_AMEDIACODEC_CREATE_FUNC(createEncoderByType, CREATE_ENCODER_BY_TYPE)

static int mediacodec_jni_delete(FFAMediaCodec* ctx)
{
    int ret = 0;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
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

static char *mediacodec_jni_getName(FFAMediaCodec *ctx)
{
    char *ret = NULL;
    JNIEnv *env = NULL;
    jobject *name = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

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

static int mediacodec_jni_configure(FFAMediaCodec *ctx,
                                    const FFAMediaFormat* format_ctx,
                                    FFANativeWindow* window,
                                    void *crypto,
                                    uint32_t flags)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    const FFAMediaFormatJni *format = (FFAMediaFormatJni *)format_ctx;
    jobject *surface = window ? window->surface : NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    if (flags & codec->CONFIGURE_FLAG_ENCODE) {
        if (surface && !codec->jfields.set_input_surface_id) {
            av_log(ctx, AV_LOG_ERROR, "System doesn't support setInputSurface\n");
            return AVERROR_EXTERNAL;
        }

        (*env)->CallVoidMethod(env, codec->object, codec->jfields.configure_id, format->object, NULL, NULL, flags);
        if (ff_jni_exception_check(env, 1, codec) < 0)
            return AVERROR_EXTERNAL;

        if (!surface)
            return 0;

        (*env)->CallVoidMethod(env, codec->object, codec->jfields.set_input_surface_id, surface);
        if (ff_jni_exception_check(env, 1, codec) < 0)
            return AVERROR_EXTERNAL;
        return 0;
    } else {
        (*env)->CallVoidMethod(env, codec->object, codec->jfields.configure_id, format->object, surface, NULL, flags);
    }
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_start(FFAMediaCodec* ctx)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.start_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_stop(FFAMediaCodec* ctx)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.stop_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_flush(FFAMediaCodec* ctx)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.flush_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_releaseOutputBuffer(FFAMediaCodec* ctx, size_t idx, int render)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_id, (jint)idx, (jboolean)render);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_releaseOutputBufferAtTime(FFAMediaCodec *ctx, size_t idx, int64_t timestampNs)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_at_time_id, (jint)idx, (jlong)timestampNs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static ssize_t mediacodec_jni_dequeueInputBuffer(FFAMediaCodec* ctx, int64_t timeoutUs)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    ret = (*env)->CallIntMethod(env, codec->object, codec->jfields.dequeue_input_buffer_id, timeoutUs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static int mediacodec_jni_queueInputBuffer(FFAMediaCodec* ctx, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.queue_input_buffer_id, (jint)idx, (jint)offset, (jint)size, time, flags);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    return ret;
}

static ssize_t mediacodec_jni_dequeueOutputBuffer(FFAMediaCodec* ctx, FFAMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    int ret = 0;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

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

static uint8_t* mediacodec_jni_getInputBuffer(FFAMediaCodec* ctx, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
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
    (*env)->DeleteLocalRef(env, buffer);
    (*env)->DeleteLocalRef(env, input_buffers);

    return ret;
}

static uint8_t* mediacodec_jni_getOutputBuffer(FFAMediaCodec* ctx, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
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
    (*env)->DeleteLocalRef(env, buffer);
    (*env)->DeleteLocalRef(env, output_buffers);

    return ret;
}

static FFAMediaFormat* mediacodec_jni_getOutputFormat(FFAMediaCodec* ctx)
{
    FFAMediaFormat *ret = NULL;
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    jobject mediaformat = NULL;

    JNI_GET_ENV_OR_RETURN(env, codec, NULL);

    mediaformat = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_format_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    ret = mediaformat_jni_newFromObject(mediaformat);
fail:
    (*env)->DeleteLocalRef(env, mediaformat);

    return ret;
}

static int mediacodec_jni_infoTryAgainLater(FFAMediaCodec *ctx, ssize_t idx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return idx == codec->INFO_TRY_AGAIN_LATER;
}

static int mediacodec_jni_infoOutputBuffersChanged(FFAMediaCodec *ctx, ssize_t idx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return idx == codec->INFO_OUTPUT_BUFFERS_CHANGED;
}

static int mediacodec_jni_infoOutputFormatChanged(FFAMediaCodec *ctx, ssize_t idx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return idx == codec->INFO_OUTPUT_FORMAT_CHANGED;
}

static int mediacodec_jni_getBufferFlagCodecConfig(FFAMediaCodec *ctx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return codec->BUFFER_FLAG_CODEC_CONFIG;
}

static int mediacodec_jni_getBufferFlagEndOfStream(FFAMediaCodec *ctx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return codec->BUFFER_FLAG_END_OF_STREAM;
}

static int mediacodec_jni_getBufferFlagKeyFrame(FFAMediaCodec *ctx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return codec->BUFFER_FLAG_KEY_FRAME;
}

static int mediacodec_jni_getConfigureFlagEncode(FFAMediaCodec *ctx)
{
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;
    return codec->CONFIGURE_FLAG_ENCODE;
}

static int mediacodec_jni_cleanOutputBuffers(FFAMediaCodec *ctx)
{
    int ret = 0;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

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

static int mediacodec_jni_signalEndOfInputStream(FFAMediaCodec *ctx)
{
    JNIEnv *env = NULL;
    FFAMediaCodecJni *codec = (FFAMediaCodecJni *)ctx;

    JNI_GET_ENV_OR_RETURN(env, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.signal_end_of_input_stream_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int mediacodec_jni_setAsyncNotifyCallback(FFAMediaCodec *codec,
                                                 const FFAMediaCodecOnAsyncNotifyCallback *callback,
                                                 void *userdata)
{
    av_log(codec, AV_LOG_ERROR, "Doesn't support aync mode with JNI, please try ndk_codec=1\n");
    return AVERROR(ENOSYS);
}

static const FFAMediaFormat media_format_jni = {
    .class = &amediaformat_class,

    .create = mediaformat_jni_new,
    .delete = mediaformat_jni_delete,

    .toString = mediaformat_jni_toString,

    .getInt32 = mediaformat_jni_getInt32,
    .getInt64 = mediaformat_jni_getInt64,
    .getFloat = mediaformat_jni_getFloat,
    .getBuffer = mediaformat_jni_getBuffer,
    .getString = mediaformat_jni_getString,

    .setInt32 = mediaformat_jni_setInt32,
    .setInt64 = mediaformat_jni_setInt64,
    .setFloat = mediaformat_jni_setFloat,
    .setString = mediaformat_jni_setString,
    .setBuffer = mediaformat_jni_setBuffer,
};

static const FFAMediaCodec media_codec_jni = {
    .class = &amediacodec_class,

    .getName = mediacodec_jni_getName,

    .createCodecByName = mediacodec_jni_createCodecByName,
    .createDecoderByType = mediacodec_jni_createDecoderByType,
    .createEncoderByType = mediacodec_jni_createEncoderByType,
    .delete = mediacodec_jni_delete,

    .configure = mediacodec_jni_configure,
    .start = mediacodec_jni_start,
    .stop = mediacodec_jni_stop,
    .flush = mediacodec_jni_flush,

    .getInputBuffer = mediacodec_jni_getInputBuffer,
    .getOutputBuffer = mediacodec_jni_getOutputBuffer,

    .dequeueInputBuffer = mediacodec_jni_dequeueInputBuffer,
    .queueInputBuffer = mediacodec_jni_queueInputBuffer,

    .dequeueOutputBuffer = mediacodec_jni_dequeueOutputBuffer,
    .getOutputFormat = mediacodec_jni_getOutputFormat,

    .releaseOutputBuffer = mediacodec_jni_releaseOutputBuffer,
    .releaseOutputBufferAtTime = mediacodec_jni_releaseOutputBufferAtTime,

    .infoTryAgainLater = mediacodec_jni_infoTryAgainLater,
    .infoOutputBuffersChanged = mediacodec_jni_infoOutputBuffersChanged,
    .infoOutputFormatChanged = mediacodec_jni_infoOutputFormatChanged,

    .getBufferFlagCodecConfig = mediacodec_jni_getBufferFlagCodecConfig,
    .getBufferFlagEndOfStream = mediacodec_jni_getBufferFlagEndOfStream,
    .getBufferFlagKeyFrame = mediacodec_jni_getBufferFlagKeyFrame,

    .getConfigureFlagEncode = mediacodec_jni_getConfigureFlagEncode,
    .cleanOutputBuffers = mediacodec_jni_cleanOutputBuffers,
    .signalEndOfInputStream = mediacodec_jni_signalEndOfInputStream,
    .setAsyncNotifyCallback = mediacodec_jni_setAsyncNotifyCallback,
};

typedef struct FFAMediaFormatNdk {
    FFAMediaFormat api;

    void *libmedia;
    AMediaFormat *impl;

    bool (*getRect)(AMediaFormat *, const char *name,
                    int32_t *left, int32_t *top, int32_t *right, int32_t *bottom);
    void (*setRect)(AMediaFormat *, const char *name,
                    int32_t left, int32_t top, int32_t right, int32_t bottom);
} FFAMediaFormatNdk;

typedef struct FFAMediaCodecNdk {
    FFAMediaCodec api;

    void *libmedia;
    AMediaCodec *impl;
    ANativeWindow *window;

    FFAMediaCodecOnAsyncNotifyCallback async_cb;
    void *async_userdata;

    // Available since API level 28.
    media_status_t (*getName)(AMediaCodec*, char** out_name);
    void (*releaseName)(AMediaCodec*, char* name);

    // Available since API level 26.
    media_status_t (*setInputSurface)(AMediaCodec*, ANativeWindow *);
    media_status_t (*signalEndOfInputStream)(AMediaCodec *);
    media_status_t (*setAsyncNotifyCallback)(AMediaCodec *,
            struct AMediaCodecOnAsyncNotifyCallback callback, void *userdata);
} FFAMediaCodecNdk;

static const FFAMediaFormat media_format_ndk;
static const FFAMediaCodec media_codec_ndk;

static const AVClass amediaformat_ndk_class = {
    .class_name = "amediaformat_ndk",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass amediacodec_ndk_class = {
    .class_name = "amediacodec_ndk",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int media_status_to_error(media_status_t status)
{
    switch (status) {
    case AMEDIA_OK:
        return 0;
    case AMEDIACODEC_ERROR_INSUFFICIENT_RESOURCE:
        return AVERROR(ENOMEM);
    case AMEDIA_ERROR_MALFORMED:
        return AVERROR_INVALIDDATA;
    case AMEDIA_ERROR_UNSUPPORTED:
        return AVERROR(ENOTSUP);
    case AMEDIA_ERROR_INVALID_PARAMETER:
        return AVERROR(EINVAL);
    case AMEDIA_ERROR_INVALID_OPERATION:
        return AVERROR(EOPNOTSUPP);
    case AMEDIA_ERROR_END_OF_STREAM:
        return AVERROR_EOF;
    case AMEDIA_ERROR_IO:
        return AVERROR(EIO);
    case AMEDIA_ERROR_WOULD_BLOCK:
        return AVERROR(EWOULDBLOCK);
    default:
        return AVERROR_EXTERNAL;
    }
}

static FFAMediaFormat *mediaformat_ndk_create(AMediaFormat *impl)
{
    FFAMediaFormatNdk *format = av_mallocz(sizeof(*format));
    if (!format)
        return NULL;

    format->api = media_format_ndk;

    format->libmedia = dlopen("libmediandk.so", RTLD_NOW);
    if (!format->libmedia)
        goto error;

#define GET_OPTIONAL_SYMBOL(sym) \
    format->sym = dlsym(format->libmedia, "AMediaFormat_" #sym);

    GET_OPTIONAL_SYMBOL(getRect)
    GET_OPTIONAL_SYMBOL(setRect)

#undef GET_OPTIONAL_SYMBOL

    if (impl) {
        format->impl = impl;
    } else {
        format->impl = AMediaFormat_new();
        if (!format->impl)
            goto error;
    }

    return (FFAMediaFormat *)format;

error:
    if (format->libmedia)
        dlclose(format->libmedia);
    av_freep(&format);
    return NULL;
}

static FFAMediaFormat *mediaformat_ndk_new(void)
{
    return mediaformat_ndk_create(NULL);
}

static int mediaformat_ndk_delete(FFAMediaFormat* ctx)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    int ret = 0;
    if (!format)
        return 0;

    av_assert0(format->api.class == &amediaformat_ndk_class);

    if (format->impl && (AMediaFormat_delete(format->impl) != AMEDIA_OK))
            ret = AVERROR_EXTERNAL;
    if (format->libmedia)
        dlclose(format->libmedia);
    av_free(format);

    return ret;
}

static char* mediaformat_ndk_toString(FFAMediaFormat* ctx)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    const char *str = AMediaFormat_toString(format->impl);
    return av_strdup(str);
}

static int mediaformat_ndk_getInt32(FFAMediaFormat* ctx, const char *name, int32_t *out)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    return AMediaFormat_getInt32(format->impl, name, out);
}

static int mediaformat_ndk_getInt64(FFAMediaFormat* ctx, const char *name, int64_t *out)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    return AMediaFormat_getInt64(format->impl, name, out);
}

static int mediaformat_ndk_getFloat(FFAMediaFormat* ctx, const char *name, float *out)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    return AMediaFormat_getFloat(format->impl, name, out);
}

static int mediaformat_ndk_getBuffer(FFAMediaFormat* ctx, const char *name, void** data, size_t *size)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    return AMediaFormat_getBuffer(format->impl, name, data, size);
}

static int mediaformat_ndk_getString(FFAMediaFormat* ctx, const char *name, const char **out)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    const char *tmp = NULL;
    int ret = AMediaFormat_getString(format->impl, name, &tmp);

    if (tmp)
        *out = av_strdup(tmp);
    return ret;
}

static int mediaformat_ndk_getRect(FFAMediaFormat *ctx, const char *name,
                                   int32_t *left, int32_t *top, int32_t *right, int32_t *bottom)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    if (!format->getRect)
        return AVERROR_EXTERNAL;
    return format->getRect(format->impl, name, left, top, right, bottom);
}

static void mediaformat_ndk_setInt32(FFAMediaFormat* ctx, const char* name, int32_t value)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    AMediaFormat_setInt32(format->impl, name, value);
}

static void mediaformat_ndk_setInt64(FFAMediaFormat* ctx, const char* name, int64_t value)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    AMediaFormat_setInt64(format->impl, name, value);
}

static void mediaformat_ndk_setFloat(FFAMediaFormat* ctx, const char* name, float value)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    AMediaFormat_setFloat(format->impl, name, value);
}

static void mediaformat_ndk_setString(FFAMediaFormat* ctx, const char* name, const char* value)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    AMediaFormat_setString(format->impl, name, value);
}

static void mediaformat_ndk_setBuffer(FFAMediaFormat* ctx, const char* name, void* data, size_t size)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    AMediaFormat_setBuffer(format->impl, name, data, size);
}

static void mediaformat_ndk_setRect(FFAMediaFormat *ctx, const char *name,
                                     int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)ctx;
    if (!format->setRect) {
        av_log(ctx, AV_LOG_WARNING, "Doesn't support setRect\n");
        return;
    }
    format->setRect(format->impl, name, left, top, right, bottom);
}

static char *mediacodec_ndk_getName(FFAMediaCodec *ctx)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    char *ret = NULL;
    char *name = NULL;

    if (!codec->getName || !codec->releaseName) {
        av_log(ctx, AV_LOG_DEBUG, "getName() unavailable\n");
        return ret;
    }

    codec->getName(codec->impl, &name);
    if (name) {
        ret = av_strdup(name);
        codec->releaseName(codec->impl, name);
    }

    return ret;
}

static inline FFAMediaCodec *ndk_codec_create(int method, const char *arg) {
    FFAMediaCodecNdk *codec = av_mallocz(sizeof(*codec));
    const char *lib_name = "libmediandk.so";

    if (!codec)
        return NULL;

    codec->api = media_codec_ndk;
    codec->libmedia = dlopen(lib_name, RTLD_NOW);
    if (!codec->libmedia)
        goto error;

#define GET_SYMBOL(sym)                                             \
    codec->sym = dlsym(codec->libmedia, "AMediaCodec_" #sym);       \
    if (!codec->sym)                                                \
        av_log(codec, AV_LOG_INFO, #sym "() unavailable from %s\n", lib_name);

    GET_SYMBOL(getName)
    GET_SYMBOL(releaseName)

    GET_SYMBOL(setInputSurface)
    GET_SYMBOL(signalEndOfInputStream)
    GET_SYMBOL(setAsyncNotifyCallback)

#undef GET_SYMBOL

    switch (method) {
    case CREATE_CODEC_BY_NAME:
        codec->impl = AMediaCodec_createCodecByName(arg);
        break;
    case CREATE_DECODER_BY_TYPE:
        codec->impl = AMediaCodec_createDecoderByType(arg);
        break;
    case CREATE_ENCODER_BY_TYPE:
        codec->impl = AMediaCodec_createEncoderByType(arg);
        break;
    default:
        av_assert0(0);
    }
    if (!codec->impl)
        goto error;

    return (FFAMediaCodec *)codec;

error:
    if (codec->libmedia)
        dlclose(codec->libmedia);
    av_freep(&codec);
    return NULL;
}

#define DECLARE_NDK_AMEDIACODEC_CREATE_FUNC(name, method)       \
static FFAMediaCodec *mediacodec_ndk_##name(const char *arg)    \
{                                                               \
    return ndk_codec_create(method, arg);                       \
}                                                               \

DECLARE_NDK_AMEDIACODEC_CREATE_FUNC(createCodecByName,   CREATE_CODEC_BY_NAME)
DECLARE_NDK_AMEDIACODEC_CREATE_FUNC(createDecoderByType, CREATE_DECODER_BY_TYPE)
DECLARE_NDK_AMEDIACODEC_CREATE_FUNC(createEncoderByType, CREATE_ENCODER_BY_TYPE)

static int mediacodec_ndk_delete(FFAMediaCodec* ctx)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    int ret = 0;

    if (!codec)
        return 0;

    av_assert0(codec->api.class == &amediacodec_ndk_class);

    if (codec->impl && (AMediaCodec_delete(codec->impl) != AMEDIA_OK))
        ret = AVERROR_EXTERNAL;
    if (codec->window)
        ANativeWindow_release(codec->window);
    if (codec->libmedia)
        dlclose(codec->libmedia);
    av_free(codec);

    return ret;
}

static int mediacodec_ndk_configure(FFAMediaCodec* ctx,
                                    const FFAMediaFormat* format_ctx,
                                    FFANativeWindow* window,
                                    void *crypto,
                                    uint32_t flags)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    FFAMediaFormatNdk *format = (FFAMediaFormatNdk *)format_ctx;
    media_status_t status;
    ANativeWindow *native_window = NULL;

    if (window) {
        if (window->surface) {
            JNIEnv *env = NULL;
            JNI_GET_ENV_OR_RETURN(env, ctx, -1);
            native_window = ANativeWindow_fromSurface(env, window->surface);
            // Save for release
            codec->window = native_window;
        } else if (window->native_window) {
            native_window = window->native_window;
        }
    }

    if (format_ctx->class != &amediaformat_ndk_class) {
        av_log(ctx, AV_LOG_ERROR, "invalid media format\n");
        return AVERROR(EINVAL);
    }

    if (flags & AMEDIACODEC_CONFIGURE_FLAG_ENCODE) {
        if (native_window && !codec->setInputSurface) {
            av_log(ctx, AV_LOG_ERROR, "System doesn't support setInputSurface\n");
            return AVERROR_EXTERNAL;
        }

        status = AMediaCodec_configure(codec->impl, format->impl, NULL, NULL, flags);
        if (status != AMEDIA_OK) {
            av_log(codec, AV_LOG_ERROR, "Encoder configure failed, %d\n", status);
            return AVERROR_EXTERNAL;
        }

        if (!native_window)
            return 0;

        status = codec->setInputSurface(codec->impl, native_window);
        if (status != AMEDIA_OK) {
            av_log(codec, AV_LOG_ERROR, "Encoder set input surface failed, %d\n", status);
            return AVERROR_EXTERNAL;
        }
    } else {
        status = AMediaCodec_configure(codec->impl, format->impl, native_window, NULL, flags);
        if (status != AMEDIA_OK) {
            av_log(codec, AV_LOG_ERROR, "Decoder configure failed, %d\n", status);
            return AVERROR_EXTERNAL;
        }
    }

    return 0;
}

#define MEDIACODEC_NDK_WRAPPER(method)                                   \
static int mediacodec_ndk_ ## method(FFAMediaCodec* ctx)                 \
{                                                                        \
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;                   \
    media_status_t status = AMediaCodec_ ## method (codec->impl);                  \
                                                                         \
    if (status != AMEDIA_OK) {                                           \
        av_log(codec, AV_LOG_ERROR, #method " failed, %d\n", status);    \
        return AVERROR_EXTERNAL;                                         \
    }                                                                    \
                                                                         \
    return 0;                                                            \
}                                                                        \

MEDIACODEC_NDK_WRAPPER(start)
MEDIACODEC_NDK_WRAPPER(stop)
MEDIACODEC_NDK_WRAPPER(flush)

static uint8_t* mediacodec_ndk_getInputBuffer(FFAMediaCodec* ctx, size_t idx, size_t *out_size)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    return AMediaCodec_getInputBuffer(codec->impl, idx, out_size);
}

static uint8_t* mediacodec_ndk_getOutputBuffer(FFAMediaCodec* ctx, size_t idx, size_t *out_size)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    return AMediaCodec_getOutputBuffer(codec->impl, idx, out_size);
}

static ssize_t mediacodec_ndk_dequeueInputBuffer(FFAMediaCodec* ctx, int64_t timeoutUs)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    return AMediaCodec_dequeueInputBuffer(codec->impl, timeoutUs);
}

static int mediacodec_ndk_queueInputBuffer(FFAMediaCodec *ctx, size_t idx,
                                           off_t offset, size_t size,
                                           uint64_t time, uint32_t flags)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    return AMediaCodec_queueInputBuffer(codec->impl, idx, offset, size, time, flags);
}

static ssize_t mediacodec_ndk_dequeueOutputBuffer(FFAMediaCodec* ctx, FFAMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    AMediaCodecBufferInfo buf_info = {0};
    ssize_t ret;

    ret = AMediaCodec_dequeueOutputBuffer(codec->impl, &buf_info, timeoutUs);
    info->offset = buf_info.offset;
    info->size = buf_info.size;
    info->presentationTimeUs = buf_info.presentationTimeUs;
    info->flags = buf_info.flags;

    return ret;
}

static FFAMediaFormat* mediacodec_ndk_getOutputFormat(FFAMediaCodec* ctx)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    AMediaFormat *format = AMediaCodec_getOutputFormat(codec->impl);

    if (!format)
        return NULL;
    return mediaformat_ndk_create(format);
}

static int mediacodec_ndk_releaseOutputBuffer(FFAMediaCodec* ctx, size_t idx, int render)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    media_status_t status;

    status = AMediaCodec_releaseOutputBuffer(codec->impl, idx, render);
    if (status != AMEDIA_OK) {
        av_log(codec, AV_LOG_ERROR, "release output buffer failed, %d\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int mediacodec_ndk_releaseOutputBufferAtTime(FFAMediaCodec *ctx, size_t idx, int64_t timestampNs)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    media_status_t status;

    status = AMediaCodec_releaseOutputBufferAtTime(codec->impl, idx, timestampNs);
    if (status != AMEDIA_OK) {
        av_log(codec, AV_LOG_ERROR, "releaseOutputBufferAtTime failed, %d\n", status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int mediacodec_ndk_infoTryAgainLater(FFAMediaCodec *ctx, ssize_t idx)
{
    return idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER;
}

static int mediacodec_ndk_infoOutputBuffersChanged(FFAMediaCodec *ctx, ssize_t idx)
{
    return idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED;
}

static int mediacodec_ndk_infoOutputFormatChanged(FFAMediaCodec *ctx, ssize_t idx)
{
    return idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;
}

static int mediacodec_ndk_getBufferFlagCodecConfig(FFAMediaCodec *ctx)
{
    return AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG;
}

static int mediacodec_ndk_getBufferFlagEndOfStream(FFAMediaCodec *ctx)
{
    return AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
}

static int mediacodec_ndk_getBufferFlagKeyFrame(FFAMediaCodec *ctx)
{
    return 1;
}

static int mediacodec_ndk_getConfigureFlagEncode(FFAMediaCodec *ctx)
{
    return AMEDIACODEC_CONFIGURE_FLAG_ENCODE;
}

static int mediacodec_ndk_cleanOutputBuffers(FFAMediaCodec *ctx)
{
    return 0;
}

static int mediacodec_ndk_signalEndOfInputStream(FFAMediaCodec *ctx)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    media_status_t status;

    if (!codec->signalEndOfInputStream) {
        av_log(codec, AV_LOG_ERROR, "signalEndOfInputStream unavailable\n");
        return AVERROR_EXTERNAL;
    }

    status = codec->signalEndOfInputStream(codec->impl);
    if (status != AMEDIA_OK) {
        av_log(codec, AV_LOG_ERROR, "signalEndOfInputStream failed, %d\n", status);
        return AVERROR_EXTERNAL;
    }
    av_log(codec, AV_LOG_DEBUG, "signalEndOfInputStream success\n");

    return 0;
}

static void mediacodec_ndk_onInputAvailable(AMediaCodec *impl, void *userdata,
                                            int32_t index)
{
    FFAMediaCodecNdk *codec = userdata;
    codec->async_cb.onAsyncInputAvailable((FFAMediaCodec *) codec,
                                          codec->async_userdata, index);
}

static void mediacodec_ndk_onOutputAvailable(AMediaCodec *impl,
                                             void *userdata,
                                             int32_t index,
                                             AMediaCodecBufferInfo *buffer_info)
{
    FFAMediaCodecNdk *codec = userdata;
    FFAMediaCodecBufferInfo info = {
            .offset = buffer_info->offset,
            .size = buffer_info->size,
            .presentationTimeUs = buffer_info->presentationTimeUs,
            .flags = buffer_info->flags,
    };

    codec->async_cb.onAsyncOutputAvailable(&codec->api, codec->async_userdata,
                                           index, &info);
}

static void mediacodec_ndk_onFormatChanged(AMediaCodec *impl, void *userdata,
                                           AMediaFormat *format)
{
    FFAMediaCodecNdk *codec = userdata;
    FFAMediaFormat *media_format = mediaformat_ndk_create(format);
    if (!media_format)
        return;

    codec->async_cb.onAsyncFormatChanged(&codec->api, codec->async_userdata,
                                         media_format);
    ff_AMediaFormat_delete(media_format);
}

static void mediacodec_ndk_onError(AMediaCodec *impl, void *userdata,
                                   media_status_t status,
                                   int32_t actionCode,
                                   const char *detail)
{
    FFAMediaCodecNdk *codec = userdata;
    int error = media_status_to_error(status);

    codec->async_cb.onAsyncError(&codec->api, codec->async_userdata, error,
                                 detail);
}

static int mediacodec_ndk_setAsyncNotifyCallback(FFAMediaCodec *ctx,
         const FFAMediaCodecOnAsyncNotifyCallback *callback,
         void *userdata)
{
    FFAMediaCodecNdk *codec = (FFAMediaCodecNdk *)ctx;
    struct AMediaCodecOnAsyncNotifyCallback cb = {
            .onAsyncInputAvailable = mediacodec_ndk_onInputAvailable,
            .onAsyncOutputAvailable = mediacodec_ndk_onOutputAvailable,
            .onAsyncFormatChanged = mediacodec_ndk_onFormatChanged,
            .onAsyncError = mediacodec_ndk_onError,
    };
    media_status_t status;

    if (!codec->setAsyncNotifyCallback) {
        av_log(codec, AV_LOG_ERROR, "setAsyncNotifyCallback unavailable\n");
        return AVERROR(ENOSYS);
    }

    if (!callback ||
        !callback->onAsyncInputAvailable ||
        !callback->onAsyncOutputAvailable ||
        !callback->onAsyncFormatChanged ||
        !callback->onAsyncError)
        return AVERROR(EINVAL);

    codec->async_cb = *callback;
    codec->async_userdata = userdata;

    status = codec->setAsyncNotifyCallback(codec->impl, cb, codec);
    if (status != AMEDIA_OK) {
        av_log(codec, AV_LOG_ERROR, "setAsyncNotifyCallback failed, %d\n",
               status);
        return AVERROR_EXTERNAL;
    }

    return 0;
}

static const FFAMediaFormat media_format_ndk = {
    .class = &amediaformat_ndk_class,

    .create = mediaformat_ndk_new,
    .delete = mediaformat_ndk_delete,

    .toString = mediaformat_ndk_toString,

    .getInt32 = mediaformat_ndk_getInt32,
    .getInt64 = mediaformat_ndk_getInt64,
    .getFloat = mediaformat_ndk_getFloat,
    .getBuffer = mediaformat_ndk_getBuffer,
    .getString = mediaformat_ndk_getString,
    .getRect = mediaformat_ndk_getRect,

    .setInt32 = mediaformat_ndk_setInt32,
    .setInt64 = mediaformat_ndk_setInt64,
    .setFloat = mediaformat_ndk_setFloat,
    .setString = mediaformat_ndk_setString,
    .setBuffer = mediaformat_ndk_setBuffer,
    .setRect = mediaformat_ndk_setRect,
};

static const FFAMediaCodec media_codec_ndk = {
    .class = &amediacodec_ndk_class,

    .getName = mediacodec_ndk_getName,

    .createCodecByName = mediacodec_ndk_createCodecByName,
    .createDecoderByType = mediacodec_ndk_createDecoderByType,
    .createEncoderByType = mediacodec_ndk_createEncoderByType,
    .delete = mediacodec_ndk_delete,

    .configure = mediacodec_ndk_configure,
    .start = mediacodec_ndk_start,
    .stop = mediacodec_ndk_stop,
    .flush = mediacodec_ndk_flush,

    .getInputBuffer = mediacodec_ndk_getInputBuffer,
    .getOutputBuffer = mediacodec_ndk_getOutputBuffer,

    .dequeueInputBuffer = mediacodec_ndk_dequeueInputBuffer,
    .queueInputBuffer = mediacodec_ndk_queueInputBuffer,

    .dequeueOutputBuffer = mediacodec_ndk_dequeueOutputBuffer,
    .getOutputFormat = mediacodec_ndk_getOutputFormat,

    .releaseOutputBuffer = mediacodec_ndk_releaseOutputBuffer,
    .releaseOutputBufferAtTime = mediacodec_ndk_releaseOutputBufferAtTime,

    .infoTryAgainLater = mediacodec_ndk_infoTryAgainLater,
    .infoOutputBuffersChanged = mediacodec_ndk_infoOutputBuffersChanged,
    .infoOutputFormatChanged = mediacodec_ndk_infoOutputFormatChanged,

    .getBufferFlagCodecConfig = mediacodec_ndk_getBufferFlagCodecConfig,
    .getBufferFlagEndOfStream = mediacodec_ndk_getBufferFlagEndOfStream,
    .getBufferFlagKeyFrame = mediacodec_ndk_getBufferFlagKeyFrame,

    .getConfigureFlagEncode = mediacodec_ndk_getConfigureFlagEncode,
    .cleanOutputBuffers = mediacodec_ndk_cleanOutputBuffers,
    .signalEndOfInputStream = mediacodec_ndk_signalEndOfInputStream,
    .setAsyncNotifyCallback = mediacodec_ndk_setAsyncNotifyCallback,
};

FFAMediaFormat *ff_AMediaFormat_new(int ndk)
{
    if (ndk)
        return media_format_ndk.create();
    return media_format_jni.create();
}

FFAMediaCodec* ff_AMediaCodec_createCodecByName(const char *name, int ndk)
{
    if (ndk)
        return media_codec_ndk.createCodecByName(name);
    return media_codec_jni.createCodecByName(name);
}

FFAMediaCodec* ff_AMediaCodec_createDecoderByType(const char *mime_type, int ndk)
{
   if (ndk)
        return media_codec_ndk.createDecoderByType(mime_type);
    return media_codec_jni.createDecoderByType(mime_type);
}

FFAMediaCodec* ff_AMediaCodec_createEncoderByType(const char *mime_type, int ndk)
{
    if (ndk)
        return media_codec_ndk.createEncoderByType(mime_type);
    return media_codec_jni.createEncoderByType(mime_type);
}

int ff_Build_SDK_INT(AVCodecContext *avctx)
{
    int ret = -1;

#if __ANDROID_API__ >= 24
    // android_get_device_api_level() is a static inline before API level 29.
    // dlsym() might doesn't work.
    //
    // We can implement android_get_device_api_level() by
    // __system_property_get(), but __system_property_get() has created a lot of
    // troubles and is deprecated. So avoid using __system_property_get() for
    // now.
    //
    // Hopy we can remove the conditional compilation finally by bumping the
    // required API level.
    //
    ret = android_get_device_api_level();
#else
    JNIEnv *env = NULL;
    jclass versionClass;
    jfieldID sdkIntFieldID;
    JNI_GET_ENV_OR_RETURN(env, avctx, -1);

    versionClass = (*env)->FindClass(env, "android/os/Build$VERSION");
    sdkIntFieldID = (*env)->GetStaticFieldID(env, versionClass, "SDK_INT", "I");
    ret = (*env)->GetStaticIntField(env, versionClass, sdkIntFieldID);
    (*env)->DeleteLocalRef(env, versionClass);
#endif
    av_log(avctx, AV_LOG_DEBUG, "device api level %d\n", ret);

    return ret;
}

static struct {
    enum FFAMediaFormatColorRange mf_range;
    enum AVColorRange range;
} color_range_map[] = {
    { COLOR_RANGE_FULL,     AVCOL_RANGE_JPEG },
    { COLOR_RANGE_LIMITED,  AVCOL_RANGE_MPEG },
};

static struct {
    enum FFAMediaFormatColorStandard mf_standard;
    enum AVColorSpace space;
} color_space_map[] = {
    { COLOR_STANDARD_BT709,         AVCOL_SPC_BT709         },
    { COLOR_STANDARD_BT601_PAL,     AVCOL_SPC_BT470BG       },
    { COLOR_STANDARD_BT601_NTSC,    AVCOL_SPC_SMPTE170M     },
    { COLOR_STANDARD_BT2020,        AVCOL_SPC_BT2020_NCL    },
};

static struct {
    enum FFAMediaFormatColorStandard mf_standard;
    enum AVColorPrimaries primaries;
} color_primaries_map[] = {
    { COLOR_STANDARD_BT709,         AVCOL_PRI_BT709     },
    { COLOR_STANDARD_BT601_PAL,     AVCOL_PRI_BT470BG   },
    { COLOR_STANDARD_BT601_NTSC,    AVCOL_PRI_SMPTE170M },
    { COLOR_STANDARD_BT2020,        AVCOL_PRI_BT2020    },
};

static struct {
    enum FFAMediaFormatColorTransfer mf_transfer;
    enum AVColorTransferCharacteristic transfer;
} color_transfer_map[] = {
    { COLOR_TRANSFER_LINEAR,        AVCOL_TRC_LINEAR        },
    { COLOR_TRANSFER_SDR_VIDEO,     AVCOL_TRC_SMPTE170M     },
    { COLOR_TRANSFER_ST2084,        AVCOL_TRC_SMPTEST2084   },
    { COLOR_TRANSFER_HLG,           AVCOL_TRC_ARIB_STD_B67  },
};

enum AVColorRange ff_AMediaFormatColorRange_to_AVColorRange(int color_range)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_range_map); i++)
        if (color_range_map[i].mf_range == color_range)
            return color_range_map[i].range;

    return AVCOL_RANGE_UNSPECIFIED;
}

int ff_AMediaFormatColorRange_from_AVColorRange(enum AVColorRange color_range)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_range_map); i++)
        if (color_range_map[i].range == color_range)
            return color_range_map[i].mf_range;
    return COLOR_RANGE_UNSPECIFIED;
}

enum AVColorSpace ff_AMediaFormatColorStandard_to_AVColorSpace(int color_standard)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_space_map); i++)
        if (color_space_map[i].mf_standard == color_standard)
            return color_space_map[i].space;

    return AVCOL_SPC_UNSPECIFIED;
}

int ff_AMediaFormatColorStandard_from_AVColorSpace(enum AVColorSpace color_space)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_space_map); i++)
        if (color_space_map[i].space == color_space)
            return color_space_map[i].mf_standard;

    return COLOR_STANDARD_UNSPECIFIED;
}

enum AVColorPrimaries ff_AMediaFormatColorStandard_to_AVColorPrimaries(int color_standard)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_primaries_map); i++)
        if (color_primaries_map[i].mf_standard == color_standard)
            return color_primaries_map[i].primaries;

    return AVCOL_PRI_UNSPECIFIED;
}

enum AVColorTransferCharacteristic
ff_AMediaFormatColorTransfer_to_AVColorTransfer(int color_transfer)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_transfer_map); i++)
        if (color_transfer_map[i].mf_transfer == color_transfer)
            return color_transfer_map[i].transfer;

    return AVCOL_TRC_UNSPECIFIED;
}

int ff_AMediaFormatColorTransfer_from_AVColorTransfer(
    enum AVColorTransferCharacteristic color_transfer)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(color_transfer_map); i++)
        if (color_transfer_map[i].transfer == color_transfer)
            return color_transfer_map[i].mf_transfer;

    return COLOR_TRANSFER_UNSPECIFIED;
}
