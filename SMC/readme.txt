
This is a small list of steps in order to build FFmpeg into a msvc DLL and lib file.

The project contains Release and Debug builds for static lib files (Debug/Release)
  as well as dynamic shared dll files (DebugDLL/ReleaseDLL).
Choose whichever project configuration meets your requirements.

*** Building with YASM ***

In order to build FFmpeg using msvc you must first download and install YASM.
YASM is required to compile all FFmpeg assembly files.

1) Download yasm for Visual Studio from here:
http://yasm.tortall.net/Download.html

Currently only up to VS2010 is supported on the web page so just download that.

2) Follow the instructions found within the downloaded archive for installing YASM
	Note: With newer version of VS the BuildCustomization path should be the version specific to the VS version you are using.
		so for instance the path for Visual Studio 2013 is:
		C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V120\BuildCustomizations
		
3) Currently there is a bug in Visual Studio 2013 so in order to make the build customizations work you must edit a file
	a) Open vsyasm.props that you just extracted
	b) Replace the 1 occurrence of [Input] with "%(FullPath)"  (make sure to include the "s)
	
4) ???

5) Profit


*** Generating custom project files ***

This project comes with the ffmpeg_generator program which can be used to build a custom project file.
This program accepts many of the configuration parameters that are supported by FFmpegs standard configure script.
These options can be used to disable/enable specific options that define how ffmpeg is built. Using ffmpeg_generator
a new project can be built using these options.

To generate a custom project using different configuration options simply build ffmpeg_generator and then run it by
passing in the desired config options.

The default project is built using standard includes for SMC. For example:

ffmpeg_generator.exe --enable-gpl --enable-version3 --enable-avisynth --enable-nonfree --enable-bzlib --enable-iconv --enable-zlib 
   --enable-libmp3lame --enable-libvorbis --enable-libspeex --enable-libopus --enable-libfdk-aac --enable-libtheora --enable-libx264 
   --enable-libxvid --enable-libvpx --enable-libmodplug --enable-libsoxr --enable-libfreetype --enable-fontconfig --enable-libass 
   --enable-openssl --enable-librtmp --enable-libssh --toolchain=icl --enable-sdl

As well as external libs specific config options can also be enabled/disabled. For instance a project that does not require yasm 
can be built by passing --disable-yasm.
To be able to build ffplay the SDL libs must be available in the OutputDir. If you have these libs and wish to use them then sdl
can be enabled by passing --enable-sdl.
Specific programs can be ignored such as --disable-ffprobe etc.
The generator is designed to automatically resolve dependencies so if a configure option is disabled all options that depend on it will 
also be disabled accordingly.
By default the generator will build a project using Visual Studio 2013 platform toolsets. These can be changed by hand in the project configuration
or simply by passing an option to the project generator. The --toolchain= option can be used to change between standard microsoft compiler (msvc)
and the Intel compiler (icl).
