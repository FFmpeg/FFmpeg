/*
 * Multiple format streaming server
 * Copyright (c) 2000,2001 Gerard Lantau.
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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>

#include "avformat.h"

/* maximum number of simultaneous HTTP connections */
#define HTTP_MAX_CONNECTIONS 2000

enum HTTPState {
    HTTPSTATE_WAIT_REQUEST,
    HTTPSTATE_SEND_HEADER,
    HTTPSTATE_SEND_DATA_HEADER,
    HTTPSTATE_SEND_DATA,
    HTTPSTATE_SEND_DATA_TRAILER,
    HTTPSTATE_RECEIVE_DATA,
    HTTPSTATE_WAIT_FEED,
};

const char *http_state[] = {
    "WAIT_REQUEST",
    "SEND_HEADER",
    "SEND_DATA_HEADER",
    "SEND_DATA",
    "SEND_DATA_TRAILER",
    "RECEIVE_DATA",
    "WAIT_FEED",
};

#define IOBUFFER_MAX_SIZE 16384

/* coef for exponential mean for bitrate estimation in statistics */
#define AVG_COEF 0.9

/* timeouts are in ms */
#define REQUEST_TIMEOUT (15 * 1000)
#define SYNC_TIMEOUT (10 * 1000)

/* context associated with one connection */
typedef struct HTTPContext {
    enum HTTPState state;
    int fd; /* socket file descriptor */
    struct sockaddr_in from_addr; /* origin */
    struct pollfd *poll_entry; /* used when polling */
    long timeout;
    UINT8 buffer[IOBUFFER_MAX_SIZE];
    UINT8 *buffer_ptr, *buffer_end;
    int http_error;
    struct HTTPContext *next;
    int got_key_frame[MAX_STREAMS]; /* for each type */
    INT64 data_count;
    /* feed input */
    int feed_fd;
    /* input format handling */
    AVFormatContext *fmt_in;
    /* output format handling */
    struct FFStream *stream;
    AVFormatContext fmt_ctx;
    int last_packet_sent; /* true if last data packet was sent */
} HTTPContext;

/* each generated stream is described here */
enum StreamType {
    STREAM_TYPE_LIVE,
    STREAM_TYPE_STATUS,
};

/* description of each stream of the ffserver.conf file */
typedef struct FFStream {
    enum StreamType stream_type;
    char filename[1024];     /* stream filename */
    struct FFStream *feed;
    AVFormat *fmt;
    int nb_streams;
    AVStream *streams[MAX_STREAMS];
    int feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    char feed_filename[1024]; /* file name of the feed storage, or
                                 input file name for a stream */
    struct FFStream *next;
    /* feed specific */
    int feed_opened;     /* true if someone if writing to feed */
    int is_feed;         /* true if it is a feed */
    INT64 feed_max_size;      /* maximum storage size */
    INT64 feed_write_index;   /* current write position in feed (it wraps round) */
    INT64 feed_size;          /* current size of feed */
    struct FFStream *next_feed;
} FFStream;

typedef struct FeedData {
    long long data_count;
    float avg_frame_size;   /* frame size averraged over last frames with exponential mean */
} FeedData;

struct sockaddr_in my_addr;
char logfilename[1024];
HTTPContext *first_http_ctx;
FFStream *first_feed;   /* contains only feeds */
FFStream *first_stream; /* contains all streams, including feeds */

static int handle_http(HTTPContext *c, long cur_time);
static int http_parse_request(HTTPContext *c);
static int http_send_data(HTTPContext *c);
static void compute_stats(HTTPContext *c);
static int open_input_stream(HTTPContext *c, const char *info);
static int http_start_receive_data(HTTPContext *c);
static int http_receive_data(HTTPContext *c);

int nb_max_connections;
int nb_connections;

static long gettime_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

static FILE *logfile = NULL;

static void http_log(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    
    if (logfile)
        vfprintf(logfile, fmt, ap);
    va_end(ap);
}

