/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2003 Michael Niedermayer
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

#include "parser.h"

static AVCodecParser *av_first_parser = NULL;

AVCodecParser* av_parser_next(AVCodecParser *p){
    if(p) return p->next;
    else  return av_first_parser;
}

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
    s->pict_type = AV_PICTURE_TYPE_I;
    s->key_frame = -1;
    s->convergence_duration = 0;
    s->dts_sync_point       = INT_MIN;
    s->dts_ref_dts_delta    = INT_MIN;
    s->pts_dts_delta        = INT_MIN;
    return s;
}

void ff_fetch_timestamp(AVCodecParserContext *s, int off, int remove){
    int i;

    s->dts= s->pts= AV_NOPTS_VALUE;
    s->pos= -1;
    s->offset= 0;
    for(i = 0; i < AV_PARSER_PTS_NB; i++) {
        if (   s->cur_offset + off >= s->cur_frame_offset[i]
            && (s->frame_offset < s->cur_frame_offset[i] ||
              (!s->frame_offset && !s->next_frame_offset)) // first field/frame
            //check is disabled  because mpeg-ts doesnt send complete PES packets
            && /*s->next_frame_offset + off <*/  s->cur_frame_end[i]){
            s->dts= s->cur_frame_dts[i];
            s->pts= s->cur_frame_pts[i];
            s->pos= s->cur_frame_pos[i];
            s->offset = s->next_frame_offset - s->cur_frame_offset[i];
            if(remove)
                s->cur_frame_offset[i]= INT64_MAX;
            if(s->cur_offset + off < s->cur_frame_end[i])
                break;
        }
    }
}

int av_parser_parse2(AVCodecParserContext *s,
                     AVCodecContext *avctx,
                     uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size,
                     int64_t pts, int64_t dts,
                     int64_t pos)
{
    int index, i;
    uint8_t dummy_buf[FF_INPUT_BUFFER_PADDING_SIZE];

    if(!(s->flags & PARSER_FLAG_FETCHED_OFFSET)) {
        s->next_frame_offset =
        s->cur_offset        = pos;
        s->flags |= PARSER_FLAG_FETCHED_OFFSET;
    }

    if (buf_size == 0) {
        /* padding is always necessary even if EOF, so we add it here */
        memset(dummy_buf, 0, sizeof(dummy_buf));
        buf = dummy_buf;
    } else if (s->cur_offset + buf_size !=
               s->cur_frame_end[s->cur_frame_start_index]) { /* skip remainder packets */
        /* add a new packet descriptor */
            i = (s->cur_frame_start_index + 1) & (AV_PARSER_PTS_NB - 1);
            s->cur_frame_start_index = i;
            s->cur_frame_offset[i] = s->cur_offset;
            s->cur_frame_end[i] = s->cur_offset + buf_size;
            s->cur_frame_pts[i] = pts;
            s->cur_frame_dts[i] = dts;
            s->cur_frame_pos[i] = pos;
    }

    if (s->fetch_timestamp){
        s->fetch_timestamp=0;
        s->last_pts = s->pts;
        s->last_dts = s->dts;
        s->last_pos = s->pos;
        ff_fetch_timestamp(s, 0, 0);
    }

    /* WARNING: the returned index can be negative */
    index = s->parser->parser_parse(s, avctx, (const uint8_t **)poutbuf, poutbuf_size, buf, buf_size);
//av_log(NULL, AV_LOG_DEBUG, "parser: in:%"PRId64", %"PRId64", out:%"PRId64", %"PRId64", in:%d out:%d id:%d\n", pts, dts, s->last_pts, s->last_dts, buf_size, *poutbuf_size, avctx->codec_id);
    /* update the file pointer */
    if (*poutbuf_size) {
        /* fill the data for the current frame */
        s->frame_offset = s->next_frame_offset;

        /* offset of the next frame */
        s->next_frame_offset = s->cur_offset + index;
        s->fetch_timestamp=1;
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
            /*||(s->pict_type != AV_PICTURE_TYPE_I && (s->flags & PARSER_FLAG_DUMP_EXTRADATA_AT_NOKEY))*/
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
    if(s){
        if (s->parser->parser_close)
            s->parser->parser_close(s);
        av_free(s->priv_data);
        av_free(s);
    }
}

/*****************************************************/

/**
 * combines the (truncated) bitstream to a complete frame
 * @return -1 if no complete frame could be created, AVERROR(ENOMEM) if there was a memory allocation error
 */
int ff_combine_frame(ParseContext *pc, int next, const uint8_t **buf, int *buf_size)
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
        void* new_buffer = av_fast_realloc(pc->buffer, &pc->buffer_size, (*buf_size) + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        if(!new_buffer)
            return AVERROR(ENOMEM);
        pc->buffer = new_buffer;
        memcpy(&pc->buffer[pc->index], *buf, *buf_size);
        pc->index += *buf_size;
        return -1;
    }

    *buf_size=
    pc->overread_index= pc->index + next;

    /* append to buffer */
    if(pc->index){
        void* new_buffer = av_fast_realloc(pc->buffer, &pc->buffer_size, next + pc->index + FF_INPUT_BUFFER_PADDING_SIZE);

        if(!new_buffer)
            return AVERROR(ENOMEM);
        pc->buffer = new_buffer;
        memcpy(&pc->buffer[pc->index], *buf, next + FF_INPUT_BUFFER_PADDING_SIZE );
        pc->index = 0;
        *buf= pc->buffer;
    }

    /* store overread bytes */
    for(;next < 0; next++){
        pc->state = (pc->state<<8) | pc->buffer[pc->last_index + next];
        pc->state64 = (pc->state64<<8) | pc->buffer[pc->last_index + next];
        pc->overread++;
    }

    if(pc->overread){
        av_dlog(pc, "overread %d, state:%X next:%d index:%d o_index:%d\n",
                pc->overread, pc->state, next, pc->index, pc->overread_index);
        av_dlog(pc, "%X %X %X %X\n", (*buf)[0], (*buf)[1],(*buf)[2],(*buf)[3]);
    }

    return 0;
}

void ff_parse_close(AVCodecParserContext *s)
{
    ParseContext *pc = s->priv_data;

    av_freep(&pc->buffer);
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
