/*
 * Option handlers shared between the tools.
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

#include "config.h"

#include <stdio.h>

#include "cmdutils.h"
#include "opt_common.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/cpu.h"
#include "libavutil/dict.h"
#include "libavutil/error.h"
#include "libavutil/ffversion.h"
#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/version.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavcodec/codec.h"
#include "libavcodec/codec_desc.h"
#include "libavcodec/version.h"

#include "libavformat/avformat.h"
#include "libavformat/version.h"

#include "libavdevice/avdevice.h"
#include "libavdevice/version.h"

#include "libavfilter/avfilter.h"
#include "libavfilter/version.h"

#include "libswscale/swscale.h"
#include "libswscale/version.h"

#include "libswresample/swresample.h"
#include "libswresample/version.h"

#include "libpostproc/postprocess.h"
#include "libpostproc/version.h"

enum show_muxdemuxers {
    SHOW_DEFAULT,
    SHOW_DEMUXERS,
    SHOW_MUXERS,
};

static FILE *report_file;
static int report_file_level = AV_LOG_DEBUG;

int show_license(void *optctx, const char *opt, const char *arg)
{
#if CONFIG_NONFREE
    printf(
    "This version of %s has nonfree parts compiled in.\n"
    "Therefore it is not legally redistributable.\n",
    program_name );
#elif CONFIG_GPLV3
    printf(
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with %s.  If not, see <http://www.gnu.org/licenses/>.\n",
    program_name, program_name, program_name );
#elif CONFIG_GPL
    printf(
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with %s; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n",
    program_name, program_name, program_name );
#elif CONFIG_LGPLV3
    printf(
    "%s is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU Lesser General Public License as published by\n"
    "the Free Software Foundation; either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public License\n"
    "along with %s.  If not, see <http://www.gnu.org/licenses/>.\n",
    program_name, program_name, program_name );
#else
    printf(
    "%s is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU Lesser General Public\n"
    "License as published by the Free Software Foundation; either\n"
    "version 2.1 of the License, or (at your option) any later version.\n"
    "\n"
    "%s is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public\n"
    "License along with %s; if not, write to the Free Software\n"
    "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n",
    program_name, program_name, program_name );
#endif

    return 0;
}

static int warned_cfg = 0;

#define INDENT        1
#define SHOW_VERSION  2
#define SHOW_CONFIG   4
#define SHOW_COPYRIGHT 8

#define PRINT_LIB_INFO(libname, LIBNAME, flags, level)                  \
    if (CONFIG_##LIBNAME) {                                             \
        const char *indent = flags & INDENT? "  " : "";                 \
        if (flags & SHOW_VERSION) {                                     \
            unsigned int version = libname##_version();                 \
            av_log(NULL, level,                                         \
                   "%slib%-11s %2d.%3d.%3d / %2d.%3d.%3d\n",            \
                   indent, #libname,                                    \
                   LIB##LIBNAME##_VERSION_MAJOR,                        \
                   LIB##LIBNAME##_VERSION_MINOR,                        \
                   LIB##LIBNAME##_VERSION_MICRO,                        \
                   AV_VERSION_MAJOR(version), AV_VERSION_MINOR(version),\
                   AV_VERSION_MICRO(version));                          \
        }                                                               \
        if (flags & SHOW_CONFIG) {                                      \
            const char *cfg = libname##_configuration();                \
            if (strcmp(FFMPEG_CONFIGURATION, cfg)) {                    \
                if (!warned_cfg) {                                      \
                    av_log(NULL, level,                                 \
                            "%sWARNING: library configuration mismatch\n", \
                            indent);                                    \
                    warned_cfg = 1;                                     \
                }                                                       \
                av_log(NULL, level, "%s%-11s configuration: %s\n",      \
                        indent, #libname, cfg);                         \
            }                                                           \
        }                                                               \
    }                                                                   \

static void print_all_libs_info(int flags, int level)
{
    PRINT_LIB_INFO(avutil,     AVUTIL,     flags, level);
    PRINT_LIB_INFO(avcodec,    AVCODEC,    flags, level);
    PRINT_LIB_INFO(avformat,   AVFORMAT,   flags, level);
    PRINT_LIB_INFO(avdevice,   AVDEVICE,   flags, level);
    PRINT_LIB_INFO(avfilter,   AVFILTER,   flags, level);
    PRINT_LIB_INFO(swscale,    SWSCALE,    flags, level);
    PRINT_LIB_INFO(swresample, SWRESAMPLE, flags, level);
    PRINT_LIB_INFO(postproc,   POSTPROC,   flags, level);
}

static void print_program_info(int flags, int level)
{
    const char *indent = flags & INDENT? "  " : "";

    av_log(NULL, level, "%s version " FFMPEG_VERSION, program_name);
    if (flags & SHOW_COPYRIGHT)
        av_log(NULL, level, " Copyright (c) %d-%d the FFmpeg developers",
               program_birth_year, CONFIG_THIS_YEAR);
    av_log(NULL, level, "\n");
    av_log(NULL, level, "%sbuilt with %s\n", indent, CC_IDENT);

    av_log(NULL, level, "%sconfiguration: " FFMPEG_CONFIGURATION "\n", indent);
}

static void print_buildconf(int flags, int level)
{
    const char *indent = flags & INDENT ? "  " : "";
    char str[] = { FFMPEG_CONFIGURATION };
    char *conflist, *remove_tilde, *splitconf;

    // Change all the ' --' strings to '~--' so that
    // they can be identified as tokens.
    while ((conflist = strstr(str, " --")) != NULL) {
        conflist[0] = '~';
    }

    // Compensate for the weirdness this would cause
    // when passing 'pkg-config --static'.
    while ((remove_tilde = strstr(str, "pkg-config~")) != NULL) {
        remove_tilde[sizeof("pkg-config~") - 2] = ' ';
    }

    splitconf = strtok(str, "~");
    av_log(NULL, level, "\n%sconfiguration:\n", indent);
    while (splitconf != NULL) {
        av_log(NULL, level, "%s%s%s\n", indent, indent, splitconf);
        splitconf = strtok(NULL, "~");
    }
}

void show_banner(int argc, char **argv, const OptionDef *options)
{
    int idx = locate_option(argc, argv, options, "version");
    if (hide_banner || idx)
        return;

    print_program_info (INDENT|SHOW_COPYRIGHT, AV_LOG_INFO);
    print_all_libs_info(INDENT|SHOW_CONFIG,  AV_LOG_INFO);
    print_all_libs_info(INDENT|SHOW_VERSION, AV_LOG_INFO);
}

int show_version(void *optctx, const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    print_program_info (SHOW_COPYRIGHT, AV_LOG_INFO);
    print_all_libs_info(SHOW_VERSION, AV_LOG_INFO);

    return 0;
}

int show_buildconf(void *optctx, const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    print_buildconf      (INDENT|0, AV_LOG_INFO);

    return 0;
}

#define PRINT_CODEC_SUPPORTED(codec, config, type, name, elem, fmt, ...)        \
    do {                                                                        \
        int num = 0;                                                            \
        const type *elem = NULL;                                                \
        avcodec_get_supported_config(NULL, codec, config, 0,                    \
                                     (const void **) &elem, &num);              \
        if (elem) {                                                             \
            printf("    Supported " name ":");                                  \
            for (int i = 0; i < num; i++) {                                     \
                printf(" " fmt, __VA_ARGS__);                                   \
                elem++;                                                         \
            }                                                                   \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

static const char *get_channel_layout_desc(const AVChannelLayout *layout, AVBPrint *bp)
{
    int ret;
    av_bprint_clear(bp);
    ret = av_channel_layout_describe_bprint(layout, bp);
    if (!av_bprint_is_complete(bp) || ret < 0)
        return "unknown/invalid";
    return bp->str;
}

static void print_codec(const AVCodec *c)
{
    int encoder = av_codec_is_encoder(c);
    AVBPrint desc;

    printf("%s %s [%s]:\n", encoder ? "Encoder" : "Decoder", c->name,
           c->long_name ? c->long_name : "");

    printf("    General capabilities: ");
    if (c->capabilities & AV_CODEC_CAP_DRAW_HORIZ_BAND)
        printf("horizband ");
    if (c->capabilities & AV_CODEC_CAP_DR1)
        printf("dr1 ");
    if (c->capabilities & AV_CODEC_CAP_DELAY)
        printf("delay ");
    if (c->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME)
        printf("small ");
    if (c->capabilities & AV_CODEC_CAP_EXPERIMENTAL)
        printf("exp ");
    if (c->capabilities & AV_CODEC_CAP_CHANNEL_CONF)
        printf("chconf ");
    if (c->capabilities & AV_CODEC_CAP_PARAM_CHANGE)
        printf("paramchange ");
    if (c->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        printf("variable ");
    if (c->capabilities & (AV_CODEC_CAP_FRAME_THREADS |
                           AV_CODEC_CAP_SLICE_THREADS |
                           AV_CODEC_CAP_OTHER_THREADS))
        printf("threads ");
    if (c->capabilities & AV_CODEC_CAP_AVOID_PROBING)
        printf("avoidprobe ");
    if (c->capabilities & AV_CODEC_CAP_HARDWARE)
        printf("hardware ");
    if (c->capabilities & AV_CODEC_CAP_HYBRID)
        printf("hybrid ");
    if (!c->capabilities)
        printf("none");
    printf("\n");

    if (c->type == AVMEDIA_TYPE_VIDEO ||
        c->type == AVMEDIA_TYPE_AUDIO) {
        printf("    Threading capabilities: ");
        switch (c->capabilities & (AV_CODEC_CAP_FRAME_THREADS |
                                   AV_CODEC_CAP_SLICE_THREADS |
                                   AV_CODEC_CAP_OTHER_THREADS)) {
        case AV_CODEC_CAP_FRAME_THREADS |
             AV_CODEC_CAP_SLICE_THREADS: printf("frame and slice"); break;
        case AV_CODEC_CAP_FRAME_THREADS: printf("frame");           break;
        case AV_CODEC_CAP_SLICE_THREADS: printf("slice");           break;
        case AV_CODEC_CAP_OTHER_THREADS: printf("other");           break;
        default:                         printf("none");            break;
        }
        printf("\n");
    }

    if (avcodec_get_hw_config(c, 0)) {
        printf("    Supported hardware devices: ");
        for (int i = 0;; i++) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(c, i);
            const char *name;
            if (!config)
                break;
            name = av_hwdevice_get_type_name(config->device_type);
            if (name)
                printf("%s ", name);
        }
        printf("\n");
    }

    PRINT_CODEC_SUPPORTED(c, AV_CODEC_CONFIG_FRAME_RATE, AVRational, "framerates",
                          fps, "%d/%d", fps->num, fps->den);
    PRINT_CODEC_SUPPORTED(c, AV_CODEC_CONFIG_PIX_FORMAT, enum AVPixelFormat,
                          "pixel formats", fmt,  "%s", av_get_pix_fmt_name(*fmt));
    PRINT_CODEC_SUPPORTED(c, AV_CODEC_CONFIG_SAMPLE_RATE, int, "sample rates",
                          rate, "%d", *rate);
    PRINT_CODEC_SUPPORTED(c, AV_CODEC_CONFIG_SAMPLE_FORMAT, enum AVSampleFormat,
                          "sample formats", fmt, "%s", av_get_sample_fmt_name(*fmt));

    av_bprint_init(&desc, 0, AV_BPRINT_SIZE_AUTOMATIC);
    PRINT_CODEC_SUPPORTED(c, AV_CODEC_CONFIG_CHANNEL_LAYOUT, AVChannelLayout,
                          "channel layouts", layout, "%s",
                          get_channel_layout_desc(layout, &desc));
    av_bprint_finalize(&desc, NULL);

    if (c->priv_class) {
        show_help_children(c->priv_class,
                           AV_OPT_FLAG_ENCODING_PARAM |
                           AV_OPT_FLAG_DECODING_PARAM);
    }
}

static const AVCodec *next_codec_for_id(enum AVCodecID id, void **iter,
                                        int encoder)
{
    const AVCodec *c;
    while ((c = av_codec_iterate(iter))) {
        if (c->id == id &&
            (encoder ? av_codec_is_encoder(c) : av_codec_is_decoder(c)))
            return c;
    }
    return NULL;
}

static void show_help_codec(const char *name, int encoder)
{
    const AVCodecDescriptor *desc;
    const AVCodec *codec;

    if (!name) {
        av_log(NULL, AV_LOG_ERROR, "No codec name specified.\n");
        return;
    }

    codec = encoder ? avcodec_find_encoder_by_name(name) :
                      avcodec_find_decoder_by_name(name);

    if (codec)
        print_codec(codec);
    else if ((desc = avcodec_descriptor_get_by_name(name))) {
        void *iter = NULL;
        int printed = 0;

        while ((codec = next_codec_for_id(desc->id, &iter, encoder))) {
            printed = 1;
            print_codec(codec);
        }

        if (!printed) {
            av_log(NULL, AV_LOG_ERROR, "Codec '%s' is known to FFmpeg, "
                   "but no %s for it are available. FFmpeg might need to be "
                   "recompiled with additional external libraries.\n",
                   name, encoder ? "encoders" : "decoders");
        }
    } else {
        av_log(NULL, AV_LOG_ERROR, "Codec '%s' is not recognized by FFmpeg.\n",
               name);
    }
}

static void show_help_demuxer(const char *name)
{
    const AVInputFormat *fmt = av_find_input_format(name);

    if (!fmt) {
        av_log(NULL, AV_LOG_ERROR, "Unknown format '%s'.\n", name);
        return;
    }

    printf("Demuxer %s [%s]:\n", fmt->name, fmt->long_name);

    if (fmt->extensions)
        printf("    Common extensions: %s.\n", fmt->extensions);

    if (fmt->priv_class)
        show_help_children(fmt->priv_class, AV_OPT_FLAG_DECODING_PARAM);
}

static void show_help_protocol(const char *name)
{
    const AVClass *proto_class;

    if (!name) {
        av_log(NULL, AV_LOG_ERROR, "No protocol name specified.\n");
        return;
    }

    proto_class = avio_protocol_get_class(name);
    if (!proto_class) {
        av_log(NULL, AV_LOG_ERROR, "Unknown protocol '%s'.\n", name);
        return;
    }

    show_help_children(proto_class, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM);
}

static void show_help_muxer(const char *name)
{
    const AVCodecDescriptor *desc;
    const AVOutputFormat *fmt = av_guess_format(name, NULL, NULL);

    if (!fmt) {
        av_log(NULL, AV_LOG_ERROR, "Unknown format '%s'.\n", name);
        return;
    }

    printf("Muxer %s [%s]:\n", fmt->name, fmt->long_name);

    if (fmt->extensions)
        printf("    Common extensions: %s.\n", fmt->extensions);
    if (fmt->mime_type)
        printf("    Mime type: %s.\n", fmt->mime_type);
    if (fmt->video_codec != AV_CODEC_ID_NONE &&
        (desc = avcodec_descriptor_get(fmt->video_codec))) {
        printf("    Default video codec: %s.\n", desc->name);
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE &&
        (desc = avcodec_descriptor_get(fmt->audio_codec))) {
        printf("    Default audio codec: %s.\n", desc->name);
    }
    if (fmt->subtitle_codec != AV_CODEC_ID_NONE &&
        (desc = avcodec_descriptor_get(fmt->subtitle_codec))) {
        printf("    Default subtitle codec: %s.\n", desc->name);
    }

    if (fmt->priv_class)
        show_help_children(fmt->priv_class, AV_OPT_FLAG_ENCODING_PARAM);
}

#if CONFIG_AVFILTER
static void show_help_filter(const char *name)
{
#if CONFIG_AVFILTER
    const AVFilter *f = avfilter_get_by_name(name);
    int i, count;

    if (!name) {
        av_log(NULL, AV_LOG_ERROR, "No filter name specified.\n");
        return;
    } else if (!f) {
        av_log(NULL, AV_LOG_ERROR, "Unknown filter '%s'.\n", name);
        return;
    }

    printf("Filter %s\n", f->name);
    if (f->description)
        printf("  %s\n", f->description);

    if (f->flags & AVFILTER_FLAG_SLICE_THREADS)
        printf("    slice threading supported\n");

    printf("    Inputs:\n");
    count = avfilter_filter_pad_count(f, 0);
    for (i = 0; i < count; i++) {
        printf("       #%d: %s (%s)\n", i, avfilter_pad_get_name(f->inputs, i),
               av_get_media_type_string(avfilter_pad_get_type(f->inputs, i)));
    }
    if (f->flags & AVFILTER_FLAG_DYNAMIC_INPUTS)
        printf("        dynamic (depending on the options)\n");
    else if (!count)
        printf("        none (source filter)\n");

    printf("    Outputs:\n");
    count = avfilter_filter_pad_count(f, 1);
    for (i = 0; i < count; i++) {
        printf("       #%d: %s (%s)\n", i, avfilter_pad_get_name(f->outputs, i),
               av_get_media_type_string(avfilter_pad_get_type(f->outputs, i)));
    }
    if (f->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS)
        printf("        dynamic (depending on the options)\n");
    else if (!count)
        printf("        none (sink filter)\n");

    if (f->priv_class)
        show_help_children(f->priv_class, AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM |
                                          AV_OPT_FLAG_AUDIO_PARAM);
    if (f->flags & AVFILTER_FLAG_SUPPORT_TIMELINE)
        printf("This filter has support for timeline through the 'enable' option.\n");
#else
    av_log(NULL, AV_LOG_ERROR, "Build without libavfilter; "
           "can not to satisfy request\n");
#endif
}
#endif

static void show_help_bsf(const char *name)
{
    const AVBitStreamFilter *bsf = av_bsf_get_by_name(name);

    if (!name) {
        av_log(NULL, AV_LOG_ERROR, "No bitstream filter name specified.\n");
        return;
    } else if (!bsf) {
        av_log(NULL, AV_LOG_ERROR, "Unknown bit stream filter '%s'.\n", name);
        return;
    }

    printf("Bit stream filter %s\n", bsf->name);
    if (bsf->codec_ids) {
        const enum AVCodecID *id = bsf->codec_ids;
        printf("    Supported codecs:");
        while (*id != AV_CODEC_ID_NONE) {
            printf(" %s", avcodec_descriptor_get(*id)->name);
            id++;
        }
        printf("\n");
    }
    if (bsf->priv_class)
        show_help_children(bsf->priv_class, AV_OPT_FLAG_BSF_PARAM);
}

int show_help(void *optctx, const char *opt, const char *arg)
{
    char *topic, *par;
    av_log_set_callback(log_callback_help);

    topic = av_strdup(arg ? arg : "");
    if (!topic)
        return AVERROR(ENOMEM);
    par = strchr(topic, '=');
    if (par)
        *par++ = 0;

    if (!*topic) {
        show_help_default(topic, par);
    } else if (!strcmp(topic, "decoder")) {
        show_help_codec(par, 0);
    } else if (!strcmp(topic, "encoder")) {
        show_help_codec(par, 1);
    } else if (!strcmp(topic, "demuxer")) {
        show_help_demuxer(par);
    } else if (!strcmp(topic, "muxer")) {
        show_help_muxer(par);
    } else if (!strcmp(topic, "protocol")) {
        show_help_protocol(par);
#if CONFIG_AVFILTER
    } else if (!strcmp(topic, "filter")) {
        show_help_filter(par);
#endif
    } else if (!strcmp(topic, "bsf")) {
        show_help_bsf(par);
    } else {
        show_help_default(topic, par);
    }

    av_freep(&topic);
    return 0;
}

static void print_codecs_for_id(enum AVCodecID id, int encoder)
{
    void *iter = NULL;
    const AVCodec *codec;

    printf(" (%s:", encoder ? "encoders" : "decoders");

    while ((codec = next_codec_for_id(id, &iter, encoder)))
        printf(" %s", codec->name);

    printf(")");
}

static int compare_codec_desc(const void *a, const void *b)
{
    const AVCodecDescriptor * const *da = a;
    const AVCodecDescriptor * const *db = b;

    return (*da)->type != (*db)->type ? FFDIFFSIGN((*da)->type, (*db)->type) :
           strcmp((*da)->name, (*db)->name);
}

static int get_codecs_sorted(const AVCodecDescriptor ***rcodecs)
{
    const AVCodecDescriptor *desc = NULL;
    const AVCodecDescriptor **codecs;
    unsigned nb_codecs = 0, i = 0;

    while ((desc = avcodec_descriptor_next(desc)))
        nb_codecs++;
    if (!(codecs = av_calloc(nb_codecs, sizeof(*codecs))))
        return AVERROR(ENOMEM);
    desc = NULL;
    while ((desc = avcodec_descriptor_next(desc)))
        codecs[i++] = desc;
    av_assert0(i == nb_codecs);
    qsort(codecs, nb_codecs, sizeof(*codecs), compare_codec_desc);
    *rcodecs = codecs;
    return nb_codecs;
}

static char get_media_type_char(enum AVMediaType type)
{
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:    return 'V';
        case AVMEDIA_TYPE_AUDIO:    return 'A';
        case AVMEDIA_TYPE_DATA:     return 'D';
        case AVMEDIA_TYPE_SUBTITLE: return 'S';
        case AVMEDIA_TYPE_ATTACHMENT:return 'T';
        default:                    return '?';
    }
}

int show_codecs(void *optctx, const char *opt, const char *arg)
{
    const AVCodecDescriptor **codecs;
    unsigned i;
    int nb_codecs = get_codecs_sorted(&codecs);

    if (nb_codecs < 0)
        return nb_codecs;

    printf("Codecs:\n"
           " D..... = Decoding supported\n"
           " .E.... = Encoding supported\n"
           " ..V... = Video codec\n"
           " ..A... = Audio codec\n"
           " ..S... = Subtitle codec\n"
           " ..D... = Data codec\n"
           " ..T... = Attachment codec\n"
           " ...I.. = Intra frame-only codec\n"
           " ....L. = Lossy compression\n"
           " .....S = Lossless compression\n"
           " -------\n");
    for (i = 0; i < nb_codecs; i++) {
        const AVCodecDescriptor *desc = codecs[i];
        const AVCodec *codec;
        void *iter = NULL;

        if (strstr(desc->name, "_deprecated"))
            continue;

        printf(" %c%c%c%c%c%c",
               avcodec_find_decoder(desc->id) ? 'D' : '.',
               avcodec_find_encoder(desc->id) ? 'E' : '.',
               get_media_type_char(desc->type),
               (desc->props & AV_CODEC_PROP_INTRA_ONLY) ? 'I' : '.',
               (desc->props & AV_CODEC_PROP_LOSSY)      ? 'L' : '.',
               (desc->props & AV_CODEC_PROP_LOSSLESS)   ? 'S' : '.');

        printf(" %-20s %s", desc->name, desc->long_name ? desc->long_name : "");

        /* print decoders/encoders when there's more than one or their
         * names are different from codec name */
        while ((codec = next_codec_for_id(desc->id, &iter, 0))) {
            if (strcmp(codec->name, desc->name)) {
                print_codecs_for_id(desc->id, 0);
                break;
            }
        }
        iter = NULL;
        while ((codec = next_codec_for_id(desc->id, &iter, 1))) {
            if (strcmp(codec->name, desc->name)) {
                print_codecs_for_id(desc->id, 1);
                break;
            }
        }

        printf("\n");
    }
    av_free(codecs);
    return 0;
}

