/*
 * Closed Caption Decoding
 * Copyright (c) 2015 Anshul Maheshwari
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

#include "avcodec.h"
#include "ass.h"
#include "codec_internal.h"
#include "libavutil/opt.h"

#define SCREEN_ROWS 15
#define SCREEN_COLUMNS 32

#define SET_FLAG(var, val)   ( (var) |=   ( 1 << (val)) )
#define UNSET_FLAG(var, val) ( (var) &=  ~( 1 << (val)) )
#define CHECK_FLAG(var, val) ( (var) &    ( 1 << (val)) )

static const AVRational ms_tb = {1, 1000};

enum cc_mode {
    CCMODE_POPON,
    CCMODE_PAINTON,
    CCMODE_ROLLUP,
    CCMODE_TEXT,
};

enum cc_color_code {
    CCCOL_WHITE,
    CCCOL_GREEN,
    CCCOL_BLUE,
    CCCOL_CYAN,
    CCCOL_RED,
    CCCOL_YELLOW,
    CCCOL_MAGENTA,
    CCCOL_USERDEFINED,
    CCCOL_BLACK,
    CCCOL_TRANSPARENT,
};

enum cc_font {
    CCFONT_REGULAR,
    CCFONT_ITALICS,
    CCFONT_UNDERLINED,
    CCFONT_UNDERLINED_ITALICS,
};

enum cc_charset {
    CCSET_BASIC_AMERICAN,
    CCSET_SPECIAL_AMERICAN,
    CCSET_EXTENDED_SPANISH_FRENCH_MISC,
    CCSET_EXTENDED_PORTUGUESE_GERMAN_DANISH,
};

static const char *charset_overrides[4][128] =
{
    [CCSET_BASIC_AMERICAN] = {
        [0x27] = "\u2019",
        [0x2a] = "\u00e1",
        [0x5c] = "\u00e9",
        [0x5e] = "\u00ed",
        [0x5f] = "\u00f3",
        [0x60] = "\u00fa",
        [0x7b] = "\u00e7",
        [0x7c] = "\u00f7",
        [0x7d] = "\u00d1",
        [0x7e] = "\u00f1",
        [0x7f] = "\u2588"
    },
    [CCSET_SPECIAL_AMERICAN] = {
        [0x30] = "\u00ae",
        [0x31] = "\u00b0",
        [0x32] = "\u00bd",
        [0x33] = "\u00bf",
        [0x34] = "\u2122",
        [0x35] = "\u00a2",
        [0x36] = "\u00a3",
        [0x37] = "\u266a",
        [0x38] = "\u00e0",
        [0x39] = "\u00A0",
        [0x3a] = "\u00e8",
        [0x3b] = "\u00e2",
        [0x3c] = "\u00ea",
        [0x3d] = "\u00ee",
        [0x3e] = "\u00f4",
        [0x3f] = "\u00fb",
    },
    [CCSET_EXTENDED_SPANISH_FRENCH_MISC] = {
        [0x20] = "\u00c1",
        [0x21] = "\u00c9",
        [0x22] = "\u00d3",
        [0x23] = "\u00da",
        [0x24] = "\u00dc",
        [0x25] = "\u00fc",
        [0x26] = "\u00b4",
        [0x27] = "\u00a1",
        [0x28] = "*",
        [0x29] = "\u2018",
        [0x2a] = "-",
        [0x2b] = "\u00a9",
        [0x2c] = "\u2120",
        [0x2d] = "\u00b7",
        [0x2e] = "\u201c",
        [0x2f] = "\u201d",
        [0x30] = "\u00c0",
        [0x31] = "\u00c2",
        [0x32] = "\u00c7",
        [0x33] = "\u00c8",
        [0x34] = "\u00ca",
        [0x35] = "\u00cb",
        [0x36] = "\u00eb",
        [0x37] = "\u00ce",
        [0x38] = "\u00cf",
        [0x39] = "\u00ef",
        [0x3a] = "\u00d4",
        [0x3b] = "\u00d9",
        [0x3c] = "\u00f9",
        [0x3d] = "\u00db",
        [0x3e] = "\u00ab",
        [0x3f] = "\u00bb",
    },
    [CCSET_EXTENDED_PORTUGUESE_GERMAN_DANISH] = {
        [0x20] = "\u00c3",
        [0x21] = "\u00e3",
        [0x22] = "\u00cd",
        [0x23] = "\u00cc",
        [0x24] = "\u00ec",
        [0x25] = "\u00d2",
        [0x26] = "\u00f2",
        [0x27] = "\u00d5",
        [0x28] = "\u00f5",
        [0x29] = "{",
        [0x2a] = "}",
        [0x2b] = "\\",
        [0x2c] = "^",
        [0x2d] = "_",
        [0x2e] = "|",
        [0x2f] = "~",
        [0x30] = "\u00c4",
        [0x31] = "\u00e4",
        [0x32] = "\u00d6",
        [0x33] = "\u00f6",
        [0x34] = "\u00df",
        [0x35] = "\u00a5",
        [0x36] = "\u00a4",
        [0x37] = "\u00a6",
        [0x38] = "\u00c5",
        [0x39] = "\u00e5",
        [0x3a] = "\u00d8",
        [0x3b] = "\u00f8",
        [0x3c] = "\u250c",
        [0x3d] = "\u2510",
        [0x3e] = "\u2514",
        [0x3f] = "\u2518",
    },
};

static const unsigned char bg_attribs[8] = // Color
{
    CCCOL_WHITE,
    CCCOL_GREEN,
    CCCOL_BLUE,
    CCCOL_CYAN,
    CCCOL_RED,
    CCCOL_YELLOW,
    CCCOL_MAGENTA,
    CCCOL_BLACK,
};

static const unsigned char pac2_attribs[32][3] = // Color, font, ident
{
    { CCCOL_WHITE,   CCFONT_REGULAR,            0 },  // 0x40 || 0x60
    { CCCOL_WHITE,   CCFONT_UNDERLINED,         0 },  // 0x41 || 0x61
    { CCCOL_GREEN,   CCFONT_REGULAR,            0 },  // 0x42 || 0x62
    { CCCOL_GREEN,   CCFONT_UNDERLINED,         0 },  // 0x43 || 0x63
    { CCCOL_BLUE,    CCFONT_REGULAR,            0 },  // 0x44 || 0x64
    { CCCOL_BLUE,    CCFONT_UNDERLINED,         0 },  // 0x45 || 0x65
    { CCCOL_CYAN,    CCFONT_REGULAR,            0 },  // 0x46 || 0x66
    { CCCOL_CYAN,    CCFONT_UNDERLINED,         0 },  // 0x47 || 0x67
    { CCCOL_RED,     CCFONT_REGULAR,            0 },  // 0x48 || 0x68
    { CCCOL_RED,     CCFONT_UNDERLINED,         0 },  // 0x49 || 0x69
    { CCCOL_YELLOW,  CCFONT_REGULAR,            0 },  // 0x4a || 0x6a
    { CCCOL_YELLOW,  CCFONT_UNDERLINED,         0 },  // 0x4b || 0x6b
    { CCCOL_MAGENTA, CCFONT_REGULAR,            0 },  // 0x4c || 0x6c
    { CCCOL_MAGENTA, CCFONT_UNDERLINED,         0 },  // 0x4d || 0x6d
    { CCCOL_WHITE,   CCFONT_ITALICS,            0 },  // 0x4e || 0x6e
    { CCCOL_WHITE,   CCFONT_UNDERLINED_ITALICS, 0 },  // 0x4f || 0x6f
    { CCCOL_WHITE,   CCFONT_REGULAR,            0 },  // 0x50 || 0x70
    { CCCOL_WHITE,   CCFONT_UNDERLINED,         0 },  // 0x51 || 0x71
    { CCCOL_WHITE,   CCFONT_REGULAR,            4 },  // 0x52 || 0x72
    { CCCOL_WHITE,   CCFONT_UNDERLINED,         4 },  // 0x53 || 0x73
    { CCCOL_WHITE,   CCFONT_REGULAR,            8 },  // 0x54 || 0x74
    { CCCOL_WHITE,   CCFONT_UNDERLINED,         8 },  // 0x55 || 0x75
    { CCCOL_WHITE,   CCFONT_REGULAR,           12 },  // 0x56 || 0x76
    { CCCOL_WHITE,   CCFONT_UNDERLINED,        12 },  // 0x57 || 0x77
    { CCCOL_WHITE,   CCFONT_REGULAR,           16 },  // 0x58 || 0x78
    { CCCOL_WHITE,   CCFONT_UNDERLINED,        16 },  // 0x59 || 0x79
    { CCCOL_WHITE,   CCFONT_REGULAR,           20 },  // 0x5a || 0x7a
    { CCCOL_WHITE,   CCFONT_UNDERLINED,        20 },  // 0x5b || 0x7b
    { CCCOL_WHITE,   CCFONT_REGULAR,           24 },  // 0x5c || 0x7c
    { CCCOL_WHITE,   CCFONT_UNDERLINED,        24 },  // 0x5d || 0x7d
    { CCCOL_WHITE,   CCFONT_REGULAR,           28 },  // 0x5e || 0x7e
    { CCCOL_WHITE,   CCFONT_UNDERLINED,        28 }   // 0x5f || 0x7f
    /* total 32 entries */
};

