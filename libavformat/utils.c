/*
 * Various utilities for ffmpeg system
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"

#undef NDEBUG
#include <assert.h>

AVInputFormat *first_iformat;
AVOutputFormat *first_oformat;
AVImageFormat *first_image_format;

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
            while (*p != '\0' && *p != ',') 
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
    if (!short_name && filename && 
        filename_number_test(filename) >= 0 &&
        guess_image_format(filename)) {
        return guess_format("image", NULL, NULL);
    }

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

/**
 * Default packet destructor 
 */
static void av_destruct_packet(AVPacket *pkt)
{
    av_free(pkt->data);
    pkt->data = NULL; pkt->size = 0;
}

/**
 * Allocate the payload of a packet and intialized its fields to default values.
 *
 * @param pkt packet
 * @param size wanted payload size
 * @return 0 if OK. AVERROR_xxx otherwise.
 */
int av_new_packet(AVPacket *pkt, int size)
{
    void *data = av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!data)
        return AVERROR_NOMEM;
    memset(data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    av_init_packet(pkt);
    pkt->data = data; 
    pkt->size = size;
    pkt->destruct = av_destruct_packet;
    return 0;
}

/* This is a hack - the packet memory allocation stuff is broken. The
   packet is allocated if it was not really allocated */
int av_dup_packet(AVPacket *pkt)
{
    if (pkt->destruct != av_destruct_packet) {
        uint8_t *data;
        /* we duplicate the packet and don't forget to put the padding
           again */
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

/* fifo handling */

int fifo_init(FifoBuffer *f, int size)
{
    f->buffer = av_malloc(size);
    if (!f->buffer)
        return -1;
    f->end = f->buffer + size;
    f->wptr = f->rptr = f->buffer;
    return 0;
}

void fifo_free(FifoBuffer *f)
{
    av_free(f->buffer);
}

int fifo_size(FifoBuffer *f, uint8_t *rptr)
{
    int size;

    if (f->wptr >= rptr) {
        size = f->wptr - rptr;
    } else {
        size = (f->end - rptr) + (f->wptr - f->buffer);
    }
    return size;
}

/* get data from the fifo (return -1 if not enough data) */
int fifo_read(FifoBuffer *f, uint8_t *buf, int buf_size, uint8_t **rptr_ptr)
{
    uint8_t *rptr = *rptr_ptr;
    int size, len;

    if (f->wptr >= rptr) {
        size = f->wptr - rptr;
    } else {
        size = (f->end - rptr) + (f->wptr - f->buffer);
    }
    
    if (size < buf_size)
        return -1;
    while (buf_size > 0) {
        len = f->end - rptr;
        if (len > buf_size)
            len = buf_size;
        memcpy(buf, rptr, len);
        buf += len;
        rptr += len;
        if (rptr >= f->end)
            rptr = f->buffer;
        buf_size -= len;
    }
    *rptr_ptr = rptr;
    return 0;
}

void fifo_write(FifoBuffer *f, uint8_t *buf, int size, uint8_t **wptr_ptr)
{
    int len;
    uint8_t *wptr;
    wptr = *wptr_ptr;
    while (size > 0) {
        len = f->end - wptr;
        if (len > size)
            len = size;
        memcpy(wptr, buf, len);
        wptr += len;
        if (wptr >= f->end)
            wptr = f->buffer;
        buf += len;
        size -= len;
    }
    *wptr_ptr = wptr;
}

int filename_number_test(const char *filename)
{
    char buf[1024];
    if(!filename)
        return -1;
    return get_frame_filename(buf, sizeof(buf), filename, 1);
}

/* guess file format */
AVInputFormat *av_probe_input_format(AVProbeData *pd, int is_opened)
{
    AVInputFormat *fmt1, *fmt;
    int score, score_max;

    fmt = NULL;
    score_max = 0;
    for(fmt1 = first_iformat; fmt1 != NULL; fmt1 = fmt1->next) {
        if (!is_opened && !(fmt1->flags & AVFMT_NOFILE))
            continue;
        score = 0;
        if (fmt1->read_probe) {
            score = fmt1->read_probe(pd);
        } else if (fmt1->extensions) {
            if (match_ext(pd->filename, fmt1->extensions)) {
                score = 50;
            }
        } 
        if (score > score_max) {
            score_max = score;
            fmt = fmt1;
        }
    }
    return fmt;
}

/************************************************************/
/* input media file */

/**
 * open a media file from an IO stream. 'fmt' must be specified.
 */

static const char* format_to_name(void* ptr)
{
    AVFormatContext* fc = (AVFormatContext*) ptr;
    if(fc->iformat) return fc->iformat->name;
    else if(fc->oformat) return fc->oformat->name;
    else return "NULL";
}

static const AVClass av_format_context_class = { "AVFormatContext", format_to_name };

AVFormatContext *av_alloc_format_context(void)
{
    AVFormatContext *ic;
    ic = av_mallocz(sizeof(AVFormatContext));
    if (!ic) return ic;
    ic->av_class = &av_format_context_class;
    return ic;
}

int av_open_input_stream(AVFormatContext **ic_ptr, 
                         ByteIOContext *pb, const char *filename, 
                         AVInputFormat *fmt, AVFormatParameters *ap)
{
    int err;
    AVFormatContext *ic;

    ic = av_alloc_format_context();
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

    if (pb)
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

#define PROBE_BUF_SIZE 2048

/**
 * Open a media file as input. The codec are not opened. Only the file
 * header (if present) is read.
 *
 * @param ic_ptr the opened media file handle is put here
 * @param filename filename to open.
 * @param fmt if non NULL, force the file format to use
 * @param buf_size optional buffer size (zero if default is OK)
 * @param ap additionnal parameters needed when opening the file (NULL if default)
 * @return 0 if OK. AVERROR_xxx otherwise.
 */
int av_open_input_file(AVFormatContext **ic_ptr, const char *filename, 
                       AVInputFormat *fmt,
                       int buf_size,
                       AVFormatParameters *ap)
{
    int err, must_open_file, file_opened;
    uint8_t buf[PROBE_BUF_SIZE];
    AVProbeData probe_data, *pd = &probe_data;
    ByteIOContext pb1, *pb = &pb1;
    
    file_opened = 0;
    pd->filename = "";
    if (filename)
        pd->filename = filename;
    pd->buf = buf;
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
        if (!fmt) {
            /* read probe data */
            pd->buf_size = get_buffer(pb, buf, PROBE_BUF_SIZE);
            if (url_fseek(pb, 0, SEEK_SET) == (offset_t)-EPIPE) {
                url_fclose(pb);
                if (url_fopen(pb, filename, URL_RDONLY) < 0) {
                    err = AVERROR_IO;
                    goto fail;
                }
            }
        }
    }
    
    /* guess file format */
    if (!fmt) {
        fmt = av_probe_input_format(pd, 1);
    }

    /* if still no format found, error */
    if (!fmt) {
        err = AVERROR_NOFMT;
        goto fail;
    }
        
    /* XXX: suppress this hack for redirectors */
#ifdef CONFIG_NETWORK
    if (fmt == &redir_demux) {
        err = redir_open(ic_ptr, pb);
        url_fclose(pb);
        return err;
    }
#endif

    /* check filename in case of an image number is expected */
    if (fmt->flags & AVFMT_NEEDNUMBER) {
        if (filename_number_test(filename) < 0) { 
            err = AVERROR_NUMEXPECTED;
            goto fail;
        }
    }
    err = av_open_input_stream(ic_ptr, pb, filename, fmt, ap);
    if (err)
        goto fail;
    return 0;
 fail:
    if (file_opened)
        url_fclose(pb);
    *ic_ptr = NULL;
    return err;
    
}

/*******************************************************/

/**
 * Read a transport packet from a media file. This function is
 * absolete and should never be used. Use av_read_frame() instead.
 * 
 * @param s media file handle
 * @param pkt is filled 
 * @return 0 if OK. AVERROR_xxx if error.  
 */
int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return s->iformat->read_packet(s, pkt);
}

/**********************************************************/

/* get the number of samples of an audio frame. Return (-1) if error */
static int get_audio_frame_size(AVCodecContext *enc, int size)
{
    int frame_size;

    if (enc->frame_size <= 1) {
        /* specific hack for pcm codecs because no frame size is
           provided */
        switch(enc->codec_id) {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            if (enc->channels == 0)
                return -1;
            frame_size = size / (2 * enc->channels);
            break;
        case CODEC_ID_PCM_S8:
        case CODEC_ID_PCM_U8:
        case CODEC_ID_PCM_MULAW:
        case CODEC_ID_PCM_ALAW:
            if (enc->channels == 0)
                return -1;
            frame_size = size / (enc->channels);
            break;
        default:
            /* used for example by ADPCM codecs */
            if (enc->bit_rate == 0)
                return -1;
            frame_size = (size * 8 * enc->sample_rate) / enc->bit_rate;
            break;
        }
    } else {
        frame_size = enc->frame_size;
    }
    return frame_size;
}


/* return the frame duration in seconds, return 0 if not available */
static void compute_frame_duration(int *pnum, int *pden, AVStream *st, 
                                   AVCodecParserContext *pc, AVPacket *pkt)
{
    int frame_size;

    *pnum = 0;
    *pden = 0;
    switch(st->codec.codec_type) {
    case CODEC_TYPE_VIDEO:
        *pnum = st->codec.frame_rate_base;
        *pden = st->codec.frame_rate;
        if (pc && pc->repeat_pict) {
            *pden *= 2;
            *pnum = (*pnum) * (2 + pc->repeat_pict);
        }
        break;
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(&st->codec, pkt->size);
        if (frame_size < 0)
            break;
        *pnum = frame_size;
        *pden = st->codec.sample_rate;
        break;
    default:
        break;
    }
}

static int64_t lsb2full(int64_t lsb, int64_t last_ts, int lsb_bits){
    int64_t mask = lsb_bits < 64 ? (1LL<<lsb_bits)-1 : -1LL;
    int64_t delta= last_ts - mask/2;
    return  ((lsb - delta)&mask) + delta;
}

static void compute_pkt_fields(AVFormatContext *s, AVStream *st, 
                               AVCodecParserContext *pc, AVPacket *pkt)
{
    int num, den, presentation_delayed;

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

    /* do we have a video B frame ? */
    presentation_delayed = 0;
    if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
        /* XXX: need has_b_frame, but cannot get it if the codec is
           not initialized */
        if ((st->codec.codec_id == CODEC_ID_MPEG1VIDEO ||
             st->codec.codec_id == CODEC_ID_MPEG2VIDEO ||
             st->codec.codec_id == CODEC_ID_MPEG4 ||
             st->codec.codec_id == CODEC_ID_H264) && 
            pc && pc->pict_type != FF_B_TYPE)
            presentation_delayed = 1;
        /* this may be redundant, but it shouldnt hurt */
        if(pkt->dts != AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE && pkt->pts > pkt->dts)
            presentation_delayed = 1;
    }
    
    if(st->cur_dts == AV_NOPTS_VALUE){
        if(presentation_delayed) st->cur_dts = -pkt->duration;
        else                     st->cur_dts = 0;
    }

//    av_log(NULL, AV_LOG_DEBUG, "IN delayed:%d pts:%lld, dts:%lld cur_dts:%lld st:%d pc:%p\n", presentation_delayed, pkt->pts, pkt->dts, st->cur_dts, pkt->stream_index, pc);
    /* interpolate PTS and DTS if they are not present */
    if (presentation_delayed) {
        /* DTS = decompression time stamp */
        /* PTS = presentation time stamp */
        if (pkt->dts == AV_NOPTS_VALUE) {
            /* if we know the last pts, use it */
            if(st->last_IP_pts != AV_NOPTS_VALUE)
                st->cur_dts = pkt->dts = st->last_IP_pts;
            else
                pkt->dts = st->cur_dts;
        } else {
            st->cur_dts = pkt->dts;
        }
        /* this is tricky: the dts must be incremented by the duration
           of the frame we are displaying, i.e. the last I or P frame */
        if (st->last_IP_duration == 0)
            st->cur_dts += pkt->duration;
        else
            st->cur_dts += st->last_IP_duration;
        st->last_IP_duration  = pkt->duration;
        st->last_IP_pts= pkt->pts;
        /* cannot compute PTS if not present (we can compute it only
           by knowing the futur */
    } else {
        /* presentation is not delayed : PTS and DTS are the same */
        if (pkt->pts == AV_NOPTS_VALUE) {
            if (pkt->dts == AV_NOPTS_VALUE) {
                pkt->pts = st->cur_dts;
                pkt->dts = st->cur_dts;
            }
            else {
                st->cur_dts = pkt->dts;
                pkt->pts = pkt->dts;
            }
        } else {
            st->cur_dts = pkt->pts;
            pkt->dts = pkt->pts;
        }
        st->cur_dts += pkt->duration;
    }
//    av_log(NULL, AV_LOG_DEBUG, "OUTdelayed:%d pts:%lld, dts:%lld cur_dts:%lld\n", presentation_delayed, pkt->pts, pkt->dts, st->cur_dts);
    
    /* update flags */
    if (pc) {
        pkt->flags = 0;
        /* key frame computation */
        switch(st->codec.codec_type) {
        case CODEC_TYPE_VIDEO:
            if (pc->pict_type == FF_I_TYPE)
                pkt->flags |= PKT_FLAG_KEY;
            break;
        case CODEC_TYPE_AUDIO:
            pkt->flags |= PKT_FLAG_KEY;
            break;
        default:
            break;
        }
    }

    /* convert the packet time stamp units */
    if(pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale(pkt->pts, AV_TIME_BASE * (int64_t)st->time_base.num, st->time_base.den);
    if(pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale(pkt->dts, AV_TIME_BASE * (int64_t)st->time_base.num, st->time_base.den);

    /* duration field */
    pkt->duration = av_rescale(pkt->duration, AV_TIME_BASE * (int64_t)st->time_base.num, st->time_base.den);
}

static void av_destruct_packet_nofree(AVPacket *pkt)
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
            if (!st->parser) {
                /* no parsing needed: we just output the packet as is */
                /* raw data support */
                *pkt = s->cur_pkt;
                compute_pkt_fields(s, st, NULL, pkt);
                s->cur_st = NULL;
                return 0;
            } else if (s->cur_len > 0) {
                len = av_parser_parse(st->parser, &st->codec, &pkt->data, &pkt->size, 
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
                    return 0;
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
                if (ret == -EAGAIN)
                    return ret;
                /* return the last frames, if any */
                for(i = 0; i < s->nb_streams; i++) {
                    st = s->streams[i];
                    if (st->parser) {
                        av_parser_parse(st->parser, &st->codec, 
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

            s->cur_st = st;
            s->cur_ptr = s->cur_pkt.data;
            s->cur_len = s->cur_pkt.size;
            if (st->need_parsing && !st->parser) {
                st->parser = av_parser_init(st->codec.codec_id);
                if (!st->parser) {
                    /* no parser available : just output the raw packets */
                    st->need_parsing = 0;
                }
            }
        }
    }
}

/**
 * Return the next frame of a stream. The returned packet is valid
 * until the next av_read_frame() or until av_close_input_file() and
 * must be freed with av_free_packet. For video, the packet contains
 * exactly one frame. For audio, it contains an integer number of
 * frames if each frame has a known fixed size (e.g. PCM or ADPCM
 * data). If the audio frames have a variable size (e.g. MPEG audio),
 * then it contains one frame.
 * 
 * pkt->pts, pkt->dts and pkt->duration are always set to correct
 * values in AV_TIME_BASE unit (and guessed if the format cannot
 * provided them). pkt->pts can be AV_NOPTS_VALUE if the video format
 * has B frames, so it is better to rely on pkt->dts if you do not
 * decompress the payload.
 * 
 * Return 0 if OK, < 0 if error or end of file.  
 */
int av_read_frame(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl;

    pktl = s->packet_buffer;
    if (pktl) {
        /* read packet from packet buffer, if there is data */
        *pkt = pktl->pkt;
        s->packet_buffer = pktl->next;
        av_free(pktl);
        return 0;
    } else {
        return av_read_frame_internal(s, pkt);
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
        if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
            return i;
        }
    }
    return 0;
}

/* flush the frame reader */
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
        st->cur_dts = 0; /* we set the current DTS to an unspecified origin */
    }
}

/**
 * add a index entry into a sorted list updateing if it is already there.
 * @param timestamp timestamp in the timebase of the given stream
 */
int av_add_index_entry(AVStream *st,
                            int64_t pos, int64_t timestamp, int distance, int flags)
{
    AVIndexEntry *entries, *ie;
    int index;
    
    entries = av_fast_realloc(st->index_entries,
                              &st->index_entries_allocated_size,
                              (st->nb_index_entries + 1) * 
                              sizeof(AVIndexEntry));
    st->index_entries= entries;

    if(st->nb_index_entries){
        index= av_index_search_timestamp(st, timestamp);
        ie= &entries[index];

        if(ie->timestamp != timestamp){
            if(ie->timestamp < timestamp){
                index++; //index points to next instead of previous entry, maybe nonexistant
                ie= &st->index_entries[index];
            }else
                assert(index==0);
                
            if(index != st->nb_index_entries){
                assert(index < st->nb_index_entries);
                memmove(entries + index + 1, entries + index, sizeof(AVIndexEntry)*(st->nb_index_entries - index));
            }
            st->nb_index_entries++;
        }else{
            if(ie->pos == pos && distance < ie->min_distance) //dont reduce the distance
                distance= ie->min_distance;
        }
    }else{
        index= st->nb_index_entries++;
        ie= &entries[index];
    }
    
    ie->pos = pos;
    ie->timestamp = timestamp;
    ie->min_distance= distance;
    ie->flags = flags;
    
    return index;
}

/* build an index for raw streams using a parser */
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
            int64_t dts= av_rescale(pkt->dts, st->time_base.den, AV_TIME_BASE*(int64_t)st->time_base.num);
            av_add_index_entry(st, st->parser->frame_offset, dts, 
                            0, AVINDEX_KEYFRAME);
        }
        av_free_packet(pkt);
    }
}

