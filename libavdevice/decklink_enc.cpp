/*
 * Blackmagic DeckLink output
 * Copyright (c) 2013-2014 Ramiro Polla
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
#ifdef _WIN32
#include <DeckLinkAPI_i.c>
typedef unsigned long buffercount_type;
#else
#include <DeckLinkAPIDispatch.cpp>
typedef uint32_t buffercount_type;
#endif

#include <pthread.h>
#include <semaphore.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavutil/imgutils.h"
}

#include "decklink_enc.h"

class decklink_callback;

struct decklink_ctx {
    /* DeckLink SDK interfaces */
    IDeckLink *dl;
    IDeckLinkOutput *dlo;
    decklink_callback *callback;

    /* DeckLink mode information */
    IDeckLinkDisplayModeIterator *itermode;
    BMDTimeValue bmd_tb_den;
    BMDTimeValue bmd_tb_num;
    BMDDisplayMode bmd_mode;
    int bmd_width;
    int bmd_height;

    /* Streams present */
    int audio;
    int video;

    /* Status */
    int playback_started;
    int64_t last_pts;

    /* Options */
    int list_devices;
    int list_formats;
    double preroll;

    int frames_preroll;
    int frames_buffer;

    sem_t semaphore;

    int channels;
};

/* DeckLink callback class declaration */
class decklink_frame : public IDeckLinkVideoFrame
{
public:
    decklink_frame(struct decklink_ctx *ctx, AVFrame *avframe, long width,
                   long height, void *buffer) :
                   _ctx(ctx), _avframe(avframe), _width(width),
                   _height(height), _buffer(buffer), _refs(0) { }

    virtual long           STDMETHODCALLTYPE GetWidth      (void)          { return _width; }
    virtual long           STDMETHODCALLTYPE GetHeight     (void)          { return _height; }
    virtual long           STDMETHODCALLTYPE GetRowBytes   (void)          { return _width<<1; }
    virtual BMDPixelFormat STDMETHODCALLTYPE GetPixelFormat(void)          { return bmdFormat8BitYUV; }
    virtual BMDFrameFlags  STDMETHODCALLTYPE GetFlags      (void)          { return bmdVideoOutputFlagDefault; }
    virtual HRESULT        STDMETHODCALLTYPE GetBytes      (void **buffer) { *buffer = _buffer; return S_OK; }

    virtual HRESULT STDMETHODCALLTYPE GetTimecode     (BMDTimecodeFormat format, IDeckLinkTimecode **timecode) { return S_FALSE; }
    virtual HRESULT STDMETHODCALLTYPE GetAncillaryData(IDeckLinkVideoFrameAncillary **ancillary)               { return S_FALSE; }

    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
    virtual ULONG   STDMETHODCALLTYPE AddRef(void)                            { return ++_refs; }
    virtual ULONG   STDMETHODCALLTYPE Release(void)                           { if (!--_refs) delete this; return _refs; }

    struct decklink_ctx *_ctx;
    AVFrame *_avframe;

private:
    long  _width;
    long  _height;
    void *_buffer;
    int   _refs;
};

class decklink_callback : public IDeckLinkVideoOutputCallback
{
public:
    virtual HRESULT STDMETHODCALLTYPE ScheduledFrameCompleted(IDeckLinkVideoFrame *_frame, BMDOutputFrameCompletionResult result)
    {
        decklink_frame *frame = static_cast<decklink_frame *>(_frame);
        struct decklink_ctx *ctx = frame->_ctx;
        AVFrame *avframe = frame->_avframe;

        av_frame_free(&avframe);

        sem_post(&ctx->semaphore);

        return S_OK;
    }
    virtual HRESULT STDMETHODCALLTYPE ScheduledPlaybackHasStopped(void)       { return S_OK; }
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, LPVOID *ppv) { return E_NOINTERFACE; }
    virtual ULONG   STDMETHODCALLTYPE AddRef(void)                            { return 1; }
    virtual ULONG   STDMETHODCALLTYPE Release(void)                           { return 1; }
};

#ifdef _WIN32
static IDeckLinkIterator *CreateDeckLinkIteratorInstance(void)
{
    IDeckLinkIterator *iter;

    if (CoInitialize(NULL) != S_OK) {
        av_log(NULL, AV_LOG_ERROR, "COM initialization failed.\n");
        return NULL;
    }

    if (CoCreateInstance(CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL,
                         IID_IDeckLinkIterator, (void**) &iter) != S_OK) {
        av_log(NULL, AV_LOG_ERROR, "DeckLink drivers not installed.\n");
        return NULL;
    }

    return iter;
}
#endif

