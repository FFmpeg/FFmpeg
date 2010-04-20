/*
 * MJPEG decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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

/**
 * @file
 * MJPEG decoder.
 */

//#define DEBUG
#include <assert.h>

#include "avcodec.h"
#include "dsputil.h"
#include "mjpeg.h"
#include "mjpegdec.h"
#include "jpeglsdec.h"


static int build_vlc(VLC *vlc, const uint8_t *bits_table, const uint8_t *val_table,
                      int nb_codes, int use_static, int is_ac)
{
    uint8_t huff_size[256+16];
    uint16_t huff_code[256+16];

    assert(nb_codes <= 256);

    memset(huff_size, 0, sizeof(huff_size));
    ff_mjpeg_build_huffman_codes(huff_size, huff_code, bits_table, val_table);

    if(is_ac){
        memmove(huff_size+16, huff_size, sizeof(uint8_t)*nb_codes);
        memmove(huff_code+16, huff_code, sizeof(uint16_t)*nb_codes);
        memset(huff_size, 0, sizeof(uint8_t)*16);
        memset(huff_code, 0, sizeof(uint16_t)*16);
        nb_codes += 16;
    }

    return init_vlc(vlc, 9, nb_codes, huff_size, 1, 1, huff_code, 2, 2, use_static);
}

static void build_basic_mjpeg_vlc(MJpegDecodeContext * s) {
    build_vlc(&s->vlcs[0][0], ff_mjpeg_bits_dc_luminance,
              ff_mjpeg_val_dc, 12, 0, 0);
    build_vlc(&s->vlcs[0][1], ff_mjpeg_bits_dc_chrominance,
              ff_mjpeg_val_dc, 12, 0, 0);
    build_vlc(&s->vlcs[1][0], ff_mjpeg_bits_ac_luminance,
              ff_mjpeg_val_ac_luminance, 251, 0, 1);
    build_vlc(&s->vlcs[1][1], ff_mjpeg_bits_ac_chrominance,
              ff_mjpeg_val_ac_chrominance, 251, 0, 1);
}

av_cold int ff_mjpeg_decode_init(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;

    s->avctx = avctx;
    dsputil_init(&s->dsp, avctx);
    ff_init_scantable(s->dsp.idct_permutation, &s->scantable, ff_zigzag_direct);
    s->buffer_size = 0;
    s->buffer = NULL;
    s->start_code = -1;
    s->first_picture = 1;
    s->org_height = avctx->coded_height;
    avctx->chroma_sample_location = AVCHROMA_LOC_CENTER;

    build_basic_mjpeg_vlc(s);

    if (avctx->flags & CODEC_FLAG_EXTERN_HUFF)
    {
        av_log(avctx, AV_LOG_INFO, "mjpeg: using external huffman table\n");
        init_get_bits(&s->gb, avctx->extradata, avctx->extradata_size*8);
        if (ff_mjpeg_decode_dht(s)) {
            av_log(avctx, AV_LOG_ERROR, "mjpeg: error using external huffman table, switching back to internal\n");
            build_basic_mjpeg_vlc(s);
        }
    }
    if (avctx->extradata_size > 9 &&
        AV_RL32(avctx->extradata + 4) == MKTAG('f','i','e','l')) {
        if (avctx->extradata[9] == 6) { /* quicktime icefloe 019 */
            s->interlace_polarity = 1; /* bottom field first */
            av_log(avctx, AV_LOG_DEBUG, "mjpeg bottom field first\n");
        }
    }
    if (avctx->codec->id == CODEC_ID_AMV)
        s->flipped = 1;

    return 0;
}


/* quantize tables */
int ff_mjpeg_decode_dqt(MJpegDecodeContext *s)
{
    int len, index, i, j;

    len = get_bits(&s->gb, 16) - 2;

    while (len >= 65) {
        /* only 8 bit precision handled */
        if (get_bits(&s->gb, 4) != 0)
        {
            av_log(s->avctx, AV_LOG_ERROR, "dqt: 16bit precision\n");
            return -1;
        }
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        av_log(s->avctx, AV_LOG_DEBUG, "index=%d\n", index);
        /* read quant table */
        for(i=0;i<64;i++) {
            j = s->scantable.permutated[i];
            s->quant_matrixes[index][j] = get_bits(&s->gb, 8);
        }

        //XXX FIXME finetune, and perhaps add dc too
        s->qscale[index]= FFMAX(
            s->quant_matrixes[index][s->scantable.permutated[1]],
            s->quant_matrixes[index][s->scantable.permutated[8]]) >> 1;
        av_log(s->avctx, AV_LOG_DEBUG, "qscale[%d]: %d\n", index, s->qscale[index]);
        len -= 65;
    }

    return 0;
}

/* decode huffman tables and build VLC decoders */
int ff_mjpeg_decode_dht(MJpegDecodeContext *s)
{
    int len, index, i, class, n, v, code_max;
    uint8_t bits_table[17];
    uint8_t val_table[256];

    len = get_bits(&s->gb, 16) - 2;

    while (len > 0) {
        if (len < 17)
            return -1;
        class = get_bits(&s->gb, 4);
        if (class >= 2)
            return -1;
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        n = 0;
        for(i=1;i<=16;i++) {
            bits_table[i] = get_bits(&s->gb, 8);
            n += bits_table[i];
        }
        len -= 17;
        if (len < n || n > 256)
            return -1;

        code_max = 0;
        for(i=0;i<n;i++) {
            v = get_bits(&s->gb, 8);
            if (v > code_max)
                code_max = v;
            val_table[i] = v;
        }
        len -= n;

        /* build VLC and flush previous vlc if present */
        free_vlc(&s->vlcs[class][index]);
        av_log(s->avctx, AV_LOG_DEBUG, "class=%d index=%d nb_codes=%d\n",
               class, index, code_max + 1);
        if(build_vlc(&s->vlcs[class][index], bits_table, val_table, code_max + 1, 0, class > 0) < 0){
            return -1;
        }
    }
    return 0;
}