/* return TRUE if we deal with a raw stream (raw codec data and
   parsing needed) */
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

/* return the largest index entry whose timestamp is <=
   wanted_timestamp */
int av_index_search_timestamp(AVStream *st, int wanted_timestamp)
{
    AVIndexEntry *entries= st->index_entries;
    int nb_entries= st->nb_index_entries;
    int a, b, m;
    int64_t timestamp;

    if (nb_entries <= 0)
        return -1;
    
    a = 0;
    b = nb_entries - 1;

    while (a < b) {
        m = (a + b + 1) >> 1;
        timestamp = entries[m].timestamp;
        if (timestamp > wanted_timestamp) {
            b = m - 1;
        } else {
            a = m;
        }
    }
    return a;
}

#define DEBUG_SEEK

/**
 * Does a binary search using av_index_search_timestamp() and AVCodec.read_timestamp().
 * this isnt supposed to be called directly by a user application, but by demuxers
 * @param target_ts target timestamp in the time base of the given stream
 * @param stream_index stream number
 */
int av_seek_frame_binary(AVFormatContext *s, int stream_index, int64_t target_ts){
    AVInputFormat *avif= s->iformat;
    int64_t pos_min, pos_max, pos, pos_limit;
    int64_t ts_min, ts_max, ts;
    int64_t start_pos;
    int index, no_change, i;
    AVStream *st;

    if (stream_index < 0)
        return -1;
    
#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "read_seek: %d %lld\n", stream_index, target_ts);
#endif

    ts_max=
    ts_min= AV_NOPTS_VALUE;
    pos_limit= -1; //gcc falsely says it may be uninitalized

    st= s->streams[stream_index];
    if(st->index_entries){
        AVIndexEntry *e;

        index= av_index_search_timestamp(st, target_ts);
        e= &st->index_entries[index];

        if(e->timestamp <= target_ts || e->pos == e->min_distance){
            pos_min= e->pos;
            ts_min= e->timestamp;
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "using cached pos_min=0x%llx dts_min=%lld\n", 
               pos_min,ts_min);
#endif
        }else{
            assert(index==0);
        }
        index++;
        if(index < st->nb_index_entries){
            e= &st->index_entries[index];
            assert(e->timestamp >= target_ts);
            pos_max= e->pos;
            ts_max= e->timestamp;
            pos_limit= pos_max - e->min_distance;
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "using cached pos_max=0x%llx pos_limit=0x%llx dts_max=%lld\n", 
               pos_max,pos_limit, ts_max);
#endif
        }
    }

    if(ts_min == AV_NOPTS_VALUE){
        pos_min = s->data_offset;
        ts_min = avif->read_timestamp(s, stream_index, &pos_min, INT64_MAX);
        if (ts_min == AV_NOPTS_VALUE)
            return -1;
    }

    if(ts_max == AV_NOPTS_VALUE){
        int step= 1024;
        pos_max = url_filesize(url_fileno(&s->pb)) - 1;
        do{
            pos_max -= step;
            ts_max = avif->read_timestamp(s, stream_index, &pos_max, pos_max + step);
            step += step;
        }while(ts_max == AV_NOPTS_VALUE && pos_max >= step);
        if (ts_max == AV_NOPTS_VALUE)
            return -1;
        
        for(;;){
            int64_t tmp_pos= pos_max + 1;
            int64_t tmp_ts= avif->read_timestamp(s, stream_index, &tmp_pos, INT64_MAX);
            if(tmp_ts == AV_NOPTS_VALUE)
                break;
            ts_max= tmp_ts;
            pos_max= tmp_pos;
        }
        pos_limit= pos_max;
    }

    no_change=0;
    while (pos_min < pos_limit) {
#ifdef DEBUG_SEEK
        av_log(s, AV_LOG_DEBUG, "pos_min=0x%llx pos_max=0x%llx dts_min=%lld dts_max=%lld\n", 
               pos_min, pos_max,
               ts_min, ts_max);
#endif
        assert(pos_limit <= pos_max);

        if(no_change==0){
            int64_t approximate_keyframe_distance= pos_max - pos_limit;
            // interpolate position (better than dichotomy)
            pos = (int64_t)((double)(pos_max - pos_min) *
                            (double)(target_ts - ts_min) /
                            (double)(ts_max - ts_min)) + pos_min - approximate_keyframe_distance;
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

        ts = avif->read_timestamp(s, stream_index, &pos, INT64_MAX); //may pass pos_limit instead of -1
        if(pos == pos_max)
            no_change++;
        else
            no_change=0;
#ifdef DEBUG_SEEK
av_log(s, AV_LOG_DEBUG, "%Ld %Ld %Ld / %Ld %Ld %Ld target:%Ld limit:%Ld start:%Ld noc:%d\n", pos_min, pos, pos_max, ts_min, ts, ts_max, target_ts, pos_limit, start_pos, no_change);
#endif
        assert(ts != AV_NOPTS_VALUE);
        if (target_ts < ts) {
            pos_limit = start_pos - 1;
            pos_max = pos;
            ts_max = ts;
        } else {
            pos_min = pos;
            ts_min = ts;
            /* check if we are lucky */
            if (target_ts == ts)
                break;
        }
    }
    
    pos = pos_min;
#ifdef DEBUG_SEEK
    pos_min = pos;
    ts_min = avif->read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    pos_min++;
    ts_max = avif->read_timestamp(s, stream_index, &pos_min, INT64_MAX);
    av_log(s, AV_LOG_DEBUG, "pos=0x%llx %lld<=%lld<=%lld\n", 
           pos, ts_min, target_ts, ts_max);
#endif
    /* do the seek */
    url_fseek(&s->pb, pos, SEEK_SET);

    ts= av_rescale(ts_min, AV_TIME_BASE*(int64_t)st->time_base.num, st->time_base.den);
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        st->cur_dts = av_rescale(ts, st->time_base.den, AV_TIME_BASE*(int64_t)st->time_base.num);
    }

    return 0;
}

