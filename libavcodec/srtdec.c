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

typedef struct {
    char tag[128];
    char param[PARAM_NUMBER][128];
} SrtStack;

static const char *srt_to_ass(AVCodecContext *avctx, char *out, char *out_end,
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
        if (x2 >= 0 && y2 >= 0 && (x2 != x1 || y2 != y1))
            snprintf(out, out_end-out,
                            "{\\an1}{\\move(%d,%d,%d,%d)}", x1, y1, x2, y2);
        else
            snprintf(out, out_end-out, "{\\an1}{\\pos(%d,%d)}", x1, y1);
        out += strlen(out);
    }

    for (; out < out_end && !end && *in; in++) {
        switch (*in) {
        case '\r':
            break;
        case '\n':
            if (line_start) {
                end = 1;
                break;
            }
            while (out[-1] == ' ')
                out--;
            snprintf(out, out_end-out, "\\N");
            if(out<out_end) out += strlen(out);
            line_start = 1;
            break;
        case ' ':
            if (!line_start)
                *out++ = *in;
            break;
        case '{':    /* skip all {\xxx} substrings except for {\an%d}
                        and all microdvd like styles such as {Y:xxx} */
            len = 0;
            an += sscanf(in, "{\\an%*1u}%n", &len) >= 0 && len > 0;
            if ((an != 1 && (len = 0, sscanf(in, "{\\%*[^}]}%n", &len) >= 0 && len > 0)) ||
                (len = 0, sscanf(in, "{%*1[CcFfoPSsYy]:%*[^}]}%n", &len) >= 0 && len > 0)) {
                in += len - 1;
            } else
                *out++ = *in;
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
                                            snprintf(out, out_end-out,
                                                            "%s", stack[j].param[i]);
                                            if(out<out_end) out += strlen(out);
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
                                if (stack[sptr].param[i][0]) {
                                    snprintf(out, out_end-out,
                                                    "%s", stack[sptr].param[i]);
                                    if(out<out_end) out += strlen(out);
                                }
                        }
                    } else if (!buffer[1] && strspn(buffer, "bisu") == 1) {
                        snprintf(out, out_end-out,
                                        "{\\%c%d}", buffer[0], !tag_close);
                        if(out<out_end) out += strlen(out);
                    } else {
                        unknown = 1;
                        snprintf(tmp, sizeof(tmp), "</%s>", buffer);
                    }
                    if (tag_close) {
                        sptr--;
                    } else if (unknown && !strstr(in, tmp)) {
                        in -= len + tag_close;
                        *out++ = *in;
                    } else
                        av_strlcpy(stack[sptr++].tag, buffer,
                                   sizeof(stack[0].tag));
                    break;
                }
            }
        default:
            *out++ = *in;
            break;
        }
        if (*in != ' ' && *in != '\r' && *in != '\n')
            line_start = 0;
    }

    out = FFMIN(out, out_end-3);
    while (!strncmp(out-2, "\\N", 2))
        out -= 2;
    while (out[-1] == ' ')
        out--;
    snprintf(out, out_end-out, "\r\n");
    return in;
}

static const char *read_ts(const char *buf, int *ts_start, int *ts_end,
                           int *x1, int *y1, int *x2, int *y2)
{
    int i, hs, ms, ss, he, me, se;

    for (i=0; i<2; i++) {
        /* try to read timestamps in either the first or second line */
        int c = sscanf(buf, "%d:%2d:%2d%*1[,.]%3d --> %d:%2d:%2d%*1[,.]%3d"
                       "%*[ ]X1:%u X2:%u Y1:%u Y2:%u",
                       &hs, &ms, &ss, ts_start, &he, &me, &se, ts_end,
                       x1, x2, y1, y2);
        buf += strcspn(buf, "\n");
        buf += !!*buf;
        if (c >= 8) {
            *ts_start = 100*(ss + 60*(ms + 60*hs)) + *ts_start/10;
            *ts_end   = 100*(se + 60*(me + 60*he)) + *ts_end  /10;
            return buf;
        }
    }
    return NULL;
}

static int srt_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    int ts_start, ts_end, x1 = -1, y1 = -1, x2 = -1, y2 = -1;
    char buffer[2048];
    const char *ptr = avpkt->data;
    const char *end = avpkt->data + avpkt->size;
    int size;
    const uint8_t *p = av_packet_get_side_data(avpkt, AV_PKT_DATA_SUBTITLE_POSITION, &size);

    if (p && size == 16) {
        x1 = AV_RL32(p     );
        y1 = AV_RL32(p +  4);
        x2 = AV_RL32(p +  8);
        y2 = AV_RL32(p + 12);
    }

    if (avpkt->size <= 0)
        return avpkt->size;

    while (ptr < end && *ptr) {
        if (avctx->codec->id == AV_CODEC_ID_SRT) {
            ptr = read_ts(ptr, &ts_start, &ts_end, &x1, &y1, &x2, &y2);
            if (!ptr)
                break;
        } else {
            // Do final divide-by-10 outside rescale to force rounding down.
            ts_start = av_rescale_q(avpkt->pts,
                                    avctx->time_base,
                                    (AVRational){1,100});
            ts_end   = av_rescale_q(avpkt->pts + avpkt->duration,
                                    avctx->time_base,
                                    (AVRational){1,100});
        }
        ptr = srt_to_ass(avctx, buffer, buffer+sizeof(buffer), ptr,
                         x1, y1, x2, y2);
        ff_ass_add_rect(sub, buffer, ts_start, ts_end-ts_start, 0);
    }

    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

#if CONFIG_SRT_DECODER
/* deprecated decoder */
AVCodec ff_srt_decoder = {
    .name         = "srt",
    .long_name    = NULL_IF_CONFIG_SMALL("SubRip subtitle with embedded timing"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SRT,
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
