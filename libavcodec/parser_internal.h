/*
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

#ifndef AVCODEC_PARSER_INTERNAL_H
#define AVCODEC_PARSER_INTERNAL_H

#include <stdint.h>

#include "libavutil/macros.h"
#include "avcodec.h"
#include "codec_id.h"

#if FF_API_PARSER_PRIVATE
typedef union FFCodecParser {
    struct {
#if FF_API_PARSER_CODECID
        int codec_ids[7]; /* several codec IDs are permitted */
#else
        enum AVCodecID codec_ids[7]; /* several codec IDs are permitted */
#endif
        int priv_data_size;
        int (*init)(AVCodecParserContext *s);
        int (*parse)(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size);
        void (*close)(AVCodecParserContext *s);
        int (*split)(AVCodecContext *avctx, const uint8_t *buf, int buf_size);
    };
    AVCodecParser p;
#else
typedef struct FFCodecParser {
    AVCodecParser p;
    unsigned priv_data_size;
    int (*init)(AVCodecParserContext *s);
    int (*parse)(AVCodecParserContext *s,
                 AVCodecContext *avctx,
                 const uint8_t **poutbuf, int *poutbuf_size,
                 const uint8_t *buf, int buf_size);
    void (*close)(AVCodecParserContext *s);
#endif
} FFCodecParser;

static inline const FFCodecParser *ffcodecparser(const AVCodecParser *parser)
{
    return (const FFCodecParser*)parser;
}

#define EIGTH_ARG(a,b,c,d,e,f,g,h,...) h
#define NO_FAIL
// Expands to nothing if <= 7 args; induces compilation failure if not.
#define CHECK_FOR_TOO_MANY_IDS(...) AV_JOIN(EIGTH_ARG(__VA_ARGS__, NO, NO, NO, NO, NO, NO, NO, NO), _FAIL)

// For compatibility with MSVC's old, spec-incompliant preprocessor.
#define FF_MSVC_EXPAND(...) __VA_ARGS__
#define FIRST_SEVEN2(a,b,c,d,e,f,g,...) a,b,c,d,e,f,g
#define FIRST_SEVEN(...) FF_MSVC_EXPAND(FIRST_SEVEN2(__VA_ARGS__))
#define TIMES_SEVEN(a) a,a,a,a,a,a,a

#if FF_API_PARSER_PRIVATE
#define PARSER_CODEC_LIST(...) CHECK_FOR_TOO_MANY_IDS(__VA_ARGS__) \
    .codec_ids = { FIRST_SEVEN(__VA_ARGS__, TIMES_SEVEN(AV_CODEC_ID_NONE)) }
#else
#define PARSER_CODEC_LIST(...) CHECK_FOR_TOO_MANY_IDS(__VA_ARGS__) \
    .p.codec_ids = { FIRST_SEVEN(__VA_ARGS__, TIMES_SEVEN(AV_CODEC_ID_NONE)) }
#endif

#endif /* AVCODEC_PARSER_INTERNAL_H */
