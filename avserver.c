/*
 * Multiple format streaming server
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"
#if !HAVE_CLOSESOCKET
#define closesocket close
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "libavformat/avformat.h"
// FIXME those are internal headers, avserver _really_ shouldn't use them
#include "libavformat/ffm.h"
#include "libavformat/network.h"
#include "libavformat/os_support.h"
#include "libavformat/rtpdec.h"
#include "libavformat/rtpproto.h"
#include "libavformat/rtsp.h"
#include "libavformat/avio_internal.h"
#include "libavformat/internal.h"
#include "libavformat/url.h"

#include "libavutil/avstring.h"
#include "libavutil/lfg.h"
#include "libavutil/dict.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/random_seed.h"
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"

#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#if HAVE_POLL_H
#include <poll.h>
#endif
#include <errno.h>
#include <time.h>
#include <sys/wait.h>
#include <signal.h>

#include "cmdutils.h"

const char program_name[] = "avserver";
const int program_birth_year = 2000;

static const OptionDef options[];

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

static const char *http_state[] = {
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

#define MAX_STREAMS 20

#define IOBUFFER_INIT_SIZE 8192

/* timeouts are in ms */
#define HTTP_REQUEST_TIMEOUT (15 * 1000)
#define RTSP_REQUEST_TIMEOUT (3600 * 24 * 1000)

#define SYNC_TIMEOUT (10 * 1000)

typedef struct RTSPActionServerSetup {
    uint32_t ipaddr;
    char transport_option[512];
} RTSPActionServerSetup;

typedef struct {
    int64_t count1, count2;
    int64_t time1, time2;
} DataRateData;

/* context associated with one connection */
typedef struct HTTPContext {
    enum HTTPState state;
    int fd; /* socket file descriptor */
    struct sockaddr_in from_addr; /* origin */
    struct pollfd *poll_entry; /* used when polling */
    int64_t timeout;
    uint8_t *buffer_ptr, *buffer_end;
    int http_error;
    int post;
    int chunked_encoding;
    int chunk_size;               /* 0 if it needs to be read */
    struct HTTPContext *next;
    int got_key_frame; /* stream 0 => 1, stream 1 => 2, stream 2=> 4 */
    int64_t data_count;
    /* feed input */
    int feed_fd;
    /* input format handling */
    AVFormatContext *fmt_in;
    int64_t start_time;            /* In milliseconds - this wraps fairly often */
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
    AVIOContext *pb;
    int seq; /* RTSP sequence number */

    /* RTP state specific */
    enum RTSPLowerTransport rtp_protocol;
    char session_id[32]; /* session id */
    AVFormatContext *rtp_ctx[MAX_STREAMS];

    /* RTP/UDP specific */
    URLContext *rtp_handles[MAX_STREAMS];

    /* RTP/TCP specific */
    struct HTTPContext *rtsp_c;
    uint8_t *packet_buffer, *packet_buffer_ptr, *packet_buffer_end;
} HTTPContext;

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

/* description of each stream of the avserver.conf file */
typedef struct FFStream {
    enum StreamType stream_type;
    char filename[1024];     /* stream filename */
    struct FFStream *feed;   /* feed we are using (can be null if
                                coming from file) */
    AVDictionary *in_opts;   /* input parameters */
    AVInputFormat *ifmt;       /* if non NULL, force input format */
    AVOutputFormat *fmt;
    IPAddressACL *acl;
    char dynamic_acl[1024];
    int nb_streams;
    int prebuffer;      /* Number of millseconds early to start */
    int64_t max_time;      /* Number of milliseconds to run */
    int send_on_key;
    AVStream *streams[MAX_STREAMS];
    int feed_streams[MAX_STREAMS]; /* index of streams in the feed */
    char feed_filename[1024]; /* file name of the feed storage, or
                                 input file name for a stream */
    char author[512];
    char title[512];
    char copyright[512];
    char comment[512];
    pid_t pid;  /* of avconv process */
    time_t pid_start;  /* of avconv process */
    char **child_argv;
    struct FFStream *next;
    unsigned bandwidth; /* bandwidth, in kbits/s */
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
    int truncate;        /* True if feeder connection truncate the feed file */
    int conns_served;
    int64_t bytes_served;
    int64_t feed_max_size;      /* maximum storage size, zero means unlimited */
    int64_t feed_write_index;   /* current write position in feed (it wraps around) */
    int64_t feed_size;          /* current size of feed */
    struct FFStream *next_feed;
} FFStream;

typedef struct FeedData {
    long long data_count;
    float avg_frame_size;   /* frame size averaged over last frames with exponential mean */
} FeedData;

static struct sockaddr_in my_http_addr;
static struct sockaddr_in my_rtsp_addr;

static char logfilename[1024];
static HTTPContext *first_http_ctx;
static FFStream *first_feed;   /* contains only feeds */
static FFStream *first_stream; /* contains all streams, including feeds */

static void new_connection(int server_fd, int is_rtsp);
static void close_connection(HTTPContext *c);

/* HTTP handling */
static int handle_connection(HTTPContext *c);
static int http_parse_request(HTTPContext *c);
static int http_send_data(HTTPContext *c);
static void compute_status(HTTPContext *c);
static int open_input_stream(HTTPContext *c, const char *info);
static int http_start_receive_data(HTTPContext *c);
static int http_receive_data(HTTPContext *c);

/* RTSP handling */
static int rtsp_parse_request(HTTPContext *c);
static void rtsp_cmd_describe(HTTPContext *c, const char *url);
static void rtsp_cmd_options(HTTPContext *c, const char *url);
static void rtsp_cmd_setup(HTTPContext *c, const char *url, RTSPMessageHeader *h);
static void rtsp_cmd_play(HTTPContext *c, const char *url, RTSPMessageHeader *h);
static void rtsp_cmd_pause(HTTPContext *c, const char *url, RTSPMessageHeader *h);
static void rtsp_cmd_teardown(HTTPContext *c, const char *url, RTSPMessageHeader *h);

/* SDP handling */
static int prepare_sdp_description(FFStream *stream, uint8_t **pbuffer,
                                   struct in_addr my_ip);

/* RTP handling */
static HTTPContext *rtp_new_connection(struct sockaddr_in *from_addr,
                                       FFStream *stream, const char *session_id,
                                       enum RTSPLowerTransport rtp_protocol);
static int rtp_new_av_stream(HTTPContext *c,
                             int stream_index, struct sockaddr_in *dest_addr,
                             HTTPContext *rtsp_c);

static const char *my_program_name;

static const char *config_filename;

static int avserver_debug;
static int no_launch;
static int need_to_start_children;

/* maximum number of simultaneous HTTP connections */
static unsigned int nb_max_http_connections = 2000;
static unsigned int nb_max_connections = 5;
static unsigned int nb_connections;

static uint64_t max_bandwidth = 1000;
static uint64_t current_bandwidth;

static int64_t cur_time;           // Making this global saves on passing it around everywhere

static AVLFG random_state;

static FILE *logfile = NULL;

static int64_t ffm_read_write_index(int fd)
{
    uint8_t buf[8];

    lseek(fd, 8, SEEK_SET);
    if (read(fd, buf, 8) != 8)
        return AVERROR(EIO);
    return AV_RB64(buf);
}

static int ffm_write_write_index(int fd, int64_t pos)
{
    uint8_t buf[8];
    int i;

    for(i=0;i<8;i++)
        buf[i] = (pos >> (56 - i * 8)) & 0xff;
    lseek(fd, 8, SEEK_SET);
    if (write(fd, buf, 8) != 8)
        return AVERROR(EIO);
    return 8;
}

static void ffm_set_write_index(AVFormatContext *s, int64_t pos,
                                int64_t file_size)
{
    FFMContext *ffm = s->priv_data;
    ffm->write_index = pos;
    ffm->file_size = file_size;
}

/* FIXME: make avserver work with IPv6 */
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

static void http_vlog(const char *fmt, va_list vargs)
{
    static int print_prefix = 1;
    if (logfile) {
        if (print_prefix) {
            char buf[32];
            ctime1(buf);
            fprintf(logfile, "%s ", buf);
        }
        print_prefix = strstr(fmt, "\n") != NULL;
        vfprintf(logfile, fmt, vargs);
        fflush(logfile);
    }
}

#ifdef __GNUC__
__attribute__ ((format (printf, 1, 2)))
#endif
static void http_log(const char *fmt, ...)
{
    va_list vargs;
    va_start(vargs, fmt);
    http_vlog(fmt, vargs);
    va_end(vargs);
}

static void http_av_log(void *ptr, int level, const char *fmt, va_list vargs)
{
    static int print_prefix = 1;
    AVClass *avc = ptr ? *(AVClass**)ptr : NULL;
    if (level > av_log_get_level())
        return;
    if (print_prefix && avc)
        http_log("[%s @ %p]", avc->item_name(ptr), ptr);
    print_prefix = strstr(fmt, "\n") != NULL;
    http_vlog(fmt, vargs);
}

static void log_connection(HTTPContext *c)
{
    if (c->suppress_log)
        return;

    http_log("%s - - [%s] \"%s %s\" %d %"PRId64"\n",
             inet_ntoa(c->from_addr.sin_addr), c->method, c->url,
             c->protocol, (c->http_error ? c->http_error : 200), c->data_count);
}

static void update_datarate(DataRateData *drd, int64_t count)
{
    if (!drd->time1 && !drd->count1) {
        drd->time1 = drd->time2 = cur_time;
        drd->count1 = drd->count2 = count;
    } else if (cur_time - drd->time2 > 5000) {
        drd->time1 = drd->time2;
        drd->count1 = drd->count2;
        drd->time2 = cur_time;
        drd->count2 = count;
    }
}

