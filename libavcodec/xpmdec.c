/*
 * XPM image format
 *
 * Copyright (c) 2012 Paul B Mahol
 * Copyright (c) 2017 Paras Chadha
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
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"

#define MIN_ELEMENT ' '
#define MAX_ELEMENT 0xfe
#define NB_ELEMENTS (MAX_ELEMENT - MIN_ELEMENT + 1)

typedef struct XPMContext {
    uint32_t  *pixels;
    int        pixels_size;
    uint8_t   *buf;
    int        buf_size;
} XPMDecContext;

typedef struct ColorEntry {
    const char *name;         ///< a string representing the name of the color
    uint32_t    rgb_color;    ///< RGB values for the color
} ColorEntry;

static int color_table_compare(const void *lhs, const void *rhs)
{
    return av_strcasecmp(lhs, ((const ColorEntry *)rhs)->name);
}

static const ColorEntry color_table[] = {
    { "AliceBlue",            0xFFF0F8FF },
    { "AntiqueWhite",         0xFFFAEBD7 },
    { "Aqua",                 0xFF00FFFF },
    { "Aquamarine",           0xFF7FFFD4 },
    { "Azure",                0xFFF0FFFF },
    { "Beige",                0xFFF5F5DC },
    { "Bisque",               0xFFFFE4C4 },
    { "Black",                0xFF000000 },
    { "BlanchedAlmond",       0xFFFFEBCD },
    { "Blue",                 0xFF0000FF },
    { "BlueViolet",           0xFF8A2BE2 },
    { "Brown",                0xFFA52A2A },
    { "BurlyWood",            0xFFDEB887 },
    { "CadetBlue",            0xFF5F9EA0 },
    { "Chartreuse",           0xFF7FFF00 },
    { "Chocolate",            0xFFD2691E },
    { "Coral",                0xFFFF7F50 },
    { "CornflowerBlue",       0xFF6495ED },
    { "Cornsilk",             0xFFFFF8DC },
    { "Crimson",              0xFFDC143C },
    { "Cyan",                 0xFF00FFFF },
    { "DarkBlue",             0xFF00008B },
    { "DarkCyan",             0xFF008B8B },
    { "DarkGoldenRod",        0xFFB8860B },
    { "DarkGray",             0xFFA9A9A9 },
    { "DarkGreen",            0xFF006400 },
    { "DarkKhaki",            0xFFBDB76B },
    { "DarkMagenta",          0xFF8B008B },
    { "DarkOliveGreen",       0xFF556B2F },
    { "Darkorange",           0xFFFF8C00 },
    { "DarkOrchid",           0xFF9932CC },
    { "DarkRed",              0xFF8B0000 },
    { "DarkSalmon",           0xFFE9967A },
    { "DarkSeaGreen",         0xFF8FBC8F },
    { "DarkSlateBlue",        0xFF483D8B },
    { "DarkSlateGray",        0xFF2F4F4F },
    { "DarkTurquoise",        0xFF00CED1 },
    { "DarkViolet",           0xFF9400D3 },
    { "DeepPink",             0xFFFF1493 },
    { "DeepSkyBlue",          0xFF00BFFF },
    { "DimGray",              0xFF696969 },
    { "DodgerBlue",           0xFF1E90FF },
    { "FireBrick",            0xFFB22222 },
    { "FloralWhite",          0xFFFFFAF0 },
    { "ForestGreen",          0xFF228B22 },
    { "Fuchsia",              0xFFFF00FF },
    { "Gainsboro",            0xFFDCDCDC },
    { "GhostWhite",           0xFFF8F8FF },
    { "Gold",                 0xFFFFD700 },
    { "GoldenRod",            0xFFDAA520 },
    { "Gray",                 0xFFBEBEBE },
    { "Green",                0xFF00FF00 },
    { "GreenYellow",          0xFFADFF2F },
    { "HoneyDew",             0xFFF0FFF0 },
    { "HotPink",              0xFFFF69B4 },
    { "IndianRed",            0xFFCD5C5C },
    { "Indigo",               0xFF4B0082 },
    { "Ivory",                0xFFFFFFF0 },
    { "Khaki",                0xFFF0E68C },
    { "Lavender",             0xFFE6E6FA },
    { "LavenderBlush",        0xFFFFF0F5 },
    { "LawnGreen",            0xFF7CFC00 },
    { "LemonChiffon",         0xFFFFFACD },
    { "LightBlue",            0xFFADD8E6 },
    { "LightCoral",           0xFFF08080 },
    { "LightCyan",            0xFFE0FFFF },
    { "LightGoldenRodYellow", 0xFFFAFAD2 },
    { "LightGreen",           0xFF90EE90 },
    { "LightGrey",            0xFFD3D3D3 },
    { "LightPink",            0xFFFFB6C1 },
    { "LightSalmon",          0xFFFFA07A },
    { "LightSeaGreen",        0xFF20B2AA },
    { "LightSkyBlue",         0xFF87CEFA },
    { "LightSlateGray",       0xFF778899 },
    { "LightSteelBlue",       0xFFB0C4DE },
    { "LightYellow",          0xFFFFFFE0 },
    { "Lime",                 0xFF00FF00 },
    { "LimeGreen",            0xFF32CD32 },
    { "Linen",                0xFFFAF0E6 },
    { "Magenta",              0xFFFF00FF },
    { "Maroon",               0xFFB03060 },
    { "MediumAquaMarine",     0xFF66CDAA },
    { "MediumBlue",           0xFF0000CD },
    { "MediumOrchid",         0xFFBA55D3 },
    { "MediumPurple",         0xFF9370D8 },
    { "MediumSeaGreen",       0xFF3CB371 },
    { "MediumSlateBlue",      0xFF7B68EE },
    { "MediumSpringGreen",    0xFF00FA9A },
    { "MediumTurquoise",      0xFF48D1CC },
    { "MediumVioletRed",      0xFFC71585 },
    { "MidnightBlue",         0xFF191970 },
    { "MintCream",            0xFFF5FFFA },
    { "MistyRose",            0xFFFFE4E1 },
    { "Moccasin",             0xFFFFE4B5 },
    { "NavajoWhite",          0xFFFFDEAD },
    { "Navy",                 0xFF000080 },
    { "None",                 0x00000000 },
    { "OldLace",              0xFFFDF5E6 },
    { "Olive",                0xFF808000 },
    { "OliveDrab",            0xFF6B8E23 },
    { "Orange",               0xFFFFA500 },
    { "OrangeRed",            0xFFFF4500 },
    { "Orchid",               0xFFDA70D6 },
    { "PaleGoldenRod",        0xFFEEE8AA },
    { "PaleGreen",            0xFF98FB98 },
    { "PaleTurquoise",        0xFFAFEEEE },
    { "PaleVioletRed",        0xFFD87093 },
    { "PapayaWhip",           0xFFFFEFD5 },
    { "PeachPuff",            0xFFFFDAB9 },
    { "Peru",                 0xFFCD853F },
    { "Pink",                 0xFFFFC0CB },
    { "Plum",                 0xFFDDA0DD },
    { "PowderBlue",           0xFFB0E0E6 },
    { "Purple",               0xFFA020F0 },
    { "Red",                  0xFFFF0000 },
    { "RosyBrown",            0xFFBC8F8F },
    { "RoyalBlue",            0xFF4169E1 },
    { "SaddleBrown",          0xFF8B4513 },
    { "Salmon",               0xFFFA8072 },
    { "SandyBrown",           0xFFF4A460 },
    { "SeaGreen",             0xFF2E8B57 },
    { "SeaShell",             0xFFFFF5EE },
    { "Sienna",               0xFFA0522D },
    { "Silver",               0xFFC0C0C0 },
    { "SkyBlue",              0xFF87CEEB },
    { "SlateBlue",            0xFF6A5ACD },
    { "SlateGray",            0xFF708090 },
    { "Snow",                 0xFFFFFAFA },
    { "SpringGreen",          0xFF00FF7F },
    { "SteelBlue",            0xFF4682B4 },
    { "Tan",                  0xFFD2B48C },
    { "Teal",                 0xFF008080 },
    { "Thistle",              0xFFD8BFD8 },
    { "Tomato",               0xFFFF6347 },
    { "Turquoise",            0xFF40E0D0 },
    { "Violet",               0xFFEE82EE },
    { "Wheat",                0xFFF5DEB3 },
    { "White",                0xFFFFFFFF },
    { "WhiteSmoke",           0xFFF5F5F5 },
    { "Yellow",               0xFFFFFF00 },
    { "YellowGreen",          0xFF9ACD32 }
};

static unsigned hex_char_to_number(uint8_t x)
{
    if (x >= 'a' && x <= 'f')
        x -= 'a' - 10;
    else if (x >= 'A' && x <= 'F')
        x -= 'A' - 10;
    else if (x >= '0' && x <= '9')
        x -= '0';
    else
        x = 0;
    return x;
}

/*
 * Function same as strcspn but ignores characters if they are inside a C style comments
 */
