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

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_av1.h"
#include "cbs_h265.h"
#include "dovi_rpu.h"
#include "h2645data.h"
#include "h265_profile_level.h"
#include "itut35.h"

#include "hevc/hevc.h"

typedef struct DoviRpuContext {
    CBSBSFContext common;
    DOVIContext dec;
    DOVIContext enc;

    int strip;
    int compression;
} DoviRpuContext;

static int update_rpu(AVBSFContext *bsf, const AVPacket *pkt, int flags,
                      const uint8_t *rpu, size_t rpu_size,
                      uint8_t **out_rpu, int *out_size)
{
    DoviRpuContext *s = bsf->priv_data;
    AVDOVIMetadata *metadata = NULL;
    int ret;

    ret = ff_dovi_rpu_parse(&s->dec, rpu, rpu_size, 0);
    if (ret < 0) {
        ff_dovi_ctx_flush(&s->dec);
        return ret;
    }

    ret = ff_dovi_get_metadata(&s->dec, &metadata);
    if (ret == 0 /* no metadata */) {
        *out_rpu = NULL;
        *out_size = 0;
        return 0;
    } else if (ret < 0) {
        ff_dovi_ctx_flush(&s->dec);
        return ret;
    }

    if (pkt && !(pkt->flags & AV_PKT_FLAG_KEY))
        flags |= FF_DOVI_COMPRESS_RPU;
    ret = ff_dovi_rpu_generate(&s->enc, metadata, flags, out_rpu, out_size);
    av_free(metadata);
    if (ret < 0)
        ff_dovi_ctx_flush(&s->enc);

    return ret;
}

static int dovi_rpu_update_fragment_hevc(AVBSFContext *bsf, AVPacket *pkt,
                                         CodedBitstreamFragment *au)
{
    DoviRpuContext *s = bsf->priv_data;
    CodedBitstreamUnit *nal = au->nb_units ? &au->units[au->nb_units - 1] : NULL;
    uint8_t *rpu = NULL;
    int rpu_size, ret;

    if (!nal || nal->type != HEVC_NAL_UNSPEC62)
        return 0;

    if (s->strip) {
        ff_cbs_delete_unit(au, au->nb_units - 1);
        return 0;
    }

    ret = update_rpu(bsf, pkt, 0, nal->data + 2, nal->data_size - 2, &rpu, &rpu_size);
    if (ret < 0)
        return ret;

    /* NAL unit header + NAL prefix */
    if (rpu_size + 3 <= nal->data_size && av_buffer_is_writable(nal->data_ref)) {
        memcpy(nal->data + 3, rpu, rpu_size);
        av_free(rpu);
        nal->data_size = rpu_size + 3;
    } else {
        AVBufferRef *ref = av_buffer_alloc(rpu_size + 3);
        if (!ref) {
            av_free(rpu);
            return AVERROR(ENOMEM);
        }

        memcpy(ref->data, nal->data, 3);
        memcpy(ref->data + 3, rpu, rpu_size);
        av_buffer_unref(&nal->data_ref);
        av_free(rpu);
        nal->data = ref->data;
        nal->data_size = rpu_size + 3;
        nal->data_ref = ref;
        nal->data_bit_padding = 0;
    }

    return 0;
}

static int dovi_rpu_update_fragment_av1(AVBSFContext *bsf, AVPacket *pkt,
                                        CodedBitstreamFragment *frag)
{
    DoviRpuContext *s = bsf->priv_data;
    int provider_code, provider_oriented_code, rpu_size, ret;
    AVBufferRef *ref;
    uint8_t *rpu;

    for (int i = 0; i < frag->nb_units; i++) {
        AV1RawOBU *obu = frag->units[i].content;
        AV1RawMetadataITUTT35 *t35 = &obu->obu.metadata.metadata.itut_t35;
        if (frag->units[i].type != AV1_OBU_METADATA ||
            obu->obu.metadata.metadata_type != AV1_METADATA_TYPE_ITUT_T35 ||
            t35->itu_t_t35_country_code != ITU_T_T35_COUNTRY_CODE_US ||
            t35->payload_size < 6)
            continue;

        provider_code = AV_RB16(t35->payload);
        provider_oriented_code = AV_RB32(t35->payload + 2);
        if (provider_code != ITU_T_T35_PROVIDER_CODE_DOLBY ||
            provider_oriented_code != 0x800)
            continue;

        if (s->strip) {
            ff_cbs_delete_unit(frag, i);
            return 0;
        }

        ret = update_rpu(bsf, pkt, FF_DOVI_WRAP_T35,
                         t35->payload + 6, t35->payload_size - 6,
                         &rpu, &rpu_size);
        if (ret < 0)
            return ret;

        ref = av_buffer_create(rpu, rpu_size, av_buffer_default_free, NULL, 0);
        if (!ref) {
            av_free(rpu);
            return AVERROR(ENOMEM);
        }

        av_buffer_unref(&t35->payload_ref);
        t35->payload_ref = ref;
        t35->payload = rpu + 1; /* skip country code */
        t35->payload_size = rpu_size - 1;
        break; /* should be only one RPU per packet */
    }

    return 0;
}

