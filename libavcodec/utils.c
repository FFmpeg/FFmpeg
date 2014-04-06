/*
 * utils for libavcodec
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
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
 * utils.
 */

#include "config.h"
#include "libavutil/atomic.h"
#include "libavutil/attributes.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"
#include "libavutil/channel_layout.h"
#include "libavutil/crc.h"
#include "libavutil/frame.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/dict.h"
#include "avcodec.h"
#include "dsputil.h"
#include "libavutil/opt.h"
#include "thread.h"
#include "frame_thread_encoder.h"
#include "internal.h"
#include "raw.h"
#include "bytestream.h"
#include "version.h"
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#if CONFIG_ICONV
# include <iconv.h>
#endif

#if HAVE_PTHREADS
#include <pthread.h>
#elif HAVE_W32THREADS
#include "compat/w32pthreads.h"
#elif HAVE_OS2THREADS
#include "compat/os2threads.h"
#endif

#if HAVE_PTHREADS || HAVE_W32THREADS || HAVE_OS2THREADS
static int default_lockmgr_cb(void **arg, enum AVLockOp op)
{
    void * volatile * mutex = arg;
    int err;

    switch (op) {
    case AV_LOCK_CREATE:
        return 0;
    case AV_LOCK_OBTAIN:
        if (!*mutex) {
            pthread_mutex_t *tmp = av_malloc(sizeof(pthread_mutex_t));
            if (!tmp)
                return AVERROR(ENOMEM);
            if ((err = pthread_mutex_init(tmp, NULL))) {
                av_free(tmp);
                return AVERROR(err);
            }
            if (avpriv_atomic_ptr_cas(mutex, NULL, tmp)) {
                pthread_mutex_destroy(tmp);
                av_free(tmp);
            }
        }

        if ((err = pthread_mutex_lock(*mutex)))
            return AVERROR(err);

        return 0;
    case AV_LOCK_RELEASE:
        if ((err = pthread_mutex_unlock(*mutex)))
            return AVERROR(err);

        return 0;
    case AV_LOCK_DESTROY:
        if (*mutex)
            pthread_mutex_destroy(*mutex);
        av_free(*mutex);
        avpriv_atomic_ptr_cas(mutex, *mutex, NULL);
        return 0;
    }
    return 1;
}
static int (*lockmgr_cb)(void **mutex, enum AVLockOp op) = default_lockmgr_cb;
#else
static int (*lockmgr_cb)(void **mutex, enum AVLockOp op) = NULL;
#endif


volatile int ff_avcodec_locked;
static int volatile entangled_thread_counter = 0;
static void *codec_mutex;
static void *avformat_mutex;

#if CONFIG_RAISE_MAJOR
#    define LIBNAME "LIBAVCODEC_155"
#else
#    define LIBNAME "LIBAVCODEC_55"
#endif

#if FF_API_FAST_MALLOC && CONFIG_SHARED && HAVE_SYMVER
FF_SYMVER(void*, av_fast_realloc, (void *ptr, unsigned int *size, size_t min_size), LIBNAME)
{
    return av_fast_realloc(ptr, size, min_size);
}

FF_SYMVER(void, av_fast_malloc, (void *ptr, unsigned int *size, size_t min_size), LIBNAME)
{
    av_fast_malloc(ptr, size, min_size);
}
#endif

static inline int ff_fast_malloc(void *ptr, unsigned int *size, size_t min_size, int zero_realloc)
{
    void **p = ptr;
    if (min_size < *size)
        return 0;
    min_size = FFMAX(17 * min_size / 16 + 32, min_size);
    av_free(*p);
    *p = zero_realloc ? av_mallocz(min_size) : av_malloc(min_size);
    if (!*p)
        min_size = 0;
    *size = min_size;
    return 1;
}

void av_fast_padded_malloc(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    if (!ff_fast_malloc(p, size, min_size + FF_INPUT_BUFFER_PADDING_SIZE, 1))
        memset(*p + min_size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
}

void av_fast_padded_mallocz(void *ptr, unsigned int *size, size_t min_size)
{
    uint8_t **p = ptr;
    if (min_size > SIZE_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_freep(p);
        *size = 0;
        return;
    }
    if (!ff_fast_malloc(p, size, min_size + FF_INPUT_BUFFER_PADDING_SIZE, 1))
        memset(*p, 0, min_size + FF_INPUT_BUFFER_PADDING_SIZE);
}

/* encoder management */
static AVCodec *first_avcodec = NULL;
static AVCodec **last_avcodec = &first_avcodec;

AVCodec *av_codec_next(const AVCodec *c)
{
    if (c)
        return c->next;
    else
        return first_avcodec;
}

static av_cold void avcodec_init(void)
{
    static int initialized = 0;

    if (initialized != 0)
        return;
    initialized = 1;

    if (CONFIG_DSPUTIL)
        ff_dsputil_static_init();
}

int av_codec_is_encoder(const AVCodec *codec)
{
    return codec && (codec->encode_sub || codec->encode2);
}

int av_codec_is_decoder(const AVCodec *codec)
{
    return codec && codec->decode;
}

av_cold void avcodec_register(AVCodec *codec)
{
    AVCodec **p;
    avcodec_init();
    p = last_avcodec;
    codec->next = NULL;

    while(*p || avpriv_atomic_ptr_cas((void * volatile *)p, NULL, codec))
        p = &(*p)->next;
    last_avcodec = &codec->next;

    if (codec->init_static_data)
        codec->init_static_data(codec);
}

#if FF_API_EMU_EDGE
unsigned avcodec_get_edge_width(void)
{
    return EDGE_WIDTH;
}
#endif

#if FF_API_SET_DIMENSIONS
void avcodec_set_dimensions(AVCodecContext *s, int width, int height)
{
    int ret = ff_set_dimensions(s, width, height);
    if (ret < 0) {
        av_log(s, AV_LOG_WARNING, "Failed to set dimensions %d %d\n", width, height);
    }
}
#endif

int ff_set_dimensions(AVCodecContext *s, int width, int height)
{
    int ret = av_image_check_size(width, height, 0, s);

    if (ret < 0)
        width = height = 0;

    s->coded_width  = width;
    s->coded_height = height;
    s->width        = FF_CEIL_RSHIFT(width,  s->lowres);
    s->height       = FF_CEIL_RSHIFT(height, s->lowres);

    return ret;
}

int ff_side_data_update_matrix_encoding(AVFrame *frame,
                                        enum AVMatrixEncoding matrix_encoding)
{
    AVFrameSideData *side_data;
    enum AVMatrixEncoding *data;

    side_data = av_frame_get_side_data(frame, AV_FRAME_DATA_MATRIXENCODING);
    if (!side_data)
        side_data = av_frame_new_side_data(frame, AV_FRAME_DATA_MATRIXENCODING,
                                           sizeof(enum AVMatrixEncoding));

    if (!side_data)
        return AVERROR(ENOMEM);

    data  = (enum AVMatrixEncoding*)side_data->data;
    *data = matrix_encoding;

    return 0;
}

void avcodec_align_dimensions2(AVCodecContext *s, int *width, int *height,
                               int linesize_align[AV_NUM_DATA_POINTERS])
{
    int i;
    int w_align = 1;
    int h_align = 1;

    switch (s->pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_GBRAP:
    case AV_PIX_FMT_GBRP:
    case AV_PIX_FMT_GRAY8:
    case AV_PIX_FMT_GRAY16BE:
    case AV_PIX_FMT_GRAY16LE:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P:
    case AV_PIX_FMT_YUV420P9LE:
    case AV_PIX_FMT_YUV420P9BE:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV420P10BE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV420P12BE:
    case AV_PIX_FMT_YUV420P14LE:
    case AV_PIX_FMT_YUV420P14BE:
    case AV_PIX_FMT_YUV420P16LE:
    case AV_PIX_FMT_YUV420P16BE:
    case AV_PIX_FMT_YUVA420P9LE:
    case AV_PIX_FMT_YUVA420P9BE:
    case AV_PIX_FMT_YUVA420P10LE:
    case AV_PIX_FMT_YUVA420P10BE:
    case AV_PIX_FMT_YUVA420P16LE:
    case AV_PIX_FMT_YUVA420P16BE:
    case AV_PIX_FMT_YUV422P9LE:
    case AV_PIX_FMT_YUV422P9BE:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV422P10BE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV422P12BE:
    case AV_PIX_FMT_YUV422P14LE:
    case AV_PIX_FMT_YUV422P14BE:
    case AV_PIX_FMT_YUV422P16LE:
    case AV_PIX_FMT_YUV422P16BE:
    case AV_PIX_FMT_YUVA422P9LE:
    case AV_PIX_FMT_YUVA422P9BE:
    case AV_PIX_FMT_YUVA422P10LE:
    case AV_PIX_FMT_YUVA422P10BE:
    case AV_PIX_FMT_YUVA422P16LE:
    case AV_PIX_FMT_YUVA422P16BE:
    case AV_PIX_FMT_YUV444P9LE:
    case AV_PIX_FMT_YUV444P9BE:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV444P10BE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV444P12BE:
    case AV_PIX_FMT_YUV444P14LE:
    case AV_PIX_FMT_YUV444P14BE:
    case AV_PIX_FMT_YUV444P16LE:
    case AV_PIX_FMT_YUV444P16BE:
    case AV_PIX_FMT_YUVA444P9LE:
    case AV_PIX_FMT_YUVA444P9BE:
    case AV_PIX_FMT_YUVA444P10LE:
    case AV_PIX_FMT_YUVA444P10BE:
    case AV_PIX_FMT_YUVA444P16LE:
    case AV_PIX_FMT_YUVA444P16BE:
    case AV_PIX_FMT_GBRP9LE:
    case AV_PIX_FMT_GBRP9BE:
    case AV_PIX_FMT_GBRP10LE:
    case AV_PIX_FMT_GBRP10BE:
    case AV_PIX_FMT_GBRP12LE:
    case AV_PIX_FMT_GBRP12BE:
    case AV_PIX_FMT_GBRP14LE:
    case AV_PIX_FMT_GBRP14BE:
        w_align = 16; //FIXME assume 16 pixel per macroblock
        h_align = 16 * 2; // interlaced needs 2 macroblocks height
        break;
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUVJ411P:
    case AV_PIX_FMT_UYYVYY411:
        w_align = 32;
        h_align = 8;
        break;
    case AV_PIX_FMT_YUV410P:
        if (s->codec_id == AV_CODEC_ID_SVQ1) {
            w_align = 64;
            h_align = 64;
        }
        break;
    case AV_PIX_FMT_RGB555:
        if (s->codec_id == AV_CODEC_ID_RPZA) {
            w_align = 4;
            h_align = 4;
        }
        break;
    case AV_PIX_FMT_PAL8:
    case AV_PIX_FMT_BGR8:
    case AV_PIX_FMT_RGB8:
        if (s->codec_id == AV_CODEC_ID_SMC ||
            s->codec_id == AV_CODEC_ID_CINEPAK) {
            w_align = 4;
            h_align = 4;
        }
        break;
    case AV_PIX_FMT_BGR24:
        if ((s->codec_id == AV_CODEC_ID_MSZH) ||
            (s->codec_id == AV_CODEC_ID_ZLIB)) {
            w_align = 4;
            h_align = 4;
        }
        break;
    case AV_PIX_FMT_RGB24:
        if (s->codec_id == AV_CODEC_ID_CINEPAK) {
            w_align = 4;
            h_align = 4;
        }
        break;
    default:
        w_align = 1;
        h_align = 1;
        break;
    }

    if (s->codec_id == AV_CODEC_ID_IFF_ILBM || s->codec_id == AV_CODEC_ID_IFF_BYTERUN1) {
        w_align = FFMAX(w_align, 8);
    }

    *width  = FFALIGN(*width, w_align);
    *height = FFALIGN(*height, h_align);
    if (s->codec_id == AV_CODEC_ID_H264 || s->lowres)
        // some of the optimized chroma MC reads one line too much
        // which is also done in mpeg decoders with lowres > 0
        *height += 2;

    for (i = 0; i < 4; i++)
        linesize_align[i] = STRIDE_ALIGN;
}

void avcodec_align_dimensions(AVCodecContext *s, int *width, int *height)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(s->pix_fmt);
    int chroma_shift = desc->log2_chroma_w;
    int linesize_align[AV_NUM_DATA_POINTERS];
    int align;

    avcodec_align_dimensions2(s, width, height, linesize_align);
    align               = FFMAX(linesize_align[0], linesize_align[3]);
    linesize_align[1] <<= chroma_shift;
    linesize_align[2] <<= chroma_shift;
    align               = FFMAX3(align, linesize_align[1], linesize_align[2]);
    *width              = FFALIGN(*width, align);
}

int avcodec_enum_to_chroma_pos(int *xpos, int *ypos, enum AVChromaLocation pos)
{
    if (pos <= AVCHROMA_LOC_UNSPECIFIED || pos >= AVCHROMA_LOC_NB)
        return AVERROR(EINVAL);
    pos--;

    *xpos = (pos&1) * 128;
    *ypos = ((pos>>1)^(pos<4)) * 128;

    return 0;
}

enum AVChromaLocation avcodec_chroma_pos_to_enum(int xpos, int ypos)
{
    int pos, xout, yout;

    for (pos = AVCHROMA_LOC_UNSPECIFIED + 1; pos < AVCHROMA_LOC_NB; pos++) {
        if (avcodec_enum_to_chroma_pos(&xout, &yout, pos) == 0 && xout == xpos && yout == ypos)
            return pos;
    }
    return AVCHROMA_LOC_UNSPECIFIED;
}

int avcodec_fill_audio_frame(AVFrame *frame, int nb_channels,
                             enum AVSampleFormat sample_fmt, const uint8_t *buf,
                             int buf_size, int align)
{
    int ch, planar, needed_size, ret = 0;

    needed_size = av_samples_get_buffer_size(NULL, nb_channels,
                                             frame->nb_samples, sample_fmt,
                                             align);
    if (buf_size < needed_size)
        return AVERROR(EINVAL);

    planar = av_sample_fmt_is_planar(sample_fmt);
    if (planar && nb_channels > AV_NUM_DATA_POINTERS) {
        if (!(frame->extended_data = av_mallocz(nb_channels *
                                                sizeof(*frame->extended_data))))
            return AVERROR(ENOMEM);
    } else {
        frame->extended_data = frame->data;
    }

    if ((ret = av_samples_fill_arrays(frame->extended_data, &frame->linesize[0],
                                      (uint8_t *)(intptr_t)buf, nb_channels, frame->nb_samples,
                                      sample_fmt, align)) < 0) {
        if (frame->extended_data != frame->data)
            av_freep(&frame->extended_data);
        return ret;
    }
    if (frame->extended_data != frame->data) {
        for (ch = 0; ch < AV_NUM_DATA_POINTERS; ch++)
            frame->data[ch] = frame->extended_data[ch];
    }

    return ret;
}

