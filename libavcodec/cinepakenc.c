/*
 * Cinepak encoder (c) 2011 Tomas Härdin
 * http://titan.codemill.se/~tomhar/cinepakenc.patch

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

 */

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "avcodec.h"
#include "libavutil/lfg.h"
#include "elbg.h"

#define CVID_HEADER_SIZE 10
#define STRIP_HEADER_SIZE 12
#define CHUNK_HEADER_SIZE 4

#define MB_SIZE 4           //4x4 MBs
#define MB_AREA (MB_SIZE*MB_SIZE)

#define VECTOR_MAX 6        //six or four entries per vector depending on format
#define CODEBOOK_MAX 256
#define CODEBOOK_NUM 5      //five potential codebooks (1, 4, 16, 64, 256) for V1 and V4

#define MAX_STRIPS  1       //Note: having fewer choices regarding the number of strip speeds up encoding (obviously)
#define MIN_STRIPS  1       //Note: having more strips speeds up encoding the frame (this is less obvious)

typedef enum {
    MODE_V1_ONLY = 0,
    MODE_V1_V4,
    MODE_MC,

    MODE_COUNT,
} CinepakMode;

typedef enum {
    ENC_V1,
    ENC_V4,
    ENC_SKIP
} mb_encoding;

typedef struct {
    int v1_vector;                  //index into v1 codebook
    int v1_error;                   //error when using V1 encoding
    int v4_vector[CODEBOOK_NUM][4]; //indices into v4 codebooks
    int v4_error[CODEBOOK_NUM];     //error when using V4 encodings
    int skip_error;                 //error when block is skipped (aka copied from last frame)
    mb_encoding best_encoding;      //last result from calculate_mode_score()
} mb_info;

typedef struct {
    int v1_codebook[CODEBOOK_MAX*VECTOR_MAX];
    int *v4_codebook;
} strip_info;

typedef struct {
    AVCodecContext *avctx;
    unsigned char *pict_bufs[3], *strip_buf, *frame_buf;
    AVFrame last_frame;
    AVFrame best_frame;
    AVFrame scratch_frame;
    enum AVPixelFormat pix_fmt;
    int w, h;
    int curframe, keyint;
    AVLFG randctx;
    uint64_t lambda;
    int *codebook_input;
    int *codebook_closest;
    mb_info *mb;                                //MB RD state
#ifdef CINEPAKENC_DEBUG
    mb_info *best_mb;                           //TODO: remove. only used for printing stats
#endif
    int num_v1_mode, num_v4_mode, num_mc_mode;
    int num_v1_encs, num_v4_encs, num_skips;
} CinepakEncContext;

