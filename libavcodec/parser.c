/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
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
#include "mpegvideo.h"
#include "mpegaudio.h"
#include "ac3.h"
#include "parser.h"

AVCodecParser *av_first_parser = NULL;

void av_register_codec_parser(AVCodecParser *parser)
{
    parser->next = av_first_parser;
    av_first_parser = parser;
}

AVCodecParserContext *av_parser_init(int codec_id)
{
    AVCodecParserContext *s;
    AVCodecParser *parser;
    int ret;

    if(codec_id == CODEC_ID_NONE)
        return NULL;

    for(parser = av_first_parser; parser != NULL; parser = parser->next) {
        if (parser->codec_ids[0] == codec_id ||
            parser->codec_ids[1] == codec_id ||
            parser->codec_ids[2] == codec_id ||
            parser->codec_ids[3] == codec_id ||
            parser->codec_ids[4] == codec_id)
            goto found;
    }
    return NULL;
 found:
    s = av_mallocz(sizeof(AVCodecParserContext));
    if (!s)
        return NULL;
    s->parser = parser;
    s->priv_data = av_mallocz(parser->priv_data_size);
    if (!s->priv_data) {
        av_free(s);
        return NULL;
    }
    if (parser->parser_init) {
        ret = parser->parser_init(s);
        if (ret != 0) {
            av_free(s->priv_data);
            av_free(s);
            return NULL;
        }
    }
    s->fetch_timestamp=1;
    s->pict_type = FF_I_TYPE;
    return s;
}

/**
 *
 * @param buf           input
 * @param buf_size      input length, to signal EOF, this should be 0 (so that the last frame can be output)
 * @param pts           input presentation timestamp
 * @param dts           input decoding timestamp
 * @param poutbuf       will contain a pointer to the first byte of the output frame
 * @param poutbuf_size  will contain the length of the output frame
 * @return the number of bytes of the input bitstream used
 *
 * Example:
 * @code
 *   while(in_len){
 *       len = av_parser_parse(myparser, AVCodecContext, &data, &size,
 *                                       in_data, in_len,
 *                                       pts, dts);
 *       in_data += len;
 *       in_len  -= len;
 *
 *       if(size)
 *          decode_frame(data, size);
 *   }
 * @endcode
 */
int av_parser_parse(AVCodecParserContext *s,
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size,
                    const uint8_t *buf, int buf_size,
                    int64_t pts, int64_t dts)
{
    int index, i, k;
    uint8_t dummy_buf[FF_INPUT_BUFFER_PADDING_SIZE];

    if (buf_size == 0) {
        /* padding is always necessary even if EOF, so we add it here */
        memset(dummy_buf, 0, sizeof(dummy_buf));
        buf = dummy_buf;
    } else {
        /* add a new packet descriptor */
        k = (s->cur_frame_start_index + 1) & (AV_PARSER_PTS_NB - 1);
        s->cur_frame_start_index = k;
        s->cur_frame_offset[k] = s->cur_offset;
        s->cur_frame_pts[k] = pts;
        s->cur_frame_dts[k] = dts;

        /* fill first PTS/DTS */
        if (s->fetch_timestamp){
            s->fetch_timestamp=0;
            s->last_pts = pts;
            s->last_dts = dts;
            s->cur_frame_pts[k] =
            s->cur_frame_dts[k] = AV_NOPTS_VALUE;
        }
    }

    /* WARNING: the returned index can be negative */
    index = s->parser->parser_parse(s, avctx, poutbuf, poutbuf_size, buf, buf_size);
//av_log(NULL, AV_LOG_DEBUG, "parser: in:%"PRId64", %"PRId64", out:%"PRId64", %"PRId64", in:%d out:%d id:%d\n", pts, dts, s->last_pts, s->last_dts, buf_size, *poutbuf_size, avctx->codec_id);
    /* update the file pointer */
    if (*poutbuf_size) {
        /* fill the data for the current frame */
        s->frame_offset = s->last_frame_offset;
        s->pts = s->last_pts;
        s->dts = s->last_dts;

        /* offset of the next frame */
        s->last_frame_offset = s->cur_offset + index;
        /* find the packet in which the new frame starts. It
           is tricky because of MPEG video start codes
           which can begin in one packet and finish in
           another packet. In the worst case, an MPEG
           video start code could be in 4 different
           packets. */
        k = s->cur_frame_start_index;
        for(i = 0; i < AV_PARSER_PTS_NB; i++) {
            if (s->last_frame_offset >= s->cur_frame_offset[k])
                break;
            k = (k - 1) & (AV_PARSER_PTS_NB - 1);
        }

        s->last_pts = s->cur_frame_pts[k];
        s->last_dts = s->cur_frame_dts[k];

        /* some parsers tell us the packet size even before seeing the first byte of the next packet,
           so the next pts/dts is in the next chunk */
        if(index == buf_size){
            s->fetch_timestamp=1;
        }
    }
    if (index < 0)
        index = 0;
    s->cur_offset += index;
    return index;
}