struct Screen {
    /* +1 is used to compensate null character of string */
    uint8_t characters[SCREEN_ROWS+1][SCREEN_COLUMNS+1];
    uint8_t charsets[SCREEN_ROWS+1][SCREEN_COLUMNS+1];
    uint8_t colors[SCREEN_ROWS+1][SCREEN_COLUMNS+1];
    uint8_t bgs[SCREEN_ROWS+1][SCREEN_COLUMNS+1];
    uint8_t fonts[SCREEN_ROWS+1][SCREEN_COLUMNS+1];
    /*
     * Bitmask of used rows; if a bit is not set, the
     * corresponding row is not used.
     * for setting row 1  use row | (1 << 0)
     * for setting row 15 use row | (1 << 14)
     */
    int16_t row_used;
};

typedef struct CCaptionSubContext {
    AVClass *class;
    int real_time;
    int real_time_latency_msec;
    int data_field;
    struct Screen screen[2];
    int active_screen;
    uint8_t cursor_row;
    uint8_t cursor_column;
    uint8_t cursor_color;
    uint8_t bg_color;
    uint8_t cursor_font;
    uint8_t cursor_charset;
    AVBPrint buffer[2];
    int buffer_index;
    int buffer_changed;
    int rollup;
    enum cc_mode mode;
    int64_t buffer_time[2];
    int screen_touched;
    int64_t last_real_time;
    uint8_t prev_cmd[2];
    int readorder;
} CCaptionSubContext;

