/**
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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "libavutil/tree.h"
#include "ops.h"
#include "ops_dispatch.h"
#include "op_list_gen_template.c"
#include "swscale.h"
#include "uops.h"
#include "uops_list.h"

static const struct {
    char full[32];
    char abbr[32];
} uop_names[SWS_UOP_TYPE_NB] = {
#define UOP_NAME(OP, ABBR) [OP] = { #OP, ABBR },
    UOPS_LIST(UOP_NAME)
};

static const struct {
    char full[16];
    char prefix[8];
} pixel_types[SWS_PIXEL_TYPE_NB] = {
    [SWS_PIXEL_NONE] = { "SWS_PIXEL_NONE", ""     },
    [SWS_PIXEL_U8]   = { "SWS_PIXEL_U8",   "U8_"  },
    [SWS_PIXEL_U16]  = { "SWS_PIXEL_U16",  "U16_" },
    [SWS_PIXEL_U32]  = { "SWS_PIXEL_U32",  "U32_" },
    [SWS_PIXEL_F32]  = { "SWS_PIXEL_F32",  "F32_" },
};

static int generate_entry_struct(void *opaque, void *key)
{
    const SwsUOp *ref = opaque;
    const SwsUOp *uop = key;
    AVBPrint *bp = ref->data.opaque;
    char name[SWS_UOP_NAME_MAX];
    ff_sws_uop_name(uop, name);
    av_bprintf(bp, " \\\n    MACRO(__VA_ARGS__, %-40s", name);
    av_bprintf(bp, ", .type = %-13s, .uop = %-24s, .mask = 0x%x",
               pixel_types[uop->type].full, uop_names[uop->uop].full, uop->mask);

    const SwsUOpParams *par = &uop->par;
    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_bprintf(bp, ", .par.filter.type = %s", pixel_types[par->filter.type].full);
        break;
    case SWS_UOP_RW_SHUFFLE:
        av_bprintf(bp, ", .par.shuffle.clear_value = 0x%x"
                       ", .par.shuffle.read_size = %u"
                       ", .par.shuffle.write_size = %u",
                   par->shuffle.clear_value,
                   par->shuffle.read_size, par->shuffle.write_size);
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(bp, ", .par.shift.amount = %u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprintf(bp, ", .par.move.num_moves = %d", par->move.num_moves);
        av_bprintf(bp, ", .par.move.dst = {%d, %d, %d, %d, %d, %d}",
                   par->move.dst[0], par->move.dst[1], par->move.dst[2],
                   par->move.dst[3], par->move.dst[4], par->move.dst[5]);
        av_bprintf(bp, ", .par.move.src = {%d, %d, %d, %d, %d, %d}",
                   par->move.src[0], par->move.src[1], par->move.src[2],
                   par->move.src[3], par->move.src[4], par->move.src[5]);
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprintf(bp, ", .par.pack.pattern = {%d, %d, %d, %d}",
                   par->pack.pattern[0], par->pack.pattern[1],
                   par->pack.pattern[2], par->pack.pattern[3]);
        break;
    case SWS_UOP_CLEAR:
        av_bprintf(bp, ", .par.clear.one = 0x%x, .par.clear.zero = 0x%x",
                   par->clear.one, par->clear.zero);
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        av_bprintf(bp, ", .par.lin.one = 0x%x, .par.lin.zero = 0x%x",
                   par->lin.one, par->lin.zero);
        if (uop->uop == SWS_UOP_LINEAR_FMA)
            av_bprintf(bp, ", .par.lin.exact = 0x%x", par->lin.exact);
        break;
    case SWS_UOP_DITHER:
        av_bprintf(bp, ", .par.dither = { .y_offset = {%u, %u, %u, %u}, .size_log2 = %u }",
                   par->dither.y_offset[0], par->dither.y_offset[1],
                   par->dither.y_offset[2], par->dither.y_offset[3],
                   par->dither.size_log2);
        break;
    case SWS_UOP_LUT_3D:
        av_bprintf(bp, ", .par.lut3d.dynamic = %d", par->lut3d.dynamic);
        break;
    }

    av_bprintf(bp, ")");
    return 0;
}

static int generate_entry_args(void *opaque, void *key)
{
    const SwsUOp *ref = opaque;
    const SwsUOp *uop = key;
    AVBPrint *bp = ref->data.opaque;
    char name[SWS_UOP_NAME_MAX];
    ff_sws_uop_name(uop, name);
    av_bprintf(bp, " \\\n    MACRO(__VA_ARGS__, %-40s, %-13s, %-24s, 0x%x",
               name, pixel_types[uop->type].full, uop_names[uop->uop].full, uop->mask);

    const SwsUOpParams *par = &uop->par;
    switch (uop->uop) {
    case SWS_UOP_READ_PLANAR_FH:
    case SWS_UOP_READ_PLANAR_FV:
    case SWS_UOP_READ_PLANAR_FV_FMA:
        av_bprintf(bp, ", %s", pixel_types[par->filter.type].full);
        break;
    case SWS_UOP_RW_SHUFFLE:
        av_bprintf(bp, ", 0x%x, %u, %u", par->shuffle.clear_value,
                   par->shuffle.read_size, par->shuffle.write_size);
        break;
    case SWS_UOP_LSHIFT:
    case SWS_UOP_RSHIFT:
        av_bprintf(bp, ", %u", par->shift.amount);
        break;
    case SWS_UOP_PERMUTE:
    case SWS_UOP_COPY:
        av_bprintf(bp, ", %d", par->move.num_moves);
        av_bprintf(bp, ", %d, %d, %d, %d, %d, %d",
                   par->move.dst[0], par->move.dst[1], par->move.dst[2],
                   par->move.dst[3], par->move.dst[4], par->move.dst[5]);
        av_bprintf(bp, ", %d, %d, %d, %d, %d, %d",
                   par->move.src[0], par->move.src[1], par->move.src[2],
                   par->move.src[3], par->move.src[4], par->move.src[5]);
        break;
    case SWS_UOP_PACK:
    case SWS_UOP_UNPACK:
        av_bprintf(bp, ", %d, %d, %d, %d",
                   par->pack.pattern[0], par->pack.pattern[1],
                   par->pack.pattern[2], par->pack.pattern[3]);
        break;
    case SWS_UOP_CLEAR:
        av_bprintf(bp, ", 0x%05x, 0x%05x", par->clear.one, par->clear.zero);
        break;
    case SWS_UOP_LINEAR:
    case SWS_UOP_LINEAR_FMA:
        av_bprintf(bp, ", 0x%05x, 0x%05x", par->lin.one, par->lin.zero);
        if (uop->uop == SWS_UOP_LINEAR_FMA)
            av_bprintf(bp, ", 0x%05x", par->lin.exact);
        break;
    case SWS_UOP_DITHER:
        av_bprintf(bp, ", %u, %u, %u, %u, %u",
                   par->dither.y_offset[0], par->dither.y_offset[1],
                   par->dither.y_offset[2], par->dither.y_offset[3],
                   par->dither.size_log2);
        break;
    case SWS_UOP_LUT_3D:
        av_bprintf(bp, ", %d", par->lut3d.dynamic);
        break;
    }

    av_bprintf(bp, ")");
    return 0;
}

struct EnumPriv {
    struct AVTreeNode *root;
    AVMutex lock;
};

static int register_uop(struct EnumPriv *s, const SwsUOp *uop)
{
    SwsUOp *key = av_memdup(uop, sizeof(*uop));
    if (!key)
        return AVERROR(ENOMEM);
    memset(&key->data, 0, sizeof(key->data));

    struct AVTreeNode *node = av_tree_node_alloc();
    if (!node) {
        av_free(key);
        return AVERROR(ENOMEM);
    }

    ff_mutex_lock(&s->lock);
    av_tree_insert(&s->root, key, ff_sws_uop_cmp_v, &node);
    ff_mutex_unlock(&s->lock);
    if (node) {
        av_free(node);
        av_free(key);
    }
    return 0;
}

static int register_flags(SwsContext *ctx, const SwsOpList *ops, SwsUOpFlags flags)
{
    SwsUOpList *uops = ff_sws_uop_list_alloc();
    if (!uops)
        return AVERROR(ENOMEM);

    int ret = ff_sws_ops_translate(ctx, ops, flags, uops);
    if (ret < 0)
        goto fail;

    for (int i = 0; i < uops->num_ops; i++) {
        ret = register_uop(ctx->opaque, &uops->ops[i]);
        if (ret < 0)
            goto fail;
    }

fail:
    ff_sws_uop_list_free(&uops);
    return ret;
}

static const SwsUOpFlags uop_flags[] = {
    0,
    SWS_UOP_FLAG_PSHUFB | SWS_UOP_FLAG_FMA, /* x86 backend */
};

