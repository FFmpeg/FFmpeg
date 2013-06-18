/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2010 S.N. Hemanth Meenakshisundaram
 * Copyright (c) 2003 Gustavo Sverzut Barbieri <gsbarbieri@yahoo.com.br>
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
 * drawtext filter, based on the original vhook/drawtext.c
 * filter by Gustavo Sverzut Barbieri
 */

#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/file.h"
#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/parseutils.h"
#include "libavutil/timecode.h"
#include "libavutil/tree.h"
#include "libavutil/lfg.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#include <ft2build.h>
#include <freetype/config/ftheader.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#if CONFIG_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

static const char *const var_names[] = {
    "dar",
    "hsub", "vsub",
    "line_h", "lh",           ///< line height, same as max_glyph_h
    "main_h", "h", "H",       ///< height of the input video
    "main_w", "w", "W",       ///< width  of the input video
    "max_glyph_a", "ascent",  ///< max glyph ascent
    "max_glyph_d", "descent", ///< min glyph descent
    "max_glyph_h",            ///< max glyph height
    "max_glyph_w",            ///< max glyph width
    "n",                      ///< number of frame
    "sar",
    "t",                      ///< timestamp expressed in seconds
    "text_h", "th",           ///< height of the rendered text
    "text_w", "tw",           ///< width  of the rendered text
    "x",
    "y",
    "pict_type",
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
    VAR_DAR,
    VAR_HSUB, VAR_VSUB,
    VAR_LINE_H, VAR_LH,
    VAR_MAIN_H, VAR_h, VAR_H,
    VAR_MAIN_W, VAR_w, VAR_W,
    VAR_MAX_GLYPH_A, VAR_ASCENT,
    VAR_MAX_GLYPH_D, VAR_DESCENT,
    VAR_MAX_GLYPH_H,
    VAR_MAX_GLYPH_W,
    VAR_N,
    VAR_SAR,
    VAR_T,
    VAR_TEXT_H, VAR_TH,
    VAR_TEXT_W, VAR_TW,
    VAR_X,
    VAR_Y,
    VAR_PICT_TYPE,
    VAR_VARS_NB
};

enum expansion_mode {
    EXP_NONE,
    EXP_NORMAL,
    EXP_STRFTIME,
};

typedef struct {
    const AVClass *class;
    enum expansion_mode exp_mode;   ///< expansion mode to use for the text
    int reinit;                     ///< tells if the filter is being reinited
    uint8_t *fontfile;              ///< font to be used
    uint8_t *text;                  ///< text to be drawn
    AVBPrint expanded_text;         ///< used to contain the expanded text
    int ft_load_flags;              ///< flags used for loading fonts, see FT_LOAD_*
    FT_Vector *positions;           ///< positions for each element in the text
    size_t nb_positions;            ///< number of elements of positions array
    char *textfile;                 ///< file with text to be drawn
    int x;                          ///< x position to start drawing text
    int y;                          ///< y position to start drawing text
    int max_glyph_w;                ///< max glyph width
    int max_glyph_h;                ///< max glyph height
    int shadowx, shadowy;
    unsigned int fontsize;          ///< font size to use

    short int draw_box;             ///< draw box around text - true or false
    int use_kerning;                ///< font kerning is used - true/false
    int tabsize;                    ///< tab size
    int fix_bounds;                 ///< do we let it go out of frame bounds - t/f

    FFDrawContext dc;
    FFDrawColor fontcolor;          ///< foreground color
    FFDrawColor shadowcolor;        ///< shadow color
    FFDrawColor boxcolor;           ///< background color

    FT_Library library;             ///< freetype font library handle
    FT_Face face;                   ///< freetype font face handle
    struct AVTreeNode *glyphs;      ///< rendered glyphs, stored using the UTF-32 char code
    char *x_expr;                   ///< expression for x position
    char *y_expr;                   ///< expression for y position
    AVExpr *x_pexpr, *y_pexpr;      ///< parsed expressions for x and y
    int64_t basetime;               ///< base pts time in the real world for display
    double var_values[VAR_VARS_NB];
    char   *draw_expr;              ///< expression for draw
    AVExpr *draw_pexpr;             ///< parsed expression for draw
    int draw;                       ///< set to zero to prevent drawing
    AVLFG  prng;                    ///< random
    char       *tc_opt_string;      ///< specified timecode option string
    AVRational  tc_rate;            ///< frame rate for timecode
    AVTimecode  tc;                 ///< timecode context
    int tc24hmax;                   ///< 1 if timecode is wrapped to 24 hours, 0 otherwise
    int reload;                     ///< reload text file for each frame
    int start_number;               ///< starting frame number for n/frame_num var
} DrawTextContext;

