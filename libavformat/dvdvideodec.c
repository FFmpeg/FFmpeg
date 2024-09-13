/*
 * DVD-Video demuxer, powered by libdvdnav and libdvdread
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
 * See doc/demuxers.texi for a high-level overview.
 *
 * The tactical approach is as follows:
 * 1) Open the volume with dvdread
 * 2) Analyze the user-requested title and PGC coordinates in the IFO structures
 * 3) Request playback at the coordinates and chosen angle with dvdnav
 * 5) Begin the playback (reading and demuxing) of MPEG-PS blocks
 * 6) End playback if navigation goes backwards, to a menu, or a different PGC or angle
 * 7) Close the dvdnav VM, and free dvdread's IFO structures
 */

#include <inttypes.h>

#include <dvdnav/dvdnav.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
#include <dvdread/ifo_types.h>
#include <dvdread/nav_read.h>

#include "libavutil/avstring.h"
#include "libavutil/avutil.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libavutil/time.h"
#include "libavutil/timestamp.h"

#include "avformat.h"
#include "avio_internal.h"
#include "avlanguage.h"
#include "demux.h"
#include "dvdclut.h"
#include "internal.h"
#include "url.h"

#define DVDVIDEO_MAX_PS_SEARCH_BLOCKS                   128
#define DVDVIDEO_BLOCK_SIZE                             2048
#define DVDVIDEO_TIME_BASE_Q                            (AVRational) { 1, 90000 }
#define DVDVIDEO_PTS_WRAP_BITS                          64 /* VOBUs use 32 (PES allows 33) */
#define DVDVIDEO_LIBDVDX_LOG_BUFFER_SIZE                1024

#define PCI_START_BYTE                                  45 /* complement dvdread's DSI_START_BYTE */
static const uint8_t dvdvideo_nav_header[4] =           { 0x00, 0x00, 0x01, 0xBF };

enum DVDVideoSubpictureViewport {
    DVDVIDEO_SUBP_VIEWPORT_FULLSCREEN,
    DVDVIDEO_SUBP_VIEWPORT_WIDESCREEN,
    DVDVIDEO_SUBP_VIEWPORT_LETTERBOX,
    DVDVIDEO_SUBP_VIEWPORT_PANSCAN
};
static const char dvdvideo_subp_viewport_labels[4][13] = {
    "Fullscreen", "Widescreen", "Letterbox", "Pan and Scan"
};

typedef struct DVDVideoVTSVideoStreamEntry {
    int                                 startcode;
    enum AVCodecID                      codec_id;
    int                                 width;
    int                                 height;
    AVRational                          dar;
    AVRational                          framerate;
    int                                 has_cc;
} DVDVideoVTSVideoStreamEntry;

typedef struct DVDVideoPGCAudioStreamEntry {
    int                                 startcode;
    enum AVCodecID                      codec_id;
    int                                 sample_fmt;
    int                                 sample_rate;
    int                                 bit_depth;
    int                                 nb_channels;
    AVChannelLayout                     ch_layout;
    int                                 disposition;
    const char                          *lang_iso;
} DVDVideoPGCAudioStreamEntry;

typedef struct DVDVideoPGCSubtitleStreamEntry {
    int                                 startcode;
    enum DVDVideoSubpictureViewport     viewport;
    int                                 disposition;
    uint32_t                            clut[FF_DVDCLUT_CLUT_LEN];
    const char                          *lang_iso;
} DVDVideoPGCSubtitleStreamEntry;

typedef struct DVDVideoPlaybackState {
    int                         celln;              /* ID of the active cell */
    int                         entry_pgn;          /* ID of the PG we are starting in */
    int                         in_pgc;             /* if our navigator is in the PGC */
    int                         in_ps;              /* if our navigator is in the program stream */
    int                         in_vts;             /* if our navigator is in the VTS */
    int                         is_seeking;         /* relax navigation path while seeking */
    int64_t                     nav_pts;            /* PTS according to IFO, not frame-accurate */
    uint64_t                    pgc_duration_est;   /* estimated duration as reported by IFO */
    uint64_t                    pgc_elapsed;        /* the elapsed time of the PGC, cell-relative */
    int                         pgc_nb_pg_est;      /* number of PGs as reported by IFOs */
    int                         pgcn;               /* ID of the PGC we are playing */
    int                         pgn;                /* ID of the PG we are in now */
    int                         ptt;                /* ID of the chapter we are in now */
    int64_t                     ts_offset;          /* PTS discontinuity offset (ex. VOB change) */
    uint32_t                    vobu_duration;      /* duration of the current VOBU */
    uint32_t                    vobu_e_ptm;         /* end PTM of the current VOBU */
    int                         vtsn;               /* ID of the active VTS (video title set) */
    uint64_t                    *pgc_pg_times_est;  /* PG start times as reported by IFO */
    pgc_t                       *pgc;               /* handle to the active PGC */
    dvdnav_t                    *dvdnav;            /* handle to the dvdnav VM */

    /* the following fields are only used for menu playback */
    int                         celln_start;        /* starting cell number */
    int                         celln_end;          /* ending cell number */
    int                         sector_offset;      /* current sector relative to the current VOB */
    uint32_t                    sector_end;         /* end sector relative to the current VOBU */
    uint32_t                    vobu_next;          /* the next VOBU pointer */
    uint32_t                    vobu_remaining;     /* remaining blocks for current VOBU */
    dvd_file_t                  *vob_file;          /* handle to the menu VOB (VMG or VTS) */
} DVDVideoPlaybackState;

typedef struct DVDVideoDemuxContext {
    const AVClass               *class;

    /* options */
    int                         opt_angle;          /* the user-provided angle number (1-indexed) */
    int                         opt_chapter_end;    /* the user-provided exit PTT (0 for last) */
    int                         opt_chapter_start;  /* the user-provided entry PTT (1-indexed) */
    int                         opt_menu;           /* demux menu domain instead of title domain */
    int                         opt_menu_lu;        /* the menu language unit (logical grouping) */
    int                         opt_menu_vts;       /* the menu VTS, or 0 for VMG (main menu) */
    int                         opt_pg;             /* the user-provided PG number (1-indexed) */
    int                         opt_pgc;            /* the user-provided PGC number (1-indexed) */
    int                         opt_preindex;       /* pre-indexing mode (2-pass read) */
    int                         opt_region;         /* the user-provided region digit */
    int                         opt_title;          /* the user-provided title number (1-indexed) */
    int                         opt_trim;           /* trim padding cells at beginning */

    /* subdemux */
    AVFormatContext             *mpeg_ctx;          /* context for inner demuxer */
    uint8_t                     *mpeg_buf;          /* buffer for inner demuxer */
    FFIOContext                 mpeg_pb;            /* buffer context for inner demuxer */

    /* volume */
    dvd_reader_t                *dvdread;           /* handle to libdvdread */
    ifo_handle_t                *vmg_ifo;           /* handle to the VMG (VIDEO_TS.IFO) */
    ifo_handle_t                *vts_ifo;           /* handle to the active VTS (VTS_nn_n.IFO) */

    /* playback control */
    int64_t                     first_pts;          /* the PTS of the first video keyframe */
    int                         play_end;           /* signal EOF to the parent demuxer */
    DVDVideoPlaybackState       play_state;         /* the active playback state */
    int                         play_started;       /* signal that playback has started */
    int                         seek_warned;        /* signal that we warned about seeking limits */
    int                         segment_started;    /* signal that subdemuxer is on a segment */
} DVDVideoDemuxContext;

static void dvdvideo_libdvdread_log(void *opaque, dvd_logger_level_t level,
                                    const char *msg, va_list msg_va)
{
    AVFormatContext *s = opaque;
    char msg_buf[DVDVIDEO_LIBDVDX_LOG_BUFFER_SIZE];
    int lavu_level = AV_LOG_DEBUG;

    vsnprintf(msg_buf, sizeof(msg_buf), msg, msg_va);

    if (level == DVD_LOGGER_LEVEL_ERROR)
        lavu_level = AV_LOG_ERROR;
    else if (level == DVD_LOGGER_LEVEL_WARN)
        lavu_level = AV_LOG_WARNING;

    av_log(s, lavu_level, "libdvdread: %s\n", msg_buf);
}

static void dvdvideo_libdvdnav_log(void *opaque, dvdnav_logger_level_t level,
                                   const char *msg, va_list msg_va)
{
    AVFormatContext *s = opaque;
    char msg_buf[DVDVIDEO_LIBDVDX_LOG_BUFFER_SIZE];
    int lavu_level = AV_LOG_DEBUG;

    vsnprintf(msg_buf, sizeof(msg_buf), msg, msg_va);

    if (level == DVDNAV_LOGGER_LEVEL_ERROR)
        lavu_level = AV_LOG_ERROR;
    /* some discs have invalid language codes set for menus, which throws noisy warnings */
    else if (level == DVDNAV_LOGGER_LEVEL_WARN && !av_strstart(msg, "Language", NULL))
        lavu_level = AV_LOG_WARNING;

    av_log(s, lavu_level, "libdvdnav: %s\n", msg_buf);
}

