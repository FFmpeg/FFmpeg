
This is a small list of steps in order to build a custom FFmpeg Visual Studio project file.

The generated projects will contain Release and Debug builds for static lib files (Debug/Release)
  as well as dynamic shared dll files (DebugDLL/ReleaseDLL).
Choose whichever project configuration meets your requirements.


*** Generating Custom Project Files ***

This project comes with the ffmpeg_generator program which can be used to build custom project files.
This program accepts many of the configuration parameters that are supported by FFmpegs standard configure 
script (see FFmpegs documentation for more details).
These options can be used to disable/enable specific options that define how FFmpeg is built. Using ffmpeg_generator
a new project can be built using these options.

To generate a custom project using different configuration options simply build ffmpeg_generator and then run it by
passing in the desired config options.

For example to build FFmpeg without any additional dependencies:

ffmpeg_generator.exe --enable-gpl --enable-version3 --disable-bzlib --disable-iconv --disable-zlib 
    --disable-lzma --disable-sdl --toolchain=msvc

As well as enabling/disabling dependency libraries, specific config options can also be enabled/disabled. For instance a 
project that does not require yasm can be built by passing --disable-yasm. Specific FFmpeg programs can also be disabled
such as --disable-ffprobe which will prevent a project file being generated for the FFmpeg program ffprobe.

The generator is designed to automatically resolve dependencies so if a configure option is disabled all options that depend on it will 
also be disabled accordingly.

By default the generator will build a project using the latest available toolchain detected on the host computer. This can be changed by 
hand in the generated projects properties "Project Configuration->General->Platform Toolset" if an older toolset is desired. The generator 
also supports use of the Intel compiler. The --toolchain= option can be used to change between standard Microsoft compiler (msvc)
and the Intel compiler (icl).

Note: FFmpeg requires C99 support in order to compile. Only Visual Studio 2013 or newer supports required C99 functionality and so any 
older version is not supported. Visual Studio 2013 or newer is required. If using an older unsupported version of Visual Studio the
Intel compiler can be used to add in the required C99 capability.

To be able to build ffplay the SDL libs must be available in the OutputDir*. If SDL is disabled then ffplay will also be disabled.

The project generator will also check the availability of dependencies. Any enabled options must have the appropriate headers installed
in OutputDir* otherwise an error will be generated.

Many of the possible FFmpeg dependencies (and there dependencies) are available in the ShiftMediaProject repositories.
However the following is a list of extra dependency options that require external downloads:
    1) opengl (requires glext)
		a) Download glext from the glext homepage.
		b) Extract all the header files into OutputDir/include/gl/*.
    2) opencl (requires Intel or AMD OpenCL SDK, NVIDIAs does not support required features)
		a) Download either the "Intel OpenCL SDK" or the "AMD OpenCL SDK" from their respective suppliers.
		b) Install the downloaded SDK wherever desired.
    3) nvenc (requires NVIDIA CUDA SDK)
		a) Download the "NVIDIA CUDA SDK" from the NVIDIA website.
		b) Install the downloaded SDK wherever desired.
		c) Download the "NVIDIA Video Codec SDK" from the NVIDIA website.
		d) Copy 'nvEncodeAPI.h' from the "NVIDIA Video Codec SDK" into the installed %CUDA%\include folder 
			(where %CUDA% is the location that the CUDA SDK was installed).
			
*OutputDir is the "Output Directory" specified in the project properties. 
    Note: There is a different OutputDir for 32/64bit configurations. Lib's and DLL's should be placed in the correct directory.
	Any header files will need to be placed in the Output directories include folder OutputDir\include.
	By default the 32bit lib OutputDir is "OutputDir/lib/x86" and 64bit is "OutputDir/lib/x64". DLLs followin a similar schemes
    except using "OutputDir/bin/x86" and for 64bit "OutputDir/bin/x64".
    The default value of OutputDir is "..\..\msvc" relative to the FFmpeg source directory. An example of the expetced 
    directory structure is:
        -  msvc          (OutputDir)
        -> source
            - FFmpeg
            - ..Any other libraries source code..
	

*** Using the Supplied Dependency Projects ***
	
Many of the possible FFmpeg dependencies (and their dependencies) are provided by the ShiftMediaProject repositories.
 
Any dependencies supplied by ShiftMediaProject should be downloaded next to the FFmpeg folder as they will use the same OutputDir
location. Projects to build each dependency can be found in the respective repository ./SMP directories.

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
