/*
 * Copyright (c) 2013 Lukasz Marek <lukasz.m.luki@gmail.com>
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

#include <stdlib.h>
#include "libavutil/avstring.h"
#include "libavutil/avassert.h"
#include "avformat.h"
#include "internal.h"
#include "network.h"
#include "os_support.h"
#include "url.h"
#include "libavutil/opt.h"

#define CONTROL_BUFFER_SIZE 1024

typedef enum {
    UNKNOWN,
    READY,
    DOWNLOADING,
    UPLOADING,
    DISCONNECTED
} FTPState;


typedef struct {
    const AVClass *class;
    URLContext *conn_control;        /**< Control connection */
    int conn_control_block_flag;     /**< Controls block/unblock mode of data connection */
    AVIOInterruptCB conn_control_interrupt_cb; /**< Controls block/unblock mode of data connection */
    URLContext *conn_data;           /**< Data connection, NULL when not connected */
    uint8_t control_buffer[CONTROL_BUFFER_SIZE], *control_buf_ptr, *control_buf_end; /**< Control connection buffer */
    int server_data_port;            /**< Data connection port opened by server, -1 on error. */
    char hostname[512];              /**< Server address. */
    char path[MAX_URL_SIZE];         /**< Path to resource on server. */
    int64_t filesize;                /**< Size of file on server, -1 on error. */
    int64_t position;                /**< Current position, calculated. */
    int rw_timeout;                  /**< Network timeout. */
    const char *anonymous_password;  /**< Password to be used for anonymous user. An email should be used. */
    int write_seekable;              /**< Control seekability, 0 = disable, 1 = enable. */
    FTPState state;                  /**< State of data connection */
} FTPContext;

#define OFFSET(x) offsetof(FTPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"timeout", "set timeout of socket I/O operations", OFFSET(rw_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, D|E },
    {"ftp-write-seekable", "control seekability of connection during encoding", OFFSET(write_seekable), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, E },
    {"ftp-anonymous-password", "password for anonynous login. E-mail address should be used.", OFFSET(anonymous_password), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
    {NULL}
};

static const AVClass ftp_context_class = {
    .class_name     = "ftp",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int ftp_conn_control_block_control(void *data)
{
    FTPContext *s = data;
    return s->conn_control_block_flag;
}

static int ftp_getc(FTPContext *s)
{
    int len;
    if (s->control_buf_ptr >= s->control_buf_end) {
        if (s->conn_control_block_flag)
            return AVERROR_EXIT;
        len = ffurl_read(s->conn_control, s->control_buffer, CONTROL_BUFFER_SIZE);
        if (len < 0) {
            return len;
        } else if (!len) {
            return -1;
        } else {
            s->control_buf_ptr = s->control_buffer;
            s->control_buf_end = s->control_buffer + len;
        }
    }
    return *s->control_buf_ptr++;
}

static int ftp_get_line(FTPContext *s, char *line, int line_size)
{
    int ch;
    char *q = line;
    int ori_block_flag = s->conn_control_block_flag;

    for (;;) {
        ch = ftp_getc(s);
        if (ch < 0) {
            s->conn_control_block_flag = ori_block_flag;
            return ch;
        }
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';

            s->conn_control_block_flag = ori_block_flag;
            return 0;
        } else {
            s->conn_control_block_flag = 0; /* line need to be finished */
            if ((q - line) < line_size - 1)
                *q++ = ch;
        }
    }
}

/*
 * This routine returns ftp server response code.
 * Server may send more than one response for a certain command, following priorites are used:
 *   - 5xx code is returned if occurred. (means error)
 *   - When pref_code is set then pref_code is return if occurred. (expected result)
 *   - The lowest code is returned. (means success)
 */
static int ftp_status(FTPContext *s, int *major, int *minor, int *extra, char **line, int pref_code)
{
    int err, result = -1, pref_code_found = 0;
    char buf[CONTROL_BUFFER_SIZE];
    unsigned char d_major, d_minor, d_extra;

    /* Set blocking mode */
    s->conn_control_block_flag = 0;
    for (;;) {
        if ((err = ftp_get_line(s, buf, CONTROL_BUFFER_SIZE)) < 0) {
            if (err == AVERROR_EXIT)
                return result;
            return err;
        }

        if (strlen(buf) < 3)
            continue;
        d_major = buf[0];
        if (d_major < '1' || d_major > '6' || d_major == '4')
            continue;
        d_minor = buf[1];
        if (d_minor < '0' || d_minor > '9')
            continue;
        d_extra = buf[2];
        if (d_extra < '0' || d_extra > '9')
            continue;

        av_log(s, AV_LOG_DEBUG, "%s\n", buf);

        err = d_major * 100 + d_minor * 10 + d_extra - 111 * '0';

        if ((result < 0 || err < result || pref_code == err) && !pref_code_found || d_major == '5') {
            if (pref_code == err || d_major == '5')
                pref_code_found = 1;
            result = err;
            if (major)
                *major = d_major - '0';
            if (minor)
                *minor = d_minor - '0';
            if (extra)
                *extra = d_extra - '0';
            if (line)
                *line = av_strdup(buf);
        }

        /* first code received. Now get all lines in non blocking mode */
        if (pref_code < 0 || pref_code_found)
            s->conn_control_block_flag = 1;
    }
    return result;
}

