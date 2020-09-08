/*
 * Copyright (c) 2016 Thilo Borgmann
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
 * Video processing based on Apple's CoreImage API
 */

#import <CoreImage/CoreImage.h>
#import <AppKit/AppKit.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

typedef struct CoreImageContext {
    const AVClass   *class;

    int             is_video_source;    ///< filter is used as video source

    int             w, h;               ///< video size
    AVRational      sar;                ///< sample aspect ratio
    AVRational      frame_rate;         ///< video frame rate
    AVRational      time_base;          ///< stream time base
    int64_t         duration;           ///< duration expressed in microseconds
    int64_t         pts;                ///< increasing presentation time stamp
    AVFrame         *picref;            ///< cached reference containing the painted picture

    CFTypeRef       glctx;              ///< OpenGL context
    CGContextRef    cgctx;              ///< Bitmap context for image copy
    CFTypeRef       input_image;        ///< Input image container for passing into Core Image API
    CGColorSpaceRef color_space;        ///< Common color space for input image and cgcontext
    int             bits_per_component; ///< Shared bpc for input-output operation

    char            *filter_string;     ///< The complete user provided filter definition
    CFTypeRef       *filters;           ///< CIFilter object for all requested filters
    int             num_filters;        ///< Amount of filters in *filters

    char            *output_rect;       ///< Rectangle to be filled with filter intput
    int             list_filters;       ///< Option used to list all available filters including generators
    int             list_generators;    ///< Option used to list all available generators
} CoreImageContext;

static int config_output(AVFilterLink *link)
{
    CoreImageContext *ctx = link->src->priv;

    link->w                   = ctx->w;
    link->h                   = ctx->h;
    link->sample_aspect_ratio = ctx->sar;
    link->frame_rate          = ctx->frame_rate;
    link->time_base           = ctx->time_base;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    ctx->bits_per_component        = av_get_bits_per_pixel(desc) / desc->nb_components;

    return 0;
}

/** Determine image properties from input link of filter chain.
 */
static int config_input(AVFilterLink *link)
{
    CoreImageContext *ctx          = link->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    ctx->bits_per_component        = av_get_bits_per_pixel(desc) / desc->nb_components;

    return 0;
}

/** Print a list of all available filters including options and respective value ranges and defaults.
 */
static void list_filters(CoreImageContext *ctx)
{
    // querying filters and attributes
    NSArray *filter_categories = nil;

    if (ctx->list_generators && !ctx->list_filters) {
        filter_categories = [NSArray arrayWithObjects:kCICategoryGenerator, nil];
    }

    NSArray *filter_names = [CIFilter filterNamesInCategories:filter_categories];
    NSEnumerator *filters = [filter_names objectEnumerator];

    NSString *filter_name;
    while (filter_name = [filters nextObject]) {
        av_log(ctx, AV_LOG_INFO, "Filter: %s\n", [filter_name UTF8String]);
        NSString *input;

        CIFilter *filter             = [CIFilter filterWithName:filter_name];
        NSDictionary *filter_attribs = [filter attributes]; // <nsstring, id>
        NSArray      *filter_inputs  = [filter inputKeys];  // <nsstring>

        for (input in filter_inputs) {
            NSDictionary *input_attribs = [filter_attribs valueForKey:input];
            NSString *input_class       = [input_attribs valueForKey:kCIAttributeClass];
            if ([input_class isEqualToString:@"NSNumber"]) {
                NSNumber *value_default = [input_attribs valueForKey:kCIAttributeDefault];
                NSNumber *value_min     = [input_attribs valueForKey:kCIAttributeSliderMin];
                NSNumber *value_max     = [input_attribs valueForKey:kCIAttributeSliderMax];

                av_log(ctx, AV_LOG_INFO, "\tOption: %s\t[%s]\t[%s %s][%s]\n",
                    [input UTF8String],
                    [input_class UTF8String],
                    [[value_min stringValue] UTF8String],
                    [[value_max stringValue] UTF8String],
                    [[value_default stringValue] UTF8String]);
            } else {
                av_log(ctx, AV_LOG_INFO, "\tOption: %s\t[%s]\n",
                    [input UTF8String],
                    [input_class UTF8String]);
            }
        }
    }
}

