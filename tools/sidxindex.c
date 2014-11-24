/*
 * Copyright (c) 2014 Martin Storsjo
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

#include <stdio.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"

static int usage(const char *argv0, int ret)
{
    fprintf(stderr, "%s -out foo.mpd file1\n", argv0);
    return ret;
}

struct Track {
    const char *name;
    int64_t duration;
    int bitrate;
    int track_id;
    int is_audio, is_video;
    int width, height;
    int sample_rate, channels;
    int timescale;
    char codec_str[30];
    int64_t sidx_start, sidx_length;
    int64_t  earliest_presentation;
    uint32_t earliest_presentation_timescale;
};

struct Tracks {
    int nb_tracks;
    int64_t duration;
    struct Track **tracks;
    int multiple_tracks_per_file;
};

static void set_codec_str(AVCodecContext *codec, char *str, int size)
{
    switch (codec->codec_id) {
    case AV_CODEC_ID_H264:
        snprintf(str, size, "avc1");
        if (codec->extradata_size >= 4 && codec->extradata[0] == 1) {
            av_strlcatf(str, size, ".%02x%02x%02x",
                        codec->extradata[1], codec->extradata[2], codec->extradata[3]);
        }
        break;
    case AV_CODEC_ID_AAC:
        snprintf(str, size, "mp4a.40"); // 0x40 is the mp4 object type for AAC
        if (codec->extradata_size >= 2) {
            int aot = codec->extradata[0] >> 3;
            if (aot == 31)
                aot = ((AV_RB16(codec->extradata) >> 5) & 0x3f) + 32;
            av_strlcatf(str, size, ".%d", aot);
        }
        break;
    }
}

static int find_sidx(struct Tracks *tracks, int start_index,
                     const char *file)
{
    int err = 0;
    AVIOContext *f = NULL;
    int i;

    if ((err = avio_open2(&f, file, AVIO_FLAG_READ, NULL, NULL)) < 0)
        goto fail;

    while (!f->eof_reached) {
        int64_t pos = avio_tell(f);
        int32_t size, tag;

        size = avio_rb32(f);
        tag  = avio_rb32(f);
        if (size < 8)
            break;
        if (tag == MKBETAG('s', 'i', 'd', 'x')) {
            int version, track_id;
            uint32_t timescale;
            int64_t earliest_presentation;
            version = avio_r8(f);
            avio_rb24(f); /* flags */
            track_id = avio_rb32(f);
            timescale = avio_rb32(f);
            earliest_presentation = version ? avio_rb64(f) : avio_rb32(f);
            for (i = start_index; i < tracks->nb_tracks; i++) {
                struct Track *track = tracks->tracks[i];
                if (!track->sidx_start) {
                    track->sidx_start  = pos;
                    track->sidx_length = size;
                } else if (pos == track->sidx_start + track->sidx_length) {
                    track->sidx_length = pos + size - track->sidx_start;
                }
                if (track->track_id == track_id) {
                    track->earliest_presentation = earliest_presentation;
                    track->earliest_presentation_timescale = timescale;
                }
            }
        }
        if (avio_seek(f, pos + size, SEEK_SET) != pos + size)
            break;
    }

fail:
    if (f)
        avio_close(f);
    return err;
}

