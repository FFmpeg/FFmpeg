/*
 * Various utilities for ffmpeg system
 * Copyright (c) 2000,2001 Gerard Lantau
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "avformat.h"

AVFormat *first_format;

void register_avformat(AVFormat *format)
{
    AVFormat **p;
    p = &first_format;
    while (*p != NULL) p = &(*p)->next;
    *p = format;
    format->next = NULL;
}

int match_ext(const char *filename, const char *extensions)
{
    const char *ext, *p;
    char ext1[32], *q;

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

AVFormat *guess_format(const char *short_name, const char *filename, const char *mime_type)
{
    AVFormat *fmt, *fmt_found;
    int score_max, score;

    /* find the proper file type */
    fmt_found = NULL;
    score_max = 0;
    fmt = first_format;
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

/* return TRUE if val is a prefix of str. If it returns TRUE, ptr is
   set to the next character in 'str' after the prefix */
int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;
    return 1;
}

void nstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)
            break;
        *q++ = c;
    }
    *q = '\0';
}

void register_all(void)
{
    avcodec_init();
    avcodec_register_all();

    register_avformat(&mp2_format);
    register_avformat(&ac3_format);
    register_avformat(&mpeg_mux_format);
    register_avformat(&mpeg1video_format);
    register_avformat(&h263_format);
    register_avformat(&rm_format);
    register_avformat(&asf_format);
    register_avformat(&avi_format);
    register_avformat(&mpjpeg_format);
    register_avformat(&jpeg_format);
    register_avformat(&swf_format);
    register_avformat(&wav_format);
    register_avformat(&pcm_format);
    register_avformat(&rawvideo_format);
    register_avformat(&ffm_format);
    register_avformat(&pgm_format);
    register_avformat(&ppm_format);
    register_avformat(&pgmyuv_format);
    register_avformat(&imgyuv_format);
    register_avformat(&pgmpipe_format);
    register_avformat(&pgmyuvpipe_format);
    register_avformat(&ppmpipe_format);

    register_protocol(&file_protocol);
    register_protocol(&pipe_protocol);
    register_protocol(&audio_protocol);
    register_protocol(&video_protocol);
    register_protocol(&udp_protocol);
    register_protocol(&http_protocol);
}

/* memory handling */

int av_new_packet(AVPacket *pkt, int size)
{
    pkt->data = malloc(size);
    if (!pkt->data)
        return -ENOMEM;
    pkt->size = size;
    /* sane state */
    pkt->pts = 0;
    pkt->stream_index = 0;
    pkt->flags = 0;
    return 0;
}

void av_free_packet(AVPacket *pkt)
{
    free(pkt->data);
    /* fail safe */
    pkt->data = NULL;
    pkt->size = 0;
}

/* fifo handling */

int fifo_init(FifoBuffer *f, int size)
{
    f->buffer = malloc(size);
    if (!f->buffer)
        return -1;
    f->end = f->buffer + size;
    f->wptr = f->rptr = f->buffer;
    return 0;
}

void fifo_free(FifoBuffer *f)
{
    free(f->buffer);
}

int fifo_size(FifoBuffer *f, UINT8 *rptr)
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
int fifo_read(FifoBuffer *f, UINT8 *buf, int buf_size, UINT8 **rptr_ptr)
{
    UINT8 *rptr = *rptr_ptr;
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

void fifo_write(FifoBuffer *f, UINT8 *buf, int size, UINT8 **wptr_ptr)
{
    int len;
    UINT8 *wptr;
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

/* media file handling */

AVFormatContext *av_open_input_file(const char *filename, int buf_size)
{
    AVFormatParameters params, *ap;
    AVFormat *fmt;
    AVFormatContext *ic = NULL;
    URLFormat url_format;
    int err;

    ic = av_mallocz(sizeof(AVFormatContext));
    if (!ic)
        goto fail;
    if (url_fopen(&ic->pb, filename, URL_RDONLY) < 0)
        goto fail;
    
    if (buf_size > 0) {
        url_setbufsize(&ic->pb, buf_size);
    }

    /* find format */
    err = url_getformat(url_fileno(&ic->pb), &url_format);
    if (err >= 0) {
        fmt = guess_format(url_format.format_name, NULL, NULL);
        ap = &params;
        ap->sample_rate = url_format.sample_rate;
        ap->frame_rate = url_format.frame_rate;
        ap->channels = url_format.channels;
        ap->width = url_format.width;
        ap->height = url_format.height;
        ap->pix_fmt = url_format.pix_fmt;
    } else {
        fmt = guess_format(NULL, filename, NULL);
        ap = NULL;
    }
    if (!fmt || !fmt->read_header) {
        return NULL;
    }
    ic->format = fmt;

    err = ic->format->read_header(ic, ap);
    if (err < 0) {
        url_fclose(&ic->pb);
        goto fail;
    }
    
    return ic;

 fail:
    if (ic)
        free(ic);
    return NULL;
}

int av_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVPacketList *pktl;

    pktl = s->packet_buffer;
    if (pktl) {
        /* read packet from packet buffer, if there is data */
        *pkt = pktl->pkt;
        s->packet_buffer = pktl->next;
        free(pktl);
        return 0;
    } else {
        return s->format->read_packet(s, pkt);
    }
}

void av_close_input_file(AVFormatContext *s)
{
    int i;

    if (s->format->read_close)
        s->format->read_close(s);
    for(i=0;i<s->nb_streams;i++) {
        free(s->streams[i]);
    }
    if (s->packet_buffer) {
        AVPacketList *p, *p1;
        p = s->packet_buffer;
        while (p != NULL) {
            p1 = p->next;
            av_free_packet(&p->pkt);
            free(p);
            p = p1;
        }
        s->packet_buffer = NULL;
    }
    url_fclose(&s->pb);
    free(s);
}


int av_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    /* XXX: currently, an emulation because internal API must change */
    return s->format->write_packet(s, pkt->stream_index, pkt->data, pkt->size);
}

