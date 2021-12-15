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
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavutil/parseutils.h"
#include "internal.h"
#include "avio_internal.h"
#include "dash.h"

#define INITIAL_BUFFER_SIZE 32768
#define MAX_BPRINT_READ_SIZE (UINT_MAX - 1)
#define DEFAULT_MANIFEST_SIZE 8 * 1024

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
    FFIOContext pb;
    AVIOContext *input;
    AVFormatContext *parent;
    AVFormatContext *ctx;
    int stream_index;

    char *id;
    char *lang;
    int bandwidth;
    AVRational framerate;
    AVStream *assoc_stream; /* demuxer stream associated with this representation */

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

    int n_videos;
    struct representation **videos;
    int n_audios;
    struct representation **audios;
    int n_subtitles;
    struct representation **subtitles;

    /* MediaPresentationDescription Attribute */
    uint64_t media_presentation_duration;
    uint64_t suggested_presentation_delay;
    uint64_t availability_start_time;
    uint64_t availability_end_time;
    uint64_t publish_time;
    uint64_t minimum_update_period;
    uint64_t time_shift_buffer_depth;
    uint64_t min_buffer_time;

    /* Period Attribute */
    uint64_t period_duration;
    uint64_t period_start;

    /* AdaptationSet Attribute */
    char *adaptionset_lang;

    int is_live;
    AVIOInterruptCB *interrupt_callback;
    char *allowed_extensions;
    AVDictionary *avio_opts;
    int max_url_size;

    /* Flags for init section*/
    int is_init_section_common_video;
    int is_init_section_common_audio;
    int is_init_section_common_subtitle;

} DASHContext;

static int ishttp(char *url)
{
    const char *proto_name = avio_find_protocol_name(url);
    return proto_name && av_strstart(proto_name, "http", NULL);
}

static int aligned(int val)
{
    return ((val + 0x3F) >> 6) << 6;
}

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

            if (pls->timelines[i]->repeat == -1) {
                start_time = pls->timelines[i]->duration * cur_seq_no;
                goto finish;
            }

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
    av_freep(&pls->pb.pub.buffer);
    ff_format_io_close(pls->parent, &pls->input);
    if (pls->ctx) {
        pls->ctx->pb = NULL;
        avformat_close_input(&pls->ctx);
    }

    av_freep(&pls->url_template);
    av_freep(&pls->lang);
    av_freep(&pls->id);
    av_freep(&pls);
}

static void free_video_list(DASHContext *c)
{
    int i;
    for (i = 0; i < c->n_videos; i++) {
        struct representation *pls = c->videos[i];
        free_representation(pls);
    }
    av_freep(&c->videos);
    c->n_videos = 0;
}

static void free_audio_list(DASHContext *c)
{
    int i;
    for (i = 0; i < c->n_audios; i++) {
        struct representation *pls = c->audios[i];
        free_representation(pls);
    }
    av_freep(&c->audios);
    c->n_audios = 0;
}

static void free_subtitle_list(DASHContext *c)
{
    int i;
    for (i = 0; i < c->n_subtitles; i++) {
        struct representation *pls = c->subtitles[i];
        free_representation(pls);
    }
    av_freep(&c->subtitles);
    c->n_subtitles = 0;
}

static int open_url(AVFormatContext *s, AVIOContext **pb, const char *url,
                    AVDictionary **opts, AVDictionary *opts2, int *is_http)
{
    DASHContext *c = s->priv_data;
    AVDictionary *tmp = NULL;
    const char *proto_name = NULL;
    int ret;

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

    av_freep(pb);
    av_dict_copy(&tmp, *opts, 0);
    av_dict_copy(&tmp, opts2, 0);
    ret = avio_open2(pb, url, AVIO_FLAG_READ, c->interrupt_callback, &tmp);
    if (ret >= 0) {
        // update cookies on http response with setcookies.
        char *new_cookies = NULL;

        if (!(s->flags & AVFMT_FLAG_CUSTOM_IO))
            av_opt_get(*pb, "cookies", AV_OPT_SEARCH_CHILDREN, (uint8_t**)&new_cookies);

        if (new_cookies) {
            av_dict_set(opts, "cookies", new_cookies, AV_DICT_DONT_STRDUP_VAL);
        }

    }

    av_dict_free(&tmp);

    if (is_http)
        *is_http = av_strstart(proto_name, "http", NULL);

    return ret;
}

static char *get_content_url(xmlNodePtr *baseurl_nodes,
                             int n_baseurl_nodes,
                             int max_url_size,
                             char *rep_id_val,
                             char *rep_bandwidth_val,
                             char *val)
{
    int i;
    char *text;
    char *url = NULL;
    char *tmp_str = av_mallocz(max_url_size);

    if (!tmp_str)
        return NULL;

    for (i = 0; i < n_baseurl_nodes; ++i) {
        if (baseurl_nodes[i] &&
            baseurl_nodes[i]->children &&
            baseurl_nodes[i]->children->type == XML_TEXT_NODE) {
            text = xmlNodeGetContent(baseurl_nodes[i]->children);
            if (text) {
                memset(tmp_str, 0, max_url_size);
                ff_make_absolute_url(tmp_str, max_url_size, "", text);
                xmlFree(text);
            }
        }
    }

    if (val)
        ff_make_absolute_url(tmp_str, max_url_size, tmp_str, val);

    if (rep_id_val) {
        url = av_strireplace(tmp_str, "$RepresentationID$", rep_id_val);
        if (!url) {
            goto end;
        }
        av_strlcpy(tmp_str, url, max_url_size);
    }
    if (rep_bandwidth_val && tmp_str[0] != '\0') {
        // free any previously assigned url before reassigning
        av_free(url);
        url = av_strireplace(tmp_str, "$Bandwidth$", rep_bandwidth_val);
        if (!url) {
            goto end;
        }
    }
end:
    av_free(tmp_str);
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
                if (av_stristr(val, "video")) {
                    type = AVMEDIA_TYPE_VIDEO;
                } else if (av_stristr(val, "audio")) {
                    type = AVMEDIA_TYPE_AUDIO;
                } else if (av_stristr(val, "text")) {
                    type = AVMEDIA_TYPE_SUBTITLE;
                }
                xmlFree(val);
            }
        }
    }
    return type;
}

static struct fragment * get_Fragment(char *range)
{
    struct fragment * seg =  av_mallocz(sizeof(struct fragment));

    if (!seg)
        return NULL;

    seg->size = -1;
    if (range) {
        char *str_end_offset;
        char *str_offset = av_strtok(range, "-", &str_end_offset);
        seg->url_offset = strtoll(str_offset, NULL, 10);
        seg->size = strtoll(str_end_offset, NULL, 10) - seg->url_offset + 1;
    }

    return seg;
}

static int parse_manifest_segmenturlnode(AVFormatContext *s, struct representation *rep,
                                         xmlNodePtr fragmenturl_node,
                                         xmlNodePtr *baseurl_nodes,
                                         char *rep_id_val,
                                         char *rep_bandwidth_val)
{
    DASHContext *c = s->priv_data;
    char *initialization_val = NULL;
    char *media_val = NULL;
    char *range_val = NULL;
    int max_url_size = c ? c->max_url_size: MAX_URL_SIZE;
    int err;

    if (!av_strcasecmp(fragmenturl_node->name, "Initialization")) {
        initialization_val = xmlGetProp(fragmenturl_node, "sourceURL");
        range_val = xmlGetProp(fragmenturl_node, "range");
        if (initialization_val || range_val) {
            free_fragment(&rep->init_section);
            rep->init_section = get_Fragment(range_val);
            xmlFree(range_val);
            if (!rep->init_section) {
                xmlFree(initialization_val);
                return AVERROR(ENOMEM);
            }
            rep->init_section->url = get_content_url(baseurl_nodes, 4,
                                                     max_url_size,
                                                     rep_id_val,
                                                     rep_bandwidth_val,
                                                     initialization_val);
            xmlFree(initialization_val);
            if (!rep->init_section->url) {
                av_freep(&rep->init_section);
                return AVERROR(ENOMEM);
            }
        }
    } else if (!av_strcasecmp(fragmenturl_node->name, "SegmentURL")) {
        media_val = xmlGetProp(fragmenturl_node, "media");
        range_val = xmlGetProp(fragmenturl_node, "mediaRange");
        if (media_val || range_val) {
            struct fragment *seg = get_Fragment(range_val);
            xmlFree(range_val);
            if (!seg) {
                xmlFree(media_val);
                return AVERROR(ENOMEM);
            }
            seg->url = get_content_url(baseurl_nodes, 4,
                                       max_url_size,
                                       rep_id_val,
                                       rep_bandwidth_val,
                                       media_val);
            xmlFree(media_val);
            if (!seg->url) {
                av_free(seg);
                return AVERROR(ENOMEM);
            }
            err = av_dynarray_add_nofree(&rep->fragments, &rep->n_fragments, seg);
            if (err < 0) {
                free_fragment(&seg);
                return err;
            }
        }
    }

    return 0;
}