int ff_mjpeg_decode_sof(MJpegDecodeContext *s)
{
    int len, nb_components, i, width, height, pix_fmt_id;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    s->bits= get_bits(&s->gb, 8);

    if(s->pegasus_rct) s->bits=9;
    if(s->bits==9 && !s->pegasus_rct) s->rct=1;    //FIXME ugly

    if (s->bits != 8 && !s->lossless){
        av_log(s->avctx, AV_LOG_ERROR, "only 8 bits/component accepted\n");
        return -1;
    }

    height = get_bits(&s->gb, 16);
    width = get_bits(&s->gb, 16);

    //HACK for odd_height.mov
    if(s->interlaced && s->width == width && s->height == height + 1)
        height= s->height;

    av_log(s->avctx, AV_LOG_DEBUG, "sof0: picture: %dx%d\n", width, height);
    if(avcodec_check_dimensions(s->avctx, width, height))
        return -1;

    nb_components = get_bits(&s->gb, 8);
    if (nb_components <= 0 ||
        nb_components > MAX_COMPONENTS)
        return -1;
    if (s->ls && !(s->bits <= 8 || nb_components == 1)){
        av_log(s->avctx, AV_LOG_ERROR, "only <= 8 bits/component or 16-bit gray accepted for JPEG-LS\n");
        return -1;
    }
    s->nb_components = nb_components;
    s->h_max = 1;
    s->v_max = 1;
    for(i=0;i<nb_components;i++) {
        /* component id */
        s->component_id[i] = get_bits(&s->gb, 8) - 1;
        s->h_count[i] = get_bits(&s->gb, 4);
        s->v_count[i] = get_bits(&s->gb, 4);
        /* compute hmax and vmax (only used in interleaved case) */
        if (s->h_count[i] > s->h_max)
            s->h_max = s->h_count[i];
        if (s->v_count[i] > s->v_max)
            s->v_max = s->v_count[i];
        s->quant_index[i] = get_bits(&s->gb, 8);
        if (s->quant_index[i] >= 4)
            return -1;
        av_log(s->avctx, AV_LOG_DEBUG, "component %d %d:%d id: %d quant:%d\n", i, s->h_count[i],
               s->v_count[i], s->component_id[i], s->quant_index[i]);
    }

    if(s->ls && (s->h_max > 1 || s->v_max > 1)) {
        av_log(s->avctx, AV_LOG_ERROR, "Subsampling in JPEG-LS is not supported.\n");
        return -1;
    }

    if(s->v_max==1 && s->h_max==1 && s->lossless==1) s->rgb=1;

    /* if different size, realloc/alloc picture */
    /* XXX: also check h_count and v_count */
    if (width != s->width || height != s->height) {
        av_freep(&s->qscale_table);

        s->width = width;
        s->height = height;
        s->interlaced = 0;

        /* test interlaced mode */
        if (s->first_picture &&
            s->org_height != 0 &&
            s->height < ((s->org_height * 3) / 4)) {
            s->interlaced = 1;
            s->bottom_field = s->interlace_polarity;
            s->picture.interlaced_frame = 1;
            s->picture.top_field_first = !s->interlace_polarity;
            height *= 2;
        }

        avcodec_set_dimensions(s->avctx, width, height);

        s->qscale_table= av_mallocz((s->width+15)/16);

        s->first_picture = 0;
    }

    if(s->interlaced && (s->bottom_field == !s->interlace_polarity))
        return 0;

    /* XXX: not complete test ! */
    pix_fmt_id = (s->h_count[0] << 28) | (s->v_count[0] << 24) |
                 (s->h_count[1] << 20) | (s->v_count[1] << 16) |
                 (s->h_count[2] << 12) | (s->v_count[2] <<  8) |
                 (s->h_count[3] <<  4) |  s->v_count[3];
    av_log(s->avctx, AV_LOG_DEBUG, "pix fmt id %x\n", pix_fmt_id);
    //NOTE we do not allocate pictures large enough for the possible padding of h/v_count being 4
    if(!(pix_fmt_id & 0xD0D0D0D0))
        pix_fmt_id-= (pix_fmt_id & 0xF0F0F0F0)>>1;
    if(!(pix_fmt_id & 0x0D0D0D0D))
        pix_fmt_id-= (pix_fmt_id & 0x0F0F0F0F)>>1;

    switch(pix_fmt_id){
    case 0x11111100:
        if(s->rgb){
            s->avctx->pix_fmt = PIX_FMT_BGRA;
        }else
            s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV444P : PIX_FMT_YUVJ444P;
        assert(s->nb_components==3);
        break;
    case 0x11000000:
        s->avctx->pix_fmt = PIX_FMT_GRAY8;
        break;
    case 0x12111100:
        s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV440P : PIX_FMT_YUVJ440P;
        break;
    case 0x21111100:
        s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P;
        break;
    case 0x22111100:
        s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV420P : PIX_FMT_YUVJ420P;
        break;
    default:
        av_log(s->avctx, AV_LOG_ERROR, "Unhandled pixel format 0x%x\n", pix_fmt_id);
        return -1;
    }
    if(s->ls){
        if(s->nb_components > 1)
            s->avctx->pix_fmt = PIX_FMT_RGB24;
        else if(s->bits <= 8)
            s->avctx->pix_fmt = PIX_FMT_GRAY8;
        else
            s->avctx->pix_fmt = PIX_FMT_GRAY16;
    }

    if(s->picture.data[0])
        s->avctx->release_buffer(s->avctx, &s->picture);

    s->picture.reference= 0;
    if(s->avctx->get_buffer(s->avctx, &s->picture) < 0){
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->picture.pict_type= FF_I_TYPE;
    s->picture.key_frame= 1;
    s->got_picture = 1;

    for(i=0; i<3; i++){
        s->linesize[i]= s->picture.linesize[i] << s->interlaced;
    }

//    printf("%d %d %d %d %d %d\n", s->width, s->height, s->linesize[0], s->linesize[1], s->interlaced, s->avctx->height);

    if (len != (8+(3*nb_components)))
    {
        av_log(s->avctx, AV_LOG_DEBUG, "decode_sof0: error, len(%d) mismatch\n", len);
    }

    /* totally blank picture as progressive JPEG will only add details to it */
    if(s->progressive){
        int bw = (width  + s->h_max*8-1) / (s->h_max*8);
        int bh = (height + s->v_max*8-1) / (s->v_max*8);
        for(i=0; i<s->nb_components; i++) {
            int size = bw * bh * s->h_count[i] * s->v_count[i];
            av_freep(&s->blocks[i]);
            av_freep(&s->last_nnz[i]);
            s->blocks[i] = av_malloc(size * sizeof(**s->blocks));
            s->last_nnz[i] = av_mallocz(size * sizeof(**s->last_nnz));
            s->block_stride[i] = bw * s->h_count[i];
        }
        memset(s->coefs_finished, 0, sizeof(s->coefs_finished));
    }
    return 0;
}

static inline int mjpeg_decode_dc(MJpegDecodeContext *s, int dc_index)
{
    int code;
    code = get_vlc2(&s->gb, s->vlcs[0][dc_index].table, 9, 2);
    if (code < 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "mjpeg_decode_dc: bad vlc: %d:%d (%p)\n", 0, dc_index,
               &s->vlcs[0][dc_index]);
        return 0xffff;
    }

    if(code)
        return get_xbits(&s->gb, code);
    else
        return 0;
}

