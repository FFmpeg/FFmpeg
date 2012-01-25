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
 * avconv <normal input/transcoding options> -movflags frag_keyframe foo.ismv
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
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define mkdir(a, b) mkdir(a)
#endif

#include "libavformat/avformat.h"
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

struct VideoFile {
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

struct VideoFiles {
    int nb_files;
    int64_t duration;
    struct VideoFile **files;
    int video_file, audio_file;
    int nb_video_files, nb_audio_files;
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

static int write_fragments(struct VideoFiles *files, int start_index,
                           AVIOContext *in)
{
    char dirname[100], filename[500];
    int i, j;

    for (i = start_index; i < files->nb_files; i++) {
        struct VideoFile *vf = files->files[i];
        const char *type     = vf->is_video ? "video" : "audio";
        snprintf(dirname, sizeof(dirname), "QualityLevels(%d)", vf->bitrate);
        mkdir(dirname, 0777);
        for (j = 0; j < vf->chunks; j++) {
            snprintf(filename, sizeof(filename), "%s/Fragments(%s=%"PRId64")",
                     dirname, type, vf->offsets[j].time);
            avio_seek(in, vf->offsets[j].offset, SEEK_SET);
            write_fragment(filename, in);
        }
    }
    return 0;
}

static int read_tfra(struct VideoFiles *files, int start_index, AVIOContext *f)
{
    int ret = AVERROR_EOF, track_id;
    int version, fieldlength, i, j;
    int64_t pos   = avio_tell(f);
    uint32_t size = avio_rb32(f);
    struct VideoFile *vf = NULL;

    if (avio_rb32(f) != MKBETAG('t', 'f', 'r', 'a'))
        goto fail;
    version = avio_r8(f);
    avio_rb24(f);
    track_id = avio_rb32(f); /* track id */
    for (i = start_index; i < files->nb_files && !vf; i++)
        if (files->files[i]->track_id == track_id)
            vf = files->files[i];
    if (!vf) {
        /* Ok, continue parsing the next atom */
        ret = 0;
        goto fail;
    }
    fieldlength = avio_rb32(f);
    vf->chunks  = avio_rb32(f);
    vf->offsets = av_mallocz(sizeof(*vf->offsets) * vf->chunks);
    if (!vf->offsets) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    for (i = 0; i < vf->chunks; i++) {
        if (version == 1) {
            vf->offsets[i].time   = avio_rb64(f);
            vf->offsets[i].offset = avio_rb64(f);
        } else {
            vf->offsets[i].time   = avio_rb32(f);
            vf->offsets[i].offset = avio_rb32(f);
        }
        for (j = 0; j < ((fieldlength >> 4) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 2) & 3) + 1; j++)
            avio_r8(f);
        for (j = 0; j < ((fieldlength >> 0) & 3) + 1; j++)
            avio_r8(f);
        if (i > 0)
            vf->offsets[i - 1].duration = vf->offsets[i].time -
                                          vf->offsets[i - 1].time;
    }
    if (vf->chunks > 0)
        vf->offsets[vf->chunks - 1].duration = vf->duration -
                                               vf->offsets[vf->chunks - 1].time;
    ret = 0;

fail:
    avio_seek(f, pos + size, SEEK_SET);
    return ret;
}

static int read_mfra(struct VideoFiles *files, int start_index,
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
    if (avio_rb32(f) != mfra_size)
        goto fail;
    if (avio_rb32(f) != MKBETAG('m', 'f', 'r', 'a'))
        goto fail;
    while (!read_tfra(files, start_index, f)) {
        /* Empty */
    }

    if (split)
        write_fragments(files, start_index, f);

fail:
    if (f)
        avio_close(f);
    return err;
}

static int get_private_data(struct VideoFile *vf, AVCodecContext *codec)
{
    vf->codec_private_size = codec->extradata_size;
    vf->codec_private      = av_mallocz(codec->extradata_size);
    if (!vf->codec_private)
        return AVERROR(ENOMEM);
    memcpy(vf->codec_private, codec->extradata, codec->extradata_size);
    return 0;
}

static int get_video_private_data(struct VideoFile *vf, AVCodecContext *codec)
{
    AVIOContext *io = NULL;
    uint16_t sps_size, pps_size;
    int err = AVERROR(EINVAL);

    if (codec->codec_id == CODEC_ID_VC1)
        return get_private_data(vf, codec);

    avio_open_dyn_buf(&io);
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
    vf->codec_private_size = avio_close_dyn_buf(io, &vf->codec_private);
    return err;
}

static int handle_file(struct VideoFiles *files, const char *file, int split)
{
    AVFormatContext *ctx = NULL;
    int err = 0, i, orig_files = files->nb_files;
    char errbuf[50], *ptr;
    struct VideoFile *vf;

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
    if (!files->duration)
        files->duration = ctx->duration;

    for (i = 0; i < ctx->nb_streams; i++) {
        AVStream *st = ctx->streams[i];
        vf = av_mallocz(sizeof(*vf));
        files->files = av_realloc(files->files,
                                  sizeof(*files->files) * (files->nb_files + 1));
        files->files[files->nb_files] = vf;

        vf->name = file;
        if ((ptr = strrchr(file, '/')) != NULL)
            vf->name = ptr + 1;

        vf->bitrate   = st->codec->bit_rate;
        vf->track_id  = st->id;
        vf->timescale = st->time_base.den;
        vf->duration  = av_rescale_rnd(ctx->duration, vf->timescale,
                                       AV_TIME_BASE, AV_ROUND_UP);
        vf->is_audio  = st->codec->codec_type == AVMEDIA_TYPE_AUDIO;
        vf->is_video  = st->codec->codec_type == AVMEDIA_TYPE_VIDEO;

        if (!vf->is_audio && !vf->is_video) {
            fprintf(stderr,
                    "Track %d in %s is neither video nor audio, skipping\n",
                    vf->track_id, file);
            av_freep(&files->files[files->nb_files]);
            continue;
        }

        if (vf->is_audio) {
            if (files->audio_file < 0)
                files->audio_file = files->nb_files;
            files->nb_audio_files++;
            vf->channels    = st->codec->channels;
            vf->sample_rate = st->codec->sample_rate;
            if (st->codec->codec_id == CODEC_ID_AAC) {
                vf->fourcc    = "AACL";
                vf->tag       = 255;
                vf->blocksize = 4;
            } else if (st->codec->codec_id == CODEC_ID_WMAPRO) {
                vf->fourcc    = "WMAP";
                vf->tag       = st->codec->codec_tag;
                vf->blocksize = st->codec->block_align;
            }
            get_private_data(vf, st->codec);
        }
        if (vf->is_video) {
            if (files->video_file < 0)
                files->video_file = files->nb_files;
            files->nb_video_files++;
            vf->width  = st->codec->width;
            vf->height = st->codec->height;
            if (st->codec->codec_id == CODEC_ID_H264)
                vf->fourcc = "H264";
            else if (st->codec->codec_id == CODEC_ID_VC1)
                vf->fourcc = "WVC1";
            get_video_private_data(vf, st->codec);
        }

        files->nb_files++;
    }

    avformat_close_input(&ctx);

    read_mfra(files, orig_files, file, split);

fail:
    if (ctx)
        avformat_close_input(&ctx);
    return err;
}

static void output_server_manifest(struct VideoFiles *files,
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
    for (i = 0; i < files->nb_files; i++) {
        struct VideoFile *vf = files->files[i];
        const char *type     = vf->is_video ? "video" : "audio";
        fprintf(out, "\t\t\t<%s src=\"%s\" systemBitrate=\"%d\">\n",
                type, vf->name, vf->bitrate);
        fprintf(out, "\t\t\t\t<param name=\"trackID\" value=\"%d\" "
                     "valueType=\"data\" />\n", vf->track_id);
        fprintf(out, "\t\t\t</%s>\n", type);
    }
    fprintf(out, "\t\t</switch>\n");
    fprintf(out, "\t</body>\n");
    fprintf(out, "</smil>\n");
    fclose(out);
}

static void output_client_manifest(struct VideoFiles *files,
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
                 "Duration=\"%"PRId64 "\">\n", files->duration * 10);
    if (files->video_file >= 0) {
        struct VideoFile *vf = files->files[files->video_file];
        int index = 0;
        fprintf(out,
                "\t<StreamIndex Type=\"video\" QualityLevels=\"%d\" "
                "Chunks=\"%d\" "
                "Url=\"QualityLevels({bitrate})/Fragments(video={start time})\">\n",
                files->nb_video_files, vf->chunks);
        for (i = 0; i < files->nb_files; i++) {
            vf = files->files[i];
            if (!vf->is_video)
                continue;
            fprintf(out,
                    "\t\t<QualityLevel Index=\"%d\" Bitrate=\"%d\" "
                    "FourCC=\"%s\" MaxWidth=\"%d\" MaxHeight=\"%d\" "
                    "CodecPrivateData=\"",
                    index, vf->bitrate, vf->fourcc, vf->width, vf->height);
            for (j = 0; j < vf->codec_private_size; j++)
                fprintf(out, "%02X", vf->codec_private[j]);
            fprintf(out, "\" />\n");
            index++;
        }
        vf = files->files[files->video_file];
        for (i = 0; i < vf->chunks; i++)
            fprintf(out, "\t\t<c n=\"%d\" d=\"%d\" />\n", i,
                    vf->offsets[i].duration);
        fprintf(out, "\t</StreamIndex>\n");
    }
    if (files->audio_file >= 0) {
        struct VideoFile *vf = files->files[files->audio_file];
        int index = 0;
        fprintf(out,
                "\t<StreamIndex Type=\"audio\" QualityLevels=\"%d\" "
                "Chunks=\"%d\" "
                "Url=\"QualityLevels({bitrate})/Fragments(audio={start time})\">\n",
                files->nb_audio_files, vf->chunks);
        for (i = 0; i < files->nb_files; i++) {
            vf = files->files[i];
            if (!vf->is_audio)
                continue;
            fprintf(out,
                    "\t\t<QualityLevel Index=\"%d\" Bitrate=\"%d\" "
                    "FourCC=\"%s\" SamplingRate=\"%d\" Channels=\"%d\" "
                    "BitsPerSample=\"16\" PacketSize=\"%d\" "
                    "AudioTag=\"%d\" CodecPrivateData=\"",
                    index, vf->bitrate, vf->fourcc, vf->sample_rate,
                    vf->channels, vf->blocksize, vf->tag);
            for (j = 0; j < vf->codec_private_size; j++)
                fprintf(out, "%02X", vf->codec_private[j]);
            fprintf(out, "\" />\n");
            index++;
        }
        vf = files->files[files->audio_file];
        for (i = 0; i < vf->chunks; i++)
            fprintf(out, "\t\t<c n=\"%d\" d=\"%d\" />\n",
                    i, vf->offsets[i].duration);
        fprintf(out, "\t</StreamIndex>\n");
    }
    fprintf(out, "</SmoothStreamingMedia>\n");
    fclose(out);
}

static void clean_files(struct VideoFiles *files)
{
    int i;
    for (i = 0; i < files->nb_files; i++) {
        av_freep(&files->files[i]->codec_private);
        av_freep(&files->files[i]->offsets);
        av_freep(&files->files[i]);
    }
    av_freep(&files->files);
    files->nb_files = 0;
}

int main(int argc, char **argv)
{
    const char *basename = NULL;
    int split = 0, i;
    struct VideoFiles vf = { 0, .video_file = -1, .audio_file = -1 };

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
            handle_file(&vf, argv[i], split);
        }
    }
    if (!vf.nb_files || (!basename && !split))
        return usage(argv[0], 1);

    if (!split)
        output_server_manifest(&vf, basename);
    output_client_manifest(&vf, basename, split);

    clean_files(&vf);

    return 0;
}
