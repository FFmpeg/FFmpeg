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

#include <stdio.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/dict.h"
#include "libavutil/display.h"
#include "libavutil/downmix_info.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/motion_vector.h"
#include "libavutil/spherical.h"
#include "libavutil/stereo3d.h"

typedef struct Set {
    AVFrameSideData **sd;
    int            nb_sd;
} Set;

/* Every entry below carries a payload laid out as AVFrameSideDataType
 * documents for its type: a producer owes that layout even when no consumer
 * reads the buffer back. The small number that identifies an entry in the
 * reference output therefore lives in the metadata dictionary, which
 * av_frame_side_data_clone() copies along with the buffer. */
static void set_marker(AVFrameSideData *sd, int marker)
{
    char value[16];

    snprintf(value, sizeof(value), "%d", marker);
    av_assert0(av_dict_set(&sd->metadata, "marker", value, 0) >= 0);
}

static AVFrameSideData *new_entry(Set *s, enum AVFrameSideDataType type,
                                  const void *payload, size_t size, int marker)
{
    AVFrameSideData *sd = av_frame_side_data_new(&s->sd, &s->nb_sd, type,
                                                 size, 0);

    av_assert0(sd && sd->size == size);
    memcpy(sd->data, payload, size);
    set_marker(sd, marker);
    return sd;
}

static AVBufferRef *payload_buffer(const void *payload, size_t size)
{
    AVBufferRef *buf = av_buffer_alloc(size);

    av_assert0(buf);
    memcpy(buf->data, payload, size);
    return buf;
}

static const char *entry_marker(const AVFrameSideData *sd)
{
    const AVDictionaryEntry *e = av_dict_get(sd->metadata, "marker", NULL, 0);

    return e ? e->value : "none";
}

static void print_set(const char *label, const Set *s)
{
    printf("%s: n=%d\n", label, s->nb_sd);
    for (int i = 0; i < s->nb_sd; i++)
        printf("  [%d] %s marker=%s\n", i,
               av_frame_side_data_name(s->sd[i]->type),
               entry_marker(s->sd[i]));
}

/* Dumps a set by looking each type up instead of walking the array. Removal is
 * documented to drop the matching entries, not to keep the survivors where
 * they were, so printing by index would tie the reference to the current habit
 * of moving the last entry into the freed slot. */
static void print_lookup(const char *label, const Set *s,
                         const enum AVFrameSideDataType *types, size_t nb_types)
{
    printf("%s: n=%d\n", label, s->nb_sd);
    for (size_t i = 0; i < nb_types; i++) {
        const AVFrameSideData *sd =
            av_frame_side_data_get(s->sd, s->nb_sd, types[i]);
        const char *name = av_frame_side_data_name(types[i]);

        if (sd)
            printf("    %s: marker=%s\n", name, entry_marker(sd));
        else
            printf("    %s: absent\n", name);
    }
}

