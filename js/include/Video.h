#ifndef _HTML_VIDEO_ELEMENT_H_
#define _HTML_VIDEO_ELEMENT_H_

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
#include <libavutil/time.h>
}

#include <defines.h>

using namespace std;
using namespace v8;
using namespace node;

namespace ffmpeg {

class AppData {
public:
  AppData();
  ~AppData();

  void set(unsigned char *data, size_t dataLength);

public:
  unsigned char *data;
  int64_t dataLength;
  int64_t dataPos;

  size_t buffer_size_;
  unsigned char *buffer_;

	AVFormatContext *fmt_ctx;
	AVIOContext *io_ctx;
	int stream_idx;
	AVStream *video_stream;
	AVCodecContext *codec_ctx;
	AVCodec *decoder;
	AVPacket *packet;
	bool packetValid;
	AVFrame *av_frame;
	AVFrame *gl_frame;
	struct SwsContext *conv_ctx;
};

class Video : public ObjectWrap {
public:
  static Handle<Object> Initialize(Isolate *isolate);
  void Load(uint8_t *bufferValue, size_t bufferLength);
  void Update();
  void Play();
  void Pause();
  uint32_t GetWidth();
  uint32_t GetHeight();

protected:
  static NAN_METHOD(New);
  static NAN_METHOD(Load);
  static NAN_METHOD(Update);
  static NAN_METHOD(Play);
  static NAN_METHOD(Pause);
  static NAN_GETTER(WidthGetter);
  static NAN_GETTER(HeightGetter);
  static NAN_GETTER(DataGetter);
  static NAN_GETTER(CurrentTimeGetter);
  static NAN_SETTER(CurrentTimeSetter);
  static NAN_GETTER(DurationGetter);
  double getTimeBase();
  double getRequiredCurrentTime();
  double getRequiredCurrentTimeS();
  double getFrameCurrentTimeS();
  bool advanceToFrameAt(double timestamp);
  static int bufferRead(void *opaque, unsigned char *buf, int buf_size);
  static int64_t bufferSeek(void *opaque, int64_t offset, int whence);
  
  Video();
  ~Video();

  private:
    AppData data;
    bool playing;
    int64_t startTime;
    Nan::Persistent<Uint8ClampedArray> dataArray;
    bool dataDirty;
  };

}

#endif
