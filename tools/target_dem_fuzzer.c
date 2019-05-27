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
    }
    if (offset < 0 || offset > c->filesize)
        return -1;
    c->pos = offset;
    return 0;
}

// Ensure we don't loop forever
const uint32_t maxiteration = 8096;

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

    if (!c) {
        av_register_all();
        avcodec_register_all();
        av_log_set_level(AV_LOG_PANIC);
        c=1;
    }

    if (!avfmt)
        error("Failed avformat_alloc_context()");

    if (size > 2048) {
        GetByteContext gbc;
        memcpy (filename, data + size - 1024, 1024);
        bytestream2_init(&gbc, data + size - 2048, 1024);
        size -= 2048;

        io_buffer_size = bytestream2_get_le32(&gbc) & 0xFFFFFFF;
        seekable       = bytestream2_get_byte(&gbc) & 1;
        filesize       = bytestream2_get_le64(&gbc) & 0x7FFFFFFFFFFFFFFF;
    }
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

    ret = avformat_open_input(&avfmt, filename, NULL, NULL);
    if (ret < 0) {
        av_freep(&fuzzed_pb->buffer);
        av_freep(&fuzzed_pb);
        avformat_free_context(avfmt);
        return 0;
    }

    ret = avformat_find_stream_info(avfmt, NULL);
    if (ret < 0)
        goto end;

    av_init_packet(&pkt);

    //TODO, test seeking

    for(it = 0; it < maxiteration; it++) {
        ret = av_read_frame(avfmt, &pkt);
        if (ret < 0)
            break;
        av_packet_unref(&pkt);
    }
end:
    av_freep(&fuzzed_pb->buffer);
    av_freep(&fuzzed_pb);
    avformat_close_input(&avfmt);

    return 0;
}
