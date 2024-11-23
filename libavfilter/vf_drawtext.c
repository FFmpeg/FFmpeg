/*
 * Copyright (c) 2023 Francesco Carusi
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

#include "config.h"

#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fenv.h>

#if CONFIG_LIBFONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/random_seed.h"
#include "libavutil/parseutils.h"
#include "libavutil/time.h"
#include "libavutil/timecode.h"
#include "libavutil/time_internal.h"
#include "libavutil/tree.h"
#include "libavutil/lfg.h"
#include "libavutil/detection_bbox.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "textutils.h"
#include "video.h"

#if CONFIG_LIBFRIBIDI
#include <fribidi.h>
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#include <hb.h>
#include <hb-ft.h>

// Ceiling operation for positive integers division
#define POS_CEIL(x, y) ((x)/(y) + ((x)%(y) != 0))

static const char *const var_names[] = {
    "dar",
    "hsub", "vsub",
    "line_h", "lh",           ///< line height
    "main_h", "h", "H",       ///< height of the input video
    "main_w", "w", "W",       ///< width  of the input video
    "max_glyph_a", "ascent",  ///< max glyph ascender
    "max_glyph_d", "descent", ///< min glyph descender
    "max_glyph_h",            ///< max glyph height
    "max_glyph_w",            ///< max glyph width
    "font_a",                 ///< font-defined ascent
    "font_d",                 ///< font-defined descent
    "top_a",                  ///< max glyph ascender of the top line
    "bottom_d",               ///< max glyph descender of the bottom line
    "n",                      ///< number of frame
    "sar",
    "t",                      ///< timestamp expressed in seconds
    "text_h", "th",           ///< height of the rendered text
    "text_w", "tw",           ///< width  of the rendered text
    "x",
    "y",
    "pict_type",
#if FF_API_FRAME_PKT
    "pkt_pos",
#endif
#if FF_API_FRAME_PKT
    "pkt_size",
#endif
    "duration",
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
    VAR_FONT_A,
    VAR_FONT_D,
    VAR_TOP_A,
    VAR_BOTTOM_D,
    VAR_N,
    VAR_SAR,
    VAR_T,
    VAR_TEXT_H, VAR_TH,
    VAR_TEXT_W, VAR_TW,
    VAR_X,
    VAR_Y,
    VAR_PICT_TYPE,
#if FF_API_FRAME_PKT
    VAR_PKT_POS,
#endif
#if FF_API_FRAME_PKT
    VAR_PKT_SIZE,
#endif
    VAR_DURATION,
    VAR_VARS_NB
};

enum expansion_mode {
    EXP_NONE,
    EXP_NORMAL,
    EXP_STRFTIME,
};

enum y_alignment {
    YA_TEXT,
    YA_BASELINE,
    YA_FONT,
};

enum text_alignment {
    TA_LEFT   = (1 << 0),
    TA_RIGHT  = (1 << 1),
    TA_TOP    = (1 << 2),
    TA_BOTTOM = (1 << 3),
};

typedef struct HarfbuzzData {
    hb_buffer_t* buf;
    hb_font_t* font;
    unsigned int glyph_count;
    hb_glyph_info_t* glyph_info;
    hb_glyph_position_t* glyph_pos;
} HarfbuzzData;

/** Information about a single glyph in a text line */
typedef struct GlyphInfo {
    uint32_t code;                  ///< the glyph code point
    int x;                          ///< the x position of the glyph
    int y;                          ///< the y position of the glyph
    int shift_x64;                  ///< the horizontal shift of the glyph in 26.6 units
    int shift_y64;                  ///< the vertical shift of the glyph in 26.6 units
} GlyphInfo;

/** Information about a single line of text */
typedef struct TextLine {
    int offset_left64;              ///< offset between the origin and
                                    ///  the leftmost pixel of the first glyph
    int offset_right64;             ///< maximum offset between the origin and
                                    ///  the rightmost pixel of the last glyph
    int width64;                    ///< width of the line
    HarfbuzzData hb_data;           ///< libharfbuzz data of this text line
    GlyphInfo* glyphs;              ///< array of glyphs in this text line
    int cluster_offset;             ///< the offset at which this line begins
} TextLine;

/** A glyph as loaded and rendered using libfreetype */
typedef struct Glyph {
    FT_Glyph glyph;
    FT_Glyph border_glyph;
    uint32_t code;
    unsigned int fontsize;
    /** Glyph bitmaps with 1/4 pixel precision in both directions */
    FT_BitmapGlyph bglyph[16];
    /** Outlined glyph bitmaps with 1/4 pixel precision in both directions */
    FT_BitmapGlyph border_bglyph[16];
    FT_BBox bbox;
} Glyph;

/** Global text metrics */
typedef struct TextMetrics {
    int offset_top64;               ///< ascender amount of the first line (in 26.6 units)
    int offset_bottom64;            ///< descender amount of the last line (in 26.6 units)
    int offset_left64;              ///< maximum offset between the origin and
                                    ///  the leftmost pixel of the first glyph
                                    ///  of each line (in 26.6 units)
    int offset_right64;             ///< maximum offset between the origin and
                                    ///  the rightmost pixel of the last glyph
                                    ///  of each line (in 26.6 units)
    int line_height64;              ///< the font-defined line height
    int width;                      ///< width of the longest line - ceil(width64/64)
    int height;                     ///< total height of the text - ceil(height64/64)

    int min_y64;                    ///< minimum value of bbox.yMin among glyphs (in 26.6 units)
    int max_y64;                    ///< maximum value of bbox.yMax among glyphs (in 26.6 units)
    int min_x64;                    ///< minimum value of bbox.xMin among glyphs (in 26.6 units)
    int max_x64;                    ///< maximum value of bbox.xMax among glyphs (in 26.6 units)

    // Position of the background box (without borders)
    int rect_x;                     ///< x position of the box
    int rect_y;                     ///< y position of the box
} TextMetrics;

typedef struct DrawTextContext {
    const AVClass *class;
    int exp_mode;                   ///< expansion mode to use for the text
    FFExpandTextContext expand_text; ///< expand text in case exp_mode == NORMAL
    int reinit;                     ///< tells if the filter is being reinited
#if CONFIG_LIBFONTCONFIG
    uint8_t *font;                  ///< font to be used
#endif
    uint8_t *fontfile;              ///< font to be used
    uint8_t *text;                  ///< text to be drawn
    AVBPrint expanded_text;         ///< used to contain the expanded text
    uint8_t *fontcolor_expr;        ///< fontcolor expression to evaluate
    AVBPrint expanded_fontcolor;    ///< used to contain the expanded fontcolor spec
    int ft_load_flags;              ///< flags used for loading fonts, see FT_LOAD_*
    char *textfile;                 ///< file with text to be drawn
    double x;                       ///< x position to start drawing text
    double y;                       ///< y position to start drawing text
    int max_glyph_w;                ///< max glyph width
    int max_glyph_h;                ///< max glyph height
    int shadowx, shadowy;
    int borderw;                    ///< border width
    char *fontsize_expr;            ///< expression for fontsize
    AVExpr *fontsize_pexpr;         ///< parsed expressions for fontsize
    unsigned int fontsize;          ///< font size to use
    unsigned int default_fontsize;  ///< default font size to use

    int line_spacing;               ///< lines spacing in pixels
    short int draw_box;             ///< draw box around text - true or false
    char *boxborderw;               ///< box border width (padding)
                                    ///  allowed formats: "all", "vert|oriz", "top|right|bottom|left"
    int bb_top;                     ///< the size of the top box border
    int bb_right;                   ///< the size of the right box border
    int bb_bottom;                  ///< the size of the bottom box border
    int bb_left;                    ///< the size of the left box border
    int box_width;                  ///< the width of box
    int box_height;                 ///< the height of box
    int tabsize;                    ///< tab size
    int fix_bounds;                 ///< do we let it go out of frame bounds - t/f

    FFDrawContext dc;
    FFDrawColor fontcolor;          ///< foreground color
    FFDrawColor shadowcolor;        ///< shadow color
    FFDrawColor bordercolor;        ///< border color
    FFDrawColor boxcolor;           ///< background color

    FT_Library library;             ///< freetype font library handle
    FT_Face face;                   ///< freetype font face handle
    FT_Stroker stroker;             ///< freetype stroker handle
    struct AVTreeNode *glyphs;      ///< rendered glyphs, stored using the UTF-32 char code
    char *x_expr;                   ///< expression for x position
    char *y_expr;                   ///< expression for y position
    AVExpr *x_pexpr, *y_pexpr;      ///< parsed expressions for x and y
    int64_t basetime;               ///< base pts time in the real world for display
    double var_values[VAR_VARS_NB];
    char   *a_expr;
    AVExpr *a_pexpr;
    int alpha;
    AVLFG  prng;                    ///< random
    char       *tc_opt_string;      ///< specified timecode option string
    AVRational  tc_rate;            ///< frame rate for timecode
    AVTimecode  tc;                 ///< timecode context
    int tc24hmax;                   ///< 1 if timecode is wrapped to 24 hours, 0 otherwise
    int reload;                     ///< reload text file at specified frame interval
    int start_number;               ///< starting frame number for n/frame_num var
    char *text_source_string;       ///< the string to specify text data source
    enum AVFrameSideDataType text_source;
#if CONFIG_LIBFRIBIDI
    int text_shaping;               ///< 1 to shape the text before drawing it
#endif
    AVDictionary *metadata;

    int boxw;                       ///< the value of the boxw parameter
    int boxh;                       ///< the value of the boxh parameter
    int text_align;                 ///< the horizontal and vertical text alignment
    int y_align;                    ///< the value of the y_align parameter

    TextLine *lines;                ///< computed information about text lines
    int line_count;                 ///< the number of text lines
    uint32_t *tab_clusters;         ///< the position of tab characters in the text
    int tab_count;                  ///< the number of tab characters
    int blank_advance64;            ///< the size of the space character
    int tab_warning_printed;        ///< ensure the tab warning to be printed only once
} DrawTextContext;