static int query_formats(AVFilterContext *fctx)
{
    static const enum AVPixelFormat inout_fmts_rgb[] = {
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *inout_formats;
    int ret;

    if (!(inout_formats = ff_make_format_list(inout_fmts_rgb))) {
        return AVERROR(ENOMEM);
    }

    if ((ret = ff_formats_ref(inout_formats, &fctx->inputs[0]->outcfg.formats)) < 0 ||
        (ret = ff_formats_ref(inout_formats, &fctx->outputs[0]->incfg.formats)) < 0) {
        return ret;
    }

    return 0;
}

static int query_formats_src(AVFilterContext *fctx)
{
    static const enum AVPixelFormat inout_fmts_rgb[] = {
        AV_PIX_FMT_ARGB,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *inout_formats;
    int ret;

    if (!(inout_formats = ff_make_format_list(inout_fmts_rgb))) {
        return AVERROR(ENOMEM);
    }

    if ((ret = ff_formats_ref(inout_formats, &fctx->outputs[0]->incfg.formats)) < 0) {
        return ret;
    }

    return 0;
}

static int apply_filter(CoreImageContext *ctx, AVFilterLink *link, AVFrame *frame)
{
    int i;

    // (re-)initialize input image
    const CGSize frame_size = {
        frame->width,
        frame->height
    };

    NSData *data = [NSData dataWithBytesNoCopy:frame->data[0]
                           length:frame->height*frame->linesize[0]
                           freeWhenDone:NO];

    CIImage *ret = [(__bridge CIImage*)ctx->input_image initWithBitmapData:data
                                                        bytesPerRow:frame->linesize[0]
                                                        size:frame_size
                                                        format:kCIFormatARGB8
                                                        colorSpace:ctx->color_space]; //kCGColorSpaceGenericRGB
    if (!ret) {
        av_log(ctx, AV_LOG_ERROR, "Input image could not be initialized.\n");
        return AVERROR_EXTERNAL;
    }

    CIFilter *filter       = NULL;
    CIImage *filter_input  = (__bridge CIImage*)ctx->input_image;
    CIImage *filter_output = NULL;

    // successively apply all filters
    for (i = 0; i < ctx->num_filters; i++) {
        if (i) {
            // set filter input to previous filter output
            filter_input    = [(__bridge CIImage*)ctx->filters[i-1] valueForKey:kCIOutputImageKey];
            CGRect out_rect = [filter_input extent];
            if (out_rect.size.width > frame->width || out_rect.size.height > frame->height) {
                // do not keep padded image regions after filtering
                out_rect.origin.x    = 0.0f;
                out_rect.origin.y    = 0.0f;
                out_rect.size.width  = frame->width;
                out_rect.size.height = frame->height;
            }
            filter_input = [filter_input imageByCroppingToRect:out_rect];
        }

        filter = (__bridge CIFilter*)ctx->filters[i];

        // do not set input image for the first filter if used as video source
        if (!ctx->is_video_source || i) {
            @try {
                [filter setValue:filter_input forKey:kCIInputImageKey];
            } @catch (NSException *exception) {
                if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
                    av_log(ctx, AV_LOG_ERROR, "An error occurred: %s.", [exception.reason UTF8String]);
                    return AVERROR_EXTERNAL;
                } else {
                    av_log(ctx, AV_LOG_WARNING, "Selected filter does not accept an input image.\n");
                }
            }
        }
    }

    // get output of last filter
    filter_output = [filter valueForKey:kCIOutputImageKey];

    if (!filter_output) {
        av_log(ctx, AV_LOG_ERROR, "Filter output not available.\n");
        return AVERROR_EXTERNAL;
    }

    // do not keep padded image regions after filtering
    CGRect out_rect = [filter_output extent];
    if (out_rect.size.width > frame->width || out_rect.size.height > frame->height) {
        av_log(ctx, AV_LOG_DEBUG, "Cropping output image.\n");
        out_rect.origin.x    = 0.0f;
        out_rect.origin.y    = 0.0f;
        out_rect.size.width  = frame->width;
        out_rect.size.height = frame->height;
    }

    CGImageRef out = [(__bridge CIContext*)ctx->glctx createCGImage:filter_output
                                                      fromRect:out_rect];

    if (!out) {
        av_log(ctx, AV_LOG_ERROR, "Cannot create valid output image.\n");
    }

    // create bitmap context on the fly for rendering into current frame->data[]
    if (ctx->cgctx) {
        CGContextRelease(ctx->cgctx);
        ctx->cgctx = NULL;
    }
    size_t out_width  = CGImageGetWidth(out);
    size_t out_height = CGImageGetHeight(out);

    if (out_width > frame->width || out_height > frame->height) { // this might result in segfault
        av_log(ctx, AV_LOG_WARNING, "Output image has unexpected size: %lux%lu (expected: %ix%i). This may crash...\n",
               out_width, out_height, frame->width, frame->height);
    }
    ctx->cgctx = CGBitmapContextCreate(frame->data[0],
                                       frame->width,
                                       frame->height,
                                       ctx->bits_per_component,
                                       frame->linesize[0],
                                       ctx->color_space,
                                       (uint32_t)kCGImageAlphaPremultipliedFirst); // ARGB
    if (!ctx->cgctx) {
        av_log(ctx, AV_LOG_ERROR, "CGBitmap context cannot be created.\n");
        return AVERROR_EXTERNAL;
    }

    // copy ("draw") the output image into the frame data
    CGRect rect = {{0,0},{frame->width, frame->height}};
    if (ctx->output_rect) {
        @try {
            NSString *tmp_string = [NSString stringWithUTF8String:ctx->output_rect];
            NSRect tmp           = NSRectFromString(tmp_string);
            rect                 = NSRectToCGRect(tmp);
        } @catch (NSException *exception) {
            av_log(ctx, AV_LOG_ERROR, "An error occurred: %s.", [exception.reason UTF8String]);
            return AVERROR_EXTERNAL;
        }
        if (rect.size.width == 0.0f) {
            av_log(ctx, AV_LOG_WARNING, "Width of output rect is zero.\n");
        }
        if (rect.size.height == 0.0f) {
            av_log(ctx, AV_LOG_WARNING, "Height of output rect is zero.\n");
        }
    }

    CGContextDrawImage(ctx->cgctx, rect, out);

    return ff_filter_frame(link, frame);
}

/** Apply all valid filters successively to the input image.
 *  The final output image is copied from the GPU by "drawing" using a bitmap context.
 */
static int filter_frame(AVFilterLink *link, AVFrame *frame)
{
    return apply_filter(link->dst->priv, link->dst->outputs[0], frame);
}

static int request_frame(AVFilterLink *link)
{
    CoreImageContext *ctx = link->src->priv;
    AVFrame *frame;

    if (ctx->duration >= 0 &&
        av_rescale_q(ctx->pts, ctx->time_base, AV_TIME_BASE_Q) >= ctx->duration) {
        return AVERROR_EOF;
    }

    if (!ctx->picref) {
        ctx->picref = ff_get_video_buffer(link, ctx->w, ctx->h);
        if (!ctx->picref) {
            return AVERROR(ENOMEM);
        }
    }

    frame = av_frame_clone(ctx->picref);
    if (!frame) {
        return AVERROR(ENOMEM);
    }

    frame->pts                 = ctx->pts;
    frame->key_frame           = 1;
    frame->interlaced_frame    = 0;
    frame->pict_type           = AV_PICTURE_TYPE_I;
    frame->sample_aspect_ratio = ctx->sar;

    ctx->pts++;

    return apply_filter(ctx, link, frame);
}

/** Set an option of the given filter to the provided key-value pair.
 */
static void set_option(CoreImageContext *ctx, CIFilter *filter, const char *key, const char *value)
{
        NSString *input_key = [NSString stringWithUTF8String:key];
        NSString *input_val = [NSString stringWithUTF8String:value];

        NSDictionary *filter_attribs = [filter attributes]; // <nsstring, id>
        NSDictionary *input_attribs  = [filter_attribs valueForKey:input_key];

        NSString *input_class = [input_attribs valueForKey:kCIAttributeClass];
        NSString *input_type  = [input_attribs valueForKey:kCIAttributeType];

        if (!input_attribs) {
            av_log(ctx, AV_LOG_WARNING, "Skipping unknown option: \"%s\".\n",
                   [input_key UTF8String]); // [[filter name] UTF8String]) not currently defined...
            return;
        }

        av_log(ctx, AV_LOG_DEBUG, "key: %s, val: %s, #attribs: %lu, class: %s, type: %s\n",
               [input_key UTF8String],
               [input_val UTF8String],
               input_attribs ? (unsigned long)[input_attribs count] : -1,
               [input_class UTF8String],
               [input_type UTF8String]);

        if ([input_class isEqualToString:@"NSNumber"]) {
            float input          = input_val.floatValue;
            NSNumber *max_value  = [input_attribs valueForKey:kCIAttributeSliderMax];
            NSNumber *min_value  = [input_attribs valueForKey:kCIAttributeSliderMin];
            NSNumber *used_value = nil;

#define CLAMP_WARNING do {     \
av_log(ctx, AV_LOG_WARNING, "Value of \"%f\" for option \"%s\" is out of range [%f %f], clamping to \"%f\".\n", \
       input,                  \
       [input_key UTF8String], \
       min_value.floatValue,   \
       max_value.floatValue,   \
       used_value.floatValue); \
} while(0)
            if (input > max_value.floatValue) {
                used_value = max_value;
                CLAMP_WARNING;
            } else if (input < min_value.floatValue) {
                used_value = min_value;
                CLAMP_WARNING;
            } else {
                used_value = [NSNumber numberWithFloat:input];
            }

            [filter setValue:used_value forKey:input_key];
        } else if ([input_class isEqualToString:@"CIVector"]) {
            CIVector *input = [CIVector vectorWithString:input_val];

            if (!input) {
                av_log(ctx, AV_LOG_WARNING, "Skipping invalid CIVctor description: \"%s\".\n",
                       [input_val UTF8String]);
                return;
            }

            [filter setValue:input forKey:input_key];
        } else if ([input_class isEqualToString:@"CIColor"]) {
            CIColor *input = [CIColor colorWithString:input_val];

            if (!input) {
                av_log(ctx, AV_LOG_WARNING, "Skipping invalid CIColor description: \"%s\".\n",
                       [input_val UTF8String]);
                return;
            }

            [filter setValue:input forKey:input_key];
        } else if ([input_class isEqualToString:@"NSString"]) { // set display name as string with latin1 encoding
            [filter setValue:input_val forKey:input_key];
        } else if ([input_class isEqualToString:@"NSData"]) { // set display name as string with latin1 encoding
            NSData *input = [NSData dataWithBytes:(const void*)[input_val cStringUsingEncoding:NSISOLatin1StringEncoding]
                                    length:[input_val lengthOfBytesUsingEncoding:NSISOLatin1StringEncoding]];

            if (!input) {
                av_log(ctx, AV_LOG_WARNING, "Skipping invalid NSData description: \"%s\".\n",
                       [input_val UTF8String]);
                return;
            }

            [filter setValue:input forKey:input_key];
        } else {
            av_log(ctx, AV_LOG_WARNING, "Skipping unsupported option class: \"%s\".\n",
                   [input_class UTF8String]);
            avpriv_report_missing_feature(ctx, "Handling of some option classes");
            return;
        }
}

