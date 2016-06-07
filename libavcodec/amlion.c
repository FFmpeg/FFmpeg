#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/mman.h>

#include "avcodec.h"
#include "amldec.h"
#include "amltools.h"
#include "amlion.h"

#define ION_IOC_MAGIC 'I'



enum ion_heap_type
{
  ION_HEAP_TYPE_SYSTEM,
  ION_HEAP_TYPE_SYSTEM_CONTIG,
  ION_HEAP_TYPE_CARVEOUT,
  ION_HEAP_TYPE_CHUNK,
  ION_HEAP_TYPE_CUSTOM,
  ION_NUM_HEAPS = 16
};


#define ION_HEAP_SYSTEM_MASK        (1 << ION_HEAP_TYPE_SYSTEM)
#define ION_HEAP_SYSTEM_CONTIG_MASK (1 << ION_HEAP_TYPE_SYSTEM_CONTIG)
#define ION_HEAP_CARVEOUT_MASK      (1 << ION_HEAP_TYPE_CARVEOUT)

typedef int ion_handle;

typedef struct ion_allocation_data
{
  size_t len;
  size_t align;
  unsigned int heap_id_mask;
  unsigned int flags;
  ion_handle handle;
} ion_allocation_data;

typedef struct ion_fd_data
{
  ion_handle handle;
  int fd;
} ion_fd_data;

typedef struct ion_handle_data
{
  ion_handle handle;
} ion_handle_data;

#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, ion_allocation_data)
#define ION_IOC_FREE  _IOWR(ION_IOC_MAGIC, 1, ion_handle_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, ion_fd_data)

#define ALIGN(value, alignment) (((value)+(alignment-1))&~(alignment-1))

int vtop(int vaddr);

int aml_ion_open(AVCodecContext *avctx, AMLIonContext *ionctx)
{
  //AMLDecodeContext *aml_context = (AMLDecodeContext*)avctx->priv_data;
  struct v4l2_format fmt = { 0 };
  struct v4l2_requestbuffers req = { 0 };
  int type;
  int ret;

  memset(ionctx, 0, sizeof(*ionctx));
  
  // open the ion device
  if ((ionctx->ion_fd = open(ION_DEVICE_NAME, O_RDWR)) < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "Failed to open %s\n", ION_DEVICE_NAME);
    return -1;
  }

  av_log(avctx, AV_LOG_DEBUG, "openned %s with fd=%d\n", ION_DEVICE_NAME, ionctx->ion_fd);

  // open the ion video device
  if ((ionctx->video_fd = open(ION_VIDEO_DEVICE_NAME, O_RDWR | O_NONBLOCK)) < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "Failed to open %s\n", ION_VIDEO_DEVICE_NAME);
    return -1;
  }

  av_log(avctx, AV_LOG_DEBUG, "openned %s with fd=%d\n", ION_VIDEO_DEVICE_NAME, ionctx->video_fd);

  // Now setup the format
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix_mp.width = avctx->width;
  fmt.fmt.pix_mp.height = avctx->height;
  fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;

  if (ioctl(ionctx->video_fd, VIDIOC_S_FMT, &fmt))
  {
    av_log(avctx, AV_LOG_ERROR, "ioctl for VIDIOC_S_FMT failed\n");
    return -1;
  }

  // setup the buffers
  req.count = ION_BUFFER_COUNT;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_DMABUF;
  
  if (ioctl(ionctx->video_fd, VIDIOC_REQBUFS, &req))
  {
    av_log(avctx, AV_LOG_ERROR, "ioctl for VIDIOC_REQBUFS failed\n");
    return -1;
  }

  // setup streaming 
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl(ionctx->video_fd, VIDIOC_STREAMON, &type))
  {
    av_log(avctx, AV_LOG_ERROR, "ioctl for VIDIOC_STREAMON failed\n");
    return -1;
  }

  // create the video Buffers
  for (int i=0; i < ION_BUFFER_COUNT; i++)
  {
    memset(&ionctx->buffers[i], 0, sizeof(ionctx->buffers[i]));
    ionctx->buffers[i].index = i;
    ret = aml_ion_create_buffer(avctx, ionctx, &ionctx->buffers[i]);
    if (ret < 0)
    {
      av_log(avctx, AV_LOG_ERROR, "failed to create ion buffer %d\n", i);
      return -1;
    }

    ret = aml_ion_queue_buffer(avctx, ionctx, &ionctx->buffers[i]);
    if (ret < 0)
    {
      return -1;
    }
  }

  // setup vfm : we remove default frame handler and add ion handler
  amlsysfs_write_string(avctx, "/sys/class/vfm/map", "rm default");
  amlsysfs_write_string(avctx, "/sys/class/vfm/map", "add default decoder ionvideo");
  amlsysfs_write_int(avctx, "/sys/class/ionvideo/scaling_rate", 100);

  return 0;
}

int aml_ion_close(AVCodecContext *avctx, AMLIonContext *ionctx)
{
  int type;

  // clode ion device
  if (ionctx->ion_fd)
  {

    // free the buffers
    for (int i=0; i < ION_BUFFER_COUNT; i++)
    {
      aml_ion_free_buffer(avctx, ionctx, &ionctx->buffers[i]);
    }

    // Stop streaming
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(ionctx->video_fd, VIDIOC_STREAMOFF, &type))
    {
      av_log(avctx, AV_LOG_ERROR, "ioctl for VIDIOC_STREAMOFF failed\n");
      return -1;
    }

    close(ionctx->ion_fd);
  }

  // close video device
  if (ionctx->video_fd)
  {
    close(ionctx->video_fd);
  }


  return 0;
}

