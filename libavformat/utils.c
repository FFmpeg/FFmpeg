/*
 * Various utilities for ffmpeg system
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
#include "avformat.h"
#include "allformats.h"
#include "opt.h"

#undef NDEBUG
#include <assert.h>

/**
 * @file libavformat/utils.c
 * Various utility functions for using ffmpeg library.
 */

static void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den);
static void av_frac_add(AVFrac *f, int64_t incr);

/** head of registered input format linked list. */
AVInputFormat *first_iformat = NULL;
/** head of registered output format linked list. */
AVOutputFormat *first_oformat = NULL;

void av_register_input_format(AVInputFormat *format)
{
    AVInputFormat **p;
    p = &first_iformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

void av_register_output_format(AVOutputFormat *format)
{
    AVOutputFormat **p;
    p = &first_oformat;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

int match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;

    if(!filename)
        return 0;

    ext = strrchr(filename, '.');
    if (ext) {
        ext++;
        p = extensions;
        for(;;) {
            q = ext1;
            while (*p != '\0' && *p != ',' && q-ext1<sizeof(ext1)-1)
                *q++ = *p++;
            *q = '\0';
            if (!strcasecmp(ext1, ext))
                return 1;
            if (*p == '\0')
                break;
            p++;
        }
    }
    return 0;
}

AVOutputFormat *guess_format(const char *short_name, const char *filename,
                             const char *mime_type)
{
    AVOutputFormat *fmt, *fmt_found;
    int score_max, score;

    /* specific test for image sequences */
#ifdef CONFIG_IMAGE2_MUXER
    if (!short_name && filename &&
        av_filename_number_test(filename) &&
        av_guess_image2_codec(filename) != CODEC_ID_NONE) {
        return guess_format("image2", NULL, NULL);
    }
#endif
    /* find the proper file type */
    fmt_found = NULL;
    score_max = 0;
    fmt = first_oformat;
    while (fmt != NULL) {
        score = 0;
        if (fmt->name && short_name && !strcmp(fmt->name, short_name))
            score += 100;
        if (fmt->mime_type && mime_type && !strcmp(fmt->mime_type, mime_type))
            score += 10;
        if (filename && fmt->extensions &&
            match_ext(filename, fmt->extensions)) {
            score += 5;
        }
        if (score > score_max) {
            score_max = score;
            fmt_found = fmt;
        }
        fmt = fmt->next;
    }
    return fmt_found;
}

AVOutputFormat *guess_stream_format(const char *short_name, const char *filename,
                             const char *mime_type)
{
    AVOutputFormat *fmt = guess_format(short_name, filename, mime_type);

    if (fmt) {
        AVOutputFormat *stream_fmt;
        char stream_format_name[64];

        snprintf(stream_format_name, sizeof(stream_format_name), "%s_stream", fmt->name);
        stream_fmt = guess_format(stream_format_name, NULL, NULL);

        if (stream_fmt)
            fmt = stream_fmt;
    }

    return fmt;
}

enum CodecID av_guess_codec(AVOutputFormat *fmt, const char *short_name,
                            const char *filename, const char *mime_type, enum CodecType type){
    if(type == CODEC_TYPE_VIDEO){
        enum CodecID codec_id= CODEC_ID_NONE;

#ifdef CONFIG_IMAGE2_MUXER
        if(!strcmp(fmt->name, "image2") || !strcmp(fmt->name, "image2pipe")){
            codec_id= av_guess_image2_codec(filename);
        }
#endif
        if(codec_id == CODEC_ID_NONE)
            codec_id= fmt->video_codec;
        return codec_id;
    }else if(type == CODEC_TYPE_AUDIO)
        return fmt->audio_codec;
    else
        return CODEC_ID_NONE;
}

AVInputFormat *av_find_input_format(const char *short_name)
{
    AVInputFormat *fmt;
    for(fmt = first_iformat; fmt != NULL; fmt = fmt->next) {
        if (!strcmp(fmt->name, short_name))
            return fmt;
    }
    return NULL;
}

/* memory handling */

void av_destruct_packet(AVPacket *pkt)
{
    av_free(pkt->data);
    pkt->data = NULL; pkt->size = 0;
}

int av_new_packet(AVPacket *pkt, int size)
{
    uint8_t *data;
    if((unsigned)size > (unsigned)size + FF_INPUT_BUFFER_PADDING_SIZE)
        return AVERROR_NOMEM;
    data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return AVERROR_NOMEM;
    memset(data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    av_init_packet(pkt);
    pkt->data = data;
    pkt->size = size;
    pkt->destruct = av_destruct_packet;
    return 0;
}

int av_get_packet(ByteIOContext *s, AVPacket *pkt, int size)
{
    int ret= av_new_packet(pkt, size);

    if(ret<0)
        return ret;

    pkt->pos= url_ftell(s);

    ret= get_buffer(s, pkt->data, size);
    if(ret<=0)
        av_free_packet(pkt);
    else
        pkt->size= ret;

    return ret;
}

int av_dup_packet(AVPacket *pkt)
{
    if (pkt->destruct != av_destruct_packet) {
        uint8_t *data;
        /* we duplicate the packet and don't forget to put the padding
           again */
        if((unsigned)pkt->size > (unsigned)pkt->size + FF_INPUT_BUFFER_PADDING_SIZE)
            return AVERROR_NOMEM;
        data = av_malloc(pkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!data) {
            return AVERROR_NOMEM;
        }
        memcpy(data, pkt->data, pkt->size);
        memset(data + pkt->size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
        pkt->data = data;
        pkt->destruct = av_destruct_packet;
    }
    return 0;
}

int av_filename_number_test(const char *filename)
{
    char buf[1024];
    return filename && (av_get_frame_filename(buf, sizeof(buf), filename, 1)>=0);
}

static AVInputFormat *av_probe_input_format2(AVProbeData *pd, int is_opened, int *score_max)
{
    AVInputFormat *fmt1, *fmt;
    int score;

    fmt = NULL;
    for(fmt1 = first_iformat; fmt1 != NULL; fmt1 = fmt1->next) {
        if (!is_opened == !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(pd);
        } else if (fmt1->extensions) {
            if (match_ext(pd->filename, fmt1->extensions)) {
                score = 50;
            }
        }
        if (score > *score_max) {
            *score_max = score;
            fmt = fmt1;
        }
    }
    return fmt;
}

AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened){
    int score=0;
    return av_probe_input_format2(pd, is_opened, &score);
}

/************************************************************/
/* input media file */

/**
 * Open a media file from an IO stream. 'fmt' must be specified.
 */
static const char* format_to_name(void* ptr)
{
    AVFormatContext* fc = (AVFormatContext*) ptr;
    if(fc->iformat) return fc->iformat->name;
    else if(fc->oformat) return fc->oformat->name;
    else return "NULL";
}

#define OFFSET(x) offsetof(AVFormatContext,x)
#define DEFAULT 0 //should be NAN but it doesnt work as its not a constant in glibc as required by ANSI/ISO C
//these names are too long to be readable
#define E AV_OPT_FLAG_ENCODING_PARAM
#define D AV_OPT_FLAG_DECODING_PARAM

static const AVOption options[]={
{"probesize", NULL, OFFSET(probesize), FF_OPT_TYPE_INT, 32000, 32, INT_MAX, D}, /* 32000 from mpegts.c: 1.0 second at 24Mbit/s */
{"muxrate", "set mux rate", OFFSET(mux_rate), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"packetsize", "set packet size", OFFSET(packet_size), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"fflags", NULL, OFFSET(flags), FF_OPT_TYPE_FLAGS, DEFAULT, INT_MIN, INT_MAX, D|E, "fflags"},
{"ignidx", "ignore index", 0, FF_OPT_TYPE_CONST, AVFMT_FLAG_IGNIDX, INT_MIN, INT_MAX, D, "fflags"},
{"genpts", "generate pts", 0, FF_OPT_TYPE_CONST, AVFMT_FLAG_GENPTS, INT_MIN, INT_MAX, D, "fflags"},
{"track", " set the track number", OFFSET(track), FF_OPT_TYPE_INT, DEFAULT, 0, INT_MAX, E},
{"year", "set the year", OFFSET(year), FF_OPT_TYPE_INT, DEFAULT, INT_MIN, INT_MAX, E},
{"analyzeduration", NULL, OFFSET(max_analyze_duration), FF_OPT_TYPE_INT, 3*AV_TIME_BASE, 0, INT_MAX, D},
{NULL},
};

#undef E
#undef D
#undef DEFAULT

static const AVClass av_format_context_class = { "AVFormatContext", format_to_name, options };

static void avformat_get_context_defaults(AVFormatContext *s)
{
    memset(s, 0, sizeof(AVFormatContext));

    s->av_class = &av_format_context_class;

    av_opt_set_defaults(s);
}

AVFormatContext *av_alloc_format_context(void)
{
    AVFormatContext *ic;
    ic = av_malloc(sizeof(AVFormatContext));
    if (!ic) return ic;
    avformat_get_context_defaults(ic);
    ic->av_class = &av_format_context_class;
    return ic;
}

int av_open_input_stream(AVFormatContext **ic_ptr,
                         ByteIOContext *pb, const char *filename,
                         AVInputFormat *fmt, AVFormatParameters *ap)
{
    int err;
    AVFormatContext *ic;
    AVFormatParameters default_ap;

    if(!ap){
        ap=&default_ap;
        memset(ap, 0, sizeof(default_ap));
    }

    if(!ap->prealloced_context)
        ic = av_alloc_format_context();
    else
        ic = *ic_ptr;
    if (!ic) {
        err = AVERROR_NOMEM;
        goto fail;
    }
    ic->iformat = fmt;
    if (pb)
        ic->pb = *pb;
    ic->duration = AV_NOPTS_VALUE;
    ic->start_time = AV_NOPTS_VALUE;
    pstrcpy(ic->filename, sizeof(ic->filename), filename);

    /* allocate private data */
    if (fmt->priv_data_size > 0) {
        ic->priv_data = av_mallocz(fmt->priv_data_size);
        if (!ic->priv_data) {
            err = AVERROR_NOMEM;
            goto fail;
        }
    } else {
        ic->priv_data = NULL;
    }

    err = ic->iformat->read_header(ic, ap);
    if (err < 0)
        goto fail;

    if (pb && !ic->data_offset)
        ic->data_offset = url_ftell(&ic->pb);

    *ic_ptr = ic;
    return 0;
 fail:
    if (ic) {
        av_freep(&ic->priv_data);
    }
    av_free(ic);
    *ic_ptr = NULL;
    return err;
}

/** Size of probe buffer, for guessing file type from file contents. */
#define PROBE_BUF_MIN 2048
#define PROBE_BUF_MAX (1<<20)

int av_open_input_file(AVFormatContext **ic_ptr, const char *filename,
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap)
{
    int err, must_open_file, file_opened, probe_size;
    AVProbeData probe_data, *pd = &probe_data;
    ByteIOContext pb1, *pb = &pb1;

    file_opened = 0;
    pd->filename = "";
    if (filename)
        pd->filename = filename;
    pd->buf = NULL;
    pd->buf_size = 0;

    if (!fmt) {
        /* guess format if no file can be opened  */
        fmt = av_probe_input_format(pd, 0);
    }

    /* do not open file if the format does not need it. XXX: specific
       hack needed to handle RTSP/TCP */
    must_open_file = 1;
    if (fmt && (fmt->flags & AVFMT_NOFILE)) {
        must_open_file = 0;
        pb= NULL; //FIXME this or memset(pb, 0, sizeof(ByteIOContext)); otherwise its uninitalized
    }

    if (!fmt || must_open_file) {
        /* if no file needed do not try to open one */
        if (url_fopen(pb, filename, URL_RDONLY) < 0) {
            err = AVERROR_IO;
            goto fail;
        }
        file_opened = 1;
        if (buf_size > 0) {
            url_setbufsize(pb, buf_size);
        }

        for(probe_size= PROBE_BUF_MIN; probe_size<=PROBE_BUF_MAX && !fmt; probe_size<<=1){
            int score= probe_size < PROBE_BUF_MAX ? AVPROBE_SCORE_MAX/4 : 0;
            /* read probe data */
            pd->buf= av_realloc(pd->buf, probe_size);
            pd->buf_size = get_buffer(pb, pd->buf, probe_size);
            if (url_fseek(pb, 0, SEEK_SET) < 0) {
                url_fclose(pb);
                if (url_fopen(pb, filename, URL_RDONLY) < 0) {
                    file_opened = 0;
                    err = AVERROR_IO;
                    goto fail;
                }
            }
            /* guess file format */
            fmt = av_probe_input_format2(pd, 1, &score);
        }
        av_freep(&pd->buf);
    }

    /* if still no format found, error */
    if (!fmt) {
        err = AVERROR_NOFMT;
        goto fail;
    }

    /* XXX: suppress this hack for redirectors */
#ifdef CONFIG_NETWORK
    if (fmt == &redir_demuxer) {
        err = redir_open(ic_ptr, pb);
        url_fclose(pb);
        return err;
    }
#endif

    /* check filename in case of an image number is expected */
    if (fmt->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(filename)) {
            err = AVERROR_NUMEXPECTED;
            goto fail;
        }
    }
    err = av_open_input_stream(ic_ptr, pb, filename, fmt, ap);
    if (err)
        goto fail;
    return 0;
 fail:
    av_freep(&pd->buf);
    if (file_opened)
        url_fclose(pb);
    *ic_ptr = NULL;
    return err;

}

/*******************************************************/

int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return s->iformat->read_packet(s, pkt);
}