/* free() is needed for a string returned by the DeckLink SDL. */
#undef free

#ifdef _WIN32
static char *dup_wchar_to_utf8(wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = (char *) av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}
#define DECKLINK_STR    OLECHAR *
#define DECKLINK_STRDUP dup_wchar_to_utf8
#else
#define DECKLINK_STR    const char *
#define DECKLINK_STRDUP av_strdup
#endif

static HRESULT IDeckLink_GetDisplayName(IDeckLink *This, const char **displayName)
{
    DECKLINK_STR tmpDisplayName;
    HRESULT hr = This->GetDisplayName(&tmpDisplayName);
    if (hr != S_OK)
        return hr;
    *displayName = DECKLINK_STRDUP(tmpDisplayName);
    free((void *) tmpDisplayName);
    return hr;
}

static int decklink_set_format(struct decklink_ctx *ctx,
                               int width, int height,
                               int tb_num, int tb_den)
{
    BMDDisplayModeSupport support;
    IDeckLinkDisplayMode *mode;

    if (tb_num == 1) {
        tb_num *= 1000;
        tb_den *= 1000;
    }
    ctx->bmd_mode = bmdModeUnknown;
    while ((ctx->bmd_mode == bmdModeUnknown) && ctx->itermode->Next(&mode) == S_OK) {
        BMDTimeValue bmd_tb_num, bmd_tb_den;
        int bmd_width  = mode->GetWidth();
        int bmd_height = mode->GetHeight();

        mode->GetFrameRate(&bmd_tb_num, &bmd_tb_den);

        if (bmd_width == width && bmd_height == height &&
            bmd_tb_num == tb_num && bmd_tb_den == tb_den) {
            ctx->bmd_mode   = mode->GetDisplayMode();
            ctx->bmd_width  = bmd_width;
            ctx->bmd_height = bmd_height;
            ctx->bmd_tb_den = bmd_tb_den;
            ctx->bmd_tb_num = bmd_tb_num;
        }

        mode->Release();
    }
    if (ctx->bmd_mode == bmdModeUnknown)
        return -1;
    if (ctx->dlo->DoesSupportVideoMode(ctx->bmd_mode, bmdFormat8BitYUV,
                                       bmdVideoOutputFlagDefault,
                                       &support, NULL) != S_OK)
        return -1;
    if (support == bmdDisplayModeSupported)
        return 0;

    return -1;
}

static int decklink_setup_video(AVFormatContext *avctx, AVStream *st)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    AVCodecContext *c = st->codec;

    if (ctx->video) {
        av_log(avctx, AV_LOG_ERROR, "Only one video stream is supported!\n");
        return -1;
    }

    if (c->pix_fmt != AV_PIX_FMT_UYVY422) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported pixel format!"
               " Only AV_PIX_FMT_UYVY422 is supported.\n");
        return -1;
    }
    if (decklink_set_format(ctx, c->width, c->height,
                            c->time_base.num, c->time_base.den)) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported video size or framerate!"
               " Check available formats with -list_formats 1.\n");
        return -1;
    }
    if (ctx->dlo->EnableVideoOutput(ctx->bmd_mode,
                                    bmdVideoOutputFlagDefault) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enable video output!\n");
        return -1;
    }

    /* Set callback. */
    ctx->callback = new decklink_callback();
    ctx->dlo->SetScheduledFrameCompletionCallback(ctx->callback);

    /* Start video semaphore. */
    ctx->frames_preroll = c->time_base.den * ctx->preroll;
    if (c->time_base.den > 1000)
        ctx->frames_preroll /= 1000;

    /* Buffer twice as many frames as the preroll. */
    ctx->frames_buffer = ctx->frames_preroll * 2;
    ctx->frames_buffer = FFMIN(ctx->frames_buffer, 60);
    sem_init(&ctx->semaphore, 0, ctx->frames_buffer);

    /* The device expects the framerate to be fixed. */
    avpriv_set_pts_info(st, 64, c->time_base.num, c->time_base.den);

    ctx->video = 1;

    return 0;
}