static int print_codecs(int encoder)
{
    const AVCodecDescriptor **codecs;
    int i, nb_codecs = get_codecs_sorted(&codecs);

    if (nb_codecs < 0)
        return nb_codecs;

    printf("%s:\n"
           " V..... = Video\n"
           " A..... = Audio\n"
           " S..... = Subtitle\n"
           " .F.... = Frame-level multithreading\n"
           " ..S... = Slice-level multithreading\n"
           " ...X.. = Codec is experimental\n"
           " ....B. = Supports draw_horiz_band\n"
           " .....D = Supports direct rendering method 1\n"
           " ------\n",
           encoder ? "Encoders" : "Decoders");
    for (i = 0; i < nb_codecs; i++) {
        const AVCodecDescriptor *desc = codecs[i];
        const AVCodec *codec;
        void *iter = NULL;

        while ((codec = next_codec_for_id(desc->id, &iter, encoder))) {
            printf(" %c%c%c%c%c%c",
                   get_media_type_char(desc->type),
                   (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)   ? 'F' : '.',
                   (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)   ? 'S' : '.',
                   (codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL)    ? 'X' : '.',
                   (codec->capabilities & AV_CODEC_CAP_DRAW_HORIZ_BAND) ? 'B' : '.',
                   (codec->capabilities & AV_CODEC_CAP_DR1)             ? 'D' : '.');

            printf(" %-20s %s", codec->name, codec->long_name ? codec->long_name : "");
            if (strcmp(codec->name, desc->name))
                printf(" (codec %s)", desc->name);

            printf("\n");
        }
    }
    av_free(codecs);
    return 0;
}