static void dvdvideo_ifo_close(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    if (c->vts_ifo)
        ifoClose(c->vts_ifo);

    if (c->vmg_ifo)
        ifoClose(c->vmg_ifo);

    if (c->dvdread)
        DVDClose(c->dvdread);
}

static int dvdvideo_ifo_open(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    dvd_logger_cb dvdread_log_cb;
    title_info_t title_info;

    dvdread_log_cb = (dvd_logger_cb) { .pf_log = dvdvideo_libdvdread_log };
    c->dvdread = DVDOpen2(s, &dvdread_log_cb, s->url);

    if (!c->dvdread) {
        av_log(s, AV_LOG_ERROR, "Unable to open the DVD-Video structure\n");

        return AVERROR_EXTERNAL;
    }

    if (!(c->vmg_ifo = ifoOpen(c->dvdread, 0))) {
        av_log(s, AV_LOG_ERROR, "Unable to open the VMG (VIDEO_TS.IFO)\n");

        return AVERROR_EXTERNAL;
    }

    if (c->opt_menu) {
        if (c->opt_menu_vts > 0 && !(c->vts_ifo = ifoOpen(c->dvdread, c->opt_menu_vts))) {
            av_log(s, AV_LOG_ERROR, "Unable to open IFO structure for VTS %d\n", c->opt_menu_vts);

            return AVERROR_EXTERNAL;
        }

        return 0;
    }

    if (c->opt_title > c->vmg_ifo->tt_srpt->nr_of_srpts) {
        av_log(s, AV_LOG_ERROR, "Title %d not found\n", c->opt_title);

        return AVERROR_STREAM_NOT_FOUND;
    }

    title_info = c->vmg_ifo->tt_srpt->title[c->opt_title - 1];
    if (c->opt_angle > title_info.nr_of_angles) {
        av_log(s, AV_LOG_ERROR, "Angle %d not found\n", c->opt_angle);

        return AVERROR_STREAM_NOT_FOUND;
    }

    if (title_info.nr_of_ptts < 1) {
        av_log(s, AV_LOG_ERROR, "Title %d has invalid headers (no PTTs found)\n", c->opt_title);

        return AVERROR_INVALIDDATA;
    }

    if (c->opt_chapter_start > title_info.nr_of_ptts ||
       (c->opt_chapter_end > 0 && c->opt_chapter_end > title_info.nr_of_ptts)) {
        av_log(s, AV_LOG_ERROR, "Chapter (PTT) range [%d, %d] is invalid\n",
                                c->opt_chapter_start, c->opt_chapter_end);

        return AVERROR_INVALIDDATA;
    }

    if (!(c->vts_ifo = ifoOpen(c->dvdread, title_info.title_set_nr))) {
        av_log(s, AV_LOG_ERROR, "Unable to process IFO structure for VTS %d\n",
                                title_info.title_set_nr);

        return AVERROR_EXTERNAL;
    }

    if (title_info.vts_ttn < 1                                      ||
        title_info.vts_ttn > 99                                     ||
        title_info.vts_ttn > c->vts_ifo->vts_ptt_srpt->nr_of_srpts  ||
        c->vts_ifo->vtsi_mat->nr_of_vts_audio_streams > 8           ||
        c->vts_ifo->vtsi_mat->nr_of_vts_subp_streams > 32) {

        av_log(s, AV_LOG_ERROR, "Title %d has invalid headers in VTS\n", c->opt_title);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int dvdvideo_is_cell_promising(AVFormatContext *s, pgc_t *pgc, int celln)
{
    dvd_time_t cell_duration = pgc->cell_playback[celln - 1].playback_time;

    return cell_duration.second >= 1 || cell_duration.minute >= 1 || cell_duration.hour >= 1;
}

static int dvdvideo_is_pgc_promising(AVFormatContext *s, pgc_t *pgc)
{
    for (int i = 1; i <= pgc->nr_of_cells; i++)
        if (dvdvideo_is_cell_promising(s, pgc, i))
            return 1;

    return 0;
}

static void dvdvideo_menu_close(AVFormatContext *s, DVDVideoPlaybackState *state)
{
    if (state->vob_file)
        DVDCloseFile(state->vob_file);
}

static int dvdvideo_menu_open(AVFormatContext *s, DVDVideoPlaybackState *state)
{
    DVDVideoDemuxContext *c = s->priv_data;
    pgci_ut_t *pgci_ut;

    pgci_ut = c->opt_menu_vts ? c->vts_ifo->pgci_ut : c->vmg_ifo->pgci_ut;
    if (!pgci_ut) {
        av_log(s, AV_LOG_ERROR, "Invalid PGC table for menu [LU %d, PGC %d]\n",
                                c->opt_menu_lu, c->opt_pgc);

        return AVERROR_INVALIDDATA;
    }

    if (c->opt_pgc < 1                      ||
        c->opt_menu_lu < 1                  ||
        c->opt_menu_lu > pgci_ut->nr_of_lus ||
        c->opt_pgc > pgci_ut->lu[c->opt_menu_lu - 1].pgcit->nr_of_pgci_srp) {

        av_log(s, AV_LOG_ERROR, "Menu [LU %d, PGC %d] not found\n", c->opt_menu_lu, c->opt_pgc);

        return AVERROR(EINVAL);
    }

    /* make sure the PGC is valid */
    state->pgcn          = c->opt_pgc - 1;
    state->pgc           = pgci_ut->lu[c->opt_menu_lu - 1].pgcit->pgci_srp[c->opt_pgc - 1].pgc;
    if (!state->pgc || !state->pgc->program_map || !state->pgc->cell_playback) {
        av_log(s, AV_LOG_ERROR, "Invalid PGC structure for menu [LU %d, PGC %d]\n",
                                c->opt_menu_lu, c->opt_pgc);

        return AVERROR_INVALIDDATA;
    }

    /* make sure the PG is valid */
    state->entry_pgn     = c->opt_pg;
    if (state->entry_pgn < 1 || state->entry_pgn > state->pgc->nr_of_programs) {
        av_log(s, AV_LOG_ERROR, "Entry PG %d not found\n", state->entry_pgn);

        return AVERROR(EINVAL);
    }

    /* make sure the program map isn't leading us to nowhere */
    state->celln_start   = state->pgc->program_map[state->entry_pgn - 1];
    state->celln_end     = state->pgc->nr_of_cells;
    state->celln         = state->celln_start;
    if (state->celln_start > state->pgc->nr_of_cells) {
        av_log(s, AV_LOG_ERROR, "Invalid PGC structure: program map points to unknown cell\n");

        return AVERROR_INVALIDDATA;
    }

    state->sector_end    = state->pgc->cell_playback[state->celln - 1].last_sector;
    state->vobu_next     = state->pgc->cell_playback[state->celln - 1].first_sector;
    state->sector_offset = state->vobu_next;

    if (c->opt_menu_vts > 0)
        state->in_vts    = 1;

    if (!(state->vob_file = DVDOpenFile(c->dvdread, c->opt_menu_vts, DVD_READ_MENU_VOBS))) {
        av_log(s, AV_LOG_ERROR, !c->opt_menu_vts ?
                                "Unable to open main menu VOB (VIDEO_TS.VOB)\n" :
                                "Unable to open menu VOBs for VTS %d\n", c->opt_menu_vts);

        return AVERROR_EXTERNAL;
    }

    return 0;
}

static int dvdvideo_menu_next_ps_block(AVFormatContext *s, DVDVideoPlaybackState *state,
                                       uint8_t *buf, int buf_size,
                                       void (*flush_cb)(AVFormatContext *s))
{
    int64_t blocks_read                   = 0;
    uint8_t read_buf[DVDVIDEO_BLOCK_SIZE] = {0};
    pci_t pci                             = (pci_t) {0};
    dsi_t dsi                             = (dsi_t) {0};

    if (buf_size != DVDVIDEO_BLOCK_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid buffer size (expected=%d actual=%d)\n",
                                DVDVIDEO_BLOCK_SIZE, buf_size);

        return AVERROR(EINVAL);
    }

    /* we were at the end of a vobu, so now go to the next one or EOF */
    if (!state->vobu_remaining && state->in_pgc) {
        if (state->vobu_next == SRI_END_OF_CELL) {
            if (state->celln == state->celln_end && state->sector_offset > state->sector_end)
                return AVERROR_EOF;

            state->celln++;
            state->sector_offset = state->pgc->cell_playback[state->celln - 1].first_sector;
            state->sector_end    = state->pgc->cell_playback[state->celln - 1].last_sector;
        } else {
            state->sector_offset = state->vobu_next;
        }
    }

    /* continue reading the VOBU */
    av_log(s, AV_LOG_TRACE, "reading block at offset %d\n", state->sector_offset);

    blocks_read = DVDReadBlocks(state->vob_file, state->sector_offset, 1, read_buf);
    if (blocks_read != 1) {
        av_log(s, AV_LOG_ERROR, "Unable to read VOB block: offset=%d blocks_read=%" PRId64 "\n",
                                state->sector_offset, blocks_read);

        return AVERROR_INVALIDDATA;
    }

    /* we are at the start of a VOBU, so we are expecting a NAV packet */
    if (!state->vobu_remaining) {
        if (!memcmp(&read_buf[PCI_START_BYTE - 4], dvdvideo_nav_header, 4) ||
            !memcmp(&read_buf[DSI_START_BYTE - 4], dvdvideo_nav_header, 4) ||
            read_buf[PCI_START_BYTE - 1] != 0x00                           ||
            read_buf[DSI_START_BYTE - 1] != 0x01) {

            av_log(s, AV_LOG_ERROR, "Invalid NAV packet at offset %d: PCI or DSI header mismatch\n",
                                    state->sector_offset);

            return AVERROR_INVALIDDATA;
        }

        navRead_PCI(&pci, &read_buf[PCI_START_BYTE]);
        navRead_DSI(&dsi, &read_buf[DSI_START_BYTE]);

        if (!pci.pci_gi.vobu_s_ptm                          ||
            !pci.pci_gi.vobu_e_ptm                          ||
            pci.pci_gi.vobu_s_ptm > pci.pci_gi.vobu_e_ptm) {

            av_log(s, AV_LOG_ERROR, "Invalid NAV packet at offset %d: PCI header is invalid\n",
                                    state->sector_offset);

            return AVERROR_INVALIDDATA;
        }

        state->vobu_remaining    = dsi.dsi_gi.vobu_ea;
        state->vobu_next         = dsi.vobu_sri.next_vobu == SRI_END_OF_CELL ? SRI_END_OF_CELL :
                                   dsi.dsi_gi.nv_pck_lbn + (dsi.vobu_sri.next_vobu & 0x7FFFFFFF);
        state->sector_offset++;

        if (state->in_pgc) {
            if (state->vobu_e_ptm != pci.pci_gi.vobu_s_ptm) {
                if (flush_cb)
                    flush_cb(s);

                state->ts_offset += state->vobu_e_ptm - pci.pci_gi.vobu_s_ptm;
            }
        } else {
            state->in_pgc        = 1;
            state->in_ps         = 1;
        }

        state->vobu_e_ptm        = pci.pci_gi.vobu_e_ptm;

        av_log(s, AV_LOG_DEBUG, "NAV packet: sector=%d "
                                "vobu_s_ptm=%d vobu_e_ptm=%d ts_offset=%" PRId64 "\n",
                                dsi.dsi_gi.nv_pck_lbn,
                                pci.pci_gi.vobu_s_ptm, pci.pci_gi.vobu_e_ptm, state->ts_offset);

        return FFERROR_REDO;
    }

    /* we are in the middle of a VOBU, so pass on the PS packet */
    memcpy(buf, &read_buf, DVDVIDEO_BLOCK_SIZE);
    state->sector_offset++;
    state->vobu_remaining--;

    return DVDVIDEO_BLOCK_SIZE;
}

static void dvdvideo_play_close(AVFormatContext *s, DVDVideoPlaybackState *state)
{
    if (!state->dvdnav)
        return;

    /* not allocated by av_malloc() */
    if (state->pgc_pg_times_est)
        free(state->pgc_pg_times_est);

    if (dvdnav_close(state->dvdnav) != DVDNAV_STATUS_OK)
        av_log(s, AV_LOG_ERROR, "Unable to close dvdnav successfully, dvdnav error: %s\n",
                                dvdnav_err_to_string(state->dvdnav));
}

static int dvdvideo_play_open(AVFormatContext *s, DVDVideoPlaybackState *state)
{
    DVDVideoDemuxContext *c = s->priv_data;

    dvdnav_logger_cb dvdnav_log_cb;
    dvdnav_status_t dvdnav_open_status;
    int32_t disc_region_mask;
    int32_t player_region_mask;
    int cur_title, cur_pgcn, cur_pgn;
    pgc_t *pgc;

    dvdnav_log_cb = (dvdnav_logger_cb) { .pf_log = dvdvideo_libdvdnav_log };
    dvdnav_open_status = dvdnav_open2(&state->dvdnav, s, &dvdnav_log_cb, s->url);

    if (!state->dvdnav                                                          ||
        dvdnav_open_status != DVDNAV_STATUS_OK                                  ||
        dvdnav_set_readahead_flag(state->dvdnav, 0) != DVDNAV_STATUS_OK         ||
        dvdnav_set_PGC_positioning_flag(state->dvdnav, 1) != DVDNAV_STATUS_OK   ||
        dvdnav_get_region_mask(state->dvdnav, &disc_region_mask) != DVDNAV_STATUS_OK) {

        av_log(s, AV_LOG_ERROR, "Unable to open the DVD for playback\n");
        goto end_dvdnav_error;
    }

    player_region_mask = c->opt_region > 0 ? (1 << (c->opt_region - 1)) : disc_region_mask;
    if (dvdnav_set_region_mask(state->dvdnav, player_region_mask) != DVDNAV_STATUS_OK) {
        av_log(s, AV_LOG_ERROR, "Unable to set the playback region code %d\n", c->opt_region);

        goto end_dvdnav_error;
    }

    if (c->opt_pgc > 0 && c->opt_pg > 0) {
        if (dvdnav_program_play(state->dvdnav, c->opt_title, c->opt_pgc, c->opt_pg) != DVDNAV_STATUS_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to start playback at title %d, PGC %d, PG %d\n",
                                    c->opt_title, c->opt_pgc, c->opt_pg);

            goto end_dvdnav_error;
        }

        state->pgcn = c->opt_pgc;
        state->entry_pgn = c->opt_pg;
    } else {
        if (dvdnav_part_play(state->dvdnav, c->opt_title, c->opt_chapter_start) != DVDNAV_STATUS_OK ||
            dvdnav_current_title_program(state->dvdnav, &cur_title, &cur_pgcn, &cur_pgn) != DVDNAV_STATUS_OK) {

            av_log(s, AV_LOG_ERROR, "Unable to start playback at title %d, chapter (PTT) %d\n",
                                    c->opt_title, c->opt_chapter_start);
            goto end_dvdnav_error;
        }

        state->pgcn = cur_pgcn;
        state->entry_pgn = cur_pgn;
    }

    pgc = c->vts_ifo->vts_pgcit->pgci_srp[state->pgcn - 1].pgc;

    if (pgc->pg_playback_mode != 0) {
        av_log(s, AV_LOG_ERROR, "Non-sequential PGCs, such as shuffles, are not supported\n");

        return AVERROR_PATCHWELCOME;
    }

    if (c->opt_trim && !dvdvideo_is_pgc_promising(s, pgc)) {
        av_log(s, AV_LOG_ERROR, "Title %d, PGC %d looks empty (may consist of padding cells), "
                                "if you want to try anyway, disable the -trim option\n",
                                c->opt_title, state->pgcn);

        return AVERROR_INVALIDDATA;
    }

    if (dvdnav_angle_change(state->dvdnav, c->opt_angle) != DVDNAV_STATUS_OK) {
        av_log(s, AV_LOG_ERROR, "Unable to start playback at angle %d\n", c->opt_angle);

        goto end_dvdnav_error;
    }

    /* dvdnav_describe_title_chapters() performs several validations on the title structure */
    /* take advantage of this side effect to increase chances of a safe navigation path */
    state->pgc_nb_pg_est = dvdnav_describe_title_chapters(state->dvdnav, c->opt_title,
                                                          &state->pgc_pg_times_est,
                                                          &state->pgc_duration_est);

    /* dvdnav returning 0 PGs is documented as an error condition */
    if (!state->pgc_nb_pg_est) {
        av_log(s, AV_LOG_ERROR, "Unable to read chapter information for title %d\n", c->opt_title);

        goto end_dvdnav_error;
    }

    state->nav_pts = dvdnav_get_current_time(state->dvdnav);
    state->vtsn = c->vmg_ifo->tt_srpt->title[c->opt_title - 1].title_set_nr;
    state->pgc = pgc;

    return 0;

end_dvdnav_error:
    if (state->dvdnav)
        av_log(s, AV_LOG_ERROR, "dvdnav error: %s\n", dvdnav_err_to_string(state->dvdnav));
    else
        av_log(s, AV_LOG_ERROR, "dvdnav could not be initialized\n");

    return AVERROR_EXTERNAL;
}