static av_cold int cinepak_encode_init(AVCodecContext *avctx)
{
    CinepakEncContext *s = avctx->priv_data;
    int x, mb_count, strip_buf_size, frame_buf_size;

    if (avctx->width & 3 || avctx->height & 3) {
        av_log(avctx, AV_LOG_ERROR, "width and height must be multiples of four (got %ix%i)\n",
                avctx->width, avctx->height);
        return AVERROR(EINVAL);
    }

    if (!(s->codebook_input = av_malloc(sizeof(int) * (avctx->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4) * (avctx->width * avctx->height) >> 2)))
        return AVERROR(ENOMEM);

    if (!(s->codebook_closest = av_malloc(sizeof(int) * (avctx->width * avctx->height) >> 2)))
        goto enomem;

    for(x = 0; x < 3; x++)
        if(!(s->pict_bufs[x] = av_malloc((avctx->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4) * (avctx->width * avctx->height) >> 2)))
            goto enomem;

    mb_count = avctx->width * avctx->height / MB_AREA;

    //the largest possible chunk is 0x31 with all MBs encoded in V4 mode, which is 34 bits per MB
    strip_buf_size = STRIP_HEADER_SIZE + 3 * CHUNK_HEADER_SIZE + 2 * VECTOR_MAX * CODEBOOK_MAX + 4 * (mb_count + (mb_count + 15) / 16);

    frame_buf_size = CVID_HEADER_SIZE + MAX_STRIPS * strip_buf_size;

    if (!(s->strip_buf = av_malloc(strip_buf_size)))
        goto enomem;

    if (!(s->frame_buf = av_malloc(frame_buf_size)))
        goto enomem;

    if (!(s->mb = av_malloc(mb_count*sizeof(mb_info))))
        goto enomem;

#ifdef CINEPAKENC_DEBUG
    if (!(s->best_mb = av_malloc(mb_count*sizeof(mb_info))))
        goto enomem;
#endif

    av_lfg_init(&s->randctx, 1);
    s->avctx = avctx;
    s->w = avctx->width;
    s->h = avctx->height;
    s->curframe = 0;
    s->keyint = avctx->keyint_min;
    s->pix_fmt = avctx->pix_fmt;

    //set up AVFrames
    s->last_frame.data[0]        = s->pict_bufs[0];
    s->last_frame.linesize[0]    = s->w;
    s->best_frame.data[0]        = s->pict_bufs[1];
    s->best_frame.linesize[0]    = s->w;
    s->scratch_frame.data[0]     = s->pict_bufs[2];
    s->scratch_frame.linesize[0] = s->w;

    if(s->pix_fmt == AV_PIX_FMT_YUV420P) {
        s->last_frame.data[1]        = s->last_frame.data[0] + s->w * s->h;
        s->last_frame.data[2]        = s->last_frame.data[1] + ((s->w * s->h) >> 2);
        s->last_frame.linesize[1]    = s->last_frame.linesize[2] = s->w >> 1;

        s->best_frame.data[1]        = s->best_frame.data[0] + s->w * s->h;
        s->best_frame.data[2]        = s->best_frame.data[1] + ((s->w * s->h) >> 2);
        s->best_frame.linesize[1]    = s->best_frame.linesize[2] = s->w >> 1;

        s->scratch_frame.data[1]     = s->scratch_frame.data[0] + s->w * s->h;
        s->scratch_frame.data[2]     = s->scratch_frame.data[1] + ((s->w * s->h) >> 2);
        s->scratch_frame.linesize[1] = s->scratch_frame.linesize[2] = s->w >> 1;
    }

    s->num_v1_mode = s->num_v4_mode = s->num_mc_mode = s->num_v1_encs = s->num_v4_encs = s->num_skips = 0;

    return 0;

enomem:
    av_free(s->codebook_input);
    av_free(s->codebook_closest);
    av_free(s->strip_buf);
    av_free(s->frame_buf);
    av_free(s->mb);
#ifdef CINEPAKENC_DEBUG
    av_free(s->best_mb);
#endif

    for(x = 0; x < 3; x++)
        av_free(s->pict_bufs[x]);

    return AVERROR(ENOMEM);
}

static int64_t calculate_mode_score(CinepakEncContext *s, CinepakMode mode, int h, int v1_size, int v4_size, int v4, strip_info *info)
{
    //score = FF_LAMBDA_SCALE * error + lambda * bits
    int x;
    int entry_size = s->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4;
    int mb_count = s->w * h / MB_AREA;
    mb_info *mb;
    int64_t score1, score2, score3;
    int64_t ret = s->lambda * ((v1_size ? CHUNK_HEADER_SIZE + v1_size * entry_size : 0) +
                   (v4_size ? CHUNK_HEADER_SIZE + v4_size * entry_size : 0) +
                   CHUNK_HEADER_SIZE) << 3;

    //av_log(s->avctx, AV_LOG_INFO, "sizes %3i %3i -> %9li score mb_count %i", v1_size, v4_size, ret, mb_count);

    switch(mode) {
    case MODE_V1_ONLY:
        //one byte per MB
        ret += s->lambda * 8 * mb_count;

        for(x = 0; x < mb_count; x++) {
            mb = &s->mb[x];
            ret += FF_LAMBDA_SCALE * mb->v1_error;
            mb->best_encoding = ENC_V1;
        }

        break;
    case MODE_V1_V4:
        //9 or 33 bits per MB
        for(x = 0; x < mb_count; x++) {
            mb = &s->mb[x];
            score1 = s->lambda * 9  + FF_LAMBDA_SCALE * mb->v1_error;
            score2 = s->lambda * 33 + FF_LAMBDA_SCALE * mb->v4_error[v4];

            if(score1 <= score2) {
                ret += score1;
                mb->best_encoding = ENC_V1;
            } else {
                ret += score2;
                mb->best_encoding = ENC_V4;
            }
        }

        break;
    case MODE_MC:
        //1, 10 or 34 bits per MB
        for(x = 0; x < mb_count; x++) {
            mb = &s->mb[x];
            score1 = s->lambda * 1  + FF_LAMBDA_SCALE * mb->skip_error;
            score2 = s->lambda * 10 + FF_LAMBDA_SCALE * mb->v1_error;
            score3 = s->lambda * 34 + FF_LAMBDA_SCALE * mb->v4_error[v4];


            if(score1 <= score2 && score1 <= score3) {
                ret += score1;
                mb->best_encoding = ENC_SKIP;
            } else if(score2 <= score1 && score2 <= score3) {
                ret += score2;
                mb->best_encoding = ENC_V1;
            } else {
                ret += score3;
                mb->best_encoding = ENC_V4;
            }
        }

        break;
    }

    return ret;
}

