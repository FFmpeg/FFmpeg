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
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <errno.h>
#include <sys/time.h>
#undef time //needed because HAVE_AV_CONFIG_H is defined on top
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#ifdef CONFIG_HAVE_DLFCN
#include <dlfcn.h>
#endif

#include "ffserver.h"

/* maximum number of simultaneous HTTP connections */
#define HTTP_MAX_CONNECTIONS 2000

enum HTTPState {
    HTTPSTATE_WAIT_REQUEST,
    HTTPSTATE_SEND_HEADER,
    HTTPSTATE_SEND_DATA_HEADER,
    HTTPSTATE_SEND_DATA,          /* sending TCP or UDP data */
    HTTPSTATE_SEND_DATA_TRAILER,
    HTTPSTATE_RECEIVE_DATA,       
    HTTPSTATE_WAIT_FEED,          /* wait for data from the feed */
    HTTPSTATE_READY,

    RTSPSTATE_WAIT_REQUEST,
    RTSPSTATE_SEND_REPLY,
    RTSPSTATE_SEND_PACKET,
};

const char *http_state[] = {
    "HTTP_WAIT_REQUEST",
    "HTTP_SEND_HEADER",

    "SEND_DATA_HEADER",
    "SEND_DATA",
    "SEND_DATA_TRAILER",
    "RECEIVE_DATA",
    "WAIT_FEED",
    "READY",

    "RTSP_WAIT_REQUEST",
    "RTSP_SEND_REPLY",
    "RTSP_SEND_PACKET",
};

#define IOBUFFER_INIT_SIZE 8192

/* coef for exponential mean for bitrate estimation in statistics */
#define AVG_COEF 0.9

/* timeouts are in ms */
#define HTTP_REQUEST_TIMEOUT (15 * 1000)
#define RTSP_REQUEST_TIMEOUT (3600 * 24 * 1000)

#define SYNC_TIMEOUT (10 * 1000)

typedef struct {
    int64_t count1, count2;
    long time1, time2;
} DataRateData;

/* context associated with one connection */
typedef struct HTTPContext {
    enum HTTPState state;
    int fd; /* socket file descriptor */
    struct sockaddr_in from_addr; /* origin */
    struct pollfd *poll_entry; /* used when polling */
    long timeout;
    uint8_t *buffer_ptr, *buffer_end;
    int http_error;
    struct HTTPContext *next;
    int got_key_frame; /* stream 0 => 1, stream 1 => 2, stream 2=> 4 */
    int64_t data_count;
    /* feed input */
    int feed_fd;
    /* input format handling */
    AVFormatContext *fmt_in;
    long start_time;            /* In milliseconds - this wraps fairly often */
    int64_t first_pts;            /* initial pts value */
    int64_t cur_pts;             /* current pts value from the stream in us */
    int64_t cur_frame_duration;  /* duration of the current frame in us */
    int cur_frame_bytes;       /* output frame size, needed to compute
                                  the time at which we send each
                                  packet */
    int pts_stream_index;        /* stream we choose as clock reference */
    int64_t cur_clock;           /* current clock reference value in us */
    /* output format handling */
    struct FFStream *stream;
    /* -1 is invalid stream */
    int feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    int switch_feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    int switch_pending;
    AVFormatContext fmt_ctx; /* instance of FFStream for one user */
    int last_packet_sent; /* true if last data packet was sent */
    int suppress_log;
    DataRateData datarate;
    int wmp_client_id;
    char protocol[16];
    char method[16];
    char url[128];
    int buffer_size;
    uint8_t *buffer;
    int is_packetized; /* if true, the stream is packetized */
    int packet_stream_index; /* current stream for output in state machine */
    
    /* RTSP state specific */
    uint8_t *pb_buffer; /* XXX: use that in all the code */
    ByteIOContext *pb;
    int seq; /* RTSP sequence number */
    
    /* RTP state specific */
    enum RTSPProtocol rtp_protocol;
    char session_id[32]; /* session id */
    AVFormatContext *rtp_ctx[MAX_STREAMS];

    /* RTP/UDP specific */
    URLContext *rtp_handles[MAX_STREAMS];

    /* RTP/TCP specific */
    struct HTTPContext *rtsp_c;
    uint8_t *packet_buffer, *packet_buffer_ptr, *packet_buffer_end;
} HTTPContext;

static AVFrame dummy_frame;

/* each generated stream is described here */
enum StreamType {
    STREAM_TYPE_LIVE,
    STREAM_TYPE_STATUS,
    STREAM_TYPE_REDIRECT,
};

enum IPAddressAction {
    IP_ALLOW = 1,
    IP_DENY,
};

typedef struct IPAddressACL {
    struct IPAddressACL *next;
    enum IPAddressAction action;
    /* These are in host order */
    struct in_addr first;
    struct in_addr last;
} IPAddressACL;

/* description of each stream of the ffserver.conf file */
typedef struct FFStream {
    enum StreamType stream_type;
    char filename[1024];     /* stream filename */
    struct FFStream *feed;   /* feed we are using (can be null if
                                coming from file) */
    AVFormatParameters *ap_in; /* input parameters */
    AVInputFormat *ifmt;       /* if non NULL, force input format */
    AVOutputFormat *fmt;
    IPAddressACL *acl;
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
    int bandwidth; /* bandwidth, in kbits/s */
    /* RTSP options */
    char *rtsp_option;
    /* multicast specific */
    int is_multicast;
    struct in_addr multicast_ip;
    int multicast_port; /* first port used for multicast */
    int multicast_ttl;
    int loop; /* if true, send the stream in loops (only meaningful if file) */

    /* feed specific */
    int feed_opened;     /* true if someone is writing to the feed */
    int is_feed;         /* true if it is a feed */
    int readonly;        /* True if writing is prohibited to the file */
    int conns_served;
    int64_t bytes_served;
    int64_t feed_max_size;      /* maximum storage size */
    int64_t feed_write_index;   /* current write position in feed (it wraps round) */
    int64_t feed_size;          /* current size of feed */
    struct FFStream *next_feed;
} FFStream;

typedef struct FeedData {
    long long data_count;
    float avg_frame_size;   /* frame size averraged over last frames with exponential mean */
} FeedData;

struct sockaddr_in my_http_addr;
struct sockaddr_in my_rtsp_addr;

char logfilename[1024];
HTTPContext *first_http_ctx;
FFStream *first_feed;   /* contains only feeds */
FFStream *first_stream; /* contains all streams, including feeds */

static void new_connection(int server_fd, int is_rtsp);
static void close_connection(HTTPContext *c);

/* HTTP handling */
static int handle_connection(HTTPContext *c);
static int http_parse_request(HTTPContext *c);
static int http_send_data(HTTPContext *c);
static void compute_stats(HTTPContext *c);
static int open_input_stream(HTTPContext *c, const char *info);
static int http_start_receive_data(HTTPContext *c);
static int http_receive_data(HTTPContext *c);

/* RTSP handling */
static int rtsp_parse_request(HTTPContext *c);
static void rtsp_cmd_describe(HTTPContext *c, const char *url);
static void rtsp_cmd_options(HTTPContext *c, const char *url);
static void rtsp_cmd_setup(HTTPContext *c, const char *url, RTSPHeader *h);
static void rtsp_cmd_play(HTTPContext *c, const char *url, RTSPHeader *h);
static void rtsp_cmd_pause(HTTPContext *c, const char *url, RTSPHeader *h);
static void rtsp_cmd_teardown(HTTPContext *c, const char *url, RTSPHeader *h);

/* SDP handling */
static int prepare_sdp_description(FFStream *stream, uint8_t **pbuffer, 
                                   struct in_addr my_ip);

/* RTP handling */
static HTTPContext *rtp_new_connection(struct sockaddr_in *from_addr, 
                                       FFStream *stream, const char *session_id,
                                       enum RTSPProtocol rtp_protocol);
static int rtp_new_av_stream(HTTPContext *c, 
                             int stream_index, struct sockaddr_in *dest_addr,
                             HTTPContext *rtsp_c);

static const char *my_program_name;
static const char *my_program_dir;

static int ffserver_debug;
static int ffserver_daemon;
static int no_launch;
static int need_to_start_children;

int nb_max_connections;
int nb_connections;

int max_bandwidth;
int current_bandwidth;

static long cur_time;           // Making this global saves on passing it around everywhere

static long gettime_ms(void)
{
    struct timeval tv;

    gettimeofday(&tv,NULL);
    return (long long)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

static FILE *logfile = NULL;

static void __attribute__ ((format (printf, 1, 2))) http_log(const char *fmt, ...) 
{
    va_list ap;
    va_start(ap, fmt);
    
    if (logfile) {
        vfprintf(logfile, fmt, ap);
        fflush(logfile);
    }
    va_end(ap);
}

static char *ctime1(char *buf2)
{
    time_t ti;
    char *p;

    ti = time(NULL);
    p = ctime(&ti);
    strcpy(buf2, p);
    p = buf2 + strlen(p) - 1;
    if (*p == '\n')
        *p = '\0';
    return buf2;
}

static void log_connection(HTTPContext *c)
{
    char buf2[32];

    if (c->suppress_log) 
        return;

    http_log("%s - - [%s] \"%s %s %s\" %d %lld\n", 
             inet_ntoa(c->from_addr.sin_addr), 
             ctime1(buf2), c->method, c->url, 
             c->protocol, (c->http_error ? c->http_error : 200), c->data_count);
}

static void update_datarate(DataRateData *drd, int64_t count)
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
static int compute_datarate(DataRateData *drd, int64_t count)
{
    if (cur_time == drd->time1)
        return 0;
    
    return ((count - drd->count1) * 1000) / (cur_time - drd->time1);
}

static int get_longterm_datarate(DataRateData *drd, int64_t count)
{
    /* You get the first 3 seconds flat out */
    if (cur_time - drd->time1 < 3000)
        return 0;
    return compute_datarate(drd, count);
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

                /* This is needed to make relative pathnames work */
                chdir(my_program_dir);

                signal(SIGPIPE, SIG_DFL);

                execvp(pathname, feed->child_argv);

                _exit(1);
            }
        }
    }
}

/* open a listening socket */
static int socket_open_listen(struct sockaddr_in *my_addr)
{
    int server_fd, tmp;

    server_fd = socket(AF_INET,SOCK_STREAM,0);
    if (server_fd < 0) {
        perror ("socket");
        return -1;
    }
        
    tmp = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp));

    if (bind (server_fd, (struct sockaddr *) my_addr, sizeof (*my_addr)) < 0) {
        char bindmsg[32];
        snprintf(bindmsg, sizeof(bindmsg), "bind(port %d)", ntohs(my_addr->sin_port));
        perror (bindmsg);
        close(server_fd);
        return -1;
    }
  
    if (listen (server_fd, 5) < 0) {
        perror ("listen");
        close(server_fd);
        return -1;
    }
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    return server_fd;
}

/* start all multicast streams */
static void start_multicast(void)
{
    FFStream *stream;
    char session_id[32];
    HTTPContext *rtp_c;
    struct sockaddr_in dest_addr;
    int default_port, stream_index;

    default_port = 6000;
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        if (stream->is_multicast) {
            /* open the RTP connection */
            snprintf(session_id, sizeof(session_id), 
                     "%08x%08x", (int)random(), (int)random());

            /* choose a port if none given */
            if (stream->multicast_port == 0) {
                stream->multicast_port = default_port;
                default_port += 100;
            }

            dest_addr.sin_family = AF_INET;
            dest_addr.sin_addr = stream->multicast_ip;
            dest_addr.sin_port = htons(stream->multicast_port);

            rtp_c = rtp_new_connection(&dest_addr, stream, session_id, 
                                       RTSP_PROTOCOL_RTP_UDP_MULTICAST);
            if (!rtp_c) {
                continue;
            }
            if (open_input_stream(rtp_c, "") < 0) {
                fprintf(stderr, "Could not open input stream for stream '%s'\n", 
                        stream->filename);
                continue;
            }

            /* open each RTP stream */
            for(stream_index = 0; stream_index < stream->nb_streams; 
                stream_index++) {
                dest_addr.sin_port = htons(stream->multicast_port + 
                                           2 * stream_index);
                if (rtp_new_av_stream(rtp_c, stream_index, &dest_addr, NULL) < 0) {
                    fprintf(stderr, "Could not open output stream '%s/streamid=%d'\n", 
                            stream->filename, stream_index);
                    exit(1);
                }
            }

            /* change state to send data */
            rtp_c->state = HTTPSTATE_SEND_DATA;
        }
    }
}

