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

#ifndef AVFILTER_FORMATS_H
#define AVFILTER_FORMATS_H

#include "avfilter.h"

/**
 * A list of supported formats for one end of a filter link. This is used
 * during the format negotiation process to try to pick the best format to
 * use to minimize the number of necessary conversions. Each filter gives a
 * list of the formats supported by each input and output pad. The list
 * given for each pad need not be distinct - they may be references to the
 * same list of formats, as is often the case when a filter supports multiple
 * formats, but will always output the same format as it is given in input.
 *
 * In this way, a list of possible input formats and a list of possible
 * output formats are associated with each link. When a set of formats is
 * negotiated over a link, the input and output lists are merged to form a
 * new list containing only the common elements of each list. In the case
 * that there were no common elements, a format conversion is necessary.
 * Otherwise, the lists are merged, and all other links which reference
 * either of the format lists involved in the merge are also affected.
 *
 * For example, consider the filter chain:
 * filter (a) --> (b) filter (b) --> (c) filter
 *
 * where the letters in parenthesis indicate a list of formats supported on
 * the input or output of the link. Suppose the lists are as follows:
 * (a) = {A, B}
 * (b) = {A, B, C}
 * (c) = {B, C}
 *
 * First, the first link's lists are merged, yielding:
 * filter (a) --> (a) filter (a) --> (c) filter
 *
 * Notice that format list (b) now refers to the same list as filter list (a).
 * Next, the lists for the second link are merged, yielding:
 * filter (a) --> (a) filter (a) --> (a) filter
 *
 * where (a) = {B}.
 *
 * Unfortunately, when the format lists at the two ends of a link are merged,
 * we must ensure that all links which reference either pre-merge format list
 * get updated as well. Therefore, we have the format list structure store a
 * pointer to each of the pointers to itself.
 */
struct AVFilterFormats {
    unsigned nb_formats;        ///< number of formats
    int *formats;               ///< list of media formats

    unsigned refcount;          ///< number of references to this list
    struct AVFilterFormats ***refs; ///< references to this list
};

/**
 * A list of supported channel layouts.
 *
 * The list works the same as AVFilterFormats, except for the following
 * differences:
 * - A list with all_layouts = 1 means all channel layouts with a known
 *   disposition; nb_channel_layouts must then be 0.
 * - A list with all_counts = 1 means all channel counts, with a known or
 *   unknown disposition; nb_channel_layouts must then be 0 and all_layouts 1.
 * - The list must not contain a layout with a known disposition and a
 *   channel count with unknown disposition with the same number of channels
 *   (e.g. AV_CH_LAYOUT_STEREO and FF_COUNT2LAYOUT(2).
 */
typedef struct AVFilterChannelLayouts {
    uint64_t *channel_layouts;  ///< list of channel layouts
    int    nb_channel_layouts;  ///< number of channel layouts
    char all_layouts;           ///< accept any known channel layout
    char all_counts;            ///< accept any channel layout or count

    unsigned refcount;          ///< number of references to this list
    struct AVFilterChannelLayouts ***refs; ///< references to this list
} AVFilterChannelLayouts;

/**
 * Encode a channel count as a channel layout.
 * FF_COUNT2LAYOUT(c) means any channel layout with c channels, with a known
 * or unknown disposition.
 * The result is only valid inside AVFilterChannelLayouts and immediately
 * related functions.
 */
#define FF_COUNT2LAYOUT(c) (0x8000000000000000ULL | (c))

/**
 * Decode a channel count encoded as a channel layout.
 * Return 0 if the channel layout was a real one.
 */
#define FF_LAYOUT2COUNT(l) (((l) & 0x8000000000000000ULL) ? \
                           (int)((l) & 0x7FFFFFFF) : 0)

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
 * representing any channel layout (with known disposition)/sample rate.
 */
av_warn_unused_result
AVFilterChannelLayouts *ff_all_channel_layouts(void);

av_warn_unused_result
AVFilterFormats *ff_all_samplerates(void);

/**
 * Construct an AVFilterChannelLayouts coding for any channel layout, with
 * known or unknown disposition.
 */
av_warn_unused_result
AVFilterChannelLayouts *ff_all_channel_counts(void);

av_warn_unused_result
AVFilterChannelLayouts *avfilter_make_format64_list(const int64_t *fmts);

av_warn_unused_result
AVFilterChannelLayouts *ff_make_formatu64_list(const uint64_t *fmts);


/**
 * A helper for query_formats() which sets all links to the same list of channel
 * layouts/sample rates. If there are no links hooked to this filter, the list
 * is freed.
 */
