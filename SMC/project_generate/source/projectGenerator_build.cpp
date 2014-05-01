
#include "projectGenerator.h"

void projectGenerator::buildDependencies( const string & sProjectName, StaticList & vLibs, StaticList & vAddLibs, StaticList & vIncludeDirs, StaticList & vLib32Dirs, StaticList & vLib64Dirs )
{
    //Add any forced dependencies
    if( sProjectName.compare( "libavformat" ) == 0 )
    {
        vAddLibs.push_back( "ws2_32" ); //Add the additional required libs
    }

    //Determine only those dependencies that are valid for current project
    map<string,bool> mProjectDeps;
    buildProjectDependencies( sProjectName, mProjectDeps );

    //Loop through each known configuration option and add the required dependencies
    vector<string> vExternLibs;
    m_ConfigHelper.getConfigList( "EXTERNAL_LIBRARY_LIST", vExternLibs );
    //Add extra external libraries
    vExternLibs.push_back( "vfwcap_indev" );
    vExternLibs.push_back( "dshow_indev" );
    vExternLibs.push_back( "sdl" );
    for( vector<string>::iterator vitLib=vExternLibs.begin(); vitLib<vExternLibs.end(); vitLib++ )
    {
        //Check if enabled
        if( m_ConfigHelper.getConfigOption( *vitLib )->m_sValue.compare("1") == 0 )
        {
            //Check if this dependency is valid for this project (if the dependency is not known default to enable)
            if( mProjectDeps.find( *vitLib ) == mProjectDeps.end() )
            {
                cout << "  Warning: Unknown dependency found (" << *vitLib << ")" << endl;
            }
            else
            {
                if( !mProjectDeps[*vitLib] )
                {
                    //This dependency is not valid for this project so skip it
                    continue;
                }
            }

            string sLib;
            if( vitLib->compare("avisynth") == 0 )
            {
                //doesnt need any additional libs
            }
            else if( vitLib->compare("bzlib") == 0 )
            {
                sLib = "libbz2";
            }
            else if( vitLib->compare("zlib") == 0 )
            {
                sLib = "libz";
            }
            else if( vitLib->compare("libfdk_aac") == 0 )
            {
                sLib = "libfdk-aac";
            }
            else if( vitLib->compare("libxvid") == 0 )
            {
                sLib = "libxvidcore";
            }
            else if( vitLib->compare("openssl") == 0 )
            {
                //Needs ws2_32 but libavformat needs this even if not using openssl so it is already included
                sLib = "libopenssl";
            }
            else if( vitLib->compare("vfwcap_indev") == 0 )
            {
                vAddLibs.push_back( "vfw32" ); //Add the additional required libs
            }
            else if( vitLib->compare("dshow_indev") == 0 )
            {
                vAddLibs.push_back( "strmiids" ); //Add the additional required libs
            }
            else if( vitLib->compare("sdl") == 0 )
            {
                vAddLibs.push_back( "SDL" ); //Add the additional required libs
            }
            else if( vitLib->compare( "opengl" ) == 0 )
            {
                vAddLibs.push_back( "Opengl32" ); //Add the additional required libs
            }
            else if( vitLib->compare( "opencl" ) == 0 )
            {
                vAddLibs.push_back( "OpenCL" ); //Add the additional required libs
            }
            else if( vitLib->compare( "openal" ) == 0 )
            {
                vAddLibs.push_back( "OpenAL32" ); //Add the additional required libs
            }
            else
            {
                //By default just use the lib name and prefix with lib if not already
                if( vitLib->find( "lib" ) == 0 )
                {
                    sLib = *vitLib;
                }
                else
                {
                    sLib = "lib" + *vitLib;
                }
            }
            if( sLib.length() > 0 )
            {
                //Check already not in list
                vector<string>::iterator vitList=vLibs.begin();
                for( vitList; vitList<vLibs.end(); vitList++ )
                {
                    if( vitList->compare( sLib ) == 0 )
                    {
                        break;
                    }
                }
                if( vitList == vLibs.end() )
                {
                    vLibs.push_back( sLib );
                }
            }

            //Add in the additional include directories
            if( vitLib->compare("libopus") == 0 )
            {
                vIncludeDirs.push_back("$(OutDir)\\include\\opus");
            }
            else if( vitLib->compare("libfreetype") == 0 )
            {
                vIncludeDirs.push_back("$(OutDir)\\include\\freetype2");
            }
            else if( vitLib->compare("sdl") == 0 )
            {
                vIncludeDirs.push_back("$(OutDir)\\include\\SDL");
            }
            else if( vitLib->compare( "opengl" ) == 0 )
            {
                //Requires glext headers to be installed in include dir (does not require the libs)
            }
            else if( vitLib->compare( "opencl" ) == 0 )
            {
                //Need to check for the existence of environment variables
                if( GetEnvironmentVariable( "AMDAPPSDKROOT", NULL, 0 ) )
                {
                    vIncludeDirs.push_back( "$(AMDAPPSDKROOT)\\include\\" );
                    vLib32Dirs.push_back( "$(AMDAPPSDKROOT)\\lib\\Win32" );
                    vLib64Dirs.push_back( "$(AMDAPPSDKROOT)\\lib\\x64" );
                }
                else if( GetEnvironmentVariable( "INTELOCLSDKROOT", NULL, 0 ) )
                {
                    vIncludeDirs.push_back( "$(INTELOCLSDKROOT)\\include\\" );
                    vLib32Dirs.push_back( "$(INTELOCLSDKROOT)\\lib\\x86" );
                    vLib64Dirs.push_back( "$(INTELOCLSDKROOT)\\lib\\x64" );
                }
                else if( GetEnvironmentVariable( "CUDA_PATH", NULL, 0 ) )
                {
                    cout << "  Warning: NVIDIA OpenCl currently is only 1.1. OpenCl 1.2 is needed for FFMpeg support" << endl;
                    vIncludeDirs.push_back( "$(CUDA_PATH)\\include\\" );
                    vLib32Dirs.push_back( "$(CUDA_PATH)\\lib\\Win32" );
                    vLib64Dirs.push_back( "$(CUDA_PATH)\\lib\\x64" );
                }
                else
                {
                    cout << "  Warning: Could not find an OpenCl SDK environment variable." << endl;
                    cout << "    Either an OpenCL SDK is not installed or the environment variables are missing." << endl;
                }
            }
            else if( vitLib->compare( "openal" ) == 0 )
            {
                if( !GetEnvironmentVariable( "OPENAL_SDK", NULL, 0 ) )
                {
                    cout << "  Warning: Could not find the OpenAl SDK environment variable." << endl;
                    cout << "    Either the OpenAL SDK is not installed or the environment variable is missing." << endl;
                    cout << "    Using the default environment variable of 'OPENAL_SDK'." << endl;
                }
                vIncludeDirs.push_back( "$(OPENAL_SDK)\\include\\" );
                vLib32Dirs.push_back( "$(OPENAL_SDK)\\libs\\Win32" );
                vLib64Dirs.push_back( "$(CUDA_PATH)\\lib\\Win64" );
            }
        }
    }
}

