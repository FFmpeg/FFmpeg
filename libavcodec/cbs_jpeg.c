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

#include "cbs.h"
#include "cbs_internal.h"
#include "cbs_jpeg.h"


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
    xu(width, name, range_min, range_max, 0, )
#define us(width, name, sub, range_min, range_max) \
    xu(width, name, range_min, range_max, 1, sub)


#define READ
#define READWRITE read
#define RWContext GetBitContext
#define FUNC(name) cbs_jpeg_read_ ## name

#define xu(width, name, range_min, range_max, subs, ...) do { \
        uint32_t value; \
        CHECK(ff_cbs_read_unsigned(ctx, rw, width, #name, \
                                   SUBSCRIPTS(subs, __VA_ARGS__), \
                                   &value, range_min, range_max)); \
        current->name = value; \
    } while (0)

#include "cbs_jpeg_syntax_template.c"

#undef READ
#undef READWRITE
#undef RWContext
#undef FUNC
#undef xu

#define WRITE
#define READWRITE write
#define RWContext PutBitContext
#define FUNC(name) cbs_jpeg_write_ ## name

#define xu(width, name, range_min, range_max, subs, ...) do { \
        uint32_t value = current->name; \
        CHECK(ff_cbs_write_unsigned(ctx, rw, width, #name, \
                                    SUBSCRIPTS(subs, __VA_ARGS__), \
                                    value, range_min, range_max)); \
    } while (0)


#include "cbs_jpeg_syntax_template.c"

#undef WRITE
#undef READWRITE
#undef RWContext
#undef FUNC
#undef xu


static void cbs_jpeg_free_application_data(void *opaque, uint8_t *content)
{
    JPEGRawApplicationData *ad = (JPEGRawApplicationData*)content;
    av_buffer_unref(&ad->Ap_ref);
    av_freep(&content);
}

static void cbs_jpeg_free_comment(void *opaque, uint8_t *content)
{
    JPEGRawComment *comment = (JPEGRawComment*)content;
    av_buffer_unref(&comment->Cm_ref);
    av_freep(&content);
}

static void cbs_jpeg_free_scan(void *opaque, uint8_t *content)
{
    JPEGRawScan *scan = (JPEGRawScan*)content;
    av_buffer_unref(&scan->data_ref);
    av_freep(&content);
}

static int cbs_jpeg_split_fragment(CodedBitstreamContext *ctx,
                                   CodedBitstreamFragment *frag,
                                   int header)
{
    AVBufferRef *data_ref;
    uint8_t *data;
    size_t data_size;
    int start, end, marker, next_start, next_marker;
    int err, i, j, length;

    if (frag->data_size < 4) {
        // Definitely too short to be meaningful.
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i + 1 < frag->data_size && frag->data[i] != 0xff; i++);
    if (i > 0) {
        av_log(ctx->log_ctx, AV_LOG_WARNING, "Discarding %d bytes at "
               "beginning of image.\n", i);
    }
    for (++i; i + 1 < frag->data_size && frag->data[i] == 0xff; i++);
    if (i + 1 >= frag->data_size && frag->data[i]) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid JPEG image: "
               "no SOI marker found.\n");
        return AVERROR_INVALIDDATA;
    }
    marker = frag->data[i];
    if (marker != JPEG_MARKER_SOI) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid JPEG image: first "
               "marker is %02x, should be SOI.\n", marker);
        return AVERROR_INVALIDDATA;
    }
    for (++i; i + 1 < frag->data_size && frag->data[i] == 0xff; i++);
    if (i + 1 >= frag->data_size) {
        av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid JPEG image: "
               "no image content found.\n");
        return AVERROR_INVALIDDATA;
    }
    marker = frag->data[i];
    start  = i + 1;

    do {
        if (marker == JPEG_MARKER_EOI) {
            break;
        } else if (marker == JPEG_MARKER_SOS) {
            next_marker = -1;
            end = start;
            for (i = start; i + 1 < frag->data_size; i++) {
                if (frag->data[i] != 0xff)
                    continue;
                end = i;
                for (++i; i + 1 < frag->data_size &&
                          frag->data[i] == 0xff; i++);
                if (i + 1 < frag->data_size) {
                    if (frag->data[i] == 0x00)
                        continue;
                    next_marker = frag->data[i];
                    next_start  = i + 1;
                }
                break;
            }
        } else {
            i = start;
            if (i + 2 > frag->data_size) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid JPEG image: "
                       "truncated at %02x marker.\n", marker);
                return AVERROR_INVALIDDATA;
            }
            length = AV_RB16(frag->data + i);
            if (i + length > frag->data_size) {
                av_log(ctx->log_ctx, AV_LOG_ERROR, "Invalid JPEG image: "
                       "truncated at %02x marker segment.\n", marker);
                return AVERROR_INVALIDDATA;
            }
            end = start + length;

            i = end;
            if (frag->data[i] != 0xff) {
                next_marker = -1;
            } else {
                for (++i; i + 1 < frag->data_size &&
                          frag->data[i] == 0xff; i++);
                if (i + 1 >= frag->data_size) {
                    next_marker = -1;
                } else {
                    next_marker = frag->data[i];
                    next_start  = i + 1;
                }
            }
        }

        if (marker == JPEG_MARKER_SOS) {
            length = AV_RB16(frag->data + start);

            if (length > end - start)
                return AVERROR_INVALIDDATA;

            data_ref = NULL;
            data     = av_malloc(end - start +
                                 AV_INPUT_BUFFER_PADDING_SIZE);
            if (!data)
                return AVERROR(ENOMEM);

            memcpy(data, frag->data + start, length);
            for (i = start + length, j = length; i < end; i++, j++) {
                if (frag->data[i] == 0xff) {
                    while (frag->data[i] == 0xff)
                        ++i;
                    data[j] = 0xff;
                } else {
                    data[j] = frag->data[i];
                }
            }
            data_size = j;

            memset(data + data_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        } else {
            data      = frag->data + start;
            data_size = end - start;
            data_ref  = frag->data_ref;
        }

        err = ff_cbs_append_unit_data(frag, marker,
                                      data, data_size, data_ref);
        if (err < 0)
            return err;

        marker = next_marker;
        start  = next_start;
    } while (next_marker != -1);

    return 0;
}

