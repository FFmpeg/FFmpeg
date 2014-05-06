
#ifndef _UNISTD_H_
#define _UNISTD_H_

#ifndef _MSC_VER
#error "Use this header only with Microsoft Visual C++ compilers!"
#endif

#ifndef __STRICT_ANSI__

#include <stdlib.h>
//#include <io.h>
#include <process.h>

#define R_OK    4       /* Test for read permission.  */
#define W_OK    2       /* Test for write permission.  */
//#define   X_OK    1       /* execute permission - unsupported in windows*/
#define F_OK    0       /* Test for existence.  */

#define inline __inline
typedef unsigned short mode_t;

#endif

#endif