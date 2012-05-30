/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram
 * Copyright (c) 2003 Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * drawtext filter, based on the original vhook/drawtext.c
 * filter by Gustavo Sverzut Barbieri
 */

#include <sys/time.h>
#include <time.h>

#include "libavutil/colorspace.h"
#include "libavutil/file.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/mathematics.h"
#include "libavutil/random_seed.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/tree.h"
#include "libavutil/lfg.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "video.h"

#undef time

#include <ft2build.h>
#include <freetype/config/ftheader.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

static const char *const var_names[] = {
    "E",
    "PHI",
    "PI",
    "main_w",    "W", ///< width  of the main    video
    "main_h",    "H", ///< height of the main    video
    "text_w",    "w", ///< width  of the overlay text
    "text_h",    "h", ///< height of the overlay text
    "x",
    "y",
    "n",              ///< number of processed frames
    "t",              ///< timestamp expressed in seconds
    NULL
};

static const char *const fun2_names[] = {
    "rand"
};

static double drand(void *opaque, double min, double max)
{
    return min + (max-min) / UINT_MAX * av_lfg_get(opaque);
}

typedef double (*eval_func2)(void *, double a, double b);

static const eval_func2 fun2[] = {
    drand,
    NULL
};

enum var_name {
    VAR_E,
    VAR_PHI,
    VAR_PI,
    VAR_MAIN_W, VAR_MW,
    VAR_MAIN_H, VAR_MH,
    VAR_TEXT_W, VAR_TW,
    VAR_TEXT_H, VAR_TH,
    VAR_X,
    VAR_Y,
    VAR_N,
    VAR_T,
    VAR_VARS_NB
};

typedef struct {
    const AVClass *class;
    uint8_t *fontfile;              ///< font to be used
    uint8_t *text;                  ///< text to be drawn
    uint8_t *expanded_text;         ///< used to contain the strftime()-expanded text
    size_t   expanded_text_size;    ///< size in bytes of the expanded_text buffer
    int ft_load_flags;              ///< flags used for loading fonts, see FT_LOAD_*
    FT_Vector *positions;           ///< positions for each element in the text
    size_t nb_positions;            ///< number of elements of positions array
    char *textfile;                 ///< file with text to be drawn
    int x, y;                       ///< position to start drawing text
    int w, h;                       ///< dimension of the text block
    int shadowx, shadowy;
    unsigned int fontsize;          ///< font size to use
    char *fontcolor_string;         ///< font color as string
    char *boxcolor_string;          ///< box color as string
    char *shadowcolor_string;       ///< shadow color as string
    uint8_t fontcolor[4];           ///< foreground color
    uint8_t boxcolor[4];            ///< background color
    uint8_t shadowcolor[4];         ///< shadow color
    uint8_t fontcolor_rgba[4];      ///< foreground color in RGBA
    uint8_t boxcolor_rgba[4];       ///< background color in RGBA
    uint8_t shadowcolor_rgba[4];    ///< shadow color in RGBA

    short int draw_box;             ///< draw box around text - true or false
    int use_kerning;                ///< font kerning is used - true/false
    int tabsize;                    ///< tab size
    int fix_bounds;                 ///< do we let it go out of frame bounds - t/f

    FT_Library library;             ///< freetype font library handle
    FT_Face face;                   ///< freetype font face handle
    struct AVTreeNode *glyphs;      ///< rendered glyphs, stored using the UTF-32 char code
    int hsub, vsub;                 ///< chroma subsampling values
    int is_packed_rgb;
    int pixel_step[4];              ///< distance in bytes between the component of each pixel
    uint8_t rgba_map[4];            ///< map RGBA offsets to the positions in the packed RGBA format
    uint8_t *box_line[4];           ///< line used for filling the box background
    char   *x_expr, *y_expr;
    AVExpr *x_pexpr, *y_pexpr;      ///< parsed expressions for x and y
    double var_values[VAR_VARS_NB];
    char   *d_expr;
    AVExpr *d_pexpr;
    int draw;                       ///< set to zero to prevent drawing
    AVLFG  prng;                    ///< random
} DrawTextContext;

#define OFFSET(x) offsetof(DrawTextContext, x)

