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

#include "libavformat/hls.c"

static void print_playlists(const HLSContext *c, int parse_ret)
{
    if (parse_ret < 0) {
        printf("error: %s\n", av_err2str(parse_ret));
        return;
    }
    printf("%d variant(s), %d playlist(s)\n", c->n_variants, c->n_playlists);
    for (int i = 0; i < c->n_playlists; i++) {
        const struct playlist *pls = c->playlists[i];
        printf("%s: %s, ", av_basename(pls->url), pls->finished ? "finished" : "live");
        if (pls->time_offset_flag)
            printf("start offset %"PRId64"\n", pls->start_time_offset);
        else
            printf("no start offset\n");
    }
}

/**
 * Parse a playlist, then optionally a Media Playlist into the first
 * playlist it declared, as hls_read_header() does, and print the parsed
 * playlists with their finished state and EXT-X-START offset.
 * Parsing errors are part of the output; only I/O and allocation errors
 * make the program fail.
 */
int main(int argc, char **argv)
{
    AVIOContext *top_level = NULL, *first_media = NULL;
    AVFormatContext *s = NULL;
    HLSContext *c;
    int ret, parse_ret;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <playlist> [<media playlist>]\n", argv[0]);
        return 1;
    }

    av_log_set_level(AV_LOG_QUIET);

    ret = avio_open(&top_level, argv[1], AVIO_FLAG_READ);
    if (ret < 0)
        goto end;
    if (argc > 2) {
        ret = avio_open(&first_media, argv[2], AVIO_FLAG_READ);
        if (ret < 0)
            goto end;
    }

    s = avformat_alloc_context();
    if (!s) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    s->iformat   = &ff_hls_demuxer.p;
    s->priv_data = av_mallocz(ff_hls_demuxer.priv_data_size);
    if (!s->priv_data) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    *(const AVClass **)s->priv_data = ff_hls_demuxer.p.priv_class;
    av_opt_set_defaults(s->priv_data);
    c      = s->priv_data;
    c->ctx = s;

    parse_ret = parse_playlist(c, argv[1], NULL, top_level);
    if (parse_ret >= 0 && first_media && c->n_playlists)
        parse_ret = parse_playlist(c, argv[2], c->playlists[0], first_media);
    print_playlists(c, parse_ret);

end:
    if (s && s->priv_data)
        hls_close(s);
    avformat_free_context(s);
    avio_closep(&top_level);
    avio_closep(&first_media);
    if (ret < 0) {
        fprintf(stderr, "%s\n", av_err2str(ret));
        return 1;
    }
    return 0;
}
