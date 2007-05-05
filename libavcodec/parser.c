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
            s->last_offset = 0;
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
        s->offset = s->last_offset;

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
        s->last_offset = s->last_frame_offset - s->cur_frame_offset[k];

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

    /* Copy overread bytes from last frame into buffer. */
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
int ff_ac3_parse_header(const uint8_t buf[7], AC3HeaderInfo *hdr)
{
    GetBitContext gbc;

    memset(hdr, 0, sizeof(*hdr));

    init_get_bits(&gbc, buf, 54);

    hdr->sync_word = get_bits(&gbc, 16);
    if(hdr->sync_word != 0x0B77)
        return -1;

    /* read ahead to bsid to make sure this is AC-3, not E-AC-3 */
    hdr->bsid = show_bits_long(&gbc, 29) & 0x1F;
    if(hdr->bsid > 10)
        return -2;

    hdr->crc1 = get_bits(&gbc, 16);
    hdr->fscod = get_bits(&gbc, 2);
    if(hdr->fscod == 3)
        return -3;

    hdr->frmsizecod = get_bits(&gbc, 6);
    if(hdr->frmsizecod > 37)
        return -4;

    skip_bits(&gbc, 5); // skip bsid, already got it

    hdr->bsmod = get_bits(&gbc, 3);
    hdr->acmod = get_bits(&gbc, 3);
    if((hdr->acmod & 1) && hdr->acmod != 1) {
        hdr->cmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod & 4) {
        hdr->surmixlev = get_bits(&gbc, 2);
    }
    if(hdr->acmod == 2) {
        hdr->dsurmod = get_bits(&gbc, 2);
    }
    hdr->lfeon = get_bits1(&gbc);

    hdr->halfratecod = FFMAX(hdr->bsid, 8) - 8;
    hdr->sample_rate = ff_ac3_freqs[hdr->fscod] >> hdr->halfratecod;
    hdr->bit_rate = (ff_ac3_bitratetab[hdr->frmsizecod>>1] * 1000) >> hdr->halfratecod;
    hdr->channels = ff_ac3_channels[hdr->acmod] + hdr->lfeon;
    hdr->frame_size = ff_ac3_frame_sizes[hdr->frmsizecod][hdr->fscod] * 2;

    return 0;
}

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
