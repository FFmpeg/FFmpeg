#include <Video.h>

using namespace v8;

namespace ffmpeg {

const int kBufferSize = 4 * 1024;
const AVPixelFormat kPixelFormat = AV_PIX_FMT_RGB32;

AppData::AppData() :
  dataPos(0),
  fmt_ctx(nullptr), io_ctx(nullptr), stream_idx(-1), video_stream(nullptr), codec_ctx(nullptr), decoder(nullptr), packet(nullptr), av_frame(nullptr), gl_frame(nullptr), conv_ctx(nullptr) {}
AppData::~AppData() {
  resetState();
}

void AppData::resetState() {
  if (av_frame) {
    av_free(av_frame);
    av_frame = nullptr;
  }
  if (gl_frame) {
    av_free(gl_frame);
    gl_frame = nullptr;
  }
  if (packet) {
    av_free_packet(packet);
    packet = nullptr;
  }
  if (codec_ctx) {
    avcodec_close(codec_ctx);
    codec_ctx = nullptr;
  }
  if (fmt_ctx) {
    avformat_free_context(fmt_ctx);
    fmt_ctx = nullptr;
  }
  if (io_ctx) {
    av_free(io_ctx->buffer);
    av_free(io_ctx);
    io_ctx = nullptr;
  }
}

bool AppData::set(vector<unsigned char> &memory, string *error) {
  data = std::move(memory);
  resetState();

  // open video
  fmt_ctx = avformat_alloc_context();
  io_ctx = avio_alloc_context((unsigned char *)av_malloc(kBufferSize), kBufferSize, 0, this, bufferRead, NULL, bufferSeek);
  fmt_ctx->pb = io_ctx;
  if (avformat_open_input(&fmt_ctx, "memory input", NULL, NULL) < 0) {
    if (error) {
      *error = "failed to open input";
    }
    return false;
  }

  // find stream info
  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    if (error) {
      *error = "failed to get stream info";
    }
    return false;
  }

  // dump debug info
  // av_dump_format(fmt_ctx, 0, argv[1], 0);

   // find the video stream
  for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i)
  {
      if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
      {
          stream_idx = i;
          break;
      }
  }

  if (stream_idx == -1) {
    if (error) {
      *error = "failed to find video stream";
    }
    return false;
  }

  video_stream = fmt_ctx->streams[stream_idx];
  codec_ctx = video_stream->codec;

  // find the decoder
  decoder = avcodec_find_decoder(codec_ctx->codec_id);
  if (decoder == NULL) {
    if (error) {
      *error = "failed to find decoder";
    }
    return false;
  }

  // open the decoder
  if (avcodec_open2(codec_ctx, decoder, NULL) < 0) {
    if (error) {
      *error = "failed to open codec";
    }
    return false;
  }

  // allocate the video frames
  av_frame = av_frame_alloc();
  gl_frame = av_frame_alloc();
  int size = avpicture_get_size(kPixelFormat, codec_ctx->width, codec_ctx->height);
  uint8_t *internal_buffer = (uint8_t *)av_malloc(size * sizeof(uint8_t));
  avpicture_fill((AVPicture *)gl_frame, internal_buffer, kPixelFormat, codec_ctx->width, codec_ctx->height);
  packet = (AVPacket *)av_malloc(sizeof(AVPacket));

  return true;
}

int AppData::bufferRead(void *opaque, unsigned char *buf, int buf_size) {
  AppData *appData = (AppData *)opaque;
  int64_t readLength = std::min<int64_t>(buf_size, appData->data.size() - appData->dataPos);
  if (readLength > 0) {
    memcpy(buf, appData->data.data() + appData->dataPos, readLength);
    appData->dataPos += readLength;
    return readLength;
  } else {
    return AVERROR_EOF;
  }
}
int64_t AppData::bufferSeek(void *opaque, int64_t offset, int whence) {
  AppData *appData = (AppData *)opaque;
  if (whence == AVSEEK_SIZE) {
    return appData->data.size();
  } else {
    int64_t newPos;
    if (whence == SEEK_SET) {
      newPos = offset;
    } else if (whence == SEEK_CUR) {
      newPos = appData->dataPos + offset;
    } else if (whence == SEEK_END) {
      newPos = appData->data.size() + offset;
    } else {
      newPos = offset;
    }
    newPos = std::min<int64_t>(std::max<int64_t>(newPos, 0), appData->data.size() - appData->dataPos);
    appData->dataPos = newPos;
    return newPos;
  }
}

bool AppData::advanceToFrameAt(double timestamp) {
  double timeBase = getTimeBase();

  for (;;) {
    bool packetOk = false;
    bool packetValid = false;
    while (!packetValid || !(packetOk = packet->stream_index == stream_idx && ((double)packet->pts * timeBase) >= timestamp)) {
      if (packetValid) {
        av_free_packet(packet);
        packetValid = false;
      }

      int ret = av_read_frame(fmt_ctx, packet);
      packetValid = true;
      if (ret == AVERROR_EOF) {
        break;
      } else if (ret < 0) {
        // std::cout << "Unknown error " << ret << "\n";
        av_free_packet(packet);
        return false;
      } else {
        continue;
      }
    }
    // we have a valid packet at this point
    if (packetOk) {
      int frame_finished = 0;

      if (avcodec_decode_video2(codec_ctx, av_frame, &frame_finished, packet) < 0) {
        av_free_packet(packet);
        return false;
      }

      if (frame_finished) {
        if (!conv_ctx) {
          conv_ctx = sws_getContext(
            codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
            codec_ctx->width, codec_ctx->height, kPixelFormat,
            SWS_BICUBIC, NULL, NULL, NULL);
        }

        sws_scale(conv_ctx, av_frame->data, av_frame->linesize, 0, codec_ctx->height, gl_frame->data, gl_frame->linesize);

        av_free_packet(packet);

        return true;
      } else {
        av_free_packet(packet);

        continue;
      }
    } else {
      // std::cout << "Do not have packet up to " << timestamp << "\n";
      av_free_packet(packet);
      return false;
    }
  }
}