static int parse_manifest_segmenttimeline(AVFormatContext *s, struct representation *rep,
                                          xmlNodePtr fragment_timeline_node)
{
    xmlAttrPtr attr = NULL;
    char *val  = NULL;
    int err;

    if (!av_strcasecmp(fragment_timeline_node->name, "S")) {
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

            if (!av_strcasecmp(attr->name, "t")) {
                tml->starttime = (int64_t)strtoll(val, NULL, 10);
            } else if (!av_strcasecmp(attr->name, "r")) {
                tml->repeat =(int64_t) strtoll(val, NULL, 10);
            } else if (!av_strcasecmp(attr->name, "d")) {
                tml->duration = (int64_t)strtoll(val, NULL, 10);
            }
            attr = attr->next;
            xmlFree(val);
        }
        err = av_dynarray_add_nofree(&rep->timelines, &rep->n_timelines, tml);
        if (err < 0) {
            av_free(tml);
            return err;
        }
    }

    return 0;
}

static int resolve_content_path(AVFormatContext *s, const char *url, int *max_url_size, xmlNodePtr *baseurl_nodes, int n_baseurl_nodes)
{
    char *tmp_str = NULL;
    char *path = NULL;
    char *mpdName = NULL;
    xmlNodePtr node = NULL;
    char *baseurl = NULL;
    char *root_url = NULL;
    char *text = NULL;
    char *tmp = NULL;
    int isRootHttp = 0;
    char token ='/';
    int start =  0;
    int rootId = 0;
    int updated = 0;
    int size = 0;
    int i;
    int tmp_max_url_size = strlen(url);

    for (i = n_baseurl_nodes-1; i >= 0 ; i--) {
        text = xmlNodeGetContent(baseurl_nodes[i]);
        if (!text)
            continue;
        tmp_max_url_size += strlen(text);
        if (ishttp(text)) {
            xmlFree(text);
            break;
        }
        xmlFree(text);
    }

    tmp_max_url_size = aligned(tmp_max_url_size);
    text = av_mallocz(tmp_max_url_size);
    if (!text) {
        updated = AVERROR(ENOMEM);
        goto end;
    }
    av_strlcpy(text, url, strlen(url)+1);
    tmp = text;
    while (mpdName = av_strtok(tmp, "/", &tmp))  {
        size = strlen(mpdName);
    }
    av_free(text);

    path = av_mallocz(tmp_max_url_size);
    tmp_str = av_mallocz(tmp_max_url_size);
    if (!tmp_str || !path) {
        updated = AVERROR(ENOMEM);
        goto end;
    }

    av_strlcpy (path, url, strlen(url) - size + 1);
    for (rootId = n_baseurl_nodes - 1; rootId > 0; rootId --) {
        if (!(node = baseurl_nodes[rootId])) {
            continue;
        }
        text = xmlNodeGetContent(node);
        if (ishttp(text)) {
            xmlFree(text);
            break;
        }
        xmlFree(text);
    }

    node = baseurl_nodes[rootId];
    baseurl = xmlNodeGetContent(node);
    root_url = (av_strcasecmp(baseurl, "")) ? baseurl : path;
    if (node) {
        xmlNodeSetContent(node, root_url);
        updated = 1;
    }

    size = strlen(root_url);
    isRootHttp = ishttp(root_url);

    if (size > 0 && root_url[size - 1] != token) {
        av_strlcat(root_url, "/", size + 2);
        size += 2;
    }

    for (i = 0; i < n_baseurl_nodes; ++i) {
        if (i == rootId) {
            continue;
        }
        text = xmlNodeGetContent(baseurl_nodes[i]);
        if (text && !av_strstart(text, "/", NULL)) {
            memset(tmp_str, 0, strlen(tmp_str));
            if (!ishttp(text) && isRootHttp) {
                av_strlcpy(tmp_str, root_url, size + 1);
            }
            start = (text[0] == token);
            if (start && av_stristr(tmp_str, text)) {
                char *p = tmp_str;
                if (!av_strncasecmp(tmp_str, "http://", 7)) {
                    p += 7;
                } else if (!av_strncasecmp(tmp_str, "https://", 8)) {
                    p += 8;
                }
                p = strchr(p, '/');
                memset(p + 1, 0, strlen(p));
            }
            av_strlcat(tmp_str, text + start, tmp_max_url_size);
            xmlNodeSetContent(baseurl_nodes[i], tmp_str);
            updated = 1;
            xmlFree(text);
        }
    }

end:
    if (tmp_max_url_size > *max_url_size) {
        *max_url_size = tmp_max_url_size;
    }
    av_free(path);
    av_free(tmp_str);
    xmlFree(baseurl);
    return updated;

}

