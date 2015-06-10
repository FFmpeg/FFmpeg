/*
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
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

#include <float.h>
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

// FIXME those are internal headers, ffserver _really_ shouldn't use them
#include "libavformat/ffm.h"

#include "cmdutils.h"
#include "ffserver_config.h"

#define MAX_CHILD_ARGS 64

static int ffserver_save_avoption(const char *opt, const char *arg, int type,
                                  FFServerConfig *config);
static void vreport_config_error(const char *filename, int line_num,
                                 int log_level, int *errors, const char *fmt,
                                 va_list vl);
static void report_config_error(const char *filename, int line_num,
                                int log_level, int *errors, const char *fmt,
                                ...);

#define ERROR(...)   report_config_error(config->filename, config->line_num,\
                                         AV_LOG_ERROR, &config->errors,  __VA_ARGS__)
#define WARNING(...) report_config_error(config->filename, config->line_num,\
                                         AV_LOG_WARNING, &config->warnings, __VA_ARGS__)

/* FIXME: make ffserver work with IPv6 */
/* resolve host with also IP address parsing */
static int resolve_host(struct in_addr *sin_addr, const char *hostname)
{

    if (!ff_inet_aton(hostname, sin_addr)) {
#if HAVE_GETADDRINFO
        struct addrinfo *ai, *cur;
        struct addrinfo hints = { 0 };
        hints.ai_family = AF_INET;
        if (getaddrinfo(hostname, NULL, &hints, &ai))
            return -1;
        /* getaddrinfo returns a linked list of addrinfo structs.
         * Even if we set ai_family = AF_INET above, make sure
         * that the returned one actually is of the correct type. */
        for (cur = ai; cur; cur = cur->ai_next) {
            if (cur->ai_family == AF_INET) {
                *sin_addr = ((struct sockaddr_in *)cur->ai_addr)->sin_addr;
                freeaddrinfo(ai);
                return 0;
            }
        }
        freeaddrinfo(ai);
        return -1;
#else
        struct hostent *hp;
        hp = gethostbyname(hostname);
        if (!hp)
            return -1;
        memcpy(sin_addr, hp->h_addr_list[0], sizeof(struct in_addr));
#endif
    }
    return 0;
}

void ffserver_get_arg(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int quote = 0;

    p = *pp;
    q = buf;

    while (av_isspace(*p)) p++;

    if (*p == '\"' || *p == '\'')
        quote = *p++;

    while (*p != '\0') {
        if (quote && *p == quote || !quote && av_isspace(*p))
            break;
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }

    *q = '\0';
    if (quote && *p == quote)
        p++;
    *pp = p;
}

void ffserver_parse_acl_row(FFServerStream *stream, FFServerStream* feed,
                            FFServerIPAddressACL *ext_acl,
                            const char *p, const char *filename, int line_num)
{
    char arg[1024];
    FFServerIPAddressACL acl;
    int errors = 0;

    ffserver_get_arg(arg, sizeof(arg), &p);
    if (av_strcasecmp(arg, "allow") == 0)
        acl.action = IP_ALLOW;
    else if (av_strcasecmp(arg, "deny") == 0)
        acl.action = IP_DENY;
    else {
        fprintf(stderr, "%s:%d: ACL action '%s' should be ALLOW or DENY.\n",
                filename, line_num, arg);
        errors++;
    }

    ffserver_get_arg(arg, sizeof(arg), &p);

    if (resolve_host(&acl.first, arg)) {
        fprintf(stderr,
                "%s:%d: ACL refers to invalid host or IP address '%s'\n",
                filename, line_num, arg);
        errors++;
    } else
        acl.last = acl.first;

    ffserver_get_arg(arg, sizeof(arg), &p);

    if (arg[0]) {
        if (resolve_host(&acl.last, arg)) {
            fprintf(stderr,
                    "%s:%d: ACL refers to invalid host or IP address '%s'\n",
                    filename, line_num, arg);
            errors++;
        }
    }

    if (!errors) {
        FFServerIPAddressACL *nacl = av_mallocz(sizeof(*nacl));
        FFServerIPAddressACL **naclp = 0;

        acl.next = 0;
        *nacl = acl;

        if (stream)
            naclp = &stream->acl;
        else if (feed)
            naclp = &feed->acl;
        else if (ext_acl)
            naclp = &ext_acl;
        else {
            fprintf(stderr, "%s:%d: ACL found not in <Stream> or <Feed>\n",
                    filename, line_num);
            errors++;
        }

        if (naclp) {
            while (*naclp)
                naclp = &(*naclp)->next;

            *naclp = nacl;
        } else
            av_free(nacl);
    }
}

