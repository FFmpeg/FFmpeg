/*
 * Audio and Video frame extraction
 * Copyright (c) 2003 Fabrice Bellard.
 * Copyright (c) 2003 Michael Niedermayer.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avcodec.h"
#include "mpegvideo.h"
#include "mpegaudio.h"

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

    for(parser = av_first_parser; parser != NULL; parser = parser->next) {
        if (parser->codec_ids[0] == codec_id ||
            parser->codec_ids[1] == codec_id ||
            parser->codec_ids[2] == codec_id)
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
    return s;
}

int av_parser_parse(AVCodecParserContext *s, 
                    AVCodecContext *avctx,
                    uint8_t **poutbuf, int *poutbuf_size, 
                    const uint8_t *buf, int buf_size)
{
    int index;
    /* WARNING: the returned index can be negative */
    index = s->parser->parser_parse(s, avctx, poutbuf, poutbuf_size, buf, buf_size);
    /* update the file pointer */
    if (*poutbuf_size) {
        s->frame_offset = s->last_frame_offset;
        s->last_frame_offset = s->cur_offset + index;
    }
    if (index < 0)
        index = 0;
    s->cur_offset += index;
    return index;
}

void av_parser_close(AVCodecParserContext *s)
{
    if (s->parser->parser_close)
        s->parser->parser_close(s);
    av_free(s->priv_data);
    av_free(s);
}

/*****************************************************/

//#define END_NOT_FOUND (-100)

#define PICTURE_START_CODE	0x00000100
#define SEQ_START_CODE		0x000001b3
#define EXT_START_CODE		0x000001b5
#define SLICE_MIN_START_CODE	0x00000101
#define SLICE_MAX_START_CODE	0x000001af

typedef struct ParseContext1{
    uint8_t *buffer;
    int index;
    int last_index;
    int buffer_size;
    uint32_t state;             ///< contains the last few bytes in MSB order
    int frame_start_found;
    int overread;               ///< the number of bytes which where irreversibly read from the next frame
    int overread_index;         ///< the index into ParseContext1.buffer of the overreaded bytes

    /* MPEG2 specific */
    int frame_rate;
    int progressive_sequence;
    int width, height;

    /* XXX: suppress that, needed by MPEG4 */
    MpegEncContext *enc;
    int first_picture;
} ParseContext1;

/**
 * combines the (truncated) bitstream to a complete frame
 * @returns -1 if no complete frame could be created
 */
static int ff_combine_frame1(ParseContext1 *pc, int next, uint8_t **buf, int *buf_size)
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

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int mpeg1_find_frame_end(ParseContext1 *pc, const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state;
    
    state= pc->state;
    
    i=0;
    if(!pc->frame_start_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state >= SLICE_MIN_START_CODE && state <= SLICE_MAX_START_CODE){
                i++;
                pc->frame_start_found=1;
                break;
            }
        }
    }
    
    if(pc->frame_start_found){
        for(; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if((state&0xFFFFFF00) == 0x100){
                if(state < SLICE_MIN_START_CODE || state > SLICE_MAX_START_CODE){
                    pc->frame_start_found=0;
                    pc->state=-1; 
                    return i-3;
                }
            }
        }
    }        
    pc->state= state;
    return END_NOT_FOUND;
}

