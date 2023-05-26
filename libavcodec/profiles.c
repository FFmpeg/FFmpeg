/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "config.h"

#include "avcodec.h"
#include "profiles.h"

#if !CONFIG_SMALL

const AVProfile ff_aac_profiles[] = {
    { FF_PROFILE_AAC_LOW,   "LC"       },
    { FF_PROFILE_AAC_HE,    "HE-AAC"   },
    { FF_PROFILE_AAC_HE_V2, "HE-AACv2" },
    { FF_PROFILE_AAC_LD,    "LD"       },
    { FF_PROFILE_AAC_ELD,   "ELD"      },
    { FF_PROFILE_AAC_MAIN,  "Main" },
    { FF_PROFILE_AAC_SSR,   "SSR"  },
    { FF_PROFILE_AAC_LTP,   "LTP"  },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_dca_profiles[] = {
    { FF_PROFILE_DTS,                "DTS"                    },
    { FF_PROFILE_DTS_ES,             "DTS-ES"                 },
    { FF_PROFILE_DTS_96_24,          "DTS 96/24"              },
    { FF_PROFILE_DTS_HD_HRA,         "DTS-HD HRA"             },
    { FF_PROFILE_DTS_HD_MA,          "DTS-HD MA"              },
    { FF_PROFILE_DTS_HD_MA_X,        "DTS-HD MA + DTS:X"      },
    { FF_PROFILE_DTS_HD_MA_X_IMAX,   "DTS-HD MA + DTS:X IMAX" },
    { FF_PROFILE_DTS_EXPRESS,        "DTS Express"            },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_eac3_profiles[] = {
  { FF_PROFILE_EAC3_DDP_ATMOS, "Dolby Digital Plus + Dolby Atmos"},
  { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_truehd_profiles[] = {
  { FF_PROFILE_TRUEHD_ATMOS,   "Dolby TrueHD + Dolby Atmos"},
  { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_dnxhd_profiles[] = {
  { FF_PROFILE_DNXHD,      "DNXHD"},
  { FF_PROFILE_DNXHR_LB,   "DNXHR LB"},
  { FF_PROFILE_DNXHR_SQ,   "DNXHR SQ"},
  { FF_PROFILE_DNXHR_HQ,   "DNXHR HQ" },
  { FF_PROFILE_DNXHR_HQX,  "DNXHR HQX"},
  { FF_PROFILE_DNXHR_444,  "DNXHR 444"},
  { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_h264_profiles[] = {
    { FF_PROFILE_H264_BASELINE,             "Baseline"              },
    { FF_PROFILE_H264_CONSTRAINED_BASELINE, "Constrained Baseline"  },
    { FF_PROFILE_H264_MAIN,                 "Main"                  },
    { FF_PROFILE_H264_EXTENDED,             "Extended"              },
    { FF_PROFILE_H264_HIGH,                 "High"                  },
    { FF_PROFILE_H264_HIGH_10,              "High 10"               },
    { FF_PROFILE_H264_HIGH_10_INTRA,        "High 10 Intra"         },
    { FF_PROFILE_H264_HIGH_422,             "High 4:2:2"            },
    { FF_PROFILE_H264_HIGH_422_INTRA,       "High 4:2:2 Intra"      },
    { FF_PROFILE_H264_HIGH_444,             "High 4:4:4"            },
    { FF_PROFILE_H264_HIGH_444_PREDICTIVE,  "High 4:4:4 Predictive" },
    { FF_PROFILE_H264_HIGH_444_INTRA,       "High 4:4:4 Intra"      },
    { FF_PROFILE_H264_CAVLC_444,            "CAVLC 4:4:4"           },
    { FF_PROFILE_H264_MULTIVIEW_HIGH,       "Multiview High"        },
    { FF_PROFILE_H264_STEREO_HIGH,          "Stereo High"           },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_vvc_profiles[] = {
    { FF_PROFILE_VVC_MAIN_10,                   "Main 10" },
    { FF_PROFILE_VVC_MAIN_10_444,               "Main 10 4:4:4" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_hevc_profiles[] = {
    { FF_PROFILE_HEVC_MAIN,                 "Main"                },
    { FF_PROFILE_HEVC_MAIN_10,              "Main 10"             },
    { FF_PROFILE_HEVC_MAIN_STILL_PICTURE,   "Main Still Picture"  },
    { FF_PROFILE_HEVC_REXT,                 "Rext"                },
    { FF_PROFILE_HEVC_SCC,                  "Scc"                 },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_jpeg2000_profiles[] = {
    { FF_PROFILE_JPEG2000_CSTREAM_RESTRICTION_0,  "JPEG 2000 codestream restriction 0"   },
    { FF_PROFILE_JPEG2000_CSTREAM_RESTRICTION_1,  "JPEG 2000 codestream restriction 1"   },
    { FF_PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION, "JPEG 2000 no codestream restrictions" },
    { FF_PROFILE_JPEG2000_DCINEMA_2K,             "JPEG 2000 digital cinema 2K"          },
    { FF_PROFILE_JPEG2000_DCINEMA_4K,             "JPEG 2000 digital cinema 4K"          },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_mpeg2_video_profiles[] = {
    { FF_PROFILE_MPEG2_422,          "4:2:2"              },
    { FF_PROFILE_MPEG2_HIGH,         "High"               },
    { FF_PROFILE_MPEG2_SS,           "Spatially Scalable" },
    { FF_PROFILE_MPEG2_SNR_SCALABLE, "SNR Scalable"       },
    { FF_PROFILE_MPEG2_MAIN,         "Main"               },
    { FF_PROFILE_MPEG2_SIMPLE,       "Simple"             },
    { FF_PROFILE_RESERVED,           "Reserved"           },
    { FF_PROFILE_UNKNOWN                                  },
};

const AVProfile ff_mpeg4_video_profiles[] = {
    { FF_PROFILE_MPEG4_SIMPLE,                    "Simple Profile" },
    { FF_PROFILE_MPEG4_SIMPLE_SCALABLE,           "Simple Scalable Profile" },
    { FF_PROFILE_MPEG4_CORE,                      "Core Profile" },
    { FF_PROFILE_MPEG4_MAIN,                      "Main Profile" },
    { FF_PROFILE_MPEG4_N_BIT,                     "N-bit Profile" },
    { FF_PROFILE_MPEG4_SCALABLE_TEXTURE,          "Scalable Texture Profile" },
    { FF_PROFILE_MPEG4_SIMPLE_FACE_ANIMATION,     "Simple Face Animation Profile" },
    { FF_PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE,    "Basic Animated Texture Profile" },
    { FF_PROFILE_MPEG4_HYBRID,                    "Hybrid Profile" },
    { FF_PROFILE_MPEG4_ADVANCED_REAL_TIME,        "Advanced Real Time Simple Profile" },
    { FF_PROFILE_MPEG4_CORE_SCALABLE,             "Code Scalable Profile" },
    { FF_PROFILE_MPEG4_ADVANCED_CODING,           "Advanced Coding Profile" },
    { FF_PROFILE_MPEG4_ADVANCED_CORE,             "Advanced Core Profile" },
    { FF_PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE, "Advanced Scalable Texture Profile" },
    { FF_PROFILE_MPEG4_SIMPLE_STUDIO,             "Simple Studio Profile" },
    { FF_PROFILE_MPEG4_ADVANCED_SIMPLE,           "Advanced Simple Profile" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_vc1_profiles[] = {
    { FF_PROFILE_VC1_SIMPLE,   "Simple"   },
    { FF_PROFILE_VC1_MAIN,     "Main"     },
    { FF_PROFILE_VC1_COMPLEX,  "Complex"  },
    { FF_PROFILE_VC1_ADVANCED, "Advanced" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_vp9_profiles[] = {
    { FF_PROFILE_VP9_0, "Profile 0" },
    { FF_PROFILE_VP9_1, "Profile 1" },
    { FF_PROFILE_VP9_2, "Profile 2" },
    { FF_PROFILE_VP9_3, "Profile 3" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_av1_profiles[] = {
    { FF_PROFILE_AV1_MAIN,         "Main" },
    { FF_PROFILE_AV1_HIGH,         "High" },
    { FF_PROFILE_AV1_PROFESSIONAL, "Professional" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_sbc_profiles[] = {
    { FF_PROFILE_SBC_MSBC, "mSBC" },
    { FF_PROFILE_UNKNOWN },
};

const AVProfile ff_prores_profiles[] = {
    { FF_PROFILE_PRORES_PROXY,    "Proxy"    },
    { FF_PROFILE_PRORES_LT,       "LT"       },
    { FF_PROFILE_PRORES_STANDARD, "Standard" },
    { FF_PROFILE_PRORES_HQ,       "HQ"       },
    { FF_PROFILE_PRORES_4444,     "4444"     },
    { FF_PROFILE_PRORES_XQ,       "XQ"       },
    { FF_PROFILE_UNKNOWN }
};

const AVProfile ff_mjpeg_profiles[] = {
    { FF_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,            "Baseline"    },
    { FF_PROFILE_MJPEG_HUFFMAN_EXTENDED_SEQUENTIAL_DCT, "Sequential"  },
    { FF_PROFILE_MJPEG_HUFFMAN_PROGRESSIVE_DCT,         "Progressive" },
    { FF_PROFILE_MJPEG_HUFFMAN_LOSSLESS,                "Lossless"    },
    { FF_PROFILE_MJPEG_JPEG_LS,                         "JPEG LS"     },
    { FF_PROFILE_UNKNOWN }
};

const AVProfile ff_arib_caption_profiles[] = {
    { FF_PROFILE_ARIB_PROFILE_A, "Profile A" },
    { FF_PROFILE_ARIB_PROFILE_C, "Profile C" },
    { FF_PROFILE_UNKNOWN }
};

const AVProfile ff_evc_profiles[] = {
    { FF_PROFILE_EVC_BASELINE,             "Baseline"              },
    { FF_PROFILE_EVC_MAIN,                 "Main"                  },
    { FF_PROFILE_UNKNOWN },
};

#endif /* !CONFIG_SMALL */