static int update_frame_pool(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int i, ret;

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO: {
        AVPicture picture;
        int size[4] = { 0 };
        int w = frame->width;
        int h = frame->height;
        int tmpsize, unaligned;

        if (pool->format == frame->format &&
            pool->width == frame->width && pool->height == frame->height)
            return 0;

        avcodec_align_dimensions2(avctx, &w, &h, pool->stride_align);

        if (!(avctx->flags & CODEC_FLAG_EMU_EDGE)) {
            w += EDGE_WIDTH * 2;
            h += EDGE_WIDTH * 2;
        }

        do {
            // NOTE: do not align linesizes individually, this breaks e.g. assumptions
            // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
            av_image_fill_linesizes(picture.linesize, avctx->pix_fmt, w);
            // increase alignment of w for next try (rhs gives the lowest bit set in w)
            w += w & ~(w - 1);

            unaligned = 0;
            for (i = 0; i < 4; i++)
                unaligned |= picture.linesize[i] % pool->stride_align[i];
        } while (unaligned);

        tmpsize = av_image_fill_pointers(picture.data, avctx->pix_fmt, h,
                                         NULL, picture.linesize);
        if (tmpsize < 0)
            return -1;

        for (i = 0; i < 3 && picture.data[i + 1]; i++)
            size[i] = picture.data[i + 1] - picture.data[i];
        size[i] = tmpsize - (picture.data[i] - picture.data[0]);

        for (i = 0; i < 4; i++) {
            av_buffer_pool_uninit(&pool->pools[i]);
            pool->linesize[i] = picture.linesize[i];
            if (size[i]) {
                pool->pools[i] = av_buffer_pool_init(size[i] + 16 + STRIDE_ALIGN - 1,
                                                     CONFIG_MEMORY_POISONING ?
                                                        NULL :
                                                        av_buffer_allocz);
                if (!pool->pools[i]) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }
        }
        pool->format = frame->format;
        pool->width  = frame->width;
        pool->height = frame->height;

        break;
        }
    case AVMEDIA_TYPE_AUDIO: {
        int ch     = av_frame_get_channels(frame); //av_get_channel_layout_nb_channels(frame->channel_layout);
        int planar = av_sample_fmt_is_planar(frame->format);
        int planes = planar ? ch : 1;

        if (pool->format == frame->format && pool->planes == planes &&
            pool->channels == ch && frame->nb_samples == pool->samples)
            return 0;

        av_buffer_pool_uninit(&pool->pools[0]);
        ret = av_samples_get_buffer_size(&pool->linesize[0], ch,
                                         frame->nb_samples, frame->format, 0);
        if (ret < 0)
            goto fail;

        pool->pools[0] = av_buffer_pool_init(pool->linesize[0], NULL);
        if (!pool->pools[0]) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        pool->format     = frame->format;
        pool->planes     = planes;
        pool->channels   = ch;
        pool->samples = frame->nb_samples;
        break;
        }
    default: av_assert0(0);
    }
    return 0;
fail:
    for (i = 0; i < 4; i++)
        av_buffer_pool_uninit(&pool->pools[i]);
    pool->format = -1;
    pool->planes = pool->channels = pool->samples = 0;
    pool->width  = pool->height = 0;
    return ret;
}

static int audio_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    FramePool *pool = avctx->internal->pool;
    int planes = pool->planes;
    int i;

    frame->linesize[0] = pool->linesize[0];

    if (planes > AV_NUM_DATA_POINTERS) {
        frame->extended_data = av_mallocz(planes * sizeof(*frame->extended_data));
        frame->nb_extended_buf = planes - AV_NUM_DATA_POINTERS;
        frame->extended_buf  = av_mallocz(frame->nb_extended_buf *
                                          sizeof(*frame->extended_buf));
        if (!frame->extended_data || !frame->extended_buf) {
            av_freep(&frame->extended_data);
            av_freep(&frame->extended_buf);
            return AVERROR(ENOMEM);
        }
    } else {
        frame->extended_data = frame->data;
        av_assert0(frame->nb_extended_buf == 0);
    }

    for (i = 0; i < FFMIN(planes, AV_NUM_DATA_POINTERS); i++) {
        frame->buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->buf[i])
            goto fail;
        frame->extended_data[i] = frame->data[i] = frame->buf[i]->data;
    }
    for (i = 0; i < frame->nb_extended_buf; i++) {
        frame->extended_buf[i] = av_buffer_pool_get(pool->pools[0]);
        if (!frame->extended_buf[i])
            goto fail;
        frame->extended_data[i + AV_NUM_DATA_POINTERS] = frame->extended_buf[i]->data;
    }

    if (avctx->debug & FF_DEBUG_BUFFERS)
        av_log(avctx, AV_LOG_DEBUG, "default_get_buffer called on frame %p", frame);

    return 0;
fail:
    av_frame_unref(frame);
    return AVERROR(ENOMEM);
}

static int video_get_buffer(AVCodecContext *s, AVFrame *pic)
{
    FramePool *pool = s->internal->pool;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pic->format);
    int pixel_size = desc->comp[0].step_minus1 + 1;
    int h_chroma_shift, v_chroma_shift;
    int i;

    if (pic->data[0] != NULL) {
        av_log(s, AV_LOG_ERROR, "pic->data[0]!=NULL in avcodec_default_get_buffer\n");
        return -1;
    }

    memset(pic->data, 0, sizeof(pic->data));
    pic->extended_data = pic->data;

    av_pix_fmt_get_chroma_sub_sample(s->pix_fmt, &h_chroma_shift, &v_chroma_shift);

    for (i = 0; i < 4 && pool->pools[i]; i++) {
        const int h_shift = i == 0 ? 0 : h_chroma_shift;
        const int v_shift = i == 0 ? 0 : v_chroma_shift;
        int is_planar = pool->pools[2] || (i==0 && s->pix_fmt == AV_PIX_FMT_GRAY8);

        pic->linesize[i] = pool->linesize[i];

        pic->buf[i] = av_buffer_pool_get(pool->pools[i]);
        if (!pic->buf[i])
            goto fail;

        // no edge if EDGE EMU or not planar YUV
        if ((s->flags & CODEC_FLAG_EMU_EDGE) || !is_planar)
            pic->data[i] = pic->buf[i]->data;
        else {
            pic->data[i] = pic->buf[i]->data +
                FFALIGN((pic->linesize[i] * EDGE_WIDTH >> v_shift) +
                        (pixel_size * EDGE_WIDTH >> h_shift), pool->stride_align[i]);
        }
    }
    for (; i < AV_NUM_DATA_POINTERS; i++) {
        pic->data[i] = NULL;
        pic->linesize[i] = 0;
    }
    if (pic->data[1] && !pic->data[2])
        avpriv_set_systematic_pal2((uint32_t *)pic->data[1], s->pix_fmt);

    if (s->debug & FF_DEBUG_BUFFERS)
        av_log(s, AV_LOG_DEBUG, "default_get_buffer called on pic %p\n", pic);

    return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

void avpriv_color_frame(AVFrame *frame, const int c[4])
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);
    int p, y, x;

    av_assert0(desc->flags & AV_PIX_FMT_FLAG_PLANAR);

    for (p = 0; p<desc->nb_components; p++) {
        uint8_t *dst = frame->data[p];
        int is_chroma = p == 1 || p == 2;
        int bytes  = is_chroma ? FF_CEIL_RSHIFT(frame->width,  desc->log2_chroma_w) : frame->width;
        int height = is_chroma ? FF_CEIL_RSHIFT(frame->height, desc->log2_chroma_h) : frame->height;
        for (y = 0; y < height; y++) {
            if (desc->comp[0].depth_minus1 >= 8) {
                for (x = 0; x<bytes; x++)
                    ((uint16_t*)dst)[x] = c[p];
            }else
                memset(dst, c[p], bytes);
            dst += frame->linesize[p];
        }
    }
}

int avcodec_default_get_buffer2(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret;

    if ((ret = update_frame_pool(avctx, frame)) < 0)
        return ret;

#if FF_API_GET_BUFFER
FF_DISABLE_DEPRECATION_WARNINGS
    frame->type = FF_BUFFER_TYPE_INTERNAL;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    switch (avctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        return video_get_buffer(avctx, frame);
    case AVMEDIA_TYPE_AUDIO:
        return audio_get_buffer(avctx, frame);
    default:
        return -1;
    }
}

int ff_init_buffer_info(AVCodecContext *avctx, AVFrame *frame)
{
    if (avctx->internal->pkt) {
        frame->pkt_pts = avctx->internal->pkt->pts;
        av_frame_set_pkt_pos     (frame, avctx->internal->pkt->pos);
        av_frame_set_pkt_duration(frame, avctx->internal->pkt->duration);
        av_frame_set_pkt_size    (frame, avctx->internal->pkt->size);
    } else {
        frame->pkt_pts = AV_NOPTS_VALUE;
        av_frame_set_pkt_pos     (frame, -1);
        av_frame_set_pkt_duration(frame, 0);
        av_frame_set_pkt_size    (frame, -1);
    }
    frame->reordered_opaque = avctx->reordered_opaque;

    switch (avctx->codec->type) {
    case AVMEDIA_TYPE_VIDEO:
        frame->format              = avctx->pix_fmt;
        if (!frame->sample_aspect_ratio.num)
            frame->sample_aspect_ratio = avctx->sample_aspect_ratio;
        if (av_frame_get_colorspace(frame) == AVCOL_SPC_UNSPECIFIED)
            av_frame_set_colorspace(frame, avctx->colorspace);
        if (av_frame_get_color_range(frame) == AVCOL_RANGE_UNSPECIFIED)
            av_frame_set_color_range(frame, avctx->color_range);
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (!frame->sample_rate)
            frame->sample_rate    = avctx->sample_rate;
        if (frame->format < 0)
            frame->format         = avctx->sample_fmt;
        if (!frame->channel_layout) {
            if (avctx->channel_layout) {
                 if (av_get_channel_layout_nb_channels(avctx->channel_layout) !=
                     avctx->channels) {
                     av_log(avctx, AV_LOG_ERROR, "Inconsistent channel "
                            "configuration.\n");
                     return AVERROR(EINVAL);
                 }

                frame->channel_layout = avctx->channel_layout;
            } else {
                if (avctx->channels > FF_SANE_NB_CHANNELS) {
                    av_log(avctx, AV_LOG_ERROR, "Too many channels: %d.\n",
                           avctx->channels);
                    return AVERROR(ENOSYS);
                }
            }
        }
        av_frame_set_channels(frame, avctx->channels);
        break;
    }
    return 0;
}

#if FF_API_GET_BUFFER
FF_DISABLE_DEPRECATION_WARNINGS
int avcodec_default_get_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    return avcodec_default_get_buffer2(avctx, frame, 0);
}

typedef struct CompatReleaseBufPriv {
    AVCodecContext avctx;
    AVFrame frame;
    uint8_t avframe_padding[1024]; // hack to allow linking to a avutil with larger AVFrame
} CompatReleaseBufPriv;

static void compat_free_buffer(void *opaque, uint8_t *data)
{
    CompatReleaseBufPriv *priv = opaque;
    if (priv->avctx.release_buffer)
        priv->avctx.release_buffer(&priv->avctx, &priv->frame);
    av_freep(&priv);
}

static void compat_release_buffer(void *opaque, uint8_t *data)
{
    AVBufferRef *buf = opaque;
    av_buffer_unref(&buf);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif

int ff_decode_frame_props(AVCodecContext *avctx, AVFrame *frame)
{
    AVPacket *pkt = avctx->internal->pkt;
    uint8_t *packet_sd;
    int size;
    AVFrameSideData *frame_sd;


    frame->reordered_opaque = avctx->reordered_opaque;
    if (!pkt) {
        frame->pkt_pts = AV_NOPTS_VALUE;
        return 0;
    }

    frame->pkt_pts = pkt->pts;

    /* copy the replaygain data to the output frame */
    packet_sd = av_packet_get_side_data(pkt, AV_PKT_DATA_REPLAYGAIN, &size);
    if (packet_sd) {
        frame_sd = av_frame_new_side_data(frame, AV_FRAME_DATA_REPLAYGAIN, size);
        if (!frame_sd)
            return AVERROR(ENOMEM);

        memcpy(frame_sd->data, packet_sd, size);
    }

    return 0;
}

static int get_buffer_internal(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int override_dimensions = 1;
    int ret;

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if ((ret = av_image_check_size(avctx->width, avctx->height, 0, avctx)) < 0 || avctx->pix_fmt<0) {
            av_log(avctx, AV_LOG_ERROR, "video_get_buffer: image parameters invalid\n");
            return AVERROR(EINVAL);
        }
    }
    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (frame->width <= 0 || frame->height <= 0) {
            frame->width  = FFMAX(avctx->width,  FF_CEIL_RSHIFT(avctx->coded_width,  avctx->lowres));
            frame->height = FFMAX(avctx->height, FF_CEIL_RSHIFT(avctx->coded_height, avctx->lowres));
            override_dimensions = 0;
        }
    }
    ret = ff_decode_frame_props(avctx, frame);
    if (ret < 0)
        return ret;
    if ((ret = ff_init_buffer_info(avctx, frame)) < 0)
        return ret;

