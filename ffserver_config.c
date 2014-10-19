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

#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/avstring.h"
#include "libavutil/pixdesc.h"
#include "libavutil/avassert.h"

// FIXME those are internal headers, ffserver _really_ shouldn't use them
#include "libavformat/ffm.h"

#include "cmdutils.h"
#include "ffserver_config.h"

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
    int quote;

    p = *pp;
    while (av_isspace(*p)) p++;
    q = buf;
    quote = 0;
    if (*p == '\"' || *p == '\'')
        quote = *p++;
    for(;;) {
        if (quote) {
            if (*p == quote)
                break;
        } else {
            if (av_isspace(*p))
                break;
        }
        if (*p == '\0')
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
        fprintf(stderr, "%s:%d: ACL action '%s' is not ALLOW or DENY\n",
                filename, line_num, arg);
        errors++;
    }

    ffserver_get_arg(arg, sizeof(arg), &p);

    if (resolve_host(&acl.first, arg) != 0) {
        fprintf(stderr, "%s:%d: ACL refers to invalid host or IP address '%s'\n",
                filename, line_num, arg);
        errors++;
    } else
        acl.last = acl.first;

    ffserver_get_arg(arg, sizeof(arg), &p);

    if (arg[0]) {
        if (resolve_host(&acl.last, arg) != 0) {
            fprintf(stderr, "%s:%d: ACL refers to invalid host or IP address '%s'\n",
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
            fprintf(stderr, "%s:%d: ACL found not in <stream> or <feed>\n",
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
static void add_codec(FFServerStream *stream, AVCodecContext *av)
{
    AVStream *st;

    if(stream->nb_streams >= FF_ARRAY_ELEMS(stream->streams))
        return;

    /* compute default parameters */
    switch(av->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->sample_rate == 0)
            av->sample_rate = 22050;
        if (av->channels == 0)
            av->channels = 1;
        break;
    case AVMEDIA_TYPE_VIDEO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->time_base.num == 0){
            av->time_base.den = 5;
            av->time_base.num = 1;
        }
        if (av->width == 0 || av->height == 0) {
            av->width = 160;
            av->height = 128;
        }
        /* Bitrate tolerance is less for streaming */
        if (av->bit_rate_tolerance == 0)
            av->bit_rate_tolerance = FFMAX(av->bit_rate / 4,
                      (int64_t)av->bit_rate*av->time_base.num/av->time_base.den);
        if (av->qmin == 0)
            av->qmin = 3;
        if (av->qmax == 0)
            av->qmax = 31;
        if (av->max_qdiff == 0)
            av->max_qdiff = 3;
        av->qcompress = 0.5;
        av->qblur = 0.5;

        if (!av->nsse_weight)
            av->nsse_weight = 8;

        av->frame_skip_cmp = FF_CMP_DCTMAX;
        if (!av->me_method)
            av->me_method = ME_EPZS;
        av->rc_buffer_aggressivity = 1.0;

        if (!av->rc_eq)
            av->rc_eq = av_strdup("tex^qComp");
        if (!av->i_quant_factor)
            av->i_quant_factor = -0.8;
        if (!av->b_quant_factor)
            av->b_quant_factor = 1.25;
        if (!av->b_quant_offset)
            av->b_quant_offset = 1.25;
        if (!av->rc_max_rate)
            av->rc_max_rate = av->bit_rate * 2;

        if (av->rc_max_rate && !av->rc_buffer_size) {
            av->rc_buffer_size = av->rc_max_rate;
        }


        break;
    default:
        abort();
    }

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return;
    st->codec = avcodec_alloc_context3(NULL);
    stream->streams[stream->nb_streams++] = st;
    memcpy(st->codec, av, sizeof(AVCodecContext));
}

static enum AVCodecID opt_codec(const char *name, enum AVMediaType type)
{
    AVCodec *codec = avcodec_find_encoder_by_name(name);

    if (!codec || codec->type != type)
        return AV_CODEC_ID_NONE;
    return codec->id;
}

static int ffserver_opt_default(const char *opt, const char *arg,
                       AVCodecContext *avctx, int type)
{
    int ret = 0;
    const AVOption *o = av_opt_find(avctx, opt, NULL, type, 0);
    if(o)
        ret = av_opt_set(avctx, opt, arg, 0);
    return ret;
}

static int ffserver_opt_preset(const char *arg,
                       AVCodecContext *avctx, int type,
                       enum AVCodecID *audio_id, enum AVCodecID *video_id)
{
    FILE *f=NULL;
    char filename[1000], tmp[1000], tmp2[1000], line[1000];
    int ret = 0;
    AVCodec *codec = avcodec_find_encoder(avctx->codec_id);

    if (!(f = get_preset_file(filename, sizeof(filename), arg, 0,
                              codec ? codec->name : NULL))) {
        fprintf(stderr, "File for preset '%s' not found\n", arg);
        return 1;
    }

    while(!feof(f)){
        int e= fscanf(f, "%999[^\n]\n", line) - 1;
        if(line[0] == '#' && !e)
            continue;
        e|= sscanf(line, "%999[^=]=%999[^\n]\n", tmp, tmp2) - 2;
        if(e){
            fprintf(stderr, "%s: Invalid syntax: '%s'\n", filename, line);
            ret = 1;
            break;
        }
        if(!strcmp(tmp, "acodec")){
            *audio_id = opt_codec(tmp2, AVMEDIA_TYPE_AUDIO);
        }else if(!strcmp(tmp, "vcodec")){
            *video_id = opt_codec(tmp2, AVMEDIA_TYPE_VIDEO);
        }else if(!strcmp(tmp, "scodec")){
            /* opt_subtitle_codec(tmp2); */
        }else if(ffserver_opt_default(tmp, tmp2, avctx, type) < 0){
            fprintf(stderr, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n", filename, line, tmp, tmp2);
            ret = 1;
            break;
        }
    }

    fclose(f);

    return ret;
}

static AVOutputFormat *ffserver_guess_format(const char *short_name, const char *filename, const char *mime_type)
{
    AVOutputFormat *fmt = av_guess_format(short_name, filename, mime_type);

    if (fmt) {
        AVOutputFormat *stream_fmt;
        char stream_format_name[64];

        snprintf(stream_format_name, sizeof(stream_format_name), "%s_stream", fmt->name);
        stream_fmt = av_guess_format(stream_format_name, NULL, NULL);

        if (stream_fmt)
            fmt = stream_fmt;
    }

    return fmt;
}

static void report_config_error(const char *filename, int line_num, int log_level, int *errors, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    av_log(NULL, log_level, "%s:%d: ", filename, line_num);
    av_vlog(NULL, log_level, fmt, vl);
    va_end(vl);

    (*errors)++;
}

#define ERROR(...)   report_config_error(config->filename, line_num, AV_LOG_ERROR,   &config->errors,   __VA_ARGS__)
#define WARNING(...) report_config_error(config->filename, line_num, AV_LOG_WARNING, &config->warnings, __VA_ARGS__)

static int ffserver_parse_config_global(FFServerConfig *config, const char *cmd,
                                        const char **p, int line_num)
{
    int val;
    char arg[1024];
    if (!av_strcasecmp(cmd, "Port") || !av_strcasecmp(cmd, "HTTPPort")) {
        if (!av_strcasecmp(cmd, "Port"))
            WARNING("Port option is deprecated, use HTTPPort instead\n");
        ffserver_get_arg(arg, sizeof(arg), p);
        val = atoi(arg);
        if (val < 1 || val > 65536)
            ERROR("Invalid port: %s\n", arg);
        if (val < 1024)
            WARNING("Trying to use IETF assigned system port: %d\n", val);
        config->http_addr.sin_port = htons(val);
    } else if (!av_strcasecmp(cmd, "HTTPBindAddress") || !av_strcasecmp(cmd, "BindAddress")) {
        if (!av_strcasecmp(cmd, "BindAddress"))
            WARNING("BindAddress option is deprecated, use HTTPBindAddress instead\n");
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&config->http_addr.sin_addr, arg) != 0)
            ERROR("%s:%d: Invalid host/IP address: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "NoDaemon")) {
        WARNING("NoDaemon option has no effect, you should remove it\n");
    } else if (!av_strcasecmp(cmd, "RTSPPort")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        val = atoi(arg);
        if (val < 1 || val > 65536)
            ERROR("%s:%d: Invalid port: %s\n", arg);
        config->rtsp_addr.sin_port = htons(atoi(arg));
    } else if (!av_strcasecmp(cmd, "RTSPBindAddress")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&config->rtsp_addr.sin_addr, arg) != 0)
            ERROR("Invalid host/IP address: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "MaxHTTPConnections")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        val = atoi(arg);
        if (val < 1 || val > 65536)
            ERROR("Invalid MaxHTTPConnections: %s\n", arg);
        config->nb_max_http_connections = val;
    } else if (!av_strcasecmp(cmd, "MaxClients")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        val = atoi(arg);
        if (val < 1 || val > config->nb_max_http_connections)
            ERROR("Invalid MaxClients: %s\n", arg);
        else
            config->nb_max_connections = val;
    } else if (!av_strcasecmp(cmd, "MaxBandwidth")) {
        int64_t llval;
        ffserver_get_arg(arg, sizeof(arg), p);
        llval = strtoll(arg, NULL, 10);
        if (llval < 10 || llval > 10000000)
            ERROR("Invalid MaxBandwidth: %s\n", arg);
        else
            config->max_bandwidth = llval;
    } else if (!av_strcasecmp(cmd, "CustomLog")) {
        if (!config->debug)
            ffserver_get_arg(config->logfilename, sizeof(config->logfilename), p);
    } else if (!av_strcasecmp(cmd, "LoadModule")) {
        ERROR("Loadable modules no longer supported\n");
    } else
        ERROR("Incorrect keyword: '%s'\n", cmd);
    return 0;
}

static int ffserver_parse_config_feed(FFServerConfig *config, const char *cmd, const char **p,
                                      int line_num, FFServerStream **pfeed)
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

        feed->child_argv = av_mallocz(64 * sizeof(char *));
        if (!feed->child_argv)
            return AVERROR(ENOMEM);
        for (i = 0; i < 62; i++) {
            ffserver_get_arg(arg, sizeof(arg), p);
            if (!arg[0])
                break;

            feed->child_argv[i] = av_strdup(arg);
            if (!feed->child_argv[i])
                return AVERROR(ENOMEM);
        }

        feed->child_argv[i] =
            av_asprintf("http://%s:%d/%s",
                        (config->http_addr.sin_addr.s_addr == INADDR_ANY) ? "127.0.0.1" :
                        inet_ntoa(config->http_addr.sin_addr), ntohs(config->http_addr.sin_port),
                        feed->filename);
        if (!feed->child_argv[i])
            return AVERROR(ENOMEM);
    } else if (!av_strcasecmp(cmd, "ACL")) {
        ffserver_parse_acl_row(NULL, feed, NULL, *p, config->filename, line_num);
    } else if (!av_strcasecmp(cmd, "File") || !av_strcasecmp(cmd, "ReadOnlyFile")) {
        ffserver_get_arg(feed->feed_filename, sizeof(feed->feed_filename), p);
        feed->readonly = !av_strcasecmp(cmd, "ReadOnlyFile");
    } else if (!av_strcasecmp(cmd, "Truncate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        /* assume Truncate is true in case no argument is specified */
        if (!arg[0]) {
            feed->truncate = 1;
        } else {
            WARNING("Truncate N syntax in configuration file is deprecated, "
                    "use Truncate alone with no arguments\n");
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
        }
        feed->feed_max_size = (int64_t)fsize;
        if (feed->feed_max_size < FFM_PACKET_SIZE*4)
            ERROR("Feed max file size is too small, must be at least %d\n", FFM_PACKET_SIZE*4);
    } else if (!av_strcasecmp(cmd, "</Feed>")) {
        *pfeed = NULL;
    } else {
        ERROR("Invalid entry '%s' inside <Feed></Feed>\n", cmd);
    }
    return 0;
}

