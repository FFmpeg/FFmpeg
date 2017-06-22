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

#ifndef AVCODEC_CBS_H
#define AVCODEC_CBS_H

#include <stddef.h>
#include <stdint.h>

#include "libavutil/buffer.h"

#include "avcodec.h"


/*
 * This defines a framework for converting between a coded bitstream
 * and structures defining all individual syntax elements found in
 * such a stream.
 *
 * Conversion in both directions is possible.  Given a coded bitstream
 * (any meaningful fragment), it can be parsed and decomposed into
 * syntax elements stored in a set of codec-specific structures.
 * Similarly, given a set of those same codec-specific structures the
 * syntax elements can be serialised and combined to create a coded
 * bitstream.
 */

struct CodedBitstreamType;

/**
 * The codec-specific type of a bitstream unit.
 *
 * H.264 / AVC: nal_unit_type
 * H.265 / HEVC: nal_unit_type
 * MPEG-2: start code value (without prefix)
 * VP9: unused, set to zero (every unit is a frame)
 */
typedef uint32_t CodedBitstreamUnitType;

/**
 * Coded bitstream unit structure.
 *
 * A bitstream unit the smallest element of a bitstream which
 * is meaningful on its own.  For example, an H.264 NAL unit.
 *
 * See the codec-specific header for the meaning of this for any
 * particular codec.
 */
typedef struct CodedBitstreamUnit {
    /**
     * Codec-specific type of this unit.
     */
    CodedBitstreamUnitType type;

    /**
     * Pointer to the directly-parsable bitstream form of this unit.
     *
     * May be NULL if the unit currently only exists in decomposed form.
     */
    uint8_t *data;
    /**
     * The number of bytes in the bitstream (including any padding bits
     * in the final byte).
     */
    size_t   data_size;
    /**
     * The number of bits which should be ignored in the final byte.
     *
     * This supports non-byte-aligned bitstreams.
     */
    size_t   data_bit_padding;
    /**
     * A reference to the buffer containing data.
     *
     * Must be set if data is not NULL.
     */
    AVBufferRef *data_ref;

    /**
     * Pointer to the decomposed form of this unit.
     *
     * The type of this structure depends on both the codec and the
     * type of this unit.  May be NULL if the unit only exists in
     * bitstream form.
     */
    void *content;
    /**
     * If content is reference counted, a reference to the buffer containing
     * content.  Null if content is not reference counted.
     */
    AVBufferRef *content_ref;
} CodedBitstreamUnit;

/**
 * Coded bitstream fragment structure, combining one or more units.
 *
 * This is any sequence of units.  It need not form some greater whole,
 * though in many cases it will.  For example, an H.264 access unit,
 * which is composed of a sequence of H.264 NAL units.
 */
typedef struct CodedBitstreamFragment {
    /**
     * Pointer to the bitstream form of this fragment.
     *
     * May be NULL if the fragment only exists as component units.
     */
    uint8_t *data;
    /**
     * The number of bytes in the bitstream.
     *
     * The number of bytes in the bitstream (including any padding bits
     * in the final byte).
     */
    size_t   data_size;
    /**
     * The number of bits which should be ignored in the final byte.
     */
    size_t data_bit_padding;
    /**
     * A reference to the buffer containing data.
     *
     * Must be set if data is not NULL.
     */
    AVBufferRef *data_ref;

    /**
     * Number of units in this fragment.
     *
     * This may be zero if the fragment only exists in bitstream form
     * and has not been decomposed.
     */
    int              nb_units;
    /**
     * Pointer to an array of units of length nb_units.
     *
     * Must be NULL if nb_units is zero.
     */
    CodedBitstreamUnit *units;
} CodedBitstreamFragment;

/**
 * Context structure for coded bitstream operations.
 */
typedef struct CodedBitstreamContext {
    /**
     * Logging context to be passed to all av_log() calls associated
     * with this context.
     */
    void *log_ctx;

    /**
     * Internal codec-specific hooks.
     */
    const struct CodedBitstreamType *codec;

    /**
     * Internal codec-specific data.
     *
     * This contains any information needed when reading/writing
     * bitsteams which will not necessarily be present in a fragment.
     * For example, for H.264 it contains all currently visible
     * parameter sets - they are required to determine the bitstream
     * syntax but need not be present in every access unit.
     */
    void *priv_data;

    /**
     * Array of unit types which should be decomposed when reading.
     *
     * Types not in this list will be available in bitstream form only.
     * If NULL, all supported types will be decomposed.
     */
    CodedBitstreamUnitType *decompose_unit_types;
    /**
     * Length of the decompose_unit_types array.
     */
    int nb_decompose_unit_types;

    /**
     * Enable trace output during read/write operations.
     */
    int trace_enable;
    /**
     * Log level to use for trace output.
     *
     * From AV_LOG_*; defaults to AV_LOG_TRACE.
     */
    int trace_level;
} CodedBitstreamContext;


