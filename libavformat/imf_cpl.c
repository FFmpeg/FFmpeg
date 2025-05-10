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

/*
 *
 * Copyright (c) Sandflow Consulting LLC
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Implements IMP CPL processing
 *
 * @author Pierre-Anthony Lemieux
 * @file
 * @ingroup lavu_imf
 */

#include "imf.h"
#include "libavutil/bprint.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
#include <libxml/parser.h>

xmlNodePtr ff_imf_xml_get_child_element_by_name(xmlNodePtr parent, const char *name_utf8)
{
    xmlNodePtr cur_element;

    cur_element = xmlFirstElementChild(parent);
    while (cur_element) {
        if (xmlStrcmp(cur_element->name, name_utf8) == 0)
            return cur_element;

        cur_element = xmlNextElementSibling(cur_element);
    }
    return NULL;
}

int ff_imf_xml_read_uuid(xmlNodePtr element, AVUUID uuid)
{
    int ret = 0;

    xmlChar *element_text = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1);
    if (!element_text)
        return AVERROR_INVALIDDATA;
    ret = av_uuid_urn_parse(element_text, uuid);
    if (ret)
        ret = AVERROR_INVALIDDATA;
    xmlFree(element_text);

    return ret;
}

int ff_imf_xml_read_rational(xmlNodePtr element, AVRational *rational)
{
    int ret = 0;

    xmlChar *element_text = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1);
    if (element_text == NULL || sscanf(element_text, "%i %i", &rational->num, &rational->den) != 2)
        ret = AVERROR_INVALIDDATA;
    xmlFree(element_text);

    return ret;
}

int ff_imf_xml_read_uint32(xmlNodePtr element, uint32_t *number)
{
    int ret = 0;

    xmlChar *element_text = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1);
    if (element_text == NULL || sscanf(element_text, "%" PRIu32, number) != 1)
        ret = AVERROR_INVALIDDATA;
    xmlFree(element_text);

    return ret;
}

static int ff_imf_xml_read_boolean(xmlNodePtr element, int *value)
{
    int ret = 0;

    xmlChar *element_text = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1);
    if (xmlStrcmp(element_text, "true") == 0 || xmlStrcmp(element_text, "1") == 0)
        *value = 1;
    else if (xmlStrcmp(element_text, "false") == 0 || xmlStrcmp(element_text, "0") == 0)
        *value = 0;
    else
        ret = 1;
    xmlFree(element_text);

    return ret;
}

static void imf_base_virtual_track_init(FFIMFBaseVirtualTrack *track)
{
    memset(track->id_uuid, 0, sizeof(track->id_uuid));
}

static void imf_marker_virtual_track_init(FFIMFMarkerVirtualTrack *track)
{
    imf_base_virtual_track_init((FFIMFBaseVirtualTrack *)track);
    track->resource_count = 0;
    track->resources = NULL;
}

static void imf_trackfile_virtual_track_init(FFIMFTrackFileVirtualTrack *track)
{
    imf_base_virtual_track_init((FFIMFBaseVirtualTrack *)track);
    track->resource_count = 0;
    track->resources_alloc_sz = 0;
    track->resources = NULL;
}

static void imf_base_resource_init(FFIMFBaseResource *rsrc)
{
    rsrc->duration = 0;
    rsrc->edit_rate = av_make_q(0, 1);
    rsrc->entry_point = 0;
    rsrc->repeat_count = 1;
}

static void imf_marker_resource_init(FFIMFMarkerResource *rsrc)
{
    imf_base_resource_init((FFIMFBaseResource *)rsrc);
    rsrc->marker_count = 0;
    rsrc->markers = NULL;
}

static void imf_marker_init(FFIMFMarker *marker)
{
    marker->label_utf8 = NULL;
    marker->offset = 0;
    marker->scope_utf8 = NULL;
}

static void imf_trackfile_resource_init(FFIMFTrackFileResource *rsrc)
{
    imf_base_resource_init((FFIMFBaseResource *)rsrc);
    memset(rsrc->track_file_uuid, 0, sizeof(rsrc->track_file_uuid));
}

