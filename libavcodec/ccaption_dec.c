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
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#define SCREEN_ROWS 15
#define SCREEN_COLUMNS 32

#define SET_FLAG(var, val)   ( (var) |=   ( 1 << (val)) )
#define UNSET_FLAG(var, val) ( (var) &=  ~( 1 << (val)) )
#define CHECK_FLAG(var, val) ( (var) &    ( 1 << (val)) )

/*
 * TODO list
 * 1) handle font and color completely
 */
enum cc_mode {
    CCMODE_POPON,
    CCMODE_PAINTON,
    CCMODE_ROLLUP_2,
    CCMODE_ROLLUP_3,
    CCMODE_ROLLUP_4,
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

/* 0-255 needs 256 spaces */
static const uint8_t parity_table[256] = { 0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           0, 1, 1, 0, 1, 0, 0, 1,
                                           1, 0, 0, 1, 0, 1, 1, 0 };

struct Screen {
    /* +1 is used to compensate null character of string */
    uint8_t characters[SCREEN_ROWS][SCREEN_COLUMNS+1];
    uint8_t colors[SCREEN_ROWS][SCREEN_COLUMNS+1];
    uint8_t fonts[SCREEN_ROWS][SCREEN_COLUMNS+1];
    /*
     * Bitmask of used rows; if a bit is not set, the
     * corresponding row is not used.
     * for setting row 1  use row | (1 << 0)
     * for setting row 15 use row | (1 << 14)
     */
    int16_t  row_used;
};


typedef struct CCaptionSubContext {
    AVClass *class;
    struct Screen screen[2];
    int active_screen;
    uint8_t cursor_row;
    uint8_t cursor_column;
    uint8_t cursor_color;
    uint8_t cursor_font;
    AVBPrint buffer;
    int screen_changed;
    int rollup;
    enum  cc_mode mode;
    int64_t start_time;
    /* visible screen time */
    int64_t startv_time;
    int64_t end_time;
    char prev_cmd[2];
    /* buffer to store pkt data */
    AVBufferRef *pktbuf;
}CCaptionSubContext;


static av_cold int init_decoder(AVCodecContext *avctx)
{
    int ret;
    CCaptionSubContext *ctx = avctx->priv_data;

    av_bprint_init(&ctx->buffer, 0, AV_BPRINT_SIZE_UNLIMITED);
    /* taking by default roll up to 2 */
    ctx->mode = CCMODE_ROLLUP_2;
    ctx->rollup = 2;
    ret = ff_ass_subtitle_header_default(avctx);
    if(ret < 0) {
        return ret;
    }
    /* allocate pkt buffer */
    ctx->pktbuf = av_buffer_alloc(128);
    if( !ctx->pktbuf) {
        ret = AVERROR(ENOMEM);
    }
    return ret;
}

static av_cold int close_decoder(AVCodecContext *avctx)
{
    CCaptionSubContext *ctx = avctx->priv_data;
    av_bprint_finalize( &ctx->buffer, NULL);
    av_buffer_unref(&ctx->pktbuf);
    return 0;
}

/**
 * @param ctx closed caption context just to print log
 */
static int write_char (CCaptionSubContext *ctx, char *row,uint8_t col, char ch)
{
    if(col < SCREEN_COLUMNS) {
        row[col] = ch;
        return 0;
    }
    /* We have extra space at end only for null character */
    else if ( col == SCREEN_COLUMNS && ch == 0) {
        row[col] = ch;
        return 0;
    }
    else {
        av_log(ctx, AV_LOG_WARNING,"Data Ignored since exceeding screen width\n");
        return AVERROR_INVALIDDATA;
    }
}

/**
 * This function after validating parity bit, also remove it from data pair.
 * The first byte doesn't pass parity, we replace it with a solid blank
 * and process the pair.
 * If the second byte doesn't pass parity, it returns INVALIDDATA
 * user can ignore the whole pair and pass the other pair.
 */
static int validate_cc_data_pair (uint8_t *cc_data_pair)
{
    uint8_t cc_valid = (*cc_data_pair & 4) >>2;
    uint8_t cc_type = *cc_data_pair & 3;

    if (!cc_valid)
        return AVERROR_INVALIDDATA;

    // if EIA-608 data then verify parity.
    if (cc_type==0 || cc_type==1) {
        if (!parity_table[cc_data_pair[2]]) {
            return AVERROR_INVALIDDATA;
        }
        if (!parity_table[cc_data_pair[1]]) {
            cc_data_pair[1]=0x7F;
        }
    }

    //Skip non-data
    if( (cc_data_pair[0] == 0xFA || cc_data_pair[0] == 0xFC || cc_data_pair[0] == 0xFD )
         && (cc_data_pair[1] & 0x7F) == 0 && (cc_data_pair[2] & 0x7F) == 0)
        return AVERROR_PATCHWELCOME;

    //skip 708 data
    if(cc_type == 3 || cc_type == 2 )
        return AVERROR_PATCHWELCOME;

    /* remove parity bit */
    cc_data_pair[1] &= 0x7F;
    cc_data_pair[2] &= 0x7F;


    return 0;

}

static struct Screen *get_writing_screen(CCaptionSubContext *ctx)
{
    switch (ctx->mode) {
    case CCMODE_POPON:
        // use Inactive screen
        return ctx->screen + !ctx->active_screen;
    case CCMODE_PAINTON:
    case CCMODE_ROLLUP_2:
    case CCMODE_ROLLUP_3:
    case CCMODE_ROLLUP_4:
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

    if(ctx->mode == CCMODE_TEXT)
        return;

    screen = get_writing_screen(ctx);

    /* +1 signify cursor_row starts from 0
     * Can't keep lines less then row cursor pos
     */
    keep_lines = FFMIN(ctx->cursor_row + 1, ctx->rollup);

    for( i = 0; i < ctx->cursor_row - keep_lines; i++ )
        UNSET_FLAG(screen->row_used, i);


    for( i = 0; i < keep_lines && screen->row_used; i++ ) {
        const int i_row = ctx->cursor_row - keep_lines + i + 1;

        memcpy( screen->characters[i_row], screen->characters[i_row+1], SCREEN_COLUMNS );
        memcpy( screen->colors[i_row], screen->colors[i_row+1], SCREEN_COLUMNS);
        memcpy( screen->fonts[i_row], screen->fonts[i_row+1], SCREEN_COLUMNS);
        if(CHECK_FLAG(screen->row_used, i_row + 1))
            SET_FLAG(screen->row_used, i_row);

    }
    UNSET_FLAG(screen->row_used, ctx->cursor_row);

}

static int reap_screen(CCaptionSubContext *ctx, int64_t pts)
{
    int i;
    int ret = 0;
    struct Screen *screen = ctx->screen + ctx->active_screen;
    ctx->start_time = ctx->startv_time;

    for( i = 0; screen->row_used && i < SCREEN_ROWS; i++)
    {
        if(CHECK_FLAG(screen->row_used,i)) {
            char *str = screen->characters[i];
            /* skip space */
            while (*str == ' ')
                str++;

            av_bprintf(&ctx->buffer, "%s\\N", str);
            ret = av_bprint_is_complete(&ctx->buffer);
            if( ret == 0) {
                ret = AVERROR(ENOMEM);
                break;
            }
        }

    }
    if(screen->row_used && ctx->buffer.len >= 2 ) {
        ctx->buffer.len -= 2;
        ctx->buffer.str[ctx->buffer.len] = 0;
    }
    ctx->startv_time = pts;
    ctx->end_time = pts;
    return ret;
}

static void handle_textattr( CCaptionSubContext *ctx, uint8_t hi, uint8_t lo )
{
    int i = lo - 0x20;
    int ret;
    struct Screen *screen = get_writing_screen(ctx);
    char *row = screen->characters[ctx->cursor_row];

    if( i >= 32)
        return;

    ctx->cursor_color =  pac2_attribs[i][0];
    ctx->cursor_font = pac2_attribs[i][1];

    SET_FLAG(screen->row_used,ctx->cursor_row);
    ret = write_char(ctx, row, ctx->cursor_column, ' ');
    if(ret == 0)
        ctx->cursor_column++;
}

static void handle_pac( CCaptionSubContext *ctx, uint8_t hi, uint8_t lo )
{
    static const int8_t row_map[] = {
        11, -1, 1, 2, 3, 4, 12, 13, 14, 15, 5, 6, 7, 8, 9, 10
    };
    const int index = ( (hi<<1) & 0x0e) | ( (lo>>5) & 0x01 );
    struct Screen *screen = get_writing_screen(ctx);
    char *row;
    int indent,i,ret;

    if( row_map[index] <= 0 ) {
        av_log(ctx, AV_LOG_DEBUG,"Invalid pac index encountered\n");
        return;
    }

    lo &= 0x1f;

    ctx->cursor_row = row_map[index] - 1;
    ctx->cursor_color =  pac2_attribs[lo][0];
    ctx->cursor_font = pac2_attribs[lo][1];
    ctx->cursor_column = 0;
    indent = pac2_attribs[lo][2];
    row = screen->characters[ctx->cursor_row];
    for(i = 0;i < indent; i++) {
        ret = write_char(ctx, row, ctx->cursor_column, ' ');
        if(  ret == 0 )
            ctx->cursor_column++;
    }

}

/**
 * @param pts it is required to set end time
 */
static int handle_edm(CCaptionSubContext *ctx,int64_t pts)
{
    int ret = 0;
    struct Screen *screen = ctx->screen + ctx->active_screen;

    reap_screen(ctx, pts);
    screen->row_used = 0;
    ctx->screen_changed = 1;
    return ret;
}

static int handle_eoc(CCaptionSubContext *ctx, int64_t pts)
{
    int ret;
    ret = handle_edm(ctx,pts);
    ctx->active_screen = !ctx->active_screen;
    ctx->cursor_column = 0;
    return ret;
}

static void handle_delete_end_of_row( CCaptionSubContext *ctx, char hi, char lo)
{
    struct Screen *screen = get_writing_screen(ctx);
    char *row = screen->characters[ctx->cursor_row];
    write_char(ctx, row, ctx->cursor_column, 0);

}

static void handle_char(CCaptionSubContext *ctx, char hi, char lo, int64_t pts)
{
    struct Screen *screen = get_writing_screen(ctx);
    char *row = screen->characters[ctx->cursor_row];
    int ret;

    SET_FLAG(screen->row_used,ctx->cursor_row);

    ret = write_char(ctx, row, ctx->cursor_column, hi);
    if( ret == 0 )
        ctx->cursor_column++;

    if(lo) {
        ret = write_char(ctx, row, ctx->cursor_column, lo);
        if ( ret == 0 )
            ctx->cursor_column++;
    }
    write_char(ctx, row, ctx->cursor_column, 0);

    /* reset prev command since character can repeat */
    ctx->prev_cmd[0] = 0;
    ctx->prev_cmd[1] = 0;
    if (lo)
       ff_dlog(ctx, "(%c,%c)\n",hi,lo);
    else
       ff_dlog(ctx, "(%c)\n",hi);
}

static int process_cc608(CCaptionSubContext *ctx, int64_t pts, uint8_t hi, uint8_t lo)
{
    int ret = 0;
#define COR3(var, with1, with2, with3)  ( (var) == (with1) ||  (var) == (with2) || (var) == (with3) )
    if ( hi == ctx->prev_cmd[0] && lo == ctx->prev_cmd[1]) {
    /* ignore redundant command */
    } else if ( (hi == 0x10 && (lo >= 0x40 || lo <= 0x5f)) ||
              ( (hi >= 0x11 && hi <= 0x17) && (lo >= 0x40 && lo <= 0x7f) ) ) {
        handle_pac(ctx, hi, lo);
    } else if ( ( hi == 0x11 && lo >= 0x20 && lo <= 0x2f ) ||
                ( hi == 0x17 && lo >= 0x2e && lo <= 0x2f) ) {
        handle_textattr(ctx, hi, lo);
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x20 ) {
    /* resume caption loading */
        ctx->mode = CCMODE_POPON;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x24 ) {
        handle_delete_end_of_row(ctx, hi, lo);
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x25 ) {
        ctx->rollup = 2;
        ctx->mode = CCMODE_ROLLUP_2;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x26 ) {
        ctx->rollup = 3;
        ctx->mode = CCMODE_ROLLUP_3;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x27 ) {
        ctx->rollup = 4;
        ctx->mode = CCMODE_ROLLUP_4;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x29 ) {
    /* resume direct captioning */
        ctx->mode = CCMODE_PAINTON;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x2B ) {
    /* resume text display */
        ctx->mode = CCMODE_TEXT;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x2C ) {
    /* erase display memory */
        ret = handle_edm(ctx, pts);
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x2D ) {
    /* carriage return */
        ff_dlog(ctx, "carriage return\n");
        reap_screen(ctx, pts);
        roll_up(ctx);
        ctx->screen_changed = 1;
        ctx->cursor_column = 0;
    } else if ( COR3(hi, 0x14, 0x15, 0x1C) && lo == 0x2F ) {
    /* end of caption */
        ff_dlog(ctx, "handle_eoc\n");
        ret = handle_eoc(ctx, pts);
    } else if (hi>=0x20) {
    /* Standard characters (always in pairs) */
        handle_char(ctx, hi, lo, pts);
    } else {
    /* Ignoring all other non data code */
        ff_dlog(ctx, "Unknown command 0x%hhx 0x%hhx\n", hi, lo);
    }

    /* set prev command */
     ctx->prev_cmd[0] = hi;
     ctx->prev_cmd[1] = lo;

#undef COR3
    return ret;

}

static int decode(AVCodecContext *avctx, void *data, int *got_sub, AVPacket *avpkt)
{
    CCaptionSubContext *ctx = avctx->priv_data;
    AVSubtitle *sub = data;
    uint8_t *bptr = NULL;
    int len = avpkt->size;
    int ret = 0;
    int i;

    if ( ctx->pktbuf->size < len) {
        ret = av_buffer_realloc(&ctx->pktbuf, len);
         if(ret < 0) {
            av_log(ctx, AV_LOG_WARNING, "Insufficient Memory of %d truncated to %d\n",len, ctx->pktbuf->size);
            len = ctx->pktbuf->size;
            ret = 0;
        }
    }
    memcpy(ctx->pktbuf->data, avpkt->data, len);
    bptr = ctx->pktbuf->data;


    for (i  = 0; i < len; i += 3) {
        uint8_t cc_type = *(bptr + i) & 3;
        if (validate_cc_data_pair( bptr + i) )
            continue;
        /* ignoring data field 1 */
        if(cc_type == 1)
            continue;
        else
            process_cc608(ctx, avpkt->pts, *(bptr + i + 1) & 0x7f, *(bptr + i + 2) & 0x7f);
        if(ctx->screen_changed && *ctx->buffer.str)
        {
            int start_time = av_rescale_q(ctx->start_time, avctx->time_base, (AVRational){ 1, 100 });
            int end_time = av_rescale_q(ctx->end_time, avctx->time_base, (AVRational){ 1, 100 });
            ff_dlog(ctx, "cdp writing data (%s)\n",ctx->buffer.str);
            ret = ff_ass_add_rect_bprint(sub, &ctx->buffer, start_time, end_time - start_time);
            if (ret < 0)
                return ret;
            sub->pts = av_rescale_q(ctx->start_time, avctx->time_base, AV_TIME_BASE_Q);
            ctx->screen_changed = 0;
            av_bprint_clear(&ctx->buffer);
        }
    }

    *got_sub = sub->num_rects > 0;
    return ret;
}

static const AVOption options[] = {
    {NULL}
};

static const AVClass ccaption_dec_class = {
    .class_name = "Closed caption Decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_ccaption_decoder = {
    .name           = "cc_dec",
    .long_name      = NULL_IF_CONFIG_SMALL("Closed Caption (EIA-608 / CEA-708) Decoder"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_EIA_608,
    .priv_data_size = sizeof(CCaptionSubContext),
    .init           = init_decoder,
    .close          = close_decoder,
    .decode         = decode,
    .priv_class     = &ccaption_dec_class,
};
