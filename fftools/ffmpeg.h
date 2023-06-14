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

#ifndef FFTOOLS_FFMPEG_H
#define FFTOOLS_FFMPEG_H

#include "config.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>

#include "cmdutils.h"
#include "sync_queue.h"

#include "libavformat/avformat.h"
#include "libavformat/avio.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"

#include "libavfilter/avfilter.h"

#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/eval.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"
#include "libavutil/thread.h"
#include "libavutil/threadmessage.h"

#include "libswresample/swresample.h"

// deprecated features
#define FFMPEG_OPT_PSNR 1
#define FFMPEG_OPT_MAP_CHANNEL 1
#define FFMPEG_OPT_MAP_SYNC 1
#define FFMPEG_ROTATION_METADATA 1
#define FFMPEG_OPT_QPHIST 1
#define FFMPEG_OPT_ADRIFT_THRESHOLD 1

enum VideoSyncMethod {
    VSYNC_AUTO = -1,
    VSYNC_PASSTHROUGH,
    VSYNC_CFR,
    VSYNC_VFR,
    VSYNC_VSCFR,
    VSYNC_DROP,
};

#define MAX_STREAMS 1024    /* arbitrary sanity check value */

enum HWAccelID {
    HWACCEL_NONE = 0,
    HWACCEL_AUTO,
    HWACCEL_GENERIC,
};

typedef struct HWDevice {
    const char *name;
    enum AVHWDeviceType type;
    AVBufferRef *device_ref;
} HWDevice;

/* select an input stream for an output stream */
typedef struct StreamMap {
    int disabled;           /* 1 is this mapping is disabled by a negative map */
    int file_index;
    int stream_index;
    char *linklabel;       /* name of an output link, for mapping lavfi outputs */
} StreamMap;

#if FFMPEG_OPT_MAP_CHANNEL
typedef struct {
    int  file_idx,  stream_idx,  channel_idx; // input
    int ofile_idx, ostream_idx;               // output
} AudioChannelMap;
#endif

typedef struct DemuxPktData {
    // estimated dts in AV_TIME_BASE_Q,
    // to be used when real dts is missing
    int64_t dts_est;
} DemuxPktData;

