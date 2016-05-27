/*
 * avconv option parsing
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

#include <stdint.h>

#include "avconv.h"
#include "cmdutils.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/fifo.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#define DEFAULT_PASS_LOGFILENAME_PREFIX "av2pass"

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st)\
{\
    int i, ret;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if ((ret = check_stream_specifier(fmtctx, st, spec)) > 0)\
            outvar = o->name[i].u.type;\
        else if (ret < 0)\
            exit_program(1);\
    }\
}

const HWAccel hwaccels[] = {
#if HAVE_VDPAU_X11
    { "vdpau", vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU },
#endif
#if HAVE_DXVA2_LIB
    { "dxva2", dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD },
#endif
#if CONFIG_VDA
    { "vda",   vda_init,   HWACCEL_VDA,   AV_PIX_FMT_VDA },
#endif
#if CONFIG_LIBMFX
    { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
#endif
#if CONFIG_VAAPI
    { "vaapi", vaapi_decode_init, HWACCEL_VAAPI, AV_PIX_FMT_VAAPI },
#endif
    { 0 },
};
int hwaccel_lax_profile_check = 0;
AVBufferRef *hw_device_ctx;

char *vstats_filename;

float audio_drift_threshold = 0.1;
float dts_delta_threshold   = 10;

int audio_volume      = 256;
int audio_sync_method = 0;
int video_sync_method = VSYNC_AUTO;
int do_benchmark      = 0;
int do_hex_dump       = 0;
int do_pkt_dump       = 0;
int copy_ts           = 0;
int copy_tb           = 1;
int exit_on_error     = 0;
int print_stats       = 1;
int qp_hist           = 0;

static int file_overwrite     = 0;
static int file_skip          = 0;
static int video_discard      = 0;
static int intra_dc_precision = 8;
static int using_stdin        = 0;
static int input_sync;

static void uninit_options(OptionsContext *o)
{
    const OptionDef *po = options;
    int i;

    /* all OPT_SPEC and OPT_STRING can be freed in generic way */
    while (po->name) {
        void *dst = (uint8_t*)o + po->u.off;

        if (po->flags & OPT_SPEC) {
            SpecifierOpt **so = dst;
            int i, *count = (int*)(so + 1);
            for (i = 0; i < *count; i++) {
                av_freep(&(*so)[i].specifier);
                if (po->flags & OPT_STRING)
                    av_freep(&(*so)[i].u.str);
            }
            av_freep(so);
            *count = 0;
        } else if (po->flags & OPT_OFFSET && po->flags & OPT_STRING)
            av_freep(dst);
        po++;
    }

    for (i = 0; i < o->nb_stream_maps; i++)
        av_freep(&o->stream_maps[i].linklabel);
    av_freep(&o->stream_maps);
    av_freep(&o->meta_data_maps);
    av_freep(&o->streamid_map);
}

static void init_options(OptionsContext *o)
{
    memset(o, 0, sizeof(*o));

    o->mux_max_delay  = 0.7;
    o->start_time     = AV_NOPTS_VALUE;
    o->recording_time = INT64_MAX;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;
    o->accurate_seek  = 1;
}

/* return a copy of the input with the stream specifiers removed from the keys */
static AVDictionary *strip_specifiers(AVDictionary *dict)
{
    AVDictionaryEntry *e = NULL;
    AVDictionary    *ret = NULL;

    while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        char *p = strchr(e->key, ':');

        if (p)
            *p = 0;
        av_dict_set(&ret, e->key, e->value, 0);
        if (p)
            *p = ':';
    }
    return ret;
}

static double parse_frame_aspect_ratio(const char *arg)
{
    int x = 0, y = 0;
    double ar = 0;
    const char *p;
    char *end;

    p = strchr(arg, ':');
    if (p) {
        x = strtol(arg, &end, 10);
        if (end == p)
            y = strtol(end + 1, &end, 10);
        if (x > 0 && y > 0)
            ar = (double)x / (double)y;
    } else
        ar = strtod(arg, NULL);

    if (!ar) {
        av_log(NULL, AV_LOG_FATAL, "Incorrect aspect ratio specification.\n");
        exit_program(1);
    }
    return ar;
}

static int show_hwaccels(void *optctx, const char *opt, const char *arg)
{
    int i;

    printf("Supported hardware acceleration:\n");
    for (i = 0; i < FF_ARRAY_ELEMS(hwaccels) - 1; i++) {
        printf("%s\n", hwaccels[i].name);
    }
    printf("\n");
    return 0;
}

static int opt_audio_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:a", arg, options);
}

static int opt_video_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:v", arg, options);
}

static int opt_subtitle_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:s", arg, options);
}

static int opt_data_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:d", arg, options);
}

static int opt_map(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    StreamMap *m = NULL;
    int i, negative = 0, file_idx;
    int sync_file_idx = -1, sync_stream_idx;
    char *p, *sync;
    char *map;

    if (*arg == '-') {
        negative = 1;
        arg++;
    }
    map = av_strdup(arg);
    if (!map)
        return AVERROR(ENOMEM);

    /* parse sync stream first, just pick first matching stream */
    if (sync = strchr(map, ',')) {
        *sync = 0;
        sync_file_idx = strtol(sync + 1, &sync, 0);
        if (sync_file_idx >= nb_input_files || sync_file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sync file index: %d.\n", sync_file_idx);
            exit_program(1);
        }
        if (*sync)
            sync++;
        for (i = 0; i < input_files[sync_file_idx]->nb_streams; i++)
            if (check_stream_specifier(input_files[sync_file_idx]->ctx,
                                       input_files[sync_file_idx]->ctx->streams[i], sync) == 1) {
                sync_stream_idx = i;
                break;
            }
        if (i == input_files[sync_file_idx]->nb_streams) {
            av_log(NULL, AV_LOG_FATAL, "Sync stream specification in map %s does not "
                                       "match any streams.\n", arg);
            exit_program(1);
        }
    }


    if (map[0] == '[') {
        /* this mapping refers to lavfi output */
        const char *c = map + 1;
        GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
        m = &o->stream_maps[o->nb_stream_maps - 1];
        m->linklabel = av_get_token(&c, "]");
        if (!m->linklabel) {
            av_log(NULL, AV_LOG_ERROR, "Invalid output link label: %s.\n", map);
            exit_program(1);
        }
    } else {
        file_idx = strtol(map, &p, 0);
        if (file_idx >= nb_input_files || file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index: %d.\n", file_idx);
            exit_program(1);
        }
        if (negative)
            /* disable some already defined maps */
            for (i = 0; i < o->nb_stream_maps; i++) {
                m = &o->stream_maps[i];
                if (file_idx == m->file_index &&
                    check_stream_specifier(input_files[m->file_index]->ctx,
                                           input_files[m->file_index]->ctx->streams[m->stream_index],
                                           *p == ':' ? p + 1 : p) > 0)
                    m->disabled = 1;
            }
        else
            for (i = 0; i < input_files[file_idx]->nb_streams; i++) {
                if (check_stream_specifier(input_files[file_idx]->ctx, input_files[file_idx]->ctx->streams[i],
                            *p == ':' ? p + 1 : p) <= 0)
                    continue;
                GROW_ARRAY(o->stream_maps, o->nb_stream_maps);
                m = &o->stream_maps[o->nb_stream_maps - 1];

                m->file_index   = file_idx;
                m->stream_index = i;

                if (sync_file_idx >= 0) {
                    m->sync_file_index   = sync_file_idx;
                    m->sync_stream_index = sync_stream_idx;
                } else {
                    m->sync_file_index   = file_idx;
                    m->sync_stream_index = i;
                }
            }
    }

    if (!m) {
        av_log(NULL, AV_LOG_FATAL, "Stream map '%s' matches no streams.\n", arg);
        exit_program(1);
    }

    av_freep(&map);
    return 0;
}

static int opt_attach(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    GROW_ARRAY(o->attachments, o->nb_attachments);
    o->attachments[o->nb_attachments - 1] = arg;
    return 0;
}

#if CONFIG_VAAPI
static int opt_vaapi_device(void *optctx, const char *opt, const char *arg)
{
    int err;
    err = vaapi_device_init(arg);
    if (err < 0)
        exit_program(1);
    return 0;
}
#endif

/**
 * Parse a metadata specifier passed as 'arg' parameter.
 * @param arg  metadata string to parse
 * @param type metadata type is written here -- g(lobal)/s(tream)/c(hapter)/p(rogram)
 * @param index for type c/p, chapter/program index is written here
 * @param stream_spec for type s, the stream specifier is written here
 */
static void parse_meta_type(char *arg, char *type, int *index, const char **stream_spec)
{
    if (*arg) {
        *type = *arg;
        switch (*arg) {
        case 'g':
            break;
        case 's':
            if (*(++arg) && *arg != ':') {
                av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", arg);
                exit_program(1);
            }
            *stream_spec = *arg == ':' ? arg + 1 : "";
            break;
        case 'c':
        case 'p':
            if (*(++arg) == ':')
                *index = strtol(++arg, NULL, 0);
            break;
        default:
            av_log(NULL, AV_LOG_FATAL, "Invalid metadata type %c.\n", *arg);
            exit_program(1);
        }
    } else
        *type = 'g';
}