/** Create a filter object by a given name and set all options to defaults.
 *  Overwrite any option given by the user to the provided value in filter_options.
 */
static CIFilter* create_filter(CoreImageContext *ctx, const char *filter_name, AVDictionary *filter_options)
{
    // create filter object
    CIFilter *filter = [CIFilter filterWithName:[NSString stringWithUTF8String:filter_name]];

    // set default options
    [filter setDefaults];

    // set user options
    if (filter_options) {
        AVDictionaryEntry *o = NULL;
        while ((o = av_dict_get(filter_options, "", o, AV_DICT_IGNORE_SUFFIX))) {
            set_option(ctx, filter, o->key, o->value);
        }
    }

    return filter;
}

static av_cold int init(AVFilterContext *fctx)
{
    CoreImageContext *ctx     = fctx->priv;
    AVDictionary *filter_dict = NULL;
    AVDictionaryEntry *f      = NULL;
    AVDictionaryEntry *o      = NULL;
    int ret;
    int i;

    if (ctx->list_filters || ctx->list_generators) {
        list_filters(ctx);
        return AVERROR_EXIT;
    }

    if (ctx->filter_string) {
        // parse filter string (filter=name@opt=val@opt2=val2#name2@opt3=val3) for filters separated by #
        av_log(ctx, AV_LOG_DEBUG, "Filter_string: %s\n", ctx->filter_string);
        ret = av_dict_parse_string(&filter_dict, ctx->filter_string, "@", "#", AV_DICT_MULTIKEY); // parse filter_name:all_filter_options
        if (ret) {
            av_dict_free(&filter_dict);
            av_log(ctx, AV_LOG_ERROR, "Parsing of filters failed.\n");
            return AVERROR(EIO);
        }
        ctx->num_filters = av_dict_count(filter_dict);
        av_log(ctx, AV_LOG_DEBUG, "Filter count: %i\n", ctx->num_filters);

        // allocate CIFilter array
        ctx->filters = av_mallocz_array(ctx->num_filters, sizeof(CIFilter*));
        if (!ctx->filters) {
            av_log(ctx, AV_LOG_ERROR, "Could not allocate filter array.\n");
            return AVERROR(ENOMEM);
        }

        // parse filters for option key-value pairs (opt=val@opt2=val2) separated by @
        i = 0;
        while ((f = av_dict_get(filter_dict, "", f, AV_DICT_IGNORE_SUFFIX))) {
            AVDictionary *filter_options = NULL;

            if (strncmp(f->value, "default", 7)) { // not default
                ret = av_dict_parse_string(&filter_options, f->value, "=", "@", 0); // parse option_name:option_value
                if (ret) {
                    av_dict_free(&filter_options);
                    av_log(ctx, AV_LOG_ERROR, "Parsing of filter options for \"%s\" failed.\n", f->key);
                    return AVERROR(EIO);
                }
            }

            if (av_log_get_level() >= AV_LOG_DEBUG) {
                av_log(ctx, AV_LOG_DEBUG, "Creating filter %i: \"%s\":\n", i, f->key);
                if (!filter_options) {
                    av_log(ctx, AV_LOG_DEBUG, "\tusing default options\n");
                } else {
                    while ((o = av_dict_get(filter_options, "", o, AV_DICT_IGNORE_SUFFIX))) {
                        av_log(ctx, AV_LOG_DEBUG, "\t%s: %s\n", o->key, o->value);
                    }
                }
            }

            ctx->filters[i] = CFBridgingRetain(create_filter(ctx, f->key, filter_options));
            if (!ctx->filters[i]) {
                av_log(ctx, AV_LOG_ERROR, "Could not create filter \"%s\".\n", f->key);
                return AVERROR(EINVAL);
            }

            i++;
        }
    } else {
        av_log(ctx, AV_LOG_ERROR, "No filters specified.\n");
        return AVERROR(EINVAL);
    }

    // create GPU context on OSX
    const NSOpenGLPixelFormatAttribute attr[] = {
        NSOpenGLPFAAccelerated,
        NSOpenGLPFANoRecovery,
        NSOpenGLPFAColorSize, 32,
        0
    };

    NSOpenGLPixelFormat *pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:(void *)&attr];
    ctx->color_space                  = CGColorSpaceCreateWithName(kCGColorSpaceGenericRGB);
    ctx->glctx                        = CFBridgingRetain([CIContext contextWithCGLContext:CGLGetCurrentContext()
                                                         pixelFormat:[pixel_format CGLPixelFormatObj]
                                                         colorSpace:ctx->color_space
                                                         options:nil]);

    if (!ctx->glctx) {
        av_log(ctx, AV_LOG_ERROR, "CIContext not created.\n");
        return AVERROR_EXTERNAL;
    }

    // Creating an empty input image as input container for the context
    ctx->input_image = CFBridgingRetain([CIImage emptyImage]);

    return 0;
}