static int dvdvideo_play_next_ps_block(AVFormatContext *s, DVDVideoPlaybackState *state,
                                       uint8_t *buf, int buf_size,
                                       int *p_nav_event,
                                       void (*flush_cb)(AVFormatContext *s))
{
    DVDVideoDemuxContext *c = s->priv_data;

    uint8_t nav_buf[DVDVIDEO_BLOCK_SIZE] = {0};
    int nav_event;
    int nav_len;

    dvdnav_vts_change_event_t *e_vts;
    dvdnav_cell_change_event_t *e_cell;
    int cur_title, cur_pgcn, cur_pgn, cur_angle, cur_title_unused, cur_ptt, cur_nb_angles;
    pci_t *e_pci;
    dsi_t *e_dsi;

    if (buf_size != DVDVIDEO_BLOCK_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid buffer size (expected=%d actual=%d)\n",
                                DVDVIDEO_BLOCK_SIZE, buf_size);

        return AVERROR(EINVAL);
    }

    for (int i = 0; i < DVDVIDEO_MAX_PS_SEARCH_BLOCKS; i++) {
        if (ff_check_interrupt(&s->interrupt_callback))
            return AVERROR_EXIT;

        if (dvdnav_get_next_block(state->dvdnav, nav_buf, &nav_event, &nav_len) != DVDNAV_STATUS_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to read next block of PGC\n");

            goto end_dvdnav_error;
        }

        /* STOP event can come at any time and should be honored */
        if (nav_event == DVDNAV_STOP)
            return AVERROR_EOF;

        if (nav_len > DVDVIDEO_BLOCK_SIZE) {
            av_log(s, AV_LOG_ERROR, "Invalid block size (expected<=%d actual=%d)\n",
                                    DVDVIDEO_BLOCK_SIZE, nav_len);

            return AVERROR_INVALIDDATA;
        }

        if (dvdnav_current_title_info(state->dvdnav, &cur_title, &cur_ptt) != DVDNAV_STATUS_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to determine current title coordinates\n");

            goto end_dvdnav_error;
        }

        /* we somehow navigated to a menu */
        if (cur_title == 0 || !dvdnav_is_domain_vts(state->dvdnav))
            return AVERROR_EOF;

        if (dvdnav_current_title_program(state->dvdnav, &cur_title_unused, &cur_pgcn, &cur_pgn) != DVDNAV_STATUS_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to determine current PGC coordinates\n");

            goto end_dvdnav_error;
        }

        /* we somehow left the PGC */
        if (state->in_pgc && cur_pgcn != state->pgcn)
            return AVERROR_EOF;

        if (dvdnav_get_angle_info(state->dvdnav, &cur_angle, &cur_nb_angles) != DVDNAV_STATUS_OK) {
            av_log(s, AV_LOG_ERROR, "Unable to determine current video angle\n");

            goto end_dvdnav_error;
        }

        av_log(s, nav_event == DVDNAV_BLOCK_OK ? AV_LOG_TRACE : AV_LOG_DEBUG,
               "new block: i=%d nav_event=%d nav_len=%d cur_title=%d "
               "cur_ptt=%d cur_angle=%d cur_celln=%d cur_pgcn=%d cur_pgn=%d "
               "play_in_vts=%d play_in_pgc=%d play_in_ps=%d\n",
               i, nav_event, nav_len, cur_title,
               cur_ptt, cur_angle, state->celln, cur_pgcn, cur_pgn,
               state->in_vts, state->in_pgc, state->in_ps);

        switch (nav_event) {
            case DVDNAV_VTS_CHANGE:
                if (state->in_vts)
                    return AVERROR_EOF;

                e_vts = (dvdnav_vts_change_event_t *) nav_buf;

                if (e_vts->new_vtsN == state->vtsn && e_vts->new_domain == DVD_DOMAIN_VTSTitle)
                    state->in_vts = 1;

                continue;
            case DVDNAV_CELL_CHANGE:
                if (!state->in_vts)
                    continue;

                e_cell = (dvdnav_cell_change_event_t *) nav_buf;

                av_log(s, AV_LOG_DEBUG, "new cell: prev=%d new=%d\n", state->celln, e_cell->cellN);

                if (!state->in_ps && !state->in_pgc) {
                    if (cur_title == c->opt_title                        &&
                        (c->opt_pgc || cur_ptt == c->opt_chapter_start)  &&
                        cur_pgcn == state->pgcn                          &&
                        cur_pgn == state->entry_pgn) {

                        state->in_pgc = 1;
                    }
                } else if (!state->is_seeking &&
                           (state->celln >= e_cell->cellN || state->pgn > cur_pgn)) {
                    return AVERROR_EOF;
                }

                state->celln = e_cell->cellN;
                state->ptt = cur_ptt;
                state->pgn = cur_pgn;

                continue;
            case DVDNAV_NAV_PACKET:
                if (!state->in_pgc)
                    continue;

                if ((!state->is_seeking && state->ptt > 0 && state->ptt > cur_ptt) ||
                    (c->opt_chapter_end > 0 && cur_ptt > c->opt_chapter_end)) {
                    return AVERROR_EOF;
                }

                e_pci = dvdnav_get_current_nav_pci(state->dvdnav);
                e_dsi = dvdnav_get_current_nav_dsi(state->dvdnav);

                if (e_pci == NULL || e_dsi == NULL ||
                    e_pci->pci_gi.vobu_s_ptm > e_pci->pci_gi.vobu_e_ptm) {

                    av_log(s, AV_LOG_ERROR, "Invalid NAV packet\n");
                    return AVERROR_INVALIDDATA;
                }

                state->vobu_duration = e_pci->pci_gi.vobu_e_ptm - e_pci->pci_gi.vobu_s_ptm;
                state->pgc_elapsed += state->vobu_duration;
                state->nav_pts = dvdnav_get_current_time(state->dvdnav);
                state->ptt = cur_ptt;
                state->pgn = cur_pgn;

                av_log(s, AV_LOG_DEBUG,
                       "NAV packet: s_ptm=%d e_ptm=%d "
                       "scr=%d lbn=%d vobu_duration=%d nav_pts=%" PRId64 "\n",
                       e_pci->pci_gi.vobu_s_ptm, e_pci->pci_gi.vobu_e_ptm,
                       e_dsi->dsi_gi.nv_pck_scr,
                       e_pci->pci_gi.nv_pck_lbn, state->vobu_duration, state->nav_pts);

                if (!state->in_ps) {
                    if (c->opt_trim && !dvdvideo_is_cell_promising(s, state->pgc, state->celln)) {
                        av_log(s, AV_LOG_INFO, "Skipping padding cell #%d\n", state->celln);

                        i = 0;
                        continue;
                    }

                    av_log(s, AV_LOG_DEBUG, "navigation: locked to program stream\n");

                    state->in_ps = 1;
                } else {
                    if (state->vobu_e_ptm != e_pci->pci_gi.vobu_s_ptm) {
                        if (flush_cb)
                            flush_cb(s);

                        state->ts_offset += state->vobu_e_ptm - e_pci->pci_gi.vobu_s_ptm;
                    }
                }

                state->vobu_e_ptm = e_pci->pci_gi.vobu_e_ptm;

                (*p_nav_event) = nav_event;

                return nav_len;
            case DVDNAV_BLOCK_OK:
                if (!state->in_ps) {
                    if (state->in_pgc)
                        i = 0; /* necessary in case we are skipping junk cells at the beginning */
                    continue;
                }

                if (nav_len != DVDVIDEO_BLOCK_SIZE) {
                    av_log(s, AV_LOG_ERROR, "Invalid MPEG block size (expected=%d actual=%d)\n",
                                            DVDVIDEO_BLOCK_SIZE, nav_len);

                    return AVERROR_INVALIDDATA;
                }

                if (cur_angle != c->opt_angle) {
                    av_log(s, AV_LOG_ERROR, "Unexpected angle change (expected=%d new=%d)\n",
                                            c->opt_angle, cur_angle);

                    return AVERROR_INPUT_CHANGED;
                }

                memcpy(buf, &nav_buf, nav_len);

                if (state->pgn != cur_pgn)
                    av_log(s, AV_LOG_WARNING, "Unexpected PG change (expected=%d actual=%d); "
                                              "this could be due to a missed NAV packet\n",
                                              state->pgn, cur_pgn);

                (*p_nav_event) = nav_event;

                state->is_seeking = 0;

                return nav_len;
            case DVDNAV_WAIT:
                if (dvdnav_wait_skip(state->dvdnav) != DVDNAV_STATUS_OK) {
                    av_log(s, AV_LOG_ERROR, "Unable to skip WAIT event\n");

                    goto end_dvdnav_error;
                }

                continue;
            case DVDNAV_STILL_FRAME:
            case DVDNAV_HOP_CHANNEL:
            case DVDNAV_HIGHLIGHT:
                if (state->in_ps)
                    return AVERROR_EOF;

                if (nav_event == DVDNAV_STILL_FRAME) {
                    if (dvdnav_still_skip(state->dvdnav) != DVDNAV_STATUS_OK) {
                        av_log(s, AV_LOG_ERROR, "Unable to skip still image\n");

                        goto end_dvdnav_error;
                    }
                }

                continue;
            default:
                continue;
        }
    }

    av_log(s, AV_LOG_ERROR, "Unable to find next program stream block\n");

    return AVERROR_INVALIDDATA;