/**
 * Table of all supported codec IDs.
 *
 * Terminated by AV_CODEC_ID_NONE.
 */
extern const enum AVCodecID ff_cbs_all_codec_ids[];


/**
 * Create and initialise a new context for the given codec.
 */
int ff_cbs_init(CodedBitstreamContext **ctx,
                enum AVCodecID codec_id, void *log_ctx);

/**
 * Close a context and free all internal state.
 */
void ff_cbs_close(CodedBitstreamContext **ctx);


/**
 * Read the extradata bitstream found in codec parameters into a
 * fragment, then split into units and decompose.
 *
 * This also updates the internal state, so will need to be called for
 * codecs with extradata to read parameter sets necessary for further
 * parsing even if the fragment itself is not desired.
 */
int ff_cbs_read_extradata(CodedBitstreamContext *ctx,
                          CodedBitstreamFragment *frag,
                          const AVCodecParameters *par);

/**
 * Read the data bitstream from a packet into a fragment, then
 * split into units and decompose.
 *
 * This also updates the internal state of the coded bitstream context
 * with any persistent data from the fragment which may be required to
 * read following fragments (e.g. parameter sets).
 */
int ff_cbs_read_packet(CodedBitstreamContext *ctx,
                       CodedBitstreamFragment *frag,
                       const AVPacket *pkt);

/**
 * Read a bitstream from a memory region into a fragment, then
 * split into units and decompose.
 *
 * This also updates the internal state of the coded bitstream context
 * with any persistent data from the fragment which may be required to
 * read following fragments (e.g. parameter sets).
 */
int ff_cbs_read(CodedBitstreamContext *ctx,
                CodedBitstreamFragment *frag,
                const uint8_t *data, size_t size);


/**
 * Write the content of the fragment to its own internal buffer.
 *
 * Writes the content of all units and then assembles them into a new
 * data buffer.  When modifying the content of decomposed units, this
 * can be used to regenerate the bitstream form of units or the whole
 * fragment so that it can be extracted for other use.
 *
 * This also updates the internal state of the coded bitstream context
 * with any persistent data from the fragment which may be required to
 * write following fragments (e.g. parameter sets).
 */
int ff_cbs_write_fragment_data(CodedBitstreamContext *ctx,
                               CodedBitstreamFragment *frag);

/**
 * Write the bitstream of a fragment to the extradata in codec parameters.
 *
 * This replaces any existing extradata in the structure.
 */
int ff_cbs_write_extradata(CodedBitstreamContext *ctx,
                           AVCodecParameters *par,
                           CodedBitstreamFragment *frag);

/**
 * Write the bitstream of a fragment to a packet.
 */
int ff_cbs_write_packet(CodedBitstreamContext *ctx,
                        AVPacket *pkt,
                        CodedBitstreamFragment *frag);


/**
 * Free all allocated memory in a fragment.
 */
void ff_cbs_fragment_uninit(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *frag);


/**
 * Allocate a new internal content buffer of the given size in the unit.
 *
 * The content will be zeroed.
 */
int ff_cbs_alloc_unit_content(CodedBitstreamContext *ctx,
                              CodedBitstreamUnit *unit,
                              size_t size,
                              void (*free)(void *unit, uint8_t *content));

/**
 * Allocate a new internal data buffer of the given size in the unit.
 *
 * The data buffer will have input padding.
 */
int ff_cbs_alloc_unit_data(CodedBitstreamContext *ctx,
                           CodedBitstreamUnit *unit,
                           size_t size);

/**
 * Insert a new unit into a fragment with the given content.
 *
 * The content structure continues to be owned by the caller if
 * content_buf is not supplied.
 */
int ff_cbs_insert_unit_content(CodedBitstreamContext *ctx,
                               CodedBitstreamFragment *frag,
                               int position,
                               CodedBitstreamUnitType type,
                               void *content,
                               AVBufferRef *content_buf);

/**
 * Insert a new unit into a fragment with the given data bitstream.
 *
 * If data_buf is not supplied then data must have been allocated with
 * av_malloc() and will become owned by the unit after this call.
 */
int ff_cbs_insert_unit_data(CodedBitstreamContext *ctx,
                            CodedBitstreamFragment *frag,
                            int position,
                            CodedBitstreamUnitType type,
                            uint8_t *data, size_t data_size,
                            AVBufferRef *data_buf);

/**
 * Delete a unit from a fragment and free all memory it uses.
 */
int ff_cbs_delete_unit(CodedBitstreamContext *ctx,
                       CodedBitstreamFragment *frag,
                       int position);


#endif /* AVCODEC_CBS_H */
