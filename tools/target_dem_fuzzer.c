/*
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

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bytestream.h"
#include "libavformat/avformat.h"


typedef struct IOContext {
    int64_t pos;
    int64_t filesize;
    uint8_t *fuzz;
    int fuzz_size;
} IOContext;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static int io_read(void *opaque, uint8_t *buf, int buf_size)
{
    IOContext *c = opaque;
    int size = FFMIN(buf_size, c->fuzz_size);

    if (!c->fuzz_size) {
        c->filesize = FFMIN(c->pos, c->filesize);
        return AVERROR_EOF;
    }
    if (c->pos > INT64_MAX - size)
        return AVERROR(EIO);

    memcpy(buf, c->fuzz, size);
    c->fuzz      += size;
    c->fuzz_size -= size;
    c->pos       += size;
    c->filesize   = FFMAX(c->filesize, c->pos);

    return size;
}

static int64_t io_seek(void *opaque, int64_t offset, int whence)
{
    IOContext *c = opaque;

    if (whence == SEEK_CUR) {
        if (offset > INT64_MAX - c->pos)
            return -1;
        offset += c->pos;
    } else if (whence == SEEK_END) {
        if (offset > INT64_MAX - c->filesize)
            return -1;
        offset += c->filesize;
    } else if (whence == AVSEEK_SIZE) {
        return c->filesize;
    }
    if (offset < 0 || offset > c->filesize)
        return -1;
    if (IO_FLAT) {
        c->fuzz      += offset - c->pos;
        c->fuzz_size -= offset - c->pos;
    }
    c->pos = offset;
    return 0;
}

// Ensure we don't loop forever
const uint32_t maxiteration = 8096;
const int maxblocks= 50000;

static const uint64_t FUZZ_TAG = 0x4741542D5A5A5546ULL;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const uint64_t fuzz_tag = FUZZ_TAG;
    uint32_t it = 0;
    AVFormatContext *avfmt = avformat_alloc_context();
    AVPacket pkt;
    char filename[1025] = {0};
    AVIOContext *fuzzed_pb = NULL;
    uint8_t *io_buffer;
    int io_buffer_size = 32768;
    int64_t filesize   = size;
    IOContext opaque;
    static int c;
    int seekable = 0;
    int ret;
    AVInputFormat *fmt = NULL;
#ifdef FFMPEG_DEMUXER
#define DEMUXER_SYMBOL0(DEMUXER) ff_##DEMUXER##_demuxer
#define DEMUXER_SYMBOL(DEMUXER) DEMUXER_SYMBOL0(DEMUXER)
    extern AVInputFormat DEMUXER_SYMBOL(FFMPEG_DEMUXER);
    fmt = &DEMUXER_SYMBOL(FFMPEG_DEMUXER);
#endif

    if (!c) {
        av_log_set_level(AV_LOG_PANIC);
        c=1;
    }

    if (!avfmt)
        error("Failed avformat_alloc_context()");

    if (IO_FLAT) {
        seekable = 1;
        io_buffer_size = size;
    } else if (size > 2048) {
        int flags;
        char extension[64];

        GetByteContext gbc;
        memcpy (filename, data + size - 1024, 1024);
        bytestream2_init(&gbc, data + size - 2048, 1024);
        size -= 2048;

        io_buffer_size = bytestream2_get_le32(&gbc) & 0xFFFFFFF;
        flags          = bytestream2_get_byte(&gbc);
        seekable       = flags & 1;
        filesize       = bytestream2_get_le64(&gbc) & 0x7FFFFFFFFFFFFFFF;

        if ((flags & 2) && strlen(filename) < sizeof(filename) / 2) {
            const AVInputFormat *avif = NULL;
            void *avif_iter = NULL;
            int avif_count = 0;
            while ((avif = av_demuxer_iterate(&avif_iter))) {
                if (avif->extensions)
                    avif_count ++;
            }
            avif_count =  bytestream2_get_le32(&gbc) % avif_count;

            avif_iter = NULL;
            while ((avif = av_demuxer_iterate(&avif_iter))) {
                if (avif->extensions)
                    if (!avif_count--)
                        break;
            }
            av_strlcpy(extension, avif->extensions, sizeof(extension));
            if (strchr(extension, ','))
                *strchr(extension, ',') = 0;
            av_strlcatf(filename, sizeof(filename), ".%s", extension);
        }
    }

    if (!io_buffer_size || size / io_buffer_size > maxblocks)
        io_buffer_size = size;

    io_buffer = av_malloc(io_buffer_size);
    if (!io_buffer)
        error("Failed to allocate io_buffer");

    opaque.filesize = filesize;
    opaque.pos      = 0;
    opaque.fuzz     = data;
    opaque.fuzz_size= size;
    fuzzed_pb = avio_alloc_context(io_buffer, io_buffer_size, 0, &opaque,
                                   io_read, NULL, seekable ? io_seek : NULL);
    if (!fuzzed_pb)
        error("avio_alloc_context failed");

    avfmt->pb = fuzzed_pb;

    ret = avformat_open_input(&avfmt, filename, fmt, NULL);
    if (ret < 0) {
        av_freep(&fuzzed_pb->buffer);
        av_freep(&fuzzed_pb);
        avformat_free_context(avfmt);
        return 0;
    }

    ret = avformat_find_stream_info(avfmt, NULL);

    av_init_packet(&pkt);

    //TODO, test seeking

    for(it = 0; it < maxiteration; it++) {
        ret = av_read_frame(avfmt, &pkt);
        if (ret < 0)
            break;
        av_packet_unref(&pkt);
    }

    av_freep(&fuzzed_pb->buffer);
    avio_context_free(&fuzzed_pb);
    avformat_close_input(&avfmt);

    return 0;
}
