
#include "configGenerator.h"

#include <algorithm>

void configGenerator::buildFixedValues( DefaultValuesList & mFixedValues )
{
    mFixedValues.clear( );
    mFixedValues["$(c_escape $FFMPEG_CONFIGURATION)"] = "";
    mFixedValues["$(c_escape $LIBAV_CONFIGURATION)"] = "";
    mFixedValues["$(c_escape $license)"] = "lgpl";
    mFixedValues["$(eval c_escape $datadir)"] = ".";
    mFixedValues["$(c_escape ${cc_ident:-Unknown compiler})"] = "msvc";
    mFixedValues["$_restrict"] = "restrict";
    mFixedValues["${extern_prefix}"] = "";
    mFixedValues["$build_suffix"] = "";
    mFixedValues["$SLIBSUF"] = "";
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
    mReplaceValues["EXTERN_PREFIX"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define EXTERN_PREFIX \"\"\n\
#else\n\
#   define EXTERN_PREFIX \"_\"\n\
#endif";
    mReplaceValues["EXTERN_ASM"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define EXTERN_ASM\n\
#else\n\
#   define EXTERN_ASM _\n\
#endif";
    mReplaceValues["SLIBSUF"] = "#if defined(_USRDLL)\n\
#   define SLIBSUF \".dll\"\n\
#else\n\
#   define SLIBSUF \".lib\"\n\
#endif";

    mReplaceValues["ARCH_X86_32"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define ARCH_X86_32 0\n\
#else\n\
#   define ARCH_X86_32 1\n\
#endif";
    mReplaceValues["ARCH_X86_64"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define ARCH_X86_64 1\n\
#else\n\
#   define ARCH_X86_64 0\n\
#endif";
    mReplaceValues["HAVE_XMM_CLOBBERS"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define HAVE_XMM_CLOBBERS 1\n\
#else\n\
#   define HAVE_XMM_CLOBBERS 0\n\
#endif";
    mReplaceValues["CONFIG_SHARED"] = "#if defined(_USRDLL)\n\
#   define CONFIG_SHARED 1\n\
#else\n\
#   define CONFIG_SHARED 0\n\
#endif";
    mReplaceValues["CONFIG_STATIC"] = "#if defined(_USRDLL)\n\
#   define CONFIG_STATIC 0\n\
#else\n\
#   define CONFIG_STATIC 1\n\
#endif";
    mReplaceValues["HAVE_ALIGNED_STACK"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define HAVE_ALIGNED_STACK 1\n\
#else\n\
#   define HAVE_ALIGNED_STACK 0\n\
#endif";
    mReplaceValues["HAVE_FAST_64BIT"] = "#if defined( __x86_64 ) || defined( _M_X64 )\n\
#   define HAVE_FAST_64BIT 1\n\
#else\n\
#   define HAVE_FAST_64BIT 0\n\
#endif";
    mReplaceValues["HAVE_INLINE_ASM"] = "#if defined(__INTEL_COMPILER)\n\
#   define HAVE_INLINE_ASM 1\n\
#else\n\
#   define HAVE_INLINE_ASM 0\n\
#endif";
    //Build replace values for all inline asm
    vector<string> vInlineList;
    getConfigList( "ARCH_EXT_LIST", vInlineList );
    for( vector<string>::iterator vitIt=vInlineList.begin(); vitIt<vInlineList.end(); vitIt++ )
    {
        transform( vitIt->begin(), vitIt->end(), vitIt->begin(), ::toupper);
        string sName = "HAVE_" + *vitIt + "_INLINE";
        mReplaceValues[sName] = "#if defined(__INTEL_COMPILER)\n\
#   define " + sName + " 1\n\
#else\n\
#   define " + sName + " 0\n\
#endif";
    }
     

    //Add to config.asm only list
    mASMReplaceValues["ARCH_X86_32"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define ARCH_X86_32 0\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define ARCH_X86_32 1\n\
%define PREFIX\n\
%endif";
    mASMReplaceValues["ARCH_X86_64"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define ARCH_X86_64 1\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define ARCH_X86_64 0\n\
%endif";
    mASMReplaceValues["HAVE_ALIGNED_STACK"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
%define HAVE_ALIGNED_STACK 1\n\
%elifidn __OUTPUT_FORMAT__,win32\n\
%define HAVE_ALIGNED_STACK 0\n\
%endif";
    mASMReplaceValues["HAVE_FAST_64BIT"] = "%ifidn __OUTPUT_FORMAT__,x64\n\
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
}

void configGenerator::buildAdditionalDependencies( DependencyList & mAdditionalDependencies )
{
    mAdditionalDependencies.clear( );
    mAdditionalDependencies["capCreateCaptureWindow"] = true;
    mAdditionalDependencies["CreateDIBSection"] = false;
    mAdditionalDependencies["dv1394"] = false;
    mAdditionalDependencies["dxva2api_h"] = true;
    mAdditionalDependencies["jack_jack_h"] = false;
    mAdditionalDependencies["IBaseFilter"] = true;
    mAdditionalDependencies["libcrystalhd_libcrystalhd_if_h"] = false;
    mAdditionalDependencies["linux_fb_h"] = false;
    mAdditionalDependencies["linux_videodev_h"] = false;
    mAdditionalDependencies["linux_videodev2_h"] = false;
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
    
    //*****Encoder optimization is currently ignored as people may want to compare encoders. The commandline should be used to disable unwanted necoders*****//
    
    //mOptimisedDisables["LIBTWOLAME_ENCODER"].push_back( "MP2_ENCODER" );

    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBFAAC_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "AAC_ENCODER" );
    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );

    //mOptimisedDisables["LIBFDK_AAC_ENCODER"].push_back( "LIBAACPLUS_ENCODER" );

    //mOptimisedDisables["LIBFAAC_ENCODER"].push_back( "AAC_ENCODER" );
    //mOptimisedDisables["LIBFAAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );
    //Eventually the inbuilt aac encoder should get better
    //mOptimisedDisables["AAC_ENCODER"].push_back( "LIBVO_AACENC_ENCODER" );

    //mOptimisedDisables["LIBVORBIS_ENCODER"].push_back( "VORBIS_ENCODER" );

    //mOptimisedDisables["LIBMP3LAME_ENCODER"].push_back( "LIBSHINE_ENCODER" );

    mOptimisedDisables["LIBGSM_DECODER"].push_back( "GSM_DECODER" );//???
    mOptimisedDisables["LIBGSM_MS_DECODER"].push_back( "GSM_MS_DECODER" );//???

    mOptimisedDisables["LIBNUT_MUXER"].push_back( "NUT_MUXER" );
    mOptimisedDisables["LIBNUT_DEMUXER"].push_back( "NUT_DEMUXER" );

    mOptimisedDisables["LIBOPENCORE_AMRNB_DECODER"].push_back( "AMRNB_DECODER" );//???
    mOptimisedDisables["LIBOPENCORE_AMRWB_DECODER"].push_back( "AMRWB_DECODER" );//???

    mOptimisedDisables["LIBOPENJPEG_DECODER"].push_back( "JPEG2000_DECODER" );//???
    //mOptimisedDisables["LIBOPENJPEG_ENCODER"].push_back( "JPEG2000_ENCODER" );//???

    mOptimisedDisables["LIBSCHROEDINGER_DECODER"].push_back( "DIRAC_DECODER" );

    mOptimisedDisables["LIBSTAGEFRIGHT_H264_DECODER"].push_back( "H264_DECODER" );

    mOptimisedDisables["LIBUTVIDEO_DECODER"].push_back( "UTVIDEO_DECODER" );//???
    //mOptimisedDisables["LIBUTVIDEO_ENCODER"].push_back( "UTVIDEO_ENCODER" );//???

    //Inbuilt native decoder is apparently faster
    mOptimisedDisables["VP8_DECODER"].push_back( "LIBVPX_VP8_DECODER" );
    mOptimisedDisables["VP9_DECODER"].push_back( "LIBVPX_VP9_DECODER" );

    //mOptimisedDisables["LIBWAVPACK_ENCODER"].push_back( "WAVPACK_ENCODER" );//???
}