/* main loop of the http server */
static int http_server(void)
{
    int server_fd, ret, rtsp_server_fd, delay, delay1;
    struct pollfd poll_table[HTTP_MAX_CONNECTIONS + 2], *poll_entry;
    HTTPContext *c, *c_next;

    server_fd = socket_open_listen(&my_http_addr);
    if (server_fd < 0)
        return -1;

    rtsp_server_fd = socket_open_listen(&my_rtsp_addr);
    if (rtsp_server_fd < 0)
        return -1;
    
    http_log("ffserver started.\n");

    start_children(first_feed);

    first_http_ctx = NULL;
    nb_connections = 0;

    start_multicast();

    for(;;) {
        poll_entry = poll_table;
        poll_entry->fd = server_fd;
        poll_entry->events = POLLIN;
        poll_entry++;

        poll_entry->fd = rtsp_server_fd;
        poll_entry->events = POLLIN;
        poll_entry++;

        /* wait for events on each HTTP handle */
        c = first_http_ctx;
        delay = 1000;
        while (c != NULL) {
            int fd;
            fd = c->fd;
            switch(c->state) {
            case HTTPSTATE_SEND_HEADER:
            case RTSPSTATE_SEND_REPLY:
            case RTSPSTATE_SEND_PACKET:
                c->poll_entry = poll_entry;
                poll_entry->fd = fd;
                poll_entry->events = POLLOUT;
                poll_entry++;
                break;
            case HTTPSTATE_SEND_DATA_HEADER:
            case HTTPSTATE_SEND_DATA:
            case HTTPSTATE_SEND_DATA_TRAILER:
                if (!c->is_packetized) {
                    /* for TCP, we output as much as we can (may need to put a limit) */
                    c->poll_entry = poll_entry;
                    poll_entry->fd = fd;
                    poll_entry->events = POLLOUT;
                    poll_entry++;
                } else {
                    /* when ffserver is doing the timing, we work by
                       looking at which packet need to be sent every
                       10 ms */
                    delay1 = 10; /* one tick wait XXX: 10 ms assumed */
                    if (delay1 < delay)
                        delay = delay1;
                }
                break;
            case HTTPSTATE_WAIT_REQUEST:
            case HTTPSTATE_RECEIVE_DATA:
            case HTTPSTATE_WAIT_FEED:
            case RTSPSTATE_WAIT_REQUEST:
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
            ret = poll(poll_table, poll_entry - poll_table, delay);
            if (ret < 0 && errno != EAGAIN && errno != EINTR)
                return -1;
        } while (ret <= 0);
        
        cur_time = gettime_ms();

        if (need_to_start_children) {
            need_to_start_children = 0;
            start_children(first_feed);
        }

        /* now handle the events */
        for(c = first_http_ctx; c != NULL; c = c_next) {
            c_next = c->next;
            if (handle_connection(c) < 0) {
                /* close and free the connection */
                log_connection(c);
                close_connection(c);
            }
        }

        poll_entry = poll_table;
        /* new HTTP connection request ? */
        if (poll_entry->revents & POLLIN) {
            new_connection(server_fd, 0);
        }
        poll_entry++;
        /* new RTSP connection request ? */
        if (poll_entry->revents & POLLIN) {
            new_connection(rtsp_server_fd, 1);
        }
    }
}

/* start waiting for a new HTTP/RTSP request */
static void start_wait_request(HTTPContext *c, int is_rtsp)
{
    c->buffer_ptr = c->buffer;
    c->buffer_end = c->buffer + c->buffer_size - 1; /* leave room for '\0' */

    if (is_rtsp) {
        c->timeout = cur_time + RTSP_REQUEST_TIMEOUT;
        c->state = RTSPSTATE_WAIT_REQUEST;
    } else {
        c->timeout = cur_time + HTTP_REQUEST_TIMEOUT;
        c->state = HTTPSTATE_WAIT_REQUEST;
    }
}

static void new_connection(int server_fd, int is_rtsp)
{
    struct sockaddr_in from_addr;
    int fd, len;
    HTTPContext *c = NULL;

    len = sizeof(from_addr);
    fd = accept(server_fd, (struct sockaddr *)&from_addr, 
                &len);
    if (fd < 0)
        return;
    fcntl(fd, F_SETFL, O_NONBLOCK);

    /* XXX: should output a warning page when coming
       close to the connection limit */
    if (nb_connections >= nb_max_connections)
        goto fail;
    
    /* add a new connection */
    c = av_mallocz(sizeof(HTTPContext));
    if (!c)
        goto fail;
    
    c->fd = fd;
    c->poll_entry = NULL;
    c->from_addr = from_addr;
    c->buffer_size = IOBUFFER_INIT_SIZE;
    c->buffer = av_malloc(c->buffer_size);
    if (!c->buffer)
        goto fail;

    c->next = first_http_ctx;
    first_http_ctx = c;
    nb_connections++;
    
    start_wait_request(c, is_rtsp);

    return;

 fail:
    if (c) {
        av_free(c->buffer);
        av_free(c);
    }
    close(fd);
}

static void close_connection(HTTPContext *c)
{
    HTTPContext **cp, *c1;
    int i, nb_streams;
    AVFormatContext *ctx;
    URLContext *h;
    AVStream *st;

    /* remove connection from list */
    cp = &first_http_ctx;
    while ((*cp) != NULL) {
        c1 = *cp;
        if (c1 == c) {
            *cp = c->next;
        } else {
            cp = &c1->next;
        }
    }

    /* remove references, if any (XXX: do it faster) */
    for(c1 = first_http_ctx; c1 != NULL; c1 = c1->next) {
        if (c1->rtsp_c == c)
            c1->rtsp_c = NULL;
    }

    /* remove connection associated resources */
    if (c->fd >= 0)
        close(c->fd);
    if (c->fmt_in) {
        /* close each frame parser */
        for(i=0;i<c->fmt_in->nb_streams;i++) {
            st = c->fmt_in->streams[i];
            if (st->codec.codec) {
                avcodec_close(&st->codec);
            }
        }
        av_close_input_file(c->fmt_in);
    }

    /* free RTP output streams if any */
    nb_streams = 0;
    if (c->stream) 
        nb_streams = c->stream->nb_streams;
    
    for(i=0;i<nb_streams;i++) {
        ctx = c->rtp_ctx[i];
        if (ctx) {
            av_write_trailer(ctx);
            av_free(ctx);
        }
        h = c->rtp_handles[i];
        if (h) {
            url_close(h);
        }
    }
    
    ctx = &c->fmt_ctx;

    if (!c->last_packet_sent) {
        if (ctx->oformat) {
            /* prepare header */
            if (url_open_dyn_buf(&ctx->pb) >= 0) {
                av_write_trailer(ctx);
                url_close_dyn_buf(&ctx->pb, &c->pb_buffer);
            }
        }
    }

    for(i=0; i<ctx->nb_streams; i++) 
        av_free(ctx->streams[i]) ; 

    if (c->stream)
        current_bandwidth -= c->stream->bandwidth;
    av_freep(&c->pb_buffer);
    av_freep(&c->packet_buffer);
    av_free(c->buffer);
    av_free(c);
    nb_connections--;
}

static int handle_connection(HTTPContext *c)
{
    int len, ret;
    
    switch(c->state) {
    case HTTPSTATE_WAIT_REQUEST:
    case RTSPSTATE_WAIT_REQUEST:
        /* timeout ? */
        if ((c->timeout - cur_time) < 0)
            return -1;
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;

        /* no need to read if no events */
        if (!(c->poll_entry->revents & POLLIN))
            return 0;
        /* read the data */
    read_loop:
        len = read(c->fd, c->buffer_ptr, 1);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR)
                return -1;
        } else if (len == 0) {
            return -1;
        } else {
            /* search for end of request. */
            uint8_t *ptr;
            c->buffer_ptr += len;
            ptr = c->buffer_ptr;
            if ((ptr >= c->buffer + 2 && !memcmp(ptr-2, "\n\n", 2)) ||
                (ptr >= c->buffer + 4 && !memcmp(ptr-4, "\r\n\r\n", 4))) {
                /* request found : parse it and reply */
                if (c->state == HTTPSTATE_WAIT_REQUEST) {
                    ret = http_parse_request(c);
                } else {
                    ret = rtsp_parse_request(c);
                }
                if (ret < 0)
                    return -1;
            } else if (ptr >= c->buffer_end) {
                /* request too long: cannot do anything */
                return -1;
            } else goto read_loop;
        }
        break;

    case HTTPSTATE_SEND_HEADER:
        if (c->poll_entry->revents & (POLLERR | POLLHUP))
            return -1;

        /* no need to write if no events */
        if (!(c->poll_entry->revents & POLLOUT))
            return 0;
        len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                /* error : close connection */
                av_freep(&c->pb_buffer);
                return -1;
            }
        } else {
            c->buffer_ptr += len;
            if (c->stream)
                c->stream->bytes_served += len;
            c->data_count += len;
            if (c->buffer_ptr >= c->buffer_end) {
                av_freep(&c->pb_buffer);
                /* if error, exit */
                if (c->http_error) {
                    return -1;
                }
                /* all the buffer was sent : synchronize to the incoming stream */
                c->state = HTTPSTATE_SEND_DATA_HEADER;
                c->buffer_ptr = c->buffer_end = c->buffer;
            }
        }
        break;

    case HTTPSTATE_SEND_DATA:
    case HTTPSTATE_SEND_DATA_HEADER:
    case HTTPSTATE_SEND_DATA_TRAILER:
        /* for packetized output, we consider we can always write (the
           input streams sets the speed). It may be better to verify
           that we do not rely too much on the kernel queues */
        if (!c->is_packetized) {
            if (c->poll_entry->revents & (POLLERR | POLLHUP))
                return -1;
            
            /* no need to read if no events */
            if (!(c->poll_entry->revents & POLLOUT))
                return 0;
        }
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

    case RTSPSTATE_SEND_REPLY:
        if (c->poll_entry->revents & (POLLERR | POLLHUP)) {
            av_freep(&c->pb_buffer);
            return -1;
        }
        /* no need to write if no events */
        if (!(c->poll_entry->revents & POLLOUT))
            return 0;
        len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                /* error : close connection */
                av_freep(&c->pb_buffer);
                return -1;
            }
        } else {
            c->buffer_ptr += len;
            c->data_count += len;
            if (c->buffer_ptr >= c->buffer_end) {
                /* all the buffer was sent : wait for a new request */
                av_freep(&c->pb_buffer);
                start_wait_request(c, 1);
            }
        }
        break;
    case RTSPSTATE_SEND_PACKET:
        if (c->poll_entry->revents & (POLLERR | POLLHUP)) {
            av_freep(&c->packet_buffer);
            return -1;
        }
        /* no need to write if no events */
        if (!(c->poll_entry->revents & POLLOUT))
            return 0;
        len = write(c->fd, c->packet_buffer_ptr, 
                    c->packet_buffer_end - c->packet_buffer_ptr);
        if (len < 0) {
            if (errno != EAGAIN && errno != EINTR) {
                /* error : close connection */
                av_freep(&c->packet_buffer);
                return -1;
            }
        } else {
            c->packet_buffer_ptr += len;
            if (c->packet_buffer_ptr >= c->packet_buffer_end) {
                /* all the buffer was sent : wait for a new request */
                av_freep(&c->packet_buffer);
                c->state = RTSPSTATE_WAIT_REQUEST;
            }
        }
        break;
    case HTTPSTATE_READY:
        /* nothing to do */
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

    /* Not much we can do for a feed */
    if (!req->feed)
        return 0;

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

/* XXX: factorize in utils.c ? */
/* XXX: take care with different space meaning */
static void skip_spaces(const char **pp)
{
    const char *p;
    p = *pp;
    while (*p == ' ' || *p == '\t')
        p++;
    *pp = p;
}

