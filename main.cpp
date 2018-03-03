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

#include <defines.h>
#include <Video.h>

using namespace v8;

namespace ffmpeg {

void Init(Handle<Object> exports) {
  Isolate *isolate = Isolate::GetCurrent();

  exports->Set(JS_STR("Video"), Video::Initialize(isolate));
}

NODE_MODULE(NODE_GYP_MODULE_NAME, Init)

}
