/*
 * Multiple format streaming server
 * Copyright (c) 2000 Gerard Lantau.
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
#include <linux/videodev.h>
#include <linux/soundcard.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/time.h>
#include <getopt.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <pthread.h>

#include "mpegenc.h"

/* maximum number of simultaneous HTTP connections */
#define HTTP_MAX_CONNECTIONS 2000

enum HTTPState {
    HTTPSTATE_WAIT_REQUEST,
    HTTPSTATE_SEND_HEADER,
    HTTPSTATE_SEND_DATA_HEADER,
    HTTPSTATE_SEND_DATA,
    HTTPSTATE_SEND_DATA_TRAILER,
};

enum MasterState {
    MASTERSTATE_RECEIVE_HEADER,
    MASTERSTATE_RECEIVE_DATA,
};
    
#define IOBUFFER_MAX_SIZE 16384
#define FIFO_MAX_SIZE (1024*1024)

/* coef for exponential mean for bitrate estimation in statistics */
#define AVG_COEF 0.9

/* timeouts are in ms */
#define REQUEST_TIMEOUT (15 * 1000)
#define SYNC_TIMEOUT (10 * 1000)
#define MASTER_CONNECT_TIMEOUT (10 * 1000)

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
    UINT8 *rptr; /* read pointer in the fifo */
    int got_key_frame[2]; /* for each type */
    long long data_count;
    long long last_http_fifo_write_count; /* used to monitor overflow in the fifo */
    /* format handling */
    struct FFStream *stream;
    AVFormatContext fmt_ctx;
    int last_packet_sent; /* true if last data packet was sent */
} HTTPContext;

/* each generated stream is described here */
enum StreamType {
    STREAM_TYPE_LIVE,
    STREAM_TYPE_MASTER,
    STREAM_TYPE_STATUS,
};

typedef struct FFStream {
    enum StreamType stream_type;
    char filename[1024];
    AVFormat *fmt;
    AVEncodeContext *audio_enc;
    AVEncodeContext *video_enc;
    struct FFStream *next;
} FFStream;

typedef struct FifoBuffer {
    UINT8 *buffer;
    UINT8 *rptr, *wptr, *end;
} FifoBuffer;

/* each codec is here */
typedef struct FFCodec {
    struct FFCodec *next;
    FifoBuffer fifo;     /* for compression: one audio fifo per codec */
    ReSampleContext resample; /* for audio resampling */
    long long data_count;
    float avg_frame_size;   /* frame size averraged over last frames with exponential mean */
    AVEncodeContext enc;
} FFCodec;

/* packet header */
typedef struct {
    UINT8 codec_type;
    UINT8 codec_id;
    UINT8 data[4];
    UINT16 bit_rate;
    UINT16 payload_size;
} PacketHeader;

struct sockaddr_in my_addr;
char logfilename[1024];
HTTPContext *first_http_ctx;
FFStream *first_stream;
FFCodec *first_codec;

/* master state */
char master_url[1024];
enum MasterState master_state;
UINT8 *master_wptr;
int master_count;

long long http_fifo_write_count;
static FifoBuffer http_fifo;

static int handle_http(HTTPContext *c, long cur_time);
static int http_parse_request(HTTPContext *c);
static int http_send_data(HTTPContext *c);
static int master_receive(int fd);
static void compute_stats(HTTPContext *c);

int nb_max_connections;
int nb_connections;

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

static int fifo_size(FifoBuffer *f, UINT8 *rptr)
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
static int fifo_read(FifoBuffer *f, UINT8 *buf, int buf_size, UINT8 **rptr_ptr)
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

static void fifo_write(FifoBuffer *f, UINT8 *buf, int size, UINT8 **wptr_ptr)
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


