#include "labeling.h"

#define MAX_LABEL_COUNT 1024

typedef struct {
    int label;
    double start_time;
    double duration;
} TimePeriod;

int key_down = 0;
int label_count = 0;
TimePeriod labels[MAX_LABEL_COUNT];
double current_pts = 0;

void timeline_keydown(int label) {
    if (!key_down) {
        fprintf(stderr, "Starting new label at PTS %lf with label %d\n", current_pts, label);
        labels[label_count].start_time = current_pts;
        labels[label_count].label = label;
    }
    key_down = 1;
}

void timeline_keyup() {
    if (key_down && label_count < MAX_LABEL_COUNT - 2) {
        fprintf(stderr, "Writing new label at PTS %lf\n", current_pts);
        labels[label_count].duration = current_pts - labels[label_count].start_time;
        if (labels[label_count].duration < 0) {
            labels[label_count].start_time = current_pts;
            labels[label_count].duration *= -1;
        }
        label_count++;
    }
    key_down = 0;
}

static void index_to_yuv(int index, uint8_t *y, uint8_t *u, uint8_t *v) {
    *y = 128 + (index * 79) % 128;
    *u = (index * 71) % 256;
    *v = (255 - index * 193) % 256;
}

void timeline_update(AVFrame *frame, double pts, double duration) {
    current_pts = pts;
    if (key_down) {
        labels[label_count].duration = current_pts - labels[label_count].start_time;
    }
    // Draw a progress bar at the bottom of the video
    int progress_bar_height = frame->width/2 > 32 ? 32 : frame->width/2;
    double progress = pts / duration;

    int *progress_bar_color = calloc(frame->width, sizeof(int));
    for (int i = 0; i < label_count + key_down; i++) {
        int left = frame->width * (labels[i].start_time / duration);
        int right = left + frame->width * (labels[i].duration / duration);
        for (int x = fmax(0, left); x < fmin(right, frame->width); x++) {
            progress_bar_color[x] = labels[i].label;
        }
    }

    for (int x = 0; x < frame->width; x++) {
        for (int y = frame->height - progress_bar_height; y < frame->height; y++) {
            int color = x < progress * frame->width ? 128 : 32;
            frame->data[0][x + y * frame->linesize[0]] = color;
        }
    }

    for (int x = 0; x < frame->width; x++) {
        for (int y = frame->height - progress_bar_height/2; y < frame->height; y++) {
            int ux = x/2, uy = y/2;
            if (progress_bar_color[x] > 0) {
                index_to_yuv(progress_bar_color[x],
                    &frame->data[0][x + y * frame->linesize[0]],
                    &frame->data[1][ux + uy * frame->linesize[1]], 
                    &frame->data[2][ux + uy * frame->linesize[2]]);
            }
        }
    }
    free(progress_bar_color);
}

void timeline_write_output(FILE *out) {
    fprintf(out, "#Label,Start,Duration\n");
    for (int i = 0; i < label_count; i++) {
        fprintf(out, "%d,%.2lf,%.2lf\n", labels[i].label, labels[i].start_time, labels[i].duration);
    }
}
