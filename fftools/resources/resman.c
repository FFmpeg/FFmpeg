/*
 * Copyright (c) 2025 - softworkz
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

/**
 * @file
 * output writers for filtergraph details
 */

#include "config.h"

#include <string.h>

#if CONFIG_RESOURCE_COMPRESSION
#include <zlib.h>
#endif

#include "resman.h"
#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"
#include "libavutil/dict.h"
#include "libavutil/common.h"

extern const unsigned char ff_graph_html_data[];
extern const unsigned int ff_graph_html_len;

extern const unsigned char ff_graph_css_data[];
extern const unsigned ff_graph_css_len;

static const FFResourceDefinition resource_definitions[] = {
    [FF_RESOURCE_GRAPH_CSS]   = { FF_RESOURCE_GRAPH_CSS,   "graph.css",   &ff_graph_css_data[0],   &ff_graph_css_len   },
    [FF_RESOURCE_GRAPH_HTML]  = { FF_RESOURCE_GRAPH_HTML,  "graph.html",  &ff_graph_html_data[0],  &ff_graph_html_len  },
};


static const AVClass resman_class = {
    .class_name = "ResourceManager",
};

typedef struct ResourceManagerContext {
    const AVClass *class;
    AVDictionary *resource_dic;
} ResourceManagerContext;

static AVMutex mutex = AV_MUTEX_INITIALIZER;

static ResourceManagerContext resman_ctx = { .class = &resman_class };


#if CONFIG_RESOURCE_COMPRESSION

static int decompress_gzip(ResourceManagerContext *ctx, uint8_t *in, unsigned in_len, char **out, size_t *out_len)
{
    z_stream strm;
    unsigned chunk = 65534;
    int ret;
    uint8_t *buf;

    *out = NULL;
    memset(&strm, 0, sizeof(strm));

    // Allocate output buffer with extra byte for null termination
    buf = (uint8_t *)av_mallocz(chunk + 1);
    if (!buf) {
        av_log(ctx, AV_LOG_ERROR, "Failed to allocate decompression buffer\n");
        return AVERROR(ENOMEM);
    }

    // 15 + 16 tells zlib to detect GZIP or zlib automatically
    ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        av_log(ctx, AV_LOG_ERROR, "Error during zlib initialization: %s\n", strm.msg);
        av_free(buf);
        return AVERROR(ENOSYS);
    }

    strm.avail_in  = in_len;
    strm.next_in   = in;
    strm.avail_out = chunk;
    strm.next_out  = buf;

    ret = inflate(&strm, Z_FINISH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
        av_log(ctx, AV_LOG_ERROR, "Inflate failed: %d, %s\n", ret, strm.msg);
        inflateEnd(&strm);
        av_free(buf);
        return (ret == Z_STREAM_END) ? Z_OK : ((ret == Z_OK) ? Z_BUF_ERROR : ret);
    }

    if (strm.avail_out == 0) {
        // TODO: Error or loop decoding?
        av_log(ctx, AV_LOG_WARNING, "Decompression buffer may be too small\n");
    }

    *out_len = chunk - strm.avail_out;
    buf[*out_len] = 0; // Ensure null termination

    inflateEnd(&strm);
    *out = (char *)buf;
    return Z_OK;
}
#endif

void ff_resman_uninit(void)
{
    ff_mutex_lock(&mutex);

    av_dict_free(&resman_ctx.resource_dic);

    ff_mutex_unlock(&mutex);
}


char *ff_resman_get_string(FFResourceId resource_id)
{
    ResourceManagerContext *ctx = &resman_ctx;
    FFResourceDefinition resource_definition = { 0 };
    AVDictionaryEntry *dic_entry;
    char *res = NULL;

    for (unsigned i = 0; i < FF_ARRAY_ELEMS(resource_definitions); ++i) {
        FFResourceDefinition def = resource_definitions[i];
        if (def.resource_id == resource_id) {
            resource_definition = def;
            break;
        }
    }

    av_assert1(resource_definition.name);

    ff_mutex_lock(&mutex);

    dic_entry = av_dict_get(ctx->resource_dic, resource_definition.name, NULL, 0);

    if (!dic_entry) {
        int dict_ret;

#if CONFIG_RESOURCE_COMPRESSION

        char *out = NULL;
        size_t out_len;

        int ret = decompress_gzip(ctx, (uint8_t *)resource_definition.data, *resource_definition.data_len, &out, &out_len);

        if (ret) {
            av_log(ctx, AV_LOG_ERROR, "Unable to decompress the resource with ID %d\n", resource_id);
            goto end;
        }

        dict_ret = av_dict_set(&ctx->resource_dic, resource_definition.name, out, 0);
        if (dict_ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to store decompressed resource in dictionary: %d\n", dict_ret);
            av_freep(&out);
            goto end;
        }

        av_freep(&out);
#else

        dict_ret = av_dict_set(&ctx->resource_dic, resource_definition.name, (const char *)resource_definition.data, 0);
        if (dict_ret < 0) {
            av_log(ctx, AV_LOG_ERROR, "Failed to store resource in dictionary: %d\n", dict_ret);
            goto end;
        }

#endif
        dic_entry = av_dict_get(ctx->resource_dic, resource_definition.name, NULL, 0);

        if (!dic_entry) {
            av_log(ctx, AV_LOG_ERROR, "Failed to retrieve resource from dictionary after storing it\n");
            goto end;
        }
    }

    res = dic_entry->value;

end:
    ff_mutex_unlock(&mutex);
    return res;
}
