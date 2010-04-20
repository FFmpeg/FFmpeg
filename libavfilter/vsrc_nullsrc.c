/*
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
 * null video source
 */

#include "avfilter.h"

typedef struct {
    int w, h;
} NullContext;

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    NullContext *priv = ctx->priv;

    priv->w = 352;
    priv->h = 288;

    if (args)
        sscanf(args, "%d:%d", &priv->w, &priv->h);

    if (priv->w <= 0 || priv->h <= 0) {
        av_log(ctx, AV_LOG_ERROR, "Non-positive size values are not acceptable.\n");
        return -1;
    }

    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    NullContext *priv = outlink->src->priv;

    outlink->w = priv->w;
    outlink->h = priv->h;

    av_log(outlink->src, AV_LOG_INFO, "w:%d h:%d\n", priv->w, priv->h);

    return 0;
}

static int request_frame(AVFilterLink *link)
{
    return -1;
}

AVFilter avfilter_vsrc_nullsrc = {
    .name        = "nullsrc",
    .description = "Null video source, never return images.",

    .init       = init,
    .priv_size = sizeof(NullContext),

    .inputs    = (AVFilterPad[]) {{ .name = NULL}},

    .outputs   = (AVFilterPad[]) {
        {
            .name            = "default",
            .type            = AVMEDIA_TYPE_VIDEO,
            .config_props    = config_props,
            .request_frame   = request_frame,
        },
        { .name = NULL}
    },
};
