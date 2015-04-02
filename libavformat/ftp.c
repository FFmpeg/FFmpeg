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

#include "libavutil/avstring.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"
#include "libavutil/opt.h"
#include "libavutil/bprint.h"

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
    URLContext *conn_control;                    /**< Control connection */
    URLContext *conn_data;                       /**< Data connection, NULL when not connected */
    uint8_t control_buffer[CONTROL_BUFFER_SIZE]; /**< Control connection buffer */
    uint8_t *control_buf_ptr, *control_buf_end;
    int server_data_port;                        /**< Data connection port opened by server, -1 on error. */
    int server_control_port;                     /**< Control connection port, default is 21 */
    char *hostname;                              /**< Server address. */
    char *user;                                  /**< Server user */
    char *password;                              /**< Server user's password */
    char *path;                                  /**< Path to resource on server. */
    int64_t filesize;                            /**< Size of file on server, -1 on error. */
    int64_t position;                            /**< Current position, calculated. */
    int rw_timeout;                              /**< Network timeout. */
    const char *anonymous_password;              /**< Password to be used for anonymous user. An email should be used. */
    int write_seekable;                          /**< Control seekability, 0 = disable, 1 = enable. */
    FTPState state;                              /**< State of data connection */
} FTPContext;

#define OFFSET(x) offsetof(FTPContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"timeout", "set timeout of socket I/O operations", OFFSET(rw_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, D|E },
    {"ftp-write-seekable", "control seekability of connection during encoding", OFFSET(write_seekable), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, E },
    {"ftp-anonymous-password", "password for anonymous login. E-mail address should be used.", OFFSET(anonymous_password), AV_OPT_TYPE_STRING, { 0 }, 0, 0, D|E },
    {NULL}
};