/* decode block and dequantize */
static int decode_block(MJpegDecodeContext *s, DCTELEM *block,
                        int component, int dc_index, int ac_index, int16_t *quant_matrix)
{
    int code, i, j, level, val;

    /* DC coef */
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xffff) {
        av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
        return -1;
    }
    val = val * quant_matrix[0] + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    /* AC coefs */
    i = 0;
    {OPEN_READER(re, &s->gb)
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2)

        /* EOB */
        if (code == 0x10)
            break;
        i += ((unsigned)code) >> 4;
        if(code != 0x100){
            code &= 0xf;
            if(code > MIN_CACHE_BITS - 16){
                UPDATE_CACHE(re, &s->gb)
            }
            {
                int cache=GET_CACHE(re,&s->gb);
                int sign=(~cache)>>31;
                level = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
            }

            LAST_SKIP_BITS(re, &s->gb, code)

            if (i >= 63) {
                if(i == 63){
                    j = s->scantable.permutated[63];
                    block[j] = level * quant_matrix[j];
                    break;
                }
                av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                return -1;
            }
            j = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j];
        }
    }
    CLOSE_READER(re, &s->gb)}

    return 0;
}

static int decode_dc_progressive(MJpegDecodeContext *s, DCTELEM *block, int component,
                                 int dc_index, int16_t *quant_matrix, int Al)
{
    int val;
    s->dsp.clear_block(block);
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xffff) {
        av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
        return -1;
    }
    val = (val * quant_matrix[0] << Al) + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    return 0;
}

/* decode block and dequantize - progressive JPEG version */
static int decode_block_progressive(MJpegDecodeContext *s, DCTELEM *block, uint8_t *last_nnz,
                                    int ac_index, int16_t *quant_matrix,
                                    int ss, int se, int Al, int *EOBRUN)
{
    int code, i, j, level, val, run;

    if(*EOBRUN){
        (*EOBRUN)--;
        return 0;
    }
    {OPEN_READER(re, &s->gb)
    for(i=ss;;i++) {
        UPDATE_CACHE(re, &s->gb);
        GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2)
        /* Progressive JPEG use AC coeffs from zero and this decoder sets offset 16 by default */
        code -= 16;
        if(code & 0xF) {
            i += ((unsigned) code) >> 4;
            code &= 0xf;
            if(code > MIN_CACHE_BITS - 16){
                UPDATE_CACHE(re, &s->gb)
            }
            {
                int cache=GET_CACHE(re,&s->gb);
                int sign=(~cache)>>31;
                level = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
            }

            LAST_SKIP_BITS(re, &s->gb, code)

            if (i >= se) {
                if(i == se){
                    j = s->scantable.permutated[se];
                    block[j] = level * quant_matrix[j] << Al;
                    break;
                }
                av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                return -1;
            }
            j = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j] << Al;
        }else{
            run = ((unsigned) code) >> 4;
            if(run == 0xF){// ZRL - skip 15 coefficients
                i += 15;
            }else{
                val = run;
                run = (1 << run);
                UPDATE_CACHE(re, &s->gb);
                run += (GET_CACHE(re, &s->gb) >> (32 - val)) & (run - 1);
                if(val)
                    LAST_SKIP_BITS(re, &s->gb, val);
                *EOBRUN = run - 1;
                break;
            }
        }
    }
    CLOSE_READER(re, &s->gb)}
    if(i > *last_nnz)
        *last_nnz = i;
    return 0;
}

#define REFINE_BIT(j) {\
    UPDATE_CACHE(re, &s->gb);\
    sign = block[j]>>15;\
    block[j] += SHOW_UBITS(re, &s->gb, 1) * ((quant_matrix[j]^sign)-sign) << Al;\
    LAST_SKIP_BITS(re, &s->gb, 1);\
}

#define ZERO_RUN \
for(;;i++) {\
    if(i > last) {\
        i += run;\
        if(i > se) {\
            av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);\
            return -1;\
        }\
        break;\
    }\
    j = s->scantable.permutated[i];\
    if(block[j])\
        REFINE_BIT(j)\
    else if(run-- == 0)\
        break;\
}