static int av_seek_frame_generic(AVFormatContext *s, 
                                 int stream_index, int64_t timestamp)
{
    int index, i;
    AVStream *st;
    AVIndexEntry *ie;

    if (!s->index_built) {
        if (is_raw_stream(s)) {
            av_build_index_raw(s);
        } else {
            return -1;
        }
        s->index_built = 1;
    }

    st = s->streams[stream_index];
    index = av_index_search_timestamp(st, timestamp);
    if (index < 0)
        return -1;

    /* now we have found the index, we can seek */
    ie = &st->index_entries[index];
    av_read_frame_flush(s);
    url_fseek(&s->pb, ie->pos, SEEK_SET);
    
    timestamp= av_rescale(ie->timestamp, AV_TIME_BASE*(int64_t)st->time_base.num, st->time_base.den);
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];

        st->cur_dts = av_rescale(timestamp, st->time_base.den, AV_TIME_BASE*(int64_t)st->time_base.num);
    }

    return 0;
}

/**
 * Seek to the key frame just before the frame at timestamp
 * 'timestamp' in 'stream_index'.
 * @param stream_index If stream_index is (-1), a default
 * stream is selected
 * @param timestamp timestamp in AV_TIME_BASE units
 * @return >= 0 on success
 */
int av_seek_frame(AVFormatContext *s, int stream_index, int64_t timestamp)
{
    int ret;
    AVStream *st;
    
    av_read_frame_flush(s);
    
    if(stream_index < 0){
        stream_index= av_find_default_stream_index(s);
        if(stream_index < 0)
            return -1;
    }
    st= s->streams[stream_index];

    timestamp = av_rescale(timestamp, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);

    /* first, we try the format specific seek */
    if (s->iformat->read_seek)
        ret = s->iformat->read_seek(s, stream_index, timestamp);
    else
        ret = -1;
    if (ret >= 0) {
        return 0;
    }

    if(s->iformat->read_timestamp)
        return av_seek_frame_binary(s, stream_index, timestamp);
    else
        return av_seek_frame_generic(s, stream_index, timestamp);
}

