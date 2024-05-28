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

#include "codec.h"
#include "defs.h"
#include "profiles.h"

#if !CONFIG_SMALL

const AVProfile ff_aac_profiles[] = {
    { AV_PROFILE_AAC_LOW,   "LC"       },
    { AV_PROFILE_AAC_HE,    "HE-AAC"   },
    { AV_PROFILE_AAC_HE_V2, "HE-AACv2" },
    { AV_PROFILE_AAC_LD,    "LD"       },
    { AV_PROFILE_AAC_ELD,   "ELD"      },
    { AV_PROFILE_AAC_MAIN,  "Main" },
    { AV_PROFILE_AAC_SSR,   "SSR"  },
    { AV_PROFILE_AAC_LTP,   "LTP"  },
    { AV_PROFILE_AAC_USAC,  "xHE-AAC" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_dca_profiles[] = {
    { AV_PROFILE_DTS,                "DTS"                    },
    { AV_PROFILE_DTS_ES,             "DTS-ES"                 },
    { AV_PROFILE_DTS_96_24,          "DTS 96/24"              },
    { AV_PROFILE_DTS_HD_HRA,         "DTS-HD HRA"             },
    { AV_PROFILE_DTS_HD_MA,          "DTS-HD MA"              },
    { AV_PROFILE_DTS_HD_MA_X,        "DTS-HD MA + DTS:X"      },
    { AV_PROFILE_DTS_HD_MA_X_IMAX,   "DTS-HD MA + DTS:X IMAX" },
    { AV_PROFILE_DTS_EXPRESS,        "DTS Express"            },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_eac3_profiles[] = {
  { AV_PROFILE_EAC3_DDP_ATMOS, "Dolby Digital Plus + Dolby Atmos"},
  { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_truehd_profiles[] = {
  { AV_PROFILE_TRUEHD_ATMOS,   "Dolby TrueHD + Dolby Atmos"},
  { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_dnxhd_profiles[] = {
  { AV_PROFILE_DNXHD,      "DNXHD"},
  { AV_PROFILE_DNXHR_LB,   "DNXHR LB"},
  { AV_PROFILE_DNXHR_SQ,   "DNXHR SQ"},
  { AV_PROFILE_DNXHR_HQ,   "DNXHR HQ" },
  { AV_PROFILE_DNXHR_HQX,  "DNXHR HQX"},
  { AV_PROFILE_DNXHR_444,  "DNXHR 444"},
  { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_h264_profiles[] = {
    { AV_PROFILE_H264_BASELINE,             "Baseline"              },
    { AV_PROFILE_H264_CONSTRAINED_BASELINE, "Constrained Baseline"  },
    { AV_PROFILE_H264_MAIN,                 "Main"                  },
    { AV_PROFILE_H264_EXTENDED,             "Extended"              },
    { AV_PROFILE_H264_HIGH,                 "High"                  },
    { AV_PROFILE_H264_HIGH_10,              "High 10"               },
    { AV_PROFILE_H264_HIGH_10_INTRA,        "High 10 Intra"         },
    { AV_PROFILE_H264_HIGH_422,             "High 4:2:2"            },
    { AV_PROFILE_H264_HIGH_422_INTRA,       "High 4:2:2 Intra"      },
    { AV_PROFILE_H264_HIGH_444,             "High 4:4:4"            },
    { AV_PROFILE_H264_HIGH_444_PREDICTIVE,  "High 4:4:4 Predictive" },
    { AV_PROFILE_H264_HIGH_444_INTRA,       "High 4:4:4 Intra"      },
    { AV_PROFILE_H264_CAVLC_444,            "CAVLC 4:4:4"           },
    { AV_PROFILE_H264_MULTIVIEW_HIGH,       "Multiview High"        },
    { AV_PROFILE_H264_STEREO_HIGH,          "Stereo High"           },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_vvc_profiles[] = {
    { AV_PROFILE_VVC_MAIN_10,                   "Main 10" },
    { AV_PROFILE_VVC_MAIN_10_444,               "Main 10 4:4:4" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_hevc_profiles[] = {
    { AV_PROFILE_HEVC_MAIN,                 "Main"                },
    { AV_PROFILE_HEVC_MAIN_10,              "Main 10"             },
    { AV_PROFILE_HEVC_MAIN_STILL_PICTURE,   "Main Still Picture"  },
    { AV_PROFILE_HEVC_REXT,                 "Rext"                },
    { AV_PROFILE_HEVC_MULTIVIEW_MAIN,       "Multiview Main"      },
    { AV_PROFILE_HEVC_SCC,                  "Scc"                 },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_jpeg2000_profiles[] = {
    { AV_PROFILE_JPEG2000_CSTREAM_RESTRICTION_0,  "JPEG 2000 codestream restriction 0"   },
    { AV_PROFILE_JPEG2000_CSTREAM_RESTRICTION_1,  "JPEG 2000 codestream restriction 1"   },
    { AV_PROFILE_JPEG2000_CSTREAM_NO_RESTRICTION, "JPEG 2000 no codestream restrictions" },
    { AV_PROFILE_JPEG2000_DCINEMA_2K,             "JPEG 2000 digital cinema 2K"          },
    { AV_PROFILE_JPEG2000_DCINEMA_4K,             "JPEG 2000 digital cinema 4K"          },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_mpeg2_video_profiles[] = {
    { AV_PROFILE_MPEG2_422,          "4:2:2"              },
    { AV_PROFILE_MPEG2_HIGH,         "High"               },
    { AV_PROFILE_MPEG2_SS,           "Spatially Scalable" },
    { AV_PROFILE_MPEG2_SNR_SCALABLE, "SNR Scalable"       },
    { AV_PROFILE_MPEG2_MAIN,         "Main"               },
    { AV_PROFILE_MPEG2_SIMPLE,       "Simple"             },
    { AV_PROFILE_RESERVED,           "Reserved"           },
    { AV_PROFILE_UNKNOWN                                  },
};

const AVProfile ff_mpeg4_video_profiles[] = {
    { AV_PROFILE_MPEG4_SIMPLE,                    "Simple Profile" },
    { AV_PROFILE_MPEG4_SIMPLE_SCALABLE,           "Simple Scalable Profile" },
    { AV_PROFILE_MPEG4_CORE,                      "Core Profile" },
    { AV_PROFILE_MPEG4_MAIN,                      "Main Profile" },
    { AV_PROFILE_MPEG4_N_BIT,                     "N-bit Profile" },
    { AV_PROFILE_MPEG4_SCALABLE_TEXTURE,          "Scalable Texture Profile" },
    { AV_PROFILE_MPEG4_SIMPLE_FACE_ANIMATION,     "Simple Face Animation Profile" },
    { AV_PROFILE_MPEG4_BASIC_ANIMATED_TEXTURE,    "Basic Animated Texture Profile" },
    { AV_PROFILE_MPEG4_HYBRID,                    "Hybrid Profile" },
    { AV_PROFILE_MPEG4_ADVANCED_REAL_TIME,        "Advanced Real Time Simple Profile" },
    { AV_PROFILE_MPEG4_CORE_SCALABLE,             "Code Scalable Profile" },
    { AV_PROFILE_MPEG4_ADVANCED_CODING,           "Advanced Coding Profile" },
    { AV_PROFILE_MPEG4_ADVANCED_CORE,             "Advanced Core Profile" },
    { AV_PROFILE_MPEG4_ADVANCED_SCALABLE_TEXTURE, "Advanced Scalable Texture Profile" },
    { AV_PROFILE_MPEG4_SIMPLE_STUDIO,             "Simple Studio Profile" },
    { AV_PROFILE_MPEG4_ADVANCED_SIMPLE,           "Advanced Simple Profile" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_vc1_profiles[] = {
    { AV_PROFILE_VC1_SIMPLE,   "Simple"   },
    { AV_PROFILE_VC1_MAIN,     "Main"     },
    { AV_PROFILE_VC1_COMPLEX,  "Complex"  },
    { AV_PROFILE_VC1_ADVANCED, "Advanced" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_vp9_profiles[] = {
    { AV_PROFILE_VP9_0, "Profile 0" },
    { AV_PROFILE_VP9_1, "Profile 1" },
    { AV_PROFILE_VP9_2, "Profile 2" },
    { AV_PROFILE_VP9_3, "Profile 3" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_av1_profiles[] = {
    { AV_PROFILE_AV1_MAIN,         "Main" },
    { AV_PROFILE_AV1_HIGH,         "High" },
    { AV_PROFILE_AV1_PROFESSIONAL, "Professional" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_sbc_profiles[] = {
    { AV_PROFILE_SBC_MSBC, "mSBC" },
    { AV_PROFILE_UNKNOWN },
};

const AVProfile ff_prores_profiles[] = {
    { AV_PROFILE_PRORES_PROXY,    "Proxy"    },
    { AV_PROFILE_PRORES_LT,       "LT"       },
    { AV_PROFILE_PRORES_STANDARD, "Standard" },
    { AV_PROFILE_PRORES_HQ,       "HQ"       },
    { AV_PROFILE_PRORES_4444,     "4444"     },
    { AV_PROFILE_PRORES_XQ,       "XQ"       },
    { AV_PROFILE_UNKNOWN }
};

const AVProfile ff_mjpeg_profiles[] = {
    { AV_PROFILE_MJPEG_HUFFMAN_BASELINE_DCT,            "Baseline"    },
    { AV_PROFILE_MJPEG_HUFFMAN_EXTENDED_SEQUENTIAL_DCT, "Sequential"  },
    { AV_PROFILE_MJPEG_HUFFMAN_PROGRESSIVE_DCT,         "Progressive" },
    { AV_PROFILE_MJPEG_HUFFMAN_LOSSLESS,                "Lossless"    },
    { AV_PROFILE_MJPEG_JPEG_LS,                         "JPEG LS"     },
    { AV_PROFILE_UNKNOWN }
};

const AVProfile ff_arib_caption_profiles[] = {
    { AV_PROFILE_ARIB_PROFILE_A, "Profile A" },
    { AV_PROFILE_ARIB_PROFILE_C, "Profile C" },
    { AV_PROFILE_UNKNOWN }
};

const AVProfile ff_evc_profiles[] = {
    { AV_PROFILE_EVC_BASELINE,             "Baseline"              },
    { AV_PROFILE_EVC_MAIN,                 "Main"                  },
    { AV_PROFILE_UNKNOWN },
};

#endif /* !CONFIG_SMALL */