#define OFFSET(x) offsetof(DrawTextContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption drawtext_options[]= {
    {"fontfile",    "set font file",        OFFSET(fontfile),           AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX, FLAGS},
    {"text",        "set text",             OFFSET(text),               AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX, FLAGS},
    {"textfile",    "set text file",        OFFSET(textfile),           AV_OPT_TYPE_STRING, {.str=NULL},  CHAR_MIN, CHAR_MAX, FLAGS},
    {"fontcolor",   "set foreground color", OFFSET(fontcolor.rgba),     AV_OPT_TYPE_COLOR,  {.str="black"}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"boxcolor",    "set box color",        OFFSET(boxcolor.rgba),      AV_OPT_TYPE_COLOR,  {.str="white"}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"shadowcolor", "set shadow color",     OFFSET(shadowcolor.rgba),   AV_OPT_TYPE_COLOR,  {.str="black"}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"box",         "set box",              OFFSET(draw_box),           AV_OPT_TYPE_INT,    {.i64=0},     0,        1       , FLAGS},
    {"fontsize",    "set font size",        OFFSET(fontsize),           AV_OPT_TYPE_INT,    {.i64=0},     0,        INT_MAX , FLAGS},
    {"x",           "set x expression",     OFFSET(x_expr),             AV_OPT_TYPE_STRING, {.str="0"},   CHAR_MIN, CHAR_MAX, FLAGS},
    {"y",           "set y expression",     OFFSET(y_expr),             AV_OPT_TYPE_STRING, {.str="0"},   CHAR_MIN, CHAR_MAX, FLAGS},
    {"shadowx",     "set x",                OFFSET(shadowx),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN,  INT_MAX , FLAGS},
    {"shadowy",     "set y",                OFFSET(shadowy),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN,  INT_MAX , FLAGS},
    {"tabsize",     "set tab size",         OFFSET(tabsize),            AV_OPT_TYPE_INT,    {.i64=4},     0,        INT_MAX , FLAGS},
    {"basetime",    "set base time",        OFFSET(basetime),           AV_OPT_TYPE_INT64,  {.i64=AV_NOPTS_VALUE}, INT64_MIN, INT64_MAX , FLAGS},
    {"draw",        "if false do not draw", OFFSET(draw_expr),          AV_OPT_TYPE_STRING, {.str="1"},   CHAR_MIN, CHAR_MAX, FLAGS},

    {"expansion", "set the expansion mode", OFFSET(exp_mode), AV_OPT_TYPE_INT, {.i64=EXP_NORMAL}, 0, 2, FLAGS, "expansion"},
        {"none",     "set no expansion",                    OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_NONE},     0, 0, FLAGS, "expansion"},
        {"normal",   "set normal expansion",                OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_NORMAL},   0, 0, FLAGS, "expansion"},
        {"strftime", "set strftime expansion (deprecated)", OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_STRFTIME}, 0, 0, FLAGS, "expansion"},

    {"timecode",        "set initial timecode",             OFFSET(tc_opt_string), AV_OPT_TYPE_STRING,   {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    {"tc24hmax",        "set 24 hours max (timecode only)", OFFSET(tc24hmax),      AV_OPT_TYPE_INT,      {.i64=0},           0,        1, FLAGS},
    {"timecode_rate",   "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},           0,  INT_MAX, FLAGS},
    {"r",               "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},           0,  INT_MAX, FLAGS},
    {"rate",            "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},           0,  INT_MAX, FLAGS},
    {"reload",     "reload text file for each frame",                       OFFSET(reload),     AV_OPT_TYPE_INT, {.i64=0}, 0, 1, FLAGS},
    {"fix_bounds", "if true, check and fix text coords to avoid clipping",  OFFSET(fix_bounds), AV_OPT_TYPE_INT, {.i64=1}, 0, 1, FLAGS},
    {"start_number", "start frame number for n/frame_num variable", OFFSET(start_number), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS},

    /* FT_LOAD_* flags */
    { "ft_load_flags", "set font loading flags for libfreetype", OFFSET(ft_load_flags), AV_OPT_TYPE_FLAGS, { .i64 = FT_LOAD_DEFAULT | FT_LOAD_RENDER}, 0, INT_MAX, FLAGS, "ft_load_flags" },
        { "default",                     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_DEFAULT },                     .flags = FLAGS, .unit = "ft_load_flags" },
        { "no_scale",                    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_NO_SCALE },                    .flags = FLAGS, .unit = "ft_load_flags" },
        { "no_hinting",                  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_NO_HINTING },                  .flags = FLAGS, .unit = "ft_load_flags" },
        { "render",                      NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_RENDER },                      .flags = FLAGS, .unit = "ft_load_flags" },
        { "no_bitmap",                   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_NO_BITMAP },                   .flags = FLAGS, .unit = "ft_load_flags" },
        { "vertical_layout",             NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_VERTICAL_LAYOUT },             .flags = FLAGS, .unit = "ft_load_flags" },
        { "force_autohint",              NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_FORCE_AUTOHINT },              .flags = FLAGS, .unit = "ft_load_flags" },
        { "crop_bitmap",                 NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_CROP_BITMAP },                 .flags = FLAGS, .unit = "ft_load_flags" },
        { "pedantic",                    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_PEDANTIC },                    .flags = FLAGS, .unit = "ft_load_flags" },
        { "ignore_global_advance_width", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_IGNORE_GLOBAL_ADVANCE_WIDTH }, .flags = FLAGS, .unit = "ft_load_flags" },
        { "no_recurse",                  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_NO_RECURSE },                  .flags = FLAGS, .unit = "ft_load_flags" },
        { "ignore_transform",            NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_IGNORE_TRANSFORM },            .flags = FLAGS, .unit = "ft_load_flags" },
        { "monochrome",                  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_MONOCHROME },                  .flags = FLAGS, .unit = "ft_load_flags" },
        { "linear_design",               NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_LINEAR_DESIGN },               .flags = FLAGS, .unit = "ft_load_flags" },
        { "no_autohint",                 NULL, 0, AV_OPT_TYPE_CONST, { .i64 = FT_LOAD_NO_AUTOHINT },                 .flags = FLAGS, .unit = "ft_load_flags" },
    { NULL},
};