static int fill_content_title(xmlNodePtr cpl_element, FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;

    if (!(element = ff_imf_xml_get_child_element_by_name(cpl_element, "ContentTitle")))
        return AVERROR_INVALIDDATA;
    cpl->content_title_utf8 = xmlNodeListGetString(cpl_element->doc,
                                                   element->xmlChildrenNode,
                                                   1);
    if (!cpl->content_title_utf8)
        cpl->content_title_utf8 = xmlStrdup("");
    if (!cpl->content_title_utf8)
        return AVERROR(ENOMEM);

    return 0;
}

static int digit_to_int(char digit)
{
    if (digit >= '0' && digit <= '9')
        return digit - '0';
    return -1;
}

/**
 * Parses a string that conform to the TimecodeType used in IMF CPL and defined
 * in SMPTE ST 2067-3.
 * @param[in] s string to parse
 * @param[out] tc_comps pointer to an array of 4 integers where the parsed HH,
 *                      MM, SS and FF fields of the timecode are returned.
 * @return 0 on success, < 0 AVERROR code on error.
 */
static int parse_cpl_tc_type(const char *s, int *tc_comps)
{
    if (av_strnlen(s, 11) != 11)
        return AVERROR(EINVAL);

    for (int i = 0; i < 4; i++) {
        int hi;
        int lo;

        hi = digit_to_int(s[i * 3]);
        lo = digit_to_int(s[i * 3 + 1]);

        if (hi == -1 || lo == -1)
            return AVERROR(EINVAL);

        tc_comps[i] = 10 * hi + lo;
    }

    return 0;
}

static int fill_timecode(xmlNodePtr cpl_element, FFIMFCPL *cpl)
{
    xmlNodePtr tc_element = NULL;
    xmlNodePtr element = NULL;
    xmlChar *tc_str = NULL;
    int df = 0;
    int comps[4];
    int ret = 0;

    tc_element = ff_imf_xml_get_child_element_by_name(cpl_element, "CompositionTimecode");
    if (!tc_element)
       return 0;

    element = ff_imf_xml_get_child_element_by_name(tc_element, "TimecodeDropFrame");
    if (!element)
        return AVERROR_INVALIDDATA;

    if (ff_imf_xml_read_boolean(element, &df))
        return AVERROR_INVALIDDATA;

    element = ff_imf_xml_get_child_element_by_name(tc_element, "TimecodeStartAddress");
    if (!element)
        return AVERROR_INVALIDDATA;

    tc_str = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1);
    if (!tc_str)
        return AVERROR_INVALIDDATA;
    ret = parse_cpl_tc_type(tc_str, comps);
    xmlFree(tc_str);
    if (ret)
        return ret;

    cpl->tc = av_malloc(sizeof(AVTimecode));
    if (!cpl->tc)
        return AVERROR(ENOMEM);
    ret = av_timecode_init_from_components(cpl->tc, cpl->edit_rate,
                                           df ? AV_TIMECODE_FLAG_DROPFRAME : 0,
                                           comps[0], comps[1], comps[2], comps[3],
                                           NULL);

    return ret;
}

static int fill_edit_rate(xmlNodePtr cpl_element, FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;

    if (!(element = ff_imf_xml_get_child_element_by_name(cpl_element, "EditRate")))
        return AVERROR_INVALIDDATA;

    return ff_imf_xml_read_rational(element, &cpl->edit_rate);
}

static int fill_id(xmlNodePtr cpl_element, FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;

    if (!(element = ff_imf_xml_get_child_element_by_name(cpl_element, "Id")))
        return AVERROR_INVALIDDATA;

    return ff_imf_xml_read_uuid(element, cpl->id_uuid);
}

