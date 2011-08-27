/*
 * ffprobe : Simple Media Prober based on the FFmpeg libraries
 * Copyright (c) 2007-2010 Stefano Sabatini
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

#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavdevice/avdevice.h"
#include "cmdutils.h"

const char program_name[] = "ffprobe";
const int program_birth_year = 2007;

static int do_show_format  = 0;
static int do_show_packets = 0;
static int do_show_streams = 0;

static int show_value_unit              = 0;
static int use_value_prefix             = 0;
static int use_byte_value_binary_prefix = 0;
static int use_value_sexagesimal_format = 0;

static char *print_format;

/* globals */
static const OptionDef options[];

/* FFprobe context */
static const char *input_filename;
static AVInputFormat *iformat = NULL;

static const char *binary_unit_prefixes [] = { "", "Ki", "Mi", "Gi", "Ti", "Pi" };
static const char *decimal_unit_prefixes[] = { "", "K" , "M" , "G" , "T" , "P"  };

static const char *unit_second_str          = "s"    ;
static const char *unit_hertz_str           = "Hz"   ;
static const char *unit_byte_str            = "byte" ;
static const char *unit_bit_per_second_str  = "bit/s";

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
            index = av_clip(index, 0, FF_ARRAY_ELEMS(binary_unit_prefixes) -1);
            val /= pow(2, index*10);
            prefix_string = binary_unit_prefixes[index];
        } else {
            index = (int) (log10(val)) / 3;
            index = av_clip(index, 0, FF_ARRAY_ELEMS(decimal_unit_prefixes) -1);
            val /= pow(10, index*3);
            prefix_string = decimal_unit_prefixes[index];
        }

        snprintf(buf, buf_size, "%.3f%s%s%s", val, prefix_string || show_value_unit ? " " : "",
                 prefix_string, show_value_unit ? unit : "");
    } else {
        snprintf(buf, buf_size, "%f%s%s", val, show_value_unit ? " " : "",
                 show_value_unit ? unit : "");
    }

    return buf;
}

static char *time_value_string(char *buf, int buf_size, int64_t val, const AVRational *time_base)
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
    const char *s = av_get_media_type_string(media_type);
    return s ? s : "unknown";
}


struct writer {
    const char *name;
    const char *item_sep;           ///< separator between key/value couples
    const char *items_sep;          ///< separator between sets of key/value couples
    const char *section_sep;        ///< separator between sections (streams, packets, ...)
    const char *header, *footer;
    void (*print_header)(const char *);
    void (*print_footer)(const char *);
    void (*print_fmt_f)(const char *, const char *, va_list ap);
    void (*print_int_f)(const char *, int);
    void (*show_tags)(struct writer *w, AVDictionary *dict);
};


/* Default output */

static void default_print_header(const char *section)
{
    printf("[%s]\n", section);
}

static void default_print_fmt(const char *key, const char *fmt, va_list ap)
{
    printf("%s=", key);
    vprintf(fmt, ap);
}

static void default_print_int(const char *key, int value)
{
    printf("%s=%d", key, value);
}

static void default_print_footer(const char *section)
{
    printf("\n[/%s]", section);
}


/* Print helpers */

static void print_vfmt0(struct writer *w, const char *key, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    w->print_fmt_f(key, fmt, ap);
    va_end(ap);
}

#define print_fmt0(k, f, a...) print_vfmt0(w, k, f, ##a)
#define print_fmt( k, f, a...) do {   \
    if (w->item_sep)                  \
        printf("%s", w->item_sep);    \
    print_vfmt0(w, k, f, ##a);        \
} while (0)

#define print_int0(k, v) w->print_int_f(k, v)
#define print_int( k, v) do {      \
    if (w->item_sep)               \
        printf("%s", w->item_sep); \
    print_int0(k, v);              \
} while (0)

#define print_str0(k, v) print_fmt0(k, "%s", v)
#define print_str( k, v) print_fmt (k, "%s", v)


