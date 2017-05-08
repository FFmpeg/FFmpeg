/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2010-2017 NVIDIA Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the software, and to permit persons to whom the
 * software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/********************************************************************************************************************/
//! \file nvcuvid.h
//!   NVDECODE API provides video decoding interface to NVIDIA GPU devices.
//! \date 2015-2017
//!  This file contains the interface constants, structure definitions and function prototypes.
/********************************************************************************************************************/

#if !defined(__NVCUVID_H__)
#define __NVCUVID_H__

#include "compat/cuda/dynlink_cuviddec.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/*********************************
** Initialization
*********************************/
CUresult  CUDAAPI cuvidInit(unsigned int Flags);

/***********************************************/
//!
//! High-level helper APIs for video sources
//!
/***********************************************/

typedef void *CUvideosource;
typedef void *CUvideoparser;
typedef long long CUvideotimestamp;


/************************************************************************/
//! \enum cudaVideoState
//! Video source state enums
//! Used in cuvidSetVideoSourceState and cuvidGetVideoSourceState APIs
/************************************************************************/
typedef enum {
    cudaVideoState_Error   = -1,    /**< Error state (invalid source)                  */
    cudaVideoState_Stopped = 0,     /**< Source is stopped (or reached end-of-stream)  */
    cudaVideoState_Started = 1      /**< Source is running and delivering data         */
} cudaVideoState;

/************************************************************************/
//! \enum cudaAudioCodec
//! Audio compression enums
//! Used in CUAUDIOFORMAT structure
/************************************************************************/
typedef enum {
    cudaAudioCodec_MPEG1=0,         /**< MPEG-1 Audio               */
    cudaAudioCodec_MPEG2,           /**< MPEG-2 Audio               */
    cudaAudioCodec_MP3,             /**< MPEG-1 Layer III Audio     */
    cudaAudioCodec_AC3,             /**< Dolby Digital (AC3) Audio  */
    cudaAudioCodec_LPCM,            /**< PCM Audio                  */
    cudaAudioCodec_AAC,             /**< AAC Audio                  */
} cudaAudioCodec;

/************************************************************************************************/
//! \ingroup STRUCTS
//! \struct CUVIDEOFORMAT
//! Video format
//! Used in cuvidGetSourceVideoFormat API
/************************************************************************************************/
typedef struct
{
    cudaVideoCodec codec;                   /**< OUT: Compression format          */
   /**
    * OUT: frame rate = numerator / denominator (for example: 30000/1001)
    */
    struct {
        /**< OUT: frame rate numerator   (0 = unspecified or variable frame rate) */
        unsigned int numerator;
        /**< OUT: frame rate denominator (0 = unspecified or variable frame rate) */
        unsigned int denominator;
    } frame_rate;
    unsigned char progressive_sequence;     /**< OUT: 0=interlaced, 1=progressive                                      */
    unsigned char bit_depth_luma_minus8;    /**< OUT: high bit depth luma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth   */
    unsigned char bit_depth_chroma_minus8;  /**< OUT: high bit depth chroma. E.g, 2 for 10-bitdepth, 4 for 12-bitdepth */
    unsigned char reserved1;                /**< Reserved for future use                                               */
    unsigned int coded_width;               /**< OUT: coded frame width in pixels                                      */
    unsigned int coded_height;              /**< OUT: coded frame height in pixels                                     */
   /**
    * area of the frame that should be displayed
    * typical example:
    * coded_width = 1920, coded_height = 1088
    * display_area = { 0,0,1920,1080 }
    */
    struct {
        int left;                           /**< OUT: left position of display rect    */
        int top;                            /**< OUT: top position of display rect     */
        int right;                          /**< OUT: right position of display rect   */
        int bottom;                         /**< OUT: bottom position of display rect  */
    } display_area;
    cudaVideoChromaFormat chroma_format;    /**< OUT:  Chroma format                   */
    unsigned int bitrate;                   /**< OUT: video bitrate (bps, 0=unknown)   */
   /**
    * OUT: Display Aspect Ratio = x:y (4:3, 16:9, etc)
    */
    struct {
        int x;
        int y;
    } display_aspect_ratio;
    /**
    * Video Signal Description
    * Refer section E.2.1 (VUI parameters semantics) of H264 spec file
    */
    struct {
        unsigned char video_format          : 3; /**< OUT: 0-Component, 1-PAL, 2-NTSC, 3-SECAM, 4-MAC, 5-Unspecified     */
        unsigned char video_full_range_flag : 1; /**< OUT: indicates the black level and luma and chroma range           */
        unsigned char reserved_zero_bits    : 4; /**< Reserved bits                                                      */
        unsigned char color_primaries;           /**< OUT: chromaticity coordinates of source primaries                  */
        unsigned char transfer_characteristics;  /**< OUT: opto-electronic transfer characteristic of the source picture */
        unsigned char matrix_coefficients;       /**< OUT: used in deriving luma and chroma signals from RGB primaries   */
    } video_signal_description;
    unsigned int seqhdr_data_length;             /**< OUT: Additional bytes following (CUVIDEOFORMATEX)                  */
} CUVIDEOFORMAT;