/**********************************************************/

/**
 * Get the number of samples of an audio frame. Return (-1) if error.
 */
static int get_audio_frame_size(AVCodecContext *enc, int size)
{
    int frame_size;

    if (enc->frame_size <= 1) {
        int bits_per_sample = av_get_bits_per_sample(enc->codec_id);

        if (bits_per_sample) {
            if (enc->channels == 0)
                return -1;
            frame_size = (size << 3) / (bits_per_sample * enc->channels);
        } else {
            /* used for example by ADPCM codecs */
            if (enc->bit_rate == 0)
                return -1;
            frame_size = (size * 8 * enc->sample_rate) / enc->bit_rate;
        }
    } else {
        frame_size = enc->frame_size;
    }
    return frame_size;
}


/**
 * Return the frame duration in seconds, return 0 if not available.
 */
static void compute_frame_duration(int *pnum, int *pden, AVStream *st,
                                   AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;

    *pnum = 0;
    *pden = 0;
    switch(st->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
        if(st->time_base.num*1000LL > st->time_base.den){
            *pnum = st->time_base.num;
            *pden = st->time_base.den;
        }else if(st->codec->time_base.num*1000LL > st->codec->time_base.den){
            *pnum = st->codec->time_base.num;
            *pden = st->codec->time_base.den;
            if (pc && pc->repeat_pict) {
                *pden *= 2;
                *pnum = (*pnum) * (2 + pc->repeat_pict);
            }
        }
        break;
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(st->codec, pkt->size);
        if (frame_size < 0)
            break;
        *pnum = frame_size;
        *pden = st->codec->sample_rate;
        break;
    default:
        break;
    }
}

static int is_intra_only(AVCodecContext *enc){
    if(enc->codec_type == CODEC_TYPE_AUDIO){
        return 1;
    }else if(enc->codec_type == CODEC_TYPE_VIDEO){
        switch(enc->codec_id){
        case CODEC_ID_MJPEG:
        case CODEC_ID_MJPEGB:
        case CODEC_ID_LJPEG:
        case CODEC_ID_RAWVIDEO:
        case CODEC_ID_DVVIDEO:
        case CODEC_ID_HUFFYUV:
        case CODEC_ID_FFVHUFF:
        case CODEC_ID_ASV1:
        case CODEC_ID_ASV2:
        case CODEC_ID_VCR1:
            return 1;
        default: break;
        }
    }
    return 0;
}

static int64_t lsb2full(int64_t lsb, int64_t last_ts, int lsb_bits){
    int64_t mask = lsb_bits < 64 ? (1LL<<lsb_bits)-1 : -1LL;
    int64_t delta= last_ts - mask/2;
    return  ((lsb - delta)&mask) + delta;
}

static void compute_pkt_fields(AVFormatContext *s, AVStream *st,
                               AVCodecParserContext *pc, AVPacket *pkt)
{
    int num, den, presentation_delayed, delay, i;
    /* handle wrapping */
    if(st->cur_dts != AV_NOPTS_VALUE){
        if(pkt->pts != AV_NOPTS_VALUE)
            pkt->pts= lsb2full(pkt->pts, st->cur_dts, st->pts_wrap_bits);
        if(pkt->dts != AV_NOPTS_VALUE)
            pkt->dts= lsb2full(pkt->dts, st->cur_dts, st->pts_wrap_bits);
    }

    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, pc, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num);
        }
    }

    if(is_intra_only(st->codec))
        pkt->flags |= PKT_FLAG_KEY;

    /* do we have a video B frame ? */
    delay= st->codec->has_b_frames;
    presentation_delayed = 0;
    /* XXX: need has_b_frame, but cannot get it if the codec is
        not initialized */
    if (delay &&
        pc && pc->pict_type != FF_B_TYPE)
        presentation_delayed = 1;
    /* this may be redundant, but it shouldnt hurt */
    if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts > pkt->dts)
        presentation_delayed = 1;

    if(st->cur_dts == AV_NOPTS_VALUE){
        st->cur_dts = -delay * pkt->duration;
    }

//    av_log(NULL, AV_LOG_DEBUG, "IN delayed:%d pts:%"PRId64", dts:%"PRId64" cur_dts:%"PRId64" st:%d pc:%p\n", presentation_delayed, pkt->pts, pkt->dts, st->cur_dts, pkt->stream_index, pc);
    /* interpolate PTS and DTS if they are not present */
    if(delay <=1){
        if (presentation_delayed) {
            /* DTS = decompression time stamp */
            /* PTS = presentation time stamp */
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->last_IP_pts;
            if (pkt->dts == AV_NOPTS_VALUE)
                pkt->dts = st->cur_dts;

            /* this is tricky: the dts must be incremented by the duration
            of the frame we are displaying, i.e. the last I or P frame */
            if (st->last_IP_duration == 0)
                st->last_IP_duration = pkt->duration;
            st->cur_dts = pkt->dts + st->last_IP_duration;
            st->last_IP_duration  = pkt->duration;
            st->last_IP_pts= pkt->pts;
            /* cannot compute PTS if not present (we can compute it only
            by knowing the futur */
        } else if(pkt->pts != AV_NOPTS_VALUE || pkt->dts != AV_NOPTS_VALUE || pkt->duration){
            if(pkt->pts != AV_NOPTS_VALUE && pkt->duration){
                int64_t old_diff= FFABS(st->cur_dts - pkt->duration - pkt->pts);
                int64_t new_diff= FFABS(st->cur_dts - pkt->pts);
                if(old_diff < new_diff && old_diff < (pkt->duration>>3)){
                    pkt->pts += pkt->duration;
    //                av_log(NULL, AV_LOG_DEBUG, "id:%d old:%"PRId64" new:%"PRId64" dur:%d cur:%"PRId64" size:%d\n", pkt->stream_index, old_diff, new_diff, pkt->duration, st->cur_dts, pkt->size);
                }
            }

            /* presentation is not delayed : PTS and DTS are the same */
            if(pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = pkt->dts;
            if(pkt->pts == AV_NOPTS_VALUE)
                pkt->pts = st->cur_dts;
            pkt->dts = pkt->pts;
            st->cur_dts = pkt->pts + pkt->duration;
        }
    }

    if(pkt->pts != AV_NOPTS_VALUE){
        st->pts_buffer[0]= pkt->pts;
        for(i=1; i<delay+1 && st->pts_buffer[i] == AV_NOPTS_VALUE; i++)
            st->pts_buffer[i]= (i-delay-1) * pkt->duration;
        for(i=0; i<delay && st->pts_buffer[i] > st->pts_buffer[i+1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i+1]);
        if(pkt->dts == AV_NOPTS_VALUE)
            pkt->dts= st->pts_buffer[0];
        if(pkt->dts > st->cur_dts)
            st->cur_dts = pkt->dts;
    }

//    av_log(NULL, AV_LOG_ERROR, "OUTdelayed:%d/%d pts:%"PRId64", dts:%"PRId64" cur_dts:%"PRId64"\n", presentation_delayed, delay, pkt->pts, pkt->dts, st->cur_dts);

    /* update flags */
    if (pc) {
        pkt->flags = 0;
        /* key frame computation */
            if (pc->pict_type == FF_I_TYPE)
                pkt->flags |= PKT_FLAG_KEY;
    }
}

void av_destruct_packet_nofree(AVPacket *pkt)
{
    pkt->data = NULL; pkt->size = 0;
}