/* In bytes per second */
static int compute_datarate(DataRateData *drd, int64_t count)
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
                http_log("Unable to create children\n");
                exit(1);
            }
            if (!feed->pid) {
                /* In child */
                char pathname[1024];
                char *slash;
                int i;

                av_strlcpy(pathname, my_program_name, sizeof(pathname));

                slash = strrchr(pathname, '/');
                if (!slash)
                    slash = pathname;
                else
                    slash++;
                strcpy(slash, "avconv");

                http_log("Launch command line: ");
                http_log("%s ", pathname);
                for (i = 1; feed->child_argv[i] && feed->child_argv[i][0]; i++)
                    http_log("%s ", feed->child_argv[i]);
                http_log("\n");

                for (i = 3; i < 256; i++)
                    close(i);

                if (!avserver_debug) {
                    if (!freopen("/dev/null", "r", stdin))
                        http_log("failed to redirect STDIN to /dev/null\n;");
                    if (!freopen("/dev/null", "w", stdout))
                        http_log("failed to redirect STDOUT to /dev/null\n;");
                    if (!freopen("/dev/null", "w", stderr))
                        http_log("failed to redirect STDERR to /dev/null\n;");
                }

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

    my_addr->sin_family = AF_INET;
    if (bind (server_fd, (struct sockaddr *) my_addr, sizeof (*my_addr)) < 0) {
        char bindmsg[32];
        snprintf(bindmsg, sizeof(bindmsg), "bind(port %d)", ntohs(my_addr->sin_port));
        perror (bindmsg);
        closesocket(server_fd);
        return -1;
    }

    if (listen (server_fd, 5) < 0) {
        perror ("listen");
        closesocket(server_fd);
        return -1;
    }
    ff_socket_nonblock(server_fd, 1);

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
            snprintf(session_id, sizeof(session_id), "%08x%08x",
                     av_lfg_get(&random_state), av_lfg_get(&random_state));

            /* choose a port if none given */
            if (stream->multicast_port == 0) {
                stream->multicast_port = default_port;
                default_port += 100;
            }

            dest_addr.sin_family = AF_INET;
            dest_addr.sin_addr = stream->multicast_ip;
            dest_addr.sin_port = htons(stream->multicast_port);

            rtp_c = rtp_new_connection(&dest_addr, stream, session_id,
                                       RTSP_LOWER_TRANSPORT_UDP_MULTICAST);
            if (!rtp_c)
                continue;

            if (open_input_stream(rtp_c, "") < 0) {
                http_log("Could not open input stream for stream '%s'\n",
                         stream->filename);
                continue;
            }

            /* open each RTP stream */
            for(stream_index = 0; stream_index < stream->nb_streams;
                stream_index++) {
                dest_addr.sin_port = htons(stream->multicast_port +
                                           2 * stream_index);
                if (rtp_new_av_stream(rtp_c, stream_index, &dest_addr, NULL) < 0) {
                    http_log("Could not open output stream '%s/streamid=%d'\n",
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
    int server_fd = 0, rtsp_server_fd = 0;
    int ret, delay, delay1;
    struct pollfd *poll_table, *poll_entry;
    HTTPContext *c, *c_next;

    if(!(poll_table = av_mallocz((nb_max_http_connections + 2)*sizeof(*poll_table)))) {
        http_log("Impossible to allocate a poll table handling %d connections.\n", nb_max_http_connections);
        return -1;
    }

    if (my_http_addr.sin_port) {
        server_fd = socket_open_listen(&my_http_addr);
        if (server_fd < 0)
            return -1;
    }

    if (my_rtsp_addr.sin_port) {
        rtsp_server_fd = socket_open_listen(&my_rtsp_addr);
        if (rtsp_server_fd < 0)
            return -1;
    }

    if (!rtsp_server_fd && !server_fd) {
        http_log("HTTP and RTSP disabled.\n");
        return -1;
    }

    http_log("AVserver started.\n");

    start_children(first_feed);

    start_multicast();

    for(;;) {
        poll_entry = poll_table;
        if (server_fd) {
            poll_entry->fd = server_fd;
            poll_entry->events = POLLIN;
            poll_entry++;
        }
        if (rtsp_server_fd) {
            poll_entry->fd = rtsp_server_fd;
            poll_entry->events = POLLIN;
            poll_entry++;
        }

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
                    /* when avserver is doing the timing, we work by
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
            if (ret < 0 && ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR))
                return -1;
        } while (ret < 0);

        cur_time = av_gettime() / 1000;

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
        if (server_fd) {
            /* new HTTP connection request ? */
            if (poll_entry->revents & POLLIN)
                new_connection(server_fd, 0);
            poll_entry++;
        }
        if (rtsp_server_fd) {
            /* new RTSP connection request ? */
            if (poll_entry->revents & POLLIN)
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

static void http_send_too_busy_reply(int fd)
{
    char buffer[300];
    int len = snprintf(buffer, sizeof(buffer),
                       "HTTP/1.0 503 Server too busy\r\n"
                       "Content-type: text/html\r\n"
                       "\r\n"
                       "<html><head><title>Too busy</title></head><body>\r\n"
                       "<p>The server is too busy to serve your request at this time.</p>\r\n"
                       "<p>The number of current connections is %d, and this exceeds the limit of %d.</p>\r\n"
                       "</body></html>\r\n",
                       nb_connections, nb_max_connections);
    send(fd, buffer, len, 0);
}


static void new_connection(int server_fd, int is_rtsp)
{
    struct sockaddr_in from_addr;
    socklen_t len;
    int fd;
    HTTPContext *c = NULL;

    len = sizeof(from_addr);
    fd = accept(server_fd, (struct sockaddr *)&from_addr,
                &len);
    if (fd < 0) {
        http_log("error during accept %s\n", strerror(errno));
        return;
    }
    ff_socket_nonblock(fd, 1);

    if (nb_connections >= nb_max_connections) {
        http_send_too_busy_reply(fd);
        goto fail;
    }

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
    closesocket(fd);
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
        if (c1 == c)
            *cp = c->next;
        else
            cp = &c1->next;
    }

    /* remove references, if any (XXX: do it faster) */
    for(c1 = first_http_ctx; c1 != NULL; c1 = c1->next) {
        if (c1->rtsp_c == c)
            c1->rtsp_c = NULL;
    }

    /* remove connection associated resources */
    if (c->fd >= 0)
        closesocket(c->fd);
    if (c->fmt_in) {
        /* close each frame parser */
        for(i=0;i<c->fmt_in->nb_streams;i++) {
            st = c->fmt_in->streams[i];
            if (st->codec->codec)
                avcodec_close(st->codec);
        }
        avformat_close_input(&c->fmt_in);
    }

    /* free RTP output streams if any */
    nb_streams = 0;
    if (c->stream)
        nb_streams = c->stream->nb_streams;

    for(i=0;i<nb_streams;i++) {
        ctx = c->rtp_ctx[i];
        if (ctx) {
            av_write_trailer(ctx);
            av_dict_free(&ctx->metadata);
            av_free(ctx->streams[0]);
            av_free(ctx);
        }
        h = c->rtp_handles[i];
        if (h)
            ffurl_close(h);
    }

    ctx = &c->fmt_ctx;

    if (!c->last_packet_sent && c->state == HTTPSTATE_SEND_DATA_TRAILER) {
        if (ctx->oformat) {
            /* prepare header */
            if (avio_open_dyn_buf(&ctx->pb) >= 0) {
                av_write_trailer(ctx);
                av_freep(&c->pb_buffer);
                avio_close_dyn_buf(ctx->pb, &c->pb_buffer);
            }
        }
    }

    for(i=0; i<ctx->nb_streams; i++)
        av_free(ctx->streams[i]);

    if (c->stream && !c->post && c->stream->stream_type == STREAM_TYPE_LIVE)
        current_bandwidth -= c->stream->bandwidth;

    /* signal that there is no feed if we are the feeder socket */
    if (c->state == HTTPSTATE_RECEIVE_DATA && c->stream) {
        c->stream->feed_opened = 0;
        close(c->feed_fd);
    }

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
        len = recv(c->fd, c->buffer_ptr, 1, 0);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR))
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
        len = send(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr, 0);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR)) {
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
                if (c->http_error)
                    return -1;
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
        /* close connection if trailer sent */
        if (c->state == HTTPSTATE_SEND_DATA_TRAILER)
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
        len = send(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr, 0);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR)) {
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
        len = send(c->fd, c->packet_buffer_ptr,
                    c->packet_buffer_end - c->packet_buffer_ptr, 0);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR)) {
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
        if (av_strncasecmp(p, "Pragma:", 7) == 0) {
            const char *q = p + 7;

            while (*q && *q != '\n' && av_isspace(*q))
                q++;

            if (av_strncasecmp(q, "stream-switch-entry=", 20) == 0) {
                int stream_no;
                int rate_no;

                q += 20;

                memset(rates, 0xff, ratelen);

                while (1) {
                    while (*q && *q != '\n' && *q != ':')
                        q++;

                    if (sscanf(q, ":%d:%d", &stream_no, &rate_no) != 2)
                        break;

                    stream_no--;
                    if (stream_no < ratelen && stream_no >= 0)
                        rates[stream_no] = rate_no;

                    while (*q && *q != '\n' && !av_isspace(*q))
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
        AVCodecContext *feed_codec = feed->streams[i]->codec;

        if (feed_codec->codec_id != codec->codec_id ||
            feed_codec->sample_rate != codec->sample_rate ||
            feed_codec->width != codec->width ||
            feed_codec->height != codec->height)
            continue;

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
        AVCodecContext *codec = req->streams[i]->codec;

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
    while (!av_isspace(*p) && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    if (buf_size > 0)
        *q = '\0';
    *pp = p;
}

static void get_arg(char *buf, int buf_size, const char **pp)
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

static void parse_acl_row(FFStream *stream, FFStream* feed, IPAddressACL *ext_acl,
                         const char *p, const char *filename, int line_num)
{
    char arg[1024];
    IPAddressACL acl;
    int errors = 0;

    get_arg(arg, sizeof(arg), &p);
    if (av_strcasecmp(arg, "allow") == 0)
        acl.action = IP_ALLOW;
    else if (av_strcasecmp(arg, "deny") == 0)
        acl.action = IP_DENY;
    else {
        fprintf(stderr, "%s:%d: ACL action '%s' is not ALLOW or DENY\n",
                filename, line_num, arg);
        errors++;
    }

    get_arg(arg, sizeof(arg), &p);

    if (resolve_host(&acl.first, arg) != 0) {
        fprintf(stderr, "%s:%d: ACL refers to invalid host or ip address '%s'\n",
                filename, line_num, arg);
        errors++;
    } else
        acl.last = acl.first;

    get_arg(arg, sizeof(arg), &p);

    if (arg[0]) {
        if (resolve_host(&acl.last, arg) != 0) {
            fprintf(stderr, "%s:%d: ACL refers to invalid host or ip address '%s'\n",
                    filename, line_num, arg);
            errors++;
        }
    }

    if (!errors) {
        IPAddressACL *nacl = av_mallocz(sizeof(*nacl));
        IPAddressACL **naclp = 0;

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
        }
    }
}


static IPAddressACL* parse_dynamic_acl(FFStream *stream, HTTPContext *c)
{
    FILE* f;
    char line[1024];
    char  cmd[1024];
    IPAddressACL *acl = NULL;
    int line_num = 0;
    const char *p;

    f = fopen(stream->dynamic_acl, "r");
    if (!f) {
        perror(stream->dynamic_acl);
        return NULL;
    }

    acl = av_mallocz(sizeof(IPAddressACL));

    /* Build ACL */
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        p = line;
        while (av_isspace(*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;
        get_arg(cmd, sizeof(cmd), &p);

        if (!av_strcasecmp(cmd, "ACL"))
            parse_acl_row(NULL, NULL, acl, p, stream->dynamic_acl, line_num);
    }
    fclose(f);
    return acl;
}


static void free_acl_list(IPAddressACL *in_acl)
{
    IPAddressACL *pacl,*pacl2;

    pacl = in_acl;
    while(pacl) {
        pacl2 = pacl;
        pacl = pacl->next;
        av_freep(pacl2);
    }
}

static int validate_acl_list(IPAddressACL *in_acl, HTTPContext *c)
{
    enum IPAddressAction last_action = IP_DENY;
    IPAddressACL *acl;
    struct in_addr *src = &c->from_addr.sin_addr;
    unsigned long src_addr = src->s_addr;

    for (acl = in_acl; acl; acl = acl->next) {
        if (src_addr >= acl->first.s_addr && src_addr <= acl->last.s_addr)
            return (acl->action == IP_ALLOW) ? 1 : 0;
        last_action = acl->action;
    }

    /* Nothing matched, so return not the last action */
    return (last_action == IP_DENY) ? 1 : 0;
}

static int validate_acl(FFStream *stream, HTTPContext *c)
{
    int ret = 0;
    IPAddressACL *acl;


    /* if stream->acl is null validate_acl_list will return 1 */
    ret = validate_acl_list(stream->acl, c);

    if (stream->dynamic_acl[0]) {
        acl = parse_dynamic_acl(stream, c);

        ret = validate_acl_list(acl, c);

        free_acl_list(acl);
    }

    return ret;
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
    av_strlcpy(file1, filename, sizeof(file1));
    p = strrchr(file1, '.');
    if (p)
        *p = '\0';
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        av_strlcpy(file2, stream->filename, sizeof(file2));
        p = strrchr(file2, '.');
        if (p)
            *p = '\0';
        if (!strcmp(file1, file2)) {
            av_strlcpy(filename, stream->filename, max_size);
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
    const char *p;
    char *p1;
    enum RedirType redir_type;
    char cmd[32];
    char info[1024], filename[1024];
    char url[1024], *q;
    char protocol[32];
    char msg[1024];
    const char *mime_type;
    FFStream *stream;
    int i;
    char ratebuf[32];
    const char *useragent = 0;

    p = c->buffer;
    get_word(cmd, sizeof(cmd), &p);
    av_strlcpy(c->method, cmd, sizeof(c->method));

    if (!strcmp(cmd, "GET"))
        c->post = 0;
    else if (!strcmp(cmd, "POST"))
        c->post = 1;
    else
        return -1;

    get_word(url, sizeof(url), &p);
    av_strlcpy(c->url, url, sizeof(c->url));

    get_word(protocol, sizeof(protocol), (const char **)&p);
    if (strcmp(protocol, "HTTP/1.0") && strcmp(protocol, "HTTP/1.1"))
        return -1;

    av_strlcpy(c->protocol, protocol, sizeof(c->protocol));

    if (avserver_debug)
        http_log("%s - - New connection: %s %s\n", inet_ntoa(c->from_addr.sin_addr), cmd, url);

    /* find the filename and the optional info string in the request */
    p1 = strchr(url, '?');
    if (p1) {
        av_strlcpy(info, p1, sizeof(info));
        *p1 = '\0';
    } else
        info[0] = '\0';

    av_strlcpy(filename, url + ((*url == '/') ? 1 : 0), sizeof(filename)-1);

    for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
        if (av_strncasecmp(p, "User-Agent:", 11) == 0) {
            useragent = p + 11;
            if (*useragent && *useragent != '\n' && av_isspace(*useragent))
                useragent++;
            break;
        }
        p = strchr(p, '\n');
        if (!p)
            break;

        p++;
    }

    redir_type = REDIR_NONE;
    if (av_match_ext(filename, "asx")) {
        redir_type = REDIR_ASX;
        filename[strlen(filename)-1] = 'f';
    } else if (av_match_ext(filename, "asf") &&
        (!useragent || av_strncasecmp(useragent, "NSPlayer", 8) != 0)) {
        /* if this isn't WMP or lookalike, return the redirector file */
        redir_type = REDIR_ASF;
    } else if (av_match_ext(filename, "rpm,ram")) {
        redir_type = REDIR_RAM;
        strcpy(filename + strlen(filename)-2, "m");
    } else if (av_match_ext(filename, "rtsp")) {
        redir_type = REDIR_RTSP;
        compute_real_filename(filename, sizeof(filename) - 1);
    } else if (av_match_ext(filename, "sdp")) {
        redir_type = REDIR_SDP;
        compute_real_filename(filename, sizeof(filename) - 1);
    }

    // "redirect" / request to index.html
    if (!strlen(filename))
        av_strlcpy(filename, "index.html", sizeof(filename) - 1);

    stream = first_stream;
    while (stream != NULL) {
        if (!strcmp(stream->filename, filename) && validate_acl(stream, c))
            break;
        stream = stream->next;
    }
    if (stream == NULL) {
        snprintf(msg, sizeof(msg), "File '%s' not found", url);
        http_log("File '%s' not found\n", url);
        goto send_error;
    }

    c->stream = stream;
    memcpy(c->feed_streams, stream->feed_streams, sizeof(c->feed_streams));
    memset(c->switch_feed_streams, -1, sizeof(c->switch_feed_streams));

    if (stream->stream_type == STREAM_TYPE_REDIRECT) {
        c->http_error = 301;
        q = c->buffer;
        q += snprintf(q, c->buffer_size,
                      "HTTP/1.0 301 Moved\r\n"
                      "Location: %s\r\n"
                      "Content-type: text/html\r\n"
                      "\r\n"
                      "<html><head><title>Moved</title></head><body>\r\n"
                      "You should be <a href=\"%s\">redirected</a>.\r\n"
                      "</body></html>\r\n", stream->feed_filename, stream->feed_filename);
        /* prepare output buffer */
        c->buffer_ptr = c->buffer;
        c->buffer_end = q;
        c->state = HTTPSTATE_SEND_HEADER;
        return 0;
    }

    /* If this is WMP, get the rate information */
    if (extract_rates(ratebuf, sizeof(ratebuf), c->buffer)) {
        if (modify_current_stream(c, ratebuf)) {
            for (i = 0; i < FF_ARRAY_ELEMS(c->feed_streams); i++) {
                if (c->switch_feed_streams[i] >= 0)
                    c->switch_feed_streams[i] = -1;
            }
        }
    }

    if (c->post == 0 && stream->stream_type == STREAM_TYPE_LIVE)
        current_bandwidth += stream->bandwidth;

    /* If already streaming this feed, do not let start another feeder. */
    if (stream->feed_opened) {
        snprintf(msg, sizeof(msg), "This feed is already being received.");
        http_log("Feed '%s' already being received\n", stream->feed_filename);
        goto send_error;
    }

    if (c->post == 0 && max_bandwidth < current_bandwidth) {
        c->http_error = 503;
        q = c->buffer;
        q += snprintf(q, c->buffer_size,
                      "HTTP/1.0 503 Server too busy\r\n"
                      "Content-type: text/html\r\n"
                      "\r\n"
                      "<html><head><title>Too busy</title></head><body>\r\n"
                      "<p>The server is too busy to serve your request at this time.</p>\r\n"
                      "<p>The bandwidth being served (including your stream) is %"PRIu64"kbit/sec, "
                      "and this exceeds the limit of %"PRIu64"kbit/sec.</p>\r\n"
                      "</body></html>\r\n", current_bandwidth, max_bandwidth);
        /* prepare output buffer */
        c->buffer_ptr = c->buffer;
        c->buffer_end = q;
        c->state = HTTPSTATE_SEND_HEADER;
        return 0;
    }

    if (redir_type != REDIR_NONE) {
        const char *hostinfo = 0;

        for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
            if (av_strncasecmp(p, "Host:", 5) == 0) {
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

            while (av_isspace(*hostinfo))
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
                        q += snprintf(q, c->buffer_size,
                                      "HTTP/1.0 200 ASX Follows\r\n"
                                      "Content-type: video/x-ms-asf\r\n"
                                      "\r\n"
                                      "<ASX Version=\"3\">\r\n"
                                      //"<!-- Autogenerated by avserver -->\r\n"
                                      "<ENTRY><REF HREF=\"http://%s/%s%s\"/></ENTRY>\r\n"
                                      "</ASX>\r\n", hostbuf, filename, info);
                        break;
                    case REDIR_RAM:
                        q += snprintf(q, c->buffer_size,
                                      "HTTP/1.0 200 RAM Follows\r\n"
                                      "Content-type: audio/x-pn-realaudio\r\n"
                                      "\r\n"
                                      "# Autogenerated by avserver\r\n"
                                      "http://%s/%s%s\r\n", hostbuf, filename, info);
                        break;
                    case REDIR_ASF:
                        q += snprintf(q, c->buffer_size,
                                      "HTTP/1.0 200 ASF Redirect follows\r\n"
                                      "Content-type: video/x-ms-asf\r\n"
                                      "\r\n"
                                      "[Reference]\r\n"
                                      "Ref1=http://%s/%s%s\r\n", hostbuf, filename, info);
                        break;
                    case REDIR_RTSP:
                        {
                            char hostname[256], *p;
                            /* extract only hostname */
                            av_strlcpy(hostname, hostbuf, sizeof(hostname));
                            p = strrchr(hostname, ':');
                            if (p)
                                *p = '\0';
                            q += snprintf(q, c->buffer_size,
                                          "HTTP/1.0 200 RTSP Redirect follows\r\n"
                                          /* XXX: incorrect mime type ? */
                                          "Content-type: application/x-rtsp\r\n"
                                          "\r\n"
                                          "rtsp://%s:%d/%s\r\n", hostname, ntohs(my_rtsp_addr.sin_port), filename);
                        }
                        break;
                    case REDIR_SDP:
                        {
                            uint8_t *sdp_data;
                            int sdp_data_size;
                            socklen_t len;
                            struct sockaddr_in my_addr;

                            q += snprintf(q, c->buffer_size,
                                          "HTTP/1.0 200 OK\r\n"
                                          "Content-type: application/sdp\r\n"
                                          "\r\n");

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
                        abort();
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

    if (c->post) {
        /* if post, it means a feed is being sent */
        if (!stream->is_feed) {
            /* However it might be a status report from WMP! Let us log the
             * data as it might come in handy one day. */
            const char *logline = 0;
            int client_id = 0;

            for (p = c->buffer; *p && *p != '\r' && *p != '\n'; ) {
                if (av_strncasecmp(p, "Pragma: log-line=", 17) == 0) {
                    logline = p;
                    break;
                }
                if (av_strncasecmp(p, "Pragma: client-id=", 18) == 0)
                    client_id = strtol(p + 18, 0, 10);
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

#ifdef DEBUG
            http_log("\nGot request:\n%s\n", c->buffer);
#endif

            if (client_id && extract_rates(ratebuf, sizeof(ratebuf), c->buffer)) {
                HTTPContext *wmpc;

                /* Now we have to find the client_id */
                for (wmpc = first_http_ctx; wmpc; wmpc = wmpc->next) {
                    if (wmpc->wmp_client_id == client_id)
                        break;
                }

                if (wmpc && modify_current_stream(wmpc, ratebuf))
                    wmpc->switch_pending = 1;
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

#ifdef DEBUG
    if (strcmp(stream->filename + strlen(stream->filename) - 4, ".asf") == 0)
        http_log("\nGot request:\n%s\n", c->buffer);
#endif

    if (c->stream->stream_type == STREAM_TYPE_STATUS)
        goto send_status;

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
        mime_type = "application/x-octet-stream";
    q += snprintf(q, q - (char *) c->buffer + c->buffer_size, "Pragma: no-cache\r\n");

    /* for asf, we need extra headers */
    if (!strcmp(c->stream->fmt->name,"asf_stream")) {
        /* Need to allocate a client id */

        c->wmp_client_id = av_lfg_get(&random_state);

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
    q += snprintf(q, c->buffer_size,
                  "HTTP/1.0 404 Not Found\r\n"
                  "Content-type: text/html\r\n"
                  "\r\n"
                  "<html>\n"
                  "<head><title>404 Not Found</title></head>\n"
                  "<body>%s</body>\n"
                  "</html>\n", msg);
    /* prepare output buffer */
    c->buffer_ptr = c->buffer;
    c->buffer_end = q;
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
 send_status:
    compute_status(c);
    c->http_error = 200; /* horrible : we use this value to avoid
                            going to the send data state */
    c->state = HTTPSTATE_SEND_HEADER;
    return 0;
}

static void fmt_bytecount(AVIOContext *pb, int64_t count)
{
    static const char suffix[] = " kMGTP";
    const char *s;

    for (s = suffix; count >= 100000 && s[1]; count /= 1000, s++);

    avio_printf(pb, "%"PRId64"%c", count, *s);
}

static void compute_status(HTTPContext *c)
{
    HTTPContext *c1;
    FFStream *stream;
    char *p;
    time_t ti;
    int i, len;
    AVIOContext *pb;

    if (avio_open_dyn_buf(&pb) < 0) {
        /* XXX: return an error ? */
        c->buffer_ptr = c->buffer;
        c->buffer_end = c->buffer;
        return;
    }

    avio_printf(pb, "HTTP/1.0 200 OK\r\n");
    avio_printf(pb, "Content-type: %s\r\n", "text/html");
    avio_printf(pb, "Pragma: no-cache\r\n");
    avio_printf(pb, "\r\n");

    avio_printf(pb, "<html><head><title>%s Status</title>\n", program_name);
    if (c->stream->feed_filename[0])
        avio_printf(pb, "<link rel=\"shortcut icon\" href=\"%s\">\n", c->stream->feed_filename);
    avio_printf(pb, "</head>\n<body>");
    avio_printf(pb, "<h1>%s Status</h1>\n", program_name);
    /* format status */
    avio_printf(pb, "<h2>Available Streams</h2>\n");
    avio_printf(pb, "<table cellspacing=0 cellpadding=4>\n");
    avio_printf(pb, "<tr><th valign=top>Path<th align=left>Served<br>Conns<th><br>bytes<th valign=top>Format<th>Bit rate<br>kbits/s<th align=left>Video<br>kbits/s<th><br>Codec<th align=left>Audio<br>kbits/s<th><br>Codec<th align=left valign=top>Feed\n");
    stream = first_stream;
    while (stream != NULL) {
        char sfilename[1024];
        char *eosf;

        if (stream->feed != stream) {
            av_strlcpy(sfilename, stream->filename, sizeof(sfilename) - 10);
            eosf = sfilename + strlen(sfilename);
            if (eosf - sfilename >= 4) {
                if (strcmp(eosf - 4, ".asf") == 0)
                    strcpy(eosf - 4, ".asx");
                else if (strcmp(eosf - 3, ".rm") == 0)
                    strcpy(eosf - 3, ".ram");
                else if (stream->fmt && !strcmp(stream->fmt->name, "rtp")) {
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

            avio_printf(pb, "<tr><td><a href=\"/%s\">%s</a> ",
                         sfilename, stream->filename);
            avio_printf(pb, "<td align=right> %d <td align=right> ",
                        stream->conns_served);
            fmt_bytecount(pb, stream->bytes_served);
            switch(stream->stream_type) {
            case STREAM_TYPE_LIVE: {
                    int audio_bit_rate = 0;
                    int video_bit_rate = 0;
                    const char *audio_codec_name = "";
                    const char *video_codec_name = "";
                    const char *audio_codec_name_extra = "";
                    const char *video_codec_name_extra = "";

                    for(i=0;i<stream->nb_streams;i++) {
                        AVStream *st = stream->streams[i];
                        AVCodec *codec = avcodec_find_encoder(st->codec->codec_id);
                        switch(st->codec->codec_type) {
                        case AVMEDIA_TYPE_AUDIO:
                            audio_bit_rate += st->codec->bit_rate;
                            if (codec) {
                                if (*audio_codec_name)
                                    audio_codec_name_extra = "...";
                                audio_codec_name = codec->name;
                            }
                            break;
                        case AVMEDIA_TYPE_VIDEO:
                            video_bit_rate += st->codec->bit_rate;
                            if (codec) {
                                if (*video_codec_name)
                                    video_codec_name_extra = "...";
                                video_codec_name = codec->name;
                            }
                            break;
                        case AVMEDIA_TYPE_DATA:
                            video_bit_rate += st->codec->bit_rate;
                            break;
                        default:
                            abort();
                        }
                    }
                    avio_printf(pb, "<td align=center> %s <td align=right> %d <td align=right> %d <td> %s %s <td align=right> %d <td> %s %s",
                                 stream->fmt->name,
                                 stream->bandwidth,
                                 video_bit_rate / 1000, video_codec_name, video_codec_name_extra,
                                 audio_bit_rate / 1000, audio_codec_name, audio_codec_name_extra);
                    if (stream->feed)
                        avio_printf(pb, "<td>%s", stream->feed->filename);
                    else
                        avio_printf(pb, "<td>%s", stream->feed_filename);
                    avio_printf(pb, "\n");
                }
                break;
            default:
                avio_printf(pb, "<td align=center> - <td align=right> - <td align=right> - <td><td align=right> - <td>\n");
                break;
            }
        }
        stream = stream->next;
    }
    avio_printf(pb, "</table>\n");

    stream = first_stream;
    while (stream != NULL) {
        if (stream->feed == stream) {
            avio_printf(pb, "<h2>Feed %s</h2>", stream->filename);
            if (stream->pid) {
                avio_printf(pb, "Running as pid %d.\n", stream->pid);

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
                            avio_printf(pb, "Currently using %s%% of the cpu. Total time used %s.\n",
                                         cpuperc, cpuused);
                        }
                        fclose(pid_stat);
                    }
                }
#endif

                avio_printf(pb, "<p>");
            }
            avio_printf(pb, "<table cellspacing=0 cellpadding=4><tr><th>Stream<th>type<th>kbits/s<th align=left>codec<th align=left>Parameters\n");

            for (i = 0; i < stream->nb_streams; i++) {
                AVStream *st = stream->streams[i];
                AVCodec *codec = avcodec_find_encoder(st->codec->codec_id);
                const char *type = "unknown";
                char parameters[64];

                parameters[0] = 0;

                switch(st->codec->codec_type) {
                case AVMEDIA_TYPE_AUDIO:
                    type = "audio";
                    snprintf(parameters, sizeof(parameters), "%d channel(s), %d Hz", st->codec->channels, st->codec->sample_rate);
                    break;
                case AVMEDIA_TYPE_VIDEO:
                    type = "video";
                    snprintf(parameters, sizeof(parameters), "%dx%d, q=%d-%d, fps=%d", st->codec->width, st->codec->height,
                                st->codec->qmin, st->codec->qmax, st->codec->time_base.den / st->codec->time_base.num);
                    break;
                default:
                    abort();
                }
                avio_printf(pb, "<tr><td align=right>%d<td>%s<td align=right>%d<td>%s<td>%s\n",
                        i, type, st->codec->bit_rate/1000, codec ? codec->name : "", parameters);
            }
            avio_printf(pb, "</table>\n");

        }
        stream = stream->next;
    }

    /* connection status */
    avio_printf(pb, "<h2>Connection Status</h2>\n");

    avio_printf(pb, "Number of connections: %d / %d<br>\n",
                 nb_connections, nb_max_connections);

    avio_printf(pb, "Bandwidth in use: %"PRIu64"k / %"PRIu64"k<br>\n",
                 current_bandwidth, max_bandwidth);

    avio_printf(pb, "<table>\n");
    avio_printf(pb, "<tr><th>#<th>File<th>IP<th>Proto<th>State<th>Target bits/sec<th>Actual bits/sec<th>Bytes transferred\n");
    c1 = first_http_ctx;
    i = 0;
    while (c1 != NULL) {
        int bitrate;
        int j;

        bitrate = 0;
        if (c1->stream) {
            for (j = 0; j < c1->stream->nb_streams; j++) {
                if (!c1->stream->feed)
                    bitrate += c1->stream->streams[j]->codec->bit_rate;
                else if (c1->feed_streams[j] >= 0)
                    bitrate += c1->stream->feed->streams[c1->feed_streams[j]]->codec->bit_rate;
            }
        }

        i++;
        p = inet_ntoa(c1->from_addr.sin_addr);
        avio_printf(pb, "<tr><td><b>%d</b><td>%s%s<td>%s<td>%s<td>%s<td align=right>",
                    i,
                    c1->stream ? c1->stream->filename : "",
                    c1->state == HTTPSTATE_RECEIVE_DATA ? "(input)" : "",
                    p,
                    c1->protocol,
                    http_state[c1->state]);
        fmt_bytecount(pb, bitrate);
        avio_printf(pb, "<td align=right>");
        fmt_bytecount(pb, compute_datarate(&c1->datarate, c1->data_count) * 8);
        avio_printf(pb, "<td align=right>");
        fmt_bytecount(pb, c1->data_count);
        avio_printf(pb, "\n");
        c1 = c1->next;
    }
    avio_printf(pb, "</table>\n");

    /* date */
    ti = time(NULL);
    p = ctime(&ti);
    avio_printf(pb, "<hr size=1 noshade>Generated at %s", p);
    avio_printf(pb, "</body>\n</html>\n");

    len = avio_close_dyn_buf(pb, &c->pb_buffer);
    c->buffer_ptr = c->pb_buffer;
    c->buffer_end = c->pb_buffer + len;
}

static int open_input_stream(HTTPContext *c, const char *info)
{
    char buf[128];
    char input_filename[1024];
    AVFormatContext *s = NULL;
    int i, ret;
    int64_t stream_pos;

    /* find file name */
    if (c->stream->feed) {
        strcpy(input_filename, c->stream->feed->feed_filename);
        /* compute position (absolute time) */
        if (av_find_info_tag(buf, sizeof(buf), "date", info)) {
            if ((ret = av_parse_time(&stream_pos, buf, 0)) < 0)
                return ret;
        } else if (av_find_info_tag(buf, sizeof(buf), "buffer", info)) {
            int prebuffer = strtol(buf, 0, 10);
            stream_pos = av_gettime() - prebuffer * (int64_t)1000000;
        } else
            stream_pos = av_gettime() - c->stream->prebuffer * (int64_t)1000;
    } else {
        strcpy(input_filename, c->stream->feed_filename);
        /* compute position (relative time) */
        if (av_find_info_tag(buf, sizeof(buf), "date", info)) {
            if ((ret = av_parse_time(&stream_pos, buf, 1)) < 0)
                return ret;
        } else
            stream_pos = 0;
    }
    if (input_filename[0] == '\0')
        return -1;

    /* open stream */
    if ((ret = avformat_open_input(&s, input_filename, c->stream->ifmt, &c->stream->in_opts)) < 0) {
        http_log("could not open %s: %d\n", input_filename, ret);
        return -1;
    }
    s->flags |= AVFMT_FLAG_GENPTS;
    c->fmt_in = s;
    if (strcmp(s->iformat->name, "ffm") && avformat_find_stream_info(c->fmt_in, NULL) < 0) {
        http_log("Could not find stream info '%s'\n", input_filename);
        avformat_close_input(&s);
        return -1;
    }

    /* choose stream as clock source (we favorize video stream if
       present) for packet sending */
    c->pts_stream_index = 0;
    for(i=0;i<c->stream->nb_streams;i++) {
        if (c->pts_stream_index == 0 &&
            c->stream->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            c->pts_stream_index = i;
        }
    }

    if (c->fmt_in->iformat->read_seek)
        av_seek_frame(c->fmt_in, -1, stream_pos, 0);
    /* set the start time (needed for maxtime and RTP packet timing) */
    c->start_time = cur_time;
    c->first_pts = AV_NOPTS_VALUE;
    return 0;
}

/* return the server clock (in us) */
static int64_t get_server_clock(HTTPContext *c)
{
    /* compute current pts value from system time */
    return (cur_time - c->start_time) * 1000;
}

/* return the estimated time at which the current packet must be sent
   (in us) */
static int64_t get_packet_send_clock(HTTPContext *c)
{
    int bytes_left, bytes_sent, frame_bytes;

    frame_bytes = c->cur_frame_bytes;
    if (frame_bytes <= 0)
        return c->cur_pts;
    else {
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
        av_dict_set(&c->fmt_ctx.metadata, "author"   , c->stream->author   , 0);
        av_dict_set(&c->fmt_ctx.metadata, "comment"  , c->stream->comment  , 0);
        av_dict_set(&c->fmt_ctx.metadata, "copyright", c->stream->copyright, 0);
        av_dict_set(&c->fmt_ctx.metadata, "title"    , c->stream->title    , 0);

        c->fmt_ctx.streams = av_mallocz(sizeof(AVStream *) * c->stream->nb_streams);

        for(i=0;i<c->stream->nb_streams;i++) {
            AVStream *src;
            c->fmt_ctx.streams[i] = av_mallocz(sizeof(AVStream));
            /* if file or feed, then just take streams from FFStream struct */
            if (!c->stream->feed ||
                c->stream->feed == c->stream)
                src = c->stream->streams[i];
            else
                src = c->stream->feed->streams[c->stream->feed_streams[i]];

            *(c->fmt_ctx.streams[i]) = *src;
            c->fmt_ctx.streams[i]->priv_data = 0;
            c->fmt_ctx.streams[i]->codec->frame_number = 0; /* XXX: should be done in
                                           AVStream, not in codec */
        }
        /* set output format parameters */
        c->fmt_ctx.oformat = c->stream->fmt;
        c->fmt_ctx.nb_streams = c->stream->nb_streams;

        c->got_key_frame = 0;

        /* prepare header and save header data in a stream */
        if (avio_open_dyn_buf(&c->fmt_ctx.pb) < 0) {
            /* XXX: potential leak */
            return -1;
        }
        c->fmt_ctx.pb->seekable = 0;

        /*
         * HACK to avoid mpeg ps muxer to spit many underflow errors
         * Default value from Libav
         * Try to set it use configuration option
         */
        c->fmt_ctx.max_delay = (int)(0.7*AV_TIME_BASE);

        if (avformat_write_header(&c->fmt_ctx, NULL) < 0) {
            http_log("Error writing output header\n");
            return -1;
        }
        av_dict_free(&c->fmt_ctx.metadata);

        len = avio_close_dyn_buf(c->fmt_ctx.pb, &c->pb_buffer);
        c->buffer_ptr = c->pb_buffer;
        c->buffer_end = c->pb_buffer + len;

        c->state = HTTPSTATE_SEND_DATA;
        c->last_packet_sent = 0;
        break;
    case HTTPSTATE_SEND_DATA:
        /* find a new packet */
        /* read a packet from the input stream */
        if (c->stream->feed)
            ffm_set_write_index(c->fmt_in,
                                c->stream->feed->feed_write_index,
                                c->stream->feed->feed_size);

        if (c->stream->max_time &&
            c->stream->max_time + c->start_time - cur_time < 0)
            /* We have timed out */
            c->state = HTTPSTATE_SEND_DATA_TRAILER;
        else {
            AVPacket pkt;
        redo:
            ret = av_read_frame(c->fmt_in, &pkt);
            if (ret < 0) {
                if (c->stream->feed) {
                    /* if coming from feed, it means we reached the end of the
                       ffm file, so must wait for more data */
                    c->state = HTTPSTATE_WAIT_FEED;
                    return 1; /* state changed */
                } else if (ret == AVERROR(EAGAIN)) {
                    /* input not ready, come back later */
                    return 0;
                } else {
                    if (c->stream->loop) {
                        avformat_close_input(&c->fmt_in);
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
                int source_index = pkt.stream_index;
                /* update first pts if needed */
                if (c->first_pts == AV_NOPTS_VALUE) {
                    c->first_pts = av_rescale_q(pkt.dts, c->fmt_in->streams[pkt.stream_index]->time_base, AV_TIME_BASE_Q);
                    c->start_time = cur_time;
                }
                /* send it to the appropriate stream */
                if (c->stream->feed) {
                    /* if coming from a feed, select the right stream */
                    if (c->switch_pending) {
                        c->switch_pending = 0;
                        for(i=0;i<c->stream->nb_streams;i++) {
                            if (c->switch_feed_streams[i] == pkt.stream_index)
                                if (pkt.flags & AV_PKT_FLAG_KEY)
                                    c->switch_feed_streams[i] = -1;
                            if (c->switch_feed_streams[i] >= 0)
                                c->switch_pending = 1;
                        }
                    }
                    for(i=0;i<c->stream->nb_streams;i++) {
                        if (c->stream->feed_streams[i] == pkt.stream_index) {
                            AVStream *st = c->fmt_in->streams[source_index];
                            pkt.stream_index = i;
                            if (pkt.flags & AV_PKT_FLAG_KEY &&
                                (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
                                 c->stream->nb_streams == 1))
                                c->got_key_frame = 1;
                            if (!c->stream->send_on_key || c->got_key_frame)
                                goto send_it;
                        }
                    }
                } else {
                    AVCodecContext *codec;
                    AVStream *ist, *ost;
                send_it:
                    ist = c->fmt_in->streams[source_index];
                    /* specific handling for RTP: we use several
                       output stream (one for each RTP
                       connection). XXX: need more abstract handling */
                    if (c->is_packetized) {
                        /* compute send time and duration */
                        c->cur_pts = av_rescale_q(pkt.dts, ist->time_base, AV_TIME_BASE_Q);
                        c->cur_pts -= c->first_pts;
                        c->cur_frame_duration = av_rescale_q(pkt.duration, ist->time_base, AV_TIME_BASE_Q);
                        /* find RTP context */
                        c->packet_stream_index = pkt.stream_index;
                        ctx = c->rtp_ctx[c->packet_stream_index];
                        if(!ctx) {
                            av_free_packet(&pkt);
                            break;
                        }
                        codec = ctx->streams[0]->codec;
                        /* only one stream per RTP connection */
                        pkt.stream_index = 0;
                    } else {
                        ctx = &c->fmt_ctx;
                        /* Fudge here */
                        codec = ctx->streams[pkt.stream_index]->codec;
                    }

                    if (c->is_packetized) {
                        int max_packet_size;
                        if (c->rtp_protocol == RTSP_LOWER_TRANSPORT_TCP)
                            max_packet_size = RTSP_TCP_MAX_PACKET_SIZE;
                        else
                            max_packet_size = c->rtp_handles[c->packet_stream_index]->max_packet_size;
                        ret = ffio_open_dyn_packet_buf(&ctx->pb, max_packet_size);
                    } else {
                        ret = avio_open_dyn_buf(&ctx->pb);
                    }
                    if (ret < 0) {
                        /* XXX: potential leak */
                        return -1;
                    }
                    ost = ctx->streams[pkt.stream_index];

                    ctx->pb->seekable = 0;
                    if (pkt.dts != AV_NOPTS_VALUE)
                        pkt.dts = av_rescale_q(pkt.dts, ist->time_base, ost->time_base);
                    if (pkt.pts != AV_NOPTS_VALUE)
                        pkt.pts = av_rescale_q(pkt.pts, ist->time_base, ost->time_base);
                    pkt.duration = av_rescale_q(pkt.duration, ist->time_base, ost->time_base);
                    if (av_write_frame(ctx, &pkt) < 0) {
                        http_log("Error writing frame to output\n");
                        c->state = HTTPSTATE_SEND_DATA_TRAILER;
                    }

                    len = avio_close_dyn_buf(ctx->pb, &c->pb_buffer);
                    c->cur_frame_bytes = len;
                    c->buffer_ptr = c->pb_buffer;
                    c->buffer_end = c->pb_buffer + len;

                    codec->frame_number++;
                    if (len == 0) {
                        av_free_packet(&pkt);
                        goto redo;
                    }
                }
                av_free_packet(&pkt);
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
        if (avio_open_dyn_buf(&ctx->pb) < 0) {
            /* XXX: potential leak */
            return -1;
        }
        c->fmt_ctx.pb->seekable = 0;
        av_write_trailer(ctx);
        len = avio_close_dyn_buf(ctx->pb, &c->pb_buffer);
        c->buffer_ptr = c->pb_buffer;
        c->buffer_end = c->pb_buffer + len;

        c->last_packet_sent = 1;
        break;
    }
    return 0;
}

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
            else if (ret != 0)
                /* state change requested */
                break;
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

                if (c->rtp_protocol == RTSP_LOWER_TRANSPORT_TCP) {
                    /* RTP packets are sent inside the RTSP TCP connection */
                    AVIOContext *pb;
                    int interleaved_index, size;
                    uint8_t header[4];
                    HTTPContext *rtsp_c;

                    rtsp_c = c->rtsp_c;
                    /* if no RTSP connection left, error */
                    if (!rtsp_c)
                        return -1;
                    /* if already sending something, then wait. */
                    if (rtsp_c->state != RTSPSTATE_WAIT_REQUEST)
                        break;
                    if (avio_open_dyn_buf(&pb) < 0)
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
                    avio_write(pb, header, 4);
                    /* write RTP packet data */
                    c->buffer_ptr += 4;
                    avio_write(pb, c->buffer_ptr, len);
                    size = avio_close_dyn_buf(pb, &c->packet_buffer);
                    /* prepare asynchronous TCP sending */
                    rtsp_c->packet_buffer_ptr = c->packet_buffer;
                    rtsp_c->packet_buffer_end = c->packet_buffer + size;
                    c->buffer_ptr += len;

                    /* send everything we can NOW */
                    len = send(rtsp_c->fd, rtsp_c->packet_buffer_ptr,
                                rtsp_c->packet_buffer_end - rtsp_c->packet_buffer_ptr, 0);
                    if (len > 0)
                        rtsp_c->packet_buffer_ptr += len;
                    if (rtsp_c->packet_buffer_ptr < rtsp_c->packet_buffer_end) {
                        /* if we could not send all the data, we will
                           send it later, so a new state is needed to
                           "lock" the RTSP TCP connection */
                        rtsp_c->state = RTSPSTATE_SEND_PACKET;
                        break;
                    } else
                        /* all data has been sent */
                        av_freep(&c->packet_buffer);
                } else {
                    /* send RTP packet directly in UDP */
                    c->buffer_ptr += 4;
                    ffurl_write(c->rtp_handles[c->packet_stream_index],
                                c->buffer_ptr, len);
                    c->buffer_ptr += len;
                    /* here we continue as we can send several packets per 10 ms slot */
                }
            } else {
                /* TCP data output */
                len = send(c->fd, c->buffer_ptr, c->buffer_end - c->buffer_ptr, 0);
                if (len < 0) {
                    if (ff_neterrno() != AVERROR(EAGAIN) &&
                        ff_neterrno() != AVERROR(EINTR))
                        /* error : close connection */
                        return -1;
                    else
                        return 0;
                } else
                    c->buffer_ptr += len;

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
    if (fd < 0) {
        http_log("Error opening feeder file: %s\n", strerror(errno));
        return -1;
    }
    c->feed_fd = fd;

    if (c->stream->truncate) {
        /* truncate feed file */
        ffm_write_write_index(c->feed_fd, FFM_PACKET_SIZE);
        http_log("Truncating feed file '%s'\n", c->stream->feed_filename);
        if (ftruncate(c->feed_fd, FFM_PACKET_SIZE) < 0) {
            http_log("Error truncating feed file: %s\n", strerror(errno));
            return -1;
        }
    } else {
        if ((c->stream->feed_write_index = ffm_read_write_index(fd)) < 0) {
            http_log("Error reading write index from feed file: %s\n", strerror(errno));
            return -1;
        }
    }

    c->stream->feed_write_index = FFMAX(ffm_read_write_index(fd), FFM_PACKET_SIZE);
    c->stream->feed_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    /* init buffer input */
    c->buffer_ptr = c->buffer;
    c->buffer_end = c->buffer + FFM_PACKET_SIZE;
    c->stream->feed_opened = 1;
    c->chunked_encoding = !!av_stristr(c->buffer, "Transfer-Encoding: chunked");
    return 0;
}

static int http_receive_data(HTTPContext *c)
{
    HTTPContext *c1;
    int len, loop_run = 0;

    while (c->chunked_encoding && !c->chunk_size &&
           c->buffer_end > c->buffer_ptr) {
        /* read chunk header, if present */
        len = recv(c->fd, c->buffer_ptr, 1, 0);

        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR))
                /* error : close connection */
                goto fail;
            return 0;
        } else if (len == 0) {
            /* end of connection : close it */
            goto fail;
        } else if (c->buffer_ptr - c->buffer >= 2 &&
                   !memcmp(c->buffer_ptr - 1, "\r\n", 2)) {
            c->chunk_size = strtol(c->buffer, 0, 16);
            if (c->chunk_size == 0) // end of stream
                goto fail;
            c->buffer_ptr = c->buffer;
            break;
        } else if (++loop_run > 10) {
            /* no chunk header, abort */
            goto fail;
        } else {
            c->buffer_ptr++;
        }
    }

    if (c->buffer_end > c->buffer_ptr) {
        len = recv(c->fd, c->buffer_ptr,
                   FFMIN(c->chunk_size, c->buffer_end - c->buffer_ptr), 0);
        if (len < 0) {
            if (ff_neterrno() != AVERROR(EAGAIN) &&
                ff_neterrno() != AVERROR(EINTR))
                /* error : close connection */
                goto fail;
        } else if (len == 0)
            /* end of connection : close it */
            goto fail;
        else {
            c->chunk_size -= len;
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
            /* XXX: use llseek or url_seek */
            lseek(c->feed_fd, feed->feed_write_index, SEEK_SET);
            if (write(c->feed_fd, c->buffer, FFM_PACKET_SIZE) < 0) {
                http_log("Error writing to feed file: %s\n", strerror(errno));
                goto fail;
            }

            feed->feed_write_index += FFM_PACKET_SIZE;
            /* update file size */
            if (feed->feed_write_index > c->stream->feed_size)
                feed->feed_size = feed->feed_write_index;

            /* handle wrap around if max file size reached */
            if (c->stream->feed_max_size && feed->feed_write_index >= c->stream->feed_max_size)
                feed->feed_write_index = FFM_PACKET_SIZE;

            /* write index */
            if (ffm_write_write_index(c->feed_fd, feed->feed_write_index) < 0) {
                http_log("Error writing index to feed file: %s\n", strerror(errno));
                goto fail;
            }

            /* wake up any waiting connections */
            for(c1 = first_http_ctx; c1 != NULL; c1 = c1->next) {
                if (c1->state == HTTPSTATE_WAIT_FEED &&
                    c1->stream->feed == c->stream->feed)
                    c1->state = HTTPSTATE_SEND_DATA;
            }
        } else {
            /* We have a header in our hands that contains useful data */
            AVFormatContext *s = avformat_alloc_context();
            AVIOContext *pb;
            AVInputFormat *fmt_in;
            int i;

            if (!s)
                goto fail;

            /* use feed output format name to find corresponding input format */
            fmt_in = av_find_input_format(feed->fmt->name);
            if (!fmt_in)
                goto fail;

            pb = avio_alloc_context(c->buffer, c->buffer_end - c->buffer,
                                    0, NULL, NULL, NULL, NULL);
            pb->seekable = 0;

            s->pb = pb;
            if (avformat_open_input(&s, c->stream->feed_filename, fmt_in, NULL) < 0) {
                av_free(pb);
                goto fail;
            }

            /* Now we have the actual streams */
            if (s->nb_streams != feed->nb_streams) {
                avformat_close_input(&s);
                av_free(pb);
                http_log("Feed '%s' stream number does not match registered feed\n",
                         c->stream->feed_filename);
                goto fail;
            }

            for (i = 0; i < s->nb_streams; i++) {
                AVStream *fst = feed->streams[i];
                AVStream *st = s->streams[i];
                avcodec_copy_context(fst->codec, st->codec);
            }

            avformat_close_input(&s);
            av_free(pb);
        }
        c->buffer_ptr = c->buffer;
    }

    return 0;
 fail:
    c->stream->feed_opened = 0;
    close(c->feed_fd);
    /* wake up any waiting connections to stop waiting for feed */
    for(c1 = first_http_ctx; c1 != NULL; c1 = c1->next) {
        if (c1->state == HTTPSTATE_WAIT_FEED &&
            c1->stream->feed == c->stream->feed)
            c1->state = HTTPSTATE_SEND_DATA_TRAILER;
    }
    return -1;
}

/********************************************************************/
/* RTSP handling */

static void rtsp_reply_header(HTTPContext *c, enum RTSPStatusCode error_number)
{
    const char *str;
    time_t ti;
    struct tm *tm;
    char buf2[32];

    switch(error_number) {
    case RTSP_STATUS_OK:
        str = "OK";
        break;
    case RTSP_STATUS_METHOD:
        str = "Method Not Allowed";
        break;
    case RTSP_STATUS_BANDWIDTH:
        str = "Not Enough Bandwidth";
        break;
    case RTSP_STATUS_SESSION:
        str = "Session Not Found";
        break;
    case RTSP_STATUS_STATE:
        str = "Method Not Valid in This State";
        break;
    case RTSP_STATUS_AGGREGATE:
        str = "Aggregate operation not allowed";
        break;
    case RTSP_STATUS_ONLY_AGGREGATE:
        str = "Only aggregate operation allowed";
        break;
    case RTSP_STATUS_TRANSPORT:
        str = "Unsupported transport";
        break;
    case RTSP_STATUS_INTERNAL:
        str = "Internal Server Error";
        break;
    case RTSP_STATUS_SERVICE:
        str = "Service Unavailable";
        break;
    case RTSP_STATUS_VERSION:
        str = "RTSP Version not supported";
        break;
    default:
        str = "Unknown Error";
        break;
    }

    avio_printf(c->pb, "RTSP/1.0 %d %s\r\n", error_number, str);
    avio_printf(c->pb, "CSeq: %d\r\n", c->seq);

    /* output GMT time */
    ti = time(NULL);
    tm = gmtime(&ti);
    strftime(buf2, sizeof(buf2), "%a, %d %b %Y %H:%M:%S", tm);
    avio_printf(c->pb, "Date: %s GMT\r\n", buf2);
}

static void rtsp_reply_error(HTTPContext *c, enum RTSPStatusCode error_number)
{
    rtsp_reply_header(c, error_number);
    avio_printf(c->pb, "\r\n");
}

static int rtsp_parse_request(HTTPContext *c)
{
    const char *p, *p1, *p2;
    char cmd[32];
    char url[1024];
    char protocol[32];
    char line[1024];
    int len;
    RTSPMessageHeader header1 = { 0 }, *header = &header1;

    c->buffer_ptr[0] = '\0';
    p = c->buffer;

    get_word(cmd, sizeof(cmd), &p);
    get_word(url, sizeof(url), &p);
    get_word(protocol, sizeof(protocol), &p);

    av_strlcpy(c->method, cmd, sizeof(c->method));
    av_strlcpy(c->url, url, sizeof(c->url));
    av_strlcpy(c->protocol, protocol, sizeof(c->protocol));

    if (avio_open_dyn_buf(&c->pb) < 0) {
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
    /* skip to next line */
    while (*p != '\n' && *p != '\0')
        p++;
    if (*p == '\n')
        p++;
    while (*p != '\0') {
        p1 = memchr(p, '\n', (char *)c->buffer_ptr - p);
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
        ff_rtsp_parse_line(header, line, NULL, NULL);
        p = p1 + 1;
    }

    /* handle sequence number */
    c->seq = header->seq;

    if (!strcmp(cmd, "DESCRIBE"))
        rtsp_cmd_describe(c, url);
    else if (!strcmp(cmd, "OPTIONS"))
        rtsp_cmd_options(c, url);
    else if (!strcmp(cmd, "SETUP"))
        rtsp_cmd_setup(c, url, header);
    else if (!strcmp(cmd, "PLAY"))
        rtsp_cmd_play(c, url, header);
    else if (!strcmp(cmd, "PAUSE"))
        rtsp_cmd_pause(c, url, header);
    else if (!strcmp(cmd, "TEARDOWN"))
        rtsp_cmd_teardown(c, url, header);
    else
        rtsp_reply_error(c, RTSP_STATUS_METHOD);

 the_end:
    len = avio_close_dyn_buf(c->pb, &c->pb_buffer);
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

static int prepare_sdp_description(FFStream *stream, uint8_t **pbuffer,
                                   struct in_addr my_ip)
{
    AVFormatContext *avc;
    AVStream *avs = NULL;
    int i;

    avc =  avformat_alloc_context();
    if (avc == NULL) {
        return -1;
    }
    av_dict_set(&avc->metadata, "title",
               stream->title[0] ? stream->title : "No Title", 0);
    avc->nb_streams = stream->nb_streams;
    if (stream->is_multicast) {
        snprintf(avc->filename, 1024, "rtp://%s:%d?multicast=1?ttl=%d",
                 inet_ntoa(stream->multicast_ip),
                 stream->multicast_port, stream->multicast_ttl);
    } else {
        snprintf(avc->filename, 1024, "rtp://0.0.0.0");
    }

    if (avc->nb_streams >= INT_MAX/sizeof(*avc->streams) ||
        !(avc->streams = av_malloc(avc->nb_streams * sizeof(*avc->streams))))
        goto sdp_done;
    if (avc->nb_streams >= INT_MAX/sizeof(*avs) ||
        !(avs = av_malloc(avc->nb_streams * sizeof(*avs))))
        goto sdp_done;

    for(i = 0; i < stream->nb_streams; i++) {
        avc->streams[i] = &avs[i];
        avc->streams[i]->codec = stream->streams[i]->codec;
    }
    *pbuffer = av_mallocz(2048);
    av_sdp_create(&avc, 1, *pbuffer, 2048);

 sdp_done:
    av_free(avc->streams);
    av_dict_free(&avc->metadata);
    av_free(avc);
    av_free(avs);

    return strlen(*pbuffer);
}

static void rtsp_cmd_options(HTTPContext *c, const char *url)
{
//    rtsp_reply_header(c, RTSP_STATUS_OK);
    avio_printf(c->pb, "RTSP/1.0 %d %s\r\n", RTSP_STATUS_OK, "OK");
    avio_printf(c->pb, "CSeq: %d\r\n", c->seq);
    avio_printf(c->pb, "Public: %s\r\n", "OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE");
    avio_printf(c->pb, "\r\n");
}

static void rtsp_cmd_describe(HTTPContext *c, const char *url)
{
    FFStream *stream;
    char path1[1024];
    const char *path;
    uint8_t *content;
    int content_length;
    socklen_t len;
    struct sockaddr_in my_addr;

    /* find which url is asked */
    av_url_split(NULL, 0, NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
    path = path1;
    if (*path == '/')
        path++;

    for(stream = first_stream; stream != NULL; stream = stream->next) {
        if (!stream->is_feed &&
            stream->fmt && !strcmp(stream->fmt->name, "rtp") &&
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
    avio_printf(c->pb, "Content-Base: %s/\r\n", url);
    avio_printf(c->pb, "Content-Type: application/sdp\r\n");
    avio_printf(c->pb, "Content-Length: %d\r\n", content_length);
    avio_printf(c->pb, "\r\n");
    avio_write(c->pb, content, content_length);
    av_free(content);
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

static RTSPTransportField *find_transport(RTSPMessageHeader *h, enum RTSPLowerTransport lower_transport)
{
    RTSPTransportField *th;
    int i;

    for(i=0;i<h->nb_transports;i++) {
        th = &h->transports[i];
        if (th->lower_transport == lower_transport)
            return th;
    }
    return NULL;
}

static void rtsp_cmd_setup(HTTPContext *c, const char *url,
                           RTSPMessageHeader *h)
{
    FFStream *stream;
    int stream_index, rtp_port, rtcp_port;
    char buf[1024];
    char path1[1024];
    const char *path;
    HTTPContext *rtp_c;
    RTSPTransportField *th;
    struct sockaddr_in dest_addr;
    RTSPActionServerSetup setup;

    /* find which url is asked */
    av_url_split(NULL, 0, NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
    path = path1;
    if (*path == '/')
        path++;

    /* now check each stream */
    for(stream = first_stream; stream != NULL; stream = stream->next) {
        if (!stream->is_feed &&
            stream->fmt && !strcmp(stream->fmt->name, "rtp")) {
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
    if (h->session_id[0] == '\0')
        snprintf(h->session_id, sizeof(h->session_id), "%08x%08x",
                 av_lfg_get(&random_state), av_lfg_get(&random_state));

    /* find rtp session, and create it if none found */
    rtp_c = find_rtp_session(h->session_id);
    if (!rtp_c) {
        /* always prefer UDP */
        th = find_transport(h, RTSP_LOWER_TRANSPORT_UDP);
        if (!th) {
            th = find_transport(h, RTSP_LOWER_TRANSPORT_TCP);
            if (!th) {
                rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
                return;
            }
        }

        rtp_c = rtp_new_connection(&c->from_addr, stream, h->session_id,
                                   th->lower_transport);
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
    if (!th || (th->lower_transport == RTSP_LOWER_TRANSPORT_UDP &&
                th->client_port_min <= 0)) {
        rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
        return;
    }

    /* setup default options */
    setup.transport_option[0] = '\0';
    dest_addr = rtp_c->from_addr;
    dest_addr.sin_port = htons(th->client_port_min);

    /* setup stream */
    if (rtp_new_av_stream(rtp_c, stream_index, &dest_addr, c) < 0) {
        rtsp_reply_error(c, RTSP_STATUS_TRANSPORT);
        return;
    }

    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    avio_printf(c->pb, "Session: %s\r\n", rtp_c->session_id);

    switch(rtp_c->rtp_protocol) {
    case RTSP_LOWER_TRANSPORT_UDP:
        rtp_port = ff_rtp_get_local_rtp_port(rtp_c->rtp_handles[stream_index]);
        rtcp_port = ff_rtp_get_local_rtcp_port(rtp_c->rtp_handles[stream_index]);
        avio_printf(c->pb, "Transport: RTP/AVP/UDP;unicast;"
                    "client_port=%d-%d;server_port=%d-%d",
                    th->client_port_min, th->client_port_max,
                    rtp_port, rtcp_port);
        break;
    case RTSP_LOWER_TRANSPORT_TCP:
        avio_printf(c->pb, "Transport: RTP/AVP/TCP;interleaved=%d-%d",
                    stream_index * 2, stream_index * 2 + 1);
        break;
    default:
        break;
    }
    if (setup.transport_option[0] != '\0')
        avio_printf(c->pb, ";%s", setup.transport_option);
    avio_printf(c->pb, "\r\n");


    avio_printf(c->pb, "\r\n");
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
    int s, len;

    rtp_c = find_rtp_session(session_id);
    if (!rtp_c)
        return NULL;

    /* find which url is asked */
    av_url_split(NULL, 0, NULL, 0, NULL, 0, NULL, path1, sizeof(path1), url);
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
    len = strlen(path);
    if (len > 0 && path[len - 1] == '/' &&
        !strncmp(path, rtp_c->stream->filename, len - 1))
        return rtp_c;
    return NULL;
}

static void rtsp_cmd_play(HTTPContext *c, const char *url, RTSPMessageHeader *h)
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

    rtp_c->state = HTTPSTATE_SEND_DATA;

    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    avio_printf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    avio_printf(c->pb, "\r\n");
}

static void rtsp_cmd_pause(HTTPContext *c, const char *url, RTSPMessageHeader *h)
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
    avio_printf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    avio_printf(c->pb, "\r\n");
}

static void rtsp_cmd_teardown(HTTPContext *c, const char *url, RTSPMessageHeader *h)
{
    HTTPContext *rtp_c;

    rtp_c = find_rtp_session_with_url(url, h->session_id);
    if (!rtp_c) {
        rtsp_reply_error(c, RTSP_STATUS_SESSION);
        return;
    }

    /* now everything is OK, so we can send the connection parameters */
    rtsp_reply_header(c, RTSP_STATUS_OK);
    /* session ID */
    avio_printf(c->pb, "Session: %s\r\n", rtp_c->session_id);
    avio_printf(c->pb, "\r\n");

    /* abort the session */
    close_connection(rtp_c);
}


/********************************************************************/
/* RTP handling */

static HTTPContext *rtp_new_connection(struct sockaddr_in *from_addr,
                                       FFStream *stream, const char *session_id,
                                       enum RTSPLowerTransport rtp_protocol)
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
    av_strlcpy(c->session_id, session_id, sizeof(c->session_id));
    c->state = HTTPSTATE_READY;
    c->is_packetized = 1;
    c->rtp_protocol = rtp_protocol;

    /* protocol is shown in statistics */
    switch(c->rtp_protocol) {
    case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
        proto_str = "MCAST";
        break;
    case RTSP_LOWER_TRANSPORT_UDP:
        proto_str = "UDP";
        break;
    case RTSP_LOWER_TRANSPORT_TCP:
        proto_str = "TCP";
        break;
    default:
        proto_str = "???";
        break;
    }
    av_strlcpy(c->protocol, "RTP/", sizeof(c->protocol));
    av_strlcat(c->protocol, proto_str, sizeof(c->protocol));

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
    URLContext *h = NULL;
    uint8_t *dummy_buf;
    int max_packet_size;

    /* now we can open the relevant output stream */
    ctx = avformat_alloc_context();
    if (!ctx)
        return -1;
    ctx->oformat = av_guess_format("rtp", NULL, NULL);

    st = av_mallocz(sizeof(AVStream));
    if (!st)
        goto fail;
    ctx->nb_streams = 1;
    ctx->streams = av_mallocz(sizeof(AVStream *) * ctx->nb_streams);
    if (!ctx->streams)
      goto fail;
    ctx->streams[0] = st;

    if (!c->stream->feed ||
        c->stream->feed == c->stream)
        memcpy(st, c->stream->streams[stream_index], sizeof(AVStream));
    else
        memcpy(st,
               c->stream->feed->streams[c->stream->feed_streams[stream_index]],
               sizeof(AVStream));
    st->priv_data = NULL;

    /* build destination RTP address */
    ipaddr = inet_ntoa(dest_addr->sin_addr);

    switch(c->rtp_protocol) {
    case RTSP_LOWER_TRANSPORT_UDP:
    case RTSP_LOWER_TRANSPORT_UDP_MULTICAST:
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

        if (ffurl_open(&h, ctx->filename, AVIO_FLAG_WRITE, NULL, NULL) < 0)
            goto fail;
        c->rtp_handles[stream_index] = h;
        max_packet_size = h->max_packet_size;
        break;
    case RTSP_LOWER_TRANSPORT_TCP:
        /* RTP/TCP case */
        c->rtsp_c = rtsp_c;
        max_packet_size = RTSP_TCP_MAX_PACKET_SIZE;
        break;
    default:
        goto fail;
    }

    http_log("%s:%d - - \"PLAY %s/streamid=%d %s\"\n",
             ipaddr, ntohs(dest_addr->sin_port),
             c->stream->filename, stream_index, c->protocol);

    /* normally, no packets should be output here, but the packet size may be checked */
    if (ffio_open_dyn_packet_buf(&ctx->pb, max_packet_size) < 0) {
        /* XXX: close stream */
        goto fail;
    }
    if (avformat_write_header(ctx, NULL) < 0) {
    fail:
        if (h)
            ffurl_close(h);
        av_free(ctx);
        return -1;
    }
    avio_close_dyn_buf(ctx->pb, &dummy_buf);
    av_free(dummy_buf);

    c->rtp_ctx[stream_index] = ctx;
    return 0;
}

/********************************************************************/
/* avserver initialization */

static AVStream *add_av_stream1(FFStream *stream, AVCodecContext *codec, int copy)
{
    AVStream *fst;

    fst = av_mallocz(sizeof(AVStream));
    if (!fst)
        return NULL;
    if (copy) {
        fst->codec = avcodec_alloc_context3(NULL);
        memcpy(fst->codec, codec, sizeof(AVCodecContext));
        if (codec->extradata_size) {
            fst->codec->extradata = av_malloc(codec->extradata_size);
            memcpy(fst->codec->extradata, codec->extradata,
                codec->extradata_size);
        }
    } else {
        /* live streams must use the actual feed's codec since it may be
         * updated later to carry extradata needed by the streams.
         */
        fst->codec = codec;
    }
    fst->priv_data = av_mallocz(sizeof(FeedData));
    fst->index = stream->nb_streams;
    avpriv_set_pts_info(fst, 33, 1, 90000);
    fst->sample_aspect_ratio = codec->sample_aspect_ratio;
    stream->streams[stream->nb_streams++] = fst;
    return fst;
}

/* return the stream number in the feed */
static int add_av_stream(FFStream *feed, AVStream *st)
{
    AVStream *fst;
    AVCodecContext *av, *av1;
    int i;

    av = st->codec;
    for(i=0;i<feed->nb_streams;i++) {
        st = feed->streams[i];
        av1 = st->codec;
        if (av1->codec_id == av->codec_id &&
            av1->codec_type == av->codec_type &&
            av1->bit_rate == av->bit_rate) {

            switch(av->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
                if (av1->channels == av->channels &&
                    av1->sample_rate == av->sample_rate)
                    return i;
                break;
            case AVMEDIA_TYPE_VIDEO:
                if (av1->width == av->width &&
                    av1->height == av->height &&
                    av1->time_base.den == av->time_base.den &&
                    av1->time_base.num == av->time_base.num &&
                    av1->gop_size == av->gop_size)
                    return i;
                break;
            default:
                abort();
            }
        }
    }

    fst = add_av_stream1(feed, av, 0);
    if (!fst)
        return -1;
    return feed->nb_streams - 1;
}

static void remove_stream(FFStream *stream)
{
    FFStream **ps;
    ps = &first_stream;
    while (*ps != NULL) {
        if (*ps == stream)
            *ps = (*ps)->next;
        else
            ps = &(*ps)->next;
    }
}

/* specific mpeg4 handling : we extract the raw parameters */
static void extract_mpeg4_header(AVFormatContext *infile)
{
    int mpeg4_count, i, size;
    AVPacket pkt;
    AVStream *st;
    const uint8_t *p;

    infile->flags |= AVFMT_FLAG_NOFILLIN | AVFMT_FLAG_NOPARSE;

    mpeg4_count = 0;
    for(i=0;i<infile->nb_streams;i++) {
        st = infile->streams[i];
        if (st->codec->codec_id == AV_CODEC_ID_MPEG4 &&
            st->codec->extradata_size == 0) {
            mpeg4_count++;
        }
    }
    if (!mpeg4_count)
        return;

    printf("MPEG4 without extra data: trying to find header in %s\n", infile->filename);
    while (mpeg4_count > 0) {
        if (av_read_frame(infile, &pkt) < 0)
            break;
        st = infile->streams[pkt.stream_index];
        if (st->codec->codec_id == AV_CODEC_ID_MPEG4 &&
            st->codec->extradata_size == 0) {
            av_freep(&st->codec->extradata);
            /* fill extradata with the header */
            /* XXX: we make hard suppositions here ! */
            p = pkt.data;
            while (p < pkt.data + pkt.size - 4) {
                /* stop when vop header is found */
                if (p[0] == 0x00 && p[1] == 0x00 &&
                    p[2] == 0x01 && p[3] == 0xb6) {
                    size = p - pkt.data;
                    //                    av_hex_dump_log(infile, AV_LOG_DEBUG, pkt.data, size);
                    st->codec->extradata = av_malloc(size);
                    st->codec->extradata_size = size;
                    memcpy(st->codec->extradata, pkt.data, size);
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
    int i, ret;

    /* gather all streams */
    for(stream = first_stream; stream != NULL; stream = stream_next) {
        AVFormatContext *infile = NULL;
        stream_next = stream->next;
        if (stream->stream_type == STREAM_TYPE_LIVE &&
            !stream->feed) {
            /* the stream comes from a file */
            /* try to open the file */
            /* open stream */
            if (stream->fmt && !strcmp(stream->fmt->name, "rtp")) {
                /* specific case : if transport stream output to RTP,
                   we use a raw transport stream reader */
                av_dict_set(&stream->in_opts, "mpeg2ts_compute_pcr", "1", 0);
            }

            http_log("Opening file '%s'\n", stream->feed_filename);
            if ((ret = avformat_open_input(&infile, stream->feed_filename, stream->ifmt, &stream->in_opts)) < 0) {
                http_log("Could not open '%s': %d\n", stream->feed_filename, ret);
                /* remove stream (no need to spend more time on it) */
            fail:
                remove_stream(stream);
            } else {
                /* find all the AVStreams inside and reference them in
                   'stream' */
                if (avformat_find_stream_info(infile, NULL) < 0) {
                    http_log("Could not find codec parameters from '%s'\n",
                             stream->feed_filename);
                    avformat_close_input(&infile);
                    goto fail;
                }
                extract_mpeg4_header(infile);

                for(i=0;i<infile->nb_streams;i++)
                    add_av_stream1(stream, infile->streams[i]->codec, 1);

                avformat_close_input(&infile);
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
            if (stream->is_feed) {
                for(i=0;i<stream->nb_streams;i++)
                    stream->feed_streams[i] = i;
            } else {
                /* we handle a stream coming from a feed */
                for(i=0;i<stream->nb_streams;i++)
                    stream->feed_streams[i] = add_av_stream(feed, stream->streams[i]);
            }
        }
    }

    /* create feed files if needed */
    for(feed = first_feed; feed != NULL; feed = feed->next_feed) {
        int fd;

        if (avio_check(feed->feed_filename, AVIO_FLAG_READ) > 0) {
            /* See if it matches */
            AVFormatContext *s = NULL;
            int matches = 0;

            if (avformat_open_input(&s, feed->feed_filename, NULL, NULL) >= 0) {
                /* Now see if it matches */
                if (s->nb_streams == feed->nb_streams) {
                    matches = 1;
                    for(i=0;i<s->nb_streams;i++) {
                        AVStream *sf, *ss;
                        sf = feed->streams[i];
                        ss = s->streams[i];

                        if (sf->index != ss->index ||
                            sf->id != ss->id) {
                            http_log("Index & Id do not match for stream %d (%s)\n",
                                   i, feed->feed_filename);
                            matches = 0;
                        } else {
                            AVCodecContext *ccf, *ccs;

                            ccf = sf->codec;
                            ccs = ss->codec;
#define CHECK_CODEC(x)  (ccf->x != ccs->x)

                            if (CHECK_CODEC(codec_id) || CHECK_CODEC(codec_type)) {
                                http_log("Codecs do not match for stream %d\n", i);
                                matches = 0;
                            } else if (CHECK_CODEC(bit_rate) || CHECK_CODEC(flags)) {
                                http_log("Codec bitrates do not match for stream %d\n", i);
                                matches = 0;
                            } else if (ccf->codec_type == AVMEDIA_TYPE_VIDEO) {
                                if (CHECK_CODEC(time_base.den) ||
                                    CHECK_CODEC(time_base.num) ||
                                    CHECK_CODEC(width) ||
                                    CHECK_CODEC(height)) {
                                    http_log("Codec width, height and framerate do not match for stream %d\n", i);
                                    matches = 0;
                                }
                            } else if (ccf->codec_type == AVMEDIA_TYPE_AUDIO) {
                                if (CHECK_CODEC(sample_rate) ||
                                    CHECK_CODEC(channels) ||
                                    CHECK_CODEC(frame_size)) {
                                    http_log("Codec sample_rate, channels, frame_size do not match for stream %d\n", i);
                                    matches = 0;
                                }
                            } else {
                                http_log("Unknown codec type\n");
                                matches = 0;
                            }
                        }
                        if (!matches)
                            break;
                    }
                } else
                    http_log("Deleting feed file '%s' as stream counts differ (%d != %d)\n",
                        feed->feed_filename, s->nb_streams, feed->nb_streams);

                avformat_close_input(&s);
            } else
                http_log("Deleting feed file '%s' as it appears to be corrupt\n",
                        feed->feed_filename);

            if (!matches) {
                if (feed->readonly) {
                    http_log("Unable to delete feed file '%s' as it is marked readonly\n",
                        feed->feed_filename);
                    exit(1);
                }
                unlink(feed->feed_filename);
            }
        }
        if (avio_check(feed->feed_filename, AVIO_FLAG_WRITE) <= 0) {
            AVFormatContext s1 = {0}, *s = &s1;

            if (feed->readonly) {
                http_log("Unable to create feed file '%s' as it is marked readonly\n",
                    feed->feed_filename);
                exit(1);
            }

            /* only write the header of the ffm file */
            if (avio_open(&s->pb, feed->feed_filename, AVIO_FLAG_WRITE) < 0) {
                http_log("Could not open output feed file '%s'\n",
                         feed->feed_filename);
                exit(1);
            }
            s->oformat = feed->fmt;
            s->nb_streams = feed->nb_streams;
            s->streams = feed->streams;
            if (avformat_write_header(s, NULL) < 0) {
                http_log("Container doesn't supports the required parameters\n");
                exit(1);
            }
            /* XXX: need better api */
            av_freep(&s->priv_data);
            avio_close(s->pb);
        }
        /* get feed size and write index */
        fd = open(feed->feed_filename, O_RDONLY);
        if (fd < 0) {
            http_log("Could not open output feed file '%s'\n",
                    feed->feed_filename);
            exit(1);
        }

        feed->feed_write_index = FFMAX(ffm_read_write_index(fd), FFM_PACKET_SIZE);
        feed->feed_size = lseek(fd, 0, SEEK_END);
        /* ensure that we do not wrap before the end of file */
        if (feed->feed_max_size && feed->feed_max_size < feed->feed_size)
            feed->feed_max_size = feed->feed_size;

        close(fd);
    }
}

/* compute the bandwidth used by each stream */
static void compute_bandwidth(void)
{
    unsigned bandwidth;
    int i;
    FFStream *stream;

    for(stream = first_stream; stream != NULL; stream = stream->next) {
        bandwidth = 0;
        for(i=0;i<stream->nb_streams;i++) {
            AVStream *st = stream->streams[i];
            switch(st->codec->codec_type) {
            case AVMEDIA_TYPE_AUDIO:
            case AVMEDIA_TYPE_VIDEO:
                bandwidth += st->codec->bit_rate;
                break;
            default:
                break;
            }
        }
        stream->bandwidth = (bandwidth + 999) / 1000;
    }
}

/* add a codec and set the default parameters */
static void add_codec(FFStream *stream, AVCodecContext *av)
{
    AVStream *st;

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
            av->rc_eq = "tex^qComp";
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

static enum AVCodecID opt_audio_codec(const char *arg)
{
    AVCodec *p= avcodec_find_encoder_by_name(arg);

    if (p == NULL || p->type != AVMEDIA_TYPE_AUDIO)
        return AV_CODEC_ID_NONE;

    return p->id;
}

static enum AVCodecID opt_video_codec(const char *arg)
{
    AVCodec *p= avcodec_find_encoder_by_name(arg);

    if (p == NULL || p->type != AVMEDIA_TYPE_VIDEO)
        return AV_CODEC_ID_NONE;

    return p->id;
}

static int avserver_opt_default(const char *opt, const char *arg,
                       AVCodecContext *avctx, int type)
{
    int ret = 0;
    const AVOption *o = av_opt_find(avctx, opt, NULL, type, 0);
    if(o)
        ret = av_opt_set(avctx, opt, arg, 0);
    return ret;
}

static int avserver_opt_preset(const char *arg,
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
            *audio_id = opt_audio_codec(tmp2);
        }else if(!strcmp(tmp, "vcodec")){
            *video_id = opt_video_codec(tmp2);
        }else if(!strcmp(tmp, "scodec")){
            /* opt_subtitle_codec(tmp2); */
        }else if(avserver_opt_default(tmp, tmp2, avctx, type) < 0){
            fprintf(stderr, "%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n", filename, line, tmp, tmp2);
            ret = 1;
            break;
        }
    }

    fclose(f);

    return ret;
}

static AVOutputFormat *avserver_guess_format(const char *short_name, const char *filename,
                                             const char *mime_type)
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

static void report_config_error(const char *filename, int line_num, int *errors, const char *fmt, ...)
{
    va_list vl;
    va_start(vl, fmt);
    fprintf(stderr, "%s:%d: ", filename, line_num);
    vfprintf(stderr, fmt, vl);
    va_end(vl);

    (*errors)++;
}

static int parse_ffconfig(const char *filename)
{
    FILE *f;
    char line[1024];
    char cmd[64];
    char arg[1024];
    const char *p;
    int val, errors, line_num;
    FFStream **last_stream, *stream, *redirect;
    FFStream **last_feed, *feed, *s;
    AVCodecContext audio_enc, video_enc;
    enum AVCodecID audio_id, video_id;

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
    audio_id = AV_CODEC_ID_NONE;
    video_id = AV_CODEC_ID_NONE;

#define ERROR(...) report_config_error(filename, line_num, &errors, __VA_ARGS__)
    for(;;) {
        if (fgets(line, sizeof(line), f) == NULL)
            break;
        line_num++;
        p = line;
        while (av_isspace(*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        get_arg(cmd, sizeof(cmd), &p);

        if (!av_strcasecmp(cmd, "Port")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 1 || val > 65536) {
                ERROR("Invalid_port: %s\n", arg);
            }
            my_http_addr.sin_port = htons(val);
        } else if (!av_strcasecmp(cmd, "BindAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (resolve_host(&my_http_addr.sin_addr, arg) != 0) {
                ERROR("%s:%d: Invalid host/IP address: %s\n", arg);
            }
        } else if (!av_strcasecmp(cmd, "RTSPPort")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 1 || val > 65536) {
                ERROR("%s:%d: Invalid port: %s\n", arg);
            }
            my_rtsp_addr.sin_port = htons(atoi(arg));
        } else if (!av_strcasecmp(cmd, "RTSPBindAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (resolve_host(&my_rtsp_addr.sin_addr, arg) != 0) {
                ERROR("Invalid host/IP address: %s\n", arg);
            }
        } else if (!av_strcasecmp(cmd, "MaxHTTPConnections")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 1 || val > 65536) {
                ERROR("Invalid MaxHTTPConnections: %s\n", arg);
            }
            nb_max_http_connections = val;
        } else if (!av_strcasecmp(cmd, "MaxClients")) {
            get_arg(arg, sizeof(arg), &p);
            val = atoi(arg);
            if (val < 1 || val > nb_max_http_connections) {
                ERROR("Invalid MaxClients: %s\n", arg);
            } else {
                nb_max_connections = val;
            }
        } else if (!av_strcasecmp(cmd, "MaxBandwidth")) {
            int64_t llval;
            get_arg(arg, sizeof(arg), &p);
            llval = atoll(arg);
            if (llval < 10 || llval > 10000000) {
                ERROR("Invalid MaxBandwidth: %s\n", arg);
            } else
                max_bandwidth = llval;
        } else if (!av_strcasecmp(cmd, "CustomLog")) {
            if (!avserver_debug)
                get_arg(logfilename, sizeof(logfilename), &p);
        } else if (!av_strcasecmp(cmd, "<Feed")) {
            /*********************************************/
            /* Feed related options */
            char *q;
            if (stream || feed) {
                ERROR("Already in a tag\n");
            } else {
                feed = av_mallocz(sizeof(FFStream));
                get_arg(feed->filename, sizeof(feed->filename), &p);
                q = strrchr(feed->filename, '>');
                if (*q)
                    *q = '\0';

                for (s = first_feed; s; s = s->next) {
                    if (!strcmp(feed->filename, s->filename)) {
                        ERROR("Feed '%s' already registered\n", s->filename);
                    }
                }

                feed->fmt = av_guess_format("ffm", NULL, NULL);
                /* defaut feed file */
                snprintf(feed->feed_filename, sizeof(feed->feed_filename),
                         "/tmp/%s.ffm", feed->filename);
                feed->feed_max_size = 5 * 1024 * 1024;
                feed->is_feed = 1;
                feed->feed = feed; /* self feeding :-) */

                /* add in stream list */
                *last_stream = feed;
                last_stream = &feed->next;
                /* add in feed list */
                *last_feed = feed;
                last_feed = &feed->next_feed;
            }
        } else if (!av_strcasecmp(cmd, "Launch")) {
            if (feed) {
                int i;

                feed->child_argv = av_mallocz(64 * sizeof(char *));

                for (i = 0; i < 62; i++) {
                    get_arg(arg, sizeof(arg), &p);
                    if (!arg[0])
                        break;

                    feed->child_argv[i] = av_strdup(arg);
                }

                feed->child_argv[i] = av_malloc(30 + strlen(feed->filename));

                snprintf(feed->child_argv[i], 30+strlen(feed->filename),
                    "http://%s:%d/%s",
                        (my_http_addr.sin_addr.s_addr == INADDR_ANY) ? "127.0.0.1" :
                    inet_ntoa(my_http_addr.sin_addr),
                    ntohs(my_http_addr.sin_port), feed->filename);
            }
        } else if (!av_strcasecmp(cmd, "ReadOnlyFile")) {
            if (feed) {
                get_arg(feed->feed_filename, sizeof(feed->feed_filename), &p);
                feed->readonly = 1;
            } else if (stream) {
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
            }
        } else if (!av_strcasecmp(cmd, "File")) {
            if (feed) {
                get_arg(feed->feed_filename, sizeof(feed->feed_filename), &p);
            } else if (stream)
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
        } else if (!av_strcasecmp(cmd, "Truncate")) {
            if (feed) {
                get_arg(arg, sizeof(arg), &p);
                feed->truncate = strtod(arg, NULL);
            }
        } else if (!av_strcasecmp(cmd, "FileMaxSize")) {
            if (feed) {
                char *p1;
                double fsize;

                get_arg(arg, sizeof(arg), &p);
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
                if (feed->feed_max_size < FFM_PACKET_SIZE*4) {
                    ERROR("Feed max file size is too small, must be at least %d\n", FFM_PACKET_SIZE*4);
                }
            }
        } else if (!av_strcasecmp(cmd, "</Feed>")) {
            if (!feed) {
                ERROR("No corresponding <Feed> for </Feed>\n");
            }
            feed = NULL;
        } else if (!av_strcasecmp(cmd, "<Stream")) {
            /*********************************************/
            /* Stream related options */
            char *q;
            if (stream || feed) {
                ERROR("Already in a tag\n");
            } else {
                FFStream *s;
                stream = av_mallocz(sizeof(FFStream));
                get_arg(stream->filename, sizeof(stream->filename), &p);
                q = strrchr(stream->filename, '>');
                if (*q)
                    *q = '\0';

                for (s = first_stream; s; s = s->next) {
                    if (!strcmp(stream->filename, s->filename)) {
                        ERROR("Stream '%s' already registered\n", s->filename);
                    }
                }

                stream->fmt = avserver_guess_format(NULL, stream->filename, NULL);
                avcodec_get_context_defaults3(&video_enc, NULL);
                avcodec_get_context_defaults3(&audio_enc, NULL);
                audio_id = AV_CODEC_ID_NONE;
                video_id = AV_CODEC_ID_NONE;
                if (stream->fmt) {
                    audio_id = stream->fmt->audio_codec;
                    video_id = stream->fmt->video_codec;
                }

                *last_stream = stream;
                last_stream = &stream->next;
            }
        } else if (!av_strcasecmp(cmd, "Feed")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                FFStream *sfeed;

                sfeed = first_feed;
                while (sfeed != NULL) {
                    if (!strcmp(sfeed->filename, arg))
                        break;
                    sfeed = sfeed->next_feed;
                }
                if (!sfeed)
                    ERROR("feed '%s' not defined\n", arg);
                else
                    stream->feed = sfeed;
            }
        } else if (!av_strcasecmp(cmd, "Format")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                if (!strcmp(arg, "status")) {
                    stream->stream_type = STREAM_TYPE_STATUS;
                    stream->fmt = NULL;
                } else {
                    stream->stream_type = STREAM_TYPE_LIVE;
                    /* jpeg cannot be used here, so use single frame jpeg */
                    if (!strcmp(arg, "jpeg"))
                        strcpy(arg, "mjpeg");
                    stream->fmt = avserver_guess_format(arg, NULL, NULL);
                    if (!stream->fmt) {
                        ERROR("Unknown Format: %s\n", arg);
                    }
                }
                if (stream->fmt) {
                    audio_id = stream->fmt->audio_codec;
                    video_id = stream->fmt->video_codec;
                }
            }
        } else if (!av_strcasecmp(cmd, "InputFormat")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                stream->ifmt = av_find_input_format(arg);
                if (!stream->ifmt) {
                    ERROR("Unknown input format: %s\n", arg);
                }
            }
        } else if (!av_strcasecmp(cmd, "FaviconURL")) {
            if (stream && stream->stream_type == STREAM_TYPE_STATUS) {
                get_arg(stream->feed_filename, sizeof(stream->feed_filename), &p);
            } else {
                ERROR("FaviconURL only permitted for status streams\n");
            }
        } else if (!av_strcasecmp(cmd, "Author")) {
            if (stream)
                get_arg(stream->author, sizeof(stream->author), &p);
        } else if (!av_strcasecmp(cmd, "Comment")) {
            if (stream)
                get_arg(stream->comment, sizeof(stream->comment), &p);
        } else if (!av_strcasecmp(cmd, "Copyright")) {
            if (stream)
                get_arg(stream->copyright, sizeof(stream->copyright), &p);
        } else if (!av_strcasecmp(cmd, "Title")) {
            if (stream)
                get_arg(stream->title, sizeof(stream->title), &p);
        } else if (!av_strcasecmp(cmd, "Preroll")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                stream->prebuffer = atof(arg) * 1000;
        } else if (!av_strcasecmp(cmd, "StartSendOnKey")) {
            if (stream)
                stream->send_on_key = 1;
        } else if (!av_strcasecmp(cmd, "AudioCodec")) {
            get_arg(arg, sizeof(arg), &p);
            audio_id = opt_audio_codec(arg);
            if (audio_id == AV_CODEC_ID_NONE) {
                ERROR("Unknown AudioCodec: %s\n", arg);
            }
        } else if (!av_strcasecmp(cmd, "VideoCodec")) {
            get_arg(arg, sizeof(arg), &p);
            video_id = opt_video_codec(arg);
            if (video_id == AV_CODEC_ID_NONE) {
                ERROR("Unknown VideoCodec: %s\n", arg);
            }
        } else if (!av_strcasecmp(cmd, "MaxTime")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                stream->max_time = atof(arg) * 1000;
        } else if (!av_strcasecmp(cmd, "AudioBitRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                audio_enc.bit_rate = lrintf(atof(arg) * 1000);
        } else if (!av_strcasecmp(cmd, "AudioChannels")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                audio_enc.channels = atoi(arg);
        } else if (!av_strcasecmp(cmd, "AudioSampleRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                audio_enc.sample_rate = atoi(arg);
        } else if (!av_strcasecmp(cmd, "AudioQuality")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
//                audio_enc.quality = atof(arg) * 1000;
            }
        } else if (!av_strcasecmp(cmd, "VideoBitRateRange")) {
            if (stream) {
                int minrate, maxrate;

                get_arg(arg, sizeof(arg), &p);

                if (sscanf(arg, "%d-%d", &minrate, &maxrate) == 2) {
                    video_enc.rc_min_rate = minrate * 1000;
                    video_enc.rc_max_rate = maxrate * 1000;
                } else {
                    ERROR("Incorrect format for VideoBitRateRange -- should be <min>-<max>: %s\n", arg);
                }
            }
        } else if (!av_strcasecmp(cmd, "Debug")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.debug = strtol(arg,0,0);
            }
        } else if (!av_strcasecmp(cmd, "Strict")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.strict_std_compliance = atoi(arg);
            }
        } else if (!av_strcasecmp(cmd, "VideoBufferSize")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.rc_buffer_size = atoi(arg) * 8*1024;
            }
        } else if (!av_strcasecmp(cmd, "VideoBitRateTolerance")) {
            if (stream) {
                get_arg(arg, sizeof(arg), &p);
                video_enc.bit_rate_tolerance = atoi(arg) * 1000;
            }
        } else if (!av_strcasecmp(cmd, "VideoBitRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.bit_rate = atoi(arg) * 1000;
            }
        } else if (!av_strcasecmp(cmd, "VideoSize")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                av_parse_video_size(&video_enc.width, &video_enc.height, arg);
                if ((video_enc.width % 16) != 0 ||
                    (video_enc.height % 16) != 0) {
                    ERROR("Image size must be a multiple of 16\n");
                }
            }
        } else if (!av_strcasecmp(cmd, "VideoFrameRate")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                AVRational frame_rate;
                if (av_parse_video_rate(&frame_rate, arg) < 0) {
                    ERROR("Incorrect frame rate: %s\n", arg);
                } else {
                    video_enc.time_base.num = frame_rate.den;
                    video_enc.time_base.den = frame_rate.num;
                }
            }
        } else if (!av_strcasecmp(cmd, "VideoGopSize")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                video_enc.gop_size = atoi(arg);
        } else if (!av_strcasecmp(cmd, "VideoIntraOnly")) {
            if (stream)
                video_enc.gop_size = 1;
        } else if (!av_strcasecmp(cmd, "VideoHighQuality")) {
            if (stream)
                video_enc.mb_decision = FF_MB_DECISION_BITS;
        } else if (!av_strcasecmp(cmd, "Video4MotionVector")) {
            if (stream) {
                video_enc.mb_decision = FF_MB_DECISION_BITS; //FIXME remove
                video_enc.flags |= CODEC_FLAG_4MV;
            }
        } else if (!av_strcasecmp(cmd, "AVOptionVideo") ||
                   !av_strcasecmp(cmd, "AVOptionAudio")) {
            char arg2[1024];
            AVCodecContext *avctx;
            int type;
            get_arg(arg, sizeof(arg), &p);
            get_arg(arg2, sizeof(arg2), &p);
            if (!av_strcasecmp(cmd, "AVOptionVideo")) {
                avctx = &video_enc;
                type = AV_OPT_FLAG_VIDEO_PARAM;
            } else {
                avctx = &audio_enc;
                type = AV_OPT_FLAG_AUDIO_PARAM;
            }
            if (avserver_opt_default(arg, arg2, avctx, type|AV_OPT_FLAG_ENCODING_PARAM)) {
                ERROR("AVOption error: %s %s\n", arg, arg2);
            }
        } else if (!av_strcasecmp(cmd, "AVPresetVideo") ||
                   !av_strcasecmp(cmd, "AVPresetAudio")) {
            AVCodecContext *avctx;
            int type;
            get_arg(arg, sizeof(arg), &p);
            if (!av_strcasecmp(cmd, "AVPresetVideo")) {
                avctx = &video_enc;
                video_enc.codec_id = video_id;
                type = AV_OPT_FLAG_VIDEO_PARAM;
            } else {
                avctx = &audio_enc;
                audio_enc.codec_id = audio_id;
                type = AV_OPT_FLAG_AUDIO_PARAM;
            }
            if (avserver_opt_preset(arg, avctx, type|AV_OPT_FLAG_ENCODING_PARAM, &audio_id, &video_id)) {
                ERROR("AVPreset error: %s\n", arg);
            }
        } else if (!av_strcasecmp(cmd, "VideoTag")) {
            get_arg(arg, sizeof(arg), &p);
            if ((strlen(arg) == 4) && stream)
                video_enc.codec_tag = MKTAG(arg[0], arg[1], arg[2], arg[3]);
        } else if (!av_strcasecmp(cmd, "BitExact")) {
            if (stream)
                video_enc.flags |= CODEC_FLAG_BITEXACT;
        } else if (!av_strcasecmp(cmd, "DctFastint")) {
            if (stream)
                video_enc.dct_algo  = FF_DCT_FASTINT;
        } else if (!av_strcasecmp(cmd, "IdctSimple")) {
            if (stream)
                video_enc.idct_algo = FF_IDCT_SIMPLE;
        } else if (!av_strcasecmp(cmd, "Qscale")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.flags |= CODEC_FLAG_QSCALE;
                video_enc.global_quality = FF_QP2LAMBDA * atoi(arg);
            }
        } else if (!av_strcasecmp(cmd, "VideoQDiff")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.max_qdiff = atoi(arg);
                if (video_enc.max_qdiff < 1 || video_enc.max_qdiff > 31) {
                    ERROR("VideoQDiff out of range\n");
                }
            }
        } else if (!av_strcasecmp(cmd, "VideoQMax")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.qmax = atoi(arg);
                if (video_enc.qmax < 1 || video_enc.qmax > 31) {
                    ERROR("VideoQMax out of range\n");
                }
            }
        } else if (!av_strcasecmp(cmd, "VideoQMin")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                video_enc.qmin = atoi(arg);
                if (video_enc.qmin < 1 || video_enc.qmin > 31) {
                    ERROR("VideoQMin out of range\n");
                }
            }
        } else if (!av_strcasecmp(cmd, "LumiMask")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                video_enc.lumi_masking = atof(arg);
        } else if (!av_strcasecmp(cmd, "DarkMask")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                video_enc.dark_masking = atof(arg);
        } else if (!av_strcasecmp(cmd, "NoVideo")) {
            video_id = AV_CODEC_ID_NONE;
        } else if (!av_strcasecmp(cmd, "NoAudio")) {
            audio_id = AV_CODEC_ID_NONE;
        } else if (!av_strcasecmp(cmd, "ACL")) {
            parse_acl_row(stream, feed, NULL, p, filename, line_num);
        } else if (!av_strcasecmp(cmd, "DynamicACL")) {
            if (stream) {
                get_arg(stream->dynamic_acl, sizeof(stream->dynamic_acl), &p);
            }
        } else if (!av_strcasecmp(cmd, "RTSPOption")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                av_freep(&stream->rtsp_option);
                stream->rtsp_option = av_strdup(arg);
            }
        } else if (!av_strcasecmp(cmd, "MulticastAddress")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream) {
                if (resolve_host(&stream->multicast_ip, arg) != 0) {
                    ERROR("Invalid host/IP address: %s\n", arg);
                }
                stream->is_multicast = 1;
                stream->loop = 1; /* default is looping */
            }
        } else if (!av_strcasecmp(cmd, "MulticastPort")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                stream->multicast_port = atoi(arg);
        } else if (!av_strcasecmp(cmd, "MulticastTTL")) {
            get_arg(arg, sizeof(arg), &p);
            if (stream)
                stream->multicast_ttl = atoi(arg);
        } else if (!av_strcasecmp(cmd, "NoLoop")) {
            if (stream)
                stream->loop = 0;
        } else if (!av_strcasecmp(cmd, "</Stream>")) {
            if (!stream) {
                ERROR("No corresponding <Stream> for </Stream>\n");
            } else {
                if (stream->feed && stream->fmt && strcmp(stream->fmt->name, "ffm") != 0) {
                    if (audio_id != AV_CODEC_ID_NONE) {
                        audio_enc.codec_type = AVMEDIA_TYPE_AUDIO;
                        audio_enc.codec_id = audio_id;
                        add_codec(stream, &audio_enc);
                    }
                    if (video_id != AV_CODEC_ID_NONE) {
                        video_enc.codec_type = AVMEDIA_TYPE_VIDEO;
                        video_enc.codec_id = video_id;
                        add_codec(stream, &video_enc);
                    }
                }
                stream = NULL;
            }
        } else if (!av_strcasecmp(cmd, "<Redirect")) {
            /*********************************************/
            char *q;
            if (stream || feed || redirect) {
                ERROR("Already in a tag\n");
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
        } else if (!av_strcasecmp(cmd, "URL")) {
            if (redirect)
                get_arg(redirect->feed_filename, sizeof(redirect->feed_filename), &p);
        } else if (!av_strcasecmp(cmd, "</Redirect>")) {
            if (!redirect) {
                ERROR("No corresponding <Redirect> for </Redirect>\n");
            } else {
                if (!redirect->feed_filename[0]) {
                    ERROR("No URL found for <Redirect>\n");
                }
                redirect = NULL;
            }
        } else if (!av_strcasecmp(cmd, "LoadModule")) {
            ERROR("Loadable modules no longer supported\n");
        } else {
            ERROR("Incorrect keyword: '%s'\n", cmd);
        }
    }
#undef ERROR

    fclose(f);
    if (errors)
        return -1;
    else
        return 0;
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

                if (uptime < 30)
                    /* Turn off any more restarts */
                    feed->child_argv = 0;
            }
        }
    }

    need_to_start_children = 1;
}

static void opt_debug(void)
{
    avserver_debug = 1;
    logfilename[0] = '-';
}

void show_help_default(const char *opt, const char *arg)
{
    printf("usage: avserver [options]\n"
           "Hyper fast multi format Audio/Video streaming server\n");
    printf("\n");
    show_help_options(options, "Main options:", 0, 0, 0);
}

static const OptionDef options[] = {
#include "cmdutils_common_opts.h"
    { "n", OPT_BOOL, {(void *)&no_launch }, "enable no-launch mode" },
    { "d", 0, {(void*)opt_debug}, "enable debug mode" },
    { "f", HAS_ARG | OPT_STRING, {(void*)&config_filename }, "use configfile instead of /etc/avserver.conf", "configfile" },
    { NULL },
};

int main(int argc, char **argv)
{
    struct sigaction sigact = { { 0 } };

    config_filename = av_strdup("/etc/avserver.conf");

    parse_loglevel(argc, argv, options);
    av_register_all();
    avformat_network_init();

    show_banner();

    my_program_name = argv[0];

    parse_options(NULL, argc, argv, options, NULL);

    unsetenv("http_proxy");             /* Kill the http_proxy */

    av_lfg_init(&random_state, av_get_random_seed());

    sigact.sa_handler = handle_child_exit;
    sigact.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sigact, 0);

    if (parse_ffconfig(config_filename) < 0) {
        fprintf(stderr, "Incorrect config file - exiting.\n");
        exit(1);
    }

    /* open log file if needed */
    if (logfilename[0] != '\0') {
        if (!strcmp(logfilename, "-"))
            logfile = stdout;
        else
            logfile = fopen(logfilename, "a");
        av_log_set_callback(http_av_log);
    }

    build_file_streams();

    build_feed_streams();

    compute_bandwidth();

    /* signal init */
    signal(SIGPIPE, SIG_IGN);

    if (http_server() < 0) {
        http_log("Could not start server\n");
        exit(1);
    }

    return 0;
}
