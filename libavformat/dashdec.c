/*
 * Dynamic Adaptive Streaming over HTTP demux
 * Copyright (c) 2017 samsamsam@o2.pl based on HLS demux
 * Copyright (c) 2017 Steven Liu
 *
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
#include <libxml/parser.h>
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"
#include "internal.h"
#include "avio_internal.h"
#include "dash.h"

#define INITIAL_BUFFER_SIZE 32768

struct fragment {
    int64_t url_offset;
    int64_t size;
    char *url;
};

/*
 * reference to : ISO_IEC_23009-1-DASH-2012
 * Section: 5.3.9.6.2
 * Table: Table 17 — Semantics of SegmentTimeline element
 * */
struct timeline {
    /* starttime: Element or Attribute Name
     * specifies the MPD start time, in @timescale units,
     * the first Segment in the series starts relative to the beginning of the Period.
     * The value of this attribute must be equal to or greater than the sum of the previous S
     * element earliest presentation time and the sum of the contiguous Segment durations.
     * If the value of the attribute is greater than what is expressed by the previous S element,
     * it expresses discontinuities in the timeline.
     * If not present then the value shall be assumed to be zero for the first S element
     * and for the subsequent S elements, the value shall be assumed to be the sum of
     * the previous S element's earliest presentation time and contiguous duration
     * (i.e. previous S@starttime + @duration * (@repeat + 1)).
     * */
    int64_t starttime;
    /* repeat: Element or Attribute Name
     * specifies the repeat count of the number of following contiguous Segments with
     * the same duration expressed by the value of @duration. This value is zero-based
     * (e.g. a value of three means four Segments in the contiguous series).
     * */
    int64_t repeat;
    /* duration: Element or Attribute Name
     * specifies the Segment duration, in units of the value of the @timescale.
     * */
    int64_t duration;
};

/*
 * Each playlist has its own demuxer. If it is currently active,
 * it has an opened AVIOContext too, and potentially an AVPacket
 * containing the next packet from this stream.
 */
struct representation {
    char *url_template;
    AVIOContext pb;
    AVIOContext *input;
    AVFormatContext *parent;
    AVFormatContext *ctx;
    AVPacket pkt;
    int rep_idx;
    int rep_count;
    int stream_index;

    enum AVMediaType type;

    int n_fragments;
    struct fragment **fragments; /* VOD list of fragment for profile */

    int n_timelines;
    struct timeline **timelines;

    int64_t first_seq_no;
    int64_t last_seq_no;
    int64_t start_number; /* used in case when we have dynamic list of segment to know which segments are new one*/

    int64_t fragment_duration;
    int64_t fragment_timescale;

    int64_t presentation_timeoffset;

    int64_t cur_seq_no;
    int64_t cur_seg_offset;
    int64_t cur_seg_size;
    struct fragment *cur_seg;

    /* Currently active Media Initialization Section */
    struct fragment *init_section;
    uint8_t *init_sec_buf;
    uint32_t init_sec_buf_size;
    uint32_t init_sec_data_len;
    uint32_t init_sec_buf_read_offset;
    int64_t cur_timestamp;
    int is_restart_needed;
};

typedef struct DASHContext {
    const AVClass *class;
    char *base_url;
    struct representation *cur_video;
    struct representation *cur_audio;

    /* MediaPresentationDescription Attribute */
    uint64_t media_presentation_duration;
    uint64_t suggested_presentation_delay;
    uint64_t availability_start_time;
    uint64_t publish_time;
    uint64_t minimum_update_period;
    uint64_t time_shift_buffer_depth;
    uint64_t min_buffer_time;

    /* Period Attribute */
    uint64_t period_duration;
    uint64_t period_start;

    int is_live;
    AVIOInterruptCB *interrupt_callback;
    char *user_agent;                    ///< holds HTTP user agent set as an AVOption to the HTTP protocol context
    char *cookies;                       ///< holds HTTP cookie values set in either the initial response or as an AVOption to the HTTP protocol context
    char *headers;                       ///< holds HTTP headers set as an AVOption to the HTTP protocol context
    char *allowed_extensions;
    AVDictionary *avio_opts;
} DASHContext;

static uint64_t get_current_time_in_sec(void)
{
    return  av_gettime() / 1000000;
}

static uint64_t get_utc_date_time_insec(AVFormatContext *s, const char *datetime)
{
    struct tm timeinfo;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int ret = 0;
    float second = 0.0;

    /* ISO-8601 date parser */
    if (!datetime)
        return 0;

    ret = sscanf(datetime, "%d-%d-%dT%d:%d:%fZ", &year, &month, &day, &hour, &minute, &second);
    /* year, month, day, hour, minute, second  6 arguments */
    if (ret != 6) {
        av_log(s, AV_LOG_WARNING, "get_utc_date_time_insec get a wrong time format\n");
    }
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon  = month - 1;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min  = minute;
    timeinfo.tm_sec  = (int)second;

    return av_timegm(&timeinfo);
}

static uint32_t get_duration_insec(AVFormatContext *s, const char *duration)
{
    /* ISO-8601 duration parser */
    uint32_t days = 0;
    uint32_t hours = 0;
    uint32_t mins = 0;
    uint32_t secs = 0;
    int size = 0;
    float value = 0;
    char type = '\0';
    const char *ptr = duration;

    while (*ptr) {
        if (*ptr == 'P' || *ptr == 'T') {
            ptr++;
            continue;
        }

        if (sscanf(ptr, "%f%c%n", &value, &type, &size) != 2) {
            av_log(s, AV_LOG_WARNING, "get_duration_insec get a wrong time format\n");
            return 0; /* parser error */
        }
        switch (type) {
            case 'D':
                days = (uint32_t)value;
                break;
            case 'H':
                hours = (uint32_t)value;
                break;
            case 'M':
                mins = (uint32_t)value;
                break;
            case 'S':
                secs = (uint32_t)value;
                break;
            default:
                // handle invalid type
                break;
        }
        ptr += size;
    }
    return  ((days * 24 + hours) * 60 + mins) * 60 + secs;
}

static int64_t get_segment_start_time_based_on_timeline(struct representation *pls, int64_t cur_seq_no)
{
    int64_t start_time = 0;
    int64_t i = 0;
    int64_t j = 0;
    int64_t num = 0;

    if (pls->n_timelines) {
        for (i = 0; i < pls->n_timelines; i++) {
            if (pls->timelines[i]->starttime > 0) {
                start_time = pls->timelines[i]->starttime;
            }
            if (num == cur_seq_no)
                goto finish;

            start_time += pls->timelines[i]->duration;
            for (j = 0; j < pls->timelines[i]->repeat; j++) {
                num++;
                if (num == cur_seq_no)
                    goto finish;
                start_time += pls->timelines[i]->duration;
            }
            num++;
        }
    }
finish:
    return start_time;
}

static int64_t calc_next_seg_no_from_timelines(struct representation *pls, int64_t cur_time)
{
    int64_t i = 0;
    int64_t j = 0;
    int64_t num = 0;
    int64_t start_time = 0;

    for (i = 0; i < pls->n_timelines; i++) {
        if (pls->timelines[i]->starttime > 0) {
            start_time = pls->timelines[i]->starttime;
        }
        if (start_time > cur_time)
            goto finish;

        start_time += pls->timelines[i]->duration;
        for (j = 0; j < pls->timelines[i]->repeat; j++) {
            num++;
            if (start_time > cur_time)
                goto finish;
            start_time += pls->timelines[i]->duration;
        }
        num++;
    }

    return -1;

finish:
    return num;
}

static void free_fragment(struct fragment **seg)
{
    if (!(*seg)) {
        return;
    }
    av_freep(&(*seg)->url);
    av_freep(seg);
}

static void free_fragment_list(struct representation *pls)
{
    int i;

    for (i = 0; i < pls->n_fragments; i++) {
        free_fragment(&pls->fragments[i]);
    }
    av_freep(&pls->fragments);
    pls->n_fragments = 0;
}