/* decode block and dequantize - progressive JPEG refinement pass */
static int decode_block_refinement(MJpegDecodeContext *s, DCTELEM *block, uint8_t *last_nnz,
                        int ac_index, int16_t *quant_matrix,
                        int ss, int se, int Al, int *EOBRUN)
{
    int code, i=ss, j, sign, val, run;
    int last = FFMIN(se, *last_nnz);

    OPEN_READER(re, &s->gb);
    if(*EOBRUN)
        (*EOBRUN)--;
    else {
        for(;;i++) {
            UPDATE_CACHE(re, &s->gb);
            GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2)
            /* Progressive JPEG use AC coeffs from zero and this decoder sets offset 16 by default */
            code -= 16;
            if(code & 0xF) {
                run = ((unsigned) code) >> 4;
                UPDATE_CACHE(re, &s->gb);
                val = SHOW_UBITS(re, &s->gb, 1);
                LAST_SKIP_BITS(re, &s->gb, 1);
                ZERO_RUN;
                j = s->scantable.permutated[i];
                val--;
                block[j] = ((quant_matrix[j]^val)-val) << Al;
                if(i == se) {
                    if(i > *last_nnz)
                        *last_nnz = i;
                    CLOSE_READER(re, &s->gb)
                    return 0;
                }
            }else{
                run = ((unsigned) code) >> 4;
                if(run == 0xF){
                    ZERO_RUN;
                }else{
                    val = run;
                    run = (1 << run);
                    if(val) {
                        UPDATE_CACHE(re, &s->gb);
                        run += SHOW_UBITS(re, &s->gb, val);
                        LAST_SKIP_BITS(re, &s->gb, val);
                    }
                    *EOBRUN = run - 1;
                    break;
                }
            }
        }

        if(i > *last_nnz)
            *last_nnz = i;
    }

    for(;i<=last;i++) {
        j = s->scantable.permutated[i];
        if(block[j])
            REFINE_BIT(j)
    }
    CLOSE_READER(re, &s->gb);

    return 0;
}
#undef REFINE_BIT
#undef ZERO_RUN

static int ljpeg_decode_rgb_scan(MJpegDecodeContext *s, int predictor, int point_transform){
    int i, mb_x, mb_y;
    uint16_t (*buffer)[4];
    int left[3], top[3], topleft[3];
    const int linesize= s->linesize[0];
    const int mask= (1<<s->bits)-1;

    av_fast_malloc(&s->ljpeg_buffer, &s->ljpeg_buffer_size, (unsigned)s->mb_width * 4 * sizeof(s->ljpeg_buffer[0][0]));
    buffer= s->ljpeg_buffer;

    for(i=0; i<3; i++){
        buffer[0][i]= 1 << (s->bits + point_transform - 1);
    }
    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        const int modified_predictor= mb_y ? predictor : 1;
        uint8_t *ptr = s->picture.data[0] + (linesize * mb_y);

        if (s->interlaced && s->bottom_field)
            ptr += linesize >> 1;

        for(i=0; i<3; i++){
            top[i]= left[i]= topleft[i]= buffer[0][i];
        }
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            for(i=0;i<3;i++) {
                int pred;

                topleft[i]= top[i];
                top[i]= buffer[mb_x][i];

                PREDICT(pred, topleft[i], top[i], left[i], modified_predictor);

                left[i]=
                buffer[mb_x][i]= mask & (pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform));
            }

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }

        if(s->rct){
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2] - 0x200)>>2);
                ptr[4*mb_x+0] = buffer[mb_x][1] + ptr[4*mb_x+1];
                ptr[4*mb_x+2] = buffer[mb_x][2] + ptr[4*mb_x+1];
            }
        }else if(s->pegasus_rct){
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2])>>2);
                ptr[4*mb_x+0] = buffer[mb_x][1] + ptr[4*mb_x+1];
                ptr[4*mb_x+2] = buffer[mb_x][2] + ptr[4*mb_x+1];
            }
        }else{
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+0] = buffer[mb_x][2];
                ptr[4*mb_x+1] = buffer[mb_x][1];
                ptr[4*mb_x+2] = buffer[mb_x][0];
            }
        }
    }
    return 0;
}

static int ljpeg_decode_yuv_scan(MJpegDecodeContext *s, int predictor, int point_transform){
    int i, mb_x, mb_y;
    const int nb_components=3;

    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            if(mb_x==0 || mb_y==0 || s->interlaced){
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];

                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->picture.data[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        if(y==0 && mb_y==0){
                            if(x==0 && mb_x==0){
                                pred= 128 << point_transform;
                            }else{
                                pred= ptr[-1];
                            }
                        }else{
                            if(x==0 && mb_x==0){
                                pred= ptr[-linesize];
                            }else{
                                PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                            }
                        }

                        if (s->interlaced && s->bottom_field)
                            ptr += linesize >> 1;
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);

                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }else{
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];

                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->picture.data[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);
                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }
            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
    }
    return 0;
}

static int mjpeg_decode_scan(MJpegDecodeContext *s, int nb_components, int Ah, int Al){
    int i, mb_x, mb_y;
    uint8_t* data[MAX_COMPONENTS];
    int linesize[MAX_COMPONENTS];

    if(s->flipped && s->avctx->flags & CODEC_FLAG_EMU_EDGE) {
        av_log(s->avctx, AV_LOG_ERROR, "Can not flip image with CODEC_FLAG_EMU_EDGE set!\n");
        s->flipped = 0;
    }
    for(i=0; i < nb_components; i++) {
        int c = s->comp_index[i];
        data[c] = s->picture.data[c];
        linesize[c]=s->linesize[c];
        s->coefs_finished[c] |= 1;
        if(s->flipped) {
            //picture should be flipped upside-down for this codec
            data[c] += (linesize[c] * (s->v_scount[i] * (8 * s->mb_height -((s->height/s->v_max)&7)) - 1 ));
            linesize[c] *= -1;
        }
    }

    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            for(i=0;i<nb_components;i++) {
                uint8_t *ptr;
                int n, h, v, x, y, c, j;
                n = s->nb_blocks[i];
                c = s->comp_index[i];
                h = s->h_scount[i];
                v = s->v_scount[i];
                x = 0;
                y = 0;
                for(j=0;j<n;j++) {
                    ptr = data[c] +
                        (((linesize[c] * (v * mb_y + y) * 8) +
                        (h * mb_x + x) * 8) >> s->avctx->lowres);
                    if(s->interlaced && s->bottom_field)
                        ptr += linesize[c] >> 1;
                    if(!s->progressive) {
                        s->dsp.clear_block(s->block);
                        if(decode_block(s, s->block, i,
                                     s->dc_index[i], s->ac_index[i],
                                     s->quant_matrixes[ s->quant_index[c] ]) < 0) {
                            av_log(s->avctx, AV_LOG_ERROR, "error y=%d x=%d\n", mb_y, mb_x);
                            return -1;
                        }
                        s->dsp.idct_put(ptr, linesize[c], s->block);
                    } else {
                        int block_idx = s->block_stride[c] * (v * mb_y + y) + (h * mb_x + x);
                        DCTELEM *block = s->blocks[c][block_idx];
                        if(Ah)
                            block[0] += get_bits1(&s->gb) * s->quant_matrixes[ s->quant_index[c] ][0] << Al;
                        else if(decode_dc_progressive(s, block, i, s->dc_index[i], s->quant_matrixes[ s->quant_index[c] ], Al) < 0) {
                            av_log(s->avctx, AV_LOG_ERROR, "error y=%d x=%d\n", mb_y, mb_x);
                            return -1;
                        }
                    }
//                    av_log(s->avctx, AV_LOG_DEBUG, "mb: %d %d processed\n", mb_y, mb_x);
//av_log(NULL, AV_LOG_DEBUG, "%d %d %d %d %d %d %d %d \n", mb_x, mb_y, x, y, c, s->bottom_field, (v * mb_y + y) * 8, (h * mb_x + x) * 8);
                    if (++x == h) {
                        x = 0;
                        y++;
                    }
                }
            }

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
                for (i=0; i<nb_components; i++) /* reset dc */
                    s->last_dc[i] = 1024;
            }
        }
    }
    return 0;
}