static const AVOption drawtext_options[]= {
{"fontfile", "set font file",        OFFSET(fontfile),           AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"text",     "set text",             OFFSET(text),               AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"textfile", "set text file",        OFFSET(textfile),           AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"fontcolor","set foreground color", OFFSET(fontcolor_string),   AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"boxcolor", "set box color",        OFFSET(boxcolor_string),    AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"shadowcolor", "set shadow color",  OFFSET(shadowcolor_string), AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX },
{"box",      "set box",              OFFSET(draw_box),           AV_OPT_TYPE_INT,    {.dbl=0},     0,        1        },
{"fontsize", "set font size",        OFFSET(fontsize),           AV_OPT_TYPE_INT,    {.dbl=16},    1,        72       },
{"x",        "set x",                OFFSET(x_expr),             AV_OPT_TYPE_STRING, {.str="0"},   CHAR_MIN, CHAR_MAX },
{"y",        "set y",                OFFSET(y_expr),             AV_OPT_TYPE_STRING, {.str="0"},   CHAR_MIN, CHAR_MAX },
{"shadowx",  "set x",                OFFSET(shadowx),            AV_OPT_TYPE_INT,    {.dbl=0},     INT_MIN,  INT_MAX  },
{"shadowy",  "set y",                OFFSET(shadowy),            AV_OPT_TYPE_INT,    {.dbl=0},     INT_MIN,  INT_MAX  },
{"tabsize",  "set tab size",         OFFSET(tabsize),            AV_OPT_TYPE_INT,    {.dbl=4},     0,        INT_MAX  },
{"draw",     "if false do not draw", OFFSET(d_expr),             AV_OPT_TYPE_STRING, {.str="1"},   CHAR_MIN, CHAR_MAX },
{"fix_bounds", "if true, check and fix text coords to avoid clipping",
                                     OFFSET(fix_bounds),         AV_OPT_TYPE_INT,    {.dbl=1},     0,        1        },

/* FT_LOAD_* flags */
{"ft_load_flags", "set font loading flags for libfreetype",   OFFSET(ft_load_flags),  AV_OPT_TYPE_FLAGS,  {.dbl=FT_LOAD_DEFAULT|FT_LOAD_RENDER}, 0, INT_MAX, 0, "ft_load_flags" },
{"default",                     "set default",                     0, AV_OPT_TYPE_CONST, {FT_LOAD_DEFAULT},                     INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"no_scale",                    "set no_scale",                    0, AV_OPT_TYPE_CONST, {FT_LOAD_NO_SCALE},                    INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"no_hinting",                  "set no_hinting",                  0, AV_OPT_TYPE_CONST, {FT_LOAD_NO_HINTING},                  INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"render",                      "set render",                      0, AV_OPT_TYPE_CONST, {FT_LOAD_RENDER},                      INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"no_bitmap",                   "set no_bitmap",                   0, AV_OPT_TYPE_CONST, {FT_LOAD_NO_BITMAP},                   INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"vertical_layout",             "set vertical_layout",             0, AV_OPT_TYPE_CONST, {FT_LOAD_VERTICAL_LAYOUT},             INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"force_autohint",              "set force_autohint",              0, AV_OPT_TYPE_CONST, {FT_LOAD_FORCE_AUTOHINT},              INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"crop_bitmap",                 "set crop_bitmap",                 0, AV_OPT_TYPE_CONST, {FT_LOAD_CROP_BITMAP},                 INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"pedantic",                    "set pedantic",                    0, AV_OPT_TYPE_CONST, {FT_LOAD_PEDANTIC},                    INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"ignore_global_advance_width", "set ignore_global_advance_width", 0, AV_OPT_TYPE_CONST, {FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH}, INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"no_recurse",                  "set no_recurse",                  0, AV_OPT_TYPE_CONST, {FT_LOAD_NO_RECURSE},                  INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"ignore_transform",            "set ignore_transform",            0, AV_OPT_TYPE_CONST, {FT_LOAD_IGNORE_TRANSFORM},            INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"monochrome",                  "set monochrome",                  0, AV_OPT_TYPE_CONST, {FT_LOAD_MONOCHROME},                  INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"linear_design",               "set linear_design",               0, AV_OPT_TYPE_CONST, {FT_LOAD_LINEAR_DESIGN},               INT_MIN, INT_MAX, 0, "ft_load_flags" },
{"no_autohint",                 "set no_autohint",                 0, AV_OPT_TYPE_CONST, {FT_LOAD_NO_AUTOHINT},                 INT_MIN, INT_MAX, 0, "ft_load_flags" },
{NULL},
};