static int copy_metadata(char *outspec, char *inspec, AVFormatContext *oc, AVFormatContext *ic, OptionsContext *o)
{
    AVDictionary **meta_in = NULL;
    AVDictionary **meta_out;
    int i, ret = 0;
    char type_in, type_out;
    const char *istream_spec = NULL, *ostream_spec = NULL;
    int idx_in = 0, idx_out = 0;

    parse_meta_type(inspec,  &type_in,  &idx_in,  &istream_spec);
    parse_meta_type(outspec, &type_out, &idx_out, &ostream_spec);

    if (type_in == 'g' || type_out == 'g')
        o->metadata_global_manual = 1;
    if (type_in == 's' || type_out == 's')
        o->metadata_streams_manual = 1;
    if (type_in == 'c' || type_out == 'c')
        o->metadata_chapters_manual = 1;

    /* ic is NULL when just disabling automatic mappings */
    if (!ic)
        return 0;

#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
    if ((index) < 0 || (index) >= (nb_elems)) {\
        av_log(NULL, AV_LOG_FATAL, "Invalid %s index %d while processing metadata maps.\n",\
                (desc), (index));\
        exit_program(1);\
    }

#define SET_DICT(type, meta, context, index)\
        switch (type) {\
        case 'g':\
            meta = &context->metadata;\
            break;\
        case 'c':\
            METADATA_CHECK_INDEX(index, context->nb_chapters, "chapter")\
            meta = &context->chapters[index]->metadata;\
            break;\
        case 'p':\
            METADATA_CHECK_INDEX(index, context->nb_programs, "program")\
            meta = &context->programs[index]->metadata;\
            break;\
        case 's':\
            break; /* handled separately below */ \
        default: av_assert0(0);\
        }\

    SET_DICT(type_in, meta_in, ic, idx_in);
    SET_DICT(type_out, meta_out, oc, idx_out);

    /* for input streams choose first matching stream */
    if (type_in == 's') {
        for (i = 0; i < ic->nb_streams; i++) {
            if ((ret = check_stream_specifier(ic, ic->streams[i], istream_spec)) > 0) {
                meta_in = &ic->streams[i]->metadata;
                break;
            } else if (ret < 0)
                exit_program(1);
        }
        if (!meta_in) {
            av_log(NULL, AV_LOG_FATAL, "Stream specifier %s does not match  any streams.\n", istream_spec);
            exit_program(1);
        }
    }

    if (type_out == 's') {
        for (i = 0; i < oc->nb_streams; i++) {
            if ((ret = check_stream_specifier(oc, oc->streams[i], ostream_spec)) > 0) {
                meta_out = &oc->streams[i]->metadata;
                av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);
            } else if (ret < 0)
                exit_program(1);
        }
    } else
        av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static AVCodec *find_codec_or_die(const char *name, enum AVMediaType type, int encoder)
{
    const AVCodecDescriptor *desc;
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;

    codec = encoder ?
        avcodec_find_encoder_by_name(name) :
        avcodec_find_decoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name))) {
        codec = encoder ? avcodec_find_encoder(desc->id) :
                          avcodec_find_decoder(desc->id);
        if (codec)
            av_log(NULL, AV_LOG_VERBOSE, "Matched %s '%s' for codec '%s'.\n",
                   codec_string, codec->name, desc->name);
    }

    if (!codec) {
        av_log(NULL, AV_LOG_FATAL, "Unknown %s '%s'\n", codec_string, name);
        exit_program(1);
    }
    if (codec->type != type) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s type '%s'\n", codec_string, name);
        exit_program(1);
    }
    return codec;
}

static AVCodec *choose_decoder(OptionsContext *o, AVFormatContext *s, AVStream *st)
{
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st);
    if (codec_name) {
        AVCodec *codec = find_codec_or_die(codec_name, st->codecpar->codec_type, 0);
        st->codecpar->codec_id = codec->id;
        return codec;
    } else
        return avcodec_find_decoder(st->codecpar->codec_id);
}

/* Add all the streams from the given input file to the global
 * list of input streams. */
static void add_input_streams(OptionsContext *o, AVFormatContext *ic)
{
    int i, ret;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecParameters *par = st->codecpar;
        InputStream *ist = av_mallocz(sizeof(*ist));
        char *framerate = NULL, *hwaccel = NULL, *hwaccel_device = NULL;
        char *hwaccel_output_format = NULL;
        char *codec_tag = NULL;
        char *next;

        if (!ist)
            exit_program(1);

        GROW_ARRAY(input_streams, nb_input_streams);
        input_streams[nb_input_streams - 1] = ist;

        ist->st = st;
        ist->file_index = nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->nb_samples = 0;
        ist->min_pts = INT64_MAX;
        ist->max_pts = INT64_MIN;

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st);

        ist->autorotate = 1;
        MATCH_PER_STREAM_OPT(autorotate, i, ist->autorotate, ic, st);

        MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st);
        if (codec_tag) {
            uint32_t tag = strtol(codec_tag, &next, 0);
            if (*next)
                tag = AV_RL32(codec_tag);
            st->codecpar->codec_tag = tag;
        }

        ist->dec = choose_decoder(o, ic, st);
        ist->decoder_opts = filter_codec_opts(o->g->codec_opts, par->codec_id, ic, st, ist->dec);

        ist->dec_ctx = avcodec_alloc_context3(ist->dec);
        if (!ist->dec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "Error allocating the decoder context.\n");
            exit_program(1);
        }

        ret = avcodec_parameters_to_context(ist->dec_ctx, par);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
            exit_program(1);
        }

        switch (par->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st);
            if (framerate && av_parse_video_rate(&ist->framerate,
                                                 framerate) < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error parsing framerate %s.\n",
                       framerate);
                exit_program(1);
            }

            MATCH_PER_STREAM_OPT(hwaccels, str, hwaccel, ic, st);
            if (hwaccel) {
                if (!strcmp(hwaccel, "none"))
                    ist->hwaccel_id = HWACCEL_NONE;
                else if (!strcmp(hwaccel, "auto"))
                    ist->hwaccel_id = HWACCEL_AUTO;
                else {
                    int i;
                    for (i = 0; hwaccels[i].name; i++) {
                        if (!strcmp(hwaccels[i].name, hwaccel)) {
                            ist->hwaccel_id = hwaccels[i].id;
                            break;
                        }
                    }

                    if (!ist->hwaccel_id) {
                        av_log(NULL, AV_LOG_FATAL, "Unrecognized hwaccel: %s.\n",
                               hwaccel);
                        av_log(NULL, AV_LOG_FATAL, "Supported hwaccels: ");
                        for (i = 0; hwaccels[i].name; i++)
                            av_log(NULL, AV_LOG_FATAL, "%s ", hwaccels[i].name);
                        av_log(NULL, AV_LOG_FATAL, "\n");
                        exit_program(1);
                    }
                }
            }

            MATCH_PER_STREAM_OPT(hwaccel_devices, str, hwaccel_device, ic, st);
            if (hwaccel_device) {
                ist->hwaccel_device = av_strdup(hwaccel_device);
                if (!ist->hwaccel_device)
                    exit_program(1);
            }

            MATCH_PER_STREAM_OPT(hwaccel_output_formats, str,
                                 hwaccel_output_format, ic, st);
            if (hwaccel_output_format) {
                ist->hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
                if (ist->hwaccel_output_format == AV_PIX_FMT_NONE) {
                    av_log(NULL, AV_LOG_FATAL, "Unrecognised hwaccel output "
                           "format: %s", hwaccel_output_format);
                }
            } else {
                ist->hwaccel_output_format = AV_PIX_FMT_NONE;
            }

            ist->hwaccel_pix_fmt = AV_PIX_FMT_NONE;

            break;
        case AVMEDIA_TYPE_AUDIO:
            guess_input_channel_layout(ist);
            break;
        case AVMEDIA_TYPE_DATA:
        case AVMEDIA_TYPE_SUBTITLE:
        case AVMEDIA_TYPE_ATTACHMENT:
        case AVMEDIA_TYPE_UNKNOWN:
            break;
        default:
            abort();
        }
    }
}

static void assert_file_overwrite(const char *filename)
{
    if (file_overwrite && file_skip) {
        fprintf(stderr, "Error, both -y and -n supplied. Exiting.\n");
        exit_program(1);
    }

    if (!file_overwrite &&
        (!strchr(filename, ':') || filename[1] == ':' ||
         av_strstart(filename, "file:", NULL))) {
        if (avio_check(filename, 0) == 0) {
            if (!using_stdin && !file_skip) {
                fprintf(stderr,"File '%s' already exists. Overwrite ? [y/N] ", filename);
                fflush(stderr);
                if (!read_yesno()) {
                    fprintf(stderr, "Not overwriting - exiting\n");
                    exit_program(1);
                }
            }
            else {
                fprintf(stderr,"File '%s' already exists. Exiting.\n", filename);
                exit_program(1);
            }
        }
    }
}

static void dump_attachment(AVStream *st, const char *filename)
{
    int ret;
    AVIOContext *out = NULL;
    AVDictionaryEntry *e;

    if (!st->codecpar->extradata_size) {
        av_log(NULL, AV_LOG_WARNING, "No extradata to dump in stream #%d:%d.\n",
               nb_input_files - 1, st->index);
        return;
    }
    if (!*filename && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
        filename = e->value;
    if (!*filename) {
        av_log(NULL, AV_LOG_FATAL, "No filename specified and no 'filename' tag"
               "in stream #%d:%d.\n", nb_input_files - 1, st->index);
        exit_program(1);
    }

    assert_file_overwrite(filename);

    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, &int_cb, NULL)) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not open file %s for writing.\n",
               filename);
        exit_program(1);
    }

    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
    avio_flush(out);
    avio_close(out);
}

static int open_input_file(OptionsContext *o, const char *filename)
{
    InputFile *f;
    AVFormatContext *ic;
    AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    uint8_t buf[128];
    AVDictionary **opts;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;
    int orig_nb_streams;                     // number of streams before avformat_find_stream_info

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(NULL, AV_LOG_FATAL, "Unknown input format: '%s'\n", o->format);
            exit_program(1);
        }
    }

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    using_stdin |= !strncmp(filename, "pipe:", 5) ||
                    !strcmp(filename, "/dev/stdin");

    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic) {
        print_error(filename, AVERROR(ENOMEM));
        exit_program(1);
    }
    if (o->nb_audio_sample_rate) {
        snprintf(buf, sizeof(buf), "%d", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i);
        av_dict_set(&o->g->format_opts, "sample_rate", buf, 0);
    }
    if (o->nb_audio_channels) {
        /* because we set audio_channels based on both the "ac" and
         * "channel_layout" options, we need to check that the specified
         * demuxer actually has the "channels" option before setting it */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "channels", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            snprintf(buf, sizeof(buf), "%d",
                     o->audio_channels[o->nb_audio_channels - 1].u.i);
            av_dict_set(&o->g->format_opts, "channels", buf, 0);
        }
    }
    if (o->nb_frame_rates) {
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    ic->interrupt_callback = int_cb;

    /* open the input file with generic Libav function */
    err = avformat_open_input(&ic, filename, file_iformat, &o->g->format_opts);
    if (err < 0) {
        print_error(filename, err);
        exit_program(1);
    }
    assert_avoptions(o->g->format_opts);

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        choose_decoder(o, ic, ic->streams[i]);

    /* Set AVCodecContext options for avformat_find_stream_info */
    opts = setup_find_stream_info_opts(ic, o->g->codec_opts);
    orig_nb_streams = ic->nb_streams;

    /* If not enough info to get the stream parameters, we decode the
       first frames to get it. (used in mpeg case for example) */
    ret = avformat_find_stream_info(ic, opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "%s: could not find codec parameters\n", filename);
        avformat_close_input(&ic);
        exit_program(1);
    }

    timestamp = (o->start_time == AV_NOPTS_VALUE) ? 0 : o->start_time;
    /* add the stream start time */
    if (ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (o->start_time != AV_NOPTS_VALUE) {
        ret = av_seek_frame(ic, -1, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "%s: could not seek to position %0.3f\n",
                   filename, (double)timestamp / AV_TIME_BASE);
        }
    }

    /* update the current parameters so that they match the one of the input stream */
    add_input_streams(o, ic);

    /* dump the file content */
    av_dump_format(ic, nb_input_files, filename, 0);

    GROW_ARRAY(input_files, nb_input_files);
    f = av_mallocz(sizeof(*f));
    if (!f)
        exit_program(1);
    input_files[nb_input_files - 1] = f;

    f->ctx        = ic;
    f->ist_index  = nb_input_streams - ic->nb_streams;
    f->start_time = o->start_time;
    f->recording_time = o->recording_time;
    f->ts_offset  = o->input_ts_offset - (copy_ts ? 0 : timestamp);
    f->nb_streams = ic->nb_streams;
    f->rate_emu   = o->rate_emu;
    f->accurate_seek = o->accurate_seek;
    f->loop = o->loop;
    f->duration = 0;
    f->time_base = (AVRational){ 1, 1 };

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = f->ist_index; i < nb_input_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(input_streams[i]->decoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option)
            continue;
        if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                   "input file #%d (%s) is not a decoding option.\n", e->key,
                   option->help ? option->help : "", nb_input_files - 1,
                   filename);
            exit_program(1);
        }

        av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
               "input file #%d (%s) has not been used for any stream. The most "
               "likely reason is either wrong type (e.g. a video option with "
               "no video streams) or that it is a private option of some decoder "
               "which was not actually used for any stream.\n", e->key,
               option->help ? option->help : "", nb_input_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < ic->nb_streams; j++) {
            AVStream *st = ic->streams[j];

            if (check_stream_specifier(ic, st, o->dump_attachment[i].specifier) == 1)
                dump_attachment(st, o->dump_attachment[i].u.str);
        }
    }

    for (i = 0; i < orig_nb_streams; i++)
        av_dict_free(&opts[i]);
    av_freep(&opts);

    return 0;
}

