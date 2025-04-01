/*
 * Copyright (c) 2012 Martin Storsjo
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

/*
 * To create a simple file for smooth streaming:
 * ffmpeg <normal input/transcoding options> -movflags frag_keyframe foo.ismv
 * ismindex -n foo foo.ismv
 * This step creates foo.ism and foo.ismc that is required by IIS for
 * serving it.
 *
 * With -ismf, it also creates foo.ismf, which maps fragment names to
 * start-end offsets in the ismv, for use in your own streaming server.
 *
 * By adding -path-prefix path/, the produced foo.ism will refer to the
 * files foo.ismv as "path/foo.ismv" - the prefix for the generated ismc
 * file can be set with the -ismc-prefix option similarly.
 *
 * To pre-split files for serving as static files by a web server without
 * any extra server support, create the ismv file as above, and split it:
 * ismindex -split foo.ismv
 * This step creates a file Manifest and directories QualityLevel(...),
 * that can be read directly by a smooth streaming player.
 *
 * The -output dir option can be used to request that output files
 * (both .ism/.ismc, or Manifest/QualityLevels* when splitting)
 * should be written to this directory instead of in the current directory.
 * (The directory itself isn't created if it doesn't already exist.)
 */

#include <stdio.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavformat/isom.h"
#include "libavformat/os_support.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mathematics.h"
#include "libavutil/mem.h"

static int usage(const char *argv0, int ret)
{
    fprintf(stderr, "%s [-split] [-ismf] [-n basename] [-path-prefix prefix] "
                    "[-ismc-prefix prefix] [-output dir] file1 [file2] ...\n", argv0);
    return ret;
}

struct MoofOffset {
    int64_t time;
    int64_t offset;
    int64_t duration;
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

static int expect_tag(int32_t got_tag, int32_t expected_tag) {
    if (got_tag != expected_tag) {
        char got_tag_str[4], expected_tag_str[4];
        AV_WB32(got_tag_str, got_tag);
        AV_WB32(expected_tag_str, expected_tag);
        fprintf(stderr, "wanted tag %.4s, got %.4s\n", expected_tag_str,
                got_tag_str);
        return -1;
    }
    return 0;
}

static int copy_tag(AVIOContext *in, AVIOContext *out, int32_t tag_name)
{
    int32_t size, tag;

    size = avio_rb32(in);
    tag  = avio_rb32(in);
    avio_wb32(out, size);
    avio_wb32(out, tag);
    if (expect_tag(tag, tag_name) != 0)
        return -1;
    size -= 8;
    while (size > 0) {
        char buf[1024];
        int len = FFMIN(sizeof(buf), size);
        int got;
        if ((got = avio_read(in, buf, len)) != len) {
            fprintf(stderr, "short read, wanted %d, got %d\n", len, got);
            break;
        }
        avio_write(out, buf, len);
        size -= len;
    }
    return 0;
}

static int skip_tag(AVIOContext *in, int32_t tag_name)
{
    int64_t pos = avio_tell(in);
    int32_t size, tag;

    size = avio_rb32(in);
    tag  = avio_rb32(in);
    if (expect_tag(tag, tag_name) != 0)
        return -1;
    avio_seek(in, pos + size, SEEK_SET);
    return 0;
}

static int write_fragment(const char *filename, AVIOContext *in)
{
    AVIOContext *out = NULL;
    int ret;

    if ((ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, NULL, NULL)) < 0) {
        fprintf(stderr, "Unable to open %s: %s\n", filename, av_err2str(ret));
        return ret;
    }
    ret = copy_tag(in, out, MKBETAG('m', 'o', 'o', 'f'));
    if (!ret)
        ret = copy_tag(in, out, MKBETAG('m', 'd', 'a', 't'));

    avio_flush(out);
    avio_close(out);

    return ret;
}

static int skip_fragment(AVIOContext *in)
{
    int ret;
    ret = skip_tag(in, MKBETAG('m', 'o', 'o', 'f'));
    if (!ret)
        ret = skip_tag(in, MKBETAG('m', 'd', 'a', 't'));
    return ret;
}