static int ffserver_parse_config_stream(FFServerConfig *config, const char *cmd, const char **p,
                                        int line_num, FFServerStream **pstream)
{
    char arg[1024], arg2[1024];
    FFServerStream *stream;

    av_assert0(pstream);
    stream = *pstream;

    if (!av_strcasecmp(cmd, "<Stream")) {
        char *q;
        FFServerStream *s;
        stream = av_mallocz(sizeof(FFServerStream));
        if (!stream)
            return AVERROR(ENOMEM);
        ffserver_get_arg(stream->filename, sizeof(stream->filename), p);
        q = strrchr(stream->filename, '>');
        if (q)
            *q = '\0';

        for (s = config->first_stream; s; s = s->next) {
            if (!strcmp(stream->filename, s->filename))
                ERROR("Stream '%s' already registered\n", s->filename);
        }

        stream->fmt = ffserver_guess_format(NULL, stream->filename, NULL);
        avcodec_get_context_defaults3(&config->video_enc, NULL);
        avcodec_get_context_defaults3(&config->audio_enc, NULL);

        config->audio_id = AV_CODEC_ID_NONE;
        config->video_id = AV_CODEC_ID_NONE;
        if (stream->fmt) {
            config->audio_id = stream->fmt->audio_codec;
            config->video_id = stream->fmt->video_codec;
        }
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
            ERROR("Feed with name '%s' for stream '%s' is not defined\n", arg, stream->filename);
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
            if (!strcmp(arg, "jpeg"))
                strcpy(arg, "mjpeg");
            stream->fmt = ffserver_guess_format(arg, NULL, NULL);
            if (!stream->fmt)
                ERROR("Unknown Format: %s\n", arg);
        }
        if (stream->fmt) {
            config->audio_id = stream->fmt->audio_codec;
            config->video_id = stream->fmt->video_codec;
        }
    } else if (!av_strcasecmp(cmd, "InputFormat")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->ifmt = av_find_input_format(arg);
        if (!stream->ifmt)
            ERROR("Unknown input format: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "FaviconURL")) {
        if (stream->stream_type == STREAM_TYPE_STATUS)
            ffserver_get_arg(stream->feed_filename, sizeof(stream->feed_filename), p);
        else
            ERROR("FaviconURL only permitted for status streams\n");
    } else if (!av_strcasecmp(cmd, "Author")    ||
               !av_strcasecmp(cmd, "Comment")   ||
               !av_strcasecmp(cmd, "Copyright") ||
               !av_strcasecmp(cmd, "Title")) {
        char key[32];
        int i, ret;
        ffserver_get_arg(arg, sizeof(arg), p);
        for (i = 0; i < strlen(cmd); i++)
            key[i] = av_tolower(cmd[i]);
        key[i] = 0;
        WARNING("'%s' option in configuration file is deprecated, "
                "use 'Metadata %s VALUE' instead\n", cmd, key);
        if ((ret = av_dict_set(&stream->metadata, key, arg, 0)) < 0)
            ERROR("Could not set metadata '%s' to value '%s': %s\n", key, arg, av_err2str(ret));
    } else if (!av_strcasecmp(cmd, "Metadata")) {
        int ret;
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_get_arg(arg2, sizeof(arg2), p);
        if ((ret = av_dict_set(&stream->metadata, arg, arg2, 0)) < 0) {
            ERROR("Could not set metadata '%s' to value '%s': %s\n",
                  arg, arg2, av_err2str(ret));
        }
    } else if (!av_strcasecmp(cmd, "Preroll")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->prebuffer = atof(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "StartSendOnKey")) {
        stream->send_on_key = 1;
    } else if (!av_strcasecmp(cmd, "AudioCodec")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->audio_id = opt_codec(arg, AVMEDIA_TYPE_AUDIO);
        if (config->audio_id == AV_CODEC_ID_NONE)
            ERROR("Unknown AudioCodec: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "VideoCodec")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_id = opt_codec(arg, AVMEDIA_TYPE_VIDEO);
        if (config->video_id == AV_CODEC_ID_NONE)
            ERROR("Unknown VideoCodec: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "MaxTime")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->max_time = atof(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "AudioBitRate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->audio_enc.bit_rate = lrintf(atof(arg) * 1000);
    } else if (!av_strcasecmp(cmd, "AudioChannels")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->audio_enc.channels = atoi(arg);
    } else if (!av_strcasecmp(cmd, "AudioSampleRate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->audio_enc.sample_rate = atoi(arg);
    } else if (!av_strcasecmp(cmd, "VideoBitRateRange")) {
        int minrate, maxrate;
        ffserver_get_arg(arg, sizeof(arg), p);
        if (sscanf(arg, "%d-%d", &minrate, &maxrate) == 2) {
            config->video_enc.rc_min_rate = minrate * 1000;
            config->video_enc.rc_max_rate = maxrate * 1000;
        } else
            ERROR("Incorrect format for VideoBitRateRange -- should be <min>-<max>: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "Debug")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.debug = strtol(arg,0,0);
    } else if (!av_strcasecmp(cmd, "Strict")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.strict_std_compliance = atoi(arg);
    } else if (!av_strcasecmp(cmd, "VideoBufferSize")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.rc_buffer_size = atoi(arg) * 8*1024;
    } else if (!av_strcasecmp(cmd, "VideoBitRateTolerance")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.bit_rate_tolerance = atoi(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "VideoBitRate")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.bit_rate = atoi(arg) * 1000;
    } else if (!av_strcasecmp(cmd, "VideoSize")) {
        int ret;
        ffserver_get_arg(arg, sizeof(arg), p);
        ret = av_parse_video_size(&config->video_enc.width, &config->video_enc.height, arg);
        if (ret < 0)
            ERROR("Invalid video size '%s'\n", arg);
        else if ((config->video_enc.width % 16) != 0 || (config->video_enc.height % 16) != 0)
            ERROR("Image size must be a multiple of 16\n");
    } else if (!av_strcasecmp(cmd, "VideoFrameRate")) {
        AVRational frame_rate;
        ffserver_get_arg(arg, sizeof(arg), p);
        if (av_parse_video_rate(&frame_rate, arg) < 0) {
            ERROR("Incorrect frame rate: %s\n", arg);
        } else {
            config->video_enc.time_base.num = frame_rate.den;
            config->video_enc.time_base.den = frame_rate.num;
        }
    } else if (!av_strcasecmp(cmd, "PixelFormat")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.pix_fmt = av_get_pix_fmt(arg);
        if (config->video_enc.pix_fmt == AV_PIX_FMT_NONE)
            ERROR("Unknown pixel format: %s\n", arg);
    } else if (!av_strcasecmp(cmd, "VideoGopSize")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.gop_size = atoi(arg);
    } else if (!av_strcasecmp(cmd, "VideoIntraOnly")) {
        config->video_enc.gop_size = 1;
    } else if (!av_strcasecmp(cmd, "VideoHighQuality")) {
        config->video_enc.mb_decision = FF_MB_DECISION_BITS;
    } else if (!av_strcasecmp(cmd, "Video4MotionVector")) {
        config->video_enc.mb_decision = FF_MB_DECISION_BITS; //FIXME remove
        config->video_enc.flags |= CODEC_FLAG_4MV;
    } else if (!av_strcasecmp(cmd, "AVOptionVideo") ||
               !av_strcasecmp(cmd, "AVOptionAudio")) {
        AVCodecContext *avctx;
        int type;
        ffserver_get_arg(arg, sizeof(arg), p);
        ffserver_get_arg(arg2, sizeof(arg2), p);
        if (!av_strcasecmp(cmd, "AVOptionVideo")) {
            avctx = &config->video_enc;
            type = AV_OPT_FLAG_VIDEO_PARAM;
        } else {
            avctx = &config->audio_enc;
            type = AV_OPT_FLAG_AUDIO_PARAM;
        }
        if (ffserver_opt_default(arg, arg2, avctx, type|AV_OPT_FLAG_ENCODING_PARAM)) {
            ERROR("Error setting %s option to %s %s\n", cmd, arg, arg2);
        }
    } else if (!av_strcasecmp(cmd, "AVPresetVideo") ||
               !av_strcasecmp(cmd, "AVPresetAudio")) {
        AVCodecContext *avctx;
        int type;
        ffserver_get_arg(arg, sizeof(arg), p);
        if (!av_strcasecmp(cmd, "AVPresetVideo")) {
            avctx = &config->video_enc;
            config->video_enc.codec_id = config->video_id;
            type = AV_OPT_FLAG_VIDEO_PARAM;
        } else {
            avctx = &config->audio_enc;
            config->audio_enc.codec_id = config->audio_id;
            type = AV_OPT_FLAG_AUDIO_PARAM;
        }
        if (ffserver_opt_preset(arg, avctx, type|AV_OPT_FLAG_ENCODING_PARAM, &config->audio_id, &config->video_id)) {
            ERROR("AVPreset error: %s\n", arg);
        }
    } else if (!av_strcasecmp(cmd, "VideoTag")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (strlen(arg) == 4)
            config->video_enc.codec_tag = MKTAG(arg[0], arg[1], arg[2], arg[3]);
    } else if (!av_strcasecmp(cmd, "BitExact")) {
        config->video_enc.flags |= CODEC_FLAG_BITEXACT;
    } else if (!av_strcasecmp(cmd, "DctFastint")) {
        config->video_enc.dct_algo  = FF_DCT_FASTINT;
    } else if (!av_strcasecmp(cmd, "IdctSimple")) {
        config->video_enc.idct_algo = FF_IDCT_SIMPLE;
    } else if (!av_strcasecmp(cmd, "Qscale")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.flags |= CODEC_FLAG_QSCALE;
        config->video_enc.global_quality = FF_QP2LAMBDA * atoi(arg);
    } else if (!av_strcasecmp(cmd, "VideoQDiff")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.max_qdiff = atoi(arg);
        if (config->video_enc.max_qdiff < 1 || config->video_enc.max_qdiff > 31)
            ERROR("VideoQDiff out of range\n");
    } else if (!av_strcasecmp(cmd, "VideoQMax")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.qmax = atoi(arg);
        if (config->video_enc.qmax < 1 || config->video_enc.qmax > 31)
            ERROR("VideoQMax out of range\n");
    } else if (!av_strcasecmp(cmd, "VideoQMin")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.qmin = atoi(arg);
        if (config->video_enc.qmin < 1 || config->video_enc.qmin > 31)
            ERROR("VideoQMin out of range\n");
    } else if (!av_strcasecmp(cmd, "LumiMask")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.lumi_masking = atof(arg);
    } else if (!av_strcasecmp(cmd, "DarkMask")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        config->video_enc.dark_masking = atof(arg);
    } else if (!av_strcasecmp(cmd, "NoVideo")) {
        config->video_id = AV_CODEC_ID_NONE;
    } else if (!av_strcasecmp(cmd, "NoAudio")) {
        config->audio_id = AV_CODEC_ID_NONE;
    } else if (!av_strcasecmp(cmd, "ACL")) {
        ffserver_parse_acl_row(stream, NULL, NULL, *p, config->filename, line_num);
    } else if (!av_strcasecmp(cmd, "DynamicACL")) {
        ffserver_get_arg(stream->dynamic_acl, sizeof(stream->dynamic_acl), p);
    } else if (!av_strcasecmp(cmd, "RTSPOption")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        av_freep(&stream->rtsp_option);
        stream->rtsp_option = av_strdup(arg);
    } else if (!av_strcasecmp(cmd, "MulticastAddress")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        if (resolve_host(&stream->multicast_ip, arg) != 0)
            ERROR("Invalid host/IP address: %s\n", arg);
        stream->is_multicast = 1;
        stream->loop = 1; /* default is looping */
    } else if (!av_strcasecmp(cmd, "MulticastPort")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->multicast_port = atoi(arg);
    } else if (!av_strcasecmp(cmd, "MulticastTTL")) {
        ffserver_get_arg(arg, sizeof(arg), p);
        stream->multicast_ttl = atoi(arg);
    } else if (!av_strcasecmp(cmd, "NoLoop")) {
        stream->loop = 0;
    } else if (!av_strcasecmp(cmd, "</Stream>")) {
        if (stream->feed && stream->fmt && strcmp(stream->fmt->name, "ffm") != 0) {
            if (config->audio_id != AV_CODEC_ID_NONE) {
                config->audio_enc.codec_type = AVMEDIA_TYPE_AUDIO;
                config->audio_enc.codec_id = config->audio_id;
                add_codec(stream, &config->audio_enc);
            }
            if (config->video_id != AV_CODEC_ID_NONE) {
                config->video_enc.codec_type = AVMEDIA_TYPE_VIDEO;
                config->video_enc.codec_id = config->video_id;
                add_codec(stream, &config->video_enc);
            }
        }
        *pstream = NULL;
    } else if (!av_strcasecmp(cmd, "File") || !av_strcasecmp(cmd, "ReadOnlyFile")) {
        ffserver_get_arg(stream->feed_filename, sizeof(stream->feed_filename), p);
    } else {
        ERROR("Invalid entry '%s' inside <Stream></Stream>\n", cmd);
    }
    return 0;
}

static int ffserver_parse_config_redirect(FFServerConfig *config, const char *cmd, const char **p,
                                          int line_num, FFServerStream **predirect)
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
        ffserver_get_arg(redirect->feed_filename, sizeof(redirect->feed_filename), p);
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
    int line_num = 0;
    FFServerStream **last_stream, *stream = NULL, *redirect = NULL;
    FFServerStream **last_feed, *feed = NULL;
    int ret = 0;

    av_assert0(config);

    f = fopen(filename, "r");
    if (!f) {
        ret = AVERROR(errno);
        av_log(NULL, AV_LOG_ERROR, "Could not open the configuration file '%s'\n", filename);
        return ret;
    }

    config->first_stream = NULL;
    last_stream = &config->first_stream;
    config->first_feed = NULL;
    last_feed = &config->first_feed;
    config->errors = config->warnings = 0;

    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
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
                if ((ret = ffserver_parse_config_feed(config, cmd, &p, line_num, &feed)) < 0)
                    break;
                if (opening) {
                    /* add in stream list */
                    *last_stream = feed;
                    last_stream = &feed->next;
                    /* add in feed list */
                    *last_feed = feed;
                    last_feed = &feed->next_feed;
                }
            }
        } else if (stream || !av_strcasecmp(cmd, "<Stream")) {
            int opening = !av_strcasecmp(cmd, "<Stream");
            if (opening && (stream || feed || redirect)) {
                ERROR("Already in a tag\n");
            } else {
                if ((ret = ffserver_parse_config_stream(config, cmd, &p, line_num, &stream)) < 0)
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
                if ((ret = ffserver_parse_config_redirect(config, cmd, &p, line_num, &redirect)) < 0)
                    break;
                if (opening) {
                    /* add in stream list */
                    *last_stream = redirect;
                    last_stream = &redirect->next;
                }
            }
        } else {
            ffserver_parse_config_global(config, cmd, &p, line_num);
        }
    }

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