static void show_packet(struct writer *w, AVFormatContext *fmt_ctx, AVPacket *pkt, int packet_idx)
{
    char val_str[128];
    AVStream *st = fmt_ctx->streams[pkt->stream_index];

    if (packet_idx)
        printf("%s", w->items_sep);
    w->print_header("PACKET");
    print_str0("codec_type",      media_type_string(st->codec->codec_type));
    print_int("stream_index",     pkt->stream_index);
    print_str("pts",              ts_value_string  (val_str, sizeof(val_str), pkt->pts));
    print_str("pts_time",         time_value_string(val_str, sizeof(val_str), pkt->pts, &st->time_base));
    print_str("dts",              ts_value_string  (val_str, sizeof(val_str), pkt->dts));
    print_str("dts_time",         time_value_string(val_str, sizeof(val_str), pkt->dts, &st->time_base));
    print_str("duration",         ts_value_string  (val_str, sizeof(val_str), pkt->duration));
    print_str("duration_time",    time_value_string(val_str, sizeof(val_str), pkt->duration, &st->time_base));
    print_str("size",             value_string     (val_str, sizeof(val_str), pkt->size, unit_byte_str));
    print_fmt("pos",   "%"PRId64, pkt->pos);
    print_fmt("flags", "%c",      pkt->flags & AV_PKT_FLAG_KEY ? 'K' : '_');
    w->print_footer("PACKET");
    fflush(stdout);
}

static void show_packets(struct writer *w, AVFormatContext *fmt_ctx)
{
    AVPacket pkt;
    int i = 0;

    av_init_packet(&pkt);

    while (!av_read_frame(fmt_ctx, &pkt))
        show_packet(w, fmt_ctx, &pkt, i++);
}