static av_cold int init_decoder(AVCodecContext *avctx)
{
    int ret;
    CCaptionSubContext *ctx = avctx->priv_data;

    av_bprint_init(&ctx->buffer[0], 0, AV_BPRINT_SIZE_UNLIMITED);
    av_bprint_init(&ctx->buffer[1], 0, AV_BPRINT_SIZE_UNLIMITED);
    /* taking by default roll up to 2 */
    ctx->mode = CCMODE_ROLLUP;
    ctx->bg_color = CCCOL_BLACK;
    ctx->rollup = 2;
    ctx->cursor_row = 10;
    ret = ff_ass_subtitle_header(avctx, "Monospace",
                                 ASS_DEFAULT_FONT_SIZE,
                                 ASS_DEFAULT_COLOR,
                                 ASS_DEFAULT_BACK_COLOR,
                                 ASS_DEFAULT_BOLD,
                                 ASS_DEFAULT_ITALIC,
                                 ASS_DEFAULT_UNDERLINE,
                                 3,
                                 ASS_DEFAULT_ALIGNMENT);
    if (ret < 0) {
        return ret;
    }

    return ret;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    CCaptionSubContext *ctx = avctx->priv_data;
    av_bprint_finalize(&ctx->buffer[0], NULL);
    av_bprint_finalize(&ctx->buffer[1], NULL);
    return 0;
}

static void flush_decoder(AVCodecContext *avctx)
{
    CCaptionSubContext *ctx = avctx->priv_data;
    ctx->screen[0].row_used = 0;
    ctx->screen[1].row_used = 0;
    ctx->prev_cmd[0] = 0;
    ctx->prev_cmd[1] = 0;
    ctx->mode = CCMODE_ROLLUP;
    ctx->rollup = 2;
    ctx->cursor_row = 10;
    ctx->cursor_column = 0;
    ctx->cursor_font = 0;
    ctx->cursor_color = 0;
    ctx->bg_color = CCCOL_BLACK;
    ctx->cursor_charset = 0;
    ctx->active_screen = 0;
    ctx->last_real_time = 0;
    ctx->screen_touched = 0;
    ctx->buffer_changed = 0;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        ctx->readorder = 0;
    av_bprint_clear(&ctx->buffer[0]);
    av_bprint_clear(&ctx->buffer[1]);
}

/**
 * @param ctx closed caption context just to print log
 */