int show_decoders(void *optctx, const char *opt, const char *arg)
{
    return print_codecs(0);
}

int show_encoders(void *optctx, const char *opt, const char *arg)
{
    return print_codecs(1);
}

int show_bsfs(void *optctx, const char *opt, const char *arg)
{
    const AVBitStreamFilter *bsf = NULL;
    void *opaque = NULL;

    printf("Bitstream filters:\n");
    while ((bsf = av_bsf_iterate(&opaque)))
        printf("%s\n", bsf->name);
    printf("\n");
    return 0;
}

int show_filters(void *optctx, const char *opt, const char *arg)
{
#if CONFIG_AVFILTER
    const AVFilter *filter = NULL;
    char descr[64], *descr_cur;
    void *opaque = NULL;
    int i, j;
    const AVFilterPad *pad;

    printf("Filters:\n"
           "  T.. = Timeline support\n"
           "  .S. = Slice threading\n"
           "  A = Audio input/output\n"
           "  V = Video input/output\n"
           "  N = Dynamic number and/or type of input/output\n"
           "  | = Source or sink filter\n");
    while ((filter = av_filter_iterate(&opaque))) {
        descr_cur = descr;
        for (i = 0; i < 2; i++) {
            unsigned nb_pads;
            if (i) {
                *(descr_cur++) = '-';
                *(descr_cur++) = '>';
            }
            pad = i ? filter->outputs : filter->inputs;
            nb_pads = avfilter_filter_pad_count(filter, i);
            for (j = 0; j < nb_pads; j++) {
                if (descr_cur >= descr + sizeof(descr) - 4)
                    break;
                *(descr_cur++) = get_media_type_char(avfilter_pad_get_type(pad, j));
            }
            if (!j)
                *(descr_cur++) = ((!i && (filter->flags & AVFILTER_FLAG_DYNAMIC_INPUTS)) ||
                                  ( i && (filter->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS))) ? 'N' : '|';
        }
        *descr_cur = 0;
        printf(" %c%c %-17s %-10s %s\n",
               filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE ? 'T' : '.',
               filter->flags & AVFILTER_FLAG_SLICE_THREADS    ? 'S' : '.',
               filter->name, descr, filter->description);
    }
#else
    printf("No filters available: libavfilter disabled\n");
#endif
    return 0;
}

