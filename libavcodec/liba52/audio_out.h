/*
 * audio_out.h
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

typedef struct ao_instance_s ao_instance_t;

struct ao_instance_s {
    int (* setup) (ao_instance_t * instance, int sample_rate, int * flags,
		   sample_t * level, sample_t * bias);
    int (* play) (ao_instance_t * instance, int flags, sample_t * samples);
    void (* close) (ao_instance_t * instance);
};

typedef ao_instance_t * ao_open_t (void);

typedef struct ao_driver_s {
    char * name;
    ao_open_t * open;
} ao_driver_t;

/* return NULL terminated array of all drivers */
ao_driver_t * ao_drivers (void);

static inline ao_instance_t * ao_open (ao_open_t * open)
{
    return open ();
}

static inline int ao_setup (ao_instance_t * instance, int sample_rate,
			    int * flags, sample_t * level, sample_t * bias)
{
    return instance->setup (instance, sample_rate, flags, level, bias);
}

static inline int ao_play (ao_instance_t * instance, int flags,
			   sample_t * samples)
{
    return instance->play (instance, flags, samples);
}

static inline void ao_close (ao_instance_t * instance)
{
    if (instance->close)
	instance->close (instance);
}