static int av_read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st;
    int len, ret, i;

    for(;;) {
        /* select current input stream component */
        st = s->cur_st;
        if (st) {
            if (!st->need_parsing || !st->parser) {
                /* no parsing needed: we just output the packet as is */
                /* raw data support */
                *pkt = s->cur_pkt;
                compute_pkt_fields(s, st, NULL, pkt);
                s->cur_st = NULL;
                break;
            } else if (s->cur_len > 0 && st->discard < AVDISCARD_ALL) {
                len = av_parser_parse(st->parser, st->codec, &pkt->data, &pkt->size,
                                      s->cur_ptr, s->cur_len,
                                      s->cur_pkt.pts, s->cur_pkt.dts);
                s->cur_pkt.pts = AV_NOPTS_VALUE;
                s->cur_pkt.dts = AV_NOPTS_VALUE;
                /* increment read pointer */
                s->cur_ptr += len;
                s->cur_len -= len;

                /* return packet if any */
                if (pkt->size) {
                got_packet:
                    pkt->duration = 0;
                    pkt->stream_index = st->index;
                    pkt->pts = st->parser->pts;
                    pkt->dts = st->parser->dts;
                    pkt->destruct = av_destruct_packet_nofree;
                    compute_pkt_fields(s, st, st->parser, pkt);

                    if((s->iformat->flags & AVFMT_GENERIC_INDEX) && pkt->flags & PKT_FLAG_KEY){
                        av_add_index_entry(st, st->parser->frame_offset, pkt->dts,
                                           0, 0, AVINDEX_KEYFRAME);
                    }

                    break;
                }
            } else {
                /* free packet */
                av_free_packet(&s->cur_pkt);
                s->cur_st = NULL;
            }
        } else {
            /* read next packet */
            ret = av_read_packet(s, &s->cur_pkt);
            if (ret < 0) {
                if (ret == AVERROR(EAGAIN))
                    return ret;
                /* return the last frames, if any */
                for(i = 0; i < s->nb_streams; i++) {
                    st = s->streams[i];
                    if (st->parser && st->need_parsing) {
                        av_parser_parse(st->parser, st->codec,
                                        &pkt->data, &pkt->size,
                                        NULL, 0,
                                        AV_NOPTS_VALUE, AV_NOPTS_VALUE);
                        if (pkt->size)
                            goto got_packet;
                    }
                }
                /* no more packets: really terminates parsing */
                return ret;
            }

            st = s->streams[s->cur_pkt.stream_index];
            if(st->codec->debug & FF_DEBUG_PTS)
                av_log(s, AV_LOG_DEBUG, "av_read_packet stream=%d, pts=%"PRId64", dts=%"PRId64", size=%d\n",
                    s->cur_pkt.stream_index,
                    s->cur_pkt.pts,
                    s->cur_pkt.dts,
                    s->cur_pkt.size);

            s->cur_st = st;
            s->cur_ptr = s->cur_pkt.data;
            s->cur_len = s->cur_pkt.size;
            if (st->need_parsing && !st->parser) {
                st->parser = av_parser_init(st->codec->codec_id);
                if (!st->parser) {
                    /* no parser available : just output the raw packets */
                    st->need_parsing = 0;
                }else if(st->need_parsing == 2){
                    st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
                }
                if(st->parser && (s->iformat->flags & AVFMT_GENERIC_INDEX)){
                    st->parser->last_frame_offset=
                    st->parser->cur_offset= s->cur_pkt.pos;
                }
            }
        }
    }
    if(st->codec->debug & FF_DEBUG_PTS)
        av_log(s, AV_LOG_DEBUG, "av_read_frame_internal stream=%d, pts=%"PRId64", dts=%"PRId64", size=%d\n",
            pkt->stream_index,
            pkt->pts,
            pkt->dts,
            pkt->size);

    return 0;
}

int av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl;
    int eof=0;
    const int genpts= s->flags & AVFMT_FLAG_GENPTS;

    for(;;){
        pktl = s->packet_buffer;
        if (pktl) {
            AVPacket *next_pkt= &pktl->pkt;

            if(genpts && next_pkt->dts != AV_NOPTS_VALUE){
                while(pktl && next_pkt->pts == AV_NOPTS_VALUE){
                    if(   pktl->pkt.stream_index == next_pkt->stream_index
                       && next_pkt->dts < pktl->pkt.dts
                       && pktl->pkt.pts != pktl->pkt.dts //not b frame
                       /*&& pktl->pkt.dts != AV_NOPTS_VALUE*/){
                        next_pkt->pts= pktl->pkt.dts;
                    }
                    pktl= pktl->next;
                }
                pktl = s->packet_buffer;
            }

            if(   next_pkt->pts != AV_NOPTS_VALUE
               || next_pkt->dts == AV_NOPTS_VALUE
               || !genpts || eof){
                /* read packet from packet buffer, if there is data */
                *pkt = *next_pkt;
                s->packet_buffer = pktl->next;
                av_free(pktl);
                return 0;
            }
        }
        if(genpts){
            AVPacketList **plast_pktl= &s->packet_buffer;
            int ret= av_read_frame_internal(s, pkt);
            if(ret<0){
                if(pktl && ret != AVERROR(EAGAIN)){
                    eof=1;
                    continue;
                }else
                    return ret;
            }

            /* duplicate the packet */
            if (av_dup_packet(pkt) < 0)
                return AVERROR_NOMEM;

            while(*plast_pktl) plast_pktl= &(*plast_pktl)->next; //FIXME maybe maintain pointer to the last?

            pktl = av_mallocz(sizeof(AVPacketList));
            if (!pktl)
                return AVERROR_NOMEM;

            /* add the packet in the buffered packet list */
            *plast_pktl = pktl;
            pktl->pkt= *pkt;
        }else{
            assert(!s->packet_buffer);
            return av_read_frame_internal(s, pkt);
        }
    }
}

/* XXX: suppress the packet queue */
static void flush_packet_queue(AVFormatContext *s)
{
    AVPacketList *pktl;

    for(;;) {
        pktl = s->packet_buffer;
        if (!pktl)
            break;
        s->packet_buffer = pktl->next;
        av_free_packet(&pktl->pkt);
        av_free(pktl);
    }
}

/*******************************************************/
/* seek support */

int av_find_default_stream_index(AVFormatContext *s)
{
    int i;
    AVStream *st;

    if (s->nb_streams <= 0)
        return -1;
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            return i;
        }
    }
    return 0;
}

/**
 * Flush the frame reader.
 */
static void av_read_frame_flush(AVFormatContext *s)
{
    AVStream *st;
    int i;

    flush_packet_queue(s);

    /* free previous packet */
    if (s->cur_st) {
        if (s->cur_st->parser)
            av_free_packet(&s->cur_pkt);
        s->cur_st = NULL;
    }
    /* fail safe */
    s->cur_ptr = NULL;
    s->cur_len = 0;

    /* for each stream, reset read state */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        if (st->parser) {
            av_parser_close(st->parser);
            st->parser = NULL;
        }
        st->last_IP_pts = AV_NOPTS_VALUE;
        st->cur_dts = AV_NOPTS_VALUE; /* we set the current DTS to an unspecified origin */
    }
}

void av_update_cur_dts(AVFormatContext *s, AVStream *ref_st, int64_t timestamp){
    int i;

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];

        st->cur_dts = av_rescale(timestamp,
                                 st->time_base.den * (int64_t)ref_st->time_base.num,
                                 st->time_base.num * (int64_t)ref_st->time_base.den);
    }
}

int av_add_index_entry(AVStream *st,
                            int64_t pos, int64_t timestamp, int size, int distance, int flags)
{
    AVIndexEntry *entries, *ie;
    int index;

    if((unsigned)st->nb_index_entries + 1 >= UINT_MAX / sizeof(AVIndexEntry))
        return -1;

    entries = av_fast_realloc(st->index_entries,
                              &st->index_entries_allocated_size,
                              (st->nb_index_entries + 1) *
                              sizeof(AVIndexEntry));
    if(!entries)
        return -1;

    st->index_entries= entries;

    index= av_index_search_timestamp(st, timestamp, AVSEEK_FLAG_ANY);

    if(index<0){
        index= st->nb_index_entries++;
        ie= &entries[index];
        assert(index==0 || ie[-1].timestamp < timestamp);
    }else{
        ie= &entries[index];
        if(ie->timestamp != timestamp){
            if(ie->timestamp <= timestamp)
                return -1;
            memmove(entries + index + 1, entries + index, sizeof(AVIndexEntry)*(st->nb_index_entries - index));
            st->nb_index_entries++;
        }else if(ie->pos == pos && distance < ie->min_distance) //dont reduce the distance
            distance= ie->min_distance;
    }

    ie->pos = pos;
    ie->timestamp = timestamp;
    ie->min_distance= distance;
    ie->size= size;
    ie->flags = flags;

    return index;
}

/**
 * build an index for raw streams using a parser.
 */
static void av_build_index_raw(AVFormatContext *s)
{
    AVPacket pkt1, *pkt = &pkt1;
    int ret;
    AVStream *st;

    st = s->streams[0];
    av_read_frame_flush(s);
    url_fseek(&s->pb, s->data_offset, SEEK_SET);

    for(;;) {
        ret = av_read_frame(s, pkt);
        if (ret < 0)
            break;
        if (pkt->stream_index == 0 && st->parser &&
            (pkt->flags & PKT_FLAG_KEY)) {
            av_add_index_entry(st, st->parser->frame_offset, pkt->dts,
                            0, 0, AVINDEX_KEYFRAME);
        }
        av_free_packet(pkt);
    }
}

/**
 * Returns TRUE if we deal with a raw stream.
 *
 * Raw codec data and parsing needed.
 */
static int is_raw_stream(AVFormatContext *s)
{
    AVStream *st;

    if (s->nb_streams != 1)
        return 0;
    st = s->streams[0];
    if (!st->need_parsing)
        return 0;
    return 1;
}

int av_index_search_timestamp(AVStream *st, int64_t wanted_timestamp,
                              int flags)
{
    AVIndexEntry *entries= st->index_entries;
    int nb_entries= st->nb_index_entries;
    int a, b, m;
    int64_t timestamp;

    a = - 1;
    b = nb_entries;

    while (b - a > 1) {
        m = (a + b) >> 1;
        timestamp = entries[m].timestamp;
        if(timestamp >= wanted_timestamp)
            b = m;
        if(timestamp <= wanted_timestamp)
            a = m;
    }
    m= (flags & AVSEEK_FLAG_BACKWARD) ? a : b;

    if(!(flags & AVSEEK_FLAG_ANY)){
        while(m>=0 && m<nb_entries && !(entries[m].flags & AVINDEX_KEYFRAME)){
            m += (flags & AVSEEK_FLAG_BACKWARD) ? -1 : 1;
        }
    }

    if(m == nb_entries)
        return -1;
    return  m;
}

#define DEBUG_SEEK