static void write_char(CCaptionSubContext *ctx, struct Screen *screen, char ch)
{
    uint8_t col = ctx->cursor_column;
    char *row = screen->characters[ctx->cursor_row];
    char *font = screen->fonts[ctx->cursor_row];
    char *color = screen->colors[ctx->cursor_row];
    char *bg = screen->bgs[ctx->cursor_row];
    char *charset = screen->charsets[ctx->cursor_row];

    if (col < SCREEN_COLUMNS) {
        row[col] = ch;
        font[col] = ctx->cursor_font;
        color[col] = ctx->cursor_color;
        bg[col] = ctx->bg_color;
        charset[col] = ctx->cursor_charset;
        ctx->cursor_charset = CCSET_BASIC_AMERICAN;
        if (ch) ctx->cursor_column++;
        return;
    }
    /* We have extra space at end only for null character */
    else if (col == SCREEN_COLUMNS && ch == 0) {
        row[col] = ch;
        return;
    }
    else {
        av_log(ctx, AV_LOG_WARNING, "Data Ignored since exceeding screen width\n");
        return;
    }
}

/**
 * This function after validating parity bit, also remove it from data pair.
 * The first byte doesn't pass parity, we replace it with a solid blank
 * and process the pair.
 * If the second byte doesn't pass parity, it returns INVALIDDATA
 * user can ignore the whole pair and pass the other pair.
 */
static int validate_cc_data_pair(const uint8_t *cc_data_pair, uint8_t *hi)
{
    uint8_t cc_valid = (*cc_data_pair & 4) >>2;
    uint8_t cc_type = *cc_data_pair & 3;

    *hi = cc_data_pair[1];

    if (!cc_valid)
        return AVERROR_INVALIDDATA;

    // if EIA-608 data then verify parity.
    if (cc_type==0 || cc_type==1) {
        if (!av_parity(cc_data_pair[2])) {
            return AVERROR_INVALIDDATA;
        }
        if (!av_parity(cc_data_pair[1])) {
            *hi = 0x7F;
        }
    }

    //Skip non-data
    if ((cc_data_pair[0] == 0xFA || cc_data_pair[0] == 0xFC || cc_data_pair[0] == 0xFD)
         && (cc_data_pair[1] & 0x7F) == 0 && (cc_data_pair[2] & 0x7F) == 0)
        return AVERROR_PATCHWELCOME;

    //skip 708 data
    if (cc_type == 3 || cc_type == 2)
        return AVERROR_PATCHWELCOME;

    return 0;
}

static struct Screen *get_writing_screen(CCaptionSubContext *ctx)
{
    switch (ctx->mode) {
    case CCMODE_POPON:
        // use Inactive screen
        return ctx->screen + !ctx->active_screen;
    case CCMODE_PAINTON:
    case CCMODE_ROLLUP:
    case CCMODE_TEXT:
        // use active screen
        return ctx->screen + ctx->active_screen;
    }
    /* It was never an option */
    return NULL;
}

static void roll_up(CCaptionSubContext *ctx)
{
    struct Screen *screen;
    int i, keep_lines;

    if (ctx->mode == CCMODE_TEXT)
        return;

    screen = get_writing_screen(ctx);

    /* +1 signify cursor_row starts from 0
     * Can't keep lines less then row cursor pos
     */
    keep_lines = FFMIN(ctx->cursor_row + 1, ctx->rollup);

    for (i = 0; i < SCREEN_ROWS; i++) {
        if (i > ctx->cursor_row - keep_lines && i <= ctx->cursor_row)
            continue;
        UNSET_FLAG(screen->row_used, i);
    }

    for (i = 0; i < keep_lines && screen->row_used; i++) {
        const int i_row = ctx->cursor_row - keep_lines + i + 1;

        memcpy(screen->characters[i_row], screen->characters[i_row+1], SCREEN_COLUMNS);
        memcpy(screen->colors[i_row], screen->colors[i_row+1], SCREEN_COLUMNS);
        memcpy(screen->bgs[i_row], screen->bgs[i_row+1], SCREEN_COLUMNS);
        memcpy(screen->fonts[i_row], screen->fonts[i_row+1], SCREEN_COLUMNS);
        memcpy(screen->charsets[i_row], screen->charsets[i_row+1], SCREEN_COLUMNS);
        if (CHECK_FLAG(screen->row_used, i_row + 1))
            SET_FLAG(screen->row_used, i_row);
    }

    UNSET_FLAG(screen->row_used, ctx->cursor_row);
}

