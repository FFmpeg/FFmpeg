/*
 * Tee pseudo-muxer
 * Copyright (c) 2012 Nicolas George
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software * Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#include "libavutil/avutil.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "avformat.h"
#include "avio_internal.h"

typedef enum {
    ON_SLAVE_FAILURE_ABORT  = 1,
    ON_SLAVE_FAILURE_IGNORE = 2
} SlaveFailurePolicy;

#define DEFAULT_SLAVE_FAILURE_POLICY ON_SLAVE_FAILURE_ABORT

typedef struct {
    AVFormatContext *avf;
    AVBitStreamFilterContext **bsfs; ///< bitstream filters per stream

    SlaveFailurePolicy on_fail;

    /** map from input to output streams indexes,
     * disabled output streams are set to -1 */
    int *stream_map;
    int header_written;
} TeeSlave;

typedef struct TeeContext {
    const AVClass *class;
    unsigned nb_slaves;
    unsigned nb_alive;
    TeeSlave *slaves;
} TeeContext;

static const char *const slave_delim     = "|";
static const char *const slave_opt_open  = "[";
static const char *const slave_opt_close = "]";
static const char *const slave_opt_delim = ":]"; /* must have the close too */
static const char *const slave_bsfs_spec_sep = "/";
static const char *const slave_select_sep = ",";

static const AVClass tee_muxer_class = {
    .class_name = "Tee muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int parse_slave_options(void *log, char *slave,
                               AVDictionary **options, char **filename)
{
    const char *p;
    char *key, *val;
    int ret;

    if (!strspn(slave, slave_opt_open)) {
        *filename = slave;
        return 0;
    }
    p = slave + 1;
    if (strspn(p, slave_opt_close)) {
        *filename = (char *)p + 1;
        return 0;
    }
    while (1) {
        ret = av_opt_get_key_value(&p, "=", slave_opt_delim, 0, &key, &val);
        if (ret < 0) {
            av_log(log, AV_LOG_ERROR, "No option found near \"%s\"\n", p);
            goto fail;
        }
        ret = av_dict_set(options, key, val,
                          AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
        if (ret < 0)
            goto fail;
        if (strspn(p, slave_opt_close))
            break;
        p++;
    }
    *filename = (char *)p + 1;
    return 0;

fail:
    av_dict_free(options);
    return ret;
}

/**
 * Parse list of bitstream filters and add them to the list of filters
 * pointed to by bsfs.
 *
 * The list must be specified in the form:
 * BSFS ::= BSF[,BSFS]
 */
static int parse_bsfs(void *log_ctx, const char *bsfs_spec,
                      AVBitStreamFilterContext **bsfs)
{
    char *bsf_name, *buf, *dup, *saveptr;
    int ret = 0;

    if (!(dup = buf = av_strdup(bsfs_spec)))
        return AVERROR(ENOMEM);

    while (bsf_name = av_strtok(buf, ",", &saveptr)) {
        AVBitStreamFilterContext *bsf = av_bitstream_filter_init(bsf_name);

        if (!bsf) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Cannot initialize bitstream filter with name '%s', "
                   "unknown filter or internal error happened\n",
                   bsf_name);
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        /* append bsf context to the list of bsf contexts */
        *bsfs = bsf;
        bsfs = &bsf->next;

        buf = NULL;
    }

end:
    av_free(dup);
    return ret;
}

static inline int parse_slave_failure_policy_option(const char *opt, TeeSlave *tee_slave)
{
    if (!opt) {
        tee_slave->on_fail = DEFAULT_SLAVE_FAILURE_POLICY;
        return 0;
    } else if (!av_strcasecmp("abort", opt)) {
        tee_slave->on_fail = ON_SLAVE_FAILURE_ABORT;
        return 0;
    } else if (!av_strcasecmp("ignore", opt)) {
        tee_slave->on_fail = ON_SLAVE_FAILURE_IGNORE;
        return 0;
    }
    /* Set failure behaviour to abort, so invalid option error will not be ignored */
    tee_slave->on_fail = ON_SLAVE_FAILURE_ABORT;
    return AVERROR(EINVAL);
}

