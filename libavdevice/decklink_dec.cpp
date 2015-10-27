/*
 * Blackmagic DeckLink output
 * Copyright (c) 2013-2014 Luca Barbato, Deti Fliegl
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

#include <DeckLinkAPI.h>

#include <pthread.h>
#include <semaphore.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/imgutils.h"
}

#include "decklink_common.h"
#include "decklink_dec.h"

static void avpacket_queue_init(AVFormatContext *avctx, AVPacketQueue *q)
{
    memset(q, 0, sizeof(AVPacketQueue));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    q->avctx = avctx;
}

static void avpacket_queue_flush(AVPacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    pthread_mutex_lock(&q->mutex);
    for (pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_packet_unref(&pkt->pkt);
        av_freep(&pkt);
    }
    q->last_pkt   = NULL;
    q->first_pkt  = NULL;
    q->nb_packets = 0;
    q->size       = 0;
    pthread_mutex_unlock(&q->mutex);
}

static void avpacket_queue_end(AVPacketQueue *q)
{
    avpacket_queue_flush(q);
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static unsigned long long avpacket_queue_size(AVPacketQueue *q)
{
    unsigned long long size;
    pthread_mutex_lock(&q->mutex);
    size = q->size;
    pthread_mutex_unlock(&q->mutex);
    return size;
}

static int avpacket_queue_put(AVPacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;

    // Drop Packet if queue size is > 1GB
    if (avpacket_queue_size(q) >  1024 * 1024 * 1024 ) {
        av_log(q->avctx, AV_LOG_WARNING,  "Decklink input buffer overrun!\n");
        return -1;
    }
    /* duplicate the packet */
    if (av_dup_packet(pkt) < 0) {
        return -1;
    }

    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!pkt1) {
        return -1;
    }
    pkt1->pkt  = *pkt;
    pkt1->next = NULL;

    pthread_mutex_lock(&q->mutex);

    if (!q->last_pkt) {
        q->first_pkt = pkt1;
    } else {
        q->last_pkt->next = pkt1;
    }

    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size + sizeof(*pkt1);

    pthread_cond_signal(&q->cond);

    pthread_mutex_unlock(&q->mutex);
    return 0;
}

static int avpacket_queue_get(AVPacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    pthread_mutex_lock(&q->mutex);

    for (;; ) {
        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt) {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= pkt1->pkt.size + sizeof(*pkt1);
            *pkt     = pkt1->pkt;
            av_free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    return ret;
}

class decklink_input_callback : public IDeckLinkInputCallback
{
public:
        decklink_input_callback(AVFormatContext *_avctx);
        ~decklink_input_callback();

        virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
        virtual ULONG STDMETHODCALLTYPE AddRef(void);
        virtual ULONG STDMETHODCALLTYPE  Release(void);
        virtual HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents, IDeckLinkDisplayMode*, BMDDetectedVideoInputFormatFlags);
        virtual HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame*, IDeckLinkAudioInputPacket*);

private:
        ULONG           m_refCount;
        pthread_mutex_t m_mutex;
        AVFormatContext *avctx;
        decklink_ctx    *ctx;
        int no_video;
        int64_t initial_video_pts;
        int64_t initial_audio_pts;
};

decklink_input_callback::decklink_input_callback(AVFormatContext *_avctx) : m_refCount(0)
{
    avctx = _avctx;
    decklink_cctx       *cctx = (struct decklink_cctx *) avctx->priv_data;
    ctx = (struct decklink_ctx *) cctx->ctx;
    initial_audio_pts = initial_video_pts = AV_NOPTS_VALUE;
    pthread_mutex_init(&m_mutex, NULL);
}

decklink_input_callback::~decklink_input_callback()
{
    pthread_mutex_destroy(&m_mutex);
}

ULONG decklink_input_callback::AddRef(void)
{
    pthread_mutex_lock(&m_mutex);
    m_refCount++;
    pthread_mutex_unlock(&m_mutex);

    return (ULONG)m_refCount;
}

ULONG decklink_input_callback::Release(void)
{
    pthread_mutex_lock(&m_mutex);
    m_refCount--;
    pthread_mutex_unlock(&m_mutex);

    if (m_refCount == 0) {
        delete this;
        return 0;
    }

    return (ULONG)m_refCount;
}