int av_seek_frame_binary(AVFormatContext *s, int stream_index, int64_t target_ts, int flags){
    AVInputFormat *avif= s->iformat;
    int64_t pos_min, pos_max, pos, pos_limit;
    int64_t ts_min, ts_max, ts;
    int index;
    AVStream *st;

    if (stream_index < 0)
        return -1;

#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "read_seek: %d %"PRId64"\n", stream_index, target_ts);
#endif

    ts_max=
    ts_min= AV_NOPTS_VALUE;
    pos_limit= -1; //gcc falsely says it may be uninitalized

    st= s->streams[stream_index];
    if(st->index_entries){
        AVIndexEntry *e;

        index= av_index_search_timestamp(st, target_ts, flags | AVSEEK_FLAG_BACKWARD); //FIXME whole func must be checked for non keyframe entries in index case, especially read_timestamp()
        index= FFMAX(index, 0);
        e= &st->index_entries[index];

        if(e->timestamp <= target_ts || e->pos == e->min_distance){
            pos_min= e->pos;
            ts_min= e->timestamp;
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "using cached pos_min=0x%"PRIx64" dts_min=%"PRId64"\n",
               pos_min,ts_min);
#endif
        }else{
            assert(index==0);
        }

        index= av_index_search_timestamp(st, target_ts, flags & ~AVSEEK_FLAG_BACKWARD);
        assert(index < st->nb_index_entries);
        if(index >= 0){
            e= &st->index_entries[index];
            assert(e->timestamp >= target_ts);
            pos_max= e->pos;
            ts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "using cached pos_max=0x%"PRIx64" pos_limit=0x%"PRIx64" dts_max=%"PRId64"\n",
               pos_max,pos_limit, ts_max);
#endif
        }
    }

    pos= av_gen_search(s, stream_index, target_ts, pos_min, pos_max, pos_limit, ts_min, ts_max, flags, &ts, avif->read_timestamp);
    if(pos<0)
        return -1;

    /* do the seek */
    url_fseek(&s->pb, pos, SEEK_SET);

    av_update_cur_dts(s, st, ts);

    return 0;
}

int64_t av_gen_search(AVFormatContext *s, int stream_index, int64_t target_ts, int64_t pos_min, int64_t pos_max, int64_t pos_limit, int64_t ts_min, int64_t ts_max, int flags, int64_t *ts_ret, int64_t (*read_timestamp)(struct AVFormatContext *, int , int64_t *, int64_t )){
    int64_t pos, ts;
    int64_t start_pos, filesize;
    int no_change;

#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "gen_seek: %d %"PRId64"\n", stream_index, target_ts);
#endif

    if(ts_min == AV_NOPTS_VALUE){
        pos_min = s->data_offset;
        ts_min = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if(ts_max == AV_NOPTS_VALUE){
        int step= 1024;
        filesize = url_fsize(&s->pb);
        pos_max = filesize - 1;
        do{
            pos_max -= step;
            ts_max = read_timestamp(s, stream_index, &pos_max, pos_max + step);
            step += step;
        }while(ts_max == AV_NOPTS_VALUE && pos_max >= step);
        if (ts_max == AV_NOPTS_VALUE)
            return -1;

        for(;;){
            int64_t tmp_pos= pos_max + 1;
            int64_t tmp_ts= read_timestamp(s, stream_index, &tmp_pos, INT64_MAX);
            if(tmp_ts == AV_NOPTS_VALUE)
                break;
            ts_max= tmp_ts;
            pos_max= tmp_pos;
            if(tmp_pos >= filesize)
                break;
        }
        pos_limit= pos_max;
    }

    if(ts_min > ts_max){
        return -1;
    }else if(ts_min == ts_max){
        pos_limit= pos_min;
    }

    no_change=0;
    while (pos_min < pos_limit) {
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "pos_min=0x%"PRIx64" pos_max=0x%"PRIx64" dts_min=%"PRId64" dts_max=%"PRId64"\n",
               pos_min, pos_max,
               ts_min, ts_max);
#endif
        assert(pos_limit <= pos_max);

        if(no_change==0){
            int64_t approximate_keyframe_distance= pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = av_rescale(target_ts - ts_min, pos_max - pos_min, ts_max - ts_min)
                + pos_min - approximate_keyframe_distance;
        }else if(no_change==1){
            // bisection, if interpolation failed to change min or max pos last time
            pos = (pos_min + pos_limit)>>1;
        }else{
            // linear search if bisection failed, can only happen if there are very few or no keframes between min/max
            pos=pos_min;
        }
        if(pos <= pos_min)
            pos= pos_min + 1;
        else if(pos > pos_limit)
            pos= pos_limit;
        start_pos= pos;

        ts = read_timestamp(s, stream_index, &pos, INT64_MAX); //may pass pos_limit instead of -1
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;
#ifdef DEBUG_SEEK
av_log(s, AV_LOG_DEBUG, "%"PRId64" %"PRId64" %"PRId64" / %"PRId64" %"PRId64" %"PRId64" target:%"PRId64" limit:%"PRId64" start:%"PRId64" noc:%d\n", pos_min, pos, pos_max, ts_min, ts, ts_max, target_ts, pos_limit, start_pos, no_change);
#endif
        assert(ts != AV_NOPTS_VALUE);
        if (target_ts <= ts) {
            pos_limit = start_pos - 1;
            pos_max = pos;
            ts_max = ts;
        }
        if (target_ts >= ts) {
            pos_min = pos;
            ts_min = ts;
        }
    }

    pos = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;
    ts  = (flags & AVSEEK_FLAG_BACKWARD) ?  ts_min :  ts_max;
#ifdef DEBUG_SEEK
    pos_min = pos;
    ts_min = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    pos_min++;
    ts_max = read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    av_log(s, AV_LOG_DEBUG, "pos=0x%"PRIx64" %"PRId64"<=%"PRId64"<=%"PRId64"\n",
           pos, ts_min, target_ts, ts_max);
#endif
    *ts_ret= ts;
    return pos;
}

static int av_seek_frame_byte(AVFormatContext *s, int stream_index, int64_t pos, int flags){
    int64_t pos_min, pos_max;
#if 0
    AVStream *st;

    if (stream_index < 0)
        return -1;

    st= s->streams[stream_index];
#endif

    pos_min = s->data_offset;
    pos_max = url_fsize(&s->pb) - 1;

    if     (pos < pos_min) pos= pos_min;
    else if(pos > pos_max) pos= pos_max;

    url_fseek(&s->pb, pos, SEEK_SET);

#if 0
    av_update_cur_dts(s, st, ts);
#endif
    return 0;
}

static int av_seek_frame_generic(AVFormatContext *s,
                                 int stream_index, int64_t timestamp, int flags)
{
    int index;
    AVStream *st;
    AVIndexEntry *ie;

    st = s->streams[stream_index];

    index = av_index_search_timestamp(st, timestamp, flags);

    if(index < 0){
        int i;
        AVPacket pkt;

        if(st->index_entries && st->nb_index_entries){
            ie= &st->index_entries[st->nb_index_entries-1];
            url_fseek(&s->pb, ie->pos, SEEK_SET);
            av_update_cur_dts(s, st, ie->timestamp);
        }else
            url_fseek(&s->pb, 0, SEEK_SET);

        for(i=0;; i++) {
            int ret = av_read_frame(s, &pkt);
            if(ret<0)
                break;
            av_free_packet(&pkt);
            if(stream_index == pkt.stream_index){
                if((pkt.flags & PKT_FLAG_KEY) && pkt.dts > timestamp)
                    break;
            }
        }
        index = av_index_search_timestamp(st, timestamp, flags);
    }
    if (index < 0)
        return -1;

    av_read_frame_flush(s);
    if (s->iformat->read_seek){
        if(s->iformat->read_seek(s, stream_index, timestamp, flags) >= 0)
            return 0;
    }
    ie = &st->index_entries[index];
    url_fseek(&s->pb, ie->pos, SEEK_SET);

    av_update_cur_dts(s, st, ie->timestamp);

    return 0;
}

int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    int ret;
    AVStream *st;

    av_read_frame_flush(s);

    if(flags & AVSEEK_FLAG_BYTE)
        return av_seek_frame_byte(s, stream_index, timestamp, flags);

    if(stream_index < 0){
        stream_index= av_find_default_stream_index(s);
        if(stream_index < 0)
            return -1;

        st= s->streams[stream_index];
       /* timestamp for default must be expressed in AV_TIME_BASE units */
        timestamp = av_rescale(timestamp, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    }
    st= s->streams[stream_index];

    /* first, we try the format specific seek */
    if (s->iformat->read_seek)
        ret = s->iformat->read_seek(s, stream_index, timestamp, flags);
    else
        ret = -1;
    if (ret >= 0) {
        return 0;
    }

    if(s->iformat->read_timestamp)
        return av_seek_frame_binary(s, stream_index, timestamp, flags);
    else
        return av_seek_frame_generic(s, stream_index, timestamp, flags);
}

/*******************************************************/

/**
 * Returns TRUE if the stream has accurate timings in any stream.
 *
 * @return TRUE if the stream has accurate timings for at least one component.
 */
static int av_has_timings(AVFormatContext *ic)
{
    int i;
    AVStream *st;

    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE &&
            st->duration != AV_NOPTS_VALUE)
            return 1;
    }
    return 0;
}

/**
 * Estimate the stream timings from the one of each components.
 *
 * Also computes the global bitrate if possible.
 */
static void av_update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, start_time1, end_time, end_time1;
    int i;
    AVStream *st;

    start_time = INT64_MAX;
    end_time = INT64_MIN;
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE) {
            start_time1= av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
            if (start_time1 < start_time)
                start_time = start_time1;
            if (st->duration != AV_NOPTS_VALUE) {
                end_time1 = start_time1
                          + av_rescale_q(st->duration, st->time_base, AV_TIME_BASE_Q);
                if (end_time1 > end_time)
                    end_time = end_time1;
            }
        }
    }
    if (start_time != INT64_MAX) {
        ic->start_time = start_time;
        if (end_time != INT64_MIN) {
            ic->duration = end_time - start_time;
            if (ic->file_size > 0) {
                /* compute the bit rate */
                ic->bit_rate = (double)ic->file_size * 8.0 * AV_TIME_BASE /
                    (double)ic->duration;
            }
        }
    }

}

static void fill_all_stream_timings(AVFormatContext *ic)
{
    int i;
    AVStream *st;

    av_update_stream_timings(ic);
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time == AV_NOPTS_VALUE) {
            if(ic->start_time != AV_NOPTS_VALUE)
                st->start_time = av_rescale_q(ic->start_time, AV_TIME_BASE_Q, st->time_base);
            if(ic->duration != AV_NOPTS_VALUE)
                st->duration = av_rescale_q(ic->duration, AV_TIME_BASE_Q, st->time_base);
        }
    }
}

static void av_estimate_timings_from_bit_rate(AVFormatContext *ic)
{
    int64_t filesize, duration;
    int bit_rate, i;
    AVStream *st;

    /* if bit_rate is already set, we believe it */
    if (ic->bit_rate == 0) {
        bit_rate = 0;
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            bit_rate += st->codec->bit_rate;
        }
        ic->bit_rate = bit_rate;
    }

    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE &&
        ic->bit_rate != 0 &&
        ic->file_size != 0)  {
        filesize = ic->file_size;
        if (filesize > 0) {
            for(i = 0; i < ic->nb_streams; i++) {
                st = ic->streams[i];
                duration= av_rescale(8*filesize, st->time_base.den, ic->bit_rate*(int64_t)st->time_base.num);
                if (st->start_time == AV_NOPTS_VALUE ||
                    st->duration == AV_NOPTS_VALUE) {
                    st->start_time = 0;
                    st->duration = duration;
                }
            }
        }
    }
}

