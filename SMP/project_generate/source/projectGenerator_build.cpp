/*
 * copyright (c) 2014 Matthew Oliver
 *
 * This file is part of ShiftMediaProject.
 *
 * ShiftMediaProject is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * ShiftMediaProject is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with ShiftMediaProject; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "projectGenerator.h"

#include <iostream>
#include <Windows.h>

void projectGenerator::buildInterDependenciesHelper( const StaticList & vConfigOptions, const StaticList & vAddDeps, StaticList & vLibs )
{
    bool bFound = false;
    for( StaticList::const_iterator itI = vConfigOptions.begin( ); itI < vConfigOptions.end( ); itI++ )
    {
        configGenerator::ValuesList::iterator itConfOpt = m_ConfigHelper.getConfigOption( *itI );
        bFound = ( ( itConfOpt != m_ConfigHelper.m_vConfigValues.end( ) ) && ( m_ConfigHelper.getConfigOption( *itI )->m_sValue.compare( "1" ) == 0 ) );
        if( !bFound )
        {
            break;
        }
    }
    if( bFound )
    {
        for( StaticList::const_iterator itI = vAddDeps.begin( ); itI < vAddDeps.end( ); itI++ )
        {
            string sSearchTag = "lib" + *itI;
            if( find( vLibs.begin( ), vLibs.end( ), sSearchTag ) == vLibs.end( ) )
            {
                vLibs.push_back( sSearchTag );
            }
        }
    }
}
void projectGenerator::buildInterDependencies( const string & sProjectName, StaticList & vLibs )
{
    //Get the lib dependencies from the configure file
    string sLibName = sProjectName.substr( 3 ) + "_deps";
    vector<string> vLibDeps;
    if( m_ConfigHelper.getConfigList( sLibName, vLibDeps, false ) )
    {
        for( vector<string>::iterator itI = vLibDeps.begin( ); itI < vLibDeps.end( ); itI++ )
        {
            string sSearchTag = "lib" + *itI;
            if( find( vLibs.begin( ), vLibs.end( ), sSearchTag ) == vLibs.end( ) )
            {
                vLibs.push_back( sSearchTag );
            }
        }
    }

    //Hard coded configuration checks for inter dependencies between different source libs.
    if( sProjectName.compare( "libavfilter" ) == 0 )
    {
        buildInterDependenciesHelper( { "amovie_filter" }, { "avformat", "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "aresample_filter" }, { "swresample" }, vLibs );
        buildInterDependenciesHelper( { "asyncts_filter" }, { "avresample" }, vLibs );
        buildInterDependenciesHelper( { "atempo_filter" }, { "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "ebur128_filter", "swresample" }, { "swresample" }, vLibs );
        buildInterDependenciesHelper( { "elbg_filter" }, { "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "fftfilt_filter" }, { "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "mcdeint_filter" }, { "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "movie_filter" }, { "avformat", "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "pan_filter" }, { "swresample" }, vLibs );
        buildInterDependenciesHelper( { "pp_filter" }, { "postproc" }, vLibs );
        buildInterDependenciesHelper( { "removelogo_filter" }, { "avformat", "avcodec", "swscale" }, vLibs );
        buildInterDependenciesHelper( { "resample_filter" }, { "avresample" }, vLibs );
        buildInterDependenciesHelper( { "sab_filter" }, { "swscale" }, vLibs );
        buildInterDependenciesHelper( { "scale_filter" }, { "swscale" }, vLibs );
        buildInterDependenciesHelper( { "showspectrum_filter" }, { "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "smartblur_filter" }, { "swscale" }, vLibs );
        buildInterDependenciesHelper( { "subtitles_filter" }, { "avformat", "avcodec" }, vLibs );
        buildInterDependenciesHelper( { "scale2ref_filter" }, { "swscale" }, vLibs );
    }
    else if( sProjectName.compare( "libavdevice" ) == 0 )
    {
        buildInterDependenciesHelper( { "lavfi_indev" }, { "avfilter" }, vLibs );
    }
    else if( sProjectName.compare( "libavcodec" ) == 0 )
    {
        buildInterDependenciesHelper( { "opus_decoder" }, { "swresample" }, vLibs );
    }
}

void projectGenerator::buildDependencies( const string & sProjectName, StaticList & vLibs, StaticList & vAddLibs )
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
            else if( !mProjectDeps[*vitLib] )
            {
                //This dependency is not valid for this project so skip it
                continue;
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
            else if( vitLib->compare( "libcdio" ) == 0 )
            {
                sLib = "libcdio_paranoia";
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
                sLib = "libssl";
            }
            else if( vitLib->compare("vfwcap_indev") == 0 )
            {
                vAddLibs.push_back( "vfw32" ); //Add the additional required libs
                vAddLibs.push_back( "shlwapi" );
            }
            else if( vitLib->compare("dshow_indev") == 0 )
            {
                vAddLibs.push_back( "strmiids" ); //Add the additional required libs
            }
            else if( vitLib->compare("decklink") == 0 )
            {
                //doesnt need any additional libs
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
            else if( vitLib->compare( "nvenc" ) == 0 )
            {
                //Doesnt require any additional libs
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
        }
    }
}

void projectGenerator::buildDependencyDirs(const string & sProjectName, StaticList & vIncludeDirs, StaticList & vLib32Dirs, StaticList & vLib64Dirs)
{
    //Determine only those dependencies that are valid for current project
    map<string, bool> mProjectDeps;
    buildProjectDependencies(sProjectName, mProjectDeps);

    //Loop through each known configuration option and add the required dependencies
    for (map<string, bool>::iterator mitLib = mProjectDeps.begin(); mitLib != mProjectDeps.end(); mitLib++) {
        //Check if enabled
        if (mitLib->second && m_ConfigHelper.getConfigOption(mitLib->first)->m_sValue.compare("1") == 0) {
            //Add in the additional include directories
            if (mitLib->first.compare("libopus") == 0) {
                vIncludeDirs.push_back("$(OutDir)\\include\\opus");
            } else if (mitLib->first.compare("libfreetype") == 0) {
                vIncludeDirs.push_back("$(OutDir)\\include\\freetype2");
            } else if (mitLib->first.compare("libfribidi") == 0) {
                vIncludeDirs.push_back("$(OutDir)\\include\\fribidi");
            } else if (mitLib->first.compare("sdl") == 0) {
                vIncludeDirs.push_back("$(OutDir)\\include\\SDL");
            } else if (mitLib->first.compare("opengl") == 0) {
                //Requires glext headers to be installed in include dir (does not require the libs)
            } else if (mitLib->first.compare("opencl") == 0) {
                //Need to check for the existence of environment variables
                if (GetEnvironmentVariable("AMDAPPSDKROOT", NULL, 0)) {
                    vIncludeDirs.push_back("$(AMDAPPSDKROOT)\\include\\");
                    vLib32Dirs.push_back("$(AMDAPPSDKROOT)\\lib\\Win32");
                    vLib64Dirs.push_back("$(AMDAPPSDKROOT)\\lib\\x64");
                } else if (GetEnvironmentVariable("INTELOCLSDKROOT", NULL, 0)) {
                    vIncludeDirs.push_back("$(INTELOCLSDKROOT)\\include\\");
                    vLib32Dirs.push_back("$(INTELOCLSDKROOT)\\lib\\x86");
                    vLib64Dirs.push_back("$(INTELOCLSDKROOT)\\lib\\x64");
                } else if (GetEnvironmentVariable("CUDA_PATH", NULL, 0)) {
                    cout << "  Warning: NVIDIA OpenCl currently is only 1.1. OpenCl 1.2 is needed for FFMpeg support" << endl;
                    vIncludeDirs.push_back("$(CUDA_PATH)\\include\\");
                    vLib32Dirs.push_back("$(CUDA_PATH)\\lib\\Win32");
                    vLib64Dirs.push_back("$(CUDA_PATH)\\lib\\x64");
                } else {
                    cout << "  Warning: Could not find an OpenCl SDK environment variable." << endl;
                    cout << "    Either an OpenCL SDK is not installed or the environment variables are missing." << endl;
                }
            } else if (mitLib->first.compare("openal") == 0) {
                if (!GetEnvironmentVariable("OPENAL_SDK", NULL, 0)) {
                    cout << "  Warning: Could not find the OpenAl SDK environment variable." << endl;
                    cout << "    Either the OpenAL SDK is not installed or the environment variable is missing." << endl;
                    cout << "    Using the default environment variable of 'OPENAL_SDK'." << endl;
                }
                vIncludeDirs.push_back("$(OPENAL_SDK)\\include\\");
                vLib32Dirs.push_back("$(OPENAL_SDK)\\libs\\Win32");
                vLib64Dirs.push_back("$(CUDA_PATH)\\lib\\Win64");
            } else if (mitLib->first.compare("nvenc") == 0) {
                //Need to check for the existence of environment variables
                if (!GetEnvironmentVariable("CUDA_PATH", NULL, 0)) {
                    cout << "  Warning: Could not find the CUDA SDK environment variable." << endl;
                    cout << "    Either the CUDA SDK is not installed or the environment variable is missing." << endl;
                    cout << "    NVENC requires CUDA to be installed with NVENC headers made available in the CUDA SDK include path." << endl;
                }
                vIncludeDirs.push_back("$(CUDA_PATH)\\include\\");
            }
        }
    }
}

void projectGenerator::buildProjectDependencies( const string & sProjectName, map<string,bool> & mProjectDeps )
{
    mProjectDeps["avisynth"] = false; //no dependencies ever needed
    mProjectDeps["bzlib"] = ( sProjectName.compare("libavformat") == 0 ) || ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["crystalhd"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["chromaprint"] = (sProjectName.compare("libavformat") == 0);
    mProjectDeps["decklink"] = (sProjectName.compare("libavdevice") == 0);
    mProjectDeps["libfontconfig"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["frei0r"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["gcrypt"] = (sProjectName.compare("libavformat") == 0);
    mProjectDeps["gmp"] = (sProjectName.compare("libavformat") == 0);
    mProjectDeps["gnutls"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["iconv"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["ladspa"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libaacplus"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libass"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libbluray"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libbs2b"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["libcaca"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["libcdio"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["libcelt"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libdc1394"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["libdcadec"] = (sProjectName.compare("libavcodec") == 0);
    mProjectDeps["libfaac"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libfdk_aac"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libflite"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libfreetype"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["libfribidi"] = ( sProjectName.compare( "libavfilter" ) == 0 );
    mProjectDeps["libgme"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libgsm"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libiec61883"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["libilbc"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libkvazaar"] = (sProjectName.compare("libavcodec") == 0);
    mProjectDeps["libmfx"] = (sProjectName.compare("libavcodec") == 0) || (sProjectName.compare("ffmpeg") == 0) || (sProjectName.compare("avconv") == 0);
    mProjectDeps["libmodplug"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libmp3lame"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libnut"] = ( sProjectName.compare("libformat") == 0 );
    mProjectDeps["libopencore_amrnb"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopencore_amrwb"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopencv"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libopenjpeg"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libopenh264"] = (sProjectName.compare("libavcodec") == 0);
    mProjectDeps["libopus"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libpulse"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["librubberband"] = (sProjectName.compare("libavfilter") == 0);
    mProjectDeps["libquvi"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["librtmp"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libschroedinger"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libshine"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libsmbclient"] = (sProjectName.compare("libavformat") == 0);
    mProjectDeps["libsnappy"] = (sProjectName.compare("libavcodec") == 0);
    mProjectDeps["libsoxr"] = ( sProjectName.compare("libswresample") == 0 );
    mProjectDeps["libspeex"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libssh"] = ( sProjectName.compare("libavformat") == 0 );
    mProjectDeps["libstagefright_h264"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libtesseract"] = (sProjectName.compare("libavfilter") == 0);
    mProjectDeps["libtheora"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libtwolame"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libutvideo"] = ( sProjectName.compare("libavcodec") == 0 );
    mProjectDeps["libv4l2"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["libvidstab"] = ( sProjectName.compare("libavfilter") == 0 );
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
    mProjectDeps["libzimg"] = (sProjectName.compare("libavfilter") == 0);
    mProjectDeps["libzmq"] = ( sProjectName.compare("libavfilter") == 0 );
    mProjectDeps["libzvbi"] = ( sProjectName.compare( "libavcodec" ) == 0 );
    mProjectDeps["lzma"] = ( sProjectName.compare( "libavcodec" ) == 0 );
    mProjectDeps["nvenc"] = ( sProjectName.compare( "libavcodec" ) == 0 );
    mProjectDeps["openal"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["opencl"] = ( sProjectName.compare( "libavutil" ) == 0 ) || ( sProjectName.compare( "libavfilter" ) == 0 ) || ( sProjectName.compare( "ffmpeg" ) == 0 ) || ( sProjectName.compare( "avconv" ) == 0 )
        || (sProjectName.compare("ffplay") == 0) || (sProjectName.compare("avplay") == 0) || (sProjectName.compare("ffprobe") == 0) || (sProjectName.compare("avprobe") == 0);
    mProjectDeps["opengl"] = ( sProjectName.compare( "libavdevice" ) == 0 );
    mProjectDeps["openssl"] = ( sProjectName.compare( "libavformat" ) == 0 );
    mProjectDeps["schannel"] = (sProjectName.compare("libavformat") == 0);
    mProjectDeps["sdl"] = ( sProjectName.compare( "libavdevice" ) == 0 ) || (sProjectName.compare("ffplay") == 0) || (sProjectName.compare("avplay") == 0);
    //mProjectDeps["x11grab"] = ( sProjectName.compare("libavdevice") == 0 );//Always disabled on Win32
    mProjectDeps["zlib"] = ( sProjectName.compare("libavformat") == 0 ) || ( sProjectName.compare("libavcodec") == 0 );

    //extras
    mProjectDeps["vfwcap_indev"] = ( sProjectName.compare("libavdevice") == 0 );
    mProjectDeps["dshow_indev"] = ( sProjectName.compare("libavdevice") == 0 );
}

void projectGenerator::buildProjectGUIDs( map<string, string> & mKeys )
{
    mKeys["libavcodec"] = "B4824EFF-C340-425D-A4A8-E2E02A71A7AE";
    mKeys["libavdevice"] = "6E165FA4-44EB-4330-8394-9F0D76D8E03E";
    mKeys["libavfilter"] = "BC2E1028-66CD-41A0-AF90-EEBD8CC52787";
    mKeys["libavformat"] = "30A96E9B-8061-4F19-BD71-FDE7EA8F7929";
    mKeys["libavresample"] = "0096CB8C-3B04-462B-BF4F-0A9970A57C91";
    mKeys["libavutil"] = "CE6C44DD-6E38-4293-8AB3-04EE28CCA972";
    mKeys["libswresample"] = "3CE4A9EF-98B6-4454-B76E-3AD9C03A2114";
    mKeys["libswscale"] = "6D8A6330-8EBE-49FD-9281-0A396F9F28F2";
    mKeys["libpostproc"] = "4D9C457D-9ADA-4A12-9D06-42D80124C5AB";

    if( !m_ConfigHelper.m_bLibav )
    {
        mKeys["ffmpeg"] = "4081C77E-F1F7-49FA-9BD8-A4D267C83716";
        mKeys["ffplay"] = "E2A6865D-BD68-45B4-8130-EFD620F2C7EB";
        mKeys["ffprobe"] = "147A422A-FA63-4724-A5D9-08B1CAFDAB59";
    }
    else
    {
        mKeys["avconv"] = "4081C77E-F1F7-49FA-9BD8-A4D267C83716";
        mKeys["avplay"] = "E2A6865D-BD68-45B4-8130-EFD620F2C7EB";
        mKeys["avprobe"] = "147A422A-FA63-4724-A5D9-08B1CAFDAB59";
    }
}