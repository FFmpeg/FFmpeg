/*
 * AVISynth support
 * Copyright (c) 2006 DivX, Inc.
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

#include "avformat.h"
#include "riff.h"

#include <windows.h>
#include <vfw.h>

typedef struct {
  PAVISTREAM handle;
  AVISTREAMINFO info;
  DWORD read;
  LONG chunck_size;
  LONG chunck_samples;
} AVISynthStream;

typedef struct {
  PAVIFILE file;
  AVISynthStream *streams;
  int nb_streams;
  int next_stream;
} AVISynthContext;

static int avisynth_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
  AVISynthContext *avs = s->priv_data;
  HRESULT res;
  AVIFILEINFO info;
  DWORD id;
  AVStream *st;
  AVISynthStream *stream;

  AVIFileInit();

  res = AVIFileOpen(&avs->file, s->filename, OF_READ|OF_SHARE_DENY_WRITE, NULL);
  if (res != S_OK)
    {
      av_log(s, AV_LOG_ERROR, "AVIFileOpen failed with error %ld", res);
      AVIFileExit();
      return -1;
    }

  res = AVIFileInfo(avs->file, &info, sizeof(info));
  if (res != S_OK)
    {
      av_log(s, AV_LOG_ERROR, "AVIFileInfo failed with error %ld", res);
      AVIFileExit();
      return -1;
    }

  avs->streams = av_mallocz(info.dwStreams * sizeof(AVISynthStream));

  for (id=0; id<info.dwStreams; id++)
    {
      stream = &avs->streams[id];
      stream->read = 0;
      if (AVIFileGetStream(avs->file, &stream->handle, 0, id) == S_OK)
        {
          if (AVIStreamInfo(stream->handle, &stream->info, sizeof(stream->info)) == S_OK)
            {
              if (stream->info.fccType == streamtypeAUDIO)
                {
                  WAVEFORMATEX wvfmt;
                  LONG struct_size = sizeof(WAVEFORMATEX);
                  if (AVIStreamReadFormat(stream->handle, 0, &wvfmt, &struct_size) != S_OK)
                    continue;

                  st = avformat_new_stream(s, NULL);
                  st->id = id;
                  st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

                  st->codec->block_align = wvfmt.nBlockAlign;
                  st->codec->channels = wvfmt.nChannels;
                  st->codec->sample_rate = wvfmt.nSamplesPerSec;
                  st->codec->bit_rate = wvfmt.nAvgBytesPerSec * 8;
                  st->codec->bits_per_coded_sample = wvfmt.wBitsPerSample;

                  stream->chunck_samples = wvfmt.nSamplesPerSec * (uint64_t)info.dwScale / (uint64_t)info.dwRate;
                  stream->chunck_size = stream->chunck_samples * wvfmt.nChannels * wvfmt.wBitsPerSample / 8;

                  st->codec->codec_tag = wvfmt.wFormatTag;
                  st->codec->codec_id = ff_wav_codec_get_id(wvfmt.wFormatTag, st->codec->bits_per_coded_sample);
                }
              else if (stream->info.fccType == streamtypeVIDEO)
                {
                  BITMAPINFO imgfmt;
                  LONG struct_size = sizeof(BITMAPINFO);

                  stream->chunck_size = stream->info.dwSampleSize;
                  stream->chunck_samples = 1;

                  if (AVIStreamReadFormat(stream->handle, 0, &imgfmt, &struct_size) != S_OK)
                    continue;

                  st = avformat_new_stream(s, NULL);
                  st->id = id;
                  st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
                  st->r_frame_rate.num = stream->info.dwRate;
                  st->r_frame_rate.den = stream->info.dwScale;

                  st->codec->width = imgfmt.bmiHeader.biWidth;
                  st->codec->height = imgfmt.bmiHeader.biHeight;

                  st->codec->bits_per_coded_sample = imgfmt.bmiHeader.biBitCount;
                  st->codec->bit_rate = (uint64_t)stream->info.dwSampleSize * (uint64_t)stream->info.dwRate * 8 / (uint64_t)stream->info.dwScale;
                  st->codec->codec_tag = imgfmt.bmiHeader.biCompression;
                  st->codec->codec_id = ff_codec_get_id(ff_codec_bmp_tags, imgfmt.bmiHeader.biCompression);
                  if (st->codec->codec_id == CODEC_ID_RAWVIDEO && imgfmt.bmiHeader.biCompression== BI_RGB) {
                    st->codec->extradata = av_malloc(9 + FF_INPUT_BUFFER_PADDING_SIZE);
                    if (st->codec->extradata) {
                      st->codec->extradata_size = 9;
                      memcpy(st->codec->extradata, "BottomUp", 9);
                    }
                  }


                  st->duration = stream->info.dwLength;
                }
              else
                {
                  AVIStreamRelease(stream->handle);
                  continue;
                }

              avs->nb_streams++;

              st->codec->stream_codec_tag = stream->info.fccHandler;

              av_set_pts_info(st, 64, info.dwScale, info.dwRate);
              st->start_time = stream->info.dwStart;
            }
        }
    }

  return 0;
}

static int avisynth_read_packet(AVFormatContext *s, AVPacket *pkt)
{
  AVISynthContext *avs = s->priv_data;
  HRESULT res;
  AVISynthStream *stream;
  int stream_id = avs->next_stream;
  LONG read_size;

  // handle interleaving manually...
  stream = &avs->streams[stream_id];

  if (stream->read >= stream->info.dwLength)
    return AVERROR(EIO);

  if (av_new_packet(pkt, stream->chunck_size))
    return AVERROR(EIO);
  pkt->stream_index = stream_id;
  pkt->pts = avs->streams[stream_id].read / avs->streams[stream_id].chunck_samples;

  res = AVIStreamRead(stream->handle, stream->read, stream->chunck_samples, pkt->data, stream->chunck_size, &read_size, NULL);

  pkt->size = read_size;

  stream->read += stream->chunck_samples;

  // prepare for the next stream to read
  do {
    avs->next_stream = (avs->next_stream+1) % avs->nb_streams;
  } while (avs->next_stream != stream_id && s->streams[avs->next_stream]->discard >= AVDISCARD_ALL);

  return (res == S_OK) ? pkt->size : -1;
}

static int avisynth_read_close(AVFormatContext *s)
{
  AVISynthContext *avs = s->priv_data;
  int i;

  for (i=0;i<avs->nb_streams;i++)
    {
      AVIStreamRelease(avs->streams[i].handle);
    }

  av_free(avs->streams);
  AVIFileRelease(avs->file);
  AVIFileExit();
  return 0;
}

static int avisynth_read_seek(AVFormatContext *s, int stream_index, int64_t pts, int flags)
{
  AVISynthContext *avs = s->priv_data;
  int stream_id;

  for (stream_id = 0; stream_id < avs->nb_streams; stream_id++)
    {
      avs->streams[stream_id].read = pts * avs->streams[stream_id].chunck_samples;
    }

  return 0;
}

AVInputFormat ff_avisynth_demuxer = {
    .name           = "avs",
    .long_name      = NULL_IF_CONFIG_SMALL("AVISynth"),
    .priv_data_size = sizeof(AVISynthContext),
    .read_header    = avisynth_read_header,
    .read_packet    = avisynth_read_packet,
    .read_close     = avisynth_read_close,
    .read_seek      = avisynth_read_seek,
    .extensions     = "avs",
};