static int write_fragments(struct Tracks *tracks, int start_index,
                           AVIOContext *in, const char *basename,
                           int split, int ismf, const char* output_prefix)
{
    char dirname[2048], filename[2048], idxname[2048];
    int i, j, ret = 0, fragment_ret;
    FILE* out = NULL;

    if (ismf) {
        snprintf(idxname, sizeof(idxname), "%s%s.ismf", output_prefix, basename);
        out = fopen(idxname, "w");
        if (!out) {
            ret = AVERROR(errno);
            perror(idxname);
            goto fail;
        }
    }
    for (i = start_index; i < tracks->nb_tracks; i++) {
        struct Track *track = tracks->tracks[i];
        const char *type    = track->is_video ? "video" : "audio";
        snprintf(dirname, sizeof(dirname), "%sQualityLevels(%d)", output_prefix, track->bitrate);
        if (split) {
            if (mkdir(dirname, 0777) == -1 && errno != EEXIST) {
                ret = AVERROR(errno);
                perror(dirname);
                goto fail;
            }
        }
        for (j = 0; j < track->chunks; j++) {
            snprintf(filename, sizeof(filename), "%s/Fragments(%s=%"PRId64")",
                     dirname, type, track->offsets[j].time);
            avio_seek(in, track->offsets[j].offset, SEEK_SET);
            if (ismf)
                fprintf(out, "%s %"PRId64, filename, avio_tell(in));
            if (split)
                fragment_ret = write_fragment(filename, in);
            else
                fragment_ret = skip_fragment(in);
            if (ismf)
                fprintf(out, " %"PRId64"\n", avio_tell(in));
            if (fragment_ret != 0) {
                fprintf(stderr, "failed fragment %d in track %d (%s)\n", j,
                        track->track_id, track->name);
                ret = fragment_ret;
            }
        }
    }
fail:
    if (out)
        fclose(out);
    return ret;
}

static int64_t read_trun_duration(AVIOContext *in, int default_duration,
                                  int64_t end)
{
    int64_t dts = 0;
    int64_t pos;
    int flags, i;
    int entries;
    int64_t first_pts = 0;
    int64_t max_pts = 0;
    avio_r8(in); /* version */
    flags = avio_rb24(in);
    if (default_duration <= 0 && !(flags & MOV_TRUN_SAMPLE_DURATION)) {
        fprintf(stderr, "No sample duration in trun flags\n");
        return -1;
    }
    entries = avio_rb32(in);

    if (flags & MOV_TRUN_DATA_OFFSET)        avio_rb32(in);
    if (flags & MOV_TRUN_FIRST_SAMPLE_FLAGS) avio_rb32(in);

    pos = avio_tell(in);
    for (i = 0; i < entries && pos < end; i++) {
        int sample_duration = default_duration;
        int64_t pts = dts;
        if (flags & MOV_TRUN_SAMPLE_DURATION) sample_duration = avio_rb32(in);
        if (flags & MOV_TRUN_SAMPLE_SIZE)     avio_rb32(in);
        if (flags & MOV_TRUN_SAMPLE_FLAGS)    avio_rb32(in);
        if (flags & MOV_TRUN_SAMPLE_CTS)      pts += avio_rb32(in);
        if (sample_duration < 0) {
            fprintf(stderr, "Negative sample duration %d\n", sample_duration);
            return -1;
        }
        if (i == 0)
            first_pts = pts;
        max_pts = FFMAX(max_pts, pts + sample_duration);
        dts += sample_duration;
        pos = avio_tell(in);
    }

    return max_pts - first_pts;
}