static int is_device(const AVClass *avclass)
{
    if (!avclass)
        return 0;
    return AV_IS_INPUT_DEVICE(avclass->category) || AV_IS_OUTPUT_DEVICE(avclass->category);
}

static int show_formats_devices(void *optctx, const char *opt, const char *arg, int device_only, int muxdemuxers)
{
    void *ifmt_opaque = NULL;
    const AVInputFormat *ifmt  = NULL;
    void *ofmt_opaque = NULL;
    const AVOutputFormat *ofmt = NULL;
    const char *last_name;
    int is_dev;
    const char *is_device_placeholder = device_only ? "" : ".";

    printf("%s:\n"
           " D.%s = Demuxing supported\n"
           " .E%s = Muxing supported\n"
           "%s"
           " ---\n",
           device_only ? "Devices" : "Formats",
           is_device_placeholder, is_device_placeholder,
           device_only ? "": " ..d = Is a device\n");

    last_name = "000";
    for (;;) {
        int decode = 0;
        int encode = 0;
        int device = 0;
        const char *name      = NULL;
        const char *long_name = NULL;

        if (muxdemuxers !=SHOW_DEMUXERS) {
            ofmt_opaque = NULL;
            while ((ofmt = av_muxer_iterate(&ofmt_opaque))) {
                is_dev = is_device(ofmt->priv_class);
                if (!is_dev && device_only)
                    continue;
                if ((!name || strcmp(ofmt->name, name) < 0) &&
                    strcmp(ofmt->name, last_name) > 0) {
                    name      = ofmt->name;
                    long_name = ofmt->long_name;
                    encode    = 1;
                    device    = is_dev;
                }
            }
        }
        if (muxdemuxers != SHOW_MUXERS) {
            ifmt_opaque = NULL;
            while ((ifmt = av_demuxer_iterate(&ifmt_opaque))) {
                is_dev = is_device(ifmt->priv_class);
                if (!is_dev && device_only)
                    continue;
                if ((!name || strcmp(ifmt->name, name) < 0) &&
                    strcmp(ifmt->name, last_name) > 0) {
                    name      = ifmt->name;
                    long_name = ifmt->long_name;
                    encode    = 0;
                    device    = is_dev;
                }
                if (name && strcmp(ifmt->name, name) == 0) {
                    decode = 1;
                    device = is_dev;
                }
            }
        }
        if (!name)
            break;
        last_name = name;

        printf(" %c%c%s %-15s %s\n",
               decode ? 'D' : ' ',
               encode ? 'E' : ' ',
               device_only ? "" : (device ? "d" : " "),
               name,
            long_name ? long_name : " ");
    }
    return 0;
}

