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

/*****************************************************************************************************/
//! \file cuviddec.h
//! NVDECODE API provides video decoding interface to NVIDIA GPU devices.
//! \date 2015-2017
//! This file contains constants, structure definitions and function prototypes used for decoding.
/*****************************************************************************************************/

#if !defined(__CUDA_VIDEO_H__)
#define __CUDA_VIDEO_H__

#if defined(_WIN64) || defined(__LP64__) || defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
#if (CUDA_VERSION >= 3020) && (!defined(CUDA_FORCE_API_VERSION) || (CUDA_FORCE_API_VERSION >= 3020))
#define __CUVID_DEVPTR64
#endif
#endif

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#if defined(__CYGWIN__)
typedef unsigned int tcu_ulong;
#else
typedef unsigned long tcu_ulong;
#endif

typedef void *CUvideodecoder;
typedef struct _CUcontextlock_st *CUvideoctxlock;

/*********************************************************************************/
//! \enum cudaVideoCodec
//! Video codec enums
//! These enums are used in CUVIDDECODECREATEINFO and CUVIDDECODECAPS structures
/*********************************************************************************/
typedef enum cudaVideoCodec_enum {
    cudaVideoCodec_MPEG1=0,                                         /**<  MPEG1             */
    cudaVideoCodec_MPEG2,                                           /**<  MPEG2             */
    cudaVideoCodec_MPEG4,                                           /**<  MPEG4             */
    cudaVideoCodec_VC1,                                             /**<  VC1               */
    cudaVideoCodec_H264,                                            /**<  H264              */
    cudaVideoCodec_JPEG,                                            /**<  JPEG              */
    cudaVideoCodec_H264_SVC,                                        /**<  H264-SVC          */
    cudaVideoCodec_H264_MVC,                                        /**<  H264-MVC          */
    cudaVideoCodec_HEVC,                                            /**<  HEVC              */
    cudaVideoCodec_VP8,                                             /**<  VP8               */
    cudaVideoCodec_VP9,                                             /**<  VP9               */
    cudaVideoCodec_NumCodecs,                                       /**<  Max codecs        */
    // Uncompressed YUV
    cudaVideoCodec_YUV420 = (('I'<<24)|('Y'<<16)|('U'<<8)|('V')),   /**< Y,U,V (4:2:0)      */
    cudaVideoCodec_YV12   = (('Y'<<24)|('V'<<16)|('1'<<8)|('2')),   /**< Y,V,U (4:2:0)      */
    cudaVideoCodec_NV12   = (('N'<<24)|('V'<<16)|('1'<<8)|('2')),   /**< Y,UV  (4:2:0)      */
    cudaVideoCodec_YUYV   = (('Y'<<24)|('U'<<16)|('Y'<<8)|('V')),   /**< YUYV/YUY2 (4:2:2)  */
    cudaVideoCodec_UYVY   = (('U'<<24)|('Y'<<16)|('V'<<8)|('Y'))    /**< UYVY (4:2:2)       */
} cudaVideoCodec;

/*********************************************************************************/
//! \enum cudaVideoSurfaceFormat
//! Video surface format enums used for output format of decoded output
//! These enums are used in CUVIDDECODECREATEINFO structure
/*********************************************************************************/
typedef enum cudaVideoSurfaceFormat_enum {
    cudaVideoSurfaceFormat_NV12=0,       /**< NV12 format          */
    cudaVideoSurfaceFormat_P016=1        /**< 16 bit semiplaner format. Can be used for 10 bit(6LSB bits 0),
                                              12 bit (4LSB bits 0) */
} cudaVideoSurfaceFormat;

/******************************************************************************************************************/
//! \enum cudaVideoDeinterlaceMode
//! Deinterlacing mode enums
//! These enums are used in CUVIDDECODECREATEINFO structure
//! Use cudaVideoDeinterlaceMode_Weave for progressive content and for content that doesn't need deinterlacing
//! cudaVideoDeinterlaceMode_Adaptive needs more video memory than other DImodes
/******************************************************************************************************************/
typedef enum cudaVideoDeinterlaceMode_enum {
    cudaVideoDeinterlaceMode_Weave=0,   /**< Weave both fields (no deinterlacing) */
    cudaVideoDeinterlaceMode_Bob,       /**< Drop one field                       */
    cudaVideoDeinterlaceMode_Adaptive   /**< Adaptive deinterlacing               */
} cudaVideoDeinterlaceMode;

/**************************************************************************************************************/
//! \enum cudaVideoChromaFormat
//! Chroma format enums
//! These enums are used in CUVIDDECODECREATEINFO and CUVIDDECODECAPS structures
//! JPEG supports Monochrome, YUV 4:2:0, YUV 4:2:2 and YUV 4:4:4 chroma formats.
//! H264, HEVC, VP9, VP8, VC1, MPEG1, MPEG2 and MPEG4 support YUV 4:2:0 chroma format only.
/**************************************************************************************************************/
typedef enum cudaVideoChromaFormat_enum {
    cudaVideoChromaFormat_Monochrome=0,  /**< MonoChrome */
    cudaVideoChromaFormat_420,           /**< YUV 4:2:0  */
    cudaVideoChromaFormat_422,           /**< YUV 4:2:2  */
    cudaVideoChromaFormat_444            /**< YUV 4:4:4  */
} cudaVideoChromaFormat;