static int find_start_code(const uint8_t **pbuf_ptr, const uint8_t *buf_end)
{
    const uint8_t *buf_ptr;
    unsigned int state=0xFFFFFFFF, v;
    int val;

    buf_ptr = *pbuf_ptr;
    while (buf_ptr < buf_end) {
        v = *buf_ptr++;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;
 found:
    *pbuf_ptr = buf_ptr;
    return val;
}

/* XXX: merge with libavcodec ? */
#define MPEG1_FRAME_RATE_BASE 1001

static const int frame_rate_tab[16] = {
        0,        
    24000,
    24024,
    25025,
    30000,
    30030,
    50050,
    60000,
    60060,
  // Xing's 15fps: (9)
    15015,
  // libmpeg3's "Unofficial economy rates": (10-13)
     5005,
    10010,
    12012,
    15015,
  // random, just to avoid segfault !never encode these
    25025,
    25025,
};

static void mpegvideo_extract_headers(AVCodecParserContext *s, 
                                      AVCodecContext *avctx,
                                      const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s->priv_data;
    const uint8_t *buf_end;
    int32_t start_code;
    int frame_rate_index, ext_type, bytes_left;
    int frame_rate_ext_n, frame_rate_ext_d;
    int top_field_first, repeat_first_field, progressive_frame;
    int horiz_size_ext, vert_size_ext;

    s->repeat_pict = 0;
    buf_end = buf + buf_size;
    while (buf < buf_end) {
        start_code = find_start_code(&buf, buf_end);
        bytes_left = buf_end - buf;
        switch(start_code) {
        case PICTURE_START_CODE:
            if (bytes_left >= 2) {
                s->pict_type = (buf[1] >> 3) & 7;
            }
            break;
        case SEQ_START_CODE:
            if (bytes_left >= 4) {
                pc->width = avctx->width = (buf[0] << 4) | (buf[1] >> 4);
                pc->height = avctx->height = ((buf[1] & 0x0f) << 8) | buf[2];
                frame_rate_index = buf[3] & 0xf;
                pc->frame_rate = avctx->frame_rate = frame_rate_tab[frame_rate_index];
                avctx->frame_rate_base = MPEG1_FRAME_RATE_BASE;
                avctx->codec_id = CODEC_ID_MPEG1VIDEO;
                avctx->sub_id = 1;
            }
            break;
        case EXT_START_CODE:
            if (bytes_left >= 1) {
                ext_type = (buf[0] >> 4);
                switch(ext_type) {
                case 0x1: /* sequence extension */
                    if (bytes_left >= 6) {
                        horiz_size_ext = ((buf[1] & 1) << 1) | (buf[2] >> 7);
                        vert_size_ext = (buf[2] >> 5) & 3;
                        frame_rate_ext_n = (buf[5] >> 5) & 3;
                        frame_rate_ext_d = (buf[5] & 0x1f);
                        pc->progressive_sequence = buf[1] & (1 << 3);

                        avctx->width = pc->width | (horiz_size_ext << 12);
                        avctx->height = pc->height | (vert_size_ext << 12);
                        avctx->frame_rate = pc->frame_rate * (frame_rate_ext_n + 1);
                        avctx->frame_rate_base = MPEG1_FRAME_RATE_BASE * (frame_rate_ext_d + 1);
                        avctx->codec_id = CODEC_ID_MPEG2VIDEO;
                        avctx->sub_id = 2; /* forces MPEG2 */
                    }
                    break;
                case 0x8: /* picture coding extension */
                    if (bytes_left >= 5) {
                        top_field_first = buf[3] & (1 << 7);
                        repeat_first_field = buf[3] & (1 << 1);
                        progressive_frame = buf[4] & (1 << 7);
                    
                        /* check if we must repeat the frame */
                        if (repeat_first_field) {
                            if (pc->progressive_sequence) {
                                if (top_field_first)
                                    s->repeat_pict = 4;
                                else
                                    s->repeat_pict = 2;
                            } else if (progressive_frame) {
                                s->repeat_pict = 1;
                            }
                        }
                    }
                    break;
                }
            }
            break;
        case -1:
            goto the_end;
        default:
            /* we stop parsing when we encounter a slice. It ensures
               that this function takes a negligible amount of time */
            if (start_code >= SLICE_MIN_START_CODE && 
                start_code <= SLICE_MAX_START_CODE)
                goto the_end;
            break;
        }
    }
 the_end: ;
}

static int mpegvideo_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size, 
                           const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s->priv_data;
    int next;
    
    next= mpeg1_find_frame_end(pc, buf, buf_size);
    
    if (ff_combine_frame1(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    /* we have a full frame : we just parse the first few MPEG headers
       to have the full timing information. The time take by this
       function should be negligible for uncorrupted streams */
    mpegvideo_extract_headers(s, avctx, buf, buf_size);
#if 0
    printf("pict_type=%d frame_rate=%0.3f repeat_pict=%d\n", 
           s->pict_type, (double)avctx->frame_rate / avctx->frame_rate_base, s->repeat_pict);
#endif

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

static void mpegvideo_parse_close(AVCodecParserContext *s)
{
    ParseContext1 *pc = s->priv_data;

    av_free(pc->buffer);
    av_free(pc->enc);
}

/*************************/

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int mpeg4_find_frame_end(ParseContext1 *pc, 
                                const uint8_t *buf, int buf_size)
{
    int vop_found, i;
    uint32_t state;
    
    vop_found= pc->frame_start_found;
    state= pc->state;
    
    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == 0x1B6){
                i++;
                vop_found=1;
                break;
            }
        }
    }

    if(vop_found){    
      for(; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if((state&0xFFFFFF00) == 0x100){
            pc->frame_start_found=0;
            pc->state=-1; 
            return i-3;
        }
      }
    }
    pc->frame_start_found= vop_found;
    pc->state= state;
    return END_NOT_FOUND;
}

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
        avctx->width = s->width;
        avctx->height = s->height;
    }
    pc->first_picture = 0;
    return ret;
}