int main(void)
{
    /* desc / name accessors. */
    printf("Testing av_frame_side_data_desc() / av_frame_side_data_name()\n");
    static const enum AVFrameSideDataType known[] = {
        AV_FRAME_DATA_STEREO3D,                    /* PROP_GLOBAL */
        AV_FRAME_DATA_DYNAMIC_HDR_PLUS,            /* PROP_COLOR_DEPENDENT */
        AV_FRAME_DATA_SPHERICAL,                   /* PROP_GLOBAL|SIZE_DEPENDENT */
        AV_FRAME_DATA_DOWNMIX_INFO,                /* PROP_CHANNEL_DEPENDENT */
        AV_FRAME_DATA_SEI_UNREGISTERED,            /* PROP_MULTI */
    };
    for (size_t i = 0; i < FF_ARRAY_ELEMS(known); i++) {
        const AVSideDataDescriptor *d = av_frame_side_data_desc(known[i]);
        const char *n = av_frame_side_data_name(known[i]);
        /* Compare content: the API only promises a string identifying the
         * type, not the descriptor's own pointer. */
        printf("  type=%d name_match=%s props=0x%x\n", known[i],
               d && n && !strcmp(d->name, n) ? "yes" : "no",
               d ? d->props : 0);
    }

    /* Invalid / unmapped types must return NULL (sd_props gap). */
    {
        enum AVFrameSideDataType bad = (enum AVFrameSideDataType)-1;
        printf("  invalid type desc=%s name=%s\n",
               av_frame_side_data_desc(bad) ? "non-null" : "null",
               av_frame_side_data_name(bad) ? "non-null" : "null");
    }

    /* Payloads. The two structs are allocated through their public
     * allocators because their sizes are not part of the public ABI. */
    size_t stereo3d_size, spherical_size;
    AVStereo3D         *stereo3d  = av_stereo3d_alloc_size(&stereo3d_size);
    AVSphericalMapping *spherical = av_spherical_alloc(&spherical_size);
    /* 16 bytes of uuid_iso_iec_11578 followed by user_data_payload_byte. */
    uint8_t udu[16 + 8] = {
        0x9a, 0x21, 0xf3, 0x4d, 0x6b, 0x1c, 0x47, 0x8e,
        0xa5, 0x30, 0xc2, 0x7f, 0x11, 0x8d, 0x46, 0x02,
        'p',  'a',  'y',  'l',  'o',  'a',  'd',  '1',
    };
    int32_t displaymatrix[9];
    /* src_x = dst_x + motion_x / motion_scale, likewise for y. */
    AVMotionVector mvs[2] = {
        { .source = -1, .w = 16, .h = 16, .src_x = 32, .src_y = 48,
          .dst_x = 40, .dst_y = 48, .motion_x = -8, .motion_y = 0,
          .motion_scale = 1 },
        { .source =  1, .w =  8, .h =  8, .src_x = 64, .src_y = 64,
          .dst_x = 64, .dst_y = 72, .motion_x = 0, .motion_y = -8,
          .motion_scale = 1 },
    };
    /* The mix levels are absolute linear scale factors, not dB. */
    AVDownmixInfo downmix = {
        .preferred_downmix_type  = AV_DOWNMIX_TYPE_LTRT,
        .center_mix_level        = 0.5,
        .center_mix_level_ltrt   = 0.625,
        .surround_mix_level      = 0.25,
        .surround_mix_level_ltrt = 0.375,
        .lfe_mix_level           = 0.125,
    };

    av_assert0(stereo3d && spherical);

    stereo3d->type        = AV_STEREO3D_SIDEBYSIDE;
    stereo3d->view        = AV_STEREO3D_VIEW_PACKED;
    stereo3d->primary_eye = AV_PRIMARY_EYE_LEFT;
    stereo3d->baseline    = 65000;

    spherical->projection = AV_SPHERICAL_EQUIRECTANGULAR;
    spherical->yaw        = 90 << 16;

    av_display_rotation_set(displaymatrix, 90.0);

    /* Populate a set with several types, one of them MULTI. */
    Set set = { 0 };
    new_entry(&set, AV_FRAME_DATA_STEREO3D, stereo3d, stereo3d_size, 100);
    new_entry(&set, AV_FRAME_DATA_DOWNMIX_INFO, &downmix, sizeof(downmix),
              200);
    new_entry(&set, AV_FRAME_DATA_SEI_UNREGISTERED, udu, sizeof(udu), 1);
    udu[sizeof(udu) - 1] = '2';
    new_entry(&set, AV_FRAME_DATA_SEI_UNREGISTERED, udu, sizeof(udu), 2);
    new_entry(&set, AV_FRAME_DATA_SPHERICAL, spherical, spherical_size, 300);
    /* Entries are appended, so this dump is the insertion order by
     * construction and shows both MULTI entries. */
    print_set("\nInitial set", &set);

    /* Types probed by every dump below. */
    static const enum AVFrameSideDataType probed[] = {
        AV_FRAME_DATA_STEREO3D,
        AV_FRAME_DATA_DOWNMIX_INFO,
        AV_FRAME_DATA_SEI_UNREGISTERED,
        AV_FRAME_DATA_SPHERICAL,
        AV_FRAME_DATA_DISPLAYMATRIX,
        AV_FRAME_DATA_MOTION_VECTORS,
    };

    /* get / get_c: present and missing types. */
    printf("\nTesting av_frame_side_data_get()\n");
    {
        const AVFrameSideData *got =
            av_frame_side_data_get(set.sd, set.nb_sd, AV_FRAME_DATA_STEREO3D);
        printf("  stereo3d: %s marker=%s payload_match=%s\n",
               got ? "found" : "missing", got ? entry_marker(got) : "none",
               got && got->size == stereo3d_size &&
               !memcmp(got->data, stereo3d, stereo3d_size) ? "yes" : "no");
        got = av_frame_side_data_get(set.sd, set.nb_sd,
                                     AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        printf("  mastering (absent): %s\n", got ? "FAIL" : "null");
    }

    /* remove by type clears all matching entries (including duplicates). */
    printf("\nTesting av_frame_side_data_remove()\n");
    av_frame_side_data_remove(&set.sd, &set.nb_sd,
                              AV_FRAME_DATA_SEI_UNREGISTERED);
    print_lookup("  after remove SEI_UNREGISTERED", &set,
                 probed, FF_ARRAY_ELEMS(probed));

    /* remove_by_props: PROP_GLOBAL drops STEREO3D and SPHERICAL. */
    printf("\nTesting av_frame_side_data_remove_by_props()\n");
    av_frame_side_data_remove_by_props(&set.sd, &set.nb_sd,
                                       AV_SIDE_DATA_PROP_GLOBAL);
    print_lookup("  after remove_by_props(GLOBAL)", &set,
                 probed, FF_ARRAY_ELEMS(probed));

    /* Clone: copy remaining entry into a second set. */
    printf("\nTesting av_frame_side_data_clone()\n");
    Set dst = { 0 };
    if (set.nb_sd > 0) {
        int ret = av_frame_side_data_clone(&dst.sd, &dst.nb_sd,
                                           set.sd[0], 0);
        printf("  clone into empty: ret=%d nb=%d marker=%s\n", ret, dst.nb_sd,
               dst.nb_sd ? entry_marker(dst.sd[0]) : "none");

        /* Same type again without REPLACE must fail with EEXIST. */
        ret = av_frame_side_data_clone(&dst.sd, &dst.nb_sd, set.sd[0], 0);
        printf("  clone duplicate no REPLACE: ret==AVERROR(EEXIST)=%s\n",
               ret == AVERROR(EEXIST) ? "yes" : "no");

        /* With REPLACE, clone replaces the existing entry in place. */
        ret = av_frame_side_data_clone(&dst.sd, &dst.nb_sd, set.sd[0],
                                       AV_FRAME_SIDE_DATA_FLAG_REPLACE);
        printf("  clone REPLACE: ret=%d nb=%d\n", ret, dst.nb_sd);

        /* Invalid args. */
        ret = av_frame_side_data_clone(NULL, &dst.nb_sd, set.sd[0], 0);
        printf("  clone NULL sd: ret==AVERROR(EINVAL)=%s\n",
               ret == AVERROR(EINVAL) ? "yes" : "no");
    }

    /* add: buffer ownership transfer and NEW_REF. */
    printf("\nTesting av_frame_side_data_add()\n");
    {
        AVBufferRef *buf = payload_buffer(displaymatrix, sizeof(displaymatrix));

        AVFrameSideData *sd_added =
            av_frame_side_data_add(&dst.sd, &dst.nb_sd,
                                   AV_FRAME_DATA_DISPLAYMATRIX,
                                   &buf, AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
        printf("  add (move): added=%s pbuf_nulled=%s\n",
               sd_added ? "yes" : "no", buf == NULL ? "yes" : "no");
        if (sd_added)
            set_marker(sd_added, 999);

        /* NEW_REF: the caller retains its reference. */
        AVBufferRef *keep = payload_buffer(mvs, sizeof(mvs));
        sd_added = av_frame_side_data_add(&dst.sd, &dst.nb_sd,
                                          AV_FRAME_DATA_MOTION_VECTORS, &keep,
                                          AV_FRAME_SIDE_DATA_FLAG_NEW_REF);
        printf("  add (NEW_REF): added=%s caller_ref_kept=%s nb_mvs=%d\n",
               sd_added ? "yes" : "no", keep ? "yes" : "no",
               sd_added ? (int)(sd_added->size / sizeof(AVMotionVector)) : -1);
        if (sd_added)
            set_marker(sd_added, 42);
        av_buffer_unref(&keep);

        /* REPLACE on existing entry: same type, payload swaps in place. */
        int32_t replacement[9];
        av_display_rotation_set(replacement, 180.0);
        AVBufferRef *repl = payload_buffer(replacement, sizeof(replacement));
        sd_added = av_frame_side_data_add(&dst.sd, &dst.nb_sd,
                                          AV_FRAME_DATA_DISPLAYMATRIX, &repl,
                                          AV_FRAME_SIDE_DATA_FLAG_REPLACE);
        if (sd_added)
            set_marker(sd_added, 1234);
        {
            const AVFrameSideData *after =
                av_frame_side_data_get(dst.sd, dst.nb_sd,
                                       AV_FRAME_DATA_DISPLAYMATRIX);
            printf("  add (REPLACE existing): added=%s marker=%s"
                   " payload_match=%s\n",
                   sd_added ? "yes" : "no",
                   after ? entry_marker(after) : "none",
                   after && after->size == sizeof(replacement) &&
                   !memcmp(after->data, replacement, sizeof(replacement)) ?
                   "yes" : "no");
        }
    }

    /* clone with UNIQUE: existing same-type entry is removed first. dst
     * already has a set.sd[0]->type entry from the REPLACE clone above. */
    printf("\nTesting av_frame_side_data_clone() UNIQUE\n");
    if (set.nb_sd > 0) {
        int before = dst.nb_sd;
        int ret = av_frame_side_data_clone(&dst.sd, &dst.nb_sd, set.sd[0],
                                           AV_FRAME_SIDE_DATA_FLAG_UNIQUE);
        printf("  clone UNIQUE: ret=%d before=%d after=%d\n",
               ret, before, dst.nb_sd);
    }

    print_lookup("\nFinal dst", &dst, probed, FF_ARRAY_ELEMS(probed));

    av_frame_side_data_free(&dst.sd, &dst.nb_sd);
    av_frame_side_data_free(&set.sd, &set.nb_sd);
    printf("\nfree: set_nb=%d dst_nb=%d\n", set.nb_sd, dst.nb_sd);

    av_free(stereo3d);
    av_free(spherical);

    return 0;
}