/* main loop of the http server */
static int http_server(struct sockaddr_in my_addr)
{
    int server_fd, tmp, ret;
    struct sockaddr_in from_addr;
    struct pollfd poll_table[HTTP_MAX_CONNECTIONS + 1], *poll_entry;
    HTTPContext *c, **cp;
    long cur_time;

    server_fd = socket(AF_INET,SOCK_STREAM,0);
    if (server_fd < 0) {
        perror ("socket");
        return -1;
    }
        
    tmp = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));

    if (bind (server_fd, (struct sockaddr *) &my_addr, sizeof (my_addr)) < 0) {
        perror ("bind");
        close(server_fd);
        return -1;
    }
  
    if (listen (server_fd, 5) < 0) {
        perror ("listen");
        close(server_fd);
        return -1;
    }

    http_log("ffserver started.\n");

    fcntl(server_fd, F_SETFL, O_NONBLOCK);
    first_http_ctx = NULL;
    nb_connections = 0;
    first_http_ctx = NULL;
    for(;;) {
        poll_entry = poll_table;
        poll_entry->fd = server_fd;
        poll_entry->events = POLLIN;
        poll_entry++;

        /* wait for events on each HTTP handle */
        c = first_http_ctx;
        while (c != NULL) {
            int fd;
            fd = c->fd;
            switch(c->state) {
            case HTTPSTATE_WAIT_REQUEST:
                c->poll_entry = poll_entry;
                poll_entry->fd = fd;
                poll_entry->events = POLLIN;
                poll_entry++;
                break;
            case HTTPSTATE_SEND_HEADER:
            case HTTPSTATE_SEND_DATA_HEADER:
            case HTTPSTATE_SEND_DATA:
            case HTTPSTATE_SEND_DATA_TRAILER:
                c->poll_entry = poll_entry;
                poll_entry->fd = fd;
                poll_entry->events = POLLOUT;
                poll_entry++;
                break;
            case HTTPSTATE_RECEIVE_DATA:
                c->poll_entry = poll_entry;
                poll_entry->fd = fd;
                poll_entry->events = POLLIN;
                poll_entry++;
                break;
            case HTTPSTATE_WAIT_FEED:
                /* need to catch errors */
                c->poll_entry = poll_entry;
                poll_entry->fd = fd;
                poll_entry->events = 0;
                poll_entry++;
                break;
            default:
                c->poll_entry = NULL;
                break;
            }
            c = c->next;
        }

        /* wait for an event on one connection. We poll at least every
           second to handle timeouts */
        do {
            ret = poll(poll_table, poll_entry - poll_table, 1000);
        } while (ret == -1);
        
        cur_time = gettime_ms();

        /* now handle the events */

        cp = &first_http_ctx;
        while ((*cp) != NULL) {
            c = *cp;
            if (handle_http (c, cur_time) < 0) {
                /* close and free the connection */
                close(c->fd);
                if (c->fmt_in)
                    av_close_input_file(c->fmt_in);
                *cp = c->next;
                free(c);
                nb_connections--;
            } else {
                cp = &c->next;
            }
        }

        /* new connection request ? */
        poll_entry = poll_table;
        if (poll_entry->revents & POLLIN) {
            int fd, len;

            len = sizeof(from_addr);
            fd = accept(server_fd, (struct sockaddr *)&from_addr, 
                        &len);
            if (fd >= 0) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                /* XXX: should output a warning page when coming
                   close to the connection limit */
                if (nb_connections >= nb_max_connections) {
                    close(fd);
                } else {
                    /* add a new connection */
                    c = av_mallocz(sizeof(HTTPContext));
                    c->next = first_http_ctx;
                    first_http_ctx = c;
                    c->fd = fd;
                    c->poll_entry = NULL;
                    c->from_addr = from_addr;
                    c->state = HTTPSTATE_WAIT_REQUEST;
                    c->buffer_ptr = c->buffer;
                    c->buffer_end = c->buffer + IOBUFFER_MAX_SIZE;
                    c->timeout = cur_time + REQUEST_TIMEOUT;
                    nb_connections++;
                }
            }
        }
        poll_entry++;
    }
}

static int handle_http(HTTPContext *c, long cur_time)
{
    int len;
    
    switch(c->state) {
    case HTTPSTATE_WAIT_REQUEST:
        /* timeout ? */
        if ((c->timeout - cur_time) < 0)
            return -1;
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;

        /* no need to read if no events */
        if (!(c->poll_entry->revents & POLLIN))
            return 0;
        /* read the data */
        len = read(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR)
                return -1;
        } else if (len == 0) {
            return -1;
        } else {
            /* search for end of request. XXX: not fully correct since garbage could come after the end */
            UINT8 *ptr;
            c->buffer_ptr += len;
            ptr = c->buffer_ptr;
            if ((ptr >= c->buffer + 2 && !memcmp(ptr-2, "\n\n", 2)) ||
                (ptr >= c->buffer + 4 && !memcmp(ptr-4, "\r\n\r\n", 4))) {
                /* request found : parse it and reply */
                if (http_parse_request(c) < 0)
                    return -1;
            } else if (ptr >= c->buffer_end) {
                /* request too long: cannot do anything */
                return -1;
            }
        }
        break;

    case HTTPSTATE_SEND_HEADER:
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;

        /* no need to read if no events */
        if (!(c->poll_entry->revents & POLLOUT))
            return 0;
        len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                /* error : close connection */
                return -1;
            }
        } else {
            c->buffer_ptr += len;
            if (c->buffer_ptr >= c->buffer_end) {
                /* if error, exit */
                if (c->http_error)
                    return -1;
                /* all the buffer was send : synchronize to the incoming stream */
                c->state = HTTPSTATE_SEND_DATA_HEADER;
                c->buffer_ptr = c->buffer_end = c->buffer;
            }
        }
        break;

    case HTTPSTATE_SEND_DATA:
    case HTTPSTATE_SEND_DATA_HEADER:
    case HTTPSTATE_SEND_DATA_TRAILER:
        /* no need to read if no events */
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;
        
        if (!(c->poll_entry->revents & POLLOUT))
            return 0;
        if (http_send_data(c) < 0)
            return -1;
        break;
    case HTTPSTATE_RECEIVE_DATA:
        /* no need to read if no events */
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;
        if (!(c->poll_entry->revents & POLLIN))
            return 0;
        if (http_receive_data(c) < 0)
            return -1;
        break;
    case HTTPSTATE_WAIT_FEED:
        /* no need to read if no events */
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;

        /* nothing to do, we'll be waken up by incoming feed packets */
        break;
    default:
        return -1;
    }
    return 0;
}