/**
 *
 * @return 0 if the output buffer is a subset of the input, 1 if it is allocated and must be freed
 * @deprecated use AVBitstreamFilter
 */
int av_parser_change(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size, int keyframe){

    if(s && s->parser->split){
        if((avctx->flags & CODEC_FLAG_GLOBAL_HEADER) || (avctx->flags2 & CODEC_FLAG2_LOCAL_HEADER)){
            int i= s->parser->split(avctx, buf, buf_size);
            buf += i;
            buf_size -= i;
        }
    }

    /* cast to avoid warning about discarding qualifiers */
    *poutbuf= (uint8_t *) buf;
    *poutbuf_size= buf_size;
    if(avctx->extradata){
        if(  (keyframe && (avctx->flags2 & CODEC_FLAG2_LOCAL_HEADER))
            /*||(s->pict_type != I_TYPE && (s->flags & PARSER_FLAG_DUMP_EXTRADATA_AT_NOKEY))*/
            /*||(? && (s->flags & PARSER_FLAG_DUMP_EXTRADATA_AT_BEGIN)*/){
            int size= buf_size + avctx->extradata_size;
            *poutbuf_size= size;
            *poutbuf= av_malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);

            memcpy(*poutbuf, avctx->extradata, avctx->extradata_size);
            memcpy((*poutbuf) + avctx->extradata_size, buf, buf_size + FF_INPUT_BUFFER_PADDING_SIZE);
            return 1;
        }
    }

    return 0;
}

void av_parser_close(AVCodecParserContext *s)
{
    if (s->parser->parser_close)
        s->parser->parser_close(s);
    av_free(s->priv_data);
    av_free(s);
}

/*****************************************************/

/**
 * combines the (truncated) bitstream to a complete frame
 * @returns -1 if no complete frame could be created
 */