/* connect to url 'url' and return the connected socket ready to read data */
static int url_get(const char *url)
{
    struct sockaddr_in dest_addr;
    struct hostent *h;
    int s, port, size, line_size, len;
    char hostname[1024], *q;
    const char *p, *path;
    char req[1024];
    unsigned char ch;

    if (!strstart(url, "http://", &p))
        return -1;
    q = hostname;
    while (*p != ':' && *p != '\0' && *p != '/') {
        if ((q - hostname) < (sizeof(hostname) - 1))
            *q++ = *p;
        p++;
    }
    port = 80;
    if (*p == ':') {
        p++;
        port = strtol(p, (char **)&p, 10);
    }
    path = p;
        
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    if (!inet_aton(hostname, &dest_addr.sin_addr)) {
	if ((h = gethostbyname(hostname)) == NULL)
	    return -1;
	memcpy(&dest_addr.sin_addr, h->h_addr, sizeof(dest_addr.sin_addr));
    }

    s=socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) 
        return -1;

    if (connect(s, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
    fail:
	close(s);
	return -1;
    }
    
    /* send http request */
    snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\n\r\n", path);
    p = req;
    size = strlen(req);
    while (size > 0) {
        len = write(s, p, size);
        if (len == -1) {
            if (errno != EAGAIN && errno != EINTR)
                goto fail;
        } else {
            size -= len;
            p += len;
        }
    }
    
    /* receive answer */
    line_size = 0;
    for(;;) {
        len = read(s, &ch, 1);
        if (len == -1) {
            if (errno != EAGAIN && errno != EINTR)
                goto fail;
        } else if (len == 0) {
            goto fail;
        } else {
            if (ch == '\n') {
                if (line_size == 0)
                    break;
                line_size = 0;
            } else if (ch != '\r') {
                line_size++;
            }
        }
    }

    return s;
}

/* Each request is served by reading the input FIFO and by adding the
   right format headers */
static int http_server(struct sockaddr_in my_addr)
{
    int server_fd, tmp, ret;
    struct sockaddr_in from_addr;
    struct pollfd poll_table[HTTP_MAX_CONNECTIONS + 1], *poll_entry;
    HTTPContext *c, **cp;
    long cur_time;
    int master_fd, master_timeout;

    /* will try to connect to master as soon as possible */
    master_fd = -1;
    master_timeout = gettime_ms();

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

        if (master_fd >= 0) {
            poll_entry->fd = master_fd;
            poll_entry->events = POLLIN;
            poll_entry++;
        }

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
            fd = accept(server_fd, &from_addr, &len);
            if (fd >= 0) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                /* XXX: should output a warning page when comming
                   close to the connection limit */
                if (nb_connections >= nb_max_connections) {
                    close(fd);
                } else {
                    /* add a new connection */
                    c = malloc(sizeof(HTTPContext));
                    memset(c, 0, sizeof(*c));
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

        /* master events */
        if (poll_entry->revents & POLLIN) {
            if (master_receive(master_fd) < 0) {
                close(master_fd);
                master_fd = -1;
            }
        }

        /* master (re)connection handling */
        if (master_url[0] != '\0' && 
            master_fd < 0 && (master_timeout - cur_time) <= 0) {
            master_fd = url_get(master_url);
            if (master_fd < 0) {
                master_timeout = gettime_ms() + MASTER_CONNECT_TIMEOUT;
                http_log("Connection to master: '%s' failed\n", master_url);
            } else {
                fcntl(master_fd, F_SETFL, O_NONBLOCK);
                master_state = MASTERSTATE_RECEIVE_HEADER;
                master_count = sizeof(PacketHeader);
                master_wptr = http_fifo.wptr;
            }
        }
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
    default:
        return -1;
    }
    return 0;
}

