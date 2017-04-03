
This is a small list of steps in order to build FFmpeg into a msvc DLL and lib file.

The projects contain Release and Debug builds for static lib files (Debug/Release)
  as well as dynamic shared dll files (DebugDLL/ReleaseDLL).
Choose whichever project configuration meets your requirements.

Note: FFmpeg requires C99 support in order to compile. Only Visual Studio 2013 or newer supports required C99 functionality and so any 
older version is not supported. Visual Studio 2013 or newer is required. If using an older unsupported version of Visual Studio the
Intel compiler can be used to add in the required C99 capability.


*** Using the Default Supplied Projects ***

The supplied project files are created using default configuration options as used by the ShiftMediaProject.
These projects use Visual Studio 2013/2015 and require certain additional dependencies to be built and available at compile time.
Required project dependencies include:
    bzlib
    iconv
    zlib
    lzma
    sdl
    libmp3lame
    libvorbis
    libspeex
    libopus
    libilbc
    libtheora
    libx264
    libx265
    libxvid
    libvpx
    libgme
    libmodplug
    libsoxr
    libfreetype
    fontconfig
    libfribidi
    libass
    gnutls
    librtmp
    libssh
    libcdio
	libcdio_paranoia
    libbluray
    opengl
    nvenc
	libmfx

Most of the above dependencies are supplied as part of the ShiftMediaProject repositories.
These repositories can be manually downloaded or automatically cloned using the supplied
  project_get_dependencies.bat file. This file can also be used to check for and download
  any dependency updates at any point after the first clone of the library.

Many of the possible FFmpeg dependencies (and there dependencies) are available in the ShiftMediaProject repositories.
However the following is a list of extra dependency options that require external downloads:
    1) opengl (requires glext)
		a) Download glext.h and wglext.h from opengl.org.
		b) Save the header files into OutputDir/include/gl/*.
			
*OutputDir is the "Output Directory" specified in the project properties. 
The default value of OutputDir is "..\..\msvc" relative to the FFmpeg source directory. An example of the expected 
directory structure is:
    -  msvc          (OutputDir)
    -> source
        - FFmpeg
        - ..Any other libraries source code..
	
Any dependencies supplied by ShiftMediaProject should be downloaded next to the FFmpeg folder as they will use the same OutputDir
location. Projects to build each dependency can be found in the respective repository ./SMP directories or all together using
the all inclusive ffmpeg_deps.sln.

Only dependencies built from supplied ShiftMediaProject repositories are tested and supported. Using compiled dependencies from
other sources may result in version mismatch or other issues. Although these external sources generally work fine any problems associated
with them are not covered by ShiftMediaProject and so they should be used with discretion.
	

*** Building with YASM ***

In order to build gmp using msvc you must first download and install YASM.
YASM is required to compile all gmp assembly files.

1) Visual Studio YASM integration can be downloaded from https://github.com/ShiftMediaProject/VSYASM/releases/latest

2) Once downloaded simply follow the install instructions included in the download.