/****************************************************************/
//! \ingroup STRUCTS
//! \struct CUVIDEOFORMATEX
//! Video format including raw sequence header information
//! Used in cuvidGetSourceVideoFormat API
/****************************************************************/
typedef struct
{
    CUVIDEOFORMAT format;                 /**< OUT: CUVIDEOFORMAT structure */
    unsigned char raw_seqhdr_data[1024];  /**< OUT: Sequence header data    */
} CUVIDEOFORMATEX;

/****************************************************************/
//! \ingroup STRUCTS
//! \struct CUAUDIOFORMAT
//! Audio formats
//! Used in cuvidGetSourceAudioFormat API
/****************************************************************/
typedef struct
{
    cudaAudioCodec codec;       /**< OUT: Compression format                                              */
    unsigned int channels;      /**< OUT: number of audio channels                                        */
    unsigned int samplespersec; /**< OUT: sampling frequency                                              */
    unsigned int bitrate;       /**< OUT: For uncompressed, can also be used to determine bits per sample */
    unsigned int reserved1;     /**< Reserved for future use                                              */
    unsigned int reserved2;     /**< Reserved for future use                                              */
} CUAUDIOFORMAT;


/***************************************************************/
//! \enum CUvideopacketflags
//! Data packet flags
//! Used in CUVIDSOURCEDATAPACKET structure
/***************************************************************/
typedef enum {
    CUVID_PKT_ENDOFSTREAM   = 0x01,   /**< Set when this is the last packet for this stream  */
    CUVID_PKT_TIMESTAMP     = 0x02,   /**< Timestamp is valid                                */
    CUVID_PKT_DISCONTINUITY = 0x04,   /**< Set when a discontinuity has to be signalled      */
    CUVID_PKT_ENDOFPICTURE  = 0x08,   /**< Set when the packet contains exactly one frame    */
} CUvideopacketflags;

/*****************************************************************************/
//! \ingroup STRUCTS
//! \struct CUVIDSOURCEDATAPACKET
//! Data Packet
//! Used in cuvidParseVideoData API
//! IN for cuvidParseVideoData
/*****************************************************************************/
typedef struct _CUVIDSOURCEDATAPACKET
{
    tcu_ulong flags;                /**< IN: Combination of CUVID_PKT_XXX flags                              */
    tcu_ulong payload_size;         /**< IN: number of bytes in the payload (may be zero if EOS flag is set) */
    const unsigned char *payload;   /**< IN: Pointer to packet payload data (may be NULL if EOS flag is set) */
    CUvideotimestamp timestamp;     /**< IN: Presentation time stamp (10MHz clock), only valid if
                                             CUVID_PKT_TIMESTAMP flag is set                                 */
} CUVIDSOURCEDATAPACKET;

// Callback for packet delivery
typedef int (CUDAAPI *PFNVIDSOURCECALLBACK)(void *, CUVIDSOURCEDATAPACKET *);