static int write_chunk_header(unsigned char *buf, int chunk_type, int chunk_size)
{
    buf[0] = chunk_type;
    AV_WB24(&buf[1], chunk_size + CHUNK_HEADER_SIZE);
    return CHUNK_HEADER_SIZE;
}

static int encode_codebook(CinepakEncContext *s, int *codebook, int size, int chunk_type_yuv, int chunk_type_gray, unsigned char *buf)
{
    int x, y, ret, entry_size = s->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4;

    ret = write_chunk_header(buf, s->pix_fmt == AV_PIX_FMT_YUV420P ? chunk_type_yuv : chunk_type_gray, entry_size * size);

    for(x = 0; x < size; x++)
        for(y = 0; y < entry_size; y++)
            buf[ret++] = codebook[y + x*entry_size] ^ (y >= 4 ? 0x80 : 0);

    return ret;
}

//sets out to the sub picture starting at (x,y) in in
static void get_sub_picture(CinepakEncContext *s, int x, int y, AVPicture *in, AVPicture *out)
{
    out->data[0] = in->data[0] + x + y * in->linesize[0];
    out->linesize[0] = in->linesize[0];

    if(s->pix_fmt == AV_PIX_FMT_YUV420P) {
        out->data[1] = in->data[1] + (x >> 1) + (y >> 1) * in->linesize[1];
        out->linesize[1] = in->linesize[1];

        out->data[2] = in->data[2] + (x >> 1) + (y >> 1) * in->linesize[2];
        out->linesize[2] = in->linesize[2];
    }
}

//decodes the V1 vector in mb into the 4x4 MB pointed to by sub_pict
static void decode_v1_vector(CinepakEncContext *s, AVPicture *sub_pict, mb_info *mb, strip_info *info)
{
    int entry_size = s->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4;

    sub_pict->data[0][0] =
            sub_pict->data[0][1] =
            sub_pict->data[0][    sub_pict->linesize[0]] =
            sub_pict->data[0][1+  sub_pict->linesize[0]] = info->v1_codebook[mb->v1_vector*entry_size];

    sub_pict->data[0][2] =
            sub_pict->data[0][3] =
            sub_pict->data[0][2+  sub_pict->linesize[0]] =
            sub_pict->data[0][3+  sub_pict->linesize[0]] = info->v1_codebook[mb->v1_vector*entry_size+1];

    sub_pict->data[0][2*sub_pict->linesize[0]] =
            sub_pict->data[0][1+2*sub_pict->linesize[0]] =
            sub_pict->data[0][  3*sub_pict->linesize[0]] =
            sub_pict->data[0][1+3*sub_pict->linesize[0]] = info->v1_codebook[mb->v1_vector*entry_size+2];

    sub_pict->data[0][2+2*sub_pict->linesize[0]] =
            sub_pict->data[0][3+2*sub_pict->linesize[0]] =
            sub_pict->data[0][2+3*sub_pict->linesize[0]] =
            sub_pict->data[0][3+3*sub_pict->linesize[0]] = info->v1_codebook[mb->v1_vector*entry_size+3];

    if(s->pix_fmt == AV_PIX_FMT_YUV420P) {
        sub_pict->data[1][0] =
            sub_pict->data[1][1] =
            sub_pict->data[1][    sub_pict->linesize[1]] =
            sub_pict->data[1][1+  sub_pict->linesize[1]] = info->v1_codebook[mb->v1_vector*entry_size+4];

        sub_pict->data[2][0] =
            sub_pict->data[2][1] =
            sub_pict->data[2][    sub_pict->linesize[2]] =
            sub_pict->data[2][1+  sub_pict->linesize[2]] = info->v1_codebook[mb->v1_vector*entry_size+5];
    }
}