#if FF_API_GET_BUFFER
FF_DISABLE_DEPRECATION_WARNINGS
    /*
     * Wrap an old get_buffer()-allocated buffer in a bunch of AVBuffers.
     * We wrap each plane in its own AVBuffer. Each of those has a reference to
     * a dummy AVBuffer as its private data, unreffing it on free.
     * When all the planes are freed, the dummy buffer's free callback calls
     * release_buffer().
     */
    if (avctx->get_buffer) {
        CompatReleaseBufPriv *priv = NULL;
        AVBufferRef *dummy_buf = NULL;
        int planes, i, ret;

        if (flags & AV_GET_BUFFER_FLAG_REF)
            frame->reference    = 1;

        ret = avctx->get_buffer(avctx, frame);
        if (ret < 0)
            return ret;

        /* return if the buffers are already set up
         * this would happen e.g. when a custom get_buffer() calls
         * avcodec_default_get_buffer
         */
        if (frame->buf[0])
            goto end;

        priv = av_mallocz(sizeof(*priv));
        if (!priv) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        priv->avctx = *avctx;
        priv->frame = *frame;

        dummy_buf = av_buffer_create(NULL, 0, compat_free_buffer, priv, 0);
        if (!dummy_buf) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

#define WRAP_PLANE(ref_out, data, data_size)                            \
do {                                                                    \
    AVBufferRef *dummy_ref = av_buffer_ref(dummy_buf);                  \
    if (!dummy_ref) {                                                   \
        ret = AVERROR(ENOMEM);                                          \
        goto fail;                                                      \
    }                                                                   \
    ref_out = av_buffer_create(data, data_size, compat_release_buffer,  \
                               dummy_ref, 0);                           \
    if (!ref_out) {                                                     \
        av_frame_unref(frame);                                          \
        ret = AVERROR(ENOMEM);                                          \
        goto fail;                                                      \
    }                                                                   \
} while (0)

        if (avctx->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);

            planes = av_pix_fmt_count_planes(frame->format);
            /* workaround for AVHWAccel plane count of 0, buf[0] is used as
               check for allocated buffers: make libavcodec happy */
            if (desc && desc->flags & AV_PIX_FMT_FLAG_HWACCEL)
                planes = 1;
            if (!desc || planes <= 0) {
                ret = AVERROR(EINVAL);
                goto fail;
            }

            for (i = 0; i < planes; i++) {
                int v_shift    = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;
                int plane_size = (frame->height >> v_shift) * frame->linesize[i];

                WRAP_PLANE(frame->buf[i], frame->data[i], plane_size);
            }
        } else {
            int planar = av_sample_fmt_is_planar(frame->format);
            planes = planar ? avctx->channels : 1;

            if (planes > FF_ARRAY_ELEMS(frame->buf)) {
                frame->nb_extended_buf = planes - FF_ARRAY_ELEMS(frame->buf);
                frame->extended_buf = av_malloc(sizeof(*frame->extended_buf) *
                                                frame->nb_extended_buf);
                if (!frame->extended_buf) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
            }

            for (i = 0; i < FFMIN(planes, FF_ARRAY_ELEMS(frame->buf)); i++)
                WRAP_PLANE(frame->buf[i], frame->extended_data[i], frame->linesize[0]);

            for (i = 0; i < frame->nb_extended_buf; i++)
                WRAP_PLANE(frame->extended_buf[i],
                           frame->extended_data[i + FF_ARRAY_ELEMS(frame->buf)],
                           frame->linesize[0]);
        }

        av_buffer_unref(&dummy_buf);

end:
        frame->width  = avctx->width;
        frame->height = avctx->height;

        return 0;

fail:
        avctx->release_buffer(avctx, frame);
        av_freep(&priv);
        av_buffer_unref(&dummy_buf);
        return ret;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    ret = avctx->get_buffer2(avctx, frame, flags);

    if (avctx->codec_type == AVMEDIA_TYPE_VIDEO && !override_dimensions) {
        frame->width  = avctx->width;
        frame->height = avctx->height;
    }

    return ret;
}

int ff_get_buffer(AVCodecContext *avctx, AVFrame *frame, int flags)
{
    int ret = get_buffer_internal(avctx, frame, flags);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
    return ret;
}

static int reget_buffer_internal(AVCodecContext *avctx, AVFrame *frame)
{
    AVFrame *tmp;
    int ret;

    av_assert0(avctx->codec_type == AVMEDIA_TYPE_VIDEO);

    if (frame->data[0] && (frame->width != avctx->width || frame->height != avctx->height || frame->format != avctx->pix_fmt)) {
        av_log(avctx, AV_LOG_WARNING, "Picture changed from size:%dx%d fmt:%s to size:%dx%d fmt:%s in reget buffer()\n",
               frame->width, frame->height, av_get_pix_fmt_name(frame->format), avctx->width, avctx->height, av_get_pix_fmt_name(avctx->pix_fmt));
        av_frame_unref(frame);
    }

    ff_init_buffer_info(avctx, frame);

    if (!frame->data[0])
        return ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);

    if (av_frame_is_writable(frame))
        return ff_decode_frame_props(avctx, frame);

    tmp = av_frame_alloc();
    if (!tmp)
        return AVERROR(ENOMEM);

    av_frame_move_ref(tmp, frame);

    ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF);
    if (ret < 0) {
        av_frame_free(&tmp);
        return ret;
    }

    av_frame_copy(frame, tmp);
    av_frame_free(&tmp);

    return 0;
}

int ff_reget_buffer(AVCodecContext *avctx, AVFrame *frame)
{
    int ret = reget_buffer_internal(avctx, frame);
    if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
    return ret;
}

#if FF_API_GET_BUFFER
void avcodec_default_release_buffer(AVCodecContext *s, AVFrame *pic)
{
    av_assert0(s->codec_type == AVMEDIA_TYPE_VIDEO);

    av_frame_unref(pic);
}

int avcodec_default_reget_buffer(AVCodecContext *s, AVFrame *pic)
{
    av_assert0(0);
    return AVERROR_BUG;
}
#endif

int avcodec_default_execute(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2), void *arg, int *ret, int count, int size)
{
    int i;

    for (i = 0; i < count; i++) {
        int r = func(c, (char *)arg + i * size);
        if (ret)
            ret[i] = r;
    }
    return 0;
}

int avcodec_default_execute2(AVCodecContext *c, int (*func)(AVCodecContext *c2, void *arg2, int jobnr, int threadnr), void *arg, int *ret, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        int r = func(c, arg, i, 0);
        if (ret)
            ret[i] = r;
    }
    return 0;
}

enum AVPixelFormat avpriv_find_pix_fmt(const PixelFormatTag *tags,
                                       unsigned int fourcc)
{
    while (tags->pix_fmt >= 0) {
        if (tags->fourcc == fourcc)
            return tags->pix_fmt;
        tags++;
    }
    return AV_PIX_FMT_NONE;
}

static int is_hwaccel_pix_fmt(enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    return desc->flags & AV_PIX_FMT_FLAG_HWACCEL;
}

enum AVPixelFormat avcodec_default_get_format(struct AVCodecContext *s, const enum AVPixelFormat *fmt)
{
    while (*fmt != AV_PIX_FMT_NONE && is_hwaccel_pix_fmt(*fmt))
        ++fmt;
    return fmt[0];
}

#if FF_API_AVFRAME_LAVC
void avcodec_get_frame_defaults(AVFrame *frame)
{
#if LIBAVCODEC_VERSION_MAJOR >= 55
     // extended_data should explicitly be freed when needed, this code is unsafe currently
     // also this is not compatible to the <55 ABI/API
    if (frame->extended_data != frame->data && 0)
        av_freep(&frame->extended_data);
#endif

    memset(frame, 0, sizeof(AVFrame));
    av_frame_unref(frame);
}

AVFrame *avcodec_alloc_frame(void)
{
    return av_frame_alloc();
}

void avcodec_free_frame(AVFrame **frame)
{
    av_frame_free(frame);
}
#endif

MAKE_ACCESSORS(AVCodecContext, codec, AVRational, pkt_timebase)
MAKE_ACCESSORS(AVCodecContext, codec, const AVCodecDescriptor *, codec_descriptor)
MAKE_ACCESSORS(AVCodecContext, codec, int, lowres)
MAKE_ACCESSORS(AVCodecContext, codec, int, seek_preroll)
MAKE_ACCESSORS(AVCodecContext, codec, uint16_t*, chroma_intra_matrix)

int av_codec_get_max_lowres(const AVCodec *codec)
{
    return codec->max_lowres;
}

static void avcodec_get_subtitle_defaults(AVSubtitle *sub)
{
    memset(sub, 0, sizeof(*sub));
    sub->pts = AV_NOPTS_VALUE;
}

static int get_bit_rate(AVCodecContext *ctx)
{
    int bit_rate;
    int bits_per_sample;

    switch (ctx->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
    case AVMEDIA_TYPE_DATA:
    case AVMEDIA_TYPE_SUBTITLE:
    case AVMEDIA_TYPE_ATTACHMENT:
        bit_rate = ctx->bit_rate;
        break;
    case AVMEDIA_TYPE_AUDIO:
        bits_per_sample = av_get_bits_per_sample(ctx->codec_id);
        bit_rate = bits_per_sample ? ctx->sample_rate * ctx->channels * bits_per_sample : ctx->bit_rate;
        break;
    default:
        bit_rate = 0;
        break;
    }
    return bit_rate;
}

int attribute_align_arg ff_codec_open2_recursive(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)
{
    int ret = 0;

    ff_unlock_avcodec();

    ret = avcodec_open2(avctx, codec, options);

    ff_lock_avcodec(avctx);
    return ret;
}