/* parse http request and prepare header */
static int http_parse_request(HTTPContext *c)
{
    char *p;
    int post;
    char cmd[32];
    char info[1024], *filename;
    char url[1024], *q;
    char protocol[32];
    char msg[1024];
    const char *mime_type;
    FFStream *stream;

    p = c->buffer;
    q = cmd;
    while (!isspace(*p) && *p != '\0') {
        if ((q - cmd) < sizeof(cmd) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (!strcmp(cmd, "GET"))
        post = 0;
    else if (!strcmp(cmd, "POST"))
        post = 1;
    else
        return -1;

    while (isspace(*p)) p++;
    q = url;
    while (!isspace(*p) && *p != '\0') {
        if ((q - url) < sizeof(url) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';

    while (isspace(*p)) p++;
    q = protocol;
    while (!isspace(*p) && *p != '\0') {
        if ((q - protocol) < sizeof(protocol) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (strcmp(protocol, "HTTP/1.0") && strcmp(protocol, "HTTP/1.1"))
        return -1;
    
    /* find the filename and the optional info string in the request */
    p = url;
    if (*p == '/')
        p++;
    filename = p;
    p = strchr(p, '?');
    if (p) {
        strcpy(info, p);
        *p = '\0';
    } else {
        info[0] = '\0';
    }

    stream = first_stream;
    while (stream != NULL) {
        if (!strcmp(stream->filename, filename))
            break;
        stream = stream->next;
    }
    if (stream == NULL) {
        sprintf(msg, "File '%s' not found", url);
        goto send_error;
    }
    c->stream = stream;
    
    /* should do it after so that the size can be computed */
    {
        char buf1[32], buf2[32], *p;
        time_t ti;
        /* XXX: reentrant function ? */
        p = inet_ntoa(c->from_addr.sin_addr);
        strcpy(buf1, p);
        ti = time(NULL);
        p = ctime(&ti);
        strcpy(buf2, p);
        p = buf2 + strlen(p) - 1;
        if (*p == '\n')
            *p = '\0';
        http_log("%s - - [%s] \"%s %s %s\" %d %d\n", 
                 buf1, buf2, cmd, url, protocol, 200, 1024);
    }

    /* XXX: add there authenticate and IP match */

    if (post) {
        /* if post, it means a feed is being sent */
        if (!stream->is_feed) {
            sprintf(msg, "POST command not handled");
            goto send_error;
        }
        if (http_start_receive_data(c) < 0) {
            sprintf(msg, "could not open feed");
            goto send_error;
        }
        c->http_error = 0;
        c->state = HTTPSTATE_RECEIVE_DATA;
        return 0;
    }

    if (c->stream->stream_type == STREAM_TYPE_STATUS)
        goto send_stats;

    /* open input stream */
    if (open_input_stream(c, info) < 0) {
        sprintf(msg, "Input stream corresponding to '%s' not found", url);
        goto send_error;
    }

    /* prepare http header */
    q = c->buffer;
    q += sprintf(q, "HTTP/1.0 200 OK\r\n");
    mime_type = c->stream->fmt->mime_type;
    if (!mime_type)
        mime_type = "application/x-octet_stream";
    q += sprintf(q, "Content-type: %s\r\n", mime_type);
    q += sprintf(q, "Pragma: no-cache\r\n");

    /* for asf, we need extra headers */
    if (!strcmp(c->stream->fmt->name,"asf")) {
        q += sprintf(q, "Pragma: features=broadcast\r\n");
    }
    q += sprintf(q, "\r\n");
    
    /* prepare output buffer */
    c->http_error = 0;
    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
 send_error:
    c->http_error = 404;
    q = c->buffer;
    q += sprintf(q, "HTTP/1.0 404 Not Found\r\n");
    q += sprintf(q, "Content-type: %s\r\n", "text/html");
    q += sprintf(q, "\r\n");
    q += sprintf(q, "<HTML>\n");
    q += sprintf(q, "<HEAD><TITLE>404 Not Found</TITLE></HEAD>\n");
    q += sprintf(q, "<BODY>%s</BODY>\n", msg);
    q += sprintf(q, "</HTML>\n");

    /* prepare output buffer */
    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
 send_stats:
    compute_stats(c);
    c->http_error = 200; /* horrible : we use this value to avoid
                            going to the send data state */
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
}

static void compute_stats(HTTPContext *c)
{
    HTTPContext *c1;
    FFStream *stream;
    char *q, *p;
    time_t ti;
    int i;

    q = c->buffer;
    q += sprintf(q, "HTTP/1.0 200 OK\r\n");
    q += sprintf(q, "Content-type: %s\r\n", "text/html");
    q += sprintf(q, "Pragma: no-cache\r\n");
    q += sprintf(q, "\r\n");
    
    q += sprintf(q, "<HEAD><TITLE>FFServer Status</TITLE></HEAD>\n<BODY>");
    q += sprintf(q, "<H1>FFServer Status</H1>\n");
    /* format status */
    q += sprintf(q, "<H1>Available Streams</H1>\n");
    q += sprintf(q, "<TABLE>\n");
    q += sprintf(q, "<TR><TD>Path<TD>Format<TD>Bit rate (kbits/s)<TD>Video<TD>Audio<TD>Feed\n");
    stream = first_stream;
    while (stream != NULL) {
        q += sprintf(q, "<TR><TD><A HREF=\"/%s\">%s</A> ", 
                     stream->filename, stream->filename);
        switch(stream->stream_type) {
        case STREAM_TYPE_LIVE:
            {
                int audio_bit_rate = 0;
                int video_bit_rate = 0;

                for(i=0;i<stream->nb_streams;i++) {
                    AVStream *st = stream->streams[i];
                    switch(st->codec.codec_type) {
                    case CODEC_TYPE_AUDIO:
                        audio_bit_rate += st->codec.bit_rate;
                        break;
                    case CODEC_TYPE_VIDEO:
                        video_bit_rate += st->codec.bit_rate;
                        break;
                    }
                }
                q += sprintf(q, "<TD> %s <TD> %d <TD> %d <TD> %d", 
                             stream->fmt->name,
                             (audio_bit_rate + video_bit_rate) / 1000,
                             video_bit_rate / 1000, audio_bit_rate / 1000);
                if (stream->feed) {
                    q += sprintf(q, "<TD>%s", stream->feed->filename);
                } else {
                    q += sprintf(q, "<TD>%s", stream->feed_filename);
                }
                q += sprintf(q, "\n");
            }
            break;
        default:
            q += sprintf(q, "<TD> - <TD> - <TD> - <TD> -\n");
            break;
        }
        stream = stream->next;
    }
    q += sprintf(q, "</TABLE>\n");
    
#if 0
    {
        float avg;
        AVCodecContext *enc;
        char buf[1024];
        
        /* feed status */
        stream = first_feed;
        while (stream != NULL) {
            q += sprintf(q, "<H1>Feed '%s'</H1>\n", stream->filename);
            q += sprintf(q, "<TABLE>\n");
            q += sprintf(q, "<TR><TD>Parameters<TD>Frame count<TD>Size<TD>Avg bitrate (kbits/s)\n");
            for(i=0;i<stream->nb_streams;i++) {
                AVStream *st = stream->streams[i];
                FeedData *fdata = st->priv_data;
                enc = &st->codec;
            
                avcodec_string(buf, sizeof(buf), enc);
                avg = fdata->avg_frame_size * (float)enc->rate * 8.0;
                if (enc->codec->type == CODEC_TYPE_AUDIO && enc->frame_size > 0)
                    avg /= enc->frame_size;
                q += sprintf(q, "<TR><TD>%s <TD> %d <TD> %Ld <TD> %0.1f\n", 
                             buf, enc->frame_number, fdata->data_count, avg / 1000.0);
            }
            q += sprintf(q, "</TABLE>\n");
            stream = stream->next_feed;
        }
    }
#endif

    /* connection status */
    q += sprintf(q, "<H1>Connection Status</H1>\n");

    q += sprintf(q, "Number of connections: %d / %d<BR>\n",
                 nb_connections, nb_max_connections);

    q += sprintf(q, "<TABLE>\n");
    q += sprintf(q, "<TR><TD>#<TD>File<TD>IP<TD>State<TD>Size\n");
    c1 = first_http_ctx;
    i = 0;
    while (c1 != NULL) {
        i++;
        p = inet_ntoa(c1->from_addr.sin_addr);
        q += sprintf(q, "<TR><TD><B>%d</B><TD>%s%s <TD> %s <TD> %s <TD> %Ld\n", 
                     i, c1->stream->filename, 
                     c1->state == HTTPSTATE_RECEIVE_DATA ? "(input)" : "",
                     p, 
                     http_state[c1->state],
                     c1->data_count);
        c1 = c1->next;
    }
    q += sprintf(q, "</TABLE>\n");
    
    /* date */
    ti = time(NULL);
    p = ctime(&ti);
    q += sprintf(q, "<HR>Generated at %s", p);
    q += sprintf(q, "</BODY>\n</HTML>\n");

    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
}


static void http_write_packet(void *opaque, 
                              unsigned char *buf, int size)
{
    HTTPContext *c = opaque;
    if (size > IOBUFFER_MAX_SIZE)
        abort();
    memcpy(c->buffer, buf, size);
    c->buffer_ptr = c->buffer;
    c->buffer_end = c->buffer + size;
}

static int open_input_stream(HTTPContext *c, const char *info)
{
    char buf[128];
    char input_filename[1024];
    AVFormatContext *s;
    int buf_size;
    INT64 stream_pos;

    /* find file name */
    if (c->stream->feed) {
        strcpy(input_filename, c->stream->feed->feed_filename);
        buf_size = FFM_PACKET_SIZE;
        /* compute position (absolute time) */
        if (find_info_tag(buf, sizeof(buf), "date", info)) {
            stream_pos = parse_date(buf, 0);
        } else {
            stream_pos = gettime();
        }
    } else {
        strcpy(input_filename, c->stream->feed_filename);
        buf_size = 0;
        /* compute position (relative time) */
        if (find_info_tag(buf, sizeof(buf), "date", info)) {
            stream_pos = parse_date(buf, 1);
        } else {
            stream_pos = 0;
        }
    }
    if (input_filename[0] == '\0')
        return -1;

    /* open stream */
    s = av_open_input_file(input_filename, buf_size);
    if (!s)
        return -1;
    c->fmt_in = s;

    if (c->fmt_in->format->read_seek) {
        c->fmt_in->format->read_seek(c->fmt_in, stream_pos);
    }
    
    //    printf("stream %s opened pos=%0.6f\n", input_filename, stream_pos / 1000000.0);
    return 0;
}

static int http_prepare_data(HTTPContext *c)
{
    int i;

    switch(c->state) {
    case HTTPSTATE_SEND_DATA_HEADER:
        memset(&c->fmt_ctx, 0, sizeof(c->fmt_ctx));
        if (c->stream->feed) {
            /* open output stream by using specified codecs */
            c->fmt_ctx.format = c->stream->fmt;
            c->fmt_ctx.nb_streams = c->stream->nb_streams;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                AVStream *st;
                st = av_mallocz(sizeof(AVStream));
                c->fmt_ctx.streams[i] = st;
                memcpy(st, c->stream->streams[i], sizeof(AVStream));
                st->codec.frame_number = 0; /* XXX: should be done in
                                               AVStream, not in codec */
                c->got_key_frame[i] = 0;
            }
        } else {
            /* open output stream by using codecs in specified file */
            c->fmt_ctx.format = c->stream->fmt;
            c->fmt_ctx.nb_streams = c->fmt_in->nb_streams;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                AVStream *st;
                st = av_mallocz(sizeof(AVStream));
                c->fmt_ctx.streams[i] = st;
                memcpy(st, c->fmt_in->streams[i], sizeof(AVStream));
                st->codec.frame_number = 0; /* XXX: should be done in
                                               AVStream, not in codec */
                c->got_key_frame[i] = 0;
            }
        }
        init_put_byte(&c->fmt_ctx.pb, c->buffer, IOBUFFER_MAX_SIZE,
                      1, c, NULL, http_write_packet, NULL);
        c->fmt_ctx.pb.is_streamed = 1;
        /* prepare header */
        c->fmt_ctx.format->write_header(&c->fmt_ctx);
        c->state = HTTPSTATE_SEND_DATA;
        c->last_packet_sent = 0;
        break;
    case HTTPSTATE_SEND_DATA:
        /* find a new packet */
#if 0
        fifo_total_size = http_fifo_write_count - c->last_http_fifo_write_count;
        if (fifo_total_size >= ((3 * FIFO_MAX_SIZE) / 4)) {
            /* overflow : resync. We suppose that wptr is at this
               point a pointer to a valid packet */
            c->rptr = http_fifo.wptr;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                c->got_key_frame[i] = 0;
            }
        }
        
        start_rptr = c->rptr;
        if (fifo_read(&http_fifo, (UINT8 *)&hdr, sizeof(hdr), &c->rptr) < 0)
            return 0;
        payload_size = ntohs(hdr.payload_size);
        payload = malloc(payload_size);
        if (fifo_read(&http_fifo, payload, payload_size, &c->rptr) < 0) {
            /* cannot read all the payload */
            free(payload);
            c->rptr = start_rptr;
            return 0;
        }
        
        c->last_http_fifo_write_count = http_fifo_write_count - 
            fifo_size(&http_fifo, c->rptr);
        
        if (c->stream->stream_type != STREAM_TYPE_MASTER) {
            /* test if the packet can be handled by this format */
            ret = 0;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                AVStream *st = c->fmt_ctx.streams[i];
                if (test_header(&hdr, &st->codec)) {
                    /* only begin sending when got a key frame */
                    if (st->codec.key_frame)
                        c->got_key_frame[i] = 1;
                    if (c->got_key_frame[i]) {
                        ret = c->fmt_ctx.format->write_packet(&c->fmt_ctx, i,
                                                                   payload, payload_size);
                    }
                    break;
                }
            }
            if (ret) {
                /* must send trailer now */
                c->state = HTTPSTATE_SEND_DATA_TRAILER;
            }
        } else {
            /* master case : send everything */
            char *q;
            q = c->buffer;
            memcpy(q, &hdr, sizeof(hdr));
            q += sizeof(hdr);
            memcpy(q, payload, payload_size);
            q += payload_size;
            c->buffer_ptr = c->buffer;
            c->buffer_end = q;
        }
        free(payload);
#endif
        {
            AVPacket pkt;

            /* read a packet from the input stream */
            if (c->stream->feed) {
                ffm_set_write_index(c->fmt_in, 
                                    c->stream->feed->feed_write_index,
                                    c->stream->feed->feed_size);
            }
            if (av_read_packet(c->fmt_in, &pkt) < 0) {
                if (c->stream->feed && c->stream->feed->feed_opened) {
                    /* if coming from feed, it means we reached the end of the
                       ffm file, so must wait for more data */
                    c->state = HTTPSTATE_WAIT_FEED;
                    return 1; /* state changed */
                } else {
                    /* must send trailer now because eof or error */
                    c->state = HTTPSTATE_SEND_DATA_TRAILER;
                }
            } else {
                /* send it to the appropriate stream */
                if (c->stream->feed) {
                    /* if coming from a feed, select the right stream */
                    for(i=0;i<c->stream->nb_streams;i++) {
                        if (c->stream->feed_streams[i] == pkt.stream_index) {
                            pkt.stream_index = i;
                            goto send_it;
                        }
                    }
                } else {
                send_it:
                    av_write_packet(&c->fmt_ctx, &pkt);
                }
                
                av_free_packet(&pkt);
            }
        }
        break;
    default:
    case HTTPSTATE_SEND_DATA_TRAILER:
        /* last packet test ? */
        if (c->last_packet_sent)
            return -1;
        /* prepare header */
        c->fmt_ctx.format->write_trailer(&c->fmt_ctx);
        c->last_packet_sent = 1;
        break;
    }
    return 0;
}

/* should convert the format at the same time */
static int http_send_data(HTTPContext *c)
{
    int len, ret;

    while (c->buffer_ptr >= c->buffer_end) {
        ret = http_prepare_data(c);
        if (ret < 0)
            return -1;
        else if (ret == 0) {
            break;
        } else {
            /* state change requested */
            return 0;
        }
    }

    len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
    if (len < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            /* error : close connection */
            return -1;
        }
    } else {
        c->buffer_ptr += len;
        c->data_count += len;
    }
    return 0;
}

static int http_start_receive_data(HTTPContext *c)
{
    int fd;

    if (c->stream->feed_opened)
        return -1;

    /* open feed */
    fd = open(c->stream->feed_filename, O_RDWR);
    if (fd < 0)
        return -1;
    c->feed_fd = fd;
    
    c->stream->feed_write_index = ffm_read_write_index(fd);
    c->stream->feed_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    /* init buffer input */
    c->buffer_ptr = c->buffer;
    c->buffer_end = c->buffer + FFM_PACKET_SIZE;
    c->stream->feed_opened = 1;
    return 0;
}
    
static int http_receive_data(HTTPContext *c)
{
    int len;
    HTTPContext *c1;

    if (c->buffer_ptr >= c->buffer_end) {
        /* a packet has been received : write it in the store, except
           if header */
        if (c->data_count > FFM_PACKET_SIZE) {
            FFStream *feed = c->stream;
            
            //            printf("writing pos=0x%Lx size=0x%Lx\n", feed->feed_write_index, feed->feed_size);
            /* XXX: use llseek or url_seek */
            lseek(c->feed_fd, feed->feed_write_index, SEEK_SET);
            write(c->feed_fd, c->buffer, FFM_PACKET_SIZE);
            
            feed->feed_write_index += FFM_PACKET_SIZE;
            /* update file size */
            if (feed->feed_write_index > c->stream->feed_size)
                feed->feed_size = feed->feed_write_index;

            /* handle wrap around if max file size reached */
            if (feed->feed_write_index >= c->stream->feed_max_size)
                feed->feed_write_index = FFM_PACKET_SIZE;

            /* write index */
            ffm_write_write_index(c->feed_fd, feed->feed_write_index);

            /* wake up any waiting connections */
            for(c1 = first_http_ctx; c1 != NULL; c1 = c1->next) {
                if (c1->state == HTTPSTATE_WAIT_FEED && 
                    c1->stream->feed == c->stream->feed) {
                    c1->state = HTTPSTATE_SEND_DATA;
                }
            }
        }
        c->buffer_ptr = c->buffer;
    }

    len = read(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
    if (len < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            /* error : close connection */
            goto fail;
        }
    } else if (len == 0) {
        /* end of connection : close it */
        goto fail;
    } else {
        c->buffer_ptr += len;
        c->data_count += len;
    }
    return 0;
 fail:
    c->stream->feed_opened = 0;
    close(c->feed_fd);
    return -1;
}

/* return the stream number in the feed */
int add_av_stream(FFStream *feed,
                  AVStream *st)
{
    AVStream *fst;
    AVCodecContext *av, *av1;
    int i;

    av = &st->codec;
    for(i=0;i<feed->nb_streams;i++) {
        st = feed->streams[i];
        av1 = &st->codec;
        if (av1->codec == av->codec &&
            av1->bit_rate == av->bit_rate) {

            switch(av->codec_type) {
            case CODEC_TYPE_AUDIO:
                if (av1->channels == av->channels &&
                    av1->sample_rate == av->sample_rate)
                    goto found;
                break;
            case CODEC_TYPE_VIDEO:
                if (av1->width == av->width &&
                    av1->height == av->height &&
                    av1->frame_rate == av->frame_rate &&
                    av1->gop_size == av->gop_size)
                    goto found;
                break;
            }
        }
    }
    
    fst = av_mallocz(sizeof(AVStream));
    if (!fst)
        return -1;
    fst->priv_data = av_mallocz(sizeof(FeedData));
    memcpy(&fst->codec, av, sizeof(AVCodecContext));
    feed->streams[feed->nb_streams++] = fst;
    return feed->nb_streams - 1;
 found:
    return i;
}

/* compute the needed AVStream for each feed */
void build_feed_streams(void)
{
    FFStream *stream, *feed;
    int i;

    /* gather all streams */
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        feed = stream->feed;
        if (feed) {
            if (!stream->is_feed) {
                for(i=0;i<stream->nb_streams;i++) {
                    stream->feed_streams[i] = add_av_stream(feed, stream->streams[i]);
                }
            } else {
                for(i=0;i<stream->nb_streams;i++) {
                    stream->feed_streams[i] = i;
                }
            }
        }
    }

    /* create feed files if needed */
    for(feed = first_feed; feed != NULL; feed = feed->next_feed) {
        int fd;

        if (!url_exist(feed->feed_filename)) {
            AVFormatContext s1, *s = &s1;

            /* only write the header of the ffm file */
            if (url_fopen(&s->pb, feed->feed_filename, URL_WRONLY) < 0) {
                fprintf(stderr, "Could not open output feed file '%s'\n",
                        feed->feed_filename);
                exit(1);
            }
            s->format = feed->fmt;
            s->nb_streams = feed->nb_streams;
            for(i=0;i<s->nb_streams;i++) {
                AVStream *st;
                st = feed->streams[i];
                s->streams[i] = st;
            }
            s->format->write_header(s);

            url_fclose(&s->pb);
        }
        /* get feed size and write index */
        fd = open(feed->feed_filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Could not open output feed file '%s'\n",
                    feed->feed_filename);
            exit(1);
        }

        feed->feed_write_index = ffm_read_write_index(fd);
        feed->feed_size = lseek(fd, 0, SEEK_END);
        /* ensure that we do not wrap before the end of file */
        if (feed->feed_max_size < feed->feed_size)
            feed->feed_max_size = feed->feed_size;

        close(fd);
    }
}

