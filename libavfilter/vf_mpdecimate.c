/*
 * Copyright (c) 2003 Rich Felker
 * Copyright (c) 2012 Stefano Sabatini
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file mpdecimate filter, ported from libmpcodecs/vf_decimate.c by
 * Rich Felker.
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixelutils.h"
#include "libavutil/timestamp.h"
#include "avfilter.h"
#include "internal.h"
#include "formats.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    int lo, hi;                    ///< lower and higher threshold number of differences
                                   ///< values for 8x8 blocks

    float frac;                    ///< threshold of changed pixels over the total fraction

    int max_drop_count;            ///< if positive: maximum number of sequential frames to drop
                                   ///< if negative: minimum number of frames between two drops

    int drop_count;                ///< if positive: number of frames sequentially dropped
                                   ///< if negative: number of sequential frames which were not dropped

    int packed_bpp;	           ///< width multiplier to give us all the pixels
    int hsub, vsub;                ///< chroma subsampling values
    AVFrame *ref;                  ///< reference picture
    av_pixelutils_sad_fn sad;      ///< sum of absolute difference function
} DecimateContext;

#define OFFSET(x) offsetof(DecimateContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption mpdecimate_options[] = {
    { "max",  "set the maximum number of consecutive dropped frames (positive), or the minimum interval between dropped frames (negative)",
      OFFSET(max_drop_count), AV_OPT_TYPE_INT, {.i64=0}, INT_MIN, INT_MAX, FLAGS },
    { "hi",   "set high dropping threshold", OFFSET(hi), AV_OPT_TYPE_INT, {.i64=64*12}, INT_MIN, INT_MAX, FLAGS },
    { "lo",   "set low dropping threshold", OFFSET(lo), AV_OPT_TYPE_INT, {.i64=64*5}, INT_MIN, INT_MAX, FLAGS },
    { "frac", "set fraction dropping threshold",  OFFSET(frac), AV_OPT_TYPE_FLOAT, {.dbl=0.33}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(mpdecimate);

/**
 * Return 1 if the two planes are different, 0 otherwise.
 */
static int diff_planes(AVFilterContext *ctx,
                       uint8_t *cur, int cur_linesize,
                       uint8_t *ref, int ref_linesize,
                       int w, int h)
{
    DecimateContext *decimate = ctx->priv;

    int x, y;
    int d, c = 0;
    int t = (w/16)*(h/16)*decimate->frac;

    /* compute difference for blocks of 8x8 bytes */
    for (y = 0; y < h-7; y += 4) {
        for (x = 8; x < w-7; x += 4) {
            d = decimate->sad(cur + y*cur_linesize + x, cur_linesize,
                              ref + y*ref_linesize + x, ref_linesize);
            if (d > decimate->hi) {
		av_log(ctx, AV_LOG_DEBUG, "%d>=hi ", d);
                return 1;
	    }
            if (d > decimate->lo) {
                c++;
                if (c > t) {
		    av_log(ctx, AV_LOG_DEBUG, "lo:%d>=%d ", c, t);
                    return 1;
		}
            }
        }
    }

    av_log(ctx, AV_LOG_DEBUG, "lo:%d<%d ", c, t);
    return 0;
}

/**
 * Tell if the frame should be decimated, for example if it is no much
 * different with respect to the reference frame ref.
 */
static int decimate_frame(AVFilterContext *ctx,
                          AVFrame *cur, AVFrame *ref)
{
    DecimateContext *decimate = ctx->priv;
    int plane;

    if (decimate->max_drop_count > 0 &&
        decimate->drop_count >= decimate->max_drop_count)
        return 0;
    if (decimate->max_drop_count < 0 &&
        (decimate->drop_count-1) > decimate->max_drop_count)
        return 0;

    // MPlayer's non-planar hack: just look at blocks of 8x8 bytes,
    // regardless of what they mean, since they should come from the same pixels
    // in each frame.
    //  return diff_to_drop_plane(hi,lo,frac, old->planes[0], new->planes[0],
    //        new->w*(new->bpp/8), new->h, old->stride[0], new->stride[0]);

    for (plane = 0; ref->data[plane] && ref->linesize[plane]; plane++) {
	/* use 8x8 SAD even on subsampled planes.  The blocks won't match up with
	 * luma blocks, but hopefully nobody is depending on this to catch
	 * localized chroma changes that wouldn't exceed the thresholds when
	 * diluted by using what's effectively a larger block size.
	 */
	/* also do this on packed planes */
	/* TODO: does this work right on cropped frames?
	 *  and do we need to look at AVComponentDescriptor.offset_plus1?
	 */
        int vsub = plane == 1 || plane == 2 ? decimate->vsub : 0;
        int hsub = plane == 1 || plane == 2 ? decimate->hsub : 0;
        if (diff_planes(ctx,
                        cur->data[plane], cur->linesize[plane],
                        ref->data[plane], ref->linesize[plane],
                        FF_CEIL_RSHIFT(ref->width,  hsub) * decimate->packed_bpp / 8, // bpp=8 for planar
                        FF_CEIL_RSHIFT(ref->height, vsub)))
            return 0;
    }

    return 1;
}

static av_cold int init(AVFilterContext *ctx)
{
    DecimateContext *decimate = ctx->priv;

    decimate->sad = av_pixelutils_get_sad_fn(3, 3, 0, ctx); // 8x8, not aligned on blocksize
    if (!decimate->sad)
        return AVERROR(EINVAL);

    av_log(ctx, AV_LOG_VERBOSE, "max_drop_count:%d hi:%d lo:%d frac:%f\n",
           decimate->max_drop_count, decimate->hi, decimate->lo, decimate->frac);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DecimateContext *decimate = ctx->priv;
    av_frame_free(&decimate->ref);
}