static uint8_t *get_line(AVIOContext *s)
{
    AVIOContext *line;
    uint8_t *buf;
    char c;

    if (avio_open_dyn_buf(&line) < 0) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloc buffer for reading preset.\n");
        exit_program(1);
    }

    while ((c = avio_r8(s)) && c != '\n')
        avio_w8(line, c);
    avio_w8(line, 0);
    avio_close_dyn_buf(line, &buf);

    return buf;
}

static int get_preset_file_2(const char *preset_name, const char *codec_name, AVIOContext **s)
{
    int i, ret = -1;
    char filename[1000];
    const char *base[3] = { getenv("AVCONV_DATADIR"),
                            getenv("HOME"),
                            AVCONV_DATADIR,
                            };

    for (i = 0; i < FF_ARRAY_ELEMS(base) && ret < 0; i++) {
        if (!base[i])
            continue;
        if (codec_name) {
            snprintf(filename, sizeof(filename), "%s%s/%s-%s.avpreset", base[i],
                     i != 1 ? "" : "/.avconv", codec_name, preset_name);
            ret = avio_open2(s, filename, AVIO_FLAG_READ, &int_cb, NULL);
        }
        if (ret < 0) {
            snprintf(filename, sizeof(filename), "%s%s/%s.avpreset", base[i],
                     i != 1 ? "" : "/.avconv", preset_name);
            ret = avio_open2(s, filename, AVIO_FLAG_READ, &int_cb, NULL);
        }
    }
    return ret;
}

static int choose_encoder(OptionsContext *o, AVFormatContext *s, OutputStream *ost)
{
    enum AVMediaType type = ost->st->codecpar->codec_type;
    char *codec_name = NULL;

    if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_SUBTITLE) {
        MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, ost->st);
        if (!codec_name) {
            ost->st->codecpar->codec_id = av_guess_codec(s->oformat, NULL, s->filename,
                                                         NULL, ost->st->codecpar->codec_type);
            ost->enc = avcodec_find_encoder(ost->st->codecpar->codec_id);
            if (!ost->enc) {
                av_log(NULL, AV_LOG_FATAL, "Automatic encoder selection failed for "
                       "output stream #%d:%d. Default encoder for format %s is "
                       "probably disabled. Please choose an encoder manually.\n",
                       ost->file_index, ost->index, s->oformat->name);
                return AVERROR_ENCODER_NOT_FOUND;
            }
        } else if (!strcmp(codec_name, "copy"))
            ost->stream_copy = 1;
        else {
            ost->enc = find_codec_or_die(codec_name, ost->st->codecpar->codec_type, 1);
            ost->st->codecpar->codec_id = ost->enc->id;
        }

        ost->encoding_needed = !ost->stream_copy;
    } else {
        /* no encoding supported for other media types */
        ost->stream_copy     = 1;
        ost->encoding_needed = 0;
    }

    return 0;
}

static OutputStream *new_output_stream(OptionsContext *o, AVFormatContext *oc, enum AVMediaType type)
{
    OutputStream *ost;
    AVStream *st = avformat_new_stream(oc, NULL);
    int idx      = oc->nb_streams - 1, ret = 0;
    const char *bsfs = NULL;
    char *next, *codec_tag = NULL;
    double qscale = -1;

    if (!st) {
        av_log(NULL, AV_LOG_FATAL, "Could not alloc stream.\n");
        exit_program(1);
    }

    if (oc->nb_streams - 1 < o->nb_streamid_map)
        st->id = o->streamid_map[oc->nb_streams - 1];

    GROW_ARRAY(output_streams, nb_output_streams);
    if (!(ost = av_mallocz(sizeof(*ost))))
        exit_program(1);
    output_streams[nb_output_streams - 1] = ost;

    ost->file_index = nb_output_files - 1;
    ost->index      = idx;
    ost->st         = st;
    st->codecpar->codec_type = type;

    ret = choose_encoder(o, oc, ost);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error selecting an encoder for stream "
               "%d:%d\n", ost->file_index, ost->index);
        exit_program(1);
    }

    ost->enc_ctx = avcodec_alloc_context3(ost->enc);
    if (!ost->enc_ctx) {
        av_log(NULL, AV_LOG_ERROR, "Error allocating the encoding context.\n");
        exit_program(1);
    }
    ost->enc_ctx->codec_type = type;

    if (ost->enc) {
        AVIOContext *s = NULL;
        char *buf = NULL, *arg = NULL, *preset = NULL;

        ost->encoder_opts  = filter_codec_opts(o->g->codec_opts, ost->enc->id, oc, st, ost->enc);

        MATCH_PER_STREAM_OPT(presets, str, preset, oc, st);
        if (preset && (!(ret = get_preset_file_2(preset, ost->enc->name, &s)))) {
            do  {
                buf = get_line(s);
                if (!buf[0] || buf[0] == '#') {
                    av_free(buf);
                    continue;
                }
                if (!(arg = strchr(buf, '='))) {
                    av_log(NULL, AV_LOG_FATAL, "Invalid line found in the preset file.\n");
                    exit_program(1);
                }
                *arg++ = 0;
                av_dict_set(&ost->encoder_opts, buf, arg, AV_DICT_DONT_OVERWRITE);
                av_free(buf);
            } while (!s->eof_reached);
            avio_close(s);
        }
        if (ret) {
            av_log(NULL, AV_LOG_FATAL,
                   "Preset %s specified for stream %d:%d, but could not be opened.\n",
                   preset, ost->file_index, ost->index);
            exit_program(1);
        }
    } else {
        ost->encoder_opts = filter_codec_opts(o->g->codec_opts, AV_CODEC_ID_NONE, oc, st, NULL);
    }

    ost->max_frames = INT64_MAX;
    MATCH_PER_STREAM_OPT(max_frames, i64, ost->max_frames, oc, st);

    MATCH_PER_STREAM_OPT(bitstream_filters, str, bsfs, oc, st);
    while (bsfs && *bsfs) {
        const AVBitStreamFilter *filter;
        char *bsf;

        bsf = av_get_token(&bsfs, ",");
        if (!bsf)
            exit_program(1);

        filter = av_bsf_get_by_name(bsf);
        if (!filter) {
            av_log(NULL, AV_LOG_FATAL, "Unknown bitstream filter %s\n", bsf);
            exit_program(1);
        }
        av_freep(&bsf);

        ost->bitstream_filters = av_realloc_array(ost->bitstream_filters,
                                                  ost->nb_bitstream_filters + 1,
                                                  sizeof(*ost->bitstream_filters));
        if (!ost->bitstream_filters)
            exit_program(1);

        ost->bitstream_filters[ost->nb_bitstream_filters++] = filter;
        if (*bsfs)
            bsfs++;
    }

    MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, oc, st);
    if (codec_tag) {
        uint32_t tag = strtol(codec_tag, &next, 0);
        if (*next)
            tag = AV_RL32(codec_tag);
        ost->enc_ctx->codec_tag = tag;
    }

    MATCH_PER_STREAM_OPT(qscale, dbl, qscale, oc, st);
    if (qscale >= 0) {
        ost->enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
        ost->enc_ctx->global_quality = FF_QP2LAMBDA * qscale;
    }

    ost->max_muxing_queue_size = 128;
    MATCH_PER_STREAM_OPT(max_muxing_queue_size, i, ost->max_muxing_queue_size, oc, st);
    ost->max_muxing_queue_size *= sizeof(AVPacket);

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        ost->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_get_int(o->g->sws_opts, "sws_flags", 0, &ost->sws_flags);

    av_dict_copy(&ost->resample_opts, o->g->resample_opts, 0);

    ost->pix_fmts[0] = ost->pix_fmts[1] = AV_PIX_FMT_NONE;
    ost->last_mux_dts = AV_NOPTS_VALUE;

    ost->muxing_queue = av_fifo_alloc(8 * sizeof(AVPacket));
    if (!ost->muxing_queue)
        exit_program(1);

    return ost;
}

static void parse_matrix_coeffs(uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for (i = 0;; i++) {
        dest[i] = atoi(p);
        if (i == 63)
            break;
        p = strchr(p, ',');
        if (!p) {
            av_log(NULL, AV_LOG_FATAL, "Syntax error in matrix \"%s\" at coeff %d\n", str, i);
            exit_program(1);
        }
        p++;
    }
}

/* read file contents into a string */
static uint8_t *read_file(const char *filename)
{
    AVIOContext *pb      = NULL;
    AVIOContext *dyn_buf = NULL;
    int ret = avio_open(&pb, filename, AVIO_FLAG_READ);
    uint8_t buf[1024], *str;

    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error opening file %s.\n", filename);
        return NULL;
    }

    ret = avio_open_dyn_buf(&dyn_buf);
    if (ret < 0) {
        avio_closep(&pb);
        return NULL;
    }
    while ((ret = avio_read(pb, buf, sizeof(buf))) > 0)
        avio_write(dyn_buf, buf, ret);
    avio_w8(dyn_buf, 0);
    avio_closep(&pb);

    ret = avio_close_dyn_buf(dyn_buf, &str);
    if (ret < 0)
        return NULL;
    return str;
}