static void get_arg(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;
    int quote;

    p = *pp;
    while (isspace(*p)) p++;
    q = buf;
    quote = 0;
    if (*p == '\"' || *p == '\'')
        quote = *p++;
    for(;;) {
        if (quote) {
            if (*p == quote)
                break;
        } else {
            if (isspace(*p))
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

/* add a codec and set the default parameters */
void add_codec(FFStream *stream, AVCodecContext *av)
{
    AVStream *st;

    /* compute default parameters */
    switch(av->codec_type) {
    case CODEC_TYPE_AUDIO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->sample_rate == 0)
            av->sample_rate = 22050;
        if (av->channels == 0)
            av->channels = 1;
        break;
    case CODEC_TYPE_VIDEO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->frame_rate == 0)
            av->frame_rate = 5 * FRAME_RATE_BASE;
        if (av->width == 0 || av->height == 0) {
            av->width = 160;
            av->height = 128;
        }
        break;
    }

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return;
    stream->streams[stream->nb_streams++] = st;
    memcpy(&st->codec, av, sizeof(AVCodecContext));
}

int parse_ffconfig(const char *filename)
{
    FILE *f;
    char line[1024];
    char cmd[64];
    char arg[1024];
    const char *p;
    int val, errors, line_num;
    FFStream **last_stream, *stream;
    FFStream **last_feed, *feed;
    AVCodecContext audio_enc, video_enc;
    int audio_id, video_id;

    f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        return -1;
    }
    
    errors = 0;
    line_num = 0;
    first_stream = NULL;
    last_stream = &first_stream;
    first_feed = NULL;
    last_feed = &first_feed;
    stream = NULL;
    feed = NULL;
    audio_id = CODEC_ID_NONE;
    video_id = CODEC_ID_NONE;
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        p = line;
        while (isspace(*p)) 
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        get_arg(cmd, sizeof(cmd), &p);
        
        if (!strcasecmp(cmd, "Port")) {
            get_arg(arg, sizeof(arg), &p);
            my_addr.sin_port = htons (atoi(arg));
        } else if (!strcasecmp(cmd, "BindAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (!inet_aton(arg, &my_addr.sin_addr)) {
                fprintf(stderr, "%s:%d: Invalid IP address: %s\n", 
                        filename, line_num, arg);
                errors++;
            }
        } else if (!strcasecmp(cmd, "MaxClients")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 1 || val > HTTP_MAX_CONNECTIONS) {
                fprintf(stderr, "%s:%d: Invalid MaxClients: %s\n", 
                        filename, line_num, arg);
                errors++;
            } else {
                nb_max_connections = val;
            }
        } else if (!strcasecmp(cmd, "CustomLog")) {
            get_arg(logfilename, sizeof(logfilename), &p);
        } else if (!strcasecmp(cmd, "<Feed")) {
            /*********************************************/
            /* Feed related options */
            char *q;
            if (stream || feed) {
                fprintf(stderr, "%s:%d: Already in a tag\n",
                        filename, line_num);
            } else {
                feed = av_mallocz(sizeof(FFStream));
                /* add in stream list */
                *last_stream = feed;
                last_stream = &feed->next;
                /* add in feed list */
                *last_feed = feed;
                last_feed = &feed->next_feed;
                
                get_arg(feed->filename, sizeof(feed->filename), &p);
                q = strrchr(feed->filename, '>');
                if (*q)
                    *q = '\0';
                feed->fmt = guess_format("ffm", NULL, NULL);
                /* defaut feed file */
                snprintf(feed->feed_filename, sizeof(feed->feed_filename),
                         "/tmp/%s.ffm", feed->filename);
                feed->feed_max_size = 5 * 1024 * 1024;
                feed->is_feed = 1;
                feed->feed = feed; /* self feeding :-) */
            }
        } else if (!strcasecmp(cmd, "File")) {
            if (feed) {
                get_arg(feed->feed_filename, sizeof(feed->feed_filename), &p);
            } else if (stream) {
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
            }
        } else if (!strcasecmp(cmd, "FileMaxSize")) {
            if (feed) {
                const char *p1;
                double fsize;

                get_arg(arg, sizeof(arg), &p);
                p1 = arg;
                fsize = strtod(p1, (char **)&p1);
                switch(toupper(*p1)) {
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
                feed->feed_max_size = (INT64)fsize;
            }
        } else if (!strcasecmp(cmd, "</Feed>")) {
            if (!feed) {
                fprintf(stderr, "%s:%d: No corresponding <Feed> for </Feed>\n",
                        filename, line_num);
                errors++;
            }
            feed = NULL;
        } else if (!strcasecmp(cmd, "<Stream")) {
            /*********************************************/
            /* Stream related options */
            char *q;
            if (stream || feed) {
                fprintf(stderr, "%s:%d: Already in a tag\n",
                        filename, line_num);
            } else {
                stream = av_mallocz(sizeof(FFStream));
                *last_stream = stream;
                last_stream = &stream->next;

                get_arg(stream->filename, sizeof(stream->filename), &p);
                q = strrchr(stream->filename, '>');
                if (*q)
                    *q = '\0';
                stream->fmt = guess_format(NULL, stream->filename, NULL);
                memset(&audio_enc, 0, sizeof(AVCodecContext));
                memset(&video_enc, 0, sizeof(AVCodecContext));
                audio_id = CODEC_ID_NONE;
                video_id = CODEC_ID_NONE;
                if (stream->fmt) {
                    audio_id = stream->fmt->audio_codec;
                    video_id = stream->fmt->video_codec;
                }
            }
        } else if (!strcasecmp(cmd, "Feed")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                FFStream *sfeed;
                
                sfeed = first_feed;
                while (sfeed != NULL) {
                    if (!strcmp(sfeed->filename, arg))
                        break;
                    sfeed = sfeed->next_feed;
                }
                if (!sfeed) {
                    fprintf(stderr, "%s:%d: feed '%s' not defined\n",
                            filename, line_num, arg);
                } else {
                    stream->feed = sfeed;
                }
            }
        } else if (!strcasecmp(cmd, "Format")) {
            get_arg(arg, sizeof(arg), &p);
            if (!strcmp(arg, "status")) {
                stream->stream_type = STREAM_TYPE_STATUS;
                stream->fmt = NULL;
            } else {
                stream->stream_type = STREAM_TYPE_LIVE;
                /* jpeg cannot be used here, so use single frame jpeg */
                if (!strcmp(arg, "jpeg"))
                    strcpy(arg, "singlejpeg");
                stream->fmt = guess_format(arg, NULL, NULL);
                if (!stream->fmt) {
                    fprintf(stderr, "%s:%d: Unknown Format: %s\n", 
                            filename, line_num, arg);
                    errors++;
                }
            }
            if (stream->fmt) {
                audio_id = stream->fmt->audio_codec;
                video_id = stream->fmt->video_codec;
            }
        } else if (!strcasecmp(cmd, "AudioBitRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                audio_enc.bit_rate = atoi(arg) * 1000;
            }
        } else if (!strcasecmp(cmd, "AudioChannels")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                audio_enc.channels = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "AudioSampleRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                audio_enc.sample_rate = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "VideoBitRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.bit_rate = atoi(arg) * 1000;
            }
        } else if (!strcasecmp(cmd, "VideoSize")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                parse_image_size(&video_enc.width, &video_enc.height, arg);
                if ((video_enc.width % 16) != 0 ||
                    (video_enc.height % 16) != 0) {
                    fprintf(stderr, "%s:%d: Image size must be a multiple of 16\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoFrameRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.frame_rate = (int)(strtod(arg, NULL) * FRAME_RATE_BASE);
            }
        } else if (!strcasecmp(cmd, "VideoGopSize")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.gop_size = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "VideoIntraOnly")) {
            if (stream) {
                video_enc.gop_size = 1;
            }
        } else if (!strcasecmp(cmd, "NoVideo")) {
            video_id = CODEC_ID_NONE;
        } else if (!strcasecmp(cmd, "NoAudio")) {
            audio_id = CODEC_ID_NONE;
        } else if (!strcasecmp(cmd, "</Stream>")) {
            if (!stream) {
                fprintf(stderr, "%s:%d: No corresponding <Stream> for </Stream>\n",
                        filename, line_num);
                errors++;
            }
            if (stream->feed && stream->fmt && strcmp(stream->fmt->name, "ffm") != 0) {
                if (audio_id != CODEC_ID_NONE) {
                    audio_enc.codec_type = CODEC_TYPE_AUDIO;
                    audio_enc.codec_id = audio_id;
                    add_codec(stream, &audio_enc);
                }
                if (video_id != CODEC_ID_NONE) {
                    video_enc.codec_type = CODEC_TYPE_VIDEO;
                    video_enc.codec_id = video_id;
                    add_codec(stream, &video_enc);
                }
            }
            stream = NULL;
        } else {
            fprintf(stderr, "%s:%d: Incorrect keyword: '%s'\n", 
                    filename, line_num, cmd);
            errors++;
        }
    }

    fclose(f);
    if (errors)
        return -1;
    else
        return 0;
}


