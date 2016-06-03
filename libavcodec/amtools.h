
#ifndef _AMLTOOLS_H_
#define _AMLTOOLS_H_

bool amlsysfs_write_string(const char *path, const char *value);
bool amlsysfs_write_int(const char *path, int value);

#endif /* _AMLTOOLS_H_*/