static int decklink_setup_audio(AVFormatContext *avctx, AVStream *st)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    AVCodecContext *c = st->codec;

    if (ctx->audio) {
        av_log(avctx, AV_LOG_ERROR, "Only one audio stream is supported!\n");
        return -1;
    }
    if (c->sample_rate != 48000) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported sample rate!"
               " Only 48kHz is supported.\n");
        return -1;
    }
    if (c->channels != 2 && c->channels != 8) {
        av_log(avctx, AV_LOG_ERROR, "Unsupported number of channels!"
               " Only stereo and 7.1 are supported.\n");
        return -1;
    }
    if (ctx->dlo->EnableAudioOutput(bmdAudioSampleRate48kHz,
                                    bmdAudioSampleType16bitInteger,
                                    c->channels,
                                    bmdAudioOutputStreamTimestamped) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not enable audio output!\n");
        return -1;
    }
    if (ctx->dlo->BeginAudioPreroll() != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not begin audio preroll!\n");
        return -1;
    }

    /* The device expects the sample rate to be fixed. */
    avpriv_set_pts_info(st, 64, 1, c->sample_rate);
    ctx->channels = c->channels;

    ctx->audio = 1;

    return 0;
}

av_cold int ff_decklink_write_trailer(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;

    if (ctx->playback_started) {
        BMDTimeValue actual;
        ctx->dlo->StopScheduledPlayback(ctx->last_pts * ctx->bmd_tb_num,
                                        &actual, ctx->bmd_tb_den);
        ctx->dlo->DisableVideoOutput();
        if (ctx->audio)
            ctx->dlo->DisableAudioOutput();
    }

    if (ctx->dlo)
        ctx->dlo->Release();
    if (ctx->dl)
        ctx->dl->Release();

    if (ctx->callback)
        delete ctx->callback;

    sem_destroy(&ctx->semaphore);

    av_freep(&cctx->ctx);

    return 0;
}

static int decklink_write_video_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    AVPicture *avpicture = (AVPicture *) pkt->data;
    AVFrame *avframe, *tmp;
    decklink_frame *frame;
    buffercount_type buffered;
    HRESULT hr;

    /* HACK while av_uncoded_frame() isn't implemented */
    int ret;

    tmp = av_frame_alloc();
    if (!tmp)
        return AVERROR(ENOMEM);
    tmp->format = AV_PIX_FMT_UYVY422;
    tmp->width  = ctx->bmd_width;
    tmp->height = ctx->bmd_height;
    ret = av_frame_get_buffer(tmp, 32);
    if (ret < 0) {
        av_frame_free(&tmp);
        return ret;
    }
    av_image_copy(tmp->data, tmp->linesize, (const uint8_t **) avpicture->data,
                  avpicture->linesize, (AVPixelFormat) tmp->format, tmp->width,
                  tmp->height);
    avframe = av_frame_clone(tmp);
    av_frame_free(&tmp);
    if (!avframe) {
        av_log(avctx, AV_LOG_ERROR, "Could not clone video frame.\n");
        return AVERROR(EIO);
    }
    /* end HACK */

    frame = new decklink_frame(ctx, avframe, ctx->bmd_width, ctx->bmd_height,
                               (void *) avframe->data[0]);
    if (!frame) {
        av_log(avctx, AV_LOG_ERROR, "Could not create new frame.\n");
        return AVERROR(EIO);
    }

    /* Always keep at most one second of frames buffered. */
    sem_wait(&ctx->semaphore);

    /* Schedule frame for playback. */
    hr = ctx->dlo->ScheduleVideoFrame((struct IDeckLinkVideoFrame *) frame,
                                      pkt->pts * ctx->bmd_tb_num,
                                      ctx->bmd_tb_num, ctx->bmd_tb_den);
    if (hr != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not schedule video frame."
                " error %08x.\n", (uint32_t) hr);
        frame->Release();
        return AVERROR(EIO);
    }

    ctx->dlo->GetBufferedVideoFrameCount(&buffered);
    av_log(avctx, AV_LOG_DEBUG, "Buffered video frames: %d.\n", (int) buffered);
    if (pkt->pts > 2 && buffered <= 2)
        av_log(avctx, AV_LOG_WARNING, "There are not enough buffered video frames."
               " Video may misbehave!\n");

    /* Preroll video frames. */
    if (!ctx->playback_started && pkt->pts > ctx->frames_preroll) {
        av_log(avctx, AV_LOG_DEBUG, "Ending audio preroll.\n");
        if (ctx->audio && ctx->dlo->EndAudioPreroll() != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not end audio preroll!\n");
            return AVERROR(EIO);
        }
        av_log(avctx, AV_LOG_DEBUG, "Starting scheduled playback.\n");
        if (ctx->dlo->StartScheduledPlayback(0, ctx->bmd_tb_den, 1.0) != S_OK) {
            av_log(avctx, AV_LOG_ERROR, "Could not start scheduled playback!\n");
            return AVERROR(EIO);
        }
        ctx->playback_started = 1;
    }

    return 0;
}