void *http_server_thread(void *arg)
{
    http_server(my_addr);
    return NULL;
}

#if 0
static void write_packet(FFCodec *ffenc,
                         UINT8 *buf, int size)
{
    PacketHeader hdr;
    AVCodecContext *enc = &ffenc->enc;
    UINT8 *wptr;
    mk_header(&hdr, enc, size);
    wptr = http_fifo.wptr;
    fifo_write(&http_fifo, (UINT8 *)&hdr, sizeof(hdr), &wptr);
    fifo_write(&http_fifo, buf, size, &wptr);
    /* atomic modification of wptr */
    http_fifo.wptr = wptr;
    ffenc->data_count += size;
    ffenc->avg_frame_size = ffenc->avg_frame_size * AVG_COEF + size * (1.0 - AVG_COEF);
}
#endif

void help(void)
{
    printf("ffserver version " FFMPEG_VERSION ", Copyright (c) 2000,2001 Gerard Lantau\n"
           "usage: ffserver [-L] [-h] [-f configfile]\n"
           "Hyper fast multi format Audio/Video streaming server\n"
           "\n"
           "-L            : print the LICENCE\n"
           "-h            : this help\n"
           "-f configfile : use configfile instead of /etc/ffserver.conf\n"
           );
}

void licence(void)
{
    printf(
    "ffserver version " FFMPEG_VERSION "\n"
    "Copyright (c) 2000,2001 Gerard Lantau\n"
    "This program is free software; you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation; either version 2 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program; if not, write to the Free Software\n"
    "Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n"
    );
}

int main(int argc, char **argv)
{
    const char *config_filename;
    int c;

    register_all();

    config_filename = "/etc/ffserver.conf";

    for(;;) {
        c = getopt_long_only(argc, argv, "Lh?f:", NULL, NULL);
        if (c == -1)
            break;
        switch(c) {
        case 'L':
            licence();
            exit(1);
        case '?':
        case 'h':
            help();
            exit(1);
        case 'f':
            config_filename = optarg;
            break;
        default:
            exit(2);
        }
    }

    /* address on which the server will handle connections */
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons (8080);
    my_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    nb_max_connections = 5;
    first_stream = NULL;
    logfilename[0] = '\0';

    if (parse_ffconfig(config_filename) < 0) {
        fprintf(stderr, "Incorrect config file - exiting.\n");
        exit(1);
    }

    build_feed_streams();

    /* signal init */
    signal(SIGPIPE, SIG_IGN);

    /* open log file if needed */
    if (logfilename[0] != '\0') {
        if (!strcmp(logfilename, "-"))
            logfile = stdout;
        else
            logfile = fopen(logfilename, "w");
    }

    if (http_server(my_addr) < 0) {
        fprintf(stderr, "Could start http server\n");
        exit(1);
    }

    return 0;
}