static size_t mod_strcspn(const char *string, const char *reject)
{
    int i, j;

    for (i = 0; string && string[i]; i++) {
        if (string[i] == '/' && string[i+1] == '*') {
            i += 2;
            while ( string && string[i] && (string[i] != '*' || string[i+1] != '/') )
                i++;
            i++;
        } else if (string[i] == '/' && string[i+1] == '/') {
            i += 2;
            while ( string && string[i] && string[i] != '\n' )
                i++;
        } else {
            for (j = 0; reject && reject[j]; j++) {
                if (string[i] == reject[j])
                    break;
            }
            if (reject && reject[j])
                break;
        }
    }
    return i;
}

static uint32_t color_string_to_rgba(const char *p, int len)
{
    uint32_t ret = 0xFF000000;
    const ColorEntry *entry;
    char color_name[100];

    len = FFMIN(FFMAX(len, 0), sizeof(color_name) - 1);

    if (*p == '#') {
        p++;
        len--;
        if (len == 3) {
            ret |= (hex_char_to_number(p[2]) <<  4) |
                   (hex_char_to_number(p[1]) << 12) |
                   (hex_char_to_number(p[0]) << 20);
        } else if (len == 4) {
            ret  = (hex_char_to_number(p[3]) <<  4) |
                   (hex_char_to_number(p[2]) << 12) |
                   (hex_char_to_number(p[1]) << 20) |
                   (hex_char_to_number(p[0]) << 28);
        } else if (len == 6) {
            ret |=  hex_char_to_number(p[5])        |
                   (hex_char_to_number(p[4]) <<  4) |
                   (hex_char_to_number(p[3]) <<  8) |
                   (hex_char_to_number(p[2]) << 12) |
                   (hex_char_to_number(p[1]) << 16) |
                   (hex_char_to_number(p[0]) << 20);
        } else if (len == 8) {
            ret  =  hex_char_to_number(p[7])        |
                   (hex_char_to_number(p[6]) <<  4) |
                   (hex_char_to_number(p[5]) <<  8) |
                   (hex_char_to_number(p[4]) << 12) |
                   (hex_char_to_number(p[3]) << 16) |
                   (hex_char_to_number(p[2]) << 20) |
                   (hex_char_to_number(p[1]) << 24) |
                   (hex_char_to_number(p[0]) << 28);
        }
    } else {
        strncpy(color_name, p, len);
        color_name[len] = '\0';

        entry = bsearch(color_name,
                        color_table,
                        FF_ARRAY_ELEMS(color_table),
                        sizeof(ColorEntry),
                        color_table_compare);

        if (!entry)
            return ret;

        ret = entry->rgb_color;
    }
    return ret;
}

