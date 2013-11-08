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

#include <fcntl.h>
#include <libssh/sftp.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"
#include "url.h"

typedef struct {
    const AVClass *class;
    ssh_session session;
    sftp_session sftp;
    sftp_file file;
    int64_t filesize;
    int rw_timeout;
    int trunc;
} LIBSSHContext;

static int libssh_close(URLContext *h)
{
    LIBSSHContext *s = h->priv_data;
    if (s->file)
        sftp_close(s->file);
    if (s->sftp)
        sftp_free(s->sftp);
    if (s->session) {
        ssh_disconnect(s->session);
        ssh_free(s->session);
    }
    return 0;
}

static int libssh_open(URLContext *h, const char *url, int flags)
{
    static const int verbosity = SSH_LOG_NOLOG;
    LIBSSHContext *s = h->priv_data;
    char proto[10], path[MAX_URL_SIZE], hostname[1024], credencials[1024];
    int port = 22, access, ret;
    long timeout = s->rw_timeout * 1000;
    const char *user = NULL, *pass = NULL;
    char *end = NULL;
    sftp_attributes stat;

    av_url_split(proto, sizeof(proto),
                 credencials, sizeof(credencials),
                 hostname, sizeof(hostname),
                 &port,
                 path, sizeof(path),
                 url);

    if (port <= 0 || port > 65535)
        port = 22;

    if (!(s->session = ssh_new())) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    user = av_strtok(credencials, ":", &end);
    pass = av_strtok(end, ":", &end);
    ssh_options_set(s->session, SSH_OPTIONS_HOST, hostname);
    ssh_options_set(s->session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(s->session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    if (timeout > 0)
        ssh_options_set(s->session, SSH_OPTIONS_TIMEOUT_USEC, &timeout);
    if (user)
        ssh_options_set(s->session, SSH_OPTIONS_USER, user);

    if (ssh_connect(s->session) != SSH_OK) {
        av_log(h, AV_LOG_ERROR, "Connection failed. %s\n", ssh_get_error(s->session));
        ret = AVERROR(EIO);
        goto fail;
    }

    if (pass && ssh_userauth_password(s->session, NULL, pass) != SSH_AUTH_SUCCESS) {
        av_log(h, AV_LOG_ERROR, "Error authenticating with password: %s\n", ssh_get_error(s->session));
        ret = AVERROR(EACCES);
        goto fail;
    }

    if (!(s->sftp = sftp_new(s->session))) {
        av_log(h, AV_LOG_ERROR, "SFTP session creation failed: %s\n", ssh_get_error(s->session));
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (sftp_init(s->sftp) != SSH_OK) {
        av_log(h, AV_LOG_ERROR, "Error initializing sftp session: %s\n", ssh_get_error(s->session));
        ret = AVERROR(EIO);
        goto fail;
    }

    if ((flags & AVIO_FLAG_WRITE) && (flags & AVIO_FLAG_READ)) {
        access = O_CREAT | O_RDWR;
        if (s->trunc)
            access |= O_TRUNC;
    } else if (flags & AVIO_FLAG_WRITE) {
        access = O_CREAT | O_WRONLY;
        if (s->trunc)
            access |= O_TRUNC;
    } else {
        access = O_RDONLY;
    }

    if (!(s->file = sftp_open(s->sftp, path, access, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH))) {
        av_log(h, AV_LOG_ERROR, "Error opening sftp file: %s\n", ssh_get_error(s->session));
        ret = AVERROR(EIO);
        goto fail;
    }

    if (!(stat = sftp_fstat(s->file))) {
        av_log(h, AV_LOG_WARNING, "Cannot stat remote file %s.\n", path);
        s->filesize = -1;
    } else {
        s->filesize = stat->size;
        sftp_attributes_free(stat);
    }

    return 0;

  fail:
    libssh_close(h);
    return ret;
}

static int64_t libssh_seek(URLContext *h, int64_t pos, int whence)
{
    LIBSSHContext *s = h->priv_data;
    int64_t newpos;

    if (s->filesize == -1 && (whence == AVSEEK_SIZE || whence == SEEK_END)) {
        av_log(h, AV_LOG_ERROR, "Error during seeking.\n");
        return AVERROR(EIO);
    }

    switch(whence) {
    case AVSEEK_SIZE:
        return s->filesize;
    case SEEK_SET:
        newpos = pos;
        break;
    case SEEK_CUR:
        newpos = sftp_tell64(s->file);
        break;
    case SEEK_END:
        newpos = s->filesize + pos;
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (sftp_seek64(s->file, newpos)) {
        av_log(h, AV_LOG_ERROR, "Error during seeking.\n");
        return AVERROR(EIO);
    }

    return newpos;
}

static int libssh_read(URLContext *h, unsigned char *buf, int size)
{
    LIBSSHContext *s = h->priv_data;
    int bytes_read;

    if ((bytes_read = sftp_read(s->file, buf, size)) < 0) {
        av_log(h, AV_LOG_ERROR, "Read error.\n");
        return AVERROR(EIO);
    }
    return bytes_read;
}

static int libssh_write(URLContext *h, const unsigned char *buf, int size)
{
    LIBSSHContext *s = h->priv_data;
    int bytes_written;

    if ((bytes_written = sftp_write(s->file, buf, size)) < 0) {
        av_log(h, AV_LOG_ERROR, "Write error.\n");
        return AVERROR(EIO);
    }
    return bytes_written;
}

#define OFFSET(x) offsetof(LIBSSHContext, x)
#define D AV_OPT_FLAG_DECODING_PARAM
#define E AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"timeout", "set timeout of socket I/O operations", OFFSET(rw_timeout), AV_OPT_TYPE_INT, {.i64 = -1}, -1, INT_MAX, D|E },
    {"truncate", "Truncate existing files on write", OFFSET(trunc), AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 1, E },
    {NULL}
};

static const AVClass libssh_context_class = {
    .class_name     = "libssh",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

URLProtocol ff_libssh_protocol = {
    .name                = "sftp",
    .url_open            = libssh_open,
    .url_read            = libssh_read,
    .url_write           = libssh_write,
    .url_seek            = libssh_seek,
    .url_close           = libssh_close,
    .priv_data_size      = sizeof(LIBSSHContext),
    .priv_data_class     = &libssh_context_class,
    .flags               = URL_PROTOCOL_FLAG_NETWORK,
};