AVFILTER_DEFINE_CLASS(drawtext);

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
    DrawTextContext *s = ctx->priv;
    Glyph *glyph;
    struct AVTreeNode *node = NULL;
    int ret;

    /* load glyph into s->face->glyph */
    if (FT_Load_Char(s->face, code, s->ft_load_flags))
        return AVERROR(EINVAL);

    /* save glyph */
    if (!(glyph = av_mallocz(sizeof(*glyph))) ||
        !(glyph->glyph = av_mallocz(sizeof(*glyph->glyph)))) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    glyph->code  = code;

    if (FT_Get_Glyph(s->face->glyph, glyph->glyph)) {
        ret = AVERROR(EINVAL);
        goto error;
    }

    glyph->bitmap      = s->face->glyph->bitmap;
    glyph->bitmap_left = s->face->glyph->bitmap_left;
    glyph->bitmap_top  = s->face->glyph->bitmap_top;
    glyph->advance     = s->face->glyph->advance.x >> 6;

    /* measure text height to calculate text_height (or the maximum text height) */
    FT_Glyph_Get_CBox(*glyph->glyph, ft_glyph_bbox_pixels, &glyph->bbox);

    /* cache the newly created glyph */
    if (!(node = av_tree_node_alloc())) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    av_tree_insert(&s->glyphs, glyph, glyph_cmp, &node);

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

static int load_font_file(AVFilterContext *ctx, const char *path, int index,
                          const char **error)
{
    DrawTextContext *s = ctx->priv;
    int err;

    err = FT_New_Face(s->library, path, index, &s->face);
    if (err) {
        *error = FT_ERRMSG(err);
        return AVERROR(EINVAL);
    }
    return 0;
}