static int ascii2index(const uint8_t *cpixel, int cpp)
{
    const uint8_t *p = cpixel;
    int n = 0, m = 1, i;

    for (i = 0; i < cpp; i++) {
        if (*p < MIN_ELEMENT || *p > MAX_ELEMENT)
            return AVERROR_INVALIDDATA;
        n += (*p++ - MIN_ELEMENT) * m;
        m *= NB_ELEMENTS;
    }
    return n;
}

static int xpm_decode_frame(AVCodecContext *avctx, AVFrame *p,
                            int *got_frame, AVPacket *avpkt)
{
    XPMDecContext *x = avctx->priv_data;
    const uint8_t *end, *ptr;
    int ncolors, cpp, ret, i, j;
    int64_t size;
    uint32_t *dst;
    int width, height;

    avctx->pix_fmt = AV_PIX_FMT_BGRA;

    av_fast_padded_malloc(&x->buf, &x->buf_size, avpkt->size);
    if (!x->buf)
        return AVERROR(ENOMEM);
    memcpy(x->buf, avpkt->data, avpkt->size);
    x->buf[avpkt->size] = 0;

    ptr = x->buf;
    end = x->buf + avpkt->size;
    while (end - ptr > 9 && memcmp(ptr, "/* XPM */", 9))
        ptr++;

    if (end - ptr <= 9) {
        av_log(avctx, AV_LOG_ERROR, "missing signature\n");
        return AVERROR_INVALIDDATA;
    }

    ptr += mod_strcspn(ptr, "\"");
    if (sscanf(ptr, "\"%u %u %u %u\",",
               &width, &height, &ncolors, &cpp) != 4) {
        av_log(avctx, AV_LOG_ERROR, "missing image parameters\n");
        return AVERROR_INVALIDDATA;
    }

    if ((ret = ff_set_dimensions(avctx, width, height)) < 0)
        return ret;

    if (cpp <= 0 || cpp >= 5) {
        av_log(avctx, AV_LOG_ERROR, "unsupported/invalid number of chars per pixel: %d\n", cpp);
        return AVERROR_INVALIDDATA;
    }

    size = 1;
    for (i = 0; i < cpp; i++)
        size *= NB_ELEMENTS;

    if (ncolors <= 0 || ncolors > size) {
        av_log(avctx, AV_LOG_ERROR, "invalid number of colors: %d\n", ncolors);
        return AVERROR_INVALIDDATA;
    }

    if (size > SIZE_MAX / 4)
        return AVERROR(ENOMEM);

    size *= 4;

    ptr += mod_strcspn(ptr, ",") + 1;
    if (end - ptr < 1)
        return AVERROR_INVALIDDATA;

    if (avctx->skip_frame >= AVDISCARD_ALL)
        return avpkt->size;

    if ((ret = ff_get_buffer(avctx, p, 0)) < 0)
        return ret;

    av_fast_padded_malloc(&x->pixels, &x->pixels_size, size);
    if (!x->pixels)
        return AVERROR(ENOMEM);

    for (i = 0; i < ncolors; i++) {
        const uint8_t *index;
        int len;

        ptr += mod_strcspn(ptr, "\"") + 1;
        if (end - ptr < cpp)
            return AVERROR_INVALIDDATA;
        index = ptr;
        ptr += cpp;

        ptr = strstr(ptr, "c ");
        if (ptr) {
            ptr += 2;
        } else {
            return AVERROR_INVALIDDATA;
        }

        len = strcspn(ptr, "\" ");

        if ((ret = ascii2index(index, cpp)) < 0)
            return ret;

        x->pixels[ret] = color_string_to_rgba(ptr, len);
        ptr += mod_strcspn(ptr, ",") + 1;
        if (end - ptr < 1)
            return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < avctx->height; i++) {
        dst = (uint32_t *)(p->data[0] + i * p->linesize[0]);
        if (end - ptr < 1)
            return AVERROR_INVALIDDATA;
        ptr += mod_strcspn(ptr, "\"") + 1;
        if (end - ptr < 1)
            return AVERROR_INVALIDDATA;

        for (j = 0; j < avctx->width; j++) {
            if (end - ptr < cpp)
                return AVERROR_INVALIDDATA;

            if ((ret = ascii2index(ptr, cpp)) < 0)
                return ret;

            *dst++ = x->pixels[ret];
            ptr += cpp;
        }
        ptr += mod_strcspn(ptr, ",") + 1;
    }

    p->key_frame = 1;
    p->pict_type = AV_PICTURE_TYPE_I;

    *got_frame = 1;

    return avpkt->size;
}

static av_cold int xpm_decode_close(AVCodecContext *avctx)
{
    XPMDecContext *x = avctx->priv_data;
    av_freep(&x->pixels);

    av_freep(&x->buf);
    x->buf_size = 0;

    return 0;
}

const FFCodec ff_xpm_decoder = {
    .p.name         = "xpm",
    CODEC_LONG_NAME("XPM (X PixMap) image"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_XPM,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(XPMDecContext),
    .close          = xpm_decode_close,
    .caps_internal  = FF_CODEC_CAP_SKIP_FRAME_FILL_PARAM,
    FF_CODEC_DECODE_CB(xpm_decode_frame),
};