/* "user interface" functions */

void dump_format(AVFormatContext *ic,
                 int index, 
                 const char *url,
                 int is_output)
{
    int i;
    char buf[256];

    fprintf(stderr, "%s #%d, %s, %s '%s':\n", 
            is_output ? "Output" : "Input",
            index, ic->format->name, 
            is_output ? "to" : "from", url);
    for(i=0;i<ic->nb_streams;i++) {
        AVStream *st = ic->streams[i];
        avcodec_string(buf, sizeof(buf), &st->codec, is_output);
        fprintf(stderr, "  Stream #%d.%d: %s\n", index, i, buf);
    }
}

typedef struct {
    const char *str;
    int width, height;
} SizeEntry;

static SizeEntry sizes[] = {
    { "sqcif", 128, 96 },
    { "qcif", 176, 144 },
    { "cif", 352, 288 },
    { "4cif", 704, 576 },
};
    
int parse_image_size(int *width_ptr, int *height_ptr, const char *str)
{
    int i;
    int n = sizeof(sizes) / sizeof(SizeEntry);
    const char *p;
    int frame_width = 0, frame_height = 0;

    for(i=0;i<n;i++) {
        if (!strcmp(sizes[i].str, str)) {
            frame_width = sizes[i].width;
            frame_height = sizes[i].height;
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

INT64 gettime(void)
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (INT64)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* syntax: [YYYY-MM-DD ][[HH:]MM:]SS[.m...] . Return the date in micro seconds since 1970 */
INT64 parse_date(const char *datestr, int duration)
{
    const char *p;
    INT64 t;
    int sec;

    p = datestr;
    if (!duration) {
        static const UINT8 months[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        int year, month, day, i;

        if (strlen(p) >= 5 && p[4] == '-') {
            
            year = strtol(p, (char **)&p, 10);
            if (*p)
                p++;
            month = strtol(p, (char **)&p, 10) - 1;
            if (*p)
                p++;
            day = strtol(p, (char **)&p, 10) - 1;
            if (*p)
                p++;
            day += (year - 1970) * 365;
            /* if >= March, take February of current year into account too */
            if (month >= 2)
                year++;
            for(i=1970;i<year;i++) {
                if ((i % 100) == 0) {
                    if ((i % 400) == 0) day++;
                } else if ((i % 4) == 0) {
                    day++;
                }
            }
            for(i=0;i<month;i++)
                day += months[i];
        } else {
            day = (time(NULL) / (3600 * 24));
        }
        t = day * (3600 * 24);
    } else {
        t = 0;
    }
    
    sec = 0;
    for(;;) {
        int val;
        val = strtol(p, (char **)&p, 10);
        sec = sec * 60 + val;
        if (*p != ':')
            break;
        p++;
    }
    t = (t + sec) * 1000000;
    if (*p == '.') {
        int val, n;
        p++;
        n = strlen(p);
        if (n > 6)
            n = 6;
        val = strtol(p, NULL, 10);
        while (n < 6) {
            val = val * 10;
            n++;
        }
        t += val;
    }
    return t;
}

/* syntax: '?tag1=val1&tag2=val2...'. No URL decoding is done. Return
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
                if ((q - arg) < arg_size - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
        }
        if (!strcmp(tag, tag1)) 
            return 1;
        if (*p != '&')
            break;
    }
    return 0;
}