static int close_slave(TeeSlave *tee_slave)
{
    AVFormatContext *avf;
    unsigned i;
    int ret = 0;

    avf = tee_slave->avf;
    if (!avf)
        return 0;

    if (tee_slave->header_written)
        ret = av_write_trailer(avf);

    if (tee_slave->bsfs) {
        for (i = 0; i < avf->nb_streams; ++i) {
            AVBitStreamFilterContext *bsf_next, *bsf = tee_slave->bsfs[i];
            while (bsf) {
                bsf_next = bsf->next;
                av_bitstream_filter_close(bsf);
                bsf = bsf_next;
            }
        }
    }
    av_freep(&tee_slave->stream_map);
    av_freep(&tee_slave->bsfs);

    ff_format_io_close(avf, &avf->pb);
    avformat_free_context(avf);
    tee_slave->avf = NULL;
    return ret;
}

static void close_slaves(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    unsigned i;

    for (i = 0; i < tee->nb_slaves; i++) {
        close_slave(&tee->slaves[i]);
    }
    av_freep(&tee->slaves);
}

static int open_slave(AVFormatContext *avf, char *slave, TeeSlave *tee_slave)
{
    int i, ret;
    AVDictionary *options = NULL;
    AVDictionaryEntry *entry;
    char *filename;
    char *format = NULL, *select = NULL, *on_fail = NULL;
    AVFormatContext *avf2 = NULL;
    AVStream *st, *st2;
    int stream_count;
    int fullret;
    char *subselect = NULL, *next_subselect = NULL, *first_subselect = NULL, *tmp_select = NULL;

    if ((ret = parse_slave_options(avf, slave, &options, &filename)) < 0)
        return ret;

#define STEAL_OPTION(option, field) do {                                \
        if ((entry = av_dict_get(options, option, NULL, 0))) {          \
            field = entry->value;                                       \
            entry->value = NULL; /* prevent it from being freed */      \
            av_dict_set(&options, option, NULL, 0);                     \
        }                                                               \
    } while (0)

    STEAL_OPTION("f", format);
    STEAL_OPTION("select", select);
    STEAL_OPTION("onfail", on_fail);

    ret = parse_slave_failure_policy_option(on_fail, tee_slave);
    if (ret < 0) {
        av_log(avf, AV_LOG_ERROR,
               "Invalid onfail option value, valid options are 'abort' and 'ignore'\n");
        goto end;
    }

    ret = avformat_alloc_output_context2(&avf2, NULL, format, filename);
    if (ret < 0)
        goto end;
    tee_slave->avf = avf2;
    av_dict_copy(&avf2->metadata, avf->metadata, 0);
    avf2->opaque   = avf->opaque;
    avf2->io_open  = avf->io_open;
    avf2->io_close = avf->io_close;

    tee_slave->stream_map = av_calloc(avf->nb_streams, sizeof(*tee_slave->stream_map));
    if (!tee_slave->stream_map) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    stream_count = 0;
    for (i = 0; i < avf->nb_streams; i++) {
        st = avf->streams[i];
        if (select) {
            tmp_select = av_strdup(select);  // av_strtok is destructive so we regenerate it in each loop
            if (!tmp_select) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            fullret = 0;
            first_subselect = tmp_select;
            next_subselect = NULL;
            while (subselect = av_strtok(first_subselect, slave_select_sep, &next_subselect)) {
                first_subselect = NULL;

                ret = avformat_match_stream_specifier(avf, avf->streams[i], subselect);
                if (ret < 0) {
                    av_log(avf, AV_LOG_ERROR,
                           "Invalid stream specifier '%s' for output '%s'\n",
                           subselect, slave);
                    goto end;
                }
                if (ret != 0) {
                    fullret = 1; // match
                    break;
                }
            }
            av_freep(&tmp_select);

            if (fullret == 0) { /* no match */
                tee_slave->stream_map[i] = -1;
                continue;
            }
        }
        tee_slave->stream_map[i] = stream_count++;

        if (!(st2 = avformat_new_stream(avf2, NULL))) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        st2->id = st->id;
        st2->r_frame_rate        = st->r_frame_rate;
        st2->time_base           = st->time_base;
        st2->start_time          = st->start_time;
        st2->duration            = st->duration;
        st2->nb_frames           = st->nb_frames;
        st2->disposition         = st->disposition;
        st2->sample_aspect_ratio = st->sample_aspect_ratio;
        st2->avg_frame_rate      = st->avg_frame_rate;
        av_dict_copy(&st2->metadata, st->metadata, 0);
        if ((ret = avcodec_parameters_copy(st2->codecpar, st->codecpar)) < 0)
            goto end;
    }

    if (!(avf2->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avf2->io_open(avf2, &avf2->pb, filename, AVIO_FLAG_WRITE, NULL)) < 0) {
            av_log(avf, AV_LOG_ERROR, "Slave '%s': error opening: %s\n",
                   slave, av_err2str(ret));
            goto end;
        }
    }

    if ((ret = avformat_write_header(avf2, &options)) < 0) {
        av_log(avf, AV_LOG_ERROR, "Slave '%s': error writing header: %s\n",
               slave, av_err2str(ret));
        goto end;
    }
    tee_slave->header_written = 1;

    tee_slave->bsfs = av_calloc(avf2->nb_streams, sizeof(*tee_slave->bsfs));
    if (!tee_slave->bsfs) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    entry = NULL;
    while (entry = av_dict_get(options, "bsfs", NULL, AV_DICT_IGNORE_SUFFIX)) {
        const char *spec = entry->key + strlen("bsfs");
        if (*spec) {
            if (strspn(spec, slave_bsfs_spec_sep) != 1) {
                av_log(avf, AV_LOG_ERROR,
                       "Specifier separator in '%s' is '%c', but only characters '%s' "
                       "are allowed\n", entry->key, *spec, slave_bsfs_spec_sep);
                ret = AVERROR(EINVAL);
                goto end;
            }
            spec++; /* consume separator */
        }

        for (i = 0; i < avf2->nb_streams; i++) {
            ret = avformat_match_stream_specifier(avf2, avf2->streams[i], spec);
            if (ret < 0) {
                av_log(avf, AV_LOG_ERROR,
                       "Invalid stream specifier '%s' in bsfs option '%s' for slave "
                       "output '%s'\n", spec, entry->key, filename);
                goto end;
            }

            if (ret > 0) {
                av_log(avf, AV_LOG_DEBUG, "spec:%s bsfs:%s matches stream %d of slave "
                       "output '%s'\n", spec, entry->value, i, filename);
                if (tee_slave->bsfs[i]) {
                    av_log(avf, AV_LOG_WARNING,
                           "Duplicate bsfs specification associated to stream %d of slave "
                           "output '%s', filters will be ignored\n", i, filename);
                    continue;
                }
                ret = parse_bsfs(avf, entry->value, &tee_slave->bsfs[i]);
                if (ret < 0) {
                    av_log(avf, AV_LOG_ERROR,
                           "Error parsing bitstream filter sequence '%s' associated to "
                           "stream %d of slave output '%s'\n", entry->value, i, filename);
                    goto end;
                }
            }
        }

        av_dict_set(&options, entry->key, NULL, 0);
    }

    if (options) {
        entry = NULL;
        while ((entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX)))
            av_log(avf2, AV_LOG_ERROR, "Unknown option '%s'\n", entry->key);
        ret = AVERROR_OPTION_NOT_FOUND;
        goto end;
    }

