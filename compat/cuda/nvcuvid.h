/*
 * This copyright notice applies to this header file only:
 *
 * Copyright (c) 2010-2016 NVIDIA Corporation
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

/**
 * \file nvcuvid.h
 *   NvCuvid API provides Video Decoding interface to NVIDIA GPU devices.
 * \date 2015-2015
 *  This file contains the interface constants, structure definitions and function prototypes.
 */

#if !defined(__NVCUVID_H__)
#define __NVCUVID_H__

#include "compat/cuda/cuviddec.h"

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/*********************************
** Initialization
*********************************/
CUresult  CUDAAPI cuvidInit(unsigned int Flags);

////////////////////////////////////////////////////////////////////////////////////////////////
//
// High-level helper APIs for video sources
//

typedef void *CUvideosource;
typedef void *CUvideoparser;
typedef long long CUvideotimestamp;

/**
 * \addtogroup VIDEO_PARSER Video Parser
 * @{
 */

/*!
 * \enum cudaVideoState
 * Video Source State
 */
typedef enum {
    cudaVideoState_Error   = -1,    /**< Error state (invalid source)  */
    cudaVideoState_Stopped = 0,     /**< Source is stopped (or reached end-of-stream)  */
    cudaVideoState_Started = 1      /**< Source is running and delivering data  */
} cudaVideoState;

/*!
 * \enum cudaAudioCodec
 * Audio compression
 */
typedef enum {
    cudaAudioCodec_MPEG1=0,         /**< MPEG-1 Audio  */
    cudaAudioCodec_MPEG2,           /**< MPEG-2 Audio  */
    cudaAudioCodec_MP3,             /**< MPEG-1 Layer III Audio  */
    cudaAudioCodec_AC3,             /**< Dolby Digital (AC3) Audio  */
    cudaAudioCodec_LPCM             /**< PCM Audio  */
} cudaAudioCodec;

/*!
 * \struct CUVIDEOFORMAT
 * Video format
 */
typedef struct
{
    cudaVideoCodec codec;                   /**< Compression format  */
   /**
    * frame rate = numerator / denominator (for example: 30000/1001)
    */
    struct {
        unsigned int numerator;             /**< frame rate numerator   (0 = unspecified or variable frame rate) */
        unsigned int denominator;           /**< frame rate denominator (0 = unspecified or variable frame rate) */
    } frame_rate;
    unsigned char progressive_sequence;     /**< 0=interlaced, 1=progressive */
    unsigned char bit_depth_luma_minus8;    /**< high bit depth Luma */
    unsigned char bit_depth_chroma_minus8;  /**< high bit depth Chroma */
    unsigned char reserved1;                /**< Reserved for future use */
    unsigned int coded_width;               /**< coded frame width */
    unsigned int coded_height;              /**< coded frame height  */
   /**
    *   area of the frame that should be displayed
    * typical example:
    *   coded_width = 1920, coded_height = 1088
    *   display_area = { 0,0,1920,1080 }
    */
    struct {
        int left;                           /**< left position of display rect  */
        int top;                            /**< top position of display rect  */
        int right;                          /**< right position of display rect  */
        int bottom;                         /**< bottom position of display rect  */
    } display_area;
    cudaVideoChromaFormat chroma_format;    /**<  Chroma format */
    unsigned int bitrate;                   /**< video bitrate (bps, 0=unknown) */
   /**
    * Display Aspect Ratio = x:y (4:3, 16:9, etc)
    */
    struct {
        int x;
        int y;
    } display_aspect_ratio;
    /**
    * Video Signal Description
    */
    struct {
        unsigned char video_format          : 3;
        unsigned char video_full_range_flag : 1;
        unsigned char reserved_zero_bits    : 4;
        unsigned char color_primaries;
        unsigned char transfer_characteristics;
        unsigned char matrix_coefficients;
    } video_signal_description;
    unsigned int seqhdr_data_length;          /**< Additional bytes following (CUVIDEOFORMATEX)  */
} CUVIDEOFORMAT;