int ff_combine_frame(ParseContext *pc, int next, uint8_t **buf, int *buf_size)
{
#if 0
    if(pc->overread){
        printf("overread %d, state:%X next:%d index:%d o_index:%d\n", pc->overread, pc->state, next, pc->index, pc->overread_index);
        printf("%X %X %X %X\n", (*buf)[0], (*buf)[1],(*buf)[2],(*buf)[3]);
    }
#endif

    /* copy overreaded bytes from last frame into buffer */
    for(; pc->overread>0; pc->overread--){
        pc->buffer[pc->index++]= pc->buffer[pc->overread_index++];
    }

    /* flush remaining if EOF */
    if(!*buf_size && next == END_NOT_FOUND){
        next= 0;
    }

    pc->last_index= pc->index;

    /* copy into buffer end return */
    if(next == END_NOT_FOUND){
        pc->buffer= av_fast_realloc(pc->buffer, &pc->buffer_size, (*buf_size) + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        memcpy(&pc->buffer[pc->index], *buf, *buf_size);
        pc->index += *buf_size;
        return -1;
    }

    *buf_size=
    pc->overread_index= pc->index + next;

    /* append to buffer */
    if(pc->index){
        pc->buffer= av_fast_realloc(pc->buffer, &pc->buffer_size, next + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        memcpy(&pc->buffer[pc->index], *buf, next + FF_INPUT_BUFFER_PADDING_SIZE );
        pc->index = 0;
        *buf= pc->buffer;
    }

    /* store overread bytes */
    for(;next < 0; next++){
        pc->state = (pc->state<<8) | pc->buffer[pc->last_index + next];
        pc->overread++;
    }

#if 0
    if(pc->overread){
        printf("overread %d, state:%X next:%d index:%d o_index:%d\n", pc->overread, pc->state, next, pc->index, pc->overread_index);
        printf("%X %X %X %X\n", (*buf)[0], (*buf)[1],(*buf)[2],(*buf)[3]);
    }
#endif

    return 0;
}

void ff_parse_close(AVCodecParserContext *s)
{
    ParseContext *pc = s->priv_data;

    av_free(pc->buffer);
}

void ff_parse1_close(AVCodecParserContext *s)
{
    ParseContext1 *pc1 = s->priv_data;

    av_free(pc1->pc.buffer);
    av_free(pc1->enc);
}

/*************************/

#ifdef CONFIG_MPEG4VIDEO_PARSER
/* used by parser */
/* XXX: make it use less memory */
static int av_mpeg4_decode_header(AVCodecParserContext *s1,
                                  AVCodecContext *avctx,
                                  const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s1->priv_data;
    MpegEncContext *s = pc->enc;
    GetBitContext gb1, *gb = &gb1;
    int ret;

    s->avctx = avctx;
    s->current_picture_ptr = &s->current_picture;

    if (avctx->extradata_size && pc->first_picture){
        init_get_bits(gb, avctx->extradata, avctx->extradata_size*8);
        ret = ff_mpeg4_decode_picture_header(s, gb);
    }

    init_get_bits(gb, buf, 8 * buf_size);
    ret = ff_mpeg4_decode_picture_header(s, gb);
    if (s->width) {
        avcodec_set_dimensions(avctx, s->width, s->height);
    }
    s1->pict_type= s->pict_type;
    pc->first_picture = 0;
    return ret;
}

static int mpeg4video_parse_init(AVCodecParserContext *s)
{
    ParseContext1 *pc = s->priv_data;

    pc->enc = av_mallocz(sizeof(MpegEncContext));
    if (!pc->enc)
        return -1;
    pc->first_picture = 1;
    return 0;
}

static int mpeg4video_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    if(s->flags & PARSER_FLAG_COMPLETE_FRAMES){
        next= buf_size;
    }else{
        next= ff_mpeg4_find_frame_end(pc, buf, buf_size);

        if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
            *poutbuf = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }
    av_mpeg4_decode_header(s, avctx, buf, buf_size);

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}
#endif

int ff_mpeg4video_split(AVCodecContext *avctx,
                           const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state= -1;

    for(i=0; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if(state == 0x1B3 || state == 0x1B6)
            return i-3;
    }
    return 0;
}

/*************************/

#ifdef CONFIG_MPEGAUDIO_PARSER
typedef struct MpegAudioParseContext {
    uint8_t inbuf[MPA_MAX_CODED_FRAME_SIZE];    /* input buffer */
    uint8_t *inbuf_ptr;
    int frame_size;
    int free_format_frame_size;
    int free_format_next_header;
    uint32_t header;
    int header_count;
} MpegAudioParseContext;

#define MPA_HEADER_SIZE 4

/* header + layer + bitrate + freq + lsf/mpeg25 */
#undef SAME_HEADER_MASK /* mpegaudio.h defines different version */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (3 << 10) | (3 << 19))

static int mpegaudio_parse_init(AVCodecParserContext *s1)
{
    MpegAudioParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
    return 0;
}

static int mpegaudio_parse(AVCodecParserContext *s1,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    MpegAudioParseContext *s = s1->priv_data;
    int len, ret, sr;
    uint32_t header;
    const uint8_t *buf_ptr;

    *poutbuf = NULL;
    *poutbuf_size = 0;
    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* special case for next header for first frame in free
               format case (XXX: find a simpler method) */
            if (s->free_format_next_header != 0) {
                s->inbuf[0] = s->free_format_next_header >> 24;
                s->inbuf[1] = s->free_format_next_header >> 16;
                s->inbuf[2] = s->free_format_next_header >> 8;
                s->inbuf[3] = s->free_format_next_header;
                s->inbuf_ptr = s->inbuf + 4;
                s->free_format_next_header = 0;
                goto got_header;
            }
            /* no header seen : find one. We need at least MPA_HEADER_SIZE
               bytes to parse it */
            len = FFMIN(MPA_HEADER_SIZE - len, buf_size);
            if (len > 0) {
                memcpy(s->inbuf_ptr, buf_ptr, len);
                buf_ptr += len;
                buf_size -= len;
                s->inbuf_ptr += len;
            }
            if ((s->inbuf_ptr - s->inbuf) >= MPA_HEADER_SIZE) {
            got_header:
                header = (s->inbuf[0] << 24) | (s->inbuf[1] << 16) |
                    (s->inbuf[2] << 8) | s->inbuf[3];

                ret = mpa_decode_header(avctx, header, &sr);
                if (ret < 0) {
                    s->header_count= -2;
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memmove(s->inbuf, s->inbuf + 1, s->inbuf_ptr - s->inbuf - 1);
                    s->inbuf_ptr--;
                    dprintf(avctx, "skip %x\n", header);
                    /* reset free format frame size to give a chance
                       to get a new bitrate */
                    s->free_format_frame_size = 0;
                } else {
                    if((header&SAME_HEADER_MASK) != (s->header&SAME_HEADER_MASK) && s->header)
                        s->header_count= -3;
                    s->header= header;
                    s->header_count++;
                    s->frame_size = ret;

#if 0
                    /* free format: prepare to compute frame size */
                    if (decode_header(s, header) == 1) {
                        s->frame_size = -1;
                    }
#endif
                }
                if(s->header_count > 1)
                    avctx->sample_rate= sr;
            }
        } else
#if 0
        if (s->frame_size == -1) {
            /* free format : find next sync to compute frame size */
            len = MPA_MAX_CODED_FRAME_SIZE - len;
            if (len > buf_size)
                len = buf_size;
            if (len == 0) {
                /* frame too long: resync */
                s->frame_size = 0;
                memmove(s->inbuf, s->inbuf + 1, s->inbuf_ptr - s->inbuf - 1);
                s->inbuf_ptr--;
            } else {
                uint8_t *p, *pend;
                uint32_t header1;
                int padding;

                memcpy(s->inbuf_ptr, buf_ptr, len);
                /* check for header */
                p = s->inbuf_ptr - 3;
                pend = s->inbuf_ptr + len - 4;
                while (p <= pend) {
                    header = (p[0] << 24) | (p[1] << 16) |
                        (p[2] << 8) | p[3];
                    header1 = (s->inbuf[0] << 24) | (s->inbuf[1] << 16) |
                        (s->inbuf[2] << 8) | s->inbuf[3];
                    /* check with high probability that we have a
                       valid header */
                    if ((header & SAME_HEADER_MASK) ==
                        (header1 & SAME_HEADER_MASK)) {
                        /* header found: update pointers */
                        len = (p + 4) - s->inbuf_ptr;
                        buf_ptr += len;
                        buf_size -= len;
                        s->inbuf_ptr = p;
                        /* compute frame size */
                        s->free_format_next_header = header;
                        s->free_format_frame_size = s->inbuf_ptr - s->inbuf;
                        padding = (header1 >> 9) & 1;
                        if (s->layer == 1)
                            s->free_format_frame_size -= padding * 4;
                        else
                            s->free_format_frame_size -= padding;
                        dprintf(avctx, "free frame size=%d padding=%d\n",
                                s->free_format_frame_size, padding);
                        decode_header(s, header1);
                        goto next_data;
                    }
                    p++;
                }
                /* not found: simply increase pointers */
                buf_ptr += len;
                s->inbuf_ptr += len;
                buf_size -= len;
            }
        } else
#endif
        if (len < s->frame_size) {
            if (s->frame_size > MPA_MAX_CODED_FRAME_SIZE)
                s->frame_size = MPA_MAX_CODED_FRAME_SIZE;
            len = FFMIN(s->frame_size - len, buf_size);
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
        }

        if(s->frame_size > 0 && buf_ptr - buf == s->inbuf_ptr - s->inbuf
           && buf_size + buf_ptr - buf >= s->frame_size){
            if(s->header_count > 0){
                *poutbuf = buf;
                *poutbuf_size = s->frame_size;
            }
            buf_ptr = buf + s->frame_size;
            s->inbuf_ptr = s->inbuf;
            s->frame_size = 0;
            break;
        }

        //    next_data:
        if (s->frame_size > 0 &&
            (s->inbuf_ptr - s->inbuf) >= s->frame_size) {
            if(s->header_count > 0){
                *poutbuf = s->inbuf;
                *poutbuf_size = s->inbuf_ptr - s->inbuf;
            }
            s->inbuf_ptr = s->inbuf;
            s->frame_size = 0;
            break;
        }
    }
    return buf_ptr - buf;
}
#endif /* CONFIG_MPEGAUDIO_PARSER */