int attribute_align_arg avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options)
{
    int ret = 0;
    AVDictionary *tmp = NULL;

    if (avcodec_is_open(avctx))
        return 0;

    if ((!codec && !avctx->codec)) {
        av_log(avctx, AV_LOG_ERROR, "No codec provided to avcodec_open2()\n");
        return AVERROR(EINVAL);
    }
    if ((codec && avctx->codec && codec != avctx->codec)) {
        av_log(avctx, AV_LOG_ERROR, "This AVCodecContext was allocated for %s, "
                                    "but %s passed to avcodec_open2()\n", avctx->codec->name, codec->name);
        return AVERROR(EINVAL);
    }
    if (!codec)
        codec = avctx->codec;

    if (avctx->extradata_size < 0 || avctx->extradata_size >= FF_MAX_EXTRADATA_SIZE)
        return AVERROR(EINVAL);

    if (options)
        av_dict_copy(&tmp, *options, 0);

    ret = ff_lock_avcodec(avctx);
    if (ret < 0)
        return ret;

    avctx->internal = av_mallocz(sizeof(AVCodecInternal));
    if (!avctx->internal) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avctx->internal->pool = av_mallocz(sizeof(*avctx->internal->pool));
    if (!avctx->internal->pool) {
        ret = AVERROR(ENOMEM);
        goto free_and_end;
    }

    avctx->internal->to_free = av_frame_alloc();
    if (!avctx->internal->to_free) {
        ret = AVERROR(ENOMEM);
        goto free_and_end;
    }

    if (codec->priv_data_size > 0) {
        if (!avctx->priv_data) {
            avctx->priv_data = av_mallocz(codec->priv_data_size);
            if (!avctx->priv_data) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            if (codec->priv_class) {
                *(const AVClass **)avctx->priv_data = codec->priv_class;
                av_opt_set_defaults(avctx->priv_data);
            }
        }
        if (codec->priv_class && (ret = av_opt_set_dict(avctx->priv_data, &tmp)) < 0)
            goto free_and_end;
    } else {
        avctx->priv_data = NULL;
    }
    if ((ret = av_opt_set_dict(avctx, &tmp)) < 0)
        goto free_and_end;

    // only call ff_set_dimensions() for non H.264/VP6F codecs so as not to overwrite previously setup dimensions
    if (!(avctx->coded_width && avctx->coded_height && avctx->width && avctx->height &&
          (avctx->codec_id == AV_CODEC_ID_H264 || avctx->codec_id == AV_CODEC_ID_VP6F))) {
    if (avctx->coded_width && avctx->coded_height)
        ret = ff_set_dimensions(avctx, avctx->coded_width, avctx->coded_height);
    else if (avctx->width && avctx->height)
        ret = ff_set_dimensions(avctx, avctx->width, avctx->height);
    if (ret < 0)
        goto free_and_end;
    }

    if ((avctx->coded_width || avctx->coded_height || avctx->width || avctx->height)
        && (  av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx) < 0
           || av_image_check_size(avctx->width,       avctx->height,       0, avctx) < 0)) {
        av_log(avctx, AV_LOG_WARNING, "Ignoring invalid width/height values\n");
        ff_set_dimensions(avctx, 0, 0);
    }

    /* if the decoder init function was already called previously,
     * free the already allocated subtitle_header before overwriting it */
    if (av_codec_is_decoder(codec))
        av_freep(&avctx->subtitle_header);

    if (avctx->channels > FF_SANE_NB_CHANNELS) {
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    avctx->codec = codec;
    if ((avctx->codec_type == AVMEDIA_TYPE_UNKNOWN || avctx->codec_type == codec->type) &&
        avctx->codec_id == AV_CODEC_ID_NONE) {
        avctx->codec_type = codec->type;
        avctx->codec_id   = codec->id;
    }
    if (avctx->codec_id != codec->id || (avctx->codec_type != codec->type
                                         && avctx->codec_type != AVMEDIA_TYPE_ATTACHMENT)) {
        av_log(avctx, AV_LOG_ERROR, "Codec type or id mismatches\n");
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }
    avctx->frame_number = 0;
    avctx->codec_descriptor = avcodec_descriptor_get(avctx->codec_id);

    if (avctx->codec->capabilities & CODEC_CAP_EXPERIMENTAL &&
        avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL) {
        const char *codec_string = av_codec_is_encoder(codec) ? "encoder" : "decoder";
        AVCodec *codec2;
        av_log(avctx, AV_LOG_ERROR,
               "The %s '%s' is experimental but experimental codecs are not enabled, "
               "add '-strict %d' if you want to use it.\n",
               codec_string, codec->name, FF_COMPLIANCE_EXPERIMENTAL);
        codec2 = av_codec_is_encoder(codec) ? avcodec_find_encoder(codec->id) : avcodec_find_decoder(codec->id);
        if (!(codec2->capabilities & CODEC_CAP_EXPERIMENTAL))
            av_log(avctx, AV_LOG_ERROR, "Alternatively use the non experimental %s '%s'.\n",
                codec_string, codec2->name);
        ret = AVERROR_EXPERIMENTAL;
        goto free_and_end;
    }

    if (avctx->codec_type == AVMEDIA_TYPE_AUDIO &&
        (!avctx->time_base.num || !avctx->time_base.den)) {
        avctx->time_base.num = 1;
        avctx->time_base.den = avctx->sample_rate;
    }

    if (!HAVE_THREADS)
        av_log(avctx, AV_LOG_WARNING, "Warning: not compiled with thread support, using thread emulation\n");

    if (CONFIG_FRAME_THREAD_ENCODER) {
        ff_unlock_avcodec(); //we will instanciate a few encoders thus kick the counter to prevent false detection of a problem
        ret = ff_frame_thread_encoder_init(avctx, options ? *options : NULL);
        ff_lock_avcodec(avctx);
        if (ret < 0)
            goto free_and_end;
    }

    if (HAVE_THREADS
        && !(avctx->internal->frame_thread_encoder && (avctx->active_thread_type&FF_THREAD_FRAME))) {
        ret = ff_thread_init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }
    if (!HAVE_THREADS && !(codec->capabilities & CODEC_CAP_AUTO_THREADS))
        avctx->thread_count = 1;

    if (avctx->codec->max_lowres < avctx->lowres || avctx->lowres < 0) {
        av_log(avctx, AV_LOG_ERROR, "The maximum value for lowres supported by the decoder is %d\n",
               avctx->codec->max_lowres);
        ret = AVERROR(EINVAL);
        goto free_and_end;
    }

    if (av_codec_is_encoder(avctx->codec)) {
        int i;
        if (avctx->codec->sample_fmts) {
            for (i = 0; avctx->codec->sample_fmts[i] != AV_SAMPLE_FMT_NONE; i++) {
                if (avctx->sample_fmt == avctx->codec->sample_fmts[i])
                    break;
                if (avctx->channels == 1 &&
                    av_get_planar_sample_fmt(avctx->sample_fmt) ==
                    av_get_planar_sample_fmt(avctx->codec->sample_fmts[i])) {
                    avctx->sample_fmt = avctx->codec->sample_fmts[i];
                    break;
                }
            }
            if (avctx->codec->sample_fmts[i] == AV_SAMPLE_FMT_NONE) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%d", avctx->sample_fmt);
                av_log(avctx, AV_LOG_ERROR, "Specified sample format %s is invalid or not supported\n",
                       (char *)av_x_if_null(av_get_sample_fmt_name(avctx->sample_fmt), buf));
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (avctx->codec->pix_fmts) {
            for (i = 0; avctx->codec->pix_fmts[i] != AV_PIX_FMT_NONE; i++)
                if (avctx->pix_fmt == avctx->codec->pix_fmts[i])
                    break;
            if (avctx->codec->pix_fmts[i] == AV_PIX_FMT_NONE
                && !((avctx->codec_id == AV_CODEC_ID_MJPEG || avctx->codec_id == AV_CODEC_ID_LJPEG)
                     && avctx->strict_std_compliance <= FF_COMPLIANCE_UNOFFICIAL)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%d", avctx->pix_fmt);
                av_log(avctx, AV_LOG_ERROR, "Specified pixel format %s is invalid or not supported\n",
                       (char *)av_x_if_null(av_get_pix_fmt_name(avctx->pix_fmt), buf));
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (avctx->codec->supported_samplerates) {
            for (i = 0; avctx->codec->supported_samplerates[i] != 0; i++)
                if (avctx->sample_rate == avctx->codec->supported_samplerates[i])
                    break;
            if (avctx->codec->supported_samplerates[i] == 0) {
                av_log(avctx, AV_LOG_ERROR, "Specified sample rate %d is not supported\n",
                       avctx->sample_rate);
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (avctx->codec->channel_layouts) {
            if (!avctx->channel_layout) {
                av_log(avctx, AV_LOG_WARNING, "Channel layout not specified\n");
            } else {
                for (i = 0; avctx->codec->channel_layouts[i] != 0; i++)
                    if (avctx->channel_layout == avctx->codec->channel_layouts[i])
                        break;
                if (avctx->codec->channel_layouts[i] == 0) {
                    char buf[512];
                    av_get_channel_layout_string(buf, sizeof(buf), -1, avctx->channel_layout);
                    av_log(avctx, AV_LOG_ERROR, "Specified channel layout '%s' is not supported\n", buf);
                    ret = AVERROR(EINVAL);
                    goto free_and_end;
                }
            }
        }
        if (avctx->channel_layout && avctx->channels) {
            int channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
            if (channels != avctx->channels) {
                char buf[512];
                av_get_channel_layout_string(buf, sizeof(buf), -1, avctx->channel_layout);
                av_log(avctx, AV_LOG_ERROR,
                       "Channel layout '%s' with %d channels does not match number of specified channels %d\n",
                       buf, channels, avctx->channels);
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        } else if (avctx->channel_layout) {
            avctx->channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
        }
        if(avctx->codec_type == AVMEDIA_TYPE_VIDEO &&
           avctx->codec_id != AV_CODEC_ID_PNG // For mplayer
        ) {
            if (avctx->width <= 0 || avctx->height <= 0) {
                av_log(avctx, AV_LOG_ERROR, "dimensions not set\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            }
        }
        if (   (avctx->codec_type == AVMEDIA_TYPE_VIDEO || avctx->codec_type == AVMEDIA_TYPE_AUDIO)
            && avctx->bit_rate>0 && avctx->bit_rate<1000) {
            av_log(avctx, AV_LOG_WARNING, "Bitrate %d is extremely low, maybe you mean %dk\n", avctx->bit_rate, avctx->bit_rate);
        }

        if (!avctx->rc_initial_buffer_occupancy)
            avctx->rc_initial_buffer_occupancy = avctx->rc_buffer_size * 3 / 4;
    }

    avctx->pts_correction_num_faulty_pts =
    avctx->pts_correction_num_faulty_dts = 0;
    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if (   avctx->codec->init && (!(avctx->active_thread_type&FF_THREAD_FRAME)
        || avctx->internal->frame_thread_encoder)) {
        ret = avctx->codec->init(avctx);
        if (ret < 0) {
            goto free_and_end;
        }
    }

    ret=0;

    if (av_codec_is_decoder(avctx->codec)) {
        if (!avctx->bit_rate)
            avctx->bit_rate = get_bit_rate(avctx);
        /* validate channel layout from the decoder */
        if (avctx->channel_layout) {
            int channels = av_get_channel_layout_nb_channels(avctx->channel_layout);
            if (!avctx->channels)
                avctx->channels = channels;
            else if (channels != avctx->channels) {
                char buf[512];
                av_get_channel_layout_string(buf, sizeof(buf), -1, avctx->channel_layout);
                av_log(avctx, AV_LOG_WARNING,
                       "Channel layout '%s' with %d channels does not match specified number of channels %d: "
                       "ignoring specified channel layout\n",
                       buf, channels, avctx->channels);
                avctx->channel_layout = 0;
            }
        }
        if (avctx->channels && avctx->channels < 0 ||
            avctx->channels > FF_SANE_NB_CHANNELS) {
            ret = AVERROR(EINVAL);
            goto free_and_end;
        }
        if (avctx->sub_charenc) {
            if (avctx->codec_type != AVMEDIA_TYPE_SUBTITLE) {
                av_log(avctx, AV_LOG_ERROR, "Character encoding is only "
                       "supported with subtitles codecs\n");
                ret = AVERROR(EINVAL);
                goto free_and_end;
            } else if (avctx->codec_descriptor->props & AV_CODEC_PROP_BITMAP_SUB) {
                av_log(avctx, AV_LOG_WARNING, "Codec '%s' is bitmap-based, "
                       "subtitles character encoding will be ignored\n",
                       avctx->codec_descriptor->name);
                avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_DO_NOTHING;
            } else {
                /* input character encoding is set for a text based subtitle
                 * codec at this point */
                if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_AUTOMATIC)
                    avctx->sub_charenc_mode = FF_SUB_CHARENC_MODE_PRE_DECODER;

                if (avctx->sub_charenc_mode == FF_SUB_CHARENC_MODE_PRE_DECODER) {
#if CONFIG_ICONV
                    iconv_t cd = iconv_open("UTF-8", avctx->sub_charenc);
                    if (cd == (iconv_t)-1) {
                        av_log(avctx, AV_LOG_ERROR, "Unable to open iconv context "
                               "with input character encoding \"%s\"\n", avctx->sub_charenc);
                        ret = AVERROR(errno);
                        goto free_and_end;
                    }
                    iconv_close(cd);
#else
                    av_log(avctx, AV_LOG_ERROR, "Character encoding subtitles "
                           "conversion needs a libavcodec built with iconv support "
                           "for this codec\n");
                    ret = AVERROR(ENOSYS);
                    goto free_and_end;
#endif
                }
            }
        }
    }
end:
    ff_unlock_avcodec();
    if (options) {
        av_dict_free(options);
        *options = tmp;
    }

    return ret;
free_and_end:
    av_dict_free(&tmp);
    av_freep(&avctx->priv_data);
    if (avctx->internal) {
        av_frame_free(&avctx->internal->to_free);
        av_freep(&avctx->internal->pool);
    }
    av_freep(&avctx->internal);
    avctx->codec = NULL;
    goto end;
}

int ff_alloc_packet2(AVCodecContext *avctx, AVPacket *avpkt, int64_t size)
{
    if (avpkt->size < 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid negative user packet size %d\n", avpkt->size);
        return AVERROR(EINVAL);
    }
    if (size < 0 || size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid minimum required packet size %"PRId64" (max allowed is %d)\n",
               size, INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE);
        return AVERROR(EINVAL);
    }

    if (avctx) {
        av_assert0(!avpkt->data || avpkt->data != avctx->internal->byte_buffer);
        if (!avpkt->data || avpkt->size < size) {
            av_fast_padded_malloc(&avctx->internal->byte_buffer, &avctx->internal->byte_buffer_size, size);
            avpkt->data = avctx->internal->byte_buffer;
            avpkt->size = avctx->internal->byte_buffer_size;
            avpkt->destruct = NULL;
        }
    }

    if (avpkt->data) {
        AVBufferRef *buf = avpkt->buf;
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
        void *destruct = avpkt->destruct;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

        if (avpkt->size < size) {
            av_log(avctx, AV_LOG_ERROR, "User packet is too small (%d < %"PRId64")\n", avpkt->size, size);
            return AVERROR(EINVAL);
        }

        av_init_packet(avpkt);
#if FF_API_DESTRUCT_PACKET
FF_DISABLE_DEPRECATION_WARNINGS
        avpkt->destruct = destruct;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
        avpkt->buf      = buf;
        avpkt->size     = size;
        return 0;
    } else {
        int ret = av_new_packet(avpkt, size);
        if (ret < 0)
            av_log(avctx, AV_LOG_ERROR, "Failed to allocate packet of size %"PRId64"\n", size);
        return ret;
    }
}

int ff_alloc_packet(AVPacket *avpkt, int size)
{
    return ff_alloc_packet2(NULL, avpkt, size);
}

/**
 * Pad last frame with silence.
 */
static int pad_last_frame(AVCodecContext *s, AVFrame **dst, const AVFrame *src)
{
    AVFrame *frame = NULL;
    int ret;

    if (!(frame = av_frame_alloc()))
        return AVERROR(ENOMEM);

    frame->format         = src->format;
    frame->channel_layout = src->channel_layout;
    av_frame_set_channels(frame, av_frame_get_channels(src));
    frame->nb_samples     = s->frame_size;
    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0)
        goto fail;

    ret = av_frame_copy_props(frame, src);
    if (ret < 0)
        goto fail;

    if ((ret = av_samples_copy(frame->extended_data, src->extended_data, 0, 0,
                               src->nb_samples, s->channels, s->sample_fmt)) < 0)
        goto fail;
    if ((ret = av_samples_set_silence(frame->extended_data, src->nb_samples,
                                      frame->nb_samples - src->nb_samples,
                                      s->channels, s->sample_fmt)) < 0)
        goto fail;

    *dst = frame;

    return 0;

fail:
    av_frame_free(&frame);
    return ret;
}

int attribute_align_arg avcodec_encode_audio2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    AVFrame *extended_frame = NULL;
    AVFrame *padded_frame = NULL;
    int ret;
    AVPacket user_pkt = *avpkt;
    int needs_realloc = !user_pkt.data;

    *got_packet_ptr = 0;

    if (!(avctx->codec->capabilities & CODEC_CAP_DELAY) && !frame) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        return 0;
    }

    /* ensure that extended_data is properly set */
    if (frame && !frame->extended_data) {
        if (av_sample_fmt_is_planar(avctx->sample_fmt) &&
            avctx->channels > AV_NUM_DATA_POINTERS) {
            av_log(avctx, AV_LOG_ERROR, "Encoding to a planar sample format, "
                                        "with more than %d channels, but extended_data is not set.\n",
                   AV_NUM_DATA_POINTERS);
            return AVERROR(EINVAL);
        }
        av_log(avctx, AV_LOG_WARNING, "extended_data is not set.\n");

        extended_frame = av_frame_alloc();
        if (!extended_frame)
            return AVERROR(ENOMEM);

        memcpy(extended_frame, frame, sizeof(AVFrame));
        extended_frame->extended_data = extended_frame->data;
        frame = extended_frame;
    }

    /* check for valid frame size */
    if (frame) {
        if (avctx->codec->capabilities & CODEC_CAP_SMALL_LAST_FRAME) {
            if (frame->nb_samples > avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "more samples than frame size (avcodec_encode_audio2)\n");
                ret = AVERROR(EINVAL);
                goto end;
            }
        } else if (!(avctx->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)) {
            if (frame->nb_samples < avctx->frame_size &&
                !avctx->internal->last_audio_frame) {
                ret = pad_last_frame(avctx, &padded_frame, frame);
                if (ret < 0)
                    goto end;

                frame = padded_frame;
                avctx->internal->last_audio_frame = 1;
            }

            if (frame->nb_samples != avctx->frame_size) {
                av_log(avctx, AV_LOG_ERROR, "nb_samples (%d) != frame_size (%d) (avcodec_encode_audio2)\n", frame->nb_samples, avctx->frame_size);
                ret = AVERROR(EINVAL);
                goto end;
            }
        }
    }

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    if (!ret) {
        if (*got_packet_ptr) {
            if (!(avctx->codec->capabilities & CODEC_CAP_DELAY)) {
                if (avpkt->pts == AV_NOPTS_VALUE)
                    avpkt->pts = frame->pts;
                if (!avpkt->duration)
                    avpkt->duration = ff_samples_to_time_base(avctx,
                                                              frame->nb_samples);
            }
            avpkt->dts = avpkt->pts;
        } else {
            avpkt->size = 0;
        }
    }
    if (avpkt->data && avpkt->data == avctx->internal->byte_buffer) {
        needs_realloc = 0;
        if (user_pkt.data) {
            if (user_pkt.size >= avpkt->size) {
                memcpy(user_pkt.data, avpkt->data, avpkt->size);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Provided packet is too small, needs to be %d\n", avpkt->size);
                avpkt->size = user_pkt.size;
                ret = -1;
            }
            avpkt->buf      = user_pkt.buf;
            avpkt->data     = user_pkt.data;
            avpkt->destruct = user_pkt.destruct;
        } else {
            if (av_dup_packet(avpkt) < 0) {
                ret = AVERROR(ENOMEM);
            }
        }
    }

    if (!ret) {
        if (needs_realloc && avpkt->data) {
            ret = av_buffer_realloc(&avpkt->buf, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (ret >= 0)
                avpkt->data = avpkt->buf->data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        goto end;
    }

    /* NOTE: if we add any audio encoders which output non-keyframe packets,
     *       this needs to be moved to the encoders, but for now we can do it
     *       here to simplify things */
    avpkt->flags |= AV_PKT_FLAG_KEY;

end:
    av_frame_free(&padded_frame);
    av_free(extended_frame);

    return ret;
}

#if FF_API_OLD_ENCODE_AUDIO
int attribute_align_arg avcodec_encode_audio(AVCodecContext *avctx,
                                             uint8_t *buf, int buf_size,
                                             const short *samples)
{
    AVPacket pkt;
    AVFrame *frame;
    int ret, samples_size, got_packet;

    av_init_packet(&pkt);
    pkt.data = buf;
    pkt.size = buf_size;

    if (samples) {
        frame = av_frame_alloc();
        if (!frame)
            return AVERROR(ENOMEM);

        if (avctx->frame_size) {
            frame->nb_samples = avctx->frame_size;
        } else {
            /* if frame_size is not set, the number of samples must be
             * calculated from the buffer size */
            int64_t nb_samples;
            if (!av_get_bits_per_sample(avctx->codec_id)) {
                av_log(avctx, AV_LOG_ERROR, "avcodec_encode_audio() does not "
                                            "support this codec\n");
                av_frame_free(&frame);
                return AVERROR(EINVAL);
            }
            nb_samples = (int64_t)buf_size * 8 /
                         (av_get_bits_per_sample(avctx->codec_id) *
                          avctx->channels);
            if (nb_samples >= INT_MAX) {
                av_frame_free(&frame);
                return AVERROR(EINVAL);
            }
            frame->nb_samples = nb_samples;
        }

        /* it is assumed that the samples buffer is large enough based on the
         * relevant parameters */
        samples_size = av_samples_get_buffer_size(NULL, avctx->channels,
                                                  frame->nb_samples,
                                                  avctx->sample_fmt, 1);
        if ((ret = avcodec_fill_audio_frame(frame, avctx->channels,
                                            avctx->sample_fmt,
                                            (const uint8_t *)samples,
                                            samples_size, 1)) < 0) {
            av_frame_free(&frame);
            return ret;
        }

        /* fabricate frame pts from sample count.
         * this is needed because the avcodec_encode_audio() API does not have
         * a way for the user to provide pts */
        if (avctx->sample_rate && avctx->time_base.num)
            frame->pts = ff_samples_to_time_base(avctx,
                                                 avctx->internal->sample_count);
        else
            frame->pts = AV_NOPTS_VALUE;
        avctx->internal->sample_count += frame->nb_samples;
    } else {
        frame = NULL;
    }

    got_packet = 0;
    ret = avcodec_encode_audio2(avctx, &pkt, frame, &got_packet);
    if (!ret && got_packet && avctx->coded_frame) {
        avctx->coded_frame->pts       = pkt.pts;
        avctx->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }
    /* free any side data since we cannot return it */
    av_packet_free_side_data(&pkt);

    if (frame && frame->extended_data != frame->data)
        av_freep(&frame->extended_data);

    av_frame_free(&frame);
    return ret ? ret : pkt.size;
}

#endif

#if FF_API_OLD_ENCODE_VIDEO
int attribute_align_arg avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                                             const AVFrame *pict)
{
    AVPacket pkt;
    int ret, got_packet = 0;

    if (buf_size < FF_MIN_BUFFER_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
        return -1;
    }

    av_init_packet(&pkt);
    pkt.data = buf;
    pkt.size = buf_size;

    ret = avcodec_encode_video2(avctx, &pkt, pict, &got_packet);
    if (!ret && got_packet && avctx->coded_frame) {
        avctx->coded_frame->pts       = pkt.pts;
        avctx->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

    /* free any side data since we cannot return it */
    if (pkt.side_data_elems > 0) {
        int i;
        for (i = 0; i < pkt.side_data_elems; i++)
            av_free(pkt.side_data[i].data);
        av_freep(&pkt.side_data);
        pkt.side_data_elems = 0;
    }

    return ret ? ret : pkt.size;
}

#endif

int attribute_align_arg avcodec_encode_video2(AVCodecContext *avctx,
                                              AVPacket *avpkt,
                                              const AVFrame *frame,
                                              int *got_packet_ptr)
{
    int ret;
    AVPacket user_pkt = *avpkt;
    int needs_realloc = !user_pkt.data;

    *got_packet_ptr = 0;

    if(CONFIG_FRAME_THREAD_ENCODER &&
       avctx->internal->frame_thread_encoder && (avctx->active_thread_type&FF_THREAD_FRAME))
        return ff_thread_video_encode_frame(avctx, avpkt, frame, got_packet_ptr);

    if ((avctx->flags&CODEC_FLAG_PASS1) && avctx->stats_out)
        avctx->stats_out[0] = '\0';

    if (!(avctx->codec->capabilities & CODEC_CAP_DELAY) && !frame) {
        av_free_packet(avpkt);
        av_init_packet(avpkt);
        avpkt->size = 0;
        return 0;
    }

    if (av_image_check_size(avctx->width, avctx->height, 0, avctx))
        return AVERROR(EINVAL);

    av_assert0(avctx->codec->encode2);

    ret = avctx->codec->encode2(avctx, avpkt, frame, got_packet_ptr);
    av_assert0(ret <= 0);

    if (avpkt->data && avpkt->data == avctx->internal->byte_buffer) {
        needs_realloc = 0;
        if (user_pkt.data) {
            if (user_pkt.size >= avpkt->size) {
                memcpy(user_pkt.data, avpkt->data, avpkt->size);
            } else {
                av_log(avctx, AV_LOG_ERROR, "Provided packet is too small, needs to be %d\n", avpkt->size);
                avpkt->size = user_pkt.size;
                ret = -1;
            }
            avpkt->buf      = user_pkt.buf;
            avpkt->data     = user_pkt.data;
            avpkt->destruct = user_pkt.destruct;
        } else {
            if (av_dup_packet(avpkt) < 0) {
                ret = AVERROR(ENOMEM);
            }
        }
    }

    if (!ret) {
        if (!*got_packet_ptr)
            avpkt->size = 0;
        else if (!(avctx->codec->capabilities & CODEC_CAP_DELAY))
            avpkt->pts = avpkt->dts = frame->pts;

        if (needs_realloc && avpkt->data) {
            ret = av_buffer_realloc(&avpkt->buf, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (ret >= 0)
                avpkt->data = avpkt->buf->data;
        }

        avctx->frame_number++;
    }

    if (ret < 0 || !*got_packet_ptr)
        av_free_packet(avpkt);
    else
        av_packet_merge_side_data(avpkt);

    emms_c();
    return ret;
}

int avcodec_encode_subtitle(AVCodecContext *avctx, uint8_t *buf, int buf_size,
                            const AVSubtitle *sub)
{
    int ret;
    if (sub->start_display_time) {
        av_log(avctx, AV_LOG_ERROR, "start_display_time must be 0.\n");
        return -1;
    }

    ret = avctx->codec->encode_sub(avctx, buf, buf_size, sub);
    avctx->frame_number++;
    return ret;
}

/**
 * Attempt to guess proper monotonic timestamps for decoded video frames
 * which might have incorrect times. Input timestamps may wrap around, in
 * which case the output will as well.
 *
 * @param pts the pts field of the decoded AVPacket, as passed through
 * AVFrame.pkt_pts
 * @param dts the dts field of the decoded AVPacket
 * @return one of the input values, may be AV_NOPTS_VALUE
 */
static int64_t guess_correct_pts(AVCodecContext *ctx,
                                 int64_t reordered_pts, int64_t dts)
{
    int64_t pts = AV_NOPTS_VALUE;

    if (dts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_dts += dts <= ctx->pts_correction_last_dts;
        ctx->pts_correction_last_dts = dts;
    } else if (reordered_pts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_dts = reordered_pts;

    if (reordered_pts != AV_NOPTS_VALUE) {
        ctx->pts_correction_num_faulty_pts += reordered_pts <= ctx->pts_correction_last_pts;
        ctx->pts_correction_last_pts = reordered_pts;
    } else if(dts != AV_NOPTS_VALUE)
        ctx->pts_correction_last_pts = dts;

    if ((ctx->pts_correction_num_faulty_pts<=ctx->pts_correction_num_faulty_dts || dts == AV_NOPTS_VALUE)
       && reordered_pts != AV_NOPTS_VALUE)
        pts = reordered_pts;
    else
        pts = dts;

    return pts;
}

static int apply_param_change(AVCodecContext *avctx, AVPacket *avpkt)
{
    int size = 0, ret;
    const uint8_t *data;
    uint32_t flags;

    data = av_packet_get_side_data(avpkt, AV_PKT_DATA_PARAM_CHANGE, &size);
    if (!data)
        return 0;

    if (!(avctx->codec->capabilities & CODEC_CAP_PARAM_CHANGE)) {
        av_log(avctx, AV_LOG_ERROR, "This decoder does not support parameter "
               "changes, but PARAM_CHANGE side data was sent to it.\n");
        return AVERROR(EINVAL);
    }

    if (size < 4)
        goto fail;

    flags = bytestream_get_le32(&data);
    size -= 4;

    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
        if (size < 4)
            goto fail;
        avctx->channels = bytestream_get_le32(&data);
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
        if (size < 8)
            goto fail;
        avctx->channel_layout = bytestream_get_le64(&data);
        size -= 8;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
        if (size < 4)
            goto fail;
        avctx->sample_rate = bytestream_get_le32(&data);
        size -= 4;
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
        if (size < 8)
            goto fail;
        avctx->width  = bytestream_get_le32(&data);
        avctx->height = bytestream_get_le32(&data);
        size -= 8;
        ret = ff_set_dimensions(avctx, avctx->width, avctx->height);
        if (ret < 0)
            return ret;
    }

    return 0;
fail:
    av_log(avctx, AV_LOG_ERROR, "PARAM_CHANGE side data too small.\n");
    return AVERROR_INVALIDDATA;
}

static int add_metadata_from_side_data(AVCodecContext *avctx, AVFrame *frame)
{
    int size;
    const uint8_t *side_metadata;

    AVDictionary **frame_md = avpriv_frame_get_metadatap(frame);

    side_metadata = av_packet_get_side_data(avctx->internal->pkt,
                                            AV_PKT_DATA_STRINGS_METADATA, &size);
    return av_packet_unpack_dictionary(side_metadata, size, frame_md);
}

static int unrefcount_frame(AVCodecInternal *avci, AVFrame *frame)
{
    int ret;

    /* move the original frame to our backup */
    av_frame_unref(avci->to_free);
    av_frame_move_ref(avci->to_free, frame);

    /* now copy everything except the AVBufferRefs back
     * note that we make a COPY of the side data, so calling av_frame_free() on
     * the caller's frame will work properly */
    ret = av_frame_copy_props(frame, avci->to_free);
    if (ret < 0)
        return ret;

    memcpy(frame->data,     avci->to_free->data,     sizeof(frame->data));
    memcpy(frame->linesize, avci->to_free->linesize, sizeof(frame->linesize));
    if (avci->to_free->extended_data != avci->to_free->data) {
        int planes = av_frame_get_channels(avci->to_free);
        int size   = planes * sizeof(*frame->extended_data);

        if (!size) {
            av_frame_unref(frame);
            return AVERROR_BUG;
        }

        frame->extended_data = av_malloc(size);
        if (!frame->extended_data) {
            av_frame_unref(frame);
            return AVERROR(ENOMEM);
        }
        memcpy(frame->extended_data, avci->to_free->extended_data,
               size);
    } else
        frame->extended_data = frame->data;

    frame->format         = avci->to_free->format;
    frame->width          = avci->to_free->width;
    frame->height         = avci->to_free->height;
    frame->channel_layout = avci->to_free->channel_layout;
    frame->nb_samples     = avci->to_free->nb_samples;
    av_frame_set_channels(frame, av_frame_get_channels(avci->to_free));

    return 0;
}

int attribute_align_arg avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
                                              int *got_picture_ptr,
                                              const AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret;
    // copy to ensure we do not change avpkt
    AVPacket tmp = *avpkt;

    if (!avctx->codec)
        return AVERROR(EINVAL);
    if (avctx->codec->type != AVMEDIA_TYPE_VIDEO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for video\n");
        return AVERROR(EINVAL);
    }

    *got_picture_ptr = 0;
    if ((avctx->coded_width || avctx->coded_height) && av_image_check_size(avctx->coded_width, avctx->coded_height, 0, avctx))
        return AVERROR(EINVAL);

    av_frame_unref(picture);

    if ((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size || (avctx->active_thread_type & FF_THREAD_FRAME)) {
        int did_split = av_packet_split_side_data(&tmp);
        ret = apply_param_change(avctx, &tmp);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error applying parameter changes.\n");
            if (avctx->err_recognition & AV_EF_EXPLODE)
                goto fail;
        }

        avctx->internal->pkt = &tmp;
        if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
            ret = ff_thread_decode_frame(avctx, picture, got_picture_ptr,
                                         &tmp);
        else {
            ret = avctx->codec->decode(avctx, picture, got_picture_ptr,
                                       &tmp);
            picture->pkt_dts = avpkt->dts;

            if(!avctx->has_b_frames){
                av_frame_set_pkt_pos(picture, avpkt->pos);
            }
            //FIXME these should be under if(!avctx->has_b_frames)
            /* get_buffer is supposed to set frame parameters */
            if (!(avctx->codec->capabilities & CODEC_CAP_DR1)) {
                if (!picture->sample_aspect_ratio.num)    picture->sample_aspect_ratio = avctx->sample_aspect_ratio;
                if (!picture->width)                      picture->width               = avctx->width;
                if (!picture->height)                     picture->height              = avctx->height;
                if (picture->format == AV_PIX_FMT_NONE)   picture->format              = avctx->pix_fmt;
            }
        }
        add_metadata_from_side_data(avctx, picture);

fail:
        emms_c(); //needed to avoid an emms_c() call before every return;

        avctx->internal->pkt = NULL;
        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }

        if (*got_picture_ptr) {
            if (!avctx->refcounted_frames) {
                int err = unrefcount_frame(avci, picture);
                if (err < 0)
                    return err;
            }

            avctx->frame_number++;
            av_frame_set_best_effort_timestamp(picture,
                                               guess_correct_pts(avctx,
                                                                 picture->pkt_pts,
                                                                 picture->pkt_dts));
        } else
            av_frame_unref(picture);
    } else
        ret = 0;

    /* many decoders assign whole AVFrames, thus overwriting extended_data;
     * make sure it's set correctly */
    av_assert0(!picture->extended_data || picture->extended_data == picture->data);

    return ret;
}

#if FF_API_OLD_DECODE_AUDIO
int attribute_align_arg avcodec_decode_audio3(AVCodecContext *avctx, int16_t *samples,
                                              int *frame_size_ptr,
                                              AVPacket *avpkt)
{
    AVFrame *frame = av_frame_alloc();
    int ret, got_frame = 0;

    if (!frame)
        return AVERROR(ENOMEM);
    if (avctx->get_buffer != avcodec_default_get_buffer) {
        av_log(avctx, AV_LOG_ERROR, "Custom get_buffer() for use with"
                                    "avcodec_decode_audio3() detected. Overriding with avcodec_default_get_buffer\n");
        av_log(avctx, AV_LOG_ERROR, "Please port your application to "
                                    "avcodec_decode_audio4()\n");
        avctx->get_buffer = avcodec_default_get_buffer;
        avctx->release_buffer = avcodec_default_release_buffer;
    }

    ret = avcodec_decode_audio4(avctx, frame, &got_frame, avpkt);

    if (ret >= 0 && got_frame) {
        int ch, plane_size;
        int planar    = av_sample_fmt_is_planar(avctx->sample_fmt);
        int data_size = av_samples_get_buffer_size(&plane_size, avctx->channels,
                                                   frame->nb_samples,
                                                   avctx->sample_fmt, 1);
        if (*frame_size_ptr < data_size) {
            av_log(avctx, AV_LOG_ERROR, "output buffer size is too small for "
                                        "the current frame (%d < %d)\n", *frame_size_ptr, data_size);
            av_frame_free(&frame);
            return AVERROR(EINVAL);
        }

        memcpy(samples, frame->extended_data[0], plane_size);

        if (planar && avctx->channels > 1) {
            uint8_t *out = ((uint8_t *)samples) + plane_size;
            for (ch = 1; ch < avctx->channels; ch++) {
                memcpy(out, frame->extended_data[ch], plane_size);
                out += plane_size;
            }
        }
        *frame_size_ptr = data_size;
    } else {
        *frame_size_ptr = 0;
    }
    av_frame_free(&frame);
    return ret;
}

#endif

int attribute_align_arg avcodec_decode_audio4(AVCodecContext *avctx,
                                              AVFrame *frame,
                                              int *got_frame_ptr,
                                              const AVPacket *avpkt)
{
    AVCodecInternal *avci = avctx->internal;
    int ret = 0;

    *got_frame_ptr = 0;

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }
    if (!avctx->codec)
        return AVERROR(EINVAL);
    if (avctx->codec->type != AVMEDIA_TYPE_AUDIO) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for audio\n");
        return AVERROR(EINVAL);
    }

    av_frame_unref(frame);

    if ((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size || (avctx->active_thread_type & FF_THREAD_FRAME)) {
        uint8_t *side;
        int side_size;
        uint32_t discard_padding = 0;
        // copy to ensure we do not change avpkt
        AVPacket tmp = *avpkt;
        int did_split = av_packet_split_side_data(&tmp);
        ret = apply_param_change(avctx, &tmp);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error applying parameter changes.\n");
            if (avctx->err_recognition & AV_EF_EXPLODE)
                goto fail;
        }

        avctx->internal->pkt = &tmp;
        if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
            ret = ff_thread_decode_frame(avctx, frame, got_frame_ptr, &tmp);
        else {
            ret = avctx->codec->decode(avctx, frame, got_frame_ptr, &tmp);
            frame->pkt_dts = avpkt->dts;
        }
        if (ret >= 0 && *got_frame_ptr) {
            add_metadata_from_side_data(avctx, frame);
            avctx->frame_number++;
            av_frame_set_best_effort_timestamp(frame,
                                               guess_correct_pts(avctx,
                                                                 frame->pkt_pts,
                                                                 frame->pkt_dts));
            if (frame->format == AV_SAMPLE_FMT_NONE)
                frame->format = avctx->sample_fmt;
            if (!frame->channel_layout)
                frame->channel_layout = avctx->channel_layout;
            if (!av_frame_get_channels(frame))
                av_frame_set_channels(frame, avctx->channels);
            if (!frame->sample_rate)
                frame->sample_rate = avctx->sample_rate;
        }

        side= av_packet_get_side_data(avctx->internal->pkt, AV_PKT_DATA_SKIP_SAMPLES, &side_size);
        if(side && side_size>=10) {
            avctx->internal->skip_samples = AV_RL32(side);
            av_log(avctx, AV_LOG_DEBUG, "skip %d samples due to side data\n",
                   avctx->internal->skip_samples);
            discard_padding = AV_RL32(side + 4);
        }
        if (avctx->internal->skip_samples && *got_frame_ptr) {
            if(frame->nb_samples <= avctx->internal->skip_samples){
                *got_frame_ptr = 0;
                avctx->internal->skip_samples -= frame->nb_samples;
                av_log(avctx, AV_LOG_DEBUG, "skip whole frame, skip left: %d\n",
                       avctx->internal->skip_samples);
            } else {
                av_samples_copy(frame->extended_data, frame->extended_data, 0, avctx->internal->skip_samples,
                                frame->nb_samples - avctx->internal->skip_samples, avctx->channels, frame->format);
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(avctx->internal->skip_samples,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    if(frame->pkt_pts!=AV_NOPTS_VALUE)
                        frame->pkt_pts += diff_ts;
                    if(frame->pkt_dts!=AV_NOPTS_VALUE)
                        frame->pkt_dts += diff_ts;
                    if (av_frame_get_pkt_duration(frame) >= diff_ts)
                        av_frame_set_pkt_duration(frame, av_frame_get_pkt_duration(frame) - diff_ts);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for skipped samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "skip %d/%d samples\n",
                       avctx->internal->skip_samples, frame->nb_samples);
                frame->nb_samples -= avctx->internal->skip_samples;
                avctx->internal->skip_samples = 0;
            }
        }

        if (discard_padding > 0 && discard_padding <= frame->nb_samples && *got_frame_ptr) {
            if (discard_padding == frame->nb_samples) {
                *got_frame_ptr = 0;
            } else {
                if(avctx->pkt_timebase.num && avctx->sample_rate) {
                    int64_t diff_ts = av_rescale_q(frame->nb_samples - discard_padding,
                                                   (AVRational){1, avctx->sample_rate},
                                                   avctx->pkt_timebase);
                    if (av_frame_get_pkt_duration(frame) >= diff_ts)
                        av_frame_set_pkt_duration(frame, av_frame_get_pkt_duration(frame) - diff_ts);
                } else {
                    av_log(avctx, AV_LOG_WARNING, "Could not update timestamps for discarded samples.\n");
                }
                av_log(avctx, AV_LOG_DEBUG, "discard %d/%d samples\n",
                       discard_padding, frame->nb_samples);
                frame->nb_samples -= discard_padding;
            }
        }
fail:
        avctx->internal->pkt = NULL;
        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }

        if (ret >= 0 && *got_frame_ptr) {
            if (!avctx->refcounted_frames) {
                int err = unrefcount_frame(avci, frame);
                if (err < 0)
                    return err;
            }
        } else
            av_frame_unref(frame);
    }

    return ret;
}