int mpeg4video_parse_init(AVCodecParserContext *s)
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
    ParseContext1 *pc = s->priv_data;
    int next;
    
    next= mpeg4_find_frame_end(pc, buf, buf_size);

    if (ff_combine_frame1(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    av_mpeg4_decode_header(s, avctx, buf, buf_size);

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

/*************************/

static int h263_find_frame_end(ParseContext1 *pc, const uint8_t *buf, int buf_size)
{
    int vop_found, i;
    uint32_t state;
    
    vop_found= pc->frame_start_found;
    state= pc->state;
    
    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state>>(32-22) == 0x20){
                i++;
                vop_found=1;
                break;
            }
        }
    }

    if(vop_found){    
      for(; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if(state>>(32-22) == 0x20){
            pc->frame_start_found=0;
            pc->state=-1; 
            return i-3;
        }
      }
    }
    pc->frame_start_found= vop_found;
    pc->state= state;
    
    return END_NOT_FOUND;
}

static int h263_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size, 
                           const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s->priv_data;
    int next;
    
    next= h263_find_frame_end(pc, buf, buf_size);

    if (ff_combine_frame1(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

/*************************/

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int h264_find_frame_end(ParseContext1 *pc, const uint8_t *buf, int buf_size)
{
    int i;
    uint32_t state;
//printf("first %02X%02X%02X%02X\n", buf[0], buf[1],buf[2],buf[3]);
//    mb_addr= pc->mb_addr - 1;
    state= pc->state;
    //FIXME this will fail with slices
    for(i=0; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if((state&0xFFFFFF1F) == 0x101 || (state&0xFFFFFF1F) == 0x102 || (state&0xFFFFFF1F) == 0x105){
            if(pc->frame_start_found){
                pc->state=-1; 
                pc->frame_start_found= 0;
                return i-3;
            }
            pc->frame_start_found= 1;
        }
    }
    
    pc->state= state;
    return END_NOT_FOUND;
}

static int h264_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      uint8_t **poutbuf, int *poutbuf_size, 
                      const uint8_t *buf, int buf_size)
{
    ParseContext1 *pc = s->priv_data;
    int next;
    
    next= h264_find_frame_end(pc, buf, buf_size);

    if (ff_combine_frame1(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

/*************************/

typedef struct MpegAudioParseContext {
    uint8_t inbuf[MPA_MAX_CODED_FRAME_SIZE];	/* input buffer */
    uint8_t *inbuf_ptr;
    int frame_size;
    int free_format_frame_size;
    int free_format_next_header;
} MpegAudioParseContext;

#define MPA_HEADER_SIZE 4

/* header + layer + bitrate + freq + lsf/mpeg25 */
#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (0xf << 12) | (3 << 10) | (3 << 19))

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
    int len, ret;
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
	    len = MPA_HEADER_SIZE - len;
	    if (len > buf_size)
		len = buf_size;
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

                ret = mpa_decode_header(avctx, header);
                if (ret < 0) {
		    /* no sync found : move by one byte (inefficient, but simple!) */
		    memmove(s->inbuf, s->inbuf + 1, s->inbuf_ptr - s->inbuf - 1);
		    s->inbuf_ptr--;
                    dprintf("skip %x\n", header);
                    /* reset free format frame size to give a chance
                       to get a new bitrate */
                    s->free_format_frame_size = 0;
		} else {
                    s->frame_size = ret;
#if 0
                    /* free format: prepare to compute frame size */
		    if (decode_header(s, header) == 1) {
			s->frame_size = -1;
                    }
#endif
		}
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
                        dprintf("free frame size=%d padding=%d\n", 
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
	    len = s->frame_size - len;
	    if (len > buf_size)
		len = buf_size;
	    memcpy(s->inbuf_ptr, buf_ptr, len);
	    buf_ptr += len;
	    s->inbuf_ptr += len;
	    buf_size -= len;
	}
        //    next_data:
        if (s->frame_size > 0 && 
            (s->inbuf_ptr - s->inbuf) >= s->frame_size) {
            *poutbuf = s->inbuf;
            *poutbuf_size = s->inbuf_ptr - s->inbuf;
	    s->inbuf_ptr = s->inbuf;
	    s->frame_size = 0;
	    break;
	}
    }
    return buf_ptr - buf;
}

#ifdef CONFIG_AC3
extern int a52_syncinfo (const uint8_t * buf, int * flags,
                         int * sample_rate, int * bit_rate);

typedef struct AC3ParseContext {
    uint8_t inbuf[4096]; /* input buffer */
    uint8_t *inbuf_ptr;
    int frame_size;
    int flags;
} AC3ParseContext;

#define AC3_HEADER_SIZE 7
#define A52_LFE 16

static int ac3_parse_init(AVCodecParserContext *s1)
{
    AC3ParseContext *s = s1->priv_data;
    s->inbuf_ptr = s->inbuf;
    return 0;
}

static int ac3_parse(AVCodecParserContext *s1,
                     AVCodecContext *avctx,
                     uint8_t **poutbuf, int *poutbuf_size, 
                     const uint8_t *buf, int buf_size)
{
    AC3ParseContext *s = s1->priv_data;
    const uint8_t *buf_ptr;
    int len, sample_rate, bit_rate;
    static const int ac3_channels[8] = {
	2, 1, 2, 3, 3, 4, 4, 5
    };

    *poutbuf = NULL;
    *poutbuf_size = 0;

    buf_ptr = buf;
    while (buf_size > 0) {
        len = s->inbuf_ptr - s->inbuf;
        if (s->frame_size == 0) {
            /* no header seen : find one. We need at least 7 bytes to parse it */
            len = AC3_HEADER_SIZE - len;
            if (len > buf_size)
                len = buf_size;
            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
            if ((s->inbuf_ptr - s->inbuf) == AC3_HEADER_SIZE) {
                len = a52_syncinfo(s->inbuf, &s->flags, &sample_rate, &bit_rate);
                if (len == 0) {
                    /* no sync found : move by one byte (inefficient, but simple!) */
                    memmove(s->inbuf, s->inbuf + 1, AC3_HEADER_SIZE - 1);
                    s->inbuf_ptr--;
                } else {
		    s->frame_size = len;
                    /* update codec info */
                    avctx->sample_rate = sample_rate;
                    avctx->channels = ac3_channels[s->flags & 7];
                    if (s->flags & A52_LFE)
			avctx->channels++;
		    avctx->bit_rate = bit_rate;
                    avctx->frame_size = 6 * 256;
                }
            }
        } else if (len < s->frame_size) {
            len = s->frame_size - len;
            if (len > buf_size)
                len = buf_size;

            memcpy(s->inbuf_ptr, buf_ptr, len);
            buf_ptr += len;
            s->inbuf_ptr += len;
            buf_size -= len;
        } else {
            *poutbuf = s->inbuf;
            *poutbuf_size = s->frame_size;
            s->inbuf_ptr = s->inbuf;
            s->frame_size = 0;
            break;
        }
    }
    return buf_ptr - buf;
}
#endif

AVCodecParser mpegvideo_parser = {
    { CODEC_ID_MPEG1VIDEO, CODEC_ID_MPEG2VIDEO },
    sizeof(ParseContext1),
    NULL,
    mpegvideo_parse,
    mpegvideo_parse_close,
};

AVCodecParser mpeg4video_parser = {
    { CODEC_ID_MPEG4 },
    sizeof(ParseContext1),
    mpeg4video_parse_init,
    mpeg4video_parse,
    mpegvideo_parse_close,
};

AVCodecParser h263_parser = {
    { CODEC_ID_H263 },
    sizeof(ParseContext1),
    NULL,
    h263_parse,
    mpegvideo_parse_close,
};

AVCodecParser h264_parser = {
    { CODEC_ID_H264 },
    sizeof(ParseContext1),
    NULL,
    h264_parse,
    mpegvideo_parse_close,
};

AVCodecParser mpegaudio_parser = {
    { CODEC_ID_MP2, CODEC_ID_MP3 },
    sizeof(MpegAudioParseContext),
    mpegaudio_parse_init,
    mpegaudio_parse,
    NULL,
};

#ifdef CONFIG_AC3
AVCodecParser ac3_parser = {
    { CODEC_ID_AC3 },
    sizeof(AC3ParseContext),
    ac3_parse_init,
    ac3_parse,
    NULL,
};
#endif
