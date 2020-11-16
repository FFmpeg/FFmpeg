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

//For FF_SANE_NB_CHANNELS, so we dont waste energy testing things that will get instantly rejected
#include "libavcodec/internal.h"

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

// Ensure we don't loop forever
const uint32_t maxiteration = 8096;
const uint64_t maxpixels_per_frame = 4096 * 4096;
uint64_t maxpixels;

uint64_t maxsamples_per_frame = 256*1024*32;
uint64_t maxsamples;

static const uint64_t FUZZ_TAG = 0x4741542D5A5A5546ULL;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const uint64_t fuzz_tag = FUZZ_TAG;
    const uint8_t *last = data;
    const uint8_t *end = data + size;
    uint32_t it = 0;
    uint64_t ec_pixels = 0;
    uint64_t nb_samples = 0;
    int (*decode_handler)(AVCodecContext *avctx, AVFrame *picture,
                          int *got_picture_ptr,
                          const AVPacket *avpkt) = NULL;
    AVCodecParserContext *parser = NULL;
    uint64_t keyframes = 0;
    uint64_t flushpattern = -1;
    AVDictionary *opts = NULL;

    if (!c) {
#ifdef FFMPEG_DECODER
#define DECODER_SYMBOL0(CODEC) ff_##CODEC##_decoder
#define DECODER_SYMBOL(CODEC) DECODER_SYMBOL0(CODEC)
        extern AVCodec DECODER_SYMBOL(FFMPEG_DECODER);
        codec_list[0] = &DECODER_SYMBOL(FFMPEG_DECODER);

#if FFMPEG_DECODER == tiff || FFMPEG_DECODER == tdsc
        extern AVCodec DECODER_SYMBOL(mjpeg);
        codec_list[1] = &DECODER_SYMBOL(mjpeg);
#endif

        c = &DECODER_SYMBOL(FFMPEG_DECODER);
#else
        c = AVCodecInitialize(FFMPEG_CODEC);  // Done once.
#endif
        av_log_set_level(AV_LOG_PANIC);
    }

    switch (c->type) {
    case AVMEDIA_TYPE_AUDIO   : decode_handler = avcodec_decode_audio4; break;
    case AVMEDIA_TYPE_VIDEO   : decode_handler = avcodec_decode_video2; break;
    case AVMEDIA_TYPE_SUBTITLE: decode_handler = subtitle_handler     ; break;
    }
    switch (c->id) {
    case AV_CODEC_ID_APE:       maxsamples_per_frame /= 256; break;
    }
    maxpixels = maxpixels_per_frame * maxiteration;
    maxsamples = maxsamples_per_frame * maxiteration;
    switch (c->id) {
    case AV_CODEC_ID_AGM:         maxpixels  /= 1024;  break;
    case AV_CODEC_ID_BINKVIDEO:   maxpixels  /= 32;    break;
    case AV_CODEC_ID_CFHD:        maxpixels  /= 128;   break;
    case AV_CODEC_ID_DIRAC:       maxpixels  /= 8192;  break;
    case AV_CODEC_ID_DST:         maxsamples /= 1<<20; break;
    case AV_CODEC_ID_DXV:         maxpixels  /= 32;    break;
    case AV_CODEC_ID_FFWAVESYNTH: maxsamples /= 16384; break;
    case AV_CODEC_ID_G2M:         maxpixels  /= 1024;  break;
    case AV_CODEC_ID_GDV:         maxpixels  /= 512;   break;
    case AV_CODEC_ID_GIF:         maxpixels  /= 16;    break;
    case AV_CODEC_ID_HAP:         maxpixels  /= 128;   break;
    case AV_CODEC_ID_HEVC:        maxpixels  /= 16384; break;
    case AV_CODEC_ID_HNM4_VIDEO:  maxpixels  /= 128;   break;
    case AV_CODEC_ID_IFF_ILBM:    maxpixels  /= 128;   break;
    case AV_CODEC_ID_INDEO4:      maxpixels  /= 128;   break;
    case AV_CODEC_ID_LAGARITH:    maxpixels  /= 1024;  break;
    case AV_CODEC_ID_LSCR:        maxpixels  /= 16;    break;
    case AV_CODEC_ID_MOTIONPIXELS:maxpixels  /= 256;   break;
    case AV_CODEC_ID_MP4ALS:      maxsamples /= 65536; break;
    case AV_CODEC_ID_MSRLE:       maxpixels  /= 16;    break;
    case AV_CODEC_ID_MSS2:        maxpixels  /= 16384; break;
    case AV_CODEC_ID_MSZH:        maxpixels  /= 128;   break;
    case AV_CODEC_ID_OPUS:        maxsamples /= 16384; break;
    case AV_CODEC_ID_PNG:         maxpixels  /= 128;   break;
    case AV_CODEC_ID_APNG:        maxpixels  /= 128;   break;
    case AV_CODEC_ID_QTRLE:       maxpixels  /= 16;    break;
    case AV_CODEC_ID_RASC:        maxpixels  /= 16;    break;
    case AV_CODEC_ID_SANM:        maxpixels  /= 16;    break;
    case AV_CODEC_ID_SCPR:        maxpixels  /= 32;    break;
    case AV_CODEC_ID_SCREENPRESSO:maxpixels  /= 64;    break;
    case AV_CODEC_ID_SMACKVIDEO:  maxpixels  /= 64;    break;
    case AV_CODEC_ID_SNOW:        maxpixels  /= 128;   break;
    case AV_CODEC_ID_TGV:         maxpixels  /= 32;    break;
    case AV_CODEC_ID_TRUEMOTION2: maxpixels  /= 1024;  break;
    case AV_CODEC_ID_VP7:         maxpixels  /= 256;   break;
    case AV_CODEC_ID_VP9:         maxpixels  /= 4096;  break;
    case AV_CODEC_ID_WMV3IMAGE:   maxpixels  /= 8192;  break;
    case AV_CODEC_ID_WS_VQA:      maxpixels  /= 16384; break;
    case AV_CODEC_ID_WMALOSSLESS: maxsamples /= 1024;  break;
    case AV_CODEC_ID_ZEROCODEC:   maxpixels  /= 128;   break;
    }

    maxsamples_per_frame = FFMIN(maxsamples_per_frame, maxsamples);

    AVCodecContext* ctx = avcodec_alloc_context3(c);
    AVCodecContext* parser_avctx = avcodec_alloc_context3(NULL);
    if (!ctx || !parser_avctx)
        error("Failed memory allocation");

    if (ctx->max_pixels == 0 || ctx->max_pixels > maxpixels_per_frame)
        ctx->max_pixels = maxpixels_per_frame; //To reduce false positive OOM and hangs
    ctx->refcounted_frames = 1; //To reduce false positive timeouts and focus testing on the refcounted API

    ctx->max_samples = maxsamples_per_frame;

    if (size > 1024) {
        GetByteContext gbc;
        int extradata_size;
        int flags;
        size -= 1024;
        bytestream2_init(&gbc, data + size, 1024);
        ctx->width                              = bytestream2_get_le32(&gbc);
        ctx->height                             = bytestream2_get_le32(&gbc);
        ctx->bit_rate                           = bytestream2_get_le64(&gbc);
        ctx->bits_per_coded_sample              = bytestream2_get_le32(&gbc);
        // Try to initialize a parser for this codec, note, this may fail which just means we test without one
        flags = bytestream2_get_byte(&gbc);
        if (flags & 1)
            parser = av_parser_init(c->id);
        if (flags & 2)
            ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        if (flags & 4) {
            ctx->err_recognition = AV_EF_AGGRESSIVE | AV_EF_COMPLIANT | AV_EF_CAREFUL;
            if (flags & 8)
                ctx->err_recognition |= AV_EF_EXPLODE;
        }
        if ((flags & 0x10) && c->id != AV_CODEC_ID_H264)
            ctx->flags2 |= AV_CODEC_FLAG2_FAST;

        if (flags & 0x40)
            av_force_cpu_flags(0);

        extradata_size = bytestream2_get_le32(&gbc);

        ctx->sample_rate                        = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->channels                           = (unsigned)bytestream2_get_le32(&gbc) % FF_SANE_NB_CHANNELS;
        ctx->block_align                        = bytestream2_get_le32(&gbc) & 0x7FFFFFFF;
        ctx->codec_tag                          = bytestream2_get_le32(&gbc);
        if (c->codec_tags) {
            int n;
            for (n = 0; c->codec_tags[n] != FF_CODEC_TAGS_END; n++);
            ctx->codec_tag = c->codec_tags[ctx->codec_tag % n];
        }
        keyframes                               = bytestream2_get_le64(&gbc);
        ctx->request_channel_layout             = bytestream2_get_le64(&gbc);

        ctx->idct_algo                          = bytestream2_get_byte(&gbc) % 25;
        flushpattern                            = bytestream2_get_le64(&gbc);

        if (flags & 0x20) {
            switch (ctx->codec_id) {
            case AV_CODEC_ID_AC3:
            case AV_CODEC_ID_EAC3:
                av_dict_set_int(&opts, "cons_noisegen", bytestream2_get_byte(&gbc) & 1, 0);
                av_dict_set_int(&opts, "heavy_compr",   bytestream2_get_byte(&gbc) & 1, 0);
                av_dict_set_int(&opts, "target_level",  (int)(bytestream2_get_byte(&gbc) % 32) - 31, 0);
                av_dict_set_int(&opts, "dmix_mode",     (int)(bytestream2_get_byte(&gbc) %  4) -  1, 0);
                break;
            }
        }


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

    int res = avcodec_open2(ctx, c, &opts);
    if (res < 0) {
        avcodec_free_context(&ctx);
        av_free(parser_avctx);
        av_parser_close(parser);
        av_dict_free(&opts);
        return 0; // Failure of avcodec_open2() does not imply that a issue was found
    }
    parser_avctx->codec_id = ctx->codec_id;

    int got_frame;
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        error("Failed memory allocation");

    // Read very simple container
    AVPacket avpkt, parsepkt;
    av_init_packet(&avpkt);
    av_init_packet(&parsepkt);
    while (data < end && it < maxiteration) {
        // Search for the TAG
        while (data + sizeof(fuzz_tag) < end) {
            if (data[0] == (fuzz_tag & 0xFF) && AV_RN64(data) == fuzz_tag)
                break;
            data++;
        }
        if (data + sizeof(fuzz_tag) > end)
            data = end;

        res = av_new_packet(&parsepkt, data - last);
        if (res < 0)
            error("Failed memory allocation");
        memcpy(parsepkt.data, last, data - last);
        parsepkt.flags = (keyframes & 1) * AV_PKT_FLAG_DISCARD + (!!(keyframes & 2)) * AV_PKT_FLAG_KEY;
        keyframes = (keyframes >> 2) + (keyframes<<62);
        data += sizeof(fuzz_tag);
        last = data;

        while (parsepkt.size > 0) {

            if (parser) {
                av_init_packet(&avpkt);
                int ret = av_parser_parse2(parser, parser_avctx, &avpkt.data, &avpkt.size,
                                           parsepkt.data, parsepkt.size,
                                           parsepkt.pts, parsepkt.dts, parsepkt.pos);
                if (avpkt.data == parsepkt.data) {
                    avpkt.buf = av_buffer_ref(parsepkt.buf);
                    if (!avpkt.buf)
                        error("Failed memory allocation");
                } else {
                    if (av_packet_make_refcounted(&avpkt) < 0)
                        error("Failed memory allocation");
                }
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
                av_packet_move_ref(&avpkt, &parsepkt);
            }

          if (!(flushpattern & 7))
              avcodec_flush_buffers(ctx);
          flushpattern = (flushpattern >> 3) + (flushpattern << 61);

          // Iterate through all data
          while (avpkt.size > 0 && it++ < maxiteration) {
            av_frame_unref(frame);
            int ret = decode_handler(ctx, frame, &got_frame, &avpkt);

            ec_pixels += (ctx->width + 32LL) * (ctx->height + 32LL);
            if (it > 20 || ec_pixels > 4 * ctx->max_pixels)
                ctx->error_concealment = 0;
            if (ec_pixels > maxpixels)
                goto maximums_reached;

            if (ctx->codec_type == AVMEDIA_TYPE_AUDIO &&
                frame->nb_samples == 0 && !got_frame &&
                (avpkt.flags & AV_PKT_FLAG_DISCARD))
                nb_samples += ctx->max_samples;

            nb_samples += frame->nb_samples;
            if (nb_samples > maxsamples)
                goto maximums_reached;

            if (ret <= 0 || ret > avpkt.size)
               break;
            if (ctx->codec_type != AVMEDIA_TYPE_AUDIO)
                ret = avpkt.size;
            avpkt.data += ret;
            avpkt.size -= ret;
          }
          av_packet_unref(&avpkt);
        }
        av_packet_unref(&parsepkt);
    }
maximums_reached:

    av_packet_unref(&avpkt);

    do {
        got_frame = 0;
        av_frame_unref(frame);
        decode_handler(ctx, frame, &got_frame, &avpkt);
    } while (got_frame == 1 && it++ < maxiteration);

    fprintf(stderr, "pixels decoded: %"PRId64", samples decoded: %"PRId64", iterations: %d\n", ec_pixels, nb_samples, it);

    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    avcodec_free_context(&parser_avctx);
    av_parser_close(parser);
    av_packet_unref(&parsepkt);
    av_dict_free(&opts);
    return 0;
}