#define UTF8_MAX_BYTES 4 /* 5 and 6 bytes sequences should not be used */
static int recode_subtitle(AVCodecContext *avctx,
                           AVPacket *outpkt, const AVPacket *inpkt)
{
#if CONFIG_ICONV
    iconv_t cd = (iconv_t)-1;
    int ret = 0;
    char *inb, *outb;
    size_t inl, outl;
    AVPacket tmp;
#endif

    if (avctx->sub_charenc_mode != FF_SUB_CHARENC_MODE_PRE_DECODER || inpkt->size == 0)
        return 0;

#if CONFIG_ICONV
    cd = iconv_open("UTF-8", avctx->sub_charenc);
    av_assert0(cd != (iconv_t)-1);

    inb = inpkt->data;
    inl = inpkt->size;

    if (inl >= INT_MAX / UTF8_MAX_BYTES - FF_INPUT_BUFFER_PADDING_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Subtitles packet is too big for recoding\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    ret = av_new_packet(&tmp, inl * UTF8_MAX_BYTES);
    if (ret < 0)
        goto end;
    outpkt->buf  = tmp.buf;
    outpkt->data = tmp.data;
    outpkt->size = tmp.size;
    outb = outpkt->data;
    outl = outpkt->size;

    if (iconv(cd, &inb, &inl, &outb, &outl) == (size_t)-1 ||
        iconv(cd, NULL, NULL, &outb, &outl) == (size_t)-1 ||
        outl >= outpkt->size || inl != 0) {
        av_log(avctx, AV_LOG_ERROR, "Unable to recode subtitle event \"%s\" "
               "from %s to UTF-8\n", inpkt->data, avctx->sub_charenc);
        av_free_packet(&tmp);
        ret = AVERROR(errno);
        goto end;
    }
    outpkt->size -= outl;
    memset(outpkt->data + outpkt->size, 0, outl);

end:
    if (cd != (iconv_t)-1)
        iconv_close(cd);
    return ret;
#else
    av_assert0(!"requesting subtitles recoding without iconv");
#endif
}

static int utf8_check(const uint8_t *str)
{
    const uint8_t *byte;
    uint32_t codepoint, min;

    while (*str) {
        byte = str;
        GET_UTF8(codepoint, *(byte++), return 0;);
        min = byte - str == 1 ? 0 : byte - str == 2 ? 0x80 :
              1 << (5 * (byte - str) - 4);
        if (codepoint < min || codepoint >= 0x110000 ||
            codepoint == 0xFFFE /* BOM */ ||
            codepoint >= 0xD800 && codepoint <= 0xDFFF /* surrogates */)
            return 0;
        str = byte;
    }
    return 1;
}

int avcodec_decode_subtitle2(AVCodecContext *avctx, AVSubtitle *sub,
                             int *got_sub_ptr,
                             AVPacket *avpkt)
{
    int i, ret = 0;

    if (!avpkt->data && avpkt->size) {
        av_log(avctx, AV_LOG_ERROR, "invalid packet: NULL data, size != 0\n");
        return AVERROR(EINVAL);
    }
    if (!avctx->codec)
        return AVERROR(EINVAL);
    if (avctx->codec->type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid media type for subtitles\n");
        return AVERROR(EINVAL);
    }

    *got_sub_ptr = 0;
    avcodec_get_subtitle_defaults(sub);

    if ((avctx->codec->capabilities & CODEC_CAP_DELAY) || avpkt->size) {
        AVPacket pkt_recoded;
        AVPacket tmp = *avpkt;
        int did_split = av_packet_split_side_data(&tmp);
        //apply_param_change(avctx, &tmp);

        if (did_split) {
            /* FFMIN() prevents overflow in case the packet wasn't allocated with
             * proper padding.
             * If the side data is smaller than the buffer padding size, the
             * remaining bytes should have already been filled with zeros by the
             * original packet allocation anyway. */
            memset(tmp.data + tmp.size, 0,
                   FFMIN(avpkt->size - tmp.size, FF_INPUT_BUFFER_PADDING_SIZE));
        }

        pkt_recoded = tmp;
        ret = recode_subtitle(avctx, &pkt_recoded, &tmp);
        if (ret < 0) {
            *got_sub_ptr = 0;
        } else {
            avctx->internal->pkt = &pkt_recoded;

            if (avctx->pkt_timebase.den && avpkt->pts != AV_NOPTS_VALUE)
                sub->pts = av_rescale_q(avpkt->pts,
                                        avctx->pkt_timebase, AV_TIME_BASE_Q);
            ret = avctx->codec->decode(avctx, sub, got_sub_ptr, &pkt_recoded);
            av_assert1((ret >= 0) >= !!*got_sub_ptr &&
                       !!*got_sub_ptr >= !!sub->num_rects);

            if (sub->num_rects && !sub->end_display_time && avpkt->duration &&
                avctx->pkt_timebase.num) {
                AVRational ms = { 1, 1000 };
                sub->end_display_time = av_rescale_q(avpkt->duration,
                                                     avctx->pkt_timebase, ms);
            }

            for (i = 0; i < sub->num_rects; i++) {
                if (sub->rects[i]->ass && !utf8_check(sub->rects[i]->ass)) {
                    av_log(avctx, AV_LOG_ERROR,
                           "Invalid UTF-8 in decoded subtitles text; "
                           "maybe missing -sub_charenc option\n");
                    avsubtitle_free(sub);
                    return AVERROR_INVALIDDATA;
                }
            }

            if (tmp.data != pkt_recoded.data) { // did we recode?
                /* prevent from destroying side data from original packet */
                pkt_recoded.side_data = NULL;
                pkt_recoded.side_data_elems = 0;

                av_free_packet(&pkt_recoded);
            }
            if (avctx->codec_descriptor->props & AV_CODEC_PROP_BITMAP_SUB)
                sub->format = 0;
            else if (avctx->codec_descriptor->props & AV_CODEC_PROP_TEXT_SUB)
                sub->format = 1;
            avctx->internal->pkt = NULL;
        }

        if (did_split) {
            av_packet_free_side_data(&tmp);
            if(ret == tmp.size)
                ret = avpkt->size;
        }

        if (*got_sub_ptr)
            avctx->frame_number++;
    }

    return ret;
}

void avsubtitle_free(AVSubtitle *sub)
{
    int i;

    for (i = 0; i < sub->num_rects; i++) {
        av_freep(&sub->rects[i]->pict.data[0]);
        av_freep(&sub->rects[i]->pict.data[1]);
        av_freep(&sub->rects[i]->pict.data[2]);
        av_freep(&sub->rects[i]->pict.data[3]);
        av_freep(&sub->rects[i]->text);
        av_freep(&sub->rects[i]->ass);
        av_freep(&sub->rects[i]);
    }

    av_freep(&sub->rects);

    memset(sub, 0, sizeof(AVSubtitle));
}

av_cold int ff_codec_close_recursive(AVCodecContext *avctx)
{
    int ret = 0;

    ff_unlock_avcodec();

    ret = avcodec_close(avctx);

    ff_lock_avcodec(NULL);
    return ret;
}

av_cold int avcodec_close(AVCodecContext *avctx)
{
    int ret;

    if (!avctx)
        return 0;

    ret = ff_lock_avcodec(avctx);
    if (ret < 0)
        return ret;

    if (avcodec_is_open(avctx)) {
        FramePool *pool = avctx->internal->pool;
        int i;
        if (CONFIG_FRAME_THREAD_ENCODER &&
            avctx->internal->frame_thread_encoder && avctx->thread_count > 1) {
            ff_unlock_avcodec();
            ff_frame_thread_encoder_free(avctx);
            ff_lock_avcodec(avctx);
        }
        if (HAVE_THREADS && avctx->internal->thread_ctx)
            ff_thread_free(avctx);
        if (avctx->codec && avctx->codec->close)
            avctx->codec->close(avctx);
        avctx->coded_frame = NULL;
        avctx->internal->byte_buffer_size = 0;
        av_freep(&avctx->internal->byte_buffer);
        av_frame_free(&avctx->internal->to_free);
        for (i = 0; i < FF_ARRAY_ELEMS(pool->pools); i++)
            av_buffer_pool_uninit(&pool->pools[i]);
        av_freep(&avctx->internal->pool);
        av_freep(&avctx->internal);
    }

    if (avctx->priv_data && avctx->codec && avctx->codec->priv_class)
        av_opt_free(avctx->priv_data);
    av_opt_free(avctx);
    av_freep(&avctx->priv_data);
    if (av_codec_is_encoder(avctx->codec))
        av_freep(&avctx->extradata);
    avctx->codec = NULL;
    avctx->active_thread_type = 0;

    ff_unlock_avcodec();
    return 0;
}

static enum AVCodecID remap_deprecated_codec_id(enum AVCodecID id)
{
    switch(id){
        //This is for future deprecatec codec ids, its empty since
        //last major bump but will fill up again over time, please don't remove it
//         case AV_CODEC_ID_UTVIDEO_DEPRECATED: return AV_CODEC_ID_UTVIDEO;
        case AV_CODEC_ID_BRENDER_PIX_DEPRECATED: return AV_CODEC_ID_BRENDER_PIX;
        case AV_CODEC_ID_OPUS_DEPRECATED: return AV_CODEC_ID_OPUS;
        case AV_CODEC_ID_TAK_DEPRECATED : return AV_CODEC_ID_TAK;
        case AV_CODEC_ID_PAF_AUDIO_DEPRECATED : return AV_CODEC_ID_PAF_AUDIO;
        case AV_CODEC_ID_PCM_S24LE_PLANAR_DEPRECATED : return AV_CODEC_ID_PCM_S24LE_PLANAR;
        case AV_CODEC_ID_PCM_S32LE_PLANAR_DEPRECATED : return AV_CODEC_ID_PCM_S32LE_PLANAR;
        case AV_CODEC_ID_ADPCM_VIMA_DEPRECATED : return AV_CODEC_ID_ADPCM_VIMA;
        case AV_CODEC_ID_ESCAPE130_DEPRECATED : return AV_CODEC_ID_ESCAPE130;
        case AV_CODEC_ID_EXR_DEPRECATED : return AV_CODEC_ID_EXR;
        case AV_CODEC_ID_G2M_DEPRECATED : return AV_CODEC_ID_G2M;
        case AV_CODEC_ID_PAF_VIDEO_DEPRECATED : return AV_CODEC_ID_PAF_VIDEO;
        case AV_CODEC_ID_WEBP_DEPRECATED: return AV_CODEC_ID_WEBP;
        case AV_CODEC_ID_HEVC_DEPRECATED: return AV_CODEC_ID_HEVC;
        case AV_CODEC_ID_SANM_DEPRECATED: return AV_CODEC_ID_SANM;
        case AV_CODEC_ID_VP7_DEPRECATED : return AV_CODEC_ID_VP7;
        default                         : return id;
    }
}

static AVCodec *find_encdec(enum AVCodecID id, int encoder)
{
    AVCodec *p, *experimental = NULL;
    p = first_avcodec;
    id= remap_deprecated_codec_id(id);
    while (p) {
        if ((encoder ? av_codec_is_encoder(p) : av_codec_is_decoder(p)) &&
            p->id == id) {
            if (p->capabilities & CODEC_CAP_EXPERIMENTAL && !experimental) {
                experimental = p;
            } else
                return p;
        }
        p = p->next;
    }
    return experimental;
}

AVCodec *avcodec_find_encoder(enum AVCodecID id)
{
    return find_encdec(id, 1);
}

AVCodec *avcodec_find_encoder_by_name(const char *name)
{
    AVCodec *p;
    if (!name)
        return NULL;
    p = first_avcodec;
    while (p) {
        if (av_codec_is_encoder(p) && strcmp(name, p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

AVCodec *avcodec_find_decoder(enum AVCodecID id)
{
    return find_encdec(id, 0);
}

AVCodec *avcodec_find_decoder_by_name(const char *name)
{
    AVCodec *p;
    if (!name)
        return NULL;
    p = first_avcodec;
    while (p) {
        if (av_codec_is_decoder(p) && strcmp(name, p->name) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

const char *avcodec_get_name(enum AVCodecID id)
{
    const AVCodecDescriptor *cd;
    AVCodec *codec;

    if (id == AV_CODEC_ID_NONE)
        return "none";
    cd = avcodec_descriptor_get(id);
    if (cd)
        return cd->name;
    av_log(NULL, AV_LOG_WARNING, "Codec 0x%x is not in the full list.\n", id);
    codec = avcodec_find_decoder(id);
    if (codec)
        return codec->name;
    codec = avcodec_find_encoder(id);
    if (codec)
        return codec->name;
    return "unknown_codec";
}

size_t av_get_codec_tag_string(char *buf, size_t buf_size, unsigned int codec_tag)
{
    int i, len, ret = 0;

#define TAG_PRINT(x)                                              \
    (((x) >= '0' && (x) <= '9') ||                                \
     ((x) >= 'a' && (x) <= 'z') || ((x) >= 'A' && (x) <= 'Z') ||  \
     ((x) == '.' || (x) == ' ' || (x) == '-' || (x) == '_'))

    for (i = 0; i < 4; i++) {
        len = snprintf(buf, buf_size,
                       TAG_PRINT(codec_tag & 0xFF) ? "%c" : "[%d]", codec_tag & 0xFF);
        buf        += len;
        buf_size    = buf_size > len ? buf_size - len : 0;
        ret        += len;
        codec_tag >>= 8;
    }
    return ret;
}

void avcodec_string(char *buf, int buf_size, AVCodecContext *enc, int encode)
{
    const char *codec_type;
    const char *codec_name;
    const char *profile = NULL;
    const AVCodec *p;
    int bitrate;
    AVRational display_aspect_ratio;

    if (!buf || buf_size <= 0)
        return;
    codec_type = av_get_media_type_string(enc->codec_type);
    codec_name = avcodec_get_name(enc->codec_id);
    if (enc->profile != FF_PROFILE_UNKNOWN) {
        if (enc->codec)
            p = enc->codec;
        else
            p = encode ? avcodec_find_encoder(enc->codec_id) :
                        avcodec_find_decoder(enc->codec_id);
        if (p)
            profile = av_get_profile_name(p, enc->profile);
    }

    snprintf(buf, buf_size, "%s: %s", codec_type ? codec_type : "unknown",
             codec_name);
    buf[0] ^= 'a' ^ 'A'; /* first letter in uppercase */

    if (enc->codec && strcmp(enc->codec->name, codec_name))
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s)", enc->codec->name);

    if (profile)
        snprintf(buf + strlen(buf), buf_size - strlen(buf), " (%s)", profile);
    if (enc->codec_tag) {
        char tag_buf[32];
        av_get_codec_tag_string(tag_buf, sizeof(tag_buf), enc->codec_tag);
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 " (%s / 0x%04X)", tag_buf, enc->codec_tag);
    }

    switch (enc->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        if (enc->pix_fmt != AV_PIX_FMT_NONE) {
            char detail[256] = "(";
            const char *colorspace_name;
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s",
                     av_get_pix_fmt_name(enc->pix_fmt));
            if (enc->bits_per_raw_sample &&
                enc->bits_per_raw_sample <= av_pix_fmt_desc_get(enc->pix_fmt)->comp[0].depth_minus1)
                av_strlcatf(detail, sizeof(detail), "%d bpc, ", enc->bits_per_raw_sample);
            if (enc->color_range != AVCOL_RANGE_UNSPECIFIED)
                av_strlcatf(detail, sizeof(detail),
                            enc->color_range == AVCOL_RANGE_MPEG ? "tv, ": "pc, ");

            colorspace_name = av_get_colorspace_name(enc->colorspace);
            if (colorspace_name)
                av_strlcatf(detail, sizeof(detail), "%s, ", colorspace_name);

            if (strlen(detail) > 1) {
                detail[strlen(detail) - 2] = 0;
                av_strlcatf(buf, buf_size, "%s)", detail);
            }
        }
        if (enc->width) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d",
                     enc->width, enc->height);
            if (enc->sample_aspect_ratio.num) {
                av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                          enc->width * enc->sample_aspect_ratio.num,
                          enc->height * enc->sample_aspect_ratio.den,
                          1024 * 1024);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         " [SAR %d:%d DAR %d:%d]",
                         enc->sample_aspect_ratio.num, enc->sample_aspect_ratio.den,
                         display_aspect_ratio.num, display_aspect_ratio.den);
            }
            if (av_log_get_level() >= AV_LOG_DEBUG) {
                int g = av_gcd(enc->time_base.num, enc->time_base.den);
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", %d/%d",
                         enc->time_base.num / g, enc->time_base.den / g);
            }
        }
        if (encode) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", q=%d-%d", enc->qmin, enc->qmax);
        }
        break;
    case AVMEDIA_TYPE_AUDIO:
        if (enc->sample_rate) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %d Hz", enc->sample_rate);
        }
        av_strlcat(buf, ", ", buf_size);
        av_get_channel_layout_string(buf + strlen(buf), buf_size - strlen(buf), enc->channels, enc->channel_layout);
        if (enc->sample_fmt != AV_SAMPLE_FMT_NONE) {
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %s", av_get_sample_fmt_name(enc->sample_fmt));
        }
        break;
    case AVMEDIA_TYPE_DATA:
        if (av_log_get_level() >= AV_LOG_DEBUG) {
            int g = av_gcd(enc->time_base.num, enc->time_base.den);
            if (g)
                snprintf(buf + strlen(buf), buf_size - strlen(buf),
                         ", %d/%d",
                         enc->time_base.num / g, enc->time_base.den / g);
        }
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        if (enc->width)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", %dx%d", enc->width, enc->height);
        break;
    default:
        return;
    }
    if (encode) {
        if (enc->flags & CODEC_FLAG_PASS1)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 1");
        if (enc->flags & CODEC_FLAG_PASS2)
            snprintf(buf + strlen(buf), buf_size - strlen(buf),
                     ", pass 2");
    }
    bitrate = get_bit_rate(enc);
    if (bitrate != 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", %d kb/s", bitrate / 1000);
    } else if (enc->rc_max_rate > 0) {
        snprintf(buf + strlen(buf), buf_size - strlen(buf),
                 ", max. %d kb/s", enc->rc_max_rate / 1000);
    }
}