static int ftp_auth(FTPContext *s, char *auth)
{
    const char *user = NULL, *pass = NULL;
    char *end = NULL, buf[CONTROL_BUFFER_SIZE];
    int err;
    av_assert2(auth);

    user = av_strtok(auth, ":", &end);
    pass = av_strtok(end, ":", &end);

    if (user) {
        snprintf(buf, sizeof(buf), "USER %s\r\n", user);
        if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
            return err;
        ftp_status(s, &err, NULL, NULL, NULL, -1);
        if (err == 3) {
            if (pass) {
                snprintf(buf, sizeof(buf), "PASS %s\r\n", pass);
                if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
                    return err;
                ftp_status(s, &err, NULL, NULL, NULL, -1);
            } else
                return AVERROR(EACCES);
        }
        if (err != 2) {
            return AVERROR(EACCES);
        }
    } else {
        const char* command = "USER anonymous\r\n";
        if ((err = ffurl_write(s->conn_control, command, strlen(command))) < 0)
            return err;
        ftp_status(s, &err, NULL, NULL, NULL, -1);
        if (err == 3) {
            if (s->anonymous_password) {
                snprintf(buf, sizeof(buf), "PASS %s\r\n", s->anonymous_password);
            } else
                snprintf(buf, sizeof(buf), "PASS nopassword\r\n");
            if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
                return err;
            ftp_status(s, &err, NULL, NULL, NULL, -1);
        }
        if (err != 2) {
            return AVERROR(EACCES);
        }
    }

    return 0;
}

static int ftp_passive_mode(FTPContext *s)
{
    char *res = NULL, *start, *end;
    int err, i;
    const char *command = "PASV\r\n";

    if ((err = ffurl_write(s->conn_control, command, strlen(command))) < 0)
        return err;
    if (ftp_status(s, NULL, NULL, NULL, &res, 227) != 227)
        goto fail;

    start = NULL;
    for (i = 0; i < strlen(res); ++i) {
        if (res[i] == '(') {
            start = res + i + 1;
        } else if (res[i] == ')') {
            end = res + i;
            break;
        }
    }
    if (!start || !end)
        goto fail;

    *end  = '\0';
    /* skip ip */
    if (!av_strtok(start, ",", &end)) goto fail;
    if (!av_strtok(end, ",", &end)) goto fail;
    if (!av_strtok(end, ",", &end)) goto fail;
    if (!av_strtok(end, ",", &end)) goto fail;

    /* parse port number */
    start = av_strtok(end, ",", &end);
    if (!start) goto fail;
    s->server_data_port = atoi(start) * 256;
    start = av_strtok(end, ",", &end);
    if (!start) goto fail;
    s->server_data_port += atoi(start);
    av_dlog(s, "Server data port: %d\n", s->server_data_port);

    av_free(res);
    return 0;

  fail:
    av_free(res);
    s->server_data_port = -1;
    return AVERROR(EIO);
}

static int ftp_current_dir(FTPContext *s)
{
    char *res = NULL, *start = NULL, *end = NULL;
    int err, i;
    const char *command = "PWD\r\n";

    if ((err = ffurl_write(s->conn_control, command, strlen(command))) < 0)
        return err;
    if (ftp_status(s, NULL, NULL, NULL, &res, 257) != 257)
        goto fail;

    for (i = 0; res[i]; ++i) {
        if (res[i] == '"') {
            if (!start) {
                start = res + i + 1;
                continue;
            }
            end = res + i;
            break;
        }
    }

    if (!end)
        goto fail;

    if (end > res && end[-1] == '/') {
        end[-1] = '\0';
    } else
        *end = '\0';
    av_strlcpy(s->path, start, sizeof(s->path));

    av_free(res);
    return 0;

  fail:
    av_free(res);
    return AVERROR(EIO);
}

static int ftp_file_size(FTPContext *s)
{
    char buf[CONTROL_BUFFER_SIZE];
    int err;
    char *res = NULL;

    snprintf(buf, sizeof(buf), "SIZE %s\r\n", s->path);
    if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
        return err;
    if (ftp_status(s, NULL, NULL, NULL, &res, 213) == 213) {
        s->filesize = strtoll(&res[4], NULL, 10);
    } else {
        s->filesize = -1;
        av_free(res);
        return AVERROR(EIO);
    }

    av_free(res);
    return 0;
}