/*************************************************************************************************************/
//! \enum cudaVideoCreateFlags
//! Decoder flag enums to select preferred decode path
//! cudaVideoCreate_Default and cudaVideoCreate_PreferCUVID are most optimized, use these whenever possible
/*************************************************************************************************************/
typedef enum cudaVideoCreateFlags_enum {
    cudaVideoCreate_Default     = 0x00,     /**< Default operation mode: use dedicated video engines                        */
    cudaVideoCreate_PreferCUDA  = 0x01,     /**< Use CUDA-based decoder (requires valid vidLock object for multi-threading) */
    cudaVideoCreate_PreferDXVA  = 0x02,     /**< Go through DXVA internally if possible (requires D3D9 interop)             */
    cudaVideoCreate_PreferCUVID = 0x04      /**< Use dedicated video engines directly                                       */
} cudaVideoCreateFlags;


/**************************************************************************************************************/
//! \struct CUVIDDECODECAPS;
//! This structure is used in cuvidGetDecoderCaps API
/**************************************************************************************************************/
typedef struct _CUVIDDECODECAPS
{
    cudaVideoCodec          eCodecType;                 /**< IN: cudaVideoCodec_XXX                                 */
    cudaVideoChromaFormat   eChromaFormat;              /**< IN: cudaVideoChromaFormat_XXX                          */
    unsigned int            nBitDepthMinus8;            /**< IN: The Value "BitDepth minus 8"                       */
    unsigned int            reserved1[3];               /**< Reserved for future use - set to zero                  */

    unsigned char           bIsSupported;               /**< OUT: 1 if codec supported, 0 if not supported          */
    unsigned char           reserved2[3];               /**< Reserved for future use - set to zero                  */
    unsigned int            nMaxWidth;                  /**< OUT: Max supported coded width in pixels               */
    unsigned int            nMaxHeight;                 /**< OUT: Max supported coded height in pixels              */
    unsigned int            nMaxMBCount;                /**< OUT: Max supported macroblock count
                                                                  CodedWidth*CodedHeight/256 must be <= nMaxMBCount */
    unsigned short          nMinWidth;                  /**< OUT: Min supported coded width in pixels               */
    unsigned short          nMinHeight;                 /**< OUT: Min supported coded height in pixels              */
    unsigned int            reserved3[11];              /**< Reserved for future use - set to zero                  */
} CUVIDDECODECAPS;

/**************************************************************************************************************/
//! \struct CUVIDDECODECREATEINFO
//! This structure is used in cuvidCreateDecoder API
/**************************************************************************************************************/
typedef struct _CUVIDDECODECREATEINFO
{
    tcu_ulong ulWidth;                  /**< IN: Coded sequence width in pixels                                             */
    tcu_ulong ulHeight;                 /**< IN: Coded sequence height in pixels                                            */
    tcu_ulong ulNumDecodeSurfaces;      /**< IN: Maximum number of internal decode surfaces                                 */
    cudaVideoCodec CodecType;           /**< IN: cudaVideoCodec_XXX                                                         */
    cudaVideoChromaFormat ChromaFormat; /**< IN: cudaVideoChromaFormat_XXX                                                  */
    tcu_ulong ulCreationFlags;          /**< IN: Decoder creation flags (cudaVideoCreateFlags_XXX)                          */
    tcu_ulong bitDepthMinus8;           /**< IN: The value "BitDepth minus 8"                                               */
    tcu_ulong ulIntraDecodeOnly;        /**< IN: Set 1 only if video has all intra frames (default value is 0). This will
                                             optimize video memory for Intra frames only decoding. The support is limited
                                             to specific codecs(H264 rightnow), the flag will be ignored for codecs which
                                             are not supported. However decoding might fail if the flag is enabled in case
                                             of supported codecs for regular bit streams having P and/or B frames.          */
    tcu_ulong Reserved1[3];             /**< Reserved for future use - set to zero                                          */
    /**
    * IN: area of the frame that should be displayed
    */
    struct {
        short left;
        short top;
        short right;
        short bottom;
    } display_area;

    cudaVideoSurfaceFormat OutputFormat;       /**< IN: cudaVideoSurfaceFormat_XXX                                     */
    cudaVideoDeinterlaceMode DeinterlaceMode;  /**< IN: cudaVideoDeinterlaceMode_XXX                                   */
    tcu_ulong ulTargetWidth;                   /**< IN: Post-processed output width (Should be aligned to 2)           */
    tcu_ulong ulTargetHeight;                  /**< IN: Post-processed output height (Should be aligbed to 2)          */
    tcu_ulong ulNumOutputSurfaces;             /**< IN: Maximum number of output surfaces simultaneously mapped        */
    CUvideoctxlock vidLock;                    /**< IN: If non-NULL, context lock used for synchronizing ownership of
                                                    the cuda context. Needed for cudaVideoCreate_PreferCUDA decode     */
    /**
    * IN: target rectangle in the output frame (for aspect ratio conversion)
    * if a null rectangle is specified, {0,0,ulTargetWidth,ulTargetHeight} will be used
    */
    struct {
        short left;
        short top;
        short right;
        short bottom;
    } target_rect;
    tcu_ulong Reserved2[5];                    /**< Reserved for future use - set to zero */
} CUVIDDECODECREATEINFO;