end_dvdnav_error:
    av_log(s, AV_LOG_ERROR, "dvdnav error (title=%d pgc=%d pg=%d cell=%d): %s\n",
                            cur_title, cur_pgcn, cur_pgn, state->celln,
                            dvdnav_err_to_string(state->dvdnav));

    return AVERROR_EXTERNAL;
}

static int dvdvideo_chapters_setup_simple(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    uint64_t time_prev = 0;
    int64_t total_duration = 0;

    int chapter_start = c->opt_chapter_start;
    int chapter_end = c->opt_chapter_end > 0 ? c->opt_chapter_end : c->play_state.pgc_nb_pg_est - 1;

    /* dvdnav_describe_title_chapters() describes PGs rather than PTTs, so validate our range */
    if (c->play_state.pgc_nb_pg_est == 1            ||
        chapter_start > c->play_state.pgc_nb_pg_est ||
        chapter_end > c->play_state.pgc_nb_pg_est) {

        s->duration = av_rescale_q(c->play_state.pgc_duration_est,
                                   DVDVIDEO_TIME_BASE_Q, AV_TIME_BASE_Q);
        return 0;
    }

    for (int i = chapter_start - 1; i < chapter_end; i++) {
        uint64_t time_effective = c->play_state.pgc_pg_times_est[i] - c->play_state.nav_pts;

        if (time_effective - time_prev == 0)
            continue;

        if (chapter_start != chapter_end &&
            !avpriv_new_chapter(s, i, DVDVIDEO_TIME_BASE_Q, time_prev, time_effective, NULL)) {

            return AVERROR(ENOMEM);
        }

        time_prev = time_effective;
        total_duration = time_effective;
    }

    if (c->opt_chapter_start == 1 && c->opt_chapter_end == 0)
        s->duration = av_rescale_q(c->play_state.pgc_duration_est,
                                   DVDVIDEO_TIME_BASE_Q, AV_TIME_BASE_Q);
    else
        s->duration = av_rescale_q(total_duration,
                                   DVDVIDEO_TIME_BASE_Q, AV_TIME_BASE_Q);

    return 0;
}