static void get_word(char *buf, int buf_size, const char **pp)
{
    const char *p;
    char *q;

    p = *pp;
    skip_spaces(&p);
    q = buf;
    while (!isspace(*p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (buf_size > 0)
        *q = '\0';
    *pp = p;
}

static int validate_acl(FFStream *stream, HTTPContext *c)
{
    enum IPAddressAction last_action = IP_DENY;
    IPAddressACL *acl;
    struct in_addr *src = &c->from_addr.sin_addr;
    unsigned long src_addr = ntohl(src->s_addr);

    for (acl = stream->acl; acl; acl = acl->next) {
        if (src_addr >= acl->first.s_addr && src_addr <= acl->last.s_addr) {
            return (acl->action == IP_ALLOW) ? 1 : 0;
        }
        last_action = acl->action;
    }

    /* Nothing matched, so return not the last action */
    return (last_action == IP_DENY) ? 1 : 0;
}

/* compute the real filename of a file by matching it without its
   extensions to all the stream filenames */
static void compute_real_filename(char *filename, int max_size)
{
    char file1[1024];
    char file2[1024];
    char *p;
    FFStream *stream;

    /* compute filename by matching without the file extensions */
    pstrcpy(file1, sizeof(file1), filename);
    p = strrchr(file1, '.');
    if (p)
        *p = '\0';
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        pstrcpy(file2, sizeof(file2), stream->filename);
        p = strrchr(file2, '.');
        if (p)
            *p = '\0';
        if (!strcmp(file1, file2)) {
            pstrcpy(filename, max_size, stream->filename);
            break;
        }
    }
}

enum RedirType {
    REDIR_NONE,
    REDIR_ASX,
    REDIR_RAM,
    REDIR_ASF,
    REDIR_RTSP,
    REDIR_SDP,
};

/* parse http request and prepare header */
static int http_parse_request(HTTPContext *c)
{
    char *p;
    int post;
    enum RedirType redir_type;
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
    get_word(cmd, sizeof(cmd), (const char **)&p);
    pstrcpy(c->method, sizeof(c->method), cmd);

    if (!strcmp(cmd, "GET"))
        post = 0;
    else if (!strcmp(cmd, "POST"))
        post = 1;
    else
        return -1;

    get_word(url, sizeof(url), (const char **)&p);
    pstrcpy(c->url, sizeof(c->url), url);

    get_word(protocol, sizeof(protocol), (const char **)&p);
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

    redir_type = REDIR_NONE;
    if (match_ext(filename, "asx")) {
        redir_type = REDIR_ASX;
        filename[strlen(filename)-1] = 'f';
    } else if (match_ext(filename, "asf") &&
        (!useragent || strncasecmp(useragent, "NSPlayer", 8) != 0)) {
        /* if this isn't WMP or lookalike, return the redirector file */
        redir_type = REDIR_ASF;
    } else if (match_ext(filename, "rpm,ram")) {
        redir_type = REDIR_RAM;
        strcpy(filename + strlen(filename)-2, "m");
    } else if (match_ext(filename, "rtsp")) {
        redir_type = REDIR_RTSP;
        compute_real_filename(filename, sizeof(url) - 1);
    } else if (match_ext(filename, "sdp")) {
        redir_type = REDIR_SDP;
        compute_real_filename(filename, sizeof(url) - 1);
    }
    
    stream = first_stream;
    while (stream != NULL) {
        if (!strcmp(stream->filename, filename) && validate_acl(stream, c))
            break;
        stream = stream->next;
    }
    if (stream == NULL) {
        snprintf(msg, sizeof(msg), "File '%s' not found", url);
        goto send_error;
    }

    c->stream = stream;
    memcpy(c->feed_streams, stream->feed_streams, sizeof(c->feed_streams));
    memset(c->switch_feed_streams, -1, sizeof(c->switch_feed_streams));

    if (stream->stream_type == STREAM_TYPE_REDIRECT) {
        c->http_error = 301;
        q = c->buffer;
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 301 Moved\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Location: %s\r\n", stream->feed_filename);
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: text/html\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<html><head><title>Moved</title></head><body>\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "You should be <a href=\"%s\">redirected</a>.\r\n", stream->feed_filename);
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "</body></html>\r\n");

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
        current_bandwidth += stream->bandwidth;
    }
    
    if (post == 0 && max_bandwidth < current_bandwidth) {
        c->http_error = 200;
        q = c->buffer;
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 Server too busy\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: text/html\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<html><head><title>Too busy</title></head><body>\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "The server is too busy to serve your request at this time.<p>\r\n");
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "The bandwidth being served (including your stream) is %dkbit/sec, and this exceeds the limit of %dkbit/sec\r\n",
            current_bandwidth, max_bandwidth);
        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "</body></html>\r\n");

        /* prepare output buffer */
        c->buffer_ptr = c->buffer;
        c->buffer_end = q;
        c->state = HTTPSTATE_SEND_HEADER;
        return 0;
    }
    
    if (redir_type != REDIR_NONE) {
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
                    switch(redir_type) {
                    case REDIR_ASX:
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 ASX Follows\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: video/x-ms-asf\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<ASX Version=\"3\">\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<!-- Autogenerated by ffserver -->\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<ENTRY><REF HREF=\"http://%s/%s%s\"/></ENTRY>\r\n", 
                                hostbuf, filename, info);
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "</ASX>\r\n");
                        break;
                    case REDIR_RAM:
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 RAM Follows\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: audio/x-pn-realaudio\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "# Autogenerated by ffserver\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "http://%s/%s%s\r\n", 
                                hostbuf, filename, info);
                        break;
                    case REDIR_ASF:
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 ASF Redirect follows\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: video/x-ms-asf\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "[Reference]\r\n");
                        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Ref1=http://%s/%s%s\r\n", 
                                hostbuf, filename, info);
                        break;
                    case REDIR_RTSP:
                        {
                            char hostname[256], *p;
                            /* extract only hostname */
                            pstrcpy(hostname, sizeof(hostname), hostbuf);
                            p = strrchr(hostname, ':');
                            if (p)
                                *p = '\0';
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 RTSP Redirect follows\r\n");
                            /* XXX: incorrect mime type ? */
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: application/x-rtsp\r\n");
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "rtsp://%s:%d/%s\r\n", 
                                         hostname, ntohs(my_rtsp_addr.sin_port), 
                                         filename);
                        }
                        break;
                    case REDIR_SDP:
                        {
                            uint8_t *sdp_data;
                            int sdp_data_size, len;
                            struct sockaddr_in my_addr;

                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 OK\r\n");
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: application/sdp\r\n");
                            q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");

                            len = sizeof(my_addr);
                            getsockname(c->fd, (struct sockaddr *)&my_addr, &len);
                            
                            /* XXX: should use a dynamic buffer */
                            sdp_data_size = prepare_sdp_description(stream, 
                                                                    &sdp_data, 
                                                                    my_addr.sin_addr);
                            if (sdp_data_size > 0) {
                                memcpy(q, sdp_data, sdp_data_size);
                                q += sdp_data_size;
                                *q = '\0';
                                av_free(sdp_data);
                            }
                        }
                        break;
                    default:
                        av_abort();
                        break;
                    }

                    /* prepare output buffer */
                    c->buffer_ptr = c->buffer;
                    c->buffer_end = q;
                    c->state = HTTPSTATE_SEND_HEADER;
                    return 0;
                }
            }
        }

        snprintf(msg, sizeof(msg), "ASX/RAM file not handled");
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
                    http_log("%.*s\n", (int) (eol - logline), logline);
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
            
            snprintf(msg, sizeof(msg), "POST command not handled");
            c->stream = 0;
            goto send_error;
        }
        if (http_start_receive_data(c) < 0) {
            snprintf(msg, sizeof(msg), "could not open feed");
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
        snprintf(msg, sizeof(msg), "Input stream corresponding to '%s' not found", url);
        goto send_error;
    }

    /* prepare http header */
    q = c->buffer;
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 200 OK\r\n");
    mime_type = c->stream->fmt->mime_type;
    if (!mime_type)
        mime_type = "application/x-octet_stream";
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Pragma: no-cache\r\n");

    /* for asf, we need extra headers */
    if (!strcmp(c->stream->fmt->name,"asf_stream")) {
        /* Need to allocate a client id */

        c->wmp_client_id = random() & 0x7fffffff;

        q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Server: Cougar 4.1.0.3923\r\nCache-Control: no-cache\r\nPragma: client-id=%d\r\nPragma: features=\"broadcast\"\r\n", c->wmp_client_id);
    }
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-Type: %s\r\n", mime_type);
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
    
    /* prepare output buffer */
    c->http_error = 0;
    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
 send_error:
    c->http_error = 404;
    q = c->buffer;
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "HTTP/1.0 404 Not Found\r\n");
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Content-type: %s\r\n", "text/html");
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "\r\n");
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<HTML>\n");
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<HEAD><TITLE>404 Not Found</TITLE></HEAD>\n");
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "<BODY>%s</BODY>\n", msg);
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "</HTML>\n");

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

static void fmt_bytecount(ByteIOContext *pb, int64_t count)
{
    static const char *suffix = " kMGTP";
    const char *s;

    for (s = suffix; count >= 100000 && s[1]; count /= 1000, s++) {
    }

    url_fprintf(pb, "%lld%c", count, *s);
}

static void compute_stats(HTTPContext *c)
{
    HTTPContext *c1;
    FFStream *stream;
    char *p;
    time_t ti;
    int i, len;
    ByteIOContext pb1, *pb = &pb1;

    if (url_open_dyn_buf(pb) < 0) {
        /* XXX: return an error ? */
        c->buffer_ptr = c->buffer;
        c->buffer_end = c->buffer;
        return;
    }

    url_fprintf(pb, "HTTP/1.0 200 OK\r\n");
    url_fprintf(pb, "Content-type: %s\r\n", "text/html");
    url_fprintf(pb, "Pragma: no-cache\r\n");
    url_fprintf(pb, "\r\n");
    
    url_fprintf(pb, "<HEAD><TITLE>FFServer Status</TITLE>\n");
    if (c->stream->feed_filename) {
        url_fprintf(pb, "<link rel=\"shortcut icon\" href=\"%s\">\n", c->stream->feed_filename);
    }
    url_fprintf(pb, "</HEAD>\n<BODY>");
    url_fprintf(pb, "<H1>FFServer Status</H1>\n");
    /* format status */
    url_fprintf(pb, "<H2>Available Streams</H2>\n");
    url_fprintf(pb, "<TABLE cellspacing=0 cellpadding=4>\n");
    url_fprintf(pb, "<TR><Th valign=top>Path<th align=left>Served<br>Conns<Th><br>bytes<Th valign=top>Format<Th>Bit rate<br>kbits/s<Th align=left>Video<br>kbits/s<th><br>Codec<Th align=left>Audio<br>kbits/s<th><br>Codec<Th align=left valign=top>Feed\n");
    stream = first_stream;
    while (stream != NULL) {
        char sfilename[1024];
        char *eosf;

        if (stream->feed != stream) {
            pstrcpy(sfilename, sizeof(sfilename) - 10, stream->filename);
            eosf = sfilename + strlen(sfilename);
            if (eosf - sfilename >= 4) {
                if (strcmp(eosf - 4, ".asf") == 0) {
                    strcpy(eosf - 4, ".asx");
                } else if (strcmp(eosf - 3, ".rm") == 0) {
                    strcpy(eosf - 3, ".ram");
                } else if (stream->fmt == &rtp_mux) {
                    /* generate a sample RTSP director if
                       unicast. Generate an SDP redirector if
                       multicast */
                    eosf = strrchr(sfilename, '.');
                    if (!eosf)
                        eosf = sfilename + strlen(sfilename);
                    if (stream->is_multicast)
                        strcpy(eosf, ".sdp");
                    else
                        strcpy(eosf, ".rtsp");
                }
            }
            
            url_fprintf(pb, "<TR><TD><A HREF=\"/%s\">%s</A> ", 
                         sfilename, stream->filename);
            url_fprintf(pb, "<td align=right> %d <td align=right> ",
                        stream->conns_served);
            fmt_bytecount(pb, stream->bytes_served);
            switch(stream->stream_type) {
            case STREAM_TYPE_LIVE:
                {
                    int audio_bit_rate = 0;
                    int video_bit_rate = 0;
                    const char *audio_codec_name = "";
                    const char *video_codec_name = "";
                    const char *audio_codec_name_extra = "";
                    const char *video_codec_name_extra = "";

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
                        case CODEC_TYPE_DATA:
                            video_bit_rate += st->codec.bit_rate;
                            break;
                        default:
                            av_abort();
                        }
                    }
                    url_fprintf(pb, "<TD align=center> %s <TD align=right> %d <TD align=right> %d <TD> %s %s <TD align=right> %d <TD> %s %s", 
                                 stream->fmt->name,
                                 stream->bandwidth,
                                 video_bit_rate / 1000, video_codec_name, video_codec_name_extra,
                                 audio_bit_rate / 1000, audio_codec_name, audio_codec_name_extra);
                    if (stream->feed) {
                        url_fprintf(pb, "<TD>%s", stream->feed->filename);
                    } else {
                        url_fprintf(pb, "<TD>%s", stream->feed_filename);
                    }
                    url_fprintf(pb, "\n");
                }
                break;
            default:
                url_fprintf(pb, "<TD align=center> - <TD align=right> - <TD align=right> - <td><td align=right> - <TD>\n");
                break;
            }
        }
        stream = stream->next;
    }
    url_fprintf(pb, "</TABLE>\n");

    stream = first_stream;
    while (stream != NULL) {
        if (stream->feed == stream) {
            url_fprintf(pb, "<h2>Feed %s</h2>", stream->filename);
            if (stream->pid) {
                url_fprintf(pb, "Running as pid %d.\n", stream->pid);

#if defined(linux) && !defined(CONFIG_NOCUTILS)
                {
                    FILE *pid_stat;
                    char ps_cmd[64];

                    /* This is somewhat linux specific I guess */
                    snprintf(ps_cmd, sizeof(ps_cmd), 
                             "ps -o \"%%cpu,cputime\" --no-headers %d", 
                             stream->pid);
                    
                    pid_stat = popen(ps_cmd, "r");
                    if (pid_stat) {
                        char cpuperc[10];
                        char cpuused[64];
                        
                        if (fscanf(pid_stat, "%10s %64s", cpuperc, 
                                   cpuused) == 2) {
                            url_fprintf(pb, "Currently using %s%% of the cpu. Total time used %s.\n",
                                         cpuperc, cpuused);
                        }
                        fclose(pid_stat);
                    }
                }
#endif

                url_fprintf(pb, "<p>");
            }
            url_fprintf(pb, "<table cellspacing=0 cellpadding=4><tr><th>Stream<th>type<th>kbits/s<th align=left>codec<th align=left>Parameters\n");

            for (i = 0; i < stream->nb_streams; i++) {
                AVStream *st = stream->streams[i];
                AVCodec *codec = avcodec_find_encoder(st->codec.codec_id);
                const char *type = "unknown";
                char parameters[64];

                parameters[0] = 0;

                switch(st->codec.codec_type) {
                case CODEC_TYPE_AUDIO:
                    type = "audio";
                    break;
                case CODEC_TYPE_VIDEO:
                    type = "video";
                    snprintf(parameters, sizeof(parameters), "%dx%d, q=%d-%d, fps=%d", st->codec.width, st->codec.height,
                                st->codec.qmin, st->codec.qmax, st->codec.frame_rate / st->codec.frame_rate_base);
                    break;
                default:
                    av_abort();
                }
                url_fprintf(pb, "<tr><td align=right>%d<td>%s<td align=right>%d<td>%s<td>%s\n",
                        i, type, st->codec.bit_rate/1000, codec ? codec->name : "", parameters);
            }
            url_fprintf(pb, "</table>\n");

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
            url_fprintf(pb, "<H1>Feed '%s'</H1>\n", stream->filename);
            url_fprintf(pb, "<TABLE>\n");
            url_fprintf(pb, "<TR><TD>Parameters<TD>Frame count<TD>Size<TD>Avg bitrate (kbits/s)\n");
            for(i=0;i<stream->nb_streams;i++) {
                AVStream *st = stream->streams[i];
                FeedData *fdata = st->priv_data;
                enc = &st->codec;
            
                avcodec_string(buf, sizeof(buf), enc);
                avg = fdata->avg_frame_size * (float)enc->rate * 8.0;
                if (enc->codec->type == CODEC_TYPE_AUDIO && enc->frame_size > 0)
                    avg /= enc->frame_size;
                url_fprintf(pb, "<TR><TD>%s <TD> %d <TD> %Ld <TD> %0.1f\n", 
                             buf, enc->frame_number, fdata->data_count, avg / 1000.0);
            }
            url_fprintf(pb, "</TABLE>\n");
            stream = stream->next_feed;
        }
    }