static int parse_manifest_representation(AVFormatContext *s, const char *url,
                                         xmlNodePtr node,
                                         xmlNodePtr adaptionset_node,
                                         xmlNodePtr mpd_baseurl_node,
                                         xmlNodePtr period_baseurl_node,
                                         xmlNodePtr period_segmenttemplate_node,
                                         xmlNodePtr period_segmentlist_node,
                                         xmlNodePtr fragment_template_node,
                                         xmlNodePtr content_component_node,
                                         xmlNodePtr adaptionset_baseurl_node,
                                         xmlNodePtr adaptionset_segmentlist_node,
                                         xmlNodePtr adaptionset_supplementalproperty_node)
{
    int32_t ret = 0;
    DASHContext *c = s->priv_data;
    struct representation *rep = NULL;
    struct fragment *seg = NULL;
    xmlNodePtr representation_segmenttemplate_node = NULL;
    xmlNodePtr representation_baseurl_node = NULL;
    xmlNodePtr representation_segmentlist_node = NULL;
    xmlNodePtr segmentlists_tab[3];
    xmlNodePtr fragment_timeline_node = NULL;
    xmlNodePtr fragment_templates_tab[5];
    char *val = NULL;
    xmlNodePtr baseurl_nodes[4];
    xmlNodePtr representation_node = node;
    char *rep_bandwidth_val;
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
    if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO &&
        type != AVMEDIA_TYPE_SUBTITLE) {
        av_log(s, AV_LOG_VERBOSE, "Parsing '%s' - skipp not supported representation type\n", url);
        return 0;
    }

    // convert selected representation to our internal struct
    rep = av_mallocz(sizeof(struct representation));
    if (!rep)
        return AVERROR(ENOMEM);
    if (c->adaptionset_lang) {
        rep->lang = av_strdup(c->adaptionset_lang);
        if (!rep->lang) {
            av_log(s, AV_LOG_ERROR, "alloc language memory failure\n");
            av_freep(&rep);
            return AVERROR(ENOMEM);
        }
    }
    rep->parent = s;
    representation_segmenttemplate_node = find_child_node_by_name(representation_node, "SegmentTemplate");
    representation_baseurl_node = find_child_node_by_name(representation_node, "BaseURL");
    representation_segmentlist_node = find_child_node_by_name(representation_node, "SegmentList");
    rep_bandwidth_val = xmlGetProp(representation_node, "bandwidth");
    val               = xmlGetProp(representation_node, "id");
    if (val) {
        rep->id = av_strdup(val);
        xmlFree(val);
        if (!rep->id)
            goto enomem;
    }

    baseurl_nodes[0] = mpd_baseurl_node;
    baseurl_nodes[1] = period_baseurl_node;
    baseurl_nodes[2] = adaptionset_baseurl_node;
    baseurl_nodes[3] = representation_baseurl_node;

    ret = resolve_content_path(s, url, &c->max_url_size, baseurl_nodes, 4);
    c->max_url_size = aligned(c->max_url_size
                              + (rep->id ? strlen(rep->id) : 0)
                              + (rep_bandwidth_val ? strlen(rep_bandwidth_val) : 0));
    if (ret == AVERROR(ENOMEM) || ret == 0)
        goto free;
    if (representation_segmenttemplate_node || fragment_template_node || period_segmenttemplate_node) {
        fragment_timeline_node = NULL;
        fragment_templates_tab[0] = representation_segmenttemplate_node;
        fragment_templates_tab[1] = adaptionset_segmentlist_node;
        fragment_templates_tab[2] = fragment_template_node;
        fragment_templates_tab[3] = period_segmenttemplate_node;
        fragment_templates_tab[4] = period_segmentlist_node;

        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "initialization");
        if (val) {
            rep->init_section = av_mallocz(sizeof(struct fragment));
            if (!rep->init_section) {
                xmlFree(val);
                goto enomem;
            }
            c->max_url_size = aligned(c->max_url_size  + strlen(val));
            rep->init_section->url = get_content_url(baseurl_nodes, 4,
                                                     c->max_url_size, rep->id,
                                                     rep_bandwidth_val, val);
            xmlFree(val);
            if (!rep->init_section->url)
                goto enomem;
            rep->init_section->size = -1;
        }
        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "media");
        if (val) {
            c->max_url_size = aligned(c->max_url_size  + strlen(val));
            rep->url_template = get_content_url(baseurl_nodes, 4,
                                                c->max_url_size, rep->id,
                                                rep_bandwidth_val, val);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "presentationTimeOffset");
        if (val) {
            rep->presentation_timeoffset = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->presentation_timeoffset = [%"PRId64"]\n", rep->presentation_timeoffset);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "duration");
        if (val) {
            rep->fragment_duration = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->fragment_duration = [%"PRId64"]\n", rep->fragment_duration);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "timescale");
        if (val) {
            rep->fragment_timescale = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->fragment_timescale = [%"PRId64"]\n", rep->fragment_timescale);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(fragment_templates_tab, 4, "startNumber");
        if (val) {
            rep->start_number = rep->first_seq_no = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->first_seq_no = [%"PRId64"]\n", rep->first_seq_no);
            xmlFree(val);
        }
        if (adaptionset_supplementalproperty_node) {
            if (!av_strcasecmp(xmlGetProp(adaptionset_supplementalproperty_node,"schemeIdUri"), "http://dashif.org/guidelines/last-segment-number")) {
                val = xmlGetProp(adaptionset_supplementalproperty_node,"value");
                if (!val) {
                    av_log(s, AV_LOG_ERROR, "Missing value attribute in adaptionset_supplementalproperty_node\n");
                } else {
                    rep->last_seq_no =(int64_t) strtoll(val, NULL, 10) - 1;
                    xmlFree(val);
                }
            }
        }

        fragment_timeline_node = find_child_node_by_name(representation_segmenttemplate_node, "SegmentTimeline");

        if (!fragment_timeline_node)
            fragment_timeline_node = find_child_node_by_name(fragment_template_node, "SegmentTimeline");
        if (!fragment_timeline_node)
            fragment_timeline_node = find_child_node_by_name(adaptionset_segmentlist_node, "SegmentTimeline");
        if (!fragment_timeline_node)
            fragment_timeline_node = find_child_node_by_name(period_segmentlist_node, "SegmentTimeline");
        if (fragment_timeline_node) {
            fragment_timeline_node = xmlFirstElementChild(fragment_timeline_node);
            while (fragment_timeline_node) {
                ret = parse_manifest_segmenttimeline(s, rep, fragment_timeline_node);
                if (ret < 0)
                    goto free;
                fragment_timeline_node = xmlNextElementSibling(fragment_timeline_node);
            }
        }
    } else if (representation_baseurl_node && !representation_segmentlist_node) {
        seg = av_mallocz(sizeof(struct fragment));
        if (!seg)
            goto enomem;
        ret = av_dynarray_add_nofree(&rep->fragments, &rep->n_fragments, seg);
        if (ret < 0) {
            av_free(seg);
            goto free;
        }
        seg->url = get_content_url(baseurl_nodes, 4, c->max_url_size,
                                   rep->id, rep_bandwidth_val, NULL);
        if (!seg->url)
            goto enomem;
        seg->size = -1;
    } else if (representation_segmentlist_node) {
        // TODO: https://www.brendanlong.com/the-structure-of-an-mpeg-dash-mpd.html
        // http://www-itec.uni-klu.ac.at/dash/ddash/mpdGenerator.php?fragmentlength=15&type=full
        xmlNodePtr fragmenturl_node = NULL;
        segmentlists_tab[0] = representation_segmentlist_node;
        segmentlists_tab[1] = adaptionset_segmentlist_node;
        segmentlists_tab[2] = period_segmentlist_node;

        val = get_val_from_nodes_tab(segmentlists_tab, 3, "duration");
        if (val) {
            rep->fragment_duration = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->fragment_duration = [%"PRId64"]\n", rep->fragment_duration);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(segmentlists_tab, 3, "timescale");
        if (val) {
            rep->fragment_timescale = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->fragment_timescale = [%"PRId64"]\n", rep->fragment_timescale);
            xmlFree(val);
        }
        val = get_val_from_nodes_tab(segmentlists_tab, 3, "startNumber");
        if (val) {
            rep->start_number = rep->first_seq_no = (int64_t) strtoll(val, NULL, 10);
            av_log(s, AV_LOG_TRACE, "rep->first_seq_no = [%"PRId64"]\n", rep->first_seq_no);
            xmlFree(val);
        }

        fragmenturl_node = xmlFirstElementChild(representation_segmentlist_node);
        while (fragmenturl_node) {
            ret = parse_manifest_segmenturlnode(s, rep, fragmenturl_node,
                                                baseurl_nodes, rep->id,
                                                rep_bandwidth_val);
            if (ret < 0)
                goto free;
            fragmenturl_node = xmlNextElementSibling(fragmenturl_node);
        }

        fragment_timeline_node = find_child_node_by_name(adaptionset_segmentlist_node, "SegmentTimeline");
        if (!fragment_timeline_node)
            fragment_timeline_node = find_child_node_by_name(period_segmentlist_node, "SegmentTimeline");
        if (fragment_timeline_node) {
            fragment_timeline_node = xmlFirstElementChild(fragment_timeline_node);
            while (fragment_timeline_node) {
                ret = parse_manifest_segmenttimeline(s, rep, fragment_timeline_node);
                if (ret < 0)
                    goto free;
                fragment_timeline_node = xmlNextElementSibling(fragment_timeline_node);
            }
        }
    } else {
        av_log(s, AV_LOG_ERROR, "Unknown format of Representation node id '%s' \n",
               rep->id ? rep->id : "");
        goto free;
    }

    if (rep->fragment_duration > 0 && !rep->fragment_timescale)
        rep->fragment_timescale = 1;
    rep->bandwidth = rep_bandwidth_val ? atoi(rep_bandwidth_val) : 0;
    rep->framerate = av_make_q(0, 0);
    if (type == AVMEDIA_TYPE_VIDEO) {
        char *rep_framerate_val = xmlGetProp(representation_node, "frameRate");
        if (rep_framerate_val) {
            ret = av_parse_video_rate(&rep->framerate, rep_framerate_val);
            if (ret < 0)
                av_log(s, AV_LOG_VERBOSE, "Ignoring invalid frame rate '%s'\n", rep_framerate_val);
            xmlFree(rep_framerate_val);
        }
    }

    switch (type) {
    case AVMEDIA_TYPE_VIDEO:
        ret = av_dynarray_add_nofree(&c->videos, &c->n_videos, rep);
        break;
    case AVMEDIA_TYPE_AUDIO:
        ret = av_dynarray_add_nofree(&c->audios, &c->n_audios, rep);
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        ret = av_dynarray_add_nofree(&c->subtitles, &c->n_subtitles, rep);
        break;
    }
    if (ret < 0)
        goto free;

end:
    if (rep_bandwidth_val)
        xmlFree(rep_bandwidth_val);

    return ret;
enomem:
    ret = AVERROR(ENOMEM);
free:
    free_representation(rep);
    goto end;
}

static int parse_manifest_adaptationset_attr(AVFormatContext *s, xmlNodePtr adaptionset_node)
{
    DASHContext *c = s->priv_data;

    if (!adaptionset_node) {
        av_log(s, AV_LOG_WARNING, "Cannot get AdaptionSet\n");
        return AVERROR(EINVAL);
    }
    c->adaptionset_lang = xmlGetProp(adaptionset_node, "lang");

    return 0;
}

static int parse_manifest_adaptationset(AVFormatContext *s, const char *url,
                                        xmlNodePtr adaptionset_node,
                                        xmlNodePtr mpd_baseurl_node,
                                        xmlNodePtr period_baseurl_node,
                                        xmlNodePtr period_segmenttemplate_node,
                                        xmlNodePtr period_segmentlist_node)
{
    int ret = 0;
    DASHContext *c = s->priv_data;
    xmlNodePtr fragment_template_node = NULL;
    xmlNodePtr content_component_node = NULL;
    xmlNodePtr adaptionset_baseurl_node = NULL;
    xmlNodePtr adaptionset_segmentlist_node = NULL;
    xmlNodePtr adaptionset_supplementalproperty_node = NULL;
    xmlNodePtr node = NULL;

    ret = parse_manifest_adaptationset_attr(s, adaptionset_node);
    if (ret < 0)
        return ret;

    node = xmlFirstElementChild(adaptionset_node);
    while (node) {
        if (!av_strcasecmp(node->name, "SegmentTemplate")) {
            fragment_template_node = node;
        } else if (!av_strcasecmp(node->name, "ContentComponent")) {
            content_component_node = node;
        } else if (!av_strcasecmp(node->name, "BaseURL")) {
            adaptionset_baseurl_node = node;
        } else if (!av_strcasecmp(node->name, "SegmentList")) {
            adaptionset_segmentlist_node = node;
        } else if (!av_strcasecmp(node->name, "SupplementalProperty")) {
            adaptionset_supplementalproperty_node = node;
        } else if (!av_strcasecmp(node->name, "Representation")) {
            ret = parse_manifest_representation(s, url, node,
                                                adaptionset_node,
                                                mpd_baseurl_node,
                                                period_baseurl_node,
                                                period_segmenttemplate_node,
                                                period_segmentlist_node,
                                                fragment_template_node,
                                                content_component_node,
                                                adaptionset_baseurl_node,
                                                adaptionset_segmentlist_node,
                                                adaptionset_supplementalproperty_node);
            if (ret < 0)
                goto err;
        }
        node = xmlNextElementSibling(node);
    }

err:
    xmlFree(c->adaptionset_lang);
    c->adaptionset_lang = NULL;
    return ret;
}

static int parse_programinformation(AVFormatContext *s, xmlNodePtr node)
{
    xmlChar *val = NULL;

    node = xmlFirstElementChild(node);
    while (node) {
        if (!av_strcasecmp(node->name, "Title")) {
            val = xmlNodeGetContent(node);
            if (val) {
                av_dict_set(&s->metadata, "Title", val, 0);
            }
        } else if (!av_strcasecmp(node->name, "Source")) {
            val = xmlNodeGetContent(node);
            if (val) {
                av_dict_set(&s->metadata, "Source", val, 0);
            }
        } else if (!av_strcasecmp(node->name, "Copyright")) {
            val = xmlNodeGetContent(node);
            if (val) {
                av_dict_set(&s->metadata, "Copyright", val, 0);
            }
        }
        node = xmlNextElementSibling(node);
        xmlFree(val);
        val = NULL;
    }
    return 0;
}

static int parse_manifest(AVFormatContext *s, const char *url, AVIOContext *in)
{
    DASHContext *c = s->priv_data;
    int ret = 0;
    int close_in = 0;
    int64_t filesize = 0;
    AVBPrint buf;
    AVDictionary *opts = NULL;
    xmlDoc *doc = NULL;
    xmlNodePtr root_element = NULL;
    xmlNodePtr node = NULL;
    xmlNodePtr period_node = NULL;
    xmlNodePtr tmp_node = NULL;
    xmlNodePtr mpd_baseurl_node = NULL;
    xmlNodePtr period_baseurl_node = NULL;
    xmlNodePtr period_segmenttemplate_node = NULL;
    xmlNodePtr period_segmentlist_node = NULL;
    xmlNodePtr adaptionset_node = NULL;
    xmlAttrPtr attr = NULL;
    char *val  = NULL;
    uint32_t period_duration_sec = 0;
    uint32_t period_start_sec = 0;

    if (!in) {
        close_in = 1;

        av_dict_copy(&opts, c->avio_opts, 0);
        ret = avio_open2(&in, url, AVIO_FLAG_READ, c->interrupt_callback, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
    }

    if (av_opt_get(in, "location", AV_OPT_SEARCH_CHILDREN, (uint8_t**)&c->base_url) < 0)
        c->base_url = av_strdup(url);

    filesize = avio_size(in);
    filesize = filesize > 0 ? filesize : DEFAULT_MANIFEST_SIZE;

    if (filesize > MAX_BPRINT_READ_SIZE) {
        av_log(s, AV_LOG_ERROR, "Manifest too large: %"PRId64"\n", filesize);
        return AVERROR_INVALIDDATA;
    }

    av_bprint_init(&buf, filesize + 1, AV_BPRINT_SIZE_UNLIMITED);

    if ((ret = avio_read_to_bprint(in, &buf, MAX_BPRINT_READ_SIZE)) < 0 ||
        !avio_feof(in) ||
        (filesize = buf.len) == 0) {
        av_log(s, AV_LOG_ERROR, "Unable to read to manifest '%s'\n", url);
        if (ret == 0)
            ret = AVERROR_INVALIDDATA;
    } else {
        LIBXML_TEST_VERSION

        doc = xmlReadMemory(buf.str, filesize, c->base_url, NULL, 0);
        root_element = xmlDocGetRootElement(doc);
        node = root_element;

        if (!node) {
            ret = AVERROR_INVALIDDATA;
            av_log(s, AV_LOG_ERROR, "Unable to parse '%s' - missing root node\n", url);
            goto cleanup;
        }

        if (node->type != XML_ELEMENT_NODE ||
            av_strcasecmp(node->name, "MPD")) {
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
        if (!av_strcasecmp(val, "dynamic"))
            c->is_live = 1;
        xmlFree(val);

        attr = node->properties;
        while (attr) {
            val = xmlGetProp(node, attr->name);

            if (!av_strcasecmp(attr->name, "availabilityStartTime")) {
                c->availability_start_time = get_utc_date_time_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->availability_start_time = [%"PRId64"]\n", c->availability_start_time);
            } else if (!av_strcasecmp(attr->name, "availabilityEndTime")) {
                c->availability_end_time = get_utc_date_time_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->availability_end_time = [%"PRId64"]\n", c->availability_end_time);
            } else if (!av_strcasecmp(attr->name, "publishTime")) {
                c->publish_time = get_utc_date_time_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->publish_time = [%"PRId64"]\n", c->publish_time);
            } else if (!av_strcasecmp(attr->name, "minimumUpdatePeriod")) {
                c->minimum_update_period = get_duration_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->minimum_update_period = [%"PRId64"]\n", c->minimum_update_period);
            } else if (!av_strcasecmp(attr->name, "timeShiftBufferDepth")) {
                c->time_shift_buffer_depth = get_duration_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->time_shift_buffer_depth = [%"PRId64"]\n", c->time_shift_buffer_depth);
            } else if (!av_strcasecmp(attr->name, "minBufferTime")) {
                c->min_buffer_time = get_duration_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->min_buffer_time = [%"PRId64"]\n", c->min_buffer_time);
            } else if (!av_strcasecmp(attr->name, "suggestedPresentationDelay")) {
                c->suggested_presentation_delay = get_duration_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->suggested_presentation_delay = [%"PRId64"]\n", c->suggested_presentation_delay);
            } else if (!av_strcasecmp(attr->name, "mediaPresentationDuration")) {
                c->media_presentation_duration = get_duration_insec(s, val);
                av_log(s, AV_LOG_TRACE, "c->media_presentation_duration = [%"PRId64"]\n", c->media_presentation_duration);
            }
            attr = attr->next;
            xmlFree(val);
        }

        tmp_node = find_child_node_by_name(node, "BaseURL");
        if (tmp_node) {
            mpd_baseurl_node = xmlCopyNode(tmp_node,1);
        } else {
            mpd_baseurl_node = xmlNewNode(NULL, "BaseURL");
        }

        // at now we can handle only one period, with the longest duration
        node = xmlFirstElementChild(node);
        while (node) {
            if (!av_strcasecmp(node->name, "Period")) {
                period_duration_sec = 0;
                period_start_sec = 0;
                attr = node->properties;
                while (attr) {
                    val = xmlGetProp(node, attr->name);
                    if (!av_strcasecmp(attr->name, "duration")) {
                        period_duration_sec = get_duration_insec(s, val);
                    } else if (!av_strcasecmp(attr->name, "start")) {
                        period_start_sec    = get_duration_insec(s, val);
                    }
                    attr = attr->next;
                    xmlFree(val);
                }
                if ((period_duration_sec) >= (c->period_duration)) {
                    period_node = node;
                    c->period_duration = period_duration_sec;
                    c->period_start = period_start_sec;
                    if (c->period_start > 0)
                        c->media_presentation_duration = c->period_duration;
                }
            } else if (!av_strcasecmp(node->name, "ProgramInformation")) {
                parse_programinformation(s, node);
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
            if (!av_strcasecmp(adaptionset_node->name, "BaseURL")) {
                period_baseurl_node = adaptionset_node;
            } else if (!av_strcasecmp(adaptionset_node->name, "SegmentTemplate")) {
                period_segmenttemplate_node = adaptionset_node;
            } else if (!av_strcasecmp(adaptionset_node->name, "SegmentList")) {
                period_segmentlist_node = adaptionset_node;
            } else if (!av_strcasecmp(adaptionset_node->name, "AdaptationSet")) {
                parse_manifest_adaptationset(s, url, adaptionset_node, mpd_baseurl_node, period_baseurl_node, period_segmenttemplate_node, period_segmentlist_node);
            }
            adaptionset_node = xmlNextElementSibling(adaptionset_node);
        }
cleanup:
        /*free the document */
        xmlFreeDoc(doc);
        xmlCleanupParser();
        xmlFreeNode(mpd_baseurl_node);
    }

    av_bprint_finalize(&buf, NULL);
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
            av_log(s, AV_LOG_TRACE, "in n_fragments mode\n");
            num = pls->first_seq_no;
        } else if (pls->n_timelines) {
            av_log(s, AV_LOG_TRACE, "in n_timelines mode\n");
            start_time_offset = get_segment_start_time_based_on_timeline(pls, 0xFFFFFFFF) - 60 * pls->fragment_timescale; // 60 seconds before end
            num = calc_next_seg_no_from_timelines(pls, start_time_offset);
            if (num == -1)
                num = pls->first_seq_no;
            else
                num += pls->first_seq_no;
        } else if (pls->fragment_duration){
            av_log(s, AV_LOG_TRACE, "in fragment_duration mode fragment_timescale = %"PRId64", presentation_timeoffset = %"PRId64"\n", pls->fragment_timescale, pls->presentation_timeoffset);
            if (pls->presentation_timeoffset) {
                num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time) * pls->fragment_timescale)-pls->presentation_timeoffset) / pls->fragment_duration - c->min_buffer_time;
            } else if (c->publish_time > 0 && !c->availability_start_time) {
                if (c->min_buffer_time) {
                    num = pls->first_seq_no + (((c->publish_time + pls->fragment_duration) - c->suggested_presentation_delay) * pls->fragment_timescale) / pls->fragment_duration - c->min_buffer_time;
                } else {
                    num = pls->first_seq_no + (((c->publish_time - c->time_shift_buffer_depth + pls->fragment_duration) - c->suggested_presentation_delay) * pls->fragment_timescale) / pls->fragment_duration;
                }
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
        av_log(s, AV_LOG_TRACE, "in live mode\n");
        num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time) - c->time_shift_buffer_depth) * pls->fragment_timescale) / pls->fragment_duration;
    } else {
        num = pls->first_seq_no;
    }
    return num;
}