static int cbs_jpeg_read_unit(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit)
{
    GetBitContext gbc;
    int err;

    err = init_get_bits(&gbc, unit->data, 8 * unit->data_size);
    if (err < 0)
        return err;

    if (unit->type >= JPEG_MARKER_SOF0 &&
        unit->type <= JPEG_MARKER_SOF3) {
        err = ff_cbs_alloc_unit_content(unit,
                                        sizeof(JPEGRawFrameHeader),
                                        NULL);
        if (err < 0)
            return err;

        err = cbs_jpeg_read_frame_header(ctx, &gbc, unit->content);
        if (err < 0)
            return err;

    } else if (unit->type >= JPEG_MARKER_APPN &&
               unit->type <= JPEG_MARKER_APPN + 15) {
        err = ff_cbs_alloc_unit_content(unit,
                                        sizeof(JPEGRawApplicationData),
                                        &cbs_jpeg_free_application_data);
        if (err < 0)
            return err;

        err = cbs_jpeg_read_application_data(ctx, &gbc, unit->content);
        if (err < 0)
            return err;

    } else if (unit->type == JPEG_MARKER_SOS) {
        JPEGRawScan *scan;
        int pos;

        err = ff_cbs_alloc_unit_content(unit,
                                        sizeof(JPEGRawScan),
                                        &cbs_jpeg_free_scan);
        if (err < 0)
            return err;
        scan = unit->content;

        err = cbs_jpeg_read_scan_header(ctx, &gbc, &scan->header);
        if (err < 0)
            return err;

        pos = get_bits_count(&gbc);
        av_assert0(pos % 8 == 0);
        if (pos > 0) {
            scan->data_size = unit->data_size - pos / 8;
            scan->data_ref  = av_buffer_ref(unit->data_ref);
            if (!scan->data_ref)
                return AVERROR(ENOMEM);
            scan->data = unit->data + pos / 8;
        }

    } else {
        switch (unit->type) {
#define SEGMENT(marker, type, func, free) \
        case JPEG_MARKER_ ## marker: \
            { \
                err = ff_cbs_alloc_unit_content(unit, \
                                                sizeof(type), free); \
                if (err < 0) \
                    return err; \
                err = cbs_jpeg_read_ ## func(ctx, &gbc, unit->content); \
                if (err < 0) \
                    return err; \
            } \
            break
            SEGMENT(DQT, JPEGRawQuantisationTableSpecification, dqt, NULL);
            SEGMENT(DHT, JPEGRawHuffmanTableSpecification,      dht, NULL);
            SEGMENT(COM, JPEGRawComment,  comment, &cbs_jpeg_free_comment);
#undef SEGMENT
        default:
            return AVERROR(ENOSYS);
        }
    }

    return 0;
}