static int handle_file(struct Tracks *tracks, const char *file)
{
    AVFormatContext *ctx = NULL;
    int err = 0, i, orig_tracks = tracks->nb_tracks;
    char errbuf[50], *ptr;
    struct Track *track;

    err = avformat_open_input(&ctx, file, NULL, NULL);
    if (err < 0) {
        av_strerror(err, errbuf, sizeof(errbuf));
        fprintf(stderr, "Unable to open %s: %s\n", file, errbuf);
        return 1;
    }

    err = avformat_find_stream_info(ctx, NULL);
    if (err < 0) {
        av_strerror(err, errbuf, sizeof(errbuf));
        fprintf(stderr, "Unable to identify %s: %s\n", file, errbuf);
        goto fail;
    }

    if (ctx->nb_streams < 1) {
        fprintf(stderr, "No streams found in %s\n", file);
        goto fail;
    }
    if (ctx->nb_streams > 1)
        tracks->multiple_tracks_per_file = 1;

    for (i = 0; i < ctx->nb_streams; i++) {
        struct Track **temp;
        AVStream *st = ctx->streams[i];

        if (st->codec->bit_rate == 0) {
            fprintf(stderr, "Skipping track %d in %s as it has zero bitrate\n",
                    st->id, file);
            continue;
        }

        track = av_mallocz(sizeof(*track));
        if (!track) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        temp = av_realloc(tracks->tracks,
                          sizeof(*tracks->tracks) * (tracks->nb_tracks + 1));
        if (!temp) {
            av_free(track);
            err = AVERROR(ENOMEM);
            goto fail;
        }
        tracks->tracks = temp;
        tracks->tracks[tracks->nb_tracks] = track;

        track->name = file;
        if ((ptr = strrchr(file, '/')))
            track->name = ptr + 1;

        track->bitrate   = st->codec->bit_rate;
        track->track_id  = st->id;
        track->timescale = st->time_base.den;
        track->duration  = st->duration;
        track->is_audio  = st->codec->codec_type == AVMEDIA_TYPE_AUDIO;
        track->is_video  = st->codec->codec_type == AVMEDIA_TYPE_VIDEO;

        if (!track->is_audio && !track->is_video) {
            fprintf(stderr,
                    "Track %d in %s is neither video nor audio, skipping\n",
                    track->track_id, file);
            av_freep(&tracks->tracks[tracks->nb_tracks]);
            continue;
        }

        tracks->duration = FFMAX(tracks->duration,
                                 av_rescale_rnd(track->duration, AV_TIME_BASE,
                                                track->timescale, AV_ROUND_UP));

        if (track->is_audio) {
            track->channels    = st->codec->channels;
            track->sample_rate = st->codec->sample_rate;
        }
        if (track->is_video) {
            track->width  = st->codec->width;
            track->height = st->codec->height;
        }
        set_codec_str(st->codec, track->codec_str, sizeof(track->codec_str));

        tracks->nb_tracks++;
    }

    avformat_close_input(&ctx);

    err = find_sidx(tracks, orig_tracks, file);

fail:
    if (ctx)
        avformat_close_input(&ctx);
    return err;
}

static void write_time(FILE *out, int64_t time, int decimals, enum AVRounding round)
{
    int seconds = time / AV_TIME_BASE;
    int fractions = time % AV_TIME_BASE;
    int minutes = seconds / 60;
    int hours = minutes / 60;
    fractions = av_rescale_rnd(fractions, pow(10, decimals), AV_TIME_BASE, round);
    seconds %= 60;
    minutes %= 60;
    fprintf(out, "PT");
    if (hours)
        fprintf(out, "%dH", hours);
    if (hours || minutes)
        fprintf(out, "%dM", minutes);
    fprintf(out, "%d.%0*dS", seconds, decimals, fractions);
}