/* add a codec and set the default parameters */
static void add_codec(FFServerStream *stream, AVCodecContext *av,
                      FFServerConfig *config)
{
    AVStream *st;
    AVDictionary **opts, *recommended = NULL;
    char *enc_config;

    if(stream->nb_streams >= FF_ARRAY_ELEMS(stream->streams))
        return;

    opts = av->codec_type == AVMEDIA_TYPE_AUDIO ?
           &config->audio_opts : &config->video_opts;
    av_dict_copy(&recommended, *opts, 0);
    av_opt_set_dict2(av->priv_data, opts, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_dict2(av, opts, AV_OPT_SEARCH_CHILDREN);

    if (av_dict_count(*opts))
        av_log(NULL, AV_LOG_WARNING,
               "Something is wrong, %d options are not set!\n",
               av_dict_count(*opts));

    if (!config->stream_use_defaults) {
        switch(av->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            if (av->bit_rate == 0)
                report_config_error(config->filename, config->line_num,
                                    AV_LOG_ERROR, &config->errors,
                                    "audio bit rate is not set\n");
            if (av->sample_rate == 0)
                report_config_error(config->filename, config->line_num,
                                    AV_LOG_ERROR, &config->errors,
                                    "audio sample rate is not set\n");
            break;
        case AVMEDIA_TYPE_VIDEO:
            if (av->width == 0 || av->height == 0)
                report_config_error(config->filename, config->line_num,
                                    AV_LOG_ERROR, &config->errors,
                                    "video size is not set\n");
            break;
        default:
            av_assert0(0);
        }
        goto done;
    }

    /* stream_use_defaults = true */

    /* compute default parameters */
    switch(av->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (!av_dict_get(recommended, "ab", NULL, 0)) {
            av->bit_rate = 64000;
            av_dict_set_int(&recommended, "ab", av->bit_rate, 0);
            WARNING("Setting default value for audio bit rate = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->bit_rate);
        }
        if (!av_dict_get(recommended, "ar", NULL, 0)) {
            av->sample_rate = 22050;
            av_dict_set_int(&recommended, "ar", av->sample_rate, 0);
            WARNING("Setting default value for audio sample rate = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->sample_rate);
        }
        if (!av_dict_get(recommended, "ac", NULL, 0)) {
            av->channels = 1;
            av_dict_set_int(&recommended, "ac", av->channels, 0);
            WARNING("Setting default value for audio channel count = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->channels);
        }
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (!av_dict_get(recommended, "b", NULL, 0)) {
            av->bit_rate = 64000;
            av_dict_set_int(&recommended, "b", av->bit_rate, 0);
            WARNING("Setting default value for video bit rate = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->bit_rate);
        }
        if (!av_dict_get(recommended, "time_base", NULL, 0)){
            av->time_base.den = 5;
            av->time_base.num = 1;
            av_dict_set(&recommended, "time_base", "1/5", 0);
            WARNING("Setting default value for video frame rate = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->time_base.den);
        }
        if (!av_dict_get(recommended, "video_size", NULL, 0)) {
            av->width = 160;
            av->height = 128;
            av_dict_set(&recommended, "video_size", "160x128", 0);
            WARNING("Setting default value for video size = %dx%d. "
                    "Use NoDefaults to disable it.\n",
                    av->width, av->height);
        }
        /* Bitrate tolerance is less for streaming */
        if (!av_dict_get(recommended, "bt", NULL, 0)) {
            av->bit_rate_tolerance = FFMAX(av->bit_rate / 4,
                      (int64_t)av->bit_rate*av->time_base.num/av->time_base.den);
            av_dict_set_int(&recommended, "bt", av->bit_rate_tolerance, 0);
            WARNING("Setting default value for video bit rate tolerance = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->bit_rate_tolerance);
        }

        if (!av_dict_get(recommended, "rc_eq", NULL, 0)) {
            av->rc_eq = av_strdup("tex^qComp");
            av_dict_set(&recommended, "rc_eq", "tex^qComp", 0);
            WARNING("Setting default value for video rate control equation = "
                    "%s. Use NoDefaults to disable it.\n",
                    av->rc_eq);
        }
        if (!av_dict_get(recommended, "maxrate", NULL, 0)) {
            av->rc_max_rate = av->bit_rate * 2;
            av_dict_set_int(&recommended, "maxrate", av->rc_max_rate, 0);
            WARNING("Setting default value for video max rate = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->rc_max_rate);
        }

        if (av->rc_max_rate && !av_dict_get(recommended, "bufsize", NULL, 0)) {
            av->rc_buffer_size = av->rc_max_rate;
            av_dict_set_int(&recommended, "bufsize", av->rc_buffer_size, 0);
            WARNING("Setting default value for video buffer size = %d. "
                    "Use NoDefaults to disable it.\n",
                    av->rc_buffer_size);
        }
        break;
    default:
        abort();
    }

done:
    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return;
    av_dict_get_string(recommended, &enc_config, '=', ',');
    av_dict_free(&recommended);
    av_stream_set_recommended_encoder_configuration(st, enc_config);
    st->codec = av;
    stream->streams[stream->nb_streams++] = st;
}

static int ffserver_set_codec(AVCodecContext *ctx, const char *codec_name,
                              FFServerConfig *config)
{
    int ret;
    AVCodec *codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec || codec->type != ctx->codec_type) {
        report_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                            &config->errors,
                            "Invalid codec name: '%s'\n", codec_name);
        return 0;
    }
    if (ctx->codec_id == AV_CODEC_ID_NONE && !ctx->priv_data) {
        if ((ret = avcodec_get_context_defaults3(ctx, codec)) < 0)
            return ret;
        ctx->codec = codec;
    }
    if (ctx->codec_id != codec->id)
        report_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                            &config->errors,
                            "Inconsistent configuration: trying to set '%s' "
                            "codec option, but '%s' codec is used previously\n",
                            codec_name, avcodec_get_name(ctx->codec_id));
    return 0;
}

static int ffserver_opt_preset(const char *arg, int type, FFServerConfig *config)
{
    FILE *f=NULL;
    char filename[1000], tmp[1000], tmp2[1000], line[1000];
    int ret = 0;
    AVCodecContext *avctx;
    const AVCodec *codec;

    switch(type) {
    case AV_OPT_FLAG_AUDIO_PARAM:
        avctx = config->dummy_actx;
        break;
    case AV_OPT_FLAG_VIDEO_PARAM:
        avctx = config->dummy_vctx;
        break;
    default:
        av_assert0(0);
    }
    codec = avcodec_find_encoder(avctx->codec_id);

    if (!(f = get_preset_file(filename, sizeof(filename), arg, 0,
                              codec ? codec->name : NULL))) {
        av_log(NULL, AV_LOG_ERROR, "File for preset '%s' not found\n", arg);
        return AVERROR(EINVAL);
    }

    while(!feof(f)){
        int e= fscanf(f, "%999[^\n]\n", line) - 1;
        if(line[0] == '#' && !e)
            continue;
        e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;
        if(e){
            av_log(NULL, AV_LOG_ERROR, "%s: Invalid syntax: '%s'\n", filename,
                   line);
            ret = AVERROR(EINVAL);
            break;
        }
        if (!strcmp(tmp, "acodec") && avctx->codec_type == AVMEDIA_TYPE_AUDIO ||
            !strcmp(tmp, "vcodec") && avctx->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (ffserver_set_codec(avctx, tmp2, config) < 0)
                break;
        } else if (!strcmp(tmp, "scodec")) {
            av_log(NULL, AV_LOG_ERROR, "Subtitles preset found.\n");
            ret = AVERROR(EINVAL);
            break;
        } else if (ffserver_save_avoption(tmp, tmp2, type, config) < 0)
            break;
    }

    fclose(f);

    return ret;
}

static AVOutputFormat *ffserver_guess_format(const char *short_name,
                                             const char *filename,
                                             const char *mime_type)
{
    AVOutputFormat *fmt = av_guess_format(short_name, filename, mime_type);

    if (fmt) {
        AVOutputFormat *stream_fmt;
        char stream_format_name[64];

        snprintf(stream_format_name, sizeof(stream_format_name), "%s_stream",
                fmt->name);
        stream_fmt = av_guess_format(stream_format_name, NULL, NULL);

        if (stream_fmt)
            fmt = stream_fmt;
    }

    return fmt;
}

static void vreport_config_error(const char *filename, int line_num,
                                 int log_level, int *errors, const char *fmt,
                                 va_list vl)
{
    av_log(NULL, log_level, "%s:%d: ", filename, line_num);
    av_vlog(NULL, log_level, fmt, vl);
    if (errors)
        (*errors)++;
}

static void report_config_error(const char *filename, int line_num,
                                int log_level, int *errors,
                                const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    vreport_config_error(filename, line_num, log_level, errors, fmt, vl);
    va_end(vl);
}

static int ffserver_set_int_param(int *dest, const char *value, int factor,
                                  int min, int max, FFServerConfig *config,
                                  const char *error_msg, ...)
{
    int tmp;
    char *tailp;
    if (!value || !value[0])
        goto error;
    errno = 0;
    tmp = strtol(value, &tailp, 0);
    if (tmp < min || tmp > max)
        goto error;
    if (factor) {
        if (FFABS(tmp) > INT_MAX / FFABS(factor))
            goto error;
        tmp *= factor;
    }
    if (tailp[0] || errno)
        goto error;
    if (dest)
        *dest = tmp;
    return 0;
  error:
    if (config) {
        va_list vl;
        va_start(vl, error_msg);
        vreport_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                &config->errors, error_msg, vl);
        va_end(vl);
    }
    return AVERROR(EINVAL);
}

static int ffserver_set_float_param(float *dest, const char *value,
                                    float factor, float min, float max,
                                    FFServerConfig *config,
                                    const char *error_msg, ...)
{
    double tmp;
    char *tailp;
    if (!value || !value[0])
        goto error;
    errno = 0;
    tmp = strtod(value, &tailp);
    if (tmp < min || tmp > max)
        goto error;
    if (factor)
        tmp *= factor;
    if (tailp[0] || errno)
        goto error;
    if (dest)
        *dest = tmp;
    return 0;
  error:
    if (config) {
        va_list vl;
        va_start(vl, error_msg);
        vreport_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                &config->errors, error_msg, vl);
        va_end(vl);
    }
    return AVERROR(EINVAL);
}

static int ffserver_save_avoption(const char *opt, const char *arg, int type,
                                  FFServerConfig *config)
{
    static int hinted = 0;
    int ret = 0;
    AVDictionaryEntry *e;
    const AVOption *o = NULL;
    const char *option = NULL;
    const char *codec_name = NULL;
    char buff[1024];
    AVCodecContext *ctx;
    AVDictionary **dict;
    enum AVCodecID guessed_codec_id;

    switch (type) {
    case AV_OPT_FLAG_VIDEO_PARAM:
        ctx = config->dummy_vctx;
        dict = &config->video_opts;
        guessed_codec_id = config->guessed_video_codec_id != AV_CODEC_ID_NONE ?
                           config->guessed_video_codec_id : AV_CODEC_ID_H264;
        break;
    case AV_OPT_FLAG_AUDIO_PARAM:
        ctx = config->dummy_actx;
        dict = &config->audio_opts;
        guessed_codec_id = config->guessed_audio_codec_id != AV_CODEC_ID_NONE ?
                           config->guessed_audio_codec_id : AV_CODEC_ID_AAC;
        break;
    default:
        av_assert0(0);
    }

    if (strchr(opt, ':')) {
        //explicit private option
        snprintf(buff, sizeof(buff), "%s", opt);
        codec_name = buff;
        if(!(option = strchr(buff, ':'))){
            report_config_error(config->filename, config->line_num,
                                AV_LOG_ERROR, &config->errors,
                                "Syntax error. Unmatched ':'\n");
            return -1;

        }
        buff[option - buff] = '\0';
        option++;
        if ((ret = ffserver_set_codec(ctx, codec_name, config)) < 0)
            return ret;
        if (!ctx->codec || !ctx->priv_data)
            return -1;
    } else {
        option = opt;
    }

    o = av_opt_find(ctx, option, NULL, type | AV_OPT_FLAG_ENCODING_PARAM,
                    AV_OPT_SEARCH_CHILDREN);
    if (!o &&
        (!strcmp(option, "time_base")  || !strcmp(option, "pixel_format") ||
         !strcmp(option, "video_size") || !strcmp(option, "codec_tag")))
        o = av_opt_find(ctx, option, NULL, 0, 0);
    if (!o) {
        report_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                            &config->errors, "Option not found: '%s'\n", opt);
        if (!hinted && ctx->codec_id == AV_CODEC_ID_NONE) {
            hinted = 1;
            report_config_error(config->filename, config->line_num,
                                AV_LOG_ERROR, NULL, "If '%s' is a codec private"
                                "option, then prefix it with codec name, for "
                                "example '%s:%s %s' or define codec earlier.\n",
                                opt, avcodec_get_name(guessed_codec_id) ,opt,
                                arg);
        }
    } else if ((ret = av_opt_set(ctx, option, arg, AV_OPT_SEARCH_CHILDREN)) < 0) {
        report_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                &config->errors, "Invalid value for option %s (%s): %s\n", opt,
                arg, av_err2str(ret));
    } else if ((e = av_dict_get(*dict, option, NULL, 0))) {
        if ((o->type == AV_OPT_TYPE_FLAGS) && arg &&
            (arg[0] == '+' || arg[0] == '-'))
            return av_dict_set(dict, option, arg, AV_DICT_APPEND);
        report_config_error(config->filename, config->line_num, AV_LOG_ERROR,
                            &config->errors, "Redeclaring value of option '%s'."
                            "Previous value was: '%s'.\n", opt, e->value);
    } else if (av_dict_set(dict, option, arg, 0) < 0) {
        return AVERROR(ENOMEM);
    }
    return 0;
}

static int ffserver_save_avoption_int(const char *opt, int64_t arg,
                                      int type, FFServerConfig *config)
{
    char buf[22];
    snprintf(buf, sizeof(buf), "%"PRId64, arg);
    return ffserver_save_avoption(opt, buf, type, config);
}

static int ffserver_parse_config_global(FFServerConfig *config, const char *cmd,
                                        const char **p)
{
    int val;
    char arg[1024];
    if (!av_strcasecmp(cmd, "Port") || !av_strcasecmp(cmd, "HTTPPort")) {
        if (!av_strcasecmp(cmd, "Port"))
            WARNING("Port option is deprecated. Use HTTPPort instead.\n");
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, 1, 65535, config,
                "Invalid port: %s\n", arg);
        if (val < 1024)
            WARNING("Trying to use IETF assigned system port: '%d'\n", val);
        config->http_addr.sin_port = htons(val);
    } else if (!av_strcasecmp(cmd, "HTTPBindAddress") ||
               !av_strcasecmp(cmd, "BindAddress")) {
        if (!av_strcasecmp(cmd, "BindAddress"))
            WARNING("BindAddress option is deprecated. Use HTTPBindAddress "
                    "instead.\n");
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&config->http_addr.sin_addr, arg))
            ERROR("Invalid host/IP address: '%s'\n", arg);
    } else if (!av_strcasecmp(cmd, "NoDaemon")) {
        WARNING("NoDaemon option has no effect. You should remove it.\n");
    } else if (!av_strcasecmp(cmd, "RTSPPort")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, 1, 65535, config,
                "Invalid port: %s\n", arg);
        config->rtsp_addr.sin_port = htons(val);
    } else if (!av_strcasecmp(cmd, "RTSPBindAddress")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&config->rtsp_addr.sin_addr, arg))
            ERROR("Invalid host/IP address: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "MaxHTTPConnections")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, 1, 65535, config,
                "Invalid MaxHTTPConnections: %s\n", arg);
        config->nb_max_http_connections = val;
        if (config->nb_max_connections > config->nb_max_http_connections) {
            ERROR("Inconsistent configuration: MaxClients(%d) > "
                  "MaxHTTPConnections(%d)\n", config->nb_max_connections,
                  config->nb_max_http_connections);
        }
    } else if (!av_strcasecmp(cmd, "MaxClients")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, 1, 65535, config,
                "Invalid MaxClients: '%s'\n", arg);
        config->nb_max_connections = val;
        if (config->nb_max_connections > config->nb_max_http_connections) {
            ERROR("Inconsistent configuration: MaxClients(%d) > "
                  "MaxHTTPConnections(%d)\n", config->nb_max_connections,
                  config->nb_max_http_connections);
        }
    } else if (!av_strcasecmp(cmd, "MaxBandwidth")) {
        int64_t llval;
        char *tailp;
        ffserver_get_arg(arg, sizeof(arg), p);
        errno = 0;
        llval = strtoll(arg, &tailp, 10);
        if (llval < 10 || llval > 10000000 || tailp[0] || errno)
            ERROR("Invalid MaxBandwidth: '%s'\n", arg);
        else
            config->max_bandwidth = llval;
    } else if (!av_strcasecmp(cmd, "CustomLog")) {
        if (!config->debug) {
            ffserver_get_arg(config->logfilename, sizeof(config->logfilename),
                             p);
        }
    } else if (!av_strcasecmp(cmd, "LoadModule")) {
        ERROR("Loadable modules are no longer supported\n");
    } else if (!av_strcasecmp(cmd, "NoDefaults")) {
        config->use_defaults = 0;
    } else if (!av_strcasecmp(cmd, "UseDefaults")) {
        config->use_defaults = 1;
    } else
        ERROR("Incorrect keyword: '%s'\n", cmd);
    return 0;
}

