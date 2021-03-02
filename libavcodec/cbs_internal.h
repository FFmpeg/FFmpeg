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

#ifndef AVCODEC_CBS_INTERNAL_H
#define AVCODEC_CBS_INTERNAL_H

#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/log.h"

#include "cbs.h"
#include "codec_id.h"
#include "get_bits.h"
#include "put_bits.h"


enum CBSContentType {
    // Unit content is a simple structure.
    CBS_CONTENT_TYPE_POD,
    // Unit content contains some references to other structures, but all
    // managed via buffer reference counting.  The descriptor defines the
    // structure offsets of every buffer reference.
    CBS_CONTENT_TYPE_INTERNAL_REFS,
    // Unit content is something more complex.  The descriptor defines
    // special functions to manage the content.
    CBS_CONTENT_TYPE_COMPLEX,
};

enum {
      // Maximum number of unit types described by the same unit type
      // descriptor.
      CBS_MAX_UNIT_TYPES  = 3,
      // Maximum number of reference buffer offsets in any one unit.
      CBS_MAX_REF_OFFSETS = 2,
      // Special value used in a unit type descriptor to indicate that it
      // applies to a large range of types rather than a set of discrete
      // values.
      CBS_UNIT_TYPE_RANGE = -1,
};

typedef const struct CodedBitstreamUnitTypeDescriptor {
    // Number of entries in the unit_types array, or the special value
    // CBS_UNIT_TYPE_RANGE to indicate that the range fields should be
    // used instead.
    int nb_unit_types;

    // Array of unit types that this entry describes.
    const CodedBitstreamUnitType unit_types[CBS_MAX_UNIT_TYPES];

    // Start and end of unit type range, used if nb_unit_types is
    // CBS_UNIT_TYPE_RANGE.
    const CodedBitstreamUnitType unit_type_range_start;
    const CodedBitstreamUnitType unit_type_range_end;

    // The type of content described.
    enum CBSContentType content_type;
    // The size of the structure which should be allocated to contain
    // the decomposed content of this type of unit.
    size_t content_size;

    // Number of entries in the ref_offsets array.  Only used if the
    // content_type is CBS_CONTENT_TYPE_INTERNAL_REFS.
    int nb_ref_offsets;
    // The structure must contain two adjacent elements:
    //   type        *field;
    //   AVBufferRef *field_ref;
    // where field points to something in the buffer referred to by
    // field_ref.  This offset is then set to offsetof(struct, field).
    size_t ref_offsets[CBS_MAX_REF_OFFSETS];

    void (*content_free)(void *opaque, uint8_t *data);
    int  (*content_clone)(AVBufferRef **ref, CodedBitstreamUnit *unit);
} CodedBitstreamUnitTypeDescriptor;

typedef struct CodedBitstreamType {
    enum AVCodecID codec_id;

    // A class for the private data, used to declare private AVOptions.
    // This field is NULL for types that do not declare any options.
    // If this field is non-NULL, the first member of the filter private data
    // must be a pointer to AVClass.
    const AVClass *priv_class;

    size_t priv_data_size;

    // List of unit type descriptors for this codec.
    // Terminated by a descriptor with nb_unit_types equal to zero.
    const CodedBitstreamUnitTypeDescriptor *unit_types;

    // Split frag->data into coded bitstream units, creating the
    // frag->units array.  Fill data but not content on each unit.
    // The header argument should be set if the fragment came from
    // a header block, which may require different parsing for some
    // codecs (e.g. the AVCC header in H.264).
    int (*split_fragment)(CodedBitstreamContext *ctx,
                          CodedBitstreamFragment *frag,
                          int header);

    // Read the unit->data bitstream and decompose it, creating
    // unit->content.
    int (*read_unit)(CodedBitstreamContext *ctx,
                     CodedBitstreamUnit *unit);

    // Write the data bitstream from unit->content into pbc.
    // Return value AVERROR(ENOSPC) indicates that pbc was too small.
    int (*write_unit)(CodedBitstreamContext *ctx,
                      CodedBitstreamUnit *unit,
                      PutBitContext *pbc);

    // Read the data from all of frag->units and assemble it into
    // a bitstream for the whole fragment.
    int (*assemble_fragment)(CodedBitstreamContext *ctx,
                             CodedBitstreamFragment *frag);

    // Reset the codec internal state.
    void (*flush)(CodedBitstreamContext *ctx);

    // Free the codec internal state.
    void (*close)(CodedBitstreamContext *ctx);
} CodedBitstreamType;