static int register_uops(SwsContext *ctx, const SwsOpList *ops,
                         SwsCompiledOp *out)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(uop_flags); i++) {
        int ret = register_flags(ctx, ops, uop_flags[i]);
        if (ret < 0)
            return ret;
    }

    *out = (SwsCompiledOp) {0}; /* dummy value, will be immediately freed */
    return 0;
}

/* Dummy backend that just registers all seen uops */
static const SwsOpBackend backend_uops = {
    .name    = "uops_gen",
    .compile = register_uops,
};

static int register_all_uops(SwsContext *ctx, void *graph, SwsOpList *ops)
{
    /* ff_sws_compile_pass() takes over ownership of `ops` */
    SwsOpList *copy = ff_sws_op_list_duplicate(ops);
    if (!copy)
        return AVERROR(ENOMEM);

    const int flags = SWS_OP_FLAG_DRY_RUN | SWS_OP_FLAG_SPLIT_MEMCPY;
    return ff_sws_compile_pass(graph, &backend_uops, &copy, flags, NULL, NULL);
}

static const SwsFlags flags_list[] = {
    0,
    SWS_ACCURATE_RND,   /* may insert extra 1x1 dither ops (for accurate rounding) */
    SWS_BITEXACT,       /* prevents some FMA optimizations */
    SWS_ACCURATE_RND | SWS_BITEXACT,
};

