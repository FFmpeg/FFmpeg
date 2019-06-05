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

/* Targeted fuzzer that targets specific codecs depending on two
   compile-time flags.
  INSTRUCTIONS:

  * Get the very fresh clang, e.g. see http://libfuzzer.info#versions
  * Get and build libFuzzer:
     svn co http://llvm.org/svn/llvm-project/llvm/trunk/lib/Fuzzer
     ./Fuzzer/build.sh
  * build ffmpeg for fuzzing:
    FLAGS="-fsanitize=address -fsanitize-coverage=trace-pc-guard,trace-cmp -g" CC="clang $FLAGS" CXX="clang++ $FLAGS" ./configure  --disable-x86asm
    make clean && make -j
  * build the fuzz target.
    Choose the value of FFMPEG_CODEC (e.g. AV_CODEC_ID_DVD_SUBTITLE) and
    choose one of FUZZ_FFMPEG_VIDEO, FUZZ_FFMPEG_AUDIO, FUZZ_FFMPEG_SUBTITLE.
    clang -fsanitize=address -fsanitize-coverage=trace-pc-guard,trace-cmp tools/target_dec_fuzzer.c -o target_dec_fuzzer -I.   -DFFMPEG_CODEC=AV_CODEC_ID_MPEG1VIDEO -DFUZZ_FFMPEG_VIDEO ../../libfuzzer/libFuzzer.a   -Llibavcodec -Llibavdevice -Llibavfilter -Llibavformat -Llibavresample -Llibavutil -Llibpostproc -Llibswscale -Llibswresample -Wl,--as-needed -Wl,-z,noexecstack -Wl,--warn-common -Wl,-rpath-link=:libpostproc:libswresample:libswscale:libavfilter:libavdevice:libavformat:libavcodec:libavutil:libavresample -lavdevice -lavfilter -lavformat -lavcodec -lswresample -lswscale -lavutil -ldl -lxcb -lxcb-shm -lxcb -lxcb-xfixes  -lxcb -lxcb-shape -lxcb -lX11 -lasound -lm -lbz2 -lz -pthread
  * create a corpus directory and put some samples there (empty dir is ok too):
    mkdir CORPUS && cp some-files CORPUS

  * Run fuzzing:
    ./target_dec_fuzzer -max_len=100000 CORPUS

   More info:
   http://libfuzzer.info
   http://tutorial.libfuzzer.info
   https://github.com/google/oss-fuzz
   http://lcamtuf.coredump.cx/afl/
   https://security.googleblog.com/2016/08/guided-in-process-fuzzing-of-chrome.html
*/

#include "config.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bytestream.h"
#include "libavformat/avformat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

extern AVCodec * codec_list[];

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static AVCodec *c = NULL;
static AVCodec *AVCodecInitialize(enum AVCodecID codec_id)
{
    AVCodec *res;

    res = avcodec_find_decoder(codec_id);
    if (!res)
        error("Failed to find decoder");
    return res;
}

static int subtitle_handler(AVCodecContext *avctx, void *frame,
                            int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle sub;
    int ret = avcodec_decode_subtitle2(avctx, &sub, got_sub_ptr, avpkt);
    if (ret >= 0 && *got_sub_ptr)
        avsubtitle_free(&sub);
    return ret;
}

// Class to handle buffer allocation and resize for each frame
typedef struct FuzzDataBuffer {
    size_t size_;
    uint8_t *data_;
} FuzzDataBuffer;

static void FDBCreate(FuzzDataBuffer *FDB) {
    FDB->size_ = 0x1000;
    FDB->data_ = av_malloc(FDB->size_);
    if (!FDB->data_)
        error("Failed memory allocation");
}

static void FDBDesroy(FuzzDataBuffer *FDB) { av_free(FDB->data_); }

static void FDBRealloc(FuzzDataBuffer *FDB, size_t size) {
    size_t needed = size + AV_INPUT_BUFFER_PADDING_SIZE;
    av_assert0(needed > size);
    if (needed > FDB->size_) {
        av_free(FDB->data_);
        FDB->size_ = needed;
        FDB->data_ = av_malloc(FDB->size_);
        if (!FDB->data_)
            error("Failed memory allocation");
    }
}

static void FDBPrepare(FuzzDataBuffer *FDB, AVPacket *dst, const uint8_t *data,
                size_t size)
{
    FDBRealloc(FDB, size);
    memcpy(FDB->data_, data, size);
    size_t padd = FDB->size_ - size;
    if (padd > AV_INPUT_BUFFER_PADDING_SIZE)
        padd = AV_INPUT_BUFFER_PADDING_SIZE;
    memset(FDB->data_ + size, 0, padd);
    av_init_packet(dst);
    dst->data = FDB->data_;
    dst->size = size;
}

// Ensure we don't loop forever
const uint32_t maxiteration = 8096;