//decodes the V4 vectors in mb into the 4x4 MB pointed to by sub_pict
static void decode_v4_vector(CinepakEncContext *s, AVPicture *sub_pict, int *v4_vector, strip_info *info)
{
    int i, x, y, entry_size = s->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4;

    for(i = y = 0; y < 4; y += 2) {
        for(x = 0; x < 4; x += 2, i++) {
            sub_pict->data[0][x   +     y*sub_pict->linesize[0]] = info->v4_codebook[v4_vector[i]*entry_size];
            sub_pict->data[0][x+1 +     y*sub_pict->linesize[0]] = info->v4_codebook[v4_vector[i]*entry_size+1];
            sub_pict->data[0][x   + (y+1)*sub_pict->linesize[0]] = info->v4_codebook[v4_vector[i]*entry_size+2];
            sub_pict->data[0][x+1 + (y+1)*sub_pict->linesize[0]] = info->v4_codebook[v4_vector[i]*entry_size+3];

            if(s->pix_fmt == AV_PIX_FMT_YUV420P) {
                sub_pict->data[1][(x>>1) + (y>>1)*sub_pict->linesize[1]] = info->v4_codebook[v4_vector[i]*entry_size+4];
                sub_pict->data[2][(x>>1) + (y>>1)*sub_pict->linesize[2]] = info->v4_codebook[v4_vector[i]*entry_size+5];
            }
        }
    }
}

