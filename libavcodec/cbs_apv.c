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

#include "libavutil/mem.h"
#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_apv.h"


static int cbs_apv_get_num_comp(const APVRawFrameHeader *fh)
{
    switch (fh->frame_info.chroma_format_idc) {
    case APV_CHROMA_FORMAT_400:
        return 1;
    case APV_CHROMA_FORMAT_422:
    case APV_CHROMA_FORMAT_444:
        return 3;
    case APV_CHROMA_FORMAT_4444:
        return 4;
    default:
        av_assert0(0 && "Invalid chroma_format_idc");
    }
}

static void cbs_apv_derive_tile_info(APVDerivedTileInfo *ti,
                                     const APVRawFrameHeader *fh)
{
    int frame_width_in_mbs   = (fh->frame_info.frame_width  + 15) / 16;
    int frame_height_in_mbs  = (fh->frame_info.frame_height + 15) / 16;
    int start_mb, i;

    start_mb = 0;
    for (i = 0; start_mb < frame_width_in_mbs; i++) {
        ti->col_starts[i] = start_mb * APV_MB_WIDTH;
        start_mb += fh->tile_info.tile_width_in_mbs;
    }
    av_assert0(i <= APV_MAX_TILE_COLS);
    ti->col_starts[i] = frame_width_in_mbs * APV_MB_WIDTH;
    ti->tile_cols = i;

    start_mb = 0;
    for (i = 0; start_mb < frame_height_in_mbs; i++) {
        av_assert0(i < APV_MAX_TILE_ROWS);
        ti->row_starts[i] = start_mb * APV_MB_HEIGHT;
        start_mb += fh->tile_info.tile_height_in_mbs;
    }
    av_assert0(i <= APV_MAX_TILE_ROWS);
    ti->row_starts[i] = frame_height_in_mbs * APV_MB_HEIGHT;
    ti->tile_rows = i;

    ti->num_tiles = ti->tile_cols * ti->tile_rows;
}


#define HEADER(name) do { \
        ff_cbs_trace_header(ctx, name); \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)


#define u(width, name, range_min, range_max) \
    xu(width, name, current->name, range_min, range_max, 0, )
#define ub(width, name) \
    xu(width, name, current->name, 0, MAX_UINT_BITS(width), 0, )
#define us(width, name, range_min, range_max, subs, ...) \
    xu(width, name, current->name, range_min, range_max,  subs, __VA_ARGS__)
#define ubs(width, name, subs, ...) \
    xu(width, name, current->name, 0, MAX_UINT_BITS(width),  subs, __VA_ARGS__)

#define fixed(width, name, value) do { \
        av_unused uint32_t fixed_value = value; \
        xu(width, name, fixed_value, value, value, 0, ); \
    } while (0)


#define READ
#define READWRITE read
#define RWContext GetBitContext
#define FUNC(name) cbs_apv_read_ ## name

#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        var = value; \
    } while (0)

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

#define byte_alignment(rw) (get_bits_count(rw) % 8)

#include "cbs_apv_syntax_template.c"

#undef READ
#undef READWRITE
#undef RWContext
#undef FUNC
#undef xu
#undef infer
#undef byte_alignment

#define WRITE
#define READWRITE write
#define RWContext PutBitContext
#define FUNC(name) cbs_apv_write_ ## name

#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        uint32_t value = var; \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    value, range_min, range_max)); \
    } while (0)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, \
                   "%s does not match inferred value: " \
                   "%"PRId64", but should be %"PRId64".\n", \
                   #name, (int64_t)current->name, (int64_t)(value)); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define byte_alignment(rw) (put_bits_count(rw) % 8)

#include "cbs_apv_syntax_template.c"

#undef WRITE
#undef READWRITE
#undef RWContext
#undef FUNC
#undef xu
#undef infer
#undef byte_alignment


static int cbs_apv_split_fragment(CodedBitstreamContext *ctx,
                                  CodedBitstreamFragment *frag,
                                  int header)
{
    uint8_t *data = frag->data;
    size_t   size = frag->data_size;
    uint32_t signature;
    int err, trace;

    if (header || !frag->data_size) {
        // Ignore empty or extradata fragments.
        return 0;
    }

    if (frag->data_size < 4) {
        // Too small to be a valid fragment.
        return AVERROR_INVALIDDATA;
    }

    // Don't include parsing here in trace output.
    trace = ctx->trace_enable;
    ctx->trace_enable = 0;

    signature = AV_RB32(data);
    if (signature != APV_SIGNATURE) {
        av_log(ctx->log_ctx, AV_LOG_ERROR,
               "Invalid APV access unit: bad signature %08x.\n",
               signature);
        err = AVERROR_INVALIDDATA;
        goto fail;
    }
    data += 4;
    size -= 4;

    while (size > 0) {
        GetBitContext   gbc;
        uint32_t        pbu_size;
        APVRawPBUHeader pbu_header;

        if (size < 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid PBU: "
                   "fragment too short (%"SIZE_SPECIFIER" bytes).\n",
                   size);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        pbu_size = AV_RB32(data);
        if (pbu_size < 8) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid PBU: "
                   "pbu_size too small (%"PRIu32" bytes).\n",
                   pbu_size);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        data += 4;
        size -= 4;

        if (pbu_size > size) {
            av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid PBU: "
                   "pbu_size too large (%"PRIu32" bytes).\n",
                   pbu_size);
            err = AVERROR_INVALIDDATA;
            goto fail;
        }

        init_get_bits(&gbc, data, 8 * pbu_size);

        err = cbs_apv_read_pbu_header(ctx, &gbc, &pbu_header);
        if (err < 0)
            goto fail;

        // Could select/skip frames based on type/group_id here.

        err = ff_cbs_append_unit_data(frag, pbu_header.pbu_type,
                                      data, pbu_size, frag->data_ref);
        if (err < 0)
            goto fail;

        data += pbu_size;
        size -= pbu_size;
    }

    err = 0;