static int mjpeg_decode_scan_progressive_ac(MJpegDecodeContext *s, int ss, int se, int Ah, int Al){
    int mb_x, mb_y;
    int EOBRUN = 0;
    int c = s->comp_index[0];
    uint8_t* data = s->picture.data[c];
    int linesize = s->linesize[c];
    int last_scan = 0;
    int16_t *quant_matrix = s->quant_matrixes[ s->quant_index[c] ];

    if(!Al) {
        s->coefs_finished[c] |= (1LL<<(se+1))-(1LL<<ss);
        last_scan = !~s->coefs_finished[c];
    }

    if(s->interlaced && s->bottom_field)
        data += linesize >> 1;

    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        uint8_t *ptr = data + (mb_y*linesize*8 >> s->avctx->lowres);
        int block_idx = mb_y * s->block_stride[c];
        DCTELEM (*block)[64] = &s->blocks[c][block_idx];
        uint8_t *last_nnz = &s->last_nnz[c][block_idx];
        for(mb_x = 0; mb_x < s->mb_width; mb_x++, block++, last_nnz++) {
            int ret;
            if(Ah)
                ret = decode_block_refinement(s, *block, last_nnz, s->ac_index[0],
                                              quant_matrix, ss, se, Al, &EOBRUN);
            else
                ret = decode_block_progressive(s, *block, last_nnz, s->ac_index[0],
                                               quant_matrix, ss, se, Al, &EOBRUN);
            if(ret < 0) {
                av_log(s->avctx, AV_LOG_ERROR, "error y=%d x=%d\n", mb_y, mb_x);
                return -1;
            }
            if(last_scan) {
                s->dsp.idct_put(ptr, linesize, *block);
                ptr += 8 >> s->avctx->lowres;
            }
        }
    }
    return 0;
}

int ff_mjpeg_decode_sos(MJpegDecodeContext *s)
{
    int len, nb_components, i, h, v, predictor, point_transform;
    int index, id;
    const int block_size= s->lossless ? 1 : 8;
    int ilv, prev_shift;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    nb_components = get_bits(&s->gb, 8);
    if (nb_components == 0 || nb_components > MAX_COMPONENTS){
        av_log(s->avctx, AV_LOG_ERROR, "decode_sos: nb_components (%d) unsupported\n", nb_components);
        return -1;
    }
    if (len != 6+2*nb_components)
    {
        av_log(s->avctx, AV_LOG_ERROR, "decode_sos: invalid len (%d)\n", len);
        return -1;
    }
    for(i=0;i<nb_components;i++) {
        id = get_bits(&s->gb, 8) - 1;
        av_log(s->avctx, AV_LOG_DEBUG, "component: %d\n", id);
        /* find component index */
        for(index=0;index<s->nb_components;index++)
            if (id == s->component_id[index])
                break;
        if (index == s->nb_components)
        {
            av_log(s->avctx, AV_LOG_ERROR, "decode_sos: index(%d) out of components\n", index);
            return -1;
        }
        /* Metasoft MJPEG codec has Cb and Cr swapped */
        if (s->avctx->codec_tag == MKTAG('M', 'T', 'S', 'J')
            && nb_components == 3 && s->nb_components == 3 && i)
            index = 3 - i;

        s->comp_index[i] = index;

        s->nb_blocks[i] = s->h_count[index] * s->v_count[index];
        s->h_scount[i] = s->h_count[index];
        s->v_scount[i] = s->v_count[index];

        s->dc_index[i] = get_bits(&s->gb, 4);
        s->ac_index[i] = get_bits(&s->gb, 4);

        if (s->dc_index[i] <  0 || s->ac_index[i] < 0 ||
            s->dc_index[i] >= 4 || s->ac_index[i] >= 4)
            goto out_of_range;
        if (!s->vlcs[0][s->dc_index[i]].table || !s->vlcs[1][s->ac_index[i]].table)
            goto out_of_range;
    }

    predictor= get_bits(&s->gb, 8); /* JPEG Ss / lossless JPEG predictor /JPEG-LS NEAR */
    ilv= get_bits(&s->gb, 8);    /* JPEG Se / JPEG-LS ILV */
    prev_shift = get_bits(&s->gb, 4); /* Ah */
    point_transform= get_bits(&s->gb, 4); /* Al */

    for(i=0;i<nb_components;i++)
        s->last_dc[i] = 1024;

    if (nb_components > 1) {
        /* interleaved stream */
        s->mb_width  = (s->width  + s->h_max * block_size - 1) / (s->h_max * block_size);
        s->mb_height = (s->height + s->v_max * block_size - 1) / (s->v_max * block_size);
    } else if(!s->ls) { /* skip this for JPEG-LS */
        h = s->h_max / s->h_scount[0];
        v = s->v_max / s->v_scount[0];
        s->mb_width  = (s->width  + h * block_size - 1) / (h * block_size);
        s->mb_height = (s->height + v * block_size - 1) / (v * block_size);
        s->nb_blocks[0] = 1;
        s->h_scount[0] = 1;
        s->v_scount[0] = 1;
    }

    if(s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG, "%s %s p:%d >>:%d ilv:%d bits:%d %s\n", s->lossless ? "lossless" : "sequential DCT", s->rgb ? "RGB" : "",
               predictor, point_transform, ilv, s->bits,
               s->pegasus_rct ? "PRCT" : (s->rct ? "RCT" : ""));


    /* mjpeg-b can have padding bytes between sos and image data, skip them */
    for (i = s->mjpb_skiptosod; i > 0; i--)
        skip_bits(&s->gb, 8);

    if(s->lossless){
        if(CONFIG_JPEGLS_DECODER && s->ls){
//            for(){
//            reset_ls_coding_parameters(s, 0);

            if(ff_jpegls_decode_picture(s, predictor, point_transform, ilv) < 0)
                return -1;
        }else{
            if(s->rgb){
                if(ljpeg_decode_rgb_scan(s, predictor, point_transform) < 0)
                    return -1;
            }else{
                if(ljpeg_decode_yuv_scan(s, predictor, point_transform) < 0)
                    return -1;
            }
        }
    }else{
        if(s->progressive && predictor) {
            if(mjpeg_decode_scan_progressive_ac(s, predictor, ilv, prev_shift, point_transform) < 0)
                return -1;
        } else {
            if(mjpeg_decode_scan(s, nb_components, prev_shift, point_transform) < 0)
                return -1;
        }
    }
    emms_c();
    return 0;
 out_of_range:
    av_log(s->avctx, AV_LOG_ERROR, "decode_sos: ac/dc index out of range\n");
    return -1;
}

