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

#include "ffjni.h"
#include "version.h"
#include "mediacodec_wrapper.h"

struct JNIAMediaCodecListFields {

    jclass mediacodec_list_class;
    jmethodID init_id;
    jmethodID find_decoder_for_format_id;

    jmethodID get_codec_count_id;
    jmethodID get_codec_info_at_id;

    jclass mediacodec_info_class;
    jmethodID get_name_id;
    jmethodID get_supported_types_id;
    jmethodID is_encoder_id;

} JNIAMediaCodecListFields;

static const struct FFJniField jni_amediacodeclist_mapping[] = {
    { "android/media/MediaCodecList", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, mediacodec_list_class), 1 },
        { "android/media/MediaCodecList", "<init>", "(I)V", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, init_id), 0 },
        { "android/media/MediaCodecList", "findDecoderForFormat", "(Landroid/media/MediaFormat;)Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, find_decoder_for_format_id), 0 },

        { "android/media/MediaCodecList", "getCodecCount", "()I", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecListFields, get_codec_count_id), 1 },
        { "android/media/MediaCodecList", "getCodecInfoAt", "(I)Landroid/media/MediaCodecInfo;", FF_JNI_STATIC_METHOD, offsetof(struct JNIAMediaCodecListFields, get_codec_info_at_id), 1 },

    { "android/media/MediaCodecInfo", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaCodecListFields, mediacodec_info_class), 1 },
        { "android/media/MediaCodecInfo", "getName", "()Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, get_name_id), 1 },
        { "android/media/MediaCodecInfo", "getSupportedTypes", "()[Ljava/lang/String;", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, get_supported_types_id), 1 },
        { "android/media/MediaCodecInfo", "isEncoder", "()Z", FF_JNI_METHOD, offsetof(struct JNIAMediaCodecListFields, is_encoder_id), 1 },

    { NULL }
};

struct JNIAMediaFormatFields {

    jclass mediaformat_class;

    jmethodID init_id;

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

} JNIAMediaFormatFields;

static const struct FFJniField jni_amediaformat_mapping[] = {
    { "android/media/MediaFormat", NULL, NULL, FF_JNI_CLASS, offsetof(struct JNIAMediaFormatFields, mediaformat_class), 1 },

        { "android/media/MediaFormat", "<init>", "()V", FF_JNI_METHOD, offsetof(struct JNIAMediaFormatFields, init_id), 1 },

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
    .version    = LIBAVCODEC_VERSION_INT,
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

} JNIAMediaCodecFields;

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
    .version    = LIBAVCODEC_VERSION_INT,
};

struct FFAMediaCodec {

    const AVClass *class;

    struct JNIAMediaCodecFields jfields;

    jobject object;

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

#define JNI_ATTACH_ENV_OR_RETURN(env, attached, log_ctx, ret) do { \
    (env) = ff_jni_attach_env(attached, log_ctx);                  \
    if (!(env)) {                                                  \
        return ret;                                                \
    }                                                              \
} while (0)

#define JNI_ATTACH_ENV_OR_RETURN_VOID(env, attached, log_ctx) do { \
    (env) = ff_jni_attach_env(attached, log_ctx);              \
    if (!(env)) {                                                  \
        return;                                                    \
    }                                                              \
} while (0)

#define JNI_DETACH_ENV(attached, log_ctx) do { \
    if (attached)                              \
        ff_jni_detach_env(log_ctx);            \
} while (0)