/*!
 * \struct CUVIDEOFORMATEX
 * Video format including raw sequence header information
 */
typedef struct
{
    CUVIDEOFORMAT format;
    unsigned char raw_seqhdr_data[1024];
} CUVIDEOFORMATEX;

/*!
 * \struct CUAUDIOFORMAT
 * Audio Formats
 */
typedef struct
{
    cudaAudioCodec codec;       /**< Compression format  */
    unsigned int channels;      /**< number of audio channels */
    unsigned int samplespersec; /**< sampling frequency */
    unsigned int bitrate;       /**< For uncompressed, can also be used to determine bits per sample */
    unsigned int reserved1;     /**< Reserved for future use */
    unsigned int reserved2;     /**< Reserved for future use */
} CUAUDIOFORMAT;


/*!
 * \enum CUvideopacketflags
 * Data packet flags
 */
typedef enum {
    CUVID_PKT_ENDOFSTREAM   = 0x01,   /**< Set when this is the last packet for this stream  */
    CUVID_PKT_TIMESTAMP     = 0x02,   /**< Timestamp is valid  */
    CUVID_PKT_DISCONTINUITY = 0x04    /**< Set when a discontinuity has to be signalled  */
} CUvideopacketflags;

/*!
 * \struct CUVIDSOURCEDATAPACKET
 * Data Packet
 */
typedef struct _CUVIDSOURCEDATAPACKET
{
    unsigned long flags;            /**< Combination of CUVID_PKT_XXX flags */
    unsigned long payload_size;     /**< number of bytes in the payload (may be zero if EOS flag is set) */
    const unsigned char *payload;   /**< Pointer to packet payload data (may be NULL if EOS flag is set) */
    CUvideotimestamp timestamp;     /**< Presentation timestamp (10MHz clock), only valid if CUVID_PKT_TIMESTAMP flag is set */
} CUVIDSOURCEDATAPACKET;

// Callback for packet delivery
typedef int (CUDAAPI *PFNVIDSOURCECALLBACK)(void *, CUVIDSOURCEDATAPACKET *);

/*!
 * \struct CUVIDSOURCEPARAMS
 * Source Params
 */
typedef struct _CUVIDSOURCEPARAMS
{
    unsigned int ulClockRate;                   /**< Timestamp units in Hz (0=default=10000000Hz)  */
    unsigned int uReserved1[7];                 /**< Reserved for future use - set to zero  */
    void *pUserData;                            /**< Parameter passed in to the data handlers  */
    PFNVIDSOURCECALLBACK pfnVideoDataHandler;   /**< Called to deliver audio packets  */
    PFNVIDSOURCECALLBACK pfnAudioDataHandler;   /**< Called to deliver video packets  */
    void *pvReserved2[8];                       /**< Reserved for future use - set to NULL */
} CUVIDSOURCEPARAMS;

/*!
 * \enum CUvideosourceformat_flags
 * CUvideosourceformat_flags
 */
typedef enum {
    CUVID_FMT_EXTFORMATINFO = 0x100             /**< Return extended format structure (CUVIDEOFORMATEX) */
} CUvideosourceformat_flags;

#if !defined(__APPLE__)
/**
 * \fn CUresult CUDAAPI cuvidCreateVideoSource(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams)
 * Create Video Source
 */
CUresult CUDAAPI cuvidCreateVideoSource(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams);

/**
 * \fn CUresult CUDAAPI cuvidCreateVideoSourceW(CUvideosource *pObj, const wchar_t *pwszFileName, CUVIDSOURCEPARAMS *pParams)
 * Create Video Source
 */
CUresult CUDAAPI cuvidCreateVideoSourceW(CUvideosource *pObj, const wchar_t *pwszFileName, CUVIDSOURCEPARAMS *pParams);

/**
 * \fn CUresult CUDAAPI cuvidDestroyVideoSource(CUvideosource obj)
 * Destroy Video Source
 */
CUresult CUDAAPI cuvidDestroyVideoSource(CUvideosource obj);