static void free_timelines_list(struct representation *pls)
{
    int i;

    for (i = 0; i < pls->n_timelines; i++) {
        av_freep(&pls->timelines[i]);
    }
    av_freep(&pls->timelines);
    pls->n_timelines = 0;
}

static void free_representation(struct representation *pls)
{
    free_fragment_list(pls);
    free_timelines_list(pls);
    free_fragment(&pls->cur_seg);
    free_fragment(&pls->init_section);
    av_freep(&pls->init_sec_buf);
    av_freep(&pls->pb.buffer);
    if (pls->input)
        ff_format_io_close(pls->parent, &pls->input);
    if (pls->ctx) {
        pls->ctx->pb = NULL;
        avformat_close_input(&pls->ctx);
    }

    av_freep(&pls->url_template);
    av_freep(pls);
}

static void set_httpheader_options(DASHContext *c, AVDictionary *opts)
{
    // broker prior HTTP options that should be consistent across requests
    av_dict_set(&opts, "user-agent", c->user_agent, 0);
    av_dict_set(&opts, "cookies", c->cookies, 0);
    av_dict_set(&opts, "headers", c->headers, 0);
    if (c->is_live) {
        av_dict_set(&opts, "seekable", "0", 0);
    }
}
static void update_options(char **dest, const char *name, void *src)
{
    av_freep(dest);
    av_opt_get(src, name, AV_OPT_SEARCH_CHILDREN, (uint8_t**)dest);
    if (*dest)
        av_freep(dest);
}

static int open_url(AVFormatContext *s, AVIOContext **pb, const char *url,
                    AVDictionary *opts, AVDictionary *opts2, int *is_http)
{
    DASHContext *c = s->priv_data;
    AVDictionary *tmp = NULL;
    const char *proto_name = NULL;
    int ret;

    av_dict_copy(&tmp, opts, 0);
    av_dict_copy(&tmp, opts2, 0);

    if (av_strstart(url, "crypto", NULL)) {
        if (url[6] == '+' || url[6] == ':')
            proto_name = avio_find_protocol_name(url + 7);
    }

    if (!proto_name)
        proto_name = avio_find_protocol_name(url);

    if (!proto_name)
        return AVERROR_INVALIDDATA;

    // only http(s) & file are allowed
    if (av_strstart(proto_name, "file", NULL)) {
        if (strcmp(c->allowed_extensions, "ALL") && !av_match_ext(url, c->allowed_extensions)) {
            av_log(s, AV_LOG_ERROR,
                "Filename extension of \'%s\' is not a common multimedia extension, blocked for security reasons.\n"
                "If you wish to override this adjust allowed_extensions, you can set it to \'ALL\' to allow all\n",
                url);
            return AVERROR_INVALIDDATA;
        }
    } else if (av_strstart(proto_name, "http", NULL)) {
        ;
    } else
        return AVERROR_INVALIDDATA;

    if (!strncmp(proto_name, url, strlen(proto_name)) && url[strlen(proto_name)] == ':')
        ;
    else if (av_strstart(url, "crypto", NULL) && !strncmp(proto_name, url + 7, strlen(proto_name)) && url[7 + strlen(proto_name)] == ':')
        ;
    else if (strcmp(proto_name, "file") || !strncmp(url, "file,", 5))
        return AVERROR_INVALIDDATA;

    ret = s->io_open(s, pb, url, AVIO_FLAG_READ, &tmp);
    if (ret >= 0) {
        // update cookies on http response with setcookies.
        char *new_cookies = NULL;

        if (!(s->flags & AVFMT_FLAG_CUSTOM_IO))
            av_opt_get(*pb, "cookies", AV_OPT_SEARCH_CHILDREN, (uint8_t**)&new_cookies);

        if (new_cookies) {
            av_free(c->cookies);
            c->cookies = new_cookies;
        }

        av_dict_set(&opts, "cookies", c->cookies, 0);
    }

    av_dict_free(&tmp);

    if (is_http)
        *is_http = av_strstart(proto_name, "http", NULL);

    return ret;
}

static char *get_content_url(xmlNodePtr *baseurl_nodes,
                             int n_baseurl_nodes,
                             char *rep_id_val,
                             char *rep_bandwidth_val,
                             char *val)
{
    int i;
    char *text;
    char *url = NULL;
    char tmp_str[MAX_URL_SIZE];
    char tmp_str_2[MAX_URL_SIZE];

    memset(tmp_str, 0, sizeof(tmp_str));

    for (i = 0; i < n_baseurl_nodes; ++i) {
        if (baseurl_nodes[i] &&
            baseurl_nodes[i]->children &&
            baseurl_nodes[i]->children->type == XML_TEXT_NODE) {
            text = xmlNodeGetContent(baseurl_nodes[i]->children);
            if (text) {
                memset(tmp_str, 0, sizeof(tmp_str));
                memset(tmp_str_2, 0, sizeof(tmp_str_2));
                ff_make_absolute_url(tmp_str_2, MAX_URL_SIZE, tmp_str, text);
                av_strlcpy(tmp_str, tmp_str_2, sizeof(tmp_str));
                xmlFree(text);
            }
        }
    }

    if (val)
        av_strlcat(tmp_str, (const char*)val, sizeof(tmp_str));

    if (rep_id_val) {
        url = av_strireplace(tmp_str, "$RepresentationID$", (const char*)rep_id_val);
        if (!url) {
            return NULL;
        }
        av_strlcpy(tmp_str, url, sizeof(tmp_str));
        av_free(url);
    }
    if (rep_bandwidth_val && tmp_str[0] != '\0') {
        url = av_strireplace(tmp_str, "$Bandwidth$", (const char*)rep_bandwidth_val);
        if (!url) {
            return NULL;
        }
    }
    return url;
}

static char *get_val_from_nodes_tab(xmlNodePtr *nodes, const int n_nodes, const char *attrname)
{
    int i;
    char *val;

    for (i = 0; i < n_nodes; ++i) {
        if (nodes[i]) {
            val = xmlGetProp(nodes[i], attrname);
            if (val)
                return val;
        }
    }

    return NULL;
}

static xmlNodePtr find_child_node_by_name(xmlNodePtr rootnode, const char *nodename)
{
    xmlNodePtr node = rootnode;
    if (!node) {
        return NULL;
    }

    node = xmlFirstElementChild(node);
    while (node) {
        if (!av_strcasecmp(node->name, nodename)) {
            return node;
        }
        node = xmlNextElementSibling(node);
    }
    return NULL;
}

static enum AVMediaType get_content_type(xmlNodePtr node)
{
    enum AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
    int i = 0;
    const char *attr;
    char *val = NULL;

    if (node) {
        for (i = 0; i < 2; i++) {
            attr = i ? "mimeType" : "contentType";
            val = xmlGetProp(node, attr);
            if (val) {
                if (av_stristr((const char *)val, "video")) {
                    type = AVMEDIA_TYPE_VIDEO;
                } else if (av_stristr((const char *)val, "audio")) {
                    type = AVMEDIA_TYPE_AUDIO;
                }
                xmlFree(val);
            }
        }
    }
    return type;
}