static int64_t calc_max_seg_no(struct representation *pls, DASHContext *c)
{
    int64_t num = 0;

    if (pls->n_fragments) {
        num = pls->first_seq_no + pls->n_fragments - 1;
    } else if (pls->n_timelines) {
        int i = 0;
        num = pls->first_seq_no + pls->n_timelines - 1;
        for (i = 0; i < pls->n_timelines; i++) {
            if (pls->timelines[i]->repeat == -1) {
                int length_of_each_segment = pls->timelines[i]->duration / pls->fragment_timescale;
                num =  c->period_duration / length_of_each_segment;
            } else {
                num += pls->timelines[i]->repeat;
            }
        }
    } else if (c->is_live && pls->fragment_duration) {
        num = pls->first_seq_no + (((get_current_time_in_sec() - c->availability_start_time)) * pls->fragment_timescale)  / pls->fragment_duration;
    } else if (pls->fragment_duration) {
        num = pls->first_seq_no + av_rescale_rnd(1, c->media_presentation_duration * pls->fragment_timescale, pls->fragment_duration, AV_ROUND_UP);
    }

    return num;
}

static void move_timelines(struct representation *rep_src, struct representation *rep_dest, DASHContext *c)
{
    if (rep_dest && rep_src ) {
        free_timelines_list(rep_dest);
        rep_dest->timelines    = rep_src->timelines;
        rep_dest->n_timelines  = rep_src->n_timelines;
        rep_dest->first_seq_no = rep_src->first_seq_no;
        rep_dest->last_seq_no = calc_max_seg_no(rep_dest, c);
        rep_src->timelines = NULL;
        rep_src->n_timelines = 0;
        rep_dest->cur_seq_no = rep_src->cur_seq_no;
    }
}