/* Limit the range of av_tree_enumerate() to only matching uop and type */
static int enum_type(void *opaque, void *elem)
{
    const SwsUOp *a = opaque, *b = elem;
    if (a->type != b->type)
        return (int) b->type - a->type;
    if (a->uop != b->uop)
        return (int) b->uop - a->uop;
    return 0;
}

static int free_uop_key(void *opaque, void *key)
{
    av_free(key);
    return 0;
}

/**
 * Generate a set of boilerplate C preprocessor macros for describing and
 * programmatically iterating over all possible SwsUOps.
 *
 * This function can be quite slow as it iterates over every possible
 * combination of pixel formats and flags.
 *
 * Returns 0 or a negative error code. On success, an allocated string is
 * returned via `out_str`, and must be av_free()'d by the caller.
 */
static int sws_uops_macros_gen(char **out_str)
{
    struct EnumPriv s = {0};
    SwsLut3D *lut3d = NULL;
    int ret = ff_mutex_init(&s.lock, NULL);
    if (ret)
        return AVERROR(ENOSYS);

    AVBPrint bprint, *const bp = &bprint;
    av_bprint_init(bp, 0, AV_BPRINT_SIZE_UNLIMITED);

    /* Allocate dummy graph and context for ff_sws_compile_pass() */
    SwsGraph *graph = ff_sws_graph_alloc();
    SwsContext *ctx = sws_alloc_context();
    if (!graph || !ctx) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    /* Use this to plumb the enum state through all the layers of abstraction */
    graph->ctx = ctx;
    ctx->opaque = &s;
    ctx->scaler = SWS_SCALE_BILINEAR; /* cheaper to generate filter kernels */
    ctx->threads = 0; /* use slice threading to speed up tree building */

    /* Allocate dummy 3DLUT to force generation of SWS_UOP_LUT_3D */
    lut3d = ff_sws_lut3d_alloc();
    if (!lut3d) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    ret = ff_sws_enum_op_lists(ctx, graph, lut3d, AV_PIX_FMT_NONE,
                               AV_PIX_FMT_NONE, register_all_uops);
    if (ret < 0)
        goto fail;

    lut3d->dynamic = true;
    ret = ff_sws_enum_op_lists(ctx, graph, lut3d, AV_PIX_FMT_NONE,
                               AV_PIX_FMT_NONE, register_all_uops);
    if (ret < 0)
        goto fail;

    /* Register all unique uops over every relevant combination of flags */
    for (int i = 0; i < FF_ARRAY_ELEMS(flags_list); i++) {
        ctx->flags = flags_list[i];
        ret = ff_sws_enum_op_lists(ctx, graph, NULL, AV_PIX_FMT_NONE,
                                   AV_PIX_FMT_NONE, register_all_uops);
        if (ret < 0)
            goto fail;
    }

    #define BPRINT_STR(str) av_bprint_append_data(bp, str, strlen(str))
    BPRINT_STR(
"/**\n"
" * This file is automatically generated. Do not edit manually.\n"
" * To regenerate, run: make fate-sws-uops-macros GEN=1\n"
" */\n"
"\n"
"#ifndef SWSCALE_UOPS_MACROS_H\n"
"#define SWSCALE_UOPS_MACROS_H\n"
"\n"
"/**\n"
" * Boilerplate helper macros, for template-based backends. These will be\n"
" * instantiated like this, with parameters in struct order:\n"
" *   MACRO(__VA_ARGS__, NAME, UOP, TYPE, MASK, [PARAMS,])\n"
" * The _STRUCT variants pass all arguments in C struct syntax, while the\n"
" * plain variants give them as separate C values (e.g. for use in calls)\n"
" */\n"
"#define SWS_GLUE3(x, y, z) x ## _ ## y ## _ ## z\n"
"#define SWS_FOR(TYPE, UOP, MACRO, ...) \\\n"
"    SWS_GLUE3(SWS_FOR, TYPE, UOP)(MACRO, __VA_ARGS__)\n"
"#define SWS_FOR_STRUCT(TYPE, UOP, MACRO, ...) \\\n"
"    SWS_GLUE3(SWS_FOR_STRUCT, TYPE, UOP)(MACRO, __VA_ARGS__)\n"
"\n");

    SwsUOp key = { .data.opaque = bp };
    for (key.type = SWS_PIXEL_NONE + 1; key.type < SWS_PIXEL_TYPE_NB; key.type++) {
        for (key.uop = SWS_UOP_INVALID + 1; key.uop < SWS_UOP_TYPE_NB; key.uop++) {
            const char *macro  = uop_names[key.uop].full + sizeof("SWS_UOP_") - 1;
            const char *prefix = pixel_types[key.type].prefix;
            av_bprintf(bp, "#define SWS_FOR_%s%s(MACRO, ...)", prefix, macro);
            av_tree_enumerate(s.root, &key, enum_type, generate_entry_args);
            av_bprintf(bp, "\n");
            av_bprintf(bp, "#define SWS_FOR_STRUCT_%s%s(MACRO, ...)", prefix, macro);
            av_tree_enumerate(s.root, &key, enum_type, generate_entry_struct);
            av_bprintf(bp, "\n");
        }
    }

    BPRINT_STR("\n#endif /* SWSCALE_UOPS_MACROS_H */");
    ret = av_bprint_finalize(bp, out_str);

fail:
    av_refstruct_unref(&lut3d);
    av_bprint_finalize(bp, NULL);
    av_tree_enumerate(s.root, NULL, NULL, free_uop_key);
    av_tree_destroy(s.root);
    ff_mutex_destroy(&s.lock);
    ff_sws_graph_free(&graph);
    sws_free_context(&ctx);
    return ret;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    char *macros = NULL;
    int ret = sws_uops_macros_gen(&macros);
    if (ret >= 0)
        puts(macros);
    av_free(macros);
    return ret;
}