#define DECIMATE_PACKED_HACK 0

/*
 * Enable to allow operation on packed pixel values, but still using a 64byte SAD.
 * This can save copying the data twice to convert to a planar format and back,
 * and really speed things up if the hi threshold makes us bail early in the frame
 *
 * We operate as if the packed pixel data is a single plane of 8bit component values,
 * so we're actually computing SADs of all components at once in 64/bpp x 8 blocks.
 * It's probably not important for most use cases that all components of the same pixel
 * go in the same SAD block, which lets us get away with this hack for formats like
 * RGB24, not just RGB0 or RGBA.
 *
 * Operating on packed formats changes the sensitivity of the thresholds.
 * Only 1/3rd (4:4:4) or 1/2 (4:2:2) of the samples in each block will be luma, so
 * we're a lot less sensitive to changing brightness while color (U and V) stay constant.
 * This is probably less bad for RGB than for YUV.
 *
 * Anyway, this is probably best left disabled.
 *
 * We would need different SAD routines to support formats where the LSB of
 * every byte isn't the LSB of a color component.  Otherwise we could have
 * false positive duplicate identification from big changes that happen to be
 * stored in the LSB of a byte, or fail to drop dups because of small component
 * differences being stored in the high bits of the packed bytes.
 * e.g. we can't handle AV_PIX_FMT_RGB48LE, AV_PIX_FMT_RGB565LE, or
 * AV_PIX_FMT_YUV420P10LE
 */



static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P,     AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ420P,     AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVA420P,
	AV_PIX_FMT_YUVA422P_LIBAV,
	AV_PIX_FMT_YUVA444P_LIBAV,

	AV_PIX_FMT_GRAY8A,
	AV_PIX_FMT_YA8,

	AV_PIX_FMT_GBRP,      ///  ff h.264 decoder outputs this for RGB

	AV_PIX_FMT_YUVA444P,
	AV_PIX_FMT_YUVA422P,


#ifdef DECIMATE_PACKED_HACK
	AV_PIX_FMT_YVYU422,
	AV_PIX_FMT_UYVY422,     AV_PIX_FMT_UYYVYY411,
/* Would need more code for these semi-planar formats
	AV_PIX_FMT_NV12,    AV_PIX_FMT_NV21,
	AV_PIX_FMT_NV16,
*/
	AV_PIX_FMT_ARGB, AV_PIX_FMT_RGBA,
	AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA,

	AV_PIX_FMT_BGR24,        AV_PIX_FMT_RGB24,

	// TODO: check that RGB0 really means 0, rather than unused and undefined
	// Random garbage in the unused bytes would look like changes.
	AV_PIX_FMT_0RGB,
	AV_PIX_FMT_RGB0,
	AV_PIX_FMT_0BGR,
	AV_PIX_FMT_BGR0,

	// These are 8bits per component, but with different ordering on alternating lines.
	AV_PIX_FMT_BAYER_BGGR8,
	AV_PIX_FMT_BAYER_RGGB8,
	AV_PIX_FMT_BAYER_GBRG8,
	AV_PIX_FMT_BAYER_GRBG8,
#endif

// end of packed


        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DecimateContext *decimate = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(inlink->format);
    decimate->hsub = pix_desc->log2_chroma_w;
    decimate->vsub = pix_desc->log2_chroma_h;
    decimate->packed_bpp = (pix_desc->flags & AV_PIX_FMT_FLAG_PLANAR) ?
	8 : av_get_bits_per_pixel(pix_desc);

//    av_pix_fmt_get_chroma_sub_sample(inlink->format, &decimate->hsub, &decimate->vsub);

    av_assert0( !(pix_desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) );
    av_assert0( !(pix_desc->flags & AV_PIX_FMT_FLAG_PAL) );
    // We don't handle multi-byte per sample formats at all, BE or LE
    av_assert0( !(pix_desc->flags & AV_PIX_FMT_FLAG_BE) );

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *cur)
{
    DecimateContext *decimate = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int ret;

    if (decimate->ref && decimate_frame(inlink->dst, cur, decimate->ref)) {
        decimate->drop_count = FFMAX(1, decimate->drop_count+1);
    } else {
        av_frame_free(&decimate->ref);
        decimate->ref = cur;
        decimate->drop_count = FFMIN(-1, decimate->drop_count-1);

        if ((ret = ff_filter_frame(outlink, av_frame_clone(cur))) < 0)
            return ret;
    }

    av_log(inlink->dst, AV_LOG_DEBUG,
           "%s pts:%s pts_time:%s drop_count:%d\n",
           decimate->drop_count > 0 ? "drop" : "keep",
           av_ts2str(cur->pts), av_ts2timestr(cur->pts, &inlink->time_base),
           decimate->drop_count);

    if (decimate->drop_count > 0)
        av_frame_free(&cur);

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    DecimateContext *decimate = outlink->src->priv;
    AVFilterLink *inlink = outlink->src->inputs[0];
    int ret;

    do {
        ret = ff_request_frame(inlink);
    } while (decimate->drop_count > 0 && ret >= 0);

    return ret;
}

static const AVFilterPad mpdecimate_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad mpdecimate_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_vf_mpdecimate = {
    .name          = "mpdecimate",
    .description   = NULL_IF_CONFIG_SMALL("Remove near-duplicate frames."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(DecimateContext),
    .priv_class    = &mpdecimate_class,
    .query_formats = query_formats,
    .inputs        = mpdecimate_inputs,
    .outputs       = mpdecimate_outputs,
};