static int capture_screen(CCaptionSubContext *ctx)
{
    int i, j, tab = 0;
    struct Screen *screen = ctx->screen + ctx->active_screen;
    enum cc_font prev_font = CCFONT_REGULAR;
    enum cc_color_code prev_color = CCCOL_WHITE;
    enum cc_color_code prev_bg_color = CCCOL_BLACK;
    const int bidx = ctx->buffer_index;

    av_bprint_clear(&ctx->buffer[bidx]);

    for (i = 0; screen->row_used && i < SCREEN_ROWS; i++)
    {
        if (CHECK_FLAG(screen->row_used, i)) {
            const char *row = screen->characters[i];
            const char *charset = screen->charsets[i];
            j = 0;
            while (row[j] == ' ' && charset[j] == CCSET_BASIC_AMERICAN)
                j++;
            if (!tab || j < tab)
                tab = j;
        }
    }

    for (i = 0; screen->row_used && i < SCREEN_ROWS; i++)
    {
        if (CHECK_FLAG(screen->row_used, i)) {
            const char *row = screen->characters[i];
            const char *font = screen->fonts[i];
            const char *bg = screen->bgs[i];
            const char *color = screen->colors[i];
            const char *charset = screen->charsets[i];
            const char *override;
            int x, y, seen_char = 0;
            j = 0;

            /* skip leading space */
            while (row[j] == ' ' && charset[j] == CCSET_BASIC_AMERICAN && j < tab)
                j++;

            x = ASS_DEFAULT_PLAYRESX * (0.1 + 0.0250 * j);
            y = ASS_DEFAULT_PLAYRESY * (0.1 + 0.0533 * i);
            av_bprintf(&ctx->buffer[bidx], "{\\an7}{\\pos(%d,%d)}", x, y);

            for (; j < SCREEN_COLUMNS; j++) {
                const char *e_tag = "", *s_tag = "", *c_tag = "", *b_tag = "";

                if (row[j] == 0)
                    break;

                if (prev_font != font[j]) {
                    switch (prev_font) {
                    case CCFONT_ITALICS:
                        e_tag = "{\\i0}";
                        break;
                    case CCFONT_UNDERLINED:
                        e_tag = "{\\u0}";
                        break;
                    case CCFONT_UNDERLINED_ITALICS:
                        e_tag = "{\\u0}{\\i0}";
                        break;
                    }
                    switch (font[j]) {
                    case CCFONT_ITALICS:
                        s_tag = "{\\i1}";
                        break;
                    case CCFONT_UNDERLINED:
                        s_tag = "{\\u1}";
                        break;
                    case CCFONT_UNDERLINED_ITALICS:
                        s_tag = "{\\u1}{\\i1}";
                        break;
                    }
                }
                if (prev_color != color[j]) {
                    switch (color[j]) {
                    case CCCOL_WHITE:
                        c_tag = "{\\c&HFFFFFF&}";
                        break;
                    case CCCOL_GREEN:
                        c_tag = "{\\c&H00FF00&}";
                        break;
                    case CCCOL_BLUE:
                        c_tag = "{\\c&HFF0000&}";
                        break;
                    case CCCOL_CYAN:
                        c_tag = "{\\c&HFFFF00&}";
                        break;
                    case CCCOL_RED:
                        c_tag = "{\\c&H0000FF&}";
                        break;
                    case CCCOL_YELLOW:
                        c_tag = "{\\c&H00FFFF&}";
                        break;
                    case CCCOL_MAGENTA:
                        c_tag = "{\\c&HFF00FF&}";
                        break;
                    }
                }
                if (prev_bg_color != bg[j]) {
                    switch (bg[j]) {
                    case CCCOL_WHITE:
                        b_tag = "{\\3c&HFFFFFF&}";
                        break;
                    case CCCOL_GREEN:
                        b_tag = "{\\3c&H00FF00&}";
                        break;
                    case CCCOL_BLUE:
                        b_tag = "{\\3c&HFF0000&}";
                        break;
                    case CCCOL_CYAN:
                        b_tag = "{\\3c&HFFFF00&}";
                        break;
                    case CCCOL_RED:
                        b_tag = "{\\3c&H0000FF&}";
                        break;
                    case CCCOL_YELLOW:
                        b_tag = "{\\3c&H00FFFF&}";
                        break;
                    case CCCOL_MAGENTA:
                        b_tag = "{\\3c&HFF00FF&}";
                        break;
                    case CCCOL_BLACK:
                        b_tag = "{\\3c&H000000&}";
                        break;
                    }
                }

                prev_font = font[j];
                prev_color = color[j];
                prev_bg_color = bg[j];
                override = charset_overrides[(int)charset[j]][(int)row[j]];
                if (override) {
                    av_bprintf(&ctx->buffer[bidx], "%s%s%s%s%s", e_tag, s_tag, c_tag, b_tag, override);
                    seen_char = 1;
                } else if (row[j] == ' ' && !seen_char) {
                    av_bprintf(&ctx->buffer[bidx], "%s%s%s%s\\h", e_tag, s_tag, c_tag, b_tag);
                } else {
                    av_bprintf(&ctx->buffer[bidx], "%s%s%s%s%c", e_tag, s_tag, c_tag, b_tag, row[j]);
                    seen_char = 1;
                }

            }
            av_bprintf(&ctx->buffer[bidx], "\\N");
        }
    }
    if (!av_bprint_is_complete(&ctx->buffer[bidx]))
        return AVERROR(ENOMEM);
    if (screen->row_used && ctx->buffer[bidx].len >= 2) {
        ctx->buffer[bidx].len -= 2;
        ctx->buffer[bidx].str[ctx->buffer[bidx].len] = 0;
    }
    ctx->buffer_changed = 1;
    return 0;
}