#endif

    /* connection status */
    url_fprintf(pb, "<H2>Connection Status</H2>\n");

    url_fprintf(pb, "Number of connections: %d / %d<BR>\n",
                 nb_connections, nb_max_connections);

    url_fprintf(pb, "Bandwidth in use: %dk / %dk<BR>\n",
                 current_bandwidth, max_bandwidth);

    url_fprintf(pb, "<TABLE>\n");
    url_fprintf(pb, "<TR><th>#<th>File<th>IP<th>Proto<th>State<th>Target bits/sec<th>Actual bits/sec<th>Bytes transferred\n");
    c1 = first_http_ctx;
    i = 0;
    while (c1 != NULL) {
        int bitrate;
        int j;

        bitrate = 0;
        if (c1->stream) {
            for (j = 0; j < c1->stream->nb_streams; j++) {
                if (!c1->stream->feed) {
                    bitrate += c1->stream->streams[j]->codec.bit_rate;
                } else {
                    if (c1->feed_streams[j] >= 0) {
                        bitrate += c1->stream->feed->streams[c1->feed_streams[j]]->codec.bit_rate;
                    }
                }
            }
        }

        i++;
        p = inet_ntoa(c1->from_addr.sin_addr);
        url_fprintf(pb, "<TR><TD><B>%d</B><TD>%s%s<TD>%s<TD>%s<TD>%s<td align=right>", 
                    i, 
                    c1->stream ? c1->stream->filename : "", 
                    c1->state == HTTPSTATE_RECEIVE_DATA ? "(input)" : "",
                    p, 
                    c1->protocol,
                    http_state[c1->state]);
        fmt_bytecount(pb, bitrate);
        url_fprintf(pb, "<td align=right>");
        fmt_bytecount(pb, compute_datarate(&c1->datarate, c1->data_count) * 8);
        url_fprintf(pb, "<td align=right>");
        fmt_bytecount(pb, c1->data_count);
        url_fprintf(pb, "\n");
        c1 = c1->next;
    }
    url_fprintf(pb, "</TABLE>\n");
    
    /* date */
    ti = time(NULL);
    p = ctime(&ti);
    url_fprintf(pb, "<HR size=1 noshade>Generated at %s", p);
    url_fprintf(pb, "</BODY>\n</HTML>\n");

    len = url_close_dyn_buf(pb, &c->pb_buffer);
    c->buffer_ptr = c->pb_buffer;
    c->buffer_end = c->pb_buffer + len;
}

/* check if the parser needs to be opened for stream i */
static void open_parser(AVFormatContext *s, int i)
{
    AVStream *st = s->streams[i];
    AVCodec *codec;

    if (!st->codec.codec) {
        codec = avcodec_find_decoder(st->codec.codec_id);
        if (codec && (codec->capabilities & CODEC_CAP_PARSE_ONLY)) {
            st->codec.parse_only = 1;
            if (avcodec_open(&st->codec, codec) < 0) {
                st->codec.parse_only = 0;
            }
        }
    }
}

static int open_input_stream(HTTPContext *c, const char *info)
{
    char buf[128];
    char input_filename[1024];
    AVFormatContext *s;
    int buf_size, i;
    int64_t stream_pos;

    /* find file name */
    if (c->stream->feed) {
        strcpy(input_filename, c->stream->feed->feed_filename);
        buf_size = FFM_PACKET_SIZE;
        /* compute position (absolute time) */
        if (find_info_tag(buf, sizeof(buf), "date", info)) {
            stream_pos = parse_date(buf, 0);
        } else if (find_info_tag(buf, sizeof(buf), "buffer", info)) {
            int prebuffer = strtol(buf, 0, 10);
            stream_pos = av_gettime() - prebuffer * (int64_t)1000000;
        } else {
            stream_pos = av_gettime() - c->stream->prebuffer * (int64_t)1000;
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

#if 0
    { time_t when = stream_pos / 1000000;
    http_log("Stream pos = %lld, time=%s", stream_pos, ctime(&when));
    }
#endif

    /* open stream */
    if (av_open_input_file(&s, input_filename, c->stream->ifmt, 
                           buf_size, c->stream->ap_in) < 0) {
        http_log("%s not found", input_filename);
        return -1;
    }
    c->fmt_in = s;
    
    /* open each parser */
    for(i=0;i<s->nb_streams;i++)
        open_parser(s, i);

    /* choose stream as clock source (we favorize video stream if
       present) for packet sending */
    c->pts_stream_index = 0;
    for(i=0;i<c->stream->nb_streams;i++) {
        if (c->pts_stream_index == 0 && 
            c->stream->streams[i]->codec.codec_type == CODEC_TYPE_VIDEO) {
            c->pts_stream_index = i;
        }
    }

#if 0
    if (c->fmt_in->iformat->read_seek) {
        c->fmt_in->iformat->read_seek(c->fmt_in, stream_pos);
    }
#endif
    /* set the start time (needed for maxtime and RTP packet timing) */
    c->start_time = cur_time;
    c->first_pts = AV_NOPTS_VALUE;
    return 0;
}

/* return the server clock (in us) */
static int64_t get_server_clock(HTTPContext *c)
{
    /* compute current pts value from system time */
    return (int64_t)(cur_time - c->start_time) * 1000LL;
}

/* return the estimated time at which the current packet must be sent
   (in us) */
static int64_t get_packet_send_clock(HTTPContext *c)
{
    int bytes_left, bytes_sent, frame_bytes;
    
    frame_bytes = c->cur_frame_bytes;
    if (frame_bytes <= 0) {
        return c->cur_pts;
    } else {
        bytes_left = c->buffer_end - c->buffer_ptr;
        bytes_sent = frame_bytes - bytes_left;
        return c->cur_pts + (c->cur_frame_duration * bytes_sent) / frame_bytes;
    }
}


static int http_prepare_data(HTTPContext *c)
{
    int i, len, ret;
    AVFormatContext *ctx;

    av_freep(&c->pb_buffer);
    switch(c->state) {
    case HTTPSTATE_SEND_DATA_HEADER:
        memset(&c->fmt_ctx, 0, sizeof(c->fmt_ctx));
        pstrcpy(c->fmt_ctx.author, sizeof(c->fmt_ctx.author), 
                c->stream->author);
        pstrcpy(c->fmt_ctx.comment, sizeof(c->fmt_ctx.comment), 
                c->stream->comment);
        pstrcpy(c->fmt_ctx.copyright, sizeof(c->fmt_ctx.copyright), 
                c->stream->copyright);
        pstrcpy(c->fmt_ctx.title, sizeof(c->fmt_ctx.title), 
                c->stream->title);

        /* open output stream by using specified codecs */
        c->fmt_ctx.oformat = c->stream->fmt;
        c->fmt_ctx.nb_streams = c->stream->nb_streams;
        for(i=0;i<c->fmt_ctx.nb_streams;i++) {
            AVStream *st;
            st = av_mallocz(sizeof(AVStream));
            c->fmt_ctx.streams[i] = st;
            /* if file or feed, then just take streams from FFStream struct */
            if (!c->stream->feed || 
                c->stream->feed == c->stream)
                memcpy(st, c->stream->streams[i], sizeof(AVStream));
            else
                memcpy(st, c->stream->feed->streams[c->stream->feed_streams[i]],
                           sizeof(AVStream));
            st->codec.frame_number = 0; /* XXX: should be done in
                                           AVStream, not in codec */
            /* I'm pretty sure that this is not correct...
             * However, without it, we crash
             */
            st->codec.coded_frame = &dummy_frame;
        }
        c->got_key_frame = 0;

        /* prepare header and save header data in a stream */
        if (url_open_dyn_buf(&c->fmt_ctx.pb) < 0) {
            /* XXX: potential leak */
            return -1;
        }
        c->fmt_ctx.pb.is_streamed = 1;

        av_set_parameters(&c->fmt_ctx, NULL);
        av_write_header(&c->fmt_ctx);

        len = url_close_dyn_buf(&c->fmt_ctx.pb, &c->pb_buffer);
        c->buffer_ptr = c->pb_buffer;
        c->buffer_end = c->pb_buffer + len;

        c->state = HTTPSTATE_SEND_DATA;
        c->last_packet_sent = 0;
        break;
    case HTTPSTATE_SEND_DATA:
        /* find a new packet */
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
            } else {
            redo:
                if (av_read_frame(c->fmt_in, &pkt) < 0) {
                    if (c->stream->feed && c->stream->feed->feed_opened) {
                        /* if coming from feed, it means we reached the end of the
                           ffm file, so must wait for more data */
                        c->state = HTTPSTATE_WAIT_FEED;
                        return 1; /* state changed */
                    } else {
                        if (c->stream->loop) {
                            av_close_input_file(c->fmt_in);
                            c->fmt_in = NULL;
                            if (open_input_stream(c, "") < 0)
                                goto no_loop;
                            goto redo;
                        } else {
                        no_loop:
                            /* must send trailer now because eof or error */
                            c->state = HTTPSTATE_SEND_DATA_TRAILER;
                        }
                    }
                } else {
                    /* update first pts if needed */
                    if (c->first_pts == AV_NOPTS_VALUE) {
                        c->first_pts = pkt.dts;
                        c->start_time = cur_time;
                    }
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
                                if (!c->stream->send_on_key || 
                                    ((c->got_key_frame + 1) >> c->stream->nb_streams)) {
                                    goto send_it;
                                }
                            }
                        }
                    } else {
                        AVCodecContext *codec;
                        
                    send_it:
                        /* specific handling for RTP: we use several
                           output stream (one for each RTP
                           connection). XXX: need more abstract handling */
                        if (c->is_packetized) {
                            AVStream *st;
                            /* compute send time and duration */
                            st = c->fmt_in->streams[pkt.stream_index];
                            c->cur_pts = pkt.dts;
                            if (st->start_time != AV_NOPTS_VALUE)
                                c->cur_pts -= st->start_time;
                            c->cur_frame_duration = pkt.duration;
#if 0
                            printf("index=%d pts=%0.3f duration=%0.6f\n",
                                   pkt.stream_index,
                                   (double)c->cur_pts / 
                                   AV_TIME_BASE,
                                   (double)c->cur_frame_duration / 
                                   AV_TIME_BASE);
#endif
                            /* find RTP context */
                            c->packet_stream_index = pkt.stream_index;
                            ctx = c->rtp_ctx[c->packet_stream_index];
                            if(!ctx) {
                              av_free_packet(&pkt);
                              break;
                            }
                            codec = &ctx->streams[0]->codec;
                            /* only one stream per RTP connection */
                            pkt.stream_index = 0;
                        } else {
                            ctx = &c->fmt_ctx;
                            /* Fudge here */
                            codec = &ctx->streams[pkt.stream_index]->codec;
                        }
                        
                        codec->coded_frame->key_frame = ((pkt.flags & PKT_FLAG_KEY) != 0);
                        if (c->is_packetized) {
                            int max_packet_size;
                            if (c->rtp_protocol == RTSP_PROTOCOL_RTP_TCP)
                                max_packet_size = RTSP_TCP_MAX_PACKET_SIZE;
                            else
                                max_packet_size = url_get_max_packet_size(c->rtp_handles[c->packet_stream_index]);
                            ret = url_open_dyn_packet_buf(&ctx->pb, max_packet_size);
                        } else {
                            ret = url_open_dyn_buf(&ctx->pb);
                        }
                        if (ret < 0) {
                            /* XXX: potential leak */
                            return -1;
                        }
                        if (av_write_frame(ctx, &pkt)) {
                            c->state = HTTPSTATE_SEND_DATA_TRAILER;
                        }
                        
                        len = url_close_dyn_buf(&ctx->pb, &c->pb_buffer);
                        c->cur_frame_bytes = len;
                        c->buffer_ptr = c->pb_buffer;
                        c->buffer_end = c->pb_buffer + len;
                        
                        codec->frame_number++;
                        if (len == 0)
                            goto redo;
                    }
                    av_free_packet(&pkt);
                }
            }
        }
        break;
    default:
    case HTTPSTATE_SEND_DATA_TRAILER:
        /* last packet test ? */
        if (c->last_packet_sent || c->is_packetized)
            return -1;
        ctx = &c->fmt_ctx;
        /* prepare header */
        if (url_open_dyn_buf(&ctx->pb) < 0) {
            /* XXX: potential leak */
            return -1;
        }
        av_write_trailer(ctx);
        len = url_close_dyn_buf(&ctx->pb, &c->pb_buffer);
        c->buffer_ptr = c->pb_buffer;
        c->buffer_end = c->pb_buffer + len;

        c->last_packet_sent = 1;
        break;
    }
    return 0;
}

