/*
 * Copyright (c) 2012 Martin Storsjo
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * To create a simple file for smooth streaming:
 * ffmpeg <normal input/transcoding options> -movflags frag_keyframe foo.ismv
 * ismindex -n foo foo.ismv
 * This step creates foo.ism and foo.ismc that is required by IIS for
 * serving it.
 *
 * To pre-split files for serving as static files by a web server without
 * any extra server support, create the ismv file as above, and split it:
 * ismindex -split foo.ismv
 * This step creates a file Manifest and directories QualityLevel(...),
 * that can be read directly by a smooth streaming player.
 */

#include <stdio.h>
#include <string.h>

#include "cmdutils.h"

#include "libavformat/avformat.h"
#include "libavformat/os_support.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"

static int usage(const char *argv0, int ret)
{
    fprintf(stderr, "%s [-split] [-n basename] file1 [file2] ...\n", argv0);
    return ret;
}

struct MoofOffset {
    int64_t time;
    int64_t offset;
    int duration;
};

struct Track {
    const char *name;
    int64_t duration;
    int bitrate;
    int track_id;
    int is_audio, is_video;
    int width, height;
    int chunks;
    int sample_rate, channels;
    uint8_t *codec_private;
    int codec_private_size;
    struct MoofOffset *offsets;
    int timescale;
    const char *fourcc;
    int blocksize;
    int tag;
};

struct Tracks {
    int nb_tracks;
    int64_t duration;
    struct Track **tracks;
    int video_track, audio_track;
    int nb_video_tracks, nb_audio_tracks;
};

static int copy_tag(AVIOContext *in, AVIOContext *out, int32_t tag_name)
{
    int32_t size, tag;

    size = avio_rb32(in);
    tag  = avio_rb32(in);
    avio_wb32(out, size);
    avio_wb32(out, tag);
    if (tag != tag_name)
        return -1;
    size -= 8;
    while (size > 0) {
        char buf[1024];
        int len = FFMIN(sizeof(buf), size);
        if (avio_read(in, buf, len) != len)
            break;
        avio_write(out, buf, len);
        size -= len;
    }
    return 0;
}

static int write_fragment(const char *filename, AVIOContext *in)
{
    AVIOContext *out = NULL;
    int ret;

    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, NULL, NULL)) < 0)
        return ret;
    copy_tag(in, out, MKBETAG('m', 'o', 'o', 'f'));
    copy_tag(in, out, MKBETAG('m', 'd', 'a', 't'));

    avio_flush(out);
    avio_close(out);

    return ret;
}

static int write_fragments(struct Tracks *tracks, int start_index,
                           AVIOContext *in)
{
    char dirname[100], filename[500];
    int i, j;

    for (i = start_index; i < tracks->nb_tracks; i++) {
        struct Track *track = tracks->tracks[i];
        const char *type    = track->is_video ? "video" : "audio";
        snprintf(dirname, sizeof(dirname), "QualityLevels(%d)", track->bitrate);
        if (mkdir(dirname, 0777) == -1)
            return AVERROR(errno);
        for (j = 0; j < track->chunks; j++) {
            snprintf(filename, sizeof(filename), "%s/Fragments(%s=%"PRId64")",
                     dirname, type, track->offsets[j].time);
            avio_seek(in, track->offsets[j].offset, SEEK_SET);
            write_fragment(filename, in);
        }
    }
    return 0;
}

static int read_tfra(struct Tracks *tracks, int start_index, AVIOContext *f)
{
    int ret = AVERROR_EOF, track_id;
    int version, fieldlength, i, j;
    int64_t pos   = avio_tell(f);
    uint32_t size = avio_rb32(f);
    struct Track *track = NULL;

    if (avio_rb32(f) != MKBETAG('t', 'f', 'r', 'a'))
        goto fail;
    version = avio_r8(f);
    avio_rb24(f);
    track_id = avio_rb32(f); /* track id */
    for (i = start_index; i < tracks->nb_tracks && !track; i++)
        if (tracks->tracks[i]->track_id == track_id)
            track = tracks->tracks[i];
    if (!track) {
        /* Ok, continue parsing the next atom */
        ret = 0;
        goto fail;
    }
    fieldlength = avio_rb32(f);
    track->chunks  = avio_rb32(f);
    track->offsets = av_mallocz(sizeof(*track->offsets) * track->chunks);
    if (!track->offsets) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    for (i = 0; i < track->chunks; i++) {
        if (version == 1) {
            track->offsets[i].time   = avio_rb64(f);
            track->offsets[i].offset = avio_rb64(f);
        } else {
            track->offsets[i].time   = avio_rb32(f);
            track->offsets[i].offset = avio_rb32(f);
        }
        for (j = 0; j < ((fieldlength >> 4) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 2) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 0) & 3) + 1; j++)
            avio_r8(f);
        if (i > 0)
            track->offsets[i - 1].duration = track->offsets[i].time -
                                             track->offsets[i - 1].time;
    }
    if (track->chunks > 0)
        track->offsets[track->chunks - 1].duration = track->duration -
                                                     track->offsets[track->chunks - 1].time;
    ret = 0;

