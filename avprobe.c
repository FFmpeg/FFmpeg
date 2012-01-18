/*
 * avprobe : Simple Media Prober based on the Libav libraries
 * Copyright (c) 2007-2010 Stefano Sabatini
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavdevice/avdevice.h"
#include "cmdutils.h"

const char program_name[] = "avprobe";
const int program_birth_year = 2007;

static int do_show_format  = 0;
static int do_show_packets = 0;
static int do_show_streams = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;

/* globals */
static const OptionDef options[];

/* AVprobe context */
static const char *input_filename;
static AVInputFormat *iformat = NULL;

static const char *binary_unit_prefixes [] = { "", "Ki", "Mi", "Gi", "Ti", "Pi" };
static const char *decimal_unit_prefixes[] = { "", "K" , "M" , "G" , "T" , "P"  };

static const char *unit_second_str          = "s"    ;
static const char *unit_hertz_str           = "Hz"   ;
static const char *unit_byte_str            = "byte" ;
static const char *unit_bit_per_second_str  = "bit/s";

void exit_program(int ret)
{
    exit(ret);
}

static char *value_string(char *buf, int buf_size, double val, const char *unit)
{
    if (unit == unit_second_str && use_value_sexagesimal_format) {
        double secs;
        int hours, mins;
        secs  = val;
        mins  = (int)secs / 60;
        secs  = secs - mins * 60;
        hours = mins / 60;
        mins %= 60;
        snprintf(buf, buf_size, "%d:%02d:%09.6f", hours, mins, secs);
    } else if (use_value_prefix) {
        const char *prefix_string;
        int index;

        if (unit == unit_byte_str && use_byte_value_binary_prefix) {
            index = (int) (log(val)/log(2)) / 10;
            index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) - 1);
            val  /= pow(2, index * 10);
            prefix_string = binary_unit_prefixes[index];
        } else {
            index = (int) (log10(val)) / 3;
            index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) - 1);
            val  /= pow(10, index * 3);
            prefix_string = decimal_unit_prefixes[index];
        }

        snprintf(buf, buf_size, "%.3f %s%s", val, prefix_string,
                 show_value_unit ? unit : "");
    } else {
        snprintf(buf, buf_size, "%f %s", val, show_value_unit ? unit : "");
    }

    return buf;
}

static char *time_value_string(char *buf, int buf_size, int64_t val,
                               const AVRational *time_base)
{
    if (val == AV_NOPTS_VALUE) {
        snprintf(buf, buf_size, "N/A");
    } else {
        value_string(buf, buf_size, val * av_q2d(*time_base), unit_second_str);
    }

    return buf;
}

static char *ts_value_string (char *buf, int buf_size, int64_t ts)
{
    if (ts == AV_NOPTS_VALUE) {
        snprintf(buf, buf_size, "N/A");
    } else {
        snprintf(buf, buf_size, "%"PRId64, ts);
    }

    return buf;
}

static const char *media_type_string(enum AVMediaType media_type)
{
    switch (media_type) {
    case AVMEDIA_TYPE_VIDEO:      return "video";
    case AVMEDIA_TYPE_AUDIO:      return "audio";
    case AVMEDIA_TYPE_DATA:       return "data";
    case AVMEDIA_TYPE_SUBTITLE:   return "subtitle";
    case AVMEDIA_TYPE_ATTACHMENT: return "attachment";
    default:                      return "unknown";
    }
}

