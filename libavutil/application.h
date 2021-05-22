/*
 * copyright (c) 2016 Zhang Rui
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

#ifndef AVUTIL_APPLICATION_H
#define AVUTIL_APPLICATION_H

#include "libavutil/log.h"
#include "libavcodec/avcodec.h"

#define AVAPP_EVENT_WILL_HTTP_OPEN  1 //AVAppHttpEvent
#define AVAPP_EVENT_DID_HTTP_OPEN   2 //AVAppHttpEvent
#define AVAPP_EVENT_WILL_HTTP_SEEK  3 //AVAppHttpEvent
#define AVAPP_EVENT_DID_HTTP_SEEK   4 //AVAppHttpEvent

#define AVAPP_EVENT_WILL_DNS_OPEN  5 //AVAppDnsEvent
#define AVAPP_EVENT_DID_DNS_OPEN   6 //AVAppDnsEvent

#define AVAPP_EVENT_URL_CHANGED  7 // URL CHANGE EVENT
#define AVAPP_EVENT_IJK_FIND_STREAM_INFO   8 // IJK FIND STREAM INFO EVENT

#define AVAPP_EVENT_IJK_PKG_COUNT_TRACKER   9 // IJK pkg count EVENT
#define AVAPP_EVENT_IO_STATUS         10 // IJK io status EVENT

#define AVAPP_EVENT_ASYNC_STATISTIC     0x11000 //AVAppAsyncStatistic
#define AVAPP_EVENT_ASYNC_READ_SPEED    0x11001 //AVAppAsyncReadSpeed
#define AVAPP_EVENT_IO_TRAFFIC          0x12204 //AVAppIOTraffic

#define AVAPP_CTRL_WILL_TCP_OPEN   0x20001 //AVAppTcpIOControl
#define AVAPP_CTRL_DID_TCP_OPEN    0x20002 //AVAppTcpIOControl

#define AVAPP_CTRL_WILL_HTTP_OPEN  0x20003 //AVAppIOControl
#define AVAPP_CTRL_WILL_LIVE_OPEN  0x20005 //AVAppIOControl

#define AVAPP_CTRL_WILL_CONCAT_SEGMENT_OPEN 0x20007 //AVAppIOControl
#define AVAPP_CTRL_WILL_FILE_OPEN 0x20009 //AVAppIOControl
#define AVAPP_CTRL_WILL_FILE_IO_OPEN  0x2000a //AVAppIOControl

#define AVAPP_SWITCH_CTRL_UPDATE_STREAM 0x40012

#define MAX_IP_LEN 196

#define TCP_STREAM_TYPE_DASH_AUDIO 1
#define TCP_STREAM_TYPE_DASH_VIDEO 2
#define TCP_STREAM_TYPE_NORMAL     3

#define DNS_TYPE_NO_USE 0
#define DNS_TYPE_LOCAL_DNS 1
#define DNS_TYPE_DNS_CACHE 2
#define DNS_TYPE_HTTP_DNS 3

#define WRAP_INET_FAMILY 2
#define WRAP_INET6_FAMILY 10
#define WRAP_UNKNOWN_FAMILY 0

typedef struct AVAppDashStream
{
    int audio_stream_nb;
    int video_stream_nb;
    int64_t video_bandwidth[20];
    int64_t audio_bandwidth[20];
    int video_id[20];
    int audio_id[20];
    int cur_video_id;
    int cur_audio_id;
} AVAppDashStream;

typedef struct AVAppIOControl {
    size_t  size;
    char    url[4096];      /* in, out */
    int     segment_index;  /* in, default = 0 */
    int     qn;   /**/
    int     retry_counter;  /* in */

    int     is_handled;     /* out, default = false */
    int     is_url_changed; /* out, default = false */
    int64_t    file_size; /*out*/
    int     is_audio;
    int     http_code;
    int     error_code;
} AVAppIOControl;

typedef struct AVAppTcpIOControl {
    int  error;
    int  family;
    char ip[MAX_IP_LEN];
    int  port;
    int  fd;
    int  is_audio;
    int64_t  duration;
} AVAppTcpIOControl;

typedef struct AVAppAsyncStatistic {
    size_t  size;
    int64_t buf_backwards;
    int64_t buf_forwards;
    int64_t buf_capacity;
} AVAppAsyncStatistic;

typedef struct AVAppAsyncReadSpeed {
    size_t  size;
    int     is_full_speed;
    int64_t io_bytes;
    int64_t elapsed_milli;
} AVAppAsyncReadSpeed;

typedef struct AVAppHttpEvent
{
    void    *obj;
    char     url[4096];
    int64_t  offset;
    int      error;
    int      http_code;
    int64_t  filesize;
    int64_t  start_time;
    int64_t  end_time;
    int      is_audio;
} AVAppHttpEvent;

typedef struct AVAppIOTraffic
{
    void   *obj;
    int     bytes;
    int     dash_audio_nread;
    int     dash_video_nread;
    int     normal_nread;
} AVAppIOTraffic;


typedef enum {
    SWITCH_AUDIO = 1,
    SWITCH_VIDEO = 2,
}SWITCH_MODE;