static int64_t read_moof_duration(AVIOContext *in, int64_t offset)
{
    int64_t ret = -1;
    int32_t moof_size, size, tag;
    int64_t pos = 0;
    int default_duration = 0;

    avio_seek(in, offset, SEEK_SET);
    moof_size = avio_rb32(in);
    tag  = avio_rb32(in);
    if (expect_tag(tag, MKBETAG('m', 'o', 'o', 'f')) != 0)
        goto fail;
    while (pos < offset + moof_size) {
        pos = avio_tell(in);
        size = avio_rb32(in);
        tag  = avio_rb32(in);
        if (tag == MKBETAG('t', 'r', 'a', 'f')) {
            int64_t traf_pos = pos;
            int64_t traf_size = size;
            while (pos < traf_pos + traf_size) {
                pos = avio_tell(in);
                size = avio_rb32(in);
                tag  = avio_rb32(in);
                if (tag == MKBETAG('t', 'f', 'h', 'd')) {
                    int flags = 0;
                    avio_r8(in); /* version */
                    flags = avio_rb24(in);
                    avio_rb32(in); /* track_id */
                    if (flags & MOV_TFHD_BASE_DATA_OFFSET)
                        avio_rb64(in);
                    if (flags & MOV_TFHD_STSD_ID)
                        avio_rb32(in);
                    if (flags & MOV_TFHD_DEFAULT_DURATION)
                        default_duration = avio_rb32(in);
                }
                if (tag == MKBETAG('t', 'r', 'u', 'n')) {
                    return read_trun_duration(in, default_duration,
                                              pos + size);
                }
                avio_seek(in, pos + size, SEEK_SET);
            }
            fprintf(stderr, "Couldn't find trun\n");
            goto fail;
        }
        avio_seek(in, pos + size, SEEK_SET);
    }
    fprintf(stderr, "Couldn't find traf\n");

fail:
    return ret;
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
    track->offsets = av_calloc(track->chunks, sizeof(*track->offsets));
    if (!track->offsets) {
        track->chunks = 0;
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    // The duration here is always the difference between consecutive
    // start times.
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
    if (track->chunks > 0) {
        track->offsets[track->chunks - 1].duration = track->offsets[0].time +
                                                     track->duration -
                                                     track->offsets[track->chunks - 1].time;
    }
    // Now try to read the actual durations from the trun sample data.
    for (i = 0; i < track->chunks; i++) {
        int64_t duration = read_moof_duration(f, track->offsets[i].offset);
        if (duration > 0 && llabs(duration - track->offsets[i].duration) > 3) {
            // 3 allows for integer duration to drift a few units,
            // e.g., for 1/3 durations
            track->offsets[i].duration = duration;
        }
    }
    if (track->chunks > 0) {
        if (track->offsets[track->chunks - 1].duration <= 0) {
            fprintf(stderr, "Calculated last chunk duration for track %d "
                    "was non-positive (%"PRId64"), probably due to missing "
                    "fragments ", track->track_id,
                    track->offsets[track->chunks - 1].duration);
            if (track->chunks > 1) {
                track->offsets[track->chunks - 1].duration =
                    track->offsets[track->chunks - 2].duration;
            } else {
                track->offsets[track->chunks - 1].duration = 1;
            }
            fprintf(stderr, "corrected to %"PRId64"\n",
                    track->offsets[track->chunks - 1].duration);
            track->duration = track->offsets[track->chunks - 1].time +
                              track->offsets[track->chunks - 1].duration -
                              track->offsets[0].time;
            fprintf(stderr, "Track duration corrected to %"PRId64"\n",
                    track->duration);
        }
    }
    ret = 0;

fail:
    avio_seek(f, pos + size, SEEK_SET);
    return ret;
}

static int read_mfra(struct Tracks *tracks, int start_index,
                     const char *file, int split, int ismf,
                     const char *basename, const char* output_prefix)
{
    int err = 0;
    const char* err_str = "";
    AVIOContext *f = NULL;
    int32_t mfra_size;

    if ((err = avio_open2(&f, file, AVIO_FLAG_READ, NULL, NULL)) < 0)
        goto fail;
    avio_seek(f, avio_size(f) - 4, SEEK_SET);
    mfra_size = avio_rb32(f);
    avio_seek(f, -mfra_size, SEEK_CUR);
    if (avio_rb32(f) != mfra_size) {
        err = AVERROR_INVALIDDATA;
        err_str = "mfra size mismatch";
        goto fail;
    }
    if (avio_rb32(f) != MKBETAG('m', 'f', 'r', 'a')) {
        err = AVERROR_INVALIDDATA;
        err_str = "mfra tag mismatch";
        goto fail;
    }
    while (!read_tfra(tracks, start_index, f)) {
        /* Empty */
    }

    if (split || ismf)
        err = write_fragments(tracks, start_index, f, basename, split, ismf,
                              output_prefix);
    err_str = "error in write_fragments";

fail:
    if (f)
        avio_close(f);
    if (err)
        fprintf(stderr, "Unable to read the MFRA atom in %s (%s)\n", file, err_str);
    return err;
}

static int get_private_data(struct Track *track, AVCodecParameters *codecpar)
{
    track->codec_private_size = 0;
    track->codec_private      = av_mallocz(codecpar->extradata_size);
    if (!track->codec_private)
        return AVERROR(ENOMEM);
    track->codec_private_size = codecpar->extradata_size;
    memcpy(track->codec_private, codecpar->extradata, codecpar->extradata_size);
    return 0;
}

static int get_video_private_data(struct Track *track, AVCodecParameters *codecpar)
{
    AVIOContext *io = NULL;
    uint16_t sps_size, pps_size;
    int err;

    if (codecpar->codec_id == AV_CODEC_ID_VC1)
        return get_private_data(track, codecpar);

    if ((err = avio_open_dyn_buf(&io)) < 0)
        goto fail;
    err = AVERROR(EINVAL);
    if (codecpar->extradata_size < 11 || codecpar->extradata[0] != 1)
        goto fail;
    sps_size = AV_RB16(&codecpar->extradata[6]);
    if (11 + sps_size > codecpar->extradata_size)
        goto fail;
    avio_wb32(io, 0x00000001);
    avio_write(io, &codecpar->extradata[8], sps_size);
    pps_size = AV_RB16(&codecpar->extradata[9 + sps_size]);
    if (11 + sps_size + pps_size > codecpar->extradata_size)
        goto fail;
    avio_wb32(io, 0x00000001);
    avio_write(io, &codecpar->extradata[11 + sps_size], pps_size);
    err = 0;

fail:
    track->codec_private_size = avio_close_dyn_buf(io, &track->codec_private);
    return err;
}

static int handle_file(struct Tracks *tracks, const char *file, int split,
                       int ismf, const char *basename,
                       const char* output_prefix)
{
    AVFormatContext *ctx = NULL;
    int err = 0, i, orig_tracks = tracks->nb_tracks;
    char *ptr;
    struct Track *track;

    err = avformat_open_input(&ctx, file, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "Unable to open %s: %s\n", file, av_err2str(err));
        return 1;
    }

    err = avformat_find_stream_info(ctx, NULL);
    if (err < 0) {
        fprintf(stderr, "Unable to identify %s: %s\n", file, av_err2str(err));
        goto fail;
    }

    if (ctx->nb_streams < 1) {
        fprintf(stderr, "No streams found in %s\n", file);
        goto fail;
    }

    for (i = 0; i < ctx->nb_streams; i++) {
        struct Track **temp;
        AVStream *st = ctx->streams[i];

        if (st->codecpar->bit_rate == 0) {
            fprintf(stderr, "Skipping track %d in %s as it has zero bitrate\n",
                    st->id, file);
            continue;
        }

        track = av_mallocz(sizeof(*track));
        if (!track) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        temp = av_realloc_array(tracks->tracks,
                                tracks->nb_tracks + 1,
                                sizeof(*tracks->tracks));
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

        track->bitrate   = st->codecpar->bit_rate;
        track->track_id  = st->id;
        track->timescale = st->time_base.den;
        track->duration  = st->duration;
        track->is_audio  = st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
        track->is_video  = st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO;

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
            if (tracks->audio_track < 0)
                tracks->audio_track = tracks->nb_tracks;
            tracks->nb_audio_tracks++;
            track->channels    = st->codecpar->ch_layout.nb_channels;
            track->sample_rate = st->codecpar->sample_rate;
            if (st->codecpar->codec_id == AV_CODEC_ID_AAC) {
                track->fourcc    = "AACL";
                track->tag       = 255;
                track->blocksize = 4;
            } else if (st->codecpar->codec_id == AV_CODEC_ID_WMAPRO) {
                track->fourcc    = "WMAP";
                track->tag       = st->codecpar->codec_tag;
                track->blocksize = st->codecpar->block_align;
            }
            get_private_data(track, st->codecpar);
        }
        if (track->is_video) {
            if (tracks->video_track < 0)
                tracks->video_track = tracks->nb_tracks;
            tracks->nb_video_tracks++;
            track->width  = st->codecpar->width;
            track->height = st->codecpar->height;
            if (st->codecpar->codec_id == AV_CODEC_ID_H264)
                track->fourcc = "H264";
            else if (st->codecpar->codec_id == AV_CODEC_ID_VC1)
                track->fourcc = "WVC1";
            get_video_private_data(track, st->codecpar);
        }

        tracks->nb_tracks++;
    }

    avformat_close_input(&ctx);

    err = read_mfra(tracks, orig_tracks, file, split, ismf, basename,
                    output_prefix);