static void update_time(CCaptionSubContext *ctx, int64_t pts)
{
    ctx->buffer_time[0] = ctx->buffer_time[1];
    ctx->buffer_time[1] = pts;
}

static void handle_bgattr(CCaptionSubContext *ctx, uint8_t hi, uint8_t lo)
{
    const int i = (lo & 0xf) >> 1;

    ctx->bg_color = bg_attribs[i];
}

static void handle_textattr(CCaptionSubContext *ctx, uint8_t hi, uint8_t lo)
{
    int i = lo - 0x20;
    struct Screen *screen = get_writing_screen(ctx);

    if (i >= 32)
        return;

    ctx->cursor_color = pac2_attribs[i][0];
    ctx->cursor_font = pac2_attribs[i][1];

    SET_FLAG(screen->row_used, ctx->cursor_row);
    write_char(ctx, screen, ' ');
}

static void handle_pac(CCaptionSubContext *ctx, uint8_t hi, uint8_t lo)
{
    static const int8_t row_map[] = {
        11, -1, 1, 2, 3, 4, 12, 13, 14, 15, 5, 6, 7, 8, 9, 10
    };
    const int index = ( (hi<<1) & 0x0e) | ( (lo>>5) & 0x01 );
    struct Screen *screen = get_writing_screen(ctx);
    int indent, i;

    if (row_map[index] <= 0) {
        av_log(ctx, AV_LOG_DEBUG, "Invalid pac index encountered\n");
        return;
    }

    lo &= 0x1f;

    ctx->cursor_row = row_map[index] - 1;
    ctx->cursor_color =  pac2_attribs[lo][0];
    ctx->cursor_font = pac2_attribs[lo][1];
    ctx->cursor_charset = CCSET_BASIC_AMERICAN;
    ctx->cursor_column = 0;
    indent = pac2_attribs[lo][2];
    for (i = 0; i < indent; i++) {
        write_char(ctx, screen, ' ');
    }
}

static int handle_edm(CCaptionSubContext *ctx)
{
    struct Screen *screen = ctx->screen + ctx->active_screen;
    int ret;

    // In buffered mode, keep writing to screen until it is wiped.
    // Before wiping the display, capture contents to emit subtitle.
    if (!ctx->real_time)
        ret = capture_screen(ctx);

    screen->row_used = 0;
    ctx->bg_color = CCCOL_BLACK;

    // In realtime mode, emit an empty caption so the last one doesn't
    // stay on the screen.
    if (ctx->real_time)
        ret = capture_screen(ctx);

    return ret;
}

static int handle_eoc(CCaptionSubContext *ctx)
{
    int ret;

    ctx->active_screen = !ctx->active_screen;

    // In buffered mode, we wait til the *next* EOC and
    // capture what was already on the screen since the last EOC.
    if (!ctx->real_time)
        ret = handle_edm(ctx);

    ctx->cursor_column = 0;

    // In realtime mode, we display the buffered contents (after
    // flipping the buffer to active above) as soon as EOC arrives.
    if (ctx->real_time)
        ret = capture_screen(ctx);

    return ret;
}

static void handle_delete_end_of_row(CCaptionSubContext *ctx)
{
    struct Screen *screen = get_writing_screen(ctx);
    write_char(ctx, screen, 0);
}