typedef enum {
    SWITCH_CMD_AUTO_SWITCH = 1,
    SWITCH_CMD_AUDIO_ONLY,
    SWITCH_CMD_GET_STREAM_INFO,
    SWITCH_CMD_UPDATE_CACHE_INFO,
 }SWITCH_CMD;


typedef struct {
    void *opaque;

    int (*switch_start)(void * opaque, int64_t switch_serial, int64_t switch_point, int vid, int aid);

    int64_t (*switch_wait_complete)(void * opaque, int64_t switch_serial, int *switch_mode);

    // update prop buffer_level audio_only;
    int (*switch_cmd)(void *oqaque, int cmd, AVDictionary **pm);
}AVAppSwitchControl;


typedef struct AVAppDnsEvent
{
    char host[1024];
    char ip[MAX_IP_LEN];
    int  is_ip;
    int  hit_cache;
    int64_t  dns_time;
    int  dns_type;
    int  is_audio;
    int  error_code;
    int  family;
} AVAppDnsEvent;

typedef struct{
    volatile void * item;
    volatile int item_switch_req;
    void* mutex;
} IJKItemApplication;

typedef struct{
    int64_t timestamp;
    int64_t duration;
    int is_audio;
} AVAppFindStreamInfo;

typedef struct{
    int64_t timestamp;
    int is_audio;
    int url_change_count;
} AVAppUrlChanged;

typedef struct{
    int64_t revc_video_ten_pkg_timestamp;
    int64_t revc_audio_ten_pkg_timestamp;
    int64_t revc_video_first_pkg_timestamp;
    int64_t revc_audio_first_pkg_timestamp;
    int64_t first_video_will_http_timestamp;
    int64_t first_audio_will_http_timestamp;
    int64_t first_video_did_http_ok_timestamp;
    int64_t first_audio_did_http_ok_timestamp;
} AVAppPkgCountTracker;

#define STREAM_SIZE_NOT_MATCH -1
#define STREAM_SIZE_INVALID   -2

typedef struct{
    int is_audio;
    int io_error;
    int64_t filesize;
} AVAppIOStatus;

typedef struct AVApplicationContext AVApplicationContext;
struct AVApplicationContext {
    const AVClass *av_class;    /**< information for av_log(). Set by av_application_open(). */
    void *opaque;               /**< user data. */
    int dash_audio_read_len;
    int dash_audio_recv_buffer_size;
    int dash_video_recv_buffer_size;
    int dash_audio_tcp;
    int dash_video_tcp;
    int (*func_on_app_event)(AVApplicationContext *h, int event_type ,void *obj, size_t size);
    int (*func_app_ctrl)(int what, int64_t arg0, void *obj, size_t size);
    int ioproxy;
    int active_reconnect;
    int64_t active_reconnect_count;
};
#if CONFIG_HTTPS_PROTOCOL
void dirty_openssl_extra(void);
#endif
int  av_application_alloc(AVApplicationContext **ph, void *opaque);
int  av_application_open(AVApplicationContext **ph, void *opaque);
void av_application_close(AVApplicationContext *h);
void av_application_closep(AVApplicationContext **ph);

void av_application_on_http_event(AVApplicationContext *h, int event_type, AVAppHttpEvent *event);
void av_application_will_http_open(AVApplicationContext *h, void *obj, const char *url, int64_t start_time, int64_t end_time);
void av_application_did_http_open(AVApplicationContext *h, void *obj, const char *url, int error, int http_code,
                                  int64_t filesize, int64_t start_time, int64_t end_time);
void av_application_will_http_seek(AVApplicationContext *h, void *obj, const char *url, int64_t offset, int64_t start_time, int64_t end_time);
void av_application_did_http_seek(AVApplicationContext *h, void *obj, const char *url, int64_t offset, int error,
                                  int http_code, int64_t start_time, int64_t end_time);

void av_application_did_io_tcp_read(AVApplicationContext *h, void *obj, int bytes, int nread, int type);

int  av_application_on_io_control(AVApplicationContext *h, int event_type, AVAppIOControl *control);

int av_application_on_tcp_will_open(AVApplicationContext *h, int ai_family);
int av_application_on_tcp_did_open(AVApplicationContext *h, int error, int fd, AVAppTcpIOControl *control, int is_audio, int ai_family, int64_t duration);

void av_application_on_async_statistic(AVApplicationContext *h, AVAppAsyncStatistic *statistic);
void av_application_on_async_read_speed(AVApplicationContext *h, AVAppAsyncReadSpeed *speed);
void av_application_on_dns_will_open(AVApplicationContext *h, char *hostname);
void av_application_on_dns_did_open(AVApplicationContext *h, char *hostname, char *ip, int dns_type, int64_t dns_time, int is_audio, int ai_family, int error_code);

void av_application_on_url_changed(AVApplicationContext *h,int url_change_count,int is_audio);
void av_application_on_ijk_find_stream_info(AVApplicationContext *h, int64_t duration, int is_audio);
void av_application_on_io_status(AVApplicationContext *h, AVAppIOStatus *status);
#endif /* AVUTIL_APPLICATION_H */