static int mjpeg_decode_dri(MJpegDecodeContext *s)
{
    if (get_bits(&s->gb, 16) != 4)
        return -1;
    s->restart_interval = get_bits(&s->gb, 16);
    s->restart_count = 0;
    av_log(s->avctx, AV_LOG_DEBUG, "restart interval: %d\n", s->restart_interval);

    return 0;
}

static int mjpeg_decode_app(MJpegDecodeContext *s)
{
    int len, id, i;

    len = get_bits(&s->gb, 16);
    if (len < 5)
        return -1;
    if(8*len + get_bits_count(&s->gb) > s->gb.size_in_bits)
        return -1;

    id = (get_bits(&s->gb, 16) << 16) | get_bits(&s->gb, 16);
    id = be2me_32(id);
    len -= 6;

    if(s->avctx->debug & FF_DEBUG_STARTCODE){
        av_log(s->avctx, AV_LOG_DEBUG, "APPx %8X\n", id);
    }

    /* buggy AVID, it puts EOI only at every 10th frame */
    /* also this fourcc is used by non-avid files too, it holds some
       informations, but it's always present in AVID creates files */
    if (id == AV_RL32("AVI1"))
    {
        /* structure:
            4bytes      AVI1
            1bytes      polarity
            1bytes      always zero
            4bytes      field_size
            4bytes      field_size_less_padding
        */
            s->buggy_avid = 1;
//        if (s->first_picture)
//            printf("mjpeg: workarounding buggy AVID\n");
        i = get_bits(&s->gb, 8);
        if     (i==2) s->bottom_field= 1;
        else if(i==1) s->bottom_field= 0;
#if 0
        skip_bits(&s->gb, 8);
        skip_bits(&s->gb, 32);
        skip_bits(&s->gb, 32);
        len -= 10;
#endif
//        if (s->interlace_polarity)
//            printf("mjpeg: interlace polarity: %d\n", s->interlace_polarity);
        goto out;
    }

//    len -= 2;

    if (id == AV_RL32("JFIF"))
    {
        int t_w, t_h, v1, v2;
        skip_bits(&s->gb, 8); /* the trailing zero-byte */
        v1= get_bits(&s->gb, 8);
        v2= get_bits(&s->gb, 8);
        skip_bits(&s->gb, 8);

        s->avctx->sample_aspect_ratio.num= get_bits(&s->gb, 16);
        s->avctx->sample_aspect_ratio.den= get_bits(&s->gb, 16);

        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "mjpeg: JFIF header found (version: %x.%x) SAR=%d/%d\n",
                v1, v2,
                s->avctx->sample_aspect_ratio.num,
                s->avctx->sample_aspect_ratio.den
            );

        t_w = get_bits(&s->gb, 8);
        t_h = get_bits(&s->gb, 8);
        if (t_w && t_h)
        {
            /* skip thumbnail */
            if (len-10-(t_w*t_h*3) > 0)
                len -= t_w*t_h*3;
        }
        len -= 10;
        goto out;
    }

    if (id == AV_RL32("Adob") && (get_bits(&s->gb, 8) == 'e'))
    {
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "mjpeg: Adobe header found\n");
        skip_bits(&s->gb, 16); /* version */
        skip_bits(&s->gb, 16); /* flags0 */
        skip_bits(&s->gb, 16); /* flags1 */
        skip_bits(&s->gb, 8);  /* transform */
        len -= 7;
        goto out;
    }

    if (id == AV_RL32("LJIF")){
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "Pegasus lossless jpeg header found\n");
        skip_bits(&s->gb, 16); /* version ? */
        skip_bits(&s->gb, 16); /* unknwon always 0? */
        skip_bits(&s->gb, 16); /* unknwon always 0? */
        skip_bits(&s->gb, 16); /* unknwon always 0? */
        switch( get_bits(&s->gb, 8)){
        case 1:
            s->rgb= 1;
            s->pegasus_rct=0;
            break;
        case 2:
            s->rgb= 1;
            s->pegasus_rct=1;
            break;
        default:
            av_log(s->avctx, AV_LOG_ERROR, "unknown colorspace\n");
        }
        len -= 9;
        goto out;
    }

    /* Apple MJPEG-A */
    if ((s->start_code == APP1) && (len > (0x28 - 8)))
    {
        id = (get_bits(&s->gb, 16) << 16) | get_bits(&s->gb, 16);
        id = be2me_32(id);
        len -= 4;
        if (id == AV_RL32("mjpg")) /* Apple MJPEG-A */
        {
#if 0
            skip_bits(&s->gb, 32); /* field size */
            skip_bits(&s->gb, 32); /* pad field size */
            skip_bits(&s->gb, 32); /* next off */
            skip_bits(&s->gb, 32); /* quant off */
            skip_bits(&s->gb, 32); /* huff off */
            skip_bits(&s->gb, 32); /* image off */
            skip_bits(&s->gb, 32); /* scan off */
            skip_bits(&s->gb, 32); /* data off */
#endif
            if (s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "mjpeg: Apple MJPEG-A header found\n");
        }
    }

