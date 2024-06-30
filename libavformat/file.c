/*
 * buffered file I/O
 * Copyright (c) 2001 Fabrice Bellard
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

#include "config_components.h"

#include "libavutil/avstring.h"
#include "libavutil/file_open.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avio.h"
#if HAVE_DIRENT_H
#include <dirent.h>
#endif
#include <fcntl.h>
#if HAVE_IO_H
#include <io.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <stdlib.h>
#include "os_support.h"
#include "url.h"

/* Some systems may not have S_ISFIFO */
#ifndef S_ISFIFO
#  ifdef S_IFIFO
#    define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#  else
#    define S_ISFIFO(m) 0
#  endif
#endif

/* Not available in POSIX.1-1996 */
#ifndef S_ISLNK
#  ifdef S_IFLNK
#    define S_ISLNK(m) (((m) & S_IFLNK) == S_IFLNK)
#  else
#    define S_ISLNK(m) 0
#  endif
#endif

/* Not available in POSIX.1-1996 */
#ifndef S_ISSOCK
#  ifdef S_IFSOCK
#    define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#  else
#    define S_ISSOCK(m) 0
#  endif
#endif

/* S_ISREG not available on Windows */
#ifndef S_ISREG
#  ifdef S_IFREG
#    define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#  else
#    define S_ISREG(m) 0
#  endif
#endif

/* S_ISBLK not available on Windows */
#ifndef S_ISBLK
#  ifdef S_IFBLK
#    define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#  else
#    define S_ISBLK(m) 0
#  endif
#endif

/* standard file protocol */

typedef struct FileContext {
    const AVClass *class;
    int fd;
    int trunc;
    int blocksize;
    int follow;
    int seekable;
#if HAVE_DIRENT_H
    DIR *dir;
#endif
} FileContext;