static char *get_ost_filters(OptionsContext *o, AVFormatContext *oc,
                             OutputStream *ost)
{
    AVStream *st = ost->st;
    char *filter = NULL, *filter_script = NULL;

    MATCH_PER_STREAM_OPT(filter_scripts, str, filter_script, oc, st);
    MATCH_PER_STREAM_OPT(filters, str, filter, oc, st);

    if (filter_script && filter) {
        av_log(NULL, AV_LOG_ERROR, "Both -filter and -filter_script set for "
               "output stream #%d:%d.\n", nb_output_files, st->index);
        exit_program(1);
    }

    if (filter_script)
        return read_file(filter_script);
    else if (filter)
        return av_strdup(filter);

    return av_strdup(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ?
                     "null" : "anull");
}

static OutputStream *new_video_stream(OptionsContext *o, AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *video_enc;
    char *frame_aspect_ratio = NULL;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_VIDEO);
    st  = ost->st;
    video_enc = ost->enc_ctx;

    MATCH_PER_STREAM_OPT(frame_aspect_ratios, str, frame_aspect_ratio, oc, st);
    if (frame_aspect_ratio)
        ost->frame_aspect_ratio = parse_frame_aspect_ratio(frame_aspect_ratio);

    if (!ost->stream_copy) {
        const char *p = NULL;
        char *frame_rate = NULL, *frame_size = NULL;
        char *frame_pix_fmt = NULL;
        char *intra_matrix = NULL, *inter_matrix = NULL;
        int do_pass = 0;
        int i;

        MATCH_PER_STREAM_OPT(frame_rates, str, frame_rate, oc, st);
        if (frame_rate && av_parse_video_rate(&ost->frame_rate, frame_rate) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid framerate value: %s\n", frame_rate);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st);
        if (frame_size && av_parse_video_size(&video_enc->width, &video_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Invalid frame size: %s.\n", frame_size);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st);
        if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == AV_PIX_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Unknown pixel format requested: %s.\n", frame_pix_fmt);
            exit_program(1);
        }
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

        MATCH_PER_STREAM_OPT(intra_matrices, str, intra_matrix, oc, st);
        if (intra_matrix) {
            if (!(video_enc->intra_matrix = av_mallocz(sizeof(*video_enc->intra_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for intra matrix.\n");
                exit_program(1);
            }
            parse_matrix_coeffs(video_enc->intra_matrix, intra_matrix);
        }
        MATCH_PER_STREAM_OPT(inter_matrices, str, inter_matrix, oc, st);
        if (inter_matrix) {
            if (!(video_enc->inter_matrix = av_mallocz(sizeof(*video_enc->inter_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "Could not allocate memory for inter matrix.\n");
                exit_program(1);
            }
            parse_matrix_coeffs(video_enc->inter_matrix, inter_matrix);
        }

        MATCH_PER_STREAM_OPT(rc_overrides, str, p, oc, st);
        for (i = 0; p; i++) {
            int start, end, q;
            int e = sscanf(p, "%d,%d,%d", &start, &end, &q);
            if (e != 3) {
                av_log(NULL, AV_LOG_FATAL, "error parsing rc_override\n");
                exit_program(1);
            }
            video_enc->rc_override =
                av_realloc(video_enc->rc_override,
                           sizeof(RcOverride) * (i + 1));
            if (!video_enc->rc_override) {
                av_log(NULL, AV_LOG_FATAL, "Could not (re)allocate memory for rc_override.\n");
                exit_program(1);
            }
            video_enc->rc_override[i].start_frame = start;
            video_enc->rc_override[i].end_frame   = end;
            if (q > 0) {
                video_enc->rc_override[i].qscale         = q;
                video_enc->rc_override[i].quality_factor = 1.0;
            }
            else {
                video_enc->rc_override[i].qscale         = 0;
                video_enc->rc_override[i].quality_factor = -q/100.0;
            }
            p = strchr(p, '/');
            if (p) p++;
        }
        video_enc->rc_override_count = i;
        video_enc->intra_dc_precision = intra_dc_precision - 8;

        /* two pass mode */
        MATCH_PER_STREAM_OPT(pass, i, do_pass, oc, st);
        if (do_pass) {
            if (do_pass == 1) {
                video_enc->flags |= AV_CODEC_FLAG_PASS1;
            } else {
                video_enc->flags |= AV_CODEC_FLAG_PASS2;
            }
        }

        MATCH_PER_STREAM_OPT(passlogfiles, str, ost->logfile_prefix, oc, st);
        if (ost->logfile_prefix &&
            !(ost->logfile_prefix = av_strdup(ost->logfile_prefix)))
            exit_program(1);

        if (do_pass) {
            char logfilename[1024];
            FILE *f;

            snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                     ost->logfile_prefix ? ost->logfile_prefix :
                                           DEFAULT_PASS_LOGFILENAME_PREFIX,
                     i);
            if (!strcmp(ost->enc->name, "libx264")) {
                av_dict_set(&ost->encoder_opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
            } else {
                if (video_enc->flags & AV_CODEC_FLAG_PASS1) {
                    f = fopen(logfilename, "wb");
                    if (!f) {
                        av_log(NULL, AV_LOG_FATAL, "Cannot write log file '%s' for pass-1 encoding: %s\n",
                               logfilename, strerror(errno));
                        exit_program(1);
                    }
                    ost->logfile = f;
                } else {
                    char  *logbuffer = read_file(logfilename);

                    if (!logbuffer) {
                        av_log(NULL, AV_LOG_FATAL, "Error reading log file '%s' for pass-2 encoding\n",
                               logfilename);
                        exit_program(1);
                    }
                    video_enc->stats_in = logbuffer;
                }
            }
        }

        MATCH_PER_STREAM_OPT(forced_key_frames, str, ost->forced_keyframes, oc, st);
        if (ost->forced_keyframes)
            ost->forced_keyframes = av_strdup(ost->forced_keyframes);

        MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st);

        ost->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st);


        ost->avfilter = get_ost_filters(o, oc, ost);
        if (!ost->avfilter)
            exit_program(1);
    } else {
        MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc ,st);
    }

    return ost;
}

static OutputStream *new_audio_stream(OptionsContext *o, AVFormatContext *oc)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *audio_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_AUDIO);
    st  = ost->st;

    audio_enc = ost->enc_ctx;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    if (!ost->stream_copy) {
        char *sample_fmt = NULL;

        MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st);

        MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st);
        if (sample_fmt &&
            (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "Invalid sample format '%s'\n", sample_fmt);
            exit_program(1);
        }

        MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st);

        ost->avfilter = get_ost_filters(o, oc, ost);
        if (!ost->avfilter)
            exit_program(1);
    }

    return ost;
}

static OutputStream *new_data_stream(OptionsContext *o, AVFormatContext *oc)
{
    OutputStream *ost;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_DATA);
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
        exit_program(1);
    }

    return ost;
}

static OutputStream *new_attachment_stream(OptionsContext *o, AVFormatContext *oc)
{
    OutputStream *ost = new_output_stream(o, oc, AVMEDIA_TYPE_ATTACHMENT);
    ost->stream_copy = 1;
    ost->finished    = 1;
    return ost;
}

static OutputStream *new_subtitle_stream(OptionsContext *o, AVFormatContext *oc)
{
    OutputStream *ost;
    AVCodecContext *subtitle_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_SUBTITLE);
    subtitle_enc = ost->enc_ctx;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    return ost;
}

/* arg format is "output-stream-index:streamid-value". */
static int opt_streamid(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int idx;
    char *p;
    char idx_str[16];

    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        av_log(NULL, AV_LOG_FATAL,
               "Invalid value '%s' for option '%s', required syntax is 'index:value'\n",
               arg, opt);
        exit_program(1);
    }
    *p++ = '\0';
    idx = parse_number_or_die(opt, idx_str, OPT_INT, 0, INT_MAX);
    o->streamid_map = grow_array(o->streamid_map, sizeof(*o->streamid_map), &o->nb_streamid_map, idx+1);
    o->streamid_map[idx] = parse_number_or_die(opt, p, OPT_INT, 0, INT_MAX);
    return 0;
}

static int copy_chapters(InputFile *ifile, OutputFile *ofile, int copy_metadata)
{
    AVFormatContext *is = ifile->ctx;
    AVFormatContext *os = ofile->ctx;
    AVChapter **tmp;
    int i;

    tmp = av_realloc(os->chapters, sizeof(*os->chapters) * (is->nb_chapters + os->nb_chapters));
    if (!tmp)
        return AVERROR(ENOMEM);
    os->chapters = tmp;

    for (i = 0; i < is->nb_chapters; i++) {
        AVChapter *in_ch = is->chapters[i], *out_ch;
        int64_t start_time = (ofile->start_time == AV_NOPTS_VALUE) ? 0 : ofile->start_time;
        int64_t ts_off   = av_rescale_q(start_time - ifile->ts_offset,
                                       AV_TIME_BASE_Q, in_ch->time_base);
        int64_t rt       = (ofile->recording_time == INT64_MAX) ? INT64_MAX :
                           av_rescale_q(ofile->recording_time, AV_TIME_BASE_Q, in_ch->time_base);


        if (in_ch->end < ts_off)
            continue;
        if (rt != INT64_MAX && in_ch->start > rt + ts_off)
            break;

        out_ch = av_mallocz(sizeof(AVChapter));
        if (!out_ch)
            return AVERROR(ENOMEM);

        out_ch->id        = in_ch->id;
        out_ch->time_base = in_ch->time_base;
        out_ch->start     = FFMAX(0,  in_ch->start - ts_off);
        out_ch->end       = FFMIN(rt, in_ch->end   - ts_off);

        if (copy_metadata)
            av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

        os->chapters[os->nb_chapters++] = out_ch;
    }
    return 0;
}

static void init_output_filter(OutputFilter *ofilter, OptionsContext *o,
                               AVFormatContext *oc)
{
    OutputStream *ost;

    switch (ofilter->type) {
    case AVMEDIA_TYPE_VIDEO: ost = new_video_stream(o, oc); break;
    case AVMEDIA_TYPE_AUDIO: ost = new_audio_stream(o, oc); break;
    default:
        av_log(NULL, AV_LOG_FATAL, "Only video and audio filters are supported "
               "currently.\n");
        exit_program(1);
    }

    ost->source_index = -1;
    ost->filter       = ofilter;

    ofilter->ost      = ost;
    ofilter->format   = -1;

    if (ost->stream_copy) {
        av_log(NULL, AV_LOG_ERROR, "Streamcopy requested for output stream %d:%d, "
               "which is fed from a complex filtergraph. Filtering and streamcopy "
               "cannot be used together.\n", ost->file_index, ost->index);
        exit_program(1);
    }

    avfilter_inout_free(&ofilter->out_tmp);
}

