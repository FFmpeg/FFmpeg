/*
 * PNM image format
 * Copyright (c) 2002, 2003 Fabrice Bellard.
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
#include "mpegvideo.h" //only for ParseContext

typedef struct PNMContext {
    uint8_t *bytestream;
    uint8_t *bytestream_start;
    uint8_t *bytestream_end;
    AVFrame picture;
} PNMContext;

static inline int pnm_space(int c)  
{
    return (c == ' ' || c == '\n' || c == '\r' || c == '\t');
}

static void pnm_get(PNMContext *sc, char *str, int buf_size) 
{
    char *s;
    int c;
    
    /* skip spaces and comments */
    for(;;) {
        c = *sc->bytestream++;
        if (c == '#')  {
            do  {
                c = *sc->bytestream++;
            } while (c != '\n' && sc->bytestream < sc->bytestream_end);
        } else if (!pnm_space(c)) {
            break;
        }
    }
    
    s = str;
    while (sc->bytestream < sc->bytestream_end && !pnm_space(c)) {
        if ((s - str)  < buf_size - 1)
            *s++ = c;
        c = *sc->bytestream++;
    }
    *s = '\0';
}

static int common_init(AVCodecContext *avctx){
    PNMContext *s = avctx->priv_data;

    avcodec_get_frame_defaults((AVFrame*)&s->picture);
    avctx->coded_frame= (AVFrame*)&s->picture;

    return 0;
}

static int pnm_decode_header(AVCodecContext *avctx, PNMContext * const s){
    char buf1[32], tuple_type[32];
    int h, w, depth, maxval;;

    pnm_get(s, buf1, sizeof(buf1));
    if (!strcmp(buf1, "P4")) {
        avctx->pix_fmt = PIX_FMT_MONOWHITE;
    } else if (!strcmp(buf1, "P5")) {
        if (avctx->codec_id == CODEC_ID_PGMYUV) 
            avctx->pix_fmt = PIX_FMT_YUV420P;
        else
            avctx->pix_fmt = PIX_FMT_GRAY8;
    } else if (!strcmp(buf1, "P6")) {
        avctx->pix_fmt = PIX_FMT_RGB24;
    } else if (!strcmp(buf1, "P7")) {
        w = -1;
        h = -1;
        maxval = -1;
        depth = -1;
        tuple_type[0] = '\0';
        for(;;) {
            pnm_get(s, buf1, sizeof(buf1));
            if (!strcmp(buf1, "WIDTH")) {
                pnm_get(s, buf1, sizeof(buf1));
                w = strtol(buf1, NULL, 10);
            } else if (!strcmp(buf1, "HEIGHT")) {
                pnm_get(s, buf1, sizeof(buf1));
                h = strtol(buf1, NULL, 10);
            } else if (!strcmp(buf1, "DEPTH")) {
                pnm_get(s, buf1, sizeof(buf1));
                depth = strtol(buf1, NULL, 10);
            } else if (!strcmp(buf1, "MAXVAL")) {
                pnm_get(s, buf1, sizeof(buf1));
                maxval = strtol(buf1, NULL, 10);
            } else if (!strcmp(buf1, "TUPLETYPE")) {
                pnm_get(s, tuple_type, sizeof(tuple_type));
            } else if (!strcmp(buf1, "ENDHDR")) {
                break;
            } else {
                return -1;
            }
        }
        /* check that all tags are present */
        if (w <= 0 || h <= 0 || maxval <= 0 || depth <= 0 || tuple_type[0] == '\0')
            return -1;
        avctx->width = w;
        avctx->height = h;
        if (depth == 1) {
            if (maxval == 1)
                avctx->pix_fmt = PIX_FMT_MONOWHITE;
            else 
                avctx->pix_fmt = PIX_FMT_GRAY8;
        } else if (depth == 3) {
            avctx->pix_fmt = PIX_FMT_RGB24;
        } else if (depth == 4) {
            avctx->pix_fmt = PIX_FMT_RGBA32;
        } else {
            return -1;
        }
        return 0;
    } else {
        return -1;
    }
    pnm_get(s, buf1, sizeof(buf1));
    avctx->width = atoi(buf1);
    if (avctx->width <= 0)
        return -1;
    pnm_get(s, buf1, sizeof(buf1));
    avctx->height = atoi(buf1);
    if (avctx->height <= 0)
        return -1;
    if (avctx->pix_fmt != PIX_FMT_MONOWHITE) {
        pnm_get(s, buf1, sizeof(buf1));
    }

    /* more check if YUV420 */
    if (avctx->pix_fmt == PIX_FMT_YUV420P) {
        if ((avctx->width & 1) != 0)
            return -1;
        h = (avctx->height * 2);
        if ((h % 3) != 0)
            return -1;
        h /= 3;
        avctx->height = h;
    }
    return 0;
}

