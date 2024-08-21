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

#ifndef AVCODEC_CONTAINER_FIFO_H
#define AVCODEC_CONTAINER_FIFO_H

#include <stddef.h>

/**
 * ContainerFifo is a FIFO for "containers" - dynamically allocated reusable
 * structs (e.g. AVFrame or AVPacket). ContainerFifo uses an internal pool of
 * such containers to avoid allocating and freeing them repeatedly.
 */
typedef struct ContainerFifo ContainerFifo;

/**
 * Allocate a new ContainerFifo for the container type defined by provided
 * callbacks.
 *
 * @param container_alloc allocate a new container instance and return a pointer
 *                        to it, or NULL on failure
 * @param container_reset reset the provided container instance to a clean state
 * @param container_free free the provided container instance
 * @param fifo_write transfer the contents of src to dst, where src is a
 *                   container instance provided to ff_container_fifo_write()
 * @param fifo_read transfer the contents of src to dst in other cases
 *
 * @note fifo_read() and fifo_write() are different parameters in order to allow
 *       fifo_write() implementations that make a new reference in dst, leaving
 *       src untouched (see e.g. ff_container_fifo_alloc_avframe())
 */
ContainerFifo*
ff_container_fifo_alloc(void* (*container_alloc)(void),
                        void  (*container_reset)(void *obj),
                        void  (*container_free) (void *obj),
                        int   (*fifo_write)     (void *dst, void *src),
                        int   (*fifo_read)      (void *dst, void *src));

/**
 * Allocate a ContainerFifo instance for AVFrames.
 * Note that ff_container_fifo_write() will call av_frame_ref() on src, making a
 * new reference in dst and leaving src untouched.
 *
 * @param flags unused currently
 */
ContainerFifo *ff_container_fifo_alloc_avframe(unsigned flags);

/**
 * Free a ContainerFifo and everything in it.
 */
void ff_container_fifo_free(ContainerFifo **pf);

/**
 * Write the contents of obj to the FIFO.
 *
 * The fifo_write() callback previously provided to ff_container_fifo_alloc()
 * will be called with obj as src in order to perform the actual transfer.
 */
int ff_container_fifo_write(ContainerFifo *pf, void *obj);

/**
 * Read the next available object from the FIFO into obj.
 *
 * The fifo_read() callback previously provided to ff_container_fifo_alloc()
 * will be called with obj as dst in order to perform the actual transfer.
 */
int ff_container_fifo_read(ContainerFifo *pf, void *obj);

/**
 * @return number of objects available for reading
 */
size_t ff_container_fifo_can_read(ContainerFifo *pf);

#endif // AVCODEC_CONTAINER_FIFO_H