void projectGenerator::buildProjectDependencies( const string & sProjectName, map<string,bool> & mProjectDeps )
{
    mProjectDeps["avisynth"] = false; //no dependencies ever needed
    mProjectDeps["bzlib"] = ( sProjectName.compare("libavformat") == 0 ) || ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["crystalhd"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["fontconfig"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["libfontconfig"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["frei0r"] = ( sProjectName.compare("libpostproc") == 0 );//??
    mProjectDeps["gnutls"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["iconv"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["ladspa"] = ( sProjectName.compare("libavfilter") == 0 );//?
    mProjectDeps["libaacplus"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libass"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libbluray"] = ( sProjectName.compare("libavformat") == 0 );//?
    mProjectDeps["libcaca"] = ( sProjectName.compare("libavdevice") == 0 );//????
    mProjectDeps["libcdio"] = ( sProjectName.compare("libavdevice") == 0 );//??
    mProjectDeps["libcelt"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libdc1394"] = ( sProjectName.compare("libavdevice") == 0 );//?
    mProjectDeps["libfaac"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libfdk_aac"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libflite"] = ( sProjectName.compare("libavfilter") == 0 );//??
    mProjectDeps["libfreetype"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libgme"] = ( sProjectName.compare("libavformat") == 0 );//??
    mProjectDeps["libgsm"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libiec61883"] = ( sProjectName.compare("libavdevice") == 0 );//?
    mProjectDeps["libilbc"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libmodplug"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libmp3lame"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libnut"] = ( sProjectName.compare("libformat") == 0 );
    mProjectDeps["libopencore_amrnb"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopencore_amrwb"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopencv"] = ( sProjectName.compare("libavfilter") == 0 );//??
    mProjectDeps["libopenjpeg"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopus"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libpulse"] = ( sProjectName.compare("libavdevice") == 0 );//?
    mProjectDeps["libquvi"] = ( sProjectName.compare("libavformat") == 0 );//??
    mProjectDeps["librtmp"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libschroedinger"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libshine"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libsoxr"] = ( sProjectName.compare("libswresample") == 0 );
    mProjectDeps["libspeex"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libssh"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libstagefright_h264"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libtheora"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libtwolame"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libutvideo"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libv4l2"] = ( sProjectName.compare("libavdevice") == 0 );//?
    mProjectDeps["libvidstab"] = ( sProjectName.compare("libavfilter") == 0 );//??
    mProjectDeps["libvo_aacenc"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libvo_amrwbenc"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libvorbis"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libvpx"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libwavpack"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libwebp"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libx264"] = ( sProjectName.compare( "libavcodec" ) == 0 );
    mProjectDeps["libx265"] = ( sProjectName.compare( "libavcodec" ) == 0 );
    mProjectDeps["libxavs"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libxvid"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libzmq"] = ( sProjectName.compare("libavfilter") == 0 );//??
    mProjectDeps["libzvbi"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["openal"] = ( sProjectName.compare("libavdevice") == 0 );//?
    mProjectDeps["opencl"] = ( sProjectName.compare( "libavutil" ) == 0 ) || ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["opengl"] = ( sProjectName.compare( "libavdevice" ) == 0 );
    mProjectDeps["openssl"] = ( sProjectName.compare("libavformat") == 0 );
    //mProjectDeps["x11grab"] = ( sProjectName.compare("libavdevice") == 0 );//Always disabled on Win32
    mProjectDeps["zlib"] = ( sProjectName.compare("libavformat") == 0 ) || ( sProjectName.compare("libavcodec") == 0 );

    //extras
    mProjectDeps["vfwcap_indev"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["dshow_indev"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["sdl"] = ( sProjectName.compare("libavdevice") == 0 );
}

void projectGenerator::buildProgramIncludes( const string & sProject, vector<string> & vCIncludes, vector<string> & vHIncludes, vector<string> & vLibs, vector<string> & vIncDirs )
{
    vCIncludes.clear( );
    vHIncludes.clear( );
    vLibs.clear( );
    vIncDirs.clear( );

    //All projects include cmdutils
    vCIncludes.push_back( "..\\cmdutils.c" );
    if( m_ConfigHelper.getConfigOption( "opencl" )->m_sValue.compare( "1" ) == 0 )
    {
        vCIncludes.push_back( "..\\cmdutils_opencl.c" );
        //Need to check for the existence of environment variables
        if( GetEnvironmentVariable( "AMDAPPSDKROOT", NULL, 0 ) )
        {
            vIncDirs.push_back( "$(AMDAPPSDKROOT)\\include\\" );
        }
        else if( GetEnvironmentVariable( "INTELOCLSDKROOT", NULL, 0 ) )
        {
            vIncDirs.push_back( "$(INTELOCLSDKROOT)\\include\\" );
        }
        else if( GetEnvironmentVariable( "CUDA_PATH", NULL, 0 ) )
        {
            cout << "  Warning: NVIDIA OpenCl currently is only 1.1. OpenCl 1.2 is needed for FFMpeg support" << endl;
            vIncDirs.push_back( "$(CUDA_PATH)\\include\\" );
        }
    }
    vHIncludes.push_back( "..\\cmdutils.h" );
    vHIncludes.push_back( "..\\cmdutils_common_opts.h" );

    if( sProject.compare( "ffmpeg" ) == 0 )
    {
        vCIncludes.push_back( "..\\ffmpeg.c" );
        vCIncludes.push_back( "..\\ffmpeg_filter.c" );
        vCIncludes.push_back( "..\\ffmpeg_opt.c" );

        vHIncludes.push_back( "..\\ffmpeg.h" );
    }
    else if( sProject.compare( "ffplay" ) == 0 )
    {
        vCIncludes.push_back( "..\\ffplay.c" );

        vHIncludes.push_back( "..\\ffmpeg.h" );

        vLibs.push_back( "SDL.lib" );
        vLibs.push_back( "SDLmain.lib" );

        vIncDirs.push_back( "$(OutDir)\\include\\SDL" );
    }
    else if( sProject.compare( "ffprobe" ) == 0 )
    {
        vCIncludes.push_back( "..\\ffprobe.c" );

        vHIncludes.push_back( "..\\ffmpeg.h" );
    }
    else if( sProject.compare( "avconv" ) == 0 )
    {
        vCIncludes.push_back( "..\\avconv.c" );
        vCIncludes.push_back( "..\\avconv_filter.c" );
        vCIncludes.push_back( "..\\avconv_opt.c" );

        vHIncludes.push_back( "..\\avconv.h" );
    }
    else if( sProject.compare( "avplay" ) == 0 )
    {
        vCIncludes.push_back( "..\\avplay.c" );

        vHIncludes.push_back( "..\\avconv.h" );

        vLibs.push_back( "SDL.lib" );
        vLibs.push_back( "SDLmain.lib" );

        vIncDirs.push_back( "$(OutDir)\\include\\SDL" );
    }
    else if( sProject.compare( "avprobe" ) == 0 )
    {
        vCIncludes.push_back( "..\\avprobe.c" );

        vHIncludes.push_back( "..\\avconv.h" );
    }
}