static int ffserver_parse_config_feed(FFServerConfig *config, const char *cmd, const char **p,
                                      FFServerStream **pfeed)
{
    FFServerStream *feed;
    char arg[1024];
    av_assert0(pfeed);
    feed = *pfeed;
    if (!av_strcasecmp(cmd, "<Feed")) {
        char *q;
        FFServerStream *s;
        feed = av_mallocz(sizeof(FFServerStream));
        if (!feed)
            return AVERROR(ENOMEM);
        ffserver_get_arg(feed->filename, sizeof(feed->filename), p);
        q = strrchr(feed->filename, '>');
        if (*q)
            *q = '\0';

        for (s = config->first_feed; s; s = s->next) {
            if (!strcmp(feed->filename, s->filename))
                ERROR("Feed '%s' already registered\n", s->filename);
        }

        feed->fmt = av_guess_format("ffm", NULL, NULL);
        /* default feed file */
        snprintf(feed->feed_filename, sizeof(feed->feed_filename),
                 "/tmp/%s.ffm", feed->filename);
        feed->feed_max_size = 5 * 1024 * 1024;
        feed->is_feed = 1;
        feed->feed = feed; /* self feeding :-) */
        *pfeed = feed;
        return 0;
    }
    av_assert0(feed);
    if (!av_strcasecmp(cmd, "Launch")) {
        int i;

        feed->child_argv = av_mallocz_array(MAX_CHILD_ARGS, sizeof(char *));
        if (!feed->child_argv)
            return AVERROR(ENOMEM);
        for (i = 0; i < MAX_CHILD_ARGS - 2; i++) {
            ffserver_get_arg(arg, sizeof(arg), p);
            if (!arg[0])
                break;

            feed->child_argv[i] = av_strdup(arg);
            if (!feed->child_argv[i])
                return AVERROR(ENOMEM);
        }

        feed->child_argv[i] =
            av_asprintf("http://%s:%d/%s",
                        (config->http_addr.sin_addr.s_addr == INADDR_ANY) ?
                        "127.0.0.1" : inet_ntoa(config->http_addr.sin_addr),
                        ntohs(config->http_addr.sin_port), feed->filename);
        if (!feed->child_argv[i])
            return AVERROR(ENOMEM);
    } else if (!av_strcasecmp(cmd, "ACL")) {
        ffserver_parse_acl_row(NULL, feed, NULL, *p, config->filename,
                config->line_num);
    } else if (!av_strcasecmp(cmd, "File") ||
               !av_strcasecmp(cmd, "ReadOnlyFile")) {
        ffserver_get_arg(feed->feed_filename, sizeof(feed->feed_filename), p);
        feed->readonly = !av_strcasecmp(cmd, "ReadOnlyFile");
    } else if (!av_strcasecmp(cmd, "Truncate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        /* assume Truncate is true in case no argument is specified */
        if (!arg[0]) {
            feed->truncate = 1;
        } else {
            WARNING("Truncate N syntax in configuration file is deprecated. "
                    "Use Truncate alone with no arguments.\n");
            feed->truncate = strtod(arg, NULL);
        }
    } else if (!av_strcasecmp(cmd, "FileMaxSize")) {
        char *p1;
        double fsize;

        ffserver_get_arg(arg, sizeof(arg), p);
        p1 = arg;
        fsize = strtod(p1, &p1);
        switch(av_toupper(*p1)) {
        case 'K':
            fsize *= 1024;
            break;
        case 'M':
            fsize *= 1024 * 1024;
            break;
        case 'G':
            fsize *= 1024 * 1024 * 1024;
            break;
        default:
            ERROR("Invalid file size: '%s'\n", arg);
            break;
        }
        feed->feed_max_size = (int64_t)fsize;
        if (feed->feed_max_size < FFM_PACKET_SIZE*4) {
            ERROR("Feed max file size is too small. Must be at least %d.\n",
                  FFM_PACKET_SIZE*4);
        }
    } else if (!av_strcasecmp(cmd, "</Feed>")) {
        *pfeed = NULL;
    } else {
        ERROR("Invalid entry '%s' inside <Feed></Feed>\n", cmd);
    }
    return 0;
}

static int ffserver_parse_config_stream(FFServerConfig *config, const char *cmd, const char **p,
                                        FFServerStream **pstream)
{
    char arg[1024], arg2[1024];
    FFServerStream *stream;
    int val;

    av_assert0(pstream);
    stream = *pstream;

    if (!av_strcasecmp(cmd, "<Stream")) {
        char *q;
        FFServerStream *s;
        stream = av_mallocz(sizeof(FFServerStream));
        if (!stream)
            return AVERROR(ENOMEM);
        config->dummy_actx = avcodec_alloc_context3(NULL);
        config->dummy_vctx = avcodec_alloc_context3(NULL);
        if (!config->dummy_vctx || !config->dummy_actx) {
            av_free(stream);
            avcodec_free_context(&config->dummy_vctx);
            avcodec_free_context(&config->dummy_actx);
            return AVERROR(ENOMEM);
        }
        config->dummy_actx->codec_type = AVMEDIA_TYPE_AUDIO;
        config->dummy_vctx->codec_type = AVMEDIA_TYPE_VIDEO;
        ffserver_get_arg(stream->filename, sizeof(stream->filename), p);
        q = strrchr(stream->filename, '>');
        if (q)
            *q = '\0';

        for (s = config->first_stream; s; s = s->next) {
            if (!strcmp(stream->filename, s->filename))
                ERROR("Stream '%s' already registered\n", s->filename);
        }

        stream->fmt = ffserver_guess_format(NULL, stream->filename, NULL);
        if (stream->fmt) {
            config->guessed_audio_codec_id = stream->fmt->audio_codec;
            config->guessed_video_codec_id = stream->fmt->video_codec;
        } else {
            config->guessed_audio_codec_id = AV_CODEC_ID_NONE;
            config->guessed_video_codec_id = AV_CODEC_ID_NONE;
        }
        config->stream_use_defaults = config->use_defaults;
        *pstream = stream;
        return 0;
    }
    av_assert0(stream);
    if (!av_strcasecmp(cmd, "Feed")) {
        FFServerStream *sfeed;
        ffserver_get_arg(arg, sizeof(arg), p);
        sfeed = config->first_feed;
        while (sfeed) {
            if (!strcmp(sfeed->filename, arg))
                break;
            sfeed = sfeed->next_feed;
        }
        if (!sfeed)
            ERROR("Feed with name '%s' for stream '%s' is not defined\n", arg,
                    stream->filename);
        else
            stream->feed = sfeed;
    } else if (!av_strcasecmp(cmd, "Format")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (!strcmp(arg, "status")) {
            stream->stream_type = STREAM_TYPE_STATUS;
            stream->fmt = NULL;
        } else {
            stream->stream_type = STREAM_TYPE_LIVE;
            /* JPEG cannot be used here, so use single frame MJPEG */
            if (!strcmp(arg, "jpeg")) {
                strcpy(arg, "singlejpeg");
                stream->single_frame=1;
            }
            stream->fmt = ffserver_guess_format(arg, NULL, NULL);
            if (!stream->fmt)
                ERROR("Unknown Format: '%s'\n", arg);
        }
        if (stream->fmt) {
            config->guessed_audio_codec_id = stream->fmt->audio_codec;
            config->guessed_video_codec_id = stream->fmt->video_codec;
        }
    } else if (!av_strcasecmp(cmd, "InputFormat")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->ifmt = av_find_input_format(arg);
        if (!stream->ifmt)
            ERROR("Unknown input format: '%s'\n", arg);
    } else if (!av_strcasecmp(cmd, "FaviconURL")) {
        if (stream->stream_type == STREAM_TYPE_STATUS)
            ffserver_get_arg(stream->feed_filename,
                    sizeof(stream->feed_filename), p);
        else
            ERROR("FaviconURL only permitted for status streams\n");
    } else if (!av_strcasecmp(cmd, "Author")    ||
               !av_strcasecmp(cmd, "Comment")   ||
               !av_strcasecmp(cmd, "Copyright") ||
               !av_strcasecmp(cmd, "Title")) {
        char key[32];
        int i;
        ffserver_get_arg(arg, sizeof(arg), p);
        for (i = 0; i < strlen(cmd); i++)
            key[i] = av_tolower(cmd[i]);
        key[i] = 0;
        WARNING("Deprecated '%s' option in configuration file. Use "
                "'Metadata %s VALUE' instead.\n", cmd, key);
        if (av_dict_set(&stream->metadata, key, arg, 0) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "Metadata")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_get_arg(arg2, sizeof(arg2), p);
        if (av_dict_set(&stream->metadata, arg, arg2, 0) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "Preroll")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->prebuffer = atof(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "StartSendOnKey")) {
        stream->send_on_key = 1;
    } else if (!av_strcasecmp(cmd, "AudioCodec")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_codec(config->dummy_actx, arg, config);
    } else if (!av_strcasecmp(cmd, "VideoCodec")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_codec(config->dummy_vctx, arg, config);
    } else if (!av_strcasecmp(cmd, "MaxTime")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->max_time = atof(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "AudioBitRate")) {
        float f;
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_float_param(&f, arg, 1000, -FLT_MAX, FLT_MAX, config,
                "Invalid %s: '%s'\n", cmd, arg);
        if (ffserver_save_avoption_int("ab", (int64_t)lrintf(f),
                                       AV_OPT_FLAG_AUDIO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "AudioChannels")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("ac", arg, AV_OPT_FLAG_AUDIO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "AudioSampleRate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("ar", arg, AV_OPT_FLAG_AUDIO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoBitRateRange")) {
        int minrate, maxrate;
        char *dash;
        ffserver_get_arg(arg, sizeof(arg), p);
        dash = strchr(arg, '-');
        if (dash) {
            *dash = '\0';
            dash++;
            if (ffserver_set_int_param(&minrate, arg,  1000, 0, INT_MAX, config, "Invalid %s: '%s'", cmd, arg) >= 0 &&
                ffserver_set_int_param(&maxrate, dash, 1000, 0, INT_MAX, config, "Invalid %s: '%s'", cmd, arg) >= 0) {
                if (ffserver_save_avoption_int("minrate", minrate, AV_OPT_FLAG_VIDEO_PARAM, config) < 0 ||
                    ffserver_save_avoption_int("maxrate", maxrate, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
                goto nomem;
            }
        } else
            ERROR("Incorrect format for VideoBitRateRange. It should be "
                  "<min>-<max>: '%s'.\n", arg);
    } else if (!av_strcasecmp(cmd, "Debug")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("debug", arg, AV_OPT_FLAG_AUDIO_PARAM, config) < 0 ||
            ffserver_save_avoption("debug", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "Strict")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("strict", arg, AV_OPT_FLAG_AUDIO_PARAM, config) < 0 ||
            ffserver_save_avoption("strict", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoBufferSize")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 8*1024, 0, INT_MAX, config,
                "Invalid %s: '%s'", cmd, arg);
        if (ffserver_save_avoption_int("bufsize", val, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoBitRateTolerance")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 1000, INT_MIN, INT_MAX, config,
                               "Invalid %s: '%s'", cmd, arg);
        if (ffserver_save_avoption_int("bt", val, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoBitRate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 1000, INT_MIN, INT_MAX, config,
                               "Invalid %s: '%s'", cmd, arg);
        if (ffserver_save_avoption_int("b", val, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
           goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoSize")) {
        int ret, w, h;
        ffserver_get_arg(arg, sizeof(arg), p);
        ret = av_parse_video_size(&w, &h, arg);
        if (ret < 0)
            ERROR("Invalid video size '%s'\n", arg);
        else {
            if (w % 2 || h % 2)
                WARNING("Image size is not a multiple of 2\n");
            if (ffserver_save_avoption("video_size", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
                goto nomem;
        }
    } else if (!av_strcasecmp(cmd, "VideoFrameRate")) {
        ffserver_get_arg(&arg[2], sizeof(arg) - 2, p);
        arg[0] = '1'; arg[1] = '/';
        if (ffserver_save_avoption("time_base", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "PixelFormat")) {
        enum AVPixelFormat pix_fmt;
        ffserver_get_arg(arg, sizeof(arg), p);
        pix_fmt = av_get_pix_fmt(arg);
        if (pix_fmt == AV_PIX_FMT_NONE)
            ERROR("Unknown pixel format: '%s'\n", arg);
        else if (ffserver_save_avoption("pixel_format", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoGopSize")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("g", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoIntraOnly")) {
        if (ffserver_save_avoption("g", "1", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoHighQuality")) {
        if (ffserver_save_avoption("mbd", "+bits", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "Video4MotionVector")) {
        if (ffserver_save_avoption("mbd", "+bits",  AV_OPT_FLAG_VIDEO_PARAM, config) < 0 || //FIXME remove
            ffserver_save_avoption("flags", "+mv4", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "AVOptionVideo") ||
               !av_strcasecmp(cmd, "AVOptionAudio")) {
        int ret;
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_get_arg(arg2, sizeof(arg2), p);
        if (!av_strcasecmp(cmd, "AVOptionVideo"))
            ret = ffserver_save_avoption(arg, arg2, AV_OPT_FLAG_VIDEO_PARAM,
                                         config);
        else
            ret = ffserver_save_avoption(arg, arg2, AV_OPT_FLAG_AUDIO_PARAM,
                                         config);
        if (ret < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "AVPresetVideo") ||
               !av_strcasecmp(cmd, "AVPresetAudio")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (!av_strcasecmp(cmd, "AVPresetVideo"))
            ffserver_opt_preset(arg, AV_OPT_FLAG_VIDEO_PARAM, config);
        else
            ffserver_opt_preset(arg, AV_OPT_FLAG_AUDIO_PARAM, config);
    } else if (!av_strcasecmp(cmd, "VideoTag")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (strlen(arg) == 4 &&
            ffserver_save_avoption_int("codec_tag",
                                       MKTAG(arg[0], arg[1], arg[2], arg[3]),
                                       AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "BitExact")) {
        if (ffserver_save_avoption("flags", "+bitexact", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "DctFastint")) {
        if (ffserver_save_avoption("dct", "fastint", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "IdctSimple")) {
        if (ffserver_save_avoption("idct", "simple", AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "Qscale")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, INT_MIN, INT_MAX, config,
                               "Invalid Qscale: '%s'\n", arg);
        if (ffserver_save_avoption("flags", "+qscale", AV_OPT_FLAG_VIDEO_PARAM, config) < 0 ||
            ffserver_save_avoption_int("global_quality", FF_QP2LAMBDA * val,
                                       AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoQDiff")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("qdiff", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoQMax")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("qmax", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "VideoQMin")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("qmin", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "LumiMask")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("lumi_mask", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "DarkMask")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (ffserver_save_avoption("dark_mask", arg, AV_OPT_FLAG_VIDEO_PARAM, config) < 0)
            goto nomem;
    } else if (!av_strcasecmp(cmd, "NoVideo")) {
        config->no_video = 1;
    } else if (!av_strcasecmp(cmd, "NoAudio")) {
        config->no_audio = 1;
    } else if (!av_strcasecmp(cmd, "ACL")) {
        ffserver_parse_acl_row(stream, NULL, NULL, *p, config->filename,
                config->line_num);
    } else if (!av_strcasecmp(cmd, "DynamicACL")) {
        ffserver_get_arg(stream->dynamic_acl, sizeof(stream->dynamic_acl), p);
    } else if (!av_strcasecmp(cmd, "RTSPOption")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        av_freep(&stream->rtsp_option);
        stream->rtsp_option = av_strdup(arg);
    } else if (!av_strcasecmp(cmd, "MulticastAddress")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&stream->multicast_ip, arg))
            ERROR("Invalid host/IP address: '%s'\n", arg);
        stream->is_multicast = 1;
        stream->loop = 1; /* default is looping */
    } else if (!av_strcasecmp(cmd, "MulticastPort")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, 1, 65535, config,
                "Invalid MulticastPort: '%s'\n", arg);
        stream->multicast_port = val;
    } else if (!av_strcasecmp(cmd, "MulticastTTL")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_set_int_param(&val, arg, 0, INT_MIN, INT_MAX, config,
                "Invalid MulticastTTL: '%s'\n", arg);
        stream->multicast_ttl = val;
    } else if (!av_strcasecmp(cmd, "NoLoop")) {
        stream->loop = 0;
    } else if (!av_strcasecmp(cmd, "</Stream>")) {
        config->stream_use_defaults &= 1;
        if (stream->feed && stream->fmt && strcmp(stream->fmt->name, "ffm")) {
            if (config->dummy_actx->codec_id == AV_CODEC_ID_NONE)
                config->dummy_actx->codec_id = config->guessed_audio_codec_id;
            if (!config->no_audio &&
                config->dummy_actx->codec_id != AV_CODEC_ID_NONE) {
                AVCodecContext *audio_enc = avcodec_alloc_context3(avcodec_find_encoder(config->dummy_actx->codec_id));
                add_codec(stream, audio_enc, config);
            }
            if (config->dummy_vctx->codec_id == AV_CODEC_ID_NONE)
                config->dummy_vctx->codec_id = config->guessed_video_codec_id;
            if (!config->no_video &&
                config->dummy_vctx->codec_id != AV_CODEC_ID_NONE) {
                AVCodecContext *video_enc = avcodec_alloc_context3(avcodec_find_encoder(config->dummy_vctx->codec_id));
                add_codec(stream, video_enc, config);
            }
        }
        av_dict_free(&config->video_opts);
        av_dict_free(&config->audio_opts);
        avcodec_free_context(&config->dummy_vctx);
        avcodec_free_context(&config->dummy_actx);
        *pstream = NULL;
    } else if (!av_strcasecmp(cmd, "File") ||
               !av_strcasecmp(cmd, "ReadOnlyFile")) {
        ffserver_get_arg(stream->feed_filename, sizeof(stream->feed_filename),
                p);
    } else if (!av_strcasecmp(cmd, "UseDefaults")) {
        if (config->stream_use_defaults > 1)
            WARNING("Multiple UseDefaults/NoDefaults entries.\n");
        config->stream_use_defaults = 3;
    } else if (!av_strcasecmp(cmd, "NoDefaults")) {
        if (config->stream_use_defaults > 1)
            WARNING("Multiple UseDefaults/NoDefaults entries.\n");
        config->stream_use_defaults = 2;
    } else {
        ERROR("Invalid entry '%s' inside <Stream></Stream>\n", cmd);
    }
    return 0;
  nomem:
    av_log(NULL, AV_LOG_ERROR, "Out of memory. Aborting.\n");
    av_dict_free(&config->video_opts);
    av_dict_free(&config->audio_opts);
    avcodec_free_context(&config->dummy_vctx);
    avcodec_free_context(&config->dummy_actx);
    return AVERROR(ENOMEM);
}

static int ffserver_parse_config_redirect(FFServerConfig *config,
                                          const char *cmd, const char **p,
                                          FFServerStream **predirect)
{
    FFServerStream *redirect;
    av_assert0(predirect);
    redirect = *predirect;

    if (!av_strcasecmp(cmd, "<Redirect")) {
        char *q;
        redirect = av_mallocz(sizeof(FFServerStream));
        if (!redirect)
            return AVERROR(ENOMEM);

        ffserver_get_arg(redirect->filename, sizeof(redirect->filename), p);
        q = strrchr(redirect->filename, '>');
        if (*q)
            *q = '\0';
        redirect->stream_type = STREAM_TYPE_REDIRECT;
        *predirect = redirect;
        return 0;
    }
    av_assert0(redirect);
    if (!av_strcasecmp(cmd, "URL")) {
        ffserver_get_arg(redirect->feed_filename,
                sizeof(redirect->feed_filename), p);
    } else if (!av_strcasecmp(cmd, "</Redirect>")) {
        if (!redirect->feed_filename[0])
            ERROR("No URL found for <Redirect>\n");
        *predirect = NULL;
    } else {
        ERROR("Invalid entry '%s' inside <Redirect></Redirect>\n", cmd);
    }
    return 0;
}

int ffserver_parse_ffconfig(const char *filename, FFServerConfig *config)
{
    FILE *f;
    char line[1024];
    char cmd[64];
    const char *p;
    FFServerStream **last_stream, *stream = NULL, *redirect = NULL;
    FFServerStream **last_feed, *feed = NULL;
    int ret = 0;

    av_assert0(config);

    f = fopen(filename, "r");
    if (!f) {
        ret = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR,
                "Could not open the configuration file '%s'\n", filename);
        return ret;
    }

    config->first_stream = NULL;
    config->first_feed = NULL;
    config->errors = config->warnings = 0;

    last_stream = &config->first_stream;
    last_feed = &config->first_feed;

    config->line_num = 0;
    while (fgets(line, sizeof(line), f) != NULL) {
        config->line_num++;
        p = line;
        while (av_isspace(*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        ffserver_get_arg(cmd, sizeof(cmd), &p);

        if (feed || !av_strcasecmp(cmd, "<Feed")) {
            int opening = !av_strcasecmp(cmd, "<Feed");
            if (opening && (stream || feed || redirect)) {
                ERROR("Already in a tag\n");
            } else {
                ret = ffserver_parse_config_feed(config, cmd, &p, &feed);
                if (ret < 0)
                    break;
                if (opening) {
                    /* add in stream & feed list */
                    *last_stream = feed;
                    *last_feed = feed;
                    last_stream = &feed->next;
                    last_feed = &feed->next_feed;
                }
            }
        } else if (stream || !av_strcasecmp(cmd, "<Stream")) {
            int opening = !av_strcasecmp(cmd, "<Stream");
            if (opening && (stream || feed || redirect)) {
                ERROR("Already in a tag\n");
            } else {
                ret = ffserver_parse_config_stream(config, cmd, &p, &stream);
                if (ret < 0)
                    break;
                if (opening) {
                    /* add in stream list */
                    *last_stream = stream;
                    last_stream = &stream->next;
                }
            }
        } else if (redirect || !av_strcasecmp(cmd, "<Redirect")) {
            int opening = !av_strcasecmp(cmd, "<Redirect");
            if (opening && (stream || feed || redirect))
                ERROR("Already in a tag\n");
            else {
                ret = ffserver_parse_config_redirect(config, cmd, &p,
                                                     &redirect);
                if (ret < 0)
                    break;
                if (opening) {
                    /* add in stream list */
                    *last_stream = redirect;
                    last_stream = &redirect->next;
                }
            }
        } else {
            ffserver_parse_config_global(config, cmd, &p);
        }
    }
    if (stream || feed || redirect)
        ERROR("Missing closing </%s> tag\n",
              stream ? "Stream" : (feed ? "Feed" : "Redirect"));

    fclose(f);
    if (ret < 0)
        return ret;
    if (config->errors)
        return AVERROR(EINVAL);
    else
        return 0;
}

#undef ERROR
#undef WARNING

void ffserver_free_child_args(void *argsp)
{
    int i;
    char **args;
    if (!argsp)
        return;
    args = *(char ***)argsp;
    if (!args)
        return;
    for (i = 0; i < MAX_CHILD_ARGS; i++)
        av_free(args[i]);
    av_freep(argsp);
}