static int encode_mode(CinepakEncContext *s, CinepakMode mode, int h, int v1_size, int v4_size, int v4, AVPicture *scratch_pict, strip_info *info, unsigned char *buf)
{
    int x, y, z, flags, bits, temp_size, header_ofs, ret = 0, mb_count = s->w * h / MB_AREA;
    int needs_extra_bit, should_write_temp;
    unsigned char temp[64]; //32/2 = 16 V4 blocks at 4 B each -> 64 B
    mb_info *mb;
    AVPicture sub_scratch;

    //encode codebooks
    if(v1_size)
        ret += encode_codebook(s, info->v1_codebook, v1_size, 0x22, 0x26, buf + ret);

    if(v4_size)
        ret += encode_codebook(s, info->v4_codebook, v4_size, 0x20, 0x24, buf + ret);

    //update scratch picture
    for(z = y = 0; y < h; y += MB_SIZE) {
        for(x = 0; x < s->w; x += MB_SIZE, z++) {
            mb = &s->mb[z];

            if(mode == MODE_MC && mb->best_encoding == ENC_SKIP)
                continue;

            get_sub_picture(s, x, y, scratch_pict, &sub_scratch);

            if(mode == MODE_V1_ONLY || mb->best_encoding == ENC_V1)
                decode_v1_vector(s, &sub_scratch, mb, info);
            else if(mode != MODE_V1_ONLY && mb->best_encoding == ENC_V4)
                decode_v4_vector(s, &sub_scratch, mb->v4_vector[v4], info);
        }
    }

    switch(mode) {
    case MODE_V1_ONLY:
        //av_log(s->avctx, AV_LOG_INFO, "mb_count = %i\n", mb_count);
        ret += write_chunk_header(buf + ret, 0x32, mb_count);

        for(x = 0; x < mb_count; x++)
            buf[ret++] = s->mb[x].v1_vector;

        break;
    case MODE_V1_V4:
        //remember header position
        header_ofs = ret;
        ret += CHUNK_HEADER_SIZE;

        for(x = 0; x < mb_count; x += 32) {
            flags = 0;
            for(y = x; y < FFMIN(x+32, mb_count); y++)
                if(s->mb[y].best_encoding == ENC_V4)
                    flags |= 1 << (31 - y + x);

            AV_WB32(&buf[ret], flags);
            ret += 4;

            for(y = x; y < FFMIN(x+32, mb_count); y++) {
                mb = &s->mb[y];

                if(mb->best_encoding == ENC_V1)
                    buf[ret++] = mb->v1_vector;
                else
                    for(z = 0; z < 4; z++)
                        buf[ret++] = mb->v4_vector[v4][z];
            }
        }

        write_chunk_header(buf + header_ofs, 0x30, ret - header_ofs - CHUNK_HEADER_SIZE);

        break;
    case MODE_MC:
        //remember header position
        header_ofs = ret;
        ret += CHUNK_HEADER_SIZE;
        flags = bits = temp_size = 0;

        for(x = 0; x < mb_count; x++) {
            mb = &s->mb[x];
            flags |= (mb->best_encoding != ENC_SKIP) << (31 - bits++);
            needs_extra_bit = 0;
            should_write_temp = 0;

            if(mb->best_encoding != ENC_SKIP) {
                if(bits < 32)
                    flags |= (mb->best_encoding == ENC_V4) << (31 - bits++);
                else
                    needs_extra_bit = 1;
            }

            if(bits == 32) {
                AV_WB32(&buf[ret], flags);
                ret += 4;
                flags = bits = 0;

                if(mb->best_encoding == ENC_SKIP || needs_extra_bit) {
                    memcpy(&buf[ret], temp, temp_size);
                    ret += temp_size;
                    temp_size = 0;
                } else
                    should_write_temp = 1;
            }

            if(needs_extra_bit) {
                flags = (mb->best_encoding == ENC_V4) << 31;
                bits = 1;
            }

            if(mb->best_encoding == ENC_V1)
                temp[temp_size++] = mb->v1_vector;
            else if(mb->best_encoding == ENC_V4)
                for(z = 0; z < 4; z++)
                    temp[temp_size++] = mb->v4_vector[v4][z];

            if(should_write_temp) {
                memcpy(&buf[ret], temp, temp_size);
                ret += temp_size;
                temp_size = 0;
            }
        }

        if(bits > 0) {
            AV_WB32(&buf[ret], flags);
            ret += 4;
            memcpy(&buf[ret], temp, temp_size);
            ret += temp_size;
        }

        write_chunk_header(buf + header_ofs, 0x31, ret - header_ofs - CHUNK_HEADER_SIZE);

        break;
    }

    return ret;
}

//computes distortion of 4x4 MB in b compared to a
static int compute_mb_distortion(CinepakEncContext *s, AVPicture *a, AVPicture *b)
{
    int x, y, p, d, ret = 0;

    for(y = 0; y < MB_SIZE; y++) {
        for(x = 0; x < MB_SIZE; x++) {
            d = a->data[0][x + y*a->linesize[0]] - b->data[0][x + y*b->linesize[0]];
            ret += d*d;
        }
    }

    if(s->pix_fmt == AV_PIX_FMT_YUV420P) {
        for(p = 1; p <= 2; p++) {
            for(y = 0; y < MB_SIZE/2; y++) {
                for(x = 0; x < MB_SIZE/2; x++) {
                    d = a->data[p][x + y*a->linesize[p]] - b->data[p][x + y*b->linesize[p]];
                    ret += d*d;
                }
            }
        }
    }

    return ret;
}