const char *av_get_profile_name(const AVCodec *codec, int profile)
{
    const AVProfile *p;
    if (profile == FF_PROFILE_UNKNOWN || !codec->profiles)
        return NULL;

    for (p = codec->profiles; p->profile != FF_PROFILE_UNKNOWN; p++)
        if (p->profile == profile)
            return p->name;

    return NULL;
}

unsigned avcodec_version(void)
{
//    av_assert0(AV_CODEC_ID_V410==164);
    av_assert0(AV_CODEC_ID_PCM_S8_PLANAR==65563);
    av_assert0(AV_CODEC_ID_ADPCM_G722==69660);
//     av_assert0(AV_CODEC_ID_BMV_AUDIO==86071);
    av_assert0(AV_CODEC_ID_SRT==94216);
    av_assert0(LIBAVCODEC_VERSION_MICRO >= 100);

    av_assert0(CODEC_ID_CLLC == AV_CODEC_ID_CLLC);
    av_assert0(CODEC_ID_PCM_S8_PLANAR == AV_CODEC_ID_PCM_S8_PLANAR);
    av_assert0(CODEC_ID_ADPCM_IMA_APC == AV_CODEC_ID_ADPCM_IMA_APC);
    av_assert0(CODEC_ID_ILBC == AV_CODEC_ID_ILBC);
    av_assert0(CODEC_ID_SRT == AV_CODEC_ID_SRT);
    return LIBAVCODEC_VERSION_INT;
}

