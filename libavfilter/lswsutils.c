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

#include "libavutil/imgutils.h"
#include "lswsutils.h"

int ff_scale_image(uint8_t *dst_data[4], int dst_linesize[4],
                   int dst_w, int dst_h, enum PixelFormat dst_pix_fmt,
                   uint8_t * const src_data[4], int src_linesize[4],
                   int src_w, int src_h, enum PixelFormat src_pix_fmt,
                   void *log_ctx)
{
    int ret;
    struct SwsContext *sws_ctx = sws_getContext(src_w, src_h, src_pix_fmt,
                                                dst_w, dst_h, dst_pix_fmt,
                                                SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Impossible to create scale context for the conversion "
               "fmt:%s s:%dx%d -> fmt:%s s:%dx%d\n",
               av_get_pix_fmt_name(src_pix_fmt), src_w, src_h,
               av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if ((ret = av_image_alloc(dst_data, dst_linesize, dst_w, dst_h, dst_pix_fmt, 16)) < 0)
        goto end;
    ret = 0;
    sws_scale(sws_ctx, (const uint8_t * const*)src_data, src_linesize, 0, src_h, dst_data, dst_linesize);

end:
    sws_freeContext(sws_ctx);
    return ret;
}
