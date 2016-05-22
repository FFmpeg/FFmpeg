/*
* pthread_cancel compatibility implementation for Android
* Copyright (c) 2015 Paul Idstein
*
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
#ifndef _PTHREAD_CANCEL_H_
#define _PTHREAD_CANCEL_H_
/* Alternative cancel implementation that is used on non POSIX compliant
platforms such as Android.
We will send a user-defined signal to a thread, then check for
thread_canceled to trigger thread teardown.

Attention: It's the user's duty to implement a proper
clean-up handler! */

#include <pthread.h>

/* Signals are on a per-process base.
SIGUSR1 is already in use by video grab interface*/
#define SIG_CANCEL_SIGNAL SIGUSR2
#define PTHREAD_CANCEL_ENABLE 1
#define PTHREAD_CANCEL_DISABLE 0
static __inline__ int pthread_setcancelstate(int state, int *oldstate) {
  sigset_t   new, old;
  int ret;
  sigemptyset(&new);
  sigaddset (&new, SIG_CANCEL_SIGNAL);
  /* We block CANCEL signal to see it as pending
     or unblock to let it be discarded */
  ret = pthread_sigmask(state == PTHREAD_CANCEL_ENABLE ? SIG_BLOCK : SIG_UNBLOCK, &new , &old);
  if(oldstate != NULL)
  {
    *oldstate = sigismember(&old,SIG_CANCEL_SIGNAL) == 0 ? PTHREAD_CANCEL_DISABLE : PTHREAD_CANCEL_ENABLE;
  }
  return ret;
}

static __inline__ int pthread_cancel(pthread_t thread) {
  /* Send cancel signal that is ignored (PTHREAD_CANCEL_DISABLE)
  or enqueued as blocked and pending (thread canceled)*/
  return pthread_kill(thread, SIG_CANCEL_SIGNAL);
}

/* Need to be checked, as we do not automatically
  perform any clean-up! */
static __inline__ int thread_canceled(void) {
  sigset_t waiting_mask;
  sigpending(&waiting_mask);
  return sigismember(&waiting_mask,SIG_CANCEL_SIGNAL) == 0;
}
#endif