static void handle_char(CCaptionSubContext *ctx, char hi, char lo)
{
    struct Screen *screen = get_writing_screen(ctx);

    SET_FLAG(screen->row_used, ctx->cursor_row);

    switch (hi) {
      case 0x11:
        ctx->cursor_charset = CCSET_SPECIAL_AMERICAN;
        break;
      case 0x12:
        if (ctx->cursor_column > 0)
            ctx->cursor_column -= 1;
        ctx->cursor_charset = CCSET_EXTENDED_SPANISH_FRENCH_MISC;
        break;
      case 0x13:
        if (ctx->cursor_column > 0)
            ctx->cursor_column -= 1;
        ctx->cursor_charset = CCSET_EXTENDED_PORTUGUESE_GERMAN_DANISH;
        break;
      default:
        ctx->cursor_charset = CCSET_BASIC_AMERICAN;
        write_char(ctx, screen, hi);
        break;
    }

    if (lo) {
        write_char(ctx, screen, lo);
    }
    write_char(ctx, screen, 0);

    if (ctx->mode != CCMODE_POPON)
        ctx->screen_touched = 1;

    if (lo)
       ff_dlog(ctx, "(%c,%c)\n", hi, lo);
    else
       ff_dlog(ctx, "(%c)\n", hi);
}

static int process_cc608(CCaptionSubContext *ctx, uint8_t hi, uint8_t lo)
{
    int ret = 0;

    if (hi == ctx->prev_cmd[0] && lo == ctx->prev_cmd[1]) {
        return 0;
    }

    /* set prev command */
    ctx->prev_cmd[0] = hi;
    ctx->prev_cmd[1] = lo;

    if ( (hi == 0x10 && (lo >= 0x40 && lo <= 0x5f)) ||
       ( (hi >= 0x11 && hi <= 0x17) && (lo >= 0x40 && lo <= 0x7f) ) ) {
        handle_pac(ctx, hi, lo);
    } else if ( ( hi == 0x11 && lo >= 0x20 && lo <= 0x2f ) ||
                ( hi == 0x17 && lo >= 0x2e && lo <= 0x2f) ) {
        handle_textattr(ctx, hi, lo);
    } else if ((hi == 0x10 && lo >= 0x20 && lo <= 0x2f)) {
        handle_bgattr(ctx, hi, lo);
    } else if (hi == 0x14 || hi == 0x15 || hi == 0x1c) {
        switch (lo) {
        case 0x20:
            /* resume caption loading */
            ctx->mode = CCMODE_POPON;
            break;
        case 0x24:
            handle_delete_end_of_row(ctx);
            break;
        case 0x25:
        case 0x26:
        case 0x27:
            ctx->rollup = lo - 0x23;
            ctx->mode = CCMODE_ROLLUP;
            break;
        case 0x29:
            /* resume direct captioning */
            ctx->mode = CCMODE_PAINTON;
            break;
        case 0x2b:
            /* resume text display */
            ctx->mode = CCMODE_TEXT;
            break;
        case 0x2c:
            /* erase display memory */
            handle_edm(ctx);
            break;
        case 0x2d:
            /* carriage return */
            ff_dlog(ctx, "carriage return\n");
            if (!ctx->real_time)
                ret = capture_screen(ctx);
            roll_up(ctx);
            ctx->cursor_column = 0;
            break;
        case 0x2e:
            /* erase buffered (non displayed) memory */
            // Only in realtime mode. In buffered mode, we re-use the inactive screen
            // for our own buffering.
            if (ctx->real_time) {
                struct Screen *screen = ctx->screen + !ctx->active_screen;
                screen->row_used = 0;
            }
            break;
        case 0x2f:
            /* end of caption */
            ff_dlog(ctx, "handle_eoc\n");
            ret = handle_eoc(ctx);
            break;
        default:
            ff_dlog(ctx, "Unknown command 0x%hhx 0x%hhx\n", hi, lo);
            break;
        }
    } else if (hi >= 0x11 && hi <= 0x13) {
        /* Special characters */
        handle_char(ctx, hi, lo);
    } else if (hi >= 0x20) {
        /* Standard characters (always in pairs) */
        handle_char(ctx, hi, lo);
        ctx->prev_cmd[0] = ctx->prev_cmd[1] = 0;
    } else if (hi == 0x17 && lo >= 0x21 && lo <= 0x23) {
        int i;
        /* Tab offsets (spacing) */
        for (i = 0; i < lo - 0x20; i++) {
            handle_char(ctx, ' ', 0);
        }
    } else {
        /* Ignoring all other non data code */
        ff_dlog(ctx, "Unknown command 0x%hhx 0x%hhx\n", hi, lo);
    }

    return ret;
}

