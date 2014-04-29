## Requirements
   * Intel Media SDK 2014 : http://software.intel.com/en-us/vcsource/tools/media-sdk
   * mfx_dispatch : https://github.com/lu-zero/mfx_dispatch

## Build
   * https://github.com/lu-zero/mfx_dispatch/wiki/Libav-integration
   * `./configure --enable-libmfx --enable-memalign-hack --extra-libs="-lsupc++ -lstdc++"`

## Support
   * Intel Media SDK API : v1.1
   * Codec : H.264
   * Rate control method : CBR, VBR, CQP

## Codec name
   * H.264 : `h264_qsv`

## Rate control option
   * CBR : `-b 2000k -maxrate 2000k`
   * VBR : `-b 4000k`
   * CQP : `-q 20`

## Option
   * AVCodec : `b, maxrate, q, g, bf, refs, slices, coder +vlc, flags -cgop`
   * QSV : `async_depth, timeout, qpi, qpp, qpb, idr_interval, profile, level`

## Example
QSV encode  
   * `ffmpeg -i "input.ts" -b 2000k -maxrate 2000k -c:v h264_qsv "output.mp4"`
   * `ffmpeg -i "input.ts" -b 4000k -c:v h264_qsv "output.mp4"`
   * `ffmpeg -i "input.ts" -q 20 -c:v h264_qsv "output.mp4"`
   * `ffmpeg -r 30000/1001 -i "input.mp4" -q 20 -c:v h264_qsv "output.mkv"`
   * `ffmpeg -vsync cfr -i "input.mpg" -qpi 22 -qpp 25 -qpb 27 -c:v h264_qsv "output.mp4"`