static int ftp_retrieve(FTPContext *s)
{
    char buf[CONTROL_BUFFER_SIZE];
    int err;

    snprintf(buf, sizeof(buf), "RETR %s\r\n", s->path);
    if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
        return err;
    if (ftp_status(s, NULL, NULL, NULL, NULL, 150) != 150)
        return AVERROR(EIO);

    s->state = DOWNLOADING;

    return 0;
}

static int ftp_store(FTPContext *s)
{
    char buf[CONTROL_BUFFER_SIZE];
    int err;

    snprintf(buf, sizeof(buf), "STOR %s\r\n", s->path);
    if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
        return err;
    if (ftp_status(s, NULL, NULL, NULL, NULL, 150) != 150)
        return AVERROR(EIO);

    s->state = UPLOADING;

    return 0;
}

static int ftp_send_command(FTPContext *s, const char* command)
{
    int err;

    if ((err = ffurl_write(s->conn_control, command, strlen(command))) < 0)
        return err;
    ftp_status(s, &err, NULL, NULL, NULL, -1);
    if (err != 2)
        return AVERROR(EIO);

    return 0;
}

static int ftp_reconnect_data_connection(URLContext *h)
{
    int err;
    char buf[CONTROL_BUFFER_SIZE], opts_format[20];
    AVDictionary *opts = NULL;
    FTPContext *s = h->priv_data;

    if (!s->conn_data) {
        ff_url_join(buf, sizeof(buf), "tcp", NULL, s->hostname, s->server_data_port, NULL);
        if (s->rw_timeout != -1) {
            snprintf(opts_format, sizeof(opts_format), "%d", s->rw_timeout);
            av_dict_set(&opts, "timeout", opts_format, 0);
        } /* if option is not given, don't pass it and let tcp use its own default */
        err = ffurl_open(&s->conn_data, buf, AVIO_FLAG_READ_WRITE,
                         &h->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (err < 0)
            return err;
    }
    s->state = READY;
    s->position = 0;
    return 0;
}

static int ftp_open(URLContext *h, const char *url, int flags)
{
    char proto[10], auth[1024], path[MAX_URL_SIZE], buf[CONTROL_BUFFER_SIZE], opts_format[20];
    AVDictionary *opts = NULL;
    int port, err;
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol open\n");

    s->state = DISCONNECTED;
    s->filesize = -1;
    s->conn_control_interrupt_cb.opaque = s;
    s->conn_control_interrupt_cb.callback = ftp_conn_control_block_control;

    av_url_split(proto, sizeof(proto),
                 auth, sizeof(auth),
                 s->hostname, sizeof(s->hostname),
                 &port,
                 path, sizeof(path),
                 url);

    if (port < 0)
        port = 21;

    if (!s->conn_control) {
        ff_url_join(buf, sizeof(buf), "tcp", NULL, s->hostname, port, NULL);
        if (s->rw_timeout != -1) {
            snprintf(opts_format, sizeof(opts_format), "%d", s->rw_timeout);
            av_dict_set(&opts, "timeout", opts_format, 0);
        } /* if option is not given, don't pass it and let tcp use its own default */
        err = ffurl_open(&s->conn_control, buf, AVIO_FLAG_READ_WRITE,
                         &s->conn_control_interrupt_cb, &opts);
        av_dict_free(&opts);
        if (err < 0)
            goto fail;

        /* consume all messages from server */
        if (ftp_status(s, NULL, NULL, NULL, NULL, 220) != 220) {
            av_log(h, AV_LOG_ERROR, "Server not ready for new users\n");
            err = AVERROR(EACCES);
            goto fail;
        }

        if ((err = ftp_auth(s, auth)) < 0)
            goto fail;

        if ((err = ftp_send_command(s, "TYPE I\r\n")) < 0)
            goto fail;

        if ((err = ftp_current_dir(s)) < 0)
            goto fail;
        av_strlcat(s->path, path, sizeof(s->path));

        if ((err = ftp_passive_mode(s)) < 0)
            goto fail;

        if (ftp_file_size(s) < 0 && flags & AVIO_FLAG_READ)
            h->is_streamed = 1;
        if (s->write_seekable != 1 && flags & AVIO_FLAG_WRITE)
            h->is_streamed = 1;
    }

    if ((err = ftp_reconnect_data_connection(h)) < 0)
        goto fail;

    return 0;
  fail:
    av_log(h, AV_LOG_ERROR, "FTP open failed\n");
    ffurl_closep(&s->conn_control);
    ffurl_closep(&s->conn_data);
    return err;
}

static int ftp_read(URLContext *h, unsigned char *buf, int size)
{
    FTPContext *s = h->priv_data;
    int read;

    av_dlog(h, "ftp protocol read %d bytes\n", size);

    if (s->state == READY) {
        ftp_retrieve(s);
    }
    if (s->conn_data && s->state == DOWNLOADING) {
        read = ffurl_read(s->conn_data, buf, size);
        if (read >= 0) {
            s->position += read;
            if (s->position >= s->filesize) {
                ffurl_closep(&s->conn_data);
                s->state = DISCONNECTED;
                if (ftp_status(s, NULL, NULL, NULL,NULL, 226) != 226)
                    return AVERROR(EIO);
            }
        }
        return read;
    }

    av_log(h, AV_LOG_DEBUG, "FTP read failed\n");
    return AVERROR(EIO);
}

static int ftp_write(URLContext *h, const unsigned char *buf, int size)
{
    FTPContext *s = h->priv_data;
    int written;

    av_dlog(h, "ftp protocol write %d bytes\n", size);

    if (s->state == READY) {
        ftp_store(s);
    }
    if (s->conn_data && s->state == UPLOADING) {
        written = ffurl_write(s->conn_data, buf, size);
        if (written > 0) {
            s->position += written;
            s->filesize = FFMAX(s->filesize, s->position);
        }
        return written;
    }

    av_log(h, AV_LOG_ERROR, "FTP write failed\n");
    return AVERROR(EIO);
}

static int64_t ftp_seek(URLContext *h, int64_t pos, int whence)
{
    FTPContext *s = h->priv_data;
    char buf[CONTROL_BUFFER_SIZE];
    int err;
    int64_t new_pos;

    av_dlog(h, "ftp protocol seek %"PRId64" %d\n", pos, whence);

    switch(whence) {
    case AVSEEK_SIZE:
        return s->filesize;
    case SEEK_SET:
        new_pos = pos;
        break;
    case SEEK_CUR:
        new_pos = s->position + pos;
        break;
    case SEEK_END:
        if (s->filesize < 0)
            return AVERROR(EIO);
        new_pos = s->filesize + pos;
        break;
    default:
        return AVERROR(EINVAL);
    }

    if  (h->is_streamed)
        return AVERROR(EIO);

    if (new_pos < 0 || (s->filesize >= 0 && new_pos > s->filesize))
        return AVERROR(EINVAL);

    if (new_pos != s->position) {
        /* close existing data connection */
        if (s->state != READY) {
            if (s->conn_data) {
                /* abort existing transfer */
                if (s->state == DOWNLOADING) {
                    snprintf(buf, sizeof(buf), "ABOR\r\n");
                    if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
                        return err;
                }
                ffurl_closep(&s->conn_data);
                s->state = DISCONNECTED;
                /* Servers return 225 or 226 */
                ftp_status(s, &err, NULL, NULL, NULL, -1);
                if (err != 2)
                    return AVERROR(EIO);
            }

            /* set passive */
            if ((err = ftp_passive_mode(s)) < 0)
                return err;

            /* open new data connection */
            if ((err = ftp_reconnect_data_connection(h)) < 0)
                return err;
        }

        /* resume from pos position */
        snprintf(buf, sizeof(buf), "REST %"PRId64"\r\n", pos);
        if ((err = ffurl_write(s->conn_control, buf, strlen(buf))) < 0)
            return err;
        if (ftp_status(s, NULL, NULL, NULL, NULL, 350) != 350)
            return AVERROR(EIO);

        s->position = pos;
    }
    return new_pos;
}

static int ftp_close(URLContext *h)
{
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol close\n");

    ffurl_closep(&s->conn_control);
    ffurl_closep(&s->conn_data);

    return 0;
}

static int ftp_get_file_handle(URLContext *h)
{
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol get_file_handle\n");

    if (s->conn_data)
        return ffurl_get_file_handle(s->conn_data);

    return AVERROR(EIO);
}

static int ftp_shutdown(URLContext *h, int flags)
{
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol shutdown\n");

    if (s->conn_data)
        return ffurl_shutdown(s->conn_data, flags);

    return AVERROR(EIO);
}

URLProtocol ff_ftp_protocol = {
    .name                = "ftp",
    .url_open            = ftp_open,
    .url_read            = ftp_read,
    .url_write           = ftp_write,
    .url_seek            = ftp_seek,
    .url_close           = ftp_close,
    .url_get_file_handle = ftp_get_file_handle,
    .url_shutdown        = ftp_shutdown,
    .priv_data_size      = sizeof(FTPContext),
    .priv_data_class     = &ftp_context_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