static int parse_manifest_segmenturlnode(AVFormatContext *s, struct representation *rep,
                                         xmlNodePtr fragmenturl_node,
                                         xmlNodePtr *baseurl_nodes,
                                         char *rep_id_val,
                                         char *rep_bandwidth_val)
{
    char *initialization_val = NULL;
    char *media_val = NULL;

    if (!av_strcasecmp(fragmenturl_node->name, (const char *)"Initialization")) {
        initialization_val = xmlGetProp(fragmenturl_node, "sourceURL");
        if (initialization_val) {
            rep->init_section = av_mallocz(sizeof(struct fragment));
            if (!rep->init_section) {
                xmlFree(initialization_val);
                return AVERROR(ENOMEM);
            }
            rep->init_section->url = get_content_url(baseurl_nodes, 4,
                                                     rep_id_val,
                                                     rep_bandwidth_val,
                                                     initialization_val);
            if (!rep->init_section->url) {
                av_free(rep->init_section);
                xmlFree(initialization_val);
                return AVERROR(ENOMEM);
            }
            rep->init_section->size = -1;
            xmlFree(initialization_val);
        }
    } else if (!av_strcasecmp(fragmenturl_node->name, (const char *)"SegmentURL")) {
        media_val = xmlGetProp(fragmenturl_node, "media");
        if (media_val) {
            struct fragment *seg = av_mallocz(sizeof(struct fragment));
            if (!seg) {
                xmlFree(media_val);
                return AVERROR(ENOMEM);
            }
            seg->url = get_content_url(baseurl_nodes, 4,
                                       rep_id_val,
                                       rep_bandwidth_val,
                                       media_val);
            if (!seg->url) {
                av_free(seg);
                xmlFree(media_val);
                return AVERROR(ENOMEM);
            }
            seg->size = -1;
            dynarray_add(&rep->fragments, &rep->n_fragments, seg);
            xmlFree(media_val);
        }
    }

    return 0;
}

static int parse_manifest_segmenttimeline(AVFormatContext *s, struct representation *rep,
                                          xmlNodePtr fragment_timeline_node)
{
    xmlAttrPtr attr = NULL;
    char *val  = NULL;

    if (!av_strcasecmp(fragment_timeline_node->name, (const char *)"S")) {
        struct timeline *tml = av_mallocz(sizeof(struct timeline));
        if (!tml) {
            return AVERROR(ENOMEM);
        }
        attr = fragment_timeline_node->properties;
        while (attr) {
            val = xmlGetProp(fragment_timeline_node, attr->name);

            if (!val) {
                av_log(s, AV_LOG_WARNING, "parse_manifest_segmenttimeline attr->name = %s val is NULL\n", attr->name);
                continue;
            }

            if (!av_strcasecmp(attr->name, (const char *)"t")) {
                tml->starttime = (int64_t)strtoll(val, NULL, 10);
            } else if (!av_strcasecmp(attr->name, (const char *)"r")) {
                tml->repeat =(int64_t) strtoll(val, NULL, 10);
            } else if (!av_strcasecmp(attr->name, (const char *)"d")) {
                tml->duration = (int64_t)strtoll(val, NULL, 10);
            }
            attr = attr->next;
            xmlFree(val);
        }
        dynarray_add(&rep->timelines, &rep->n_timelines, tml);
    }

    return 0;
}

static int parse_manifest_representation(AVFormatContext *s, const char *url,
                                         xmlNodePtr node,
                                         xmlNodePtr adaptionset_node,
                                         xmlNodePtr mpd_baseurl_node,
                                         xmlNodePtr period_baseurl_node,
                                         xmlNodePtr fragment_template_node,
                                         xmlNodePtr content_component_node,
                                         xmlNodePtr adaptionset_baseurl_node)
{
    int32_t ret = 0;
    int32_t audio_rep_idx = 0;
    int32_t video_rep_idx = 0;
    DASHContext *c = s->priv_data;
    struct representation *rep = NULL;
    struct fragment *seg = NULL;
    xmlNodePtr representation_segmenttemplate_node = NULL;
    xmlNodePtr representation_baseurl_node = NULL;
    xmlNodePtr representation_segmentlist_node = NULL;
    xmlNodePtr fragment_timeline_node = NULL;
    xmlNodePtr fragment_templates_tab[2];
    char *duration_val = NULL;
    char *presentation_timeoffset_val = NULL;
    char *startnumber_val = NULL;
    char *timescale_val = NULL;
    char *initialization_val = NULL;
    char *media_val = NULL;
    xmlNodePtr baseurl_nodes[4];
    xmlNodePtr representation_node = node;
    char *rep_id_val = xmlGetProp(representation_node, "id");
    char *rep_bandwidth_val = xmlGetProp(representation_node, "bandwidth");
    enum AVMediaType type = AVMEDIA_TYPE_UNKNOWN;

    // try get information from representation
    if (type == AVMEDIA_TYPE_UNKNOWN)
        type = get_content_type(representation_node);
    // try get information from contentComponen
    if (type == AVMEDIA_TYPE_UNKNOWN)
        type = get_content_type(content_component_node);
    // try get information from adaption set
    if (type == AVMEDIA_TYPE_UNKNOWN)
        type = get_content_type(adaptionset_node);
    if (type == AVMEDIA_TYPE_UNKNOWN) {
        av_log(s, AV_LOG_VERBOSE, "Parsing '%s' - skipp not supported representation type\n", url);
    } else if ((type == AVMEDIA_TYPE_VIDEO && !c->cur_video) || (type == AVMEDIA_TYPE_AUDIO && !c->cur_audio)) {
        // convert selected representation to our internal struct
        rep = av_mallocz(sizeof(struct representation));
        if (!rep) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
        representation_segmenttemplate_node = find_child_node_by_name(representation_node, "SegmentTemplate");
        representation_baseurl_node = find_child_node_by_name(representation_node, "BaseURL");
        representation_segmentlist_node = find_child_node_by_name(representation_node, "SegmentList");

        baseurl_nodes[0] = mpd_baseurl_node;
        baseurl_nodes[1] = period_baseurl_node;
        baseurl_nodes[2] = adaptionset_baseurl_node;
        baseurl_nodes[3] = representation_baseurl_node;

        if (representation_segmenttemplate_node || fragment_template_node) {
            fragment_timeline_node = NULL;
            fragment_templates_tab[0] = representation_segmenttemplate_node;
            fragment_templates_tab[1] = fragment_template_node;

            presentation_timeoffset_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "presentationTimeOffset");
            duration_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "duration");
            startnumber_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "startNumber");
            timescale_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "timescale");
            initialization_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "initialization");
            media_val = get_val_from_nodes_tab(fragment_templates_tab, 2, "media");

            if (initialization_val) {
                rep->init_section = av_mallocz(sizeof(struct fragment));
                if (!rep->init_section) {
                    av_free(rep);
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
                rep->init_section->url = get_content_url(baseurl_nodes, 4, rep_id_val, rep_bandwidth_val, initialization_val);
                if (!rep->init_section->url) {
                    av_free(rep->init_section);
                    av_free(rep);
                    ret = AVERROR(ENOMEM);
                    goto end;
                }
                rep->init_section->size = -1;
                xmlFree(initialization_val);
            }

            if (media_val) {
                rep->url_template = get_content_url(baseurl_nodes, 4, rep_id_val, rep_bandwidth_val, media_val);
                xmlFree(media_val);
            }

            if (presentation_timeoffset_val) {
                rep->presentation_timeoffset = (int64_t) strtoll(presentation_timeoffset_val, NULL, 10);
                xmlFree(presentation_timeoffset_val);
            }
            if (duration_val) {
                rep->fragment_duration = (int64_t) strtoll(duration_val, NULL, 10);
                xmlFree(duration_val);
            }
            if (timescale_val) {
                rep->fragment_timescale = (int64_t) strtoll(timescale_val, NULL, 10);
                xmlFree(timescale_val);
            }
            if (startnumber_val) {
                rep->first_seq_no = (int64_t) strtoll(startnumber_val, NULL, 10);
                xmlFree(startnumber_val);
            }

            fragment_timeline_node = find_child_node_by_name(representation_segmenttemplate_node, "SegmentTimeline");

            if (!fragment_timeline_node)
                fragment_timeline_node = find_child_node_by_name(fragment_template_node, "SegmentTimeline");
            if (fragment_timeline_node) {
                fragment_timeline_node = xmlFirstElementChild(fragment_timeline_node);
                while (fragment_timeline_node) {
                    ret = parse_manifest_segmenttimeline(s, rep, fragment_timeline_node);
                    if (ret < 0) {
                        return ret;
                    }
                    fragment_timeline_node = xmlNextElementSibling(fragment_timeline_node);
                }
            }
        } else if (representation_baseurl_node && !representation_segmentlist_node) {
            seg = av_mallocz(sizeof(struct fragment));
            if (!seg) {
                ret = AVERROR(ENOMEM);
                goto end;
            }
            seg->url = get_content_url(baseurl_nodes, 4, rep_id_val, rep_bandwidth_val, NULL);
            if (!seg->url) {
                av_free(seg);
                ret = AVERROR(ENOMEM);
                goto end;
            }
            seg->size = -1;
            dynarray_add(&rep->fragments, &rep->n_fragments, seg);
        } else if (representation_segmentlist_node) {
            // TODO: https://www.brendanlong.com/the-structure-of-an-mpeg-dash-mpd.html
            // http://www-itec.uni-klu.ac.at/dash/ddash/mpdGenerator.php?fragmentlength=15&type=full
            xmlNodePtr fragmenturl_node = NULL;
            duration_val = xmlGetProp(representation_segmentlist_node, "duration");
            timescale_val = xmlGetProp(representation_segmentlist_node, "timescale");
            if (duration_val) {
                rep->fragment_duration = (int64_t) strtoll(duration_val, NULL, 10);
                xmlFree(duration_val);
            }
            if (timescale_val) {
                rep->fragment_timescale = (int64_t) strtoll(timescale_val, NULL, 10);
                xmlFree(timescale_val);
            }
            fragmenturl_node = xmlFirstElementChild(representation_segmentlist_node);
            while (fragmenturl_node) {
                ret = parse_manifest_segmenturlnode(s, rep, fragmenturl_node,
                                                    baseurl_nodes,
                                                    rep_id_val,
                                                    rep_bandwidth_val);
                if (ret < 0) {
                    return ret;
                }
                fragmenturl_node = xmlNextElementSibling(fragmenturl_node);
            }

            fragment_timeline_node = find_child_node_by_name(representation_segmenttemplate_node, "SegmentTimeline");

            if (!fragment_timeline_node)
                fragment_timeline_node = find_child_node_by_name(fragment_template_node, "SegmentTimeline");
            if (fragment_timeline_node) {
                fragment_timeline_node = xmlFirstElementChild(fragment_timeline_node);
                while (fragment_timeline_node) {
                    ret = parse_manifest_segmenttimeline(s, rep, fragment_timeline_node);
                    if (ret < 0) {
                        return ret;
                    }
                    fragment_timeline_node = xmlNextElementSibling(fragment_timeline_node);
                }
            }
        } else {
            free_representation(rep);
            rep = NULL;
            av_log(s, AV_LOG_ERROR, "Unknown format of Representation node id[%s] \n", (const char *)rep_id_val);
        }

        if (rep) {
            if (rep->fragment_duration > 0 && !rep->fragment_timescale)
                rep->fragment_timescale = 1;
            if (type == AVMEDIA_TYPE_VIDEO) {
                rep->rep_idx = video_rep_idx;
                c->cur_video = rep;
            } else {
                rep->rep_idx = audio_rep_idx;
                c->cur_audio = rep;
            }
        }
    }

    video_rep_idx += type == AVMEDIA_TYPE_VIDEO;
    audio_rep_idx += type == AVMEDIA_TYPE_AUDIO;