#define DURATION_MAX_READ_SIZE 250000

/* only usable for MPEG-PS streams */
static void av_estimate_timings_from_pts(AVFormatContext *ic, offset_t old_offset)
{
    AVPacket pkt1, *pkt = &pkt1;
    AVStream *st;
    int read_size, i, ret;
    int64_t end_time;
    int64_t filesize, offset, duration;

    av_read_frame_flush(ic);

    /* we read the first packets to get the first PTS (not fully
       accurate, but it is enough now) */
    url_fseek(&ic->pb, 0, SEEK_SET);
    read_size = 0;
    for(;;) {
        if (read_size >= DURATION_MAX_READ_SIZE)
            break;
        /* if all info is available, we can stop */
        for(i = 0;i < ic->nb_streams; i++) {
            st = ic->streams[i];
            if (st->start_time == AV_NOPTS_VALUE)
                break;
        }
        if (i == ic->nb_streams)
            break;

        ret = av_read_packet(ic, pkt);
        if (ret != 0)
            break;
        read_size += pkt->size;
        st = ic->streams[pkt->stream_index];
        if (pkt->pts != AV_NOPTS_VALUE) {
            if (st->start_time == AV_NOPTS_VALUE)
                st->start_time = pkt->pts;
        }
        av_free_packet(pkt);
    }

    /* estimate the end time (duration) */
    /* XXX: may need to support wrapping */
    filesize = ic->file_size;
    offset = filesize - DURATION_MAX_READ_SIZE;
    if (offset < 0)
        offset = 0;

    url_fseek(&ic->pb, offset, SEEK_SET);
    read_size = 0;
    for(;;) {
        if (read_size >= DURATION_MAX_READ_SIZE)
            break;
        /* if all info is available, we can stop */
        for(i = 0;i < ic->nb_streams; i++) {
            st = ic->streams[i];
            if (st->duration == AV_NOPTS_VALUE)
                break;
        }
        if (i == ic->nb_streams)
            break;

        ret = av_read_packet(ic, pkt);
        if (ret != 0)
            break;
        read_size += pkt->size;
        st = ic->streams[pkt->stream_index];
        if (pkt->pts != AV_NOPTS_VALUE) {
            end_time = pkt->pts;
            duration = end_time - st->start_time;
            if (duration > 0) {
                if (st->duration == AV_NOPTS_VALUE ||
                    st->duration < duration)
                    st->duration = duration;
            }
        }
        av_free_packet(pkt);
    }

    fill_all_stream_timings(ic);

    url_fseek(&ic->pb, old_offset, SEEK_SET);
}

static void av_estimate_timings(AVFormatContext *ic, offset_t old_offset)
{
    int64_t file_size;

    /* get the file size, if possible */
    if (ic->iformat->flags & AVFMT_NOFILE) {
        file_size = 0;
    } else {
        file_size = url_fsize(&ic->pb);
        if (file_size < 0)
            file_size = 0;
    }
    ic->file_size = file_size;

    if ((!strcmp(ic->iformat->name, "mpeg") ||
         !strcmp(ic->iformat->name, "mpegts")) &&
        file_size && !ic->pb.is_streamed) {
        /* get accurate estimate from the PTSes */
        av_estimate_timings_from_pts(ic, old_offset);
    } else if (av_has_timings(ic)) {
        /* at least one components has timings - we use them for all
           the components */
        fill_all_stream_timings(ic);
    } else {
        /* less precise: use bit rate info */
        av_estimate_timings_from_bit_rate(ic);
    }
    av_update_stream_timings(ic);

#if 0
    {
        int i;
        AVStream *st;
        for(i = 0;i < ic->nb_streams; i++) {
            st = ic->streams[i];
        printf("%d: start_time: %0.3f duration: %0.3f\n",
               i, (double)st->start_time / AV_TIME_BASE,
               (double)st->duration / AV_TIME_BASE);
        }
        printf("stream: start_time: %0.3f duration: %0.3f bitrate=%d kb/s\n",
               (double)ic->start_time / AV_TIME_BASE,
               (double)ic->duration / AV_TIME_BASE,
               ic->bit_rate / 1000);
    }
#endif
}

static int has_codec_parameters(AVCodecContext *enc)
{
    int val;
    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        val = enc->sample_rate;
        break;
    case CODEC_TYPE_VIDEO:
        val = enc->width && enc->pix_fmt != PIX_FMT_NONE;
        break;
    default:
        val = 1;
        break;
    }
    return (val != 0);
}

static int try_decode_frame(AVStream *st, const uint8_t *data, int size)
{
    int16_t *samples;
    AVCodec *codec;
    int got_picture, data_size, ret=0;
    AVFrame picture;

  if(!st->codec->codec){
    codec = avcodec_find_decoder(st->codec->codec_id);
    if (!codec)
        return -1;
    ret = avcodec_open(st->codec, codec);
    if (ret < 0)
        return ret;
  }

  if(!has_codec_parameters(st->codec)){
    switch(st->codec->codec_type) {
    case CODEC_TYPE_VIDEO:
        ret = avcodec_decode_video(st->codec, &picture,
                                   &got_picture, (uint8_t *)data, size);
        break;
    case CODEC_TYPE_AUDIO:
        data_size = FFMAX(size, AVCODEC_MAX_AUDIO_FRAME_SIZE);
        samples = av_malloc(data_size);
        if (!samples)
            goto fail;
        ret = avcodec_decode_audio2(st->codec, samples,
                                    &data_size, (uint8_t *)data, size);
        av_free(samples);
        break;
    default:
        break;
    }
  }
 fail:
    return ret;
}

/* absolute maximum size we read until we abort */
#define MAX_READ_SIZE        5000000

#define MAX_STD_TIMEBASES (60*12+5)
static int get_std_framerate(int i){
    if(i<60*12) return i*1001;
    else        return ((int[]){24,30,60,12,15})[i-60*12]*1000*12;
}

int av_find_stream_info(AVFormatContext *ic)
{
    int i, count, ret, read_size, j;
    AVStream *st;
    AVPacket pkt1, *pkt;
    AVPacketList *pktl=NULL, **ppktl;
    int64_t last_dts[MAX_STREAMS];
    int duration_count[MAX_STREAMS]={0};
    double (*duration_error)[MAX_STD_TIMEBASES];
    offset_t old_offset = url_ftell(&ic->pb);
    int64_t codec_info_duration[MAX_STREAMS]={0};
    int codec_info_nb_frames[MAX_STREAMS]={0};

    duration_error = av_mallocz(MAX_STREAMS * sizeof(*duration_error));
    if (!duration_error) return AVERROR_NOMEM;

    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if(st->codec->codec_type == CODEC_TYPE_VIDEO){
/*            if(!st->time_base.num)
                st->time_base= */
            if(!st->codec->time_base.num)
                st->codec->time_base= st->time_base;
        }
        //only for the split stuff
        if (!st->parser) {
            st->parser = av_parser_init(st->codec->codec_id);
            if(st->need_parsing == 2 && st->parser){
                st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
            }
        }
    }

    for(i=0;i<MAX_STREAMS;i++){
        last_dts[i]= AV_NOPTS_VALUE;
    }

    count = 0;
    read_size = 0;
    ppktl = &ic->packet_buffer;
    for(;;) {
        /* check if one codec still needs to be handled */
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            if (!has_codec_parameters(st->codec))
                break;
            /* variable fps and no guess at the real fps */
            if(   (st->codec->time_base.den >= 101LL*st->codec->time_base.num || st->codec->codec_id == CODEC_ID_MPEG2VIDEO)
               && duration_count[i]<20 && st->codec->codec_type == CODEC_TYPE_VIDEO)
                break;
            if(st->parser && st->parser->parser->split && !st->codec->extradata)
                break;
        }
        if (i == ic->nb_streams) {
            /* NOTE: if the format has no header, then we need to read
               some packets to get most of the streams, so we cannot
               stop here */
            if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) {
                /* if we found the info for all the codecs, we can stop */
                ret = count;
                break;
            }
        }
        /* we did not get all the codec info, but we read too much data */
        if (read_size >= MAX_READ_SIZE) {
            ret = count;
            break;
        }

        /* NOTE: a new stream can be added there if no header in file
           (AVFMTCTX_NOHEADER) */
        ret = av_read_frame_internal(ic, &pkt1);
        if (ret < 0) {
            /* EOF or error */
            ret = -1; /* we could not have all the codec parameters before EOF */
            for(i=0;i<ic->nb_streams;i++) {
                st = ic->streams[i];
                if (!has_codec_parameters(st->codec)){
                    char buf[256];
                    avcodec_string(buf, sizeof(buf), st->codec, 0);
                    av_log(ic, AV_LOG_INFO, "Could not find codec parameters (%s)\n", buf);
                } else {
                    ret = 0;
                }
            }
            break;
        }

        pktl = av_mallocz(sizeof(AVPacketList));
        if (!pktl) {
            ret = AVERROR_NOMEM;
            break;
        }

        /* add the packet in the buffered packet list */
        *ppktl = pktl;
        ppktl = &pktl->next;

        pkt = &pktl->pkt;
        *pkt = pkt1;

        /* duplicate the packet */
        if (av_dup_packet(pkt) < 0) {
            ret = AVERROR_NOMEM;
            break;
        }

        read_size += pkt->size;

        st = ic->streams[pkt->stream_index];
        if(codec_info_nb_frames[st->index]>1)
            codec_info_duration[st->index] += pkt->duration;
        if (pkt->duration != 0)
            codec_info_nb_frames[st->index]++;

        {
            int index= pkt->stream_index;
            int64_t last= last_dts[index];
            int64_t duration= pkt->dts - last;

            if(pkt->dts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && duration>0){
                double dur= duration * av_q2d(st->time_base);

//                if(st->codec->codec_type == CODEC_TYPE_VIDEO)
//                    av_log(NULL, AV_LOG_ERROR, "%f\n", dur);
                if(duration_count[index] < 2)
                    memset(duration_error, 0, MAX_STREAMS * sizeof(*duration_error));
                for(i=1; i<MAX_STD_TIMEBASES; i++){
                    int framerate= get_std_framerate(i);
                    int ticks= lrintf(dur*framerate/(1001*12));
                    double error= dur - ticks*1001*12/(double)framerate;
                    duration_error[index][i] += error*error;
                }
                duration_count[index]++;
            }
            if(last == AV_NOPTS_VALUE || duration_count[index]<=1)
                last_dts[pkt->stream_index]= pkt->dts;
        }
        if(st->parser && st->parser->parser->split && !st->codec->extradata){
            int i= st->parser->parser->split(st->codec, pkt->data, pkt->size);
            if(i){
                st->codec->extradata_size= i;
                st->codec->extradata= av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                memcpy(st->codec->extradata, pkt->data, st->codec->extradata_size);
                memset(st->codec->extradata + i, 0, FF_INPUT_BUFFER_PADDING_SIZE);
            }
        }

        /* if still no information, we try to open the codec and to
           decompress the frame. We try to avoid that in most cases as
           it takes longer and uses more memory. For MPEG4, we need to
           decompress for Quicktime. */
        if (!has_codec_parameters(st->codec) /*&&
            (st->codec->codec_id == CODEC_ID_FLV1 ||
             st->codec->codec_id == CODEC_ID_H264 ||
             st->codec->codec_id == CODEC_ID_H263 ||
             st->codec->codec_id == CODEC_ID_H261 ||
             st->codec->codec_id == CODEC_ID_VORBIS ||
             st->codec->codec_id == CODEC_ID_MJPEG ||
             st->codec->codec_id == CODEC_ID_PNG ||
             st->codec->codec_id == CODEC_ID_PAM ||
             st->codec->codec_id == CODEC_ID_PGM ||
             st->codec->codec_id == CODEC_ID_PGMYUV ||
             st->codec->codec_id == CODEC_ID_PBM ||
             st->codec->codec_id == CODEC_ID_PPM ||
             st->codec->codec_id == CODEC_ID_SHORTEN ||
             (st->codec->codec_id == CODEC_ID_MPEG4 && !st->need_parsing))*/)
            try_decode_frame(st, pkt->data, pkt->size);

        if (av_rescale_q(codec_info_duration[st->index], st->time_base, AV_TIME_BASE_Q) >= ic->max_analyze_duration) {
            break;
        }
        count++;
    }

    // close codecs which where opened in try_decode_frame()
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if(st->codec->codec)
            avcodec_close(st->codec);
    }
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            if(st->codec->codec_id == CODEC_ID_RAWVIDEO && !st->codec->codec_tag && !st->codec->bits_per_sample)
                st->codec->codec_tag= avcodec_pix_fmt_to_codec_tag(st->codec->pix_fmt);

            if(duration_count[i]
               && (st->codec->time_base.num*101LL <= st->codec->time_base.den || st->codec->codec_id == CODEC_ID_MPEG2VIDEO) /*&&
               //FIXME we should not special case mpeg2, but this needs testing with non mpeg2 ...
               st->time_base.num*duration_sum[i]/duration_count[i]*101LL > st->time_base.den*/){
                double best_error= 2*av_q2d(st->time_base);
                best_error= best_error*best_error*duration_count[i]*1000*12*30;

                for(j=1; j<MAX_STD_TIMEBASES; j++){
                    double error= duration_error[i][j] * get_std_framerate(j);
//                    if(st->codec->codec_type == CODEC_TYPE_VIDEO)
//                        av_log(NULL, AV_LOG_ERROR, "%f %f\n", get_std_framerate(j) / 12.0/1001, error);
                    if(error < best_error){
                        best_error= error;
                        av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, get_std_framerate(j), 12*1001, INT_MAX);
                    }
                }
            }

            if (!st->r_frame_rate.num){
                if(    st->codec->time_base.den * (int64_t)st->time_base.num
                    <= st->codec->time_base.num * (int64_t)st->time_base.den){
                    st->r_frame_rate.num = st->codec->time_base.den;
                    st->r_frame_rate.den = st->codec->time_base.num;
                }else{
                    st->r_frame_rate.num = st->time_base.den;
                    st->r_frame_rate.den = st->time_base.num;
                }
            }
        }else if(st->codec->codec_type == CODEC_TYPE_AUDIO) {
            if(!st->codec->bits_per_sample)
                st->codec->bits_per_sample= av_get_bits_per_sample(st->codec->codec_id);
        }
    }

    av_estimate_timings(ic, old_offset);