typedef struct OptionsContext {
    OptionGroup *g;

    /* input/output options */
    int64_t start_time;
    int64_t start_time_eof;
    int seek_timestamp;
    const char *format;

    SpecifierOpt *codec_names;
    int        nb_codec_names;
    SpecifierOpt *audio_ch_layouts;
    int        nb_audio_ch_layouts;
    SpecifierOpt *audio_channels;
    int        nb_audio_channels;
    SpecifierOpt *audio_sample_rate;
    int        nb_audio_sample_rate;
    SpecifierOpt *frame_rates;
    int        nb_frame_rates;
    SpecifierOpt *max_frame_rates;
    int        nb_max_frame_rates;
    SpecifierOpt *frame_sizes;
    int        nb_frame_sizes;
    SpecifierOpt *frame_pix_fmts;
    int        nb_frame_pix_fmts;

    /* input options */
    int64_t input_ts_offset;
    int loop;
    int rate_emu;
    float readrate;
    double readrate_initial_burst;
    int accurate_seek;
    int thread_queue_size;
    int input_sync_ref;
    int find_stream_info;

    SpecifierOpt *ts_scale;
    int        nb_ts_scale;
    SpecifierOpt *dump_attachment;
    int        nb_dump_attachment;
    SpecifierOpt *hwaccels;
    int        nb_hwaccels;
    SpecifierOpt *hwaccel_devices;
    int        nb_hwaccel_devices;
    SpecifierOpt *hwaccel_output_formats;
    int        nb_hwaccel_output_formats;
    SpecifierOpt *autorotate;
    int        nb_autorotate;

    /* output options */
    StreamMap *stream_maps;
    int     nb_stream_maps;
#if FFMPEG_OPT_MAP_CHANNEL
    AudioChannelMap *audio_channel_maps; /* one info entry per -map_channel */
    int           nb_audio_channel_maps; /* number of (valid) -map_channel settings */
#endif
    const char **attachments;
    int       nb_attachments;

    int chapters_input_file;

    int64_t recording_time;
    int64_t stop_time;
    int64_t limit_filesize;
    float mux_preload;
    float mux_max_delay;
    float shortest_buf_duration;
    int shortest;
    int bitexact;

    int video_disable;
    int audio_disable;
    int subtitle_disable;
    int data_disable;

    /* indexed by output file stream index */
    int   *streamid_map;
    int nb_streamid_map;

    SpecifierOpt *metadata;
    int        nb_metadata;
    SpecifierOpt *max_frames;
    int        nb_max_frames;
    SpecifierOpt *bitstream_filters;
    int        nb_bitstream_filters;
    SpecifierOpt *codec_tags;
    int        nb_codec_tags;
    SpecifierOpt *sample_fmts;
    int        nb_sample_fmts;
    SpecifierOpt *qscale;
    int        nb_qscale;
    SpecifierOpt *forced_key_frames;
    int        nb_forced_key_frames;
    SpecifierOpt *fps_mode;
    int        nb_fps_mode;
    SpecifierOpt *force_fps;
    int        nb_force_fps;
    SpecifierOpt *frame_aspect_ratios;
    int        nb_frame_aspect_ratios;
    SpecifierOpt *display_rotations;
    int        nb_display_rotations;
    SpecifierOpt *display_hflips;
    int        nb_display_hflips;
    SpecifierOpt *display_vflips;
    int        nb_display_vflips;
    SpecifierOpt *rc_overrides;
    int        nb_rc_overrides;
    SpecifierOpt *intra_matrices;
    int        nb_intra_matrices;
    SpecifierOpt *inter_matrices;
    int        nb_inter_matrices;
    SpecifierOpt *chroma_intra_matrices;
    int        nb_chroma_intra_matrices;
    SpecifierOpt *top_field_first;
    int        nb_top_field_first;
    SpecifierOpt *metadata_map;
    int        nb_metadata_map;
    SpecifierOpt *presets;
    int        nb_presets;
    SpecifierOpt *copy_initial_nonkeyframes;
    int        nb_copy_initial_nonkeyframes;
    SpecifierOpt *copy_prior_start;
    int        nb_copy_prior_start;
    SpecifierOpt *filters;
    int        nb_filters;
    SpecifierOpt *filter_scripts;
    int        nb_filter_scripts;
    SpecifierOpt *reinit_filters;
    int        nb_reinit_filters;
    SpecifierOpt *fix_sub_duration;
    int        nb_fix_sub_duration;
    SpecifierOpt *fix_sub_duration_heartbeat;
    int        nb_fix_sub_duration_heartbeat;
    SpecifierOpt *canvas_sizes;
    int        nb_canvas_sizes;
    SpecifierOpt *pass;
    int        nb_pass;
    SpecifierOpt *passlogfiles;
    int        nb_passlogfiles;
    SpecifierOpt *max_muxing_queue_size;
    int        nb_max_muxing_queue_size;
    SpecifierOpt *muxing_queue_data_threshold;
    int        nb_muxing_queue_data_threshold;
    SpecifierOpt *guess_layout_max;
    int        nb_guess_layout_max;
    SpecifierOpt *apad;
    int        nb_apad;
    SpecifierOpt *discard;
    int        nb_discard;
    SpecifierOpt *disposition;
    int        nb_disposition;
    SpecifierOpt *program;
    int        nb_program;
    SpecifierOpt *time_bases;
    int        nb_time_bases;
    SpecifierOpt *enc_time_bases;
    int        nb_enc_time_bases;
    SpecifierOpt *autoscale;
    int        nb_autoscale;
    SpecifierOpt *bits_per_raw_sample;
    int        nb_bits_per_raw_sample;
    SpecifierOpt *enc_stats_pre;
    int        nb_enc_stats_pre;
    SpecifierOpt *enc_stats_post;
    int        nb_enc_stats_post;
    SpecifierOpt *mux_stats;
    int        nb_mux_stats;
    SpecifierOpt *enc_stats_pre_fmt;
    int        nb_enc_stats_pre_fmt;
    SpecifierOpt *enc_stats_post_fmt;
    int        nb_enc_stats_post_fmt;
    SpecifierOpt *mux_stats_fmt;
    int        nb_mux_stats_fmt;
} OptionsContext;