end:
    if (rep_id_val)
        xmlFree(rep_id_val);
    if (rep_bandwidth_val)
        xmlFree(rep_bandwidth_val);

    return ret;
}

static int parse_manifest_adaptationset(AVFormatContext *s, const char *url,
                                        xmlNodePtr adaptionset_node,
                                        xmlNodePtr mpd_baseurl_node,
                                        xmlNodePtr period_baseurl_node)
{
    int ret = 0;
    xmlNodePtr fragment_template_node = NULL;
    xmlNodePtr content_component_node = NULL;
    xmlNodePtr adaptionset_baseurl_node = NULL;
    xmlNodePtr node = NULL;

    node = xmlFirstElementChild(adaptionset_node);
    while (node) {
        if (!av_strcasecmp(node->name, (const char *)"SegmentTemplate")) {
            fragment_template_node = node;
        } else if (!av_strcasecmp(node->name, (const char *)"ContentComponent")) {
            content_component_node = node;
        } else if (!av_strcasecmp(node->name, (const char *)"BaseURL")) {
            adaptionset_baseurl_node = node;
        } else if (!av_strcasecmp(node->name, (const char *)"Representation")) {
            ret = parse_manifest_representation(s, url, node,
                                                adaptionset_node,
                                                mpd_baseurl_node,
                                                period_baseurl_node,
                                                fragment_template_node,
                                                content_component_node,
                                                adaptionset_baseurl_node);
            if (ret < 0) {
                return ret;
            }
        }
        node = xmlNextElementSibling(node);
    }
    return 0;
}

static int parse_manifest(AVFormatContext *s, const char *url, AVIOContext *in)
{
    DASHContext *c = s->priv_data;
    int ret = 0;
    int close_in = 0;
    uint8_t *new_url = NULL;
    int64_t filesize = 0;
    char *buffer = NULL;
    AVDictionary *opts = NULL;
    xmlDoc *doc = NULL;
    xmlNodePtr root_element = NULL;
    xmlNodePtr node = NULL;
    xmlNodePtr period_node = NULL;
    xmlNodePtr mpd_baseurl_node = NULL;
    xmlNodePtr period_baseurl_node = NULL;
    xmlNodePtr adaptionset_node = NULL;
    xmlAttrPtr attr = NULL;
    char *val  = NULL;
    uint32_t perdiod_duration_sec = 0;
    uint32_t perdiod_start_sec = 0;
    int32_t audio_rep_idx = 0;
    int32_t video_rep_idx = 0;

    if (!in) {
        close_in = 1;

        set_httpheader_options(c, opts);
        ret = avio_open2(&in, url, AVIO_FLAG_READ, c->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
    }

    if (av_opt_get(in, "location", AV_OPT_SEARCH_CHILDREN, &new_url) >= 0) {
        c->base_url = av_strdup(new_url);
    } else {
        c->base_url = av_strdup(url);
    }

    filesize = avio_size(in);
    if (filesize <= 0) {
        filesize = 8 * 1024;
    }

    buffer = av_mallocz(filesize);
    if (!buffer) {
        av_free(c->base_url);
        return AVERROR(ENOMEM);
    }

    filesize = avio_read(in, buffer, filesize);
    if (filesize <= 0) {
        av_log(s, AV_LOG_ERROR, "Unable to read to offset '%s'\n", url);
        ret = AVERROR_INVALIDDATA;
    } else {
        LIBXML_TEST_VERSION

        doc = xmlReadMemory(buffer, filesize, c->base_url, NULL, 0);
        root_element = xmlDocGetRootElement(doc);
        node = root_element;

        if (!node) {
            ret = AVERROR_INVALIDDATA;
            av_log(s, AV_LOG_ERROR, "Unable to parse '%s' - missing root node\n", url);
            goto cleanup;
        }

        if (node->type != XML_ELEMENT_NODE ||
            av_strcasecmp(node->name, (const char *)"MPD")) {
            ret = AVERROR_INVALIDDATA;
            av_log(s, AV_LOG_ERROR, "Unable to parse '%s' - wrong root node name[%s] type[%d]\n", url, node->name, (int)node->type);
            goto cleanup;
        }

        val = xmlGetProp(node, "type");
        if (!val) {
            av_log(s, AV_LOG_ERROR, "Unable to parse '%s' - missing type attrib\n", url);
            ret = AVERROR_INVALIDDATA;
            goto cleanup;
        }
        if (!av_strcasecmp(val, (const char *)"dynamic"))
            c->is_live = 1;
        xmlFree(val);

        attr = node->properties;
        while (attr) {
            val = xmlGetProp(node, attr->name);

            if (!av_strcasecmp(attr->name, (const char *)"availabilityStartTime")) {
                c->availability_start_time = get_utc_date_time_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"publishTime")) {
                c->publish_time = get_utc_date_time_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"minimumUpdatePeriod")) {
                c->minimum_update_period = get_duration_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"timeShiftBufferDepth")) {
                c->time_shift_buffer_depth = get_duration_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"minBufferTime")) {
                c->min_buffer_time = get_duration_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"suggestedPresentationDelay")) {
                c->suggested_presentation_delay = get_duration_insec(s, (const char *)val);
            } else if (!av_strcasecmp(attr->name, (const char *)"mediaPresentationDuration")) {
                c->media_presentation_duration = get_duration_insec(s, (const char *)val);
            }
            attr = attr->next;
            xmlFree(val);
        }

        mpd_baseurl_node = find_child_node_by_name(node, "BaseURL");

        // at now we can handle only one period, with the longest duration
        node = xmlFirstElementChild(node);
        while (node) {
            if (!av_strcasecmp(node->name, (const char *)"Period")) {
                perdiod_duration_sec = 0;
                perdiod_start_sec = 0;
                attr = node->properties;
                while (attr) {
                    val = xmlGetProp(node, attr->name);
                    if (!av_strcasecmp(attr->name, (const char *)"duration")) {
                        perdiod_duration_sec = get_duration_insec(s, (const char *)val);
                    } else if (!av_strcasecmp(attr->name, (const char *)"start")) {
                        perdiod_start_sec = get_duration_insec(s, (const char *)val);
                    }
                    attr = attr->next;
                    xmlFree(val);
                }
                if ((perdiod_duration_sec) >= (c->period_duration)) {
                    period_node = node;
                    c->period_duration = perdiod_duration_sec;
                    c->period_start = perdiod_start_sec;
                    if (c->period_start > 0)
                        c->media_presentation_duration = c->period_duration;
                }
            }
            node = xmlNextElementSibling(node);
        }
        if (!period_node) {
            av_log(s, AV_LOG_ERROR, "Unable to parse '%s' - missing Period node\n", url);
            ret = AVERROR_INVALIDDATA;
            goto cleanup;
        }

        adaptionset_node = xmlFirstElementChild(period_node);
        while (adaptionset_node) {
            if (!av_strcasecmp(adaptionset_node->name, (const char *)"BaseURL")) {
                period_baseurl_node = adaptionset_node;
            } else if (!av_strcasecmp(adaptionset_node->name, (const char *)"AdaptationSet")) {
                parse_manifest_adaptationset(s, url, adaptionset_node, mpd_baseurl_node, period_baseurl_node);
            }
            adaptionset_node = xmlNextElementSibling(adaptionset_node);
        }
        if (c->cur_video) {
            c->cur_video->rep_count = video_rep_idx;
            av_log(s, AV_LOG_VERBOSE, "rep_idx[%d]\n", (int)c->cur_video->rep_idx);
            av_log(s, AV_LOG_VERBOSE, "rep_count[%d]\n", (int)video_rep_idx);
        }
        if (c->cur_audio) {
            c->cur_audio->rep_count = audio_rep_idx;
        }
