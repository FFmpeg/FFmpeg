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

#include "libavformat/hlsenc.c"

/* Exercise URL generation without opening network connections or files. */
static int io_open(AVFormatContext *s, AVIOContext **pb, const char *url,
                   int flags, AVDictionary **options)
{
    return avio_open_dyn_buf(pb);
}

static int io_close(AVFormatContext *s, AVIOContext *pb)
{
    ffio_free_dyn_buf(&pb);
    return 0;
}

static int test_filename(const char *url, int single_file, int fmp4)
{
    AVFormatContext *s = NULL;
    AVStream *st;
    AVDictionary *options = NULL;
    HLSContext *hls;
    VariantStream *vs;
    int ret;

    ret = avformat_alloc_output_context2(&s, NULL, "hls", url);
    if (ret < 0)
        return ret;

    s->io_open   = io_open;
    s->io_close2 = io_close;
    hls = s->priv_data;

    st = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_MPEG2VIDEO;
    st->codecpar->width      = 16;
    st->codecpar->height     = 16;
    st->time_base = (AVRational) { 1, 25 };

    st = avformat_new_stream(s, NULL);
    if (!st) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id   = AV_CODEC_ID_WEBVTT;
    st->time_base = (AVRational) { 1, 1000 };

    /* Avoid filesystem renames when writing the in-memory playlists. */
    ret = av_dict_set(&options, "hls_playlist_type", "vod", 0);
    if (ret < 0)
        goto end;

    if (single_file) {
        ret = av_dict_set(&options, "hls_flags", "single_file", 0);
        if (ret < 0)
            goto end;
    }
    if (fmp4) {
        ret = av_dict_set(&options, "hls_segment_type", "fmp4", 0);
        if (ret < 0)
            goto end;
    }

    ret = avformat_write_header(s, &options);
    if (ret < 0)
        goto end;

    vs = &hls->var_streams[0];
    printf("%s (%s, %s)\n", url, fmp4 ? "fmp4" : "mpegts",
           single_file ? "single file" : "segments");
    printf("segment: %s\n", vs->avf->url);
    printf("subtitle: %s\n", vs->vtt_avf->url);
    printf("subtitle playlist: %s\n", vs->vtt_m3u8_name);
    printf("playlist: %s\n", vs->m3u8_name);
    ret = av_write_trailer(s);

end:
    av_dict_free(&options);
    avformat_free_context(s);
    return ret;
}

int main(int argc, char **argv)
{
    static const char *const urls[] = {
        "https://example.ffmpeg",
        "https://example.ffmpeg/",
        "https://user.name:password@example.ffmpeg:8443/dir.name/output",
        "https://example.ffmpeg/dir.name/output.m3u8",
        "https://example.ffmpeg/dir.name/output.m3u8?token=foo.bar",
        "https://example.ffmpeg/dir.name/output.m3u8#foo.bar",
        "https://example.ffmpeg/dir.name/output.m3u8?token=foo.bar#baz.qux",
        "dir.name/output",
        "dir.name/output.m3u8",
        "foo#bar.ts",
        "foo?bar.ts",
        "foo#bar",
        "foo?bar",
        "file:foo#bar.ts",
        "file:foo?bar.ts",
        "foo.m3u8?token=foo.bar",
        "file:foo.m3u8?token=foo.bar",
        "dir#tag/output.m3u8",
        "dir?tag/output.m3u8",
        "file:dir#tag/output.m3u8",
        "file:dir?tag/output.m3u8",
    };
    static const char *const file_urls[] = {
        "capture_1:take#2/output.m3u8",
        "capture_1:take?2/output.m3u8",
        "my video:dir#tag/output.m3u8",
        "my video:dir?tag/output.m3u8",
        "file://[dir/output.m3u8",
        "crypto:file:dir#tag/output.m3u8",
        "crypto+file:dir?tag/output.m3u8",
        "crypto:dir#tag/output.m3u8",
        "crypto:dir?tag/output.m3u8",
        "crypto:crypto+file:dir#tag/output.m3u8",
        "crypto+crypto:file:dir?tag/output.m3u8",
        "crypto:capture_1:take#2/output.m3u8",
        "crypto+file://[dir/output.m3u8",
    };
    const char *const *test_urls = urls;
    int nb_urls = FF_ARRAY_ELEMS(urls);
    int ret;

    if (argc == 2 && !strcmp(argv[1], "file")) {
        test_urls = file_urls;
        nb_urls = FF_ARRAY_ELEMS(file_urls);
    } else if (argc != 1) {
        fprintf(stderr, "Usage: %s [file]\n", argv[0]);
        return 1;
    }

    av_log_set_level(AV_LOG_ERROR);

    for (int i = 0; i < nb_urls; i++) {
        for (int single_file = 0; single_file < 2; single_file++) {
            for (int fmp4 = 0; fmp4 < 2; fmp4++) {
                ret = test_filename(test_urls[i], single_file, fmp4);
                if (ret < 0) {
                    fprintf(stderr, "%s: %s\n", test_urls[i], av_err2str(ret));
                    return 1;
                }
            }
        }
    }
    return 0;
}
