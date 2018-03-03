#include <v8.h>
#include <node.h>
#include <nan.h>

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