static int quantize(CinepakEncContext *s, int h, AVPicture *pict, int v1mode, int size, int v4, strip_info *info)
{
    int x, y, i, j, k, x2, y2, x3, y3, plane, shift;
    int entry_size = s->pix_fmt == AV_PIX_FMT_YUV420P ? 6 : 4;
    int *codebook = v1mode ? info->v1_codebook : info->v4_codebook;
    int64_t total_error = 0;
    uint8_t vq_pict_buf[(MB_AREA*3)/2];
    AVPicture sub_pict, vq_pict;

    for(i = y = 0; y < h; y += MB_SIZE) {
        for(x = 0; x < s->w; x += MB_SIZE, i += v1mode ? 1 : 4) {
            int *base = s->codebook_input + i*entry_size;

            if(v1mode) {
                //subsample
                for(j = y2 = 0; y2 < entry_size; y2 += 2) {
                    for(x2 = 0; x2 < 4; x2 += 2, j++) {
                        plane = y2 < 4 ? 0 : 1 + (x2 >> 1);
                        shift = y2 < 4 ? 0 : 1;
                        x3 = shift ? 0 : x2;
                        y3 = shift ? 0 : y2;
                        base[j] = (pict->data[plane][((x+x3) >> shift) +      ((y+y3) >> shift)      * pict->linesize[plane]] +
                                   pict->data[plane][((x+x3) >> shift) + 1 +  ((y+y3) >> shift)      * pict->linesize[plane]] +
                                   pict->data[plane][((x+x3) >> shift) +     (((y+y3) >> shift) + 1) * pict->linesize[plane]] +
                                   pict->data[plane][((x+x3) >> shift) + 1 + (((y+y3) >> shift) + 1) * pict->linesize[plane]]) >> 2;
                    }
                }
            } else {
                //copy
                for(j = y2 = 0; y2 < MB_SIZE; y2 += 2) {
                    for(x2 = 0; x2 < MB_SIZE; x2 += 2) {
                        for(k = 0; k < entry_size; k++, j++) {
                            plane = k >= 4 ? k - 3 : 0;

                            if(k >= 4) {
                                x3 = (x+x2) >> 1;
                                y3 = (y+y2) >> 1;
                            } else {
                                x3 = x + x2 + (k & 1);
                                y3 = y + y2 + (k >> 1);
                            }

                            base[j] = pict->data[plane][x3 + y3*pict->linesize[plane]];
                        }
                    }
                }
            }
        }
    }

    ff_init_elbg(s->codebook_input, entry_size, i, codebook, size, 1, s->codebook_closest, &s->randctx);
    ff_do_elbg(s->codebook_input, entry_size, i, codebook, size, 1, s->codebook_closest, &s->randctx);

    //setup vq_pict, which contains a single MB
    vq_pict.data[0] = vq_pict_buf;
    vq_pict.linesize[0] = MB_SIZE;
    vq_pict.data[1] = &vq_pict_buf[MB_AREA];
    vq_pict.data[2] = vq_pict.data[1] + (MB_AREA >> 2);
    vq_pict.linesize[1] = vq_pict.linesize[2] = MB_SIZE >> 1;

    //copy indices
    for(i = j = y = 0; y < h; y += MB_SIZE) {
        for(x = 0; x < s->w; x += MB_SIZE, j++, i += v1mode ? 1 : 4) {
            mb_info *mb = &s->mb[j];

            //point sub_pict to current MB
            get_sub_picture(s, x, y, pict, &sub_pict);

            if(v1mode) {
                mb->v1_vector = s->codebook_closest[i];

                //fill in vq_pict with V1 data
                decode_v1_vector(s, &vq_pict, mb, info);

                mb->v1_error = compute_mb_distortion(s, &sub_pict, &vq_pict);
                total_error += mb->v1_error;
            } else {
                for(k = 0; k < 4; k++)
                    mb->v4_vector[v4][k] = s->codebook_closest[i+k];

                //fill in vq_pict with V4 data
                decode_v4_vector(s, &vq_pict, mb->v4_vector[v4], info);

                mb->v4_error[v4] = compute_mb_distortion(s, &sub_pict, &vq_pict);
                total_error += mb->v4_error[v4];
            }
        }
    }

    //av_log(s->avctx, AV_LOG_INFO, "mode %i size %i i %i error %li\n", v1mode, size, i, total_error);

    return 0;
}

static void calculate_skip_errors(CinepakEncContext *s, int h, AVPicture *last_pict, AVPicture *pict, strip_info *info)
{
    int x, y, i;
    AVPicture sub_last, sub_pict;

    for(i = y = 0; y < h; y += MB_SIZE) {
        for(x = 0; x < s->w; x += MB_SIZE, i++) {
            get_sub_picture(s, x, y, last_pict, &sub_last);
            get_sub_picture(s, x, y, pict,      &sub_pict);

            s->mb[i].skip_error = compute_mb_distortion(s, &sub_last, &sub_pict);
        }
    }
}