int show_formats(void *optctx, const char *opt, const char *arg)
{
    return show_formats_devices(optctx, opt, arg, 0, SHOW_DEFAULT);
}

int show_muxers(void *optctx, const char *opt, const char *arg)
{
    return show_formats_devices(optctx, opt, arg, 0, SHOW_MUXERS);
}

int show_demuxers(void *optctx, const char *opt, const char *arg)
{
    return show_formats_devices(optctx, opt, arg, 0, SHOW_DEMUXERS);
}

int show_devices(void *optctx, const char *opt, const char *arg)
{
    return show_formats_devices(optctx, opt, arg, 1, SHOW_DEFAULT);
}

int show_protocols(void *optctx, const char *opt, const char *arg)
{
    void *opaque = NULL;
    const char *name;

    printf("Supported file protocols:\n"
           "Input:\n");
    while ((name = avio_enum_protocols(&opaque, 0)))
        printf("  %s\n", name);
    printf("Output:\n");
    while ((name = avio_enum_protocols(&opaque, 1)))
        printf("  %s\n", name);
    return 0;
}

int show_colors(void *optctx, const char *opt, const char *arg)
{
    const char *name;
    const uint8_t *rgb;
    int i;

    printf("%-32s #RRGGBB\n", "name");

    for (i = 0; name = av_get_known_color_name(i, &rgb); i++)
        printf("%-32s #%02x%02x%02x\n", name, rgb[0], rgb[1], rgb[2]);

    return 0;
}