static int dvdvideo_chapters_setup_preindex(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int ret = 0, interrupt = 0;
    int nb_chapters = 0, last_ptt = c->opt_chapter_start;
    uint64_t cur_chapter_offset = 0, cur_chapter_duration = 0;
    DVDVideoPlaybackState state = {0};

    uint8_t nav_buf[DVDVIDEO_BLOCK_SIZE];
    int nav_event;

    if (c->opt_chapter_start == c->opt_chapter_end)
        return ret;

    if ((ret = dvdvideo_play_open(s, &state)) < 0)
        return ret;

    if (state.pgc->nr_of_programs == 1)
        goto end_close;

    av_log(s, AV_LOG_INFO,
           "Indexing chapter markers, this will take a long time. Please wait...\n");

    while (!(interrupt = ff_check_interrupt(&s->interrupt_callback))) {
        ret = dvdvideo_play_next_ps_block(s, &state, nav_buf, DVDVIDEO_BLOCK_SIZE,
                                          &nav_event, NULL);
        if (ret < 0 && ret != AVERROR_EOF)
            goto end_close;

        if (nav_event != DVDNAV_NAV_PACKET && ret != AVERROR_EOF)
            continue;

        if (state.ptt == last_ptt) {
            cur_chapter_duration += state.vobu_duration;
            /* ensure we add the last chapter */
            if (ret != AVERROR_EOF)
                continue;
        }

        if (cur_chapter_duration > 0) {
            if (!avpriv_new_chapter(s, nb_chapters, DVDVIDEO_TIME_BASE_Q, cur_chapter_offset,
                                    cur_chapter_offset + cur_chapter_duration, NULL)) {
                ret = AVERROR(ENOMEM);
                goto end_close;
            }

            nb_chapters++;
        }

        cur_chapter_offset += cur_chapter_duration;
        cur_chapter_duration = state.vobu_duration;
        last_ptt = state.ptt;

        if (ret == AVERROR_EOF)
            break;
    }

    if (interrupt) {
        ret = AVERROR_EXIT;
        goto end_close;
    }

    if (ret < 0 && ret != AVERROR_EOF)
        goto end_close;

    s->duration = av_rescale_q(state.pgc_elapsed, DVDVIDEO_TIME_BASE_Q, AV_TIME_BASE_Q);

    av_log(s, AV_LOG_INFO, "Chapter marker indexing complete\n");
    ret = 0;

end_close:
    dvdvideo_play_close(s, &state);

    return ret;
}

static int dvdvideo_video_stream_analyze(AVFormatContext *s, video_attr_t video_attr,
                                         DVDVideoVTSVideoStreamEntry *entry)
{
    AVRational framerate;
    int height = 0;
    int width = 0;
    int is_pal = video_attr.video_format == 1;

    framerate = is_pal ? (AVRational) { 25, 1 } : (AVRational) { 30000, 1001 };
    height = is_pal ? 576 : 480;

    if (height > 0) {
        switch (video_attr.picture_size) {
            case 0: /* D1 */
                width = 720;
                break;
            case 1: /* 4CIF */
                width = 704;
                break;
            case 2: /* Half D1 */
                width = 352;
                break;
            case 3: /* CIF */
                width = 352;
                height /= 2;
                break;
        }
    }

    if (!width || !height) {
        av_log(s, AV_LOG_ERROR, "Invalid video stream parameters in the IFO headers, "
                                "this could be an authoring error or empty title "
                                "(video_format=%d picture_size=%d)\n",
                                video_attr.video_format, video_attr.picture_size);

        return AVERROR_INVALIDDATA;
    }

    entry->startcode = 0x1E0;
    entry->codec_id = !video_attr.mpeg_version ? AV_CODEC_ID_MPEG1VIDEO : AV_CODEC_ID_MPEG2VIDEO;
    entry->width = width;
    entry->height = height;
    entry->dar = video_attr.display_aspect_ratio ? (AVRational) { 16, 9 } : (AVRational) { 4, 3 };
    entry->framerate = framerate;
    entry->has_cc = !is_pal && (video_attr.line21_cc_1 || video_attr.line21_cc_2);

    return 0;
}

static int dvdvideo_video_stream_add(AVFormatContext *s,
                                     DVDVideoVTSVideoStreamEntry *entry,
                                     enum AVStreamParseType need_parsing)
{
    AVStream *st;
    FFStream *sti;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id = entry->startcode;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = entry->codec_id;
    st->codecpar->width = entry->width;
    st->codecpar->height = entry->height;
    st->codecpar->format = AV_PIX_FMT_YUV420P;
    st->codecpar->color_range = AVCOL_RANGE_MPEG;

#if FF_API_R_FRAME_RATE
    st->r_frame_rate = entry->framerate;
#endif
    st->avg_frame_rate = entry->framerate;

    sti = ffstream(st);
    sti->request_probe = 0;
    sti->need_parsing = need_parsing;
    sti->display_aspect_ratio = entry->dar;

    avpriv_set_pts_info(st, DVDVIDEO_PTS_WRAP_BITS,
                        DVDVIDEO_TIME_BASE_Q.num, DVDVIDEO_TIME_BASE_Q.den);

    return 0;
}

static int dvdvideo_video_stream_setup(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int ret;
    DVDVideoVTSVideoStreamEntry entry = {0};
    video_attr_t video_attr;

    if (c->opt_menu)
        video_attr = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->vmgm_video_attr :
                                        c->vts_ifo->vtsi_mat->vtsm_video_attr;
    else
        video_attr = c->vts_ifo->vtsi_mat->vts_video_attr;

    if ((ret = dvdvideo_video_stream_analyze(s, video_attr, &entry)) < 0 ||
        (ret = dvdvideo_video_stream_add(s, &entry, AVSTREAM_PARSE_HEADERS)) < 0) {

        av_log(s, AV_LOG_ERROR, "Unable to add video stream\n");
        return ret;
    }

    return 0;
}