static const char *drawtext_get_name(void *ctx)
{
    return "drawtext";
}

static const AVClass drawtext_class = {
    "DrawTextContext",
    drawtext_get_name,
    drawtext_options
};

#undef __FTERRORS_H__
#define FT_ERROR_START_LIST {
#define FT_ERRORDEF(e, v, s) { (e), (s) },
#define FT_ERROR_END_LIST { 0, NULL } };

struct ft_error
{
    int err;
    const char *err_msg;
} static ft_errors[] =
#include FT_ERRORS_H

#define FT_ERRMSG(e) ft_errors[e].err_msg

typedef struct {
    FT_Glyph *glyph;
    uint32_t code;
    FT_Bitmap bitmap; ///< array holding bitmaps of font
    FT_BBox bbox;
    int advance;
    int bitmap_left;
    int bitmap_top;
} Glyph;

static int glyph_cmp(void *key, const void *b)
{
    const Glyph *a = key, *bb = b;
    int64_t diff = (int64_t)a->code - (int64_t)bb->code;
    return diff > 0 ? 1 : diff < 0 ? -1 : 0;
}

/**
 * Load glyphs corresponding to the UTF-32 codepoint code.
 */
static int load_glyph(AVFilterContext *ctx, Glyph **glyph_ptr, uint32_t code)
{
    DrawTextContext *dtext = ctx->priv;
    Glyph *glyph;
    struct AVTreeNode *node = NULL;
    int ret;

    /* load glyph into dtext->face->glyph */
    if (FT_Load_Char(dtext->face, code, dtext->ft_load_flags))
        return AVERROR(EINVAL);

    /* save glyph */
    if (!(glyph = av_mallocz(sizeof(*glyph))) ||
        !(glyph->glyph = av_mallocz(sizeof(*glyph->glyph)))) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    glyph->code  = code;

    if (FT_Get_Glyph(dtext->face->glyph, glyph->glyph)) {
        ret = AVERROR(EINVAL);
        goto error;
    }

    glyph->bitmap      = dtext->face->glyph->bitmap;
    glyph->bitmap_left = dtext->face->glyph->bitmap_left;
    glyph->bitmap_top  = dtext->face->glyph->bitmap_top;
    glyph->advance     = dtext->face->glyph->advance.x >> 6;

    /* measure text height to calculate text_height (or the maximum text height) */
    FT_Glyph_Get_CBox(*glyph->glyph, ft_glyph_bbox_pixels, &glyph->bbox);

    /* cache the newly created glyph */
    if (!(node = av_mallocz(av_tree_node_size))) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    av_tree_insert(&dtext->glyphs, glyph, glyph_cmp, &node);

    if (glyph_ptr)
        *glyph_ptr = glyph;
    return 0;

error:
    if (glyph)
        av_freep(&glyph->glyph);
    av_freep(&glyph);
    av_freep(&node);
    return ret;
}

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    int err;
    DrawTextContext *dtext = ctx->priv;
    Glyph *glyph;

    dtext->class = &drawtext_class;
    av_opt_set_defaults(dtext);
    dtext->fontcolor_string = av_strdup("black");
    dtext->boxcolor_string = av_strdup("white");
    dtext->shadowcolor_string = av_strdup("black");

    if ((err = (av_set_options_string(dtext, args, "=", ":"))) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string: '%s'\n", args);
        return err;
    }

    if (!dtext->fontfile) {
        av_log(ctx, AV_LOG_ERROR, "No font filename provided\n");
        return AVERROR(EINVAL);
    }

    if (dtext->textfile) {
        uint8_t *textbuf;
        size_t textbuf_size;

        if (dtext->text) {
            av_log(ctx, AV_LOG_ERROR,
                   "Both text and text file provided. Please provide only one\n");
            return AVERROR(EINVAL);
        }
        if ((err = av_file_map(dtext->textfile, &textbuf, &textbuf_size, 0, ctx)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "The text file '%s' could not be read or is empty\n",
                   dtext->textfile);
            return err;
        }

        if (!(dtext->text = av_malloc(textbuf_size+1)))
            return AVERROR(ENOMEM);
        memcpy(dtext->text, textbuf, textbuf_size);
        dtext->text[textbuf_size] = 0;
        av_file_unmap(textbuf, textbuf_size);
    }

    if (!dtext->text) {
        av_log(ctx, AV_LOG_ERROR,
               "Either text or a valid file must be provided\n");
        return AVERROR(EINVAL);
    }

    if ((err = av_parse_color(dtext->fontcolor_rgba, dtext->fontcolor_string, -1, ctx))) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid font color '%s'\n", dtext->fontcolor_string);
        return err;
    }

    if ((err = av_parse_color(dtext->boxcolor_rgba, dtext->boxcolor_string, -1, ctx))) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid box color '%s'\n", dtext->boxcolor_string);
        return err;
    }

    if ((err = av_parse_color(dtext->shadowcolor_rgba, dtext->shadowcolor_string, -1, ctx))) {
        av_log(ctx, AV_LOG_ERROR,
               "Invalid shadow color '%s'\n", dtext->shadowcolor_string);
        return err;
    }

    if ((err = FT_Init_FreeType(&(dtext->library)))) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not load FreeType: %s\n", FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    /* load the face, and set up the encoding, which is by default UTF-8 */
    if ((err = FT_New_Face(dtext->library, dtext->fontfile, 0, &dtext->face))) {
        av_log(ctx, AV_LOG_ERROR, "Could not load fontface from file '%s': %s\n",
               dtext->fontfile, FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }
    if ((err = FT_Set_Pixel_Sizes(dtext->face, 0, dtext->fontsize))) {
        av_log(ctx, AV_LOG_ERROR, "Could not set font size to %d pixels: %s\n",
               dtext->fontsize, FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    dtext->use_kerning = FT_HAS_KERNING(dtext->face);

    /* load the fallback glyph with code 0 */
    load_glyph(ctx, NULL, 0);

    /* set the tabsize in pixels */
    if ((err = load_glyph(ctx, &glyph, ' ') < 0)) {
        av_log(ctx, AV_LOG_ERROR, "Could not set tabsize.\n");
        return err;
    }
    dtext->tabsize *= glyph->advance;

#if !HAVE_LOCALTIME_R
    av_log(ctx, AV_LOG_WARNING, "strftime() expansion unavailable!\n");
#endif

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        PIX_FMT_ARGB,    PIX_FMT_RGBA,
        PIX_FMT_ABGR,    PIX_FMT_BGRA,
        PIX_FMT_RGB24,   PIX_FMT_BGR24,
        PIX_FMT_YUV420P, PIX_FMT_YUV444P,
        PIX_FMT_YUV422P, PIX_FMT_YUV411P,
        PIX_FMT_YUV410P, PIX_FMT_YUV440P,
        PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int glyph_enu_free(void *opaque, void *elem)
{
    av_free(elem);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DrawTextContext *dtext = ctx->priv;
    int i;

    av_freep(&dtext->fontfile);
    av_freep(&dtext->text);
    av_freep(&dtext->expanded_text);
    av_freep(&dtext->fontcolor_string);
    av_freep(&dtext->boxcolor_string);
    av_freep(&dtext->positions);
    av_freep(&dtext->shadowcolor_string);
    av_tree_enumerate(dtext->glyphs, NULL, NULL, glyph_enu_free);
    av_tree_destroy(dtext->glyphs);
    dtext->glyphs = 0;
    FT_Done_Face(dtext->face);
    FT_Done_FreeType(dtext->library);

    for (i = 0; i < 4; i++) {
        av_freep(&dtext->box_line[i]);
        dtext->pixel_step[i] = 0;
    }

}

static inline int is_newline(uint32_t c)
{
    return c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int dtext_prepare_text(AVFilterContext *ctx)
{
    DrawTextContext *dtext = ctx->priv;
    uint32_t code = 0, prev_code = 0;
    int x = 0, y = 0, i = 0, ret;
    int text_height, baseline;
    char *text = dtext->text;
    uint8_t *p;
    int str_w = 0, len;
    int y_min = 32000, y_max = -32000;
    FT_Vector delta;
    Glyph *glyph = NULL, *prev_glyph = NULL;
    Glyph dummy = { 0 };
    int width  = ctx->inputs[0]->w;
    int height = ctx->inputs[0]->h;

#if HAVE_LOCALTIME_R
    time_t now = time(0);
    struct tm ltime;
    uint8_t *buf = dtext->expanded_text;
    int buf_size = dtext->expanded_text_size;

    if (!buf)
        buf_size = 2*strlen(dtext->text)+1;

    localtime_r(&now, &ltime);

    while ((buf = av_realloc(buf, buf_size))) {
        *buf = 1;
        if (strftime(buf, buf_size, dtext->text, &ltime) != 0 || *buf == 0)
            break;
        buf_size *= 2;
    }

    if (!buf)
        return AVERROR(ENOMEM);
    text = dtext->expanded_text = buf;
    dtext->expanded_text_size = buf_size;
#endif

    if ((len = strlen(text)) > dtext->nb_positions) {
        FT_Vector *p = av_realloc(dtext->positions,
                                  len * sizeof(*dtext->positions));
        if (!p) {
            av_freep(dtext->positions);
            dtext->nb_positions = 0;
            return AVERROR(ENOMEM);
        } else {
            dtext->positions = p;
            dtext->nb_positions = len;
        }
    }

    /* load and cache glyphs */
    for (i = 0, p = text; *p; i++) {
        GET_UTF8(code, *p++, continue;);

        /* get glyph */
        dummy.code = code;
        glyph = av_tree_find(dtext->glyphs, &dummy, glyph_cmp, NULL);
        if (!glyph) {
            ret = load_glyph(ctx, &glyph, code);
            if (ret)
                return ret;
        }

        y_min = FFMIN(glyph->bbox.yMin, y_min);
        y_max = FFMAX(glyph->bbox.yMax, y_max);
    }
    text_height = y_max - y_min;
    baseline    = y_max;

    /* compute and save position for each glyph */
    glyph = NULL;
    for (i = 0, p = text; *p; i++) {
        GET_UTF8(code, *p++, continue;);

        /* skip the \n in the sequence \r\n */
        if (prev_code == '\r' && code == '\n')
            continue;

        prev_code = code;
        if (is_newline(code)) {
            str_w = FFMAX(str_w, x - dtext->x);
            y += text_height;
            x = 0;
            continue;
        }

        /* get glyph */
        prev_glyph = glyph;
        dummy.code = code;
        glyph = av_tree_find(dtext->glyphs, &dummy, glyph_cmp, NULL);

        /* kerning */
        if (dtext->use_kerning && prev_glyph && glyph->code) {
            FT_Get_Kerning(dtext->face, prev_glyph->code, glyph->code,
                           ft_kerning_default, &delta);
            x += delta.x >> 6;
        }

        if (x + glyph->bbox.xMax >= width) {
            str_w = FFMAX(str_w, x);
            y += text_height;
            x = 0;
        }

        /* save position */
        dtext->positions[i].x = x + glyph->bitmap_left;
        dtext->positions[i].y = y - glyph->bitmap_top + baseline;
        if (code == '\t') x  = (x / dtext->tabsize + 1)*dtext->tabsize;
        else              x += glyph->advance;
    }

    str_w = FFMIN(width - 1, FFMAX(str_w, x));
    y     = FFMIN(y + text_height, height - 1);

    dtext->w = str_w;
    dtext->var_values[VAR_TEXT_W] = dtext->var_values[VAR_TW] = dtext->w;
    dtext->h = y;
    dtext->var_values[VAR_TEXT_H] = dtext->var_values[VAR_TH] = dtext->h;

    return 0;
}


static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    DrawTextContext *dtext = ctx->priv;
    const AVPixFmtDescriptor *pix_desc = &av_pix_fmt_descriptors[inlink->format];
    int ret;

    dtext->hsub = pix_desc->log2_chroma_w;
    dtext->vsub = pix_desc->log2_chroma_h;

    dtext->var_values[VAR_E  ] = M_E;
    dtext->var_values[VAR_PHI] = M_PHI;
    dtext->var_values[VAR_PI ] = M_PI;

    dtext->var_values[VAR_MAIN_W] =
        dtext->var_values[VAR_MW] = ctx->inputs[0]->w;
    dtext->var_values[VAR_MAIN_H] =
        dtext->var_values[VAR_MH] = ctx->inputs[0]->h;

    dtext->var_values[VAR_X] = 0;
    dtext->var_values[VAR_Y] = 0;
    dtext->var_values[VAR_N] = 0;
    dtext->var_values[VAR_T] = NAN;

    av_lfg_init(&dtext->prng, av_get_random_seed());

    if ((ret = av_expr_parse(&dtext->x_pexpr, dtext->x_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&dtext->y_pexpr, dtext->y_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&dtext->d_pexpr, dtext->d_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0)
        return AVERROR(EINVAL);

    if ((ret =
         ff_fill_line_with_color(dtext->box_line, dtext->pixel_step,
                                 inlink->w, dtext->boxcolor,
                                 inlink->format, dtext->boxcolor_rgba,
                                 &dtext->is_packed_rgb, dtext->rgba_map)) < 0)
        return ret;

    if (!dtext->is_packed_rgb) {
        uint8_t *rgba = dtext->fontcolor_rgba;
        dtext->fontcolor[0] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        dtext->fontcolor[1] = RGB_TO_U_CCIR(rgba[0], rgba[1], rgba[2], 0);
        dtext->fontcolor[2] = RGB_TO_V_CCIR(rgba[0], rgba[1], rgba[2], 0);
        dtext->fontcolor[3] = rgba[3];
        rgba = dtext->shadowcolor_rgba;
        dtext->shadowcolor[0] = RGB_TO_Y_CCIR(rgba[0], rgba[1], rgba[2]);
        dtext->shadowcolor[1] = RGB_TO_U_CCIR(rgba[0], rgba[1], rgba[2], 0);
        dtext->shadowcolor[2] = RGB_TO_V_CCIR(rgba[0], rgba[1], rgba[2], 0);
        dtext->shadowcolor[3] = rgba[3];
    }

    dtext->draw = 1;

    return dtext_prepare_text(ctx);
}

#define GET_BITMAP_VAL(r, c)                                            \
    bitmap->pixel_mode == FT_PIXEL_MODE_MONO ?                          \
        (bitmap->buffer[(r) * bitmap->pitch + ((c)>>3)] & (0x80 >> ((c)&7))) * 255 : \
         bitmap->buffer[(r) * bitmap->pitch +  (c)]

#define SET_PIXEL_YUV(picref, yuva_color, val, x, y, hsub, vsub) {           \
    luma_pos    = ((x)          ) + ((y)          ) * picref->linesize[0]; \
    alpha = yuva_color[3] * (val) * 129;                               \
    picref->data[0][luma_pos]    = (alpha * yuva_color[0] + (255*255*129 - alpha) * picref->data[0][luma_pos]   ) >> 23; \
    if (((x) & ((1<<(hsub)) - 1)) == 0 && ((y) & ((1<<(vsub)) - 1)) == 0) {\
        chroma_pos1 = ((x) >> (hsub)) + ((y) >> (vsub)) * picref->linesize[1]; \
        chroma_pos2 = ((x) >> (hsub)) + ((y) >> (vsub)) * picref->linesize[2]; \
        picref->data[1][chroma_pos1] = (alpha * yuva_color[1] + (255*255*129 - alpha) * picref->data[1][chroma_pos1]) >> 23; \
        picref->data[2][chroma_pos2] = (alpha * yuva_color[2] + (255*255*129 - alpha) * picref->data[2][chroma_pos2]) >> 23; \
    }\
}

static inline int draw_glyph_yuv(AVFilterBufferRef *picref, FT_Bitmap *bitmap, unsigned int x,
                                 unsigned int y, unsigned int width, unsigned int height,
                                 const uint8_t yuva_color[4], int hsub, int vsub)
{
    int r, c, alpha;
    unsigned int luma_pos, chroma_pos1, chroma_pos2;
    uint8_t src_val;

    for (r = 0; r < bitmap->rows && r+y < height; r++) {
        for (c = 0; c < bitmap->width && c+x < width; c++) {
            /* get intensity value in the glyph bitmap (source) */
            src_val = GET_BITMAP_VAL(r, c);
            if (!src_val)
                continue;

            SET_PIXEL_YUV(picref, yuva_color, src_val, c+x, y+r, hsub, vsub);
        }
    }

    return 0;
}

#define SET_PIXEL_RGB(picref, rgba_color, val, x, y, pixel_step, r_off, g_off, b_off, a_off) { \
    p   = picref->data[0] + (x) * pixel_step + ((y) * picref->linesize[0]); \
    alpha = rgba_color[3] * (val) * 129;                              \
    *(p+r_off) = (alpha * rgba_color[0] + (255*255*129 - alpha) * *(p+r_off)) >> 23; \
    *(p+g_off) = (alpha * rgba_color[1] + (255*255*129 - alpha) * *(p+g_off)) >> 23; \
    *(p+b_off) = (alpha * rgba_color[2] + (255*255*129 - alpha) * *(p+b_off)) >> 23; \
}

static inline int draw_glyph_rgb(AVFilterBufferRef *picref, FT_Bitmap *bitmap,
                                 unsigned int x, unsigned int y,
                                 unsigned int width, unsigned int height, int pixel_step,
                                 const uint8_t rgba_color[4], const uint8_t rgba_map[4])
{
    int r, c, alpha;
    uint8_t *p;
    uint8_t src_val;

    for (r = 0; r < bitmap->rows && r+y < height; r++) {
        for (c = 0; c < bitmap->width && c+x < width; c++) {
            /* get intensity value in the glyph bitmap (source) */
            src_val = GET_BITMAP_VAL(r, c);
            if (!src_val)
                continue;

            SET_PIXEL_RGB(picref, rgba_color, src_val, c+x, y+r, pixel_step,
                          rgba_map[0], rgba_map[1], rgba_map[2], rgba_map[3]);
        }
    }

    return 0;
}

static inline void drawbox(AVFilterBufferRef *picref, unsigned int x, unsigned int y,
                           unsigned int width, unsigned int height,
                           uint8_t *line[4], int pixel_step[4], uint8_t color[4],
                           int hsub, int vsub, int is_rgba_packed, uint8_t rgba_map[4])
{
    int i, j, alpha;

    if (color[3] != 0xFF) {
        if (is_rgba_packed) {
            uint8_t *p;
            for (j = 0; j < height; j++)
                for (i = 0; i < width; i++)
                    SET_PIXEL_RGB(picref, color, 255, i+x, y+j, pixel_step[0],
                                  rgba_map[0], rgba_map[1], rgba_map[2], rgba_map[3]);
        } else {
            unsigned int luma_pos, chroma_pos1, chroma_pos2;
            for (j = 0; j < height; j++)
                for (i = 0; i < width; i++)
                    SET_PIXEL_YUV(picref, color, 255, i+x, y+j, hsub, vsub);
        }
    } else {
        ff_draw_rectangle(picref->data, picref->linesize,
                          line, pixel_step, hsub, vsub,
                          x, y, width, height);
    }
}

static int draw_glyphs(DrawTextContext *dtext, AVFilterBufferRef *picref,
                       int width, int height, const uint8_t rgbcolor[4], const uint8_t yuvcolor[4], int x, int y)
{
    char *text = HAVE_LOCALTIME_R ? dtext->expanded_text : dtext->text;
    uint32_t code = 0;
    int i;
    uint8_t *p;
    Glyph *glyph = NULL;

    for (i = 0, p = text; *p; i++) {
        Glyph dummy = { 0 };
        GET_UTF8(code, *p++, continue;);

        /* skip new line chars, just go to new line */
        if (code == '\n' || code == '\r' || code == '\t')
            continue;

        dummy.code = code;
        glyph = av_tree_find(dtext->glyphs, &dummy, (void *)glyph_cmp, NULL);

        if (glyph->bitmap.pixel_mode != FT_PIXEL_MODE_MONO &&
            glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            return AVERROR(EINVAL);

        if (dtext->is_packed_rgb) {
            draw_glyph_rgb(picref, &glyph->bitmap,
                           dtext->positions[i].x+x, dtext->positions[i].y+y, width, height,
                           dtext->pixel_step[0], rgbcolor, dtext->rgba_map);
        } else {
            draw_glyph_yuv(picref, &glyph->bitmap,
                           dtext->positions[i].x+x, dtext->positions[i].y+y, width, height,
                           yuvcolor, dtext->hsub, dtext->vsub);
        }
    }

    return 0;
}

static int draw_text(AVFilterContext *ctx, AVFilterBufferRef *picref,
                     int width, int height)
{
    DrawTextContext *dtext = ctx->priv;
    int ret;

    /* draw box */
    if (dtext->draw_box)
        drawbox(picref, dtext->x, dtext->y, dtext->w, dtext->h,
                dtext->box_line, dtext->pixel_step, dtext->boxcolor,
                dtext->hsub, dtext->vsub, dtext->is_packed_rgb,
                dtext->rgba_map);

    if (dtext->shadowx || dtext->shadowy) {
        if ((ret = draw_glyphs(dtext, picref, width, height,
                               dtext->shadowcolor_rgba,
                               dtext->shadowcolor,
                               dtext->x + dtext->shadowx,
                               dtext->y + dtext->shadowy)) < 0)
            return ret;
    }

    if ((ret = draw_glyphs(dtext, picref, width, height,
                           dtext->fontcolor_rgba,
                           dtext->fontcolor,
                           dtext->x,
                           dtext->y)) < 0)
        return ret;

    return 0;
}

static void null_draw_slice(AVFilterLink *link, int y, int h, int slice_dir) { }

static inline int normalize_double(int *n, double d)
{
    int ret = 0;

    if (isnan(d)) {
        ret = AVERROR(EINVAL);
    } else if (d > INT_MAX || d < INT_MIN) {
        *n = d > INT_MAX ? INT_MAX : INT_MIN;
        ret = AVERROR(EINVAL);
    } else
        *n = round(d);

    return ret;
}

static void start_frame(AVFilterLink *inlink, AVFilterBufferRef *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    DrawTextContext *dtext = ctx->priv;
    int fail = 0;

    if (dtext_prepare_text(ctx) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Can't draw text\n");
        fail = 1;
    }

    dtext->var_values[VAR_T] = inpicref->pts == AV_NOPTS_VALUE ?
        NAN : inpicref->pts * av_q2d(inlink->time_base);
    dtext->var_values[VAR_X] =
        av_expr_eval(dtext->x_pexpr, dtext->var_values, &dtext->prng);
    dtext->var_values[VAR_Y] =
        av_expr_eval(dtext->y_pexpr, dtext->var_values, &dtext->prng);
    dtext->var_values[VAR_X] =
        av_expr_eval(dtext->x_pexpr, dtext->var_values, &dtext->prng);

    dtext->draw = fail ? 0 :
        av_expr_eval(dtext->d_pexpr, dtext->var_values, &dtext->prng);

    normalize_double(&dtext->x, dtext->var_values[VAR_X]);
    normalize_double(&dtext->y, dtext->var_values[VAR_Y]);

    if (dtext->fix_bounds) {
        if (dtext->x < 0) dtext->x = 0;
        if (dtext->y < 0) dtext->y = 0;
        if ((unsigned)dtext->x + (unsigned)dtext->w > inlink->w)
            dtext->x = inlink->w - dtext->w;
        if ((unsigned)dtext->y + (unsigned)dtext->h > inlink->h)
            dtext->y = inlink->h - dtext->h;
    }

    dtext->x &= ~((1 << dtext->hsub) - 1);
    dtext->y &= ~((1 << dtext->vsub) - 1);

    av_dlog(ctx, "n:%d t:%f x:%d y:%d x+w:%d y+h:%d\n",
            (int)dtext->var_values[VAR_N], dtext->var_values[VAR_T],
            dtext->x, dtext->y, dtext->x+dtext->w, dtext->y+dtext->h);

    ff_start_frame(inlink->dst->outputs[0], inpicref);
}

static void end_frame(AVFilterLink *inlink)
{
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef *picref = inlink->cur_buf;
    DrawTextContext *dtext = inlink->dst->priv;

    if (dtext->draw)
        draw_text(inlink->dst, picref, picref->video->w, picref->video->h);

    dtext->var_values[VAR_N] += 1.0;

    ff_draw_slice(outlink, 0, picref->video->h, 1);
    ff_end_frame(outlink);
}

AVFilter avfilter_vf_drawtext = {
    .name          = "drawtext",
    .description   = NULL_IF_CONFIG_SMALL("Draw text on top of video frames using libfreetype library."),
    .priv_size     = sizeof(DrawTextContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer = ff_null_get_video_buffer,
                                    .start_frame      = start_frame,
                                    .draw_slice       = null_draw_slice,
                                    .end_frame        = end_frame,
                                    .config_props     = config_input,
                                    .min_perms        = AV_PERM_WRITE |
                                                        AV_PERM_READ,
                                    .rej_perms        = AV_PERM_PRESERVE },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name             = "default",
                                    .type             = AVMEDIA_TYPE_VIDEO, },
                                  { .name = NULL}},
};