/*********************************************************/
//! \struct CUVIDH264DPBENTRY
//! H.264 DPB entry
//! This structure is used in CUVIDH264PICPARAMS structure
/*********************************************************/
typedef struct _CUVIDH264DPBENTRY
{
    int PicIdx;                 /**< picture index of reference frame                                        */
    int FrameIdx;               /**< frame_num(short-term) or LongTermFrameIdx(long-term)                    */
    int is_long_term;           /**< 0=short term reference, 1=long term reference                           */
    int not_existing;           /**< non-existing reference frame (corresponding PicIdx should be set to -1) */
    int used_for_reference;     /**< 0=unused, 1=top_field, 2=bottom_field, 3=both_fields                    */
    int FieldOrderCnt[2];       /**< field order count of top and bottom fields                              */
} CUVIDH264DPBENTRY;

/************************************************************/
//! \struct CUVIDH264MVCEXT
//! H.264 MVC picture parameters ext
//! This structure is used in CUVIDH264PICPARAMS structure
/************************************************************/
typedef struct _CUVIDH264MVCEXT
{
    int num_views_minus1;                  /**< Max number of coded views minus 1 in video : Range - 0 to 1023              */
    int view_id;                           /**< view identifier                                                             */
    unsigned char inter_view_flag;         /**< 1 if used for inter-view prediction, 0 if not                               */
    unsigned char num_inter_view_refs_l0;  /**< number of inter-view ref pics in RefPicList0                                */
    unsigned char num_inter_view_refs_l1;  /**< number of inter-view ref pics in RefPicList1                                */
    unsigned char MVCReserved8Bits;        /**< Reserved bits                                                               */
    int InterViewRefsL0[16];               /**< view id of the i-th view component for inter-view prediction in RefPicList0 */
    int InterViewRefsL1[16];               /**< view id of the i-th view component for inter-view prediction in RefPicList1 */
} CUVIDH264MVCEXT;

/*********************************************************/
//! \struct CUVIDH264SVCEXT
//! H.264 SVC picture parameters ext
//! This structure is used in CUVIDH264PICPARAMS structure
/*********************************************************/
typedef struct _CUVIDH264SVCEXT
{
    unsigned char profile_idc;
    unsigned char level_idc;
    unsigned char DQId;
    unsigned char DQIdMax;
    unsigned char disable_inter_layer_deblocking_filter_idc;
    unsigned char ref_layer_chroma_phase_y_plus1;
    signed char   inter_layer_slice_alpha_c0_offset_div2;
    signed char   inter_layer_slice_beta_offset_div2;

    unsigned short DPBEntryValidFlag;
    unsigned char inter_layer_deblocking_filter_control_present_flag;
    unsigned char extended_spatial_scalability_idc;
    unsigned char adaptive_tcoeff_level_prediction_flag;
    unsigned char slice_header_restriction_flag;
    unsigned char chroma_phase_x_plus1_flag;
    unsigned char chroma_phase_y_plus1;

    unsigned char tcoeff_level_prediction_flag;
    unsigned char constrained_intra_resampling_flag;
    unsigned char ref_layer_chroma_phase_x_plus1_flag;
    unsigned char store_ref_base_pic_flag;
    unsigned char Reserved8BitsA;
    unsigned char Reserved8BitsB;

    short scaled_ref_layer_left_offset;
    short scaled_ref_layer_top_offset;
    short scaled_ref_layer_right_offset;
    short scaled_ref_layer_bottom_offset;
    unsigned short Reserved16Bits;
    struct _CUVIDPICPARAMS *pNextLayer; /**< Points to the picparams for the next layer to be decoded.
                                             Linked list ends at the target layer. */
    int bRefBaseLayer;                  /**< whether to store ref base pic */
} CUVIDH264SVCEXT;