/* in bit/s */
#define SHORT_TERM_BANDWIDTH 8000000

/* should convert the format at the same time */
/* send data starting at c->buffer_ptr to the output connection
   (either UDP or TCP connection) */
static int http_send_data(HTTPContext *c)
{
    int len, ret;

    for(;;) {
        if (c->buffer_ptr >= c->buffer_end) {
            ret = http_prepare_data(c);
            if (ret < 0)
                return -1;
            else if (ret != 0) {
                /* state change requested */
                break;
            }
        } else {
            if (c->is_packetized) {
                /* RTP data output */
                len = c->buffer_end - c->buffer_ptr;
                if (len < 4) {
                    /* fail safe - should never happen */
                fail1:
                    c->buffer_ptr = c->buffer_end;
                    return 0;
                }
                len = (c->buffer_ptr[0] << 24) |
                    (c->buffer_ptr[1] << 16) |
                    (c->buffer_ptr[2] << 8) |
                    (c->buffer_ptr[3]);
                if (len > (c->buffer_end - c->buffer_ptr))
                    goto fail1;
                if ((get_packet_send_clock(c) - get_server_clock(c)) > 0) {
                    /* nothing to send yet: we can wait */
                    return 0;
                }

                c->data_count += len;
                update_datarate(&c->datarate, c->data_count);
                if (c->stream)
                    c->stream->bytes_served += len;

                if (c->rtp_protocol == RTSP_PROTOCOL_RTP_TCP) {
                    /* RTP packets are sent inside the RTSP TCP connection */
                    ByteIOContext pb1, *pb = &pb1;
                    int interleaved_index, size;
                    uint8_t header[4];
                    HTTPContext *rtsp_c;
                    
                    rtsp_c = c->rtsp_c;
                    /* if no RTSP connection left, error */
                    if (!rtsp_c)
                        return -1;
                    /* if already sending something, then wait. */
                    if (rtsp_c->state != RTSPSTATE_WAIT_REQUEST) {
                        break;
                    }
                    if (url_open_dyn_buf(pb) < 0)
                        goto fail1;
                    interleaved_index = c->packet_stream_index * 2;
                    /* RTCP packets are sent at odd indexes */
                    if (c->buffer_ptr[1] == 200)
                        interleaved_index++;
                    /* write RTSP TCP header */
                    header[0] = '$';
                    header[1] = interleaved_index;
                    header[2] = len >> 8;
                    header[3] = len;
                    put_buffer(pb, header, 4);
                    /* write RTP packet data */
                    c->buffer_ptr += 4;
                    put_buffer(pb, c->buffer_ptr, len);
                    size = url_close_dyn_buf(pb, &c->packet_buffer);
                    /* prepare asynchronous TCP sending */
                    rtsp_c->packet_buffer_ptr = c->packet_buffer;
                    rtsp_c->packet_buffer_end = c->packet_buffer + size;
                    c->buffer_ptr += len;
                    
                    /* send everything we can NOW */
                    len = write(rtsp_c->fd, rtsp_c->packet_buffer_ptr, 
                                rtsp_c->packet_buffer_end - rtsp_c->packet_buffer_ptr);
                    if (len > 0) {
                        rtsp_c->packet_buffer_ptr += len;
                    }
                    if (rtsp_c->packet_buffer_ptr < rtsp_c->packet_buffer_end) {
                        /* if we could not send all the data, we will
                           send it later, so a new state is needed to
                           "lock" the RTSP TCP connection */
                        rtsp_c->state = RTSPSTATE_SEND_PACKET;
                        break;
                    } else {
                        /* all data has been sent */
                        av_freep(&c->packet_buffer);
                    }
                } else {
                    /* send RTP packet directly in UDP */
                    c->buffer_ptr += 4;
                    url_write(c->rtp_handles[c->packet_stream_index], 
                              c->buffer_ptr, len);
                    c->buffer_ptr += len;
                    /* here we continue as we can send several packets per 10 ms slot */
                }
            } else {
                /* TCP data output */
                len = write(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr);
                if (len < 0) {
                    if (errno != EAGAIN && errno != EINTR) {
                        /* error : close connection */
                        return -1;
                    } else {
                        return 0;
                    }
                } else {
                    c->buffer_ptr += len;
                }
                c->data_count += len;
                update_datarate(&c->datarate, c->data_count);
                if (c->stream)
                    c->stream->bytes_served += len;
                break;
            }
        }
    } /* for(;;) */
    return 0;
}