fail:
    if (ctx)
        avformat_close_input(&ctx);
    return err;
}

static void output_server_manifest(struct Tracks *tracks, const char *basename,
                                   const char *output_prefix,
                                   const char *path_prefix,
                                   const char *ismc_prefix)
{
    char filename[1000];
    FILE *out;
    int i;

    snprintf(filename, sizeof(filename), "%s%s.ism", output_prefix, basename);
    out = fopen(filename, "w");
    if (!out) {
        perror(filename);
        return;
    }
    fprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
    fprintf(out, "<smil xmlns=\"http://www.w3.org/2001/SMIL20/Language\">\n");
    fprintf(out, "\t<head>\n");
    fprintf(out, "\t\t<meta name=\"clientManifestRelativePath\" "
                 "content=\"%s%s.ismc\" />\n", ismc_prefix, basename);
    fprintf(out, "\t</head>\n");
    fprintf(out, "\t<body>\n");
    fprintf(out, "\t\t<switch>\n");
    for (i = 0; i < tracks->nb_tracks; i++) {
        struct Track *track = tracks->tracks[i];
        const char *type    = track->is_video ? "video" : "audio";
        fprintf(out, "\t\t\t<%s src=\"%s%s\" systemBitrate=\"%d\">\n",
                type, path_prefix, track->name, track->bitrate);
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
    int64_t pos = 0;
    struct Track *track = tracks->tracks[main];
    int should_print_time_mismatch = 1;

    for (i = 0; i < track->chunks; i++) {
        for (j = main + 1; j < tracks->nb_tracks; j++) {
            if (tracks->tracks[j]->is_audio == track->is_audio) {
                if (track->offsets[i].duration != tracks->tracks[j]->offsets[i].duration) {
                    fprintf(stderr, "Mismatched duration of %s chunk %d in %s (%d) and %s (%d)\n",
                            type, i, track->name, main, tracks->tracks[j]->name, j);
                    should_print_time_mismatch = 1;
                }
                if (track->offsets[i].time != tracks->tracks[j]->offsets[i].time) {
                    if (should_print_time_mismatch)
                        fprintf(stderr, "Mismatched (start) time of %s chunk %d in %s (%d) and %s (%d)\n",
                                type, i, track->name, main, tracks->tracks[j]->name, j);
                    should_print_time_mismatch = 0;
                }
            }
        }
        fprintf(out, "\t\t<c n=\"%d\" d=\"%"PRId64"\" ",
                i, track->offsets[i].duration);
        if (pos != track->offsets[i].time) {
            fprintf(out, "t=\"%"PRId64"\" ", track->offsets[i].time);
            pos = track->offsets[i].time;
        }
        pos += track->offsets[i].duration;
        fprintf(out, "/>\n");
    }
}

static void output_client_manifest(struct Tracks *tracks, const char *basename,
                                   const char *output_prefix, int split)
{
    char filename[1000];
    FILE *out;
    int i, j;

    if (split)
        snprintf(filename, sizeof(filename), "%sManifest", output_prefix);
    else
        snprintf(filename, sizeof(filename), "%s%s.ismc", output_prefix, basename);
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
                fprintf(stderr, "Mismatched number of video chunks in %s (id: %d, chunks %d) and %s (id: %d, chunks %d)\n",
                        track->name, track->track_id, track->chunks, first_track->name, first_track->track_id, first_track->chunks);
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
    const char *path_prefix = "", *ismc_prefix = "";
    const char *output_prefix = "";
    char output_prefix_buf[2048];
    int split = 0, ismf = 0, i;
    struct Tracks tracks = { 0, .video_track = -1, .audio_track = -1 };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n")) {
            basename = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "-path-prefix")) {
            path_prefix = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "-ismc-prefix")) {
            ismc_prefix = argv[i + 1];
            i++;
        } else if (!strcmp(argv[i], "-output")) {
            output_prefix = argv[i + 1];
            i++;
            if (output_prefix[strlen(output_prefix) - 1] != '/') {
                snprintf(output_prefix_buf, sizeof(output_prefix_buf),
                         "%s/", output_prefix);
                output_prefix = output_prefix_buf;
            }
        } else if (!strcmp(argv[i], "-split")) {
            split = 1;
        } else if (!strcmp(argv[i], "-ismf")) {
            ismf = 1;
        } else if (argv[i][0] == '-') {
            return usage(argv[0], 1);
        } else {
            if (!basename)
                ismf = 0;
            if (handle_file(&tracks, argv[i], split, ismf,
                            basename, output_prefix))
                return 1;
        }
    }
    if (!tracks.nb_tracks || (!basename && !split))
        return usage(argv[0], 1);

    if (!split)
        output_server_manifest(&tracks, basename, output_prefix,
                               path_prefix, ismc_prefix);
    output_client_manifest(&tracks, basename, output_prefix, split);

    clean_tracks(&tracks);

    return 0;
}
