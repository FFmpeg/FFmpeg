/*
 * Multiple format streaming server
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
#define HAVE_AV_CONFIG_H
#include "avformat.h"

#include <stdarg.h>
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
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>

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

#define IOBUFFER_INIT_SIZE 8192
#define PBUFFER_INIT_SIZE 8192

/* coef for exponential mean for bitrate estimation in statistics */
#define AVG_COEF 0.9

/* timeouts are in ms */
#define REQUEST_TIMEOUT (15 * 1000)
#define SYNC_TIMEOUT (10 * 1000)

typedef struct {
    INT64 count1, count2;
    long time1, time2;
} DataRateData;

/* context associated with one connection */
typedef struct HTTPContext {
    enum HTTPState state;
    int fd; /* socket file descriptor */
    struct sockaddr_in from_addr; /* origin */
    struct pollfd *poll_entry; /* used when polling */
    long timeout;
    UINT8 *buffer_ptr, *buffer_end;
    int http_error;
    struct HTTPContext *next;
    int got_key_frame; /* stream 0 => 1, stream 1 => 2, stream 2=> 4 */
    INT64 data_count;
    /* feed input */
    int feed_fd;
    /* input format handling */
    AVFormatContext *fmt_in;
    /* output format handling */
    struct FFStream *stream;
    /* -1 is invalid stream */
    int feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    int switch_feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    int switch_pending;
    AVFormatContext fmt_ctx;
    int last_packet_sent; /* true if last data packet was sent */
    int suppress_log;
    int bandwidth;
    long start_time;            /* In milliseconds - this wraps fairly often */
    DataRateData datarate;
    int wmp_client_id;
    char protocol[16];
    char method[16];
    char url[128];
    int buffer_size;
    UINT8 *buffer;
    int pbuffer_size;
    UINT8 *pbuffer;
} HTTPContext;

/* each generated stream is described here */
enum StreamType {
    STREAM_TYPE_LIVE,
    STREAM_TYPE_STATUS,
    STREAM_TYPE_REDIRECT,
};

/* description of each stream of the ffserver.conf file */
typedef struct FFStream {
    enum StreamType stream_type;
    char filename[1024];     /* stream filename */
    struct FFStream *feed;
    AVOutputFormat *fmt;
    int nb_streams;
    int prebuffer;      /* Number of millseconds early to start */
    long max_time;      /* Number of milliseconds to run */
    int send_on_key;
    AVStream *streams[MAX_STREAMS];
    int feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    char feed_filename[1024]; /* file name of the feed storage, or
                                 input file name for a stream */
    char author[512];
    char title[512];
    char copyright[512];
    char comment[512];
    pid_t pid;  /* Of ffmpeg process */
    time_t pid_start;  /* Of ffmpeg process */
    char **child_argv;
    struct FFStream *next;
    /* feed specific */
    int feed_opened;     /* true if someone if writing to feed */
    int is_feed;         /* true if it is a feed */
    int conns_served;
    INT64 bytes_served;
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

static int handle_http(HTTPContext *c);
static int http_parse_request(HTTPContext *c);
static int http_send_data(HTTPContext *c);
static void compute_stats(HTTPContext *c);
static int open_input_stream(HTTPContext *c, const char *info);
static int http_start_receive_data(HTTPContext *c);
static int http_receive_data(HTTPContext *c);

static const char *my_program_name;

static int ffserver_debug;
static int no_launch;
static int need_to_start_children;

int nb_max_connections;
int nb_connections;

int nb_max_bandwidth;
int nb_bandwidth;

static long cur_time;           // Making this global saves on passing it around everywhere

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
    
    if (logfile) {
        vfprintf(logfile, fmt, ap);
        fflush(logfile);
    }
    va_end(ap);
}

static void log_connection(HTTPContext *c)
{
    char buf1[32], buf2[32], *p;
    time_t ti;

    if (c->suppress_log) 
        return;

    /* XXX: reentrant function ? */
    p = inet_ntoa(c->from_addr.sin_addr);
    strcpy(buf1, p);
    ti = time(NULL);
    p = ctime(&ti);
    strcpy(buf2, p);
    p = buf2 + strlen(p) - 1;
    if (*p == '\n')
        *p = '\0';
    http_log("%s - - [%s] \"%s %s %s\" %d %lld\n", 
             buf1, buf2, c->method, c->url, c->protocol, (c->http_error ? c->http_error : 200), c->data_count);
}

static void update_datarate(DataRateData *drd, INT64 count)
{
    if (!drd->time1 && !drd->count1) {
        drd->time1 = drd->time2 = cur_time;
        drd->count1 = drd->count2 = count;
    } else {
        if (cur_time - drd->time2 > 5000) {
            drd->time1 = drd->time2;
            drd->count1 = drd->count2;
            drd->time2 = cur_time;
            drd->count2 = count;
        }
    }
}

/* In bytes per second */
static int compute_datarate(DataRateData *drd, INT64 count)
{
    if (cur_time == drd->time1)
        return 0;

    return ((count - drd->count1) * 1000) / (cur_time - drd->time1);
}

static void start_children(FFStream *feed)
{
    if (no_launch)
        return;

    for (; feed; feed = feed->next) {
        if (feed->child_argv && !feed->pid) {
            feed->pid_start = time(0);

            feed->pid = fork();

            if (feed->pid < 0) {
                fprintf(stderr, "Unable to create children\n");
                exit(1);
            }
            if (!feed->pid) {
                /* In child */
                char pathname[1024];
                char *slash;
                int i;

                for (i = 3; i < 256; i++) {
                    close(i);
                }

                if (!ffserver_debug) {
                    i = open("/dev/null", O_RDWR);
                    if (i)
                        dup2(i, 0);
                    dup2(i, 1);
                    dup2(i, 2);
                    if (i)
                        close(i);
                }

                pstrcpy(pathname, sizeof(pathname), my_program_name);

                slash = strrchr(pathname, '/');
                if (!slash) {
                    slash = pathname;
                } else {
                    slash++;
                }
                strcpy(slash, "ffmpeg");

                execvp(pathname, feed->child_argv);

                _exit(1);
            }
        }
    }
}

