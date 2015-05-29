/*
 * SubRip subtitle decoder
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "avcodec.h"
#include "ass.h"

static int html_color_parse(AVCodecContext *avctx, const char *str)
{
    uint8_t rgba[4];
    if (av_parse_color(rgba, str, strcspn(str, "\" >"), avctx) < 0)
        return -1;
    return rgba[0] | rgba[1] << 8 | rgba[2] << 16;
}

enum {
    PARAM_UNKNOWN = -1,
    PARAM_SIZE,
    PARAM_COLOR,
    PARAM_FACE,
    PARAM_NUMBER
};

typedef struct SrtStack {
    char tag[128];
    char param[PARAM_NUMBER][128];
} SrtStack;

static void rstrip_spaces_buf(AVBPrint *buf)
{
    while (buf->len > 0 && buf->str[buf->len - 1] == ' ')
        buf->str[--buf->len] = 0;
}

static void srt_to_ass(AVCodecContext *avctx, AVBPrint *dst,
                       const char *in, int x1, int y1, int x2, int y2)
{
    char *param, buffer[128], tmp[128];
    int len, tag_close, sptr = 1, line_start = 1, an = 0, end = 0;
    SrtStack stack[16];

    stack[0].tag[0] = 0;
    strcpy(stack[0].param[PARAM_SIZE],  "{\\fs}");
    strcpy(stack[0].param[PARAM_COLOR], "{\\c}");
    strcpy(stack[0].param[PARAM_FACE],  "{\\fn}");

    if (x1 >= 0 && y1 >= 0) {
        /* XXX: here we rescale coordinate assuming they are in DVD resolution
         * (720x480) since we don't have anything better */

        if (x2 >= 0 && y2 >= 0 && (x2 != x1 || y2 != y1) && x2 >= x1 && y2 >= y1) {
            /* text rectangle defined, write the text at the center of the rectangle */
            const int cx = x1 + (x2 - x1)/2;
            const int cy = y1 + (y2 - y1)/2;
            const int scaled_x = cx * ASS_DEFAULT_PLAYRESX / 720;
            const int scaled_y = cy * ASS_DEFAULT_PLAYRESY / 480;
            av_bprintf(dst, "{\\an5}{\\pos(%d,%d)}", scaled_x, scaled_y);
        } else {
            /* only the top left corner, assume the text starts in that corner */
            const int scaled_x = x1 * ASS_DEFAULT_PLAYRESX / 720;
            const int scaled_y = y1 * ASS_DEFAULT_PLAYRESY / 480;
            av_bprintf(dst, "{\\an1}{\\pos(%d,%d)}", scaled_x, scaled_y);
        }
    }

    for (; !end && *in; in++) {
        switch (*in) {
        case '\r':
            break;
        case '\n':
            if (line_start) {
                end = 1;
                break;
            }
            rstrip_spaces_buf(dst);
            av_bprintf(dst, "\\N");
            line_start = 1;
            break;
        case ' ':
            if (!line_start)
                av_bprint_chars(dst, *in, 1);
            break;
        case '{':    /* skip all {\xxx} substrings except for {\an%d}
                        and all microdvd like styles such as {Y:xxx} */
            len = 0;
            an += sscanf(in, "{\\an%*1u}%n", &len) >= 0 && len > 0;
            if ((an != 1 && (len = 0, sscanf(in, "{\\%*[^}]}%n", &len) >= 0 && len > 0)) ||
                (len = 0, sscanf(in, "{%*1[CcFfoPSsYy]:%*[^}]}%n", &len) >= 0 && len > 0)) {
                in += len - 1;
            } else
                av_bprint_chars(dst, *in, 1);
            break;
        case '<':
            tag_close = in[1] == '/';
            len = 0;
            if (sscanf(in+tag_close+1, "%127[^>]>%n", buffer, &len) >= 1 && len > 0) {
                if ((param = strchr(buffer, ' ')))
                    *param++ = 0;
                if ((!tag_close && sptr < FF_ARRAY_ELEMS(stack)) ||
                    ( tag_close && sptr > 0 && !strcmp(stack[sptr-1].tag, buffer))) {
                    int i, j, unknown = 0;
                    in += len + tag_close;
                    if (!tag_close)
                        memset(stack+sptr, 0, sizeof(*stack));
                    if (!strcmp(buffer, "font")) {
                        if (tag_close) {
                            for (i=PARAM_NUMBER-1; i>=0; i--)
                                if (stack[sptr-1].param[i][0])
                                    for (j=sptr-2; j>=0; j--)
                                        if (stack[j].param[i][0]) {
                                            av_bprintf(dst, "%s", stack[j].param[i]);
                                            break;
                                        }
                        } else {
                            while (param) {
                                if (!strncmp(param, "size=", 5)) {
                                    unsigned font_size;
                                    param += 5 + (param[5] == '"');
                                    if (sscanf(param, "%u", &font_size) == 1) {
                                        snprintf(stack[sptr].param[PARAM_SIZE],
                                             sizeof(stack[0].param[PARAM_SIZE]),
                                             "{\\fs%u}", font_size);
                                    }
                                } else if (!strncmp(param, "color=", 6)) {
                                    param += 6 + (param[6] == '"');
                                    snprintf(stack[sptr].param[PARAM_COLOR],
                                         sizeof(stack[0].param[PARAM_COLOR]),
                                         "{\\c&H%X&}",
                                         html_color_parse(avctx, param));
                                } else if (!strncmp(param, "face=", 5)) {
                                    param += 5 + (param[5] == '"');
                                    len = strcspn(param,
                                                  param[-1] == '"' ? "\"" :" ");
                                    av_strlcpy(tmp, param,
                                               FFMIN(sizeof(tmp), len+1));
                                    param += len;
                                    snprintf(stack[sptr].param[PARAM_FACE],
                                             sizeof(stack[0].param[PARAM_FACE]),
                                             "{\\fn%s}", tmp);
                                }
                                if ((param = strchr(param, ' ')))
                                    param++;
                            }
                            for (i=0; i<PARAM_NUMBER; i++)
                                if (stack[sptr].param[i][0])
                                    av_bprintf(dst, "%s", stack[sptr].param[i]);
                        }
                    } else if (!buffer[1] && strspn(buffer, "bisu") == 1) {
                        av_bprintf(dst, "{\\%c%d}", buffer[0], !tag_close);
                    } else {
                        unknown = 1;
                        snprintf(tmp, sizeof(tmp), "</%s>", buffer);
                    }
                    if (tag_close) {
                        sptr--;
                    } else if (unknown && !strstr(in, tmp)) {
                        in -= len + tag_close;
                        av_bprint_chars(dst, *in, 1);
                    } else
                        av_strlcpy(stack[sptr++].tag, buffer,
                                   sizeof(stack[0].tag));
                    break;
                }
            }
        default:
            av_bprint_chars(dst, *in, 1);
            break;
        }
        if (*in != ' ' && *in != '\r' && *in != '\n')
            line_start = 0;
    }

    while (dst->len >= 2 && !strncmp(&dst->str[dst->len - 2], "\\N", 2))
        dst->len -= 2;
    dst->str[dst->len] = 0;
    rstrip_spaces_buf(dst);
}

