#ifndef  _AMLION_H_
#define _AMLION_H_

#define ION_DEVICE_NAME         "/dev/ion"
#define ION_VIDEO_DEVICE_NAME   "/dev/video13"
#define ION_BUFFER_COUNT        (2)

typedef struct {
  int width;
  int height;
  int stride;
  int size;

  int64_t pts;

  int handle;    // handle to the allocated buffer in ion memory
  int fd_handle; // file descriptor to the ion memory buffer
  void *data;    // memory mmaped pointer to the ion buffer
  int phys_addr;

  int queued;
  int index;
} AMLIonBuffer;

typedef struct {
  int ion_fd;
  int video_fd;

  AMLIonBuffer buffers[ION_BUFFER_COUNT];

} AMLIonContext;



int aml_ion_open(AVCodecContext *avctx, AMLIonContext *ionctx);
int aml_ion_close(AVCodecContext *avctx, AMLIonContext *ionctx);
int aml_ion_create_buffer(AVCodecContext *avctx, AMLIonContext *ionctx, AMLIonBuffer *buffer);
int aml_ion_free_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, AMLIonBuffer *buffer);
int aml_ion_queue_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, AMLIonBuffer *buffer);
int aml_ion_dequeue_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, int *got_buffer);
int aml_ion_save_buffer(const char *filename, AMLIonBuffer *buffer);

#endif /* _AMLION_H_ */