HRESULT decklink_input_callback::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame *videoFrame, IDeckLinkAudioInputPacket *audioFrame)
{
    void *frameBytes;
    void *audioFrameBytes;
    BMDTimeValue frameTime;
    BMDTimeValue frameDuration;

    ctx->frameCount++;

    // Handle Video Frame
    if (videoFrame) {
        AVPacket pkt;
        AVCodecContext *c;
        av_init_packet(&pkt);
        c = ctx->video_st->codec;
        if (ctx->frameCount % 25 == 0) {
            unsigned long long qsize = avpacket_queue_size(&ctx->queue);
            av_log(avctx, AV_LOG_DEBUG,
                    "Frame received (#%lu) - Valid (%liB) - QSize %fMB\n",
                    ctx->frameCount,
                    videoFrame->GetRowBytes() * videoFrame->GetHeight(),
                    (double)qsize / 1024 / 1024);
        }

        videoFrame->GetBytes(&frameBytes);
        videoFrame->GetStreamTime(&frameTime, &frameDuration,
                                  ctx->video_st->time_base.den);

        if (videoFrame->GetFlags() & bmdFrameHasNoInputSource) {
            if (videoFrame->GetPixelFormat() == bmdFormat8BitYUV) {
            unsigned bars[8] = {
                0xEA80EA80, 0xD292D210, 0xA910A9A5, 0x90229035,
                0x6ADD6ACA, 0x51EF515A, 0x286D28EF, 0x10801080 };
            int width  = videoFrame->GetWidth();
            int height = videoFrame->GetHeight();
            unsigned *p = (unsigned *)frameBytes;

            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x += 2)
                    *p++ = bars[(x * 8) / width];
            }
            }

            if (!no_video) {
                av_log(avctx, AV_LOG_WARNING, "Frame received (#%lu) - No input signal detected "
                        "- Frames dropped %u\n", ctx->frameCount, ++ctx->dropped);
            }
            no_video = 1;
        } else {
            if (no_video) {
                av_log(avctx, AV_LOG_WARNING, "Frame received (#%lu) - Input returned "
                        "- Frames dropped %u\n", ctx->frameCount, ++ctx->dropped);
            }
            no_video = 0;
        }

        pkt.pts = frameTime / ctx->video_st->time_base.num;

        if (initial_video_pts == AV_NOPTS_VALUE) {
            initial_video_pts = pkt.pts;
        }

        pkt.pts -= initial_video_pts;
        pkt.dts = pkt.pts;

        pkt.duration = frameDuration;
        //To be made sure it still applies
        pkt.flags       |= AV_PKT_FLAG_KEY;
        pkt.stream_index = ctx->video_st->index;
        pkt.data         = (uint8_t *)frameBytes;
        pkt.size         = videoFrame->GetRowBytes() *
                           videoFrame->GetHeight();
        //fprintf(stderr,"Video Frame size %d ts %d\n", pkt.size, pkt.pts);
        c->frame_number++;
        if (avpacket_queue_put(&ctx->queue, &pkt) < 0) {
            ++ctx->dropped;
        }
    }

    // Handle Audio Frame
    if (audioFrame) {
        AVCodecContext *c;
        AVPacket pkt;
        BMDTimeValue audio_pts;
        av_init_packet(&pkt);

        c = ctx->audio_st->codec;
        //hack among hacks
        pkt.size = audioFrame->GetSampleFrameCount() * ctx->audio_st->codec->channels * (16 / 8);
        audioFrame->GetBytes(&audioFrameBytes);
        audioFrame->GetPacketTime(&audio_pts, ctx->audio_st->time_base.den);
        pkt.pts = audio_pts / ctx->audio_st->time_base.num;

        if (initial_audio_pts == AV_NOPTS_VALUE) {
            initial_audio_pts = pkt.pts;
        }

        pkt.pts -= initial_audio_pts;
        pkt.dts = pkt.pts;

        //fprintf(stderr,"Audio Frame size %d ts %d\n", pkt.size, pkt.pts);
        pkt.flags       |= AV_PKT_FLAG_KEY;
        pkt.stream_index = ctx->audio_st->index;
        pkt.data         = (uint8_t *)audioFrameBytes;

        c->frame_number++;
        if (avpacket_queue_put(&ctx->queue, &pkt) < 0) {
            ++ctx->dropped;
        }
    }

    return S_OK;
}

HRESULT decklink_input_callback::VideoInputFormatChanged(
    BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode *mode,
    BMDDetectedVideoInputFormatFlags)
{
    return S_OK;
}

static HRESULT decklink_start_input(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;

    ctx->input_callback = new decklink_input_callback(avctx);
    ctx->dli->SetCallback(ctx->input_callback);
    return ctx->dli->StartStreams();
}