#if 0
    /* correct DTS for b frame streams with no timestamps */
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            if(b-frames){
                ppktl = &ic->packet_buffer;
                while(ppkt1){
                    if(ppkt1->stream_index != i)
                        continue;
                    if(ppkt1->pkt->dts < 0)
                        break;
                    if(ppkt1->pkt->pts != AV_NOPTS_VALUE)
                        break;
                    ppkt1->pkt->dts -= delta;
                    ppkt1= ppkt1->next;
                }
                if(ppkt1)
                    continue;
                st->cur_dts -= delta;
            }
        }
    }
#endif

    av_free(duration_error);

    return ret;
}

/*******************************************************/

int av_read_play(AVFormatContext *s)
{
    if (!s->iformat->read_play)
        return AVERROR_NOTSUPP;
    return s->iformat->read_play(s);
}

int av_read_pause(AVFormatContext *s)
{
    if (!s->iformat->read_pause)
        return AVERROR_NOTSUPP;
    return s->iformat->read_pause(s);
}

void av_close_input_file(AVFormatContext *s)
{
    int i, must_open_file;
    AVStream *st;

    /* free previous packet */
    if (s->cur_st && s->cur_st->parser)
        av_free_packet(&s->cur_pkt);

    if (s->iformat->read_close)
        s->iformat->read_close(s);
    for(i=0;i<s->nb_streams;i++) {
        /* free all data in a stream component */
        st = s->streams[i];
        if (st->parser) {
            av_parser_close(st->parser);
        }
        av_free(st->index_entries);
        av_free(st->codec->extradata);
        av_free(st->codec);
        av_free(st);
    }
    flush_packet_queue(s);
    must_open_file = 1;
    if (s->iformat->flags & AVFMT_NOFILE) {
        must_open_file = 0;
    }
    if (must_open_file) {
        url_fclose(&s->pb);
    }
    av_freep(&s->priv_data);
    av_free(s);
}

AVStream *av_new_stream(AVFormatContext *s, int id)
{
    AVStream *st;
    int i;

    if (s->nb_streams >= MAX_STREAMS)
        return NULL;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return NULL;

    st->codec= avcodec_alloc_context();
    if (s->iformat) {
        /* no default bitrate if decoding */
        st->codec->bit_rate = 0;
    }
    st->index = s->nb_streams;
    st->id = id;
    st->start_time = AV_NOPTS_VALUE;
    st->duration = AV_NOPTS_VALUE;
    st->cur_dts = AV_NOPTS_VALUE;

    /* default pts settings is MPEG like */
    av_set_pts_info(st, 33, 1, 90000);
    st->last_IP_pts = AV_NOPTS_VALUE;
    for(i=0; i<MAX_REORDER_DELAY+1; i++)
        st->pts_buffer[i]= AV_NOPTS_VALUE;

    s->streams[s->nb_streams++] = st;
    return st;
}

/************************************************************/
/* output media file */

int av_set_parameters(AVFormatContext *s, AVFormatParameters *ap)
{
    int ret;

    if (s->oformat->priv_data_size > 0) {
        s->priv_data = av_mallocz(s->oformat->priv_data_size);
        if (!s->priv_data)
            return AVERROR_NOMEM;
    } else
        s->priv_data = NULL;

    if (s->oformat->set_parameters) {
        ret = s->oformat->set_parameters(s, ap);
        if (ret < 0)
            return ret;
    }
    return 0;
}

int av_write_header(AVFormatContext *s)
{
    int ret, i;
    AVStream *st;

    // some sanity checks
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];

        switch (st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            if(st->codec->sample_rate<=0){
                av_log(s, AV_LOG_ERROR, "sample rate not set\n");
                return -1;
            }
            break;
        case CODEC_TYPE_VIDEO:
            if(st->codec->time_base.num<=0 || st->codec->time_base.den<=0){ //FIXME audio too?
                av_log(s, AV_LOG_ERROR, "time base not set\n");
                return -1;
            }
            if(st->codec->width<=0 || st->codec->height<=0){
                av_log(s, AV_LOG_ERROR, "dimensions not set\n");
                return -1;
            }
            break;
        }

        if(s->oformat->codec_tag){
            if(st->codec->codec_tag){
                //FIXME
                //check that tag + id is in the table
                //if neither is in the table -> ok
                //if tag is in the table with another id -> FAIL
                //if id is in the table with another tag -> FAIL unless strict < ?
            }else
                st->codec->codec_tag= av_codec_get_tag(s->oformat->codec_tag, st->codec->codec_id);
        }
    }

    if (!s->priv_data && s->oformat->priv_data_size > 0) {
        s->priv_data = av_mallocz(s->oformat->priv_data_size);
        if (!s->priv_data)
            return AVERROR_NOMEM;
    }

    if(s->oformat->write_header){
        ret = s->oformat->write_header(s);
        if (ret < 0)
            return ret;
    }

    /* init PTS generation */
    for(i=0;i<s->nb_streams;i++) {
        int64_t den = AV_NOPTS_VALUE;
        st = s->streams[i];

        switch (st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            den = (int64_t)st->time_base.num * st->codec->sample_rate;
            break;
        case CODEC_TYPE_VIDEO:
            den = (int64_t)st->time_base.num * st->codec->time_base.den;
            break;
        default:
            break;
        }
        if (den != AV_NOPTS_VALUE) {
            if (den <= 0)
                return AVERROR_INVALIDDATA;
            av_frac_init(&st->pts, 0, 0, den);
        }
    }
    return 0;
}

//FIXME merge with compute_pkt_fields
static int compute_pkt_fields2(AVStream *st, AVPacket *pkt){
    int delay = FFMAX(st->codec->has_b_frames, !!st->codec->max_b_frames);
    int num, den, frame_size, i;

//    av_log(st->codec, AV_LOG_DEBUG, "av_write_frame: pts:%"PRId64" dts:%"PRId64" cur_dts:%"PRId64" b:%d size:%d st:%d\n", pkt->pts, pkt->dts, st->cur_dts, delay, pkt->size, pkt->stream_index);

/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
        return -1;*/

    /* duration field */
    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num);
        }
    }

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !delay){
        pkt->dts=
//        pkt->pts= st->cur_dts;
        pkt->pts= st->pts.val;
    }

    //calculate dts from pts
    if(pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE){
        st->pts_buffer[0]= pkt->pts;
        for(i=1; i<delay+1 && st->pts_buffer[i] == AV_NOPTS_VALUE; i++)
            st->pts_buffer[i]= (i-delay-1) * pkt->duration;
        for(i=0; i<delay && st->pts_buffer[i] > st->pts_buffer[i+1]; i++)
            FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i+1]);

        pkt->dts= st->pts_buffer[0];
    }

    if(st->cur_dts && st->cur_dts != AV_NOPTS_VALUE && st->cur_dts >= pkt->dts){
        av_log(NULL, AV_LOG_ERROR, "error, non monotone timestamps %"PRId64" >= %"PRId64" st:%d\n", st->cur_dts, pkt->dts, st->index);
        return -1;
    }
    if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts < pkt->dts){
        av_log(NULL, AV_LOG_ERROR, "error, pts < dts (%"PRId64" < %"PRId64")\n",
               pkt->pts, pkt->dts);
        return -1;
    }

