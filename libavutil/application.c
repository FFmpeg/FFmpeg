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

#include "application.h"
#include "libavformat/network.h"
#include "libavutil/avstring.h"

void av_application_on_io_traffic(AVApplicationContext *h, AVAppIOTraffic *event);

int av_application_alloc(AVApplicationContext **ph, void *opaque)
{
    AVApplicationContext *h = NULL;

    h = av_mallocz(sizeof(AVApplicationContext));
    if (!h)
        return AVERROR(ENOMEM);

    h->opaque = opaque;

    *ph = h;
    return 0;
}

int av_application_open(AVApplicationContext **ph, void *opaque)
{
    int ret = av_application_alloc(ph, opaque);
    if (ret)
        return ret;

    return 0;
}

void av_application_close(AVApplicationContext *h)
{
    av_free(h);
}

void av_application_closep(AVApplicationContext **ph)
{
    if (!ph || !*ph)
        return;

    av_application_close(*ph);
    *ph = NULL;
}

void av_application_on_http_event(AVApplicationContext *h, int event_type, AVAppHttpEvent *event)
{
    if (h && h->func_on_app_event)
        h->func_on_app_event(h, event_type, (void *)event, sizeof(AVAppHttpEvent));
}

void av_application_will_http_open(AVApplicationContext *h, void *obj, const char *url, int64_t start_time, int64_t end_time)
{
    AVAppHttpEvent event = {0};

    if (!h || !obj || !url)
        return;

    event.obj        = obj;
    av_strlcpy(event.url, url, sizeof(event.url));
    event.start_time = start_time;
    event.end_time = end_time;

    av_application_on_http_event(h, AVAPP_EVENT_WILL_HTTP_OPEN, &event);
}

void av_application_did_http_open(AVApplicationContext *h, void *obj, const char *url, int error, int http_code, int64_t filesize, int64_t start_time, int64_t end_time)
{
    AVAppHttpEvent event = {0};

    if (!h || !obj || !url)
        return;

    event.obj        = obj;
    av_strlcpy(event.url, url, sizeof(event.url));
    event.error     = error;
    event.http_code = http_code;
    event.filesize  = filesize;
    event.start_time = start_time;
    event.end_time = end_time;

    av_application_on_http_event(h, AVAPP_EVENT_DID_HTTP_OPEN, &event);
}

void av_application_will_http_seek(AVApplicationContext *h, void *obj, const char *url, int64_t offset, int64_t start_time, int64_t end_time)
{
    AVAppHttpEvent event = {0};

    if (!h || !obj || !url)
        return;

    event.obj        = obj;
    event.offset     = offset;
    av_strlcpy(event.url, url, sizeof(event.url));
    event.start_time = start_time;
    event.end_time = end_time;

    av_application_on_http_event(h, AVAPP_EVENT_WILL_HTTP_SEEK, &event);
}

void av_application_did_http_seek(AVApplicationContext *h, void *obj, const char *url, int64_t offset, int error, int http_code, int64_t start_time, int64_t end_time)
{
    AVAppHttpEvent event = {0};

    if (!h || !obj || !url)
        return;

    event.obj        = obj;
    event.offset     = offset;
    av_strlcpy(event.url, url, sizeof(event.url));
    event.error     = error;
    event.http_code = http_code;
    event.start_time = start_time;
    event.end_time = end_time;

    av_application_on_http_event(h, AVAPP_EVENT_DID_HTTP_SEEK, &event);
}

void av_application_on_io_traffic(AVApplicationContext *h, AVAppIOTraffic *event)
{
    if (h && h->func_on_app_event)
        h->func_on_app_event(h, AVAPP_EVENT_IO_TRAFFIC, (void *)event, sizeof(AVAppIOTraffic));
}

int  av_application_on_io_control(AVApplicationContext *h, int event_type, AVAppIOControl *control)
{
    if (h && h->func_on_app_event)
        return h->func_on_app_event(h, event_type, (void *)control, sizeof(AVAppIOControl));
    return 0;
}

int  av_application_on_switch_control(AVApplicationContext *h, int event_type, AVAppSwitchControl *control){
    if (h && h->func_on_app_event)
        return h->func_on_app_event(h, event_type, (void *)control, sizeof(AVAppSwitchControl));
    return 0;
}