/******************************************************/
//! \struct CUVIDH264PICPARAMS
//! H.264 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/******************************************************/
typedef struct _CUVIDH264PICPARAMS
{
    // SPS
    int log2_max_frame_num_minus4;
    int pic_order_cnt_type;
    int log2_max_pic_order_cnt_lsb_minus4;
    int delta_pic_order_always_zero_flag;
    int frame_mbs_only_flag;
    int direct_8x8_inference_flag;
    int num_ref_frames;             // NOTE: shall meet level 4.1 restrictions
    unsigned char residual_colour_transform_flag;
    unsigned char bit_depth_luma_minus8;    // Must be 0 (only 8-bit supported)
    unsigned char bit_depth_chroma_minus8;  // Must be 0 (only 8-bit supported)
    unsigned char qpprime_y_zero_transform_bypass_flag;
    // PPS
    int entropy_coding_mode_flag;
    int pic_order_present_flag;
    int num_ref_idx_l0_active_minus1;
    int num_ref_idx_l1_active_minus1;
    int weighted_pred_flag;
    int weighted_bipred_idc;
    int pic_init_qp_minus26;
    int deblocking_filter_control_present_flag;
    int redundant_pic_cnt_present_flag;
    int transform_8x8_mode_flag;
    int MbaffFrameFlag;
    int constrained_intra_pred_flag;
    int chroma_qp_index_offset;
    int second_chroma_qp_index_offset;
    int ref_pic_flag;
    int frame_num;
    int CurrFieldOrderCnt[2];
    // DPB
    CUVIDH264DPBENTRY dpb[16];          // List of reference frames within the DPB
    // Quantization Matrices (raster-order)
    unsigned char WeightScale4x4[6][16];
    unsigned char WeightScale8x8[2][64];
    // FMO/ASO
    unsigned char fmo_aso_enable;
    unsigned char num_slice_groups_minus1;
    unsigned char slice_group_map_type;
    signed char pic_init_qs_minus26;
    unsigned int slice_group_change_rate_minus1;
    union
    {
        unsigned long long slice_group_map_addr;
        const unsigned char *pMb2SliceGroupMap;
    } fmo;
    unsigned int Reserved[12];
    // SVC/MVC
    union
    {
        CUVIDH264MVCEXT mvcext;
        CUVIDH264SVCEXT svcext;
    };
} CUVIDH264PICPARAMS;


/********************************************************/
//! \struct CUVIDMPEG2PICPARAMS
//! MPEG-2 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/********************************************************/
typedef struct _CUVIDMPEG2PICPARAMS
{
    int ForwardRefIdx;          // Picture index of forward reference (P/B-frames)
    int BackwardRefIdx;         // Picture index of backward reference (B-frames)
    int picture_coding_type;
    int full_pel_forward_vector;
    int full_pel_backward_vector;
    int f_code[2][2];
    int intra_dc_precision;
    int frame_pred_frame_dct;
    int concealment_motion_vectors;
    int q_scale_type;
    int intra_vlc_format;
    int alternate_scan;
    int top_field_first;
    // Quantization matrices (raster order)
    unsigned char QuantMatrixIntra[64];
    unsigned char QuantMatrixInter[64];
} CUVIDMPEG2PICPARAMS;

// MPEG-4 has VOP types instead of Picture types
#define I_VOP 0
#define P_VOP 1
#define B_VOP 2
#define S_VOP 3

/*******************************************************/
//! \struct CUVIDMPEG4PICPARAMS
//! MPEG-4 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/*******************************************************/
typedef struct _CUVIDMPEG4PICPARAMS
{
    int ForwardRefIdx;          // Picture index of forward reference (P/B-frames)
    int BackwardRefIdx;         // Picture index of backward reference (B-frames)
    // VOL
    int video_object_layer_width;
    int video_object_layer_height;
    int vop_time_increment_bitcount;
    int top_field_first;
    int resync_marker_disable;
    int quant_type;
    int quarter_sample;
    int short_video_header;
    int divx_flags;
    // VOP
    int vop_coding_type;
    int vop_coded;
    int vop_rounding_type;
    int alternate_vertical_scan_flag;
    int interlaced;
    int vop_fcode_forward;
    int vop_fcode_backward;
    int trd[2];
    int trb[2];
    // Quantization matrices (raster order)
    unsigned char QuantMatrixIntra[64];
    unsigned char QuantMatrixInter[64];
    int gmc_enabled;
} CUVIDMPEG4PICPARAMS;

/********************************************************/
//! \struct CUVIDVC1PICPARAMS
//! VC1 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/********************************************************/
typedef struct _CUVIDVC1PICPARAMS
{
    int ForwardRefIdx;      /**< Picture index of forward reference (P/B-frames) */
    int BackwardRefIdx;     /**< Picture index of backward reference (B-frames)  */
    int FrameWidth;         /**< Actual frame width                              */
    int FrameHeight;        /**< Actual frame height                             */
    // PICTURE
    int intra_pic_flag;     /**< Set to 1 for I,BI frames */
    int ref_pic_flag;       /**< Set to 1 for I,P frames  */
    int progressive_fcm;    /**< Progressive frame        */
    // SEQUENCE
    int profile;
    int postprocflag;
    int pulldown;
    int interlace;
    int tfcntrflag;
    int finterpflag;
    int psf;
    int multires;
    int syncmarker;
    int rangered;
    int maxbframes;
    // ENTRYPOINT
    int panscan_flag;
    int refdist_flag;
    int extended_mv;
    int dquant;
    int vstransform;
    int loopfilter;
    int fastuvmc;
    int overlap;
    int quantizer;
    int extended_dmv;
    int range_mapy_flag;
    int range_mapy;
    int range_mapuv_flag;
    int range_mapuv;
    int rangeredfrm;    // range reduction state
} CUVIDVC1PICPARAMS;

