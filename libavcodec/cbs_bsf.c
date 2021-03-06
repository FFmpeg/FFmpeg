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

#include "bsf_internal.h"
#include "cbs_bsf.h"

static int cbs_bsf_update_side_data(AVBSFContext *bsf, AVPacket *pkt)
{
    CBSBSFContext           *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->fragment;
    uint8_t *side_data;
    buffer_size_t side_data_size;
    int err;

    side_data = av_packet_get_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                        &side_data_size);
    if (!side_data_size)
        return 0;

    err = ff_cbs_read(ctx->input, frag, side_data, side_data_size);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR,
               "Failed to read extradata from packet side data.\n");
        return err;
    }

    err = ctx->type->update_fragment(bsf, NULL, frag);
    if (err < 0)
        return err;

    err = ff_cbs_write_fragment_data(ctx->output, frag);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR,
               "Failed to write extradata into packet side data.\n");
        return err;
    }

    side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_NEW_EXTRADATA,
                                        frag->data_size);
    if (!side_data)
        return AVERROR(ENOMEM);
    memcpy(side_data, frag->data, frag->data_size);

    ff_cbs_fragment_reset(frag);
    return 0;
}

int ff_cbs_bsf_generic_filter(AVBSFContext *bsf, AVPacket *pkt)
{
    CBSBSFContext           *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->fragment;
    int err;

    err = ff_bsf_get_packet_ref(bsf, pkt);
    if (err < 0)
        return err;

    err = cbs_bsf_update_side_data(bsf, pkt);
    if (err < 0)
        goto fail;

    err = ff_cbs_read_packet(ctx->input, frag, pkt);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to read %s from packet.\n",
               ctx->type->fragment_name);
        goto fail;
    }

    if (frag->nb_units == 0) {
        av_log(bsf, AV_LOG_ERROR, "No %s found in packet.\n",
               ctx->type->unit_name);
        err = AVERROR_INVALIDDATA;
        goto fail;
    }

    err = ctx->type->update_fragment(bsf, pkt, frag);
    if (err < 0)
        goto fail;

    err = ff_cbs_write_packet(ctx->output, pkt, frag);
    if (err < 0) {
        av_log(bsf, AV_LOG_ERROR, "Failed to write %s into packet.\n",
               ctx->type->fragment_name);
        goto fail;
    }

    err = 0;
fail:
    ff_cbs_fragment_reset(frag);

    if (err < 0)
        av_packet_unref(pkt);

    return err;
}

int ff_cbs_bsf_generic_init(AVBSFContext *bsf, const CBSBSFType *type)
{
    CBSBSFContext           *ctx = bsf->priv_data;
    CodedBitstreamFragment *frag = &ctx->fragment;
    int err;

    ctx->type = type;

    err = ff_cbs_init(&ctx->input, type->codec_id, bsf);
    if (err < 0)
        return err;

    err = ff_cbs_init(&ctx->output, type->codec_id, bsf);
    if (err < 0)
        return err;

    if (bsf->par_in->extradata) {
        err = ff_cbs_read_extradata(ctx->input, frag, bsf->par_in);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to read extradata.\n");
            goto fail;
        }

        err = type->update_fragment(bsf, NULL, frag);
        if (err < 0)
            goto fail;

        err = ff_cbs_write_extradata(ctx->output, bsf->par_out, frag);
        if (err < 0) {
            av_log(bsf, AV_LOG_ERROR, "Failed to write extradata.\n");
            goto fail;
        }
    }

    err = 0;
fail:
    ff_cbs_fragment_reset(frag);
    return err;
}

void ff_cbs_bsf_generic_close(AVBSFContext *bsf)
{
    CBSBSFContext *ctx = bsf->priv_data;

    ff_cbs_fragment_free(&ctx->fragment);
    ff_cbs_close(&ctx->input);
    ff_cbs_close(&ctx->output);
}