static void write_strip_header(CinepakEncContext *s, int y, int h, int keyframe, unsigned char *buf, int strip_size)
{
    buf[0] = keyframe ? 0x11: 0x10;
    AV_WB24(&buf[1], strip_size + STRIP_HEADER_SIZE);
    AV_WB16(&buf[4], y);
    AV_WB16(&buf[6], 0);
    AV_WB16(&buf[8], h);
    AV_WB16(&buf[10], s->w);
}

static int rd_strip(CinepakEncContext *s, int y, int h, int keyframe, AVPicture *last_pict, AVPicture *pict, AVPicture *scratch_pict, unsigned char *buf, int64_t *best_score)
{
    int64_t score = 0;
    int best_size = 0, v1_size, v4_size, v4, mb_count = s->w * h / MB_AREA;
    strip_info info;
    CinepakMode best_mode;
    int v4_codebooks[CODEBOOK_NUM][CODEBOOK_MAX*VECTOR_MAX];

    if(!keyframe)
        calculate_skip_errors(s, h, last_pict, pict, &info);

    //precompute V4 codebooks
    for(v4_size = 1, v4 = 0; v4_size <= 256; v4_size <<= 2, v4++) {
        info.v4_codebook = v4_codebooks[v4];
        quantize(s, h, pict, 0, v4_size, v4, &info);
    }

    //try all powers of 4 for the size of the codebooks
    //constraint the v4 codebook to be no bigger than the v1 codebook
    for(v1_size = 1; v1_size <= 256; v1_size <<= 2) {
        //compute V1 codebook
        quantize(s, h, pict, 1, v1_size, -1, &info);

        for(v4_size = 0, v4 = -1; v4_size <= v1_size; v4_size = v4_size ? v4_size << 2 : v1_size >= 4 ? v1_size >> 2 : 1, v4++) {
            //try all modes
            for(CinepakMode mode = 0; mode < MODE_COUNT; mode++) {
                //don't allow MODE_MC in inter frames
                if(keyframe && mode == MODE_MC)
                    continue;

                //only allow V1-only mode if v4 codebook is empty
                if(!v4_size && mode != MODE_V1_ONLY)
                    continue;

                info.v4_codebook = v4 >= 0 ? v4_codebooks[v4] : NULL;
                score = calculate_mode_score(s, mode, h, v1_size, v4_size, v4, &info);

                //av_log(s->avctx, AV_LOG_INFO, "%3i %3i score = %li\n", v1_size, v4_size, score);

                if(best_size == 0 || score < *best_score) {
                    *best_score = score;
                    best_size = encode_mode(s, mode, h, v1_size, v4_size, v4, scratch_pict, &info, s->strip_buf + STRIP_HEADER_SIZE);
                    best_mode = mode;

                    av_log(s->avctx, AV_LOG_INFO, "mode %i, %3i, %3i: %18li %i B\n", mode, v1_size, v4_size, score, best_size);

#ifdef CINEPAKENC_DEBUG
                    //save MB encoding choices
                    memcpy(s->best_mb, s->mb, mb_count*sizeof(mb_info));
#endif

                    //memcpy(strip_temp + STRIP_HEADER_SIZE, strip_temp, best_size);
                    write_strip_header(s, y, h, keyframe, s->strip_buf, best_size);
                }
            }
        }
    }

#ifdef CINEPAKENC_DEBUG
    //gather stats. this will only work properly of MAX_STRIPS == 1
    if(best_mode == MODE_V1_ONLY) {
        s->num_v1_mode++;
        s->num_v1_encs += s->w*h/MB_AREA;
    } else {
        if(best_mode == MODE_V1_V4)
            s->num_v4_mode++;
        else
            s->num_mc_mode++;

        int x;
        for(x = 0; x < s->w*h/MB_AREA; x++)
            if(s->best_mb[x].best_encoding == ENC_V1)
                s->num_v1_encs++;
            else if(s->best_mb[x].best_encoding == ENC_V4)
                s->num_v4_encs++;
            else
                s->num_skips++;
    }
#endif

    best_size += STRIP_HEADER_SIZE;
    memcpy(buf, s->strip_buf, best_size);


    return best_size;
}