static int decklink_write_audio_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    int sample_count = pkt->size / (ctx->channels << 1);
    buffercount_type buffered;

    ctx->dlo->GetBufferedAudioSampleFrameCount(&buffered);
    if (pkt->pts > 1 && !buffered)
        av_log(avctx, AV_LOG_WARNING, "There's no buffered audio."
               " Audio will misbehave!\n");

    if (ctx->dlo->ScheduleAudioSamples(pkt->data, sample_count, pkt->pts,
                                       bmdAudioSampleRate48kHz, NULL) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not schedule audio samples.\n");
        return AVERROR(EIO);
    }

    return 0;
}

extern "C" {

av_cold int ff_decklink_write_header(AVFormatContext *avctx)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx;
    IDeckLinkIterator *iter;
    IDeckLink *dl = NULL;
    unsigned int n;

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
        av_log(avctx, AV_LOG_INFO, "Blackmagic DeckLink devices:\n");
        while (iter->Next(&dl) == S_OK) {
            const char *displayName;
            IDeckLink_GetDisplayName(dl, &displayName);
            av_log(avctx, AV_LOG_INFO, "\t'%s'\n", displayName);
            av_free((void *) displayName);
            dl->Release();
        }
        iter->Release();
        return AVERROR_EXIT;
    }

    /* Open device. */
    while (iter->Next(&dl) == S_OK) {
        const char *displayName;
        IDeckLink_GetDisplayName(dl, &displayName);
        if (!strcmp(avctx->filename, displayName)) {
            av_free((void *) displayName);
            ctx->dl = dl;
            break;
        }
        av_free((void *) displayName);
        dl->Release();
    }
    iter->Release();
    if (!ctx->dl) {
        av_log(avctx, AV_LOG_ERROR, "Could not open '%s'\n", avctx->filename);
        return AVERROR(EIO);
    }

    /* Get output device. */
    if (ctx->dl->QueryInterface(IID_IDeckLinkOutput, (void **) &ctx->dlo) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not open output device from '%s'\n",
               avctx->filename);
        ctx->dl->Release();
        return AVERROR(EIO);
    }

    if (ctx->dlo->GetDisplayModeIterator(&ctx->itermode) != S_OK) {
        av_log(avctx, AV_LOG_ERROR, "Could not get Display Mode Iterator\n");
        ctx->dl->Release();
        return AVERROR(EIO);
    }

    /* List supported formats. */
    if (ctx->list_formats) {
        IDeckLinkDisplayMode *mode;

        av_log(avctx, AV_LOG_INFO, "Supported formats for '%s':\n",
               avctx->filename);
        while (ctx->itermode->Next(&mode) == S_OK) {
            BMDTimeValue tb_num, tb_den;
            mode->GetFrameRate(&tb_num, &tb_den);
            av_log(avctx, AV_LOG_INFO, "\t%ldx%ld at %d/%d fps",
                   mode->GetWidth(), mode->GetHeight(),
                   (int) tb_den, (int) tb_num);
            switch (mode->GetFieldDominance()) {
            case bmdLowerFieldFirst:
            av_log(avctx, AV_LOG_INFO, " (interlaced, lower field first)"); break;
            case bmdUpperFieldFirst:
            av_log(avctx, AV_LOG_INFO, " (interlaced, upper field first)"); break;
            }
            av_log(avctx, AV_LOG_INFO, "\n");
            mode->Release();
        }

        ctx->itermode->Release();
        ctx->dlo->Release();
        ctx->dl->Release();
        return AVERROR_EXIT;
    }

    /* Setup streams. */
    for (n = 0; n < avctx->nb_streams; n++) {
        AVStream *st = avctx->streams[n];
        AVCodecContext *c = st->codec;
        if        (c->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (decklink_setup_audio(avctx, st))
                goto error;
        } else if (c->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (decklink_setup_video(avctx, st))
                goto error;
        } else {
            av_log(avctx, AV_LOG_ERROR, "Unsupported stream type.\n");
            goto error;
        }
    }
    ctx->itermode->Release();

    return 0;

error:

    ctx->dlo->Release();
    ctx->dl->Release();

    return AVERROR(EIO);
}

int ff_decklink_write_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    struct decklink_cctx *cctx = (struct decklink_cctx *) avctx->priv_data;
    struct decklink_ctx *ctx = (struct decklink_ctx *) cctx->ctx;
    AVStream *st = avctx->streams[pkt->stream_index];

    ctx->last_pts = FFMAX(ctx->last_pts, pkt->pts);

    if      (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        return decklink_write_video_packet(avctx, pkt);
    else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO)
        return decklink_write_audio_packet(avctx, pkt);

    return AVERROR(EIO);
}

} /* extern "C" */