/***********************************************************/
//! \struct CUVIDJPEGPICPARAMS
//! JPEG picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/***********************************************************/
typedef struct _CUVIDJPEGPICPARAMS
{
    int Reserved;
} CUVIDJPEGPICPARAMS;


/*******************************************************/
//! \struct CUVIDHEVCPICPARAMS
//! HEVC picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/*******************************************************/
typedef struct _CUVIDHEVCPICPARAMS
{
    // sps
    int pic_width_in_luma_samples;
    int pic_height_in_luma_samples;
    unsigned char log2_min_luma_coding_block_size_minus3;
    unsigned char log2_diff_max_min_luma_coding_block_size;
    unsigned char log2_min_transform_block_size_minus2;
    unsigned char log2_diff_max_min_transform_block_size;
    unsigned char pcm_enabled_flag;
    unsigned char log2_min_pcm_luma_coding_block_size_minus3;
    unsigned char log2_diff_max_min_pcm_luma_coding_block_size;
    unsigned char pcm_sample_bit_depth_luma_minus1;

    unsigned char pcm_sample_bit_depth_chroma_minus1;
    unsigned char pcm_loop_filter_disabled_flag;
    unsigned char strong_intra_smoothing_enabled_flag;
    unsigned char max_transform_hierarchy_depth_intra;
    unsigned char max_transform_hierarchy_depth_inter;
    unsigned char amp_enabled_flag;
    unsigned char separate_colour_plane_flag;
    unsigned char log2_max_pic_order_cnt_lsb_minus4;

    unsigned char num_short_term_ref_pic_sets;
    unsigned char long_term_ref_pics_present_flag;
    unsigned char num_long_term_ref_pics_sps;
    unsigned char sps_temporal_mvp_enabled_flag;
    unsigned char sample_adaptive_offset_enabled_flag;
    unsigned char scaling_list_enable_flag;
    unsigned char IrapPicFlag;
    unsigned char IdrPicFlag;

    unsigned char bit_depth_luma_minus8;
    unsigned char bit_depth_chroma_minus8;
    unsigned char reserved1[14];

    // pps
    unsigned char dependent_slice_segments_enabled_flag;
    unsigned char slice_segment_header_extension_present_flag;
    unsigned char sign_data_hiding_enabled_flag;
    unsigned char cu_qp_delta_enabled_flag;
    unsigned char diff_cu_qp_delta_depth;
    signed char init_qp_minus26;
    signed char pps_cb_qp_offset;
    signed char pps_cr_qp_offset;

    unsigned char constrained_intra_pred_flag;
    unsigned char weighted_pred_flag;
    unsigned char weighted_bipred_flag;
    unsigned char transform_skip_enabled_flag;
    unsigned char transquant_bypass_enabled_flag;
    unsigned char entropy_coding_sync_enabled_flag;
    unsigned char log2_parallel_merge_level_minus2;
    unsigned char num_extra_slice_header_bits;

    unsigned char loop_filter_across_tiles_enabled_flag;
    unsigned char loop_filter_across_slices_enabled_flag;
    unsigned char output_flag_present_flag;
    unsigned char num_ref_idx_l0_default_active_minus1;
    unsigned char num_ref_idx_l1_default_active_minus1;
    unsigned char lists_modification_present_flag;
    unsigned char cabac_init_present_flag;
    unsigned char pps_slice_chroma_qp_offsets_present_flag;

    unsigned char deblocking_filter_override_enabled_flag;
    unsigned char pps_deblocking_filter_disabled_flag;
    signed char   pps_beta_offset_div2;
    signed char   pps_tc_offset_div2;
    unsigned char tiles_enabled_flag;
    unsigned char uniform_spacing_flag;
    unsigned char num_tile_columns_minus1;
    unsigned char num_tile_rows_minus1;

    unsigned short column_width_minus1[21];
    unsigned short row_height_minus1[21];
    unsigned int   reserved3[15];

    // RefPicSets
    int NumBitsForShortTermRPSInSlice;
    int NumDeltaPocsOfRefRpsIdx;
    int NumPocTotalCurr;
    int NumPocStCurrBefore;
    int NumPocStCurrAfter;
    int NumPocLtCurr;
    int CurrPicOrderCntVal;
    int RefPicIdx[16];                      // [refpic] Indices of valid reference pictures (-1 if unused for reference)
    int PicOrderCntVal[16];                 // [refpic]
    unsigned char IsLongTerm[16];           // [refpic] 0=not a long-term reference, 1=long-term reference
    unsigned char RefPicSetStCurrBefore[8]; // [0..NumPocStCurrBefore-1] -> refpic (0..15)
    unsigned char RefPicSetStCurrAfter[8];  // [0..NumPocStCurrAfter-1] -> refpic (0..15)
    unsigned char RefPicSetLtCurr[8];       // [0..NumPocLtCurr-1] -> refpic (0..15)
    unsigned char RefPicSetInterLayer0[8];
    unsigned char RefPicSetInterLayer1[8];
    unsigned int  reserved4[12];

    // scaling lists (diag order)
    unsigned char ScalingList4x4[6][16];       // [matrixId][i]
    unsigned char ScalingList8x8[6][64];       // [matrixId][i]
    unsigned char ScalingList16x16[6][64];     // [matrixId][i]
    unsigned char ScalingList32x32[2][64];     // [matrixId][i]
    unsigned char ScalingListDCCoeff16x16[6];  // [matrixId]
    unsigned char ScalingListDCCoeff32x32[2];  // [matrixId]
} CUVIDHEVCPICPARAMS;