extern "C" {

av_cold int ff_decklink_read_close(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;

    if (ctx->capture_started) {
        ctx->dli->StopStreams();
        ctx->dli->DisableVideoInput();
        ctx->dli->DisableAudioInput();
    }

    if (ctx->dli)
        ctx->dli->Release();
    if (ctx->dl)
        ctx->dl->Release();

    avpacket_queue_end(&ctx->queue);

    av_freep(&cctx->ctx);

    return 0;
}

av_cold int ff_decklink_read_header(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx;
    IDeckLinkDisplayModeIterator *itermode;
    IDeckLinkIterator *iter;
    IDeckLink *dl = NULL;
    AVStream *st;
    HRESULT result;
    char fname[1024];
    char *tmp;
    int mode_num = 0;

    ctx = (struct decklink_ctx *) av_mallocz(sizeof(struct decklink_ctx));
    if (!ctx)
        return AVERROR(ENOMEM);
    ctx->list_devices = cctx->list_devices;
    ctx->list_formats = cctx->list_formats;
    ctx->preroll      = cctx->preroll;
    cctx->ctx = ctx;

    iter = CreateDeckLinkIteratorInstance();
    if (!iter) {
        av_log(avctx, AV_LOG_ERROR, "Could not create DeckLink iterator\n");
        return AVERROR(EIO);
    }

    /* List available devices. */
    if (ctx->list_devices) {
        ff_decklink_list_devices(avctx);
        return AVERROR_EXIT;
    }

    strcpy (fname, avctx->filename);
    tmp=strchr (fname, '@');
    if (tmp != NULL) {
        mode_num = atoi (tmp+1);
        *tmp = 0;
    }

    /* Open device. */
    while (iter->Next(&dl) == S_OK) {
        const char *displayName;
        ff_decklink_get_display_name(dl, &displayName);
        if (!strcmp(fname, displayName)) {
            av_free((void *) displayName);
            ctx->dl = dl;
            break;
        }
        av_free((void *) displayName);
        dl->Release();
    }
    iter->Release();
    if (!ctx->dl) {
        av_log(avctx, AV_LOG_ERROR, "Could not open '%s'\n", fname);
        return AVERROR(EIO);
    }

    /* Get input device. */
    if (ctx->dl->QueryInterface(IID_IDeckLinkInput, (void **) &ctx->dli) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not open output device from '%s'\n",
               avctx->filename);
        ctx->dl->Release();
        return AVERROR(EIO);
    }

    /* List supported formats. */
    if (ctx->list_formats) {
        ff_decklink_list_formats(avctx, DIRECTION_IN);
        ctx->dli->Release();
        ctx->dl->Release();
        return AVERROR_EXIT;
    }

    if (ctx->dli->GetDisplayModeIterator(&itermode) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
        ctx->dl->Release();
        return AVERROR(EIO);
    }

    if (mode_num > 0) {
        if (ff_decklink_set_format(avctx, DIRECTION_IN, mode_num) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Could not set mode %d for %s\n", mode_num, fname);
            goto error;
        }
    }

    itermode->Release();

    /* Setup streams. */
    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        av_log(avctx, AV_LOG_ERROR, "Cannot add stream\n");
        goto error;
    }
    st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id    = AV_CODEC_ID_PCM_S16LE;
    st->codec->sample_rate = bmdAudioSampleRate48kHz;
    st->codec->channels    = 2;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */
    ctx->audio_st=st;

    st = avformat_new_stream(avctx, NULL);
    if (!st) {
        av_log(avctx, AV_LOG_ERROR, "Cannot add stream\n");
        goto error;
    }
    st->codec->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codec->width       = ctx->bmd_width;
    st->codec->height      = ctx->bmd_height;

    st->codec->time_base.den      = ctx->bmd_tb_den;
    st->codec->time_base.num      = ctx->bmd_tb_num;
    st->codec->bit_rate    = avpicture_get_size(st->codec->pix_fmt, ctx->bmd_width, ctx->bmd_height) * 1/av_q2d(st->codec->time_base) * 8;

    if (cctx->v210) {
        st->codec->codec_id    = AV_CODEC_ID_V210;
        st->codec->codec_tag   = MKTAG('V', '2', '1', '0');
    } else {
        st->codec->codec_id    = AV_CODEC_ID_RAWVIDEO;
        st->codec->pix_fmt     = AV_PIX_FMT_UYVY422;
        st->codec->codec_tag   = MKTAG('U', 'Y', 'V', 'Y');
    }

    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    ctx->video_st=st;

    result = ctx->dli->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType16bitInteger, 2);

    if (result != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot enable audio input\n");
        goto error;
    }

    result = ctx->dli->EnableVideoInput(ctx->bmd_mode,
                                        cctx->v210 ? bmdFormat10BitYUV : bmdFormat8BitYUV,
                                        bmdVideoInputFlagDefault);

    if (result != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot enable video input\n");
        goto error;
    }

    avpacket_queue_init (avctx, &ctx->queue);

    if (decklink_start_input (avctx) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot start input stream\n");
        goto error;
    }

    return 0;

error:

    ctx->dli->Release();
    ctx->dl->Release();

    return AVERROR(EIO);
}

int ff_decklink_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    AVFrame *frame = ctx->video_st->codec->coded_frame;

    avpacket_queue_get(&ctx->queue, pkt, 1);
    if (frame && (ctx->bmd_field_dominance == bmdUpperFieldFirst || ctx->bmd_field_dominance == bmdLowerFieldFirst)) {
        frame->interlaced_frame = 1;
        if (ctx->bmd_field_dominance == bmdUpperFieldFirst) {
            frame->top_field_first = 1;
        }
    }

    return 0;
}

} /* extern "C" */