static int output_mpd(struct Tracks *tracks, const char *filename)
{
    FILE *out;
    int i, j, ret = 0;
    struct Track **adaptation_sets_buf[2] = { NULL };
    struct Track ***adaptation_sets;
    int nb_tracks_buf[2] = { 0 };
    int *nb_tracks;
    int set, nb_sets;
    int64_t latest_start = 0;

    if (!tracks->multiple_tracks_per_file) {
        adaptation_sets = adaptation_sets_buf;
        nb_tracks = nb_tracks_buf;
        nb_sets = 2;
        for (i = 0; i < 2; i++) {
            adaptation_sets[i] = av_malloc(sizeof(*adaptation_sets[i]) * tracks->nb_tracks);
            if (!adaptation_sets[i]) {
                ret = AVERROR(ENOMEM);
                goto err;
            }
        }
        for (i = 0; i < tracks->nb_tracks; i++) {
            int set_index = -1;
            if (tracks->tracks[i]->is_video)
                set_index = 0;
            else if (tracks->tracks[i]->is_audio)
                set_index = 1;
            else
                continue;
            adaptation_sets[set_index][nb_tracks[set_index]++] = tracks->tracks[i];
        }
    } else {
        adaptation_sets = &tracks->tracks;
        nb_tracks = &tracks->nb_tracks;
        nb_sets = 1;
    }

    out = fopen(filename, "w");
    if (!out) {
        ret = AVERROR(errno);
        perror(filename);
        return ret;
    }
    fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(out, "<MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
                "\txmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
                "\txmlns:xlink=\"http://www.w3.org/1999/xlink\"\n"
                "\txsi:schemaLocation=\"urn:mpeg:DASH:schema:MPD:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\"\n"
                "\tprofiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\"\n"
                "\ttype=\"static\"\n");
    fprintf(out, "\tmediaPresentationDuration=\"");
    write_time(out, tracks->duration, 1, AV_ROUND_DOWN);
    fprintf(out, "\"\n");
    fprintf(out, "\tminBufferTime=\"PT5S\">\n");

    for (i = 0; i < tracks->nb_tracks; i++) {
        int64_t start = av_rescale_rnd(tracks->tracks[i]->earliest_presentation,
                                       AV_TIME_BASE,
                                       tracks->tracks[i]->earliest_presentation_timescale,
                                       AV_ROUND_UP);
        latest_start = FFMAX(start, latest_start);
    }
    fprintf(out, "\t<Period start=\"");
    write_time(out, latest_start, 3, AV_ROUND_UP);
    fprintf(out, "\">\n");


    for (set = 0; set < nb_sets; set++) {
        if (nb_tracks[set] == 0)
            continue;
        fprintf(out, "\t\t<AdaptationSet segmentAlignment=\"true\">\n");
        if (nb_sets == 1) {
            for (i = 0; i < nb_tracks[set]; i++) {
                struct Track *track = adaptation_sets[set][i];
                if (strcmp(track->name, adaptation_sets[set][0]->name))
                    break;
                fprintf(out, "\t\t\t<ContentComponent id=\"%d\" contentType=\"%s\" />\n", track->track_id, track->is_audio ? "audio" : "video");
            }
        }

        for (i = 0; i < nb_tracks[set]; ) {
            struct Track *first_track = adaptation_sets[set][i];
            int width = 0, height = 0, sample_rate = 0, channels = 0, bitrate = 0;
            fprintf(out, "\t\t\t<Representation id=\"%d\" codecs=\"", i);
            for (j = i; j < nb_tracks[set]; j++) {
                struct Track *track = adaptation_sets[set][j];
                if (strcmp(track->name, first_track->name))
                    break;
                if (track->is_audio) {
                    sample_rate = track->sample_rate;
                    channels = track->channels;
                }
                if (track->is_video) {
                    width = track->width;
                    height = track->height;
                }
                bitrate += track->bitrate;
                if (j > i)
                    fprintf(out, ",");
                fprintf(out, "%s", track->codec_str);
            }
            fprintf(out, "\" mimeType=\"%s/mp4\" bandwidth=\"%d\"",
                    width ? "video" : "audio", bitrate);
            if (width > 0 && height > 0)
                fprintf(out, " width=\"%d\" height=\"%d\"", width, height);
            if (sample_rate > 0)
                fprintf(out, " audioSamplingRate=\"%d\"", sample_rate);
            fprintf(out, ">\n");
            if (channels > 0)
                fprintf(out, "\t\t\t\t<AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"%d\" />\n", channels);
            fprintf(out, "\t\t\t\t<BaseURL>%s</BaseURL>\n", first_track->name);
            fprintf(out, "\t\t\t\t<SegmentBase indexRange=\"%"PRId64"-%"PRId64"\" />\n", first_track->sidx_start, first_track->sidx_start + first_track->sidx_length - 1);
            fprintf(out, "\t\t\t</Representation>\n");
            i = j;
        }
        fprintf(out, "\t\t</AdaptationSet>\n");
    }
    fprintf(out, "\t</Period>\n");
    fprintf(out, "</MPD>\n");

    fclose(out);
err:
    for (i = 0; i < 2; i++)
        av_free(adaptation_sets_buf[i]);
    return ret;
}

static void clean_tracks(struct Tracks *tracks)
{
    int i;
    for (i = 0; i < tracks->nb_tracks; i++) {
        av_freep(&tracks->tracks[i]);
    }
    av_freep(&tracks->tracks);
    tracks->nb_tracks = 0;
}

int main(int argc, char **argv)
{
    const char *out = NULL;
    struct Tracks tracks = { 0 };
    int i;

    av_register_all();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-out")) {
            out = argv[i + 1];
            i++;
        } else if (argv[i][0] == '-') {
            return usage(argv[0], 1);
        } else {
            if (handle_file(&tracks, argv[i]))
                return 1;
        }
    }
    if (!tracks.nb_tracks || !out)
        return usage(argv[0], 1);

    output_mpd(&tracks, out);

    clean_tracks(&tracks);

    return 0;
}