/**
 * \fn CUresult CUDAAPI cuvidSetVideoSourceState(CUvideosource obj, cudaVideoState state)
 * Set Video Source state
 */
CUresult CUDAAPI cuvidSetVideoSourceState(CUvideosource obj, cudaVideoState state);

/**
 * \fn cudaVideoState CUDAAPI cuvidGetVideoSourceState(CUvideosource obj)
 * Get Video Source state
 */
cudaVideoState CUDAAPI cuvidGetVideoSourceState(CUvideosource obj);

/**
 * \fn CUresult CUDAAPI cuvidGetSourceVideoFormat(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags)
 * Get Video Source Format
 */
CUresult CUDAAPI cuvidGetSourceVideoFormat(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags);

/**
 * \fn CUresult CUDAAPI cuvidGetSourceAudioFormat(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags)
 * Set Video Source state
 */
CUresult CUDAAPI cuvidGetSourceAudioFormat(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags);

#endif

/**
 * \struct CUVIDPARSERDISPINFO
 */
typedef struct _CUVIDPARSERDISPINFO
{
    int picture_index;         /**<                 */
    int progressive_frame;     /**<                 */
    int top_field_first;       /**<                 */
    int repeat_first_field;    /**< Number of additional fields (1=ivtc, 2=frame doubling, 4=frame tripling, -1=unpaired field)  */
    CUvideotimestamp timestamp; /**<     */
} CUVIDPARSERDISPINFO;

//
// Parser callbacks
// The parser will call these synchronously from within cuvidParseVideoData(), whenever a picture is ready to
// be decoded and/or displayed.
//
typedef int (CUDAAPI *PFNVIDSEQUENCECALLBACK)(void *, CUVIDEOFORMAT *);
typedef int (CUDAAPI *PFNVIDDECODECALLBACK)(void *, CUVIDPICPARAMS *);
typedef int (CUDAAPI *PFNVIDDISPLAYCALLBACK)(void *, CUVIDPARSERDISPINFO *);

/**
 * \struct CUVIDPARSERPARAMS
 */
typedef struct _CUVIDPARSERPARAMS
{
    cudaVideoCodec CodecType;               /**< cudaVideoCodec_XXX  */
    unsigned int ulMaxNumDecodeSurfaces;    /**< Max # of decode surfaces (parser will cycle through these) */
    unsigned int ulClockRate;               /**< Timestamp units in Hz (0=default=10000000Hz) */
    unsigned int ulErrorThreshold;          /**< % Error threshold (0-100) for calling pfnDecodePicture (100=always call pfnDecodePicture even if picture bitstream is fully corrupted) */
    unsigned int ulMaxDisplayDelay;         /**< Max display queue delay (improves pipelining of decode with display) - 0=no delay (recommended values: 2..4) */
    unsigned int uReserved1[5];             /**< Reserved for future use - set to 0 */
    void *pUserData;                        /**< User data for callbacks */
    PFNVIDSEQUENCECALLBACK pfnSequenceCallback; /**< Called before decoding frames and/or whenever there is a format change */
    PFNVIDDECODECALLBACK pfnDecodePicture;      /**< Called when a picture is ready to be decoded (decode order) */
    PFNVIDDISPLAYCALLBACK pfnDisplayPicture;    /**< Called whenever a picture is ready to be displayed (display order)  */
    void *pvReserved2[7];                       /**< Reserved for future use - set to NULL */
    CUVIDEOFORMATEX *pExtVideoInfo;             /**< [Optional] sequence header data from system layer */
} CUVIDPARSERPARAMS;

/**
 * \fn CUresult CUDAAPI cuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams)
 */
CUresult CUDAAPI cuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams);

/**
 * \fn CUresult CUDAAPI cuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket)
 */
CUresult CUDAAPI cuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket);

/**
 * \fn CUresult CUDAAPI cuvidDestroyVideoParser(CUvideoparser obj)
 */
CUresult CUDAAPI cuvidDestroyVideoParser(CUvideoparser obj);

/** @} */  /* END VIDEO_PARSER */
////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif // __NVCUVID_H__


