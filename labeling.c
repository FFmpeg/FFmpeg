#include "labeling.h"

typedef struct {
    int label;
    double start_time;
    double duration;
} TimePeriod;

TimePeriod positive_labels[4] = {
    {1, 1.0, 4.0},
    {2, 8.0, 2.0},
    {3, 11.0, 3.0},
    {2, 16.0, 10.0}
};

static void index_to_yuv(int index, uint8_t *y, uint8_t *u, uint8_t *v) {
    if (index < 0) {
        *y = 64;
        *u = *v = 128;
    } else if (index == 0) {
        *y = 192;
        *u = *v = 128;
    } else {
        *y = (128 + index * 79) % 256;
        *u = (index * 71) % 256;
        *v = (255 - index * 193) % 256;
    }
}


void draw_timeline(AVFrame *frame, double pts, double duration) {
    // Draw a progress bar at the bottom of the video
    int progress_bar_height = frame->width/2 > 32 ? 32 : frame->width/2;
    double progress = pts / duration;

    int *progress_bar_color = calloc(frame->width, sizeof(int));
    for (int x = 0; x < progress * frame->width; x++) {
        progress_bar_color[x] = -1;
    }
    for (int x = progress * frame->width; x < frame->width; x++) {
        progress_bar_color[x] = 0;
    }
    for (int i = 0; i < 4; i++) {
        int left = frame->width * (positive_labels[i].start_time / duration);
        int right = left + frame->width * (positive_labels[i].duration / duration);
        for (int x = fmax(0, left); x < fmin(right, frame->width); x++) {
            progress_bar_color[x] = positive_labels[i].label;
        }
    }

    for (int x = 0; x < frame->width; x++) {
        for (int y = frame->height - progress_bar_height; y < frame->height; y++) {
            int ux = x/2, uy = y/2;
            index_to_yuv(progress_bar_color[x],
                &frame->data[0][x + y * frame->linesize[0]],
                &frame->data[1][ux + uy * frame->linesize[1]], 
                &frame->data[2][ux + uy * frame->linesize[2]]);
        }
    }
    free(progress_bar_color);
}
