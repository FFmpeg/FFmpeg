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
    FLAGS="-fsanitize=address -fsanitize-coverage=trace-pc-guard,trace-cmp -g" CC="clang $FLAGS" CXX="clang++ $FLAGS" ./configure  --disable-yasm
    make clean && make -j
  * build the fuzz target.
    Choose the value of FFMPEG_CODEC (e.g. AV_CODEC_ID_DVD_SUBTITLE) and
    choose one of FUZZ_FFMPEG_VIDEO, FUZZ_FFMPEG_AUDIO, FUZZ_FFMPEG_SUBTITLE.
    clang -fsanitize=address -fsanitize-coverage=trace-pc-guard,trace-cmp tools/target_dec_fuzzer.c -o target_dec_fuzzer -I.   -DFFMPEG_CODEC=AV_CODEC_ID_MPEG1VIDEO -DFUZZ_FFMPEG_VIDEO ../../libfuzzer/libFuzzer.a   -Llibavcodec -Llibavdevice -Llibavfilter -Llibavformat -Llibavresample -Llibavutil -Llibpostproc -Llibswscale -Llibswresample -Wl,--as-needed -Wl,-z,noexecstack -Wl,--warn-common -Wl,-rpath-link=libpostproc:libswresample:libswscale:libavfilter:libavdevice:libavformat:libavcodec:libavutil:libavresample -lavdevice -lavfilter -lavformat -lavcodec -lswresample -lswscale -lavutil -ldl -lxcb -lxcb-shm -lxcb -lxcb-xfixes  -lxcb -lxcb-shape -lxcb -lX11 -lasound -lm -lbz2 -lz -pthread
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

#include "libavutil/avassert.h"
#include "libavutil/intreadwrite.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

static void error(const char *err)
{
    fprintf(stderr, "%s", err);
    exit(1);
}

static AVCodec *c = NULL;
static AVCodec *AVCodecInitialize(enum AVCodecID codec_id)
{
    AVCodec *res;
    av_register_all();
    av_log_set_level(AV_LOG_PANIC);
    res = avcodec_find_decoder(codec_id);
    if (!res)
        error("Failed to find decoder");
    return res;
}

#if defined(FUZZ_FFMPEG_VIDEO)
#define decode_handler avcodec_decode_video2
#elif defined(FUZZ_FFMPEG_AUDIO)
#define decode_handler avcodec_decode_audio4
#elif defined(FUZZ_FFMPEG_SUBTITLE)
static int subtitle_handler(AVCodecContext *avctx, void *frame,
                            int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle sub;
    int ret = avcodec_decode_subtitle2(avctx, &sub, got_sub_ptr, avpkt);
    if (ret >= 0 && *got_sub_ptr)
        avsubtitle_free(&sub);
    return ret;
}

#define decode_handler subtitle_handler
#else
#error "Specify encoder type"  // To catch mistakes
#endif

// Class to handle buffer allocation and resize for each frame
typedef struct FuzzDataBuffer {
    size_t size_;
    uint8_t *data_;
} FuzzDataBuffer;

void FDBCreate(FuzzDataBuffer *FDB) {
    FDB->size_ = 0x1000;
    FDB->data_ = av_malloc(FDB->size_);
    if (!FDB->data_)
        error("Failed memory allocation");
}

void FDBDesroy(FuzzDataBuffer *FDB) { av_free(FDB->data_); }

void FDBRealloc(FuzzDataBuffer *FDB, size_t size) {
    size_t needed = size + FF_INPUT_BUFFER_PADDING_SIZE;
    av_assert0(needed > size);
    if (needed > FDB->size_) {
        av_free(FDB->data_);
        FDB->size_ = needed;
        FDB->data_ = av_malloc(FDB->size_);
        if (!FDB->data_)
            error("Failed memory allocation");
    }
}

void FDBPrepare(FuzzDataBuffer *FDB, AVPacket *dst, const uint8_t *data,
                size_t size)
{
    FDBRealloc(FDB, size);
    memcpy(FDB->data_, data, size);
    size_t padd = FDB->size_ - size;
    if (padd > FF_INPUT_BUFFER_PADDING_SIZE)
        padd = FF_INPUT_BUFFER_PADDING_SIZE;
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

    if (!c)
        c = AVCodecInitialize(FFMPEG_CODEC);  // Done once.

    AVCodecContext* ctx = avcodec_alloc_context3(NULL);
    if (!ctx)
        error("Failed memory allocation");

    ctx->max_pixels = 4096 * 4096; //To reduce false positive OOM and hangs

    int res = avcodec_open2(ctx, c, NULL);
    if (res < 0)
        return res;

    FDBCreate(&buffer);
    int got_frame;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        error("Failed memory allocation");

    // Read very simple container
    AVPacket avpkt;
    while (data < end && it < maxiteration) {
        // Search for the TAG
        while (data + sizeof(fuzz_tag) < end) {
            if (data[0] == (fuzz_tag & 0xFF) && AV_RN64(data) == fuzz_tag)
                break;
            data++;
        }
        if (data + sizeof(fuzz_tag) > end)
            data = end;

        FDBPrepare(&buffer, &avpkt, last, data - last);
        data += sizeof(fuzz_tag);
        last = data;

        // Iterate through all data
        while (avpkt.size > 0 && it++ < maxiteration) {
            av_frame_unref(frame);
            int ret = decode_handler(ctx, frame, &got_frame, &avpkt);

            if (it > 20)
                ctx->error_concealment = 0;

            if (ret <= 0 || ret > avpkt.size)
               break;
            if (ctx->codec_type != AVMEDIA_TYPE_AUDIO)
                ret = avpkt.size;
            avpkt.data += ret;
            avpkt.size -= ret;
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
    FDBDesroy(&buffer);
    return 0;
}
