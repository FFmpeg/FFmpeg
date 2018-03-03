#include <string.h>
#include <cstring>
#include <stdlib.h>
#include <stdio.h>

#include <v8.h>
#include <node.h>
#include <nan.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavdevice/avdevice.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
}
#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace v8;

namespace ffmpeg {

// #define BUFFER_OFFSET(i) ((char *)NULL + (i))

// app data structure
class AppData {
public:
  AppData(unsigned char *data, size_t dataLength) : data(data), dataLength(dataLength), dataPos(0), 
  fmt_ctx(nullptr), io_ctx(nullptr), stream_idx(-1), video_stream(nullptr), codec_ctx(nullptr), decoder(nullptr), packet(nullptr), av_frame(nullptr), gl_frame(nullptr), conv_ctx(nullptr) {}
  ~AppData() {
    AppData *data = this;
    if (data->av_frame) av_free(data->av_frame);
    if (data->gl_frame) av_free(data->gl_frame);
    if (data->packet) av_free(data->packet);
    if (data->codec_ctx) avcodec_close(data->codec_ctx);
    if (data->fmt_ctx) avformat_free_context(data->fmt_ctx);
    if (data->io_ctx) av_free(data->io_ctx);
  }

public:
  unsigned char *data;
  int64_t dataLength;
  int64_t dataPos;