static int dvdvideo_audio_stream_analyze(AVFormatContext *s, audio_attr_t audio_attr,
                                         uint16_t audio_control, DVDVideoPGCAudioStreamEntry *entry)
{
    int startcode = 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
    int sample_fmt = AV_SAMPLE_FMT_NONE;
    int sample_rate = 0;
    int bit_depth = 0;
    int nb_channels = 0;
    AVChannelLayout ch_layout = (AVChannelLayout) {0};
    char lang_dvd[3] = {0};

    int position = (audio_control & 0x7F00) >> 8;

    /* XXX(PATCHWELCOME): SDDS is not supported due to lack of sample material */
    switch (audio_attr.audio_format) {
        case 0: /* AC3 */
            codec_id = AV_CODEC_ID_AC3;
            sample_fmt = AV_SAMPLE_FMT_FLTP;
            sample_rate = 48000;
            startcode = 0x80 + position;
            break;
        case 2: /* MP1 */
            codec_id = AV_CODEC_ID_MP1;
            sample_fmt = audio_attr.quantization ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
            sample_rate = 48000;
            bit_depth = audio_attr.quantization ? 20 : 16;
            startcode = 0x1C0 + position;
            break;
        case 3: /* MP2 */
            codec_id = AV_CODEC_ID_MP2;
            sample_fmt = audio_attr.quantization ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
            sample_rate = 48000;
            bit_depth = audio_attr.quantization ? 20 : 16;
            startcode = 0x1C0 + position;
            break;
        case 4: /* DVD PCM */
            codec_id = AV_CODEC_ID_PCM_DVD;
            sample_fmt = audio_attr.quantization ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
            sample_rate = audio_attr.sample_frequency ? 96000 : 48000;
            bit_depth = audio_attr.quantization == 2 ? 24 : (audio_attr.quantization ? 20 : 16);
            startcode = 0xA0 + position;
            break;
        case 6: /* DCA */
            codec_id = AV_CODEC_ID_DTS;
            sample_fmt = AV_SAMPLE_FMT_FLTP;
            sample_rate = 48000;
            bit_depth = audio_attr.quantization == 2 ? 24 : (audio_attr.quantization ? 20 : 16);
            startcode = 0x88 + position;
            break;
    }

    nb_channels = audio_attr.channels + 1;

    if (codec_id == AV_CODEC_ID_NONE     ||
        startcode == 0                   ||
        sample_fmt == AV_SAMPLE_FMT_NONE ||
        sample_rate == 0                 ||
        nb_channels == 0) {

        av_log(s, AV_LOG_ERROR, "Invalid audio stream parameters in the IFO headers, "
                                "this could be an authoring error or dummy title "
                                "(stream position %d in IFO)\n", position);
        return AVERROR_INVALIDDATA;
    }

    if (nb_channels == 1)
        ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_MONO;
    else if (nb_channels == 2)
        ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_STEREO;
    else if (nb_channels == 6)
        ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_5POINT1;
    else if (nb_channels == 7)
        ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_6POINT1;
    else if (nb_channels == 8)
        ch_layout = (AVChannelLayout) AV_CHANNEL_LAYOUT_7POINT1;

    /* XXX(PATCHWELCOME): IFO structures have metadata on karaoke tracks for additional features */
    if (audio_attr.application_mode == 1) {
        entry->disposition |= AV_DISPOSITION_KARAOKE;

        av_log(s, AV_LOG_WARNING, "Extended karaoke metadata is not supported at this time "
                                  "(stream id=%d)\n", startcode);
    }

    if (audio_attr.code_extension == 2)
        entry->disposition |= AV_DISPOSITION_VISUAL_IMPAIRED;
    if (audio_attr.code_extension == 3 || audio_attr.code_extension == 4)
        entry->disposition |= AV_DISPOSITION_COMMENT;

    AV_WB16(lang_dvd, audio_attr.lang_code);

    entry->startcode = startcode;
    entry->codec_id = codec_id;
    entry->sample_rate = sample_rate;
    entry->bit_depth = bit_depth;
    entry->nb_channels = nb_channels;
    entry->ch_layout = ch_layout;
    entry->lang_iso = ff_convert_lang_to(lang_dvd, AV_LANG_ISO639_2_BIBL);

    return 0;
}

static int dvdvideo_audio_stream_add(AVFormatContext *s, DVDVideoPGCAudioStreamEntry *entry,
                                     enum AVStreamParseType need_parsing)
{
    AVStream *st;
    FFStream *sti;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id = entry->startcode;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = entry->codec_id;
    st->codecpar->format = entry->sample_fmt;
    st->codecpar->sample_rate = entry->sample_rate;
    st->codecpar->bits_per_coded_sample = entry->bit_depth;
    st->codecpar->bits_per_raw_sample = entry->bit_depth;
    st->codecpar->ch_layout = entry->ch_layout;
    st->codecpar->ch_layout.nb_channels = entry->nb_channels;
    st->disposition = entry->disposition;

    if (entry->lang_iso)
        av_dict_set(&st->metadata, "language", entry->lang_iso, 0);

    sti = ffstream(st);
    sti->request_probe = 0;
    sti->need_parsing = need_parsing;

    avpriv_set_pts_info(st, DVDVIDEO_PTS_WRAP_BITS,
                        DVDVIDEO_TIME_BASE_Q.num, DVDVIDEO_TIME_BASE_Q.den);

    return 0;
}

static int dvdvideo_audio_stream_add_all(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int ret;
    int nb_streams;

    if (c->opt_menu)
        nb_streams = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->nr_of_vmgm_audio_streams :
                                        c->vts_ifo->vtsi_mat->nr_of_vtsm_audio_streams;
    else
        nb_streams = c->vts_ifo->vtsi_mat->nr_of_vts_audio_streams;

    for (int i = 0; i < nb_streams; i++) {
        DVDVideoPGCAudioStreamEntry entry = {0};
        audio_attr_t audio_attr;

        if (c->opt_menu)
            audio_attr = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->vmgm_audio_attr :
                                            c->vts_ifo->vtsi_mat->vtsm_audio_attr;
        else
            audio_attr = c->vts_ifo->vtsi_mat->vts_audio_attr[i];

        if (!(c->play_state.pgc->audio_control[i] & 0x8000))
            continue;

        if ((ret = dvdvideo_audio_stream_analyze(s, audio_attr, c->play_state.pgc->audio_control[i],
                                                 &entry)) < 0)
            goto break_error;

        /* IFO structures can declare duplicate entries for the same startcode */
        for (int j = 0; j < s->nb_streams; j++)
            if (s->streams[j]->id == entry.startcode)
                continue;

        if ((ret = dvdvideo_audio_stream_add(s, &entry, AVSTREAM_PARSE_HEADERS)) < 0)
            goto break_error;

        continue;

break_error:
        av_log(s, AV_LOG_ERROR, "Unable to add audio stream at position %d\n", i);
        return ret;
    }

    return 0;
}

static int dvdvideo_subp_stream_analyze(AVFormatContext *s, uint32_t offset, subp_attr_t subp_attr,
                                        DVDVideoPGCSubtitleStreamEntry *entry)
{
    DVDVideoDemuxContext *c = s->priv_data;

    char lang_dvd[3] = {0};

    entry->startcode = 0x20 + (offset & 0x1F);

    if (subp_attr.lang_extension == 9)
        entry->disposition |= AV_DISPOSITION_FORCED;

    memcpy(&entry->clut, c->play_state.pgc->palette, FF_DVDCLUT_CLUT_SIZE);

    /* dvdsub palettes currently have no colorspace tagging and all muxers only support RGB */
    /* this is not a lossless conversion, but no use cases are supported for the original YUV */
    ff_dvdclut_yuv_to_rgb(entry->clut, FF_DVDCLUT_CLUT_SIZE);

    AV_WB16(lang_dvd, subp_attr.lang_code);
    entry->lang_iso = ff_convert_lang_to(lang_dvd, AV_LANG_ISO639_2_BIBL);

    return 0;
}

static int dvdvideo_subp_stream_add(AVFormatContext *s, DVDVideoPGCSubtitleStreamEntry *entry,
                                    enum AVStreamParseType need_parsing)
{
    AVStream *st;
    FFStream *sti;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->id = entry->startcode;
    st->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    st->codecpar->codec_id = AV_CODEC_ID_DVD_SUBTITLE;

    if ((ret = ff_dvdclut_palette_extradata_cat(entry->clut, FF_DVDCLUT_CLUT_SIZE, st->codecpar)) < 0)
        return ret;

    if (entry->lang_iso)
        av_dict_set(&st->metadata, "language", entry->lang_iso, 0);

    av_dict_set(&st->metadata, "VIEWPORT", dvdvideo_subp_viewport_labels[entry->viewport], 0);

    st->disposition = entry->disposition;

    sti = ffstream(st);
    sti->request_probe = 0;
    sti->need_parsing = need_parsing;

    avpriv_set_pts_info(st, DVDVIDEO_PTS_WRAP_BITS,
                        DVDVIDEO_TIME_BASE_Q.num, DVDVIDEO_TIME_BASE_Q.den);

    return 0;
}

static int dvdvideo_subp_stream_add_internal(AVFormatContext *s, uint32_t offset,
                                             subp_attr_t subp_attr,
                                             enum DVDVideoSubpictureViewport viewport)
{
    int ret;
    DVDVideoPGCSubtitleStreamEntry entry = {0};

    entry.viewport = viewport;

    if ((ret = dvdvideo_subp_stream_analyze(s, offset, subp_attr, &entry)) < 0)
        goto end_error;

    /* IFO structures can declare duplicate entries for the same startcode */
    for (int i = 0; i < s->nb_streams; i++)
        if (s->streams[i]->id == entry.startcode)
            return 0;

    if ((ret = dvdvideo_subp_stream_add(s, &entry, AVSTREAM_PARSE_HEADERS)) < 0)
        goto end_error;

    return 0;

end_error:
    av_log(s, AV_LOG_ERROR, "Unable to add subtitle stream\n");
    return ret;
}