end:
    av_free(format);
    av_free(select);
    av_free(on_fail);
    av_dict_free(&options);
    av_freep(&tmp_select);
    return ret;
}

static void log_slave(TeeSlave *slave, void *log_ctx, int log_level)
{
    int i;
    av_log(log_ctx, log_level, "filename:'%s' format:%s\n",
           slave->avf->filename, slave->avf->oformat->name);
    for (i = 0; i < slave->avf->nb_streams; i++) {
        AVStream *st = slave->avf->streams[i];
        AVBitStreamFilterContext *bsf = slave->bsfs[i];

        av_log(log_ctx, log_level, "    stream:%d codec:%s type:%s",
               i, avcodec_get_name(st->codecpar->codec_id),
               av_get_media_type_string(st->codecpar->codec_type));
        if (bsf) {
            av_log(log_ctx, log_level, " bsfs:");
            while (bsf) {
                av_log(log_ctx, log_level, "%s%s",
                       bsf->filter->name, bsf->next ? "," : "");
                bsf = bsf->next;
            }
        }
        av_log(log_ctx, log_level, "\n");
    }
}

static int tee_process_slave_failure(AVFormatContext *avf, unsigned slave_idx, int err_n)
{
    TeeContext *tee = avf->priv_data;
    TeeSlave *tee_slave = &tee->slaves[slave_idx];

    tee->nb_alive--;

    close_slave(tee_slave);

    if (!tee->nb_alive) {
        av_log(avf, AV_LOG_ERROR, "All tee outputs failed.\n");
        return err_n;
    } else if (tee_slave->on_fail == ON_SLAVE_FAILURE_ABORT) {
        av_log(avf, AV_LOG_ERROR, "Slave muxer #%u failed, aborting.\n", slave_idx);
        return err_n;
    } else {
        av_log(avf, AV_LOG_ERROR, "Slave muxer #%u failed: %s, continuing with %u/%u slaves.\n",
               slave_idx, av_err2str(err_n), tee->nb_alive, tee->nb_slaves);
        return 0;
    }
}