//    av_log(NULL, AV_LOG_DEBUG, "av_write_frame: pts2:%"PRId64" dts2:%"PRId64"\n", pkt->pts, pkt->dts);
    st->cur_dts= pkt->dts;
    st->pts.val= pkt->dts;

    /* update pts */
    switch (st->codec->codec_type) {
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(st->codec, pkt->size);

        /* HACK/FIXME, we skip the initial 0-size packets as they are most likely equal to the encoder delay,
           but it would be better if we had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || st->pts.num!=st->pts.den>>1 || st->pts.val)) {
            av_frac_add(&st->pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case CODEC_TYPE_VIDEO:
        av_frac_add(&st->pts, (int64_t)st->time_base.den * st->codec->time_base.num);
        break;
    default:
        break;
    }
    return 0;
}

static void truncate_ts(AVStream *st, AVPacket *pkt){
    int64_t pts_mask = (2LL << (st->pts_wrap_bits-1)) - 1;

//    if(pkt->dts < 0)
//        pkt->dts= 0;  //this happens for low_delay=0 and b frames, FIXME, needs further invstigation about what we should do here

    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts &= pts_mask;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts &= pts_mask;
}

int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret=compute_pkt_fields2(s->streams[pkt->stream_index], pkt);
    if(ret<0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return ret;

    truncate_ts(s->streams[pkt->stream_index], pkt);

    ret= s->oformat->write_packet(s, pkt);
    if(!ret)
        ret= url_ferror(&s->pb);
    return ret;
}

int av_interleave_packet_per_dts(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush){
    AVPacketList *pktl, **next_point, *this_pktl;
    int stream_count=0;
    int streams[MAX_STREAMS];

    if(pkt){
        AVStream *st= s->streams[ pkt->stream_index];

//        assert(pkt->destruct != av_destruct_packet); //FIXME

        this_pktl = av_mallocz(sizeof(AVPacketList));
        this_pktl->pkt= *pkt;
        if(pkt->destruct == av_destruct_packet)
            pkt->destruct= NULL; // non shared -> must keep original from being freed
        else
            av_dup_packet(&this_pktl->pkt);  //shared -> must dup

        next_point = &s->packet_buffer;
        while(*next_point){
            AVStream *st2= s->streams[ (*next_point)->pkt.stream_index];
            int64_t left=  st2->time_base.num * (int64_t)st ->time_base.den;
            int64_t right= st ->time_base.num * (int64_t)st2->time_base.den;
            if((*next_point)->pkt.dts * left > pkt->dts * right) //FIXME this can overflow
                break;
            next_point= &(*next_point)->next;
        }
        this_pktl->next= *next_point;
        *next_point= this_pktl;
    }

    memset(streams, 0, sizeof(streams));
    pktl= s->packet_buffer;
    while(pktl){
//av_log(s, AV_LOG_DEBUG, "show st:%d dts:%"PRId64"\n", pktl->pkt.stream_index, pktl->pkt.dts);
        if(streams[ pktl->pkt.stream_index ] == 0)
            stream_count++;
        streams[ pktl->pkt.stream_index ]++;
        pktl= pktl->next;
    }

    if(s->nb_streams == stream_count || (flush && stream_count)){
        pktl= s->packet_buffer;
        *out= pktl->pkt;

        s->packet_buffer= pktl->next;
        av_freep(&pktl);
        return 1;
    }else{
        av_init_packet(out);
        return 0;
    }
}

/**
 * Interleaves a AVPacket correctly so it can be muxed.
 * @param out the interleaved packet will be output here
 * @param in the input packet
 * @param flush 1 if no further packets are available as input and all
 *              remaining packets should be output
 * @return 1 if a packet was output, 0 if no packet could be output,
 *         < 0 if an error occured
 */
static int av_interleave_packet(AVFormatContext *s, AVPacket *out, AVPacket *in, int flush){
    if(s->oformat->interleave_packet)
        return s->oformat->interleave_packet(s, out, in, flush);
    else
        return av_interleave_packet_per_dts(s, out, in, flush);
}

int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt){
    AVStream *st= s->streams[ pkt->stream_index];

    //FIXME/XXX/HACK drop zero sized packets
    if(st->codec->codec_type == CODEC_TYPE_AUDIO && pkt->size==0)
        return 0;

//av_log(NULL, AV_LOG_DEBUG, "av_interleaved_write_frame %d %"PRId64" %"PRId64"\n", pkt->size, pkt->dts, pkt->pts);
    if(compute_pkt_fields2(st, pkt) < 0 && !(s->oformat->flags & AVFMT_NOTIMESTAMPS))
        return -1;

    if(pkt->dts == AV_NOPTS_VALUE)
        return -1;

    for(;;){
        AVPacket opkt;
        int ret= av_interleave_packet(s, &opkt, pkt, 0);
        if(ret<=0) //FIXME cleanup needed for ret<0 ?
            return ret;

        truncate_ts(s->streams[opkt.stream_index], &opkt);
        ret= s->oformat->write_packet(s, &opkt);

        av_free_packet(&opkt);
        pkt= NULL;

        if(ret<0)
            return ret;
        if(url_ferror(&s->pb))
            return url_ferror(&s->pb);
    }
}

int av_write_trailer(AVFormatContext *s)
{
    int ret, i;

    for(;;){
        AVPacket pkt;
        ret= av_interleave_packet(s, &pkt, NULL, 1);
        if(ret<0) //FIXME cleanup needed for ret<0 ?
            goto fail;
        if(!ret)
            break;

        truncate_ts(s->streams[pkt.stream_index], &pkt);
        ret= s->oformat->write_packet(s, &pkt);

        av_free_packet(&pkt);

        if(ret<0)
            goto fail;
        if(url_ferror(&s->pb))
            goto fail;
    }

    if(s->oformat->write_trailer)
        ret = s->oformat->write_trailer(s);
fail:
    if(ret == 0)
       ret=url_ferror(&s->pb);
    for(i=0;i<s->nb_streams;i++)
        av_freep(&s->streams[i]->priv_data);
    av_freep(&s->priv_data);
    return ret;
}

/* "user interface" functions */

void dump_format(AVFormatContext *ic,
                 int index,
                 const char *url,
                 int is_output)
{
    int i, flags;
    char buf[256];

    av_log(NULL, AV_LOG_INFO, "%s #%d, %s, %s '%s':\n",
            is_output ? "Output" : "Input",
            index,
            is_output ? ic->oformat->name : ic->iformat->name,
            is_output ? "to" : "from", url);
    if (!is_output) {
        av_log(NULL, AV_LOG_INFO, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            secs = ic->duration / AV_TIME_BASE;
            us = ic->duration % AV_TIME_BASE;
            mins = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            av_log(NULL, AV_LOG_INFO, "%02d:%02d:%02d.%01d", hours, mins, secs,
                   (10 * us) / AV_TIME_BASE);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            av_log(NULL, AV_LOG_INFO, ", start: ");
            secs = ic->start_time / AV_TIME_BASE;
            us = ic->start_time % AV_TIME_BASE;
            av_log(NULL, AV_LOG_INFO, "%d.%06d",
                   secs, (int)av_rescale(us, 1000000, AV_TIME_BASE));
        }
        av_log(NULL, AV_LOG_INFO, ", bitrate: ");
        if (ic->bit_rate) {
            av_log(NULL, AV_LOG_INFO,"%d kb/s", ic->bit_rate / 1000);
        } else {
            av_log(NULL, AV_LOG_INFO, "N/A");
        }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st = ic->streams[i];
        int g= ff_gcd(st->time_base.num, st->time_base.den);
        avcodec_string(buf, sizeof(buf), st->codec, is_output);
        av_log(NULL, AV_LOG_INFO, "  Stream #%d.%d", index, i);
        /* the pid is an important information, so we display it */
        /* XXX: add a generic system */
        if (is_output)
            flags = ic->oformat->flags;
        else
            flags = ic->iformat->flags;
        if (flags & AVFMT_SHOW_IDS) {
            av_log(NULL, AV_LOG_INFO, "[0x%x]", st->id);
        }
        if (strlen(st->language) > 0) {
            av_log(NULL, AV_LOG_INFO, "(%s)", st->language);
        }
        av_log(NULL, AV_LOG_DEBUG, ", %d/%d", st->time_base.num/g, st->time_base.den/g);
        av_log(NULL, AV_LOG_INFO, ": %s", buf);
        if(st->codec->codec_type == CODEC_TYPE_VIDEO){
            if(st->r_frame_rate.den && st->r_frame_rate.num)
                av_log(NULL, AV_LOG_INFO, ", %5.2f fps(r)", av_q2d(st->r_frame_rate));
/*            else if(st->time_base.den && st->time_base.num)
                av_log(NULL, AV_LOG_INFO, ", %5.2f fps(m)", 1/av_q2d(st->time_base));*/
            else
                av_log(NULL, AV_LOG_INFO, ", %5.2f fps(c)", 1/av_q2d(st->codec->time_base));
        }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
}

typedef struct {
    const char *abv;
    int width, height;
    int frame_rate, frame_rate_base;
} AbvEntry;

static AbvEntry frame_abvs[] = {
    { "ntsc",      720, 480, 30000, 1001 },
    { "pal",       720, 576,    25,    1 },
    { "qntsc",     352, 240, 30000, 1001 }, /* VCD compliant ntsc */
    { "qpal",      352, 288,    25,    1 }, /* VCD compliant pal */
    { "sntsc",     640, 480, 30000, 1001 }, /* square pixel ntsc */
    { "spal",      768, 576,    25,    1 }, /* square pixel pal */
    { "film",      352, 240,    24,    1 },
    { "ntsc-film", 352, 240, 24000, 1001 },
    { "sqcif",     128,  96,     0,    0 },
    { "qcif",      176, 144,     0,    0 },
    { "cif",       352, 288,     0,    0 },
    { "4cif",      704, 576,     0,    0 },
};

int parse_image_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i;
    int n = sizeof(frame_abvs) / sizeof(AbvEntry);
    const char *p;
    int frame_width = 0, frame_height = 0;

    for(i=0;i<n;i++) {
        if (!strcmp(frame_abvs[i].abv, str)) {
            frame_width = frame_abvs[i].width;
            frame_height = frame_abvs[i].height;
            break;
        }
    }
    if (i == n) {
        p = str;
        frame_width = strtol(p, (char **)&p, 10);
        if (*p)
            p++;
        frame_height = strtol(p, (char **)&p, 10);
    }
    if (frame_width <= 0 || frame_height <= 0)
        return -1;
    *width_ptr = frame_width;
    *height_ptr = frame_height;
    return 0;
}

