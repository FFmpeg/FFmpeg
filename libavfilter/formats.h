/*
 * This file is part of FFMpeg.
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

#ifndef AVFILTER_FORMATS_H
#define AVFILTER_FORMATS_H

#include "avfilter.h"

typedef struct AVFilterChannelLayouts {
    uint64_t *channel_layouts;  ///< list of channel layouts
    int    nb_channel_layouts;  ///< number of channel layouts

    unsigned refcount;          ///< number of references to this list
    struct AVFilterChannelLayouts ***refs; ///< references to this list
} AVFilterChannelLayouts;

/**
 * Return a channel layouts/samplerates list which contains the intersection of
 * the layouts/samplerates of a and b. Also, all the references of a, all the
 * references of b, and a and b themselves will be deallocated.
 *
 * If a and b do not share any common elements, neither is modified, and NULL
 * is returned.
 */
AVFilterChannelLayouts *ff_merge_channel_layouts(AVFilterChannelLayouts *a,
                                                 AVFilterChannelLayouts *b);
AVFilterFormats *ff_merge_samplerates(AVFilterFormats *a,
                                      AVFilterFormats *b);

/**
 * Construct an empty AVFilterChannelLayouts/AVFilterFormats struct --
 * representing any channel layout/sample rate.
 */
AVFilterChannelLayouts *ff_all_channel_layouts(void);
AVFilterFormats *ff_all_samplerates(void);

AVFilterChannelLayouts *avfilter_make_format64_list(const int64_t *fmts);


/**
 * A helper for query_formats() which sets all links to the same list of channel
 * layouts/sample rates. If there are no links hooked to this filter, the list
 * is freed.
 */
void ff_set_common_channel_layouts(AVFilterContext *ctx,
                                   AVFilterChannelLayouts *layouts);
void ff_set_common_samplerates(AVFilterContext *ctx,
                               AVFilterFormats *samplerates);

int ff_add_channel_layout(AVFilterChannelLayouts **l, uint64_t channel_layout);

/**
 * Add *ref as a new reference to f.
 */
void ff_channel_layouts_ref(AVFilterChannelLayouts *f,
                            AVFilterChannelLayouts **ref);

/**
 * Remove a reference to a channel layouts list.
 */
void ff_channel_layouts_unref(AVFilterChannelLayouts **ref);

void ff_channel_layouts_changeref(AVFilterChannelLayouts **oldref,
                                  AVFilterChannelLayouts **newref);

int ff_default_query_formats(AVFilterContext *ctx);

#endif // AVFILTER_FORMATS_H