double AppData::getTimeBase() {
  if (video_stream) {
    return (double)video_stream->time_base.num / (double)video_stream->time_base.den;
  } else {
    return 1;
  }
}

Video::Video() : loaded(false), playing(false), startTime(0), dataDirty(true) {
  videos.push_back(this);
}

Video::~Video() {
  videos.erase(std::find(videos.begin(), videos.end(), this));
}

Handle<Object> Video::Initialize(Isolate *isolate) {
  // initialize libav
  av_register_all();
  avformat_network_init();

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

  ctorFn->Set(JS_STR("updateAll"), Nan::New<Function>(UpdateAll));

  return scope.Escape(ctorFn);
}

NAN_METHOD(Video::New) {
  Nan::HandleScope scope;

  Video *video = new Video();
  Local<Object> videoObj = info.This();
  video->Wrap(videoObj);

  info.GetReturnValue().Set(videoObj);
}

bool Video::Load(unsigned char *bufferValue, size_t bufferLength, string *error) {
  // reset state
  loaded = false;
  dataArray.Reset();
  dataDirty = true;

  // initialize custom data structure
  std::vector<unsigned char> bufferData(bufferLength);
  memcpy(bufferData.data(), bufferValue, bufferLength);

  if (data.set(bufferData, error)) { // takes ownership of bufferData
    // scan to the first frame
    advanceToFrameAt(0);

    loaded = true;

    return true;
  } else {
    return false;
  }
}

void Video::Update() {
  if (loaded && playing) {
    advanceToFrameAt(getRequiredCurrentTimeS());
  }
}

void Video::Play() {
  if (loaded) {
    playing = true;
    startTime = av_gettime();
  }
}

void Video::Pause() {
  if (loaded) {
    playing = false;
  }
}

uint32_t Video::GetWidth() {
  if (loaded) {
    return data.codec_ctx->width;
  } else {
    return 0;
  }
}

uint32_t Video::GetHeight() {
  if (loaded) {
    return data.codec_ctx->height;
  } else {
    return 0;
  }
}

NAN_METHOD(Video::Load) {
  if (info[0]->IsArrayBuffer()) {
    Video *video = ObjectWrap::Unwrap<Video>(info.This());

    Local<ArrayBuffer> arrayBuffer = Local<ArrayBuffer>::Cast(info[0]);

    string error;
    if (video->Load((uint8_t *)arrayBuffer->GetContents().Data(), arrayBuffer->ByteLength(), &error)) {
      // nothing
    } else {
      Nan::ThrowError(error.c_str());
    }
  } else if (info[0]->IsTypedArray()) {
    Video *video = ObjectWrap::Unwrap<Video>(info.This());

    Local<ArrayBufferView> arrayBufferView = Local<ArrayBufferView>::Cast(info[0]);
    Local<ArrayBuffer> arrayBuffer = arrayBufferView->Buffer();

    string error;
    if (video->Load((unsigned char *)arrayBuffer->GetContents().Data() + arrayBufferView->ByteOffset(), arrayBufferView->ByteLength())) {
      // nothing
    } else {
      Nan::ThrowError(error.c_str());
    }
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
    memcpy((unsigned char *)arrayBuffer->GetContents().Data() + uint8ClampedArray->ByteOffset(), video->data.gl_frame->data[0], dataSize);
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
    av_seek_frame(video->data.fmt_ctx, video->data.stream_idx, (int64_t )(newValueS / video->data.getTimeBase()), AVSEEK_FLAG_FRAME | AVSEEK_FLAG_ANY);
    video->data.advanceToFrameAt(newValueS);
  } else {
    Nan::ThrowError("value: invalid arguments");
  }
}

NAN_GETTER(Video::DurationGetter) {
  Nan::HandleScope scope;
  
  Video *video = ObjectWrap::Unwrap<Video>(info.This());

  double duration = video->data.fmt_ctx ? ((double)video->data.fmt_ctx->duration / (double)AV_TIME_BASE) : 1;
  info.GetReturnValue().Set(JS_NUM(duration));
}

NAN_METHOD(Video::UpdateAll) {
  for (auto i : videos) {
    i->Update();
  }
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
  double pts = data.av_frame ? (double)data.av_frame->pts : 0;
  double timeBase = data.getTimeBase();
  return pts * timeBase;
}

bool Video::advanceToFrameAt(double timestamp) {	
  if (data.advanceToFrameAt(timestamp)) {
    dataDirty = true;

    return true;
  } else {
    return false;
  }
}

std::vector<Video *> videos;

}