static int init_complex_filters(void)
{
    int i, ret = 0;

    for (i = 0; i < nb_filtergraphs; i++) {
        ret = init_complex_filtergraph(filtergraphs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int open_output_file(OptionsContext *o, const char *filename)
{
    AVFormatContext *oc;
    int i, j, err;
    AVOutputFormat *file_oformat;
    OutputFile *of;
    OutputStream *ost;
    InputStream  *ist;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;

    GROW_ARRAY(output_files, nb_output_files);
    of = av_mallocz(sizeof(*of));
    if (!of)
        exit_program(1);
    output_files[nb_output_files - 1] = of;

    of->ost_index      = nb_output_streams;
    of->recording_time = o->recording_time;
    of->start_time     = o->start_time;
    of->limit_filesize = o->limit_filesize;
    of->shortest       = o->shortest;
    av_dict_copy(&of->opts, o->g->format_opts, 0);

    if (!strcmp(filename, "-"))
        filename = "pipe:";

    oc = avformat_alloc_context();
    if (!oc) {
        print_error(filename, AVERROR(ENOMEM));
        exit_program(1);
    }
    of->ctx = oc;
    if (o->recording_time != INT64_MAX)
        oc->duration = o->recording_time;

    if (o->format) {
        file_oformat = av_guess_format(o->format, NULL, NULL);
        if (!file_oformat) {
            av_log(NULL, AV_LOG_FATAL, "Requested output format '%s' is not a suitable output format\n", o->format);
            exit_program(1);
        }
    } else {
        file_oformat = av_guess_format(NULL, filename, NULL);
        if (!file_oformat) {
            av_log(NULL, AV_LOG_FATAL, "Unable to find a suitable output format for '%s'\n",
                   filename);
            exit_program(1);
        }
    }

    oc->oformat = file_oformat;
    oc->interrupt_callback = int_cb;
    av_strlcpy(oc->filename, filename, sizeof(oc->filename));

    /* create streams for all unlabeled output pads */
    for (i = 0; i < nb_filtergraphs; i++) {
        FilterGraph *fg = filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *ofilter = fg->outputs[j];

            if (!ofilter->out_tmp || ofilter->out_tmp->name)
                continue;

            switch (ofilter->type) {
            case AVMEDIA_TYPE_VIDEO:    o->video_disable    = 1; break;
            case AVMEDIA_TYPE_AUDIO:    o->audio_disable    = 1; break;
            case AVMEDIA_TYPE_SUBTITLE: o->subtitle_disable = 1; break;
            }
            init_output_filter(ofilter, o, oc);
        }
    }

    if (!o->nb_stream_maps) {
        /* pick the "best" stream of each type */
#define NEW_STREAM(type, index)\
        if (index >= 0) {\
            ost = new_ ## type ## _stream(o, oc);\
            ost->source_index = index;\
            ost->sync_ist     = input_streams[index];\
            input_streams[index]->discard = 0;\
            input_streams[index]->st->discard = AVDISCARD_NONE;\
        }

        /* video: highest resolution */
        if (!o->video_disable && oc->oformat->video_codec != AV_CODEC_ID_NONE) {
            int area = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = input_streams[i];
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    ist->st->codecpar->width * ist->st->codecpar->height > area) {
                    area = ist->st->codecpar->width * ist->st->codecpar->height;
                    idx = i;
                }
            }
            NEW_STREAM(video, idx);
        }

        /* audio: most channels */
        if (!o->audio_disable && oc->oformat->audio_codec != AV_CODEC_ID_NONE) {
            int channels = 0, idx = -1;
            for (i = 0; i < nb_input_streams; i++) {
                ist = input_streams[i];
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                    ist->st->codecpar->channels > channels) {
                    channels = ist->st->codecpar->channels;
                    idx = i;
                }
            }
            NEW_STREAM(audio, idx);
        }

        /* subtitles: pick first */
        if (!o->subtitle_disable && oc->oformat->subtitle_codec != AV_CODEC_ID_NONE) {
            for (i = 0; i < nb_input_streams; i++)
                if (input_streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    NEW_STREAM(subtitle, i);
                    break;
                }
        }
        /* do something with data? */
    } else {
        for (i = 0; i < o->nb_stream_maps; i++) {
            StreamMap *map = &o->stream_maps[i];

            if (map->disabled)
                continue;

            if (map->linklabel) {
                FilterGraph *fg;
                OutputFilter *ofilter = NULL;
                int j, k;

                for (j = 0; j < nb_filtergraphs; j++) {
                    fg = filtergraphs[j];
                    for (k = 0; k < fg->nb_outputs; k++) {
                        AVFilterInOut *out = fg->outputs[k]->out_tmp;
                        if (out && !strcmp(out->name, map->linklabel)) {
                            ofilter = fg->outputs[k];
                            goto loop_end;
                        }
                    }
                }
loop_end:
                if (!ofilter) {
                    av_log(NULL, AV_LOG_FATAL, "Output with label '%s' does not exist "
                           "in any defined filter graph.\n", map->linklabel);
                    exit_program(1);
                }
                init_output_filter(ofilter, o, oc);
            } else {
                ist = input_streams[input_files[map->file_index]->ist_index + map->stream_index];
                switch (ist->st->codecpar->codec_type) {
                case AVMEDIA_TYPE_VIDEO:    ost = new_video_stream(o, oc);    break;
                case AVMEDIA_TYPE_AUDIO:    ost = new_audio_stream(o, oc);    break;
                case AVMEDIA_TYPE_SUBTITLE: ost = new_subtitle_stream(o, oc); break;
                case AVMEDIA_TYPE_DATA:     ost = new_data_stream(o, oc);     break;
                case AVMEDIA_TYPE_ATTACHMENT: ost = new_attachment_stream(o, oc); break;
                default:
                    av_log(NULL, AV_LOG_FATAL, "Cannot map stream #%d:%d - unsupported type.\n",
                           map->file_index, map->stream_index);
                    exit_program(1);
                }

                ost->source_index = input_files[map->file_index]->ist_index + map->stream_index;
                ost->sync_ist     = input_streams[input_files[map->sync_file_index]->ist_index +
                                               map->sync_stream_index];
                ist->discard = 0;
                ist->st->discard = AVDISCARD_NONE;
            }
        }
    }

    /* handle attached files */
    for (i = 0; i < o->nb_attachments; i++) {
        AVIOContext *pb;
        uint8_t *attachment;
        const char *p;
        int64_t len;

        if ((err = avio_open2(&pb, o->attachments[i], AVIO_FLAG_READ, &int_cb, NULL)) < 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not open attachment file %s.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        if ((len = avio_size(pb)) <= 0) {
            av_log(NULL, AV_LOG_FATAL, "Could not get size of the attachment %s.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        if (!(attachment = av_malloc(len))) {
            av_log(NULL, AV_LOG_FATAL, "Attachment %s too large to fit into memory.\n",
                   o->attachments[i]);
            exit_program(1);
        }
        avio_read(pb, attachment, len);

        ost = new_attachment_stream(o, oc);
        ost->stream_copy               = 0;
        ost->source_index              = -1;
        ost->attachment_filename       = o->attachments[i];
        ost->st->codecpar->extradata      = attachment;
        ost->st->codecpar->extradata_size = len;

        p = strrchr(o->attachments[i], '/');
        av_dict_set(&ost->st->metadata, "filename", (p && *p) ? p + 1 : o->attachments[i], AV_DICT_DONT_OVERWRITE);
        avio_close(pb);
    }

    if (!oc->nb_streams && !(oc->oformat->flags & AVFMT_NOSTREAMS)) {
        av_dump_format(oc, nb_output_files - 1, oc->filename, 1);
        av_log(NULL, AV_LOG_ERROR, "Output file #%d does not contain any stream\n", nb_output_files - 1);
        exit_program(1);
    }

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = of->ost_index; i < nb_output_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(output_streams[i]->encoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option)
            continue;
        if (!(option->flags & AV_OPT_FLAG_ENCODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "Codec AVOption %s (%s) specified for "
                   "output file #%d (%s) is not an encoding option.\n", e->key,
                   option->help ? option->help : "", nb_output_files - 1,
                   filename);
            exit_program(1);
        }

        av_log(NULL, AV_LOG_WARNING, "Codec AVOption %s (%s) specified for "
               "output file #%d (%s) has not been used for any stream. The most "
               "likely reason is either wrong type (e.g. a video option with "
               "no video streams) or that it is a private option of some encoder "
               "which was not actually used for any stream.\n", e->key,
               option->help ? option->help : "", nb_output_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    /* set the decoding_needed flags and create simple filtergraphs */
    for (i = of->ost_index; i < nb_output_streams; i++) {
        OutputStream *ost = output_streams[i];

        if (ost->encoding_needed && ost->source_index >= 0) {
            InputStream *ist = input_streams[ost->source_index];
            ist->decoding_needed = 1;

            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                err = init_simple_filtergraph(ist, ost);
                if (err < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "Error initializing a simple filtergraph between streams "
                           "%d:%d->%d:%d\n", ist->file_index, ost->source_index,
                           nb_output_files - 1, ost->st->index);
                    exit_program(1);
                }
            }
        }

        /*
         * We want CFR output if and only if one of those is true:
         * 1) user specified output framerate with -r
         * 2) user specified -vsync cfr
         * 3) output format is CFR and the user didn't force vsync to
         *    something else than CFR
         *
         * in such a case, set ost->frame_rate
         */
        if (ost->encoding_needed && ost->enc_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            int format_cfr = !(oc->oformat->flags & (AVFMT_NOTIMESTAMPS | AVFMT_VARIABLE_FPS));
            int need_cfr = !!ost->frame_rate.num;

            if (video_sync_method == VSYNC_CFR ||
                (video_sync_method == VSYNC_AUTO && format_cfr))
                need_cfr = 1;

            if (need_cfr && !ost->frame_rate.num) {
                InputStream *ist = ost->source_index >= 0 ? input_streams[ost->source_index] : NULL;

                if (ist && ist->framerate.num)
                    ost->frame_rate = ist->framerate;
                else if (ist && ist->st->avg_frame_rate.num)
                    ost->frame_rate = ist->st->avg_frame_rate;
                else {
                    av_log(NULL, AV_LOG_WARNING, "Constant framerate requested "
                           "for the output stream #%d:%d, but no information "
                           "about the input framerate is available. Falling "
                           "back to a default value of 25fps. Use the -r option "
                           "if you want a different framerate.\n",
                           ost->file_index, ost->index);
                    ost->frame_rate = (AVRational){ 25, 1 };
                }
            }

            if (need_cfr && ost->enc->supported_framerates && !ost->force_fps) {
                int idx = av_find_nearest_q_idx(ost->frame_rate, ost->enc->supported_framerates);
                ost->frame_rate = ost->enc->supported_framerates[idx];
            }
        }

        /* set the filter output constraints */
        if (ost->filter) {
            OutputFilter *f = ost->filter;
            int count;
            switch (ost->enc_ctx->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                f->frame_rate = ost->frame_rate;
                f->width      = ost->enc_ctx->width;
                f->height     = ost->enc_ctx->height;
                if (ost->enc_ctx->pix_fmt != AV_PIX_FMT_NONE) {
                    f->format = ost->enc_ctx->pix_fmt;
                } else if (ost->enc->pix_fmts) {
                    count = 0;
                    while (ost->enc->pix_fmts[count] != AV_PIX_FMT_NONE)
                        count++;
                    f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                    if (!f->formats)
                        exit_program(1);
                    memcpy(f->formats, ost->enc->pix_fmts, (count + 1) * sizeof(*f->formats));
                }
                break;
            case AVMEDIA_TYPE_AUDIO:
                if (ost->enc_ctx->sample_fmt != AV_SAMPLE_FMT_NONE) {
                    f->format = ost->enc_ctx->sample_fmt;
                } else if (ost->enc->sample_fmts) {
                    count = 0;
                    while (ost->enc->sample_fmts[count] != AV_SAMPLE_FMT_NONE)
                        count++;
                    f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                    if (!f->formats)
                        exit_program(1);
                    memcpy(f->formats, ost->enc->sample_fmts, (count + 1) * sizeof(*f->formats));
                }
                if (ost->enc_ctx->sample_rate) {
                    f->sample_rate = ost->enc_ctx->sample_rate;
                } else if (ost->enc->supported_samplerates) {
                    count = 0;
                    while (ost->enc->supported_samplerates[count])
                        count++;
                    f->sample_rates = av_mallocz_array(count + 1, sizeof(*f->sample_rates));
                    if (!f->sample_rates)
                        exit_program(1);
                    memcpy(f->sample_rates, ost->enc->supported_samplerates,
                           (count + 1) * sizeof(*f->sample_rates));
                }
                if (ost->enc_ctx->channels) {
                    f->channel_layout = av_get_default_channel_layout(ost->enc_ctx->channels);
                } else if (ost->enc->channel_layouts) {
                    count = 0;
                    while (ost->enc->channel_layouts[count])
                        count++;
                    f->channel_layouts = av_mallocz_array(count + 1, sizeof(*f->channel_layouts));
                    if (!f->channel_layouts)
                        exit_program(1);
                    memcpy(f->channel_layouts, ost->enc->channel_layouts,
                           (count + 1) * sizeof(*f->channel_layouts));
                }
                break;
            }
        }

    }

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(oc->filename)) {
            print_error(oc->filename, AVERROR(EINVAL));
            exit_program(1);
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid losing precious files */
        assert_file_overwrite(filename);

        /* open the file */
        if ((err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                              &oc->interrupt_callback,
                              &of->opts)) < 0) {
            print_error(filename, err);
            exit_program(1);
        }
    }

    if (o->mux_preload) {
        uint8_t buf[64];
        snprintf(buf, sizeof(buf), "%d", (int)(o->mux_preload*AV_TIME_BASE));
        av_dict_set(&of->opts, "preload", buf, 0);
    }
    oc->max_delay = (int)(o->mux_max_delay * AV_TIME_BASE);
    oc->flags |= AVFMT_FLAG_NONBLOCK;

    /* copy metadata */
    for (i = 0; i < o->nb_metadata_map; i++) {
        char *p;
        int in_file_index = strtol(o->metadata_map[i].u.str, &p, 0);

        if (in_file_index >= nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d while processing metadata maps\n", in_file_index);
            exit_program(1);
        }
        copy_metadata(o->metadata_map[i].specifier, *p ? p + 1 : p, oc,
                      in_file_index >= 0 ?
                      input_files[in_file_index]->ctx : NULL, o);
    }

    /* copy chapters */
    if (o->chapters_input_file >= nb_input_files) {
        if (o->chapters_input_file == INT_MAX) {
            /* copy chapters from the first input file that has them*/
            o->chapters_input_file = -1;
            for (i = 0; i < nb_input_files; i++)
                if (input_files[i]->ctx->nb_chapters) {
                    o->chapters_input_file = i;
                    break;
                }
        } else {
            av_log(NULL, AV_LOG_FATAL, "Invalid input file index %d in chapter mapping.\n",
                   o->chapters_input_file);
            exit_program(1);
        }
    }
    if (o->chapters_input_file >= 0)
        copy_chapters(input_files[o->chapters_input_file], of,
                      !o->metadata_chapters_manual);

    /* copy global metadata by default */
    if (!o->metadata_global_manual && nb_input_files)
        av_dict_copy(&oc->metadata, input_files[0]->ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
    if (!o->metadata_streams_manual)
        for (i = of->ost_index; i < nb_output_streams; i++) {
            InputStream *ist;
            if (output_streams[i]->source_index < 0)         /* this is true e.g. for attached files */
                continue;
            ist = input_streams[output_streams[i]->source_index];
            av_dict_copy(&output_streams[i]->st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
        }

    /* process manually set metadata */
    for (i = 0; i < o->nb_metadata; i++) {
        AVDictionary **m;
        char type, *val;
        const char *stream_spec;
        int index = 0, j, ret;

        val = strchr(o->metadata[i].u.str, '=');
        if (!val) {
            av_log(NULL, AV_LOG_FATAL, "No '=' character in metadata string %s.\n",
                   o->metadata[i].u.str);
            exit_program(1);
        }
        *val++ = 0;

        parse_meta_type(o->metadata[i].specifier, &type, &index, &stream_spec);
        if (type == 's') {
            for (j = 0; j < oc->nb_streams; j++) {
                if ((ret = check_stream_specifier(oc, oc->streams[j], stream_spec)) > 0) {
                    av_dict_set(&oc->streams[j]->metadata, o->metadata[i].u.str, *val ? val : NULL, 0);
                } else if (ret < 0)
                    exit_program(1);
            }
        }
        else {
            switch (type) {
            case 'g':
                m = &oc->metadata;
                break;
            case 'c':
                if (index < 0 || index >= oc->nb_chapters) {
                    av_log(NULL, AV_LOG_FATAL, "Invalid chapter index %d in metadata specifier.\n", index);
                    exit_program(1);
                }
                m = &oc->chapters[index]->metadata;
                break;
            default:
                av_log(NULL, AV_LOG_FATAL, "Invalid metadata specifier %s.\n", o->metadata[i].specifier);
                exit_program(1);
            }
            av_dict_set(m, o->metadata[i].u.str, *val ? val : NULL, 0);
        }
    }

    return 0;
}

static int opt_target(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
    static const char *const frame_rates[] = { "25", "30000/1001", "24000/1001" };

    if (!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if (!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if (!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        /* Try to determine PAL/NTSC by peeking in the input files */
        if (nb_input_files) {
            int i, j, fr;
            for (j = 0; j < nb_input_files; j++) {
                for (i = 0; i < input_files[j]->nb_streams; i++) {
                    AVStream *st = input_files[j]->ctx->streams[i];
                    if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                        continue;
                    fr = st->time_base.den * 1000 / st->time_base.num;
                    if (fr == 25000) {
                        norm = PAL;
                        break;
                    } else if ((fr == 29970) || (fr == 23976)) {
                        norm = NTSC;
                        break;
                    }
                }
                if (norm != UNKNOWN)
                    break;
            }
        }
        if (norm != UNKNOWN)
            av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if (norm == UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
        exit_program(1);
    }

    if (!strcmp(arg, "vcd")) {
        opt_video_codec(o, "c:v", "mpeg1video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "vcd", options);

        parse_option(o, "s", norm == PAL ? "352x288" : "352x240", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b", "1150000");
        opt_default(NULL, "maxrate", "1150000");
        opt_default(NULL, "minrate", "1150000");
        opt_default(NULL, "bufsize", "327680"); // 40*1024*8;

        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);
        parse_option(o, "ac", "2", options);

        opt_default(NULL, "packetsize", "2324");
        opt_default(NULL, "muxrate", "3528"); // 2352 * 75 / 50;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        o->mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
    } else if (!strcmp(arg, "svcd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "svcd", options);

        parse_option(o, "s", norm == PAL ? "480x576" : "480x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b", "2040000");
        opt_default(NULL, "maxrate", "2516000");
        opt_default(NULL, "minrate", "0"); // 1145000;
        opt_default(NULL, "bufsize", "1835008"); // 224*1024*8;
        opt_default(NULL, "scan_offset", "1");


        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);

        opt_default(NULL, "packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "ac3");
        parse_option(o, "f", "dvd", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "r", frame_rates[norm], options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b", "6000000");
        opt_default(NULL, "maxrate", "9000000");
        opt_default(NULL, "minrate", "0"); // 1500000;
        opt_default(NULL, "bufsize", "1835008"); // 224*1024*8;

        opt_default(NULL, "packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        opt_default(NULL, "muxrate", "25200"); // from mplex project: data_rate = 1260000. mux_rate = data_rate / 50

        opt_default(NULL, "b:a", "448000");
        parse_option(o, "ar", "48000", options);

    } else if (!strncmp(arg, "dv", 2)) {

        parse_option(o, "f", "dv", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                          norm == PAL ? "yuv420p" : "yuv411p", options);
        parse_option(o, "r", frame_rates[norm], options);

        parse_option(o, "ar", "48000", options);
        parse_option(o, "ac", "2", options);

    } else {
        av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }

    av_dict_copy(&o->g->codec_opts,  codec_opts, 0);
    av_dict_copy(&o->g->format_opts, format_opts, 0);

    return 0;
}

static int opt_vstats_file(void *optctx, const char *opt, const char *arg)
{
    av_free (vstats_filename);
    vstats_filename = av_strdup (arg);
    return 0;
}

static int opt_vstats(void *optctx, const char *opt, const char *arg)
{
    char filename[40];
    time_t today2 = time(NULL);
    struct tm *today = localtime(&today2);

    if (!today) { // maybe tomorrow
        av_log(NULL, AV_LOG_FATAL, "Unable to get current time.\n");
        exit_program(1);
    }

    snprintf(filename, sizeof(filename), "vstats_%02d%02d%02d.log", today->tm_hour, today->tm_min,
             today->tm_sec);
    return opt_vstats_file(NULL, opt, filename);
}

static int opt_video_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:v", arg, options);
}

static int opt_audio_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:a", arg, options);
}

static int opt_data_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:d", arg, options);
}

static int opt_video_tag(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "tag:v", arg, options);
}

static int opt_audio_tag(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "tag:a", arg, options);
}

static int opt_subtitle_tag(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "tag:s", arg, options);
}

static int opt_video_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:v", arg, options);
}

static int opt_audio_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:a", arg, options);
}