fail:
    avio_seek(f, pos + size, SEEK_SET);
    return ret;
}

static int read_mfra(struct Tracks *tracks, int start_index,
                     const char *file, int split)
{
    int err = 0;
    AVIOContext *f = NULL;
    int32_t mfra_size;

    if ((err = avio_open2(&f, file, AVIO_FLAG_READ, NULL, NULL)) < 0)
        goto fail;
    avio_seek(f, avio_size(f) - 4, SEEK_SET);
    mfra_size = avio_rb32(f);
    avio_seek(f, -mfra_size, SEEK_CUR);
    if (avio_rb32(f) != mfra_size) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }
    if (avio_rb32(f) != MKBETAG('m', 'f', 'r', 'a')) {
        err = AVERROR_INVALIDDATA;
        goto fail;
    }
    while (!read_tfra(tracks, start_index, f)) {
        /* Empty */
    }

    if (split)
        err = write_fragments(tracks, start_index, f);

fail:
    if (f)
        avio_close(f);
    if (err)
        fprintf(stderr, "Unable to read the MFRA atom in %s\n", file);
    return err;
}

static int get_private_data(struct Track *track, AVCodecContext *codec)
{
    track->codec_private_size = codec->extradata_size;
    track->codec_private      = av_mallocz(codec->extradata_size);
    if (!track->codec_private)
        return AVERROR(ENOMEM);
    memcpy(track->codec_private, codec->extradata, codec->extradata_size);
    return 0;
}

static int get_video_private_data(struct Track *track, AVCodecContext *codec)
{
    AVIOContext *io = NULL;
    uint16_t sps_size, pps_size;
    int err = AVERROR(EINVAL);

    if (codec->codec_id == AV_CODEC_ID_VC1)
        return get_private_data(track, codec);

    if (avio_open_dyn_buf(&io) < 0)  {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    if (codec->extradata_size < 11 || codec->extradata[0] != 1)
        goto fail;
    sps_size = AV_RB16(&codec->extradata[6]);
    if (11 + sps_size > codec->extradata_size)
        goto fail;
    avio_wb32(io, 0x00000001);
    avio_write(io, &codec->extradata[8], sps_size);
    pps_size = AV_RB16(&codec->extradata[9 + sps_size]);
    if (11 + sps_size + pps_size > codec->extradata_size)
        goto fail;
    avio_wb32(io, 0x00000001);
    avio_write(io, &codec->extradata[11 + sps_size], pps_size);
    err = 0;

fail:
    track->codec_private_size = avio_close_dyn_buf(io, &track->codec_private);
    return err;
}

static int handle_file(struct Tracks *tracks, const char *file, int split)
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
    if (!tracks->duration)
        tracks->duration = ctx->duration;

    for (i = 0; i < ctx->nb_streams; i++) {
        struct Track **temp;
        AVStream *st = ctx->streams[i];
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
        if ((ptr = strrchr(file, '/')) != NULL)
            track->name = ptr + 1;

        track->bitrate   = st->codec->bit_rate;
        track->track_id  = st->id;
        track->timescale = st->time_base.den;
        track->duration  = av_rescale_rnd(ctx->duration, track->timescale,
                                          AV_TIME_BASE, AV_ROUND_UP);
        track->is_audio  = st->codec->codec_type == AVMEDIA_TYPE_AUDIO;
        track->is_video  = st->codec->codec_type == AVMEDIA_TYPE_VIDEO;

        if (!track->is_audio && !track->is_video) {
            fprintf(stderr,
                    "Track %d in %s is neither video nor audio, skipping\n",
                    track->track_id, file);
            av_freep(&tracks->tracks[tracks->nb_tracks]);
            continue;
        }

        if (track->is_audio) {
            if (tracks->audio_track < 0)
                tracks->audio_track = tracks->nb_tracks;
            tracks->nb_audio_tracks++;
            track->channels    = st->codec->channels;
            track->sample_rate = st->codec->sample_rate;
            if (st->codec->codec_id == AV_CODEC_ID_AAC) {
                track->fourcc    = "AACL";
                track->tag       = 255;
                track->blocksize = 4;
            } else if (st->codec->codec_id == AV_CODEC_ID_WMAPRO) {
                track->fourcc    = "WMAP";
                track->tag       = st->codec->codec_tag;
                track->blocksize = st->codec->block_align;
            }
            get_private_data(track, st->codec);
        }
        if (track->is_video) {
            if (tracks->video_track < 0)
                tracks->video_track = tracks->nb_tracks;
            tracks->nb_video_tracks++;
            track->width  = st->codec->width;
            track->height = st->codec->height;
            if (st->codec->codec_id == AV_CODEC_ID_H264)
                track->fourcc = "H264";
            else if (st->codec->codec_id == AV_CODEC_ID_VC1)
                track->fourcc = "WVC1";
            get_video_private_data(track, st->codec);
        }

        tracks->nb_tracks++;
    }

    avformat_close_input(&ctx);

    err = read_mfra(tracks, orig_tracks, file, split);

fail:
    if (ctx)
        avformat_close_input(&ctx);
    return err;
}