#define OFFSET(x) offsetof(DrawTextContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption drawtext_options[]= {
    {"fontfile",       "set font file",         OFFSET(fontfile),           AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {"text",           "set text",              OFFSET(text),               AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, TFLAGS},
    {"textfile",       "set text file",         OFFSET(textfile),           AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {"fontcolor",      "set foreground color",  OFFSET(fontcolor.rgba),     AV_OPT_TYPE_COLOR,  {.str="black"}, 0, 0, TFLAGS},
    {"fontcolor_expr", "set foreground color expression", OFFSET(fontcolor_expr), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, FLAGS},
    {"boxcolor",       "set box color",         OFFSET(boxcolor.rgba),      AV_OPT_TYPE_COLOR,  {.str="white"}, 0, 0, TFLAGS},
    {"bordercolor",    "set border color",      OFFSET(bordercolor.rgba),   AV_OPT_TYPE_COLOR,  {.str="black"}, 0, 0, TFLAGS},
    {"shadowcolor",    "set shadow color",      OFFSET(shadowcolor.rgba),   AV_OPT_TYPE_COLOR,  {.str="black"}, 0, 0, TFLAGS},
    {"box",            "set box",               OFFSET(draw_box),           AV_OPT_TYPE_BOOL,   {.i64=0},     0, 1, TFLAGS},
    {"boxborderw",     "set box borders width", OFFSET(boxborderw),         AV_OPT_TYPE_STRING, {.str="0"},   0, 0, TFLAGS},
    {"line_spacing",   "set line spacing in pixels", OFFSET(line_spacing),  AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN, INT_MAX, TFLAGS},
    {"fontsize",       "set font size",         OFFSET(fontsize_expr),      AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, TFLAGS},
    {"text_align",     "set text alignment",    OFFSET(text_align),         AV_OPT_TYPE_FLAGS,  {.i64=0}, 0, (TA_LEFT|TA_RIGHT|TA_TOP|TA_BOTTOM), TFLAGS, .unit = "text_align"},
        { "left",    NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_LEFT   }, .flags = TFLAGS, .unit = "text_align" },
        { "L",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_LEFT   }, .flags = TFLAGS, .unit = "text_align" },
        { "right",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_RIGHT  }, .flags = TFLAGS, .unit = "text_align" },
        { "R",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_RIGHT  }, .flags = TFLAGS, .unit = "text_align" },
        { "center",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = (TA_LEFT|TA_RIGHT) }, .flags = TFLAGS, .unit = "text_align" },
        { "C",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = (TA_LEFT|TA_RIGHT) }, .flags = TFLAGS, .unit = "text_align" },
        { "top",     NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_TOP    }, .flags = TFLAGS, .unit = "text_align" },
        { "T",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_TOP    }, .flags = TFLAGS, .unit = "text_align" },
        { "bottom",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_BOTTOM }, .flags = TFLAGS, .unit = "text_align" },
        { "B",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TA_BOTTOM }, .flags = TFLAGS, .unit = "text_align" },
        { "middle",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = (TA_TOP|TA_BOTTOM) }, .flags = TFLAGS, .unit = "text_align" },
        { "M",       NULL, 0, AV_OPT_TYPE_CONST, { .i64 = (TA_TOP|TA_BOTTOM) }, .flags = TFLAGS, .unit = "text_align" },
    {"x",              "set x expression",      OFFSET(x_expr),             AV_OPT_TYPE_STRING, {.str="0"},   0, 0, TFLAGS},
    {"y",              "set y expression",      OFFSET(y_expr),             AV_OPT_TYPE_STRING, {.str="0"},   0, 0, TFLAGS},
    {"boxw",           "set box width",         OFFSET(boxw),               AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, TFLAGS},
    {"boxh",           "set box height",        OFFSET(boxh),               AV_OPT_TYPE_INT,    {.i64=0},     0, INT_MAX, TFLAGS},
    {"shadowx",        "set shadow x offset",   OFFSET(shadowx),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN, INT_MAX, TFLAGS},
    {"shadowy",        "set shadow y offset",   OFFSET(shadowy),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN, INT_MAX, TFLAGS},
    {"borderw",        "set border width",      OFFSET(borderw),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN, INT_MAX, TFLAGS},
    {"tabsize",        "set tab size",          OFFSET(tabsize),            AV_OPT_TYPE_INT,    {.i64=4},     0, INT_MAX, TFLAGS},
    {"basetime",       "set base time",         OFFSET(basetime),           AV_OPT_TYPE_INT64,  {.i64=AV_NOPTS_VALUE}, INT64_MIN, INT64_MAX, FLAGS},
#if CONFIG_LIBFONTCONFIG
    { "font",        "Font name",            OFFSET(font),               AV_OPT_TYPE_STRING, { .str = "Sans" },           .flags = FLAGS },