static void move_segments(struct representation *rep_src, struct representation *rep_dest, DASHContext *c)
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
        rep_dest->last_seq_no = calc_max_seg_no(rep_dest, c);
        rep_src->fragments = NULL;
        rep_src->n_fragments = 0;
    }
}


static int refresh_manifest(AVFormatContext *s)
{
    int ret = 0, i;
    DASHContext *c = s->priv_data;
    // save current context
    int n_videos = c->n_videos;
    struct representation **videos = c->videos;
    int n_audios = c->n_audios;
    struct representation **audios = c->audios;
    int n_subtitles = c->n_subtitles;
    struct representation **subtitles = c->subtitles;
    char *base_url = c->base_url;

    c->base_url = NULL;
    c->n_videos = 0;
    c->videos = NULL;
    c->n_audios = 0;
    c->audios = NULL;
    c->n_subtitles = 0;
    c->subtitles = NULL;
    ret = parse_manifest(s, s->url, NULL);
    if (ret)
        goto finish;

    if (c->n_videos != n_videos) {
        av_log(c, AV_LOG_ERROR,
               "new manifest has mismatched no. of video representations, %d -> %d\n",
               n_videos, c->n_videos);
        return AVERROR_INVALIDDATA;
    }
    if (c->n_audios != n_audios) {
        av_log(c, AV_LOG_ERROR,
               "new manifest has mismatched no. of audio representations, %d -> %d\n",
               n_audios, c->n_audios);
        return AVERROR_INVALIDDATA;
    }
    if (c->n_subtitles != n_subtitles) {
        av_log(c, AV_LOG_ERROR,
               "new manifest has mismatched no. of subtitles representations, %d -> %d\n",
               n_subtitles, c->n_subtitles);
        return AVERROR_INVALIDDATA;
    }

    for (i = 0; i < n_videos; i++) {
        struct representation *cur_video = videos[i];
        struct representation *ccur_video = c->videos[i];
        if (cur_video->timelines) {
            // calc current time
            int64_t currentTime = get_segment_start_time_based_on_timeline(cur_video, cur_video->cur_seq_no) / cur_video->fragment_timescale;
            // update segments
            ccur_video->cur_seq_no = calc_next_seg_no_from_timelines(ccur_video, currentTime * cur_video->fragment_timescale - 1);
            if (ccur_video->cur_seq_no >= 0) {
                move_timelines(ccur_video, cur_video, c);
            }
        }
        if (cur_video->fragments) {
            move_segments(ccur_video, cur_video, c);
        }
    }
    for (i = 0; i < n_audios; i++) {
        struct representation *cur_audio = audios[i];
        struct representation *ccur_audio = c->audios[i];
        if (cur_audio->timelines) {
            // calc current time
            int64_t currentTime = get_segment_start_time_based_on_timeline(cur_audio, cur_audio->cur_seq_no) / cur_audio->fragment_timescale;
            // update segments
            ccur_audio->cur_seq_no = calc_next_seg_no_from_timelines(ccur_audio, currentTime * cur_audio->fragment_timescale - 1);
            if (ccur_audio->cur_seq_no >= 0) {
                move_timelines(ccur_audio, cur_audio, c);
            }
        }
        if (cur_audio->fragments) {
            move_segments(ccur_audio, cur_audio, c);
        }
    }

finish:
    // restore context
    if (c->base_url)
        av_free(base_url);
    else
        c->base_url  = base_url;

    if (c->subtitles)
        free_subtitle_list(c);
    if (c->audios)
        free_audio_list(c);
    if (c->videos)
        free_video_list(c);

    c->n_subtitles = n_subtitles;
    c->subtitles = subtitles;
    c->n_audios = n_audios;
    c->audios = audios;
    c->n_videos = n_videos;
    c->videos = videos;
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
        max_seq_no = calc_max_seg_no(pls, c);

        if (pls->timelines || pls->fragments) {
            refresh_manifest(pls->parent);
        }
        if (pls->cur_seq_no <= min_seq_no) {
            av_log(pls->parent, AV_LOG_VERBOSE, "old fragment: cur[%"PRId64"] min[%"PRId64"] max[%"PRId64"]\n", (int64_t)pls->cur_seq_no, min_seq_no, max_seq_no);
            pls->cur_seq_no = calc_cur_seg_no(pls->parent, pls);
        } else if (pls->cur_seq_no > max_seq_no) {
            av_log(pls->parent, AV_LOG_VERBOSE, "new fragment: min[%"PRId64"] max[%"PRId64"]\n", min_seq_no, max_seq_no);
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
        char *tmpfilename;
        if (!pls->url_template) {
            av_log(pls->parent, AV_LOG_ERROR, "Cannot get fragment, missing template URL\n");
            av_free(seg);
            return NULL;
        }
        tmpfilename = av_mallocz(c->max_url_size);
        if (!tmpfilename) {
            av_free(seg);
            return NULL;
        }
        ff_dash_fill_tmpl_params(tmpfilename, c->max_url_size, pls->url_template, 0, pls->cur_seq_no, 0, get_segment_start_time_based_on_timeline(pls, pls->cur_seq_no));
        seg->url = av_strireplace(pls->url_template, pls->url_template, tmpfilename);
        if (!seg->url) {
            av_log(pls->parent, AV_LOG_WARNING, "Unable to resolve template url '%s', try to use origin template\n", pls->url_template);
            seg->url = av_strdup(pls->url_template);
            if (!seg->url) {
                av_log(pls->parent, AV_LOG_ERROR, "Cannot resolve template url '%s'\n", pls->url_template);
                av_free(tmpfilename);
                av_free(seg);
                return NULL;
            }
        }
        av_free(tmpfilename);
        seg->size = -1;
    }

    return seg;
}