cleanup:
        /*free the document */
        xmlFreeDoc(doc);
        xmlCleanupParser();
    }

    av_free(new_url);
    av_free(buffer);
    if (close_in) {
        avio_close(in);
    }
    return ret;
}

static int64_t calc_cur_seg_no(AVFormatContext *s, struct representation *pls)
{
    DASHContext *c = s->priv_data;
    int64_t num = 0;
    int64_t start_time_offset = 0;

    if (c->is_live) {
        if (pls->n_fragments) {
            num = pls->first_seq_no;
        } else if (pls->n_timelines) {
            start_time_offset = get_segment_start_time_based_on_timeline(pls, 0xFFFFFFFF) - pls->timelines[pls->first_seq_no]->starttime; // total duration of playlist
            if (start_time_offset < 60 * pls->fragment_timescale)
                start_time_offset = 0;
            else
                start_time_offset = start_time_offset - 60 * pls->fragment_timescale;

            num = calc_next_seg_no_from_timelines(pls, pls->timelines[pls->first_seq_no]->starttime + start_time_offset);
            if (num == -1)
                num = pls->first_seq_no;
        } else if (pls->fragment_duration){
            if (pls->presentation_timeoffset) {
                num = pls->presentation_timeoffset * pls->fragment_timescale / pls->fragment_duration;
            } else if (c->publish_time > 0 && !c->availability_start_time) {
                num = pls->first_seq_no + (((c->publish_time - c->availability_start_time) - c->suggested_presentation_delay) * pls->fragment_timescale) / pls->fragment_duration;
            } else {
                num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time) - c->suggested_presentation_delay) * pls->fragment_timescale) / pls->fragment_duration;
            }
        }
    } else {
        num = pls->first_seq_no;
    }
    return num;
}

static int64_t calc_min_seg_no(AVFormatContext *s, struct representation *pls)
{
    DASHContext *c = s->priv_data;
    int64_t num = 0;

    if (c->is_live && pls->fragment_duration) {
        num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time) - c->time_shift_buffer_depth) * pls->fragment_timescale) / pls->fragment_duration;
    } else {
        num = pls->first_seq_no;
    }
    return num;
}

static int64_t calc_max_seg_no(struct representation *pls)
{
    DASHContext *c = pls->parent->priv_data;
    int64_t num = 0;

    if (pls->n_fragments) {
        num = pls->first_seq_no + pls->n_fragments - 1;
    } else if (pls->n_timelines) {
        int i = 0;
        num = pls->first_seq_no + pls->n_timelines - 1;
        for (i = 0; i < pls->n_timelines; i++) {
            num += pls->timelines[i]->repeat;
        }
    } else if (c->is_live && pls->fragment_duration) {
        num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time)) * pls->fragment_timescale)  / pls->fragment_duration;
    } else if (pls->fragment_duration) {
        num = pls->first_seq_no + (c->media_presentation_duration * pls->fragment_timescale) / pls->fragment_duration;
    }

    return num;
}

static void move_timelines(struct representation *rep_src, struct representation *rep_dest)
{
    if (rep_dest && rep_src ) {
        free_timelines_list(rep_dest);
        rep_dest->timelines    = rep_src->timelines;
        rep_dest->n_timelines  = rep_src->n_timelines;
        rep_dest->first_seq_no = rep_src->first_seq_no;
        rep_dest->last_seq_no = calc_max_seg_no(rep_dest);
        rep_src->timelines = NULL;
        rep_src->n_timelines = 0;
        rep_dest->cur_seq_no = rep_src->cur_seq_no;
    }
}

static void move_segments(struct representation *rep_src, struct representation *rep_dest)
{
    if (rep_dest && rep_src ) {
        free_fragment_list(rep_dest);
        if (rep_src->start_number > (rep_dest->start_number + rep_dest->n_fragments))
            rep_dest->cur_seq_no = 0;
        else
            rep_dest->cur_seq_no += rep_src->start_number - rep_dest->start_number;
        rep_dest->fragments    = rep_src->fragments;
        rep_dest->n_fragments  = rep_src->n_fragments;
        rep_dest->parent  = rep_src->parent;
        rep_dest->last_seq_no = calc_max_seg_no(rep_dest);
        rep_src->fragments = NULL;
        rep_src->n_fragments = 0;
    }
}