static void show_packet(AVFormatContext *fmt_ctx, AVPacket *pkt)
{
    char val_str[128];
    AVStream *st = fmt_ctx->streams[pkt->stream_index];

    printf("[PACKET]\n");
    printf("codec_type=%s\n", media_type_string(st->codec->codec_type));
    printf("stream_index=%d\n", pkt->stream_index);
    printf("pts=%s\n", ts_value_string(val_str, sizeof(val_str), pkt->pts));
    printf("pts_time=%s\n", time_value_string(val_str, sizeof(val_str),
                                              pkt->pts, &st->time_base));
    printf("dts=%s\n", ts_value_string(val_str, sizeof(val_str), pkt->dts));
    printf("dts_time=%s\n", time_value_string(val_str, sizeof(val_str),
                                              pkt->dts, &st->time_base));
    printf("duration=%s\n", ts_value_string(val_str, sizeof(val_str),
                                            pkt->duration));
    printf("duration_time=%s\n", time_value_string(val_str, sizeof(val_str),
                                                   pkt->duration,
                                                   &st->time_base));
    printf("size=%s\n", value_string(val_str, sizeof(val_str),
                                     pkt->size, unit_byte_str));
    printf("pos=%"PRId64"\n", pkt->pos);
    printf("flags=%c\n", pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_');
    printf("[/PACKET]\n");
}

static void show_packets(AVFormatContext *fmt_ctx)
{
    AVPacket pkt;

    av_init_packet(&pkt);

    while (!av_read_frame(fmt_ctx, &pkt))
        show_packet(fmt_ctx, &pkt);
}

static void show_stream(AVFormatContext *fmt_ctx, int stream_idx)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    AVCodec *dec;
    char val_str[128];
    AVDictionaryEntry *tag = NULL;
    AVRational display_aspect_ratio;

    printf("[STREAM]\n");

    printf("index=%d\n", stream->index);

    if ((dec_ctx = stream->codec)) {
        if ((dec = dec_ctx->codec)) {
            printf("codec_name=%s\n", dec->name);
            printf("codec_long_name=%s\n", dec->long_name);
        } else {
            printf("codec_name=unknown\n");
        }

        printf("codec_type=%s\n", media_type_string(dec_ctx->codec_type));
        printf("codec_time_base=%d/%d\n",
               dec_ctx->time_base.num, dec_ctx->time_base.den);

        /* print AVI/FourCC tag */
        av_get_codec_tag_string(val_str, sizeof(val_str), dec_ctx->codec_tag);
        printf("codec_tag_string=%s\n", val_str);
        printf("codec_tag=0x%04x\n", dec_ctx->codec_tag);

        switch (dec_ctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            printf("width=%d\n", dec_ctx->width);
            printf("height=%d\n", dec_ctx->height);
            printf("has_b_frames=%d\n", dec_ctx->has_b_frames);
            if (dec_ctx->sample_aspect_ratio.num) {
                printf("sample_aspect_ratio=%d:%d\n",
                       dec_ctx->sample_aspect_ratio.num,
                       dec_ctx->sample_aspect_ratio.den);
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          dec_ctx->width  * dec_ctx->sample_aspect_ratio.num,
                          dec_ctx->height * dec_ctx->sample_aspect_ratio.den,
                          1024*1024);
                printf("display_aspect_ratio=%d:%d\n",
                       display_aspect_ratio.num, display_aspect_ratio.den);
            }
            printf("pix_fmt=%s\n",
                   dec_ctx->pix_fmt != PIX_FMT_NONE ? av_pix_fmt_descriptors[dec_ctx->pix_fmt].name
                                                    : "unknown");
            printf("level=%d\n", dec_ctx->level);
            break;

        case AVMEDIA_TYPE_AUDIO:
            printf("sample_rate=%s\n", value_string(val_str, sizeof(val_str),
                                                    dec_ctx->sample_rate,
                                                    unit_hertz_str));
            printf("channels=%d\n", dec_ctx->channels);
            printf("bits_per_sample=%d\n",
                   av_get_bits_per_sample(dec_ctx->codec_id));
            break;
        }
    } else {
        printf("codec_type=unknown\n");
    }

    if (fmt_ctx->iformat->flags & AVFMT_SHOW_IDS)
        printf("id=0x%x\n", stream->id);
    printf("r_frame_rate=%d/%d\n",
           stream->r_frame_rate.num, stream->r_frame_rate.den);
    printf("avg_frame_rate=%d/%d\n",
           stream->avg_frame_rate.num, stream->avg_frame_rate.den);
    printf("time_base=%d/%d\n",
           stream->time_base.num, stream->time_base.den);
    printf("start_time=%s\n",
           time_value_string(val_str, sizeof(val_str),
                             stream->start_time, &stream->time_base));
    printf("duration=%s\n",
           time_value_string(val_str, sizeof(val_str),
                             stream->duration, &stream->time_base));
    if (stream->nb_frames)
        printf("nb_frames=%"PRId64"\n", stream->nb_frames);

    while ((tag = av_dict_get(stream->metadata, "", tag,
                              AV_DICT_IGNORE_SUFFIX)))
        printf("TAG:%s=%s\n", tag->key, tag->value);

    printf("[/STREAM]\n");
}

static void show_format(AVFormatContext *fmt_ctx)
{
    AVDictionaryEntry *tag = NULL;
    char val_str[128];
    int64_t size = fmt_ctx->pb ? avio_size(fmt_ctx->pb) : -1;

    printf("[FORMAT]\n");

    printf("filename=%s\n", fmt_ctx->filename);
    printf("nb_streams=%d\n", fmt_ctx->nb_streams);
    printf("format_name=%s\n", fmt_ctx->iformat->name);
    printf("format_long_name=%s\n", fmt_ctx->iformat->long_name);
    printf("start_time=%s\n",
           time_value_string(val_str, sizeof(val_str),
                             fmt_ctx->start_time, &AV_TIME_BASE_Q));
    printf("duration=%s\n",
           time_value_string(val_str, sizeof(val_str),
                             fmt_ctx->duration, &AV_TIME_BASE_Q));
    printf("size=%s\n", size >= 0 ? value_string(val_str, sizeof(val_str),
                                                 size, unit_byte_str)
                                  : "unknown");
    printf("bit_rate=%s\n",
           value_string(val_str, sizeof(val_str),
                        fmt_ctx->bit_rate, unit_bit_per_second_str));

    while ((tag = av_dict_get(fmt_ctx->metadata, "", tag,
                              AV_DICT_IGNORE_SUFFIX)))
        printf("TAG:%s=%s\n", tag->key, tag->value);

    printf("[/FORMAT]\n");
}