static int fill_marker(xmlNodePtr marker_elem, FFIMFMarker *marker)
{
    xmlNodePtr element = NULL;
    int ret = 0;

    /* read Offset */
    if (!(element = ff_imf_xml_get_child_element_by_name(marker_elem, "Offset")))
        return AVERROR_INVALIDDATA;

    if ((ret = ff_imf_xml_read_uint32(element, &marker->offset)))
        return ret;

    /* read Label and Scope */
    if (!(element = ff_imf_xml_get_child_element_by_name(marker_elem, "Label")))
        return AVERROR_INVALIDDATA;

    if (!(marker->label_utf8 = xmlNodeListGetString(element->doc, element->xmlChildrenNode, 1)))
        return AVERROR_INVALIDDATA;

    if (!(marker->scope_utf8 = xmlGetNoNsProp(element, "scope"))) {
        marker->scope_utf8
            = xmlCharStrdup("http://www.smpte-ra.org/schemas/2067-3/2013#standard-markers");
        if (!marker->scope_utf8) {
            xmlFree(marker->label_utf8);
            return AVERROR(ENOMEM);
        }
    }

    return ret;
}

static int fill_base_resource(void *log_ctx, xmlNodePtr resource_elem,
                              FFIMFBaseResource *resource, FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;
    int ret = 0;

    /* read EditRate */
    if (!(element = ff_imf_xml_get_child_element_by_name(resource_elem, "EditRate"))) {
        resource->edit_rate = cpl->edit_rate;
    } else if ((ret = ff_imf_xml_read_rational(element, &resource->edit_rate))) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid EditRate element found in a Resource\n");
        return ret;
    }

    /* read EntryPoint */
    if ((element = ff_imf_xml_get_child_element_by_name(resource_elem, "EntryPoint"))) {
        if ((ret = ff_imf_xml_read_uint32(element, &resource->entry_point))) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid EntryPoint element found in a Resource\n");
            return ret;
        }
    } else {
        resource->entry_point = 0;
    }

    /* read IntrinsicDuration */
    if (!(element = ff_imf_xml_get_child_element_by_name(resource_elem, "IntrinsicDuration"))) {
        av_log(log_ctx, AV_LOG_ERROR, "IntrinsicDuration element missing from Resource\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = ff_imf_xml_read_uint32(element, &resource->duration))) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid IntrinsicDuration element found in a Resource\n");
        return ret;
    }
    resource->duration -= resource->entry_point;

    /* read SourceDuration */
    if ((element = ff_imf_xml_get_child_element_by_name(resource_elem, "SourceDuration"))) {
        if ((ret = ff_imf_xml_read_uint32(element, &resource->duration))) {
            av_log(log_ctx, AV_LOG_ERROR, "SourceDuration element missing from Resource\n");
            return ret;
        }
    }

    /* read RepeatCount */
    if ((element = ff_imf_xml_get_child_element_by_name(resource_elem, "RepeatCount")))
        ret = ff_imf_xml_read_uint32(element, &resource->repeat_count);

    return ret;
}

static int fill_trackfile_resource(void *log_ctx, xmlNodePtr tf_resource_elem,
                                   FFIMFTrackFileResource *tf_resource,
                                   FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;
    int ret = 0;

    if ((ret = fill_base_resource(log_ctx, tf_resource_elem, (FFIMFBaseResource *)tf_resource, cpl)))
        return ret;

    /* read TrackFileId */
    if ((element = ff_imf_xml_get_child_element_by_name(tf_resource_elem, "TrackFileId"))) {
        if ((ret = ff_imf_xml_read_uuid(element, tf_resource->track_file_uuid))) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid TrackFileId element found in Resource\n");
            return ret;
        }
    } else {
        av_log(log_ctx, AV_LOG_ERROR, "TrackFileId element missing from Resource\n");
        return AVERROR_INVALIDDATA;
    }

    return ret;
}