/*******************************************************/

/* return TRUE if the stream has accurate timings for at least one component */
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

/* estimate the stream timings from the one of each components. Also
   compute the global bitrate if possible */
static void av_update_stream_timings(AVFormatContext *ic)
{
    int64_t start_time, end_time, end_time1;
    int i;
    AVStream *st;

    start_time = MAXINT64;
    end_time = MININT64;
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE) {
            if (st->start_time < start_time)
                start_time = st->start_time;
            if (st->duration != AV_NOPTS_VALUE) {
                end_time1 = st->start_time + st->duration;
                if (end_time1 > end_time)
                    end_time = end_time1;
            }
        }
    }
    if (start_time != MAXINT64) {
        ic->start_time = start_time;
        if (end_time != MAXINT64) {
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
            st->start_time = ic->start_time;
            st->duration = ic->duration;
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
            bit_rate += st->codec.bit_rate;
        }
        ic->bit_rate = bit_rate;
    }

    /* if duration is already set, we believe it */
    if (ic->duration == AV_NOPTS_VALUE && 
        ic->bit_rate != 0 && 
        ic->file_size != 0)  {
        filesize = ic->file_size;
        if (filesize > 0) {
            duration = (int64_t)((8 * AV_TIME_BASE * (double)filesize) / (double)ic->bit_rate);
            for(i = 0; i < ic->nb_streams; i++) {
                st = ic->streams[i];
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
static void av_estimate_timings_from_pts(AVFormatContext *ic)
{
    AVPacket pkt1, *pkt = &pkt1;
    AVStream *st;
    int read_size, i, ret;
    int64_t start_time, end_time, end_time1;
    int64_t filesize, offset, duration;
    
    /* free previous packet */
    if (ic->cur_st && ic->cur_st->parser)
        av_free_packet(&ic->cur_pkt); 
    ic->cur_st = NULL;

    /* flush packet queue */
    flush_packet_queue(ic);

    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->parser) {
            av_parser_close(st->parser);
            st->parser= NULL;
        }
    }
    
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
                st->start_time = av_rescale(pkt->pts, st->time_base.num * (int64_t)AV_TIME_BASE, st->time_base.den);
        }
        av_free_packet(pkt);
    }

    /* we compute the minimum start_time and use it as default */
    start_time = MAXINT64;
    for(i = 0; i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->start_time != AV_NOPTS_VALUE &&
            st->start_time < start_time)
            start_time = st->start_time;
    }
    if (start_time != MAXINT64)
        ic->start_time = start_time;
    
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
            end_time = av_rescale(pkt->pts, st->time_base.num * (int64_t)AV_TIME_BASE, st->time_base.den);
            duration = end_time - st->start_time;
            if (duration > 0) {
                if (st->duration == AV_NOPTS_VALUE ||
                    st->duration < duration)
                    st->duration = duration;
            }
        }
        av_free_packet(pkt);
    }
    
    /* estimate total duration */
    end_time = MININT64;
    for(i = 0;i < ic->nb_streams; i++) {
        st = ic->streams[i];
        if (st->duration != AV_NOPTS_VALUE) {
            end_time1 = st->start_time + st->duration;
            if (end_time1 > end_time)
                end_time = end_time1;
        }
    }
    
    /* update start_time (new stream may have been created, so we do
       it at the end */
    if (ic->start_time != AV_NOPTS_VALUE) {
        for(i = 0; i < ic->nb_streams; i++) {
            st = ic->streams[i];
            if (st->start_time == AV_NOPTS_VALUE)
                st->start_time = ic->start_time;
        }
    }

    if (end_time != MININT64) {
        /* put dummy values for duration if needed */
        for(i = 0;i < ic->nb_streams; i++) {
            st = ic->streams[i];
            if (st->duration == AV_NOPTS_VALUE && 
                st->start_time != AV_NOPTS_VALUE)
                st->duration = end_time - st->start_time;
        }
        ic->duration = end_time - ic->start_time;
    }

    url_fseek(&ic->pb, 0, SEEK_SET);
}