int vtop(int vaddr)
{
  FILE *pagemap;
  int paddr = 0;
  int offset = (vaddr / sysconf(_SC_PAGESIZE)) * sizeof(uint64_t);
  uint64_t e;

  // https://www.kernel.org/doc/Documentation/vm/pagemap.txt
  if ((pagemap = fopen("/proc/self/pagemap", "r")))
  {
    if (lseek(fileno(pagemap), offset, SEEK_SET) == offset)
    {
      if (fread(&e, sizeof(uint64_t), 1, pagemap))
      {
        if (e & (1ULL << 63))
        { // page present ?
          paddr = e & ((1ULL << 54) - 1); // pfn mask
          paddr = paddr * sysconf(_SC_PAGESIZE);
          // add offset within page
          paddr = paddr | (vaddr & (sysconf(_SC_PAGESIZE) - 1));
        }
      }
    }
    fclose(pagemap);
  }

  return paddr;
}

int aml_ion_create_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, AMLIonBuffer *buffer)
{
  ion_allocation_data ion_alloc;
  ion_fd_data fd_data;
  int ret;
  void *data;

  memset(&ion_alloc, 0, sizeof(ion_alloc));
  memset(&fd_data, 0 , sizeof(fd_data));

  buffer->width = avctx->width;
  buffer->height = avctx->height;
  buffer->stride = ALIGN(buffer->width, 16);
  buffer->size = ALIGN(buffer->width, 16) * (ALIGN(buffer->height, 32) + ALIGN(buffer->stride / 2, 16));

  // allocate the buffer
  ion_alloc.len = buffer->size;
  ion_alloc.heap_id_mask = ION_HEAP_CARVEOUT_MASK;

  ret = ioctl(ionctx->ion_fd, ION_IOC_ALLOC, &ion_alloc);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to allocate ion buffer %d\n", buffer->index);
    return -1;
  }

  buffer->handle = ion_alloc.handle;
  av_log(avctx, AV_LOG_ERROR, "got ion alloc handle %d for buffer %d\n", buffer->handle, buffer->index);

  // share the buffer
  fd_data.handle = buffer->handle;
  ret = ioctl(ionctx->ion_fd, ION_IOC_SHARE, &fd_data);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to retrieve ion buffer handle \n");
    return -1;
  }

  buffer->fd_handle = fd_data.fd;

  av_log(avctx, AV_LOG_ERROR, "got ion alloc fd %d  for buffer %d\n", buffer->fd_handle, buffer->index);

  // now map the fd to a mem pointer
  data = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer->fd_handle, 0);
  if (buffer->data == MAP_FAILED)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to mmap ion buffer handle\n");
    return -1;
  }

  buffer->data = data;
  buffer->phys_addr = vtop(buffer->data);

  av_log(avctx, AV_LOG_ERROR, "got ion buffer pointer 0x%x for buffer %ld, (phy@%x)\n", (unsigned int)buffer->data, buffer->index, buffer->phys_addr);
  return 0;
}


int aml_ion_free_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, AMLIonBuffer *buffer)
{
  int ret;
  ion_handle_data handle_data;

  if (buffer->data)
    munmap(buffer->data, buffer->size);

  if (buffer->fd_handle)
    close(buffer->fd_handle);

  if (buffer->handle)
  {
    memset(&handle_data, 0 , sizeof(handle_data));
    handle_data.handle = buffer->handle;
    ret = ioctl(ionctx->ion_fd, ION_IOC_FREE, &handle_data);
    if (ret < 0)
    {
      av_log(avctx, AV_LOG_ERROR, "failed to free ion buffer handle\n");
      return -1;
    }
  }

  return 0;
}

int aml_ion_queue_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, AMLIonBuffer *buffer)
{
  int ret;
  struct v4l2_buffer vbuf;

  memset(&vbuf, 0, sizeof(vbuf));
  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_DMABUF;
  vbuf.index = buffer->index;
  vbuf.m.fd = buffer->fd_handle;
  vbuf.length = buffer->size;

  av_log(avctx, AV_LOG_DEBUG, "LongChair queuing buffer #%d\n", buffer->index);
  ret = ioctl(ionctx->video_fd, VIDIOC_QBUF, &vbuf);
  if (ret < 0)
  {
    av_log(avctx, AV_LOG_ERROR, "failed to queue ion buffer #%d (size = %d), code=%d\n", buffer->index, buffer->size, ret);
    return -1;
  }

  return buffer->index;
}


int aml_ion_dequeue_buffer(AVCodecContext *avctx,AMLIonContext *ionctx, int *got_buffer)
{
  int ret;
  struct v4l2_buffer vbuf = { 0 };

  *got_buffer = 0;

  vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  vbuf.memory = V4L2_MEMORY_DMABUF;

  ret = ioctl(ionctx->video_fd, VIDIOC_DQBUF, &vbuf);
  if (ret < 0)
  {
    if (errno == EAGAIN)
    {
       av_log(avctx, AV_LOG_DEBUG, "LongChair :dequeuing EAGAIN #%d, pts=%d\n", vbuf.index, vbuf.timestamp.tv_usec);
      return 0;
    }
    else
    {
      av_log(avctx, AV_LOG_ERROR, "failed to dequeue ion (code %d)\n", ret);
      return -1;
    }
  }

  ionctx->buffers[vbuf.index].pts = ((double)vbuf.timestamp.tv_usec / 1000000.0) / av_q2d(avctx->time_base);
  *got_buffer = 1;
  return vbuf.index;
}


int aml_ion_save_buffer(const char *filename, AMLIonBuffer *buffer)
{
  int fd = open(filename, O_CREAT | O_RDWR);

  if (fd)
  {
    write(fd, buffer->data, buffer->size);
    close(fd);

    return 0;
  }

  return -1;
}