static int fill_marker_resource(void *log_ctx, xmlNodePtr marker_resource_elem,
                                FFIMFMarkerResource *marker_resource,
                                FFIMFCPL *cpl)
{
    xmlNodePtr element = NULL;
    int ret = 0;

    if ((ret = fill_base_resource(log_ctx, marker_resource_elem, (FFIMFBaseResource *)marker_resource, cpl)))
        return ret;

    /* read markers */
    element = xmlFirstElementChild(marker_resource_elem);
    while (element) {
        if (xmlStrcmp(element->name, "Marker") == 0) {
            void *tmp;

            if (marker_resource->marker_count == UINT32_MAX)
                return AVERROR(ENOMEM);
            tmp = av_realloc_array(marker_resource->markers,
                                   marker_resource->marker_count + 1,
                                   sizeof(FFIMFMarker));
            if (!tmp)
                return AVERROR(ENOMEM);
            marker_resource->markers = tmp;

            imf_marker_init(&marker_resource->markers[marker_resource->marker_count]);
            ret = fill_marker(element,
                              &marker_resource->markers[marker_resource->marker_count]);
            marker_resource->marker_count++;
            if (ret) {
                av_log(log_ctx, AV_LOG_ERROR, "Invalid Marker element\n");
                return ret;
            }
        }

        element = xmlNextElementSibling(element);
    }

    return ret;
}

static int push_marker_sequence(void *log_ctx, xmlNodePtr marker_sequence_elem, FFIMFCPL *cpl)
{
    int ret = 0;
    AVUUID uuid;
    xmlNodePtr resource_list_elem = NULL;
    xmlNodePtr resource_elem = NULL;
    xmlNodePtr track_id_elem = NULL;
    unsigned long resource_elem_count;
    void *tmp;

    /* read TrackID element */
    if (!(track_id_elem = ff_imf_xml_get_child_element_by_name(marker_sequence_elem, "TrackId"))) {
        av_log(log_ctx, AV_LOG_ERROR, "TrackId element missing from Sequence\n");
        return AVERROR_INVALIDDATA;
    }
    if (ff_imf_xml_read_uuid(track_id_elem, uuid)) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid TrackId element found in Sequence\n");
        return AVERROR_INVALIDDATA;
    }
    av_log(log_ctx,
           AV_LOG_DEBUG,
           "Processing IMF CPL Marker Sequence for Virtual Track " AV_PRI_UUID "\n",
           AV_UUID_ARG(uuid));

    /* create main marker virtual track if it does not exist */
    if (!cpl->main_markers_track) {
        cpl->main_markers_track = av_malloc(sizeof(FFIMFMarkerVirtualTrack));
        if (!cpl->main_markers_track)
            return AVERROR(ENOMEM);
        imf_marker_virtual_track_init(cpl->main_markers_track);
        av_uuid_copy(cpl->main_markers_track->base.id_uuid, uuid);

    } else if (!av_uuid_equal(cpl->main_markers_track->base.id_uuid, uuid)) {
        av_log(log_ctx, AV_LOG_ERROR, "Multiple marker virtual tracks were found\n");
        return AVERROR_INVALIDDATA;
    }

    /* process resources */
    resource_list_elem = ff_imf_xml_get_child_element_by_name(marker_sequence_elem, "ResourceList");
    if (!resource_list_elem)
        return 0;

    resource_elem_count = xmlChildElementCount(resource_list_elem);
    if (resource_elem_count > UINT32_MAX
        || cpl->main_markers_track->resource_count > UINT32_MAX - resource_elem_count)
        return AVERROR(ENOMEM);
    tmp = av_realloc_array(cpl->main_markers_track->resources,
                           cpl->main_markers_track->resource_count + resource_elem_count,
                           sizeof(FFIMFMarkerResource));
    if (!tmp) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot allocate Marker Resources\n");
        return AVERROR(ENOMEM);
    }
    cpl->main_markers_track->resources = tmp;

    resource_elem = xmlFirstElementChild(resource_list_elem);
    while (resource_elem) {
        imf_marker_resource_init(&cpl->main_markers_track->resources[cpl->main_markers_track->resource_count]);
        ret = fill_marker_resource(log_ctx, resource_elem,
                                   &cpl->main_markers_track->resources[cpl->main_markers_track->resource_count],
                                   cpl);
        cpl->main_markers_track->resource_count++;
        if (ret)
            return ret;

        resource_elem = xmlNextElementSibling(resource_elem);
    }

    return ret;
}

