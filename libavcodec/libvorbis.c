/*
 * copyright (c) 2002 Mark Hills <mark@pogo.org.uk>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Ogg Vorbis codec support via libvorbisenc.
 * @author Mark Hills <mark@pogo.org.uk>
 */

#include <vorbis/vorbisenc.h>

#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "vorbis.h"
#include "libavutil/mathematics.h"

#undef NDEBUG
#include <assert.h>

#define OGGVORBIS_FRAME_SIZE 64

#define BUFFER_SIZE (1024 * 64)

typedef struct OggVorbisContext {
    AVClass *av_class;
    vorbis_info vi;
    vorbis_dsp_state vd;
    vorbis_block vb;
    uint8_t buffer[BUFFER_SIZE];
    int buffer_index;
    int eof;

    /* decoder */
    vorbis_comment vc;
    ogg_packet op;

    double iblock;
} OggVorbisContext;

static const AVOption options[] = {
    { "iblock", "Sets the impulse block bias", offsetof(OggVorbisContext, iblock), AV_OPT_TYPE_DOUBLE, { .dbl = 0 }, -15, 0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};
static const AVClass class = { "libvorbis", av_default_item_name, options, LIBAVUTIL_VERSION_INT };

static av_cold int oggvorbis_init_encoder(vorbis_info *vi, AVCodecContext *avccontext)
{
    OggVorbisContext *context = avccontext->priv_data;
    double cfreq;

    if (avccontext->flags & CODEC_FLAG_QSCALE) {
        /* variable bitrate */
        if (vorbis_encode_setup_vbr(vi, avccontext->channels,
                                    avccontext->sample_rate,
                                    avccontext->global_quality / (float)FF_QP2LAMBDA / 10.0))
            return -1;
    } else {
        int minrate = avccontext->rc_min_rate > 0 ? avccontext->rc_min_rate : -1;
        int maxrate = avccontext->rc_min_rate > 0 ? avccontext->rc_max_rate : -1;

        /* constant bitrate */
        if (vorbis_encode_setup_managed(vi, avccontext->channels,
                                        avccontext->sample_rate, minrate,
                                        avccontext->bit_rate, maxrate))
            return -1;

        /* variable bitrate by estimate, disable slow rate management */
        if (minrate == -1 && maxrate == -1)
            if (vorbis_encode_ctl(vi, OV_ECTL_RATEMANAGE2_SET, NULL))
                return -1;
    }

    /* cutoff frequency */
    if (avccontext->cutoff > 0) {
        cfreq = avccontext->cutoff / 1000.0;
        if (vorbis_encode_ctl(vi, OV_ECTL_LOWPASS_SET, &cfreq))
            return -1;
    }

    if (context->iblock) {
        vorbis_encode_ctl(vi, OV_ECTL_IBLOCK_SET, &context->iblock);
    }

    return vorbis_encode_setup_init(vi);
}

/* How many bytes are needed for a buffer of length 'l' */
static int xiph_len(int l)
{
    return 1 + l / 255 + l;
}

static av_cold int oggvorbis_encode_init(AVCodecContext *avccontext)
{
    OggVorbisContext *context = avccontext->priv_data;
    ogg_packet header, header_comm, header_code;
    uint8_t *p;
    unsigned int offset;

    vorbis_info_init(&context->vi);
    if (oggvorbis_init_encoder(&context->vi, avccontext) < 0) {
        av_log(avccontext, AV_LOG_ERROR, "oggvorbis_encode_init: init_encoder failed\n");
        return -1;
    }
    vorbis_analysis_init(&context->vd, &context->vi);
    vorbis_block_init(&context->vd, &context->vb);

    vorbis_comment_init(&context->vc);
    vorbis_comment_add_tag(&context->vc, "encoder", LIBAVCODEC_IDENT);

    vorbis_analysis_headerout(&context->vd, &context->vc, &header,
                              &header_comm, &header_code);

    avccontext->extradata_size =
        1 + xiph_len(header.bytes) + xiph_len(header_comm.bytes) +
        header_code.bytes;
    p = avccontext->extradata =
            av_malloc(avccontext->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    p[0]    = 2;
    offset  = 1;
    offset += av_xiphlacing(&p[offset], header.bytes);
    offset += av_xiphlacing(&p[offset], header_comm.bytes);
    memcpy(&p[offset], header.packet, header.bytes);
    offset += header.bytes;
    memcpy(&p[offset], header_comm.packet, header_comm.bytes);
    offset += header_comm.bytes;
    memcpy(&p[offset], header_code.packet, header_code.bytes);
    offset += header_code.bytes;
    assert(offset == avccontext->extradata_size);

#if 0
    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi);
#endif
    vorbis_comment_clear(&context->vc);

    avccontext->frame_size = OGGVORBIS_FRAME_SIZE;

    avccontext->coded_frame = avcodec_alloc_frame();
    avccontext->coded_frame->key_frame = 1;

    return 0;
}

static int oggvorbis_encode_frame(AVCodecContext *avccontext,
                                  unsigned char *packets,
                                  int buf_size, void *data)
{
    OggVorbisContext *context = avccontext->priv_data;
    ogg_packet op;
    signed short *audio = data;
    int l;

    if (data) {
        const int samples = avccontext->frame_size;
        float **buffer;
        int c, channels = context->vi.channels;

        buffer = vorbis_analysis_buffer(&context->vd, samples);
        for (c = 0; c < channels; c++) {
            int co = (channels > 8) ? c :
                     ff_vorbis_encoding_channel_layout_offsets[channels - 1][c];
            for (l = 0; l < samples; l++)
                buffer[c][l] = audio[l * channels + co] / 32768.f;
        }
        vorbis_analysis_wrote(&context->vd, samples);
    } else {
        if (!context->eof)
            vorbis_analysis_wrote(&context->vd, 0);
        context->eof = 1;
    }

    while (vorbis_analysis_blockout(&context->vd, &context->vb) == 1) {
        vorbis_analysis(&context->vb, NULL);
        vorbis_bitrate_addblock(&context->vb);

        while (vorbis_bitrate_flushpacket(&context->vd, &op)) {
            /* i'd love to say the following line is a hack, but sadly it's
             * not, apparently the end of stream decision is in libogg. */
            if (op.bytes == 1 && op.e_o_s)
                continue;
            if (context->buffer_index + sizeof(ogg_packet) + op.bytes > BUFFER_SIZE) {
                av_log(avccontext, AV_LOG_ERROR, "libvorbis: buffer overflow.");
                return -1;
            }
            memcpy(context->buffer + context->buffer_index, &op, sizeof(ogg_packet));
            context->buffer_index += sizeof(ogg_packet);
            memcpy(context->buffer + context->buffer_index, op.packet, op.bytes);
            context->buffer_index += op.bytes;
//            av_log(avccontext, AV_LOG_DEBUG, "e%d / %d\n", context->buffer_index, op.bytes);
        }
    }

    l = 0;
    if (context->buffer_index) {
        ogg_packet *op2 = (ogg_packet *)context->buffer;
        op2->packet = context->buffer + sizeof(ogg_packet);

        l = op2->bytes;
        avccontext->coded_frame->pts = av_rescale_q(op2->granulepos, (AVRational) { 1, avccontext->sample_rate }, avccontext->time_base);
        //FIXME we should reorder the user supplied pts and not assume that they are spaced by 1/sample_rate

        if (l > buf_size) {
            av_log(avccontext, AV_LOG_ERROR, "libvorbis: buffer overflow.");
            return -1;
        }

        memcpy(packets, op2->packet, l);
        context->buffer_index -= l + sizeof(ogg_packet);
        memmove(context->buffer, context->buffer + l + sizeof(ogg_packet), context->buffer_index);
//        av_log(avccontext, AV_LOG_DEBUG, "E%d\n", l);
    }

    return l;
}

static av_cold int oggvorbis_encode_close(AVCodecContext *avccontext)
{
    OggVorbisContext *context = avccontext->priv_data;
/*  ogg_packet op ; */

    vorbis_analysis_wrote(&context->vd, 0);  /* notify vorbisenc this is EOF */

    vorbis_block_clear(&context->vb);
    vorbis_dsp_clear(&context->vd);
    vorbis_info_clear(&context->vi);

    av_freep(&avccontext->coded_frame);
    av_freep(&avccontext->extradata);

    return 0;
}

AVCodec ff_libvorbis_encoder = {
    .name           = "libvorbis",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_VORBIS,
    .priv_data_size = sizeof(OggVorbisContext),
    .init           = oggvorbis_encode_init,
    .encode         = oggvorbis_encode_frame,
    .close          = oggvorbis_encode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE },
    .long_name      = NULL_IF_CONFIG_SMALL("libvorbis Vorbis"),
    .priv_class     = &class,
};