out:
    /* slow but needed for extreme adobe jpegs */
    if (len < 0)
        av_log(s->avctx, AV_LOG_ERROR, "mjpeg: error, decode_app parser read over the end\n");
    while(--len > 0)
        skip_bits(&s->gb, 8);

    return 0;
}

static int mjpeg_decode_com(MJpegDecodeContext *s)
{
    int len = get_bits(&s->gb, 16);
    if (len >= 2 && 8*len - 16 + get_bits_count(&s->gb) <= s->gb.size_in_bits) {
        char *cbuf = av_malloc(len - 1);
        if (cbuf) {
            int i;
            for (i = 0; i < len - 2; i++)
                cbuf[i] = get_bits(&s->gb, 8);
            if (i > 0 && cbuf[i-1] == '\n')
                cbuf[i-1] = 0;
            else
                cbuf[i] = 0;

            if(s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "mjpeg comment: '%s'\n", cbuf);

            /* buggy avid, it puts EOI only at every 10th frame */
            if (!strcmp(cbuf, "AVID"))
            {
                s->buggy_avid = 1;
                //        if (s->first_picture)
                //            printf("mjpeg: workarounding buggy AVID\n");
            }
            else if(!strcmp(cbuf, "CS=ITU601")){
                s->cs_itu601= 1;
            }
            else if((len > 20 && !strncmp(cbuf, "Intel(R) JPEG Library", 21)) ||
                    (len > 19 && !strncmp(cbuf, "Metasoft MJPEG Codec", 20))){
                s->flipped = 1;
            }

            av_free(cbuf);
        }
    }

    return 0;
}

#if 0
static int valid_marker_list[] =
{
        /* 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f */
/* 0 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 1 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 2 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 3 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 4 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 5 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 6 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 7 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 8 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* a */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* b */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* c */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* d */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* e */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* f */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
}
#endif

/* return the 8 bit start code value and update the search
   state. Return -1 if no start code found */
static int find_marker(const uint8_t **pbuf_ptr, const uint8_t *buf_end)
{
    const uint8_t *buf_ptr;
    unsigned int v, v2;
    int val;
#ifdef DEBUG
    int skipped=0;
#endif

    buf_ptr = *pbuf_ptr;
    while (buf_ptr < buf_end) {
        v = *buf_ptr++;
        v2 = *buf_ptr;
        if ((v == 0xff) && (v2 >= 0xc0) && (v2 <= 0xfe) && buf_ptr < buf_end) {
            val = *buf_ptr++;
            goto found;
        }
#ifdef DEBUG
        skipped++;
#endif
    }
    val = -1;
found:
    dprintf(NULL, "find_marker skipped %d bytes\n", skipped);
    *pbuf_ptr = buf_ptr;
    return val;
}