static int tee_write_header(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    unsigned nb_slaves = 0, i;
    const char *filename = avf->filename;
    char **slaves = NULL;
    int ret;

    while (*filename) {
        char *slave = av_get_token(&filename, slave_delim);
        if (!slave) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        ret = av_dynarray_add_nofree(&slaves, &nb_slaves, slave);
        if (ret < 0) {
            av_free(slave);
            goto fail;
        }
        if (strspn(filename, slave_delim))
            filename++;
    }

    if (!(tee->slaves = av_mallocz_array(nb_slaves, sizeof(*tee->slaves)))) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    tee->nb_slaves = tee->nb_alive = nb_slaves;

    for (i = 0; i < nb_slaves; i++) {
        if ((ret = open_slave(avf, slaves[i], &tee->slaves[i])) < 0) {
            ret = tee_process_slave_failure(avf, i, ret);
            if (ret < 0)
                goto fail;
        } else {
            log_slave(&tee->slaves[i], avf, AV_LOG_VERBOSE);
        }
        av_freep(&slaves[i]);
    }

    for (i = 0; i < avf->nb_streams; i++) {
        int j, mapped = 0;
        for (j = 0; j < tee->nb_slaves; j++)
            if (tee->slaves[j].avf)
                mapped += tee->slaves[j].stream_map[i] >= 0;
        if (!mapped)
            av_log(avf, AV_LOG_WARNING, "Input stream #%d is not mapped "
                   "to any slave.\n", i);
    }
    av_free(slaves);
    return 0;

fail:
    for (i = 0; i < nb_slaves; i++)
        av_freep(&slaves[i]);
    close_slaves(avf);
    av_free(slaves);
    return ret;
}

static int tee_write_trailer(AVFormatContext *avf)
{
    TeeContext *tee = avf->priv_data;
    int ret_all = 0, ret;
    unsigned i;

    for (i = 0; i < tee->nb_slaves; i++) {
        if ((ret = close_slave(&tee->slaves[i])) < 0) {
            ret = tee_process_slave_failure(avf, i, ret);
            if (!ret_all && ret < 0)
                ret_all = ret;
        }
    }
    av_freep(&tee->slaves);
    return ret_all;
}

static int tee_write_packet(AVFormatContext *avf, AVPacket *pkt)
{
    TeeContext *tee = avf->priv_data;
    AVFormatContext *avf2;
    AVPacket pkt2;
    int ret_all = 0, ret;
    unsigned i, s;
    int s2;
    AVRational tb, tb2;

    for (i = 0; i < tee->nb_slaves; i++) {
        if (!(avf2 = tee->slaves[i].avf))
            continue;

        s = pkt->stream_index;
        s2 = tee->slaves[i].stream_map[s];
        if (s2 < 0)
            continue;

        memset(&pkt2, 0, sizeof(AVPacket));
        if ((ret = av_packet_ref(&pkt2, pkt)) < 0)
            if (!ret_all) {
                ret_all = ret;
                continue;
            }
        tb  = avf ->streams[s ]->time_base;
        tb2 = avf2->streams[s2]->time_base;
        pkt2.pts      = av_rescale_q(pkt->pts,      tb, tb2);
        pkt2.dts      = av_rescale_q(pkt->dts,      tb, tb2);
        pkt2.duration = av_rescale_q(pkt->duration, tb, tb2);
        pkt2.stream_index = s2;

        if ((ret = av_apply_bitstream_filters(avf2->streams[s2]->codec, &pkt2,
                                              tee->slaves[i].bsfs[s2])) < 0 ||
            (ret = av_interleaved_write_frame(avf2, &pkt2)) < 0) {
            ret = tee_process_slave_failure(avf, i, ret);
            if (!ret_all && ret < 0)
                ret_all = ret;
        }
    }
    return ret_all;
}

AVOutputFormat ff_tee_muxer = {
    .name              = "tee",
    .long_name         = NULL_IF_CONFIG_SMALL("Multiple muxer tee"),
    .priv_data_size    = sizeof(TeeContext),
    .write_header      = tee_write_header,
    .write_trailer     = tee_write_trailer,
    .write_packet      = tee_write_packet,
    .priv_class        = &tee_muxer_class,
    .flags             = AVFMT_NOFILE,
};