#if CONFIG_FONTCONFIG
static int load_font_fontconfig(AVFilterContext *ctx, const char **error)
{
    DrawTextContext *s = ctx->priv;
    FcConfig *fontconfig;
    FcPattern *pattern, *fpat;
    FcResult result = FcResultMatch;
    FcChar8 *filename;
    int err, index;
    double size;

    fontconfig = FcInitLoadConfigAndFonts();
    if (!fontconfig) {
        *error = "impossible to init fontconfig\n";
        return AVERROR(EINVAL);
    }
    pattern = FcNameParse(s->fontfile ? s->fontfile :
                          (uint8_t *)(intptr_t)"default");
    if (!pattern) {
        *error = "could not parse fontconfig pattern";
        return AVERROR(EINVAL);
    }
    if (!FcConfigSubstitute(fontconfig, pattern, FcMatchPattern)) {
        *error = "could not substitue fontconfig options"; /* very unlikely */
        return AVERROR(EINVAL);
    }
    FcDefaultSubstitute(pattern);
    fpat = FcFontMatch(fontconfig, pattern, &result);
    if (!fpat || result != FcResultMatch) {
        *error = "impossible to find a matching font";
        return AVERROR(EINVAL);
    }
    if (FcPatternGetString (fpat, FC_FILE,  0, &filename) != FcResultMatch ||
        FcPatternGetInteger(fpat, FC_INDEX, 0, &index   ) != FcResultMatch ||
        FcPatternGetDouble (fpat, FC_SIZE,  0, &size    ) != FcResultMatch) {
        *error = "impossible to find font information";
        return AVERROR(EINVAL);
    }
    av_log(ctx, AV_LOG_INFO, "Using \"%s\"\n", filename);
    if (!s->fontsize)
        s->fontsize = size + 0.5;
    err = load_font_file(ctx, filename, index, error);
    if (err)
        return err;
    FcPatternDestroy(fpat);
    FcPatternDestroy(pattern);
    FcConfigDestroy(fontconfig);
    return 0;
}
#endif

static int load_font(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    int err;
    const char *error = "unknown error\n";

    /* load the face, and set up the encoding, which is by default UTF-8 */
    err = load_font_file(ctx, s->fontfile, 0, &error);
    if (!err)
        return 0;
#if CONFIG_FONTCONFIG
    err = load_font_fontconfig(ctx, &error);
    if (!err)
        return 0;
#endif
    av_log(ctx, AV_LOG_ERROR, "Could not load font \"%s\": %s\n",
           s->fontfile, error);
    return err;
}