/***********************************************************/
//! \struct CUVIDVP8PICPARAMS
//! VP8 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/***********************************************************/
typedef struct _CUVIDVP8PICPARAMS
{
    int width;
    int height;
    unsigned int first_partition_size;
    //Frame Indexes
    unsigned char LastRefIdx;
    unsigned char GoldenRefIdx;
    unsigned char AltRefIdx;
    union {
        struct {
            unsigned char frame_type : 1;    /**< 0 = KEYFRAME, 1 = INTERFRAME  */
            unsigned char version : 3;
            unsigned char show_frame : 1;
            unsigned char update_mb_segmentation_data : 1;    /**< Must be 0 if segmentation is not enabled */
            unsigned char Reserved2Bits : 2;
        };
        unsigned char wFrameTagFlags;
    };
    unsigned char Reserved1[4];
    unsigned int  Reserved2[3];
} CUVIDVP8PICPARAMS;

/***********************************************************/
//! \struct CUVIDVP9PICPARAMS
//! VP9 picture parameters
//! This structure is used in CUVIDPICPARAMS structure
/***********************************************************/
typedef struct _CUVIDVP9PICPARAMS
{
    unsigned int width;
    unsigned int height;

    //Frame Indices
    unsigned char LastRefIdx;
    unsigned char GoldenRefIdx;
    unsigned char AltRefIdx;
    unsigned char colorSpace;

    unsigned short profile : 3;
    unsigned short frameContextIdx : 2;
    unsigned short frameType : 1;
    unsigned short showFrame : 1;
    unsigned short errorResilient : 1;
    unsigned short frameParallelDecoding : 1;
    unsigned short subSamplingX : 1;
    unsigned short subSamplingY : 1;
    unsigned short intraOnly : 1;
    unsigned short allow_high_precision_mv : 1;
    unsigned short refreshEntropyProbs : 1;
    unsigned short reserved2Bits : 2;

    unsigned short reserved16Bits;

    unsigned char  refFrameSignBias[4];

    unsigned char bitDepthMinus8Luma;
    unsigned char bitDepthMinus8Chroma;
    unsigned char loopFilterLevel;
    unsigned char loopFilterSharpness;

    unsigned char modeRefLfEnabled;
    unsigned char log2_tile_columns;
    unsigned char log2_tile_rows;

    unsigned char segmentEnabled : 1;
    unsigned char segmentMapUpdate : 1;
    unsigned char segmentMapTemporalUpdate : 1;
    unsigned char segmentFeatureMode : 1;
    unsigned char reserved4Bits : 4;


    unsigned char segmentFeatureEnable[8][4];
    short         segmentFeatureData[8][4];
    unsigned char mb_segment_tree_probs[7];
    unsigned char segment_pred_probs[3];
    unsigned char reservedSegment16Bits[2];

    int qpYAc;
    int qpYDc;
    int qpChDc;
    int qpChAc;

    unsigned int activeRefIdx[3];
    unsigned int resetFrameContext;
    unsigned int mcomp_filter_type;
    unsigned int mbRefLfDelta[4];
    unsigned int mbModeLfDelta[2];
    unsigned int frameTagSize;
    unsigned int offsetToDctParts;
    unsigned int reserved128Bits[4];

} CUVIDVP9PICPARAMS;


/******************************************************************************************/
//! \struct CUVIDPICPARAMS
//! Picture parameters for decoding
//! This structure is used in cuvidDecodePicture API
//! IN  for cuvidDecodePicture
/******************************************************************************************/
typedef struct _CUVIDPICPARAMS
{
    int PicWidthInMbs;                     /**< IN: Coded frame size in macroblocks                           */
    int FrameHeightInMbs;                  /**< IN: Coded frame height in macroblocks                         */
    int CurrPicIdx;                        /**< IN: Output index of the current picture                       */
    int field_pic_flag;                    /**< IN: 0=frame picture, 1=field picture                          */
    int bottom_field_flag;                 /**< IN: 0=top field, 1=bottom field (ignored if field_pic_flag=0) */
    int second_field;                      /**< IN: Second field of a complementary field pair                */
    // Bitstream data
    unsigned int nBitstreamDataLen;        /**< IN: Number of bytes in bitstream data buffer                  */
    const unsigned char *pBitstreamData;   /**< IN: Ptr to bitstream data for this picture (slice-layer)      */
    unsigned int nNumSlices;               /**< IN: Number of slices in this picture                          */
    const unsigned int *pSliceDataOffsets; /**< IN: nNumSlices entries, contains offset of each slice within
                                                        the bitstream data buffer                             */
    int ref_pic_flag;                      /**< IN: This picture is a reference picture                       */
    int intra_pic_flag;                    /**< IN: This picture is entirely intra coded                      */
    unsigned int Reserved[30];             /**< Reserved for future use                                       */
    // IN: Codec-specific data
    union {
        CUVIDMPEG2PICPARAMS mpeg2;         /**< Also used for MPEG-1 */
        CUVIDH264PICPARAMS  h264;
        CUVIDVC1PICPARAMS   vc1;
        CUVIDMPEG4PICPARAMS mpeg4;
        CUVIDJPEGPICPARAMS  jpeg;
        CUVIDHEVCPICPARAMS  hevc;
        CUVIDVP8PICPARAMS   vp8;
        CUVIDVP9PICPARAMS   vp9;
        unsigned int CodecReserved[1024];
    } CodecSpecific;
} CUVIDPICPARAMS;