char *ff_AMediaCodecList_getCodecNameByType(const char *mime, void *log_ctx)
{
    int ret;
    char *name = NULL;
    char *supported_type = NULL;

    int attached = 0;
    JNIEnv *env = NULL;
    struct JNIAMediaCodecListFields jfields = { 0 };
    struct JNIAMediaFormatFields mediaformat_jfields = { 0 };

    jobject format = NULL;
    jobject codec = NULL;
    jstring key = NULL;
    jstring tmp = NULL;

    jobject info = NULL;
    jobject type = NULL;
    jobjectArray types = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, log_ctx, NULL);

    if ((ret = ff_jni_init_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, log_ctx)) < 0) {
        goto done;
    }

    if ((ret = ff_jni_init_jfields(env, &mediaformat_jfields, jni_amediaformat_mapping, 0, log_ctx)) < 0) {
        goto done;
    }

    if (jfields.init_id && jfields.find_decoder_for_format_id) {
        key = ff_jni_utf_chars_to_jstring(env, "mime", log_ctx);
        if (!key) {
            goto done;
        }

        tmp = ff_jni_utf_chars_to_jstring(env, mime, log_ctx);
        if (!tmp) {
            goto done;
        }

        format = (*env)->NewObject(env, mediaformat_jfields.mediaformat_class, mediaformat_jfields.init_id);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        (*env)->CallVoidMethod(env, format, mediaformat_jfields.set_string_id, key, tmp);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        (*env)->DeleteLocalRef(env, key);
        key = NULL;

        (*env)->DeleteLocalRef(env, tmp);
        tmp = NULL;

        codec = (*env)->NewObject(env, jfields.mediacodec_list_class, jfields.init_id, 0);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }

        tmp = (*env)->CallObjectMethod(env, codec, jfields.find_decoder_for_format_id, format);
        if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
            goto done;
        }
        if (!tmp) {
            av_log(NULL, AV_LOG_ERROR, "Could not find decoder in media codec list "
                                       "for format { mime=%s }\n", mime);
            goto done;
        }

        name = ff_jni_jstring_to_utf_chars(env, tmp, log_ctx);
        if (!name) {
            goto done;
        }

    } else {
        int i;
        int codec_count;

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

            if (is_encoder) {
                continue;
            }

            type_count = (*env)->GetArrayLength(env, types);
            for (j = 0; j < type_count; j++) {

                type = (*env)->GetObjectArrayElement(env, types, j);
                if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                    goto done;
                }

                supported_type = ff_jni_jstring_to_utf_chars(env, type, log_ctx);
                if (!supported_type) {
                    goto done;
                }

                if (!av_strcasecmp(supported_type, mime)) {
                    jobject codec_name;

                    codec_name = (*env)->CallObjectMethod(env, info, jfields.get_name_id);
                    if (ff_jni_exception_check(env, 1, log_ctx) < 0) {
                        goto done;
                    }

                    name = ff_jni_jstring_to_utf_chars(env, codec_name, log_ctx);
                    if (!name) {
                        goto done;
                    }

                    if (strstr(name, "OMX.google")) {
                        av_freep(&name);
                        continue;
                    }
                }

                av_freep(&supported_type);
            }

            (*env)->DeleteLocalRef(env, info);
            info = NULL;

            (*env)->DeleteLocalRef(env, types);
            types = NULL;

            if (name)
                break;
        }
    }

done:
    if (format) {
        (*env)->DeleteLocalRef(env, format);
    }

    if (codec) {
        (*env)->DeleteLocalRef(env, codec);
    }

    if (key) {
        (*env)->DeleteLocalRef(env, key);
    }

    if (tmp) {
        (*env)->DeleteLocalRef(env, tmp);
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

    av_freep(&supported_type);

    ff_jni_reset_jfields(env, &jfields, jni_amediacodeclist_mapping, 0, log_ctx);
    ff_jni_reset_jfields(env, &mediaformat_jfields, jni_amediaformat_mapping, 0, log_ctx);

    JNI_DETACH_ENV(attached, log_ctx);

    return name;
}