static int opt_vsync(void *optctx, const char *opt, const char *arg)
{
    if      (!av_strcasecmp(arg, "cfr"))         video_sync_method = VSYNC_CFR;
    else if (!av_strcasecmp(arg, "vfr"))         video_sync_method = VSYNC_VFR;
    else if (!av_strcasecmp(arg, "passthrough")) video_sync_method = VSYNC_PASSTHROUGH;

    if (video_sync_method == VSYNC_AUTO)
        video_sync_method = parse_number_or_die("vsync", arg, OPT_INT, VSYNC_AUTO, VSYNC_VFR);
    return 0;
}

static int opt_channel_layout(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char layout_str[32];
    char *stream_str;
    char *ac_str;
    int ret, channels, ac_str_size;
    uint64_t layout;

    layout = av_get_channel_layout(arg);
    if (!layout) {
        av_log(NULL, AV_LOG_ERROR, "Unknown channel layout: %s\n", arg);
        return AVERROR(EINVAL);
    }
    snprintf(layout_str, sizeof(layout_str), "%"PRIu64, layout);
    ret = opt_default(NULL, opt, layout_str);
    if (ret < 0)
        return ret;

    /* set 'ac' option based on channel layout */
    channels = av_get_channel_layout_nb_channels(layout);
    snprintf(layout_str, sizeof(layout_str), "%d", channels);
    stream_str = strchr(opt, ':');
    ac_str_size = 3 + (stream_str ? strlen(stream_str) : 0);
    ac_str = av_mallocz(ac_str_size);
    if (!ac_str)
        return AVERROR(ENOMEM);
    av_strlcpy(ac_str, "ac", 3);
    if (stream_str)
        av_strlcat(ac_str, stream_str, ac_str_size);
    ret = parse_option(o, ac_str, layout_str, options);
    av_free(ac_str);

    return ret;
}