/******************************************************/
//! \struct CUVIDPROCPARAMS
//! Picture parameters for postprocessing
//! This structure is used in cuvidMapVideoFrame API
/******************************************************/
typedef struct _CUVIDPROCPARAMS
{
    int progressive_frame;              /**< IN: Input is progressive (deinterlace_mode will be ignored)                */
    int second_field;                   /**< IN: Output the second field (ignored if deinterlace mode is Weave)         */
    int top_field_first;                /**< IN: Input frame is top field first (1st field is top, 2nd field is bottom) */
    int unpaired_field;                 /**< IN: Input only contains one field (2nd field is invalid)                   */
    // The fields below are used for raw YUV input
    unsigned int reserved_flags;        /**< Reserved for future use (set to zero)                                      */
    unsigned int reserved_zero;         /**< Reserved (set to zero)                                                     */
    unsigned long long raw_input_dptr;  /**< IN: Input CUdeviceptr for raw YUV extensions                               */
    unsigned int raw_input_pitch;       /**< IN: pitch in bytes of raw YUV input (should be aligned appropriately)      */
    unsigned int raw_input_format;      /**< IN: Input YUV format (cudaVideoCodec_enum)                                 */
    unsigned long long raw_output_dptr; /**< IN: Output CUdeviceptr for raw YUV extensions                              */
    unsigned int raw_output_pitch;      /**< IN: pitch in bytes of raw YUV output (should be aligned appropriately)     */
    unsigned int Reserved1;             /**< Reserved for future use (set to zero)                                      */
    CUstream output_stream;             /**< IN: stream object used by cuvidMapVideoFrame                               */
    unsigned int Reserved[46];          /**< Reserved for future use (set to zero)                                      */
    void *Reserved2[2];                 /**< Reserved for future use (set to zero)                                      */
} CUVIDPROCPARAMS;


/***********************************************************************************************************/
//! VIDEO_DECODER
//!
//! In order to minimize decode latencies, there should be always at least 2 pictures in the decode
//! queue at any time, in order to make sure that all decode engines are always busy.
//!
//! Overall data flow:
//!  - cuvidGetDecoderCaps(...)
//!  - cuvidCreateDecoder(...)
//!  - For each picture:
//!    + cuvidDecodePicture(N)
//!    + cuvidMapVideoFrame(N-4)
//!    + do some processing in cuda
//!    + cuvidUnmapVideoFrame(N-4)
//!    + cuvidDecodePicture(N+1)
//!    + cuvidMapVideoFrame(N-3)
//!    + ...
//!  - cuvidDestroyDecoder(...)
//!
//! NOTE:
//! - When the cuda context is created from a D3D device, the D3D device must also be created
//!   with the D3DCREATE_MULTITHREADED flag.
//! - There is a limit to how many pictures can be mapped simultaneously (ulNumOutputSurfaces)
//! - cuvidDecodePicture may block the calling thread if there are too many pictures pending
//!   in the decode queue
/***********************************************************************************************************/