typedef struct InputFilter {
    struct FilterGraph *graph;
    uint8_t            *name;
} InputFilter;

typedef struct OutputFilter {
    AVFilterContext     *filter;
    struct OutputStream *ost;
    struct FilterGraph  *graph;
    uint8_t             *name;

    /* for filters that are not yet bound to an output stream,
     * this stores the output linklabel, if any */
    uint8_t             *linklabel;

    enum AVMediaType     type;

    /* desired output stream properties */
    int width, height;
    int format;
    int sample_rate;
    AVChannelLayout ch_layout;

    // those are only set if no format is specified and the encoder gives us multiple options
    // They point directly to the relevant lists of the encoder.
    const int *formats;
    const AVChannelLayout *ch_layouts;
    const int *sample_rates;

    /* pts of the last frame received from this filter, in AV_TIME_BASE_Q */
    int64_t last_pts;
} OutputFilter;

typedef struct FilterGraph {
    const AVClass *class;
    int            index;

    AVFilterGraph *graph;

    InputFilter   **inputs;
    int          nb_inputs;
    OutputFilter **outputs;
    int         nb_outputs;
} FilterGraph;

typedef struct Decoder Decoder;

typedef struct InputStream {
    const AVClass *class;

    int file_index;
    int index;

    AVStream *st;
    int discard;             /* true if stream data should be discarded */
    int user_set_discard;
    int decoding_needed;     /* non zero if the packets must be decoded in 'raw_fifo', see DECODING_FOR_* */
#define DECODING_FOR_OST    1
#define DECODING_FOR_FILTER 2

    /**
     * Codec parameters - to be used by the decoding/streamcopy code.
     * st->codecpar should not be accessed, because it may be modified
     * concurrently by the demuxing thread.
     */
    AVCodecParameters *par;
    Decoder *decoder;
    AVCodecContext *dec_ctx;
    const AVCodec *dec;
    const AVCodecDescriptor *codec_desc;

    AVRational framerate_guessed;

    int64_t nb_samples; /* number of samples in the last decoded audio frame before looping */

    AVDictionary *decoder_opts;
    AVRational framerate;               /* framerate forced with -r */
    int top_field_first;

    int autorotate;

    int fix_sub_duration;

    struct sub2video {
        int w, h;
    } sub2video;

    /* decoded data from this stream goes into all those filters
     * currently video and audio only */
    InputFilter **filters;
    int        nb_filters;

    /*
     * Output targets that do not go through lavfi, i.e. subtitles or
     * streamcopy. Those two cases are distinguished by the OutputStream
     * having an encoder or not.
     */
    struct OutputStream **outputs;
    int                nb_outputs;

    int reinit_filters;

    /* hwaccel options */
    enum HWAccelID hwaccel_id;
    enum AVHWDeviceType hwaccel_device_type;
    char  *hwaccel_device;
    enum AVPixelFormat hwaccel_output_format;

    /* stats */
    // number of frames/samples retrieved from the decoder
    uint64_t frames_decoded;
    uint64_t samples_decoded;
    uint64_t decode_errors;
} InputStream;

typedef struct LastFrameDuration {
    int     stream_idx;
    int64_t duration;
} LastFrameDuration;

typedef struct InputFile {
    const AVClass *class;

    int index;

    // input format has no timestamps
    int format_nots;

    AVFormatContext *ctx;
    int eof_reached;      /* true if eof reached */
    int eagain;           /* true if last read attempt returned EAGAIN */
    int64_t input_ts_offset;
    int input_sync_ref;
    /**
     * Effective format start time based on enabled streams.
     */
    int64_t start_time_effective;
    int64_t ts_offset;
    int64_t start_time;   /* user-specified start time in AV_TIME_BASE or AV_NOPTS_VALUE */
    int64_t recording_time;

    /* streams that ffmpeg is aware of;
     * there may be extra streams in ctx that are not mapped to an InputStream
     * if new streams appear dynamically during demuxing */
    InputStream **streams;
    int        nb_streams;

    float readrate;
    int accurate_seek;

    /* when looping the input file, this queue is used by decoders to report
     * the last frame duration back to the demuxer thread */
    AVThreadMessageQueue *audio_duration_queue;
    int                   audio_duration_queue_size;
} InputFile;

