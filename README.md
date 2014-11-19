FFmpeg for WinRT ARM
=============

This project can be used for building ffmpeg in WinRT ARM platform.

## Summary

As there is no suitable GCC for ARM-COFF which WinRT ARM platform, if we want to build ffmpeg for this platform, we need to change the assembly codes in ffmpeg, especially the neon part. The assembly codes in this project can be built with armasm, which is the assember in Visual Studio. 
The ffmpeg version is 2.4 release.
This project is not finished, I can built it but not test. I hope someone can work together with me.

## Build

1. You need to prepare MinGW+MSYS environment, install msys-make, msys-bash, msys-m4, msys-perl. 
2. Download gas-preprocessor.pl from https://github.com/FFmpeg/gas-preprocessor , and copy it into  msys bin folder. 
3. Run VS2013 ARM Cross Tools Command Prompt first, and then run the msys from the command prompt.
4. make sure cl, lib is in the Visual Studio, not from MinGW. You can rename the cl or lib in MinGW folder.
5. Run build_ffmpeg_msvc.sh

## Issues

This project is not finished, these issues should be fixed.
1. vc1dspneon.asm , mlpdsparmv6.S not finish. So this ffmpeg doesn't support vc1 and mlp decode.
2. \libswscale\arm not finish.
3. No network support.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides a mean to alter decoded Audio and Video through chain of filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](http://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](http://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](http://ffmpeg.org/ffprobe.html) is a simple analisys tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](http://ffmpeg.org)
and in the [wiki](http://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.
