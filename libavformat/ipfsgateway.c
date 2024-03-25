/*
 * IPFS and IPNS protocol support through IPFS Gateway.
 * Copyright (c) 2022 Mark Gaiser
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
#include "libavutil/file_open.h"
#include "libavutil/getenv_utf8.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include <sys/stat.h>
#include "os_support.h"
#include "url.h"

// Define the posix PATH_MAX if not there already.
// This fixes a compile issue for MSVC.
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct IPFSGatewayContext {
    AVClass *class;
    URLContext *inner;
    // Is filled by the -gateway argument and not changed after.
    char *gateway;
    // If the above gateway is non null, it will be copied into this buffer.
    // Else this buffer will contain the auto detected gateway.
    // In either case, the gateway to use will be in this buffer.
    char gateway_buffer[PATH_MAX];
} IPFSGatewayContext;

// A best-effort way to find the IPFS gateway.
// Only the most appropiate gateway is set. It's not actually requested
// (http call) to prevent a potential slowdown in startup. A potential timeout
// is handled by the HTTP protocol.
static int populate_ipfs_gateway(URLContext *h)
{
    IPFSGatewayContext *c = h->priv_data;
    char ipfs_full_data_folder[PATH_MAX];
    char ipfs_gateway_file[PATH_MAX];
    struct stat st;
    int stat_ret = 0;
    int ret = AVERROR(EINVAL);
    FILE *gateway_file = NULL;
    char *env_ipfs_gateway, *env_ipfs_path;

    // Test $IPFS_GATEWAY.
    env_ipfs_gateway = getenv_utf8("IPFS_GATEWAY");
    if (env_ipfs_gateway != NULL) {
        int printed = snprintf(c->gateway_buffer, sizeof(c->gateway_buffer),
                               "%s", env_ipfs_gateway);
        freeenv_utf8(env_ipfs_gateway);
        if (printed >= sizeof(c->gateway_buffer)) {
            av_log(h, AV_LOG_WARNING,
                   "The IPFS_GATEWAY environment variable "
                   "exceeds the maximum length. "
                   "We allow a max of %zu characters\n",
                   sizeof(c->gateway_buffer));
            ret = AVERROR(EINVAL);
            goto err;
        }

        ret = 1;
        goto err;
    } else
        av_log(h, AV_LOG_DEBUG, "$IPFS_GATEWAY is empty.\n");

    // We need to know the IPFS folder to - eventually - read the contents of
    // the "gateway" file which would tell us the gateway to use.
    env_ipfs_path = getenv_utf8("IPFS_PATH");
    if (env_ipfs_path == NULL) {
        int printed;
        char *env_home = getenv_utf8("HOME");

        av_log(h, AV_LOG_DEBUG, "$IPFS_PATH is empty.\n");

        // Try via the home folder.
        if (env_home == NULL) {
            av_log(h, AV_LOG_WARNING, "$HOME appears to be empty.\n");
            ret = AVERROR(EINVAL);
            goto err;
        }

        // Verify the composed path fits.
        printed = snprintf(
            ipfs_full_data_folder, sizeof(ipfs_full_data_folder),
            "%s/.ipfs/", env_home);
        freeenv_utf8(env_home);
        if (printed >= sizeof(ipfs_full_data_folder)) {
            av_log(h, AV_LOG_WARNING,
                   "The IPFS data path exceeds the "
                   "max path length (%zu)\n",
                   sizeof(ipfs_full_data_folder));
            ret = AVERROR(EINVAL);
            goto err;
        }

        // Stat the folder.
        // It should exist in a default IPFS setup when run as local user.
        stat_ret = stat(ipfs_full_data_folder, &st);

        if (stat_ret < 0) {
            av_log(h, AV_LOG_INFO,
                   "Unable to find IPFS folder. We tried:\n"
                   "- $IPFS_PATH, which was empty.\n"
                   "- $HOME/.ipfs (full uri: %s) which doesn't exist.\n",
                   ipfs_full_data_folder);
            ret = AVERROR(ENOENT);
            goto err;
        }
    } else {
        int printed = snprintf(
            ipfs_full_data_folder, sizeof(ipfs_full_data_folder),
            "%s", env_ipfs_path);
        freeenv_utf8(env_ipfs_path);
        if (printed >= sizeof(ipfs_full_data_folder)) {
            av_log(h, AV_LOG_WARNING,
                   "The IPFS_PATH environment variable "
                   "exceeds the maximum length. "
                   "We allow a max of %zu characters\n",
                   sizeof(c->gateway_buffer));
            ret = AVERROR(EINVAL);
            goto err;
        }
    }

    // Copy the fully composed gateway path into ipfs_gateway_file.
    if (snprintf(ipfs_gateway_file, sizeof(ipfs_gateway_file), "%sgateway",
                 ipfs_full_data_folder)
        >= sizeof(ipfs_gateway_file)) {
        av_log(h, AV_LOG_WARNING,
               "The IPFS gateway file path exceeds "
               "the max path length (%zu)\n",
               sizeof(ipfs_gateway_file));
        ret = AVERROR(ENOENT);
        goto err;
    }

    // Get the contents of the gateway file.
    gateway_file = avpriv_fopen_utf8(ipfs_gateway_file, "r");
    if (!gateway_file) {
        av_log(h, AV_LOG_WARNING,
               "The IPFS gateway file (full uri: %s) doesn't exist. "
               "Is the gateway enabled?\n",
               ipfs_gateway_file);
        ret = AVERROR(ENOENT);
        goto err;
    }

    // Read a single line (fgets stops at new line mark).
    if (!fgets(c->gateway_buffer, sizeof(c->gateway_buffer) - 1, gateway_file)) {
        av_log(h, AV_LOG_WARNING, "Unable to read from file (full uri: %s).\n",
               ipfs_gateway_file);
        ret = AVERROR(ENOENT);
        goto err;
    }

    // Replace first occurence of end of line with \0
    c->gateway_buffer[strcspn(c->gateway_buffer, "\r\n")] = 0;

    // If strlen finds anything longer then 0 characters then we have a
    // potential gateway url.
    if (*c->gateway_buffer == '\0') {
        av_log(h, AV_LOG_WARNING,
               "The IPFS gateway file (full uri: %s) appears to be empty. "
               "Is the gateway started?\n",
               ipfs_gateway_file);
        ret = AVERROR(EILSEQ);
        goto err;
    } else {
        // We're done, the c->gateway_buffer has something that looks valid.
        ret = 1;
        goto err;
    }

err:
    if (gateway_file)
        fclose(gateway_file);

    return ret;
}

static int translate_ipfs_to_http(URLContext *h, const char *uri, int flags, AVDictionary **options)
{
    const char *ipfs_cid;
    char *fulluri = NULL;
    int ret;
    IPFSGatewayContext *c = h->priv_data;

    // Test for ipfs://, ipfs:, ipns:// and ipns:. This prefix is stripped from
    // the string leaving just the CID in ipfs_cid.
    int is_ipfs = av_stristart(uri, "ipfs://", &ipfs_cid);
    int is_ipns = av_stristart(uri, "ipns://", &ipfs_cid);

    // We must have either ipns or ipfs.
    if (!is_ipfs && !is_ipns) {
        ret = AVERROR(EINVAL);
        av_log(h, AV_LOG_WARNING, "Unsupported url %s\n", uri);
        goto err;
    }

    // If the CID has a length greater then 0 then we assume we have a proper working one.
    // It could still be wrong but in that case the gateway should save us and
    // ruturn a 403 error. The http protocol handles this.
    if (strlen(ipfs_cid) < 1) {
        av_log(h, AV_LOG_WARNING, "A CID must be provided.\n");
        ret = AVERROR(EILSEQ);
        goto err;
    }

    // Populate c->gateway_buffer with whatever is in c->gateway
    if (c->gateway != NULL) {
        if (snprintf(c->gateway_buffer, sizeof(c->gateway_buffer), "%s",
                     c->gateway)
            >= sizeof(c->gateway_buffer)) {
            av_log(h, AV_LOG_WARNING,
                   "The -gateway parameter is too long. "
                   "We allow a max of %zu characters\n",
                   sizeof(c->gateway_buffer));
            ret = AVERROR(EINVAL);
            goto err;
        }
    } else {
        // Populate the IPFS gateway if we have any.
        // If not, inform the user how to properly set one.
        ret = populate_ipfs_gateway(h);

        if (ret < 1) {
            av_log(h, AV_LOG_ERROR,
                   "IPFS does not appear to be running.\n\n"
                   "Installing IPFS locally is recommended to "
                   "improve performance and reliability, "
                   "and not share all your activity with a single IPFS gateway.\n"
                   "There are multiple options to define this gateway.\n"
                   "1. Call ffmpeg with a gateway param, "
                   "without a trailing slash: -gateway <url>.\n"
                   "2. Define an $IPFS_GATEWAY environment variable with the "
                   "full HTTP URL to the gateway "
                   "without trailing forward slash.\n"
                   "3. Define an $IPFS_PATH environment variable "
                   "and point it to the IPFS data path "
                   "- this is typically ~/.ipfs\n");
            ret = AVERROR(EINVAL);
            goto err;
        }
    }

    // Test if the gateway starts with either http:// or https://
    if (av_stristart(c->gateway_buffer, "http://", NULL) == 0
        && av_stristart(c->gateway_buffer, "https://", NULL) == 0) {
        av_log(h, AV_LOG_WARNING,
               "The gateway URL didn't start with http:// or "
               "https:// and is therefore invalid.\n");
        ret = AVERROR(EILSEQ);
        goto err;
    }

    // Concatenate the url.
    // This ends up with something like: http://localhost:8080/ipfs/Qm.....
    // The format of "%s%s%s%s" is the following:
    // 1st %s = The gateway.
    // 2nd %s = If the gateway didn't end in a slash, add a "/". Otherwise it's an empty string
    // 3rd %s = Either ipns/ or ipfs/.
    // 4th %s = The IPFS CID (Qm..., bafy..., ...).
    fulluri = av_asprintf("%s%s%s%s",
                          c->gateway_buffer,
                          (c->gateway_buffer[strlen(c->gateway_buffer) - 1] == '/') ? "" : "/",
                          (is_ipns) ? "ipns/" : "ipfs/",
                          ipfs_cid);

    if (!fulluri) {
        av_log(h, AV_LOG_ERROR, "Failed to compose the URL\n");
        ret = AVERROR(ENOMEM);
        goto err;
    }

    // Pass the URL back to FFMpeg's protocol handler.
    ret = ffurl_open_whitelist(&c->inner, fulluri, flags,
                               &h->interrupt_callback, options,
                               h->protocol_whitelist,
                               h->protocol_blacklist, h);
    if (ret < 0) {
        av_log(h, AV_LOG_WARNING, "Unable to open resource: %s\n", fulluri);
        goto err;
    }

err:
    av_free(fulluri);
    return ret;
}

static int ipfs_read(URLContext *h, unsigned char *buf, int size)
{
    IPFSGatewayContext *c = h->priv_data;
    return ffurl_read(c->inner, buf, size);
}

static int64_t ipfs_seek(URLContext *h, int64_t pos, int whence)
{
    IPFSGatewayContext *c = h->priv_data;
    return ffurl_seek(c->inner, pos, whence);
}

static int ipfs_close(URLContext *h)
{
    IPFSGatewayContext *c = h->priv_data;
    return ffurl_closep(&c->inner);
}

#define OFFSET(x) offsetof(IPFSGatewayContext, x)

static const AVOption options[] = {
    {"gateway", "The gateway to ask for IPFS data.", OFFSET(gateway), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, AV_OPT_FLAG_DECODING_PARAM},
    {NULL},
};

static const AVClass ipfs_gateway_context_class = {
    .class_name     = "IPFS Gateway",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_ipfs_gateway_protocol = {
    .name               = "ipfs",
    .url_open2          = translate_ipfs_to_http,
    .url_read           = ipfs_read,
    .url_seek           = ipfs_seek,
    .url_close          = ipfs_close,
    .priv_data_size     = sizeof(IPFSGatewayContext),
    .priv_data_class    = &ipfs_gateway_context_class,
};

const URLProtocol ff_ipns_gateway_protocol = {
    .name               = "ipns",
    .url_open2          = translate_ipfs_to_http,
    .url_read           = ipfs_read,
    .url_seek           = ipfs_seek,
    .url_close          = ipfs_close,
    .priv_data_size     = sizeof(IPFSGatewayContext),
    .priv_data_class    = &ipfs_gateway_context_class,
};