// Helper functions for trace output.

void ff_cbs_trace_header(CodedBitstreamContext *ctx,
                         const char *name);

void ff_cbs_trace_syntax_element(CodedBitstreamContext *ctx, int position,
                                 const char *name, const int *subscripts,
                                 const char *bitstring, int64_t value);


// Helper functions for read/write of common bitstream elements, including
// generation of trace output.

int ff_cbs_read_unsigned(CodedBitstreamContext *ctx, GetBitContext *gbc,
                         int width, const char *name,
                         const int *subscripts, uint32_t *write_to,
                         uint32_t range_min, uint32_t range_max);

int ff_cbs_write_unsigned(CodedBitstreamContext *ctx, PutBitContext *pbc,
                          int width, const char *name,
                          const int *subscripts, uint32_t value,
                          uint32_t range_min, uint32_t range_max);

int ff_cbs_read_signed(CodedBitstreamContext *ctx, GetBitContext *gbc,
                       int width, const char *name,
                       const int *subscripts, int32_t *write_to,
                       int32_t range_min, int32_t range_max);

int ff_cbs_write_signed(CodedBitstreamContext *ctx, PutBitContext *pbc,
                        int width, const char *name,
                        const int *subscripts, int32_t value,
                        int32_t range_min, int32_t range_max);

// The largest unsigned value representable in N bits, suitable for use as
// range_max in the above functions.
#define MAX_UINT_BITS(length) ((UINT64_C(1) << (length)) - 1)

// The largest signed value representable in N bits, suitable for use as
// range_max in the above functions.
#define MAX_INT_BITS(length) ((INT64_C(1) << ((length) - 1)) - 1)

// The smallest signed value representable in N bits, suitable for use as
// range_min in the above functions.
#define MIN_INT_BITS(length) (-(INT64_C(1) << ((length) - 1)))


#define CBS_UNIT_TYPE_POD(type, structure) { \
        .nb_unit_types  = 1, \
        .unit_types     = { type }, \
        .content_type   = CBS_CONTENT_TYPE_POD, \
        .content_size   = sizeof(structure), \
    }
#define CBS_UNIT_TYPE_INTERNAL_REF(type, structure, ref_field) { \
        .nb_unit_types  = 1, \
        .unit_types     = { type }, \
        .content_type   = CBS_CONTENT_TYPE_INTERNAL_REFS, \
        .content_size   = sizeof(structure), \
        .nb_ref_offsets = 1, \
        .ref_offsets    = { offsetof(structure, ref_field) }, \
    }
#define CBS_UNIT_TYPE_COMPLEX(type, structure, free_func) { \
        .nb_unit_types  = 1, \
        .unit_types     = { type }, \
        .content_type   = CBS_CONTENT_TYPE_COMPLEX, \
        .content_size   = sizeof(structure), \
        .content_free   = free_func, \
    }
#define CBS_UNIT_TYPE_END_OF_LIST { .nb_unit_types = 0 }


extern const CodedBitstreamType ff_cbs_type_av1;
extern const CodedBitstreamType ff_cbs_type_h264;
extern const CodedBitstreamType ff_cbs_type_h265;
extern const CodedBitstreamType ff_cbs_type_jpeg;
extern const CodedBitstreamType ff_cbs_type_mpeg2;
extern const CodedBitstreamType ff_cbs_type_vp9;


#endif /* AVCODEC_CBS_INTERNAL_H */