static int dvdvideo_subp_stream_add_all(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int nb_streams;

    if (c->opt_menu)
        nb_streams = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->nr_of_vmgm_subp_streams :
                                        c->vts_ifo->vtsi_mat->nr_of_vtsm_subp_streams;
    else
        nb_streams = c->vts_ifo->vtsi_mat->nr_of_vts_subp_streams;


    for (int i = 0; i < nb_streams; i++) {
        int ret;
        uint32_t subp_control;
        subp_attr_t subp_attr;
        video_attr_t video_attr;

        subp_control = c->play_state.pgc->subp_control[i];
        if (!(subp_control & 0x80000000))
            continue;

        /* there can be several presentations for one SPU */
        /* the DAR check is flexible in order to support weird authoring */
        if (c->opt_menu) {
            video_attr = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->vmgm_video_attr :
                                            c->vts_ifo->vtsi_mat->vtsm_video_attr;

            subp_attr  = !c->opt_menu_vts ? c->vmg_ifo->vmgi_mat->vmgm_subp_attr :
                                            c->vts_ifo->vtsi_mat->vtsm_subp_attr;
        } else {
            video_attr = c->vts_ifo->vtsi_mat->vts_video_attr;
            subp_attr = c->vts_ifo->vtsi_mat->vts_subp_attr[i];
        }

        /* 4:3 */
        if (!video_attr.display_aspect_ratio) {
            if ((ret = dvdvideo_subp_stream_add_internal(s, subp_control >> 24, subp_attr,
                                                         DVDVIDEO_SUBP_VIEWPORT_FULLSCREEN)) < 0)
                return ret;

            continue;
        }

        /* 16:9 */
        if ((    ret = dvdvideo_subp_stream_add_internal(s, subp_control >> 16, subp_attr,
                                                         DVDVIDEO_SUBP_VIEWPORT_WIDESCREEN)) < 0)
            return ret;

        /* 16:9 letterbox */
        if (video_attr.permitted_df == 2 || video_attr.permitted_df == 0)
            if ((ret = dvdvideo_subp_stream_add_internal(s, subp_control >> 8, subp_attr,
                                                         DVDVIDEO_SUBP_VIEWPORT_LETTERBOX)) < 0)
                return ret;

        /* 16:9 pan-and-scan */
        if (video_attr.permitted_df == 1 || video_attr.permitted_df == 0)
            if ((ret = dvdvideo_subp_stream_add_internal(s, subp_control, subp_attr,
                                                         DVDVIDEO_SUBP_VIEWPORT_PANSCAN)) < 0)
                return ret;
    }

    return 0;
}

static void dvdvideo_subdemux_flush(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    if (!c->segment_started)
        return;

    av_log(s, AV_LOG_DEBUG, "flushing sub-demuxer\n");
    avio_flush(&c->mpeg_pb.pub);
    ff_read_frame_flush(c->mpeg_ctx);
    c->segment_started = 0;
}

static int dvdvideo_subdemux_read_data(void *opaque, uint8_t *buf, int buf_size)
{
    AVFormatContext *s = opaque;
    DVDVideoDemuxContext *c = s->priv_data;

    int ret = 0;
    int nav_event;

    if (c->play_end)
        return AVERROR_EOF;

    if (c->opt_menu)
        ret = dvdvideo_menu_next_ps_block(s, &c->play_state, buf, buf_size,
                                          dvdvideo_subdemux_flush);
    else
        ret = dvdvideo_play_next_ps_block(opaque, &c->play_state, buf, buf_size,
                                          &nav_event, dvdvideo_subdemux_flush);

    if (ret == AVERROR_EOF) {
        c->mpeg_pb.pub.eof_reached = 1;
        c->play_end = 1;

        return AVERROR_EOF;
    }

    if (ret >= 0 && nav_event == DVDNAV_NAV_PACKET)
        return FFERROR_REDO;

    return ret;
}

static void dvdvideo_subdemux_close(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    av_freep(&c->mpeg_pb.pub.buffer);
    avformat_close_input(&c->mpeg_ctx);
}

static int dvdvideo_subdemux_open(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;
    extern const FFInputFormat ff_mpegps_demuxer;
    int ret;

    if (!(c->mpeg_buf = av_mallocz(DVDVIDEO_BLOCK_SIZE)))
        return AVERROR(ENOMEM);

    ffio_init_context(&c->mpeg_pb, c->mpeg_buf, DVDVIDEO_BLOCK_SIZE, 0, s,
                      dvdvideo_subdemux_read_data, NULL, NULL);
    c->mpeg_pb.pub.seekable = 0;

    if (!(c->mpeg_ctx = avformat_alloc_context()))
        return AVERROR(ENOMEM);

    if ((ret = ff_copy_whiteblacklists(c->mpeg_ctx, s)) < 0) {
        avformat_free_context(c->mpeg_ctx);
        c->mpeg_ctx = NULL;

        return ret;
    }

    c->mpeg_ctx->flags = AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_GENPTS;
    c->mpeg_ctx->ctx_flags |= AVFMTCTX_UNSEEKABLE;
    c->mpeg_ctx->probesize = 0;
    c->mpeg_ctx->max_analyze_duration = 0;
    c->mpeg_ctx->interrupt_callback = s->interrupt_callback;
    c->mpeg_ctx->pb = &c->mpeg_pb.pub;
    c->mpeg_ctx->correct_ts_overflow = 0;
    c->mpeg_ctx->io_open = NULL;

    return avformat_open_input(&c->mpeg_ctx, "", &ff_mpegps_demuxer.p, NULL);
}

static int dvdvideo_read_header(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int ret;

    if (c->opt_menu) {
        if (c->opt_region               ||
            c->opt_title > 1            ||
            c->opt_preindex             ||
            c->opt_chapter_start > 1    ||
            c->opt_chapter_end > 0) {
            av_log(s, AV_LOG_ERROR, "-menu is not compatible with the -region, -title, "
                                    "-preindex, or -chapter_start/-chapter_end options\n");
            return AVERROR(EINVAL);
        }

        if (!c->opt_pgc) {
            av_log(s, AV_LOG_ERROR, "If -menu is enabled, -pgc must be set to a non-zero value\n");

            return AVERROR(EINVAL);
        }

        if (!c->opt_menu_lu) {
            av_log(s, AV_LOG_INFO, "Defaulting to menu language unit #1. "
                                   "This is not always desirable, validation suggested.\n");

            c->opt_menu_lu = 1;
        }

        if (!c->opt_pg) {
            av_log(s, AV_LOG_INFO, "Defaulting to menu PG #1. "
                                   "This is not always desirable, validation suggested.\n");

            c->opt_pg = 1;
        }

        if ((ret = dvdvideo_ifo_open(s)) < 0                    ||
            (ret = dvdvideo_menu_open(s, &c->play_state)) < 0   ||
            (ret = dvdvideo_subdemux_open(s)) < 0               ||
            (ret = dvdvideo_video_stream_setup(s)) < 0          ||
            (ret = dvdvideo_audio_stream_add_all(s)) < 0)
        return ret;

        return 0;
    }

    if (c->opt_chapter_end != 0 && c->opt_chapter_start > c->opt_chapter_end) {
        av_log(s, AV_LOG_ERROR, "Chapter (PTT) range [%d, %d] is invalid\n",
                                c->opt_chapter_start, c->opt_chapter_end);

        return AVERROR(EINVAL);
    }

    if (c->opt_title == 0) {
        av_log(s, AV_LOG_INFO, "Defaulting to title #1. "
                               "This is not always the main feature, validation suggested.\n");

        c->opt_title = 1;
    }

    if (c->opt_pgc) {
        if (c->opt_pg == 0) {
            av_log(s, AV_LOG_ERROR, "Invalid coordinates. If -pgc is set, -pg must be set too.\n");

            return AVERROR(EINVAL);
        } else if (c->opt_chapter_start > 1 || c->opt_chapter_end > 0 || c->opt_preindex) {
            av_log(s, AV_LOG_ERROR, "-pgc is not compatible with the -preindex or "
                                    "-chapter_start/-chapter_end options\n");
            return AVERROR(EINVAL);
        }
    }

    if ((ret = dvdvideo_ifo_open(s)) < 0)
        return ret;

    if (!c->opt_pgc && c->opt_preindex && (ret = dvdvideo_chapters_setup_preindex(s)) < 0)
        return ret;

    if ((ret = dvdvideo_play_open(s, &c->play_state)) < 0   ||
        (ret = dvdvideo_subdemux_open(s)) < 0               ||
        (ret = dvdvideo_video_stream_setup(s)) < 0          ||
        (ret = dvdvideo_audio_stream_add_all(s)) < 0        ||
        (ret = dvdvideo_subp_stream_add_all(s)) < 0)
        return ret;

    if (!c->opt_pgc && !c->opt_preindex)
        return dvdvideo_chapters_setup_simple(s);

    return 0;
}

