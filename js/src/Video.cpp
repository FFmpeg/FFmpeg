#include <Video.h>

using namespace v8;

namespace ffmpeg {

const int kBufferSize = 4 * 1024;
const AVPixelFormat kPixelFormat = AV_PIX_FMT_RGBA;

AppData::AppData() :
  data(nullptr), dataLength(0), dataPos(0),
  buffer_((unsigned char *)av_malloc(kBufferSize)), buffer_size_(kBufferSize),
  fmt_ctx(nullptr), io_ctx(nullptr), stream_idx(-1), video_stream(nullptr), codec_ctx(nullptr), decoder(nullptr), packet(nullptr), packetValid(false), av_frame(nullptr), gl_frame(nullptr), conv_ctx(nullptr) {}
AppData::~AppData() {
  AppData *data = this;
  if (data->av_frame) av_free(data->av_frame);
  if (data->gl_frame) av_free(data->gl_frame);
  if (data->packet) av_free_packet(data->packet);
  if (data->codec_ctx) avcodec_close(data->codec_ctx);
  if (data->fmt_ctx) avformat_free_context(data->fmt_ctx);
  if (data->io_ctx) av_free(data->io_ctx);
}

void AppData::set(unsigned char *data, size_t dataLength) {
  this->data = data;
  this->dataLength = dataLength;
}

Video::Video() : playing(false), startTime(0), dataDirty(true) {}

Video::~Video() {}

Handle<Object> Video::Initialize(Isolate *isolate) {
  Nan::EscapableHandleScope scope;
  
  // constructor
  Local<FunctionTemplate> ctor = Nan::New<FunctionTemplate>(New);
  ctor->InstanceTemplate()->SetInternalFieldCount(1);
  ctor->SetClassName(JS_STR("Video"));
  
  // prototype
  Local<ObjectTemplate> proto = ctor->PrototypeTemplate();
  Nan::SetMethod(proto, "load", Load);
  Nan::SetMethod(proto, "update", Update);
  Nan::SetMethod(proto, "play", Play);
  Nan::SetMethod(proto, "pause", Pause);
  Nan::SetAccessor(proto, JS_STR("width"), WidthGetter);
  Nan::SetAccessor(proto, JS_STR("height"), HeightGetter);
  Nan::SetAccessor(proto, JS_STR("data"), DataGetter);
  Nan::SetAccessor(proto, JS_STR("currentTime"), CurrentTimeGetter, CurrentTimeSetter);
  Nan::SetAccessor(proto, JS_STR("duration"), DurationGetter);
  
  Local<Function> ctorFn = ctor->GetFunction();

  return scope.Escape(ctorFn);
}

NAN_METHOD(Video::New) {
  Nan::HandleScope scope;

  Video *video = new Video();
  Local<Object> videoObj = info.This();
  video->Wrap(videoObj);

  info.GetReturnValue().Set(videoObj);
}

void Video::Load(unsigned char *bufferValue, size_t bufferLength) {
  // initialize libav
  av_register_all();
  avformat_network_init();
  
  // initialize custom data structure
  data.set(bufferValue, bufferLength);
  
  // open video
  data.fmt_ctx = avformat_alloc_context();
  data.io_ctx = avio_alloc_context(data.buffer_, data.buffer_size_, 0, &data, bufferRead, NULL, bufferSeek); 
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

  if (data.stream_idx == -1) {
    std::cout << "failed to find video stream" << std::endl;
    return;
  }

  data.video_stream = data.fmt_ctx->streams[data.stream_idx];
  data.codec_ctx = data.video_stream->codec;

  // find the decoder
  data.decoder = avcodec_find_decoder(data.codec_ctx->codec_id);
  if (data.decoder == NULL) {
    std::cout << "failed to find decoder" << std::endl;
    return;
  }

  // open the decoder
  if (avcodec_open2(data.codec_ctx, data.decoder, NULL) < 0) {
    std::cout << "failed to open codec" << std::endl;
    return;
  }

  // allocate the video frames
  data.av_frame = av_frame_alloc();
  data.gl_frame = av_frame_alloc();
  int size = avpicture_get_size(kPixelFormat, data.codec_ctx->width, data.codec_ctx->height);
  uint8_t *internal_buffer = (uint8_t *)av_malloc(size * sizeof(uint8_t));
  avpicture_fill((AVPicture *)data.gl_frame, internal_buffer, kPixelFormat, data.codec_ctx->width, data.codec_ctx->height);
  data.packet = (AVPacket *)av_malloc(sizeof(AVPacket));

  // run the application mainloop
  /* while (readFrame(&data)) {
    drawFrame(&data);
  } */
  advanceToFrameAt(0);
}

void Video::Update() {
  if (playing) {
    advanceToFrameAt(getRequiredCurrentTimeS());
  }
}

void Video::Play() {
  playing = true;
  startTime = av_gettime();
}

void Video::Pause() {
  playing = false;
}

uint32_t Video::GetWidth() {
  return data.codec_ctx->width;
}

uint32_t Video::GetHeight() {
  return data.codec_ctx->height;
}

NAN_METHOD(Video::Load) {
  if (info[0]->IsTypedArray()) {
    Video *video = ObjectWrap::Unwrap<Video>(info.This());

    Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(info[0]);
    Local<ArrayBuffer> arrayBuffer = arrayBufferView->Buffer();
    
    video->Load((uint8_t *)arrayBuffer->GetContents().Data() + arrayBufferView->ByteOffset(), arrayBufferView->ByteLength());
  } else {
    Nan::ThrowError("invalid arguments");
  }
}

NAN_METHOD(Video::Update) {
  Video *video = ObjectWrap::Unwrap<Video>(info.This());
  video->Update();
}

NAN_METHOD(Video::Play) {
  Video *video = ObjectWrap::Unwrap<Video>(info.This());
  video->Play();
}

NAN_METHOD(Video::Pause) {
  Video *video = ObjectWrap::Unwrap<Video>(info.This());
  video->Pause();
}

NAN_GETTER(Video::WidthGetter) {
  Nan::HandleScope scope;

  Video *video = ObjectWrap::Unwrap<Video>(info.This());
  info.GetReturnValue().Set(JS_INT(video->GetWidth()));
}

NAN_GETTER(Video::HeightGetter) {
  Nan::HandleScope scope;

  Video *video = ObjectWrap::Unwrap<Video>(info.This());
  info.GetReturnValue().Set(JS_INT(video->GetHeight()));
}

NAN_GETTER(Video::DataGetter) {
  Nan::HandleScope scope;

  Video *video = ObjectWrap::Unwrap<Video>(info.This());

  unsigned int width = video->GetWidth();
  unsigned int height = video->GetHeight();
  unsigned int dataSize = width * height * 4;
  if (video->dataArray.IsEmpty()) {
    Local<ArrayBuffer> arrayBuffer = ArrayBuffer::New(Isolate::GetCurrent(), dataSize);
    Local<Uint8ClampedArray> uint8ClampedArray = Uint8ClampedArray::New(arrayBuffer, 0, arrayBuffer->ByteLength());
    video->dataArray.Reset(uint8ClampedArray);
  }

  Local<Uint8ClampedArray> uint8ClampedArray = Nan::New(video->dataArray);
  if (video->dataDirty) {
    Local<ArrayBuffer> arrayBuffer = uint8ClampedArray->Buffer();
    memcpy(arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset(), video->data.gl_frame->data[0], dataSize);
    video->dataDirty = false;
  }

  info.GetReturnValue().Set(uint8ClampedArray);
}

NAN_GETTER(Video::CurrentTimeGetter) {
  Nan::HandleScope scope;
  
  Video *video = ObjectWrap::Unwrap<Video>(info.This());

  double currentTime = video->getFrameCurrentTimeS();
  info.GetReturnValue().Set(JS_NUM(currentTime));
}

NAN_SETTER(Video::CurrentTimeSetter) {
  Nan::HandleScope scope;

  if (value->IsNumber()) {
    Video *video = ObjectWrap::Unwrap<Video>(info.This());
    double newValueS = value->NumberValue();

    video->startTime = av_gettime() - (int64_t)(newValueS * 1e6);
    av_seek_frame(video->data.fmt_ctx, video->data.stream_idx, (int64_t )(newValueS / video->getTimeBase()), AVSEEK_FLAG_FRAME | AVSEEK_FLAG_ANY);
    video->advanceToFrameAt(newValueS);
  } else {
    Nan::ThrowError("value: invalid arguments");
  }
}

NAN_GETTER(Video::DurationGetter) {
  Nan::HandleScope scope;
  
  Video *video = ObjectWrap::Unwrap<Video>(info.This());

  double duration = (double)video->data.fmt_ctx->duration / (double)AV_TIME_BASE;
  info.GetReturnValue().Set(JS_NUM(duration));
}

double Video::getTimeBase() {
  return (double)data.video_stream->time_base.num / (double)data.video_stream->time_base.den;
}

double Video::getRequiredCurrentTimeS() {
  if (playing) {
    int64_t now = av_gettime();
    int64_t timeDiff = now - startTime;
    double timeDiffS = (double)timeDiff / 1e6;
    return timeDiffS;
  } else {
    return getFrameCurrentTimeS();
  }
}

double Video::getFrameCurrentTimeS() {
  return (getTimeBase() * (double)data.av_frame->pts);
}

/* // read a video frame
bool Video::readFrame() {	
	do {
    int ret = av_read_frame(data.fmt_ctx, data.packet);
		if (ret == AVERROR_EOF) {
			av_free_packet(data.packet);
			return false;
		} else if (ret < 0) {
      std::cout << "Unknown error " << ret << "\n";

      av_free_packet(data.packet);
			return false;
    }
	
		if (data.packet->stream_index == data.stream_idx) {
			int frame_finished = 0;
		
			if (avcodec_decode_video2(data.codec_ctx, data.av_frame, &frame_finished, data.packet) < 0) {
				av_free_packet(data.packet);
				return false;
			}
		
			if (frame_finished) {
				if (!data.conv_ctx) {
					data.conv_ctx = sws_getContext(
            data.codec_ctx->width, data.codec_ctx->height, data.codec_ctx->pix_fmt,
						data.codec_ctx->width, data.codec_ctx->height, kPixelFormat,
						SWS_BICUBIC, NULL, NULL, NULL);
				}
			
				sws_scale(data.conv_ctx, data.av_frame->data, data.av_frame->linesize, 0, data.codec_ctx->height, data.gl_frame->data, data.gl_frame->linesize);
			}
		}
		
		av_free_packet(data.packet);
	} while (data.packet->stream_index != data.stream_idx);
	
	return true;
} */

bool Video::advanceToFrameAt(double timestamp) {	
  double timeBase = getTimeBase();

  for (;;) {
    bool packetOk = false;
    bool packetValid = false;
    while (!packetValid || !(packetOk = data.packet->stream_index == data.stream_idx && ((double)data.packet->pts * timeBase) >= timestamp)) {
      // std::cout << "check ts " << ((double)data.packet->pts * timeBase) << "\n";

      if (packetValid) {
        av_free_packet(data.packet);
        packetValid = false;
      }

      int ret = av_read_frame(data.fmt_ctx, data.packet);
      packetValid = true;
      if (ret == AVERROR_EOF) {
        break;
        // av_free_packet(data.packet);
        // return false;
      } else if (ret < 0) {
        std::cout << "Unknown error " << ret << "\n";
        av_free_packet(data.packet);
        return false;
      } else {
        continue;
      }
    }
    // we have a valid packet at this point
    if (packetOk) {
      int frame_finished = 0;
    
      if (avcodec_decode_video2(data.codec_ctx, data.av_frame, &frame_finished, data.packet) < 0) {
        av_free_packet(data.packet);
        return false;
      }
    
      if (frame_finished) {
        if (!data.conv_ctx) {
          data.conv_ctx = sws_getContext(
            data.codec_ctx->width, data.codec_ctx->height, data.codec_ctx->pix_fmt,
            data.codec_ctx->width, data.codec_ctx->height, kPixelFormat,
            SWS_BICUBIC, NULL, NULL, NULL);
        }
      
        sws_scale(data.conv_ctx, data.av_frame->data, data.av_frame->linesize, 0, data.codec_ctx->height, data.gl_frame->data, data.gl_frame->linesize);

        /* int max = 0;
        for (size_t i = 0; i < data.codec_ctx->width * data.codec_ctx->height * 4; i++) {
          if ((i % 4) == 3) {
            continue;
          }
          max = std::max((int)data.gl_frame->data[0][i], max);
        } */

        /* double timeBase = (double)data.video_stream->time_base.num / (double)data.video_stream->time_base.den;
        std::cout <<
          (timeBase * (double)data.av_frame->pts) << " : " <<
          data.codec_ctx->width << "," << data.codec_ctx->height << "," << data.av_frame->linesize <<
          // (timeBase * (double)data.av_frame->pkt_pts) << " : " <<
          // (timeBase * (double)data.av_frame->pkt_dts) << " : " <<
          "(" << (int)data.gl_frame->data[0][data.codec_ctx->width * data.codec_ctx->height * 4 / 2] << " " << (int)data.gl_frame->data[0][data.codec_ctx->width * data.codec_ctx->height * 4 / 2 + 1] << " " << (int)data.gl_frame->data[0][data.codec_ctx->width * data.codec_ctx->height * 4 / 2 + 2] << " " << (int)data.gl_frame->data[0][data.codec_ctx->width * data.codec_ctx->height * 4 / 2 + 3] << ") " <<
          (int)data.packet->stream_index << "/" << data.stream_idx <<
          "\n"; */
          
        /* glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, data.codec_ctx->width, 
          data.codec_ctx->height, GL_RGB, GL_UNSIGNED_BYTE, 
          data.gl_frame->data[0]); */

        av_free_packet(data.packet);

        dataDirty = true;

        return true;
      } else {
        continue;
        /* std::cout << "Failed to decode frame" << "\n";
        av_free_packet(data.packet);
        return false; */
      }
    } else {
      std::cout << "Do not have packet up to " << timestamp << "\n";
      av_free_packet(data.packet);
      return false;
    }
  }
}

/* // draw frame in opengl context
void Video::drawFrame() {
	glClear(GL_COLOR_BUFFER_BIT);	
	glBindTexture(GL_TEXTURE_2D, data.frame_tex);
	glBindVertexArray(data.vao);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, BUFFER_OFFSET(0));
	glBindVertexArray(0);
	glfwSwapBuffers();
} */

int Video::bufferRead(void *opaque, unsigned char *buf, int buf_size) {
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
int64_t Video::bufferSeek(void *opaque, int64_t offset, int whence) {
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

}