static int refresh_manifest(AVFormatContext *s)
{

    int ret = 0;
    DASHContext *c = s->priv_data;

    // save current context
    struct representation *cur_video =  c->cur_video;
    struct representation *cur_audio =  c->cur_audio;
    char *base_url = c->base_url;

    c->base_url = NULL;
    c->cur_video = NULL;
    c->cur_audio = NULL;
    ret = parse_manifest(s, s->filename, NULL);
    if (ret)
        goto finish;

    if (cur_video && cur_video->timelines || cur_audio && cur_audio->timelines) {
        // calc current time
        int64_t currentVideoTime = 0;
        int64_t currentAudioTime = 0;
        if (cur_video && cur_video->timelines)
            currentVideoTime = get_segment_start_time_based_on_timeline(cur_video, cur_video->cur_seq_no) / cur_video->fragment_timescale;
        if (cur_audio && cur_audio->timelines)
            currentAudioTime = get_segment_start_time_based_on_timeline(cur_audio, cur_audio->cur_seq_no) / cur_audio->fragment_timescale;
        // update segments
        if (cur_video && cur_video->timelines) {
            c->cur_video->cur_seq_no = calc_next_seg_no_from_timelines(c->cur_video, currentVideoTime * cur_video->fragment_timescale - 1);
            if (c->cur_video->cur_seq_no >= 0) {
                move_timelines(c->cur_video, cur_video);
            }
        }
        if (cur_audio && cur_audio->timelines) {
            c->cur_audio->cur_seq_no = calc_next_seg_no_from_timelines(c->cur_audio, currentAudioTime * cur_audio->fragment_timescale - 1);
            if (c->cur_audio->cur_seq_no >= 0) {
               move_timelines(c->cur_audio, cur_audio);
            }
        }
    }
    if (cur_video && cur_video->fragments) {
        move_segments(c->cur_video, cur_video);
    }
    if (cur_audio && cur_audio->fragments) {
        move_segments(c->cur_audio, cur_audio);
    }

finish:
    // restore context
    if (c->base_url)
        av_free(base_url);
    else
        c->base_url  = base_url;
    if (c->cur_audio)
        free_representation(c->cur_audio);
    if (c->cur_video)
        free_representation(c->cur_video);
    c->cur_audio = cur_audio;
    c->cur_video = cur_video;
    return ret;
}

static struct fragment *get_current_fragment(struct representation *pls)
{
    int64_t min_seq_no = 0;
    int64_t max_seq_no = 0;
    struct fragment *seg = NULL;
    struct fragment *seg_ptr = NULL;
    DASHContext *c = pls->parent->priv_data;

    while (( !ff_check_interrupt(c->interrupt_callback)&& pls->n_fragments > 0)) {
        if (pls->cur_seq_no < pls->n_fragments) {
            seg_ptr = pls->fragments[pls->cur_seq_no];
            seg = av_mallocz(sizeof(struct fragment));
            if (!seg) {
                return NULL;
            }
            seg->url = av_strdup(seg_ptr->url);
            if (!seg->url) {
                av_free(seg);
                return NULL;
            }
            seg->size = seg_ptr->size;
            seg->url_offset = seg_ptr->url_offset;
            return seg;
        } else if (c->is_live) {
            refresh_manifest(pls->parent);
        } else {
            break;
        }
    }
    if (c->is_live) {
        min_seq_no = calc_min_seg_no(pls->parent, pls);
        max_seq_no = calc_max_seg_no(pls);

        if (pls->timelines || pls->fragments) {
            refresh_manifest(pls->parent);
        }
        if (pls->cur_seq_no <= min_seq_no) {
            av_log(pls->parent, AV_LOG_VERBOSE, "old fragment: cur[%"PRId64"] min[%"PRId64"] max[%"PRId64"], playlist %d\n", (int64_t)pls->cur_seq_no, min_seq_no, max_seq_no, (int)pls->rep_idx);
            pls->cur_seq_no = calc_cur_seg_no(pls->parent, pls);
        } else if (pls->cur_seq_no > max_seq_no) {
            av_log(pls->parent, AV_LOG_VERBOSE, "new fragment: min[%"PRId64"] max[%"PRId64"], playlist %d\n", min_seq_no, max_seq_no, (int)pls->rep_idx);
        }
        seg = av_mallocz(sizeof(struct fragment));
        if (!seg) {
            return NULL;
        }
    } else if (pls->cur_seq_no <= pls->last_seq_no) {
        seg = av_mallocz(sizeof(struct fragment));
        if (!seg) {
            return NULL;
        }
    }
    if (seg) {
        char tmpfilename[MAX_URL_SIZE];

        ff_dash_fill_tmpl_params(tmpfilename, sizeof(tmpfilename), pls->url_template, 0, pls->cur_seq_no, 0, get_segment_start_time_based_on_timeline(pls, pls->cur_seq_no));
        seg->url = av_strireplace(pls->url_template, pls->url_template, tmpfilename);
        if (!seg->url) {
            av_log(pls->parent, AV_LOG_WARNING, "Unable to resolve template url '%s', try to use origin template\n", pls->url_template);
            seg->url = av_strdup(pls->url_template);
            if (!seg->url) {
                av_log(pls->parent, AV_LOG_ERROR, "Cannot resolve template url '%s'\n", pls->url_template);
                return NULL;
            }
        }

        seg->size = -1;
    }

    return seg;
}

enum ReadFromURLMode {
    READ_NORMAL,
    READ_COMPLETE,
};

static int read_from_url(struct representation *pls, struct fragment *seg,
                         uint8_t *buf, int buf_size,
                         enum ReadFromURLMode mode)
{
    int ret;

    /* limit read if the fragment was only a part of a file */
    if (seg->size >= 0)
        buf_size = FFMIN(buf_size, pls->cur_seg_size - pls->cur_seg_offset);

    if (mode == READ_COMPLETE) {
        ret = avio_read(pls->input, buf, buf_size);
        if (ret < buf_size) {
            av_log(pls->parent, AV_LOG_WARNING, "Could not read complete fragment.\n");
        }
    } else {
        ret = avio_read(pls->input, buf, buf_size);
    }
    if (ret > 0)
        pls->cur_seg_offset += ret;

    return ret;
}

static int open_input(DASHContext *c, struct representation *pls, struct fragment *seg)
{
    AVDictionary *opts = NULL;
    char url[MAX_URL_SIZE];
    int ret;

    set_httpheader_options(c, opts);
    if (seg->size >= 0) {
        /* try to restrict the HTTP request to the part we want
         * (if this is in fact a HTTP request) */
        av_dict_set_int(&opts, "offset", seg->url_offset, 0);
        av_dict_set_int(&opts, "end_offset", seg->url_offset + seg->size, 0);
    }

    ff_make_absolute_url(url, MAX_URL_SIZE, c->base_url, seg->url);
    av_log(pls->parent, AV_LOG_VERBOSE, "DASH request for url '%s', offset %"PRId64", playlist %d\n",
           url, seg->url_offset, pls->rep_idx);
    ret = open_url(pls->parent, &pls->input, url, c->avio_opts, opts, NULL);
    if (ret < 0) {
        goto cleanup;
    }

    /* Seek to the requested position. If this was a HTTP request, the offset
     * should already be where want it to, but this allows e.g. local testing
     * without a HTTP server. */
    if (!ret && seg->url_offset) {
        int64_t seekret = avio_seek(pls->input, seg->url_offset, SEEK_SET);
        if (seekret < 0) {
            av_log(pls->parent, AV_LOG_ERROR, "Unable to seek to offset %"PRId64" of DASH fragment '%s'\n", seg->url_offset, seg->url);
            ret = (int) seekret;
            ff_format_io_close(pls->parent, &pls->input);
        }
    }

cleanup:
    av_dict_free(&opts);
    pls->cur_seg_offset = 0;
    pls->cur_seg_size = seg->size;
    return ret;
}

static int update_init_section(struct representation *pls)
{
    static const int max_init_section_size = 1024 * 1024;
    DASHContext *c = pls->parent->priv_data;
    int64_t sec_size;
    int64_t urlsize;
    int ret;

    if (!pls->init_section || pls->init_sec_buf)
        return 0;

    ret = open_input(c, pls, pls->init_section);
    if (ret < 0) {
        av_log(pls->parent, AV_LOG_WARNING,
               "Failed to open an initialization section in playlist %d\n",
               pls->rep_idx);
        return ret;
    }

    if (pls->init_section->size >= 0)
        sec_size = pls->init_section->size;
    else if ((urlsize = avio_size(pls->input)) >= 0)
        sec_size = urlsize;
    else
        sec_size = max_init_section_size;

    av_log(pls->parent, AV_LOG_DEBUG,
           "Downloading an initialization section of size %"PRId64"\n",
           sec_size);

    sec_size = FFMIN(sec_size, max_init_section_size);

    av_fast_malloc(&pls->init_sec_buf, &pls->init_sec_buf_size, sec_size);

    ret = read_from_url(pls, pls->init_section, pls->init_sec_buf,
                        pls->init_sec_buf_size, READ_COMPLETE);
    ff_format_io_close(pls->parent, &pls->input);

    if (ret < 0)
        return ret;

    pls->init_sec_data_len = ret;
    pls->init_sec_buf_read_offset = 0;

    return 0;
}

