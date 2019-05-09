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

#include <string.h>

#include "libavutil/log.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"

#include "avcodec.h"
#include "bsf.h"

struct AVBSFInternal {
    AVPacket *buffer_pkt;
    int eof;
};

void av_bsf_free(AVBSFContext **pctx)
{
    AVBSFContext *ctx;

    if (!pctx || !*pctx)
        return;
    ctx = *pctx;

    if (ctx->filter->close)
        ctx->filter->close(ctx);
    if (ctx->filter->priv_class && ctx->priv_data)
        av_opt_free(ctx->priv_data);

    av_opt_free(ctx);

    av_packet_free(&ctx->internal->buffer_pkt);
    av_freep(&ctx->internal);
    av_freep(&ctx->priv_data);

    avcodec_parameters_free(&ctx->par_in);
    avcodec_parameters_free(&ctx->par_out);

    av_freep(pctx);
}

static void *bsf_child_next(void *obj, void *prev)
{
    AVBSFContext *ctx = obj;
    if (!prev && ctx->filter->priv_class)
        return ctx->priv_data;
    return NULL;
}

static const AVClass bsf_class = {
    .class_name       = "AVBSFContext",
    .item_name        = av_default_item_name,
    .version          = LIBAVUTIL_VERSION_INT,
    .child_next       = bsf_child_next,
    .child_class_next = ff_bsf_child_class_next,
};

const AVClass *av_bsf_get_class(void)
{
    return &bsf_class;
}

int av_bsf_alloc(const AVBitStreamFilter *filter, AVBSFContext **pctx)
{
    AVBSFContext *ctx;
    int ret;

    ctx = av_mallocz(sizeof(*ctx));
    if (!ctx)
        return AVERROR(ENOMEM);

    ctx->av_class = &bsf_class;
    ctx->filter   = filter;

    ctx->par_in  = avcodec_parameters_alloc();
    ctx->par_out = avcodec_parameters_alloc();
    if (!ctx->par_in || !ctx->par_out) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->internal = av_mallocz(sizeof(*ctx->internal));
    if (!ctx->internal) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ctx->internal->buffer_pkt = av_packet_alloc();
    if (!ctx->internal->buffer_pkt) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    av_opt_set_defaults(ctx);

    /* allocate priv data and init private options */
    if (filter->priv_data_size) {
        ctx->priv_data = av_mallocz(filter->priv_data_size);
        if (!ctx->priv_data) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        if (filter->priv_class) {
            *(const AVClass **)ctx->priv_data = filter->priv_class;
            av_opt_set_defaults(ctx->priv_data);
        }
    }

    *pctx = ctx;
    return 0;
fail:
    av_bsf_free(&ctx);
    return ret;
}

