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
#include "avformat.h"
#include "tick.h"
#ifndef CONFIG_WIN32
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#else
#define strcasecmp _stricmp
#include <sys/types.h>
#include <sys/timeb.h>
#endif

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
    register_avformat(&mjpeg_format);
    register_avformat(&h263_format);
    register_avformat(&rm_format);
    register_avformat(&asf_format);
    register_avformat(&avi_format);
    register_avformat(&mpjpeg_format);
    register_avformat(&jpeg_format);
    register_avformat(&single_jpeg_format);
    register_avformat(&swf_format);
    register_avformat(&wav_format);
    register_avformat(&pcm_s16le_format);
    register_avformat(&pcm_s16be_format);
    register_avformat(&pcm_u16le_format);
    register_avformat(&pcm_u16be_format);
    register_avformat(&pcm_s8_format);
    register_avformat(&pcm_u8_format);
    register_avformat(&pcm_mulaw_format);
    register_avformat(&pcm_alaw_format);
    register_avformat(&rawvideo_format);
#ifndef CONFIG_WIN32
    register_avformat(&ffm_format);
#endif
    register_avformat(&pgm_format);
    register_avformat(&ppm_format);
    register_avformat(&pgmyuv_format);
    register_avformat(&imgyuv_format);
    register_avformat(&pgmpipe_format);
    register_avformat(&pgmyuvpipe_format);
    register_avformat(&ppmpipe_format);
#ifdef CONFIG_GRAB
    register_avformat(&video_grab_device_format);
    register_avformat(&audio_device_format);
#endif

    /* file protocols */
    register_protocol(&file_protocol);
    register_protocol(&pipe_protocol);
#ifndef CONFIG_WIN32
    register_protocol(&udp_protocol);
    register_protocol(&http_protocol);
#endif
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

/* media file handling. 
   'filename' is the filename to open.
   'format_name' is used to force the file format (NULL if auto guess).
   'buf_size' is the optional buffer size (zero if default is OK).
   'ap' are additionnal parameters needed when opening the file (NULL if default).
*/

AVFormatContext *av_open_input_file(const char *filename, 
                                    const char *format_name,
                                    int buf_size,
                                    AVFormatParameters *ap)
{
    AVFormat *fmt;
    AVFormatContext *ic = NULL;
    int err;

    ic = av_mallocz(sizeof(AVFormatContext));
    if (!ic)
        goto fail;

    /* find format */
    if (format_name != NULL) {
        fmt = guess_format(format_name, NULL, NULL);
    } else {
        fmt = guess_format(NULL, filename, NULL);
    }
    if (!fmt || !fmt->read_header) {
        return NULL;
    }
    ic->format = fmt;

    /* if no file needed do not try to open one */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (url_fopen(&ic->pb, filename, URL_RDONLY) < 0)
            goto fail;
        if (buf_size > 0) {
            url_setbufsize(&ic->pb, buf_size);
        }
    }
    
    err = ic->format->read_header(ic, ap);
    if (err < 0) {
        if (!(fmt->flags & AVFMT_NOFILE)) {
            url_fclose(&ic->pb);
        }
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
    if (!(s->format->flags & AVFMT_NOFILE)) {
        url_fclose(&s->pb);
    }
    free(s);
}


int av_write_packet(AVFormatContext *s, AVPacket *pkt, int force_pts)
{
    /* XXX: currently, an emulation because internal API must change */
    return s->format->write_packet(s, pkt->stream_index, pkt->data, pkt->size, force_pts);
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
#ifdef CONFIG_WIN32
    struct _timeb tb;
    _ftime(&tb);
    return ((INT64)tb.time * INT64_C(1000) + (INT64)tb.millitm) * INT64_C(1000);
#else
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (INT64)tv.tv_sec * 1000000 + tv.tv_usec;
#endif
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

/* Return in 'buf' the path with '%d' replaced by number. Also handles
   the '%0nd' format where 'n' is the total number of digits and
   '%%'. Return 0 if OK, and -1 if format error */
int get_frame_filename(char *buf, int buf_size,
                       const char *path, int number)
{
    const char *p;
    char *q, buf1[20];
    int nd, len, c, percentd_found;

    q = buf;
    p = path;
    percentd_found = 0;
    for(;;) {
        c = *p++;
        if (c == '\0')
            break;
        if (c == '%') {
            nd = 0;
            while (*p >= '0' && *p <= '9') {
                nd = nd * 10 + *p++ - '0';
            }
            c = *p++;
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

static int gcd(INT64 a, INT64 b)
{
    INT64 c;

    while (1) {
        c = a % b;
        if (c == 0)
            return b;
        a = b;
        b = c;
    }
}

void ticker_init(Ticker *tick, INT64 inrate, INT64 outrate)
{
    int g;

    g = gcd(inrate, outrate);
    inrate /= g;
    outrate /= g;

    tick->value = -outrate/2;

    tick->inrate = inrate;
    tick->outrate = outrate;
    tick->div = tick->outrate / tick->inrate;
    tick->mod = tick->outrate % tick->inrate;
}