static int http_start_receive_data(HTTPContext *c)
{
    int fd;

    if (c->stream->feed_opened)
        return -1;

    /* Don't permit writing to this one */
    if (c->stream->readonly)
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

    if (c->buffer_ptr - c->buffer >= 2 && c->data_count > FFM_PACKET_SIZE) {
        if (c->buffer[0] != 'f' ||
            c->buffer[1] != 'm') {
            http_log("Feed stream has become desynchronized -- disconnecting\n");
            goto fail;
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

            if (fmt_in->priv_data_size > 0) {
                s.priv_data = av_mallocz(fmt_in->priv_data_size);
                if (!s.priv_data)
                    goto fail;
	    } else
	        s.priv_data = NULL;

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

/********************************************************************/
/* RTSP handling */

static void rtsp_reply_header(HTTPContext *c, enum RTSPStatusCode error_number)
{
    const char *str;
    time_t ti;
    char *p;
    char buf2[32];

    switch(error_number) {
#define DEF(n, c, s) case c: str = s; break; 
#include "rtspcodes.h"
#undef DEF
    default:
        str = "Unknown Error";
        break;
    }
     
    url_fprintf(c->pb, "RTSP/1.0 %d %s\r\n", error_number, str);
    url_fprintf(c->pb, "CSeq: %d\r\n", c->seq);

    /* output GMT time */
    ti = time(NULL);
    p = ctime(&ti);
    strcpy(buf2, p);
    p = buf2 + strlen(p) - 1;
    if (*p == '\n')
        *p = '\0';
    url_fprintf(c->pb, "Date: %s GMT\r\n", buf2);
}

static void rtsp_reply_error(HTTPContext *c, enum RTSPStatusCode error_number)
{
    rtsp_reply_header(c, error_number);
    url_fprintf(c->pb, "\r\n");
}

static int rtsp_parse_request(HTTPContext *c)
{
    const char *p, *p1, *p2;
    char cmd[32];
    char url[1024];
    char protocol[32];
    char line[1024];
    ByteIOContext pb1;
    int len;
    RTSPHeader header1, *header = &header1;
    
    c->buffer_ptr[0] = '\0';
    p = c->buffer;
    
    get_word(cmd, sizeof(cmd), &p);
    get_word(url, sizeof(url), &p);
    get_word(protocol, sizeof(protocol), &p);

    pstrcpy(c->method, sizeof(c->method), cmd);
    pstrcpy(c->url, sizeof(c->url), url);
    pstrcpy(c->protocol, sizeof(c->protocol), protocol);

    c->pb = &pb1;
    if (url_open_dyn_buf(c->pb) < 0) {
        /* XXX: cannot do more */
        c->pb = NULL; /* safety */
        return -1;
    }

    /* check version name */
    if (strcmp(protocol, "RTSP/1.0") != 0) {
        rtsp_reply_error(c, RTSP_STATUS_VERSION);
        goto the_end;
    }

    /* parse each header line */
    memset(header, 0, sizeof(RTSPHeader));
    /* skip to next line */
    while (*p != '\n' && *p != '\0')
        p++;
    if (*p == '\n')
        p++;
    while (*p != '\0') {
        p1 = strchr(p, '\n');
        if (!p1)
            break;
        p2 = p1;
        if (p2 > p && p2[-1] == '\r')
            p2--;
        /* skip empty line */
        if (p2 == p)
            break;
        len = p2 - p;
        if (len > sizeof(line) - 1)
            len = sizeof(line) - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        rtsp_parse_line(header, line);
        p = p1 + 1;
    }

    /* handle sequence number */
    c->seq = header->seq;

    if (!strcmp(cmd, "DESCRIBE")) {
        rtsp_cmd_describe(c, url);
    } else if (!strcmp(cmd, "OPTIONS")) {
        rtsp_cmd_options(c, url);
    } else if (!strcmp(cmd, "SETUP")) {
        rtsp_cmd_setup(c, url, header);
    } else if (!strcmp(cmd, "PLAY")) {
        rtsp_cmd_play(c, url, header);
    } else if (!strcmp(cmd, "PAUSE")) {
        rtsp_cmd_pause(c, url, header);
    } else if (!strcmp(cmd, "TEARDOWN")) {
        rtsp_cmd_teardown(c, url, header);
    } else {
        rtsp_reply_error(c, RTSP_STATUS_METHOD);
    }
 the_end:
    len = url_close_dyn_buf(c->pb, &c->pb_buffer);
    c->pb = NULL; /* safety */
    if (len < 0) {
        /* XXX: cannot do more */
        return -1;
    }
    c->buffer_ptr = c->pb_buffer;
    c->buffer_end = c->pb_buffer + len;
    c->state = RTSPSTATE_SEND_REPLY;
    return 0;
}

/* XXX: move that to rtsp.c, but would need to replace FFStream by
   AVFormatContext */
static int prepare_sdp_description(FFStream *stream, uint8_t **pbuffer, 
                                   struct in_addr my_ip)
{
    ByteIOContext pb1, *pb = &pb1;
    int i, payload_type, port, private_payload_type, j;
    const char *ipstr, *title, *mediatype;
    AVStream *st;
    
    if (url_open_dyn_buf(pb) < 0)
        return -1;
    
    /* general media info */

    url_fprintf(pb, "v=0\n");
    ipstr = inet_ntoa(my_ip);
    url_fprintf(pb, "o=- 0 0 IN IP4 %s\n", ipstr);
    title = stream->title;
    if (title[0] == '\0')
        title = "No Title";
    url_fprintf(pb, "s=%s\n", title);
    if (stream->comment[0] != '\0')
        url_fprintf(pb, "i=%s\n", stream->comment);
    if (stream->is_multicast) {
        url_fprintf(pb, "c=IN IP4 %s\n", inet_ntoa(stream->multicast_ip));
    }
    /* for each stream, we output the necessary info */
    private_payload_type = RTP_PT_PRIVATE;
    for(i = 0; i < stream->nb_streams; i++) {
        st = stream->streams[i];
        if (st->codec.codec_id == CODEC_ID_MPEG2TS) {
            mediatype = "video";
        } else {
            switch(st->codec.codec_type) {
            case CODEC_TYPE_AUDIO:
                mediatype = "audio";
                break;
            case CODEC_TYPE_VIDEO:
                mediatype = "video";
                break;
            default:
                mediatype = "application";
                break;
            }
        }
        /* NOTE: the port indication is not correct in case of
           unicast. It is not an issue because RTSP gives it */
        payload_type = rtp_get_payload_type(&st->codec);
        if (payload_type < 0)
            payload_type = private_payload_type++;
        if (stream->is_multicast) {
            port = stream->multicast_port + 2 * i;
        } else {
            port = 0;
        }
        url_fprintf(pb, "m=%s %d RTP/AVP %d\n", 
                    mediatype, port, payload_type);
        if (payload_type >= RTP_PT_PRIVATE) {
            /* for private payload type, we need to give more info */
            switch(st->codec.codec_id) {
            case CODEC_ID_MPEG4:
                {
                    uint8_t *data;
                    url_fprintf(pb, "a=rtpmap:%d MP4V-ES/%d\n", 
                                payload_type, 90000);
                    /* we must also add the mpeg4 header */
                    data = st->codec.extradata;
                    if (data) {
                        url_fprintf(pb, "a=fmtp:%d config=", payload_type);
                        for(j=0;j<st->codec.extradata_size;j++) {
                            url_fprintf(pb, "%02x", data[j]);
                        }
                        url_fprintf(pb, "\n");
                    }
                }
                break;
            default:
                /* XXX: add other codecs ? */
                goto fail;
            }
        }
        url_fprintf(pb, "a=control:streamid=%d\n", i);
    }
    return url_close_dyn_buf(pb, pbuffer);
 fail:
    url_close_dyn_buf(pb, pbuffer);
    av_free(*pbuffer);
    return -1;
}

static void rtsp_cmd_options(HTTPContext *c, const char *url)
{
//    rtsp_reply_header(c, RTSP_STATUS_OK);
    url_fprintf(c->pb, "RTSP/1.0 %d %s\r\n", RTSP_STATUS_OK, "OK");
    url_fprintf(c->pb, "CSeq: %d\r\n", c->seq);
    url_fprintf(c->pb, "Public: %s\r\n", "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE");
    url_fprintf(c->pb, "\r\n");
}

static void rtsp_cmd_describe(HTTPContext *c, const char *url)
{
    FFStream *stream;
    char path1[1024];
    const char *path;
    uint8_t *content;
    int content_length, len;
    struct sockaddr_in my_addr;
    
    /* find which url is asked */
    url_split(NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
    path = path1;
    if (*path == '/')
        path++;

    for(stream = first_stream; stream != NULL; stream = stream->next) {
        if (!stream->is_feed && stream->fmt == &rtp_mux &&
            !strcmp(path, stream->filename)) {
            goto found;
        }
    }
    /* no stream found */
    rtsp_reply_error(c, RTSP_STATUS_SERVICE); /* XXX: right error ? */
    return;

 found:
    /* prepare the media description in sdp format */

    /* get the host IP */
    len = sizeof(my_addr);
    getsockname(c->fd, (struct sockaddr *)&my_addr, &len);
    content_length = prepare_sdp_description(stream, &content, my_addr.sin_addr);
    if (content_length < 0) {
        rtsp_reply_error(c, RTSP_STATUS_INTERNAL);
        return;
    }
    rtsp_reply_header(c, RTSP_STATUS_OK);
    url_fprintf(c->pb, "Content-Type: application/sdp\r\n");
    url_fprintf(c->pb, "Content-Length: %d\r\n", content_length);
    url_fprintf(c->pb, "\r\n");
    put_buffer(c->pb, content, content_length);
}

static HTTPContext *find_rtp_session(const char *session_id)
{
    HTTPContext *c;

    if (session_id[0] == '\0')
        return NULL;

    for(c = first_http_ctx; c != NULL; c = c->next) {
        if (!strcmp(c->session_id, session_id))
            return c;
    }
    return NULL;
}

static RTSPTransportField *find_transport(RTSPHeader *h, enum RTSPProtocol protocol)
{
    RTSPTransportField *th;
    int i;

    for(i=0;i<h->nb_transports;i++) {
        th = &h->transports[i];
        if (th->protocol == protocol)
            return th;
    }
    return NULL;
}

static void rtsp_cmd_setup(HTTPContext *c, const char *url, 
                           RTSPHeader *h)
{
    FFStream *stream;
    int stream_index, port;
    char buf[1024];
    char path1[1024];
    const char *path;
    HTTPContext *rtp_c;
    RTSPTransportField *th;
    struct sockaddr_in dest_addr;
    RTSPActionServerSetup setup;
    
    /* find which url is asked */
    url_split(NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
    path = path1;
    if (*path == '/')
        path++;

    /* now check each stream */
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        if (!stream->is_feed && stream->fmt == &rtp_mux) {
            /* accept aggregate filenames only if single stream */
            if (!strcmp(path, stream->filename)) {
                if (stream->nb_streams != 1) {
                    rtsp_reply_error(c, RTSP_STATUS_AGGREGATE);
                    return;
                }
                stream_index = 0;
                goto found;
            }
                
            for(stream_index = 0; stream_index < stream->nb_streams;
                stream_index++) {
                snprintf(buf, sizeof(buf), "%s/streamid=%d", 
                         stream->filename, stream_index);
                if (!strcmp(path, buf))
                    goto found;
            }
        }
    }
    /* no stream found */
    rtsp_reply_error(c, RTSP_STATUS_SERVICE); /* XXX: right error ? */
    return;
 found:

    /* generate session id if needed */
    if (h->session_id[0] == '\0') {
        snprintf(h->session_id, sizeof(h->session_id), 
                 "%08x%08x", (int)random(), (int)random());
    }

    /* find rtp session, and create it if none found */
    rtp_c = find_rtp_session(h->session_id);
    if (!rtp_c) {
        /* always prefer UDP */
        th = find_transport(h, RTSP_PROTOCOL_RTP_UDP);
        if (!th) {
            th = find_transport(h, RTSP_PROTOCOL_RTP_TCP);
            if (!th) {
                rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
                return;
            }
        }

        rtp_c = rtp_new_connection(&c->from_addr, stream, h->session_id,
                                   th->protocol);
        if (!rtp_c) {
            rtsp_reply_error(c, RTSP_STATUS_BANDWIDTH);
            return;
        }

        /* open input stream */
        if (open_input_stream(rtp_c, "") < 0) {
            rtsp_reply_error(c, RTSP_STATUS_INTERNAL);
            return;
        }
    }
    
    /* test if stream is OK (test needed because several SETUP needs
       to be done for a given file) */
    if (rtp_c->stream != stream) {
        rtsp_reply_error(c, RTSP_STATUS_SERVICE);
        return;
    }
    
    /* test if stream is already set up */
    if (rtp_c->rtp_ctx[stream_index]) {
        rtsp_reply_error(c, RTSP_STATUS_STATE);
        return;
    }

    /* check transport */
    th = find_transport(h, rtp_c->rtp_protocol);
    if (!th || (th->protocol == RTSP_PROTOCOL_RTP_UDP && 
                th->client_port_min <= 0)) {
        rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
        return;
    }

    /* setup default options */
    setup.transport_option[0] = '\0';
    dest_addr = rtp_c->from_addr;
    dest_addr.sin_port = htons(th->client_port_min);
    
    /* add transport option if needed */
    if (ff_rtsp_callback) {
        setup.ipaddr = ntohl(dest_addr.sin_addr.s_addr);
        if (ff_rtsp_callback(RTSP_ACTION_SERVER_SETUP, rtp_c->session_id, 
                             (char *)&setup, sizeof(setup),
                             stream->rtsp_option) < 0) {
            rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
            return;
        }
        dest_addr.sin_addr.s_addr = htonl(setup.ipaddr);
    }
    
    /* setup stream */
    if (rtp_new_av_stream(rtp_c, stream_index, &dest_addr, c) < 0) {
        rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
        return;
    }

    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    url_fprintf(c->pb, "Session: %s\r\n", rtp_c->session_id);

    switch(rtp_c->rtp_protocol) {
    case RTSP_PROTOCOL_RTP_UDP:
        port = rtp_get_local_port(rtp_c->rtp_handles[stream_index]);
        url_fprintf(c->pb, "Transport: RTP/AVP/UDP;unicast;"
                    "client_port=%d-%d;server_port=%d-%d",
                    th->client_port_min, th->client_port_min + 1,
                    port, port + 1);
        break;
    case RTSP_PROTOCOL_RTP_TCP:
        url_fprintf(c->pb, "Transport: RTP/AVP/TCP;interleaved=%d-%d",
                    stream_index * 2, stream_index * 2 + 1);
        break;
    default:
        break;
    }
    if (setup.transport_option[0] != '\0') {
        url_fprintf(c->pb, ";%s", setup.transport_option);
    }
    url_fprintf(c->pb, "\r\n");
    

    url_fprintf(c->pb, "\r\n");
}


/* find an rtp connection by using the session ID. Check consistency
   with filename */
static HTTPContext *find_rtp_session_with_url(const char *url, 
                                              const char *session_id)
{
    HTTPContext *rtp_c;
    char path1[1024];
    const char *path;
    char buf[1024];
    int s;

    rtp_c = find_rtp_session(session_id);
    if (!rtp_c)
        return NULL;

    /* find which url is asked */
    url_split(NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
    path = path1;
    if (*path == '/')
        path++;
    if(!strcmp(path, rtp_c->stream->filename)) return rtp_c;
    for(s=0; s<rtp_c->stream->nb_streams; ++s) {
      snprintf(buf, sizeof(buf), "%s/streamid=%d",
        rtp_c->stream->filename, s);
      if(!strncmp(path, buf, sizeof(buf))) {
    // XXX: Should we reply with RTSP_STATUS_ONLY_AGGREGATE if nb_streams>1?
        return rtp_c;
      }
    }
    return NULL;
}

static void rtsp_cmd_play(HTTPContext *c, const char *url, RTSPHeader *h)
{
    HTTPContext *rtp_c;

    rtp_c = find_rtp_session_with_url(url, h->session_id);
    if (!rtp_c) {
        rtsp_reply_error(c, RTSP_STATUS_SESSION);
        return;
    }
    
    if (rtp_c->state != HTTPSTATE_SEND_DATA &&
        rtp_c->state != HTTPSTATE_WAIT_FEED &&
        rtp_c->state != HTTPSTATE_READY) {
        rtsp_reply_error(c, RTSP_STATUS_STATE);
        return;
    }

#if 0
    /* XXX: seek in stream */
    if (h->range_start != AV_NOPTS_VALUE) {
        printf("range_start=%0.3f\n", (double)h->range_start / AV_TIME_BASE);
        av_seek_frame(rtp_c->fmt_in, -1, h->range_start);
    }
#endif

    rtp_c->state = HTTPSTATE_SEND_DATA;
    
    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    url_fprintf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    url_fprintf(c->pb, "\r\n");
}

static void rtsp_cmd_pause(HTTPContext *c, const char *url, RTSPHeader *h)
{
    HTTPContext *rtp_c;

    rtp_c = find_rtp_session_with_url(url, h->session_id);
    if (!rtp_c) {
        rtsp_reply_error(c, RTSP_STATUS_SESSION);
        return;
    }
    
    if (rtp_c->state != HTTPSTATE_SEND_DATA &&
        rtp_c->state != HTTPSTATE_WAIT_FEED) {
        rtsp_reply_error(c, RTSP_STATUS_STATE);
        return;
    }
    
    rtp_c->state = HTTPSTATE_READY;
    rtp_c->first_pts = AV_NOPTS_VALUE;
    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    url_fprintf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    url_fprintf(c->pb, "\r\n");
}

static void rtsp_cmd_teardown(HTTPContext *c, const char *url, RTSPHeader *h)
{
    HTTPContext *rtp_c;

    rtp_c = find_rtp_session_with_url(url, h->session_id);
    if (!rtp_c) {
        rtsp_reply_error(c, RTSP_STATUS_SESSION);
        return;
    }
    
    /* abort the session */
    close_connection(rtp_c);

    if (ff_rtsp_callback) {
        ff_rtsp_callback(RTSP_ACTION_SERVER_TEARDOWN, rtp_c->session_id, 
                         NULL, 0,
                         rtp_c->stream->rtsp_option);
    }

    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    url_fprintf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    url_fprintf(c->pb, "\r\n");
}


/********************************************************************/
/* RTP handling */

static HTTPContext *rtp_new_connection(struct sockaddr_in *from_addr, 
                                       FFStream *stream, const char *session_id,
                                       enum RTSPProtocol rtp_protocol)
{
    HTTPContext *c = NULL;
    const char *proto_str;
    
    /* XXX: should output a warning page when coming
       close to the connection limit */
    if (nb_connections >= nb_max_connections)
        goto fail;
    
    /* add a new connection */
    c = av_mallocz(sizeof(HTTPContext));
    if (!c)
        goto fail;
    
    c->fd = -1;
    c->poll_entry = NULL;
    c->from_addr = *from_addr;
    c->buffer_size = IOBUFFER_INIT_SIZE;
    c->buffer = av_malloc(c->buffer_size);
    if (!c->buffer)
        goto fail;
    nb_connections++;
    c->stream = stream;
    pstrcpy(c->session_id, sizeof(c->session_id), session_id);
    c->state = HTTPSTATE_READY;
    c->is_packetized = 1;
    c->rtp_protocol = rtp_protocol;

    /* protocol is shown in statistics */
    switch(c->rtp_protocol) {
    case RTSP_PROTOCOL_RTP_UDP_MULTICAST:
        proto_str = "MCAST";
        break;
    case RTSP_PROTOCOL_RTP_UDP:
        proto_str = "UDP";
        break;
    case RTSP_PROTOCOL_RTP_TCP:
        proto_str = "TCP";
        break;
    default:
        proto_str = "???";
        break;
    }
    pstrcpy(c->protocol, sizeof(c->protocol), "RTP/");
    pstrcat(c->protocol, sizeof(c->protocol), proto_str);

    current_bandwidth += stream->bandwidth;

    c->next = first_http_ctx;
    first_http_ctx = c;
    return c;
        
 fail:
    if (c) {
        av_free(c->buffer);
        av_free(c);
    }
    return NULL;
}

/* add a new RTP stream in an RTP connection (used in RTSP SETUP
   command). If RTP/TCP protocol is used, TCP connection 'rtsp_c' is
   used. */
static int rtp_new_av_stream(HTTPContext *c, 
                             int stream_index, struct sockaddr_in *dest_addr,
                             HTTPContext *rtsp_c)
{
    AVFormatContext *ctx;
    AVStream *st;
    char *ipaddr;
    URLContext *h;
    uint8_t *dummy_buf;
    char buf2[32];
    int max_packet_size;
    
    /* now we can open the relevant output stream */
    ctx = av_alloc_format_context();
    if (!ctx)
        return -1;
    ctx->oformat = &rtp_mux;

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        goto fail;
    ctx->nb_streams = 1;
    ctx->streams[0] = st;

    if (!c->stream->feed || 
        c->stream->feed == c->stream) {
        memcpy(st, c->stream->streams[stream_index], sizeof(AVStream));
    } else {
        memcpy(st, 
               c->stream->feed->streams[c->stream->feed_streams[stream_index]],
               sizeof(AVStream));
    }
    
    /* build destination RTP address */
    ipaddr = inet_ntoa(dest_addr->sin_addr);

    switch(c->rtp_protocol) {
    case RTSP_PROTOCOL_RTP_UDP:
    case RTSP_PROTOCOL_RTP_UDP_MULTICAST:
        /* RTP/UDP case */
        
        /* XXX: also pass as parameter to function ? */
        if (c->stream->is_multicast) {
            int ttl;
            ttl = c->stream->multicast_ttl;
            if (!ttl)
                ttl = 16;
            snprintf(ctx->filename, sizeof(ctx->filename),
                     "rtp://%s:%d?multicast=1&ttl=%d", 
                     ipaddr, ntohs(dest_addr->sin_port), ttl);
        } else {
            snprintf(ctx->filename, sizeof(ctx->filename),
                     "rtp://%s:%d", ipaddr, ntohs(dest_addr->sin_port));
        }

        if (url_open(&h, ctx->filename, URL_WRONLY) < 0)
            goto fail;
        c->rtp_handles[stream_index] = h;
        max_packet_size = url_get_max_packet_size(h);
        break;
    case RTSP_PROTOCOL_RTP_TCP:
        /* RTP/TCP case */
        c->rtsp_c = rtsp_c;
        max_packet_size = RTSP_TCP_MAX_PACKET_SIZE;
        break;
    default:
        goto fail;
    }

    http_log("%s:%d - - [%s] \"PLAY %s/streamid=%d %s\"\n",
             ipaddr, ntohs(dest_addr->sin_port), 
             ctime1(buf2), 
             c->stream->filename, stream_index, c->protocol);

    /* normally, no packets should be output here, but the packet size may be checked */
    if (url_open_dyn_packet_buf(&ctx->pb, max_packet_size) < 0) {
        /* XXX: close stream */
        goto fail;
    }
    av_set_parameters(ctx, NULL);
    if (av_write_header(ctx) < 0) {
    fail:
        if (h)
            url_close(h);
        av_free(ctx);
        return -1;
    }
    url_close_dyn_buf(&ctx->pb, &dummy_buf);
    av_free(dummy_buf);
    
    c->rtp_ctx[stream_index] = ctx;
    return 0;
}

/********************************************************************/
/* ffserver initialization */

static AVStream *add_av_stream1(FFStream *stream, AVCodecContext *codec)
{
    AVStream *fst;

    fst = av_mallocz(sizeof(AVStream));
    if (!fst)
        return NULL;
    fst->priv_data = av_mallocz(sizeof(FeedData));
    memcpy(&fst->codec, codec, sizeof(AVCodecContext));
    fst->codec.coded_frame = &dummy_frame;
    fst->index = stream->nb_streams;
    stream->streams[stream->nb_streams++] = fst;
    return fst;
}

/* return the stream number in the feed */
static int add_av_stream(FFStream *feed, AVStream *st)
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
                    av1->frame_rate_base == av->frame_rate_base &&
                    av1->gop_size == av->gop_size)
                    goto found;
                break;
            default:
                av_abort();
            }
        }
    }
    
    fst = add_av_stream1(feed, av);
    if (!fst)
        return -1;
    return feed->nb_streams - 1;
 found:
    return i;
}

