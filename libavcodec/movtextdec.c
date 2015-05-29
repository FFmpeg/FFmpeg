/*
 * 3GPP TS 26.245 Timed Text decoder
 * Copyright (c) 2012  Philip Langdale <philipl@overt.org>
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
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#define STYLE_FLAG_BOLD         1
#define STYLE_FLAG_ITALIC       2
#define STYLE_FLAG_UNDERLINE    4

static int text_to_ass(AVBPrint *buf, const char *text, const char *text_end,
                        char **style_start, char **style_end,
                        uint8_t **style_flags, int style_entries)
{
    int i = 0;
    while (text < text_end) {
        for (i = 0; i < style_entries; i++) {
            if (*style_flags[i] && text == style_start[i]) {
                if (*style_flags[i] & STYLE_FLAG_BOLD)
                    av_bprintf(buf, "{\\b1}");
                if (*style_flags[i] & STYLE_FLAG_ITALIC)
                    av_bprintf(buf, "{\\i1}");
                if (*style_flags[i] & STYLE_FLAG_UNDERLINE)
                    av_bprintf(buf, "{\\u1}");
            }
        }

        switch (*text) {
        case '\r':
            break;
        case '\n':
            av_bprintf(buf, "\\N");
            break;
        default:
            av_bprint_chars(buf, *text, 1);
            break;
        }

        for (i = 0; i < style_entries; i++) {
            if (*style_flags[i] && text == style_end[i]) {
                if (*style_flags[i] & STYLE_FLAG_BOLD)
                    av_bprintf(buf, "{\\b0}");
                if (*style_flags[i] & STYLE_FLAG_ITALIC)
                    av_bprintf(buf, "{\\i0}");
                if (*style_flags[i] & STYLE_FLAG_UNDERLINE)
                    av_bprintf(buf, "{\\u0}");
            }
        }
        text++;
    }

    return 0;
}

static int mov_text_init(AVCodecContext *avctx) {
    /*
     * TODO: Handle the default text style.
     * NB: Most players ignore styles completely, with the result that
     * it's very common to find files where the default style is broken
     * and respecting it results in a worse experience than ignoring it.
     */
    return ff_ass_subtitle_header_default(avctx);
}

static int mov_text_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    int ret, ts_start, ts_end;
    AVBPrint buf;
    char *ptr = avpkt->data;
    char *end;
    //char *ptr_temp;
    int text_length, tsmb_type, style_entries;
    uint64_t tsmb_size, tracksize;
    char **style_start = { 0, };
    char **style_end = { 0, };
    uint8_t **style_flags = { 0, };
    const uint8_t *tsmb;
    int index, i, size_var;
    uint8_t *flag;
    char *style_pos;

    if (!ptr || avpkt->size < 2)
        return AVERROR_INVALIDDATA;

    /*
     * A packet of size two with value zero is an empty subtitle
     * used to mark the end of the previous non-empty subtitle.
     * We can just drop them here as we have duration information
     * already. If the value is non-zero, then it's technically a
     * bad packet.
     */
    if (avpkt->size == 2)
        return AV_RB16(ptr) == 0 ? 0 : AVERROR_INVALIDDATA;

    /*
     * The first two bytes of the packet are the length of the text string
     * In complex cases, there are style descriptors appended to the string
     * so we can't just assume the packet size is the string size.
     */
    text_length = AV_RB16(ptr);
    end = ptr + FFMIN(2 + text_length, avpkt->size);
    ptr += 2;

    ts_start = av_rescale_q(avpkt->pts,
                            avctx->time_base,
                            (AVRational){1,100});
    ts_end   = av_rescale_q(avpkt->pts + avpkt->duration,
                            avctx->time_base,
                            (AVRational){1,100});

    tsmb_size = 0;
    tracksize = 2 + text_length;
    // Note that the spec recommends lines be no longer than 2048 characters.
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (text_length + 2 != avpkt->size) {
        while (tracksize + 8 <= avpkt->size) {
            // A box is a minimum of 8 bytes.
            tsmb = ptr + tracksize - 2;
            tsmb_size = AV_RB32(tsmb);
            tsmb += 4;
            tsmb_type = AV_RB32(tsmb);
            tsmb += 4;

            if (tsmb_size == 1) {
                if (tracksize + 16 > avpkt->size)
                    break;
                tsmb_size = AV_RB64(tsmb);
                tsmb += 8;
                size_var = 18;
            } else
                size_var = 10;
            //size_var is equal to 10 or 18 depending on the size of box

            if (tracksize + tsmb_size > avpkt->size)
                break;

            if (tsmb_type == MKBETAG('s','t','y','l')) {
                if (tracksize + size_var > avpkt->size)
                    break;
                style_entries = AV_RB16(tsmb);
                tsmb += 2;

                // A single style record is of length 12 bytes.
                if (tracksize + size_var + style_entries * 12 > avpkt->size)
                    break;

                for(i = 0; i < style_entries; i++) {
                    style_pos = ptr + AV_RB16(tsmb);
                    index = i;
                    av_dynarray_add(&style_start, &index, style_pos);
                    tsmb += 2;
                    style_pos = ptr + AV_RB16(tsmb);
                    index = i;
                    av_dynarray_add(&style_end, &index, style_pos);
                    tsmb += 2;
                    // fontID = AV_RB16(tsmb);
                    tsmb += 2;
                    flag = av_malloc(1);
                    if (!flag)
                        return AVERROR(ENOMEM);
                    *flag = AV_RB8(tsmb);
                    index = i;
                    av_dynarray_add(&style_flags, &index, flag);
                    //fontsize=AV_RB8(tsmb);
                    tsmb += 2;
                    // text-color-rgba
                    tsmb += 4;
                }
                text_to_ass(&buf, ptr, end, style_start, style_end, style_flags, style_entries);

                for(i = 0; i < style_entries; i++) {
                    av_freep(&style_flags[i]);
                }
                av_freep(&style_start);
                av_freep(&style_end);
                av_freep(&style_flags);
            }
            tracksize = tracksize + tsmb_size;
        }
    } else
        text_to_ass(&buf, ptr, end, NULL, NULL, 0, 0);

    ret = ff_ass_add_rect_bprint(sub, &buf, ts_start, ts_end - ts_start);
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

AVCodec ff_movtext_decoder = {
    .name         = "mov_text",
    .long_name    = NULL_IF_CONFIG_SMALL("3GPP Timed Text subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_MOV_TEXT,
    .init         = mov_text_init,
    .decode       = mov_text_decode_frame,
};