static int load_textfile(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    int err;
    uint8_t *textbuf;
    size_t textbuf_size;

    if ((err = av_file_map(s->textfile, &textbuf, &textbuf_size, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "The text file '%s' could not be read or is empty\n",
               s->textfile);
        return err;
    }

    if (!(s->text = av_realloc(s->text, textbuf_size + 1)))
        return AVERROR(ENOMEM);
    memcpy(s->text, textbuf, textbuf_size);
    s->text[textbuf_size] = 0;
    av_file_unmap(textbuf, textbuf_size);

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    int err;
    DrawTextContext *s = ctx->priv;
    Glyph *glyph;

    if (!s->fontfile && !CONFIG_FONTCONFIG) {
        av_log(ctx, AV_LOG_ERROR, "No font filename provided\n");
        return AVERROR(EINVAL);
    }

    if (s->textfile) {
        if (s->text) {
            av_log(ctx, AV_LOG_ERROR,
                   "Both text and text file provided. Please provide only one\n");
            return AVERROR(EINVAL);
        }
        if ((err = load_textfile(ctx)) < 0)
            return err;
    }

    if (s->reload && !s->textfile)
        av_log(ctx, AV_LOG_WARNING, "No file to reload\n");

    if (s->tc_opt_string) {
        int ret = av_timecode_init_from_string(&s->tc, s->tc_rate,
                                               s->tc_opt_string, ctx);
        if (ret < 0)
            return ret;
        if (s->tc24hmax)
            s->tc.flags |= AV_TIMECODE_FLAG_24HOURSMAX;
        if (!s->text)
            s->text = av_strdup("");
    }

    if (!s->text) {
        av_log(ctx, AV_LOG_ERROR,
               "Either text, a valid file or a timecode must be provided\n");
        return AVERROR(EINVAL);
    }

    if ((err = FT_Init_FreeType(&(s->library)))) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not load FreeType: %s\n", FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    err = load_font(ctx);
    if (err)
        return err;
    if (!s->fontsize)
        s->fontsize = 16;
    if ((err = FT_Set_Pixel_Sizes(s->face, 0, s->fontsize))) {
        av_log(ctx, AV_LOG_ERROR, "Could not set font size to %d pixels: %s\n",
               s->fontsize, FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    s->use_kerning = FT_HAS_KERNING(s->face);

    /* load the fallback glyph with code 0 */
    load_glyph(ctx, NULL, 0);

    /* set the tabsize in pixels */
    if ((err = load_glyph(ctx, &glyph, ' ')) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not set tabsize.\n");
        return err;
    }
    s->tabsize *= glyph->advance;

    if (s->exp_mode == EXP_STRFTIME &&
        (strchr(s->text, '%') || strchr(s->text, '\\')))
        av_log(ctx, AV_LOG_WARNING, "expansion=strftime is deprecated.\n");

    av_bprint_init(&s->expanded_text, 0, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
    return 0;
}

static int glyph_enu_free(void *opaque, void *elem)
{
    Glyph *glyph = elem;

    FT_Done_Glyph(*glyph->glyph);
    av_freep(&glyph->glyph);
    av_free(elem);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;

    av_expr_free(s->x_pexpr);
    av_expr_free(s->y_pexpr);
    av_expr_free(s->draw_pexpr);
    s->x_pexpr = s->y_pexpr = s->draw_pexpr = NULL;
    av_freep(&s->positions);
    s->nb_positions = 0;


    av_tree_enumerate(s->glyphs, NULL, NULL, glyph_enu_free);
    av_tree_destroy(s->glyphs);
    s->glyphs = NULL;

    FT_Done_Face(s->face);
    FT_Done_FreeType(s->library);

    av_bprint_finalize(&s->expanded_text, NULL);
}

static inline int is_newline(uint32_t c)
{
    return c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DrawTextContext *s = ctx->priv;
    int ret;

    ff_draw_init(&s->dc, inlink->format, 0);
    ff_draw_color(&s->dc, &s->fontcolor,   s->fontcolor.rgba);
    ff_draw_color(&s->dc, &s->shadowcolor, s->shadowcolor.rgba);
    ff_draw_color(&s->dc, &s->boxcolor,    s->boxcolor.rgba);

    s->var_values[VAR_w]     = s->var_values[VAR_W]     = s->var_values[VAR_MAIN_W] = inlink->w;
    s->var_values[VAR_h]     = s->var_values[VAR_H]     = s->var_values[VAR_MAIN_H] = inlink->h;
    s->var_values[VAR_SAR]   = inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    s->var_values[VAR_DAR]   = (double)inlink->w / inlink->h * s->var_values[VAR_SAR];
    s->var_values[VAR_HSUB]  = 1 << s->dc.hsub_max;
    s->var_values[VAR_VSUB]  = 1 << s->dc.vsub_max;
    s->var_values[VAR_X]     = NAN;
    s->var_values[VAR_Y]     = NAN;
    s->var_values[VAR_T]     = NAN;

    av_lfg_init(&s->prng, av_get_random_seed());

    av_expr_free(s->x_pexpr);
    av_expr_free(s->y_pexpr);
    av_expr_free(s->draw_pexpr);
    s->x_pexpr = s->y_pexpr = s->draw_pexpr = NULL;
    if ((ret = av_expr_parse(&s->x_pexpr, s->x_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&s->y_pexpr, s->y_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&s->draw_pexpr, s->draw_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0)

        return AVERROR(EINVAL);

    return 0;
}

static int command(AVFilterContext *ctx, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    DrawTextContext *s = ctx->priv;

    if (!strcmp(cmd, "reinit")) {
        int ret;
        uninit(ctx);
        s->reinit = 1;
        if ((ret = init(ctx)) < 0)
            return ret;
        return config_input(ctx->inputs[0]);
    }

    return AVERROR(ENOSYS);
}

static int func_pict_type(AVFilterContext *ctx, AVBPrint *bp,
                          char *fct, unsigned argc, char **argv, int tag)
{
    DrawTextContext *s = ctx->priv;

    av_bprintf(bp, "%c", av_get_picture_type_char(s->var_values[VAR_PICT_TYPE]));
    return 0;
}

static int func_pts(AVFilterContext *ctx, AVBPrint *bp,
                    char *fct, unsigned argc, char **argv, int tag)
{
    DrawTextContext *s = ctx->priv;

    av_bprintf(bp, "%.6f", s->var_values[VAR_T]);
    return 0;
}

static int func_frame_num(AVFilterContext *ctx, AVBPrint *bp,
                          char *fct, unsigned argc, char **argv, int tag)
{
    DrawTextContext *s = ctx->priv;

    av_bprintf(bp, "%d", (int)s->var_values[VAR_N]);
    return 0;
}

#if !HAVE_LOCALTIME_R
static void localtime_r(const time_t *t, struct tm *tm)
{
    *tm = *localtime(t);
}
#endif

static int func_strftime(AVFilterContext *ctx, AVBPrint *bp,
                         char *fct, unsigned argc, char **argv, int tag)
{
    const char *fmt = argc ? argv[0] : "%Y-%m-%d %H:%M:%S";
    time_t now;
    struct tm tm;

    time(&now);
    if (tag == 'L')
        localtime_r(&now, &tm);
    else
        tm = *gmtime(&now);
    av_bprint_strftime(bp, fmt, &tm);
    return 0;
}

static int func_eval_expr(AVFilterContext *ctx, AVBPrint *bp,
                          char *fct, unsigned argc, char **argv, int tag)
{
    DrawTextContext *s = ctx->priv;
    double res;
    int ret;

    ret = av_expr_parse_and_eval(&res, argv[0], var_names, s->var_values,
                                 NULL, NULL, fun2_names, fun2,
                                 &s->prng, 0, ctx);
    if (ret < 0)
        av_log(ctx, AV_LOG_ERROR,
               "Expression '%s' for the expr text expansion function is not valid\n",
               argv[0]);
    else
        av_bprintf(bp, "%f", res);

    return ret;
}

static const struct drawtext_function {
    const char *name;
    unsigned argc_min, argc_max;
    int tag;                            /**< opaque argument to func */
    int (*func)(AVFilterContext *, AVBPrint *, char *, unsigned, char **, int);
} functions[] = {
    { "expr",      1, 1, 0,   func_eval_expr },
    { "e",         1, 1, 0,   func_eval_expr },
    { "pict_type", 0, 0, 0,   func_pict_type },
    { "pts",       0, 0, 0,   func_pts      },
    { "gmtime",    0, 1, 'G', func_strftime },
    { "localtime", 0, 1, 'L', func_strftime },
    { "frame_num", 0, 0, 0,   func_frame_num },
    { "n",         0, 0, 0,   func_frame_num },
};

static int eval_function(AVFilterContext *ctx, AVBPrint *bp, char *fct,
                         unsigned argc, char **argv)
{
    unsigned i;

    for (i = 0; i < FF_ARRAY_ELEMS(functions); i++) {
        if (strcmp(fct, functions[i].name))
            continue;
        if (argc < functions[i].argc_min) {
            av_log(ctx, AV_LOG_ERROR, "%%{%s} requires at least %d arguments\n",
                   fct, functions[i].argc_min);
            return AVERROR(EINVAL);
        }
        if (argc > functions[i].argc_max) {
            av_log(ctx, AV_LOG_ERROR, "%%{%s} requires at most %d arguments\n",
                   fct, functions[i].argc_max);
            return AVERROR(EINVAL);
        }
        break;
    }
    if (i >= FF_ARRAY_ELEMS(functions)) {
        av_log(ctx, AV_LOG_ERROR, "%%{%s} is not known\n", fct);
        return AVERROR(EINVAL);
    }
    return functions[i].func(ctx, bp, fct, argc, argv, functions[i].tag);
}

static int expand_function(AVFilterContext *ctx, AVBPrint *bp, char **rtext)
{
    const char *text = *rtext;
    char *argv[16] = { NULL };
    unsigned argc = 0, i;
    int ret;

    if (*text != '{') {
        av_log(ctx, AV_LOG_ERROR, "Stray %% near '%s'\n", text);
        return AVERROR(EINVAL);
    }
    text++;
    while (1) {
        if (!(argv[argc++] = av_get_token(&text, ":}"))) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        if (!*text) {
            av_log(ctx, AV_LOG_ERROR, "Unterminated %%{} near '%s'\n", *rtext);
            ret = AVERROR(EINVAL);
            goto end;
        }
        if (argc == FF_ARRAY_ELEMS(argv))
            av_freep(&argv[--argc]); /* error will be caught later */
        if (*text == '}')
            break;
        text++;
    }

    if ((ret = eval_function(ctx, bp, argv[0], argc - 1, argv + 1)) < 0)
        goto end;
    ret = 0;
    *rtext = (char *)text + 1;

end:
    for (i = 0; i < argc; i++)
        av_freep(&argv[i]);
    return ret;
}

static int expand_text(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    char *text = s->text;
    AVBPrint *bp = &s->expanded_text;
    int ret;

    av_bprint_clear(bp);
    while (*text) {
        if (*text == '\\' && text[1]) {
            av_bprint_chars(bp, text[1], 1);
            text += 2;
        } else if (*text == '%') {
            text++;
            if ((ret = expand_function(ctx, bp, &text)) < 0)
                return ret;
        } else {
            av_bprint_chars(bp, *text, 1);
            text++;
        }
    }
    if (!av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);
    return 0;
}

static int draw_glyphs(DrawTextContext *s, AVFrame *frame,
                       int width, int height, const uint8_t rgbcolor[4], FFDrawColor *color, int x, int y)
{
    char *text = s->expanded_text.str;
    uint32_t code = 0;
    int i, x1, y1;
    uint8_t *p;
    Glyph *glyph = NULL;

    for (i = 0, p = text; *p; i++) {
        Glyph dummy = { 0 };
        GET_UTF8(code, *p++, continue;);

        /* skip new line chars, just go to new line */
        if (code == '\n' || code == '\r' || code == '\t')
            continue;

        dummy.code = code;
        glyph = av_tree_find(s->glyphs, &dummy, (void *)glyph_cmp, NULL);

        if (glyph->bitmap.pixel_mode != FT_PIXEL_MODE_MONO &&
            glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            return AVERROR(EINVAL);

        x1 = s->positions[i].x+s->x+x;
        y1 = s->positions[i].y+s->y+y;

        ff_blend_mask(&s->dc, color,
                      frame->data, frame->linesize, width, height,
                      glyph->bitmap.buffer, glyph->bitmap.pitch,
                      glyph->bitmap.width, glyph->bitmap.rows,
                      glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO ? 0 : 3,
                      0, x1, y1);
    }

    return 0;
}

static int draw_text(AVFilterContext *ctx, AVFrame *frame,
                     int width, int height)
{
    DrawTextContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    uint32_t code = 0, prev_code = 0;
    int x = 0, y = 0, i = 0, ret;
    int max_text_line_w = 0, len;
    int box_w, box_h;
    char *text = s->text;
    uint8_t *p;
    int y_min = 32000, y_max = -32000;
    int x_min = 32000, x_max = -32000;
    FT_Vector delta;
    Glyph *glyph = NULL, *prev_glyph = NULL;
    Glyph dummy = { 0 };

    time_t now = time(0);
    struct tm ltime;
    AVBPrint *bp = &s->expanded_text;

    av_bprint_clear(bp);

    if(s->basetime != AV_NOPTS_VALUE)
        now= frame->pts*av_q2d(ctx->inputs[0]->time_base) + s->basetime/1000000;

    switch (s->exp_mode) {
    case EXP_NONE:
        av_bprintf(bp, "%s", s->text);
        break;
    case EXP_NORMAL:
        if ((ret = expand_text(ctx)) < 0)
            return ret;
        break;
    case EXP_STRFTIME:
        localtime_r(&now, &ltime);
        av_bprint_strftime(bp, s->text, &ltime);
        break;
    }

    if (s->tc_opt_string) {
        char tcbuf[AV_TIMECODE_STR_SIZE];
        av_timecode_make_string(&s->tc, tcbuf, inlink->frame_count);
        av_bprint_clear(bp);
        av_bprintf(bp, "%s%s", s->text, tcbuf);
    }

    if (!av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);
    text = s->expanded_text.str;
    if ((len = s->expanded_text.len) > s->nb_positions) {
        if (!(s->positions =
              av_realloc(s->positions, len*sizeof(*s->positions))))
            return AVERROR(ENOMEM);
        s->nb_positions = len;
    }

    x = 0;
    y = 0;

    /* load and cache glyphs */
    for (i = 0, p = text; *p; i++) {
        GET_UTF8(code, *p++, continue;);

        /* get glyph */
        dummy.code = code;
        glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);
        if (!glyph) {
            load_glyph(ctx, &glyph, code);
        }

        y_min = FFMIN(glyph->bbox.yMin, y_min);
        y_max = FFMAX(glyph->bbox.yMax, y_max);
        x_min = FFMIN(glyph->bbox.xMin, x_min);
        x_max = FFMAX(glyph->bbox.xMax, x_max);
    }
    s->max_glyph_h = y_max - y_min;
    s->max_glyph_w = x_max - x_min;

    /* compute and save position for each glyph */
    glyph = NULL;
    for (i = 0, p = text; *p; i++) {
        GET_UTF8(code, *p++, continue;);

        /* skip the \n in the sequence \r\n */
        if (prev_code == '\r' && code == '\n')
            continue;

        prev_code = code;
        if (is_newline(code)) {

            max_text_line_w = FFMAX(max_text_line_w, x);
            y += s->max_glyph_h;
            x = 0;
            continue;
        }

        /* get glyph */
        prev_glyph = glyph;
        dummy.code = code;
        glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);

        /* kerning */
        if (s->use_kerning && prev_glyph && glyph->code) {
            FT_Get_Kerning(s->face, prev_glyph->code, glyph->code,
                           ft_kerning_default, &delta);
            x += delta.x >> 6;
        }

        /* save position */
        s->positions[i].x = x + glyph->bitmap_left;
        s->positions[i].y = y - glyph->bitmap_top + y_max;
        if (code == '\t') x  = (x / s->tabsize + 1)*s->tabsize;
        else              x += glyph->advance;
    }

    max_text_line_w = FFMAX(x, max_text_line_w);

    s->var_values[VAR_TW] = s->var_values[VAR_TEXT_W] = max_text_line_w;
    s->var_values[VAR_TH] = s->var_values[VAR_TEXT_H] = y + s->max_glyph_h;

    s->var_values[VAR_MAX_GLYPH_W] = s->max_glyph_w;
    s->var_values[VAR_MAX_GLYPH_H] = s->max_glyph_h;
    s->var_values[VAR_MAX_GLYPH_A] = s->var_values[VAR_ASCENT ] = y_max;
    s->var_values[VAR_MAX_GLYPH_D] = s->var_values[VAR_DESCENT] = y_min;

    s->var_values[VAR_LINE_H] = s->var_values[VAR_LH] = s->max_glyph_h;

    s->x = s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, &s->prng);
    s->y = s->var_values[VAR_Y] = av_expr_eval(s->y_pexpr, s->var_values, &s->prng);
    s->x = s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, &s->prng);
    s->draw = av_expr_eval(s->draw_pexpr, s->var_values, &s->prng);

    if(!s->draw)
        return 0;

    box_w = FFMIN(width - 1 , max_text_line_w);
    box_h = FFMIN(height - 1, y + s->max_glyph_h);

    /* draw box */
    if (s->draw_box)
        ff_blend_rectangle(&s->dc, &s->boxcolor,
                           frame->data, frame->linesize, width, height,
                           s->x, s->y, box_w, box_h);

    if (s->shadowx || s->shadowy) {
        if ((ret = draw_glyphs(s, frame, width, height, s->shadowcolor.rgba,
                               &s->shadowcolor, s->shadowx, s->shadowy)) < 0)
            return ret;
    }

    if ((ret = draw_glyphs(s, frame, width, height, s->fontcolor.rgba,
                           &s->fontcolor, 0, 0)) < 0)
        return ret;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DrawTextContext *s = ctx->priv;
    int ret;

    if (s->reload)
        if ((ret = load_textfile(ctx)) < 0)
            return ret;

    s->var_values[VAR_N] = inlink->frame_count+s->start_number;
    s->var_values[VAR_T] = frame->pts == AV_NOPTS_VALUE ?
        NAN : frame->pts * av_q2d(inlink->time_base);

    s->var_values[VAR_PICT_TYPE] = frame->pict_type;

    draw_text(ctx, frame, frame->width, frame->height);

    av_log(ctx, AV_LOG_DEBUG, "n:%d t:%f text_w:%d text_h:%d x:%d y:%d\n",
           (int)s->var_values[VAR_N], s->var_values[VAR_T],
           (int)s->var_values[VAR_TEXT_W], (int)s->var_values[VAR_TEXT_H],
           s->x, s->y);

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad avfilter_vf_drawtext_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_video_buffer = ff_null_get_video_buffer,
        .filter_frame     = filter_frame,
        .config_props     = config_input,
        .needs_writable   = 1,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_drawtext_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter avfilter_vf_drawtext = {
    .name          = "drawtext",
    .description   = NULL_IF_CONFIG_SMALL("Draw text on top of video frames using libfreetype library."),
    .priv_size     = sizeof(DrawTextContext),
    .priv_class    = &drawtext_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = avfilter_vf_drawtext_inputs,
    .outputs   = avfilter_vf_drawtext_outputs,
    .process_command = command,
};