static int srt_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    AVBPrint buffer;
    int ts_start, ts_end, x1 = -1, y1 = -1, x2 = -1, y2 = -1;
    int size, ret;
    const uint8_t *p = av_packet_get_side_data(avpkt, AV_PKT_DATA_SUBTITLE_POSITION, &size);

    if (p && size == 16) {
        x1 = AV_RL32(p     );
        y1 = AV_RL32(p +  4);
        x2 = AV_RL32(p +  8);
        y2 = AV_RL32(p + 12);
    }

    if (avpkt->size <= 0)
        return avpkt->size;

    av_bprint_init(&buffer, 0, AV_BPRINT_SIZE_UNLIMITED);

        // TODO: reindent
            // Do final divide-by-10 outside rescale to force rounding down.
            ts_start = av_rescale_q(avpkt->pts,
                                    avctx->time_base,
                                    (AVRational){1,100});
            ts_end   = av_rescale_q(avpkt->pts + avpkt->duration,
                                    avctx->time_base,
                                    (AVRational){1,100});

    srt_to_ass(avctx, &buffer, avpkt->data, x1, y1, x2, y2);
    ret = ff_ass_add_rect_bprint(sub, &buffer, ts_start, ts_end-ts_start);
    av_bprint_finalize(&buffer, NULL);
    if (ret < 0)
        return ret;

    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

#if CONFIG_SRT_DECODER
/* deprecated decoder */
AVCodec ff_srt_decoder = {
    .name         = "srt",
    .long_name    = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SUBRIP,
    .init         = ff_ass_subtitle_header_default,
    .decode       = srt_decode_frame,
};
#endif

#if CONFIG_SUBRIP_DECODER
AVCodec ff_subrip_decoder = {
    .name         = "subrip",
    .long_name    = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SUBRIP,
    .init         = ff_ass_subtitle_header_default,
    .decode       = srt_decode_frame,
};
#endif