int av_bsf_init(AVBSFContext *ctx)
{
    int ret, i;

    /* check that the codec is supported */
    if (ctx->filter->codec_ids) {
        for (i = 0; ctx->filter->codec_ids[i] != AV_CODEC_ID_NONE; i++)
            if (ctx->par_in->codec_id == ctx->filter->codec_ids[i])
                break;
        if (ctx->filter->codec_ids[i] == AV_CODEC_ID_NONE) {
            const AVCodecDescriptor *desc = avcodec_descriptor_get(ctx->par_in->codec_id);
            av_log(ctx, AV_LOG_ERROR, "Codec '%s' (%d) is not supported by the "
                   "bitstream filter '%s'. Supported codecs are: ",
                   desc ? desc->name : "unknown", ctx->par_in->codec_id, ctx->filter->name);
            for (i = 0; ctx->filter->codec_ids[i] != AV_CODEC_ID_NONE; i++) {
                desc = avcodec_descriptor_get(ctx->filter->codec_ids[i]);
                av_log(ctx, AV_LOG_ERROR, "%s (%d) ",
                       desc ? desc->name : "unknown", ctx->filter->codec_ids[i]);
            }
            av_log(ctx, AV_LOG_ERROR, "\n");
            return AVERROR(EINVAL);
        }
    }

    /* initialize output parameters to be the same as input
     * init below might overwrite that */
    ret = avcodec_parameters_copy(ctx->par_out, ctx->par_in);
    if (ret < 0)
        return ret;

    ctx->time_base_out = ctx->time_base_in;

    if (ctx->filter->init) {
        ret = ctx->filter->init(ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

void av_bsf_flush(AVBSFContext *ctx)
{
    ctx->internal->eof = 0;

    av_packet_unref(ctx->internal->buffer_pkt);

    if (ctx->filter->flush)
        ctx->filter->flush(ctx);
}

int av_bsf_send_packet(AVBSFContext *ctx, AVPacket *pkt)
{
    int ret;

    if (!pkt || (!pkt->data && !pkt->side_data_elems)) {
        ctx->internal->eof = 1;
        return 0;
    }

    if (ctx->internal->eof) {
        av_log(ctx, AV_LOG_ERROR, "A non-NULL packet sent after an EOF.\n");
        return AVERROR(EINVAL);
    }

    if (ctx->internal->buffer_pkt->data ||
        ctx->internal->buffer_pkt->side_data_elems)
        return AVERROR(EAGAIN);

    ret = av_packet_make_refcounted(pkt);
    if (ret < 0)
        return ret;
    av_packet_move_ref(ctx->internal->buffer_pkt, pkt);

    return 0;
}

int av_bsf_receive_packet(AVBSFContext *ctx, AVPacket *pkt)
{
    return ctx->filter->filter(ctx, pkt);
}

int ff_bsf_get_packet(AVBSFContext *ctx, AVPacket **pkt)
{
    AVBSFInternal *in = ctx->internal;
    AVPacket *tmp_pkt;

    if (in->eof)
        return AVERROR_EOF;

    if (!ctx->internal->buffer_pkt->data &&
        !ctx->internal->buffer_pkt->side_data_elems)
        return AVERROR(EAGAIN);

    tmp_pkt = av_packet_alloc();
    if (!tmp_pkt)
        return AVERROR(ENOMEM);

    *pkt = ctx->internal->buffer_pkt;
    ctx->internal->buffer_pkt = tmp_pkt;

    return 0;
}

int ff_bsf_get_packet_ref(AVBSFContext *ctx, AVPacket *pkt)
{
    AVBSFInternal *in = ctx->internal;

    if (in->eof)
        return AVERROR_EOF;

    if (!ctx->internal->buffer_pkt->data &&
        !ctx->internal->buffer_pkt->side_data_elems)
        return AVERROR(EAGAIN);

    av_packet_move_ref(pkt, ctx->internal->buffer_pkt);

    return 0;
}

typedef struct BSFListContext {
    const AVClass *class;

    AVBSFContext **bsfs;
    int nb_bsfs;

    unsigned idx;           // index of currently processed BSF
    unsigned flushed_idx;   // index of BSF being flushed

    char * item_name;
} BSFListContext;


static int bsf_list_init(AVBSFContext *bsf)
{
    BSFListContext *lst = bsf->priv_data;
    int ret, i;
    const AVCodecParameters *cod_par = bsf->par_in;
    AVRational tb = bsf->time_base_in;

    for (i = 0; i < lst->nb_bsfs; ++i) {
        ret = avcodec_parameters_copy(lst->bsfs[i]->par_in, cod_par);
        if (ret < 0)
            goto fail;

        lst->bsfs[i]->time_base_in = tb;

        ret = av_bsf_init(lst->bsfs[i]);
        if (ret < 0)
            goto fail;

        cod_par = lst->bsfs[i]->par_out;
        tb = lst->bsfs[i]->time_base_out;
    }

    bsf->time_base_out = tb;
    ret = avcodec_parameters_copy(bsf->par_out, cod_par);

fail:
    return ret;
}

static int bsf_list_filter(AVBSFContext *bsf, AVPacket *out)
{
    BSFListContext *lst = bsf->priv_data;
    int ret;

    if (!lst->nb_bsfs)
        return ff_bsf_get_packet_ref(bsf, out);

    while (1) {
        if (lst->idx > lst->flushed_idx) {
            ret = av_bsf_receive_packet(lst->bsfs[lst->idx-1], out);
            if (ret == AVERROR(EAGAIN)) {
                /* no more packets from idx-1, try with previous */
                ret = 0;
                lst->idx--;
                continue;
            } else if (ret == AVERROR_EOF) {
                /* filter idx-1 is done, continue with idx...nb_bsfs */
                lst->flushed_idx = lst->idx;
                continue;
            }else if (ret < 0) {
                /* filtering error */
                break;
            }
        } else {
            ret = ff_bsf_get_packet_ref(bsf, out);
            if (ret == AVERROR_EOF) {
                lst->idx = lst->flushed_idx;
            } else if (ret < 0)
                break;
        }

        if (lst->idx < lst->nb_bsfs) {
            AVPacket *pkt;
            if (ret == AVERROR_EOF && lst->idx == lst->flushed_idx) {
                /* ff_bsf_get_packet_ref returned EOF and idx is first
                 * filter of yet not flushed filter chain */
                pkt = NULL;
            } else {
                pkt = out;
            }
            ret = av_bsf_send_packet(lst->bsfs[lst->idx], pkt);
            if (ret < 0)
                break;
            lst->idx++;
        } else {
            /* The end of filter chain, break to return result */
            break;
        }
    }

    if (ret < 0)
        av_packet_unref(out);

    return ret;
}

static void bsf_list_flush(AVBSFContext *bsf)
{
    BSFListContext *lst = bsf->priv_data;

    for (int i = 0; i < lst->nb_bsfs; i++)
        av_bsf_flush(lst->bsfs[i]);
    lst->idx = lst->flushed_idx = 0;
}

static void bsf_list_close(AVBSFContext *bsf)
{
    BSFListContext *lst = bsf->priv_data;
    int i;

    for (i = 0; i < lst->nb_bsfs; ++i)
        av_bsf_free(&lst->bsfs[i]);
    av_freep(&lst->bsfs);
    av_freep(&lst->item_name);
}

static const char *bsf_list_item_name(void *ctx)
{
    static const char *null_filter_name = "null";
    AVBSFContext *bsf_ctx = ctx;
    BSFListContext *lst = bsf_ctx->priv_data;

    if (!lst->nb_bsfs)
        return null_filter_name;

    if (!lst->item_name) {
        int i;
        AVBPrint bp;
        av_bprint_init(&bp, 16, 128);

        av_bprintf(&bp, "bsf_list(");
        for (i = 0; i < lst->nb_bsfs; i++)
            av_bprintf(&bp, i ? ",%s" : "%s", lst->bsfs[i]->filter->name);
        av_bprintf(&bp, ")");

        av_bprint_finalize(&bp, &lst->item_name);
    }

    return lst->item_name;
}

static const AVClass bsf_list_class = {
        .class_name = "bsf_list",
        .item_name  = bsf_list_item_name,
        .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_list_bsf = {
        .name           = "bsf_list",
        .priv_data_size = sizeof(BSFListContext),
        .priv_class     = &bsf_list_class,
        .init           = bsf_list_init,
        .filter         = bsf_list_filter,
        .flush          = bsf_list_flush,
        .close          = bsf_list_close,
};

struct AVBSFList {
    AVBSFContext **bsfs;
    int nb_bsfs;
};

AVBSFList *av_bsf_list_alloc(void)
{
    return av_mallocz(sizeof(AVBSFList));
}

void av_bsf_list_free(AVBSFList **lst)
{
    int i;

    if (!*lst)
        return;

    for (i = 0; i < (*lst)->nb_bsfs; ++i)
        av_bsf_free(&(*lst)->bsfs[i]);
    av_free((*lst)->bsfs);
    av_freep(lst);
}

int av_bsf_list_append(AVBSFList *lst, AVBSFContext *bsf)
{
    return av_dynarray_add_nofree(&lst->bsfs, &lst->nb_bsfs, bsf);
}

int av_bsf_list_append2(AVBSFList *lst, const char *bsf_name, AVDictionary ** options)
{
    int ret;
    const AVBitStreamFilter *filter;
    AVBSFContext *bsf;

    filter = av_bsf_get_by_name(bsf_name);
    if (!filter)
        return AVERROR_BSF_NOT_FOUND;

    ret = av_bsf_alloc(filter, &bsf);
    if (ret < 0)
        return ret;

    if (options) {
        ret = av_opt_set_dict2(bsf, options, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0)
            goto end;
    }

    ret = av_bsf_list_append(lst, bsf);

end:
    if (ret < 0)
        av_bsf_free(&bsf);

    return ret;
}

int av_bsf_list_finalize(AVBSFList **lst, AVBSFContext **bsf)
{
    int ret = 0;
    BSFListContext *ctx;

    if ((*lst)->nb_bsfs == 1) {
        *bsf = (*lst)->bsfs[0];
        av_freep(&(*lst)->bsfs);
        (*lst)->nb_bsfs = 0;
        goto end;
    }

    ret = av_bsf_alloc(&ff_list_bsf, bsf);
    if (ret < 0)
        return ret;

    ctx = (*bsf)->priv_data;

    ctx->bsfs = (*lst)->bsfs;
    ctx->nb_bsfs = (*lst)->nb_bsfs;

end:
    av_freep(lst);
    return ret;
}

static int bsf_parse_single(const char *str, AVBSFList *bsf_lst)
{
    char *bsf_name, *bsf_options_str, *buf;
    AVDictionary *bsf_options = NULL;
    int ret = 0;

    if (!(buf = av_strdup(str)))
        return AVERROR(ENOMEM);

    bsf_name = av_strtok(buf, "=", &bsf_options_str);
    if (!bsf_name) {
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (bsf_options_str) {
        ret = av_dict_parse_string(&bsf_options, bsf_options_str, "=", ":", 0);
        if (ret < 0)
            goto end;
    }

    ret = av_bsf_list_append2(bsf_lst, bsf_name, &bsf_options);

    av_dict_free(&bsf_options);
end:
    av_free(buf);
    return ret;
}

int av_bsf_list_parse_str(const char *str, AVBSFContext **bsf_lst)
{
    AVBSFList *lst;
    char *bsf_str, *buf, *dup, *saveptr;
    int ret;

    if (!str)
        return av_bsf_get_null_filter(bsf_lst);

    lst = av_bsf_list_alloc();
    if (!lst)
        return AVERROR(ENOMEM);

    if (!(dup = buf = av_strdup(str))) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while (1) {
        bsf_str = av_strtok(buf, ",", &saveptr);
        if (!bsf_str)
            break;

        ret = bsf_parse_single(bsf_str, lst);
        if (ret < 0)
            goto end;

        buf = NULL;
    }

    ret = av_bsf_list_finalize(&lst, bsf_lst);
end:
    if (ret < 0)
        av_bsf_list_free(&lst);
    av_free(dup);
    return ret;
}

int av_bsf_get_null_filter(AVBSFContext **bsf)
{
    return av_bsf_alloc(&ff_list_bsf, bsf);
}