int show_pix_fmts(void *optctx, const char *opt, const char *arg)
{
    const AVPixFmtDescriptor *pix_desc = NULL;

    printf("Pixel formats:\n"
           "I.... = Supported Input  format for conversion\n"
           ".O... = Supported Output format for conversion\n"
           "..H.. = Hardware accelerated format\n"
           "...P. = Paletted format\n"
           "....B = Bitstream format\n"
           "FLAGS NAME            NB_COMPONENTS BITS_PER_PIXEL BIT_DEPTHS\n"
           "-----\n");

#if !CONFIG_SWSCALE
#   define sws_isSupportedInput(x)  0
#   define sws_isSupportedOutput(x) 0
#endif

    while ((pix_desc = av_pix_fmt_desc_next(pix_desc))) {
        enum AVPixelFormat av_unused pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
        printf("%c%c%c%c%c %-16s       %d            %3d      %d",
               sws_isSupportedInput (pix_fmt)              ? 'I' : '.',
               sws_isSupportedOutput(pix_fmt)              ? 'O' : '.',
               pix_desc->flags & AV_PIX_FMT_FLAG_HWACCEL   ? 'H' : '.',
               pix_desc->flags & AV_PIX_FMT_FLAG_PAL       ? 'P' : '.',
               pix_desc->flags & AV_PIX_FMT_FLAG_BITSTREAM ? 'B' : '.',
               pix_desc->name,
               pix_desc->nb_components,
               av_get_bits_per_pixel(pix_desc),
               pix_desc->comp[0].depth);

        for (unsigned i = 1; i < pix_desc->nb_components; i++)
            printf("-%d", pix_desc->comp[i].depth);
        printf("\n");
    }
    return 0;
}

int show_layouts(void *optctx, const char *opt, const char *arg)
{
    const AVChannelLayout *ch_layout;
    void *iter = NULL;
    char buf[128], buf2[128];
    int i = 0;

    printf("Individual channels:\n"
           "NAME           DESCRIPTION\n");
    for (i = 0; i < 63; i++) {
        av_channel_name(buf, sizeof(buf), i);
        if (strstr(buf, "USR"))
            continue;
        av_channel_description(buf2, sizeof(buf2), i);
        printf("%-14s %s\n", buf, buf2);
    }
    printf("\nStandard channel layouts:\n"
           "NAME           DECOMPOSITION\n");
    while (ch_layout = av_channel_layout_standard(&iter)) {
            av_channel_layout_describe(ch_layout, buf, sizeof(buf));
            printf("%-14s ", buf);
            for (i = 0; i < 63; i++) {
                int idx = av_channel_layout_index_from_channel(ch_layout, i);
                if (idx >= 0) {
                    av_channel_name(buf2, sizeof(buf2), i);
                    printf("%s%s", idx ? "+" : "", buf2);
                }
            }
            printf("\n");
    }
    return 0;
}

int show_sample_fmts(void *optctx, const char *opt, const char *arg)
{
    int i;
    char fmt_str[128];
    for (i = -1; i < AV_SAMPLE_FMT_NB; i++)
        printf("%s\n", av_get_sample_fmt_string(fmt_str, sizeof(fmt_str), i));
    return 0;
}

int show_dispositions(void *optctx, const char *opt, const char *arg)
{
    for (int i = 0; i < 32; i++) {
        const char *str = av_disposition_to_string(1U << i);
        if (str)
            printf("%s\n", str);
    }
    return 0;
}

int opt_cpuflags(void *optctx, const char *opt, const char *arg)
{
    int ret;
    unsigned flags = av_get_cpu_flags();

    if ((ret = av_parse_cpu_caps(&flags, arg)) < 0)
        return ret;

    av_force_cpu_flags(flags);
    return 0;
}