static int has_stereo_resources(xmlNodePtr element)
{
    if (xmlStrcmp(element->name, "Left") == 0 || xmlStrcmp(element->name, "Right") == 0)
        return 1;

    element = xmlFirstElementChild(element);
    while (element) {
        if (has_stereo_resources(element))
            return 1;

        element = xmlNextElementSibling(element);
    }

    return 0;
}

static int push_main_audio_sequence(void *log_ctx, xmlNodePtr audio_sequence_elem, FFIMFCPL *cpl)
{
    int ret = 0;
    AVUUID uuid;
    xmlNodePtr resource_list_elem = NULL;
    xmlNodePtr resource_elem = NULL;
    xmlNodePtr track_id_elem = NULL;
    unsigned long resource_elem_count;
    FFIMFTrackFileVirtualTrack *vt = NULL;
    void *tmp;

    /* read TrackID element */
    if (!(track_id_elem = ff_imf_xml_get_child_element_by_name(audio_sequence_elem, "TrackId"))) {
        av_log(log_ctx, AV_LOG_ERROR, "TrackId element missing from audio sequence\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = ff_imf_xml_read_uuid(track_id_elem, uuid))) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid TrackId element found in audio sequence\n");
        return ret;
    }
    av_log(log_ctx,
           AV_LOG_DEBUG,
           "Processing IMF CPL Audio Sequence for Virtual Track " AV_PRI_UUID "\n",
           AV_UUID_ARG(uuid));

    /* get the main audio virtual track corresponding to the sequence */
    for (uint32_t i = 0; i < cpl->main_audio_track_count; i++) {
        if (av_uuid_equal(cpl->main_audio_tracks[i].base.id_uuid, uuid)) {
            vt = &cpl->main_audio_tracks[i];
            break;
        }
    }

    /* create a main audio virtual track if none exists for the sequence */
    if (!vt) {
        if (cpl->main_audio_track_count == UINT32_MAX)
            return AVERROR(ENOMEM);
        tmp = av_realloc_array(cpl->main_audio_tracks,
                               cpl->main_audio_track_count + 1,
                               sizeof(FFIMFTrackFileVirtualTrack));
        if (!tmp)
            return AVERROR(ENOMEM);

        cpl->main_audio_tracks = tmp;
        vt = &cpl->main_audio_tracks[cpl->main_audio_track_count];
        imf_trackfile_virtual_track_init(vt);
        cpl->main_audio_track_count++;
        av_uuid_copy(vt->base.id_uuid, uuid);
    }

    /* process resources */
    resource_list_elem = ff_imf_xml_get_child_element_by_name(audio_sequence_elem, "ResourceList");
    if (!resource_list_elem)
        return 0;

    resource_elem_count = xmlChildElementCount(resource_list_elem);
    if (resource_elem_count > UINT32_MAX
        || vt->resource_count > UINT32_MAX - resource_elem_count)
        return AVERROR(ENOMEM);
    tmp = av_fast_realloc(vt->resources,
                          &vt->resources_alloc_sz,
                          (vt->resource_count + resource_elem_count)
                              * sizeof(FFIMFTrackFileResource));
    if (!tmp) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot allocate Main Audio Resources\n");
        return AVERROR(ENOMEM);
    }
    vt->resources = tmp;

    resource_elem = xmlFirstElementChild(resource_list_elem);
    while (resource_elem) {
        imf_trackfile_resource_init(&vt->resources[vt->resource_count]);
        ret = fill_trackfile_resource(log_ctx, resource_elem,
                                      &vt->resources[vt->resource_count],
                                      cpl);
        if (ret)
            av_log(log_ctx, AV_LOG_ERROR, "Invalid Resource\n");
        else
            vt->resource_count++;

        resource_elem = xmlNextElementSibling(resource_elem);
    }

    return ret;
}

