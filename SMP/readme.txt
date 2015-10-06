
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
Required supplied project dependencies include:
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
    libfdk-aac
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

All above dependencies are supplied as part of the ShiftMediaProject repositories.

Many of the possible FFmpeg dependencies (and there dependencies) are available in the ShiftMediaProject repositories.
However the following is a list of possible extra dependency options that require external downloads:
    1) sdl (requires SDL 1.2)
		a) Download pre-built binaries from the sdl homepage.
		b) Extract all the header files into OutputDir/include/SDL/*.
		c) Extract the lib's to OutputDir/lib/x86(and x64)/* and the DLL's to OutputDir/bin/x86(and x64)/*.
    2) opengl (requires glext)
		a) Download glext from the glext homepage.
		b) Extract all the header files into OutputDir/include/gl/*.
    3) opencl (requires Intel or AMD OpenCL SDK, NVIDIAs does not support required features)
		a) Download either the "Intel OpenCL SDK" or the "AMD OpenCL SDK" from their respective suppliers.
		b) Install the downloaded SDK wherever desired.
    4) nvenc (requires NVIDIA CUDA SDK)
		a) Download the "NVIDIA CUDA SDK" from the NVIDIA website.
		b) Install the downloaded SDK wherever desired.
		c) Download the "NVIDIA Video Codec SDK" from the NVIDIA website.
		d) Copy 'nvEncodeAPI.h' from the "NVIDIA Video Codec SDK" into the installed %CUDA%\include folder 
			(where %CUDA% is the location that the CUDA SDK was installed).
	5) libmfx (requires Intel Media SDK)
		a) Download the "Intel Media SDK" as part of the "Intel Integrated Native Developer Experience" from the Intel website.
		b) Install the downloaded SDK wherever desired.
		c) Copy the SDK headers found in the SDKs include directory into a new subdirectory named "mfx".
    6) decklink (requires Blackmagic DeckLink SDK)
		a) Download the "Blackmagic DeckLink SDK" from the Blackmagic website.
		b) Extract the downloaded SDK wherever desired.
        c) Create a batch file in the extracted "Win/include" folder containing the following (Note: you may have to change 
            the first line depending on the installed Visual Studio version and location):
call "C:\Program Files (x86)\Microsoft Visual Studio 12.0\VC\bin\vcvars32.bat"
midl /win32 /h DeckLinkAPI.h DeckLinkAPI.idl
        d) Copy the newly created "DeckLinkAPI.h" and "DeckLinkAPI_i.c" files to OutputDir/include/*.
			
*OutputDir is the "Output Directory" specified in the project properties. 
    Note: There is a different OutputDir for 32/64bit configurations. Lib's and DLL's should be placed in the correct directory.
	Any header files will need to be placed in both the 32 and the 64bit OutputDir's.
	By default the 32bit OutputDir is "..\..\..\msvc32\" and 64bit is "..\..\..\msvc64\" when using the msvc compiler.
	
Any dependencies supplied by ShiftMediaProject should be downloaded next to the FFmpeg folder as they will use the same OutputDir
location. Projects to build each dependency can be found in the respective repository ./SMP directories.

The exact options used to build the default supplied projects can be found in 'project_generate_msvc.bat'.
To change these options and generate new projects see the readme contained with the project_generate project files.

Only dependencies built from supplied ShiftMediaProject repositories are tested and supported. Using compiled dependencies from
other sources may result in version or other issues. Although these external sources generally work fine any problems associated
with them are not covered by ShiftMediaProject and so they should be used with discretion.
	
	
*** Building with YASM ***

In order to build FFmpeg using msvc with assembly support you must first download and install YASM.
YASM is required to compile all FFmpeg assembly files.

1) Download yasm for Visual Studio from here:
http://yasm.tortall.net/Download.html

Currently only up to VS2010 is supported on the web page so just download that.

2) Follow the instructions found within the downloaded archive for installing YASM
    Note: With newer version of VS the BuildCustomization path should be the version specific to the VS version you are using.
        so for instance the path for Visual Studio 2013 is:
        C:\Program Files (x86)\MSBuild\Microsoft.Cpp\v4.0\V120\BuildCustomizations

3) Currently there is a bug in vsyasm so in order to make the build customizations work correctly you must edit a file
    a) Open vsyasm.props that you just extracted
    b) Replace the 1 occurrence of [Input] with "%(FullPath)"  (make sure to include the "s)

4) In order to use version 1.3.0 of vsyasm you will also have to fix a error in the distributed build customizations
    a) Open vsyasm.props
    b) Replace the 1 occurrence of $(Platform) with win$(PlatformArchitecture)

	
*** Using Debug Libraries in External Programs ***

FFmpeg uses a large amount of dead code elimination throughout its source code. This can cause some issues with MSVC debug builds as they will
not remove the dead code. This will result in many linker errors relating to supposed missing functions. These functions are in fact
never used and as such are not needed as they will never be executed. This is often due to configuration options or platform settings that 
disable sections of the code. Because these functions can never be executed they are considered "dead" code and in a normal release build will 
simply just be ignored and removed. However this is not performed by debug builds which will result in errors.

The default programs (ffmpeg, ffplay etc.) that are generated by project_generator have linker settings enabled so that these errors will be ignored
and a output binary will still be generated. The linker will still display the errors but a working executable will still be generated.

Any external programs that are to use generated static debug libraries will need to set the the linker to ignore these dead function calls. This can be done
by modifying the project settings of the external project and setting "Linker->General->Force File Output" to "Undefined Symbols Only". Alternatively
if using the command line then setting "/FORCE:UNRESOLVED" can be used instead.