static int read_from_url(struct representation *pls, struct fragment *seg,
                         uint8_t *buf, int buf_size)
{
    int ret;

    /* limit read if the fragment was only a part of a file */
    if (seg->size >= 0)
        buf_size = FFMIN(buf_size, pls->cur_seg_size - pls->cur_seg_offset);

    ret = avio_read(pls->input, buf, buf_size);
    if (ret > 0)
        pls->cur_seg_offset += ret;

    return ret;
}

static int open_input(DASHContext *c, struct representation *pls, struct fragment *seg)
{
    AVDictionary *opts = NULL;
    char *url = NULL;
    int ret = 0;

    url = av_mallocz(c->max_url_size);
    if (!url) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }

    if (seg->size >= 0) {
        /* try to restrict the HTTP request to the part we want
         * (if this is in fact a HTTP request) */
        av_dict_set_int(&opts, "offset", seg->url_offset, 0);
        av_dict_set_int(&opts, "end_offset", seg->url_offset + seg->size, 0);
    }

    ff_make_absolute_url(url, c->max_url_size, c->base_url, seg->url);
    av_log(pls->parent, AV_LOG_VERBOSE, "DASH request for url '%s', offset %"PRId64"\n",
           url, seg->url_offset);
    ret = open_url(pls->parent, &pls->input, url, &c->avio_opts, opts, NULL);

cleanup:
    av_free(url);
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
               "Failed to open an initialization section\n");
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
                        pls->init_sec_buf_size);
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
                ret = AVERROR_EXIT;
                goto end;
            }
            av_log(v->parent, AV_LOG_WARNING, "Failed to open fragment of playlist\n");
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
    ret = read_from_url(v, v->cur_seg, buf, buf_size);
    if (ret > 0)
        goto end;

    if (c->is_live || v->cur_seq_no < v->last_seq_no) {
        if (!v->is_restart_needed)
            v->cur_seq_no++;
        v->is_restart_needed = 1;
    }

end:
    return ret;
}

static int nested_io_open(AVFormatContext *s, AVIOContext **pb, const char *url,
                          int flags, AVDictionary **opts)
{
    av_log(s, AV_LOG_ERROR,
           "A DASH playlist item '%s' referred to an external file '%s'. "
           "Opening this file was forbidden for security reasons\n",
           s->url, url);
    return AVERROR(EPERM);
}

static void close_demux_for_component(struct representation *pls)
{
    /* note: the internal buffer could have changed */
    av_freep(&pls->pb.pub.buffer);
    memset(&pls->pb, 0x00, sizeof(pls->pb));
    pls->ctx->pb = NULL;
    avformat_close_input(&pls->ctx);
}