FFAMediaFormat *ff_AMediaFormat_new(void)
{
    int attached = 0;
    JNIEnv *env = NULL;
    FFAMediaFormat *format = NULL;

    format = av_mallocz(sizeof(FFAMediaFormat));
    if (!format) {
        return NULL;
    }
    format->class = &amediaformat_class;

    env = ff_jni_attach_env(&attached, format);
    if (!env) {
        av_freep(&format);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format) < 0) {
        goto fail;
    }

    format->object = (*env)->NewObject(env, format->jfields.mediaformat_class, format->jfields.init_id);
    if (!format->object) {
        goto fail;
    }

    format->object = (*env)->NewGlobalRef(env, format->object);
    if (!format->object) {
        goto fail;
    }

    JNI_DETACH_ENV(attached, format);

    return format;
fail:
    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    JNI_DETACH_ENV(attached, format);

    av_freep(&format);

    return NULL;
}

static FFAMediaFormat *ff_AMediaFormat_newFromObject(void *object)
{
    int attached = 0;
    JNIEnv *env = NULL;
    FFAMediaFormat *format = NULL;

    format = av_mallocz(sizeof(FFAMediaFormat));
    if (!format) {
        return NULL;
    }
    format->class = &amediaformat_class;

    env = ff_jni_attach_env(&attached, format);
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

    JNI_DETACH_ENV(attached, format);

    return format;
fail:
    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    JNI_DETACH_ENV(attached, format);

    av_freep(&format);

    return NULL;
}

int ff_AMediaFormat_delete(FFAMediaFormat* format)
{
    int ret = 0;

    int attached = 0;
    JNIEnv *env = NULL;

    if (!format) {
        return 0;
    }

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, AVERROR_EXTERNAL);

    (*env)->DeleteGlobalRef(env, format->object);
    format->object = NULL;

    ff_jni_reset_jfields(env, &format->jfields, jni_amediaformat_mapping, 1, format);

    JNI_DETACH_ENV(attached, format);

    av_freep(&format);

    return ret;
}

char* ff_AMediaFormat_toString(FFAMediaFormat* format)
{
    char *ret = NULL;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring description = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, NULL);

    description = (*env)->CallObjectMethod(env, format->object, format->jfields.to_string_id);
    if (ff_jni_exception_check(env, 1, NULL) < 0) {
        goto fail;
    }

    ret = ff_jni_jstring_to_utf_chars(env, description, format);
fail:

    if (description) {
        (*env)->DeleteLocalRef(env, description);
    }

    JNI_DETACH_ENV(attached, format);

    return ret;
}

int ff_AMediaFormat_getInt32(FFAMediaFormat* format, const char *name, int32_t *out)
{
    int ret = 1;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
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

    JNI_DETACH_ENV(attached, format);

    return ret;
}

int ff_AMediaFormat_getInt64(FFAMediaFormat* format, const char *name, int64_t *out)
{
    int ret = 1;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
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

    JNI_DETACH_ENV(attached, format);

    return ret;
}

int ff_AMediaFormat_getFloat(FFAMediaFormat* format, const char *name, float *out)
{
    int ret = 1;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
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

    JNI_DETACH_ENV(attached, format);

    return ret;
}

int ff_AMediaFormat_getBuffer(FFAMediaFormat* format, const char *name, void** data, size_t *size)
{
    int ret = 1;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;
    jobject result = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
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

    JNI_DETACH_ENV(attached, format);

    return ret;
}

int ff_AMediaFormat_getString(FFAMediaFormat* format, const char *name, const char **out)
{
    int ret = 1;

    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;
    jstring result = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, format, 0);

    key = ff_jni_utf_chars_to_jstring(env, name, format);
    if (!key) {
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

    JNI_DETACH_ENV(attached, format);

    return ret;
}

void ff_AMediaFormat_setInt32(FFAMediaFormat* format, const char* name, int32_t value)
{
    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN_VOID(env, &attached, format);

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

    JNI_DETACH_ENV(attached, format);
}

void ff_AMediaFormat_setInt64(FFAMediaFormat* format, const char* name, int64_t value)
{
    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN_VOID(env, &attached, format);

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

    JNI_DETACH_ENV(attached, NULL);
}

void ff_AMediaFormat_setFloat(FFAMediaFormat* format, const char* name, float value)
{
    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN_VOID(env, &attached, format);

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

    JNI_DETACH_ENV(attached, NULL);
}