static void av_estimate_timings(AVFormatContext *ic)
{
    URLContext *h;
    int64_t file_size;

    /* get the file size, if possible */
    if (ic->iformat->flags & AVFMT_NOFILE) {
        file_size = 0;
    } else {
        h = url_fileno(&ic->pb);
        file_size = url_filesize(h);
        if (file_size < 0)
            file_size = 0;
    }
    ic->file_size = file_size;

    if (ic->iformat == &mpegps_demux) {
        /* get accurate estimate from the PTSes */
        av_estimate_timings_from_pts(ic);
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
        val = enc->width;
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
    int got_picture, ret;
    AVFrame picture;
    
    codec = avcodec_find_decoder(st->codec.codec_id);
    if (!codec)
        return -1;
    ret = avcodec_open(&st->codec, codec);
    if (ret < 0)
        return ret;
    switch(st->codec.codec_type) {
    case CODEC_TYPE_VIDEO:
        ret = avcodec_decode_video(&st->codec, &picture, 
                                   &got_picture, (uint8_t *)data, size);
        break;
    case CODEC_TYPE_AUDIO:
        samples = av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
        if (!samples)
            goto fail;
        ret = avcodec_decode_audio(&st->codec, samples, 
                                   &got_picture, (uint8_t *)data, size);
        av_free(samples);
        break;
    default:
        break;
    }
 fail:
    avcodec_close(&st->codec);
    return ret;
}

/* absolute maximum size we read until we abort */
#define MAX_READ_SIZE        5000000

/* maximum duration until we stop analysing the stream */
#define MAX_STREAM_DURATION  ((int)(AV_TIME_BASE * 1.0))

/**
 * Read the beginning of a media file to get stream information. This
 * is useful for file formats with no headers such as MPEG. This
 * function also compute the real frame rate in case of mpeg2 repeat
 * frame mode.
 *
 * @param ic media file handle
 * @return >=0 if OK. AVERROR_xxx if error.  
 */
int av_find_stream_info(AVFormatContext *ic)
{
    int i, count, ret, read_size;
    AVStream *st;
    AVPacket pkt1, *pkt;
    AVPacketList *pktl=NULL, **ppktl;

    count = 0;
    read_size = 0;
    ppktl = &ic->packet_buffer;
    for(;;) {
        /* check if one codec still needs to be handled */
        for(i=0;i<ic->nb_streams;i++) {
            st = ic->streams[i];
            if (!has_codec_parameters(&st->codec))
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
        } else {
            /* we did not get all the codec info, but we read too much data */
            if (read_size >= MAX_READ_SIZE) {
                ret = count;
                break;
            }
        }

        /* NOTE: a new stream can be added there if no header in file
           (AVFMTCTX_NOHEADER) */
        ret = av_read_frame_internal(ic, &pkt1);
        if (ret < 0) {
            /* EOF or error */
            ret = -1; /* we could not have all the codec parameters before EOF */
            if ((ic->ctx_flags & AVFMTCTX_NOHEADER) &&
                i == ic->nb_streams)
                ret = 0;
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
        st->codec_info_duration += pkt->duration;
        if (pkt->duration != 0)
            st->codec_info_nb_frames++;

        /* if still no information, we try to open the codec and to
           decompress the frame. We try to avoid that in most cases as
           it takes longer and uses more memory. For MPEG4, we need to
           decompress for Quicktime. */
        if (!has_codec_parameters(&st->codec) &&
            (st->codec.codec_id == CODEC_ID_FLV1 ||
             st->codec.codec_id == CODEC_ID_H264 ||
             st->codec.codec_id == CODEC_ID_H263 ||
             st->codec.codec_id == CODEC_ID_VORBIS ||
             st->codec.codec_id == CODEC_ID_MJPEG ||
             (st->codec.codec_id == CODEC_ID_MPEG4 && !st->need_parsing)))
            try_decode_frame(st, pkt->data, pkt->size);
        
        if (st->codec_info_duration >= MAX_STREAM_DURATION) {
            break;
        }
        count++;
    }

    /* set real frame rate info */
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
            /* compute the real frame rate for telecine */
            if ((st->codec.codec_id == CODEC_ID_MPEG1VIDEO ||
                 st->codec.codec_id == CODEC_ID_MPEG2VIDEO) &&
                st->codec.sub_id == 2) {
                if (st->codec_info_nb_frames >= 20) {
                    float coded_frame_rate, est_frame_rate;
                    est_frame_rate = ((double)st->codec_info_nb_frames * AV_TIME_BASE) / 
                        (double)st->codec_info_duration ;
                    coded_frame_rate = (double)st->codec.frame_rate /
                        (double)st->codec.frame_rate_base;
#if 0
                    printf("telecine: coded_frame_rate=%0.3f est_frame_rate=%0.3f\n", 
                           coded_frame_rate, est_frame_rate);
#endif
                    /* if we detect that it could be a telecine, we
                       signal it. It would be better to do it at a
                       higher level as it can change in a film */
                    if (coded_frame_rate >= 24.97 && 
                        (est_frame_rate >= 23.5 && est_frame_rate < 24.5)) {
                        st->r_frame_rate = 24024;
                        st->r_frame_rate_base = 1001;
                    }
                }
            }
            /* if no real frame rate, use the codec one */
            if (!st->r_frame_rate){
                st->r_frame_rate      = st->codec.frame_rate;
                st->r_frame_rate_base = st->codec.frame_rate_base;
            }
        }
    }

    av_estimate_timings(ic);
#if 0
    /* correct DTS for b frame streams with no timestamps */
    for(i=0;i<ic->nb_streams;i++) {
        st = ic->streams[i];
        if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
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
    return ret;
}

/*******************************************************/

/**
 * start playing a network based stream (e.g. RTSP stream) at the
 * current position 
 */
int av_read_play(AVFormatContext *s)
{
    if (!s->iformat->read_play)
        return AVERROR_NOTSUPP;
    return s->iformat->read_play(s);
}

/**
 * pause a network based stream (e.g. RTSP stream). Use av_read_play()
 * to resume it.
 */
int av_read_pause(AVFormatContext *s)
{
    if (!s->iformat->read_pause)
        return AVERROR_NOTSUPP;
    return s->iformat->read_pause(s);
}

/**
 * Close a media file (but not its codecs)
 *
 * @param s media file handle
 */
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

/**
 * Add a new stream to a media file. Can only be called in the
 * read_header function. If the flag AVFMTCTX_NOHEADER is in the
 * format context, then new streams can be added in read_packet too.
 *
 *
 * @param s media file handle
 * @param id file format dependent stream id 
 */
AVStream *av_new_stream(AVFormatContext *s, int id)
{
    AVStream *st;

    if (s->nb_streams >= MAX_STREAMS)
        return NULL;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return NULL;
    avcodec_get_context_defaults(&st->codec);
    if (s->iformat) {
        /* no default bitrate if decoding */
        st->codec.bit_rate = 0;
    }
    st->index = s->nb_streams;
    st->id = id;
    st->start_time = AV_NOPTS_VALUE;
    st->duration = AV_NOPTS_VALUE;
    st->cur_dts = AV_NOPTS_VALUE;

    /* default pts settings is MPEG like */
    av_set_pts_info(st, 33, 1, 90000);
    st->last_IP_pts = AV_NOPTS_VALUE;

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

/**
 * allocate the stream private data and write the stream header to an
 * output media file
 *
 * @param s media file handle
 * @return 0 if OK. AVERROR_xxx if error.  
 */
int av_write_header(AVFormatContext *s)
{
    int ret, i;
    AVStream *st;

    ret = s->oformat->write_header(s);
    if (ret < 0)
        return ret;

    /* init PTS generation */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];

        switch (st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            av_frac_init(&st->pts, 0, 0, 
                         (int64_t)st->time_base.num * st->codec.sample_rate);
            break;
        case CODEC_TYPE_VIDEO:
            av_frac_init(&st->pts, 0, 0, 
                         (int64_t)st->time_base.num * st->codec.frame_rate);
            break;
        default:
            break;
        }
    }
    return 0;
}