static const AVClass ftp_context_class = {
    .class_name     = "ftp",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

static int ftp_close(URLContext *h);

static int ftp_getc(FTPContext *s)
{
    int len;
    if (s->control_buf_ptr >= s->control_buf_end) {
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

    for (;;) {
        ch = ftp_getc(s);
        if (ch < 0) {
            return ch;
        }
        if (ch == '\n') {
            /* process line */
            if (q > line && q[-1] == '\r')
                q--;
            *q = '\0';
            return 0;
        } else {
            if ((q - line) < line_size - 1)
                *q++ = ch;
        }
    }
}

/*
 * This routine returns ftp server response code.
 * Server may send more than one response for a certain command.
 * First expected code is returned.
 */
static int ftp_status(FTPContext *s, char **line, const int response_codes[])
{
    int err, i, dash = 0, result = 0, code_found = 0, linesize;
    char buf[CONTROL_BUFFER_SIZE];
    AVBPrint line_buffer;

    if (line)
        av_bprint_init(&line_buffer, 0, AV_BPRINT_SIZE_AUTOMATIC);

    while (!code_found || dash) {
        if ((err = ftp_get_line(s, buf, sizeof(buf))) < 0) {
            if (line)
                av_bprint_finalize(&line_buffer, NULL);
            return err;
        }

        av_log(s, AV_LOG_DEBUG, "%s\n", buf);

        linesize = strlen(buf);
        err = 0;
        if (linesize >= 3) {
            for (i = 0; i < 3; ++i) {
                if (buf[i] < '0' || buf[i] > '9') {
                    err = 0;
                    break;
                }
                err *= 10;
                err += buf[i] - '0';
            }
        }

        if (!code_found) {
            if (err >= 500) {
                code_found = 1;
                result = err;
            } else
                for (i = 0; response_codes[i]; ++i) {
                    if (err == response_codes[i]) {
                        code_found = 1;
                        result = err;
                        break;
                    }
                }
        }
        if (code_found) {
            if (line)
                av_bprintf(&line_buffer, "%s\r\n", buf);
            if (linesize >= 4) {
                if (!dash && buf[3] == '-')
                    dash = err;
                else if (err == dash && buf[3] == ' ')
                    dash = 0;
            }
        }
    }

    if (line)
        av_bprint_finalize(&line_buffer, line);
    return result;
}

static int ftp_send_command(FTPContext *s, const char *command,
                            const int response_codes[], char **response)
{
    int err;

    if (response)
        *response = NULL;

    if ((err = ffurl_write(s->conn_control, command, strlen(command))) < 0)
        return err;
    if (!err)
        return -1;

    /* return status */
    if (response_codes) {
        return ftp_status(s, response, response_codes);
    }
    return 0;
}

static void ftp_close_data_connection(FTPContext *s)
{
    ffurl_closep(&s->conn_data);
    s->position = 0;
    s->state = DISCONNECTED;
}

static void ftp_close_both_connections(FTPContext *s)
{
    ffurl_closep(&s->conn_control);
    ftp_close_data_connection(s);
}

static int ftp_auth(FTPContext *s)
{
    char buf[CONTROL_BUFFER_SIZE];
    int err;
    static const int user_codes[] = {331, 230, 0};
    static const int pass_codes[] = {230, 0};

    snprintf(buf, sizeof(buf), "USER %s\r\n", s->user);
    err = ftp_send_command(s, buf, user_codes, NULL);
    if (err == 331) {
        if (s->password) {
            snprintf(buf, sizeof(buf), "PASS %s\r\n", s->password);
            err = ftp_send_command(s, buf, pass_codes, NULL);
        } else
            return AVERROR(EACCES);
    }
    if (err != 230)
        return AVERROR(EACCES);

    return 0;
}

static int ftp_passive_mode_epsv(FTPContext *s)
{
    char *res = NULL, *start = NULL, *end = NULL;
    int i;
    static const char d = '|';
    static const char *command = "EPSV\r\n";
    static const int epsv_codes[] = {229, 0};

    if (ftp_send_command(s, command, epsv_codes, &res) != 229 || !res)
        goto fail;

    for (i = 0; res[i]; ++i) {
        if (res[i] == '(') {
            start = res + i + 1;
        } else if (res[i] == ')') {
            end = res + i;
            break;
        }
    }
    if (!start || !end)
        goto fail;

    *end = '\0';
    if (strlen(start) < 5)
        goto fail;
    if (start[0] != d || start[1] != d || start[2] != d || end[-1] != d)
        goto fail;
    start += 3;
    end[-1] = '\0';

    s->server_data_port = atoi(start);
    av_dlog(s, "Server data port: %d\n", s->server_data_port);

    av_free(res);
    return 0;

  fail:
    av_free(res);
    s->server_data_port = -1;
    return AVERROR(ENOSYS);
}

static int ftp_passive_mode(FTPContext *s)
{
    char *res = NULL, *start = NULL, *end = NULL;
    int i;
    static const char *command = "PASV\r\n";
    static const int pasv_codes[] = {227, 0};

    if (ftp_send_command(s, command, pasv_codes, &res) != 227 || !res)
        goto fail;

    for (i = 0; res[i]; ++i) {
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
    int i;
    static const char *command = "PWD\r\n";
    static const int pwd_codes[] = {257, 0};

    if (ftp_send_command(s, command, pwd_codes, &res) != 257 || !res)
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
    s->path = av_strdup(start);

    av_free(res);

    if (!s->path)
        return AVERROR(ENOMEM);
    return 0;

  fail:
    av_free(res);
    return AVERROR(EIO);
}

static int ftp_file_size(FTPContext *s)
{
    char command[CONTROL_BUFFER_SIZE];
    char *res = NULL;
    static const int size_codes[] = {213, 0};

    snprintf(command, sizeof(command), "SIZE %s\r\n", s->path);
    if (ftp_send_command(s, command, size_codes, &res) == 213 && res) {
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
    char command[CONTROL_BUFFER_SIZE];
    static const int retr_codes[] = {150, 0};

    snprintf(command, sizeof(command), "RETR %s\r\n", s->path);
    if (ftp_send_command(s, command, retr_codes, NULL) != 150)
        return AVERROR(EIO);

    s->state = DOWNLOADING;

    return 0;
}

static int ftp_store(FTPContext *s)
{
    char command[CONTROL_BUFFER_SIZE];
    static const int stor_codes[] = {150, 0};

    snprintf(command, sizeof(command), "STOR %s\r\n", s->path);
    if (ftp_send_command(s, command, stor_codes, NULL) != 150)
        return AVERROR(EIO);

    s->state = UPLOADING;

    return 0;
}

static int ftp_type(FTPContext *s)
{
    static const char *command = "TYPE I\r\n";
    static const int type_codes[] = {200, 0};

    if (ftp_send_command(s, command, type_codes, NULL) != 200)
        return AVERROR(EIO);

    return 0;
}

static int ftp_restart(FTPContext *s, int64_t pos)
{
    char command[CONTROL_BUFFER_SIZE];
    static const int rest_codes[] = {350, 0};

    snprintf(command, sizeof(command), "REST %"PRId64"\r\n", pos);
    if (ftp_send_command(s, command, rest_codes, NULL) != 350)
        return AVERROR(EIO);

    return 0;
}

static int ftp_features(FTPContext *s)
{
    static const char *feat_command        = "FEAT\r\n";
    static const char *enable_utf8_command = "OPTS UTF8 ON\r\n";
    static const int feat_codes[] = {211, 0};
    static const int opts_codes[] = {200, 451};
    char *feat = NULL;

    if (ftp_send_command(s, feat_command, feat_codes, &feat) == 211) {
        if (av_stristr(feat, "UTF8"))
            ftp_send_command(s, enable_utf8_command, opts_codes, NULL);
    }
    av_freep(&feat);

    return 0;
}

static int ftp_connect_control_connection(URLContext *h)
{
    char buf[CONTROL_BUFFER_SIZE], *response = NULL;
    int err;
    AVDictionary *opts = NULL;
    FTPContext *s = h->priv_data;
    static const int connect_codes[] = {220, 0};

    if (!s->conn_control) {
        ff_url_join(buf, sizeof(buf), "tcp", NULL,
                    s->hostname, s->server_control_port, NULL);
        if (s->rw_timeout != -1) {
            av_dict_set_int(&opts, "timeout", s->rw_timeout, 0);
        } /* if option is not given, don't pass it and let tcp use its own default */
        err = ffurl_open(&s->conn_control, buf, AVIO_FLAG_READ_WRITE,
                         &h->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (err < 0) {
            av_log(h, AV_LOG_ERROR, "Cannot open control connection\n");
            return err;
        }

        /* check if server is ready */
        if (ftp_status(s, ((h->flags & AVIO_FLAG_WRITE) ? &response : NULL), connect_codes) != 220) {
            av_log(h, AV_LOG_ERROR, "FTP server not ready for new users\n");
            return AVERROR(EACCES);
        }

        if ((h->flags & AVIO_FLAG_WRITE) && av_stristr(response, "pure-ftpd")) {
            av_log(h, AV_LOG_WARNING, "Pure-FTPd server is used as an output protocol. It is known issue this implementation may produce incorrect content and it cannot be fixed at this moment.");
        }
        av_free(response);

        if ((err = ftp_auth(s)) < 0) {
            av_log(h, AV_LOG_ERROR, "FTP authentication failed\n");
            return err;
        }

        if ((err = ftp_type(s)) < 0) {
            av_log(h, AV_LOG_ERROR, "Set content type failed\n");
            return err;
        }

        ftp_features(s);
    }
    return 0;
}

static int ftp_connect_data_connection(URLContext *h)
{
    int err;
    char buf[CONTROL_BUFFER_SIZE];
    AVDictionary *opts = NULL;
    FTPContext *s = h->priv_data;

    if (!s->conn_data) {
        /* Enter passive mode */
        if (ftp_passive_mode_epsv(s) < 0) {
            /* Use PASV as fallback */
            if ((err = ftp_passive_mode(s)) < 0)
                return err;
        }
        /* Open data connection */
        ff_url_join(buf, sizeof(buf), "tcp", NULL, s->hostname, s->server_data_port, NULL);
        if (s->rw_timeout != -1) {
            av_dict_set_int(&opts, "timeout", s->rw_timeout, 0);
        } /* if option is not given, don't pass it and let tcp use its own default */
        err = ffurl_open(&s->conn_data, buf, h->flags,
                         &h->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (err < 0)
            return err;

        if (s->position)
            if ((err = ftp_restart(s, s->position)) < 0)
                return err;
    }
    s->state = READY;
    return 0;
}

static int ftp_abort(URLContext *h)
{
    static const char *command = "ABOR\r\n";
    int err;
    static const int abor_codes[] = {225, 226, 0};
    FTPContext *s = h->priv_data;

    /* According to RCF 959:
       "ABOR command tells the server to abort the previous FTP
       service command and any associated transfer of data."

       There are FTP server implementations that don't response
       to any commands during data transfer in passive mode (including ABOR).

       This implementation closes data connection by force.
    */

    if (ftp_send_command(s, command, NULL, NULL) < 0) {
        ftp_close_both_connections(s);
        if ((err = ftp_connect_control_connection(h)) < 0) {
            av_log(h, AV_LOG_ERROR, "Reconnect failed.\n");
            return err;
        }
    } else {
        ftp_close_data_connection(s);
        if (ftp_status(s, NULL, abor_codes) < 225) {
            /* wu-ftpd also closes control connection after data connection closing */
            ffurl_closep(&s->conn_control);
            if ((err = ftp_connect_control_connection(h)) < 0) {
                av_log(h, AV_LOG_ERROR, "Reconnect failed.\n");
                return err;
            }
        }
    }

    return 0;
}

static int ftp_open(URLContext *h, const char *url, int flags)
{
    char proto[10], path[MAX_URL_SIZE], credencials[MAX_URL_SIZE], hostname[MAX_URL_SIZE];
    const char *tok_user = NULL, *tok_pass = NULL;
    char *end = NULL;
    int err;
    size_t pathlen;
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol open\n");

    s->state = DISCONNECTED;
    s->filesize = -1;
    s->position = 0;

    av_url_split(proto, sizeof(proto),
                 credencials, sizeof(credencials),
                 hostname, sizeof(hostname),
                 &s->server_control_port,
                 path, sizeof(path),
                 url);

    tok_user = av_strtok(credencials, ":", &end);
    tok_pass = av_strtok(end, ":", &end);
    if (!tok_user) {
        tok_user = "anonymous";
        tok_pass = av_x_if_null(s->anonymous_password, "nopassword");
    }
    s->user = av_strdup(tok_user);
    s->password = av_strdup(tok_pass);
    s->hostname = av_strdup(hostname);
    if (!s->hostname || !s->user || (tok_pass && !s->password)) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->server_control_port < 0 || s->server_control_port > 65535)
        s->server_control_port = 21;

    if ((err = ftp_connect_control_connection(h)) < 0)
        goto fail;

    if ((err = ftp_current_dir(s)) < 0)
        goto fail;
    pathlen = strlen(s->path) + strlen(path) + 1;
    if ((err = av_reallocp(&s->path, pathlen)) < 0)
        goto fail;
    av_strlcat(s->path + strlen(s->path), path, pathlen);

    if (ftp_restart(s, 0) < 0) {
        h->is_streamed = 1;
    } else {
        if (ftp_file_size(s) < 0 && flags & AVIO_FLAG_READ)
            h->is_streamed = 1;
        if (s->write_seekable != 1 && flags & AVIO_FLAG_WRITE)
            h->is_streamed = 1;
    }

    return 0;

  fail:
    av_log(h, AV_LOG_ERROR, "FTP open failed\n");
    ftp_close(h);
    return err;
}

static int64_t ftp_seek(URLContext *h, int64_t pos, int whence)
{
    FTPContext *s = h->priv_data;
    int err;
    int64_t new_pos, fake_pos;

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

    if (h->is_streamed)
        return AVERROR(EIO);

    if (new_pos < 0) {
        av_log(h, AV_LOG_ERROR, "Seeking to nagative position.\n");
        return AVERROR(EINVAL);
    }

    fake_pos = s->filesize != -1 ? FFMIN(new_pos, s->filesize) : new_pos;
    if (fake_pos != s->position) {
        if ((err = ftp_abort(h)) < 0)
            return err;
        s->position = fake_pos;
    }
    return new_pos;
}

static int ftp_read(URLContext *h, unsigned char *buf, int size)
{
    FTPContext *s = h->priv_data;
    int read, err, retry_done = 0;

    av_dlog(h, "ftp protocol read %d bytes\n", size);
  retry:
    if (s->state == DISCONNECTED) {
        /* optimization */
        if (s->position >= s->filesize)
            return 0;
        if ((err = ftp_connect_data_connection(h)) < 0)
            return err;
    }
    if (s->state == READY) {
        if (s->position >= s->filesize)
            return 0;
        if ((err = ftp_retrieve(s)) < 0)
            return err;
    }
    if (s->conn_data && s->state == DOWNLOADING) {
        read = ffurl_read(s->conn_data, buf, size);
        if (read >= 0) {
            s->position += read;
            if (s->position >= s->filesize) {
                /* server will terminate, but keep current position to avoid madness */
                /* save position to restart from it */
                int64_t pos = s->position;
                if (ftp_abort(h) < 0) {
                    s->position = pos;
                    return AVERROR(EIO);
                }
                s->position = pos;
            }
        }
        if (read <= 0 && s->position < s->filesize && !h->is_streamed) {
            /* Server closed connection. Probably due to inactivity */
            int64_t pos = s->position;
            av_log(h, AV_LOG_INFO, "Reconnect to FTP server.\n");
            if ((err = ftp_abort(h)) < 0)
                return err;
            if ((err = ftp_seek(h, pos, SEEK_SET)) < 0) {
                av_log(h, AV_LOG_ERROR, "Position cannot be restored.\n");
                return err;
            }
            if (!retry_done) {
                retry_done = 1;
                goto retry;
            }
        }
        return read;
    }

    av_log(h, AV_LOG_DEBUG, "FTP read failed\n");
    return AVERROR(EIO);
}

static int ftp_write(URLContext *h, const unsigned char *buf, int size)
{
    int err;
    FTPContext *s = h->priv_data;
    int written;

    av_dlog(h, "ftp protocol write %d bytes\n", size);

    if (s->state == DISCONNECTED) {
        if ((err = ftp_connect_data_connection(h)) < 0)
            return err;
    }
    if (s->state == READY) {
        if ((err = ftp_store(s)) < 0)
            return err;
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

static int ftp_close(URLContext *h)
{
    FTPContext *s = h->priv_data;

    av_dlog(h, "ftp protocol close\n");

    ftp_close_both_connections(s);
    av_freep(&s->user);
    av_freep(&s->password);
    av_freep(&s->hostname);
    av_freep(&s->path);

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