void ff_AMediaFormat_setString(FFAMediaFormat* format, const char* name, const char* value)
{
    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;
    jstring string = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN_VOID(env, &attached, format);

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

    JNI_DETACH_ENV(attached, format);
}

void ff_AMediaFormat_setBuffer(FFAMediaFormat* format, const char* name, void* data, size_t size)
{
    int attached = 0;
    JNIEnv *env = NULL;
    jstring key = NULL;
    jobject buffer = NULL;
    void *buffer_data = NULL;

    av_assert0(format != NULL);

    JNI_ATTACH_ENV_OR_RETURN_VOID(env, &attached, format);

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

    JNI_DETACH_ENV(attached, format);
}

static int codec_init_static_fields(FFAMediaCodec *codec)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

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
    JNI_DETACH_ENV(attached, NULL);

    return ret;
}

FFAMediaCodec* ff_AMediaCodec_createCodecByName(const char *name)
{
    int attached = 0;
    JNIEnv *env = NULL;
    FFAMediaCodec *codec = NULL;
    jstring codec_name = NULL;

    codec = av_mallocz(sizeof(FFAMediaCodec));
    if (!codec) {
        return NULL;
    }
    codec->class = &amediacodec_class;

    env = ff_jni_attach_env(&attached, codec);
    if (!env) {
        av_freep(&codec);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec) < 0) {
        goto fail;
    }

    codec_name = ff_jni_utf_chars_to_jstring(env, name, codec);
    if (!codec_name) {
        goto fail;
    }

    codec->object = (*env)->CallStaticObjectMethod(env, codec->jfields.mediacodec_class, codec->jfields.create_by_codec_name_id, codec_name);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    codec->object = (*env)->NewGlobalRef(env, codec->object);
    if (!codec->object) {
        goto fail;
    }

    if (codec_init_static_fields(codec) < 0) {
        goto fail;
    }

    if (codec->jfields.get_input_buffer_id && codec->jfields.get_output_buffer_id) {
        codec->has_get_i_o_buffer = 1;
    }

    JNI_DETACH_ENV(attached, codec);

    return codec;
fail:
    ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);

    if (codec_name) {
        (*env)->DeleteLocalRef(env, codec_name);
    }

    JNI_DETACH_ENV(attached, codec);

    av_freep(&codec);

    return NULL;
}

FFAMediaCodec* ff_AMediaCodec_createDecoderByType(const char *mime)
{
    int attached = 0;
    JNIEnv *env = NULL;
    FFAMediaCodec *codec = NULL;
    jstring mime_type = NULL;

    codec = av_mallocz(sizeof(FFAMediaCodec));
    if (!codec) {
        return NULL;
    }
    codec->class = &amediacodec_class;

    env = ff_jni_attach_env(&attached, codec);
    if (!env) {
        av_freep(&codec);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec) < 0) {
        goto fail;
    }

    mime_type = ff_jni_utf_chars_to_jstring(env, mime, codec);
    if (!mime_type) {
        goto fail;
    }

    codec->object = (*env)->CallStaticObjectMethod(env, codec->jfields.mediacodec_class, codec->jfields.create_decoder_by_type_id, mime_type);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    codec->object = (*env)->NewGlobalRef(env, codec->object);
    if (!codec->object) {
        goto fail;
    }

    if (codec_init_static_fields(codec) < 0) {
        goto fail;
    }

    if (codec->jfields.get_input_buffer_id && codec->jfields.get_output_buffer_id) {
        codec->has_get_i_o_buffer = 1;
    }

    JNI_DETACH_ENV(attached, codec);

    return codec;
fail:
    ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);

    if (mime_type) {
        (*env)->DeleteLocalRef(env, mime_type);
    }

    JNI_DETACH_ENV(attached, codec);

    av_freep(&codec);

    return NULL;
}