/**************************************************************************************************************************/
//! \ingroup STRUCTS
//! \struct CUVIDSOURCEPARAMS
//! Describes parameters needed in cuvidCreateVideoSource API
//! NVDECODE API is intended for HW accelerated video decoding so CUvideosource doesn't have audio demuxer for all supported
//! containers. It's recommended to clients to use their own or third party demuxer if audio support is needed.
/**************************************************************************************************************************/
typedef struct _CUVIDSOURCEPARAMS
{
    unsigned int ulClockRate;                   /**< IN: Time stamp units in Hz (0=default=10000000Hz)      */
    unsigned int uReserved1[7];                 /**< Reserved for future use - set to zero                  */
    void *pUserData;                            /**< IN: User private data passed in to the data handlers   */
    PFNVIDSOURCECALLBACK pfnVideoDataHandler;   /**< IN: Called to deliver video packets                    */
    PFNVIDSOURCECALLBACK pfnAudioDataHandler;   /**< IN: Called to deliver audio packets.                   */
    void *pvReserved2[8];                       /**< Reserved for future use - set to NULL                  */
} CUVIDSOURCEPARAMS;


/**********************************************/
//! \ingroup ENUMS
//! \enum CUvideosourceformat_flags
//! CUvideosourceformat_flags
//! Used in cuvidGetSourceVideoFormat API
/**********************************************/
typedef enum {
    CUVID_FMT_EXTFORMATINFO = 0x100             /**< Return extended format structure (CUVIDEOFORMATEX) */
} CUvideosourceformat_flags;

#if !defined(__APPLE__)
/**************************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCreateVideoSource(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams)
//! Create CUvideosource object. CUvideosource spawns demultiplexer thread that provides two callbacks:
//! pfnVideoDataHandler() and pfnAudioDataHandler()
//! NVDECODE API is intended for HW accelerated video decoding so CUvideosource doesn't have audio demuxer for all supported
//! containers. It's recommended to clients to use their own or third party demuxer if audio support is needed.
/**************************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCreateVideoSource(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams);

/****************************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCreateVideoSourceW(CUvideosource *pObj, const wchar_t *pwszFileName, CUVIDSOURCEPARAMS *pParams)
//! Create video source object and initialize
/****************************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCreateVideoSourceW(CUvideosource *pObj, const wchar_t *pwszFileName, CUVIDSOURCEPARAMS *pParams);

/*********************************************************************/
//! \fn CUresult CUDAAPI cuvidDestroyVideoSource(CUvideosource obj)
//! Destroy video source
/*********************************************************************/
typedef CUresult CUDAAPI tcuvidDestroyVideoSource(CUvideosource obj);

/******************************************************************************************/
//! \fn CUresult CUDAAPI cuvidSetVideoSourceState(CUvideosource obj, cudaVideoState state)
//! Set video source state
/******************************************************************************************/
typedef CUresult CUDAAPI tcuvidSetVideoSourceState(CUvideosource obj, cudaVideoState state);

/******************************************************************************************/
//! \fn cudaVideoState CUDAAPI cuvidGetVideoSourceState(CUvideosource obj)
//! Get video source state
/******************************************************************************************/
typedef cudaVideoState CUDAAPI tcuvidGetVideoSourceState(CUvideosource obj);