int ff_mjpeg_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    MJpegDecodeContext *s = avctx->priv_data;
    const uint8_t *buf_end, *buf_ptr;
    int start_code;
    AVFrame *picture = data;

    s->got_picture = 0; // picture from previous image can not be reused
    buf_ptr = buf;
    buf_end = buf + buf_size;
    while (buf_ptr < buf_end) {
        /* find start next marker */
        start_code = find_marker(&buf_ptr, buf_end);
        {
            /* EOF */
            if (start_code < 0) {
                goto the_end;
            } else {
                av_log(avctx, AV_LOG_DEBUG, "marker=%x avail_size_in_buf=%td\n", start_code, buf_end - buf_ptr);

                if ((buf_end - buf_ptr) > s->buffer_size)
                {
                    av_free(s->buffer);
                    s->buffer_size = buf_end-buf_ptr;
                    s->buffer = av_malloc(s->buffer_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    av_log(avctx, AV_LOG_DEBUG, "buffer too small, expanding to %d bytes\n",
                        s->buffer_size);
                }

                /* unescape buffer of SOS, use special treatment for JPEG-LS */
                if (start_code == SOS && !s->ls)
                {
                    const uint8_t *src = buf_ptr;
                    uint8_t *dst = s->buffer;

                    while (src<buf_end)
                    {
                        uint8_t x = *(src++);

                        *(dst++) = x;
                        if (avctx->codec_id != CODEC_ID_THP)
                        {
                            if (x == 0xff) {
                                while (src < buf_end && x == 0xff)
                                    x = *(src++);

                                if (x >= 0xd0 && x <= 0xd7)
                                    *(dst++) = x;
                                else if (x)
                                    break;
                            }
                        }
                    }
                    init_get_bits(&s->gb, s->buffer, (dst - s->buffer)*8);

                    av_log(avctx, AV_LOG_DEBUG, "escaping removed %td bytes\n",
                           (buf_end - buf_ptr) - (dst - s->buffer));
                }
                else if(start_code == SOS && s->ls){
                    const uint8_t *src = buf_ptr;
                    uint8_t *dst = s->buffer;
                    int bit_count = 0;
                    int t = 0, b = 0;
                    PutBitContext pb;

                    s->cur_scan++;

                    /* find marker */
                    while (src + t < buf_end){
                        uint8_t x = src[t++];
                        if (x == 0xff){
                            while((src + t < buf_end) && x == 0xff)
                                x = src[t++];
                            if (x & 0x80) {
                                t -= 2;
                                break;
                            }
                        }
                    }
                    bit_count = t * 8;

                    init_put_bits(&pb, dst, t);

                    /* unescape bitstream */
                    while(b < t){
                        uint8_t x = src[b++];
                        put_bits(&pb, 8, x);
                        if(x == 0xFF){
                            x = src[b++];
                            put_bits(&pb, 7, x);
                            bit_count--;
                        }
                    }
                    flush_put_bits(&pb);

                    init_get_bits(&s->gb, dst, bit_count);
                }
                else
                    init_get_bits(&s->gb, buf_ptr, (buf_end - buf_ptr)*8);

                s->start_code = start_code;
                if(s->avctx->debug & FF_DEBUG_STARTCODE){
                    av_log(avctx, AV_LOG_DEBUG, "startcode: %X\n", start_code);
                }

                /* process markers */
                if (start_code >= 0xd0 && start_code <= 0xd7) {
                    av_log(avctx, AV_LOG_DEBUG, "restart marker: %d\n", start_code&0x0f);
                    /* APP fields */
                } else if (start_code >= APP0 && start_code <= APP15) {
                    mjpeg_decode_app(s);
                    /* Comment */
                } else if (start_code == COM){
                    mjpeg_decode_com(s);
                }

                switch(start_code) {
                case SOI:
                    s->restart_interval = 0;

                    s->restart_count = 0;
                    /* nothing to do on SOI */
                    break;
                case DQT:
                    ff_mjpeg_decode_dqt(s);
                    break;
                case DHT:
                    if(ff_mjpeg_decode_dht(s) < 0){
                        av_log(avctx, AV_LOG_ERROR, "huffman table decode error\n");
                        return -1;
                    }
                    break;
                case SOF0:
                case SOF1:
                    s->lossless=0;
                    s->ls=0;
                    s->progressive=0;
                    if (ff_mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF2:
                    s->lossless=0;
                    s->ls=0;
                    s->progressive=1;
                    if (ff_mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF3:
                    s->lossless=1;
                    s->ls=0;
                    s->progressive=0;
                    if (ff_mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF48:
                    s->lossless=1;
                    s->ls=1;
                    s->progressive=0;
                    if (ff_mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case LSE:
                    if (!CONFIG_JPEGLS_DECODER || ff_jpegls_decode_lse(s) < 0)
                        return -1;
                    break;
                case EOI:
                    s->cur_scan = 0;
                    if ((s->buggy_avid && !s->interlaced) || s->restart_interval)
                        break;
eoi_parser:
                    if (!s->got_picture) {
                        av_log(avctx, AV_LOG_WARNING, "Found EOI before any SOF, ignoring\n");
                        break;
                    }
                    {
                        if (s->interlaced) {
                            s->bottom_field ^= 1;
                            /* if not bottom field, do not output image yet */
                            if (s->bottom_field == !s->interlace_polarity)
                                goto not_the_end;
                        }
                        *picture = s->picture;
                        *data_size = sizeof(AVFrame);

                        if(!s->lossless){
                            picture->quality= FFMAX3(s->qscale[0], s->qscale[1], s->qscale[2]);
                            picture->qstride= 0;
                            picture->qscale_table= s->qscale_table;
                            memset(picture->qscale_table, picture->quality, (s->width+15)/16);
                            if(avctx->debug & FF_DEBUG_QP)
                                av_log(avctx, AV_LOG_DEBUG, "QP: %d\n", picture->quality);
                            picture->quality*= FF_QP2LAMBDA;
                        }

                        goto the_end;
                    }
                    break;
                case SOS:
                    if (!s->got_picture) {
                        av_log(avctx, AV_LOG_WARNING, "Can not process SOS before SOF, skipping\n");
                        break;
                    }
                    ff_mjpeg_decode_sos(s);
                    /* buggy avid puts EOI every 10-20th frame */
                    /* if restart period is over process EOI */
                    if ((s->buggy_avid && !s->interlaced) || s->restart_interval)
                        goto eoi_parser;
                    break;
                case DRI:
                    mjpeg_decode_dri(s);
                    break;
                case SOF5:
                case SOF6:
                case SOF7:
                case SOF9:
                case SOF10:
                case SOF11:
                case SOF13:
                case SOF14:
                case SOF15:
                case JPG:
                    av_log(avctx, AV_LOG_ERROR, "mjpeg: unsupported coding type (%x)\n", start_code);
                    break;
//                default:
//                    printf("mjpeg: unsupported marker (%x)\n", start_code);
//                    break;
                }

not_the_end:
                /* eof process start code */
                buf_ptr += (get_bits_count(&s->gb)+7)/8;
                av_log(avctx, AV_LOG_DEBUG, "marker parser used %d bytes (%d bits)\n",
                       (get_bits_count(&s->gb)+7)/8, get_bits_count(&s->gb));
            }
        }
    }
    if (s->got_picture) {
        av_log(avctx, AV_LOG_WARNING, "EOI missing, emulating\n");
        goto eoi_parser;
    }
    av_log(avctx, AV_LOG_FATAL, "No JPEG data found in image\n");
    return -1;
the_end:
    av_log(avctx, AV_LOG_DEBUG, "mjpeg decode frame unused %td bytes\n", buf_end - buf_ptr);
//    return buf_end - buf_ptr;
    return buf_ptr - buf;
}

av_cold int ff_mjpeg_decode_end(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    int i, j;

    if (s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);

    av_free(s->buffer);
    av_free(s->qscale_table);
    av_freep(&s->ljpeg_buffer);
    s->ljpeg_buffer_size=0;

    for(i=0;i<2;i++) {
        for(j=0;j<4;j++)
            free_vlc(&s->vlcs[i][j]);
    }
    for(i=0; i<MAX_COMPONENTS; i++) {
        av_freep(&s->blocks[i]);
        av_freep(&s->last_nnz[i]);
    }
    return 0;
}

AVCodec mjpeg_decoder = {
    "mjpeg",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_MJPEG,
    sizeof(MJpegDecodeContext),
    ff_mjpeg_decode_init,
    NULL,
    ff_mjpeg_decode_end,
    ff_mjpeg_decode_frame,
    CODEC_CAP_DR1,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("MJPEG (Motion JPEG)"),
};

AVCodec thp_decoder = {
    "thp",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_THP,
    sizeof(MJpegDecodeContext),
    ff_mjpeg_decode_init,
    NULL,
    ff_mjpeg_decode_end,
    ff_mjpeg_decode_frame,
    CODEC_CAP_DR1,
    NULL,
    .long_name = NULL_IF_CONFIG_SMALL("Nintendo Gamecube THP video"),
};
