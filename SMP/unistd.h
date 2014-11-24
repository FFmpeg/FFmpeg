
#ifndef _UNISTD_H_
#define _UNISTD_H_

#ifndef _MSC_VER
#error "Use this header only with Microsoft Visual C++ compilers!"
#endif

#ifndef __STRICT_ANSI__

#include <stdlib.h>
//#include <io.h>
#include <process.h>
#include <direct.h>
#include <fcntl.h>

#define __UNISTD_GETOPT__
//#include <getopt.h>
#undef __UNISTD_GETOPT__

#define R_OK    4       /* Test for read permission.  */
#define W_OK    2       /* Test for write permission.  */
//#define   X_OK    1       /* execute permission - unsupported in windows*/
#define F_OK    0       /* Test for existence.  */

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#define srandom srand
#define random rand

#define inline __inline
typedef int mode_t;
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;

#endif

#endif