static int64_t seek_data(void *opaque, int64_t offset, int whence)
{
    struct representation *v = opaque;
    if (v->n_fragments && !v->init_sec_data_len) {
        return avio_seek(v->input, offset, whence);
    }

    return AVERROR(ENOSYS);
}

static int read_data(void *opaque, uint8_t *buf, int buf_size)
{
    int ret = 0;
    struct representation *v = opaque;
    DASHContext *c = v->parent->priv_data;

restart:
    if (!v->input) {
        free_fragment(&v->cur_seg);
        v->cur_seg = get_current_fragment(v);
        if (!v->cur_seg) {
            ret = AVERROR_EOF;
            goto end;
        }

        /* load/update Media Initialization Section, if any */
        ret = update_init_section(v);
        if (ret)
            goto end;

        ret = open_input(c, v, v->cur_seg);
        if (ret < 0) {
            if (ff_check_interrupt(c->interrupt_callback)) {
                goto end;
                ret = AVERROR_EXIT;
            }
            av_log(v->parent, AV_LOG_WARNING, "Failed to open fragment of playlist %d\n", v->rep_idx);
            v->cur_seq_no++;
            goto restart;
        }
    }

    if (v->init_sec_buf_read_offset < v->init_sec_data_len) {
        /* Push init section out first before first actual fragment */
        int copy_size = FFMIN(v->init_sec_data_len - v->init_sec_buf_read_offset, buf_size);
        memcpy(buf, v->init_sec_buf, copy_size);
        v->init_sec_buf_read_offset += copy_size;
        ret = copy_size;
        goto end;
    }

    /* check the v->cur_seg, if it is null, get current and double check if the new v->cur_seg*/
    if (!v->cur_seg) {
        v->cur_seg = get_current_fragment(v);
    }
    if (!v->cur_seg) {
        ret = AVERROR_EOF;
        goto end;
    }
    ret = read_from_url(v, v->cur_seg, buf, buf_size, READ_NORMAL);
    if (ret > 0)
        goto end;

    if (!v->is_restart_needed)
        v->cur_seq_no++;
    v->is_restart_needed = 1;

end:
    return ret;
}

static int save_avio_options(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    const char *opts[] = { "headers", "user_agent", "user-agent", "cookies", NULL }, **opt = opts;
    uint8_t *buf = NULL;
    int ret = 0;

    while (*opt) {
        if (av_opt_get(s->pb, *opt, AV_OPT_SEARCH_CHILDREN, &buf) >= 0) {
            if (buf[0] != '\0') {
                ret = av_dict_set(&c->avio_opts, *opt, buf, AV_DICT_DONT_STRDUP_VAL);
                if (ret < 0)
                    return ret;
            }
        }
        opt++;
    }

    return ret;
}

static int nested_io_open(AVFormatContext *s, AVIOContext **pb, const char *url,
                          int flags, AVDictionary **opts)
{
    av_log(s, AV_LOG_ERROR,
           "A DASH playlist item '%s' referred to an external file '%s'. "
           "Opening this file was forbidden for security reasons\n",
           s->filename, url);
    return AVERROR(EPERM);
}

static int reopen_demux_for_component(AVFormatContext *s, struct representation *pls)
{
    DASHContext *c = s->priv_data;
    AVInputFormat *in_fmt = NULL;
    AVDictionary  *in_fmt_opts = NULL;
    uint8_t *avio_ctx_buffer  = NULL;
    int ret = 0;

    if (pls->ctx) {
        /* note: the internal buffer could have changed, and be != avio_ctx_buffer */
        av_freep(&pls->pb.buffer);
        memset(&pls->pb, 0x00, sizeof(AVIOContext));
        pls->ctx->pb = NULL;
        avformat_close_input(&pls->ctx);
        pls->ctx = NULL;
    }
    if (!(pls->ctx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    avio_ctx_buffer  = av_malloc(INITIAL_BUFFER_SIZE);
    if (!avio_ctx_buffer ) {
        ret = AVERROR(ENOMEM);
        avformat_free_context(pls->ctx);
        pls->ctx = NULL;
        goto fail;
    }
    if (c->is_live) {
        ffio_init_context(&pls->pb, avio_ctx_buffer , INITIAL_BUFFER_SIZE, 0, pls, read_data, NULL, NULL);
    } else {
        ffio_init_context(&pls->pb, avio_ctx_buffer , INITIAL_BUFFER_SIZE, 0, pls, read_data, NULL, seek_data);
    }
    pls->pb.seekable = 0;

    if ((ret = ff_copy_whiteblacklists(pls->ctx, s)) < 0)
        goto fail;

    pls->ctx->flags = AVFMT_FLAG_CUSTOM_IO;
    pls->ctx->probesize = 1024 * 4;
    pls->ctx->max_analyze_duration = 4 * AV_TIME_BASE;
    ret = av_probe_input_buffer(&pls->pb, &in_fmt, "", NULL, 0, 0);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Error when loading first fragment, playlist %d\n", (int)pls->rep_idx);
        avformat_free_context(pls->ctx);
        pls->ctx = NULL;
        goto fail;
    }

    pls->ctx->pb = &pls->pb;
    pls->ctx->io_open  = nested_io_open;

    // provide additional information from mpd if available
    ret = avformat_open_input(&pls->ctx, "", in_fmt, &in_fmt_opts); //pls->init_section->url
    av_dict_free(&in_fmt_opts);
    if (ret < 0)
        goto fail;
    if (pls->n_fragments) {
        ret = avformat_find_stream_info(pls->ctx, NULL);
        if (ret < 0)
            goto fail;
    }

fail:
    return ret;
}

static int open_demux_for_component(AVFormatContext *s, struct representation *pls)
{
    int ret = 0;
    int i;

    pls->parent = s;
    pls->cur_seq_no  = calc_cur_seg_no(s, pls);
    pls->last_seq_no = calc_max_seg_no(pls);

    ret = reopen_demux_for_component(s, pls);
    if (ret < 0) {
        goto fail;
    }
    for (i = 0; i < pls->ctx->nb_streams; i++) {
        AVStream *st = avformat_new_stream(s, NULL);
        AVStream *ist = pls->ctx->streams[i];
        if (!st) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
        st->id = i;
        avcodec_parameters_copy(st->codecpar, pls->ctx->streams[i]->codecpar);
        avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);
    }

    return 0;
fail:
    return ret;
}