static const CBSBSFType dovi_rpu_hevc_type = {
    .codec_id        = AV_CODEC_ID_HEVC,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &dovi_rpu_update_fragment_hevc,
};

static const CBSBSFType dovi_rpu_av1_type = {
    .codec_id        = AV_CODEC_ID_AV1,
    .fragment_name   = "temporal unit",
    .unit_name       = "OBU",
    .update_fragment = &dovi_rpu_update_fragment_av1,
};

static int dovi_rpu_init(AVBSFContext *bsf)
{
    int ret;
    DoviRpuContext *s = bsf->priv_data;
    s->dec.logctx = s->enc.logctx = bsf;
    s->enc.enable = 1;

    if (s->compression == AV_DOVI_COMPRESSION_RESERVED) {
        av_log(bsf, AV_LOG_ERROR, "Invalid compression level: %d\n", s->compression);
        return AVERROR(EINVAL);
    }

    if (s->strip) {
        av_packet_side_data_remove(bsf->par_out->coded_side_data,
                                   &bsf->par_out->nb_coded_side_data,
                                   AV_PKT_DATA_DOVI_CONF);
    } else {
        const AVPacketSideData *sd;
        sd = av_packet_side_data_get(bsf->par_out->coded_side_data,
                                     bsf->par_out->nb_coded_side_data,
                                     AV_PKT_DATA_DOVI_CONF);

        if (sd) {
            AVDOVIDecoderConfigurationRecord *cfg;
            cfg = (AVDOVIDecoderConfigurationRecord *) sd->data;
            s->dec.cfg = *cfg;

            /* Update configuration record before setting to enc ctx */
            cfg->dv_md_compression = s->compression;
            if (s->compression && s->dec.cfg.dv_profile < 8) {
                av_log(bsf, AV_LOG_ERROR, "Invalid compression level %d for "
                       "Dolby Vision profile %d.\n", s->compression, s->dec.cfg.dv_profile);
                return AVERROR(EINVAL);
            }

            s->enc.cfg = *cfg;
        } else {
            av_log(bsf, AV_LOG_WARNING, "No Dolby Vision configuration record "
                   "found? Generating one, but results may be invalid.\n");
            ret = ff_dovi_configure_ext(&s->enc, bsf->par_out, NULL, s->compression,
                                        FF_COMPLIANCE_NORMAL);
            if (ret < 0)
                return ret;
            /* Be conservative in accepting all compressed RPUs */
            s->dec.cfg = s->enc.cfg;
            s->dec.cfg.dv_md_compression = AV_DOVI_COMPRESSION_EXTENDED;
        }
    }

    switch (bsf->par_in->codec_id) {
    case AV_CODEC_ID_HEVC:
        return ff_cbs_bsf_generic_init(bsf, &dovi_rpu_hevc_type);
    case AV_CODEC_ID_AV1:
        return ff_cbs_bsf_generic_init(bsf, &dovi_rpu_av1_type);
    default:
        return AVERROR_BUG;
    }
}

static void dovi_rpu_close(AVBSFContext *bsf)
{
    DoviRpuContext *s = bsf->priv_data;
    ff_dovi_ctx_unref(&s->dec);
    ff_dovi_ctx_unref(&s->enc);
    ff_cbs_bsf_generic_close(bsf);
}

#define OFFSET(x) offsetof(DoviRpuContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption dovi_rpu_options[] = {
    { "strip",          "Strip Dolby Vision metadata",  OFFSET(strip),       AV_OPT_TYPE_BOOL,  { .i64 = 0 }, 0, 1, FLAGS },
    { "compression",    "DV metadata compression mode", OFFSET(compression), AV_OPT_TYPE_INT,   { .i64 = AV_DOVI_COMPRESSION_LIMITED }, 0, AV_DOVI_COMPRESSION_EXTENDED, FLAGS, .unit = "compression" },
        { "none",       "Don't compress metadata",      0, AV_OPT_TYPE_CONST, {.i64 = 0},                            .flags = FLAGS, .unit = "compression" },
        { "limited",    "Limited metadata compression", 0, AV_OPT_TYPE_CONST, {.i64 = AV_DOVI_COMPRESSION_LIMITED},  .flags = FLAGS, .unit = "compression" },
        { "extended",   "Extended metadata compression",0, AV_OPT_TYPE_CONST, {.i64 = AV_DOVI_COMPRESSION_EXTENDED}, .flags = FLAGS, .unit = "compression" },
    { NULL }
};

static const AVClass dovi_rpu_class = {
    .class_name = "dovi_rpu_bsf",
    .item_name  = av_default_item_name,
    .option     = dovi_rpu_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID dovi_rpu_codec_ids[] = {
    AV_CODEC_ID_HEVC, AV_CODEC_ID_AV1, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_dovi_rpu_bsf = {
    .p.name         = "dovi_rpu",
    .p.codec_ids    = dovi_rpu_codec_ids,
    .p.priv_class   = &dovi_rpu_class,
    .priv_data_size = sizeof(DoviRpuContext),
    .init           = &dovi_rpu_init,
    .close          = &dovi_rpu_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