const char *avcodec_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avcodec_license(void)
{
#define LICENSE_PREFIX "libavcodec license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

void avcodec_flush_buffers(AVCodecContext *avctx)
{
    if (HAVE_THREADS && avctx->active_thread_type & FF_THREAD_FRAME)
        ff_thread_flush(avctx);
    else if (avctx->codec->flush)
        avctx->codec->flush(avctx);

    avctx->pts_correction_last_pts =
    avctx->pts_correction_last_dts = INT64_MIN;

    if (!avctx->refcounted_frames)
        av_frame_unref(avctx->internal->to_free);
}

int av_get_exact_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_8SVX_EXP:
    case AV_CODEC_ID_8SVX_FIB:
    case AV_CODEC_ID_ADPCM_CT:
    case AV_CODEC_ID_ADPCM_IMA_APC:
    case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
    case AV_CODEC_ID_ADPCM_IMA_OKI:
    case AV_CODEC_ID_ADPCM_IMA_WS:
    case AV_CODEC_ID_ADPCM_G722:
    case AV_CODEC_ID_ADPCM_YAMAHA:
        return 4;
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_S8_PLANAR:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_ZORK:
        return 8;
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_S16BE_PLANAR:
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16LE_PLANAR:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_U16LE:
        return 16;
    case AV_CODEC_ID_PCM_S24DAUD:
    case AV_CODEC_ID_PCM_S24BE:
    case AV_CODEC_ID_PCM_S24LE:
    case AV_CODEC_ID_PCM_S24LE_PLANAR:
    case AV_CODEC_ID_PCM_U24BE:
    case AV_CODEC_ID_PCM_U24LE:
        return 24;
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32LE_PLANAR:
    case AV_CODEC_ID_PCM_U32BE:
    case AV_CODEC_ID_PCM_U32LE:
    case AV_CODEC_ID_PCM_F32BE:
    case AV_CODEC_ID_PCM_F32LE:
        return 32;
    case AV_CODEC_ID_PCM_F64BE:
    case AV_CODEC_ID_PCM_F64LE:
        return 64;
    default:
        return 0;
    }
}

enum AVCodecID av_get_pcm_codec(enum AVSampleFormat fmt, int be)
{
    static const enum AVCodecID map[AV_SAMPLE_FMT_NB][2] = {
        [AV_SAMPLE_FMT_U8  ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16 ] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32 ] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_FLT ] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBL ] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
        [AV_SAMPLE_FMT_U8P ] = { AV_CODEC_ID_PCM_U8,    AV_CODEC_ID_PCM_U8    },
        [AV_SAMPLE_FMT_S16P] = { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_PCM_S16BE },
        [AV_SAMPLE_FMT_S32P] = { AV_CODEC_ID_PCM_S32LE, AV_CODEC_ID_PCM_S32BE },
        [AV_SAMPLE_FMT_FLTP] = { AV_CODEC_ID_PCM_F32LE, AV_CODEC_ID_PCM_F32BE },
        [AV_SAMPLE_FMT_DBLP] = { AV_CODEC_ID_PCM_F64LE, AV_CODEC_ID_PCM_F64BE },
    };
    if (fmt < 0 || fmt >= AV_SAMPLE_FMT_NB)
        return AV_CODEC_ID_NONE;
    if (be < 0 || be > 1)
        be = AV_NE(1, 0);
    return map[fmt][be];
}

int av_get_bits_per_sample(enum AVCodecID codec_id)
{
    switch (codec_id) {
    case AV_CODEC_ID_ADPCM_SBPRO_2:
        return 2;
    case AV_CODEC_ID_ADPCM_SBPRO_3:
        return 3;
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ADPCM_IMA_QT:
    case AV_CODEC_ID_ADPCM_SWF:
    case AV_CODEC_ID_ADPCM_MS:
        return 4;
    default:
        return av_get_exact_bits_per_sample(codec_id);
    }
}