av_warn_unused_result
int ff_set_common_channel_layouts(AVFilterContext *ctx,
                                  AVFilterChannelLayouts *layouts);
av_warn_unused_result
int ff_set_common_samplerates(AVFilterContext *ctx,
                              AVFilterFormats *samplerates);

/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
av_warn_unused_result
int ff_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats);

av_warn_unused_result
int ff_add_channel_layout(AVFilterChannelLayouts **l, uint64_t channel_layout);

/**
 * Add *ref as a new reference to f.
 */
av_warn_unused_result
int ff_channel_layouts_ref(AVFilterChannelLayouts *f,
                           AVFilterChannelLayouts **ref);

/**
 * Remove a reference to a channel layouts list.
 */
void ff_channel_layouts_unref(AVFilterChannelLayouts **ref);

void ff_channel_layouts_changeref(AVFilterChannelLayouts **oldref,
                                  AVFilterChannelLayouts **newref);

av_warn_unused_result
int ff_default_query_formats(AVFilterContext *ctx);

/**
 * Set the formats list to all existing formats.
 * This function behaves like ff_default_query_formats(), except it also
 * accepts channel layouts with unknown disposition. It should only be used
 * with audio filters.
 */
av_warn_unused_result
int ff_query_formats_all(AVFilterContext *ctx);


/**
 * Create a list of supported formats. This is intended for use in
 * AVFilter->query_formats().
 *
 * @param fmts list of media formats, terminated by -1
 * @return the format list, with no existing references
 */
av_warn_unused_result
AVFilterFormats *ff_make_format_list(const int *fmts);

/**
 * Add fmt to the list of media formats contained in *avff.
 * If *avff is NULL the function allocates the filter formats struct
 * and puts its pointer in *avff.
 *
 * @return a non negative value in case of success, or a negative
 * value corresponding to an AVERROR code in case of error
 */
av_warn_unused_result
int ff_add_format(AVFilterFormats **avff, int64_t fmt);

/**
 * Return a list of all formats supported by FFmpeg for the given media type.
 */
av_warn_unused_result
AVFilterFormats *ff_all_formats(enum AVMediaType type);

/**
 * Construct a formats list containing all planar sample formats.
 */
av_warn_unused_result
AVFilterFormats *ff_planar_sample_fmts(void);

/**
 * Return a format list which contains the intersection of the formats of
 * a and b. Also, all the references of a, all the references of b, and
 * a and b themselves will be deallocated.
 *
 * If a and b do not share any common formats, neither is modified, and NULL
 * is returned.
 */
AVFilterFormats *ff_merge_formats(AVFilterFormats *a, AVFilterFormats *b,
                                  enum AVMediaType type);

/**
 * Add *ref as a new reference to formats.
 * That is the pointers will point like in the ascii art below:
 *   ________
 *  |formats |<--------.
 *  |  ____  |     ____|___________________
 *  | |refs| |    |  __|_
 *  | |* * | |    | |  | |  AVFilterLink
 *  | |* *--------->|*ref|
 *  | |____| |    | |____|
 *  |________|    |________________________
 */
av_warn_unused_result
int ff_formats_ref(AVFilterFormats *formats, AVFilterFormats **ref);

/**
 * If *ref is non-NULL, remove *ref as a reference to the format list
 * it currently points to, deallocates that list if this was the last
 * reference, and sets *ref to NULL.
 *
 *         Before                                 After
 *   ________                               ________         NULL
 *  |formats |<--------.                   |formats |         ^
 *  |  ____  |     ____|________________   |  ____  |     ____|________________
 *  | |refs| |    |  __|_                  | |refs| |    |  __|_
 *  | |* * | |    | |  | |  AVFilterLink   | |* * | |    | |  | |  AVFilterLink
 *  | |* *--------->|*ref|                 | |*   | |    | |*ref|
 *  | |____| |    | |____|                 | |____| |    | |____|
 *  |________|    |_____________________   |________|    |_____________________
 */
void ff_formats_unref(AVFilterFormats **ref);

/**
 *
 *         Before                                 After
 *   ________                         ________
 *  |formats |<---------.            |formats |<---------.
 *  |  ____  |       ___|___         |  ____  |       ___|___
 *  | |refs| |      |   |   |        | |refs| |      |   |   |   NULL
 *  | |* *--------->|*oldref|        | |* *--------->|*newref|     ^
 *  | |* * | |      |_______|        | |* * | |      |_______|  ___|___
 *  | |____| |                       | |____| |                |   |   |
 *  |________|                       |________|                |*oldref|
 *                                                             |_______|
 */
void ff_formats_changeref(AVFilterFormats **oldref, AVFilterFormats **newref);

#endif /* AVFILTER_FORMATS_H */