enum forced_keyframes_const {
    FKF_N,
    FKF_N_FORCED,
    FKF_PREV_FORCED_N,
    FKF_PREV_FORCED_T,
    FKF_T,
    FKF_NB
};

#define ABORT_ON_FLAG_EMPTY_OUTPUT        (1 <<  0)
#define ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM (1 <<  1)

enum EncStatsType {
    ENC_STATS_LITERAL = 0,
    ENC_STATS_FILE_IDX,
    ENC_STATS_STREAM_IDX,
    ENC_STATS_FRAME_NUM,
    ENC_STATS_FRAME_NUM_IN,
    ENC_STATS_TIMEBASE,
    ENC_STATS_TIMEBASE_IN,
    ENC_STATS_PTS,
    ENC_STATS_PTS_TIME,
    ENC_STATS_PTS_IN,
    ENC_STATS_PTS_TIME_IN,
    ENC_STATS_DTS,
    ENC_STATS_DTS_TIME,
    ENC_STATS_SAMPLE_NUM,
    ENC_STATS_NB_SAMPLES,
    ENC_STATS_PKT_SIZE,
    ENC_STATS_BITRATE,
    ENC_STATS_AVG_BITRATE,
};

typedef struct EncStatsComponent {
    enum EncStatsType type;

    uint8_t *str;
    size_t   str_len;
} EncStatsComponent;

typedef struct EncStats {
    EncStatsComponent  *components;
    int              nb_components;

    AVIOContext        *io;
} EncStats;

extern const char *const forced_keyframes_const_names[];

typedef enum {
    ENCODER_FINISHED = 1,
    MUXER_FINISHED = 2,
} OSTFinished ;

enum {
    KF_FORCE_SOURCE         = 1,
    KF_FORCE_SOURCE_NO_DROP = 2,
};

typedef struct KeyframeForceCtx {
    int          type;

    int64_t      ref_pts;

    // timestamps of the forced keyframes, in AV_TIME_BASE_Q
    int64_t     *pts;
    int       nb_pts;
    int          index;

    AVExpr      *pexpr;
    double       expr_const_values[FKF_NB];

    int          dropped_keyframe;
} KeyframeForceCtx;

typedef struct Encoder Encoder;

typedef struct OutputStream {
    const AVClass *class;

    enum AVMediaType type;

    int file_index;          /* file index */
    int index;               /* stream index in the output file */

    /**
     * Codec parameters for packets submitted to the muxer (i.e. before
     * bitstream filtering, if any).
     */
    AVCodecParameters *par_in;

    /* input stream that is the source for this output stream;
     * may be NULL for streams with no well-defined source, e.g.
     * attachments or outputs from complex filtergraphs */
    InputStream *ist;

    AVStream *st;            /* stream in the output file */
    /* dts of the last packet sent to the muxing queue, in AV_TIME_BASE_Q */
    int64_t last_mux_dts;

    // the timebase of the packets sent to the muxer
    AVRational mux_timebase;
    AVRational enc_timebase;

    Encoder *enc;
    AVCodecContext *enc_ctx;

    uint64_t nb_frames_dup;
    uint64_t nb_frames_drop;
    int64_t last_dropped;

    /* video only */
    AVRational frame_rate;
    AVRational max_frame_rate;
    enum VideoSyncMethod vsync_method;
    int is_cfr;
    int force_fps;
    int top_field_first;
#if FFMPEG_ROTATION_METADATA
    int rotate_overridden;
#endif
    int autoscale;
    int bitexact;
    int bits_per_raw_sample;
#if FFMPEG_ROTATION_METADATA
    double rotate_override_value;
#endif

    AVRational frame_aspect_ratio;

    KeyframeForceCtx kf;

    /* audio only */
#if FFMPEG_OPT_MAP_CHANNEL
    int *audio_channels_map;             /* list of the channels id to pick from the source stream */
    int audio_channels_mapped;           /* number of channels in audio_channels_map */
#endif

    char *logfile_prefix;
    FILE *logfile;

    OutputFilter *filter;

    AVDictionary *encoder_opts;
    AVDictionary *sws_dict;
    AVDictionary *swr_opts;
    char *apad;
    OSTFinished finished;        /* no more packets should be written for this stream */
    int unavailable;                     /* true if the steram is unavailable (possibly temporarily) */

    // init_output_stream() has been called for this stream
    // The encoder and the bitstream filters have been initialized and the stream
    // parameters are set in the AVStream.
    int initialized;

    int inputs_done;

    const char *attachment_filename;

    int keep_pix_fmt;

    /* stats */
    // number of packets send to the muxer
    atomic_uint_least64_t packets_written;
    // number of frames/samples sent to the encoder
    uint64_t frames_encoded;
    uint64_t samples_encoded;

    /* packet quality factor */
    int quality;

    int sq_idx_encode;
    int sq_idx_mux;

    EncStats enc_stats_pre;
    EncStats enc_stats_post;

    /*
     * bool on whether this stream should be utilized for splitting
     * subtitles utilizing fix_sub_duration at random access points.
     */
    unsigned int fix_sub_duration_heartbeat;
} OutputStream;