int opt_cpucount(void *optctx, const char *opt, const char *arg)
{
    int ret;
    int count;

    static const AVOption opts[] = {
        {"count", NULL, 0, AV_OPT_TYPE_INT, { .i64 = -1}, -1, INT_MAX},
        {NULL},
    };
    static const AVClass class = {
        .class_name = "cpucount",
        .item_name  = av_default_item_name,
        .option     = opts,
        .version    = LIBAVUTIL_VERSION_INT,
    };
    const AVClass *pclass = &class;

    ret = av_opt_eval_int(&pclass, opts, arg, &count);

    if (!ret) {
        av_cpu_force_count(count);
    }

    return ret;
}

static void expand_filename_template(AVBPrint *bp, const char *template,
                                     struct tm *tm)
{
    int c;

    while ((c = *(template++))) {
        if (c == '%') {
            if (!(c = *(template++)))
                break;
            switch (c) {
            case 'p':
                av_bprintf(bp, "%s", program_name);
                break;
            case 't':
                av_bprintf(bp, "%04d%02d%02d-%02d%02d%02d",
                           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                           tm->tm_hour, tm->tm_min, tm->tm_sec);
                break;
            case '%':
                av_bprint_chars(bp, c, 1);
                break;
            }
        } else {
            av_bprint_chars(bp, c, 1);
        }
    }
}

static void log_callback_report(void *ptr, int level, const char *fmt, va_list vl)
{
    va_list vl2;
    char line[1024];
    static int print_prefix = 1;

    va_copy(vl2, vl);
    av_log_default_callback(ptr, level, fmt, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);
    if (report_file_level >= level) {
        fputs(line, report_file);
        fflush(report_file);
    }
}

int init_report(const char *env, FILE **file)
{
    char *filename_template = NULL;
    char *key, *val;
    int ret, count = 0;
    int prog_loglevel, envlevel = 0;
    time_t now;
    struct tm *tm;
    AVBPrint filename;

    if (report_file) /* already opened */
        return 0;
    time(&now);
    tm = localtime(&now);

    while (env && *env) {
        if ((ret = av_opt_get_key_value(&env, "=", ":", 0, &key, &val)) < 0) {
            if (count)
                av_log(NULL, AV_LOG_ERROR,
                       "Failed to parse FFREPORT environment variable: %s\n",
                       av_err2str(ret));
            break;
        }
        if (*env)
            env++;
        count++;
        if (!strcmp(key, "file")) {
            av_free(filename_template);
            filename_template = val;
            val = NULL;
        } else if (!strcmp(key, "level")) {
            char *tail;
            report_file_level = strtol(val, &tail, 10);
            if (*tail) {
                av_log(NULL, AV_LOG_FATAL, "Invalid report file level\n");
                av_free(key);
                av_free(val);
                av_free(filename_template);
                return AVERROR(EINVAL);
            }
            envlevel = 1;
        } else {
            av_log(NULL, AV_LOG_ERROR, "Unknown key '%s' in FFREPORT\n", key);
        }
        av_free(val);
        av_free(key);
    }

    av_bprint_init(&filename, 0, AV_BPRINT_SIZE_AUTOMATIC);
    expand_filename_template(&filename,
                             av_x_if_null(filename_template, "%p-%t.log"), tm);
    av_free(filename_template);
    if (!av_bprint_is_complete(&filename)) {
        av_log(NULL, AV_LOG_ERROR, "Out of memory building report file name\n");
        return AVERROR(ENOMEM);
    }

    prog_loglevel = av_log_get_level();
    if (!envlevel)
        report_file_level = FFMAX(report_file_level, prog_loglevel);

    report_file = fopen(filename.str, "w");
    if (!report_file) {
        int ret = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR, "Failed to open report \"%s\": %s\n",
               filename.str, strerror(errno));
        return ret;
    }
    av_log_set_callback(log_callback_report);
    av_log(NULL, AV_LOG_INFO,
           "%s started on %04d-%02d-%02d at %02d:%02d:%02d\n"
           "Report written to \"%s\"\n"
           "Log level: %d\n",
           program_name,
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec,
           filename.str, report_file_level);
    av_bprint_finalize(&filename, NULL);

    if (file)
        *file = report_file;

    return 0;
}

int opt_report(void *optctx, const char *opt, const char *arg)
{
    return init_report(NULL, NULL);
}

int opt_max_alloc(void *optctx, const char *opt, const char *arg)
{
    char *tail;
    size_t max;

    max = strtol(arg, &tail, 10);
    if (*tail) {
        av_log(NULL, AV_LOG_FATAL, "Invalid max_alloc \"%s\".\n", arg);
        return AVERROR(EINVAL);
    }
    av_max_alloc(max);
    return 0;
}

int opt_loglevel(void *optctx, const char *opt, const char *arg)
{
    const struct { const char *name; int level; } log_levels[] = {
        { "quiet"  , AV_LOG_QUIET   },
        { "panic"  , AV_LOG_PANIC   },
        { "fatal"  , AV_LOG_FATAL   },
        { "error"  , AV_LOG_ERROR   },
        { "warning", AV_LOG_WARNING },
        { "info"   , AV_LOG_INFO    },
        { "verbose", AV_LOG_VERBOSE },
        { "debug"  , AV_LOG_DEBUG   },
        { "trace"  , AV_LOG_TRACE   },
    };
    const char *token;
    char *tail;
    int flags = av_log_get_flags();
    int level = av_log_get_level();
    int cmd, i = 0;

    av_assert0(arg);
    while (*arg) {
        token = arg;
        if (*token == '+' || *token == '-') {
            cmd = *token++;
        } else {
            cmd = 0;
        }
        if (!i && !cmd) {
            flags = 0;  /* missing relative prefix, build absolute value */
        }
        if (av_strstart(token, "repeat", &arg)) {
            if (cmd == '-') {
                flags |= AV_LOG_SKIP_REPEATED;
            } else {
                flags &= ~AV_LOG_SKIP_REPEATED;
            }
        } else if (av_strstart(token, "level", &arg)) {
            if (cmd == '-') {
                flags &= ~AV_LOG_PRINT_LEVEL;
            } else {
                flags |= AV_LOG_PRINT_LEVEL;
            }
        } else {
            break;
        }
        i++;
    }
    if (!*arg) {
        goto end;
    } else if (*arg == '+') {
        arg++;
    } else if (!i) {
        flags = av_log_get_flags();  /* level value without prefix, reset flags */
    }

    for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++) {
        if (!strcmp(log_levels[i].name, arg)) {
            level = log_levels[i].level;
            goto end;
        }
    }

    level = strtol(arg, &tail, 10);
    if (*tail) {
        av_log(NULL, AV_LOG_FATAL, "Invalid loglevel \"%s\". "
               "Possible levels are numbers or:\n", arg);
        for (i = 0; i < FF_ARRAY_ELEMS(log_levels); i++)
            av_log(NULL, AV_LOG_FATAL, "\"%s\"\n", log_levels[i].name);
        return AVERROR(EINVAL);
    }