static int push_main_image_2d_sequence(void *log_ctx, xmlNodePtr image_sequence_elem, FFIMFCPL *cpl)
{
    int ret = 0;
    AVUUID uuid;
    xmlNodePtr resource_list_elem = NULL;
    xmlNodePtr resource_elem = NULL;
    xmlNodePtr track_id_elem = NULL;
    void *tmp;
    unsigned long resource_elem_count;

    /* skip stereoscopic resources */
    if (has_stereo_resources(image_sequence_elem)) {
        av_log(log_ctx, AV_LOG_ERROR, "Stereoscopic 3D image virtual tracks not supported\n");
        return AVERROR_PATCHWELCOME;
    }

    /* read TrackId element*/
    if (!(track_id_elem = ff_imf_xml_get_child_element_by_name(image_sequence_elem, "TrackId"))) {
        av_log(log_ctx, AV_LOG_ERROR, "TrackId element missing from audio sequence\n");
        return AVERROR_INVALIDDATA;
    }
    if ((ret = ff_imf_xml_read_uuid(track_id_elem, uuid))) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid TrackId element found in audio sequence\n");
        return ret;
    }

    /* create main image virtual track if one does not exist */
    if (!cpl->main_image_2d_track) {
        cpl->main_image_2d_track = av_malloc(sizeof(FFIMFTrackFileVirtualTrack));
        if (!cpl->main_image_2d_track)
            return AVERROR(ENOMEM);
        imf_trackfile_virtual_track_init(cpl->main_image_2d_track);
        av_uuid_copy(cpl->main_image_2d_track->base.id_uuid, uuid);

    } else if (!av_uuid_equal(cpl->main_image_2d_track->base.id_uuid, uuid)) {
        av_log(log_ctx, AV_LOG_ERROR, "Multiple MainImage virtual tracks found\n");
        return AVERROR_INVALIDDATA;
    }
    av_log(log_ctx,
           AV_LOG_DEBUG,
           "Processing IMF CPL Main Image Sequence for Virtual Track " AV_PRI_UUID "\n",
           AV_UUID_ARG(uuid));

    /* process resources */
    resource_list_elem = ff_imf_xml_get_child_element_by_name(image_sequence_elem, "ResourceList");
    if (!resource_list_elem)
        return 0;

    resource_elem_count = xmlChildElementCount(resource_list_elem);
    if (resource_elem_count > UINT32_MAX
        || cpl->main_image_2d_track->resource_count > UINT32_MAX - resource_elem_count
        || (cpl->main_image_2d_track->resource_count + resource_elem_count)
            > INT_MAX / sizeof(FFIMFTrackFileResource))
        return AVERROR(ENOMEM);
    tmp = av_fast_realloc(cpl->main_image_2d_track->resources,
                          &cpl->main_image_2d_track->resources_alloc_sz,
                          (cpl->main_image_2d_track->resource_count + resource_elem_count)
                              * sizeof(FFIMFTrackFileResource));
    if (!tmp) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot allocate Main Image Resources\n");
        return AVERROR(ENOMEM);
    }
    cpl->main_image_2d_track->resources = tmp;

    resource_elem = xmlFirstElementChild(resource_list_elem);
    while (resource_elem) {
        imf_trackfile_resource_init(
            &cpl->main_image_2d_track->resources[cpl->main_image_2d_track->resource_count]);
        ret = fill_trackfile_resource(log_ctx, resource_elem,
                                      &cpl->main_image_2d_track->resources[cpl->main_image_2d_track->resource_count],
                                      cpl);
        if (ret)
            av_log(log_ctx, AV_LOG_ERROR, "Invalid Resource\n");
        else
            cpl->main_image_2d_track->resource_count++;

        resource_elem = xmlNextElementSibling(resource_elem);
    }

    return 0;
}