static int reopen_demux_for_component(AVFormatContext *s, struct representation *pls)
{
    DASHContext *c = s->priv_data;
    const AVInputFormat *in_fmt = NULL;
    AVDictionary  *in_fmt_opts = NULL;
    uint8_t *avio_ctx_buffer  = NULL;
    int ret = 0, i;

    if (pls->ctx) {
        close_demux_for_component(pls);
    }

    if (ff_check_interrupt(&s->interrupt_callback)) {
        ret = AVERROR_EXIT;
        goto fail;
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
    ffio_init_context(&pls->pb, avio_ctx_buffer, INITIAL_BUFFER_SIZE, 0,
                      pls, read_data, NULL, c->is_live ? NULL : seek_data);
    pls->pb.pub.seekable = 0;

    if ((ret = ff_copy_whiteblacklists(pls->ctx, s)) < 0)
        goto fail;

    pls->ctx->flags = AVFMT_FLAG_CUSTOM_IO;
    pls->ctx->probesize = s->probesize > 0 ? s->probesize : 1024 * 4;
    pls->ctx->max_analyze_duration = s->max_analyze_duration > 0 ? s->max_analyze_duration : 4 * AV_TIME_BASE;
    pls->ctx->interrupt_callback = s->interrupt_callback;
    ret = av_probe_input_buffer(&pls->pb.pub, &in_fmt, "", NULL, 0, 0);
    if (ret < 0) {
        av_log(s, AV_LOG_ERROR, "Error when loading first fragment of playlist\n");
        avformat_free_context(pls->ctx);
        pls->ctx = NULL;
        goto fail;
    }

    pls->ctx->pb = &pls->pb.pub;
    pls->ctx->io_open  = nested_io_open;

    // provide additional information from mpd if available
    ret = avformat_open_input(&pls->ctx, "", in_fmt, &in_fmt_opts); //pls->init_section->url
    av_dict_free(&in_fmt_opts);
    if (ret < 0)
        goto fail;
    if (pls->n_fragments) {
#if FF_API_R_FRAME_RATE
        if (pls->framerate.den) {
            for (i = 0; i < pls->ctx->nb_streams; i++)
                pls->ctx->streams[i]->r_frame_rate = pls->framerate;
        }
#endif
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

    if (!pls->last_seq_no) {
        pls->last_seq_no = calc_max_seg_no(pls, s->priv_data);
    }

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
        avcodec_parameters_copy(st->codecpar, ist->codecpar);
        avpriv_set_pts_info(st, ist->pts_wrap_bits, ist->time_base.num, ist->time_base.den);

        // copy disposition
        st->disposition = ist->disposition;

        // copy side data
        for (int i = 0; i < ist->nb_side_data; i++) {
            const AVPacketSideData *sd_src = &ist->side_data[i];
            uint8_t *dst_data;

            dst_data = av_stream_new_side_data(st, sd_src->type, sd_src->size);
            if (!dst_data)
                return AVERROR(ENOMEM);
            memcpy(dst_data, sd_src->data, sd_src->size);
        }
    }

    return 0;
fail:
    return ret;
}

static int is_common_init_section_exist(struct representation **pls, int n_pls)
{
    struct fragment *first_init_section = pls[0]->init_section;
    char *url =NULL;
    int64_t url_offset = -1;
    int64_t size = -1;
    int i = 0;

    if (first_init_section == NULL || n_pls == 0)
        return 0;

    url = first_init_section->url;
    url_offset = first_init_section->url_offset;
    size = pls[0]->init_section->size;
    for (i=0;i<n_pls;i++) {
        if (!pls[i]->init_section)
            continue;

        if (av_strcasecmp(pls[i]->init_section->url, url) ||
            pls[i]->init_section->url_offset != url_offset ||
            pls[i]->init_section->size != size) {
            return 0;
        }
    }
    return 1;
}

static int copy_init_section(struct representation *rep_dest, struct representation *rep_src)
{
    rep_dest->init_sec_buf = av_mallocz(rep_src->init_sec_buf_size);
    if (!rep_dest->init_sec_buf) {
        av_log(rep_dest->ctx, AV_LOG_WARNING, "Cannot alloc memory for init_sec_buf\n");
        return AVERROR(ENOMEM);
    }
    memcpy(rep_dest->init_sec_buf, rep_src->init_sec_buf, rep_src->init_sec_data_len);
    rep_dest->init_sec_buf_size = rep_src->init_sec_buf_size;
    rep_dest->init_sec_data_len = rep_src->init_sec_data_len;
    rep_dest->cur_timestamp = rep_src->cur_timestamp;

    return 0;
}

static void move_metadata(AVStream *st, const char *key, char **value)
{
    if (*value) {
        av_dict_set(&st->metadata, key, *value, AV_DICT_DONT_STRDUP_VAL);
        *value = NULL;
    }
}

static int dash_read_header(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    struct representation *rep;
    AVProgram *program;
    int ret = 0;
    int stream_index = 0;
    int i;

    c->interrupt_callback = &s->interrupt_callback;

    if ((ret = ffio_copy_url_options(s->pb, &c->avio_opts)) < 0)
        return ret;

    if ((ret = parse_manifest(s, s->url, s->pb)) < 0)
        return ret;

    /* If this isn't a live stream, fill the total duration of the
     * stream. */
    if (!c->is_live) {
        s->duration = (int64_t) c->media_presentation_duration * AV_TIME_BASE;
    } else {
        av_dict_set(&c->avio_opts, "seekable", "0", 0);
    }

    if(c->n_videos)
        c->is_init_section_common_video = is_common_init_section_exist(c->videos, c->n_videos);

    /* Open the demuxer for video and audio components if available */
    for (i = 0; i < c->n_videos; i++) {
        rep = c->videos[i];
        if (i > 0 && c->is_init_section_common_video) {
            ret = copy_init_section(rep, c->videos[0]);
            if (ret < 0)
                return ret;
        }
        ret = open_demux_for_component(s, rep);

        if (ret)
            return ret;
        rep->stream_index = stream_index;
        ++stream_index;
    }

    if(c->n_audios)
        c->is_init_section_common_audio = is_common_init_section_exist(c->audios, c->n_audios);

    for (i = 0; i < c->n_audios; i++) {
        rep = c->audios[i];
        if (i > 0 && c->is_init_section_common_audio) {
            ret = copy_init_section(rep, c->audios[0]);
            if (ret < 0)
                return ret;
        }
        ret = open_demux_for_component(s, rep);

        if (ret)
            return ret;
        rep->stream_index = stream_index;
        ++stream_index;
    }

    if (c->n_subtitles)
        c->is_init_section_common_subtitle = is_common_init_section_exist(c->subtitles, c->n_subtitles);

    for (i = 0; i < c->n_subtitles; i++) {
        rep = c->subtitles[i];
        if (i > 0 && c->is_init_section_common_subtitle) {
            ret = copy_init_section(rep, c->subtitles[0]);
            if (ret < 0)
                return ret;
        }
        ret = open_demux_for_component(s, rep);

        if (ret)
            return ret;
        rep->stream_index = stream_index;
        ++stream_index;
    }

    if (!stream_index)
        return AVERROR_INVALIDDATA;

    /* Create a program */
    program = av_new_program(s, 0);
    if (!program)
        return AVERROR(ENOMEM);

    for (i = 0; i < c->n_videos; i++) {
        rep = c->videos[i];
        av_program_add_stream_index(s, 0, rep->stream_index);
        rep->assoc_stream = s->streams[rep->stream_index];
        if (rep->bandwidth > 0)
            av_dict_set_int(&rep->assoc_stream->metadata, "variant_bitrate", rep->bandwidth, 0);
        move_metadata(rep->assoc_stream, "id", &rep->id);
    }
    for (i = 0; i < c->n_audios; i++) {
        rep = c->audios[i];
        av_program_add_stream_index(s, 0, rep->stream_index);
        rep->assoc_stream = s->streams[rep->stream_index];
        if (rep->bandwidth > 0)
            av_dict_set_int(&rep->assoc_stream->metadata, "variant_bitrate", rep->bandwidth, 0);
        move_metadata(rep->assoc_stream, "id", &rep->id);
        move_metadata(rep->assoc_stream, "language", &rep->lang);
    }
    for (i = 0; i < c->n_subtitles; i++) {
        rep = c->subtitles[i];
        av_program_add_stream_index(s, 0, rep->stream_index);
        rep->assoc_stream = s->streams[rep->stream_index];
        move_metadata(rep->assoc_stream, "id", &rep->id);
        move_metadata(rep->assoc_stream, "language", &rep->lang);
    }

    return 0;
}

static void recheck_discard_flags(AVFormatContext *s, struct representation **p, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        struct representation *pls = p[i];
        int needed = !pls->assoc_stream || pls->assoc_stream->discard < AVDISCARD_ALL;

        if (needed && !pls->ctx) {
            pls->cur_seg_offset = 0;
            pls->init_sec_buf_read_offset = 0;
            /* Catch up */
            for (j = 0; j < n; j++) {
                pls->cur_seq_no = FFMAX(pls->cur_seq_no, p[j]->cur_seq_no);
            }
            reopen_demux_for_component(s, pls);
            av_log(s, AV_LOG_INFO, "Now receiving stream_index %d\n", pls->stream_index);
        } else if (!needed && pls->ctx) {
            close_demux_for_component(pls);
            ff_format_io_close(pls->parent, &pls->input);
            av_log(s, AV_LOG_INFO, "No longer receiving stream_index %d\n", pls->stream_index);
        }
    }
}