static void default_show_tags(struct writer *w, AVDictionary *dict)
{
    AVDictionaryEntry *tag = NULL;
    while ((tag = av_dict_get(dict, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        printf("\nTAG:");
        print_str0(tag->key, tag->value);
    }
}

static void show_stream(struct writer *w, AVFormatContext *fmt_ctx, int stream_idx)
{
    AVStream *stream = fmt_ctx->streams[stream_idx];
    AVCodecContext *dec_ctx;
    AVCodec *dec;
    char val_str[128];
    AVRational display_aspect_ratio;

    if (stream_idx)
        printf("%s", w->items_sep);
    w->print_header("STREAM");

    print_int0("index", stream->index);

    if ((dec_ctx = stream->codec)) {
        if ((dec = dec_ctx->codec)) {
            print_str("codec_name",      dec->name);
            print_str("codec_long_name", dec->long_name);
        } else {
            print_str("codec_name",      "unknown");
        }

        print_str("codec_type",               media_type_string(dec_ctx->codec_type));
        print_fmt("codec_time_base", "%d/%d", dec_ctx->time_base.num, dec_ctx->time_base.den);

        /* print AVI/FourCC tag */
        av_get_codec_tag_string(val_str, sizeof(val_str), dec_ctx->codec_tag);
        print_str("codec_tag_string",    val_str);
        print_fmt("codec_tag", "0x%04x", dec_ctx->codec_tag);

        switch (dec_ctx->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            print_int("width",        dec_ctx->width);
            print_int("height",       dec_ctx->height);
            print_int("has_b_frames", dec_ctx->has_b_frames);
            if (dec_ctx->sample_aspect_ratio.num) {
                print_fmt("sample_aspect_ratio", "%d:%d",
                          dec_ctx->sample_aspect_ratio.num,
                          dec_ctx->sample_aspect_ratio.den);
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          dec_ctx->width  * dec_ctx->sample_aspect_ratio.num,
                          dec_ctx->height * dec_ctx->sample_aspect_ratio.den,
                          1024*1024);
                print_fmt("display_aspect_ratio", "%d:%d",
                          display_aspect_ratio.num,
                          display_aspect_ratio.den);
            }
            print_str("pix_fmt", dec_ctx->pix_fmt != PIX_FMT_NONE ? av_pix_fmt_descriptors[dec_ctx->pix_fmt].name : "unknown");
            print_int("level",   dec_ctx->level);
            break;

        case AVMEDIA_TYPE_AUDIO:
            print_str("sample_rate",     value_string(val_str, sizeof(val_str), dec_ctx->sample_rate, unit_hertz_str));
            print_int("channels",        dec_ctx->channels);
            print_int("bits_per_sample", av_get_bits_per_sample(dec_ctx->codec_id));
            break;
        }
    } else {
        print_fmt("codec_type", "unknown");
    }

    if (fmt_ctx->iformat->flags & AVFMT_SHOW_IDS)
        print_fmt("id=", "0x%x", stream->id);
    print_fmt("r_frame_rate",   "%d/%d", stream->r_frame_rate.num,   stream->r_frame_rate.den);
    print_fmt("avg_frame_rate", "%d/%d", stream->avg_frame_rate.num, stream->avg_frame_rate.den);
    print_fmt("time_base",      "%d/%d", stream->time_base.num,      stream->time_base.den);
    print_str("start_time", time_value_string(val_str, sizeof(val_str), stream->start_time, &stream->time_base));
    print_str("duration",   time_value_string(val_str, sizeof(val_str), stream->duration,   &stream->time_base));
    if (stream->nb_frames)
        print_fmt("nb_frames", "%"PRId64, stream->nb_frames);

    w->show_tags(w, stream->metadata);

    w->print_footer("STREAM");
    fflush(stdout);
}

static void show_streams(struct writer *w, AVFormatContext *fmt_ctx)
{
    int i;
    for (i = 0; i < fmt_ctx->nb_streams; i++)
        show_stream(w, fmt_ctx, i);
}

static void show_format(struct writer *w, AVFormatContext *fmt_ctx)
{
    char val_str[128];

    w->print_header("FORMAT");
    print_str0("filename",        fmt_ctx->filename);
    print_int("nb_streams",       fmt_ctx->nb_streams);
    print_str("format_name",      fmt_ctx->iformat->name);
    print_str("format_long_name", fmt_ctx->iformat->long_name);
    print_str("start_time",       time_value_string(val_str, sizeof(val_str), fmt_ctx->start_time, &AV_TIME_BASE_Q));
    print_str("duration",         time_value_string(val_str, sizeof(val_str), fmt_ctx->duration,   &AV_TIME_BASE_Q));
    print_str("size",             value_string(val_str, sizeof(val_str), fmt_ctx->file_size, unit_byte_str));
    print_str("bit_rate",         value_string(val_str, sizeof(val_str), fmt_ctx->bit_rate,  unit_bit_per_second_str));
    w->show_tags(w, fmt_ctx->metadata);
    w->print_footer("FORMAT");
    fflush(stdout);
}

static int open_input_file(AVFormatContext **fmt_ctx_ptr, const char *filename)
{
    int err, i;
    AVFormatContext *fmt_ctx = NULL;
    AVDictionaryEntry *t;

    if ((err = avformat_open_input(&fmt_ctx, filename, iformat, &format_opts)) < 0) {
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
            fprintf(stderr, "Unsupported codec with id %d for input stream %d\n",
                    stream->codec->codec_id, stream->index);
        } else if (avcodec_open2(stream->codec, codec, NULL) < 0) {
            fprintf(stderr, "Error while opening codec for input stream %d\n",
                    stream->index);
        }
    }

    *fmt_ctx_ptr = fmt_ctx;
    return 0;
}

#define WRITER_FUNC(func)                  \
    .print_header = func ## _print_header, \
    .print_footer = func ## _print_footer, \
    .print_fmt_f  = func ## _print_fmt,    \
    .print_int_f  = func ## _print_int,    \
    .show_tags    = func ## _show_tags

static struct writer writers[] = {{
        .name         = "default",
        .item_sep     = "\n",
        .items_sep    = "\n",
        .section_sep  = "\n",
        .footer       = "\n",
        WRITER_FUNC(default),
    }
};