static int fill_virtual_tracks(void *log_ctx, xmlNodePtr cpl_element, FFIMFCPL *cpl)
{
    int ret = 0;
    xmlNodePtr segment_list_elem = NULL;
    xmlNodePtr segment_elem = NULL;
    xmlNodePtr sequence_list_elem = NULL;
    xmlNodePtr sequence_elem = NULL;

    if (!(segment_list_elem = ff_imf_xml_get_child_element_by_name(cpl_element, "SegmentList"))) {
        av_log(log_ctx, AV_LOG_ERROR, "SegmentList element missing\n");
        return AVERROR_INVALIDDATA;
    }

    /* process sequences */
    segment_elem = xmlFirstElementChild(segment_list_elem);
    while (segment_elem) {
        av_log(log_ctx, AV_LOG_DEBUG, "Processing IMF CPL Segment\n");

        sequence_list_elem = ff_imf_xml_get_child_element_by_name(segment_elem, "SequenceList");
        if (sequence_list_elem) {

            sequence_elem = xmlFirstElementChild(sequence_list_elem);
            while (sequence_elem) {
                if (xmlStrcmp(sequence_elem->name, "MarkerSequence") == 0)
                    ret = push_marker_sequence(log_ctx, sequence_elem, cpl);

                else if (xmlStrcmp(sequence_elem->name, "MainImageSequence") == 0)
                    ret = push_main_image_2d_sequence(log_ctx, sequence_elem, cpl);

                else if (xmlStrcmp(sequence_elem->name, "MainAudioSequence") == 0)
                    ret = push_main_audio_sequence(log_ctx, sequence_elem, cpl);

                else
                    av_log(log_ctx,
                        AV_LOG_INFO,
                        "The following Sequence is not supported and is ignored: %s\n",
                        sequence_elem->name);

                /* abort parsing only if memory error occurred */
                if (ret == AVERROR(ENOMEM))
                    return ret;

                sequence_elem = xmlNextElementSibling(sequence_elem);
            }
        }

        segment_elem = xmlNextElementSibling(segment_elem);
    }

    return ret;
}

int ff_imf_parse_cpl_from_xml_dom(void *log_ctx, xmlDocPtr doc, FFIMFCPL **cpl)
{
    int ret = 0;
    xmlNodePtr cpl_element = NULL;

    *cpl = ff_imf_cpl_alloc();
    if (!*cpl) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }

    cpl_element = xmlDocGetRootElement(doc);
    if (!cpl_element || xmlStrcmp(cpl_element->name, "CompositionPlaylist")) {
        av_log(log_ctx, AV_LOG_ERROR, "The root element of the CPL is not CompositionPlaylist\n");
        ret = AVERROR_INVALIDDATA;
        goto cleanup;
    }

    if ((ret = fill_content_title(cpl_element, *cpl))) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot read the ContentTitle element from the IMF CPL\n");
        goto cleanup;
    }
    if ((ret = fill_id(cpl_element, *cpl))) {
        av_log(log_ctx, AV_LOG_ERROR, "Id element not found in the IMF CPL\n");
        goto cleanup;
    }
    if ((ret = fill_edit_rate(cpl_element, *cpl))) {
        av_log(log_ctx, AV_LOG_ERROR, "EditRate element not found in the IMF CPL\n");
        goto cleanup;
    }
    if ((ret = fill_timecode(cpl_element, *cpl))) {
        av_log(log_ctx, AV_LOG_ERROR, "Invalid CompositionTimecode element found in the IMF CPL\n");
        goto cleanup;
    }
    if ((ret = fill_virtual_tracks(log_ctx, cpl_element, *cpl)))
        goto cleanup;

cleanup:
    if (*cpl && ret) {
        ff_imf_cpl_free(*cpl);
        *cpl = NULL;
    }
    return ret;
}

static void imf_marker_free(FFIMFMarker *marker)
{
    if (!marker)
        return;
    xmlFree(marker->label_utf8);
    xmlFree(marker->scope_utf8);
}