static int dash_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DASHContext *c = s->priv_data;
    int ret = 0, i;
    int64_t mints = 0;
    struct representation *cur = NULL;
    struct representation *rep = NULL;

    recheck_discard_flags(s, c->videos, c->n_videos);
    recheck_discard_flags(s, c->audios, c->n_audios);
    recheck_discard_flags(s, c->subtitles, c->n_subtitles);

    for (i = 0; i < c->n_videos; i++) {
        rep = c->videos[i];
        if (!rep->ctx)
            continue;
        if (!cur || rep->cur_timestamp < mints) {
            cur = rep;
            mints = rep->cur_timestamp;
        }
    }
    for (i = 0; i < c->n_audios; i++) {
        rep = c->audios[i];
        if (!rep->ctx)
            continue;
        if (!cur || rep->cur_timestamp < mints) {
            cur = rep;
            mints = rep->cur_timestamp;
        }
    }

    for (i = 0; i < c->n_subtitles; i++) {
        rep = c->subtitles[i];
        if (!rep->ctx)
            continue;
        if (!cur || rep->cur_timestamp < mints) {
            cur = rep;
            mints = rep->cur_timestamp;
        }
    }

    if (!cur) {
        return AVERROR_INVALIDDATA;
    }
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
            ff_format_io_close(cur->parent, &cur->input);
            ret = reopen_demux_for_component(s, cur);
            cur->is_restart_needed = 0;
        }
    }
    return AVERROR_EOF;
}

static int dash_close(AVFormatContext *s)
{
    DASHContext *c = s->priv_data;
    free_audio_list(c);
    free_video_list(c);
    free_subtitle_list(c);
    av_dict_free(&c->avio_opts);
    av_freep(&c->base_url);
    return 0;
}

static int dash_seek(AVFormatContext *s, struct representation *pls, int64_t seek_pos_msec, int flags, int dry_run)
{
    int ret = 0;
    int i = 0;
    int j = 0;
    int64_t duration = 0;

    av_log(pls->parent, AV_LOG_VERBOSE, "DASH seek pos[%"PRId64"ms] %s\n",
           seek_pos_msec, dry_run ? " (dry)" : "");

    // single fragment mode
    if (pls->n_fragments == 1) {
        pls->cur_timestamp = 0;
        pls->cur_seg_offset = 0;
        if (dry_run)
            return 0;
        ff_read_frame_flush(pls->ctx);
        return av_seek_frame(pls->ctx, -1, seek_pos_msec * 1000, flags);
    }

    ff_format_io_close(pls->parent, &pls->input);

    // find the nearest fragment
    if (pls->n_timelines > 0 && pls->fragment_timescale > 0) {
        int64_t num = pls->first_seq_no;
        av_log(pls->parent, AV_LOG_VERBOSE, "dash_seek with SegmentTimeline start n_timelines[%d] "
               "last_seq_no[%"PRId64"].\n",
               (int)pls->n_timelines, (int64_t)pls->last_seq_no);
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
        av_log(pls->parent, AV_LOG_VERBOSE, "dash_seek with SegmentTimeline end cur_seq_no[%"PRId64"].\n",
               (int64_t)pls->cur_seq_no);
    } else if (pls->fragment_duration > 0) {
        pls->cur_seq_no = pls->first_seq_no + ((seek_pos_msec * pls->fragment_timescale) / pls->fragment_duration) / 1000;
    } else {
        av_log(pls->parent, AV_LOG_ERROR, "dash_seek missing timeline or fragment_duration\n");
        pls->cur_seq_no = pls->first_seq_no;
    }
    pls->cur_timestamp = 0;
    pls->cur_seg_offset = 0;
    pls->init_sec_buf_read_offset = 0;
    ret = dry_run ? 0 : reopen_demux_for_component(s, pls);

    return ret;
}

static int dash_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    int ret = 0, i;
    DASHContext *c = s->priv_data;
    int64_t seek_pos_msec = av_rescale_rnd(timestamp, 1000,
                                           s->streams[stream_index]->time_base.den,
                                           flags & AVSEEK_FLAG_BACKWARD ?
                                           AV_ROUND_DOWN : AV_ROUND_UP);
    if ((flags & AVSEEK_FLAG_BYTE) || c->is_live)
        return AVERROR(ENOSYS);

    /* Seek in discarded streams with dry_run=1 to avoid reopening them */
    for (i = 0; i < c->n_videos; i++) {
        if (!ret)
            ret = dash_seek(s, c->videos[i], seek_pos_msec, flags, !c->videos[i]->ctx);
    }
    for (i = 0; i < c->n_audios; i++) {
        if (!ret)
            ret = dash_seek(s, c->audios[i], seek_pos_msec, flags, !c->audios[i]->ctx);
    }
    for (i = 0; i < c->n_subtitles; i++) {
        if (!ret)
            ret = dash_seek(s, c->subtitles[i], seek_pos_msec, flags, !c->subtitles[i]->ctx);
    }

    return ret;
}

static int dash_probe(const AVProbeData *p)
{
    if (!av_stristr(p->buf, "<MPD"))
        return 0;

    if (av_stristr(p->buf, "dash:profile:isoff-on-demand:2011") ||
        av_stristr(p->buf, "dash:profile:isoff-live:2011") ||
        av_stristr(p->buf, "dash:profile:isoff-live:2012") ||
        av_stristr(p->buf, "dash:profile:isoff-main:2011") ||
        av_stristr(p->buf, "3GPP:PSS:profile:DASH1")) {
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
        {.str = "aac,m4a,m4s,m4v,mov,mp4,webm,ts"},
        INT_MIN, INT_MAX, FLAGS},
    {NULL}
};

static const AVClass dash_class = {
    .class_name = "dash",
    .item_name  = av_default_item_name,
    .option     = dash_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVInputFormat ff_dash_demuxer = {
    .name           = "dash",
    .long_name      = NULL_IF_CONFIG_SMALL("Dynamic Adaptive Streaming over HTTP"),
    .priv_class     = &dash_class,
    .priv_data_size = sizeof(DASHContext),
    .flags_internal = FF_FMT_INIT_CLEANUP,
    .read_probe     = dash_probe,
    .read_header    = dash_read_header,
    .read_packet    = dash_read_packet,
    .read_close     = dash_close,
    .read_seek      = dash_read_seek,
    .flags          = AVFMT_NO_BYTE_SEEK,
};
