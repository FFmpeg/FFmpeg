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
#define LIBSSH_STATIC
#include <libssh/sftp.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/attributes.h"
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
    char *priv_key;
} LIBSSHContext;

static av_cold int libssh_create_ssh_session(LIBSSHContext *libssh, const char* hostname, unsigned int port)
{
    static const int verbosity = SSH_LOG_NOLOG;

    if (!(libssh->session = ssh_new())) {
        av_log(libssh, AV_LOG_ERROR, "SSH session creation failed: %s\n", ssh_get_error(libssh->session));
        return AVERROR(ENOMEM);
    }
    ssh_options_set(libssh->session, SSH_OPTIONS_HOST, hostname);
    ssh_options_set(libssh->session, SSH_OPTIONS_PORT, &port);
    ssh_options_set(libssh->session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    if (libssh->rw_timeout > 0) {
        long timeout = libssh->rw_timeout * 1000;
        ssh_options_set(libssh->session, SSH_OPTIONS_TIMEOUT_USEC, &timeout);
    }

    if (ssh_connect(libssh->session) != SSH_OK) {
        av_log(libssh, AV_LOG_ERROR, "Connection failed: %s\n", ssh_get_error(libssh->session));
        return AVERROR(EIO);
    }

    return 0;
}

static av_cold int libssh_authentication(LIBSSHContext *libssh, const char *user, const char *password)
{
    int authorized = 0;
    int auth_methods;

    if (user)
        ssh_options_set(libssh->session, SSH_OPTIONS_USER, user);

    if (ssh_userauth_none(libssh->session, NULL) == SSH_AUTH_SUCCESS)
        return 0;

    auth_methods = ssh_userauth_list(libssh->session, NULL);

    if (auth_methods & SSH_AUTH_METHOD_PUBLICKEY) {
        if (libssh->priv_key) {
            ssh_string pub_key;
            ssh_private_key priv_key;
            int type;
            if (!ssh_try_publickey_from_file(libssh->session, libssh->priv_key, &pub_key, &type)) {
                priv_key = privatekey_from_file(libssh->session, libssh->priv_key, type, password);
                if (ssh_userauth_pubkey(libssh->session, NULL, pub_key, priv_key) == SSH_AUTH_SUCCESS) {
                    av_log(libssh, AV_LOG_DEBUG, "Authentication successful with selected private key.\n");
                    authorized = 1;
                }
            } else {
                av_log(libssh, AV_LOG_DEBUG, "Invalid key is provided.\n");
                return AVERROR(EACCES);
            }
        } else if (ssh_userauth_autopubkey(libssh->session, password) == SSH_AUTH_SUCCESS) {
            av_log(libssh, AV_LOG_DEBUG, "Authentication successful with auto selected key.\n");
            authorized = 1;
        }
    }

    if (!authorized && (auth_methods & SSH_AUTH_METHOD_PASSWORD)) {
        if (ssh_userauth_password(libssh->session, NULL, password) == SSH_AUTH_SUCCESS) {
            av_log(libssh, AV_LOG_DEBUG, "Authentication successful with password.\n");
            authorized = 1;
        }
    }

    if (!authorized) {
        av_log(libssh, AV_LOG_ERROR, "Authentication failed.\n");
        return AVERROR(EACCES);
    }

    return 0;
}

static av_cold int libssh_create_sftp_session(LIBSSHContext *libssh)
{
    if (!(libssh->sftp = sftp_new(libssh->session))) {
        av_log(libssh, AV_LOG_ERROR, "SFTP session creation failed: %s\n", ssh_get_error(libssh->session));
        return AVERROR(ENOMEM);
    }

    if (sftp_init(libssh->sftp) != SSH_OK) {
        av_log(libssh, AV_LOG_ERROR, "Error initializing sftp session: %s\n", ssh_get_error(libssh->session));
        return AVERROR(EIO);
    }

    return 0;
}

static av_cold int libssh_open_file(LIBSSHContext *libssh, int flags, const char *file)
{
    int access;

    if ((flags & AVIO_FLAG_WRITE) && (flags & AVIO_FLAG_READ)) {
        access = O_CREAT | O_RDWR;
        if (libssh->trunc)
            access |= O_TRUNC;
    } else if (flags & AVIO_FLAG_WRITE) {
        access = O_CREAT | O_WRONLY;
        if (libssh->trunc)
            access |= O_TRUNC;
    } else
        access = O_RDONLY;

    /* 0666 = -rw-rw-rw- = read+write for everyone, minus umask */
    if (!(libssh->file = sftp_open(libssh->sftp, file, access, 0666))) {
        av_log(libssh, AV_LOG_ERROR, "Error opening sftp file: %s\n", ssh_get_error(libssh->session));
        return AVERROR(EIO);
    }

    return 0;
}

static av_cold void libssh_stat_file(LIBSSHContext *libssh)
{
    sftp_attributes stat;

    if (!(stat = sftp_fstat(libssh->file))) {
        av_log(libssh, AV_LOG_WARNING, "Cannot stat remote file.\n");
        libssh->filesize = -1;
    } else {
        libssh->filesize = stat->size;
        sftp_attributes_free(stat);
    }
}

static av_cold int libssh_close(URLContext *h)
{
    LIBSSHContext *libssh = h->priv_data;
    if (libssh->file) {
        sftp_close(libssh->file);
        libssh->file = NULL;
    }
    if (libssh->sftp) {
        sftp_free(libssh->sftp);
        libssh->sftp = NULL;
    }
    if (libssh->session) {
        ssh_disconnect(libssh->session);
        ssh_free(libssh->session);
        libssh->session = NULL;
    }
    return 0;
}

static av_cold int libssh_open(URLContext *h, const char *url, int flags)
{
    LIBSSHContext *libssh = h->priv_data;
    char proto[10], path[MAX_URL_SIZE], hostname[1024], credencials[1024];
    int port = 22, ret;
    const char *user = NULL, *pass = NULL;
    char *end = NULL;

    av_url_split(proto, sizeof(proto),
                 credencials, sizeof(credencials),
                 hostname, sizeof(hostname),
                 &port,
                 path, sizeof(path),
                 url);

    if (port <= 0 || port > 65535)
        port = 22;

    if ((ret = libssh_create_ssh_session(libssh, hostname, port)) < 0)
        goto fail;

    user = av_strtok(credencials, ":", &end);
    pass = av_strtok(end, ":", &end);

    if ((ret = libssh_authentication(libssh, user, pass)) < 0)
        goto fail;

    if ((ret = libssh_create_sftp_session(libssh)) < 0)
        goto fail;

    if ((ret = libssh_open_file(libssh, flags, path)) < 0)
        goto fail;

    libssh_stat_file(libssh);

    return 0;

  fail:
    libssh_close(h);
    return ret;
}

static int64_t libssh_seek(URLContext *h, int64_t pos, int whence)
{
    LIBSSHContext *libssh = h->priv_data;
    int64_t newpos;

    if (libssh->filesize == -1 && (whence == AVSEEK_SIZE || whence == SEEK_END)) {
        av_log(h, AV_LOG_ERROR, "Error during seeking.\n");
        return AVERROR(EIO);
    }

    switch(whence) {
    case AVSEEK_SIZE:
        return libssh->filesize;
    case SEEK_SET:
        newpos = pos;
        break;
    case SEEK_CUR:
        newpos = sftp_tell64(libssh->file) + pos;
        break;
    case SEEK_END:
        newpos = libssh->filesize + pos;
        break;
    default:
        return AVERROR(EINVAL);
    }

    if (newpos < 0) {
        av_log(h, AV_LOG_ERROR, "Seeking to nagative position.\n");
        return AVERROR(EINVAL);
    }

    if (sftp_seek64(libssh->file, newpos)) {
        av_log(h, AV_LOG_ERROR, "Error during seeking.\n");
        return AVERROR(EIO);
    }

    return newpos;
}

static int libssh_read(URLContext *h, unsigned char *buf, int size)
{
    LIBSSHContext *libssh = h->priv_data;
    int bytes_read;

    if ((bytes_read = sftp_read(libssh->file, buf, size)) < 0) {
        av_log(libssh, AV_LOG_ERROR, "Read error.\n");
        return AVERROR(EIO);
    }
    return bytes_read;
}

static int libssh_write(URLContext *h, const unsigned char *buf, int size)
{
    LIBSSHContext *libssh = h->priv_data;
    int bytes_written;

    if ((bytes_written = sftp_write(libssh->file, buf, size)) < 0) {
        av_log(libssh, AV_LOG_ERROR, "Write error.\n");
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
    {"private_key", "set path to private key", OFFSET(priv_key), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, D|E },
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
