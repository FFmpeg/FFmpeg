#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avformat.h"
#include <stdio.h>
#include <stdlib.h>
#include "os_support.h"
#include "url.h"

#if CONFIG_FLVSEG_PROTOCOL

/* flv file segment protocol */

typedef struct FlvSegContext {
    const AVClass *class;
    int64_t first_name;
    int     duration;
    char    *work_dir;
    char    file_path[128];
    FILE    *pf;
    uint64_t file_counts;
} FlvSegContext;

static const AVOption flvseg_options[] = {
    { "first_name",   "first flv file name",  offsetof(FlvSegContext, first_name), AV_OPT_TYPE_INT64, { .i64 = 0 }, 0, INT64_MAX, AV_OPT_FLAG_ENCODING_PARAM },
    { "duration",     "flv segment duration", offsetof(FlvSegContext, duration),   AV_OPT_TYPE_INT,   { .i64 = 5 }, 1, INT_MAX,   AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass flvseg_class = {
    .class_name = "flvseg",
    .item_name  = av_default_item_name,
    .option     = flvseg_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static inline char *make_file_path(URLContext *h, int64_t file_name)
{
    FlvSegContext *c = h->priv_data;
    memset(c->file_path, 0, sizeof(c->file_path));
    snprintf(c->file_path, 128, "%s/%lld.flv", c->work_dir, file_name);
    return c->file_path;
}

static int flvseg_write(URLContext *h, const unsigned char *buf, int size)
{
    FlvSegContext *c = h->priv_data;
    int ret = fwrite(buf, 1, size, c->pf);
    return (ret == -1) ? AVERROR(errno) : ret;
}

static int flvseg_get_handle(URLContext *h)
{
    FlvSegContext *c = h->priv_data;
    return fileno(c->pf);
}

static int flvseg_open(URLContext *h, const char *filename, int flags)
{
    FlvSegContext *c = h->priv_data;
    char *file_path = NULL;

    if (c->duration <= 0) {
        av_log(NULL, AV_LOG_WARNING, "[flvseg] duration is <= 0 refine to 5\n");
        c->duration = 5;
    }

    // filename: protocol + work_dir
    av_strstart(filename, "flvseg:", (const char **)&c->work_dir);
    if (!c->work_dir) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] work directory is empty\n");
        return AVERROR(EINVAL);
    } else {
        av_log(NULL, AV_LOG_VERBOSE, "[flvseg] work directory is %s\n", c->work_dir);
    }

    if (c->first_name == 0) {
        int64_t fn = av_gettime() / 1000000;
        fn = fn / c->duration * c->duration;
        file_path = make_file_path(h, fn);
    } else {
        c->first_name = c->first_name / c->duration * c->duration;
        file_path = make_file_path(h, c->first_name);
    }

    c->pf = fopen(file_path, "wb");
    if (!c->pf) {
        av_log(NULL, AV_LOG_ERROR, "[flvseg] open %s error\n", file_path);
        return AVERROR(EINVAL);
    } else {
        av_log(NULL, AV_LOG_VERBOSE, "[flvseg] open %s ok\n", file_path);
    }

    return 0;
}

static int flvseg_close(URLContext *h)
{
    FlvSegContext *c = h->priv_data;
    return fclose(c->pf);
}

const URLProtocol ff_flvseg_protocol = {
    .name                = "flvseg",
    .url_open            = flvseg_open,
    .url_write           = flvseg_write,
    .url_close           = flvseg_close,
    .url_get_file_handle = flvseg_get_handle,
    .priv_data_size      = sizeof(FlvSegContext),
    .priv_data_class     = &flvseg_class,
    .default_whitelist   = "flvseg"
};

#endif  /* CONFIG_FLV_SEG_PROTOCOL */
