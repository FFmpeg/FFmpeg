/* 
 * General DV muxer/demuxer 
 * Copyright (c) 2003 Roman Shaposhnick
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
 *
 * Raw DV format
 * Copyright (c) 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

typedef struct DVDemuxContext DVDemuxContext;
DVDemuxContext* dv_init_demux(AVFormatContext* s);
int dv_get_packet(DVDemuxContext*, AVPacket *);
int dv_produce_packet(DVDemuxContext*, AVPacket*, uint8_t*, int);
void dv_flush_audio_packets(DVDemuxContext*);

typedef struct DVMuxContext DVMuxContext;
DVMuxContext* dv_init_mux(AVFormatContext* s);
int dv_assemble_frame(DVMuxContext *c, AVStream*, const uint8_t*, int, uint8_t**);
void dv_delete_mux(DVMuxContext*);