int av_application_on_tcp_will_open(AVApplicationContext *h)
{
    if (h && h->func_on_app_event) {
        AVAppTcpIOControl control = {0};
        return h->func_on_app_event(h, AVAPP_CTRL_WILL_TCP_OPEN, (void *)&control, sizeof(AVAppTcpIOControl));
    }
    return 0;
}

// only callback returns error
int av_application_on_tcp_did_open(AVApplicationContext *h, int error, int fd, AVAppTcpIOControl *control)
{
    struct sockaddr_storage so_stg;
    int       ret = 0;
    socklen_t so_len = sizeof(so_stg);
    int       so_family;
    char      *so_ip_name = control->ip;

    if (!h || !h->func_on_app_event || fd <= 0)
        return 0;

    ret = getpeername(fd, (struct sockaddr *)&so_stg, &so_len);
    if (ret)
        return 0;
    control->error = error;
    control->fd = fd;

    so_family = ((struct sockaddr*)&so_stg)->sa_family;
    switch (so_family) {
        case AF_INET: {
            struct sockaddr_in* in4 = (struct sockaddr_in*)&so_stg;
            if (inet_ntop(AF_INET, &(in4->sin_addr), so_ip_name, sizeof(control->ip))) {
                control->family = AF_INET;
                control->port = in4->sin_port;
            }
            break;
        }
        case AF_INET6: {
            struct sockaddr_in6* in6 = (struct sockaddr_in6*)&so_stg;
            if (inet_ntop(AF_INET6, &(in6->sin6_addr), so_ip_name, sizeof(control->ip))) {
                control->family = AF_INET6;
                control->port = in6->sin6_port;
            }
            break;
        }
    }

    return h->func_on_app_event(h, AVAPP_CTRL_DID_TCP_OPEN, (void *)control, sizeof(AVAppTcpIOControl));
}

// only callback returns error
int av_application_quic_on_tcp_did_open(AVApplicationContext *h, int error)
{
    if (h && h->func_on_app_event) {
        AVAppTcpIOControl control = {0};
        control.error = error;
        control.port = 443;
        return h->func_on_app_event(h, AVAPP_CTRL_DID_TCP_OPEN, (void *)&control, sizeof(AVAppTcpIOControl));
    }
    return 0;
}

void av_application_on_async_statistic(AVApplicationContext *h, AVAppAsyncStatistic *statistic)
{
    if (h && h->func_on_app_event)
        h->func_on_app_event(h, AVAPP_EVENT_ASYNC_STATISTIC, (void *)statistic, sizeof(AVAppAsyncStatistic));
}

void av_application_on_async_read_speed(AVApplicationContext *h, AVAppAsyncReadSpeed *speed)
{
    if (h && h->func_on_app_event)
        h->func_on_app_event(h, AVAPP_EVENT_ASYNC_READ_SPEED, (void *)speed, sizeof(AVAppAsyncReadSpeed));
}

void av_application_did_io_tcp_read(AVApplicationContext *h, void *obj, int bytes)
{
    AVAppIOTraffic event = {0};
    if (!h || !obj || bytes <= 0)
        return;

    event.obj        = obj;
    event.bytes      = bytes;

    av_application_on_io_traffic(h, &event);
}

void av_application_on_dash_info(AVApplicationContext *h, int event_type, AVAppDashChange *info) {
    if (h && h->func_on_app_event)
        h->func_on_app_event(h, event_type, (void *)info, sizeof(AVAppDashChange));
}

void av_application_on_dns_will_open(AVApplicationContext *h, char *hostname) {
    if (h && h->func_on_app_event) {
        AVAppDnsEvent event = {0};
        if (hostname != NULL) {
            strcpy(event.host, hostname);
        }
        h->func_on_app_event(h, AVAPP_EVENT_WILL_DNS_OPEN, (void *)&event, sizeof(AVAppDnsEvent));
    }
}

void av_application_on_dns_did_open(AVApplicationContext *h, char *hostname, char *ip, int hit_cache, int64_t dns_time) {
    if (h && h->func_on_app_event) {
        AVAppDnsEvent event = {0};
        if (hostname != NULL && ip != NULL) {
            strcpy(event.host, hostname);
            strcpy(event.ip, ip);
            if (!strcmp(event.host, event.ip)) {
                event.is_ip = 1;
            }
            event.hit_cache = hit_cache;
            event.dns_time = dns_time;
        }
        h->func_on_app_event(h, AVAPP_EVENT_DID_DNS_OPEN, (void *)&event, sizeof(AVAppDnsEvent));
    }
}