/* main loop of the http server */
static int http_server(struct sockaddr_in my_addr)
{
    int server_fd, tmp, ret;
    struct sockaddr_in from_addr;
    struct pollfd poll_table[HTTP_MAX_CONNECTIONS + 1], *poll_entry;
    HTTPContext *c, **cp;

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

    start_children(first_feed);

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
                poll_entry->events = POLLIN;/* Maybe this will work */
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

        if (need_to_start_children) {
            need_to_start_children = 0;
            start_children(first_feed);
        }

        /* now handle the events */

        cp = &first_http_ctx;
        while ((*cp) != NULL) {
            c = *cp;
            if (handle_http (c) < 0) {
                /* close and free the connection */
                log_connection(c);
                close(c->fd);
                if (c->fmt_in)
                    av_close_input_file(c->fmt_in);
                *cp = c->next;
                nb_bandwidth -= c->bandwidth;
                av_free(c->buffer);
                av_free(c->pbuffer);
                av_free(c);
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
                    c = NULL;
                } else {
                    /* add a new connection */
                    c = av_mallocz(sizeof(HTTPContext));
                    if (c) {
                        c->next = first_http_ctx;
                        first_http_ctx = c;
                        c->fd = fd;
                        c->poll_entry = NULL;
                        c->from_addr = from_addr;
                        c->state = HTTPSTATE_WAIT_REQUEST;
                        c->buffer = av_malloc(c->buffer_size = IOBUFFER_INIT_SIZE);
                        c->pbuffer = av_malloc(c->pbuffer_size = PBUFFER_INIT_SIZE);
                        if (!c->buffer || !c->pbuffer) {
                            av_free(c->buffer);
                            av_free(c->pbuffer);
                            av_freep(&c);
                        } else {
                            c->buffer_ptr = c->buffer;
                            c->buffer_end = c->buffer + c->buffer_size;
                            c->timeout = cur_time + REQUEST_TIMEOUT;
                            c->start_time = cur_time;
                            nb_connections++;
                        }
                    }
                }
                if (!c) {
                    close(fd);
                }
            }
        }
        poll_entry++;
    }
}

static int handle_http(HTTPContext *c)
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
            if (c->stream)
                c->stream->bytes_served += len;
            c->data_count += len;
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
        if (c->poll_entry->revents & (POLLIN | POLLERR | POLLHUP))
            return -1;

        /* nothing to do, we'll be waken up by incoming feed packets */
        break;
    default:
        return -1;
    }
    return 0;
}

static int extract_rates(char *rates, int ratelen, const char *request)
{
    const char *p;

    for (p = request; *p && *p != '\r' && *p != '\n'; ) {
        if (strncasecmp(p, "Pragma:", 7) == 0) {
            const char *q = p + 7;

            while (*q && *q != '\n' && isspace(*q))
                q++;

            if (strncasecmp(q, "stream-switch-entry=", 20) == 0) {
                int stream_no;
                int rate_no;

                q += 20;

                memset(rates, 0xff, ratelen);

                while (1) {
                    while (*q && *q != '\n' && *q != ':')
                        q++;

                    if (sscanf(q, ":%d:%d", &stream_no, &rate_no) != 2) {
                        break;
                    }
                    stream_no--;
                    if (stream_no < ratelen && stream_no >= 0) {
                        rates[stream_no] = rate_no;
                    }

                    while (*q && *q != '\n' && !isspace(*q))
                        q++;
                }

                return 1;
            }
        }
        p = strchr(p, '\n');
        if (!p)
            break;

        p++;
    }

    return 0;
}

static int find_stream_in_feed(FFStream *feed, AVCodecContext *codec, int bit_rate)
{
    int i;
    int best_bitrate = 100000000;
    int best = -1;

    for (i = 0; i < feed->nb_streams; i++) {
        AVCodecContext *feed_codec = &feed->streams[i]->codec;

        if (feed_codec->codec_id != codec->codec_id ||
            feed_codec->sample_rate != codec->sample_rate ||
            feed_codec->width != codec->width ||
            feed_codec->height != codec->height) {
            continue;
        }

        /* Potential stream */

        /* We want the fastest stream less than bit_rate, or the slowest 
         * faster than bit_rate
         */

        if (feed_codec->bit_rate <= bit_rate) {
            if (best_bitrate > bit_rate || feed_codec->bit_rate > best_bitrate) {
                best_bitrate = feed_codec->bit_rate;
                best = i;
            }
        } else {
            if (feed_codec->bit_rate < best_bitrate) {
                best_bitrate = feed_codec->bit_rate;
                best = i;
            }
        }
    }

    return best;
}

static int modify_current_stream(HTTPContext *c, char *rates)
{
    int i;
    FFStream *req = c->stream;
    int action_required = 0;

    for (i = 0; i < req->nb_streams; i++) {
        AVCodecContext *codec = &req->streams[i]->codec;

        switch(rates[i]) {
            case 0:
                c->switch_feed_streams[i] = req->feed_streams[i];
                break;
            case 1:
                c->switch_feed_streams[i] = find_stream_in_feed(req->feed, codec, codec->bit_rate / 2);
                break;
            case 2:
                /* Wants off or slow */
                c->switch_feed_streams[i] = find_stream_in_feed(req->feed, codec, codec->bit_rate / 4);
#ifdef WANTS_OFF
                /* This doesn't work well when it turns off the only stream! */
                c->switch_feed_streams[i] = -2;
                c->feed_streams[i] = -2;
#endif
                break;
        }

        if (c->switch_feed_streams[i] >= 0 && c->switch_feed_streams[i] != c->feed_streams[i])
            action_required = 1;
    }

    return action_required;
}


static void do_switch_stream(HTTPContext *c, int i)
{
    if (c->switch_feed_streams[i] >= 0) {
#ifdef PHILIP        
        c->feed_streams[i] = c->switch_feed_streams[i];
#endif

        /* Now update the stream */
    }
    c->switch_feed_streams[i] = -1;
}