#if defined(CONFIG_AC3_PARSER) || defined(CONFIG_AAC_PARSER)
/* also used for ADTS AAC */
typedef struct AC3ParseContext {
    uint8_t *inbuf_ptr;
    int frame_size;
    int header_size;
    int (*sync)(const uint8_t *buf, int *channels, int *sample_rate,
                int *bit_rate, int *samples);
    uint8_t inbuf[8192]; /* input buffer */
} AC3ParseContext;

#define AC3_HEADER_SIZE 7
#define AAC_HEADER_SIZE 7

#ifdef CONFIG_AC3_PARSER

static const uint8_t eac3_blocks[4] = {
    1, 2, 3, 6
};

#endif /* CONFIG_AC3_PARSER */

#ifdef CONFIG_AAC_PARSER
static const int aac_sample_rates[16] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

static const int aac_channels[8] = {
    0, 1, 2, 3, 4, 5, 6, 8
};
#endif

#ifdef CONFIG_AC3_PARSER
static int ac3_sync(const uint8_t *buf, int *channels, int *sample_rate,
                    int *bit_rate, int *samples)
{
    int err;
    unsigned int fscod, acmod, bsid, lfeon;
    unsigned int strmtyp, substreamid, frmsiz, fscod2, numblkscod;
    GetBitContext bits;
    AC3HeaderInfo hdr;

    err = ff_ac3_parse_header(buf, &hdr);

    if(err < 0 && err != -2)
        return 0;

    bsid = hdr.bsid;
    if(bsid <= 10) {             /* Normal AC-3 */
        *sample_rate = hdr.sample_rate;
        *bit_rate = hdr.bit_rate;
        *channels = hdr.channels;
        *samples = AC3_FRAME_SIZE;
        return hdr.frame_size;
    } else if (bsid > 10 && bsid <= 16) { /* Enhanced AC-3 */
        init_get_bits(&bits, &buf[2], (AC3_HEADER_SIZE-2) * 8);
        strmtyp = get_bits(&bits, 2);
        substreamid = get_bits(&bits, 3);

        if (strmtyp != 0 || substreamid != 0)
            return 0;   /* Currently don't support additional streams */

        frmsiz = get_bits(&bits, 11) + 1;
        fscod = get_bits(&bits, 2);
        if (fscod == 3) {
            fscod2 = get_bits(&bits, 2);
            numblkscod = 3;

            if(fscod2 == 3)
                return 0;

            *sample_rate = ff_ac3_freqs[fscod2] / 2;
        } else {
            numblkscod = get_bits(&bits, 2);

            *sample_rate = ff_ac3_freqs[fscod];
        }

        acmod = get_bits(&bits, 3);
        lfeon = get_bits1(&bits);

        *samples = eac3_blocks[numblkscod] * 256;
        *bit_rate = frmsiz * (*sample_rate) * 16 / (*samples);
        *channels = ff_ac3_channels[acmod] + lfeon;

        return frmsiz * 2;
    }

    /* Unsupported bitstream version */
    return 0;
}
#endif /* CONFIG_AC3_PARSER */