static int open_input_file(AVFormatContext **fmt_ctx_ptr, const char *filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;

    if ((err = avformat_open_input(&fmt_ctx, filename,
                                   iformat, &format_opts)) < 0) {
        print_error(filename, err);
        return err;
    }
    if ((t = av_dict_get(format_opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_ERROR, "Option %s not found.\n", t->key);
        return AVERROR_OPTION_NOT_FOUND;
    }


    /* fill the streams in the format context */
    if ((err = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        print_error(filename, err);
        return err;
    }

    av_dump_format(fmt_ctx, 0, filename, 0);

    /* bind a decoder to each input stream */
    for (i = 0; i < fmt_ctx->nb_streams; i++) {
        AVStream *stream = fmt_ctx->streams[i];
        AVCodec *codec;

        if (!(codec = avcodec_find_decoder(stream->codec->codec_id))) {
            fprintf(stderr,
                    "Unsupported codec with id %d for input stream %d\n",
                    stream->codec->codec_id, stream->index);
        } else if (avcodec_open2(stream->codec, codec, NULL) < 0) {
            fprintf(stderr, "Error while opening codec for input stream %d\n",
                    stream->index);
        }
    }

    *fmt_ctx_ptr = fmt_ctx;
    return 0;
}

static int probe_file(const char *filename)
{
    AVFormatContext *fmt_ctx;
    int ret, i;

    if ((ret = open_input_file(&fmt_ctx, filename)))
        return ret;

    if (do_show_packets)
        show_packets(fmt_ctx);

    if (do_show_streams)
        for (i = 0; i < fmt_ctx->nb_streams; i++)
            show_stream(fmt_ctx, i);

    if (do_show_format)
        show_format(fmt_ctx);

    avformat_close_input(&fmt_ctx);
    return 0;
}

static void show_usage(void)
{
    printf("Simple multimedia streams analyzer\n");
    printf("usage: %s [OPTIONS] [INPUT_FILE]\n", program_name);
    printf("\n");
}

static int opt_format(const char *opt, const char *arg)
{
    iformat = av_find_input_format(arg);
    if (!iformat) {
        fprintf(stderr, "Unknown input format: %s\n", arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static void opt_input_file(void *optctx, const char *arg)
{
    if (input_filename) {
        fprintf(stderr,
                "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        exit(1);
    }
    if (!strcmp(arg, "-"))
        arg = "pipe:";
    input_filename = arg;
}

static void show_help(void)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:\n", 0, 0);
    printf("\n");
    show_help_children(avformat_get_class(), AV_OPT_FLAG_DECODING_PARAM);
}

static void opt_pretty(void)
{
    show_value_unit              = 1;
    use_value_prefix             = 1;
    use_byte_value_binary_prefix = 1;
    use_value_sexagesimal_format = 1;
}

static const OptionDef options[] = {
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "format" },
    { "unit", OPT_BOOL, {(void*)&show_value_unit},
      "show unit of the displayed values" },
    { "prefix", OPT_BOOL, {(void*)&use_value_prefix},
      "use SI prefixes for the displayed values" },
    { "byte_binary_prefix", OPT_BOOL, {(void*)&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal", OPT_BOOL,  {(void*)&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty", 0, {(void*)&opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "show_format",  OPT_BOOL, {(void*)&do_show_format} , "show format/container info" },
    { "show_packets", OPT_BOOL, {(void*)&do_show_packets}, "show packets info" },
    { "show_streams", OPT_BOOL, {(void*)&do_show_streams}, "show streams info" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default},
      "generic catch all option", "" },
    { NULL, },
};

int main(int argc, char **argv)
{
    int ret;

    parse_loglevel(argc, argv, options);
    av_register_all();
    avformat_network_init();
    init_opts();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner();
    parse_options(NULL, argc, argv, options, opt_input_file);

    if (!input_filename) {
        show_usage();
        fprintf(stderr, "You have to specify one input file.\n");
        fprintf(stderr,
                "Use -h to get full help or, even better, run 'man %s'.\n",
                program_name);
        exit(1);
    }

    ret = probe_file(input_filename);

    avformat_network_deinit();

    return ret;
}