typedef struct OutputFile {
    const AVClass *class;

    int index;

    const AVOutputFormat *format;
    const char           *url;

    OutputStream **streams;
    int         nb_streams;

    SyncQueue *sq_encode;

    int64_t recording_time;  ///< desired length of the resulting file in microseconds == AV_TIME_BASE units
    int64_t start_time;      ///< start time in microseconds == AV_TIME_BASE units

    int shortest;
    int bitexact;
} OutputFile;

// optionally attached as opaque_ref to decoded AVFrames
typedef struct FrameData {
    uint64_t   idx;
    int64_t    pts;
    AVRational tb;

    AVRational frame_rate_filter;

    int        bits_per_raw_sample;
} FrameData;

extern InputFile   **input_files;
extern int        nb_input_files;

extern OutputFile   **output_files;
extern int         nb_output_files;

extern FilterGraph **filtergraphs;
extern int        nb_filtergraphs;

extern char *vstats_filename;
extern char *sdp_filename;

extern float dts_delta_threshold;
extern float dts_error_threshold;

extern enum VideoSyncMethod video_sync_method;
extern float frame_drop_threshold;
extern int do_benchmark;
extern int do_benchmark_all;
extern int do_hex_dump;
extern int do_pkt_dump;
extern int copy_ts;
extern int start_at_zero;
extern int copy_tb;
extern int debug_ts;
extern int exit_on_error;
extern int abort_on_flags;
extern int print_stats;
extern int64_t stats_period;
extern int stdin_interaction;
extern AVIOContext *progress_avio;
extern float max_error_rate;

extern char *filter_nbthreads;
extern int filter_complex_nbthreads;
extern int vstats_version;
extern int auto_conversion_filters;

extern const AVIOInterruptCB int_cb;

extern const OptionDef options[];
extern HWDevice *filter_hw_device;

extern unsigned nb_output_dumped;

extern int ignore_unknown_streams;
extern int copy_unknown_streams;

extern int recast_media;

extern FILE *vstats_file;

#if FFMPEG_OPT_PSNR
extern int do_psnr;
#endif

void term_init(void);
void term_exit(void);

void show_usage(void);

void remove_avoptions(AVDictionary **a, AVDictionary *b);
void assert_avoptions(AVDictionary *m);

void assert_file_overwrite(const char *filename);
char *file_read(const char *filename);
AVDictionary *strip_specifiers(const AVDictionary *dict);
const AVCodec *find_codec_or_die(void *logctx, const char *name,
                                 enum AVMediaType type, int encoder);
int parse_and_set_vsync(const char *arg, int *vsync_var, int file_idx, int st_idx, int is_global);

void check_filter_outputs(void);
int filtergraph_is_simple(const FilterGraph *fg);
int init_simple_filtergraph(InputStream *ist, OutputStream *ost,
                            char *graph_desc);
int init_complex_filtergraph(FilterGraph *fg);

