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

#include "configGenerator.h"

#include <algorithm>

bool configGenerator::buildDefaultValues( )
{
    // configurable options
    vector<string> vList;
    if( !getConfigList( "PROGRAM_LIST", vList ) )
    {
        return false;
    }
    //Enable all programs
    vector<string>::iterator vitValues = vList.begin( );
    for( vitValues; vitValues < vList.end( ); vitValues++ )
    {
        toggleConfigValue( *vitValues, true );
    }
    //Enable all libraries
    vList.resize( 0 );
    if( !getConfigList( "LIBRARY_LIST", vList ) )
    {
        return false;
    }
    vitValues = vList.begin( );
    for( vitValues; vitValues < vList.end( ); vitValues++ )
    {
        if( !m_bLibav && vitValues->compare( "avresample" ) != 0 )
        {
            toggleConfigValue( *vitValues, true );
        }
    }
    //Enable all components
    vList.resize( 0 );
    vector<string> vList2;
    if( !getConfigList( "COMPONENT_LIST", vList ) )
    {
        return false;
    }
    vitValues = vList.begin( );
    for( vitValues; vitValues < vList.end( ); vitValues++ )
    {
        toggleConfigValue( *vitValues, true );
        //Get the corresponding list and enable all member elements as well
        vitValues->resize( vitValues->length( ) - 1 ); //Need to remove the s from end
        transform( vitValues->begin( ), vitValues->end( ), vitValues->begin( ), ::toupper );
        //Get the specific list
        vList2.resize( 0 );
        getConfigList( *vitValues + "_LIST", vList2 );
        for( vector<string>::iterator vitComponent = vList2.begin( ); vitComponent < vList2.end( ); vitComponent++ )
        {
            toggleConfigValue( *vitComponent, true );
        }
    }


    fastToggleConfigValue( "runtime_cpudetect", true );
    fastToggleConfigValue( "safe_bitstream_reader", true );
    fastToggleConfigValue( "static", true );
    fastToggleConfigValue( "shared", true );
    fastToggleConfigValue( "swscale_alpha", true );

    // Enable hwaccels by default.
    fastToggleConfigValue( "d3d11va", true );
    fastToggleConfigValue( "dxva2", true );

    //Enable x86 hardware architectures
    fastToggleConfigValue( "x86", true );
    fastToggleConfigValue( "i686", true );
    fastToggleConfigValue( "fast_cmov", true );
    fastToggleConfigValue( "x86_32", true );
    fastToggleConfigValue( "x86_64", true );
    //Enable x86 extensions
    vList.resize( 0 );
    if( !getConfigList( "ARCH_EXT_LIST_X86", vList ) )
    {
        return false;
    }
    vitValues = vList.begin( );
    for( vitValues; vitValues < vList.end( ); vitValues++ )
    {
        fastToggleConfigValue( *vitValues, true );
        //Also enable _EXTERNAL and _INLINE
        fastToggleConfigValue( *vitValues + "_EXTERNAL", true );
        fastToggleConfigValue( *vitValues + "_INLINE", true );
    }

    //Default we enable yasm
    fastToggleConfigValue( "yasm", true );

    //msvc specific options
    fastToggleConfigValue( "w32threads", true );
    fastToggleConfigValue( "atomics_win32", true );

    //math functions
    vList.resize( 0 );
    if( !getConfigList( "MATH_FUNCS", vList ) )
    {
        return false;
    }
    vitValues = vList.begin( );
    for( vitValues; vitValues < vList.end( ); vitValues++ )
    {
        fastToggleConfigValue( *vitValues, true );
    }

    fastToggleConfigValue( "access", true );
    fastToggleConfigValue( "aligned_malloc", true );

    fastToggleConfigValue( "closesocket", true );
    fastToggleConfigValue( "CommandLineToArgvW", true );
    fastToggleConfigValue( "CoTaskMemFree", true );
    fastToggleConfigValue( "cpunop", true );
    fastToggleConfigValue( "CryptGenRandom", true );
    fastToggleConfigValue( "direct_h", true );
    fastToggleConfigValue( "d3d11_h", true );
    fastToggleConfigValue( "dxva_h", true );
    fastToggleConfigValue( "ebp_available", true );
    fastToggleConfigValue( "ebx_available", true );
    fastToggleConfigValue( "fast_clz", true );
    fastToggleConfigValue( "flt_lim", true );
    fastToggleConfigValue( "getaddrinfo", true );
    fastToggleConfigValue( "getopt", false );
    fastToggleConfigValue( "GetProcessAffinityMask", true );
    fastToggleConfigValue( "GetProcessMemoryInfo", true );
    fastToggleConfigValue( "GetProcessTimes", true );
    fastToggleConfigValue( "GetSystemTimeAsFileTime", true );
    fastToggleConfigValue( "io_h", true );
    fastToggleConfigValue( "inline_asm_labels", true );
    //Additional options set for Intel compiler specific inline asm
    fastToggleConfigValue( "inline_asm_nonlocal_labels", false );
    fastToggleConfigValue( "inline_asm_direct_symbol_refs", false );
    fastToggleConfigValue( "inline_asm_non_intel_mnemonic", false );
    fastToggleConfigValue( "isatty", true );
    fastToggleConfigValue( "kbhit", true );
    fastToggleConfigValue( "libc_msvcrt", true );
    fastToggleConfigValue( "local_aligned_32", true );
    fastToggleConfigValue( "local_aligned_16", true );
    fastToggleConfigValue( "local_aligned_8", true );
    fastToggleConfigValue( "malloc_h", true );
    fastToggleConfigValue( "MapViewOfFile", true );
    fastToggleConfigValue( "MemoryBarrier", true );
    fastToggleConfigValue( "mm_empty", true );
    fastToggleConfigValue( "PeekNamedPipe", true );
    fastToggleConfigValue( "rdtsc", true );
    fastToggleConfigValue( "rsync_contimeout", true );
    fastToggleConfigValue( "SetConsoleTextAttribute", true );
    fastToggleConfigValue( "SetConsoleCtrlHandler", true );
    fastToggleConfigValue( "setmode", true );
    fastToggleConfigValue( "Sleep", true );
    fastToggleConfigValue( "CONDITION_VARIABLE_Ptr", true );
    fastToggleConfigValue( "socklen_t", true );
    fastToggleConfigValue( "struct_addrinfo", true );
    fastToggleConfigValue( "struct_group_source_req", true );
    fastToggleConfigValue( "struct_ip_mreq_source", true );
    fastToggleConfigValue( "struct_ipv6_mreq", true );
    fastToggleConfigValue( "struct_pollfd", true );
    fastToggleConfigValue( "struct_sockaddr_in6", true );
    fastToggleConfigValue( "struct_sockaddr_storage", true );
    fastToggleConfigValue( "unistd_h", true );
    fastToggleConfigValue( "VirtualAlloc", true );
    fastToggleConfigValue( "windows_h", true );
    fastToggleConfigValue( "winsock2_h", true );
    fastToggleConfigValue( "wglgetprocaddress", true );

    fastToggleConfigValue( "dos_paths", true );
    fastToggleConfigValue( "dxva2api_cobj", true );
    fastToggleConfigValue( "dxva2_lib", true );

    fastToggleConfigValue( "aligned_stack", true );
    fastToggleConfigValue( "pragma_deprecated", true );
    fastToggleConfigValue( "inline_asm", true );
    fastToggleConfigValue( "frame_thread_encoder", true );
    fastToggleConfigValue( "xmm_clobbers", true );

    fastToggleConfigValue( "xlib", false ); //enabled by default but is linux only so we force disable
    fastToggleConfigValue( "qtkit", false );
    fastToggleConfigValue( "avfoundation", false );

    //Additional (must be explicitly disabled)
    fastToggleConfigValue( "dct", true );
    fastToggleConfigValue( "dwt", true );
    fastToggleConfigValue( "error_resilience", true );
    fastToggleConfigValue( "faan", true );
    fastToggleConfigValue( "faandct", true );
    fastToggleConfigValue( "faanidct", true );
    fastToggleConfigValue( "fast_unaligned", true );
    fastToggleConfigValue( "lsp", true );
    fastToggleConfigValue( "lzo", true );
    fastToggleConfigValue( "mdct", true );
    fastToggleConfigValue( "network", true );
    fastToggleConfigValue( "rdft", true );
    fastToggleConfigValue( "fft", true );
    fastToggleConfigValue( "pixelutils", true );

    fastToggleConfigValue( "bzlib", true );
    fastToggleConfigValue( "iconv", true );
    fastToggleConfigValue( "lzma", true );
    fastToggleConfigValue( "sdl", true );
    fastToggleConfigValue( "zlib", true );

    return true;
}