static void output_server_manifest(struct Tracks *tracks,
                                   const char *basename)
{
    char filename[1000];
    FILE *out;
    int i;

    snprintf(filename, sizeof(filename), "%s.ism", basename);
    out = fopen(filename, "w");
    if (!out) {
        perror(filename);
        return;
    }
    fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(out, "<smil xmlns=\"http://www.w3.org/2001/SMIL20/Language\">\n");
    fprintf(out, "\t<head>\n");
    fprintf(out, "\t\t<meta name=\"clientManifestRelativePath\" "
                 "content=\"%s.ismc\" />\n", basename);
    fprintf(out, "\t</head>\n");
    fprintf(out, "\t<body>\n");
    fprintf(out, "\t\t<switch>\n");
    for (i = 0; i < tracks->nb_tracks; i++) {
        struct Track *track = tracks->tracks[i];
        const char *type    = track->is_video ? "video" : "audio";
        fprintf(out, "\t\t\t<%s src=\"%s\" systemBitrate=\"%d\">\n",
                type, track->name, track->bitrate);
        fprintf(out, "\t\t\t\t<param name=\"trackID\" value=\"%d\" "
                     "valueType=\"data\" />\n", track->track_id);
        fprintf(out, "\t\t\t</%s>\n", type);
    }
    fprintf(out, "\t\t</switch>\n");
    fprintf(out, "\t</body>\n");
    fprintf(out, "</smil>\n");
    fclose(out);
}

static void print_track_chunks(FILE *out, struct Tracks *tracks, int main,
                               const char *type)
{
    int i, j;
    struct Track *track = tracks->tracks[main];
    for (i = 0; i < track->chunks; i++) {
        for (j = main + 1; j < tracks->nb_tracks; j++) {
            if (tracks->tracks[j]->is_audio == track->is_audio &&
                track->offsets[i].duration != tracks->tracks[j]->offsets[i].duration)
                fprintf(stderr, "Mismatched duration of %s chunk %d in %s and %s\n",
                        type, i, track->name, tracks->tracks[j]->name);
        }
        fprintf(out, "\t\t<c n=\"%d\" d=\"%d\" />\n",
                i, track->offsets[i].duration);
    }
}