static int opt_audio_qscale(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "q:a", arg, options);
}

static int opt_filter_complex(void *optctx, const char *opt, const char *arg)
{
    GROW_ARRAY(filtergraphs, nb_filtergraphs);
    if (!(filtergraphs[nb_filtergraphs - 1] = av_mallocz(sizeof(*filtergraphs[0]))))
        return AVERROR(ENOMEM);
    filtergraphs[nb_filtergraphs - 1]->index      = nb_filtergraphs - 1;
    filtergraphs[nb_filtergraphs - 1]->graph_desc = av_strdup(arg);
    if (!filtergraphs[nb_filtergraphs - 1]->graph_desc)
        return AVERROR(ENOMEM);
    return 0;
}

static int opt_filter_complex_script(void *optctx, const char *opt, const char *arg)
{
    uint8_t *graph_desc = read_file(arg);
    if (!graph_desc)
        return AVERROR(EINVAL);

    GROW_ARRAY(filtergraphs, nb_filtergraphs);
    if (!(filtergraphs[nb_filtergraphs - 1] = av_mallocz(sizeof(*filtergraphs[0]))))
        return AVERROR(ENOMEM);
    filtergraphs[nb_filtergraphs - 1]->index      = nb_filtergraphs - 1;
    filtergraphs[nb_filtergraphs - 1]->graph_desc = graph_desc;
    return 0;
}

void show_help_default(const char *opt, const char *arg)
{
    /* per-file options have at least one of those set */
    const int per_file = OPT_SPEC | OPT_OFFSET | OPT_PERFILE;
    int show_advanced = 0, show_avoptions = 0;

    if (opt && *opt) {
        if (!strcmp(opt, "long"))
            show_advanced = 1;
        else if (!strcmp(opt, "full"))
            show_advanced = show_avoptions = 1;
        else
            av_log(NULL, AV_LOG_ERROR, "Unknown help option '%s'.\n", opt);
    }

    show_usage();

    printf("Getting help:\n"
           "    -h      -- print basic options\n"
           "    -h long -- print more options\n"
           "    -h full -- print all options (including all format and codec specific options, very long)\n"
           "    -h type=name -- print all options for the named decoder/encoder/demuxer/muxer/filter\n"
           "    See man %s for detailed description of the options.\n"
           "\n", program_name);

    show_help_options(options, "Print help / information / capabilities:",
                      OPT_EXIT, 0, 0);

    show_help_options(options, "Global options (affect whole program "
                      "instead of just one file:",
                      0, per_file | OPT_EXIT | OPT_EXPERT, 0);
    if (show_advanced)
        show_help_options(options, "Advanced global options:", OPT_EXPERT,
                          per_file | OPT_EXIT, 0);

    show_help_options(options, "Per-file main options:", 0,
                      OPT_EXPERT | OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE |
                      OPT_EXIT, per_file);
    if (show_advanced)
        show_help_options(options, "Advanced per-file options:",
                          OPT_EXPERT, OPT_AUDIO | OPT_VIDEO | OPT_SUBTITLE, per_file);

    show_help_options(options, "Video options:",
                      OPT_VIDEO, OPT_EXPERT | OPT_AUDIO, 0);
    if (show_advanced)
        show_help_options(options, "Advanced Video options:",
                          OPT_EXPERT | OPT_VIDEO, OPT_AUDIO, 0);

    show_help_options(options, "Audio options:",
                      OPT_AUDIO, OPT_EXPERT | OPT_VIDEO, 0);
    if (show_advanced)
        show_help_options(options, "Advanced Audio options:",
                          OPT_EXPERT | OPT_AUDIO, OPT_VIDEO, 0);
    show_help_options(options, "Subtitle options:",
                      OPT_SUBTITLE, 0, 0);
    printf("\n");

    if (show_avoptions) {
        int flags = AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM;
        show_help_children(avcodec_get_class(), flags);
        show_help_children(avformat_get_class(), flags);
        show_help_children(sws_get_class(), flags);
        show_help_children(avfilter_get_class(), AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_AUDIO_PARAM);
    }
}

void show_usage(void)
{
    printf("Hyper fast Audio and Video encoder\n");
    printf("usage: %s [options] [[infile options] -i infile]... {[outfile options] outfile}...\n", program_name);
    printf("\n");
}

enum OptGroup {
    GROUP_OUTFILE,
    GROUP_INFILE,
};

static const OptionGroupDef groups[] = {
    [GROUP_OUTFILE] = { "output file",  NULL, OPT_OUTPUT },
    [GROUP_INFILE]  = { "input file",   "i",  OPT_INPUT },
};

static int open_files(OptionGroupList *l, const char *inout,
                      int (*open_file)(OptionsContext*, const char*))
{
    int i, ret;

    for (i = 0; i < l->nb_groups; i++) {
        OptionGroup *g = &l->groups[i];
        OptionsContext o;

        init_options(&o);
        o.g = g;

        ret = parse_optgroup(&o, g);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing options for %s file "
                   "%s.\n", inout, g->arg);
            return ret;
        }

        av_log(NULL, AV_LOG_DEBUG, "Opening an %s file: %s.\n", inout, g->arg);
        ret = open_file(&o, g->arg);
        uninit_options(&o);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error opening %s file %s.\n",
                   inout, g->arg);
            return ret;
        }
        av_log(NULL, AV_LOG_DEBUG, "Successfully opened the file.\n");
    }

    return 0;
}

int avconv_parse_options(int argc, char **argv)
{
    OptionParseContext octx;
    uint8_t error[128];
    int ret;

    memset(&octx, 0, sizeof(octx));

    /* split the commandline into an internal representation */
    ret = split_commandline(&octx, argc, argv, options, groups,
                            FF_ARRAY_ELEMS(groups));
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error splitting the argument list: ");
        goto fail;
    }

    /* apply global options */
    ret = parse_optgroup(NULL, &octx.global_opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error parsing global options: ");
        goto fail;
    }

    /* open input files */
    ret = open_files(&octx.groups[GROUP_INFILE], "input", open_input_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error opening input files: ");
        goto fail;
    }

    /* create the complex filtergraphs */
    ret = init_complex_filters();
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error initializing complex filters.\n");
        goto fail;
    }

    /* open output files */
    ret = open_files(&octx.groups[GROUP_OUTFILE], "output", open_output_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error opening output files: ");
        goto fail;
    }

fail:
    uninit_parse_context(&octx);
    if (ret < 0) {
        av_strerror(ret, error, sizeof(error));
        av_log(NULL, AV_LOG_FATAL, "%s\n", error);
    }
    return ret;
}