static void remove_stream(FFStream *stream)
{
    FFStream **ps;
    ps = &first_stream;
    while (*ps != NULL) {
        if (*ps == stream) {
            *ps = (*ps)->next;
        } else {
            ps = &(*ps)->next;
        }
    }
}

/* specific mpeg4 handling : we extract the raw parameters */
static void extract_mpeg4_header(AVFormatContext *infile)
{
    int mpeg4_count, i, size;
    AVPacket pkt;
    AVStream *st;
    const uint8_t *p;

    mpeg4_count = 0;
    for(i=0;i<infile->nb_streams;i++) {
        st = infile->streams[i];
        if (st->codec.codec_id == CODEC_ID_MPEG4 &&
            st->codec.extradata_size == 0) {
            mpeg4_count++;
        }
    }
    if (!mpeg4_count)
        return;

    printf("MPEG4 without extra data: trying to find header in %s\n", infile->filename);
    while (mpeg4_count > 0) {
        if (av_read_packet(infile, &pkt) < 0)
            break;
        st = infile->streams[pkt.stream_index];
        if (st->codec.codec_id == CODEC_ID_MPEG4 &&
            st->codec.extradata_size == 0) {
            av_freep(&st->codec.extradata);
            /* fill extradata with the header */
            /* XXX: we make hard suppositions here ! */
            p = pkt.data;
            while (p < pkt.data + pkt.size - 4) {
                /* stop when vop header is found */
                if (p[0] == 0x00 && p[1] == 0x00 && 
                    p[2] == 0x01 && p[3] == 0xb6) {
                    size = p - pkt.data;
                    //                    av_hex_dump(pkt.data, size);
                    st->codec.extradata = av_malloc(size);
                    st->codec.extradata_size = size;
                    memcpy(st->codec.extradata, pkt.data, size);
                    break;
                }
                p++;
            }
            mpeg4_count--;
        }
        av_free_packet(&pkt);
    }
}

/* compute the needed AVStream for each file */
static void build_file_streams(void)
{
    FFStream *stream, *stream_next;
    AVFormatContext *infile;
    int i;

    /* gather all streams */
    for(stream = first_stream; stream != NULL; stream = stream_next) {
        stream_next = stream->next;
        if (stream->stream_type == STREAM_TYPE_LIVE &&
            !stream->feed) {
            /* the stream comes from a file */
            /* try to open the file */
            /* open stream */
            stream->ap_in = av_mallocz(sizeof(AVFormatParameters));
            if (stream->fmt == &rtp_mux) {
                /* specific case : if transport stream output to RTP,
                   we use a raw transport stream reader */
                stream->ap_in->mpeg2ts_raw = 1;
                stream->ap_in->mpeg2ts_compute_pcr = 1;
            }
            
            if (av_open_input_file(&infile, stream->feed_filename, 
                                   stream->ifmt, 0, stream->ap_in) < 0) {
                http_log("%s not found", stream->feed_filename);
                /* remove stream (no need to spend more time on it) */
            fail:
                remove_stream(stream);
            } else {
                /* find all the AVStreams inside and reference them in
                   'stream' */
                if (av_find_stream_info(infile) < 0) {
                    http_log("Could not find codec parameters from '%s'", 
                             stream->feed_filename);
                    av_close_input_file(infile);
                    goto fail;
                }
                extract_mpeg4_header(infile);

                for(i=0;i<infile->nb_streams;i++) {
                    add_av_stream1(stream, &infile->streams[i]->codec);
                }
                av_close_input_file(infile);
            }
        }
    }
}

