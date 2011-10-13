/*
 * Interface to the Android Stagefright library for
 * H/W accelerated H.264 decoding
 *
 * Copyright (C) 2011 Mohamed Naufal
 * Copyright (C) 2011 Martin Storsjö
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

#include <binder/ProcessState.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>
#include <utils/List.h>
#include <new>

extern "C" {
#include "avcodec.h"
#include "libavutil/imgutils.h"
}

#define OMX_QCOM_COLOR_FormatYVU420SemiPlanar 0x7FA30C00

using namespace android;

struct Frame {
    status_t status;
    size_t size;
    int64_t time;
    int key;
    uint8_t *buffer;
    MediaBuffer* mbuffer;
    int32_t w, h;
};

class CustomSource;

struct StagefrightContext {
    AVCodecContext *avctx;
    AVBitStreamFilterContext *bsfc;
    uint8_t* orig_extradata;
    int orig_extradata_size;
    sp<MediaSource> *source;
    List<Frame*> *in_queue, *out_queue;
    pthread_mutex_t in_mutex, out_mutex;
    pthread_cond_t condition;
    pthread_t decode_thread_id;

    Frame *end_frame;
    bool source_done;
    volatile sig_atomic_t thread_started, thread_exited, stop_decode;

    AVFrame ret_frame;

    uint8_t *dummy_buf;
    int dummy_bufsize;

    OMXClient *client;
    sp<MediaSource> *decoder;
    const char *decoder_component;
};

class CustomSource : public MediaSource {
public:
    CustomSource(AVCodecContext *avctx, sp<MetaData> meta) {
        s = (StagefrightContext*)avctx->priv_data;
        source_meta = meta;
        frame_size  = (avctx->width * avctx->height * 3) / 2;
        buf_group.add_buffer(new MediaBuffer(frame_size));
    }

    virtual sp<MetaData> getFormat() {
        return source_meta;
    }

    virtual status_t start(MetaData *params) {
        return OK;
    }

    virtual status_t stop() {
        return OK;
    }

    virtual status_t read(MediaBuffer **buffer,
                          const MediaSource::ReadOptions *options) {
        Frame *frame;
        status_t ret;

        if (s->thread_exited)
            return ERROR_END_OF_STREAM;
        pthread_mutex_lock(&s->in_mutex);

        while (s->in_queue->empty())
            pthread_cond_wait(&s->condition, &s->in_mutex);

        frame = *s->in_queue->begin();
        ret = frame->status;

        if (ret == OK) {
            ret = buf_group.acquire_buffer(buffer);
            if (ret == OK) {
                memcpy((*buffer)->data(), frame->buffer, frame->size);
                (*buffer)->set_range(0, frame->size);
                (*buffer)->meta_data()->clear();
                (*buffer)->meta_data()->setInt32(kKeyIsSyncFrame,frame->key);
                (*buffer)->meta_data()->setInt64(kKeyTime, frame->time);
            } else {
                av_log(s->avctx, AV_LOG_ERROR, "Failed to acquire MediaBuffer\n");
            }
            av_freep(&frame->buffer);
        }

        s->in_queue->erase(s->in_queue->begin());
        pthread_mutex_unlock(&s->in_mutex);

        av_freep(&frame);
        return ret;
    }

private:
    MediaBufferGroup buf_group;
    sp<MetaData> source_meta;
    StagefrightContext *s;
    int frame_size;
};

void* decode_thread(void *arg)
{
    AVCodecContext *avctx = (AVCodecContext*)arg;
    StagefrightContext *s = (StagefrightContext*)avctx->priv_data;
    Frame* frame;
    MediaBuffer *buffer;
    int decode_done = 0;
    do {
        buffer = NULL;
        frame = (Frame*)av_mallocz(sizeof(Frame));
        if (!frame) {
            frame         = s->end_frame;
            frame->status = AVERROR(ENOMEM);
            decode_done   = 1;
            s->end_frame  = NULL;
        } else {
            frame->status = (*s->decoder)->read(&buffer);
            if (frame->status == OK) {
                sp<MetaData> outFormat = (*s->decoder)->getFormat();
                outFormat->findInt32(kKeyWidth , &frame->w);
                outFormat->findInt32(kKeyHeight, &frame->h);
                frame->size    = buffer->range_length();
                frame->mbuffer = buffer;
            } else if (frame->status == INFO_FORMAT_CHANGED) {
                if (buffer)
                    buffer->release();
                av_free(frame);
                continue;
            } else {
                decode_done = 1;
            }
        }
        while (true) {
            pthread_mutex_lock(&s->out_mutex);
            if (s->out_queue->size() >= 10) {
                pthread_mutex_unlock(&s->out_mutex);
                usleep(10000);
                continue;
            }
            break;
        }
        s->out_queue->push_back(frame);
        pthread_mutex_unlock(&s->out_mutex);
    } while (!decode_done && !s->stop_decode);

    s->thread_exited = true;

    return 0;
}

static av_cold int Stagefright_init(AVCodecContext *avctx)
{
    StagefrightContext *s = (StagefrightContext*)avctx->priv_data;
    sp<MetaData> meta, outFormat;
    int32_t colorFormat = 0;
    int ret;

    if (!avctx->extradata || !avctx->extradata_size || avctx->extradata[0] != 1)
        return -1;

    s->avctx = avctx;
    s->bsfc  = av_bitstream_filter_init("h264_mp4toannexb");
    if (!s->bsfc) {
        av_log(avctx, AV_LOG_ERROR, "Cannot open the h264_mp4toannexb BSF!\n");
        return -1;
    }

    s->orig_extradata_size = avctx->extradata_size;
    s->orig_extradata = (uint8_t*) av_mallocz(avctx->extradata_size +
                                              FF_INPUT_BUFFER_PADDING_SIZE);
    if (!s->orig_extradata) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    memcpy(s->orig_extradata, avctx->extradata, avctx->extradata_size);

    meta = new MetaData;
    if (meta == NULL) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
    meta->setInt32(kKeyWidth, avctx->width);
    meta->setInt32(kKeyHeight, avctx->height);
    meta->setData(kKeyAVCC, kTypeAVCC, avctx->extradata, avctx->extradata_size);

    android::ProcessState::self()->startThreadPool();

    s->source    = new sp<MediaSource>();
    *s->source   = new CustomSource(avctx, meta);
    s->in_queue  = new List<Frame*>;
    s->out_queue = new List<Frame*>;
    s->client    = new OMXClient;
    s->end_frame = (Frame*)av_mallocz(sizeof(Frame));
    if (s->source == NULL || !s->in_queue || !s->out_queue || !s->client ||
        !s->end_frame) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    if (s->client->connect() !=  OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot connect OMX client\n");
        ret = -1;
        goto fail;
    }

    s->decoder  = new sp<MediaSource>();
    *s->decoder = OMXCodec::Create(s->client->interface(), meta,
                                  false, *s->source, NULL,
                                  OMXCodec::kClientNeedsFramebuffer);
    if ((*s->decoder)->start() !=  OK) {
        av_log(avctx, AV_LOG_ERROR, "Cannot start decoder\n");
        ret = -1;
        s->client->disconnect();
        goto fail;
    }

    outFormat = (*s->decoder)->getFormat();
    outFormat->findInt32(kKeyColorFormat, &colorFormat);
    if (colorFormat == OMX_QCOM_COLOR_FormatYVU420SemiPlanar ||
        colorFormat == OMX_COLOR_FormatYUV420SemiPlanar)
        avctx->pix_fmt = PIX_FMT_NV21;
    else
        avctx->pix_fmt = PIX_FMT_YUV420P;

    outFormat->findCString(kKeyDecoderComponent, &s->decoder_component);
    if (s->decoder_component)
        s->decoder_component = av_strdup(s->decoder_component);

    pthread_mutex_init(&s->in_mutex, NULL);
    pthread_mutex_init(&s->out_mutex, NULL);
    pthread_cond_init(&s->condition, NULL);
    return 0;

fail:
    av_bitstream_filter_close(s->bsfc);
    av_freep(&s->orig_extradata);
    av_freep(&s->end_frame);
    delete s->in_queue;
    delete s->out_queue;
    delete s->client;
    return ret;
}

static int Stagefright_decode_frame(AVCodecContext *avctx, void *data,
                                    int *data_size, AVPacket *avpkt)
{
    StagefrightContext *s = (StagefrightContext*)avctx->priv_data;
    Frame *frame;
    MediaBuffer *mbuffer;
    status_t status;
    size_t size;
    uint8_t *buf;
    const uint8_t *src_data[3];
    int w, h;
    int src_linesize[3];
    int orig_size = avpkt->size;
    AVPacket pkt = *avpkt;
    int ret;

    if (!s->thread_started) {
        pthread_create(&s->decode_thread_id, NULL, &decode_thread, avctx);
        s->thread_started = true;
    }

    if (avpkt && avpkt->data) {
        av_bitstream_filter_filter(s->bsfc, avctx, NULL, &pkt.data, &pkt.size,
                                   avpkt->data, avpkt->size, avpkt->flags & AV_PKT_FLAG_KEY);
        avpkt = &pkt;
    }

    if (!s->source_done) {
        if(!s->dummy_buf) {
            s->dummy_buf = (uint8_t*)av_malloc(avpkt->size);
            if (!s->dummy_buf)
                return AVERROR(ENOMEM);
            s->dummy_bufsize = avpkt->size;
            memcpy(s->dummy_buf, avpkt->data, avpkt->size);
        }

        frame = (Frame*)av_mallocz(sizeof(Frame));
        if (avpkt->data) {
            frame->status  = OK;
            frame->size    = avpkt->size;
            // Stagefright can't handle negative timestamps -
            // if needed, work around this by offsetting them manually?
            if (avpkt->pts >= 0)
                frame->time    = avpkt->pts;
            frame->key     = avpkt->flags & AV_PKT_FLAG_KEY ? 1 : 0;
            frame->buffer  = (uint8_t*)av_malloc(avpkt->size);
            if (!frame->buffer) {
                av_freep(&frame);
                return AVERROR(ENOMEM);
            }
            uint8_t *ptr = avpkt->data;
            // The OMX.SEC decoder fails without this.
            if (avpkt->size == orig_size + avctx->extradata_size) {
                ptr += avctx->extradata_size;
                frame->size = orig_size;
            }
            memcpy(frame->buffer, ptr, orig_size);
        } else {
            frame->status  = ERROR_END_OF_STREAM;
            s->source_done = true;
        }

        while (true) {
            if (s->thread_exited) {
                s->source_done = true;
                break;
            }
            pthread_mutex_lock(&s->in_mutex);
            if (s->in_queue->size() >= 10) {
                pthread_mutex_unlock(&s->in_mutex);
                usleep(10000);
                continue;
            }
            s->in_queue->push_back(frame);
            pthread_cond_signal(&s->condition);
            pthread_mutex_unlock(&s->in_mutex);
            break;
        }
    }
    while (true) {
        pthread_mutex_lock(&s->out_mutex);
        if (!s->out_queue->empty()) break;
        pthread_mutex_unlock(&s->out_mutex);
        if (s->source_done) {
            usleep(10000);
            continue;
        } else {
            return orig_size;
        }
    }

    frame = *s->out_queue->begin();
    s->out_queue->erase(s->out_queue->begin());
    pthread_mutex_unlock(&s->out_mutex);

    mbuffer = frame->mbuffer;
    status  = frame->status;
    size    = frame->size;
    w       = frame->w;
    h       = frame->h;
    av_freep(&frame);

    if (status == ERROR_END_OF_STREAM)
        return 0;
    if (status != OK) {
        if (status == AVERROR(ENOMEM))
            return status;
        av_log(avctx, AV_LOG_ERROR, "Decode failed: %x\n", status);
        return -1;
    }

    // The OMX.SEC decoder doesn't signal the modified width/height
    if (s->decoder_component && !strncmp(s->decoder_component, "OMX.SEC", 7) &&
        (w & 15 || h & 15)) {
        if (((w + 15)&~15) * ((h + 15)&~15) * 3/2 == size) {
            w = (w + 15)&~15;
            h = (h + 15)&~15;
        }
    }

    if (!avctx->width || !avctx->height || avctx->width > w || avctx->height > h) {
        avctx->width  = w;
        avctx->height = h;
    }

    ret = avctx->reget_buffer(avctx, &s->ret_frame);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "reget buffer() failed\n");
        goto end;
    }

    src_linesize[0] = w;
    if (avctx->pix_fmt == PIX_FMT_YUV420P)
        src_linesize[1] = src_linesize[2] = w/2;
    else if (avctx->pix_fmt == PIX_FMT_NV21)
        src_linesize[1] = w;

    buf = (uint8_t*)mbuffer->data();
    src_data[0] = buf;
    src_data[1] = buf + src_linesize[0] * h;
    src_data[2] = src_data[1] + src_linesize[1] * h/2;
    av_image_copy(s->ret_frame.data, s->ret_frame.linesize,
                  src_data, src_linesize,
                  avctx->pix_fmt, avctx->width, avctx->height);

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = s->ret_frame;
    ret = orig_size;
end:
    mbuffer->release();
    return ret;
}

static av_cold int Stagefright_close(AVCodecContext *avctx)
{
    StagefrightContext *s = (StagefrightContext*)avctx->priv_data;
    Frame *frame;

    if (s->thread_started) {
        if (!s->thread_exited) {
            s->stop_decode = 1;

            // Make sure decode_thread() doesn't get stuck
            pthread_mutex_lock(&s->out_mutex);
            while (!s->out_queue->empty()) {
                frame = *s->out_queue->begin();
                s->out_queue->erase(s->out_queue->begin());
                if (frame->size)
                    frame->mbuffer->release();
                av_freep(&frame);
            }
            pthread_mutex_unlock(&s->out_mutex);

            // Feed a dummy frame prior to signalling EOF.
            // This is required to terminate the decoder(OMX.SEC)
            // when only one frame is read during stream info detection.
            if (s->dummy_buf && (frame = (Frame*)av_mallocz(sizeof(Frame)))) {
                frame->status = OK;
                frame->size   = s->dummy_bufsize;
                frame->key    = 1;
                frame->buffer = s->dummy_buf;
                pthread_mutex_lock(&s->in_mutex);
                s->in_queue->push_back(frame);
                pthread_cond_signal(&s->condition);
                pthread_mutex_unlock(&s->in_mutex);
                s->dummy_buf = NULL;
            }

            pthread_mutex_lock(&s->in_mutex);
            s->end_frame->status = ERROR_END_OF_STREAM;
            s->in_queue->push_back(s->end_frame);
            pthread_cond_signal(&s->condition);
            pthread_mutex_unlock(&s->in_mutex);
            s->end_frame = NULL;
        }

        pthread_join(s->decode_thread_id, NULL);

        if (s->ret_frame.data[0])
            avctx->release_buffer(avctx, &s->ret_frame);

        s->thread_started = false;
    }

    while (!s->in_queue->empty()) {
        frame = *s->in_queue->begin();
        s->in_queue->erase(s->in_queue->begin());
        if (frame->size)
            av_freep(&frame->buffer);
        av_freep(&frame);
    }

    while (!s->out_queue->empty()) {
        frame = *s->out_queue->begin();
        s->out_queue->erase(s->out_queue->begin());
        if (frame->size)
            frame->mbuffer->release();
        av_freep(&frame);
    }

    (*s->decoder)->stop();
    s->client->disconnect();

    if (s->decoder_component)
        av_freep(&s->decoder_component);
    av_freep(&s->dummy_buf);
    av_freep(&s->end_frame);

    // Reset the extradata back to the original mp4 format, so that
    // the next invocation (both when decoding and when called from
    // av_find_stream_info) get the original mp4 format extradata.
    av_freep(&avctx->extradata);
    avctx->extradata = s->orig_extradata;
    avctx->extradata_size = s->orig_extradata_size;

    delete s->in_queue;
    delete s->out_queue;
    delete s->client;
    delete s->decoder;
    delete s->source;

    pthread_mutex_destroy(&s->in_mutex);
    pthread_mutex_destroy(&s->out_mutex);
    pthread_cond_destroy(&s->condition);
    av_bitstream_filter_close(s->bsfc);
    return 0;
}

AVCodec ff_libstagefright_h264_decoder = {
    "libstagefright_h264",
    AVMEDIA_TYPE_VIDEO,
    CODEC_ID_H264,
    sizeof(StagefrightContext),
    Stagefright_init,
    NULL, //encode
    Stagefright_close,
    Stagefright_decode_frame,
    CODEC_CAP_DELAY,
    NULL, //next
    NULL, //flush
    NULL, //supported_framerates
    NULL, //pixel_formats
    NULL_IF_CONFIG_SMALL("libstagefright H.264"),
};