FFAMediaCodec* ff_AMediaCodec_createEncoderByType(const char *mime)
{
    int attached = 0;
    JNIEnv *env = NULL;
    FFAMediaCodec *codec = NULL;
    jstring mime_type = NULL;

    codec = av_mallocz(sizeof(FFAMediaCodec));
    if (!codec) {
        return NULL;
    }
    codec->class = &amediacodec_class;

    env = ff_jni_attach_env(&attached, codec);
    if (!env) {
        av_freep(&codec);
        return NULL;
    }

    if (ff_jni_init_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec) < 0) {
        goto fail;
    }

    mime_type = ff_jni_utf_chars_to_jstring(env, mime, codec);
    if (!mime_type) {
        goto fail;
    }

    codec->object = (*env)->CallStaticObjectMethod(env, codec->jfields.mediacodec_class, codec->jfields.create_encoder_by_type_id, mime_type);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    codec->object = (*env)->NewGlobalRef(env, codec->object);
    if (!codec->object) {
        goto fail;
    }

    if (codec_init_static_fields(codec) < 0) {
        goto fail;
    }

    if (codec->jfields.get_input_buffer_id && codec->jfields.get_output_buffer_id) {
        codec->has_get_i_o_buffer = 1;
    }

    JNI_DETACH_ENV(attached, NULL);

    return codec;
fail:
    ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);

    if (mime_type) {
        (*env)->DeleteLocalRef(env, mime_type);
    }

    JNI_DETACH_ENV(attached, codec);

    av_freep(&codec);

    return NULL;
}

int ff_AMediaCodec_delete(FFAMediaCodec* codec)
{
    int ret = 0;

    int attached = 0;
    JNIEnv *env = NULL;

    if (!codec) {
        return 0;
    }

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
    }

    (*env)->DeleteGlobalRef(env, codec->object);
    codec->object = NULL;

    ff_jni_reset_jfields(env, &codec->jfields, jni_amediacodec_mapping, 1, codec);

    JNI_DETACH_ENV(attached, codec);

    av_freep(&codec);

    return ret;
}

char *ff_AMediaCodec_getName(FFAMediaCodec *codec)
{
    char *ret = NULL;
    int attached = 0;
    JNIEnv *env = NULL;
    jobject *name = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, NULL);

    name = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_name_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    ret = ff_jni_jstring_to_utf_chars(env, name, codec);

fail:
    JNI_DETACH_ENV(attached, NULL);

    return ret;
}

int ff_AMediaCodec_configure(FFAMediaCodec* codec, const FFAMediaFormat* format, void* surface, void *crypto, uint32_t flags)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    /* TODO: implement surface handling */
    av_assert0(surface == NULL);

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.configure_id, format->object, NULL, NULL, flags);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, NULL);

    return ret;
}

int ff_AMediaCodec_start(FFAMediaCodec* codec)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.start_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

int ff_AMediaCodec_stop(FFAMediaCodec* codec)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.stop_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

int ff_AMediaCodec_flush(FFAMediaCodec* codec)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.flush_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

int ff_AMediaCodec_releaseOutputBuffer(FFAMediaCodec* codec, size_t idx, int render)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_id, idx, render);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

int ff_AMediaCodec_releaseOutputBufferAtTime(FFAMediaCodec *codec, size_t idx, int64_t timestampNs)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.release_output_buffer_at_time_id, idx, timestampNs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

