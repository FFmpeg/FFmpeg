/*
 * RIFF codec tags
 * copyright (c) 2000 Fabrice Bellard
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef FF_RIFF_H
#define FF_RIFF_H

offset_t start_tag(ByteIOContext *pb, const char *tag);
void end_tag(ByteIOContext *pb, offset_t start);

typedef struct CodecTag {
    int id;
    unsigned int tag;
    unsigned int invalid_asf : 1;
} CodecTag;

void put_bmp_header(ByteIOContext *pb, AVCodecContext *enc, const CodecTag *tags, int for_asf);
int put_wav_header(ByteIOContext *pb, AVCodecContext *enc);
int wav_codec_get_id(unsigned int tag, int bps);
void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size);

extern const CodecTag codec_bmp_tags[];
extern const CodecTag codec_wav_tags[];

unsigned int codec_get_tag(const CodecTag *tags, int id);
enum CodecID codec_get_id(const CodecTag *tags, unsigned int tag);
unsigned int codec_get_bmp_tag(int id);
unsigned int codec_get_wav_tag(int id);
enum CodecID codec_get_bmp_id(unsigned int tag);
enum CodecID codec_get_wav_id(unsigned int tag);
unsigned int codec_get_asf_tag(const CodecTag *tags, unsigned int id);
void ff_parse_specific_params(AVCodecContext *stream, int *au_rate, int *au_ssize, int *au_scale);

#endif