int copy_av_subtitle(AVSubtitle *dst, const AVSubtitle *src);
int subtitle_wrap_frame(AVFrame *frame, AVSubtitle *subtitle, int copy);

/**
 * Get our axiliary frame data attached to the frame, allocating it
 * if needed.
 */
FrameData *frame_data(AVFrame *frame);

int ifilter_send_frame(InputFilter *ifilter, AVFrame *frame, int keep_reference);
int ifilter_send_eof(InputFilter *ifilter, int64_t pts, AVRational tb);
int ifilter_sub2video(InputFilter *ifilter, const AVFrame *frame);
void ifilter_sub2video_heartbeat(InputFilter *ifilter, int64_t pts, AVRational tb);

/**
 * Set up fallback filtering parameters from a decoder context. They will only
 * be used if no frames are ever sent on this input, otherwise the actual
 * parameters are taken from the frame.
 */
int ifilter_parameters_from_dec(InputFilter *ifilter, const AVCodecContext *dec);

void ofilter_bind_ost(OutputFilter *ofilter, OutputStream *ost);

/**
 * Create a new filtergraph in the global filtergraph list.
 *
 * @param graph_desc Graph description; an av_malloc()ed string, filtergraph
 *                   takes ownership of it.
 */
FilterGraph *fg_create(char *graph_desc);

void fg_free(FilterGraph **pfg);

/**
 * Perform a step of transcoding for the specified filter graph.
 *
 * @param[in]  graph     filter graph to consider
 * @param[out] best_ist  input stream where a frame would allow to continue
 * @return  0 for success, <0 for error
 */
int fg_transcode_step(FilterGraph *graph, InputStream **best_ist);

/**
 * Get and encode new output from any of the filtergraphs, without causing
 * activity.
 *
 * @return  0 for success, <0 for severe errors
 */
int reap_filters(int flush);

int ffmpeg_parse_options(int argc, char **argv);

void enc_stats_write(OutputStream *ost, EncStats *es,
                     const AVFrame *frame, const AVPacket *pkt,
                     uint64_t frame_num);

HWDevice *hw_device_get_by_name(const char *name);
HWDevice *hw_device_get_by_type(enum AVHWDeviceType type);
int hw_device_init_from_string(const char *arg, HWDevice **dev);
int hw_device_init_from_type(enum AVHWDeviceType type,
                             const char *device,
                             HWDevice **dev_out);
void hw_device_free_all(void);

/**
 * Get a hardware device to be used with this filtergraph.
 * The returned reference is owned by the callee, the caller
 * must ref it explicitly for long-term use.
 */
AVBufferRef *hw_device_for_filter(void);

int hwaccel_retrieve_data(AVCodecContext *avctx, AVFrame *input);

int dec_open(InputStream *ist);
void dec_free(Decoder **pdec);

/**
 * Submit a packet for decoding
 *
 * When pkt==NULL and no_eof=0, there will be no more input. Flush decoders and
 * mark all downstreams as finished.
 *
 * When pkt==NULL and no_eof=1, the stream was reset (e.g. after a seek). Flush
 * decoders and await further input.
 */
int dec_packet(InputStream *ist, const AVPacket *pkt, int no_eof);

int enc_alloc(Encoder **penc, const AVCodec *codec);
void enc_free(Encoder **penc);

int enc_open(OutputStream *ost, AVFrame *frame);
void enc_subtitle(OutputFile *of, OutputStream *ost, const AVSubtitle *sub);
void enc_frame(OutputStream *ost, AVFrame *frame);
void enc_flush(void);

/*
 * Initialize muxing state for the given stream, should be called
 * after the codec/streamcopy setup has been done.
 *
 * Open the muxer once all the streams have been initialized.
 */
int of_stream_init(OutputFile *of, OutputStream *ost);
int of_write_trailer(OutputFile *of);
int of_open(const OptionsContext *o, const char *filename);
void of_close(OutputFile **pof);

void of_enc_stats_close(void);

void of_output_packet(OutputFile *of, OutputStream *ost, AVPacket *pkt);

/**
 * @param dts predicted packet dts in AV_TIME_BASE_Q
 */
void of_streamcopy(OutputStream *ost, const AVPacket *pkt, int64_t dts);