ssize_t ff_AMediaCodec_dequeueInputBuffer(FFAMediaCodec* codec, int64_t timeoutUs)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    ret = (*env)->CallIntMethod(env, codec->object, codec->jfields.dequeue_input_buffer_id, timeoutUs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

int ff_AMediaCodec_queueInputBuffer(FFAMediaCodec* codec, size_t idx, off_t offset, size_t size, uint64_t time, uint32_t flags)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    (*env)->CallVoidMethod(env, codec->object, codec->jfields.queue_input_buffer_id, idx, offset, size, time, flags);
    if ((ret = ff_jni_exception_check(env, 1, codec)) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

fail:
    JNI_DETACH_ENV(attached, codec);

    return ret;
}

ssize_t ff_AMediaCodec_dequeueOutputBuffer(FFAMediaCodec* codec, FFAMediaCodecBufferInfo *info, int64_t timeoutUs)
{
    int ret = 0;
    int attached = 0;
    JNIEnv *env = NULL;

    jobject mediainfo = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, AVERROR_EXTERNAL);

    mediainfo = (*env)->NewObject(env, codec->jfields.mediainfo_class, codec->jfields.init_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    ret = (*env)->CallIntMethod(env, codec->object, codec->jfields.dequeue_output_buffer_id, mediainfo, timeoutUs);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    info->flags = (*env)->GetIntField(env, mediainfo, codec->jfields.flags_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    info->offset = (*env)->GetIntField(env, mediainfo, codec->jfields.offset_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    info->presentationTimeUs = (*env)->GetLongField(env, mediainfo, codec->jfields.presentation_time_us_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    info->size = (*env)->GetIntField(env, mediainfo, codec->jfields.size_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        ret = AVERROR_EXTERNAL;
        goto fail;
    }
fail:
    if (mediainfo) {
        (*env)->DeleteLocalRef(env, mediainfo);
    }

    JNI_DETACH_ENV(attached, NULL);

    return ret;
}

uint8_t* ff_AMediaCodec_getInputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    int attached = 0;
    JNIEnv *env = NULL;

    jobject buffer = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, NULL);

    if (codec->has_get_i_o_buffer) {
        buffer = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_input_buffer_id, idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    } else {
        if (!codec->input_buffers) {
            codec->input_buffers = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_input_buffers_id);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }

            codec->input_buffers = (*env)->NewGlobalRef(env, codec->input_buffers);
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

    JNI_DETACH_ENV(attached, codec);

    return ret;
}

uint8_t* ff_AMediaCodec_getOutputBuffer(FFAMediaCodec* codec, size_t idx, size_t *out_size)
{
    uint8_t *ret = NULL;
    int attached = 0;
    JNIEnv *env = NULL;

    jobject buffer = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, NULL);

    if (codec->has_get_i_o_buffer) {
        buffer = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_buffer_id, idx);
        if (ff_jni_exception_check(env, 1, codec) < 0) {
            goto fail;
        }
    } else {
        if (!codec->output_buffers) {
            codec->output_buffers = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_buffers_id);
            if (ff_jni_exception_check(env, 1, codec) < 0) {
                goto fail;
            }

            codec->output_buffers = (*env)->NewGlobalRef(env, codec->output_buffers);
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

    JNI_DETACH_ENV(attached, codec);

    return ret;
}

FFAMediaFormat* ff_AMediaCodec_getOutputFormat(FFAMediaCodec* codec)
{
    FFAMediaFormat *ret = NULL;
    int attached = 0;
    JNIEnv *env = NULL;

    jobject mediaformat = NULL;

    JNI_ATTACH_ENV_OR_RETURN(env, &attached, codec, NULL);

    mediaformat = (*env)->CallObjectMethod(env, codec->object, codec->jfields.get_output_format_id);
    if (ff_jni_exception_check(env, 1, codec) < 0) {
        goto fail;
    }

    ret = ff_AMediaFormat_newFromObject(mediaformat);
fail:
    if (mediaformat) {
        (*env)->DeleteLocalRef(env, mediaformat);
    }

    JNI_DETACH_ENV(attached, codec);

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
            int attached = 0;
            JNIEnv *env = NULL;

            env = ff_jni_attach_env(&attached, codec);
            if (!env) {
                ret = AVERROR_EXTERNAL;
                goto fail;
            }

            (*env)->DeleteGlobalRef(env, codec->output_buffers);
            codec->output_buffers = NULL;

            JNI_DETACH_ENV(attached, codec);
        }
    }

fail:
    return ret;
}