static const uint64_t FUZZ_TAG = 0x4741542D5A5A5546ULL;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const uint64_t fuzz_tag = FUZZ_TAG;
    FuzzDataBuffer buffer;
    const uint8_t *last = data;
    const uint8_t *end = data + size;
    uint32_t it = 0;
    uint64_t ec_pixels = 0;
    int (*decode_handler)(AVCodecContext *avctx, AVFrame *picture,
                          int *got_picture_ptr,
                          const AVPacket *avpkt) = NULL;
    AVCodecParserContext *parser = NULL;


    if (!c) {
#ifdef FFMPEG_DECODER
#define DECODER_SYMBOL0(CODEC) ff_##CODEC##_decoder
#define DECODER_SYMBOL(CODEC) DECODER_SYMBOL0(CODEC)
        extern AVCodec DECODER_SYMBOL(FFMPEG_DECODER);
        codec_list[0] = &DECODER_SYMBOL(FFMPEG_DECODER);
        avcodec_register(&DECODER_SYMBOL(FFMPEG_DECODER));

        c = &DECODER_SYMBOL(FFMPEG_DECODER);
#else
        avcodec_register_all();
        c = AVCodecInitialize(FFMPEG_CODEC);  // Done once.
#endif
        av_log_set_level(AV_LOG_PANIC);
    }

    switch (c->type) {
    case AVMEDIA_TYPE_AUDIO   : decode_handler = avcodec_decode_audio4; break;
    case AVMEDIA_TYPE_VIDEO   : decode_handler = avcodec_decode_video2; break;
    case AVMEDIA_TYPE_SUBTITLE: decode_handler = subtitle_handler     ; break;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(NULL);
    AVCodecContext* parser_avctx = avcodec_alloc_context3(NULL);
    if (!ctx || !parser_avctx)
        error("Failed memory allocation");

    ctx->max_pixels = 4096 * 4096; //To reduce false positive OOM and hangs

    if (size > 1024) {
        GetByteContext gbc;
        int extradata_size;
        size -= 1024;
        bytestream2_init(&gbc, data + size, 1024);
        ctx->width                              = bytestream2_get_le32(&gbc);
        ctx->height                             = bytestream2_get_le32(&gbc);
        ctx->bit_rate                           = bytestream2_get_le64(&gbc);
        ctx->bits_per_coded_sample              = bytestream2_get_le32(&gbc);
        // Try to initialize a parser for this codec, note, this may fail which just means we test without one
        if (bytestream2_get_byte(&gbc) & 1)
            parser = av_parser_init(c->id);

        extradata_size = bytestream2_get_le32(&gbc);
        if (extradata_size < size) {
            ctx->extradata = av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
            if (ctx->extradata) {
                ctx->extradata_size = extradata_size;
                size -= ctx->extradata_size;
                memcpy(ctx->extradata, data + size, ctx->extradata_size);
            }
        }
        if (av_image_check_size(ctx->width, ctx->height, 0, ctx))
            ctx->width = ctx->height = 0;
    }

    int res = avcodec_open2(ctx, c, NULL);
    if (res < 0) {
        av_free(ctx);
        av_free(parser_avctx);
        return 0; // Failure of avcodec_open2() does not imply that a issue was found
    }
    parser_avctx->codec_id = ctx->codec_id;

    FDBCreate(&buffer);
    int got_frame;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        error("Failed memory allocation");

    // Read very simple container
    AVPacket avpkt, parsepkt;
    while (data < end && it < maxiteration) {
        // Search for the TAG
        while (data + sizeof(fuzz_tag) < end) {
            if (data[0] == (fuzz_tag & 0xFF) && AV_RN64(data) == fuzz_tag)
                break;
            data++;
        }
        if (data + sizeof(fuzz_tag) > end)
            data = end;

        FDBPrepare(&buffer, &parsepkt, last, data - last);
        data += sizeof(fuzz_tag);
        last = data;

        while (parsepkt.size > 0) {

            if (parser) {
                av_init_packet(&avpkt);
                int ret = av_parser_parse2(parser, parser_avctx, &avpkt.data, &avpkt.size,
                                           parsepkt.data, parsepkt.size,
                                           parsepkt.pts, parsepkt.dts, parsepkt.pos);
                parsepkt.data += ret;
                parsepkt.size -= ret;
                parsepkt.pos  += ret;
                avpkt.pts = parser->pts;
                avpkt.dts = parser->dts;
                avpkt.pos = parser->pos;
                if ( parser->key_frame == 1 ||
                    (parser->key_frame == -1 && parser->pict_type == AV_PICTURE_TYPE_I))
                    avpkt.flags |= AV_PKT_FLAG_KEY;
                avpkt.flags |= parsepkt.flags & AV_PKT_FLAG_DISCARD;
            } else {
                avpkt = parsepkt;
                parsepkt.size = 0;
            }

          // Iterate through all data
          while (avpkt.size > 0 && it++ < maxiteration) {
            av_frame_unref(frame);
            int ret = decode_handler(ctx, frame, &got_frame, &avpkt);

            ec_pixels += ctx->width * ctx->height;
            if (it > 20 || ec_pixels > 4 * ctx->max_pixels)
                ctx->error_concealment = 0;

            if (ret <= 0 || ret > avpkt.size)
               break;
            if (ctx->codec_type != AVMEDIA_TYPE_AUDIO)
                ret = avpkt.size;
            avpkt.data += ret;
            avpkt.size -= ret;
          }
        }
    }

    av_init_packet(&avpkt);
    avpkt.data = NULL;
    avpkt.size = 0;

    do {
        got_frame = 0;
        decode_handler(ctx, frame, &got_frame, &avpkt);
    } while (got_frame == 1 && it++ < maxiteration);

    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    av_freep(&ctx);
    avcodec_free_context(&parser_avctx);
    av_freep(&parser_avctx);
    av_parser_close(parser);
    FDBDesroy(&buffer);
    return 0;
}