int64_t of_filesize(OutputFile *of);

int ifile_open(const OptionsContext *o, const char *filename);
void ifile_close(InputFile **f);

/**
 * Get next input packet from the demuxer.
 *
 * @param pkt the packet is written here when this function returns 0
 * @return
 * - 0 when a packet has been read successfully
 * - 1 when stream end was reached, but the stream is looped;
 *     caller should flush decoders and read from this demuxer again
 * - a negative error code on failure
 */
int ifile_get_packet(InputFile *f, AVPacket **pkt);

int ist_output_add(InputStream *ist, OutputStream *ost);
int ist_filter_add(InputStream *ist, InputFilter *ifilter, int is_simple);

/**
 * Find an unused input stream of given type.
 */
InputStream *ist_find_unused(enum AVMediaType type);

/* iterate over all input streams in all input files;
 * pass NULL to start iteration */
InputStream *ist_iter(InputStream *prev);

/* iterate over all output streams in all output files;
 * pass NULL to start iteration */
OutputStream *ost_iter(OutputStream *prev);

void close_output_stream(OutputStream *ost);
int trigger_fix_sub_duration_heartbeat(OutputStream *ost, const AVPacket *pkt);
int fix_sub_duration_heartbeat(InputStream *ist, int64_t signal_pts);
void update_benchmark(const char *fmt, ...);

/**
 * Merge two return codes - return one of the error codes if at least one of
 * them was negative, 0 otherwise.
 * Currently just picks the first one, eventually we might want to do something
 * more sophisticated, like sorting them by priority.
 */
static inline int err_merge(int err0, int err1)
{
    return (err0 < 0) ? err0 : FFMIN(err1, 0);
}

#define SPECIFIER_OPT_FMT_str  "%s"
#define SPECIFIER_OPT_FMT_i    "%i"
#define SPECIFIER_OPT_FMT_i64  "%"PRId64
#define SPECIFIER_OPT_FMT_ui64 "%"PRIu64
#define SPECIFIER_OPT_FMT_f    "%f"
#define SPECIFIER_OPT_FMT_dbl  "%lf"

#define WARN_MULTIPLE_OPT_USAGE(name, type, so, st)\
{\
    char namestr[128] = "";\
    const char *spec = so->specifier && so->specifier[0] ? so->specifier : "";\
    for (int _i = 0; opt_name_##name[_i]; _i++)\
        av_strlcatf(namestr, sizeof(namestr), "-%s%s", opt_name_##name[_i], opt_name_##name[_i+1] ? (opt_name_##name[_i+2] ? ", " : " or ") : "");\
    av_log(NULL, AV_LOG_WARNING, "Multiple %s options specified for stream %d, only the last option '-%s%s%s "SPECIFIER_OPT_FMT_##type"' will be used.\n",\
           namestr, st->index, opt_name_##name[0], spec[0] ? ":" : "", spec, so->u.type);\
}

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st)\
{\
    int _ret, _matches = 0;\
    SpecifierOpt *so;\
    for (int _i = 0; _i < o->nb_ ## name; _i++) {\
        char *spec = o->name[_i].specifier;\
        if ((_ret = check_stream_specifier(fmtctx, st, spec)) > 0) {\
            outvar = o->name[_i].u.type;\
            so = &o->name[_i];\
            _matches++;\
        } else if (_ret < 0)\
            exit_program(1);\
    }\
    if (_matches > 1)\
       WARN_MULTIPLE_OPT_USAGE(name, type, so, st);\
}

#define MATCH_PER_TYPE_OPT(name, type, outvar, fmtctx, mediatype)\
{\
    int i;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if (!strcmp(spec, mediatype))\
            outvar = o->name[i].u.type;\
    }\
}

extern const char * const opt_name_codec_names[];
extern const char * const opt_name_codec_tags[];
extern const char * const opt_name_frame_rates[];
extern const char * const opt_name_top_field_first[];

static inline void pkt_move(void *dst, void *src)
{
    av_packet_move_ref(dst, src);
}

static inline void frame_move(void *dst, void *src)
{
    av_frame_move_ref(dst, src);
}

#endif /* FFTOOLS_FFMPEG_H */