static void imf_marker_resource_free(FFIMFMarkerResource *rsrc)
{
    if (!rsrc)
        return;
    for (uint32_t i = 0; i < rsrc->marker_count; i++)
        imf_marker_free(&rsrc->markers[i]);
    av_freep(&rsrc->markers);
}

static void imf_marker_virtual_track_free(FFIMFMarkerVirtualTrack *vt)
{
    if (!vt)
        return;
    for (uint32_t i = 0; i < vt->resource_count; i++)
        imf_marker_resource_free(&vt->resources[i]);
    av_freep(&vt->resources);
}

static void imf_trackfile_virtual_track_free(FFIMFTrackFileVirtualTrack *vt)
{
    if (!vt)
        return;
    av_freep(&vt->resources);
}

static void imf_cpl_init(FFIMFCPL *cpl)
{
    av_uuid_nil(cpl->id_uuid);
    cpl->content_title_utf8 = NULL;
    cpl->edit_rate = av_make_q(0, 1);
    cpl->tc = NULL;
    cpl->main_markers_track = NULL;
    cpl->main_image_2d_track = NULL;
    cpl->main_audio_track_count = 0;
    cpl->main_audio_tracks = NULL;
}

FFIMFCPL *ff_imf_cpl_alloc(void)
{
    FFIMFCPL *cpl;

    cpl = av_malloc(sizeof(FFIMFCPL));
    if (!cpl)
        return NULL;
    imf_cpl_init(cpl);
    return cpl;
}

void ff_imf_cpl_free(FFIMFCPL *cpl)
{
    if (!cpl)
        return;

    if (cpl->tc)
        av_freep(&cpl->tc);

    xmlFree(cpl->content_title_utf8);

    imf_marker_virtual_track_free(cpl->main_markers_track);

    if (cpl->main_markers_track)
        av_freep(&cpl->main_markers_track);

    imf_trackfile_virtual_track_free(cpl->main_image_2d_track);

    if (cpl->main_image_2d_track)
        av_freep(&cpl->main_image_2d_track);

    for (uint32_t i = 0; i < cpl->main_audio_track_count; i++)
        imf_trackfile_virtual_track_free(&cpl->main_audio_tracks[i]);

    if (cpl->main_audio_tracks)
        av_freep(&cpl->main_audio_tracks);

    av_freep(&cpl);
}

int ff_imf_parse_cpl(void *log_ctx, AVIOContext *in, FFIMFCPL **cpl)
{
    AVBPrint buf;
    xmlDoc *doc = NULL;
    int ret = 0;

    av_bprint_init(&buf, 0, INT_MAX); // xmlReadMemory uses integer length

    ret = avio_read_to_bprint(in, &buf, SIZE_MAX);
    if (ret < 0 || !avio_feof(in)) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot read IMF CPL\n");
        if (ret == 0)
            ret = AVERROR_INVALIDDATA;
        goto clean_up;
    }

    LIBXML_TEST_VERSION

    doc = xmlReadMemory(buf.str, buf.len, NULL, NULL, 0);
    if (!doc) {
        av_log(log_ctx,
                AV_LOG_ERROR,
                "XML parsing failed when reading the IMF CPL\n");
        ret = AVERROR_INVALIDDATA;
        goto clean_up;
    }

    if ((ret = ff_imf_parse_cpl_from_xml_dom(log_ctx, doc, cpl))) {
        av_log(log_ctx, AV_LOG_ERROR, "Cannot parse IMF CPL\n");
    } else {
        av_log(log_ctx,
                AV_LOG_INFO,
                "IMF CPL ContentTitle: %s\n",
                (*cpl)->content_title_utf8);
        av_log(log_ctx,
                AV_LOG_INFO,
                "IMF CPL Id: " AV_PRI_UUID "\n",
                AV_UUID_ARG((*cpl)->id_uuid));
    }

    xmlFreeDoc(doc);

clean_up:
    av_bprint_finalize(&buf, NULL);

    return ret;
}