#ifdef CONFIG_AAC_PARSER
static int aac_sync(const uint8_t *buf, int *channels, int *sample_rate,
                    int *bit_rate, int *samples)
{
    GetBitContext bits;
    int size, rdb, ch, sr;

    init_get_bits(&bits, buf, AAC_HEADER_SIZE * 8);

    if(get_bits(&bits, 12) != 0xfff)
        return 0;

    skip_bits1(&bits);          /* id */
    skip_bits(&bits, 2);        /* layer */
    skip_bits1(&bits);          /* protection_absent */
    skip_bits(&bits, 2);        /* profile_objecttype */
    sr = get_bits(&bits, 4);    /* sample_frequency_index */
    if(!aac_sample_rates[sr])
        return 0;
    skip_bits1(&bits);          /* private_bit */
    ch = get_bits(&bits, 3);    /* channel_configuration */
    if(!aac_channels[ch])
        return 0;
    skip_bits1(&bits);          /* original/copy */
    skip_bits1(&bits);          /* home */

    /* adts_variable_header */
    skip_bits1(&bits);          /* copyright_identification_bit */
    skip_bits1(&bits);          /* copyright_identification_start */
    size = get_bits(&bits, 13); /* aac_frame_length */
    skip_bits(&bits, 11);       /* adts_buffer_fullness */
    rdb = get_bits(&bits, 2);   /* number_of_raw_data_blocks_in_frame */

    *channels = aac_channels[ch];
    *sample_rate = aac_sample_rates[sr];
    *samples = (rdb + 1) * 1024;
    *bit_rate = size * 8 * *sample_rate / *samples;

    return size;
}
#endif /* CONFIG_AAC_PARSER */