static int cbs_jpeg_write_scan(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit,
                               PutBitContext *pbc)
{
    JPEGRawScan *scan = unit->content;
    int err;

    err = cbs_jpeg_write_scan_header(ctx, pbc, &scan->header);
    if (err < 0)
        return err;

    if (scan->data) {
        if (scan->data_size * 8 > put_bits_left(pbc))
            return AVERROR(ENOSPC);

        av_assert0(put_bits_count(pbc) % 8 == 0);

        flush_put_bits(pbc);

        memcpy(put_bits_ptr(pbc), scan->data, scan->data_size);
        skip_put_bytes(pbc, scan->data_size);
    }

    return 0;
}

static int cbs_jpeg_write_segment(CodedBitstreamContext *ctx,
                                  CodedBitstreamUnit *unit,
                                  PutBitContext *pbc)
{
    int err;

    if (unit->type >= JPEG_MARKER_SOF0 &&
        unit->type <= JPEG_MARKER_SOF3) {
        err = cbs_jpeg_write_frame_header(ctx, pbc, unit->content);
    } else if (unit->type >= JPEG_MARKER_APPN &&
               unit->type <= JPEG_MARKER_APPN + 15) {
        err = cbs_jpeg_write_application_data(ctx, pbc, unit->content);
    } else {
        switch (unit->type) {
#define SEGMENT(marker, func) \
            case JPEG_MARKER_ ## marker: \
                err = cbs_jpeg_write_ ## func(ctx, pbc, unit->content); \
                break;
            SEGMENT(DQT, dqt);
            SEGMENT(DHT, dht);
            SEGMENT(COM, comment);
        default:
            return AVERROR_PATCHWELCOME;
        }
    }

    return err;
}

static int cbs_jpeg_write_unit(CodedBitstreamContext *ctx,
                               CodedBitstreamUnit *unit,
                               PutBitContext *pbc)
{
    if (unit->type == JPEG_MARKER_SOS)
        return cbs_jpeg_write_scan   (ctx, unit, pbc);
    else
        return cbs_jpeg_write_segment(ctx, unit, pbc);
}

static int cbs_jpeg_assemble_fragment(CodedBitstreamContext *ctx,
                                       CodedBitstreamFragment *frag)
{
    const CodedBitstreamUnit *unit;
    uint8_t *data;
    size_t size, dp, sp;
    int i;

    size = 4; // SOI + EOI.
    for (i = 0; i < frag->nb_units; i++) {
        unit = &frag->units[i];
        size += 2 + unit->data_size;
        if (unit->type == JPEG_MARKER_SOS) {
            for (sp = 0; sp < unit->data_size; sp++) {
                if (unit->data[sp] == 0xff)
                    ++size;
            }
        }
    }

    frag->data_ref = av_buffer_alloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!frag->data_ref)
        return AVERROR(ENOMEM);
    data = frag->data_ref->data;

    dp = 0;

    data[dp++] = 0xff;
    data[dp++] = JPEG_MARKER_SOI;

    for (i = 0; i < frag->nb_units; i++) {
        unit = &frag->units[i];

        data[dp++] = 0xff;
        data[dp++] = unit->type;

        if (unit->type != JPEG_MARKER_SOS) {
            memcpy(data + dp, unit->data, unit->data_size);
            dp += unit->data_size;
        } else {
            sp = AV_RB16(unit->data);
            av_assert0(sp <= unit->data_size);
            memcpy(data + dp, unit->data, sp);
            dp += sp;

            for (; sp < unit->data_size; sp++) {
                if (unit->data[sp] == 0xff) {
                    data[dp++] = 0xff;
                    data[dp++] = 0x00;
                } else {
                    data[dp++] = unit->data[sp];
                }
            }
        }
    }

    data[dp++] = 0xff;
    data[dp++] = JPEG_MARKER_EOI;

    av_assert0(dp == size);

    memset(data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    frag->data      = data;
    frag->data_size = size;

    return 0;
}

const CodedBitstreamType ff_cbs_type_jpeg = {
    .codec_id          = AV_CODEC_ID_MJPEG,

    .split_fragment    = &cbs_jpeg_split_fragment,
    .read_unit         = &cbs_jpeg_read_unit,
    .write_unit        = &cbs_jpeg_write_unit,
    .assemble_fragment = &cbs_jpeg_assemble_fragment,
};