#endif

    {"expansion", "set the expansion mode", OFFSET(exp_mode), AV_OPT_TYPE_INT, {.i64=EXP_NORMAL}, 0, 2, FLAGS, .unit = "expansion"},
        {"none",     "set no expansion",                    OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_NONE},     0, 0, FLAGS, .unit = "expansion"},
        {"normal",   "set normal expansion",                OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_NORMAL},   0, 0, FLAGS, .unit = "expansion"},
        {"strftime", "set strftime expansion (deprecated)", OFFSET(exp_mode), AV_OPT_TYPE_CONST, {.i64=EXP_STRFTIME}, 0, 0, FLAGS, .unit = "expansion"},
    {"y_align",   "set the y alignment",    OFFSET(y_align), AV_OPT_TYPE_INT,  {.i64=YA_TEXT}, 0, 2, TFLAGS, .unit = "y_align"},
        {"text",     "y is referred to the top of the first text line", OFFSET(y_align), AV_OPT_TYPE_CONST, {.i64=YA_TEXT},     0, 0, FLAGS, .unit = "y_align"},
        {"baseline", "y is referred to the baseline of the first line", OFFSET(y_align), AV_OPT_TYPE_CONST, {.i64=YA_BASELINE}, 0, 0, FLAGS, .unit = "y_align"},
        {"font",     "y is referred to the font defined line metrics",  OFFSET(y_align), AV_OPT_TYPE_CONST, {.i64=YA_FONT},     0, 0, FLAGS, .unit = "y_align"},

    {"timecode",        "set initial timecode",             OFFSET(tc_opt_string), AV_OPT_TYPE_STRING,   {.str=NULL}, 0, 0, FLAGS},
    {"tc24hmax",        "set 24 hours max (timecode only)", OFFSET(tc24hmax),      AV_OPT_TYPE_BOOL,     {.i64=0},    0, 1, FLAGS},
    {"timecode_rate",   "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},    0, INT_MAX, FLAGS},
    {"r",               "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},    0, INT_MAX, FLAGS},
    {"rate",            "set rate (timecode only)",         OFFSET(tc_rate),       AV_OPT_TYPE_RATIONAL, {.dbl=0},    0, INT_MAX, FLAGS},
    {"reload",          "reload text file at specified frame interval", OFFSET(reload), AV_OPT_TYPE_INT, {.i64=0},    0, INT_MAX, FLAGS},
    {"alpha",           "apply alpha while rendering",      OFFSET(a_expr),        AV_OPT_TYPE_STRING,   {.str = "1"}, .flags = TFLAGS},
    {"fix_bounds",      "check and fix text coords to avoid clipping", OFFSET(fix_bounds), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"start_number",    "start frame number for n/frame_num variable", OFFSET(start_number), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS},
    {"text_source",     "the source of text", OFFSET(text_source_string), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS },

#if CONFIG_LIBFRIBIDI
    {"text_shaping", "attempt to shape text before drawing", OFFSET(text_shaping), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS},
#endif

    /* FT_LOAD_* flags */
    { "ft_load_flags", "set font loading flags for libfreetype", OFFSET(ft_load_flags), AV_OPT_TYPE_FLAGS, { .i64 = FT_LOAD_DEFAULT }, 0, INT_MAX, FLAGS, .unit = "ft_load_flags" },
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
    { NULL }
};

AVFILTER_DEFINE_CLASS(drawtext);

#undef __FTERRORS_H__
#define FT_ERROR_START_LIST {
#define FT_ERRORDEF(e, v, s) { (e), (s) },
#define FT_ERROR_END_LIST { 0, NULL } };

static const struct ft_error {
    int err;
    const char *err_msg;
} ft_errors[] =
#include FT_ERRORS_H

#define FT_ERRMSG(e) ft_errors[e].err_msg

static int glyph_cmp(const void *key, const void *b)
{
    const Glyph *a = key, *bb = b;
    int64_t diff = (int64_t)a->code - (int64_t)bb->code;

    if (diff != 0)
        return diff > 0 ? 1 : -1;
    else
        return FFDIFFSIGN((int64_t)a->fontsize, (int64_t)bb->fontsize);
}

static av_cold int set_fontsize(AVFilterContext *ctx, unsigned int fontsize)
{
    int err;
    DrawTextContext *s = ctx->priv;

    if ((err = FT_Set_Pixel_Sizes(s->face, 0, fontsize))) {
        av_log(ctx, AV_LOG_ERROR, "Could not set font size to %d pixels: %s\n",
               fontsize, FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    s->fontsize = fontsize;

    return 0;
}

static av_cold int parse_fontsize(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    int err;

    if (s->fontsize_pexpr)
        return 0;

    if (s->fontsize_expr == NULL)
        return AVERROR(EINVAL);

    if ((err = av_expr_parse(&s->fontsize_pexpr, s->fontsize_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0)
        return err;

    return 0;
}

static av_cold int update_fontsize(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    unsigned int fontsize = s->default_fontsize;
    int err;
    double size, roundedsize;

    // if no fontsize specified use the default
    if (s->fontsize_expr != NULL) {
        if ((err = parse_fontsize(ctx)) < 0)
           return err;

        size = av_expr_eval(s->fontsize_pexpr, s->var_values, &s->prng);
        if (!isnan(size)) {
            roundedsize = round(size);
            // test for overflow before cast
            if (!(roundedsize > INT_MIN && roundedsize < INT_MAX)) {
                av_log(ctx, AV_LOG_ERROR, "fontsize overflow\n");
                return AVERROR(EINVAL);
            }
            fontsize = roundedsize;
        }
    }

    if (fontsize == 0)
        fontsize = 1;

    // no change
    if (fontsize == s->fontsize)
        return 0;

    return set_fontsize(ctx, fontsize);
}

static int load_font_file(AVFilterContext *ctx, const char *path, int index)
{
    DrawTextContext *s = ctx->priv;
    int err;

    err = FT_New_Face(s->library, path, index, &s->face);
    if (err) {
#if !CONFIG_LIBFONTCONFIG
        av_log(ctx, AV_LOG_ERROR, "Could not load font \"%s\": %s\n",
               s->fontfile, FT_ERRMSG(err));
#endif
        return AVERROR(EINVAL);
    }
    return 0;
}

#if CONFIG_LIBFONTCONFIG
static int load_font_fontconfig(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    FcConfig *fontconfig;
    FcPattern *pat, *best;
    FcResult result = FcResultMatch;
    FcChar8 *filename;
    int index;
    double size;
    int err = AVERROR(ENOENT);
    int parse_err;

    fontconfig = FcInitLoadConfigAndFonts();
    if (!fontconfig) {
        av_log(ctx, AV_LOG_ERROR, "impossible to init fontconfig\n");
        return AVERROR_UNKNOWN;
    }
    pat = FcNameParse(s->fontfile ? s->fontfile :
                          (uint8_t *)(intptr_t)"default");
    if (!pat) {
        av_log(ctx, AV_LOG_ERROR, "could not parse fontconfig pat");
        return AVERROR(EINVAL);
    }

    FcPatternAddString(pat, FC_FAMILY, s->font);

    parse_err = parse_fontsize(ctx);
    if (!parse_err) {
        double size = av_expr_eval(s->fontsize_pexpr, s->var_values, &s->prng);

        if (isnan(size)) {
            av_log(ctx, AV_LOG_ERROR, "impossible to find font information");
            return AVERROR(EINVAL);
        }

        FcPatternAddDouble(pat, FC_SIZE, size);
    }

    FcDefaultSubstitute(pat);

    if (!FcConfigSubstitute(fontconfig, pat, FcMatchPattern)) {
        av_log(ctx, AV_LOG_ERROR, "could not substitue fontconfig options"); /* very unlikely */
        FcPatternDestroy(pat);
        return AVERROR(ENOMEM);
    }

    best = FcFontMatch(fontconfig, pat, &result);
    FcPatternDestroy(pat);

    if (!best || result != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot find a valid font for the family %s\n",
               s->font);
        goto fail;
    }

    if (
        FcPatternGetInteger(best, FC_INDEX, 0, &index   ) != FcResultMatch ||
        FcPatternGetDouble (best, FC_SIZE,  0, &size    ) != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR, "impossible to find font information");
        return AVERROR(EINVAL);
    }

    if (FcPatternGetString(best, FC_FILE, 0, &filename) != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR, "No file path for %s\n",
               s->font);
        goto fail;
    }

    av_log(ctx, AV_LOG_VERBOSE, "Using \"%s\"\n", filename);
    if (parse_err)
        s->default_fontsize = size + 0.5;

    err = load_font_file(ctx, filename, index);
    if (err)
        return err;
    FcConfigDestroy(fontconfig);
fail:
    FcPatternDestroy(best);
    return err;
}
#endif

static int load_font(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    int err;

    /* load the face, and set up the encoding, which is by default UTF-8 */
    err = load_font_file(ctx, s->fontfile, 0);
    if (!err)
        return 0;
#if CONFIG_LIBFONTCONFIG
    err = load_font_fontconfig(ctx);
    if (!err)
        return 0;
#endif
    return err;
}

#if CONFIG_LIBFRIBIDI
static int shape_text(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;
    uint8_t *tmp;
    int ret = AVERROR(ENOMEM);
    static const FriBidiFlags flags = FRIBIDI_FLAGS_DEFAULT |
                                      FRIBIDI_FLAGS_ARABIC;
    FriBidiChar *unicodestr = NULL;
    FriBidiStrIndex len;
    FriBidiParType direction = FRIBIDI_PAR_LTR;
    FriBidiStrIndex line_start = 0;
    FriBidiStrIndex line_end = 0;
    FriBidiLevel *embedding_levels = NULL;
    FriBidiArabicProp *ar_props = NULL;
    FriBidiCharType *bidi_types = NULL;
    FriBidiStrIndex i,j;

    len = strlen(s->text);
    if (!(unicodestr = av_malloc_array(len, sizeof(*unicodestr)))) {
        goto out;
    }
    len = fribidi_charset_to_unicode(FRIBIDI_CHAR_SET_UTF8,
                                     s->text, len, unicodestr);

    bidi_types = av_malloc_array(len, sizeof(*bidi_types));
    if (!bidi_types) {
        goto out;
    }

    fribidi_get_bidi_types(unicodestr, len, bidi_types);

    embedding_levels = av_malloc_array(len, sizeof(*embedding_levels));
    if (!embedding_levels) {
        goto out;
    }

    if (!fribidi_get_par_embedding_levels(bidi_types, len, &direction,
                                          embedding_levels)) {
        goto out;
    }

    ar_props = av_malloc_array(len, sizeof(*ar_props));
    if (!ar_props) {
        goto out;
    }

    fribidi_get_joining_types(unicodestr, len, ar_props);
    fribidi_join_arabic(bidi_types, len, embedding_levels, ar_props);
    fribidi_shape(flags, embedding_levels, len, ar_props, unicodestr);

    for (line_end = 0, line_start = 0; line_end < len; line_end++) {
        if (ff_is_newline(unicodestr[line_end]) || line_end == len - 1) {
            if (!fribidi_reorder_line(flags, bidi_types,
                                      line_end - line_start + 1, line_start,
                                      direction, embedding_levels, unicodestr,
                                      NULL)) {
                goto out;
            }
            line_start = line_end + 1;
        }
    }

    /* Remove zero-width fill chars put in by libfribidi */
    for (i = 0, j = 0; i < len; i++)
        if (unicodestr[i] != FRIBIDI_CHAR_FILL)
            unicodestr[j++] = unicodestr[i];
    len = j;

    if (!(tmp = av_realloc(s->text, (len * 4 + 1) * sizeof(*s->text)))) {
        /* Use len * 4, as a unicode character can be up to 4 bytes in UTF-8 */
        goto out;
    }

    s->text = tmp;
    len = fribidi_unicode_to_charset(FRIBIDI_CHAR_SET_UTF8,
                                     unicodestr, len, s->text);

    ret = 0;

out:
    av_free(unicodestr);
    av_free(embedding_levels);
    av_free(ar_props);
    av_free(bidi_types);
    return ret;
}
#endif

static enum AVFrameSideDataType text_source_string_parse(const char *text_source_string)
{
    av_assert0(text_source_string);
    if (!strcmp(text_source_string, "side_data_detection_bboxes")) {
        return AV_FRAME_DATA_DETECTION_BBOXES;
    } else {
        return AVERROR(EINVAL);
    }
}

static inline int get_subpixel_idx(int shift_x64, int shift_y64)
{
    int idx = (shift_x64 >> 2) + (shift_y64 >> 4);
    return idx;
}

// Loads and (optionally) renders a glyph
static int load_glyph(AVFilterContext *ctx, Glyph **glyph_ptr, uint32_t code, int8_t shift_x64, int8_t shift_y64)
{
    DrawTextContext *s = ctx->priv;
    Glyph dummy = { 0 };
    Glyph *glyph;
    FT_Vector shift;
    struct AVTreeNode *node = NULL;
    int ret = 0;

    /* get glyph */
    dummy.code = code;
    dummy.fontsize = s->fontsize;
    glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);
    if (!glyph) {
        if (FT_Load_Glyph(s->face, code, s->ft_load_flags)) {
            return AVERROR(EINVAL);
        }
        glyph = av_mallocz(sizeof(*glyph));
        if (!glyph) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
        glyph->code  = code;
        glyph->fontsize = s->fontsize;
        if (FT_Get_Glyph(s->face->glyph, &glyph->glyph)) {
            ret = AVERROR(EINVAL);
            goto error;
        }
        if (s->borderw) {
            glyph->border_glyph = glyph->glyph;
            if (FT_Glyph_StrokeBorder(&glyph->border_glyph, s->stroker, 0, 0)) {
                ret = AVERROR_EXTERNAL;
                goto error;
            }
        }
        /* measure text height to calculate text_height (or the maximum text height) */
        FT_Glyph_Get_CBox(glyph->glyph, FT_GLYPH_BBOX_SUBPIXELS, &glyph->bbox);

        /* cache the newly created glyph */
        if (!(node = av_tree_node_alloc())) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
        av_tree_insert(&s->glyphs, glyph, glyph_cmp, &node);
    } else {
        if (s->borderw && !glyph->border_glyph) {
            glyph->border_glyph = glyph->glyph;
            if (FT_Glyph_StrokeBorder(&glyph->border_glyph, s->stroker, 0, 0)) {
                ret = AVERROR_EXTERNAL;
                goto error;
            }
        }
    }

    // Check if a bitmap is needed
    if (shift_x64 >= 0 && shift_y64 >= 0) {
        // Get the bitmap subpixel index (0 -> 15)
        int idx = get_subpixel_idx(shift_x64, shift_y64);
        shift.x = shift_x64;
        shift.y = shift_y64;

        if (!glyph->bglyph[idx]) {
            FT_Glyph tmp_glyph = glyph->glyph;
            if (FT_Glyph_To_Bitmap(&tmp_glyph, FT_RENDER_MODE_NORMAL, &shift, 0)) {
                ret = AVERROR_EXTERNAL;
                goto error;
            }
            glyph->bglyph[idx] = (FT_BitmapGlyph)tmp_glyph;
            if (glyph->bglyph[idx]->bitmap.pixel_mode == FT_PIXEL_MODE_MONO) {
                av_log(ctx, AV_LOG_ERROR, "Monocromatic (1bpp) fonts are not supported.\n");
                ret = AVERROR(EINVAL);
                goto error;
            }
        }
        if (s->borderw && !glyph->border_bglyph[idx]) {
            FT_Glyph tmp_glyph = glyph->border_glyph;
            if (FT_Glyph_To_Bitmap(&tmp_glyph, FT_RENDER_MODE_NORMAL, &shift, 0)) {
                ret = AVERROR_EXTERNAL;
                goto error;
            }
            glyph->border_bglyph[idx] = (FT_BitmapGlyph)tmp_glyph;
        }
    }
    if (glyph_ptr) {
        *glyph_ptr = glyph;
    }
    return 0;

error:
    if (glyph && glyph->glyph)
        FT_Done_Glyph(glyph->glyph);

    av_freep(&glyph);
    av_freep(&node);
    return ret;
}

// Convert a string formatted as "n1|n2|...|nN" into an integer array
static int string_to_array(const char *source, int *result, int result_size)
{
    int counter = 0, size = strlen(source) + 1;
    char *saveptr, *curval, *dup = av_malloc(size);
    if (!dup)
        return 0;
    av_strlcpy(dup, source, size);
    if (result_size > 0 && (curval = av_strtok(dup, "|", &saveptr))) {
        do {
            result[counter++] = atoi(curval);
        } while ((curval = av_strtok(NULL, "|", &saveptr)) && counter < result_size);
    }
    av_free(dup);
    return counter;
}

static int func_pict_type(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;

    av_bprintf(bp, "%c", av_get_picture_type_char(s->var_values[VAR_PICT_TYPE]));
    return 0;
}

static int func_pts(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;
    const char *fmt;
    const char *strftime_fmt = NULL;
    const char *delta = NULL;
    double pts = s->var_values[VAR_T];

    // argv: pts, FMT, [DELTA, 24HH | strftime_fmt]

    fmt = argc >= 1 ? argv[0] : "flt";
    if (argc >= 2) {
        delta = argv[1];
    }
    if (argc >= 3) {
        if (!strcmp(fmt, "hms")) {
            if (!strcmp(argv[2], "24HH")) {
                av_log(ctx, AV_LOG_WARNING, "pts third argument 24HH is deprected, use pts:hms24hh instead\n");
                fmt = "hms24";
            } else {
                av_log(ctx, AV_LOG_ERROR, "Invalid argument '%s', '24HH' was expected\n", argv[2]);
                return AVERROR(EINVAL);
            }
        } else {
            strftime_fmt = argv[2];
        }
    }

    return ff_print_pts(ctx, bp, pts, delta, fmt, strftime_fmt);
}

static int func_frame_num(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;

    av_bprintf(bp, "%d", (int)s->var_values[VAR_N]);
    return 0;
}

static int func_metadata(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;
    AVDictionaryEntry *e = av_dict_get(s->metadata, argv[0], NULL, 0);

    if (e && e->value)
        av_bprintf(bp, "%s", e->value);
    else if (argc >= 2)
        av_bprintf(bp, "%s", argv[1]);
    return 0;
}

static int func_strftime(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    const char *strftime_fmt = argc ? argv[0] : NULL;

    return ff_print_time(ctx, bp, strftime_fmt, !strcmp(function_name, "localtime"));
}

static int func_eval_expr(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;

    return ff_print_eval_expr(ctx, bp, argv[0],
                              fun2_names, fun2,
                              var_names, s->var_values, &s->prng);
}

static int func_eval_expr_int_format(void *ctx, AVBPrint *bp, const char *function_name, unsigned argc, char **argv)
{
    DrawTextContext *s = ((AVFilterContext *)ctx)->priv;
    int ret;
    int positions = -1;

    /*
     * argv[0] expression to be converted to `int`
     * argv[1] format: 'x', 'X', 'd' or 'u'
     * argv[2] positions printed (optional)
     */

    if (argc == 3) {
        ret = sscanf(argv[2], "%u", &positions);
        if (ret != 1) {
            av_log(ctx, AV_LOG_ERROR, "expr_int_format(): Invalid number of positions"
                    " to print: '%s'\n", argv[2]);
            return AVERROR(EINVAL);
        }
    }

    return ff_print_formatted_eval_expr(ctx, bp, argv[0],
                                        fun2_names, fun2,
                                        var_names, s->var_values,
                                        &s->prng,
                                        argv[1][0], positions);
}

static const FFExpandTextFunction expand_text_functions[] = {
    { "e",               1, 1, func_eval_expr },
    { "eif",             2, 3, func_eval_expr_int_format },
    { "expr",            1, 1, func_eval_expr },
    { "expr_int_format", 2, 3, func_eval_expr_int_format },
    { "frame_num",       0, 0, func_frame_num },
    { "gmtime",          0, 1, func_strftime },
    { "localtime",       0, 1, func_strftime },
    { "metadata",        1, 2, func_metadata },
    { "n",               0, 0, func_frame_num },
    { "pict_type",       0, 0, func_pict_type },
    { "pts",             0, 3, func_pts }
};

static av_cold int init(AVFilterContext *ctx)
{
    int err;
    DrawTextContext *s = ctx->priv;

    av_expr_free(s->fontsize_pexpr);
    s->fontsize_pexpr = NULL;

    s->fontsize = 0;
    s->default_fontsize = 16;

    if (!s->fontfile && !CONFIG_LIBFONTCONFIG) {
        av_log(ctx, AV_LOG_ERROR, "No font filename provided\n");
        return AVERROR(EINVAL);
    }

    if (s->textfile) {
        if (s->text) {
            av_log(ctx, AV_LOG_ERROR,
                   "Both text and text file provided. Please provide only one\n");
            return AVERROR(EINVAL);
        }
        if ((err = ff_load_textfile(ctx, (const char *)s->textfile, &s->text, NULL)) < 0)
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

    if (s->text_source_string) {
        s->text_source = text_source_string_parse(s->text_source_string);
        if ((int)s->text_source < 0) {
            av_log(ctx, AV_LOG_ERROR, "Error text source: %s\n", s->text_source_string);
            return AVERROR(EINVAL);
        }
    }

    if (s->text_source == AV_FRAME_DATA_DETECTION_BBOXES) {
        if (s->text) {
            av_log(ctx, AV_LOG_WARNING, "Multiple texts provided, will use text_source only\n");
            av_free(s->text);
        }
        s->text = av_mallocz(AV_DETECTION_BBOX_LABEL_NAME_MAX_SIZE *
                             (AV_NUM_DETECTION_BBOX_CLASSIFY + 1));
        if (!s->text)
            return AVERROR(ENOMEM);
    }

    if (!s->text) {
        av_log(ctx, AV_LOG_ERROR,
               "Either text, a valid file, a timecode or text source must be provided\n");
        return AVERROR(EINVAL);
    }

    s->expand_text = (FFExpandTextContext) {
        .log_ctx = ctx,
        .functions = expand_text_functions,
        .functions_nb = FF_ARRAY_ELEMS(expand_text_functions)
    };

#if CONFIG_LIBFRIBIDI
    if (s->text_shaping)
        if ((err = shape_text(ctx)) < 0)
            return err;
#endif

    if ((err = FT_Init_FreeType(&(s->library)))) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not load FreeType: %s\n", FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    if ((err = load_font(ctx)) < 0)
        return err;

    if ((err = update_fontsize(ctx)) < 0)
        return err;

    // Always init the stroker, may be needed if borderw is set via command
    if (FT_Stroker_New(s->library, &s->stroker)) {
        av_log(ctx, AV_LOG_ERROR, "Could not init FT stroker\n");
        return AVERROR_EXTERNAL;
    }

    if (s->borderw) {
        FT_Stroker_Set(s->stroker, s->borderw << 6, FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
    }

    /* load the fallback glyph with code 0 */
    load_glyph(ctx, NULL, 0, 0, 0);

    if (s->exp_mode == EXP_STRFTIME &&
        (strchr(s->text, '%') || strchr(s->text, '\\')))
        av_log(ctx, AV_LOG_WARNING, "expansion=strftime is deprecated.\n");

    av_bprint_init(&s->expanded_text, 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&s->expanded_fontcolor, 0, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    return ff_set_common_formats(ctx, ff_draw_supported_pixel_formats(0));
}

static int glyph_enu_border_free(void *opaque, void *elem)
{
    Glyph *glyph = elem;

    if (glyph->border_glyph != NULL) {
        for (int t = 0; t < 16; ++t) {
            if (glyph->border_bglyph[t] != NULL) {
                FT_Done_Glyph((FT_Glyph)glyph->border_bglyph[t]);
                glyph->border_bglyph[t] = NULL;
            }
        }
        FT_Done_Glyph(glyph->border_glyph);
        glyph->border_glyph = NULL;
    }
    return 0;
}

static int glyph_enu_free(void *opaque, void *elem)
{
    Glyph *glyph = elem;

    FT_Done_Glyph(glyph->glyph);
    FT_Done_Glyph(glyph->border_glyph);
    for (int t = 0; t < 16; ++t) {
        if (glyph->bglyph[t] != NULL) {
            FT_Done_Glyph((FT_Glyph)glyph->bglyph[t]);
        }
        if (glyph->border_bglyph[t] != NULL) {
            FT_Done_Glyph((FT_Glyph)glyph->border_bglyph[t]);
        }
    }
    av_free(elem);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DrawTextContext *s = ctx->priv;

    av_expr_free(s->x_pexpr);
    av_expr_free(s->y_pexpr);
    av_expr_free(s->a_pexpr);
    av_expr_free(s->fontsize_pexpr);

    s->x_pexpr = s->y_pexpr = s->a_pexpr = s->fontsize_pexpr = NULL;

    av_tree_enumerate(s->glyphs, NULL, NULL, glyph_enu_free);
    av_tree_destroy(s->glyphs);
    s->glyphs = NULL;

    FT_Done_Face(s->face);
    FT_Stroker_Done(s->stroker);
    FT_Done_FreeType(s->library);

    av_bprint_finalize(&s->expanded_text, NULL);
    av_bprint_finalize(&s->expanded_fontcolor, NULL);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DrawTextContext *s = ctx->priv;
    char *expr;
    int ret;

    ff_draw_init2(&s->dc, inlink->format, inlink->colorspace, inlink->color_range, FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&s->dc, &s->fontcolor,   s->fontcolor.rgba);
    ff_draw_color(&s->dc, &s->shadowcolor, s->shadowcolor.rgba);
    ff_draw_color(&s->dc, &s->bordercolor, s->bordercolor.rgba);
    ff_draw_color(&s->dc, &s->boxcolor,    s->boxcolor.rgba);

    s->var_values[VAR_w]    = s->var_values[VAR_W] = s->var_values[VAR_MAIN_W] = inlink->w;
    s->var_values[VAR_h]    = s->var_values[VAR_H] = s->var_values[VAR_MAIN_H] = inlink->h;
    s->var_values[VAR_SAR]  = inlink->sample_aspect_ratio.num ? av_q2d(inlink->sample_aspect_ratio) : 1;
    s->var_values[VAR_DAR]  = (double)inlink->w / inlink->h * s->var_values[VAR_SAR];
    s->var_values[VAR_HSUB] = 1 << s->dc.hsub_max;
    s->var_values[VAR_VSUB] = 1 << s->dc.vsub_max;
    s->var_values[VAR_X]    = NAN;
    s->var_values[VAR_Y]    = NAN;
    s->var_values[VAR_T]    = NAN;

    av_lfg_init(&s->prng, av_get_random_seed());

    av_expr_free(s->x_pexpr);
    av_expr_free(s->y_pexpr);
    av_expr_free(s->a_pexpr);
    s->x_pexpr = s->y_pexpr = s->a_pexpr = NULL;

    if ((ret = av_expr_parse(&s->x_pexpr, expr = s->x_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&s->y_pexpr, expr = s->y_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0 ||
        (ret = av_expr_parse(&s->a_pexpr, expr = s->a_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to parse expression: %s \n", expr);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int command(AVFilterContext *ctx, const char *cmd, const char *arg, char *res, int res_len, int flags)
{
    DrawTextContext *old = ctx->priv;
    DrawTextContext *new = NULL;
    int ret;

    if (!strcmp(cmd, "reinit")) {
        new = av_mallocz(sizeof(DrawTextContext));
        if (!new)
            return AVERROR(ENOMEM);

        new->class = &drawtext_class;
        ret = av_opt_copy(new, old);
        if (ret < 0)
            goto fail;

        ctx->priv = new;
        ret = av_set_options_string(ctx, arg, "=", ":");
        if (ret < 0) {
            ctx->priv = old;
            goto fail;
        }

        ret = init(ctx);
        if (ret < 0) {
            uninit(ctx);
            ctx->priv = old;
            goto fail;
        }

        new->reinit = 1;

        ctx->priv = old;
        uninit(ctx);
        av_opt_free(old);
        av_freep(&old);

        ctx->priv = new;
        return config_input(ctx->inputs[0]);
    } else {
        int old_borderw = old->borderw;
        if ((ret = ff_filter_process_command(ctx, cmd, arg, res, res_len, flags)) < 0) {
            return ret;
        }
        if (old->borderw != old_borderw) {
            FT_Stroker_Set(old->stroker, old->borderw << 6, FT_STROKER_LINECAP_ROUND,
                        FT_STROKER_LINEJOIN_ROUND, 0);
            // Dispose the old border glyphs
            av_tree_enumerate(old->glyphs, NULL, NULL, glyph_enu_border_free);
        } else if (strcmp(cmd, "fontsize") == 0) {
            av_expr_free(old->fontsize_pexpr);
            old->fontsize_pexpr = NULL;
            old->blank_advance64 = 0;
        }
        return config_input(ctx->inputs[0]);
    }

fail:
    av_log(ctx, AV_LOG_ERROR, "Failed to process command. Continuing with existing parameters.\n");
    av_freep(&new);
    return ret;
}

static void update_color_with_alpha(DrawTextContext *s, FFDrawColor *color, const FFDrawColor incolor)
{
    *color = incolor;
    color->rgba[3] = (color->rgba[3] * s->alpha) / 255;
    ff_draw_color(&s->dc, color, color->rgba);
}

static void update_alpha(DrawTextContext *s)
{
    double alpha = av_expr_eval(s->a_pexpr, s->var_values, &s->prng);

    if (isnan(alpha))
        return;

    if (alpha >= 1.0)
        s->alpha = 255;
    else if (alpha <= 0)
        s->alpha = 0;
    else
        s->alpha = 256 * alpha;
}

static int draw_glyphs(DrawTextContext *s, AVFrame *frame,
                       FFDrawColor *color,
                       TextMetrics *metrics,
                       int x, int y, int borderw)
{
    int g, l, x1, y1, w1, h1, idx;
    int dx = 0, dy = 0, pdx = 0;
    GlyphInfo *info;
    Glyph dummy = { 0 }, *glyph;
    FT_Bitmap bitmap;
    FT_BitmapGlyph b_glyph;
    uint8_t j_left = 0, j_right = 0, j_top = 0, j_bottom = 0;
    int line_w, offset_y = 0;
    int clip_x = 0, clip_y = 0;

    j_left = !!(s->text_align & TA_LEFT);
    j_right = !!(s->text_align & TA_RIGHT);
    j_top = !!(s->text_align & TA_TOP);
    j_bottom = !!(s->text_align & TA_BOTTOM);

    if (j_top && j_bottom) {
        offset_y = (s->box_height - metrics->height) / 2;
    } else if (j_bottom) {
        offset_y = s->box_height - metrics->height;
    }

    if ((!j_left || j_right) && !s->tab_warning_printed && s->tab_count > 0) {
        s->tab_warning_printed = 1;
        av_log(s, AV_LOG_WARNING, "Tab characters are only supported with left horizontal alignment\n");
    }

    clip_x = FFMIN(metrics->rect_x + s->box_width + s->bb_right, frame->width);
    clip_y = FFMIN(metrics->rect_y + s->box_height + s->bb_bottom, frame->height);

    for (l = 0; l < s->line_count; ++l) {
        TextLine *line = &s->lines[l];
        line_w = POS_CEIL(line->width64, 64);
        for (g = 0; g < line->hb_data.glyph_count; ++g) {
            info = &line->glyphs[g];
            dummy.fontsize = s->fontsize;
            dummy.code = info->code;
            glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);
            if (!glyph) {
                return AVERROR(EINVAL);
            }

            idx = get_subpixel_idx(info->shift_x64, info->shift_y64);
            b_glyph = borderw ? glyph->border_bglyph[idx] : glyph->bglyph[idx];
            bitmap = b_glyph->bitmap;
            x1 = x + info->x + b_glyph->left;
            y1 = y + info->y - b_glyph->top + offset_y;
            w1 = bitmap.width;
            h1 = bitmap.rows;

            if (j_left && j_right) {
                x1 += (s->box_width - line_w) / 2;
            } else if (j_right) {
                x1 += s->box_width - line_w;
            }

            // Offset of the glyph's bitmap in the visible region
            dx = dy = 0;
            if (x1 < metrics->rect_x - s->bb_left) {
                dx = metrics->rect_x - s->bb_left - x1;
                x1 = metrics->rect_x - s->bb_left;
            }
            if (y1 < metrics->rect_y - s->bb_top) {
                dy = metrics->rect_y - s->bb_top - y1;
                y1 = metrics->rect_y - s->bb_top;
            }

            // check if the glyph is empty or out of the clipping region
            if (dx >= w1 || dy >= h1 || x1 >= clip_x || y1 >= clip_y) {
                continue;
            }

            pdx = dx + dy * bitmap.pitch;
            w1 = FFMIN(clip_x - x1, w1 - dx);
            h1 = FFMIN(clip_y - y1, h1 - dy);

            ff_blend_mask(&s->dc, color, frame->data, frame->linesize, clip_x, clip_y,
                bitmap.buffer + pdx, bitmap.pitch, w1, h1, 3, 0, x1, y1);
        }
    }

    return 0;
}

// Shapes a line of text using libharfbuzz
static int shape_text_hb(DrawTextContext *s, HarfbuzzData* hb, const char* text, int textLen)
{
    hb->buf = hb_buffer_create();
    if(!hb_buffer_allocation_successful(hb->buf)) {
        return AVERROR(ENOMEM);
    }
    hb_buffer_set_direction(hb->buf, HB_DIRECTION_LTR);
    hb_buffer_set_script(hb->buf, HB_SCRIPT_LATIN);
    hb_buffer_set_language(hb->buf, hb_language_from_string("en", -1));
    hb_buffer_guess_segment_properties(hb->buf);
    hb->font = hb_ft_font_create(s->face, NULL);
    if(hb->font == NULL) {
        return AVERROR(ENOMEM);
    }
    hb_ft_font_set_funcs(hb->font);
    hb_buffer_add_utf8(hb->buf, text, textLen, 0, -1);
    hb_shape(hb->font, hb->buf, NULL, 0);
    hb->glyph_info = hb_buffer_get_glyph_infos(hb->buf, &hb->glyph_count);
    hb->glyph_pos = hb_buffer_get_glyph_positions(hb->buf, &hb->glyph_count);

    return 0;
}

static void hb_destroy(HarfbuzzData *hb)
{
    hb_buffer_destroy(hb->buf);
    hb_font_destroy(hb->font);
    hb->buf = NULL;
    hb->font = NULL;
    hb->glyph_info = NULL;
    hb->glyph_pos = NULL;
}

static int measure_text(AVFilterContext *ctx, TextMetrics *metrics)
{
    DrawTextContext *s = ctx->priv;
    char *text = s->expanded_text.str;
    char *textdup = NULL, *start = NULL;
    int num_chars = 0;
    int width64 = 0, w64 = 0;
    int cur_min_y64 = 0, first_max_y64 = -32000;
    int first_min_x64 = 32000, last_max_x64 = -32000;
    int min_y64 = 32000, max_y64 = -32000, min_x64 = 32000, max_x64 = -32000;
    int line_count = 0;
    uint32_t code = 0;
    Glyph *glyph = NULL;

    int i, tab_idx = 0, last_tab_idx = 0, line_offset = 0;
    char* p;
    int ret = 0;

    // Count the lines and the tab characters
    s->tab_count = 0;
    for (i = 0, p = text; 1; i++) {
        GET_UTF8(code, *p ? *p++ : 0, code = 0xfffd; goto continue_on_failed;);
continue_on_failed:
        if (ff_is_newline(code) || code == 0) {
            ++line_count;
            if (code == 0) {
                break;
            }
        } else if (code == '\t') {
            ++s->tab_count;
        }
    }

    // Evaluate the width of the space character if needed to replace tabs
    if (s->tab_count > 0 && !s->blank_advance64) {
        HarfbuzzData hb_data;
        ret = shape_text_hb(s, &hb_data, " ", 1);
        if(ret != 0) {
            goto done;
        }
        s->blank_advance64 = hb_data.glyph_pos[0].x_advance;
        hb_destroy(&hb_data);
    }

    s->line_count = line_count;
    s->lines = av_mallocz(line_count * sizeof(TextLine));
    s->tab_clusters = av_mallocz(s->tab_count * sizeof(uint32_t));
    for (i = 0; i < s->tab_count; ++i) {
        s->tab_clusters[i] = -1;
    }

    start = textdup = av_strdup(text);
    if (textdup == NULL) {
        ret = AVERROR(ENOMEM);
        goto done;
    }
    line_count = 0;
    for (i = 0, p = textdup; 1; i++) {
        if (*p == '\t') {
            s->tab_clusters[tab_idx++] = i;
            *p = ' ';
        }
        GET_UTF8(code, *p ? *p++ : 0, code = 0xfffd; goto continue_on_failed2;);
continue_on_failed2:
        if (ff_is_newline(code) || code == 0) {
            TextLine *cur_line = &s->lines[line_count];
            HarfbuzzData *hb = &cur_line->hb_data;
            cur_line->cluster_offset = line_offset;
            ret = shape_text_hb(s, hb, start, num_chars);
            if (ret != 0) {
                goto done;
            }
            w64 = 0;
            cur_min_y64 = 32000;
            for (int t = 0; t < hb->glyph_count; ++t) {
                uint8_t is_tab = last_tab_idx < s->tab_count &&
                    hb->glyph_info[t].cluster == s->tab_clusters[last_tab_idx] - line_offset;
                if (is_tab) {
                    ++last_tab_idx;
                }
                ret = load_glyph(ctx, &glyph, hb->glyph_info[t].codepoint, -1, -1);
                if (ret != 0) {
                    goto done;
                }
                if (line_count == 0) {
                    first_max_y64 = FFMAX(glyph->bbox.yMax, first_max_y64);
                }
                if (t == 0) {
                    cur_line->offset_left64 = glyph->bbox.xMin;
                    first_min_x64 = FFMIN(glyph->bbox.xMin, first_min_x64);
                }
                if (t == hb->glyph_count - 1) {
                    // The following code measures the width of the line up to the last
                    // character's horizontal advance
                    int last_char_width = hb->glyph_pos[t].x_advance;

                    // The following code measures the width of the line up to the rightmost
                    // visible pixel of the last character
                    // int last_char_width = glyph->bbox.xMax;

                    w64 += last_char_width;
                    last_max_x64 = FFMAX(last_char_width, last_max_x64);
                    cur_line->offset_right64 = last_char_width;
                } else {
                    if (is_tab) {
                        int size = s->blank_advance64 * s->tabsize;
                        w64 = (w64 / size + 1) * size;
                    } else {
                        w64 += hb->glyph_pos[t].x_advance;
                    }
                }
                cur_min_y64 = FFMIN(glyph->bbox.yMin, cur_min_y64);
                min_y64 = FFMIN(glyph->bbox.yMin, min_y64);
                max_y64 = FFMAX(glyph->bbox.yMax, max_y64);
                min_x64 = FFMIN(glyph->bbox.xMin, min_x64);
                max_x64 = FFMAX(glyph->bbox.xMax, max_x64);
            }

            cur_line->width64 = w64;

            av_log(s, AV_LOG_DEBUG, "  Line: %d -- glyphs count: %d - width64: %d - offset_left64: %d - offset_right64: %d)\n",
                line_count, hb->glyph_count, cur_line->width64, cur_line->offset_left64, cur_line->offset_right64);

            if (w64 > width64) {
                width64 = w64;
            }
            num_chars = -1;
            start = p;
            ++line_count;
            line_offset = i + 1;
        }

        if (code == 0) break;
        ++num_chars;
    }

    metrics->line_height64 = s->face->size->metrics.height;

    metrics->width = POS_CEIL(width64, 64);
    if (s->y_align == YA_FONT) {
        metrics->height = POS_CEIL(metrics->line_height64 * line_count, 64);
    } else {
        int height64 = (metrics->line_height64 + s->line_spacing * 64) *
            (FFMAX(0, line_count - 1)) + first_max_y64 - cur_min_y64;
        metrics->height = POS_CEIL(height64, 64);
    }
    metrics->offset_top64 = first_max_y64;
    metrics->offset_right64 = last_max_x64;
    metrics->offset_bottom64 = cur_min_y64;
    metrics->offset_left64 = first_min_x64;
    metrics->min_x64 = min_x64;
    metrics->min_y64 = min_y64;
    metrics->max_x64 = max_x64;
    metrics->max_y64 = max_y64;

done:
    av_free(textdup);
    return ret;
}

static int draw_text(AVFilterContext *ctx, AVFrame *frame)
{
    DrawTextContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink *inl = ff_filter_link(inlink);
    int x = 0, y = 0, ret;
    int shift_x64, shift_y64;
    int x64, y64;
    Glyph *glyph = NULL;

    time_t now = time(0);
    struct tm ltime;
    AVBPrint *bp = &s->expanded_text;

    FFDrawColor fontcolor;
    FFDrawColor shadowcolor;
    FFDrawColor bordercolor;
    FFDrawColor boxcolor;

    int width = frame->width;
    int height = frame->height;
    int rec_x = 0, rec_y = 0, rec_width = 0, rec_height = 0;
    int is_outside = 0;
    int last_tab_idx = 0;

    TextMetrics metrics;

    av_bprint_clear(bp);

    if (s->basetime != AV_NOPTS_VALUE)
        now= frame->pts*av_q2d(ctx->inputs[0]->time_base) + s->basetime/1000000;

    switch (s->exp_mode) {
    case EXP_NONE:
        av_bprintf(bp, "%s", s->text);
        break;
    case EXP_NORMAL:
        if ((ret = ff_expand_text(&s->expand_text, s->text, &s->expanded_text)) < 0)
            return ret;
        break;
    case EXP_STRFTIME:
        localtime_r(&now, &ltime);
        av_bprint_strftime(bp, s->text, &ltime);
        break;
    }

    if (s->tc_opt_string) {
        char tcbuf[AV_TIMECODE_STR_SIZE];
        av_timecode_make_string(&s->tc, tcbuf, inl->frame_count_out);
        av_bprint_clear(bp);
        av_bprintf(bp, "%s%s", s->text, tcbuf);
    }

    if (!av_bprint_is_complete(bp))
        return AVERROR(ENOMEM);

    if (s->fontcolor_expr[0]) {
        /* If expression is set, evaluate and replace the static value */
        av_bprint_clear(&s->expanded_fontcolor);
        if ((ret = ff_expand_text(&s->expand_text, s->fontcolor_expr, &s->expanded_fontcolor)) < 0)
            return ret;
        if (!av_bprint_is_complete(&s->expanded_fontcolor))
            return AVERROR(ENOMEM);
        av_log(s, AV_LOG_DEBUG, "Evaluated fontcolor is '%s'\n", s->expanded_fontcolor.str);
        ret = av_parse_color(s->fontcolor.rgba, s->expanded_fontcolor.str, -1, s);
        if (ret)
            return ret;
        ff_draw_color(&s->dc, &s->fontcolor, s->fontcolor.rgba);
    }

    if ((ret = update_fontsize(ctx)) < 0) {
        return ret;
    }

    if ((ret = measure_text(ctx, &metrics)) < 0) {
        return ret;
    }

    s->max_glyph_h = POS_CEIL(metrics.max_y64 - metrics.min_y64, 64);
    s->max_glyph_w = POS_CEIL(metrics.max_x64 - metrics.min_x64, 64);

    s->var_values[VAR_TW] = s->var_values[VAR_TEXT_W] = metrics.width;
    s->var_values[VAR_TH] = s->var_values[VAR_TEXT_H] = metrics.height;

    s->var_values[VAR_MAX_GLYPH_W] = s->max_glyph_w;
    s->var_values[VAR_MAX_GLYPH_H] = s->max_glyph_h;
    s->var_values[VAR_MAX_GLYPH_A] = s->var_values[VAR_ASCENT] = POS_CEIL(metrics.max_y64, 64);
    s->var_values[VAR_FONT_A] = s->face->size->metrics.ascender / 64;
    s->var_values[VAR_MAX_GLYPH_D] = s->var_values[VAR_DESCENT] = POS_CEIL(metrics.min_y64, 64);
    s->var_values[VAR_FONT_D] = -s->face->size->metrics.descender / 64;

    s->var_values[VAR_TOP_A] = POS_CEIL(metrics.offset_top64, 64);
    s->var_values[VAR_BOTTOM_D] = -POS_CEIL(metrics.offset_bottom64, 64);
    s->var_values[VAR_LINE_H] = s->var_values[VAR_LH] = metrics.line_height64 / 64.;

    if (s->text_source == AV_FRAME_DATA_DETECTION_BBOXES) {
        s->var_values[VAR_X] = s->x;
        s->var_values[VAR_Y] = s->y;
    } else {
        s->x = s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, &s->prng);
        s->y = s->var_values[VAR_Y] = av_expr_eval(s->y_pexpr, s->var_values, &s->prng);
        /* It is necessary if x is expressed from y  */
        s->x = s->var_values[VAR_X] = av_expr_eval(s->x_pexpr, s->var_values, &s->prng);
    }

    update_alpha(s);
    update_color_with_alpha(s, &fontcolor  , s->fontcolor  );
    update_color_with_alpha(s, &shadowcolor, s->shadowcolor);
    update_color_with_alpha(s, &bordercolor, s->bordercolor);
    update_color_with_alpha(s, &boxcolor   , s->boxcolor   );

    if (s->draw_box && s->boxborderw) {
        int bbsize[4];
        int count;
        count = string_to_array(s->boxborderw, bbsize, 4);
        if (count == 1) {
            s->bb_top = s->bb_right = s->bb_bottom = s->bb_left = bbsize[0];
        } else if (count == 2) {
            s->bb_top = s->bb_bottom = bbsize[0];
            s->bb_right = s->bb_left = bbsize[1];
        } else if (count == 3) {
            s->bb_top = bbsize[0];
            s->bb_right = s->bb_left = bbsize[1];
            s->bb_bottom = bbsize[2];
        } else if (count == 4) {
            s->bb_top = bbsize[0];
            s->bb_right = bbsize[1];
            s->bb_bottom = bbsize[2];
            s->bb_left = bbsize[3];
        }
    } else {
        s->bb_top = s->bb_right = s->bb_bottom = s->bb_left = 0;
    }

    if (s->fix_bounds) {
        /* calculate footprint of text effects */
        int borderoffset  = s->borderw  ? FFMAX(s->borderw, 0) : 0;

        int offsetleft = FFMAX3(FFMAX(s->bb_left, 0), borderoffset,
                                (s->shadowx < 0 ? FFABS(s->shadowx) : 0));
        int offsettop = FFMAX3(FFMAX(s->bb_top, 0), borderoffset,
                                (s->shadowy < 0 ? FFABS(s->shadowy) : 0));
        int offsetright = FFMAX3(FFMAX(s->bb_right, 0), borderoffset,
                                 (s->shadowx > 0 ? s->shadowx : 0));
        int offsetbottom = FFMAX3(FFMAX(s->bb_bottom, 0), borderoffset,
                                  (s->shadowy > 0 ? s->shadowy : 0));

        if (s->x - offsetleft < 0) s->x = offsetleft;
        if (s->y - offsettop < 0)  s->y = offsettop;

        if (s->x + metrics.width + offsetright > width)
            s->x = FFMAX(width - metrics.width - offsetright, 0);
        if (s->y + metrics.height + offsetbottom > height)
            s->y = FFMAX(height - metrics.height - offsetbottom, 0);
    }

    x = 0;
    y = 0;
    x64 = (int)(s->x * 64.);
    if (s->y_align == YA_FONT) {
        y64 = (int)(s->y * 64. + s->face->size->metrics.ascender);
    } else if (s->y_align == YA_BASELINE) {
        y64 = (int)(s->y * 64.);
    } else {
        y64 = (int)(s->y * 64. + metrics.offset_top64);
    }

    for (int l = 0; l < s->line_count; ++l) {
        TextLine *line = &s->lines[l];
        HarfbuzzData *hb = &line->hb_data;
        line->glyphs = av_mallocz(hb->glyph_count * sizeof(GlyphInfo));

        for (int t = 0; t < hb->glyph_count; ++t) {
            GlyphInfo *g_info = &line->glyphs[t];
            uint8_t is_tab = last_tab_idx < s->tab_count &&
                hb->glyph_info[t].cluster == s->tab_clusters[last_tab_idx] - line->cluster_offset;
            int true_x, true_y;
            if (is_tab) {
                ++last_tab_idx;
            }
            true_x = x + hb->glyph_pos[t].x_offset;
            true_y = y + hb->glyph_pos[t].y_offset;
            shift_x64 = (((x64 + true_x) >> 4) & 0b0011) << 4;
            shift_y64 = ((4 - (((y64 + true_y) >> 4) & 0b0011)) & 0b0011) << 4;

            ret = load_glyph(ctx, &glyph, hb->glyph_info[t].codepoint, shift_x64, shift_y64);
            if (ret != 0) {
                return ret;
            }
            g_info->code = hb->glyph_info[t].codepoint;
            g_info->x = (x64 + true_x) >> 6;
            g_info->y = ((y64 + true_y) >> 6) + (shift_y64 > 0 ? 1 : 0);
            g_info->shift_x64 = shift_x64;
            g_info->shift_y64 = shift_y64;

            if (!is_tab) {
                x += hb->glyph_pos[t].x_advance;
            } else {
                int size = s->blank_advance64 * s->tabsize;
                x = (x / size + 1) * size;
            }
            y += hb->glyph_pos[t].y_advance;
        }

        y += metrics.line_height64 + s->line_spacing * 64;
        x = 0;
    }

    metrics.rect_x = s->x;
    if (s->y_align == YA_BASELINE) {
        metrics.rect_y = s->y - metrics.offset_top64 / 64;
    } else {
        metrics.rect_y = s->y;
    }

    s->box_width = s->boxw == 0 ? metrics.width : s->boxw;
    s->box_height = s->boxh == 0 ? metrics.height : s->boxh;

    if (!s->draw_box) {
        // Create a border for the clipping region to take into account subpixel
        // errors in text measurement and effects.
        int borderoffset = s->borderw ? FFMAX(s->borderw, 0) : 0;
        s->bb_left = borderoffset + (s->shadowx < 0 ? FFABS(s->shadowx) : 0) + 1;
        s->bb_top = borderoffset + (s->shadowy < 0 ? FFABS(s->shadowy) : 0) + 1;
        s->bb_right = borderoffset + (s->shadowx > 0 ? s->shadowx : 0) + 1;
        s->bb_bottom = borderoffset + (s->shadowy > 0 ? s->shadowy : 0) + 1;
    }

    /* Check if the whole box is out of the frame */
    is_outside = metrics.rect_x - s->bb_left >= width ||
                    metrics.rect_y - s->bb_top >= height ||
                    metrics.rect_x + s->box_width + s->bb_right <= 0 ||
                    metrics.rect_y + s->box_height + s->bb_bottom <= 0;

    if (!is_outside) {
        /* draw box */
        if (s->draw_box) {
            rec_x = metrics.rect_x - s->bb_left;
            rec_y = metrics.rect_y - s->bb_top;
            rec_width = s->box_width + s->bb_right + s->bb_left;
            rec_height = s->box_height + s->bb_bottom + s->bb_top;
            ff_blend_rectangle(&s->dc, &boxcolor,
                frame->data, frame->linesize, width, height,
                rec_x, rec_y, rec_width, rec_height);
        }

        if (s->shadowx || s->shadowy) {
            if ((ret = draw_glyphs(s, frame, &shadowcolor, &metrics,
                    s->shadowx, s->shadowy, s->borderw)) < 0) {
                return ret;
            }
        }

        if (s->borderw) {
            if ((ret = draw_glyphs(s, frame, &bordercolor, &metrics,
                    0, 0, s->borderw)) < 0) {
                return ret;
            }
        }

        if ((ret = draw_glyphs(s, frame, &fontcolor, &metrics, 0,
                0, 0)) < 0) {
            return ret;
        }
    }

    // FREE data structures
    for (int l = 0; l < s->line_count; ++l) {
        TextLine *line = &s->lines[l];
        av_freep(&line->glyphs);
        hb_destroy(&line->hb_data);
    }
    av_freep(&s->lines);
    av_freep(&s->tab_clusters);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    FilterLink *inl = ff_filter_link(inlink);
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DrawTextContext *s = ctx->priv;
    int ret;
    const AVDetectionBBoxHeader *header = NULL;
    const AVDetectionBBox *bbox;
    AVFrameSideData *sd;
    int loop = 1;

    if (s->text_source == AV_FRAME_DATA_DETECTION_BBOXES) {
        sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DETECTION_BBOXES);
        if (sd) {
            header = (AVDetectionBBoxHeader *)sd->data;
            loop = header->nb_bboxes;
        } else {
            av_log(s, AV_LOG_WARNING, "No detection bboxes.\n");
            return ff_filter_frame(outlink, frame);
        }
    }

    if (s->reload && !(inl->frame_count_out % s->reload)) {
        if ((ret = ff_load_textfile(ctx, (const char *)s->textfile, &s->text, NULL)) < 0) {
            av_frame_free(&frame);
            return ret;
        }
#if CONFIG_LIBFRIBIDI
        if (s->text_shaping)
            if ((ret = shape_text(ctx)) < 0) {
                av_frame_free(&frame);
                return ret;
            }
#endif
    }

    s->var_values[VAR_N] = inl->frame_count_out + s->start_number;
    s->var_values[VAR_T] = frame->pts == AV_NOPTS_VALUE ?
        NAN : frame->pts * av_q2d(inlink->time_base);

    s->var_values[VAR_PICT_TYPE] = frame->pict_type;
#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
    s->var_values[VAR_PKT_POS] = frame->pkt_pos;
    s->var_values[VAR_PKT_SIZE] = frame->pkt_size;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    s->var_values[VAR_DURATION] = frame->duration * av_q2d(inlink->time_base);

    s->metadata = frame->metadata;

    for (int i = 0; i < loop; i++) {
        if (header) {
            bbox = av_get_detection_bbox(header, i);
            strcpy(s->text, bbox->detect_label);
            for (int j = 0; j < bbox->classify_count; j++) {
                strcat(s->text, ", ");
                strcat(s->text, bbox->classify_labels[j]);
            }
            s->x = bbox->x;
            s->y = bbox->y - s->fontsize;
        }
        draw_text(ctx, frame);
    }

    return ff_filter_frame(outlink, frame);
}

static const AVFilterPad avfilter_vf_drawtext_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

const AVFilter ff_vf_drawtext = {
    .name          = "drawtext",
    .description   = NULL_IF_CONFIG_SMALL("Draw text on top of video frames using libfreetype library."),
    .priv_size     = sizeof(DrawTextContext),
    .priv_class    = &drawtext_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_drawtext_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC(query_formats),
    .process_command = command,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