/* parse http request and prepare header */
static int http_parse_request(HTTPContext *c)
{
    const char *p;
    char cmd[32];
    char url[1024], *q;
    char protocol[32];
    char msg[1024];
    char *mime_type;
    FFStream *stream;

    p = c->buffer;
    q = cmd;
    while (!isspace(*p) && *p != '\0') {
        if ((q - cmd) < sizeof(cmd) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    if (strcmp(cmd, "GET"))
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
    
    /* find the filename in the request */
    p = url;
    if (*p == '/')
        p++;

    stream = first_stream;
    while (stream != NULL) {
        if (!strcmp(stream->filename, p))
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

    if (c->stream->stream_type == STREAM_TYPE_STATUS)
        goto send_stats;

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
    AVEncodeContext *enc;
    HTTPContext *c1;
    FFCodec *ffenc;
    FFStream *stream;
    float avg;
    char buf[1024], *q, *p;
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
    q += sprintf(q, "<TR><TD>Path<TD>Format<TD>Bit rate (kbits/s)<TD>Video<TD>Audio\n");
    stream = first_stream;
    while (stream != NULL) {
        q += sprintf(q, "<TR><TD><A HREF=\"/%s\">%s</A> ", 
                     stream->filename, stream->filename);
        switch(stream->stream_type) {
        case STREAM_TYPE_LIVE:
            {
                int audio_bit_rate = 0;
                int video_bit_rate = 0;
                if (stream->audio_enc)
                    audio_bit_rate = stream->audio_enc->bit_rate;
                if (stream->video_enc)
                    video_bit_rate = stream->video_enc->bit_rate;
                
                q += sprintf(q, "<TD> %s <TD> %d <TD> %d <TD> %d\n", 
                             stream->fmt->name,
                             (audio_bit_rate + video_bit_rate) / 1000,
                             video_bit_rate / 1000, audio_bit_rate / 1000);
            }
            break;
        case STREAM_TYPE_MASTER:
            q += sprintf(q, "<TD> %s <TD> - <TD> - <TD> -\n",
                         "master");
            break;
        default:
            q += sprintf(q, "<TD> - <TD> - <TD> - <TD> -\n");
            break;
        }
        stream = stream->next;
    }
    q += sprintf(q, "</TABLE>\n");
    
    /* codec status */
    q += sprintf(q, "<H1>Codec Status</H1>\n");
    q += sprintf(q, "<TABLE>\n");
    q += sprintf(q, "<TR><TD>Parameters<TD>Frame count<TD>Size<TD>Avg bitrate (kbits/s)\n");
    ffenc = first_codec;
    while (ffenc != NULL) {
        enc = &ffenc->enc;
        avencoder_string(buf, sizeof(buf), enc);
        avg = ffenc->avg_frame_size * (float)enc->rate * 8.0;
        if (enc->codec->type == CODEC_TYPE_AUDIO && enc->frame_size > 0)
            avg /= enc->frame_size;
        q += sprintf(q, "<TR><TD>%s <TD> %d <TD> %Ld <TD> %0.1f\n", 
                     buf, enc->frame_number, ffenc->data_count, avg / 1000.0);
        ffenc = ffenc->next;
    }
    q += sprintf(q, "</TABLE>\n");

    /* exclude the stat connection */
    q += sprintf(q, "Number of connections: %d / %d<BR>\n",
                 nb_connections, nb_max_connections);

    /* connection status */
    q += sprintf(q, "<H1>Connection Status</H1>\n");
    q += sprintf(q, "<TABLE>\n");
    q += sprintf(q, "<TR><TD>#<TD>File<TD>IP<TD>Size\n");
    c1 = first_http_ctx;
    i = 0;
    while (c1 != NULL) {
        i++;
        p = inet_ntoa(c1->from_addr.sin_addr);
        q += sprintf(q, "<TR><TD><B>%d</B><TD>%s <TD> %s <TD> %Ld\n", 
                     i, c1->stream->filename, p, c1->data_count);
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

/* this headers are used to identify a packet for a given codec */
void mk_header(PacketHeader *h, AVEncodeContext *c, int payload_size)
{
    h->codec_type = c->codec->type;
    h->codec_id = c->codec->id;
    h->bit_rate = htons(c->bit_rate / 1000);
    switch(c->codec->type) {
    case CODEC_TYPE_VIDEO:
        h->data[0] = c->rate;
        h->data[1] = c->width / 16;
        h->data[2] = c->height / 16;
        break;
    case CODEC_TYPE_AUDIO:
        h->data[0] = c->rate / 1000;
        h->data[1] = c->channels;
        h->data[2] = 0;
        break;
    }
    h->data[3] = c->key_frame;
    h->payload_size = htons(payload_size);
}

int test_header(PacketHeader *h, AVEncodeContext *c)
{
    if (!c)
        return 0;

    if (h->codec_type == c->codec->type &&
        h->codec_id == c->codec->id &&
        h->bit_rate == htons(c->bit_rate / 1000)) {

        switch(c->codec->type) {
        case CODEC_TYPE_VIDEO:
            if (h->data[0] == c->rate &&
                h->data[1] == (c->width / 16) &&
                h->data[2] == (c->height / 16))
                goto found;
            break;
        case CODEC_TYPE_AUDIO:
            if (h->data[0] == (c->rate / 1000) &&
                (h->data[1] == c->channels))
                goto found;
            break;
        }
    }
    return 0;
 found:
    c->frame_number++;
    c->key_frame = h->data[3];
    return 1;
}

static int http_prepare_data(HTTPContext *c)
{
    PacketHeader hdr;
    UINT8 *start_rptr, *payload;
    int payload_size, ret;
    long long fifo_total_size;

    switch(c->state) {
    case HTTPSTATE_SEND_DATA_HEADER:
        if (c->stream->stream_type != STREAM_TYPE_MASTER) {            
            memset(&c->fmt_ctx, 0, sizeof(c->fmt_ctx));
            c->fmt_ctx.format = c->stream->fmt;
            if (c->fmt_ctx.format->audio_codec != CODEC_ID_NONE) {
                /* create a fake new codec instance */
                c->fmt_ctx.audio_enc = malloc(sizeof(AVEncodeContext));
                memcpy(c->fmt_ctx.audio_enc, c->stream->audio_enc, 
                       sizeof(AVEncodeContext));
                c->fmt_ctx.audio_enc->frame_number = 0;
            }
            if (c->fmt_ctx.format->video_codec != CODEC_ID_NONE) {
                c->fmt_ctx.video_enc = malloc(sizeof(AVEncodeContext));
                memcpy(c->fmt_ctx.video_enc, c->stream->video_enc, 
                       sizeof(AVEncodeContext));
                c->fmt_ctx.video_enc->frame_number = 0;
            }
            init_put_byte(&c->fmt_ctx.pb, c->buffer, IOBUFFER_MAX_SIZE,
                          c, http_write_packet, NULL);
            c->fmt_ctx.is_streamed = 1;
            c->got_key_frame[0] = 0;
            c->got_key_frame[1] = 0;
            /* prepare header */
            c->fmt_ctx.format->write_header(&c->fmt_ctx);
        }
        c->state = HTTPSTATE_SEND_DATA;
        c->last_packet_sent = 0;
        c->rptr = http_fifo.wptr;
        c->last_http_fifo_write_count = http_fifo_write_count;
        break;
    case HTTPSTATE_SEND_DATA:
        /* find a new packet */
        fifo_total_size = http_fifo_write_count - c->last_http_fifo_write_count;
        if (fifo_total_size >= ((3 * FIFO_MAX_SIZE) / 4)) {
            /* overflow : resync. We suppose that wptr is at this
               point a pointer to a valid packet */
            c->rptr = http_fifo.wptr;
            c->got_key_frame[0] = 0;
            c->got_key_frame[1] = 0;
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
            if (test_header(&hdr, c->fmt_ctx.audio_enc)) {
                /* only begin sending when got a key frame */
                if (c->fmt_ctx.audio_enc->key_frame)
                    c->got_key_frame[1] = 1;
                if (c->got_key_frame[1]) {
                    ret = c->fmt_ctx.format->write_audio_frame(&c->fmt_ctx, 
                                                               payload, payload_size);
                }
            } else if (test_header(&hdr, c->fmt_ctx.video_enc)) {
                if (c->fmt_ctx.video_enc->key_frame)
                    c->got_key_frame[0] = 1;
                if (c->got_key_frame[0]) {
                    ret = c->fmt_ctx.format->write_video_picture(&c->fmt_ctx, 
                                                                 payload, payload_size);
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
    int len;

    while (c->buffer_ptr >= c->buffer_end) {
        if (http_prepare_data(c) < 0)
            return -1;
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

static int master_receive(int fd)
{
    int len, size;
    FifoBuffer *f = &http_fifo;
    UINT8 *rptr;

    size = f->end - f->wptr;
    if (size > master_count)
        size = master_count;
    len = read(fd, f->wptr, size);
    if (len == -1) {
        if (errno != EAGAIN && errno != EINTR) 
            return -1;
    } else if (len == 0) {
        return -1;
    } else {
        master_wptr += len;
        if (master_wptr >= f->end)
            master_wptr = f->buffer;
        master_count -= len;
        if (master_count == 0) {
            if (master_state == MASTERSTATE_RECEIVE_HEADER) {
                /* XXX: use generic fifo read to extract packet header */
                rptr = master_wptr;
                if (rptr == f->buffer)
                    rptr = f->end - 1;
                else
                    rptr--;
                master_count = *rptr;
                if (rptr == f->buffer)
                    rptr = f->end - 1;
                else
                    rptr--;
                master_count |= *rptr << 8;
                master_state = MASTERSTATE_RECEIVE_DATA;
            } else {
                /* update fifo wptr */
                f->wptr = master_wptr;
                master_state = MASTERSTATE_RECEIVE_HEADER;
            }
        }
    }
    return 0;
}

static void get_arg(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    while (isspace(*p)) p++;
    q = buf;
    while (!isspace(*p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    *pp = p;
}

/* add a codec and check if it does not already exists */
AVEncodeContext *add_codec(int codec_id,
                           AVEncodeContext *av)
{
    AVEncoder *codec;
    FFCodec *ctx, **pctx;
    AVEncodeContext *av1;

    codec = avencoder_find(codec_id);
    if (!codec)
        return NULL;

    /* compute default parameters */
    av->codec = codec;
    switch(codec->type) {
    case CODEC_TYPE_AUDIO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->rate == 0)
            av->rate = 22050;
        if (av->channels == 0)
            av->channels = 1;
        break;
    case CODEC_TYPE_VIDEO:
        if (av->bit_rate == 0)
            av->bit_rate = 64000;
        if (av->rate == 0)
            av->rate = 5;
        if (av->width == 0 || av->height == 0) {
            av->width = 160;
            av->height = 128;
        }
        break;
    }

    /* find if the codec already exists */
    pctx = &first_codec;
    while (*pctx != NULL) {
        av1 = &(*pctx)->enc;
        if (av1->codec == av->codec &&
            av1->bit_rate == av->bit_rate &&
            av1->rate == av->rate) {

            switch(av->codec->type) {
            case CODEC_TYPE_AUDIO:
                if (av1->channels == av->channels)
                    goto found;
                break;
            case CODEC_TYPE_VIDEO:
                if (av1->width == av->width &&
                    av1->height == av->height &&
                    av1->gop_size == av->gop_size)
                    goto found;
                break;
            }
        }
        pctx = &(*pctx)->next;
    }

    ctx = malloc(sizeof(FFCodec));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(FFCodec));
    *pctx = ctx;
    
    memcpy(&ctx->enc, av, sizeof(AVEncodeContext));
    return &ctx->enc;
 found:
    ctx = *pctx;
    return &ctx->enc;
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
    AVEncodeContext audio_enc, video_enc;

    f = fopen(filename, "r");
    if (!f) {
        perror(filename);
        return -1;
    }
    
    errors = 0;
    line_num = 0;
    first_stream = NULL;
    first_codec = NULL;
    last_stream = &first_stream;
    stream = NULL;
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
        } else if (!strcasecmp(cmd, "MasterServer")) {
            get_arg(master_url, sizeof(master_url), &p);
            if (!strstart(master_url, "http://", NULL)) {
                fprintf(stderr, "%s:%d: Invalid URL for master server: %s\n", 
                        filename, line_num, master_url);
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
        } else if (!strcasecmp(cmd, "<Stream")) {
            char *q;
            if (stream) {
                fprintf(stderr, "%s:%d: Already in a stream tag\n",
                        filename, line_num);
            } else {
                stream = malloc(sizeof(FFStream));
                memset(stream, 0, sizeof(FFStream));
                *last_stream = stream;
                last_stream = &stream->next;

                get_arg(stream->filename, sizeof(stream->filename), &p);
                q = strrchr(stream->filename, '>');
                if (*q)
                    *q = '\0';
                stream->fmt = guess_format(NULL, stream->filename, NULL);
                memset(&audio_enc, 0, sizeof(AVEncodeContext));
                memset(&video_enc, 0, sizeof(AVEncodeContext));
            }
        } else if (!strcasecmp(cmd, "Format")) {
            get_arg(arg, sizeof(arg), &p);
            if (!strcmp(arg, "master")) {
                stream->stream_type = STREAM_TYPE_MASTER;
                stream->fmt = NULL;
            } else if (!strcmp(arg, "status")) {
                stream->stream_type = STREAM_TYPE_STATUS;
                stream->fmt = NULL;
            } else {
                stream->stream_type = STREAM_TYPE_LIVE;
                stream->fmt = guess_format(arg, NULL, NULL);
                if (!stream->fmt) {
                    fprintf(stderr, "%s:%d: Unknown Format: %s\n", 
                            filename, line_num, arg);
                    errors++;
                }
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
                audio_enc.rate = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "VideoBitRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.bit_rate = atoi(arg) * 1000;
            }
        } else if (!strcasecmp(cmd, "VideoFrameRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.rate = atoi(arg);
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
        } else if (!strcasecmp(cmd, "</Stream>")) {
            if (!stream) {
                fprintf(stderr, "%s:%d: No corresponding <Stream> for </Stream>\n",
                        filename, line_num);
                errors++;
            }
            if (stream->fmt) {
                if (stream->fmt->audio_codec != CODEC_ID_NONE) {
                    stream->audio_enc = add_codec(stream->fmt->audio_codec,
                                                  &audio_enc);
                }
                
                if (stream->fmt->video_codec != CODEC_ID_NONE)
                    stream->video_enc = add_codec(stream->fmt->video_codec,
                                                  &video_enc);
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

static void write_packet(FFCodec *ffenc,
                         UINT8 *buf, int size)
{
    PacketHeader hdr;
    AVEncodeContext *enc = &ffenc->enc;
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

#define AUDIO_FIFO_SIZE 8192

int av_grab(void)
{
    UINT8 audio_buf[AUDIO_FIFO_SIZE/2];
    UINT8 audio_buf1[AUDIO_FIFO_SIZE/2];
    UINT8 audio_out[AUDIO_FIFO_SIZE/2];
    UINT8 video_buffer[128*1024];
    char buf[256];
    short *samples;
    int ret;
    int audio_fd;
    FFCodec *ffenc;
    AVEncodeContext *enc;
    int frame_size, frame_bytes;
    int use_audio, use_video;
    int frame_rate, sample_rate, channels;
    int width, height, frame_number;
    UINT8 *picture[3];

    use_audio = 0;
    use_video = 0;
    frame_rate = 0;
    sample_rate = 0;
    frame_size = 0;
    channels = 1;
    width = 0;
    height = 0;
    frame_number = 0;
    ffenc = first_codec;
    while (ffenc != NULL) {
        enc = &ffenc->enc;
        avencoder_string(buf, sizeof(buf), enc);
        fprintf(stderr, "  %s\n", buf);
        if (avencoder_open(enc, enc->codec) < 0) {
            fprintf(stderr, "Incorrect encode parameters\n");
            return -1;
        }
        switch(enc->codec->type) {
        case CODEC_TYPE_AUDIO:
            use_audio = 1;
            if (enc->rate > sample_rate)
                sample_rate = enc->rate;
            if (enc->frame_size > frame_size)
                frame_size = enc->frame_size;
            if (enc->channels > channels)
                channels = enc->channels;
            fifo_init(&ffenc->fifo, AUDIO_FIFO_SIZE);
            break;
        case CODEC_TYPE_VIDEO:
            use_video = 1;
            if (enc->rate > frame_rate)
                frame_rate = enc->rate;
            if (enc->width > width)
                width = enc->width;
            if (enc->height > height)
                height = enc->height;
            break;
        }
        ffenc = ffenc->next;
    }

    /* audio */
    samples = NULL;
    audio_fd = -1;
    if (use_audio) {
        printf("Audio sampling: %d Hz, %s\n", 
               sample_rate, channels == 2 ? "stereo" : "mono");
        audio_fd = audio_open(sample_rate, channels);
        if (audio_fd < 0) {
            fprintf(stderr, "Could not open audio device\n");
            exit(1);
        }
    }
    
    ffenc = first_codec;
    while (ffenc != NULL) {
        enc = &ffenc->enc;
        if (enc->codec->type == CODEC_TYPE_AUDIO &&
            (enc->channels != channels ||
             enc->rate != sample_rate)) {
            audio_resample_init(&ffenc->resample, enc->channels, channels,
                                enc->rate, sample_rate);
        }
        ffenc = ffenc->next;
    }

    /* video */
    if (use_video) {
        printf("Video sampling: %dx%d, %d fps\n", 
               width, height, frame_rate);
        ret = v4l_init(frame_rate, width, height);
        if (ret < 0) {
            fprintf(stderr,"Could not init video 4 linux capture\n");
            exit(1);
        }
    }

    for(;;) {
        /* read & compress audio frames */
        if (use_audio) {
            int ret, nb_samples, nb_samples_out;
            UINT8 *buftmp;

            for(;;) {
                ret = read(audio_fd, audio_buf, AUDIO_FIFO_SIZE/2);
                if (ret <= 0)
                    break;
                /* fill each codec fifo by doing the right sample
                   rate conversion. This is not optimal because we
                   do too much work, but it is easy to do */
                nb_samples = ret / (channels * 2);
                ffenc = first_codec;
                while (ffenc != NULL) {
                    enc = &ffenc->enc;
                    if (enc->codec->type == CODEC_TYPE_AUDIO) {
                        /* rate & stereo convertion */
                        if (enc->channels == channels &&
                            enc->rate == sample_rate) {
                            buftmp = audio_buf;
                            nb_samples_out = nb_samples;
                        } else {
                            buftmp = audio_buf1;
                            nb_samples_out = audio_resample(&ffenc->resample, 
                                                            (short *)buftmp, (short *)audio_buf,
                                                            nb_samples);
                            
                        }
                        fifo_write(&ffenc->fifo, buftmp, nb_samples_out * enc->channels * 2, 
                                   &ffenc->fifo.wptr);
                    }
                    ffenc = ffenc->next;
                }
                
                /* compress as many frame as possible with each audio codec */
                ffenc = first_codec;
                while (ffenc != NULL) {
                    enc = &ffenc->enc;
                    if (enc->codec->type == CODEC_TYPE_AUDIO) {
                        frame_bytes = enc->frame_size * 2 * enc->channels;
                        
                        while (fifo_read(&ffenc->fifo, audio_buf, frame_bytes, &ffenc->fifo.rptr) == 0) {
                            ret = avencoder_encode(enc,
                                                   audio_out, sizeof(audio_out), audio_buf);
                            write_packet(ffenc, audio_out, ret);
                        }
                    }
                    ffenc = ffenc->next;
                }
            }
        }

        if (use_video) {
            ret = v4l_read_picture (picture, width, height, 
                                    frame_number);
            if (ret < 0)
                break;
            ffenc = first_codec;
            while (ffenc != NULL) {
                enc = &ffenc->enc;
                if (enc->codec->type == CODEC_TYPE_VIDEO) {
                    int n1, n2;
                    /* feed each codec with its requested frame rate */
                    n1 = (frame_number * enc->rate) / frame_rate;
                    n2 = ((frame_number + 1) * enc->rate) / frame_rate;
                    if (n2 > n1) {
                        ret = avencoder_encode(enc, video_buffer, sizeof(video_buffer), picture);
                        write_packet(ffenc, video_buffer, ret);
                    }
                }
                ffenc = ffenc->next;
            }
            frame_number++;
        }
    }
    
    ffenc = first_codec;
    while (ffenc != NULL) {
        enc = &ffenc->enc;
        avencoder_close(enc);
        ffenc = ffenc->next;
    }
    close(audio_fd);
    return 0;
}


void help(void)
{
    printf("ffserver version 1.0, Copyright (c) 2000 Gerard Lantau\n"
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
    "ffserver version 1.0\n"
    "Copyright (c) 2000 Gerard Lantau\n"
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
    pthread_t http_server_tid;
    const char *config_filename;
    int c;

    /* codecs */
    register_avencoder(&ac3_encoder);
    register_avencoder(&mp2_encoder);
    register_avencoder(&mpeg1video_encoder);
    register_avencoder(&h263_encoder);
    register_avencoder(&rv10_encoder);
    register_avencoder(&mjpeg_encoder);

    /* audio video formats */
    register_avformat(&mp2_format);
    register_avformat(&ac3_format);
    register_avformat(&mpeg_mux_format);
    register_avformat(&mpeg1video_format);
    register_avformat(&h263_format);
    register_avformat(&rm_format);
    register_avformat(&ra_format);
    register_avformat(&asf_format);
    register_avformat(&mpjpeg_format);
    register_avformat(&jpeg_format);
    register_avformat(&swf_format);

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

    /* open log file if needed */
    if (logfilename[0] != '\0') {
        if (!strcmp(logfilename, "-"))
            logfile = stdout;
        else
            logfile = fopen(logfilename, "w");
    }

    /* init fifo */
    http_fifo_write_count = 0;
    if (fifo_init(&http_fifo, FIFO_MAX_SIZE) < 0) {
        fprintf(stderr, "Could not allow receive fifo\n");
        exit(1);
    }

    if (master_url[0] == '\0') {
        /* no master server: we grab ourself */

        /* launch server thread */
        if (pthread_create(&http_server_tid, NULL, 
                           http_server_thread, NULL) != 0) {
            fprintf(stderr, "Could not create http server thread\n");
            exit(1);
        }

        /* launch the audio / video grab */
        if (av_grab() < 0) {
            fprintf(stderr, "Could not start audio/video grab\n");
            exit(1);
        }
    } else {
        /* master server : no thread are needed */
        if (http_server(my_addr) < 0) {
            fprintf(stderr, "Could start http server\n");
            exit(1);
        }
    }

    return 0;
}