/****************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidGetSourceVideoFormat(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags)
//! Gets details of video stream in pvidfmt
/****************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidGetSourceVideoFormat(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags);

/****************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidGetSourceAudioFormat(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags)
//! Get audio source format
//! NVDECODE API is intended for HW accelarated video decoding so CUvideosource doesn't have audio demuxer for all suppported
//! containers. It's recommended to clients to use their own or third party demuxer if audio support is needed.
/****************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidGetSourceAudioFormat(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags);

#endif
/**********************************************************************************/
//! \ingroup STRUCTS
//! \struct CUVIDPARSERDISPINFO
//! Used in cuvidParseVideoData API with PFNVIDDISPLAYCALLBACK pfnDisplayPicture
/**********************************************************************************/
typedef struct _CUVIDPARSERDISPINFO
{
    int picture_index;          /**< OUT: Index of the current picture                                                         */
    int progressive_frame;      /**< OUT: 1 if progressive frame; 0 otherwise                                                  */
    int top_field_first;        /**< OUT: 1 if top field is displayed first; 0 otherwise                                       */
    int repeat_first_field;     /**< OUT: Number of additional fields (1=ivtc, 2=frame doubling, 4=frame tripling,
                                     -1=unpaired field)                                                                        */
    CUvideotimestamp timestamp; /**< OUT: Presentation time stamp                                                              */
} CUVIDPARSERDISPINFO;

/***********************************************************************************************************************/
//! Parser callbacks
//! The parser will call these synchronously from within cuvidParseVideoData(), whenever a picture is ready to
//! be decoded and/or displayed. First argument in functions is "void *pUserData" member of structure CUVIDSOURCEPARAMS
/***********************************************************************************************************************/
typedef int (CUDAAPI *PFNVIDSEQUENCECALLBACK)(void *, CUVIDEOFORMAT *);
typedef int (CUDAAPI *PFNVIDDECODECALLBACK)(void *, CUVIDPICPARAMS *);
typedef int (CUDAAPI *PFNVIDDISPLAYCALLBACK)(void *, CUVIDPARSERDISPINFO *);

/**************************************/
//! \ingroup STRUCTS
//! \struct CUVIDPARSERPARAMS
//! Used in cuvidCreateVideoParser API
/**************************************/
typedef struct _CUVIDPARSERPARAMS
{
    cudaVideoCodec CodecType;                   /**< IN: cudaVideoCodec_XXX                                                  */
    unsigned int ulMaxNumDecodeSurfaces;        /**< IN: Max # of decode surfaces (parser will cycle through these)          */
    unsigned int ulClockRate;                   /**< IN: Timestamp units in Hz (0=default=10000000Hz)                        */
    unsigned int ulErrorThreshold;              /**< IN: % Error threshold (0-100) for calling pfnDecodePicture (100=always
                                                     IN: call pfnDecodePicture even if picture bitstream is fully corrupted) */
    unsigned int ulMaxDisplayDelay;             /**< IN: Max display queue delay (improves pipelining of decode with display)
                                                         0=no delay (recommended values: 2..4)                               */
    unsigned int uReserved1[5];                 /**< IN: Reserved for future use - set to 0                                  */
    void *pUserData;                            /**< IN: User data for callbacks                                             */
    PFNVIDSEQUENCECALLBACK pfnSequenceCallback; /**< IN: Called before decoding frames and/or whenever there is a fmt change */
    PFNVIDDECODECALLBACK pfnDecodePicture;      /**< IN: Called when a picture is ready to be decoded (decode order)         */
    PFNVIDDISPLAYCALLBACK pfnDisplayPicture;    /**< IN: Called whenever a picture is ready to be displayed (display order)  */
    void *pvReserved2[7];                       /**< Reserved for future use - set to NULL                                   */
    CUVIDEOFORMATEX *pExtVideoInfo;             /**< IN: [Optional] sequence header data from system layer                   */
} CUVIDPARSERPARAMS;

/************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams)
//! Create video parser object and initialize
/************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams);

/************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket)
//! Parse the video data from source data packet in pPacket
//! Extracts parameter sets like SPS, PPS, bitstream etc. from pPacket and
//! calls back pfnDecodePicture with CUVIDPICPARAMS data for kicking of HW decoding
/************************************************************************************************/
typedef CUresult CUDAAPI tcuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket);

/*******************************************************************/
//! \fn CUresult CUDAAPI cuvidDestroyVideoParser(CUvideoparser obj)
/*******************************************************************/
typedef CUresult CUDAAPI tcuvidDestroyVideoParser(CUvideoparser obj);

/**********************************************************************************************/

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif // __NVCUVID_H__