static int dash_read_header(AVFormatContext *s)
{
    void *u = (s->flags & AVFMT_FLAG_CUSTOM_IO) ? NULL : s->pb;
    DASHContext *c = s->priv_data;
    int ret = 0;
    int stream_index = 0;

    c->interrupt_callback = &s->interrupt_callback;
    // if the URL context is good, read important options we must broker later
    if (u) {
        update_options(&c->user_agent, "user-agent", u);
        update_options(&c->cookies, "cookies", u);
        update_options(&c->headers, "headers", u);
    }

    if ((ret = parse_manifest(s, s->filename, s->pb)) < 0)
        goto fail;

    if ((ret = save_avio_options(s)) < 0)
        goto fail;

    /* If this isn't a live stream, fill the total duration of the
     * stream. */
    if (!c->is_live) {
        s->duration = (int64_t) c->media_presentation_duration * AV_TIME_BASE;
    }

    /* Open the demuxer for curent video and current audio components if available */
    if (!ret && c->cur_video) {
        ret = open_demux_for_component(s, c->cur_video);
        if (!ret) {
            c->cur_video->stream_index = stream_index;
            ++stream_index;
        } else {
            free_representation(c->cur_video);
            c->cur_video = NULL;
        }
    }

    if (!ret && c->cur_audio) {
        ret = open_demux_for_component(s, c->cur_audio);
        if (!ret) {
            c->cur_audio->stream_index = stream_index;
            ++stream_index;
        } else {
            free_representation(c->cur_audio);
            c->cur_audio = NULL;
        }
    }

    if (!stream_index) {
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    /* Create a program */
    if (!ret) {
        AVProgram *program;
        program = av_new_program(s, 0);
        if (!program) {
            goto fail;
        }

        if (c->cur_video) {
            av_program_add_stream_index(s, 0, c->cur_video->stream_index);
        }
        if (c->cur_audio) {
            av_program_add_stream_index(s, 0, c->cur_audio->stream_index);
        }
    }

    return 0;
fail:
    return ret;
}

static int dash_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DASHContext *c = s->priv_data;
    int ret = 0;
    struct representation *cur = NULL;

    if (!c->cur_audio && !c->cur_video ) {
        return AVERROR_INVALIDDATA;
    }
    if (c->cur_audio && !c->cur_video) {
        cur = c->cur_audio;
    } else if (!c->cur_audio && c->cur_video) {
        cur = c->cur_video;
    } else if (c->cur_video->cur_timestamp < c->cur_audio->cur_timestamp) {
        cur = c->cur_video;
    } else {
        cur = c->cur_audio;
    }

    if (cur->ctx) {
        while (!ff_check_interrupt(c->interrupt_callback) && !ret) {
            ret = av_read_frame(cur->ctx, pkt);
            if (ret >= 0) {
                /* If we got a packet, return it */
                cur->cur_timestamp = av_rescale(pkt->pts, (int64_t)cur->ctx->streams[0]->time_base.num * 90000, cur->ctx->streams[0]->time_base.den);
                pkt->stream_index = cur->stream_index;
                return 0;
            }
            if (cur->is_restart_needed) {
                cur->cur_seg_offset = 0;
                cur->init_sec_buf_read_offset = 0;
                if (cur->input)
                    ff_format_io_close(cur->parent, &cur->input);
                ret = reopen_demux_for_component(s, cur);
                cur->is_restart_needed = 0;
            }

        }
    }
    return AVERROR_EOF;
}

static int dash_close(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    if (c->cur_audio) {
        free_representation(c->cur_audio);
    }
    if (c->cur_video) {
        free_representation(c->cur_video);
    }

    av_freep(&c->cookies);
    av_freep(&c->user_agent);
    av_dict_free(&c->avio_opts);
    av_freep(&c->base_url);
    return 0;
}

static int dash_seek(AVFormatContext *s, struct representation *pls, int64_t seek_pos_msec, int flags)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    int64_t duration = 0;

    av_log(pls->parent, AV_LOG_VERBOSE, "DASH seek pos[%"PRId64"ms], playlist %d\n", seek_pos_msec, pls->rep_idx);

    // single fragment mode
    if (pls->n_fragments == 1) {
        pls->cur_timestamp = 0;
        pls->cur_seg_offset = 0;
        ff_read_frame_flush(pls->ctx);
        return av_seek_frame(pls->ctx, -1, seek_pos_msec * 1000, flags);
    }

    if (pls->input)
        ff_format_io_close(pls->parent, &pls->input);

    // find the nearest fragment
    if (pls->n_timelines > 0 && pls->fragment_timescale > 0) {
        int64_t num = pls->first_seq_no;
        av_log(pls->parent, AV_LOG_VERBOSE, "dash_seek with SegmentTimeline start n_timelines[%d] "
               "last_seq_no[%"PRId64"], playlist %d.\n",
               (int)pls->n_timelines, (int64_t)pls->last_seq_no, (int)pls->rep_idx);
        for (i = 0; i < pls->n_timelines; i++) {
            if (pls->timelines[i]->starttime > 0) {
                duration = pls->timelines[i]->starttime;
            }
            duration += pls->timelines[i]->duration;
            if (seek_pos_msec < ((duration * 1000) /  pls->fragment_timescale)) {
                goto set_seq_num;
            }
            for (j = 0; j < pls->timelines[i]->repeat; j++) {
                duration += pls->timelines[i]->duration;
                num++;
                if (seek_pos_msec < ((duration * 1000) /  pls->fragment_timescale)) {
                    goto set_seq_num;
                }
            }
            num++;
        }

set_seq_num:
        pls->cur_seq_no = num > pls->last_seq_no ? pls->last_seq_no : num;
        av_log(pls->parent, AV_LOG_VERBOSE, "dash_seek with SegmentTimeline end cur_seq_no[%"PRId64"], playlist %d.\n",
               (int64_t)pls->cur_seq_no, (int)pls->rep_idx);
    } else if (pls->fragment_duration > 0) {
        pls->cur_seq_no = pls->first_seq_no + ((seek_pos_msec * pls->fragment_timescale) / pls->fragment_duration) / 1000;
    } else {
        av_log(pls->parent, AV_LOG_ERROR, "dash_seek missing fragment_duration\n");
        pls->cur_seq_no = pls->first_seq_no;
    }
    pls->cur_timestamp = 0;
    pls->cur_seg_offset = 0;
    pls->init_sec_buf_read_offset = 0;
    ret = reopen_demux_for_component(s, pls);

    return ret;
}

static int dash_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    int ret = 0;
    DASHContext *c = s->priv_data;
    int64_t seek_pos_msec = av_rescale_rnd(timestamp, 1000,
                                           s->streams[stream_index]->time_base.den,
                                           flags & AVSEEK_FLAG_BACKWARD ?
                                           AV_ROUND_DOWN : AV_ROUND_UP);
    if ((flags & AVSEEK_FLAG_BYTE) || c->is_live)
        return AVERROR(ENOSYS);
    if (c->cur_audio) {
        ret = dash_seek(s, c->cur_audio, seek_pos_msec, flags);
    }
    if (!ret && c->cur_video) {
        ret = dash_seek(s, c->cur_video, seek_pos_msec, flags);
    }
    return ret;
}

static int dash_probe(AVProbeData *p)
{
    if (!av_stristr(p->buf, "<MPD"))
        return 0;

    if (av_stristr(p->buf, "dash:profile:isoff-on-demand:2011") ||
        av_stristr(p->buf, "dash:profile:isoff-live:2011") ||
        av_stristr(p->buf, "dash:profile:isoff-live:2012") ||
        av_stristr(p->buf, "dash:profile:isoff-main:2011")) {
        return AVPROBE_SCORE_MAX;
    }
    if (av_stristr(p->buf, "dash:profile")) {
        return AVPROBE_SCORE_MAX;
    }

    return 0;
}

#define OFFSET(x) offsetof(DASHContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM
static const AVOption dash_options[] = {
    {"allowed_extensions", "List of file extensions that dash is allowed to access",
        OFFSET(allowed_extensions), AV_OPT_TYPE_STRING,
        {.str = "aac,m4a,m4s,m4v,mov,mp4"},
        INT_MIN, INT_MAX, FLAGS},
    {NULL}
};

static const AVClass dash_class = {
    .class_name = "dash",
    .item_name  = av_default_item_name,
    .option     = dash_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_dash_demuxer = {
    .name           = "dash",
    .long_name      = NULL_IF_CONFIG_SMALL("Dynamic Adaptive Streaming over HTTP"),
    .priv_class     = &dash_class,
    .priv_data_size = sizeof(DASHContext),
    .read_probe     = dash_probe,
    .read_header    = dash_read_header,
    .read_packet    = dash_read_packet,
    .read_close     = dash_close,
    .read_seek      = dash_read_seek,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