static int pnm_decode_frame(AVCodecContext *avctx, 
                        void *data, int *data_size,
                        uint8_t *buf, int buf_size)
{
    PNMContext * const s = avctx->priv_data;
    AVFrame *picture = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    int i, n, linesize, h;
    unsigned char *ptr;

    /* special case for last picture */
    if (buf_size == 0) {
        return 0;
    }
    
    s->bytestream_start=
    s->bytestream= buf;
    s->bytestream_end= buf + buf_size;
    
    if(pnm_decode_header(avctx, s) < 0)
        return -1;
    
    if(p->data[0])
        avctx->release_buffer(avctx, p);

    p->reference= 0;
    if(avctx->get_buffer(avctx, p) < 0){
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    
    switch(avctx->pix_fmt) {
    default:
        return -1;
    case PIX_FMT_RGB24:
        n = avctx->width * 3;
        goto do_read;
    case PIX_FMT_GRAY8:
        n = avctx->width;
        goto do_read;
    case PIX_FMT_MONOWHITE:
    case PIX_FMT_MONOBLACK:
        n = (avctx->width + 7) >> 3;
    do_read:
        ptr = p->data[0];
        linesize = p->linesize[0];
        for(i = 0; i < avctx->height; i++) {
            memcpy(ptr, s->bytestream, n);
            s->bytestream += n;
            ptr += linesize;
        }
        break;
    case PIX_FMT_YUV420P:
        {
            unsigned char *ptr1, *ptr2;

            n = avctx->width;
            ptr = p->data[0];
            linesize = p->linesize[0];
            for(i = 0; i < avctx->height; i++) {
                memcpy(ptr, s->bytestream, n);
                s->bytestream += n;
                ptr += linesize;
            }
            ptr1 = p->data[1];
            ptr2 = p->data[2];
            n >>= 1;
            h = avctx->height >> 1;
            for(i = 0; i < h; i++) {
                memcpy(ptr1, s->bytestream, n);
                s->bytestream += n;
                memcpy(ptr2, s->bytestream, n);
                s->bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
            }
        }
        break;
    case PIX_FMT_RGBA32:
        ptr = p->data[0];
        linesize = p->linesize[0];
        for(i = 0; i < avctx->height; i++) {
            int j, r, g, b, a;

            for(j = 0;j < avctx->width; j++) {
                r = *s->bytestream++;
                g = *s->bytestream++;
                b = *s->bytestream++;
                a = *s->bytestream++;
                ((uint32_t *)ptr)[j] = (a << 24) | (r << 16) | (g << 8) | b;
            }
            ptr += linesize;
        }
        break;
    }
    *picture= *(AVFrame*)&s->picture;
    *data_size = sizeof(AVPicture);

    return s->bytestream - s->bytestream_start;
}

static int pnm_encode_frame(AVCodecContext *avctx, unsigned char *outbuf, int buf_size, void *data){
    PNMContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    int i, h, h1, c, n, linesize;
    uint8_t *ptr, *ptr1, *ptr2;

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    
    s->bytestream_start=
    s->bytestream= outbuf;
    s->bytestream_end= outbuf+buf_size;

    h = avctx->height;
    h1 = h;
    switch(avctx->pix_fmt) {
    case PIX_FMT_MONOWHITE:
        c = '4';
        n = (avctx->width + 7) >> 3;
        break;
    case PIX_FMT_GRAY8:
        c = '5';
        n = avctx->width;
        break;
    case PIX_FMT_RGB24:
        c = '6';
        n = avctx->width * 3;
        break;
    case PIX_FMT_YUV420P:
        c = '5';
        n = avctx->width;
        h1 = (h * 3) / 2;
        break;
    default:
        return -1;
    }
    snprintf(s->bytestream, s->bytestream_end - s->bytestream, 
             "P%c\n%d %d\n",
             c, avctx->width, h1);
    s->bytestream += strlen(s->bytestream);
    if (avctx->pix_fmt != PIX_FMT_MONOWHITE) {
        snprintf(s->bytestream, s->bytestream_end - s->bytestream, 
                 "%d\n", 255);
        s->bytestream += strlen(s->bytestream);
    }

    ptr = p->data[0];
    linesize = p->linesize[0];
    for(i=0;i<h;i++) {
        memcpy(s->bytestream, ptr, n);
        s->bytestream += n;
        ptr += linesize;
    }
    
    if (avctx->pix_fmt == PIX_FMT_YUV420P) {
        h >>= 1;
        n >>= 1;
        ptr1 = p->data[1];
        ptr2 = p->data[2];
        for(i=0;i<h;i++) {
            memcpy(s->bytestream, ptr1, n);
            s->bytestream += n;
            memcpy(s->bytestream, ptr2, n);
            s->bytestream += n;
                ptr1 += p->linesize[1];
                ptr2 += p->linesize[2];
        }
    }
    return s->bytestream - s->bytestream_start;
}

static int pam_encode_frame(AVCodecContext *avctx, unsigned char *outbuf, int buf_size, void *data){
    PNMContext *s = avctx->priv_data;
    AVFrame *pict = data;
    AVFrame * const p= (AVFrame*)&s->picture;
    int i, h, w, n, linesize, depth, maxval;
    const char *tuple_type;
    uint8_t *ptr;

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    
    s->bytestream_start=
    s->bytestream= outbuf;
    s->bytestream_end= outbuf+buf_size;

    h = avctx->height;
    w = avctx->width;
    switch(avctx->pix_fmt) {
    case PIX_FMT_MONOWHITE:
        n = (w + 7) >> 3;
        depth = 1;
        maxval = 1;
        tuple_type = "BLACKANDWHITE";
        break;
    case PIX_FMT_GRAY8:
        n = w;
        depth = 1;
        maxval = 255;
        tuple_type = "GRAYSCALE";
        break;
    case PIX_FMT_RGB24:
        n = w * 3;
        depth = 3;
        maxval = 255;
        tuple_type = "RGB";
        break;
    case PIX_FMT_RGBA32:
        n = w * 4;
        depth = 4;
        maxval = 255;
        tuple_type = "RGB_ALPHA";
        break;
    default:
        return -1;
    }
    snprintf(s->bytestream, s->bytestream_end - s->bytestream, 
             "P7\nWIDTH %d\nHEIGHT %d\nDEPTH %d\nMAXVAL %d\nTUPLETYPE %s\nENDHDR\n",
             w, h, depth, maxval, tuple_type);
    s->bytestream += strlen(s->bytestream);
    
    ptr = p->data[0];
    linesize = p->linesize[0];
    
    if (avctx->pix_fmt == PIX_FMT_RGBA32) {
        int j;
        unsigned int v;

        for(i=0;i<h;i++) {
            for(j=0;j<w;j++) {
                v = ((uint32_t *)ptr)[j];
                *s->bytestream++ = v >> 16;
                *s->bytestream++ = v >> 8;
                *s->bytestream++ = v;
                *s->bytestream++ = v >> 24;
            }
            ptr += linesize;
        }
    } else {
        for(i=0;i<h;i++) {
            memcpy(s->bytestream, ptr, n);
            s->bytestream += n;
            ptr += linesize;
        }
    }
    return s->bytestream - s->bytestream_start;
}

#if 0
static int pnm_probe(AVProbeData *pd)
{
    const char *p = pd->buf;
    if (pd->buf_size >= 8 &&
        p[0] == 'P' &&
        p[1] >= '4' && p[1] <= '6' &&
        pnm_space(p[2]) )
        return AVPROBE_SCORE_MAX - 1; /* to permit pgmyuv probe */
    else
        return 0;
}

static int pgmyuv_probe(AVProbeData *pd)
{
    if (match_ext(pd->filename, "pgmyuv"))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int pam_probe(AVProbeData *pd)
{
    const char *p = pd->buf;
    if (pd->buf_size >= 8 &&
        p[0] == 'P' &&
        p[1] == '7' &&
        p[2] == '\n')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}
#endif

static int pnm_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size, 
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    PNMContext pnmctx;
    int next;

    for(; pc->overread>0; pc->overread--){
        pc->buffer[pc->index++]= pc->buffer[pc->overread_index++];
    }
retry:
    if(pc->index){
        pnmctx.bytestream_start=
        pnmctx.bytestream= pc->buffer;
        pnmctx.bytestream_end= pc->buffer + pc->index;
    }else{
        pnmctx.bytestream_start=
        pnmctx.bytestream= buf;
        pnmctx.bytestream_end= buf + buf_size;
    }
    if(pnm_decode_header(avctx, &pnmctx) < 0){
        if(pnmctx.bytestream < pnmctx.bytestream_end){
            if(pc->index){
                pc->index=0;
            }else{
                buf++;
                buf_size--;
            }
            goto retry;
        }
#if 0
        if(pc->index && pc->index*2 + FF_INPUT_BUFFER_PADDING_SIZE < pc->buffer_size && buf_size > pc->index){
            memcpy(pc->buffer + pc->index, buf, pc->index);
            pc->index += pc->index;
            buf += pc->index;
            buf_size -= pc->index;
            goto retry;
        }
#endif
        next= END_NOT_FOUND;
    }else{
        next= pnmctx.bytestream - pnmctx.bytestream_start 
            + avpicture_get_size(avctx->pix_fmt, avctx->width, avctx->height);
        if(pnmctx.bytestream_start!=buf)
            next-= pc->index;
        if(next > buf_size)
            next= END_NOT_FOUND;
    }
    
    if(ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size)<0){
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

AVCodecParser pnm_parser = {
    { CODEC_ID_PGM, CODEC_ID_PGMYUV, CODEC_ID_PPM, CODEC_ID_PBM, CODEC_ID_PAM},
    sizeof(ParseContext),
    NULL,
    pnm_parse,
    ff_parse_close,
};

AVCodec pgm_encoder = {
    "pgm",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PGM,
    sizeof(PNMContext),
    common_init,
    pnm_encode_frame,
    NULL, //encode_end,
    pnm_decode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_GRAY8, -1}, 
};

AVCodec pgmyuv_encoder = {
    "pgmyuv",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PGMYUV,
    sizeof(PNMContext),
    common_init,
    pnm_encode_frame,
    NULL, //encode_end,
    pnm_decode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_YUV420P, -1}, 
};

AVCodec ppm_encoder = {
    "ppm",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PPM,
    sizeof(PNMContext),
    common_init,
    pnm_encode_frame,
    NULL, //encode_end,
    pnm_decode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_RGB24, -1}, 
};

AVCodec pbm_encoder = {
    "pbm",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PBM,
    sizeof(PNMContext),
    common_init,
    pnm_encode_frame,
    NULL, //encode_end,
    pnm_decode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_MONOWHITE, -1}, 
};

AVCodec pam_encoder = {
    "pam",
    CODEC_TYPE_VIDEO,
    CODEC_ID_PAM,
    sizeof(PNMContext),
    common_init,
    pam_encode_frame,
    NULL, //encode_end,
    pnm_decode_frame,
    .pix_fmts= (enum PixelFormat[]){PIX_FMT_RGB24, PIX_FMT_RGBA32, PIX_FMT_GRAY8, PIX_FMT_MONOWHITE, -1}, 
};