	AVFormatContext *fmt_ctx;
	AVIOContext *io_ctx;
	int stream_idx;
	AVStream *video_stream;
	AVCodecContext *codec_ctx;
	AVCodec *decoder;
	AVPacket *packet;
	AVFrame *av_frame;
	AVFrame *gl_frame;
	struct SwsContext *conv_ctx;
};

// read a video frame
bool readFrame(AppData *data) {	
	do {
    int ret = av_read_frame(data->fmt_ctx, data->packet);
		if (ret == AVERROR_EOF) {
			av_free_packet(data->packet);
			return false;
		} else if (ret < 0) {
      std::cout << "Unknown error " << ret << "\n";

      av_free_packet(data->packet);
			return false;
    }
	
		if (data->packet->stream_index == data->stream_idx) {
			int frame_finished = 0;
		
			if (avcodec_decode_video2(data->codec_ctx, data->av_frame, &frame_finished, 
				data->packet) < 0) {
				av_free_packet(data->packet);
				return false;
			}
		
			if (frame_finished) {
				if (!data->conv_ctx) {
					data->conv_ctx = sws_getContext(
            data->codec_ctx->width, data->codec_ctx->height, data->codec_ctx->pix_fmt,
						data->codec_ctx->width, data->codec_ctx->height, AV_PIX_FMT_RGBA,
						SWS_BICUBIC, NULL, NULL, NULL);
				}
			
				sws_scale(data->conv_ctx, data->av_frame->data, data->av_frame->linesize, 0, 
					data->codec_ctx->height, data->gl_frame->data, data->gl_frame->linesize);

        int max = 0;
        for (size_t i = 0; i < data->codec_ctx->width * data->codec_ctx->height * 4; i++) {
          if ((i % 4) == 3) {
            continue;
          }
          max = std::max((int)data->gl_frame->data[0][i], max);
        }

        double timeBase = (double)data->video_stream->time_base.num / (double)data->video_stream->time_base.den;
        std::cout <<
          (timeBase * (double)data->av_frame->pts) << " : " <<
          data->codec_ctx->width << "," << data->codec_ctx->height << "," << data->av_frame->linesize <<
          // (timeBase * (double)data->av_frame->pkt_pts) << " : " <<
          // (timeBase * (double)data->av_frame->pkt_dts) << " : " <<
          "(" << (int)data->gl_frame->data[0][data->codec_ctx->width * data->codec_ctx->height * 4 / 2] << " " << (int)data->gl_frame->data[0][data->codec_ctx->width * data->codec_ctx->height * 4 / 2 + 1] << " " << (int)data->gl_frame->data[0][data->codec_ctx->width * data->codec_ctx->height * 4 / 2 + 2] << " " << (int)data->gl_frame->data[0][data->codec_ctx->width * data->codec_ctx->height * 4 / 2 + 3] << ")[" << max << "] " <<
          (int)data->packet->stream_index << "/" << data->stream_idx <<
          "\n";
					
				/* glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, data->codec_ctx->width, 
					data->codec_ctx->height, GL_RGB, GL_UNSIGNED_BYTE, 
					data->gl_frame->data[0]); */
			}
		}
		
		av_free_packet(data->packet);
	} while (data->packet->stream_index != data->stream_idx);
	
	return true;
}

// draw frame in opengl context
void drawFrame(AppData *data) {
	/* glClear(GL_COLOR_BUFFER_BIT);	
	glBindTexture(GL_TEXTURE_2D, data->frame_tex);
	glBindVertexArray(data->vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, BUFFER_OFFSET(0));
	glBindVertexArray(0);
	glfwSwapBuffers(); */
}

int bufferRead(void *opaque, unsigned char *buf, int buf_size) {
  AppData *appData = (AppData *)opaque;
  int64_t readLength = std::min<int64_t>(buf_size, appData->dataLength - appData->dataPos);
  // std::cout << "read " << appData->dataPos << " " << readLength << "\n";
  if (readLength > 0) {
    memcpy(buf, appData->data + appData->dataPos, readLength);
    appData->dataPos += readLength;
    return readLength;
  } else {
    return AVERROR_EOF;
  }
}
int64_t bufferSeek(void *opaque, int64_t offset, int whence) {
  AppData *appData = (AppData *)opaque;
  if (whence == AVSEEK_SIZE) {
    return appData->dataLength;
  } else {
    int64_t newPos;
    if (whence == SEEK_SET) {
      newPos = offset;
    } else if (whence == SEEK_CUR) {
      newPos = appData->dataPos + offset;
    } else if (whence == SEEK_END) {
      newPos = appData->dataLength + offset;
    } else {
      newPos = offset;
    }
    newPos = std::min<int64_t>(std::max<int64_t>(newPos, 0), appData->dataLength - appData->dataPos);
    // std::cout << "seek " << newPos << "\n";
    appData->dataPos = newPos;
    return newPos;
  }
}

NAN_METHOD(LoadVideo) {
  if (info[0]->IsTypedArray()) {
    Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(info[0]);
    Local<ArrayBuffer> arrayBuffer = arrayBufferView->Buffer();

    const int kBufferSize = 4 * 1024;
    size_t buffer_size_ = kBufferSize;
    unsigned char *buffer_ = (unsigned char *)av_malloc(buffer_size_);

    // initialize libav
    av_register_all();
    avformat_network_init();
    
    // initialize custom data structure
    AppData data((unsigned char *)arrayBuffer->GetContents().Data() + arrayBufferView->ByteOffset(), arrayBufferView->ByteLength());
    
    // open video
    data.fmt_ctx = avformat_alloc_context();
    data.io_ctx = avio_alloc_context(buffer_, buffer_size_, 0, &data, bufferRead, NULL, bufferSeek); 
    data.fmt_ctx->pb = data.io_ctx;
    if (avformat_open_input(&data.fmt_ctx, "memory input", NULL, NULL) < 0) {
      std::cout << "failed to open input" << std::endl;
      return;
    }
    
    // find stream info
    if (avformat_find_stream_info(data.fmt_ctx, NULL) < 0) {
      std::cout << "failed to get stream info" << std::endl;
      return;
    }
    
    // dump debug info
    // av_dump_format(data.fmt_ctx, 0, argv[1], 0);
    
    // find the video stream
      for (unsigned int i = 0; i < data.fmt_ctx->nb_streams; ++i)
      {
          if (data.fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
          {
              data.stream_idx = i;
              break;
          }
      }

      if (data.stream_idx == -1)
      {
      std::cout << "failed to find video stream" << std::endl;
      return;
      }

      data.video_stream = data.fmt_ctx->streams[data.stream_idx];
      data.codec_ctx = data.video_stream->codec;

    // find the decoder
      data.decoder = avcodec_find_decoder(data.codec_ctx->codec_id);
      if (data.decoder == NULL)
      {
      std::cout << "failed to find decoder" << std::endl;
      return;
      }

    // open the decoder
      if (avcodec_open2(data.codec_ctx, data.decoder, NULL) < 0)
      {
        std::cout << "failed to open codec" << std::endl;
          return;
      }

    // allocate the video frames
      data.av_frame = av_frame_alloc();
      data.gl_frame = av_frame_alloc();
      int size = avpicture_get_size(AV_PIX_FMT_RGBA, data.codec_ctx->width, 
        data.codec_ctx->height);
      uint8_t *internal_buffer = (uint8_t *)av_malloc(size * sizeof(uint8_t));
      avpicture_fill((AVPicture *)data.gl_frame, internal_buffer, AV_PIX_FMT_RGBA,
        data.codec_ctx->width, data.codec_ctx->height);
    data.packet = (AVPacket *)av_malloc(sizeof(AVPacket));

    // run the application mainloop
    while (readFrame(&data)) {
      drawFrame(&data);
    }
  } else {
    Nan::ThrowError("Invalid arguments");
  }
}

void Init(Handle<Object> exports) {
  Nan::SetMethod(exports, "loadVideo", LoadVideo);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Init)

}