static int write_cvid_header(CinepakEncContext *s, unsigned char *buf, int num_strips, int data_size)
{
    buf[0] = 0;
    AV_WB24(&buf[1], data_size + CVID_HEADER_SIZE);
    AV_WB16(&buf[4], s->w);
    AV_WB16(&buf[6], s->h);
    AV_WB16(&buf[8], num_strips);

    return CVID_HEADER_SIZE;
}

static int rd_frame(CinepakEncContext *s, AVFrame *frame, unsigned char *buf, int buf_size)
{
    int num_strips, strip, h, i, y, size, temp_size, best_size;
    AVPicture last_pict, pict, scratch_pict;
    int64_t best_score = 0, score, score_temp;

    //TODO: support encoding zero strips (meaning skip the whole frame)
    for(num_strips = MIN_STRIPS; num_strips <= MAX_STRIPS && num_strips <= s->h / MB_SIZE; num_strips++) {
        score = 0;
        size = 0;
        h = s->h / num_strips;
        //make h into next multiple of 4
        h += 4 - (h & 3);

        for(strip = 0; strip < num_strips; strip++) {
            y = strip*h;

            get_sub_picture(s, 0, y, (AVPicture*)frame,            &pict);
            get_sub_picture(s, 0, y, (AVPicture*)&s->last_frame,    &last_pict);
            get_sub_picture(s, 0, y, (AVPicture*)&s->scratch_frame, &scratch_pict);

            if((temp_size = rd_strip(s, y, FFMIN(h, s->h - y), frame->key_frame, &last_pict, &pict, &scratch_pict, s->frame_buf + CVID_HEADER_SIZE, &score_temp)) < 0)
                return temp_size;

            score += score_temp;
            size += temp_size;
        }

        if(best_score == 0 || score < best_score) {
            best_score = score;
            best_size = size + write_cvid_header(s, s->frame_buf, num_strips, size);
            av_log(s->avctx, AV_LOG_INFO, "best number of strips so far: %2i, %12li, %i B\n", num_strips, score, best_size);

            FFSWAP(AVFrame, s->best_frame, s->scratch_frame);
        }
    }

    memcpy(buf, s->frame_buf, best_size);

    return best_size;
}

static int cinepak_encode_frame(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data)
{
    CinepakEncContext *s = avctx->priv_data;
    AVFrame *frame = data;
    int ret;

    s->lambda = frame->quality ? frame->quality - 1 : 2 * FF_LAMBDA_SCALE;

    frame->key_frame = s->curframe == 0;
    frame->pict_type = frame->key_frame ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;

    ret = rd_frame(s, frame, buf, buf_size);

    FFSWAP(AVFrame, s->last_frame, s->best_frame);

    if (++s->curframe >= s->keyint)
        s->curframe = 0;

    return ret;
}

static av_cold int cinepak_encode_end(AVCodecContext *avctx)
{
    CinepakEncContext *s = avctx->priv_data;
    int x;

    av_free(s->codebook_input);
    av_free(s->codebook_closest);
    av_free(s->strip_buf);
    av_free(s->frame_buf);
    av_free(s->mb);
#ifdef CINEPAKENC_DEBUG
    av_free(s->best_mb);
#endif

    for(x = 0; x < 3; x++)
        av_free(s->pict_bufs[x]);

    av_log(avctx, AV_LOG_INFO, "strip coding stats: %i V1 mode, %i V4 mode, %i MC mode (%i V1 encs, %i V4 encs, %i skips)\n",
        s->num_v1_mode, s->num_v4_mode, s->num_mc_mode, s->num_v1_encs, s->num_v4_encs, s->num_skips);

    return 0;
}

AVCodec ff_cinepak_encoder = {
    .name           = "cinepak",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CINEPAK,
    .priv_data_size = sizeof(CinepakEncContext),
    .init           = cinepak_encode_init,
    .encode2        = cinepak_encode_frame,
    .close          = cinepak_encode_end,
    .pix_fmts       = (const enum AVPixelFormat[]){AV_PIX_FMT_RGB24, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE},
    .long_name      = NULL_IF_CONFIG_SMALL("Cinepak"),
};