#define OFFSET(x) offsetof(OptionsContext, x)
const OptionDef options[] = {
    /* main options */
#include "cmdutils_common_opts.h"
    { "f",              HAS_ARG | OPT_STRING | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(format) },
        "force format", "fmt" },
    { "y",              OPT_BOOL,                                    {              &file_overwrite },
        "overwrite output files" },
    { "n",              OPT_BOOL,                                    {              &file_skip },
        "never overwrite output files" },
    { "c",              HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
        "codec name", "codec" },
    { "codec",          HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
        "codec name", "codec" },
    { "pre",            HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off       = OFFSET(presets) },
        "preset name", "preset" },
    { "map",            HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                        OPT_OUTPUT,                                  { .func_arg = opt_map },
        "set input stream mapping",
        "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
    { "map_metadata",   HAS_ARG | OPT_STRING | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off       = OFFSET(metadata_map) },
        "set metadata information of outfile from infile",
        "outfile[,metadata]:infile[,metadata]" },
    { "map_chapters",   HAS_ARG | OPT_INT | OPT_EXPERT | OPT_OFFSET |
                        OPT_OUTPUT,                                  { .off = OFFSET(chapters_input_file) },
        "set chapters mapping", "input_file_index" },
    { "t",              HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(recording_time) },
        "record or transcode \"duration\" seconds of audio/video",
        "duration" },
    { "fs",             HAS_ARG | OPT_INT64 | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(limit_filesize) },
        "set the limit file size in bytes", "limit_size" },
    { "ss",             HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(start_time) },
        "set the start time offset", "time_off" },
    { "accurate_seek",  OPT_BOOL | OPT_OFFSET | OPT_EXPERT |
                        OPT_INPUT,                                   { .off = OFFSET(accurate_seek) },
        "enable/disable accurate seeking with -ss" },
    { "itsoffset",      HAS_ARG | OPT_TIME | OPT_OFFSET |
                        OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(input_ts_offset) },
        "set the input ts offset", "time_off" },
    { "itsscale",       HAS_ARG | OPT_DOUBLE | OPT_SPEC |
                        OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(ts_scale) },
        "set the input ts scale", "scale" },
    { "metadata",       HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(metadata) },
        "add metadata", "string=string" },
    { "dframes",        HAS_ARG | OPT_PERFILE | OPT_EXPERT |
                        OPT_OUTPUT,                                  { .func_arg = opt_data_frames },
        "set the number of data frames to record", "number" },
    { "benchmark",      OPT_BOOL | OPT_EXPERT,                       { &do_benchmark },
        "add timings for benchmarking" },
    { "timelimit",      HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_timelimit },
        "set max runtime in seconds", "limit" },
    { "dump",           OPT_BOOL | OPT_EXPERT,                       { &do_pkt_dump },
        "dump each input packet" },
    { "hex",            OPT_BOOL | OPT_EXPERT,                       { &do_hex_dump },
        "when dumping packets, also dump the payload" },
    { "re",             OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                        OPT_INPUT,                                   { .off = OFFSET(rate_emu) },
        "read input at native frame rate", "" },
    { "target",         HAS_ARG | OPT_PERFILE | OPT_OUTPUT,          { .func_arg = opt_target },
        "specify target file type (\"vcd\", \"svcd\", \"dvd\","
        " \"dv\", \"dv50\", \"pal-vcd\", \"ntsc-svcd\", ...)", "type" },
    { "vsync",          HAS_ARG | OPT_EXPERT,                        { opt_vsync },
        "video sync method", "" },
    { "async",          HAS_ARG | OPT_INT | OPT_EXPERT,              { &audio_sync_method },
        "audio sync method", "" },
    { "adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT,          { &audio_drift_threshold },
        "audio drift threshold", "threshold" },
    { "copyts",         OPT_BOOL | OPT_EXPERT,                       { &copy_ts },
        "copy timestamps" },
    { "copytb",         OPT_BOOL | OPT_EXPERT,                       { &copy_tb },
        "copy input stream time base when stream copying" },
    { "shortest",       OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                        OPT_OUTPUT,                                  { .off = OFFSET(shortest) },
        "finish encoding within shortest input" },
    { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT,       { &dts_delta_threshold },
        "timestamp discontinuity delta threshold", "threshold" },
    { "xerror",         OPT_BOOL | OPT_EXPERT,                       { &exit_on_error },
        "exit on error", "error" },
    { "copyinkf",       OPT_BOOL | OPT_EXPERT | OPT_SPEC |
                        OPT_OUTPUT,                                  { .off = OFFSET(copy_initial_nonkeyframes) },
        "copy initial non-keyframes" },
    { "frames",         OPT_INT64 | HAS_ARG | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(max_frames) },
        "set the number of frames to record", "number" },
    { "tag",            OPT_STRING | HAS_ARG | OPT_SPEC |
                        OPT_EXPERT | OPT_OUTPUT | OPT_INPUT,         { .off = OFFSET(codec_tags) },
        "force codec tag/fourcc", "fourcc/tag" },
    { "q",              HAS_ARG | OPT_EXPERT | OPT_DOUBLE |
                        OPT_SPEC | OPT_OUTPUT,                       { .off = OFFSET(qscale) },
        "use fixed quality scale (VBR)", "q" },
    { "qscale",         HAS_ARG | OPT_EXPERT | OPT_DOUBLE |
                        OPT_SPEC | OPT_OUTPUT,                       { .off = OFFSET(qscale) },
        "use fixed quality scale (VBR)", "q" },
    { "filter",         HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filters) },
        "set stream filterchain", "filter_list" },
    { "filter_script",  HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filter_scripts) },
        "read stream filtergraph description from a file", "filename" },
    { "filter_complex", HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_filter_complex },
        "create a complex filtergraph", "graph_description" },
    { "filter_complex_script", HAS_ARG | OPT_EXPERT,                 { .func_arg = opt_filter_complex_script },
        "read complex filtergraph description from a file", "filename" },
    { "stats",          OPT_BOOL,                                    { &print_stats },
        "print progress report during encoding", },
    { "attach",         HAS_ARG | OPT_PERFILE | OPT_EXPERT |
                        OPT_OUTPUT,                                  { .func_arg = opt_attach },
        "add an attachment to the output file", "filename" },
    { "dump_attachment", HAS_ARG | OPT_STRING | OPT_SPEC |
                         OPT_EXPERT | OPT_INPUT,                     { .off = OFFSET(dump_attachment) },
        "extract an attachment into a file", "filename" },
    { "loop", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_INPUT |
                        OPT_OFFSET,                                  { .off = OFFSET(loop) }, "set number of times input stream shall be looped", "loop count" },

    /* video options */
    { "vframes",      OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_video_frames },
        "set the number of video frames to record", "number" },
    { "r",            OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_rates) },
        "set frame rate (Hz value, fraction or abbreviation)", "rate" },
    { "s",            OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_sizes) },
        "set frame size (WxH or abbreviation)", "size" },
    { "aspect",       OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(frame_aspect_ratios) },
        "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
    { "pix_fmt",      OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_pix_fmts) },
        "set pixel format", "format" },
    { "vn",           OPT_VIDEO | OPT_BOOL  | OPT_OFFSET | OPT_OUTPUT,           { .off = OFFSET(video_disable) },
        "disable video" },
    { "vdt",          OPT_VIDEO | OPT_INT | HAS_ARG | OPT_EXPERT ,               { &video_discard },
        "discard threshold", "n" },
    { "rc_override",  OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(rc_overrides) },
        "rate control override for specific intervals", "override" },
    { "vcodec",       OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_INPUT |
                      OPT_OUTPUT,                                                { .func_arg = opt_video_codec },
        "force video codec ('copy' to copy stream)", "codec" },
    { "pass",         OPT_VIDEO | HAS_ARG | OPT_SPEC | OPT_INT | OPT_OUTPUT,     { .off = OFFSET(pass) },
        "select the pass number (1 or 2)", "n" },
    { "passlogfile",  OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(passlogfiles) },
        "select two pass log file name prefix", "prefix" },
    { "vstats",       OPT_VIDEO | OPT_EXPERT ,                                   { &opt_vstats },
        "dump video coding statistics to file" },
    { "vstats_file",  OPT_VIDEO | HAS_ARG | OPT_EXPERT ,                         { opt_vstats_file },
        "dump video coding statistics to file", "file" },
    { "vf",           OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_video_filters },
        "video filters", "filter list" },
    { "intra_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(intra_matrices) },
        "specify intra matrix coeffs", "matrix" },
    { "inter_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(inter_matrices) },
        "specify inter matrix coeffs", "matrix" },
    { "top",          OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_INT| OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(top_field_first) },
        "top=1/bottom=0/auto=-1 field first", "" },
    { "dc",           OPT_VIDEO | OPT_INT | HAS_ARG | OPT_EXPERT ,               { &intra_dc_precision },
        "intra_dc_precision", "precision" },
    { "vtag",         OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_PERFILE |
                      OPT_OUTPUT,                                                { .func_arg = opt_video_tag },
        "force video tag/fourcc", "fourcc/tag" },
    { "qphist",       OPT_VIDEO | OPT_BOOL | OPT_EXPERT ,                        { &qp_hist },
        "show QP histogram" },
    { "force_fps",    OPT_VIDEO | OPT_BOOL | OPT_EXPERT  | OPT_SPEC |
                      OPT_OUTPUT,                                                { .off = OFFSET(force_fps) },
        "force the selected framerate, disable the best supported framerate selection" },
    { "streamid",     OPT_VIDEO | HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                      OPT_OUTPUT,                                                { .func_arg = opt_streamid },
        "set the value of an outfile streamid", "streamIndex:value" },
    { "force_key_frames", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_OUTPUT,                                 { .off = OFFSET(forced_key_frames) },
        "force key frames at specified timestamps", "timestamps" },
    { "hwaccel",          OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccels) },
        "use HW accelerated decoding", "hwaccel name" },
    { "hwaccel_device",   OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_devices) },
        "select a device for HW acceleration", "devicename" },
    { "hwaccel_output_format", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                          OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_output_formats) },
        "select output format used with HW accelerated decoding", "format" },

    { "hwaccels",         OPT_EXIT,                                              { .func_arg = show_hwaccels },
        "show available HW acceleration methods" },
    { "autorotate",       HAS_ARG | OPT_BOOL | OPT_SPEC |
                          OPT_EXPERT | OPT_INPUT,                                { .off = OFFSET(autorotate) },
        "automatically insert correct rotate filters" },
    { "hwaccel_lax_profile_check", OPT_BOOL | OPT_EXPERT,                        { &hwaccel_lax_profile_check},
        "attempt to decode anyway if HW accelerated decoder's supported profiles do not exactly match the stream" },

    /* audio options */
    { "aframes",        OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_frames },
        "set the number of audio frames to record", "number" },
    { "aq",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_qscale },
        "set audio quality (codec-specific)", "quality", },
    { "ar",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_sample_rate) },
        "set audio sampling rate (in Hz)", "rate" },
    { "ac",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                        OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_channels) },
        "set number of audio channels", "channels" },
    { "an",             OPT_AUDIO | OPT_BOOL | OPT_OFFSET | OPT_OUTPUT,            { .off = OFFSET(audio_disable) },
        "disable audio" },
    { "acodec",         OPT_AUDIO | HAS_ARG  | OPT_PERFILE |
                        OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = opt_audio_codec },
        "force audio codec ('copy' to copy stream)", "codec" },
    { "atag",           OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                        OPT_OUTPUT,                                                { .func_arg = opt_audio_tag },
        "force audio tag/fourcc", "fourcc/tag" },
    { "vol",            OPT_AUDIO | HAS_ARG  | OPT_INT,                            { &audio_volume },
        "change audio volume (256=normal)" , "volume" },
    { "sample_fmt",     OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_SPEC |
                        OPT_STRING | OPT_INPUT | OPT_OUTPUT,                       { .off = OFFSET(sample_fmts) },
        "set sample format", "format" },
    { "channel_layout", OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                        OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = opt_channel_layout },
        "set channel layout", "layout" },
    { "af",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_filters },
        "audio filters", "filter list" },

    /* subtitle options */
    { "sn",     OPT_SUBTITLE | OPT_BOOL | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(subtitle_disable) },
        "disable subtitle" },
    { "scodec", OPT_SUBTITLE | HAS_ARG  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT, { .func_arg = opt_subtitle_codec },
        "force subtitle codec ('copy' to copy stream)", "codec" },
    { "stag",   OPT_SUBTITLE | HAS_ARG  | OPT_EXPERT  | OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_subtitle_tag }
        , "force subtitle tag/fourcc", "fourcc/tag" },

    /* grab options */
    { "isync", OPT_BOOL | OPT_EXPERT, { &input_sync }, "this option is deprecated and does nothing", "" },

    /* muxer options */
    { "muxdelay",   OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_max_delay) },
        "set the maximum demux-decode delay", "seconds" },
    { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_preload) },
        "set the initial demux-decode delay", "seconds" },

    { "bsf", HAS_ARG | OPT_STRING | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(bitstream_filters) },
        "A comma-separated list of bitstream filters", "bitstream_filters" },

    { "max_muxing_queue_size", HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(max_muxing_queue_size) },
        "maximum number of packets that can be buffered while waiting for all streams to initialize", "packets" },

    /* data codec support */
    { "dcodec", HAS_ARG | OPT_DATA | OPT_PERFILE | OPT_EXPERT | OPT_INPUT | OPT_OUTPUT, { .func_arg = opt_data_codec },
        "force data codec ('copy' to copy stream)", "codec" },

#if CONFIG_VAAPI
    { "vaapi_device", HAS_ARG | OPT_EXPERT, { .func_arg = opt_vaapi_device },
        "set VAAPI hardware device (DRM path or X11 display name)", "device" },
#endif

    { NULL, },
};