/* parse http request and prepare header */
static int http_parse_request(HTTPContext *c)
{
    char *p;
    int post;
    int doing_asx;
    int doing_asf_redirector;
    int doing_ram;
    char cmd[32];
    char info[1024], *filename;
    char url[1024], *q;
    char protocol[32];
    char msg[1024];
    const char *mime_type;
    FFStream *stream;
    int i;
    char ratebuf[32];
    char *useragent = 0;

    p = c->buffer;
    q = cmd;
    while (!isspace(*p) && *p != '\0') {
        if ((q - cmd) < sizeof(cmd) - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';

    pstrcpy(c->method, sizeof(c->method), cmd);

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

    pstrcpy(c->url, sizeof(c->url), url);

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

    pstrcpy(c->protocol, sizeof(c->protocol), protocol);
    
    /* find the filename and the optional info string in the request */
    p = url;
    if (*p == '/')
        p++;
    filename = p;
    p = strchr(p, '?');
    if (p) {
        pstrcpy(info, sizeof(info), p);
        *p = '\0';
    } else {
        info[0] = '\0';
    }

    for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
        if (strncasecmp(p, "User-Agent:", 11) == 0) {
            useragent = p + 11;
            if (*useragent && *useragent != '\n' && isspace(*useragent))
                useragent++;
            break;
        }
        p = strchr(p, '\n');
        if (!p)
            break;

        p++;
    }

    if (strlen(filename) > 4 && strcmp(".asx", filename + strlen(filename) - 4) == 0) {
        doing_asx = 1;
        filename[strlen(filename)-1] = 'f';
    } else {
        doing_asx = 0;
    }

    if (strlen(filename) > 4 && strcmp(".asf", filename + strlen(filename) - 4) == 0 &&
        (!useragent || strncasecmp(useragent, "NSPlayer", 8) != 0)) {
        /* if this isn't WMP or lookalike, return the redirector file */
        doing_asf_redirector = 1;
    } else {
        doing_asf_redirector = 0;
    }

    if (strlen(filename) > 4 && 
        (strcmp(".rpm", filename + strlen(filename) - 4) == 0 ||
         strcmp(".ram", filename + strlen(filename) - 4) == 0)) {
        doing_ram = 1;
        strcpy(filename + strlen(filename)-2, "m");
    } else {
        doing_ram = 0;
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
    memcpy(c->feed_streams, stream->feed_streams, sizeof(c->feed_streams));
    memset(c->switch_feed_streams, -1, sizeof(c->switch_feed_streams));

    if (stream->stream_type == STREAM_TYPE_REDIRECT) {
        c->http_error = 301;
        q = c->buffer;
        q += sprintf(q, "HTTP/1.0 301 Moved\r\n");
        q += sprintf(q, "Location: %s\r\n", stream->feed_filename);
        q += sprintf(q, "Content-type: text/html\r\n");
        q += sprintf(q, "\r\n");
        q += sprintf(q, "<html><head><title>Moved</title></head><body>\r\n");
        q += sprintf(q, "You should be <a href=\"%s\">redirected</a>.\r\n", stream->feed_filename);
        q += sprintf(q, "</body></html>\r\n");

        /* prepare output buffer */
        c->buffer_ptr = c->buffer;
        c->buffer_end = q;
        c->state = HTTPSTATE_SEND_HEADER;
        return 0;
    }

    /* If this is WMP, get the rate information */
    if (extract_rates(ratebuf, sizeof(ratebuf), c->buffer)) {
        if (modify_current_stream(c, ratebuf)) {
            for (i = 0; i < sizeof(c->feed_streams) / sizeof(c->feed_streams[0]); i++) {
                if (c->switch_feed_streams[i] >= 0)
                    do_switch_stream(c, i);
            }
        }
    }

    if (post == 0 && stream->stream_type == STREAM_TYPE_LIVE) {
        /* See if we meet the bandwidth requirements */
        for(i=0;i<stream->nb_streams;i++) {
            AVStream *st = stream->streams[i];
            switch(st->codec.codec_type) {
            case CODEC_TYPE_AUDIO:
                c->bandwidth += st->codec.bit_rate;
                break;
            case CODEC_TYPE_VIDEO:
                c->bandwidth += st->codec.bit_rate;
                break;
            default:
                av_abort();
            }
        }
    }

    c->bandwidth /= 1000;
    nb_bandwidth += c->bandwidth;

    if (post == 0 && nb_max_bandwidth < nb_bandwidth) {
        c->http_error = 200;
        q = c->buffer;
        q += sprintf(q, "HTTP/1.0 200 Server too busy\r\n");
        q += sprintf(q, "Content-type: text/html\r\n");
        q += sprintf(q, "\r\n");
        q += sprintf(q, "<html><head><title>Too busy</title></head><body>\r\n");
        q += sprintf(q, "The server is too busy to serve your request at this time.<p>\r\n");
        q += sprintf(q, "The bandwidth being served (including your stream) is %dkbit/sec, and this exceeds the limit of %dkbit/sec\r\n",
            nb_bandwidth, nb_max_bandwidth);
        q += sprintf(q, "</body></html>\r\n");

        /* prepare output buffer */
        c->buffer_ptr = c->buffer;
        c->buffer_end = q;
        c->state = HTTPSTATE_SEND_HEADER;
        return 0;
    }
    
    if (doing_asx || doing_ram || doing_asf_redirector) {
        char *hostinfo = 0;
        
        for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
            if (strncasecmp(p, "Host:", 5) == 0) {
                hostinfo = p + 5;
                break;
            }
            p = strchr(p, '\n');
            if (!p)
                break;

            p++;
        }

        if (hostinfo) {
            char *eoh;
            char hostbuf[260];

            while (isspace(*hostinfo))
                hostinfo++;

            eoh = strchr(hostinfo, '\n');
            if (eoh) {
                if (eoh[-1] == '\r')
                    eoh--;

                if (eoh - hostinfo < sizeof(hostbuf) - 1) {
                    memcpy(hostbuf, hostinfo, eoh - hostinfo);
                    hostbuf[eoh - hostinfo] = 0;

                    c->http_error = 200;
                    q = c->buffer;
                    if (doing_asx) {
                        q += sprintf(q, "HTTP/1.0 200 ASX Follows\r\n");
                        q += sprintf(q, "Content-type: video/x-ms-asf\r\n");
                        q += sprintf(q, "\r\n");
                        q += sprintf(q, "<ASX Version=\"3\">\r\n");
                        q += sprintf(q, "<!-- Autogenerated by ffserver -->\r\n");
                        q += sprintf(q, "<ENTRY><REF HREF=\"http://%s/%s%s\"/></ENTRY>\r\n", 
                                hostbuf, filename, info);
                        q += sprintf(q, "</ASX>\r\n");
                    } else if (doing_ram) {
                        q += sprintf(q, "HTTP/1.0 200 RAM Follows\r\n");
                        q += sprintf(q, "Content-type: audio/x-pn-realaudio\r\n");
                        q += sprintf(q, "\r\n");
                        q += sprintf(q, "# Autogenerated by ffserver\r\n");
                        q += sprintf(q, "http://%s/%s%s\r\n", 
                                hostbuf, filename, info);
                    } else if (doing_asf_redirector) {
                        q += sprintf(q, "HTTP/1.0 200 ASF Redirect follows\r\n");
                        q += sprintf(q, "Content-type: video/x-ms-asf\r\n");
                        q += sprintf(q, "\r\n");
                        q += sprintf(q, "[Reference]\r\n");
                        q += sprintf(q, "Ref1=http://%s/%s%s\r\n", 
                                hostbuf, filename, info);
                    } else
                        av_abort();

                    /* prepare output buffer */
                    c->buffer_ptr = c->buffer;
                    c->buffer_end = q;
                    c->state = HTTPSTATE_SEND_HEADER;
                    return 0;
                }
            }
        }

        sprintf(msg, "ASX/RAM file not handled");
        goto send_error;
    }

    stream->conns_served++;

    /* XXX: add there authenticate and IP match */

    if (post) {
        /* if post, it means a feed is being sent */
        if (!stream->is_feed) {
            /* However it might be a status report from WMP! Lets log the data
             * as it might come in handy one day
             */
            char *logline = 0;
            int client_id = 0;
            
            for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
                if (strncasecmp(p, "Pragma: log-line=", 17) == 0) {
                    logline = p;
                    break;
                }
                if (strncasecmp(p, "Pragma: client-id=", 18) == 0) {
                    client_id = strtol(p + 18, 0, 10);
                }
                p = strchr(p, '\n');
                if (!p)
                    break;

                p++;
            }

            if (logline) {
                char *eol = strchr(logline, '\n');

                logline += 17;

                if (eol) {
                    if (eol[-1] == '\r')
                        eol--;
                    http_log("%.*s\n", eol - logline, logline);
                    c->suppress_log = 1;
                }
            }

#ifdef DEBUG_WMP
            http_log("\nGot request:\n%s\n", c->buffer);
#endif

            if (client_id && extract_rates(ratebuf, sizeof(ratebuf), c->buffer)) {
                HTTPContext *wmpc;

                /* Now we have to find the client_id */
                for (wmpc = first_http_ctx; wmpc; wmpc = wmpc->next) {
                    if (wmpc->wmp_client_id == client_id)
                        break;
                }

                if (wmpc) {
                    if (modify_current_stream(wmpc, ratebuf)) {
                        wmpc->switch_pending = 1;
                    }
                }
            }
            
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

#ifdef DEBUG_WMP
    if (strcmp(stream->filename + strlen(stream->filename) - 4, ".asf") == 0) {
        http_log("\nGot request:\n%s\n", c->buffer);
    }
#endif

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
    q += sprintf(q, "Pragma: no-cache\r\n");

    /* for asf, we need extra headers */
    if (!strcmp(c->stream->fmt->name,"asf")) {
        /* Need to allocate a client id */
        static int wmp_session;

        if (!wmp_session)
            wmp_session = time(0) & 0xffffff;

        c->wmp_client_id = ++wmp_session;

        q += sprintf(q, "Server: Cougar 4.1.0.3923\r\nCache-Control: no-cache\r\nPragma: client-id=%d\r\nPragma: features=\"broadcast\"\r\n", c->wmp_client_id);
        mime_type = "application/octet-stream"; 
    }
    q += sprintf(q, "Content-Type: %s\r\n", mime_type);
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

static int fmt_bytecount(char *q, INT64 count)
{
    static const char *suffix = " kMGTP";
    const char *s;

    for (s = suffix; count >= 100000 && s[1]; count /= 1000, s++) {
    }

    return sprintf(q, "%lld%c", count, *s);
}

static void compute_stats(HTTPContext *c)
{
    HTTPContext *c1;
    FFStream *stream;
    char *q, *p;
    time_t ti;
    int i;
    char *new_buffer;

    new_buffer = av_malloc(65536);
    if (new_buffer) {
        av_free(c->buffer);
        c->buffer_size = 65536;
        c->buffer = new_buffer;
        c->buffer_ptr = c->buffer;
        c->buffer_end = c->buffer + c->buffer_size;
    }

    q = c->buffer;
    q += sprintf(q, "HTTP/1.0 200 OK\r\n");
    q += sprintf(q, "Content-type: %s\r\n", "text/html");
    q += sprintf(q, "Pragma: no-cache\r\n");
    q += sprintf(q, "\r\n");
    
    q += sprintf(q, "<HEAD><TITLE>FFServer Status</TITLE>\n");
    if (c->stream->feed_filename) {
        q += sprintf(q, "<link rel=\"shortcut icon\" href=\"%s\">\n", c->stream->feed_filename);
    }
    q += sprintf(q, "</HEAD>\n<BODY>");
    q += sprintf(q, "<H1>FFServer Status</H1>\n");
    /* format status */
    q += sprintf(q, "<H2>Available Streams</H2>\n");
    q += sprintf(q, "<TABLE cellspacing=0 cellpadding=4>\n");
    q += sprintf(q, "<TR><Th valign=top>Path<th align=left>Served<br>Conns<Th><br>bytes<Th valign=top>Format<Th>Bit rate<br>kbits/s<Th align=left>Video<br>kbits/s<th><br>Codec<Th align=left>Audio<br>kbits/s<th><br>Codec<Th align=left valign=top>Feed\n");
    stream = first_stream;
    while (stream != NULL) {
        char sfilename[1024];
        char *eosf;

        if (stream->feed != stream) {
            pstrcpy(sfilename, sizeof(sfilename) - 1, stream->filename);
            eosf = sfilename + strlen(sfilename);
            if (eosf - sfilename >= 4) {
                if (strcmp(eosf - 4, ".asf") == 0) {
                    strcpy(eosf - 4, ".asx");
                } else if (strcmp(eosf - 3, ".rm") == 0) {
                    strcpy(eosf - 3, ".ram");
                }
            }
            
            q += sprintf(q, "<TR><TD><A HREF=\"/%s\">%s</A> ", 
                         sfilename, stream->filename);
            q += sprintf(q, "<td align=right> %d <td align=right> ",
                        stream->conns_served);
            q += fmt_bytecount(q, stream->bytes_served);
            switch(stream->stream_type) {
            case STREAM_TYPE_LIVE:
                {
                    int audio_bit_rate = 0;
                    int video_bit_rate = 0;
                    char *audio_codec_name = "";
                    char *video_codec_name = "";
                    char *audio_codec_name_extra = "";
                    char *video_codec_name_extra = "";

                    for(i=0;i<stream->nb_streams;i++) {
                        AVStream *st = stream->streams[i];
                        AVCodec *codec = avcodec_find_encoder(st->codec.codec_id);
                        switch(st->codec.codec_type) {
                        case CODEC_TYPE_AUDIO:
                            audio_bit_rate += st->codec.bit_rate;
                            if (codec) {
                                if (*audio_codec_name)
                                    audio_codec_name_extra = "...";
                                audio_codec_name = codec->name;
                            }
                            break;
                        case CODEC_TYPE_VIDEO:
                            video_bit_rate += st->codec.bit_rate;
                            if (codec) {
                                if (*video_codec_name)
                                    video_codec_name_extra = "...";
                                video_codec_name = codec->name;
                            }
                            break;
                        default:
                            av_abort();
                        }
                    }
                    q += sprintf(q, "<TD align=center> %s <TD align=right> %d <TD align=right> %d <TD> %s %s <TD align=right> %d <TD> %s %s", 
                                 stream->fmt->name,
                                 (audio_bit_rate + video_bit_rate) / 1000,
                                 video_bit_rate / 1000, video_codec_name, video_codec_name_extra,
                                 audio_bit_rate / 1000, audio_codec_name, audio_codec_name_extra);
                    if (stream->feed) {
                        q += sprintf(q, "<TD>%s", stream->feed->filename);
                    } else {
                        q += sprintf(q, "<TD>%s", stream->feed_filename);
                    }
                    q += sprintf(q, "\n");
                }
                break;
            default:
                q += sprintf(q, "<TD align=center> - <TD align=right> - <TD align=right> - <td><td align=right> - <TD>\n");
                break;
            }
        }
        stream = stream->next;
    }
    q += sprintf(q, "</TABLE>\n");

    stream = first_stream;
    while (stream != NULL) {
        if (stream->feed == stream) {
            q += sprintf(q, "<h2>Feed %s</h2>", stream->filename);
            if (stream->pid) {
                FILE *pid_stat;
                char ps_cmd[64];

                q += sprintf(q, "Running as pid %d.\n", stream->pid);

#ifdef linux
                /* This is somewhat linux specific I guess */
                snprintf(ps_cmd, sizeof(ps_cmd), "ps -o \"%%cpu,bsdtime\" --no-headers %d", stream->pid);

                pid_stat = popen(ps_cmd, "r");
                if (pid_stat) {
                    char cpuperc[10];
                    char cpuused[64];

                    if (fscanf(pid_stat, "%10s %64s", cpuperc, cpuused) == 2) {
                        q += sprintf(q, "Currently using %s%% of the cpu. Total time used %s.\n",
                            cpuperc, cpuused);
                    }
                    fclose(pid_stat);
                }
#endif

                q += sprintf(q, "<p>");
            }
            q += sprintf(q, "<table cellspacing=0 cellpadding=4><tr><th>Stream<th>type<th>kbits/s<th align=left>codec<th align=left>Parameters\n");

            for (i = 0; i < stream->nb_streams; i++) {
                AVStream *st = stream->streams[i];
                AVCodec *codec = avcodec_find_encoder(st->codec.codec_id);
                char *type = "unknown";
                char parameters[64];

                parameters[0] = 0;

                switch(st->codec.codec_type) {
                case CODEC_TYPE_AUDIO:
                    type = "audio";
                    break;
                case CODEC_TYPE_VIDEO:
                    type = "video";
                    sprintf(parameters, "%dx%d, q=%d-%d, fps=%d", st->codec.width, st->codec.height,
                                st->codec.qmin, st->codec.qmax, st->codec.frame_rate / FRAME_RATE_BASE);
                    break;
                default:
                    av_abort();
                }
                q += sprintf(q, "<tr><td align=right>%d<td>%s<td align=right>%d<td>%s<td>%s\n",
                        i, type, st->codec.bit_rate/1000, codec ? codec->name : "", parameters);
            }
            q += sprintf(q, "</table>\n");

        }       
        stream = stream->next;
    }
    
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
    q += sprintf(q, "<H2>Connection Status</H2>\n");

    q += sprintf(q, "Number of connections: %d / %d<BR>\n",
                 nb_connections, nb_max_connections);

    q += sprintf(q, "Bandwidth in use: %dk / %dk<BR>\n",
                 nb_bandwidth, nb_max_bandwidth);

    q += sprintf(q, "<TABLE>\n");
    q += sprintf(q, "<TR><Th>#<Th>File<Th>IP<Th>State<Th>Target bits/sec<Th>Actual bits/sec<Th>Bytes transferred\n");
    c1 = first_http_ctx;
    i = 0;
    while (c1 != NULL && q < (char *) c->buffer + c->buffer_size - 2048) {
        int bitrate;
        int j;

        bitrate = 0;
        for (j = 0; j < c1->stream->nb_streams; j++) {
            if (c1->feed_streams[j] >= 0) {
                bitrate += c1->stream->feed->streams[c1->feed_streams[j]]->codec.bit_rate;
            }
        }

        i++;
        p = inet_ntoa(c1->from_addr.sin_addr);
        q += sprintf(q, "<TR><TD><B>%d</B><TD>%s%s <TD> %s <TD> %s <td align=right>", 
                     i, c1->stream->filename, 
                     c1->state == HTTPSTATE_RECEIVE_DATA ? "(input)" : "",
                     p, 
                     http_state[c1->state]);
        q += fmt_bytecount(q, bitrate);
        q += sprintf(q, "<td align=right>");
        q += fmt_bytecount(q, compute_datarate(&c1->datarate, c1->data_count) * 8);
        q += sprintf(q, "<td align=right>");
        q += fmt_bytecount(q, c1->data_count);
        *q++ = '\n';
        c1 = c1->next;
    }
    q += sprintf(q, "</TABLE>\n");
    
    /* date */
    ti = time(NULL);
    p = ctime(&ti);
    q += sprintf(q, "<HR size=1 noshade>Generated at %s", p);
    q += sprintf(q, "</BODY>\n</HTML>\n");

    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
}


static void http_write_packet(void *opaque, 
                              unsigned char *buf, int size)
{
    HTTPContext *c = opaque;

    if (c->buffer_ptr == c->buffer_end || !c->buffer_ptr)
        c->buffer_ptr = c->buffer_end = c->buffer;

    if (c->buffer_end - c->buffer + size > c->buffer_size) {
        int new_buffer_size = c->buffer_size * 2;
        UINT8 *new_buffer;

        if (new_buffer_size <= c->buffer_end - c->buffer + size) {
            new_buffer_size = c->buffer_end - c->buffer + size + c->buffer_size;
        }

        new_buffer = av_malloc(new_buffer_size);
        if (new_buffer) {
            memcpy(new_buffer, c->buffer, c->buffer_end - c->buffer);
            c->buffer_end += (new_buffer - c->buffer);
            c->buffer_ptr += (new_buffer - c->buffer);
            av_free(c->buffer);
            c->buffer = new_buffer;
            c->buffer_size = new_buffer_size;
        } else {
            av_abort();
        }
    }

    memcpy(c->buffer_end, buf, size);
    c->buffer_end += size;
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
        } else if (find_info_tag(buf, sizeof(buf), "buffer", info)) {
            int prebuffer = strtol(buf, 0, 10);
            stream_pos = av_gettime() - prebuffer * 1000000;
        } else {
            stream_pos = av_gettime() - c->stream->prebuffer * 1000;
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
    if (av_open_input_file(&s, input_filename, NULL, buf_size, NULL) < 0)
        return -1;
    c->fmt_in = s;

    if (c->fmt_in->iformat->read_seek) {
        c->fmt_in->iformat->read_seek(c->fmt_in, stream_pos);
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
        pstrcpy(c->fmt_ctx.author, sizeof(c->fmt_ctx.author), c->stream->author);
        pstrcpy(c->fmt_ctx.comment, sizeof(c->fmt_ctx.comment), c->stream->comment);
        pstrcpy(c->fmt_ctx.copyright, sizeof(c->fmt_ctx.copyright), c->stream->copyright);
        pstrcpy(c->fmt_ctx.title, sizeof(c->fmt_ctx.title), c->stream->title);

        if (c->stream->feed) {
            /* open output stream by using specified codecs */
            c->fmt_ctx.oformat = c->stream->fmt;
            c->fmt_ctx.nb_streams = c->stream->nb_streams;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                AVStream *st;
                st = av_mallocz(sizeof(AVStream));
                c->fmt_ctx.streams[i] = st;
                if (c->stream->feed == c->stream)
                    memcpy(st, c->stream->streams[i], sizeof(AVStream));
                else
                    memcpy(st, c->stream->feed->streams[c->stream->feed_streams[i]], sizeof(AVStream));

                st->codec.frame_number = 0; /* XXX: should be done in
                                               AVStream, not in codec */
            }
            c->got_key_frame = 0;
        } else {
            /* open output stream by using codecs in specified file */
            c->fmt_ctx.oformat = c->stream->fmt;
            c->fmt_ctx.nb_streams = c->fmt_in->nb_streams;
            for(i=0;i<c->fmt_ctx.nb_streams;i++) {
                AVStream *st;
                st = av_mallocz(sizeof(AVStream));
                c->fmt_ctx.streams[i] = st;
                memcpy(st, c->fmt_in->streams[i], sizeof(AVStream));
                st->codec.frame_number = 0; /* XXX: should be done in
                                               AVStream, not in codec */
            }
            c->got_key_frame = 0;
        }
        init_put_byte(&c->fmt_ctx.pb, c->pbuffer, c->pbuffer_size,
                      1, c, NULL, http_write_packet, NULL);
        c->fmt_ctx.pb.is_streamed = 1;
        /* prepare header */
        av_write_header(&c->fmt_ctx);
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
            c->got_key_frame = 0;
        }
        
        start_rptr = c->rptr;
        if (fifo_read(&http_fifo, (UINT8 *)&hdr, sizeof(hdr), &c->rptr) < 0)
            return 0;
        payload_size = ntohs(hdr.payload_size);
        payload = av_malloc(payload_size);
        if (fifo_read(&http_fifo, payload, payload_size, &c->rptr) < 0) {
            /* cannot read all the payload */
            av_free(payload);
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
                        c->got_key_frame |= 1 << i;
                    if (c->got_key_frame & (1 << i)) {
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
        av_free(payload);
#endif
        {
            AVPacket pkt;

            /* read a packet from the input stream */
            if (c->stream->feed) {
                ffm_set_write_index(c->fmt_in, 
                                    c->stream->feed->feed_write_index,
                                    c->stream->feed->feed_size);
            }

            if (c->stream->max_time && 
                c->stream->max_time + c->start_time - cur_time < 0) {
                /* We have timed out */
                c->state = HTTPSTATE_SEND_DATA_TRAILER;
            } else if (av_read_packet(c->fmt_in, &pkt) < 0) {
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
                    if (c->switch_pending) {
                        c->switch_pending = 0;
                        for(i=0;i<c->stream->nb_streams;i++) {
                            if (c->switch_feed_streams[i] == pkt.stream_index) {
                                if (pkt.flags & PKT_FLAG_KEY) {
                                    do_switch_stream(c, i);
                                }
                            }
                            if (c->switch_feed_streams[i] >= 0) {
                                c->switch_pending = 1;
                            }
                        }
                    }
                    for(i=0;i<c->stream->nb_streams;i++) {
                        if (c->feed_streams[i] == pkt.stream_index) {
                            pkt.stream_index = i;
                            if (pkt.flags & PKT_FLAG_KEY) {
                                c->got_key_frame |= 1 << i;
                            }
                            /* See if we have all the key frames, then 
                             * we start to send. This logic is not quite
                             * right, but it works for the case of a 
                             * single video stream with one or more
                             * audio streams (for which every frame is 
                             * typically a key frame). 
                             */
                            if (!c->stream->send_on_key || ((c->got_key_frame + 1) >> c->stream->nb_streams)) {
                                goto send_it;
                            }
                        }
                    }
                } else {
                    AVCodecContext *codec;
                send_it:
                    /* Fudge here */
                    codec = &c->fmt_ctx.streams[pkt.stream_index]->codec;

                    codec->key_frame = ((pkt.flags & PKT_FLAG_KEY) != 0);

#ifdef PJSG
                    if (codec->codec_type == CODEC_TYPE_AUDIO) {
                        codec->frame_size = (codec->sample_rate * pkt.duration + 500000) / 1000000;
                        /* printf("Calculated size %d, from sr %d, duration %d\n", codec->frame_size, codec->sample_rate, pkt.duration); */
                    }
#endif

                    if (av_write_packet(&c->fmt_ctx, &pkt, 0))
                        c->state = HTTPSTATE_SEND_DATA_TRAILER;

                    codec->frame_number++;
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
        av_write_trailer(&c->fmt_ctx);
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
            continue;
        } else {
            /* state change requested */
            return 0;
        }
    }

    if (c->buffer_end > c->buffer_ptr) {
        len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                /* error : close connection */
                return -1;
            }
        } else {
            c->buffer_ptr += len;
            c->data_count += len;
            update_datarate(&c->datarate, c->data_count);
            if (c->stream)
                c->stream->bytes_served += len;
        }
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
    HTTPContext *c1;

    if (c->buffer_end > c->buffer_ptr) {
        int len;

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
            update_datarate(&c->datarate, c->data_count);
        }
    }

    if (c->buffer_ptr >= c->buffer_end) {
        FFStream *feed = c->stream;
        /* a packet has been received : write it in the store, except
           if header */
        if (c->data_count > FFM_PACKET_SIZE) {
            
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
        } else {
            /* We have a header in our hands that contains useful data */
            AVFormatContext s;
            AVInputFormat *fmt_in;
            ByteIOContext *pb = &s.pb;
            int i;

            memset(&s, 0, sizeof(s));

            url_open_buf(pb, c->buffer, c->buffer_end - c->buffer, URL_RDONLY);
            pb->buf_end = c->buffer_end;        /* ?? */
            pb->is_streamed = 1;

            /* use feed output format name to find corresponding input format */
            fmt_in = av_find_input_format(feed->fmt->name);
            if (!fmt_in)
                goto fail;

            s.priv_data = av_mallocz(fmt_in->priv_data_size);
            if (!s.priv_data)
                goto fail;

            if (fmt_in->read_header(&s, 0) < 0) {
                av_freep(&s.priv_data);
                goto fail;
            }

            /* Now we have the actual streams */
            if (s.nb_streams != feed->nb_streams) {
                av_freep(&s.priv_data);
                goto fail;
            }
            for (i = 0; i < s.nb_streams; i++) {
                memcpy(&feed->streams[i]->codec, 
                       &s.streams[i]->codec, sizeof(AVCodecContext));
            } 
            av_freep(&s.priv_data);
        }
        c->buffer_ptr = c->buffer;
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
        if (av1->codec_id == av->codec_id &&
            av1->codec_type == av->codec_type &&
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
            default:
                av_abort();
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
            }
        }
    }

    /* gather all streams */
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        feed = stream->feed;
        if (feed) {
            if (stream->is_feed) {
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
            s->oformat = feed->fmt;
            s->nb_streams = feed->nb_streams;
            for(i=0;i<s->nb_streams;i++) {
                AVStream *st;
                st = feed->streams[i];
                s->streams[i] = st;
            }
            av_write_header(s);
            /* XXX: need better api */
            av_freep(&s->priv_data);
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
        /* Bitrate tolerance is less for streaming */
        if (av->bit_rate_tolerance == 0)
            av->bit_rate_tolerance = av->bit_rate / 4;
        if (av->qmin == 0)
            av->qmin = 3;
        if (av->qmax == 0)
            av->qmax = 31;
        if (av->max_qdiff == 0)
            av->max_qdiff = 3;
        av->qcompress = 0.5;
        av->qblur = 0.5;

        break;
    default:
        av_abort();
    }

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        return;
    stream->streams[stream->nb_streams++] = st;
    memcpy(&st->codec, av, sizeof(AVCodecContext));
}

int opt_audio_codec(const char *arg)
{
    AVCodec *p;

    p = first_avcodec;
    while (p) {
        if (!strcmp(p->name, arg) && p->type == CODEC_TYPE_AUDIO)
            break;
        p = p->next;
    }
    if (p == NULL) {
        return CODEC_ID_NONE;
    }

    return p->id;
}

int opt_video_codec(const char *arg)
{
    AVCodec *p;

    p = first_avcodec;
    while (p) {
        if (!strcmp(p->name, arg) && p->type == CODEC_TYPE_VIDEO)
            break;
        p = p->next;
    }
    if (p == NULL) {
        return CODEC_ID_NONE;
    }

    return p->id;
}

int parse_ffconfig(const char *filename)
{
    FILE *f;
    char line[1024];
    char cmd[64];
    char arg[1024];
    const char *p;
    int val, errors, line_num;
    FFStream **last_stream, *stream, *redirect;
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
    redirect = NULL;
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
        } else if (!strcasecmp(cmd, "MaxBandwidth")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 10 || val > 100000) {
                fprintf(stderr, "%s:%d: Invalid MaxBandwidth: %s\n", 
                        filename, line_num, arg);
                errors++;
            } else {
                nb_max_bandwidth = val;
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
        } else if (!strcasecmp(cmd, "Launch")) {
            if (feed) {
                int i;

                feed->child_argv = (char **) av_mallocz(64 * sizeof(char *));

                feed->child_argv[0] = av_malloc(7);
                strcpy(feed->child_argv[0], "ffmpeg");

                for (i = 1; i < 62; i++) {
                    char argbuf[256];

                    get_arg(argbuf, sizeof(argbuf), &p);
                    if (!argbuf[0])
                        break;

                    feed->child_argv[i] = av_malloc(strlen(argbuf + 1));
                    strcpy(feed->child_argv[i], argbuf);
                }

                feed->child_argv[i] = av_malloc(30 + strlen(feed->filename));

                snprintf(feed->child_argv[i], 256, "http://127.0.0.1:%d/%s", 
                    ntohs(my_addr.sin_port), feed->filename);
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
            } else {
                /* Make sure that we start out clean */
                if (unlink(feed->feed_filename) < 0 
                    && errno != ENOENT) {
                    fprintf(stderr, "%s:%d: Unable to clean old feed file '%s': %s\n",
                        filename, line_num, feed->feed_filename, strerror(errno));
                    errors++;
                }
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
        } else if (!strcasecmp(cmd, "FaviconURL")) {
            if (stream && stream->stream_type == STREAM_TYPE_STATUS) {
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
            } else {
                fprintf(stderr, "%s:%d: FaviconURL only permitted for status streams\n", 
                            filename, line_num);
                errors++;
            }
        } else if (!strcasecmp(cmd, "Author")) {
            if (stream) {
                get_arg(stream->author, sizeof(stream->author), &p);
            }
        } else if (!strcasecmp(cmd, "Comment")) {
            if (stream) {
                get_arg(stream->comment, sizeof(stream->comment), &p);
            }
        } else if (!strcasecmp(cmd, "Copyright")) {
            if (stream) {
                get_arg(stream->copyright, sizeof(stream->copyright), &p);
            }
        } else if (!strcasecmp(cmd, "Title")) {
            if (stream) {
                get_arg(stream->title, sizeof(stream->title), &p);
            }
        } else if (!strcasecmp(cmd, "Preroll")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                stream->prebuffer = atoi(arg) * 1000;
            }
        } else if (!strcasecmp(cmd, "StartSendOnKey")) {
            if (stream) {
                stream->send_on_key = 1;
            }
        } else if (!strcasecmp(cmd, "AudioCodec")) {
            get_arg(arg, sizeof(arg), &p);
            audio_id = opt_audio_codec(arg);
            if (audio_id == CODEC_ID_NONE) {
                fprintf(stderr, "%s:%d: Unknown AudioCodec: %s\n", 
                        filename, line_num, arg);
                errors++;
            }
        } else if (!strcasecmp(cmd, "VideoCodec")) {
            get_arg(arg, sizeof(arg), &p);
            video_id = opt_video_codec(arg);
            if (video_id == CODEC_ID_NONE) {
                fprintf(stderr, "%s:%d: Unknown VideoCodec: %s\n", 
                        filename, line_num, arg);
                errors++;
            }
        } else if (!strcasecmp(cmd, "MaxTime")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                stream->max_time = atoi(arg) * 1000;
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
        } else if (!strcasecmp(cmd, "VideoHighQuality")) {
            if (stream) {
                video_enc.flags |= CODEC_FLAG_HQ;
            }
        } else if (!strcasecmp(cmd, "VideoQDiff")) {
            if (stream) {
                video_enc.max_qdiff = atoi(arg);
                if (video_enc.max_qdiff < 1 || video_enc.max_qdiff > 31) {
                    fprintf(stderr, "%s:%d: VideoQDiff out of range\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoQMax")) {
            if (stream) {
                video_enc.qmax = atoi(arg);
                if (video_enc.qmax < 1 || video_enc.qmax > 31) {
                    fprintf(stderr, "%s:%d: VideoQMax out of range\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoQMin")) {
            if (stream) {
                video_enc.qmin = atoi(arg);
                if (video_enc.qmin < 1 || video_enc.qmin > 31) {
                    fprintf(stderr, "%s:%d: VideoQMin out of range\n",
                            filename, line_num);
                    errors++;
                }
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
        } else if (!strcasecmp(cmd, "<Redirect")) {
            /*********************************************/
            char *q;
            if (stream || feed || redirect) {
                fprintf(stderr, "%s:%d: Already in a tag\n",
                        filename, line_num);
                errors++;
            } else {
                redirect = av_mallocz(sizeof(FFStream));
                *last_stream = redirect;
                last_stream = &redirect->next;

                get_arg(redirect->filename, sizeof(redirect->filename), &p);
                q = strrchr(redirect->filename, '>');
                if (*q)
                    *q = '\0';
                redirect->stream_type = STREAM_TYPE_REDIRECT;
            }
        } else if (!strcasecmp(cmd, "URL")) {
            if (redirect) {
                get_arg(redirect->feed_filename, sizeof(redirect->feed_filename), &p);
            }
        } else if (!strcasecmp(cmd, "</Redirect>")) {
            if (!redirect) {
                fprintf(stderr, "%s:%d: No corresponding <Redirect> for </Redirect>\n",
                        filename, line_num);
                errors++;
            }
            if (!redirect->feed_filename[0]) {
                fprintf(stderr, "%s:%d: No URL found for <Redirect>\n",
                        filename, line_num);
                errors++;
            }
            redirect = NULL;
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
    printf("ffserver version " FFMPEG_VERSION ", Copyright (c) 2000, 2001, 2002 Fabrice Bellard\n"
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
    "Copyright (c) 2000, 2001, 2002 Fabrice Bellard\n"
    "This library is free software; you can redistribute it and/or\n"
    "modify it under the terms of the GNU Lesser General Public\n"
    "License as published by the Free Software Foundation; either\n"
    "version 2 of the License, or (at your option) any later version.\n"
    "\n"
    "This library is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
    "Lesser General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU Lesser General Public\n"
    "License along with this library; if not, write to the Free Software\n"
    "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n"
    );
}

static void handle_child_exit(int sig)
{
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        FFStream *feed;

        for (feed = first_feed; feed; feed = feed->next) {
            if (feed->pid == pid) {
                int uptime = time(0) - feed->pid_start;

                feed->pid = 0;
                fprintf(stderr, "%s: Pid %d exited with status %d after %d seconds\n", feed->filename, pid, status, uptime);

                if (uptime < 30) {
                    /* Turn off any more restarts */
                    feed->child_argv = 0;
                }    
            }
        }
    }

    need_to_start_children = 1;
}

int main(int argc, char **argv)
{
    const char *config_filename;
    int c;
    struct sigaction sigact;

    av_register_all();

    config_filename = "/etc/ffserver.conf";

    my_program_name = argv[0];

    for(;;) {
        c = getopt_long_only(argc, argv, "ndLh?f:", NULL, NULL);
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
        case 'n':
            no_launch = 1;
            break;
        case 'd':
            ffserver_debug = 1;
            break;
        case 'f':
            config_filename = optarg;
            break;
        default:
            exit(2);
        }
    }

    putenv("http_proxy");               /* Kill the http_proxy */

    /* address on which the server will handle connections */
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons (8080);
    my_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    nb_max_connections = 5;
    nb_max_bandwidth = 1000;
    first_stream = NULL;
    logfilename[0] = '\0';

    memset(&sigact, 0, sizeof(sigact));
    sigact.sa_handler = handle_child_exit;
    sigact.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sigact, 0);

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
        fprintf(stderr, "Could not start http server\n");
        exit(1);
    }

    return 0;
}