#ifdef CONFIG_AC3_PARSER
static int ac3_parse_init(AVCodecParserContext *s1)
{
    AC3ParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
    s->header_size = AC3_HEADER_SIZE;
    s->sync = ac3_sync;
    return 0;
}
#endif

#ifdef CONFIG_AAC_PARSER
static int aac_parse_init(AVCodecParserContext *s1)
{
    AC3ParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
    s->header_size = AAC_HEADER_SIZE;
    s->sync = aac_sync;
    return 0;
}
#endif

/* also used for ADTS AAC */
static int ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    AC3ParseContext *s = s1->priv_data;
    const uint8_t *buf_ptr;
    int len, sample_rate, bit_rate, channels, samples;

    *poutbuf = NULL;
    *poutbuf_size = 0;

    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* no header seen : find one. We need at least s->header_size
               bytes to parse it */
            len = FFMIN(s->header_size - len, buf_size);

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
            if ((s->inbuf_ptr - s->inbuf) == s->header_size) {
                len = s->sync(s->inbuf, &channels, &sample_rate, &bit_rate,
                              &samples);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memmove(s->inbuf, s->inbuf + 1, s->header_size - 1);
                    s->inbuf_ptr--;
                } else {
                    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = sample_rate;
                    /* set channels,except if the user explicitly requests 1 or 2 channels, XXX/FIXME this is a bit ugly */
                    if(avctx->codec_id == CODEC_ID_AC3){
                        if(avctx->channels!=1 && avctx->channels!=2){
                            avctx->channels = channels;
                        }
                    } else {
                        avctx->channels = channels;
                    }
                    avctx->bit_rate = bit_rate;
                    avctx->frame_size = samples;
                }
            }
        } else {
            len = FFMIN(s->frame_size - len, buf_size);

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;

            if(s->inbuf_ptr - s->inbuf == s->frame_size){
                *poutbuf = s->inbuf;
                *poutbuf_size = s->frame_size;
                s->inbuf_ptr = s->inbuf;
                s->frame_size = 0;
                break;
            }
        }
    }
    return buf_ptr - buf;
}
#endif /* CONFIG_AC3_PARSER || CONFIG_AAC_PARSER */

#ifdef CONFIG_MPEG4VIDEO_PARSER
AVCodecParser mpeg4video_parser = {
    { CODEC_ID_MPEG4 },
    sizeof(ParseContext1),
    mpeg4video_parse_init,
    mpeg4video_parse,
    ff_parse1_close,
    ff_mpeg4video_split,
};
#endif
#ifdef CONFIG_MPEGAUDIO_PARSER
AVCodecParser mpegaudio_parser = {
    { CODEC_ID_MP2, CODEC_ID_MP3 },
    sizeof(MpegAudioParseContext),
    mpegaudio_parse_init,
    mpegaudio_parse,
    NULL,
};
#endif
#ifdef CONFIG_AC3_PARSER
AVCodecParser ac3_parser = {
    { CODEC_ID_AC3 },
    sizeof(AC3ParseContext),
    ac3_parse_init,
    ac3_parse,
    NULL,
};
#endif
#ifdef CONFIG_AAC_PARSER
AVCodecParser aac_parser = {
    { CODEC_ID_AAC },
    sizeof(AC3ParseContext),
    aac_parse_init,
    ac3_parse,
    NULL,
};
#endif