/**********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidGetDecoderCaps(CUVIDDECODECAPS *pdc)
//! Queries decode capabilities of NVDEC-HW based on CodecType, ChromaFormat and BitDepthMinus8 parameters.
//! 1. Application fills IN parameters CodecType, ChromaFormat and BitDepthMinus8 of CUVIDDECODECAPS structure
//! 2. On calling cuvidGetDecoderCaps, driver fills OUT parameters if the IN parameters are supported
//!    If IN parameters passed to the driver are not supported by NVDEC-HW, then all OUT params are set to 0.
//! E.g. on Geforce GTX 960:
//!   App fills - eCodecType = cudaVideoCodec_H264; eChromaFormat = cudaVideoChromaFormat_420; nBitDepthMinus8 = 0;
//!   Given IN parameters are supported, hence driver fills: bIsSupported = 1; nMinWidth   = 48; nMinHeight  = 16;
//!   nMaxWidth = 4096; nMaxHeight = 4096; nMaxMBCount = 65536;
//! CodedWidth*CodedHeight/256 must be less than or equal to nMaxMBCount
/**********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidGetDecoderCaps(CUVIDDECODECAPS *pdc);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCreateDecoder(CUvideodecoder *phDecoder, CUVIDDECODECREATEINFO *pdci)
//! Create the decoder object based on pdci. A handle to the created decoder is returned
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCreateDecoder(CUvideodecoder *phDecoder, CUVIDDECODECREATEINFO *pdci);
/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidDestroyDecoder(CUvideodecoder hDecoder)
//! Destroy the decoder object.
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidDestroyDecoder(CUvideodecoder hDecoder);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidDecodePicture(CUvideodecoder hDecoder, CUVIDPICPARAMS *pPicParams)
//! Decode a single picture (field or frame)
//! Kicks off HW decoding
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidDecodePicture(CUvideodecoder hDecoder, CUVIDPICPARAMS *pPicParams);


#if !defined(__CUVID_DEVPTR64) || defined(__CUVID_INTERNAL)
/************************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidMapVideoFrame(CUvideodecoder hDecoder, int nPicIdx, unsigned int *pDevPtr,
//!                                         unsigned int  *pPitch, CUVIDPROCPARAMS *pVPP);
//! Post-process and map video frame corresponding to nPicIdx for use in cuda. Returns cuda device pointer and associated
//! pitch of the video frame
/************************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidMapVideoFrame(CUvideodecoder hDecoder, int nPicIdx,
                                           unsigned int *pDevPtr, unsigned int *pPitch,
                                           CUVIDPROCPARAMS *pVPP);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidUnmapVideoFrame(CUvideodecoder hDecoder, unsigned int DevPtr)
//! Unmap a previously mapped video frame
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidUnmapVideoFrame(CUvideodecoder hDecoder, unsigned int DevPtr);
#endif

#if defined(_WIN64) || defined(__LP64__) || defined(__x86_64) || defined(AMD64) || defined(_M_AMD64)
/************************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidMapVideoFrame64(CUvideodecoder hDecoder, int nPicIdx, unsigned long long *pDevPtr,
//!                                           unsigned int *pPitch, CUVIDPROCPARAMS *pVPP);
//! Post-process and map video frame corresponding to nPicIdx for use in cuda. Returns cuda device pointer and associated
//! pitch of the video frame
/************************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidMapVideoFrame64(CUvideodecoder hDecoder, int nPicIdx, unsigned long long *pDevPtr,
                                             unsigned int *pPitch, CUVIDPROCPARAMS *pVPP);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidUnmapVideoFrame64(CUvideodecoder hDecoder, unsigned long long DevPtr);
//! Unmap a previously mapped video frame
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidUnmapVideoFrame64(CUvideodecoder hDecoder, unsigned long long DevPtr);

#if defined(__CUVID_DEVPTR64) && !defined(__CUVID_INTERNAL)
#define tcuvidMapVideoFrame      tcuvidMapVideoFrame64
#define tcuvidUnmapVideoFrame    tcuvidUnmapVideoFrame64
#endif
#endif



/********************************************************************************************************************/
//!
//! Context-locking: to facilitate multi-threaded implementations, the following 4 functions
//! provide a simple mutex-style host synchronization. If a non-NULL context is specified
//! in CUVIDDECODECREATEINFO, the codec library will acquire the mutex associated with the given
//! context before making any cuda calls.
//! A multi-threaded application could create a lock associated with a context handle so that
//! multiple threads can safely share the same cuda context:
//!  - use cuCtxPopCurrent immediately after context creation in order to create a 'floating' context
//!    that can be passed to cuvidCtxLockCreate.
//!  - When using a floating context, all cuda calls should only be made within a cuvidCtxLock/cuvidCtxUnlock section.
//!
//! NOTE: This is a safer alternative to cuCtxPushCurrent and cuCtxPopCurrent, and is not related to video
//! decoder in any way (implemented as a critical section associated with cuCtx{Push|Pop}Current calls).
/********************************************************************************************************************/

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCtxLockCreate(CUvideoctxlock *pLock, CUcontext ctx)
//! This API is used to create CtxLock object
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCtxLockCreate(CUvideoctxlock *pLock, CUcontext ctx);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCtxLockDestroy(CUvideoctxlock lck)
//! This API is used to free CtxLock object
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCtxLockDestroy(CUvideoctxlock lck);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCtxLock(CUvideoctxlock lck, unsigned int reserved_flags)
//! This API is used to acquire ctxlock
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCtxLock(CUvideoctxlock lck, unsigned int reserved_flags);

/********************************************************************************************************************/
//! \fn CUresult CUDAAPI cuvidCtxUnlock(CUvideoctxlock lck, unsigned int reserved_flags)
//! This API is used to release ctxlock
/********************************************************************************************************************/
typedef CUresult CUDAAPI tcuvidCtxUnlock(CUvideoctxlock lck, unsigned int reserved_flags);

/**********************************************************************************************/

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif // __CUDA_VIDEO_H__