static int dvdvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DVDVideoDemuxContext *c = s->priv_data;

    int ret;
    enum AVMediaType st_type;
    int found_stream = 0;

    if (c->play_end)
        return AVERROR_EOF;

    ret = av_read_frame(c->mpeg_ctx, pkt);

    if (ret < 0)
        return ret;

    if (!c->segment_started)
        c->segment_started = 1;

    st_type = c->mpeg_ctx->streams[pkt->stream_index]->codecpar->codec_type;

    /* map the subdemuxer stream to the parent demuxer's stream (by startcode) */
    for (int i = 0; i < s->nb_streams; i++) {
        if (s->streams[i]->id == c->mpeg_ctx->streams[pkt->stream_index]->id) {
            pkt->stream_index = s->streams[i]->index;
            found_stream = 1;
            break;
        }
    }

    if (!found_stream) {
        av_log(s, AV_LOG_DEBUG, "discarding frame with stream that was not in IFO headers "
                                "(stream id=%d)\n", c->mpeg_ctx->streams[pkt->stream_index]->id);

        return FFERROR_REDO;
    }

    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE) {
        if (!c->play_started) {
            /* try to start at the beginning of a GOP */
            if (st_type != AVMEDIA_TYPE_VIDEO || !(pkt->flags & AV_PKT_FLAG_KEY)) {
                av_log(s, AV_LOG_VERBOSE, "Discarding packet which is not a video keyframe or "
                                          "with unset PTS/DTS at start\n");
                return FFERROR_REDO;
            }

            c->first_pts = pkt->pts;
            c->play_started = 1;
        }

        pkt->pts += c->play_state.ts_offset - c->first_pts;
        pkt->dts += c->play_state.ts_offset - c->first_pts;

        if (pkt->pts < 0) {
            av_log(s, AV_LOG_VERBOSE, "Discarding packet with negative PTS (st=%d pts=%" PRId64 "), "
                                      "this is OK at start of playback\n",
                                      pkt->stream_index, pkt->pts);

            return FFERROR_REDO;
        }
    } else {
        av_log(s, AV_LOG_WARNING, "Unset PTS or DTS @ st=%d pts=%" PRId64 " dts=%" PRId64 "\n",
                                  pkt->stream_index, pkt->pts, pkt->dts);
    }

    av_log(s, AV_LOG_TRACE, "st=%d pts=%" PRId64 " dts=%" PRId64 " "
                            "ts_offset=%" PRId64 " first_pts=%" PRId64 "\n",
                            pkt->stream_index, pkt->pts, pkt->dts,
                            c->play_state.ts_offset, c->first_pts);

    return c->play_end ? AVERROR_EOF : 0;
}

static int dvdvideo_close(AVFormatContext *s)
{
    DVDVideoDemuxContext *c = s->priv_data;

    dvdvideo_subdemux_close(s);

    if (c->opt_menu)
        dvdvideo_menu_close(s, &c->play_state);
    else
        dvdvideo_play_close(s, &c->play_state);

    dvdvideo_ifo_close(s);

    return 0;
}

static int dvdvideo_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    DVDVideoDemuxContext *c = s->priv_data;
    int64_t new_nav_pts;
    pci_t*  new_nav_pci;
    dsi_t*  new_nav_dsi;

    if (c->opt_menu || c->opt_chapter_start > 1) {
        av_log(s, AV_LOG_ERROR, "Seeking is not compatible with menus or chapter extraction\n");

        return AVERROR_PATCHWELCOME;
    }

    if ((flags & AVSEEK_FLAG_BYTE))
        return AVERROR(ENOSYS);

    if (timestamp < 0)
        return AVERROR(EINVAL);

    if (!c->seek_warned) {
        av_log(s, AV_LOG_WARNING, "Seeking is inherently unreliable and will result "
                                  "in imprecise timecodes from this point\n");
        c->seek_warned = 1;
    }

    /* XXX(PATCHWELCOME): use dvdnav_jump_to_sector_by_time(c->play_state.dvdnav, timestamp, 0)
     * when it is available in a released version of libdvdnav; it is more accurate */
    if (dvdnav_time_search(c->play_state.dvdnav, timestamp) != DVDNAV_STATUS_OK) {
        av_log(s, AV_LOG_ERROR, "libdvdnav: seeking to %" PRId64 " failed\n", timestamp);

        return AVERROR_EXTERNAL;
    }

    new_nav_pts = dvdnav_get_current_time   (c->play_state.dvdnav);
    new_nav_pci = dvdnav_get_current_nav_pci(c->play_state.dvdnav);
    new_nav_dsi = dvdnav_get_current_nav_dsi(c->play_state.dvdnav);

    if (new_nav_pci == NULL || new_nav_dsi == NULL) {
        av_log(s, AV_LOG_ERROR, "Invalid NAV packet after seeking\n");

        return AVERROR_INVALIDDATA;
    }

    c->play_state.in_pgc      = 1;
    c->play_state.in_ps       = 0;
    c->play_state.is_seeking  = 1;
    c->play_state.nav_pts     = timestamp;
    c->play_state.ts_offset   = timestamp;
    c->play_state.vobu_e_ptm  = new_nav_pci->pci_gi.vobu_s_ptm;

    c->first_pts              = 0;
    c->play_started           = 0;

    dvdvideo_subdemux_flush(s);

    av_log(s, AV_LOG_DEBUG, "seeking: requested_nav_pts=%" PRId64 " new_nav_pts=%" PRId64 "\n",
                            timestamp, new_nav_pts);

    return 0;
}

#define OFFSET(x) offsetof(DVDVideoDemuxContext, x)
static const AVOption dvdvideo_options[] = {
    {"angle",           "playback angle number",                                    OFFSET(opt_angle),          AV_OPT_TYPE_INT,    { .i64=1 },     1,          9,         AV_OPT_FLAG_DECODING_PARAM },
    {"chapter_end",     "exit chapter (PTT) number (0=end)",                        OFFSET(opt_chapter_end),    AV_OPT_TYPE_INT,    { .i64=0 },     0,          99,        AV_OPT_FLAG_DECODING_PARAM },
    {"chapter_start",   "entry chapter (PTT) number",                               OFFSET(opt_chapter_start),  AV_OPT_TYPE_INT,    { .i64=1 },     1,          99,        AV_OPT_FLAG_DECODING_PARAM },
    {"menu",            "demux menu domain",                                        OFFSET(opt_menu),           AV_OPT_TYPE_BOOL,   { .i64=0 },     0,          1,         AV_OPT_FLAG_DECODING_PARAM },
    {"menu_lu",         "menu language unit (0=auto)",                              OFFSET(opt_menu_lu),        AV_OPT_TYPE_INT,    { .i64=0 },     0,          99,        AV_OPT_FLAG_DECODING_PARAM },
    {"menu_vts",        "menu VTS (0=VMG main menu)",                               OFFSET(opt_menu_vts),       AV_OPT_TYPE_INT,    { .i64=0 },     0,          99,        AV_OPT_FLAG_DECODING_PARAM },
    {"pg",              "entry PG number (0=auto)",                                 OFFSET(opt_pg),             AV_OPT_TYPE_INT,    { .i64=0 },     0,          255,       AV_OPT_FLAG_DECODING_PARAM },
    {"pgc",             "entry PGC number (0=auto)",                                OFFSET(opt_pgc),            AV_OPT_TYPE_INT,    { .i64=0 },     0,          999,       AV_OPT_FLAG_DECODING_PARAM },
    {"preindex",        "enable for accurate chapter markers, slow (2-pass read)",  OFFSET(opt_preindex),       AV_OPT_TYPE_BOOL,   { .i64=0 },     0,          1,         AV_OPT_FLAG_DECODING_PARAM },
    {"region",          "playback region number (0=free)",                          OFFSET(opt_region),         AV_OPT_TYPE_INT,    { .i64=0 },     0,          8,         AV_OPT_FLAG_DECODING_PARAM },
    {"title",           "title number (0=auto)",                                    OFFSET(opt_title),          AV_OPT_TYPE_INT,    { .i64=0 },     0,          99,        AV_OPT_FLAG_DECODING_PARAM },
    {"trim",            "trim padding cells from start",                            OFFSET(opt_trim),           AV_OPT_TYPE_BOOL,   { .i64=1 },     0,          1,         AV_OPT_FLAG_DECODING_PARAM },
    {NULL}
};

static const AVClass dvdvideo_class = {
    .class_name = "DVD-Video demuxer",
    .item_name  = av_default_item_name,
    .option     = dvdvideo_options,
    .version    = LIBAVUTIL_VERSION_INT
};

const FFInputFormat ff_dvdvideo_demuxer = {
    .p.name         = "dvdvideo",
    .p.long_name    = NULL_IF_CONFIG_SMALL("DVD-Video"),
    .p.priv_class   = &dvdvideo_class,
    .p.flags        = AVFMT_SHOW_IDS | AVFMT_TS_DISCONT   | AVFMT_SEEK_TO_PTS |
                      AVFMT_NOFILE   | AVFMT_NO_BYTE_SEEK | AVFMT_NOGENSEARCH | AVFMT_NOBINSEARCH,
    .priv_data_size = sizeof(DVDVideoDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_close     = dvdvideo_close,
    .read_header    = dvdvideo_read_header,
    .read_packet    = dvdvideo_read_packet,
    .read_seek      = dvdvideo_read_seek
};