void configGenerator::buildFixedValues( DefaultValuesList & mFixedValues )
{
    mFixedValues.clear( );
    mFixedValues["$(c_escape $FFMPEG_CONFIGURATION)"] = "";
    mFixedValues["$(c_escape $LIBAV_CONFIGURATION)"] = "";
    mFixedValues["$(c_escape $license)"] = "lgpl";
    mFixedValues["$(eval c_escape $datadir)"] = ".";
    mFixedValues["$(c_escape ${cc_ident:-Unknown compiler})"] = "msvc";
    mFixedValues["$_restrict"] = "__restrict";
    mFixedValues["${extern_prefix}"] = "";
    mFixedValues["$build_suffix"] = "";
    mFixedValues["$SLIBSUF"] = "";
    mFixedValues["$sws_max_filter_size"] = "256";
}

void configGenerator::buildReplaceValues( DefaultValuesList & mReplaceValues, DefaultValuesList & mASMReplaceValues )
{
    mReplaceValues.clear( );
    //Add to config.h only list
    mReplaceValues["CC_IDENT"] = "#if defined(__INTEL_COMPILER)\n\
#   define CC_IDENT \"icl\"\n\
#else\n\
#   define CC_IDENT \"msvc\"\n\
#endif";
    mReplaceValues["EXTERN_PREFIX"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define EXTERN_PREFIX \"\"\n\
#else\n\
#   define EXTERN_PREFIX \"_\"\n\
#endif";
    mReplaceValues["EXTERN_ASM"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define EXTERN_ASM\n\
#else\n\
#   define EXTERN_ASM _\n\
#endif";
    mReplaceValues["SLIBSUF"] = "#if defined(_USRDLL) || defined(_WINDLL)\n\
#   define SLIBSUF \".dll\"\n\
#else\n\
#   define SLIBSUF \".lib\"\n\
#endif";

    mReplaceValues["ARCH_X86_32"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define ARCH_X86_32 0\n\
#else\n\
#   define ARCH_X86_32 1\n\
#endif";
    mReplaceValues["ARCH_X86_64"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define ARCH_X86_64 1\n\
#else\n\
#   define ARCH_X86_64 0\n\
#endif";
    mReplaceValues["CONFIG_SHARED"] = "#if defined(_USRDLL) || defined(_WINDLL)\n\
#   define CONFIG_SHARED 1\n\
#else\n\
#   define CONFIG_SHARED 0\n\
#endif";
    mReplaceValues["CONFIG_STATIC"] = "#if defined(_USRDLL) || defined(_WINDLL)\n\
#   define CONFIG_STATIC 0\n\
#else\n\
#   define CONFIG_STATIC 1\n\
#endif";
    mReplaceValues["HAVE_ALIGNED_STACK"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define HAVE_ALIGNED_STACK 1\n\
#else\n\
#   define HAVE_ALIGNED_STACK 0\n\
#endif";
    mReplaceValues["HAVE_FAST_64BIT"] = "#if defined(__x86_64) || defined(_M_X64)\n\
#   define HAVE_FAST_64BIT 1\n\
#else\n\
#   define HAVE_FAST_64BIT 0\n\
#endif";
    mReplaceValues["HAVE_INLINE_ASM"] = "#if defined(__INTEL_COMPILER)\n\
#   define HAVE_INLINE_ASM 1\n\
#else\n\
#   define HAVE_INLINE_ASM 0\n\
#endif";
    mReplaceValues["HAVE_MM_EMPTY"] = "#if defined(__INTEL_COMPILER) || ARCH_X86_32\n\
#   define HAVE_MM_EMPTY 1\n\
#else\n\
#   define HAVE_MM_EMPTY 0\n\
#endif";
    mReplaceValues["HAVE_STRUCT_POLLFD"] = "#if !defined(_WIN32_WINNT) || _WIN32_WINNT >= 0x0600\n\
#   define HAVE_STRUCT_POLLFD 1\n\
#else\n\
#   define HAVE_STRUCT_POLLFD 0\n\
#endif";

    //Build replace values for all inline asm
    vector<string> vInlineList;
    getConfigList( "ARCH_EXT_LIST", vInlineList );
    for( vector<string>::iterator vitIt=vInlineList.begin(); vitIt<vInlineList.end(); vitIt++ )
    {
        transform( vitIt->begin(), vitIt->end(), vitIt->begin(), ::toupper);
        string sName = "HAVE_" + *vitIt + "_INLINE";
        mReplaceValues[sName] = "#define " + sName + " HAVE_INLINE_ASM";
    }

    //Sanity checks for inline asm (Needed as some code only checks availability and not inline_asm)
    mReplaceValues["HAVE_EBP_AVAILABLE"] = "#if HAVE_INLINE_ASM && !defined(_DEBUG)\n\
#   define HAVE_EBP_AVAILABLE 1\n\
#else\n\
#   define HAVE_EBP_AVAILABLE 0\n\
#endif";
    mReplaceValues["HAVE_EBX_AVAILABLE"] = "#if HAVE_INLINE_ASM && !defined(_DEBUG)\n\
#   define HAVE_EBX_AVAILABLE 1\n\
#else\n\
#   define HAVE_EBX_AVAILABLE 0\n\
#endif";

    //Add to config.asm only list
    mASMReplaceValues["ARCH_X86_32"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define ARCH_X86_32 0\n\
%elifidn __OUTPUT_FORMAT__,win64\n\
%define ARCH_X86_32 0\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define ARCH_X86_32 1\n\
%define PREFIX\n\
%endif";
    mASMReplaceValues["ARCH_X86_64"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define ARCH_X86_64 1\n\
%elifidn __OUTPUT_FORMAT__,win64\n\
%define ARCH_X86_64 1\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define ARCH_X86_64 0\n\
%endif";
    mASMReplaceValues["HAVE_ALIGNED_STACK"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define HAVE_ALIGNED_STACK 1\n\
%elifidn __OUTPUT_FORMAT__,win64\n\
%define HAVE_ALIGNED_STACK 1\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define HAVE_ALIGNED_STACK 0\n\
%endif";
    mASMReplaceValues["HAVE_FAST_64BIT"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define HAVE_FAST_64BIT 1\n\
%elifidn __OUTPUT_FORMAT__,win64\n\
%define HAVE_FAST_64BIT 1\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define HAVE_FAST_64BIT 0\n\
%endif";
}

void configGenerator::buildReservedValues( vector<string> & vReservedItems )
{
    vReservedItems.resize(0);
    //The following are reserved values that are automatically handled and can not be set explicitly
    vReservedItems.push_back( "x86_32" );
    vReservedItems.push_back( "x86_64" );
    vReservedItems.push_back( "xmm_clobbers" );
    vReservedItems.push_back( "shared" );
    vReservedItems.push_back( "static" );
    vReservedItems.push_back( "aligned_stack" );
    vReservedItems.push_back( "fast_64bit" );
    vReservedItems.push_back( "mm_empty" );
    vReservedItems.push_back( "ebp_available" );
    vReservedItems.push_back( "ebx_available" );
    vReservedItems.push_back( "debug" );
}

void configGenerator::buildAdditionalDependencies( DependencyList & mAdditionalDependencies )
{
    mAdditionalDependencies.clear( );
    mAdditionalDependencies["capCreateCaptureWindow"] = true;
    mAdditionalDependencies["CreateDIBSection"] = true;
    mAdditionalDependencies["dv1394"] = false;
    mAdditionalDependencies["DXVA_PicParams_HEVC"] = true;
    mAdditionalDependencies["dxva2api_h"] = true;
    mAdditionalDependencies["jack_jack_h"] = false;
    mAdditionalDependencies["IBaseFilter"] = true;
    mAdditionalDependencies["ID3D11VideoDecoder"] = true;
    mAdditionalDependencies["ID3D11VideoContext"] = true;
    mAdditionalDependencies["libcrystalhd_libcrystalhd_if_h"] = false;
    mAdditionalDependencies["linux_fb_h"] = false;
    mAdditionalDependencies["linux_videodev_h"] = false;
    mAdditionalDependencies["linux_videodev2_h"] = false;
    mAdditionalDependencies["DXVA2_ConfigPictureDecode"] = true;
    mAdditionalDependencies["snd_pcm_htimestamp"] = false;
    mAdditionalDependencies["va_va_h"] = false;
    mAdditionalDependencies["vdpau_vdpau_h"] = false;
    mAdditionalDependencies["vdpau_vdpau_x11_h"] = false;
    mAdditionalDependencies["vfwcap_defines"] = true;
    mAdditionalDependencies["VideoDecodeAcceleration_VDADecoder_h"] = false;
    mAdditionalDependencies["X11_extensions_Xvlib_h"] = false;
    mAdditionalDependencies["X11_extensions_XvMClib_h"] = false;
}

void configGenerator::buildOptimisedDisables( OptimisedConfigList & mOptimisedDisables )
{
    //This used is to return prioritised version of different config options
    //  For instance If enabling the decoder from an passed in library that is better than the inbuilt one
    //  then simply disable the inbuilt so as to avoid unnecessary compilation
    //This may have issues should a user not want to disable these but currently there are static compilation errors
    //  that will occur as several of these overlapping decoder/encoders have similar named methods that cause link errors.

    mOptimisedDisables.clear( );
    //From trac.ffmpeg.org/wiki/GuidelinesHighQualityAudio
    //Dolby Digital: ac3
    //Dolby Digital Plus: eac3
    //MP2: libtwolame, mp2
    //Windows Media Audio 1: wmav1
    //Windows Media Audio 2: wmav2
    //LC-AAC: libfdk_aac, libfaac, aac, libvo_aacenc
    //HE-AAC: libfdk_aac, libaacplus
    //Vorbis: libvorbis, vorbis
    //MP3: libmp3lame, libshine
    //Opus: libopus
    //libopus >= libvorbis >= libfdk_aac > libmp3lame > libfaac >= eac3/ac3 > aac > libtwolame > vorbis > mp2 > wmav2/wmav1 > libvo_aacenc

    //*****Encoder optimization is currently ignored as people may want to compare encoders. The commandline should be used to disable unwanted encoders*****//

    //mOptimisedDisables["LIBTWOLAME_ENCODER"].push_back( "MP2_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBFAAC_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "AAC_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBAACPLUS_ENCODER" );
    //mOptimisedDisables["LIBFAAC_ENCODER"].push_back( "AAC_ENCODER" );
    //mOptimisedDisables["LIBFAAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );
    //mOptimisedDisables["AAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );
    //mOptimisedDisables["LIBVORBIS_ENCODER"].push_back( "VORBIS_ENCODER" );
    //mOptimisedDisables["LIBMP3LAME_ENCODER"].push_back( "LIBSHINE_ENCODER" );
    //mOptimisedDisables["LIBOPENJPEG_ENCODER"].push_back( "JPEG2000_ENCODER" );//???
    //mOptimisedDisables["LIBUTVIDEO_ENCODER"].push_back( "UTVIDEO_ENCODER" );//???
    //mOptimisedDisables["LIBWAVPACK_ENCODER"].push_back( "WAVPACK_ENCODER" );//???

    mOptimisedDisables["LIBGSM_DECODER"].push_back( "GSM_DECODER" );//???
    mOptimisedDisables["LIBGSM_MS_DECODER"].push_back( "GSM_MS_DECODER" );//???
    mOptimisedDisables["LIBNUT_MUXER"].push_back( "NUT_MUXER" );
    mOptimisedDisables["LIBNUT_DEMUXER"].push_back( "NUT_DEMUXER" );
    mOptimisedDisables["LIBOPENCORE_AMRNB_DECODER"].push_back( "AMRNB_DECODER" );//???
    mOptimisedDisables["LIBOPENCORE_AMRWB_DECODER"].push_back( "AMRWB_DECODER" );//???
    mOptimisedDisables["LIBOPENJPEG_DECODER"].push_back( "JPEG2000_DECODER" );//???
    mOptimisedDisables["LIBSCHROEDINGER_DECODER"].push_back( "DIRAC_DECODER" );
    mOptimisedDisables["LIBSTAGEFRIGHT_H264_DECODER"].push_back( "H264_DECODER" );
    mOptimisedDisables["LIBUTVIDEO_DECODER"].push_back( "UTVIDEO_DECODER" );//???
    mOptimisedDisables["VP8_DECODER"].push_back( "LIBVPX_VP8_DECODER" );//Inbuilt native decoder is apparently faster
    mOptimisedDisables["VP9_DECODER"].push_back( "LIBVPX_VP9_DECODER" );
    mOptimisedDisables["OPUS_DECODER"].push_back( "LIBOPUS_DECODER" );//??? Not sure which is better
}

#define CHECKFORCEDENABLES( Opt ) { if( getConfigOption( Opt ) != m_vConfigValues.end( ) ){ vForceEnable.push_back( Opt ); } }

void configGenerator::buildForcedEnables( string sOptionLower, vector<string> & vForceEnable )
{
    if( sOptionLower.compare( "fontconfig" ) == 0 )
    {
        CHECKFORCEDENABLES( "libfontconfig" );
    }
    else if( sOptionLower.compare( "dxva2" ) == 0 )
    {
        CHECKFORCEDENABLES( "dxva2_lib" );
    }
    else if( sOptionLower.compare( "libcdio" ) == 0 )
    {
        CHECKFORCEDENABLES( "cdio_paranoia_paranoia_h" );
    }
    else if( sOptionLower.compare( "libmfx" ) == 0 )
    {
        CHECKFORCEDENABLES( "qsv" );
    }
    else if( sOptionLower.compare( "gnutls" ) == 0 )
    {
        CHECKFORCEDENABLES( "nettle" );//deprecated
        CHECKFORCEDENABLES( "gcrypt" );
        CHECKFORCEDENABLES( "gmp" );
    }
    else if( sOptionLower.compare( "dcadec" ) == 0 )
    {
        CHECKFORCEDENABLES( "struct_dcadec_exss_info_matrix_encoding" );
    }
}

void configGenerator::buildForcedDisables( string sOptionLower, vector<string> & vForceDisable )
{
    // Currently disable values are exact opposite of the corresponding enable ones
    buildForcedEnables( sOptionLower, vForceDisable );
}

void configGenerator::buildObjects( const string & sTag, vector<string> & vObjects )
{
    if( sTag.compare( "COMPAT_OBJS" ) == 0 )
    {
        vObjects.push_back( "msvcrt/snprintf" ); //msvc only provides _snprintf which does not conform to snprintf standard
        vObjects.push_back( "strtod" ); //msvc contains a strtod but it does not handle NaN's correctly
        vObjects.push_back( "getopt" );
    }
    else if( sTag.compare( "EMMS_OBJS__yes_" ) == 0 )
    {
        if( this->getConfigOption( "MMX_EXTERNAL" )->m_sValue.compare( "1" ) == 0 )
        {
            vObjects.push_back( "x86/emms" ); //yasm emms is not required in 32b but is for 64bit unless with icl
        }
    }
}