//FIXME merge with compute_pkt_fields
static void compute_pkt_fields2(AVStream *st, AVPacket *pkt){
    int b_frames = FFMAX(st->codec.has_b_frames, st->codec.max_b_frames);
    int num, den, frame_size;

//    av_log(NULL, AV_LOG_DEBUG, "av_write_frame: pts:%lld dts:%lld cur_dts:%lld b:%d size:%d st:%d\n", pkt->pts, pkt->dts, st->cur_dts, b_frames, pkt->size, pkt->stream_index);
    
/*    if(pkt->pts == AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE)
        return -1;*/
            
    if(pkt->pts != AV_NOPTS_VALUE)
        pkt->pts = av_rescale(pkt->pts, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    if(pkt->dts != AV_NOPTS_VALUE)
        pkt->dts = av_rescale(pkt->dts, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);

    /* duration field */
    pkt->duration = av_rescale(pkt->duration, st->time_base.den, AV_TIME_BASE * (int64_t)st->time_base.num);
    if (pkt->duration == 0) {
        compute_frame_duration(&num, &den, st, NULL, pkt);
        if (den && num) {
            pkt->duration = av_rescale(1, num * (int64_t)st->time_base.den, den * (int64_t)st->time_base.num);
        }
    }

    //XXX/FIXME this is a temporary hack until all encoders output pts
    if((pkt->pts == 0 || pkt->pts == AV_NOPTS_VALUE) && pkt->dts == AV_NOPTS_VALUE && !b_frames){
        pkt->dts=
//        pkt->pts= st->cur_dts;
        pkt->pts= st->pts.val;
    }

    //calculate dts from pts    
    if(pkt->pts != AV_NOPTS_VALUE && pkt->dts == AV_NOPTS_VALUE){
        if(b_frames){
            if(st->last_IP_pts == AV_NOPTS_VALUE){
                st->last_IP_pts= -pkt->duration;
            }
            if(st->last_IP_pts < pkt->pts){
                pkt->dts= st->last_IP_pts;
                st->last_IP_pts= pkt->pts;
            }else
                pkt->dts= pkt->pts;
        }else
            pkt->dts= pkt->pts;
    }
    
//    av_log(NULL, AV_LOG_DEBUG, "av_write_frame: pts2:%lld dts2:%lld\n", pkt->pts, pkt->dts);
    st->cur_dts= pkt->dts;
    st->pts.val= pkt->dts;

    /* update pts */
    switch (st->codec.codec_type) {
    case CODEC_TYPE_AUDIO:
        frame_size = get_audio_frame_size(&st->codec, pkt->size);

        /* HACK/FIXME, we skip the initial 0-size packets as they are most likely equal to the encoder delay,
           but it would be better if we had the real timestamps from the encoder */
        if (frame_size >= 0 && (pkt->size || st->pts.num!=st->pts.den>>1 || st->pts.val)) {
            av_frac_add(&st->pts, (int64_t)st->time_base.den * frame_size);
        }
        break;
    case CODEC_TYPE_VIDEO:
        av_frac_add(&st->pts, (int64_t)st->time_base.den * st->codec.frame_rate_base);
        break;
    default:
        break;
    }
}

static void truncate_ts(AVStream *st, AVPacket *pkt){
    int64_t pts_mask = (2LL << (st->pts_wrap_bits-1)) - 1;
    
    if(pkt->dts < 0)
        pkt->dts= 0;  //this happens for low_delay=0 and b frames, FIXME, needs further invstigation about what we should do here
    
    pkt->pts &= pts_mask;
    pkt->dts &= pts_mask;
}

/**
 * Write a packet to an output media file. The packet shall contain
 * one audio or video frame.
 *
 * @param s media file handle
 * @param pkt the packet, which contains the stream_index, buf/buf_size, dts/pts, ...
 * @return < 0 if error, = 0 if OK, 1 if end of stream wanted.
 */
int av_write_frame(AVFormatContext *s, AVPacket *pkt)
{
    compute_pkt_fields2(s->streams[pkt->stream_index], pkt);
    
    truncate_ts(s->streams[pkt->stream_index], pkt);

    return s->oformat->write_packet(s, pkt);
}

/**
 * Writes a packet to an output media file ensuring correct interleaving. 
 * The packet shall contain one audio or video frame.
 * If the packets are already correctly interleaved the application should
 * call av_write_frame() instead as its slightly faster, its also important
 * to keep in mind that non interlaved input will need huge amounts
 * of memory to interleave with this, so its prefereable to interleave at the
 * demuxer level
 *
 * @param s media file handle
 * @param pkt the packet, which contains the stream_index, buf/buf_size, dts/pts, ...
 * @return < 0 if error, = 0 if OK, 1 if end of stream wanted.
 */
int av_interleaved_write_frame(AVFormatContext *s, AVPacket *pkt){
    AVPacketList *pktl, **next_point, *this_pktl;
    int stream_count=0;
    int streams[MAX_STREAMS];
    AVStream *st= s->streams[ pkt->stream_index];

    compute_pkt_fields2(st, pkt);
    
    //FIXME/XXX/HACK drop zero sized packets
    if(st->codec.codec_type == CODEC_TYPE_AUDIO && pkt->size==0)
        return 0;

    if(pkt->dts == AV_NOPTS_VALUE)
        return -1;
        
    assert(pkt->destruct != av_destruct_packet); //FIXME

    this_pktl = av_mallocz(sizeof(AVPacketList));
    this_pktl->pkt= *pkt;
    av_dup_packet(&this_pktl->pkt);

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
    
    memset(streams, 0, sizeof(streams));
    pktl= s->packet_buffer;
    while(pktl){
//av_log(s, AV_LOG_DEBUG, "show st:%d dts:%lld\n", pktl->pkt.stream_index, pktl->pkt.dts);
        if(streams[ pktl->pkt.stream_index ] == 0)
            stream_count++;
        streams[ pktl->pkt.stream_index ]++;
        pktl= pktl->next;
    }

    while(s->nb_streams == stream_count){
        int ret;

        pktl= s->packet_buffer;
//av_log(s, AV_LOG_DEBUG, "write st:%d dts:%lld\n", pktl->pkt.stream_index, pktl->pkt.dts);
        truncate_ts(s->streams[pktl->pkt.stream_index], &pktl->pkt);
        ret= s->oformat->write_packet(s, &pktl->pkt);
        
        s->packet_buffer= pktl->next;        
        if((--streams[ pktl->pkt.stream_index ]) == 0)
            stream_count--;

        av_free_packet(&pktl->pkt);
        av_freep(&pktl);
        
        if(ret<0)
            return ret;
    }
    return 0;
}

/**
 * write the stream trailer to an output media file and and free the
 * file private data.
 *
 * @param s media file handle
 * @return 0 if OK. AVERROR_xxx if error.  */
int av_write_trailer(AVFormatContext *s)
{
    int ret;
    
    while(s->packet_buffer){
        int ret;
        AVPacketList *pktl= s->packet_buffer;

//av_log(s, AV_LOG_DEBUG, "write_trailer st:%d dts:%lld\n", pktl->pkt.stream_index, pktl->pkt.dts);
        truncate_ts(s->streams[pktl->pkt.stream_index], &pktl->pkt);
        ret= s->oformat->write_packet(s, &pktl->pkt);
        
        s->packet_buffer= pktl->next;        

        av_free_packet(&pktl->pkt);
        av_freep(&pktl);
        
        if(ret<0)
            return ret;
    }

    ret = s->oformat->write_trailer(s);
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

    av_log(NULL, AV_LOG_DEBUG, "%s #%d, %s, %s '%s':\n", 
            is_output ? "Output" : "Input",
            index, 
            is_output ? ic->oformat->name : ic->iformat->name, 
            is_output ? "to" : "from", url);
    if (!is_output) {
        av_log(NULL, AV_LOG_DEBUG, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            secs = ic->duration / AV_TIME_BASE;
            us = ic->duration % AV_TIME_BASE;
            mins = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            av_log(NULL, AV_LOG_DEBUG, "%02d:%02d:%02d.%01d", hours, mins, secs, 
                   (10 * us) / AV_TIME_BASE);
        } else {
            av_log(NULL, AV_LOG_DEBUG, "N/A");
        }
        av_log(NULL, AV_LOG_DEBUG, ", bitrate: ");
        if (ic->bit_rate) {
            av_log(NULL, AV_LOG_DEBUG,"%d kb/s", ic->bit_rate / 1000);
        } else {
            av_log(NULL, AV_LOG_DEBUG, "N/A");
        }
        av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st = ic->streams[i];
        avcodec_string(buf, sizeof(buf), &st->codec, is_output);
        av_log(NULL, AV_LOG_DEBUG, "  Stream #%d.%d", index, i);
        /* the pid is an important information, so we display it */
        /* XXX: add a generic system */
        if (is_output)
            flags = ic->oformat->flags;
        else
            flags = ic->iformat->flags;
        if (flags & AVFMT_SHOW_IDS) {
            av_log(NULL, AV_LOG_DEBUG, "[0x%x]", st->id);
        }
        av_log(NULL, AV_LOG_DEBUG, ": %s\n", buf);
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
        *frame_rate_base = DEFAULT_FRAME_RATE_BASE; //FIXME use av_d2q()
        *frame_rate = (int)(strtod(arg, 0) * (*frame_rate_base) + 0.5);
    }
    if (!*frame_rate || !*frame_rate_base)
        return -1;
    else
        return 0;
}

/* Syntax:
 * - If not a duration:
 *  [{YYYY-MM-DD|YYYYMMDD}]{T| }{HH[:MM[:SS[.m...]]][Z]|HH[MM[SS[.m...]]][Z]}
 * Time is localtime unless Z is suffixed to the end. In this case GMT
 * Return the date in micro seconds since 1970 
 * - If duration:
 *  HH[:MM[:SS[.m...]]]
 *  S+[.m...]
 */
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
            return now * int64_t_C(1000000);
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

/* syntax: '?tag1=val1&tag2=val2...'. Little URL decoding is done. Return
   1 if found */
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

/* Return in 'buf' the path with '%d' replaced by number. Also handles
   the '%0nd' format where 'n' is the total number of digits and
   '%%'. Return 0 if OK, and -1 if format error */
int get_frame_filename(char *buf, int buf_size,
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

/**
 * Print  nice hexa dump of a buffer
 * @param f stream for output
 * @param buf buffer
 * @param size buffer size
 */
void av_hex_dump(FILE *f, uint8_t *buf, int size)
{
    int len, i, j, c;

    for(i=0;i<size;i+=16) {
        len = size - i;
        if (len > 16)
            len = 16;
        fprintf(f, "%08x ", i);
        for(j=0;j<16;j++) {
            if (j < len)
                fprintf(f, " %02x", buf[i+j]);
            else
                fprintf(f, "   ");
        }
        fprintf(f, " ");
        for(j=0;j<len;j++) {
            c = buf[i+j];
            if (c < ' ' || c > '~')
                c = '.';
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
}

/**
 * Print on 'f' a nice dump of a packet
 * @param f stream for output
 * @param pkt packet to dump
 * @param dump_payload true if the payload must be displayed too
 */
void av_pkt_dump(FILE *f, AVPacket *pkt, int dump_payload)
{
    fprintf(f, "stream #%d:\n", pkt->stream_index);
    fprintf(f, "  keyframe=%d\n", ((pkt->flags & PKT_FLAG_KEY) != 0));
    fprintf(f, "  duration=%0.3f\n", (double)pkt->duration / AV_TIME_BASE);
    /* DTS is _always_ valid after av_read_frame() */
    fprintf(f, "  dts=");
    if (pkt->dts == AV_NOPTS_VALUE)
        fprintf(f, "N/A");
    else
        fprintf(f, "%0.3f", (double)pkt->dts / AV_TIME_BASE);
    /* PTS may be not known if B frames are present */
    fprintf(f, "  pts=");
    if (pkt->pts == AV_NOPTS_VALUE)
        fprintf(f, "N/A");
    else
        fprintf(f, "%0.3f", (double)pkt->pts / AV_TIME_BASE);
    fprintf(f, "\n");
    fprintf(f, "  size=%d\n", pkt->size);
    if (dump_payload)
        av_hex_dump(f, pkt->data, pkt->size);
}

void url_split(char *proto, int proto_size,
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
    if (*p == '\0') {
        if (proto_size > 0)
            proto[0] = '\0';
        if (hostname_size > 0)
            hostname[0] = '\0';
        p = url;
    } else {
        p++;
        if (*p == '/')
            p++;
        if (*p == '/')
            p++;
        q = hostname;
        while (*p != ':' && *p != '/' && *p != '?' && *p != '\0') {
            if ((q - hostname) < hostname_size - 1)
                *q++ = *p;
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

/**
 * Set the pts for a given stream
 * @param s stream 
 * @param pts_wrap_bits number of bits effectively used by the pts
 *        (used for wrap control, 33 is the value for MPEG) 
 * @param pts_num numerator to convert to seconds (MPEG: 1) 
 * @param pts_den denominator to convert to seconds (MPEG: 90000)
 */
void av_set_pts_info(AVStream *s, int pts_wrap_bits,
                     int pts_num, int pts_den)
{
    s->pts_wrap_bits = pts_wrap_bits;
    s->time_base.num = pts_num;
    s->time_base.den = pts_den;
}

/* fraction handling */

/**
 * f = val + (num / den) + 0.5. 'num' is normalized so that it is such
 * as 0 <= num < den.
 *
 * @param f fractional number
 * @param val integer value
 * @param num must be >= 0
 * @param den must be >= 1 
 */
void av_frac_init(AVFrac *f, int64_t val, int64_t num, int64_t den)
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

/* set f to (val + 0.5) */
void av_frac_set(AVFrac *f, int64_t val)
{
    f->val = val;
    f->num = f->den >> 1;
}

/**
 * Fractionnal addition to f: f = f + (incr / f->den)
 *
 * @param f fractional number
 * @param incr increment, can be positive or negative
 */
void av_frac_add(AVFrac *f, int64_t incr)
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

/**
 * register a new image format
 * @param img_fmt Image format descriptor
 */
void av_register_image_format(AVImageFormat *img_fmt)
{
    AVImageFormat **p;

    p = &first_image_format;
    while (*p != NULL) p = &(*p)->next;
    *p = img_fmt;
    img_fmt->next = NULL;
}

/* guess image format */
AVImageFormat *av_probe_image_format(AVProbeData *pd)
{
    AVImageFormat *fmt1, *fmt;
    int score, score_max;

    fmt = NULL;
    score_max = 0;
    for(fmt1 = first_image_format; fmt1 != NULL; fmt1 = fmt1->next) {
        if (fmt1->img_probe) {
            score = fmt1->img_probe(pd);
            if (score > score_max) {
                score_max = score;
                fmt = fmt1;
            }
        }
    }
    return fmt;
}

AVImageFormat *guess_image_format(const char *filename)
{
    AVImageFormat *fmt1;

    for(fmt1 = first_image_format; fmt1 != NULL; fmt1 = fmt1->next) {
        if (fmt1->extensions && match_ext(filename, fmt1->extensions))
            return fmt1;
    }
    return NULL;
}

/**
 * Read an image from a stream. 
 * @param gb byte stream containing the image
 * @param fmt image format, NULL if probing is required
 */
int av_read_image(ByteIOContext *pb, const char *filename,
                  AVImageFormat *fmt,
                  int (*alloc_cb)(void *, AVImageInfo *info), void *opaque)
{
    char buf[PROBE_BUF_SIZE];
    AVProbeData probe_data, *pd = &probe_data;
    offset_t pos;
    int ret;

    if (!fmt) {
        pd->filename = filename;
        pd->buf = buf;
        pos = url_ftell(pb);
        pd->buf_size = get_buffer(pb, buf, PROBE_BUF_SIZE);
        url_fseek(pb, pos, SEEK_SET);
        fmt = av_probe_image_format(pd);
    }
    if (!fmt)
        return AVERROR_NOFMT;
    ret = fmt->img_read(pb, alloc_cb, opaque);
    return ret;
}

/**
 * Write an image to a stream.
 * @param pb byte stream for the image output
 * @param fmt image format
 * @param img image data and informations
 */
int av_write_image(ByteIOContext *pb, AVImageFormat *fmt, AVImageInfo *img)
{
    return fmt->img_write(pb, img);
}