static av_cold int init_src(AVFilterContext *fctx)
{
    CoreImageContext *ctx = fctx->priv;

    ctx->is_video_source = 1;
    ctx->time_base       = av_inv_q(ctx->frame_rate);
    ctx->pts             = 0;

    return init(fctx);
}

static av_cold void uninit(AVFilterContext *fctx)
{
#define SafeCFRelease(ptr) do { \
    if (ptr) {                  \
        CFRelease(ptr);         \
        ptr = NULL;             \
    }                           \
} while (0)

    CoreImageContext *ctx = fctx->priv;

    SafeCFRelease(ctx->glctx);
    SafeCFRelease(ctx->cgctx);
    SafeCFRelease(ctx->color_space);
    SafeCFRelease(ctx->input_image);

    if (ctx->filters) {
        for (int i = 0; i < ctx->num_filters; i++) {
            SafeCFRelease(ctx->filters[i]);
        }
        av_freep(&ctx->filters);
    }

    av_frame_free(&ctx->picref);
}

static const AVFilterPad vf_coreimage_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad vf_coreimage_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

static const AVFilterPad vsrc_coreimagesrc_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_output,
    },
    { NULL }
};

#define OFFSET(x) offsetof(CoreImageContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

#define GENERATOR_OPTIONS                                                                                                               \
    {"size",     "set video size",                OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0,         FLAGS}, \
    {"s",        "set video size",                OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str = "320x240"}, 0, 0,         FLAGS}, \
    {"rate",     "set video rate",                OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"},      0, INT_MAX,         FLAGS}, \
    {"r",        "set video rate",                OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str = "25"},      0, INT_MAX,         FLAGS}, \
    {"duration", "set video duration",            OFFSET(duration),   AV_OPT_TYPE_DURATION,   {.i64 = -1},       -1, INT64_MAX, FLAGS}, \
    {"d",        "set video duration",            OFFSET(duration),   AV_OPT_TYPE_DURATION,   {.i64 = -1},       -1, INT64_MAX, FLAGS}, \
    {"sar",      "set video sample aspect ratio", OFFSET(sar),        AV_OPT_TYPE_RATIONAL,   {.dbl = 1},         0, INT_MAX,   FLAGS},

#define FILTER_OPTIONS                                                                                                                           \
    {"list_filters",    "list available filters",                OFFSET(list_filters),    AV_OPT_TYPE_BOOL,   {.i64 = 0}, 0, 1, .flags = FLAGS}, \
    {"list_generators", "list available generators",             OFFSET(list_generators), AV_OPT_TYPE_BOOL,   {.i64 = 0}, 0, 1, .flags = FLAGS}, \
    {"filter",          "names and options of filters to apply", OFFSET(filter_string),   AV_OPT_TYPE_STRING, {.str = NULL},    .flags = FLAGS}, \
    {"output_rect",     "output rectangle within output image",  OFFSET(output_rect),     AV_OPT_TYPE_STRING, {.str = NULL},    .flags = FLAGS},


// definitions for coreimage video filter
static const AVOption coreimage_options[] = {
    FILTER_OPTIONS
    { NULL }
};

AVFILTER_DEFINE_CLASS(coreimage);

AVFilter ff_vf_coreimage = {
    .name          = "coreimage",
    .description   = NULL_IF_CONFIG_SMALL("Video filtering using CoreImage API."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(CoreImageContext),
    .priv_class    = &coreimage_class,
    .inputs        = vf_coreimage_inputs,
    .outputs       = vf_coreimage_outputs,
    .query_formats = query_formats,
};

// definitions for coreimagesrc video source
static const AVOption coreimagesrc_options[] = {
    GENERATOR_OPTIONS
    FILTER_OPTIONS
    { NULL }
};

AVFILTER_DEFINE_CLASS(coreimagesrc);

AVFilter ff_vsrc_coreimagesrc = {
    .name          = "coreimagesrc",
    .description   = NULL_IF_CONFIG_SMALL("Video source using image generators of CoreImage API."),
    .init          = init_src,
    .uninit        = uninit,
    .priv_size     = sizeof(CoreImageContext),
    .priv_class    = &coreimagesrc_class,
    .inputs        = NULL,
    .outputs       = vsrc_coreimagesrc_outputs,
    .query_formats = query_formats_src,
};