end:
    av_log_set_flags(flags);
    av_log_set_level(level);
    return 0;
}

#if CONFIG_AVDEVICE
static void print_device_list(const AVDeviceInfoList *device_list)
{
    // print devices
    for (int i = 0; i < device_list->nb_devices; i++) {
        const AVDeviceInfo *device = device_list->devices[i];
        printf("%c %s [%s] (", device_list->default_device == i ? '*' : ' ',
            device->device_name, device->device_description);
        if (device->nb_media_types > 0) {
            for (int j = 0; j < device->nb_media_types; ++j) {
                const char* media_type = av_get_media_type_string(device->media_types[j]);
                if (j > 0)
                    printf(", ");
                printf("%s", media_type ? media_type : "unknown");
            }
        } else {
            printf("none");
        }
        printf(")\n");
    }
}

static int print_device_sources(const AVInputFormat *fmt, AVDictionary *opts)
{
    int ret;
    AVDeviceInfoList *device_list = NULL;

    if (!fmt || !fmt->priv_class  || !AV_IS_INPUT_DEVICE(fmt->priv_class->category))
        return AVERROR(EINVAL);

    printf("Auto-detected sources for %s:\n", fmt->name);
    if ((ret = avdevice_list_input_sources(fmt, NULL, opts, &device_list)) < 0) {
        printf("Cannot list sources: %s\n", av_err2str(ret));
        goto fail;
    }

    print_device_list(device_list);

  fail:
    avdevice_free_list_devices(&device_list);
    return ret;
}

static int print_device_sinks(const AVOutputFormat *fmt, AVDictionary *opts)
{
    int ret;
    AVDeviceInfoList *device_list = NULL;

    if (!fmt || !fmt->priv_class  || !AV_IS_OUTPUT_DEVICE(fmt->priv_class->category))
        return AVERROR(EINVAL);

    printf("Auto-detected sinks for %s:\n", fmt->name);
    if ((ret = avdevice_list_output_sinks(fmt, NULL, opts, &device_list)) < 0) {
        printf("Cannot list sinks: %s\n", av_err2str(ret));
        goto fail;
    }

    print_device_list(device_list);

  fail:
    avdevice_free_list_devices(&device_list);
    return ret;
}

static int show_sinks_sources_parse_arg(const char *arg, char **dev, AVDictionary **opts)
{
    int ret;
    if (arg) {
        char *opts_str = NULL;
        av_assert0(dev && opts);
        *dev = av_strdup(arg);
        if (!*dev)
            return AVERROR(ENOMEM);
        if ((opts_str = strchr(*dev, ','))) {
            *(opts_str++) = '\0';
            if (opts_str[0] && ((ret = av_dict_parse_string(opts, opts_str, "=", ":", 0)) < 0)) {
                av_freep(dev);
                return ret;
            }
        }
    } else
        printf("\nDevice name is not provided.\n"
                "You can pass devicename[,opt1=val1[,opt2=val2...]] as an argument.\n\n");
    return 0;
}

int show_sources(void *optctx, const char *opt, const char *arg)
{
    const AVInputFormat *fmt = NULL;
    char *dev = NULL;
    AVDictionary *opts = NULL;
    int ret = 0;
    int error_level = av_log_get_level();

    av_log_set_level(AV_LOG_WARNING);

    if ((ret = show_sinks_sources_parse_arg(arg, &dev, &opts)) < 0)
        goto fail;

    do {
        fmt = av_input_audio_device_next(fmt);
        if (fmt) {
            if (!strcmp(fmt->name, "lavfi"))
                continue; //it's pointless to probe lavfi
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sources(fmt, opts);
        }
    } while (fmt);
    do {
        fmt = av_input_video_device_next(fmt);
        if (fmt) {
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sources(fmt, opts);
        }
    } while (fmt);
  fail:
    av_dict_free(&opts);
    av_free(dev);
    av_log_set_level(error_level);
    return ret;
}

int show_sinks(void *optctx, const char *opt, const char *arg)
{
    const AVOutputFormat *fmt = NULL;
    char *dev = NULL;
    AVDictionary *opts = NULL;
    int ret = 0;
    int error_level = av_log_get_level();

    av_log_set_level(AV_LOG_WARNING);

    if ((ret = show_sinks_sources_parse_arg(arg, &dev, &opts)) < 0)
        goto fail;

    do {
        fmt = av_output_audio_device_next(fmt);
        if (fmt) {
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sinks(fmt, opts);
        }
    } while (fmt);
    do {
        fmt = av_output_video_device_next(fmt);
        if (fmt) {
            if (dev && !av_match_name(dev, fmt->name))
                continue;
            print_device_sinks(fmt, opts);
        }
    } while (fmt);
  fail:
    av_dict_free(&opts);
    av_free(dev);
    av_log_set_level(error_level);
    return ret;
}
#endif /* CONFIG_AVDEVICE */