static int get_writer(const char *name)
{
    int i;
    if (!name)
        return 0;
    for (i = 0; i < FF_ARRAY_ELEMS(writers); i++)
        if (!strcmp(writers[i].name, name))
            return i;
    return -1;
}

#define SECTION_PRINT(name, left) do {                        \
    if (do_show_ ## name) {                                   \
        show_ ## name (w, fmt_ctx);                           \
        if (left)                                             \
            printf("%s", w->section_sep);                     \
    }                                                         \
} while (0)

static int probe_file(const char *filename)
{
    AVFormatContext *fmt_ctx;
    int ret, writer_id;
    struct writer *w;

    writer_id = get_writer(print_format);
    if (writer_id < 0) {
        fprintf(stderr, "Invalid output format '%s'\n", print_format);
        return AVERROR(EINVAL);
    }
    w = &writers[writer_id];

    if ((ret = open_input_file(&fmt_ctx, filename)))
        return ret;

    if (w->header)
        printf("%s", w->header);

    SECTION_PRINT(packets, do_show_streams || do_show_format);
    SECTION_PRINT(streams, do_show_format);
    SECTION_PRINT(format,  0);

    if (w->footer)
        printf("%s", w->footer);

    av_close_input_file(fmt_ctx);
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

static int opt_input_file(const char *opt, const char *arg)
{
    if (input_filename) {
        fprintf(stderr, "Argument '%s' provided as input filename, but '%s' was already specified.\n",
                arg, input_filename);
        exit(1);
    }
    if (!strcmp(arg, "-"))
        arg = "pipe:";
    input_filename = arg;
    return 0;
}

static int opt_help(const char *opt, const char *arg)
{
    av_log_set_callback(log_callback_help);
    show_usage();
    show_help_options(options, "Main options:\n", 0, 0);
    printf("\n");
    av_opt_show2(avformat_opts, NULL,
                 AV_OPT_FLAG_DECODING_PARAM, 0);
    return 0;
}

static int opt_pretty(const char *opt, const char *arg)
{
    show_value_unit              = 1;
    use_value_prefix             = 1;
    use_byte_value_binary_prefix = 1;
    use_value_sexagesimal_format = 1;
    return 0;
}

static const OptionDef options[] = {
#include "cmdutils_common_opts.h"
    { "f", HAS_ARG, {(void*)opt_format}, "force format", "format" },
    { "unit", OPT_BOOL, {(void*)&show_value_unit}, "show unit of the displayed values" },
    { "prefix", OPT_BOOL, {(void*)&use_value_prefix}, "use SI prefixes for the displayed values" },
    { "byte_binary_prefix", OPT_BOOL, {(void*)&use_byte_value_binary_prefix},
      "use binary prefixes for byte units" },
    { "sexagesimal", OPT_BOOL,  {(void*)&use_value_sexagesimal_format},
      "use sexagesimal format HOURS:MM:SS.MICROSECONDS for time units" },
    { "pretty", 0, {(void*)&opt_pretty},
      "prettify the format of displayed values, make it more human readable" },
    { "show_format",  OPT_BOOL, {(void*)&do_show_format} , "show format/container info" },
    { "show_packets", OPT_BOOL, {(void*)&do_show_packets}, "show packets info" },
    { "show_streams", OPT_BOOL, {(void*)&do_show_streams}, "show streams info" },
    { "default", HAS_ARG | OPT_AUDIO | OPT_VIDEO | OPT_EXPERT, {(void*)opt_default}, "generic catch all option", "" },
    { "i", HAS_ARG, {(void *)opt_input_file}, "read specified file", "input_file"},
    { NULL, },
};

int main(int argc, char **argv)
{
    int ret;

    av_register_all();
    init_opts();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif

    show_banner();
    parse_options(argc, argv, options, opt_input_file);

    if (!input_filename) {
        show_usage();
        fprintf(stderr, "You have to specify one input file.\n");
        fprintf(stderr, "Use -h to get full help or, even better, run 'man %s'.\n", program_name);
        exit(1);
    }

    ret = probe_file(input_filename);

    av_free(avformat_opts);

    return ret;
}