static int decode(AVCodecContext *avctx, AVSubtitle *sub,
                  int *got_sub, const AVPacket *avpkt)
{
    CCaptionSubContext *ctx = avctx->priv_data;
    int64_t in_time = sub->pts;
    int64_t start_time;
    int64_t end_time;
    int bidx = ctx->buffer_index;
    const uint8_t *bptr = avpkt->data;
    int len = avpkt->size;
    int ret = 0;
    int i;
    unsigned nb_rect_allocated = 0;

    for (i = 0; i < len; i += 3) {
        uint8_t hi, cc_type = bptr[i] & 1;

        if (ctx->data_field < 0)
            ctx->data_field = cc_type;

        if (validate_cc_data_pair(bptr + i, &hi))
            continue;

        if (cc_type != ctx->data_field)
            continue;

        ret = process_cc608(ctx, hi & 0x7f, bptr[i + 2] & 0x7f);
        if (ret < 0)
            return ret;

        if (!ctx->buffer_changed)
            continue;
        ctx->buffer_changed = 0;

        if (!ctx->real_time && ctx->mode == CCMODE_POPON)
            ctx->buffer_index = bidx = !ctx->buffer_index;

        update_time(ctx, in_time);

        if (ctx->buffer[bidx].str[0] || ctx->real_time) {
            ff_dlog(ctx, "cdp writing data (%s)\n", ctx->buffer[bidx].str);
            start_time = ctx->buffer_time[0];
            sub->pts = start_time;
            end_time = ctx->buffer_time[1];
            if (!ctx->real_time)
                sub->end_display_time = av_rescale_q(end_time - start_time,
                                                     AV_TIME_BASE_Q, ms_tb);
            else
                sub->end_display_time = -1;
            ret = ff_ass_add_rect2(sub, ctx->buffer[bidx].str, ctx->readorder++, 0, NULL, NULL, &nb_rect_allocated);
            if (ret < 0)
                return ret;
            ctx->last_real_time = sub->pts;
            ctx->screen_touched = 0;
        }
    }

    if (!bptr && !ctx->real_time && ctx->buffer[!ctx->buffer_index].str[0]) {
        bidx = !ctx->buffer_index;
        ret = ff_ass_add_rect2(sub, ctx->buffer[bidx].str, ctx->readorder++, 0, NULL, NULL, &nb_rect_allocated);
        if (ret < 0)
            return ret;
        sub->pts = ctx->buffer_time[1];
        sub->end_display_time = av_rescale_q(ctx->buffer_time[1] - ctx->buffer_time[0],
                                             AV_TIME_BASE_Q, ms_tb);
        if (sub->end_display_time == 0)
            sub->end_display_time = ctx->buffer[bidx].len * 20;
    }

    if (ctx->real_time && ctx->screen_touched &&
        sub->pts >= ctx->last_real_time + av_rescale_q(ctx->real_time_latency_msec, ms_tb, AV_TIME_BASE_Q)) {
        ctx->last_real_time = sub->pts;
        ctx->screen_touched = 0;

        capture_screen(ctx);
        ctx->buffer_changed = 0;

        ret = ff_ass_add_rect2(sub, ctx->buffer[bidx].str, ctx->readorder++, 0, NULL, NULL, &nb_rect_allocated);
        if (ret < 0)
            return ret;
        sub->end_display_time = -1;
    }

    *got_sub = sub->num_rects > 0;
    return ret;
}

#define OFFSET(x) offsetof(CCaptionSubContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "real_time", "emit subtitle events as they are decoded for real-time display", OFFSET(real_time), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, SD },
    { "real_time_latency_msec", "minimum elapsed time between emitting real-time subtitle events", OFFSET(real_time_latency_msec), AV_OPT_TYPE_INT, { .i64 = 200 }, 0, 500, SD },
    { "data_field", "select data field", OFFSET(data_field), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 1, SD, "data_field" },
    { "auto",   "pick first one that appears", 0, AV_OPT_TYPE_CONST, { .i64 =-1 }, 0, 0, SD, "data_field" },
    { "first",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, SD, "data_field" },
    { "second", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, SD, "data_field" },
    {NULL}
};

static const AVClass ccaption_dec_class = {
    .class_name = "Closed caption Decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_ccaption_decoder = {
    .p.name         = "cc_dec",
    CODEC_LONG_NAME("Closed Caption (EIA-608 / CEA-708)"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_EIA_608,
    .p.priv_class   = &ccaption_dec_class,
    .p.capabilities = AV_CODEC_CAP_DELAY,
    .priv_data_size = sizeof(CCaptionSubContext),
    .init           = init_decoder,
    .close          = close_decoder,
    .flush          = flush_decoder,
    FF_CODEC_DECODE_SUB_CB(decode),
};
