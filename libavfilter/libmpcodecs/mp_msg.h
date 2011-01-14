/*
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef MPLAYER_MP_MSG_H
#define MPLAYER_MP_MSG_H

#include <stdarg.h>

// defined in mplayer.c and mencoder.c
extern int verbose;

// verbosity elevel:

/* Only messages level MSGL_FATAL-MSGL_STATUS should be translated,
 * messages level MSGL_V and above should not be translated. */

#define MSGL_FATAL 0  // will exit/abort
#define MSGL_ERR 1    // continues
#define MSGL_WARN 2   // only warning
#define MSGL_HINT 3   // short help message
#define MSGL_INFO 4   // -quiet
#define MSGL_STATUS 5 // v=0
#define MSGL_V 6      // v=1
#define MSGL_DBG2 7   // v=2
#define MSGL_DBG3 8   // v=3
#define MSGL_DBG4 9   // v=4
#define MSGL_DBG5 10  // v=5

#define MSGL_FIXME 1  // for conversions from printf where the appropriate MSGL is not known; set equal to ERR for obtrusiveness
#define MSGT_FIXME 0  // for conversions from printf where the appropriate MSGT is not known; set equal to GLOBAL for obtrusiveness

// code/module:

#define MSGT_GLOBAL 0        // common player stuff errors
#define MSGT_CPLAYER 1       // console player (mplayer.c)
#define MSGT_GPLAYER 2       // gui player

#define MSGT_VO 3       // libvo
#define MSGT_AO 4       // libao

#define MSGT_DEMUXER 5    // demuxer.c (general stuff)
#define MSGT_DS 6         // demux stream (add/read packet etc)
#define MSGT_DEMUX 7      // fileformat-specific stuff (demux_*.c)
#define MSGT_HEADER 8     // fileformat-specific header (*header.c)

#define MSGT_AVSYNC 9     // mplayer.c timer stuff
#define MSGT_AUTOQ 10     // mplayer.c auto-quality stuff

#define MSGT_CFGPARSER 11 // cfgparser.c

#define MSGT_DECAUDIO 12  // av decoder
#define MSGT_DECVIDEO 13

#define MSGT_SEEK 14    // seeking code
#define MSGT_WIN32 15   // win32 dll stuff
#define MSGT_OPEN 16    // open.c (stream opening)
#define MSGT_DVD 17     // open.c (DVD init/read/seek)

#define MSGT_PARSEES 18 // parse_es.c (mpeg stream parser)
#define MSGT_LIRC 19    // lirc_mp.c and input lirc driver

#define MSGT_STREAM 20  // stream.c
#define MSGT_CACHE 21   // cache2.c

#define MSGT_MENCODER 22

#define MSGT_XACODEC 23 // XAnim codecs

#define MSGT_TV 24      // TV input subsystem

#define MSGT_OSDEP 25  // OS-dependent parts

#define MSGT_SPUDEC 26 // spudec.c

#define MSGT_PLAYTREE 27    // Playtree handeling (playtree.c, playtreeparser.c)

#define MSGT_INPUT 28

#define MSGT_VFILTER 29

#define MSGT_OSD 30

#define MSGT_NETWORK 31

#define MSGT_CPUDETECT 32

#define MSGT_CODECCFG 33

#define MSGT_SWS 34

#define MSGT_VOBSUB 35
#define MSGT_SUBREADER 36

#define MSGT_AFILTER 37  // Audio filter messages

#define MSGT_NETST 38 // Netstream

#define MSGT_MUXER 39 // muxer layer

#define MSGT_OSD_MENU 40

#define MSGT_IDENTIFY 41  // -identify output

#define MSGT_RADIO 42

#define MSGT_ASS 43 // libass messages

#define MSGT_LOADER 44 // dll loader messages

#define MSGT_STATUSLINE 45 // playback/encoding status line

#define MSGT_TELETEXT 46       // Teletext decoder

#define MSGT_MAX 64


extern char *mp_msg_charset;
extern int mp_msg_color;
extern int mp_msg_module;

extern int mp_msg_levels[MSGT_MAX];
extern int mp_msg_level_all;


void mp_msg_init(void);
int mp_msg_test(int mod, int lev);

#include "config.h"

void mp_msg_va(int mod, int lev, const char *format, va_list va);
#ifdef __GNUC__
void mp_msg(int mod, int lev, const char *format, ... ) __attribute__ ((format (printf, 3, 4)));
#   ifdef MP_DEBUG
#      define mp_dbg(mod,lev, args... ) mp_msg(mod, lev, ## args )
#   else
#      define mp_dbg(mod,lev, args... ) /* only useful for developers */
#   endif
#else // not GNU C
void mp_msg(int mod, int lev, const char *format, ... );
#   ifdef MP_DEBUG
#      define mp_dbg(mod,lev, ... ) mp_msg(mod, lev, __VA_ARGS__)
#   else
#      define mp_dbg(mod,lev, ... ) /* only useful for developers */
#   endif
#endif /* __GNUC__ */

const char* filename_recode(const char* filename);

#endif /* MPLAYER_MP_MSG_H */