/* compute the needed AVStream for each feed */
static void build_feed_streams(void)
{
    FFStream *stream, *feed;
    int i;

    /* gather all streams */
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        feed = stream->feed;
        if (feed) {
            if (!stream->is_feed) {
                /* we handle a stream coming from a feed */
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

        if (url_exist(feed->feed_filename)) {
            /* See if it matches */
            AVFormatContext *s;
            int matches = 0;

            if (av_open_input_file(&s, feed->feed_filename, NULL, FFM_PACKET_SIZE, NULL) >= 0) {
                /* Now see if it matches */
                if (s->nb_streams == feed->nb_streams) {
                    matches = 1;
                    for(i=0;i<s->nb_streams;i++) {
                        AVStream *sf, *ss;
                        sf = feed->streams[i];
                        ss = s->streams[i];

                        if (sf->index != ss->index ||
                            sf->id != ss->id) {
                            printf("Index & Id do not match for stream %d (%s)\n", 
                                   i, feed->feed_filename);
                            matches = 0;
                        } else {
                            AVCodecContext *ccf, *ccs;

                            ccf = &sf->codec;
                            ccs = &ss->codec;
#define CHECK_CODEC(x)  (ccf->x != ccs->x)

                            if (CHECK_CODEC(codec) || CHECK_CODEC(codec_type)) {
                                printf("Codecs do not match for stream %d\n", i);
                                matches = 0;
                            } else if (CHECK_CODEC(bit_rate) || CHECK_CODEC(flags)) {
                                printf("Codec bitrates do not match for stream %d\n", i);
                                matches = 0;
                            } else if (ccf->codec_type == CODEC_TYPE_VIDEO) {
                                if (CHECK_CODEC(frame_rate) ||
                                    CHECK_CODEC(frame_rate_base) ||
                                    CHECK_CODEC(width) ||
                                    CHECK_CODEC(height)) {
                                    printf("Codec width, height and framerate do not match for stream %d\n", i);
                                    matches = 0;
                                }
                            } else if (ccf->codec_type == CODEC_TYPE_AUDIO) {
                                if (CHECK_CODEC(sample_rate) ||
                                    CHECK_CODEC(channels) ||
                                    CHECK_CODEC(frame_size)) {
                                    printf("Codec sample_rate, channels, frame_size do not match for stream %d\n", i);
                                    matches = 0;
                                }
                            } else {
                                printf("Unknown codec type\n");
                                matches = 0;
                            }
                        }
                        if (!matches) {
                            break;
                        }
                    }
                } else {
                    printf("Deleting feed file '%s' as stream counts differ (%d != %d)\n",
                        feed->feed_filename, s->nb_streams, feed->nb_streams);
                }

                av_close_input_file(s);
            } else {
                printf("Deleting feed file '%s' as it appears to be corrupt\n",
                        feed->feed_filename);
            }
            if (!matches) {
                if (feed->readonly) {
                    printf("Unable to delete feed file '%s' as it is marked readonly\n",
                        feed->feed_filename);
                    exit(1);
                }
                unlink(feed->feed_filename);
            }
        }
        if (!url_exist(feed->feed_filename)) {
            AVFormatContext s1, *s = &s1;

            if (feed->readonly) {
                printf("Unable to create feed file '%s' as it is marked readonly\n",
                    feed->feed_filename);
                exit(1);
            }

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
            av_set_parameters(s, NULL);
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

/* compute the bandwidth used by each stream */
static void compute_bandwidth(void)
{
    int bandwidth, i;
    FFStream *stream;
    
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        bandwidth = 0;
        for(i=0;i<stream->nb_streams;i++) {
            AVStream *st = stream->streams[i];
            switch(st->codec.codec_type) {
            case CODEC_TYPE_AUDIO:
            case CODEC_TYPE_VIDEO:
                bandwidth += st->codec.bit_rate;
                break;
            default:
                break;
            }
        }
        stream->bandwidth = (bandwidth + 999) / 1000;
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
static void add_codec(FFStream *stream, AVCodecContext *av)
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
        if (av->frame_rate == 0){
            av->frame_rate = 5;
            av->frame_rate_base = 1;
        }
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

        if (!av->rc_eq)
            av->rc_eq = "tex^qComp";
        if (!av->i_quant_factor)
            av->i_quant_factor = -0.8;
        if (!av->b_quant_factor)
            av->b_quant_factor = 1.25;
        if (!av->b_quant_offset)
            av->b_quant_offset = 1.25;
        if (!av->rc_max_rate)
            av->rc_max_rate = av->bit_rate * 2;

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

static int opt_audio_codec(const char *arg)
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

static int opt_video_codec(const char *arg)
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

/* simplistic plugin support */

#ifdef CONFIG_HAVE_DLOPEN
void load_module(const char *filename)
{
    void *dll;
    void (*init_func)(void);
    dll = dlopen(filename, RTLD_NOW);
    if (!dll) {
        fprintf(stderr, "Could not load module '%s' - %s\n",
                filename, dlerror());
        return;
    }
    
    init_func = dlsym(dll, "ffserver_module_init");
    if (!init_func) {
        fprintf(stderr, 
                "%s: init function 'ffserver_module_init()' not found\n",
                filename);
        dlclose(dll);
    }

    init_func();
}
#endif

static int parse_ffconfig(const char *filename)
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
            my_http_addr.sin_port = htons (atoi(arg));
        } else if (!strcasecmp(cmd, "BindAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (!inet_aton(arg, &my_http_addr.sin_addr)) {
                fprintf(stderr, "%s:%d: Invalid IP address: %s\n", 
                        filename, line_num, arg);
                errors++;
            }
        } else if (!strcasecmp(cmd, "NoDaemon")) {
            ffserver_daemon = 0;
        } else if (!strcasecmp(cmd, "RTSPPort")) {
            get_arg(arg, sizeof(arg), &p);
            my_rtsp_addr.sin_port = htons (atoi(arg));
        } else if (!strcasecmp(cmd, "RTSPBindAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (!inet_aton(arg, &my_rtsp_addr.sin_addr)) {
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
                max_bandwidth = val;
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

                    feed->child_argv[i] = av_malloc(strlen(argbuf) + 1);
                    strcpy(feed->child_argv[i], argbuf);
                }

                feed->child_argv[i] = av_malloc(30 + strlen(feed->filename));

                snprintf(feed->child_argv[i], 256, "http://127.0.0.1:%d/%s", 
                    ntohs(my_http_addr.sin_port), feed->filename);
            }
        } else if (!strcasecmp(cmd, "ReadOnlyFile")) {
            if (feed) {
                get_arg(feed->feed_filename, sizeof(feed->feed_filename), &p);
                feed->readonly = 1;
            } else if (stream) {
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
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
                feed->feed_max_size = (int64_t)fsize;
            }
        } else if (!strcasecmp(cmd, "</Feed>")) {
            if (!feed) {
                fprintf(stderr, "%s:%d: No corresponding <Feed> for </Feed>\n",
                        filename, line_num);
                errors++;
#if 0
            } else {
                /* Make sure that we start out clean */
                if (unlink(feed->feed_filename) < 0 
                    && errno != ENOENT) {
                    fprintf(stderr, "%s:%d: Unable to clean old feed file '%s': %s\n",
                        filename, line_num, feed->feed_filename, strerror(errno));
                    errors++;
                }
#endif
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
                stream->fmt = guess_stream_format(NULL, stream->filename, NULL);
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
                stream->fmt = guess_stream_format(arg, NULL, NULL);
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
        } else if (!strcasecmp(cmd, "InputFormat")) {
            stream->ifmt = av_find_input_format(arg);
            if (!stream->ifmt) {
                fprintf(stderr, "%s:%d: Unknown input format: %s\n", 
                        filename, line_num, arg);
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
                stream->prebuffer = atof(arg) * 1000;
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
                stream->max_time = atof(arg) * 1000;
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
	} else if (!strcasecmp(cmd, "AudioQuality")) {
	    get_arg(arg, sizeof(arg), &p);
            if (stream) {
//                audio_enc.quality = atof(arg) * 1000;
            }
        } else if (!strcasecmp(cmd, "VideoBitRateRange")) {
            if (stream) {
                int minrate, maxrate;

                get_arg(arg, sizeof(arg), &p);

                if (sscanf(arg, "%d-%d", &minrate, &maxrate) == 2) {
                    video_enc.rc_min_rate = minrate * 1000;
                    video_enc.rc_max_rate = maxrate * 1000;
                } else {
                    fprintf(stderr, "%s:%d: Incorrect format for VideoBitRateRange -- should be <min>-<max>: %s\n", 
                            filename, line_num, arg);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoBufferSize")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.rc_buffer_size = atoi(arg) * 1024;
            }
        } else if (!strcasecmp(cmd, "VideoBitRateTolerance")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.bit_rate_tolerance = atoi(arg) * 1000;
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
                video_enc.frame_rate_base= DEFAULT_FRAME_RATE_BASE;
                video_enc.frame_rate = (int)(strtod(arg, NULL) * video_enc.frame_rate_base);
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
                video_enc.mb_decision = FF_MB_DECISION_BITS;
            }
        } else if (!strcasecmp(cmd, "Video4MotionVector")) {
            if (stream) {
                video_enc.mb_decision = FF_MB_DECISION_BITS; //FIXME remove
                video_enc.flags |= CODEC_FLAG_4MV;
            }
        } else if (!strcasecmp(cmd, "VideoQDiff")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.max_qdiff = atoi(arg);
                if (video_enc.max_qdiff < 1 || video_enc.max_qdiff > 31) {
                    fprintf(stderr, "%s:%d: VideoQDiff out of range\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoQMax")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.qmax = atoi(arg);
                if (video_enc.qmax < 1 || video_enc.qmax > 31) {
                    fprintf(stderr, "%s:%d: VideoQMax out of range\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "VideoQMin")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.qmin = atoi(arg);
                if (video_enc.qmin < 1 || video_enc.qmin > 31) {
                    fprintf(stderr, "%s:%d: VideoQMin out of range\n",
                            filename, line_num);
                    errors++;
                }
            }
        } else if (!strcasecmp(cmd, "LumaElim")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.luma_elim_threshold = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "ChromaElim")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.chroma_elim_threshold = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "LumiMask")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.lumi_masking = atof(arg);
            }
        } else if (!strcasecmp(cmd, "DarkMask")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.dark_masking = atof(arg);
            }
        } else if (!strcasecmp(cmd, "NoVideo")) {
            video_id = CODEC_ID_NONE;
        } else if (!strcasecmp(cmd, "NoAudio")) {
            audio_id = CODEC_ID_NONE;
        } else if (!strcasecmp(cmd, "ACL")) {
            IPAddressACL acl;
            struct hostent *he;

            get_arg(arg, sizeof(arg), &p);
            if (strcasecmp(arg, "allow") == 0) {
                acl.action = IP_ALLOW;
            } else if (strcasecmp(arg, "deny") == 0) {
                acl.action = IP_DENY;
            } else {
                fprintf(stderr, "%s:%d: ACL action '%s' is not ALLOW or DENY\n",
                        filename, line_num, arg);
                errors++;
            }

            get_arg(arg, sizeof(arg), &p);

            he = gethostbyname(arg);
            if (!he) {
                fprintf(stderr, "%s:%d: ACL refers to invalid host or ip address '%s'\n",
                        filename, line_num, arg);
                errors++;
            } else {
                /* Only take the first */
                acl.first.s_addr = ntohl(((struct in_addr *) he->h_addr_list[0])->s_addr);
                acl.last = acl.first;
            }

            get_arg(arg, sizeof(arg), &p);

            if (arg[0]) {
                he = gethostbyname(arg);
                if (!he) {
                    fprintf(stderr, "%s:%d: ACL refers to invalid host or ip address '%s'\n",
                            filename, line_num, arg);
                    errors++;
                } else {
                    /* Only take the first */
                    acl.last.s_addr = ntohl(((struct in_addr *) he->h_addr_list[0])->s_addr);
                }
            }

            if (!errors) {
                IPAddressACL *nacl = (IPAddressACL *) av_mallocz(sizeof(*nacl));
                IPAddressACL **naclp = 0;

                *nacl = acl;
                nacl->next = 0;

                if (stream) {
                    naclp = &stream->acl;
                } else if (feed) {
                    naclp = &feed->acl;
                } else {
                    fprintf(stderr, "%s:%d: ACL found not in <stream> or <feed>\n",
                            filename, line_num);
                    errors++;
                }

                if (naclp) {
                    while (*naclp)
                        naclp = &(*naclp)->next;

                    *naclp = nacl;
                }
            }
        } else if (!strcasecmp(cmd, "RTSPOption")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                av_freep(&stream->rtsp_option);
                /* XXX: av_strdup ? */
                stream->rtsp_option = av_malloc(strlen(arg) + 1);
                if (stream->rtsp_option) {
                    strcpy(stream->rtsp_option, arg);
                }
            }
        } else if (!strcasecmp(cmd, "MulticastAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                if (!inet_aton(arg, &stream->multicast_ip)) {
                    fprintf(stderr, "%s:%d: Invalid IP address: %s\n", 
                            filename, line_num, arg);
                    errors++;
                }
                stream->is_multicast = 1;
                stream->loop = 1; /* default is looping */
            }
        } else if (!strcasecmp(cmd, "MulticastPort")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                stream->multicast_port = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "MulticastTTL")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                stream->multicast_ttl = atoi(arg);
            }
        } else if (!strcasecmp(cmd, "NoLoop")) {
            if (stream) {
                stream->loop = 0;
            }
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
        } else if (!strcasecmp(cmd, "LoadModule")) {
            get_arg(arg, sizeof(arg), &p);
#ifdef CONFIG_HAVE_DLOPEN
            load_module(arg);
#else
            fprintf(stderr, "%s:%d: Module support not compiled into this version: '%s'\n", 
                    filename, line_num, arg);
            errors++;
#endif
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


#if 0
static void write_packet(FFCodec *ffenc,
                         uint8_t *buf, int size)
{
    PacketHeader hdr;
    AVCodecContext *enc = &ffenc->enc;
    uint8_t *wptr;
    mk_header(&hdr, enc, size);
    wptr = http_fifo.wptr;
    fifo_write(&http_fifo, (uint8_t *)&hdr, sizeof(hdr), &wptr);
    fifo_write(&http_fifo, buf, size, &wptr);
    /* atomic modification of wptr */
    http_fifo.wptr = wptr;
    ffenc->data_count += size;
    ffenc->avg_frame_size = ffenc->avg_frame_size * AVG_COEF + size * (1.0 - AVG_COEF);
}
#endif

static void show_banner(void)
{
    printf("ffserver version " FFMPEG_VERSION ", Copyright (c) 2000-2003 Fabrice Bellard\n");
}

static void show_help(void)
{
    show_banner();
    printf("usage: ffserver [-L] [-h] [-f configfile]\n"
           "Hyper fast multi format Audio/Video streaming server\n"
           "\n"
           "-L            : print the LICENSE\n"
           "-h            : this help\n"
           "-f configfile : use configfile instead of /etc/ffserver.conf\n"
           );
}

static void show_license(void)
{
    show_banner();
    printf(
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
    my_program_dir = getcwd(0, 0);
    ffserver_daemon = 1;
    
    for(;;) {
        c = getopt(argc, argv, "ndLh?f:");
        if (c == -1)
            break;
        switch(c) {
        case 'L':
            show_license();
            exit(1);
        case '?':
        case 'h':
            show_help();
            exit(1);
        case 'n':
            no_launch = 1;
            break;
        case 'd':
            ffserver_debug = 1;
            ffserver_daemon = 0;
            break;
        case 'f':
            config_filename = optarg;
            break;
        default:
            exit(2);
        }
    }

    putenv("http_proxy");               /* Kill the http_proxy */

    srandom(gettime_ms() + (getpid() << 16));

    /* address on which the server will handle HTTP connections */
    my_http_addr.sin_family = AF_INET;
    my_http_addr.sin_port = htons (8080);
    my_http_addr.sin_addr.s_addr = htonl (INADDR_ANY);

    /* address on which the server will handle RTSP connections */
    my_rtsp_addr.sin_family = AF_INET;
    my_rtsp_addr.sin_port = htons (5454);
    my_rtsp_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    
    nb_max_connections = 5;
    max_bandwidth = 1000;
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

    build_file_streams();

    build_feed_streams();

    compute_bandwidth();

    /* put the process in background and detach it from its TTY */
    if (ffserver_daemon) {
        int pid;

        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid > 0) {
            /* parent : exit */
            exit(0);
        } else {
            /* child */
            setsid();
            chdir("/");
            close(0);
            open("/dev/null", O_RDWR);
            if (strcmp(logfilename, "-") != 0) {
                close(1);
                dup(0);
            }
            close(2);
            dup(0);
        }
    }

    /* signal init */
    signal(SIGPIPE, SIG_IGN);

    /* open log file if needed */
    if (logfilename[0] != '\0') {
        if (!strcmp(logfilename, "-"))
            logfile = stdout;
        else
            logfile = fopen(logfilename, "w");
    }

    if (http_server() < 0) {
        fprintf(stderr, "Could not start server\n");
        exit(1);
    }

    return 0;
}