fail:
    ctx->trace_enable = trace;
    return err;
}

static int cbs_apv_read_unit(CodedBitstreamContext *ctx,
                             CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    err = ff_cbs_alloc_unit_content(ctx, unit);
    if (err < 0)
        return err;

    switch (unit->type) {
    case APV_PBU_PRIMARY_FRAME:
    case APV_PBU_NON_PRIMARY_FRAME:
    case APV_PBU_PREVIEW_FRAME:
    case APV_PBU_DEPTH_FRAME:
    case APV_PBU_ALPHA_FRAME:
        {
            APVRawFrame *frame = unit->content;

            err = cbs_apv_read_frame(ctx, &gbc, frame);
            if (err < 0)
                return err;

            // Each tile inside the frame has pointers into the unit
            // data buffer; make a single reference here for all of
            // them together.
            frame->tile_data_ref = av_buffer_ref(unit->data_ref);
            if (!frame->tile_data_ref)
                return AVERROR(ENOMEM);
        }
        break;
    case APV_PBU_ACCESS_UNIT_INFORMATION:
        {
            err = cbs_apv_read_au_info(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    case APV_PBU_METADATA:
        {
            err = cbs_apv_read_metadata(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    case APV_PBU_FILLER:
        {
            err = cbs_apv_read_filler(ctx, &gbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_apv_write_unit(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit,
                              PutBitContext *pbc)
{
    int err;

    switch (unit->type) {
    case APV_PBU_PRIMARY_FRAME:
    case APV_PBU_NON_PRIMARY_FRAME:
    case APV_PBU_PREVIEW_FRAME:
    case APV_PBU_DEPTH_FRAME:
    case APV_PBU_ALPHA_FRAME:
        {
            APVRawFrame *frame = unit->content;

            err = cbs_apv_write_frame(ctx, pbc, frame);
            if (err < 0)
                return err;
        }
        break;
    case APV_PBU_ACCESS_UNIT_INFORMATION:
        {
            err = cbs_apv_write_au_info(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    case APV_PBU_METADATA:
        {
            err = cbs_apv_write_metadata(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    case APV_PBU_FILLER:
        {
            err = cbs_apv_write_filler(ctx, pbc, unit->content);
            if (err < 0)
                return err;
        }
        break;
    default:
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int cbs_apv_assemble_fragment(CodedBitstreamContext *ctx,
                                     CodedBitstreamFragment *frag)
{
    size_t size = 4, pos;

    for (int i = 0; i < frag->nb_units; i++)
        size += frag->units[i].data_size + 4;

    frag->data_ref = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!frag->data_ref)
        return AVERROR(ENOMEM);
    frag->data = frag->data_ref->data;
    memset(frag->data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    AV_WB32(frag->data, APV_SIGNATURE);
    pos = 4;
    for (int i = 0; i < frag->nb_units; i++) {
        AV_WB32(frag->data + pos, frag->units[i].data_size);
        pos += 4;

        memcpy(frag->data + pos, frag->units[i].data,
               frag->units[i].data_size);
        pos += frag->units[i].data_size;
    }
    av_assert0(pos == size);
    frag->data_size = size;

    return 0;
}


static void cbs_apv_free_metadata(AVRefStructOpaque unused, void *content)
{
    APVRawMetadata *md = content;
    av_assert0(md->pbu_header.pbu_type == APV_PBU_METADATA);

    for (int i = 0; i < md->metadata_count; i++) {
        APVRawMetadataPayload *pl = &md->payloads[i];

        switch (pl->payload_type) {
        case APV_METADATA_MDCV:
        case APV_METADATA_CLL:
        case APV_METADATA_FILLER:
            break;
        case APV_METADATA_ITU_T_T35:
            av_buffer_unref(&pl->itu_t_t35.data_ref);
            break;
        case APV_METADATA_USER_DEFINED:
            av_buffer_unref(&pl->user_defined.data_ref);
            break;
        default:
            av_buffer_unref(&pl->undefined.data_ref);
        }
    }
}

static const CodedBitstreamUnitTypeDescriptor cbs_apv_unit_types[] = {
    {
        .nb_unit_types   = CBS_UNIT_TYPE_RANGE,
        .unit_type.range = {
            .start       = APV_PBU_PRIMARY_FRAME,
            .end         = APV_PBU_ALPHA_FRAME,
        },
        .content_type    = CBS_CONTENT_TYPE_INTERNAL_REFS,
        .content_size    = sizeof(APVRawFrame),
        .type.ref        = {
            .nb_offsets  = 1,
            .offsets     = { offsetof(APVRawFrame, tile_data_ref) -
                             sizeof(void*) },
        },
    },

    CBS_UNIT_TYPE_COMPLEX(APV_PBU_METADATA, APVRawMetadata,
                          &cbs_apv_free_metadata),

    CBS_UNIT_TYPE_POD(APV_PBU_ACCESS_UNIT_INFORMATION, APVRawAUInfo),
    CBS_UNIT_TYPE_POD(APV_PBU_FILLER,                  APVRawFiller),

    CBS_UNIT_TYPE_END_OF_LIST
};

const CodedBitstreamType ff_cbs_type_apv = {
    .codec_id          = AV_CODEC_ID_APV,

    .priv_data_size    = sizeof(CodedBitstreamAPVContext),

    .unit_types        = cbs_apv_unit_types,

    .split_fragment    = &cbs_apv_split_fragment,
    .read_unit         = &cbs_apv_read_unit,
    .write_unit        = &cbs_apv_write_unit,
    .assemble_fragment = &cbs_apv_assemble_fragment,
};