int av_get_audio_frame_duration(AVCodecContext *avctx, int frame_bytes)
{
    int id, sr, ch, ba, tag, bps;

    id  = avctx->codec_id;
    sr  = avctx->sample_rate;
    ch  = avctx->channels;
    ba  = avctx->block_align;
    tag = avctx->codec_tag;
    bps = av_get_exact_bits_per_sample(avctx->codec_id);

    /* codecs with an exact constant bits per sample */
    if (bps > 0 && ch > 0 && frame_bytes > 0 && ch < 32768 && bps < 32768)
        return (frame_bytes * 8LL) / (bps * ch);
    bps = avctx->bits_per_coded_sample;

    /* codecs with a fixed packet duration */
    switch (id) {
    case AV_CODEC_ID_ADPCM_ADX:    return   32;
    case AV_CODEC_ID_ADPCM_IMA_QT: return   64;
    case AV_CODEC_ID_ADPCM_EA_XAS: return  128;
    case AV_CODEC_ID_AMR_NB:
    case AV_CODEC_ID_EVRC:
    case AV_CODEC_ID_GSM:
    case AV_CODEC_ID_QCELP:
    case AV_CODEC_ID_RA_288:       return  160;
    case AV_CODEC_ID_AMR_WB:
    case AV_CODEC_ID_GSM_MS:       return  320;
    case AV_CODEC_ID_MP1:          return  384;
    case AV_CODEC_ID_ATRAC1:       return  512;
    case AV_CODEC_ID_ATRAC3:       return 1024;
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MUSEPACK7:    return 1152;
    case AV_CODEC_ID_AC3:          return 1536;
    }

    if (sr > 0) {
        /* calc from sample rate */
        if (id == AV_CODEC_ID_TTA)
            return 256 * sr / 245;

        if (ch > 0) {
            /* calc from sample rate and channels */
            if (id == AV_CODEC_ID_BINKAUDIO_DCT)
                return (480 << (sr / 22050)) / ch;
        }
    }

    if (ba > 0) {
        /* calc from block_align */
        if (id == AV_CODEC_ID_SIPR) {
            switch (ba) {
            case 20: return 160;
            case 19: return 144;
            case 29: return 288;
            case 37: return 480;
            }
        } else if (id == AV_CODEC_ID_ILBC) {
            switch (ba) {
            case 38: return 160;
            case 50: return 240;
            }
        }
    }

    if (frame_bytes > 0) {
        /* calc from frame_bytes only */
        if (id == AV_CODEC_ID_TRUESPEECH)
            return 240 * (frame_bytes / 32);
        if (id == AV_CODEC_ID_NELLYMOSER)
            return 256 * (frame_bytes / 64);
        if (id == AV_CODEC_ID_RA_144)
            return 160 * (frame_bytes / 20);
        if (id == AV_CODEC_ID_G723_1)
            return 240 * (frame_bytes / 24);

        if (bps > 0) {
            /* calc from frame_bytes and bits_per_coded_sample */
            if (id == AV_CODEC_ID_ADPCM_G726)
                return frame_bytes * 8 / bps;
        }

        if (ch > 0) {
            /* calc from frame_bytes and channels */
            switch (id) {
            case AV_CODEC_ID_ADPCM_AFC:
                return frame_bytes / (9 * ch) * 16;
            case AV_CODEC_ID_ADPCM_DTK:
                return frame_bytes / (16 * ch) * 28;
            case AV_CODEC_ID_ADPCM_4XM:
            case AV_CODEC_ID_ADPCM_IMA_ISS:
                return (frame_bytes - 4 * ch) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
                return (frame_bytes - 4) * 2 / ch;
            case AV_CODEC_ID_ADPCM_IMA_AMV:
                return (frame_bytes - 8) * 2 / ch;
            case AV_CODEC_ID_ADPCM_XA:
                return (frame_bytes / 128) * 224 / ch;
            case AV_CODEC_ID_INTERPLAY_DPCM:
                return (frame_bytes - 6 - ch) / ch;
            case AV_CODEC_ID_ROQ_DPCM:
                return (frame_bytes - 8) / ch;
            case AV_CODEC_ID_XAN_DPCM:
                return (frame_bytes - 2 * ch) / ch;
            case AV_CODEC_ID_MACE3:
                return 3 * frame_bytes / ch;
            case AV_CODEC_ID_MACE6:
                return 6 * frame_bytes / ch;
            case AV_CODEC_ID_PCM_LXF:
                return 2 * (frame_bytes / (5 * ch));
            case AV_CODEC_ID_IAC:
            case AV_CODEC_ID_IMC:
                return 4 * frame_bytes / ch;
            }

            if (tag) {
                /* calc from frame_bytes, channels, and codec_tag */
                if (id == AV_CODEC_ID_SOL_DPCM) {
                    if (tag == 3)
                        return frame_bytes / ch;
                    else
                        return frame_bytes * 2 / ch;
                }
            }

            if (ba > 0) {
                /* calc from frame_bytes, channels, and block_align */
                int blocks = frame_bytes / ba;
                switch (avctx->codec_id) {
                case AV_CODEC_ID_ADPCM_IMA_WAV:
                    if (bps < 2 || bps > 5)
                        return 0;
                    return blocks * (1 + (ba - 4 * ch) / (bps * ch) * 8);
                case AV_CODEC_ID_ADPCM_IMA_DK3:
                    return blocks * (((ba - 16) * 2 / 3 * 4) / ch);
                case AV_CODEC_ID_ADPCM_IMA_DK4:
                    return blocks * (1 + (ba - 4 * ch) * 2 / ch);
                case AV_CODEC_ID_ADPCM_IMA_RAD:
                    return blocks * ((ba - 4 * ch) * 2 / ch);
                case AV_CODEC_ID_ADPCM_MS:
                    return blocks * (2 + (ba - 7 * ch) * 2 / ch);
                }
            }

            if (bps > 0) {
                /* calc from frame_bytes, channels, and bits_per_coded_sample */
                switch (avctx->codec_id) {
                case AV_CODEC_ID_PCM_DVD:
                    if(bps<4)
                        return 0;
                    return 2 * (frame_bytes / ((bps * 2 / 8) * ch));
                case AV_CODEC_ID_PCM_BLURAY:
                    if(bps<4)
                        return 0;
                    return frame_bytes / ((FFALIGN(ch, 2) * bps) / 8);
                case AV_CODEC_ID_S302M:
                    return 2 * (frame_bytes / ((bps + 4) / 4)) / ch;
                }
            }
        }
    }

    return 0;
}

#if !HAVE_THREADS
int ff_thread_init(AVCodecContext *s)
{
    return -1;
}

#endif

unsigned int av_xiphlacing(unsigned char *s, unsigned int v)
{
    unsigned int n = 0;

    while (v >= 0xff) {
        *s++ = 0xff;
        v -= 0xff;
        n++;
    }
    *s = v;
    n++;
    return n;
}

int ff_match_2uint16(const uint16_t(*tab)[2], int size, int a, int b)
{
    int i;
    for (i = 0; i < size && !(tab[i][0] == a && tab[i][1] == b); i++) ;
    return i;
}

#if FF_API_MISSING_SAMPLE
FF_DISABLE_DEPRECATION_WARNINGS
void av_log_missing_feature(void *avc, const char *feature, int want_sample)
{
    av_log(avc, AV_LOG_WARNING, "%s is not implemented. Update your FFmpeg "
            "version to the newest one from Git. If the problem still "
            "occurs, it means that your file has a feature which has not "
            "been implemented.\n", feature);
    if(want_sample)
        av_log_ask_for_sample(avc, NULL);
}

void av_log_ask_for_sample(void *avc, const char *msg, ...)
{
    va_list argument_list;

    va_start(argument_list, msg);

    if (msg)
        av_vlog(avc, AV_LOG_WARNING, msg, argument_list);
    av_log(avc, AV_LOG_WARNING, "If you want to help, upload a sample "
            "of this file to ftp://upload.ffmpeg.org/MPlayer/incoming/ "
            "and contact the ffmpeg-devel mailing list.\n");

    va_end(argument_list);
}
FF_ENABLE_DEPRECATION_WARNINGS
#endif /* FF_API_MISSING_SAMPLE */

static AVHWAccel *first_hwaccel = NULL;
static AVHWAccel **last_hwaccel = &first_hwaccel;

void av_register_hwaccel(AVHWAccel *hwaccel)
{
    AVHWAccel **p = last_hwaccel;
    hwaccel->next = NULL;
    while(*p || avpriv_atomic_ptr_cas((void * volatile *)p, NULL, hwaccel))
        p = &(*p)->next;
    last_hwaccel = &hwaccel->next;
}

AVHWAccel *av_hwaccel_next(AVHWAccel *hwaccel)
{
    return hwaccel ? hwaccel->next : first_hwaccel;
}

AVHWAccel *ff_find_hwaccel(AVCodecContext *avctx)
{
    enum AVCodecID codec_id = avctx->codec->id;
    enum AVPixelFormat pix_fmt = avctx->pix_fmt;

    AVHWAccel *hwaccel = NULL;

    while ((hwaccel = av_hwaccel_next(hwaccel)))
        if (hwaccel->id == codec_id
            && hwaccel->pix_fmt == pix_fmt)
            return hwaccel;
    return NULL;
}

int av_lockmgr_register(int (*cb)(void **mutex, enum AVLockOp op))
{
    if (lockmgr_cb) {
        if (lockmgr_cb(&codec_mutex, AV_LOCK_DESTROY))
            return -1;
        if (lockmgr_cb(&avformat_mutex, AV_LOCK_DESTROY))
            return -1;
    }

    lockmgr_cb = cb;

    if (lockmgr_cb) {
        if (lockmgr_cb(&codec_mutex, AV_LOCK_CREATE))
            return -1;
        if (lockmgr_cb(&avformat_mutex, AV_LOCK_CREATE))
            return -1;
    }
    return 0;
}

int ff_lock_avcodec(AVCodecContext *log_ctx)
{
    if (lockmgr_cb) {
        if ((*lockmgr_cb)(&codec_mutex, AV_LOCK_OBTAIN))
            return -1;
    }
    entangled_thread_counter++;
    if (entangled_thread_counter != 1) {
        av_log(log_ctx, AV_LOG_ERROR, "Insufficient thread locking around avcodec_open/close()\n");
        if (!lockmgr_cb)
            av_log(log_ctx, AV_LOG_ERROR, "No lock manager is set, please see av_lockmgr_register()\n");
        ff_avcodec_locked = 1;
        ff_unlock_avcodec();
        return AVERROR(EINVAL);
    }
    av_assert0(!ff_avcodec_locked);
    ff_avcodec_locked = 1;
    return 0;
}

int ff_unlock_avcodec(void)
{
    av_assert0(ff_avcodec_locked);
    ff_avcodec_locked = 0;
    entangled_thread_counter--;
    if (lockmgr_cb) {
        if ((*lockmgr_cb)(&codec_mutex, AV_LOCK_RELEASE))
            return -1;
    }
    return 0;
}

int avpriv_lock_avformat(void)
{
    if (lockmgr_cb) {
        if ((*lockmgr_cb)(&avformat_mutex, AV_LOCK_OBTAIN))
            return -1;
    }
    return 0;
}

int avpriv_unlock_avformat(void)
{
    if (lockmgr_cb) {
        if ((*lockmgr_cb)(&avformat_mutex, AV_LOCK_RELEASE))
            return -1;
    }
    return 0;
}

unsigned int avpriv_toupper4(unsigned int x)
{
    return av_toupper(x & 0xFF) +
          (av_toupper((x >>  8) & 0xFF) << 8)  +
          (av_toupper((x >> 16) & 0xFF) << 16) +
          (av_toupper((x >> 24) & 0xFF) << 24);
}

int ff_thread_ref_frame(ThreadFrame *dst, ThreadFrame *src)
{
    int ret;

    dst->owner = src->owner;

    ret = av_frame_ref(dst->f, src->f);
    if (ret < 0)
        return ret;

    if (src->progress &&
        !(dst->progress = av_buffer_ref(src->progress))) {
        ff_thread_release_buffer(dst->owner, dst);
        return AVERROR(ENOMEM);
    }

    return 0;
}

#if !HAVE_THREADS

enum AVPixelFormat ff_thread_get_format(AVCodecContext *avctx, const enum AVPixelFormat *fmt)
{
    return avctx->get_format(avctx, fmt);
}

int ff_thread_get_buffer(AVCodecContext *avctx, ThreadFrame *f, int flags)
{
    f->owner = avctx;
    return ff_get_buffer(avctx, f->f, flags);
}

void ff_thread_release_buffer(AVCodecContext *avctx, ThreadFrame *f)
{
    av_frame_unref(f->f);
}

void ff_thread_finish_setup(AVCodecContext *avctx)
{
}

void ff_thread_report_progress(ThreadFrame *f, int progress, int field)
{
}

void ff_thread_await_progress(ThreadFrame *f, int progress, int field)
{
}

int ff_thread_can_start_frame(AVCodecContext *avctx)
{
    return 1;
}

int ff_alloc_entries(AVCodecContext *avctx, int count)
{
    return 0;
}

void ff_reset_entries(AVCodecContext *avctx)
{
}

void ff_thread_await_progress2(AVCodecContext *avctx, int field, int thread, int shift)
{
}

void ff_thread_report_progress2(AVCodecContext *avctx, int field, int thread, int n)
{
}

#endif

enum AVMediaType avcodec_get_type(enum AVCodecID codec_id)
{
    AVCodec *c= avcodec_find_decoder(codec_id);
    if(!c)
        c= avcodec_find_encoder(codec_id);
    if(c)
        return c->type;

    if (codec_id <= AV_CODEC_ID_NONE)
        return AVMEDIA_TYPE_UNKNOWN;
    else if (codec_id < AV_CODEC_ID_FIRST_AUDIO)
        return AVMEDIA_TYPE_VIDEO;
    else if (codec_id < AV_CODEC_ID_FIRST_SUBTITLE)
        return AVMEDIA_TYPE_AUDIO;
    else if (codec_id < AV_CODEC_ID_FIRST_UNKNOWN)
        return AVMEDIA_TYPE_SUBTITLE;

    return AVMEDIA_TYPE_UNKNOWN;
}

int avcodec_is_open(AVCodecContext *s)
{
    return !!s->internal;
}

int avpriv_bprint_to_extradata(AVCodecContext *avctx, struct AVBPrint *buf)
{
    int ret;
    char *str;

    ret = av_bprint_finalize(buf, &str);
    if (ret < 0)
        return ret;
    avctx->extradata = str;
    /* Note: the string is NUL terminated (so extradata can be read as a
     * string), but the ending character is not accounted in the size (in
     * binary formats you are likely not supposed to mux that character). When
     * extradata is copied, it is also padded with FF_INPUT_BUFFER_PADDING_SIZE
     * zeros. */
    avctx->extradata_size = buf->len;
    return 0;
}

const uint8_t *avpriv_find_start_code(const uint8_t *av_restrict p,
                                      const uint8_t *end,
                                      uint32_t *av_restrict state)
{
    int i;

    av_assert0(p <= end);
    if (p >= end)
        return end;

    for (i = 0; i < 3; i++) {
        uint32_t tmp = *state << 8;
        *state = tmp + *(p++);
        if (tmp == 0x100 || p == end)
            return p;
    }

    while (p < end) {
        if      (p[-1] > 1      ) p += 3;
        else if (p[-2]          ) p += 2;
        else if (p[-3]|(p[-1]-1)) p++;
        else {
            p++;
            break;
        }
    }

    p = FFMIN(p, end) - 4;
    *state = AV_RB32(p);

    return p + 4;
}
