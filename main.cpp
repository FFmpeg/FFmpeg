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

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

// attribute indices
enum {
	VERTICES = 0,
	TEX_COORDS	
};

// uniform indices
enum {
	MVP_MATRIX = 0,
	FRAME_TEX
};

// app data structure
typedef struct {
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
	/* GLuint vao;
	GLuint vert_buf;
	GLuint elem_buf;
	GLuint frame_tex;
	GLuint program;
	GLuint attribs[2];
	GLuint uniforms[2]; */
} AppData;

// initialize the app data structure
void initializeAppData(AppData *data) {
	data->fmt_ctx = NULL;
  data->io_ctx = NULL;
	data->stream_idx = -1;
	data->video_stream = NULL;
	data->codec_ctx = NULL;
	data->decoder = NULL;
	data->av_frame = NULL;
	data->gl_frame = NULL;
	data->conv_ctx = NULL;
}

// clean up the app data structure
void clearAppData(AppData *data) {
	if (data->av_frame) av_free(data->av_frame);
	if (data->gl_frame) av_free(data->gl_frame);
	if (data->packet) av_free(data->packet);
	if (data->codec_ctx) avcodec_close(data->codec_ctx);
	if (data->fmt_ctx) avformat_free_context(data->fmt_ctx);
	if (data->io_ctx) av_free(data->io_ctx);
	/* glDeleteVertexArrays(1, &data->vao);
	glDeleteBuffers(1, &data->vert_buf);
	glDeleteBuffers(1, &data->elem_buf);
	glDeleteTextures(1, &data->frame_tex); */
	initializeAppData(data);
}

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

struct BufferContext {
  unsigned char *data;
  int64_t dataLength;
  int64_t dataPos;
};

int bufferRead(void *opaque, unsigned char *buf, int buf_size) {
  BufferContext *bufferContext = (BufferContext *)opaque;
  int64_t readLength = std::min<int64_t>(buf_size, bufferContext->dataLength - bufferContext->dataPos);
  // std::cout << "read " << bufferContext->dataPos << " " << readLength << "\n";
  if (readLength > 0) {
    memcpy(buf, bufferContext->data + bufferContext->dataPos, readLength);
    bufferContext->dataPos += readLength;
    return readLength;
  } else {
    return AVERROR_EOF;
  }
}
int64_t bufferSeek(void *opaque, int64_t offset, int whence) {
  BufferContext *bufferContext = (BufferContext *)opaque;
  if (whence == AVSEEK_SIZE) {
    return bufferContext->dataLength;
  } else {
    int64_t newPos;
    if (whence == SEEK_SET) {
      newPos = offset;
    } else if (whence == SEEK_CUR) {
      newPos = bufferContext->dataPos + offset;
    } else if (whence == SEEK_END) {
      newPos = bufferContext->dataLength + offset;
    } else {
      newPos = offset;
    }
    newPos = std::min<int64_t>(std::max<int64_t>(newPos, 0), bufferContext->dataLength - bufferContext->dataPos);
    // std::cout << "seek " << newPos << "\n";
    bufferContext->dataPos = newPos;
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

    BufferContext bufferContext = {
      (unsigned char *)arrayBuffer->GetContents().Data() + arrayBufferView->ByteOffset(),
      arrayBufferView->ByteLength(),
      0,
    };

    // initialize libav
    av_register_all();
    avformat_network_init();
    
    // initialize custom data structure
    AppData data;
    initializeAppData(&data);
    
    // open video
    data.fmt_ctx = avformat_alloc_context();
    data.io_ctx = avio_alloc_context(buffer_, buffer_size_, 0, &bufferContext, bufferRead, NULL, bufferSeek); 
    data.fmt_ctx->pb = data.io_ctx;
    if (avformat_open_input(&data.fmt_ctx, "memory input", NULL, NULL) < 0) {
      std::cout << "failed to open input" << std::endl;
      clearAppData(&data);
      return;
    }
    
    // find stream info
    if (avformat_find_stream_info(data.fmt_ctx, NULL) < 0) {
      std::cout << "failed to get stream info" << std::endl;
      clearAppData(&data);
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
      clearAppData(&data);
      return;
      }

      data.video_stream = data.fmt_ctx->streams[data.stream_idx];
      data.codec_ctx = data.video_stream->codec;

    // find the decoder
      data.decoder = avcodec_find_decoder(data.codec_ctx->codec_id);
      if (data.decoder == NULL)
      {
      std::cout << "failed to find decoder" << std::endl;
      clearAppData(&data);
      return;
      }

    // open the decoder
      if (avcodec_open2(data.codec_ctx, data.decoder, NULL) < 0)
      {
        std::cout << "failed to open codec" << std::endl;
          clearAppData(&data);
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

    clearAppData(&data);
  } else {
    Nan::ThrowError("Invalid arguments");
  }
}

void Init(Handle<Object> exports) {
  Nan::SetMethod(exports, "loadVideo", LoadVideo);
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Init)

}