static void output_client_manifest(struct Tracks *tracks,
                                   const char *basename, int split)
{
    char filename[1000];
    FILE *out;
    int i, j;

    if (split)
        snprintf(filename, sizeof(filename), "Manifest");
    else
        snprintf(filename, sizeof(filename), "%s.ismc", basename);
    out = fopen(filename, "w");
    if (!out) {
        perror(filename);
        return;
    }
    fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(out, "<SmoothStreamingMedia MajorVersion=\"2\" MinorVersion=\"0\" "
                 "Duration=\"%"PRId64 "\">\n", tracks->duration * 10);
    if (tracks->video_track >= 0) {
        struct Track *track = tracks->tracks[tracks->video_track];
        struct Track *first_track = track;
        int index = 0;
        fprintf(out,
                "\t<StreamIndex Type=\"video\" QualityLevels=\"%d\" "
                "Chunks=\"%d\" "
                "Url=\"QualityLevels({bitrate})/Fragments(video={start time})\">\n",
                tracks->nb_video_tracks, track->chunks);
        for (i = 0; i < tracks->nb_tracks; i++) {
            track = tracks->tracks[i];
            if (!track->is_video)
                continue;
            fprintf(out,
                    "\t\t<QualityLevel Index=\"%d\" Bitrate=\"%d\" "
                    "FourCC=\"%s\" MaxWidth=\"%d\" MaxHeight=\"%d\" "
                    "CodecPrivateData=\"",
                    index, track->bitrate, track->fourcc, track->width, track->height);
            for (j = 0; j < track->codec_private_size; j++)
                fprintf(out, "%02X", track->codec_private[j]);
            fprintf(out, "\" />\n");
            index++;
            if (track->chunks != first_track->chunks)
                fprintf(stderr, "Mismatched number of video chunks in %s and %s\n",
                        track->name, first_track->name);
        }
        print_track_chunks(out, tracks, tracks->video_track, "video");
        fprintf(out, "\t</StreamIndex>\n");
    }
    if (tracks->audio_track >= 0) {
        struct Track *track = tracks->tracks[tracks->audio_track];
        struct Track *first_track = track;
        int index = 0;
        fprintf(out,
                "\t<StreamIndex Type=\"audio\" QualityLevels=\"%d\" "
                "Chunks=\"%d\" "
                "Url=\"QualityLevels({bitrate})/Fragments(audio={start time})\">\n",
                tracks->nb_audio_tracks, track->chunks);
        for (i = 0; i < tracks->nb_tracks; i++) {
            track = tracks->tracks[i];
            if (!track->is_audio)
                continue;
            fprintf(out,
                    "\t\t<QualityLevel Index=\"%d\" Bitrate=\"%d\" "
                    "FourCC=\"%s\" SamplingRate=\"%d\" Channels=\"%d\" "
                    "BitsPerSample=\"16\" PacketSize=\"%d\" "
                    "AudioTag=\"%d\" CodecPrivateData=\"",
                    index, track->bitrate, track->fourcc, track->sample_rate,
                    track->channels, track->blocksize, track->tag);
            for (j = 0; j < track->codec_private_size; j++)
                fprintf(out, "%02X", track->codec_private[j]);
            fprintf(out, "\" />\n");
            index++;
            if (track->chunks != first_track->chunks)
                fprintf(stderr, "Mismatched number of audio chunks in %s and %s\n",
                        track->name, first_track->name);
        }
        print_track_chunks(out, tracks, tracks->audio_track, "audio");
        fprintf(out, "\t</StreamIndex>\n");
    }
    fprintf(out, "</SmoothStreamingMedia>\n");
    fclose(out);
}

static void clean_tracks(struct Tracks *tracks)
{
    int i;
    for (i = 0; i < tracks->nb_tracks; i++) {
        av_freep(&tracks->tracks[i]->codec_private);
        av_freep(&tracks->tracks[i]->offsets);
        av_freep(&tracks->tracks[i]);
    }
    av_freep(&tracks->tracks);
    tracks->nb_tracks = 0;
}

int main(int argc, char **argv)
{
    const char *basename = NULL;
    int split = 0, i;
    struct Tracks tracks = { 0, .video_track = -1, .audio_track = -1 };

    av_register_all();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            basename = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "-split")) {
            split = 1;
        } else if (argv[i][0] == '-') {
            return usage(argv[0], 1);
        } else {
            if (handle_file(&tracks, argv[i], split))
                return 1;
        }
    }
    if (!tracks.nb_tracks || (!basename && !split))
        return usage(argv[0], 1);

    if (!split)
        output_server_manifest(&tracks, basename);
    output_client_manifest(&tracks, basename, split);

    clean_tracks(&tracks);

    return 0;
}