static const AVOption file_options[] = {
    { "truncate", "truncate existing files on write", offsetof(FileContext, trunc), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, AV_OPT_FLAG_ENCODING_PARAM },
    { "blocksize", "set I/O operation maximum block size", offsetof(FileContext, blocksize), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "follow", "Follow a file as it is being written", offsetof(FileContext, follow), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_DECODING_PARAM },
    { "seekable", "Sets if the file is seekable", offsetof(FileContext, seekable), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 0, AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVOption pipe_options[] = {
    { "blocksize", "set I/O operation maximum block size", offsetof(FileContext, blocksize), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "fd", "set file descriptor", offsetof(FileContext, fd), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass file_class = {
    .class_name = "file",
    .item_name  = av_default_item_name,
    .option     = file_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass pipe_class = {
    .class_name = "pipe",
    .item_name  = av_default_item_name,
    .option     = pipe_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVClass fd_class = {
    .class_name = "fd",
    .item_name  = av_default_item_name,
    .option     = pipe_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int file_read(URLContext *h, unsigned char *buf, int size)
{
    FileContext *c = h->priv_data;
    int ret;
    size = FFMIN(size, c->blocksize);
    ret = read(c->fd, buf, size);
    if (ret == 0 && c->follow)
        return AVERROR(EAGAIN);
    if (ret == 0)
        return AVERROR_EOF;
    return (ret == -1) ? AVERROR(errno) : ret;
}

static int file_write(URLContext *h, const unsigned char *buf, int size)
{
    FileContext *c = h->priv_data;
    int ret;
    size = FFMIN(size, c->blocksize);
    ret = write(c->fd, buf, size);
    return (ret == -1) ? AVERROR(errno) : ret;
}

static int file_get_handle(URLContext *h)
{
    FileContext *c = h->priv_data;
    return c->fd;
}

static int file_check(URLContext *h, int mask)
{
    int ret = 0;
    const char *filename = h->filename;
    av_strstart(filename, "file:", &filename);

    {
#if HAVE_ACCESS && defined(R_OK)
    if (access(filename, F_OK) < 0)
        return AVERROR(errno);
    if (mask&AVIO_FLAG_READ)
        if (access(filename, R_OK) >= 0)
            ret |= AVIO_FLAG_READ;
    if (mask&AVIO_FLAG_WRITE)
        if (access(filename, W_OK) >= 0)
            ret |= AVIO_FLAG_WRITE;
#else
    struct stat st;
    ret = stat(filename, &st);
    if (ret < 0)
        return AVERROR(errno);

    ret |= st.st_mode&S_IRUSR ? mask&AVIO_FLAG_READ  : 0;
    ret |= st.st_mode&S_IWUSR ? mask&AVIO_FLAG_WRITE : 0;
#endif
    }
    return ret;
}

#if CONFIG_FD_PROTOCOL || CONFIG_PIPE_PROTOCOL
static int fd_dup(URLContext *h, int oldfd)
{
    int newfd;

#ifdef F_DUPFD_CLOEXEC
    newfd = fcntl(oldfd, F_DUPFD_CLOEXEC, 0);
#else
    newfd = dup(oldfd);
#endif
    if (newfd == -1)
        return newfd;

#if HAVE_FCNTL
    if (fcntl(newfd, F_SETFD, FD_CLOEXEC) == -1)
        av_log(h, AV_LOG_DEBUG, "Failed to set close on exec\n");
#endif

#if HAVE_SETMODE
    setmode(newfd, O_BINARY);
#endif
    return newfd;
}
#endif

static int file_close(URLContext *h)
{
    FileContext *c = h->priv_data;
    int ret = close(c->fd);
    return (ret == -1) ? AVERROR(errno) : 0;
}

/* XXX: use llseek */
static int64_t file_seek(URLContext *h, int64_t pos, int whence)
{
    FileContext *c = h->priv_data;
    int64_t ret;

    if (whence == AVSEEK_SIZE) {
        struct stat st;
        ret = fstat(c->fd, &st);
        return ret < 0 ? AVERROR(errno) : (S_ISFIFO(st.st_mode) ? 0 : st.st_size);
    }

    ret = lseek(c->fd, pos, whence);

    return ret < 0 ? AVERROR(errno) : ret;
}

#if CONFIG_FILE_PROTOCOL

static int file_delete(URLContext *h)
{
#if HAVE_UNISTD_H
    int ret;
    const char *filename = h->filename;
    av_strstart(filename, "file:", &filename);

    ret = rmdir(filename);
    if (ret < 0 && (errno == ENOTDIR
#   ifdef _WIN32
        || errno == EINVAL
#   endif
        ))
        ret = unlink(filename);
    if (ret < 0)
        return AVERROR(errno);

    return ret;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_UNISTD_H */
}

static int file_move(URLContext *h_src, URLContext *h_dst)
{
    const char *filename_src = h_src->filename;
    const char *filename_dst = h_dst->filename;
    av_strstart(filename_src, "file:", &filename_src);
    av_strstart(filename_dst, "file:", &filename_dst);

    if (rename(filename_src, filename_dst) < 0)
        return AVERROR(errno);

    return 0;
}

static int file_open(URLContext *h, const char *filename, int flags)
{
    FileContext *c = h->priv_data;
    int access;
    int fd;
    struct stat st;

    av_strstart(filename, "file:", &filename);

    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ) {
        access = O_CREAT | O_RDWR;
        if (c->trunc)
            access |= O_TRUNC;
    } else if (flags & AVIO_FLAG_WRITE) {
        access = O_CREAT | O_WRONLY;
        if (c->trunc)
            access |= O_TRUNC;
    } else {
        access = O_RDONLY;
    }
#ifdef O_BINARY
    access |= O_BINARY;
#endif
    fd = avpriv_open(filename, access, 0666);
    if (fd == -1)
        return AVERROR(errno);
    c->fd = fd;

    h->is_streamed = !fstat(fd, &st) && S_ISFIFO(st.st_mode);

    /* Buffer writes more than the default 32k to improve throughput especially
     * with networked file systems */
    if (!h->is_streamed && flags & AVIO_FLAG_WRITE)
        h->min_packet_size = h->max_packet_size = 262144;

    if (c->seekable >= 0)
        h->is_streamed = !c->seekable;

    return 0;
}

static int file_open_dir(URLContext *h)
{
#if HAVE_LSTAT
    FileContext *c = h->priv_data;

    c->dir = opendir(h->filename);
    if (!c->dir)
        return AVERROR(errno);

    return 0;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_LSTAT */
}

static int file_read_dir(URLContext *h, AVIODirEntry **next)
{
#if HAVE_LSTAT
    FileContext *c = h->priv_data;
    struct dirent *dir;
    char *fullpath = NULL;

    *next = ff_alloc_dir_entry();
    if (!*next)
        return AVERROR(ENOMEM);
    do {
        errno = 0;
        dir = readdir(c->dir);
        if (!dir) {
            av_freep(next);
            return AVERROR(errno);
        }
    } while (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."));

    fullpath = av_append_path_component(h->filename, dir->d_name);
    if (fullpath) {
        struct stat st;
        if (!lstat(fullpath, &st)) {
            if (S_ISDIR(st.st_mode))
                (*next)->type = AVIO_ENTRY_DIRECTORY;
            else if (S_ISFIFO(st.st_mode))
                (*next)->type = AVIO_ENTRY_NAMED_PIPE;
            else if (S_ISCHR(st.st_mode))
                (*next)->type = AVIO_ENTRY_CHARACTER_DEVICE;
            else if (S_ISBLK(st.st_mode))
                (*next)->type = AVIO_ENTRY_BLOCK_DEVICE;
            else if (S_ISLNK(st.st_mode))
                (*next)->type = AVIO_ENTRY_SYMBOLIC_LINK;
            else if (S_ISSOCK(st.st_mode))
                (*next)->type = AVIO_ENTRY_SOCKET;
            else if (S_ISREG(st.st_mode))
                (*next)->type = AVIO_ENTRY_FILE;
            else
                (*next)->type = AVIO_ENTRY_UNKNOWN;

            (*next)->group_id = st.st_gid;
            (*next)->user_id = st.st_uid;
            (*next)->size = st.st_size;
            (*next)->filemode = st.st_mode & 0777;
            (*next)->modification_timestamp = INT64_C(1000000) * st.st_mtime;
            (*next)->access_timestamp =  INT64_C(1000000) * st.st_atime;
            (*next)->status_change_timestamp = INT64_C(1000000) * st.st_ctime;
        }
        av_free(fullpath);
    }

    (*next)->name = av_strdup(dir->d_name);
    return 0;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_LSTAT */
}

static int file_close_dir(URLContext *h)
{
#if HAVE_LSTAT
    FileContext *c = h->priv_data;
    closedir(c->dir);
    return 0;
#else
    return AVERROR(ENOSYS);
#endif /* HAVE_LSTAT */
}

const URLProtocol ff_file_protocol = {
    .name                = "file",
    .url_open            = file_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_seek            = file_seek,
    .url_close           = file_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = file_check,
    .url_delete          = file_delete,
    .url_move            = file_move,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &file_class,
    .url_open_dir        = file_open_dir,
    .url_read_dir        = file_read_dir,
    .url_close_dir       = file_close_dir,
    .default_whitelist   = "file,crypto,data"
};

#endif /* CONFIG_FILE_PROTOCOL */

#if CONFIG_PIPE_PROTOCOL

static int pipe_open(URLContext *h, const char *filename, int flags)
{
    FileContext *c = h->priv_data;
    int fd;
    char *final;

    if (c->fd < 0) {
        av_strstart(filename, "pipe:", &filename);

        if (!*filename) {
            if (flags & AVIO_FLAG_WRITE) {
                fd = 1;
            } else {
                fd = 0;
            }
        } else {
            fd = strtol(filename, &final, 10);
            if (*final) /* No digits found, or something like 10ab */
                return AVERROR(EINVAL);
        }
        c->fd = fd;
    }

    c->fd = fd_dup(h, c->fd);
    if (c->fd == -1)
        return AVERROR(errno);
    h->is_streamed = 1;
    return 0;
}

const URLProtocol ff_pipe_protocol = {
    .name                = "pipe",
    .url_open            = pipe_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_close           = file_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = file_check,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &pipe_class,
    .default_whitelist   = "crypto,data"
};

#endif /* CONFIG_PIPE_PROTOCOL */

#if CONFIG_FD_PROTOCOL

static int fd_open(URLContext *h, const char *filename, int flags)
{
    FileContext *c = h->priv_data;
    struct stat st;

    if (strcmp(filename, "fd:") != 0) {
        av_log(h, AV_LOG_ERROR, "Doesn't support pass file descriptor via URL,"
                                " please set it via -fd {num}\n");
        return AVERROR(EINVAL);
    }

    if (c->fd < 0) {
        if (flags & AVIO_FLAG_WRITE) {
            c->fd = 1;
        } else {
            c->fd = 0;
        }
    }
    if (fstat(c->fd, &st) < 0)
        return AVERROR(errno);
    h->is_streamed = !(S_ISREG(st.st_mode) || S_ISBLK(st.st_mode));
    c->fd = fd_dup(h, c->fd);
    if (c->fd == -1)
        return AVERROR(errno);

    return 0;
}

const URLProtocol ff_fd_protocol = {
    .name                = "fd",
    .url_open            = fd_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_seek            = file_seek,
    .url_close           = file_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = file_check,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &fd_class,
    .default_whitelist   = "crypto,data"
};

#endif /* CONFIG_FD_PROTOCOL */

#if CONFIG_ANDROID_CONTENT_PROTOCOL
#include <jni.h>
#include "libavcodec/ffjni.h"
#include "libavcodec/jni.h"

typedef struct JFields {
    jclass uri_class;
    jmethodID parse_id;

    jclass context_class;
    jmethodID get_content_resolver_id;

    jclass content_resolver_class;
    jmethodID open_file_descriptor_id;

    jclass parcel_file_descriptor_class;
    jmethodID detach_fd_id;
} JFields;

#define OFFSET(x) offsetof(JFields, x)
static const struct FFJniField jfields_mapping[] = {
    { "android/net/Uri", NULL, NULL, FF_JNI_CLASS, OFFSET(uri_class), 1 },
        { "android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;", FF_JNI_STATIC_METHOD, OFFSET(parse_id), 1 },

    { "android/content/Context", NULL, NULL, FF_JNI_CLASS, OFFSET(context_class), 1 },
        { "android/content/Context", "getContentResolver", "()Landroid/content/ContentResolver;", FF_JNI_METHOD, OFFSET(get_content_resolver_id), 1 },

    { "android/content/ContentResolver", NULL, NULL, FF_JNI_CLASS, OFFSET(content_resolver_class), 1 },
        { "android/content/ContentResolver", "openFileDescriptor", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;", FF_JNI_METHOD, OFFSET(open_file_descriptor_id), 1 },

    { "android/os/ParcelFileDescriptor", NULL, NULL, FF_JNI_CLASS, OFFSET(parcel_file_descriptor_class), 1 },
        { "android/os/ParcelFileDescriptor", "detachFd", "()I", FF_JNI_METHOD, OFFSET(detach_fd_id), 1 },

    { NULL }
};
#undef OFFSET

static int android_content_open(URLContext *h, const char *filename, int flags)
{
    FileContext *c = h->priv_data;
    int fd, ret;
    struct stat st;
    const char *mode_str = "r";

    JNIEnv *env;
    JFields jfields = { 0 };
    jobject application_context = NULL;
    jobject url = NULL;
    jobject mode = NULL;
    jobject uri = NULL;
    jobject content_resolver = NULL;
    jobject parcel_file_descriptor = NULL;

    env = ff_jni_get_env(c);
    if (!env) {
        return AVERROR(EINVAL);
    }

    ret = ff_jni_init_jfields(env, &jfields, jfields_mapping, 0, c);
    if (ret < 0) {
        av_log(c, AV_LOG_ERROR, "failed to initialize jni fields\n");
        return ret;
    }

    application_context = av_jni_get_android_app_ctx();
    if (!application_context) {
        av_log(c, AV_LOG_ERROR, "application context is not set\n");
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    url = ff_jni_utf_chars_to_jstring(env, filename, c);
    if (!url) {
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    if (flags & AVIO_FLAG_WRITE && flags & AVIO_FLAG_READ)
        mode_str = "rw";
    else if (flags & AVIO_FLAG_WRITE)
        mode_str = "w";

    mode = ff_jni_utf_chars_to_jstring(env, mode_str, c);
    if (!mode) {
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    uri = (*env)->CallStaticObjectMethod(env, jfields.uri_class, jfields.parse_id, url);
    ret = ff_jni_exception_check(env, 1, c);
    if (ret < 0)
        goto done;

    content_resolver = (*env)->CallObjectMethod(env, application_context, jfields.get_content_resolver_id);
    ret = ff_jni_exception_check(env, 1, c);
    if (ret < 0)
        goto done;

    parcel_file_descriptor = (*env)->CallObjectMethod(env, content_resolver, jfields.open_file_descriptor_id, uri, mode);
    ret = ff_jni_exception_check(env, 1, c);
    if (ret < 0)
        goto done;

    fd = (*env)->CallIntMethod(env, parcel_file_descriptor, jfields.detach_fd_id);
    ret = ff_jni_exception_check(env, 1, c);
    if (ret < 0)
        goto done;

    if (fstat(fd, &st) < 0) {
        close(fd);
        return AVERROR(errno);
    }

    c->fd = fd;
    h->is_streamed = !(S_ISREG(st.st_mode) || S_ISBLK(st.st_mode));

done:
    (*env)->DeleteLocalRef(env, url);
    (*env)->DeleteLocalRef(env, mode);
    (*env)->DeleteLocalRef(env, uri);
    (*env)->DeleteLocalRef(env, content_resolver);
    (*env)->DeleteLocalRef(env, parcel_file_descriptor);
    ff_jni_reset_jfields(env, &jfields, jfields_mapping, 0, c);

    return ret;
}

static const AVOption android_content_options[] = {
    { "blocksize", "set I/O operation maximum block size", offsetof(FileContext, blocksize), AV_OPT_TYPE_INT, { .i64 = INT_MAX }, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass android_content_class = {
    .class_name = "android_content",
    .item_name  = av_default_item_name,
    .option     = android_content_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_android_content_protocol = {
    .name                = "content",
    .url_open            = android_content_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_seek            = file_seek,
    .url_close           = file_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = NULL,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &android_content_class,
};

#endif /* CONFIG_ANDROID_CONTENT_PROTOCOL */