int parse_frame_rate(int *frame_rate, int *frame_rate_base, const char *arg)
{
    int i;
    char* cp;

    /* First, we check our abbreviation table */
    for (i = 0; i < sizeof(frame_abvs)/sizeof(*frame_abvs); ++i)
         if (!strcmp(frame_abvs[i].abv, arg)) {
             *frame_rate = frame_abvs[i].frame_rate;
             *frame_rate_base = frame_abvs[i].frame_rate_base;
             return 0;
         }

    /* Then, we try to parse it as fraction */
    cp = strchr(arg, '/');
    if (!cp)
        cp = strchr(arg, ':');
    if (cp) {
        char* cpp;
        *frame_rate = strtol(arg, &cpp, 10);
        if (cpp != arg || cpp == cp)
            *frame_rate_base = strtol(cp+1, &cpp, 10);
        else
           *frame_rate = 0;
    }
    else {
        /* Finally we give up and parse it as double */
        AVRational time_base = av_d2q(strtod(arg, 0), DEFAULT_FRAME_RATE_BASE);
        *frame_rate_base = time_base.den;
        *frame_rate = time_base.num;
    }
    if (!*frame_rate || !*frame_rate_base)
        return -1;
    else
        return 0;
}

#ifndef CONFIG_WINCE
int64_t parse_date(const char *datestr, int duration)
{
    const char *p;
    int64_t t;
    struct tm dt;
    int i;
    static const char *date_fmt[] = {
        "%Y-%m-%d",
        "%Y%m%d",
    };
    static const char *time_fmt[] = {
        "%H:%M:%S",
        "%H%M%S",
    };
    const char *q;
    int is_utc, len;
    char lastch;
    int negative = 0;

#undef time
    time_t now = time(0);

    len = strlen(datestr);
    if (len > 0)
        lastch = datestr[len - 1];
    else
        lastch = '\0';
    is_utc = (lastch == 'z' || lastch == 'Z');

    memset(&dt, 0, sizeof(dt));

    p = datestr;
    q = NULL;
    if (!duration) {
        for (i = 0; i < sizeof(date_fmt) / sizeof(date_fmt[0]); i++) {
            q = small_strptime(p, date_fmt[i], &dt);
            if (q) {
                break;
            }
        }

        if (!q) {
            if (is_utc) {
                dt = *gmtime(&now);
            } else {
                dt = *localtime(&now);
            }
            dt.tm_hour = dt.tm_min = dt.tm_sec = 0;
        } else {
            p = q;
        }

        if (*p == 'T' || *p == 't' || *p == ' ')
            p++;

        for (i = 0; i < sizeof(time_fmt) / sizeof(time_fmt[0]); i++) {
            q = small_strptime(p, time_fmt[i], &dt);
            if (q) {
                break;
            }
        }
    } else {
        if (p[0] == '-') {
            negative = 1;
            ++p;
        }
        q = small_strptime(p, time_fmt[0], &dt);
        if (!q) {
            dt.tm_sec = strtol(p, (char **)&q, 10);
            dt.tm_min = 0;
            dt.tm_hour = 0;
        }
    }

    /* Now we have all the fields that we can get */
    if (!q) {
        if (duration)
            return 0;
        else
            return now * INT64_C(1000000);
    }

    if (duration) {
        t = dt.tm_hour * 3600 + dt.tm_min * 60 + dt.tm_sec;
    } else {
        dt.tm_isdst = -1;       /* unknown */
        if (is_utc) {
            t = mktimegm(&dt);
        } else {
            t = mktime(&dt);
        }
    }

    t *= 1000000;

    if (*q == '.') {
        int val, n;
        q++;
        for (val = 0, n = 100000; n >= 1; n /= 10, q++) {
            if (!isdigit(*q))
                break;
            val += n * (*q - '0');
        }
        t += val;
    }
    return negative ? -t : t;
}
#endif /* CONFIG_WINCE */

int find_info_tag(char *arg, int arg_size, const char *tag1, const char *info)
{
    const char *p;
    char tag[128], *q;

    p = info;
    if (*p == '?')
        p++;
    for(;;) {
        q = tag;
        while (*p != '\0' && *p != '=' && *p != '&') {
            if ((q - tag) < sizeof(tag) - 1)
                *q++ = *p;
            p++;
        }
        *q = '\0';
        q = arg;
        if (*p == '=') {
            p++;
            while (*p != '&' && *p != '\0') {
                if ((q - arg) < arg_size - 1) {
                    if (*p == '+')
                        *q++ = ' ';
                    else
                        *q++ = *p;
                }
                p++;
            }
            *q = '\0';
        }
        if (!strcmp(tag, tag1))
            return 1;
        if (*p != '&')
            break;
        p++;
    }
    return 0;
}

int av_get_frame_filename(char *buf, int buf_size,
                          const char *path, int number)
{
    const char *p;
    char *q, buf1[20], c;
    int nd, len, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for(;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            do {
                nd = 0;
                while (isdigit(*p)) {
                    nd = nd * 10 + *p++ - '0';
                }
                c = *p++;
            } while (isdigit(c));

            switch(c) {
            case '%':
                goto addchar;
            case 'd':
                if (percentd_found)
                    goto fail;
                percentd_found = 1;
                snprintf(buf1, sizeof(buf1), "%0*d", nd, number);
                len = strlen(buf1);
                if ((q - buf + len) > buf_size - 1)
                    goto fail;
                memcpy(q, buf1, len);
                q += len;
                break;
            default:
                goto fail;
            }
        } else {
        addchar:
            if ((q - buf) < buf_size - 1)
                *q++ = c;
        }
    }
    if (!percentd_found)
        goto fail;
    *q = '\0';
    return 0;
 fail:
    *q = '\0';
    return -1;
}

static void hex_dump_internal(void *avcl, FILE *f, int level, uint8_t *buf, int size)
{
    int len, i, j, c;
#define PRINT(...) do { if (!f) av_log(avcl, level, __VA_ARGS__); else fprintf(f, __VA_ARGS__); } while(0)

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        PRINT("%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                PRINT(" %02x", buf[i+j]);
            else
                PRINT("   ");
        }
        PRINT(" ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            PRINT("%c", c);
        }
        PRINT("\n");
    }
#undef PRINT
}

void av_hex_dump(FILE *f, uint8_t *buf, int size)
{
    hex_dump_internal(NULL, f, 0, buf, size);
}

void av_hex_dump_log(void *avcl, int level, uint8_t *buf, int size)
{
    hex_dump_internal(avcl, NULL, level, buf, size);
}

 //FIXME needs to know the time_base
static void pkt_dump_internal(void *avcl, FILE *f, int level, AVPacket *pkt, int dump_payload)
{
#define PRINT(...) do { if (!f) av_log(avcl, level, __VA_ARGS__); else fprintf(f, __VA_ARGS__); } while(0)
    PRINT("stream #%d:\n", pkt->stream_index);
    PRINT("  keyframe=%d\n", ((pkt->flags & PKT_FLAG_KEY) != 0));
    PRINT("  duration=%0.3f\n", (double)pkt->duration / AV_TIME_BASE);
    /* DTS is _always_ valid after av_read_frame() */
    PRINT("  dts=");
    if (pkt->dts == AV_NOPTS_VALUE)
        PRINT("N/A");
    else
        PRINT("%0.3f", (double)pkt->dts / AV_TIME_BASE);
    /* PTS may be not known if B frames are present */
    PRINT("  pts=");
    if (pkt->pts == AV_NOPTS_VALUE)
        PRINT("N/A");
    else
        PRINT("%0.3f", (double)pkt->pts / AV_TIME_BASE);
    PRINT("\n");
    PRINT("  size=%d\n", pkt->size);
#undef PRINT
    if (dump_payload)
        av_hex_dump(f, pkt->data, pkt->size);
}

void av_pkt_dump(FILE *f, AVPacket *pkt, int dump_payload)
{
    pkt_dump_internal(NULL, f, 0, pkt, dump_payload);
}

void av_pkt_dump_log(void *avcl, int level, AVPacket *pkt, int dump_payload)
{
    pkt_dump_internal(avcl, NULL, level, pkt, dump_payload);
}

void url_split(char *proto, int proto_size,
               char *authorization, int authorization_size,
               char *hostname, int hostname_size,
               int *port_ptr,
               char *path, int path_size,
               const char *url)
{
    const char *p;
    char *q;
    int port;

    port = -1;

    p = url;
    q = proto;
    while (*p != ':' && *p != '\0') {
        if ((q - proto) < proto_size - 1)
            *q++ = *p;
        p++;
    }
    if (proto_size > 0)
        *q = '\0';
    if (authorization_size > 0)
        authorization[0] = '\0';
    if (*p == '\0') {
        if (proto_size > 0)
            proto[0] = '\0';
        if (hostname_size > 0)
            hostname[0] = '\0';
        p = url;
    } else {
        char *at,*slash; // PETR: position of '@' character and '/' character

        p++;
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
        at = strchr(p,'@'); // PETR: get the position of '@'
        slash = strchr(p,'/');  // PETR: get position of '/' - end of hostname
        if (at && slash && at > slash) at = NULL; // PETR: not interested in '@' behind '/'

        q = at ? authorization : hostname;  // PETR: if '@' exists starting with auth.

         while ((at || *p != ':') && *p != '/' && *p != '?' && *p != '\0') { // PETR:
            if (*p == '@') {    // PETR: passed '@'
              if (authorization_size > 0)
                  *q = '\0';
              q = hostname;
              at = NULL;
            } else if (!at) {   // PETR: hostname
              if ((q - hostname) < hostname_size - 1)
                  *q++ = *p;
            } else {
              if ((q - authorization) < authorization_size - 1)
                *q++ = *p;
            }
            p++;
        }
        if (hostname_size > 0)
            *q = '\0';
        if (*p == ':') {
            p++;
            port = strtoul(p, (char **)&p, 10);
        }
    }
    if (port_ptr)
        *port_ptr = port;
    pstrcpy(path, path_size, p);
}

void av_set_pts_info(AVStream *s, int pts_wrap_bits,
                     int pts_num, int pts_den)
{
    s->pts_wrap_bits = pts_wrap_bits;
    s->time_base.num = pts_num;
    s->time_base.den = pts_den;
}

/* fraction handling */

/**
 * f = val + (num / den) + 0.5.
 *
 * 'num' is normalized so that it is such as 0 <= num < den.
 *
 * @param f fractional number
 * @param val integer value
 * @param num must be >= 0
 * @param den must be >= 1
 */
static void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den)
{
    num += (den >> 1);
    if (num >= den) {
        val += num / den;
        num = num % den;
    }
    f->val = val;
    f->num = num;
    f->den = den;
}

/**
 * Fractionnal addition to f: f = f + (incr / f->den).
 *
 * @param f fractional number
 * @param incr increment, can be positive or negative
 */
static void av_frac_add(AVFrac *f, int64_t incr)
{
    int64_t num, den;

    num = f->num + incr;
    den = f->den;
    if (num < 0) {
        f->val += num / den;
        num = num % den;
        if (num < 0) {
            num += den;
            f->val--;
        }
    } else if (num >= den) {
        f->val += num / den;
        num = num % den;
    }
    f->num = num;
}
