#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "libavcodec/amltools.h"
#include "libavutil/log.h"

int amlsysfs_write_string(AVCodecContext *avctx, const char *path, const char *value)
{
    int ret = 0;
    int fd = open(path, O_RDWR, 0644);
    if (fd >= 0)
    {
      ret = write(fd, value, strlen(value));
    	if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "failed to set %s to %s\n", path, value);
      close(fd);
      return 0;
    }
  return -1;
}

int amlsysfs_write_int(AVCodecContext *avctx, const char *path, int value)
{
    int ret = 0;
    char cmd[64];
    int fd = open(path, O_RDWR, 0644);
    if (fd >= 0)
    {
    	snprintf(cmd, sizeof(cmd), "%d", value);

      ret = write(fd, cmd, strlen(cmd));
    	if (ret < 0)
        av_log(avctx, AV_LOG_ERROR, "failed to set %s to %d\n", path, value);
      close(fd);
      return 0;
    }
  return -1;
}

