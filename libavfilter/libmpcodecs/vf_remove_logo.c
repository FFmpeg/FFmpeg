/*
 * This filter loads a .pgm mask file showing where a logo is and uses
 * a blur transform to remove the logo.
 *
 * Copyright (C) 2005 Robert Edele <yartrebo@earthlink.net>
 *
 * This file is part of MPlayer.
 *
 * MPlayer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * MPlayer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with MPlayer; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * \file
 *
 * \brief Advanced blur-based logo removing filter.

 *     Hello and welcome. This code implements a filter to remove annoying TV
 * logos and other annoying images placed onto a video stream. It works by filling
 * in the pixels that comprise the logo with neighboring pixels. The transform is
 * very loosely based on a gaussian blur, but it is different enough to merit its
 * own paragraph later on. It is a major improvement on the old delogo filter as
 * it both uses a better blurring algorithm and uses a bitmap to use an arbitrary
 * and generally much tighter fitting shape than a rectangle.
 *
 *     The filter requires 1 argument and has no optional arguments. It requires
 * a filter bitmap, which must be in PGM or PPM format. A sample invocation would
 * be -vf remove_logo=/home/username/logo_bitmaps/xyz.pgm.  Pixels with a value of
 * zero are not part of the logo, and non-zero pixels are part of the logo. If you
 * use white (255) for the logo and black (0) for the rest, you will be safe. For
 * making the filter bitmap, I recommend taking a screen capture of a black frame
 * with the logo visible, and then using The GIMP's threshold filter followed by
 * the erode filter once or twice. If needed, little splotches can be fixed
 * manually. Remember that if logo pixels are not covered, the filter quality will
 * be much reduced. Marking too many pixels as part of the logo doesn't hurt as
 * much, but it will increase the amount of blurring needed to cover over the
 * image and will destroy more information than necessary. Additionally, this blur
 * algorithm is O(n) = n^4, where n is the width and height of a hypothetical
 * square logo, so extra pixels will slow things down on a large lo
 *
 *     The logo removal algorithm has two key points. The first is that it
 * distinguishes between pixels in the logo and those not in the logo by using the
 * passed-in bitmap. Pixels not in the logo are copied over directly without being
 * modified and they also serve as source pixels for the logo fill-in. Pixels
 * inside the logo have the mask applied.
 *
 *     At init-time the bitmap is reprocessed internally, and the distance to the
 * nearest edge of the logo (Manhattan distance), along with a little extra to
 * remove rough edges, is stored in each pixel. This is done using an in-place
 * erosion algorithm, and incrementing each pixel that survives any given erosion.
 * Once every pixel is eroded, the maximum value is recorded, and a set of masks
 * from size 0 to this size are generaged. The masks are circular binary masks,
 * where each pixel within a radius N (where N is the size of the mask) is a 1,
 * and all other pixels are a 0. Although a gaussian mask would be more
 * mathematically accurate, a binary mask works better in practice because we
 * generally do not use the central pixels in the mask (because they are in the
 * logo region), and thus a gaussian mask will cause too little blur and thus a
 * very unstable image.
 *
 *     The mask is applied in a special way. Namely, only pixels in the mask that
 * line up to pixels outside the logo are used. The dynamic mask size means that
 * the mask is just big enough so that the edges touch pixels outside the logo, so
 * the blurring is kept to a minimum and at least the first boundary condition is
 * met (that the image function itself is continuous), even if the second boundary
 * condition (that the derivative of the image function is continuous) is not met.
 * A masking algorithm that does preserve the second boundary coundition
 * (perhaps something based on a highly-modified bi-cubic algorithm) should offer
 * even better results on paper, but the noise in a typical TV signal should make
 * anything based on derivatives hopelessly noisy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "config.h"
#include "mp_msg.h"
#include "libvo/fastmemcpy.h"

#include "img_format.h"
#include "mp_image.h"
#include "vf.h"

//===========================================================================//

/** \brief Returns the larger of the two arguments. **/
#define max(x,y) ((x)>(y)?(x):(y))
/** \brief Returns the smaller of the two arguments. **/
#define min(x,y) ((x)>(y)?(y):(x))

/**
 * \brief Test if a pixel is part of the logo.
 */
#define test_filter(image, x, y) ((unsigned char) (image->pixel[((y) * image->width) + (x)]))

/**
 * \brief Chooses a slightly larger mask size to improve performance.
 *
 * This function maps the absolute minimum mask size needed to the mask size we'll
 * actually use. f(x) = x (the smallest that will work) will produce the sharpest
 * results, but will be quite jittery. f(x) = 1.25x (what I'm using) is a good
 * tradeoff in my opinion. This will calculate only at init-time, so you can put a
 * long expression here without effecting performance.
 */
#define apply_mask_fudge_factor(x) (((x) >> 2) + x)

/**
 * \brief Simple implementation of the PGM image format.
 *
 * This struct holds a bare-bones image loaded from a PGM or PPM file. Once
 * loaded and pre-processed, each pixel in this struct will contain how far from
 * the edge of the logo each pixel is, using the manhattan distance (|dx| + |dy|).
 *
 * pixels in char * pixel can be addressed using (y * width) + height.
 */
typedef struct
{
  unsigned int width;
  unsigned int height;

  unsigned char * pixel;

} pgm_structure;

/**
 * \brief Stores persistant variables.
 *
 * Variables stored here are kept from frame to frame, and separate instances of
 * the filter will get their own separate copies.
 */
struct vf_priv_s
{
  unsigned int fmt; /* Not exactly sure of the use for this. It came with the example filter I used as a basis for this, and it looks like a lot of stuff will break if I remove it. */
  int max_mask_size; /* The largest possible mask size that will be needed with the given filter and corresponding half_size_filter. The half_size_filter can have a larger requirment in some rare (but not degenerate) cases. */
  int * * * mask; /* Stores our collection of masks. The first * is for an array of masks, the second for the y axis, and the third for the x axis. */
  pgm_structure * filter; /* Stores the full-size filter image. This is used to tell what pixels are in the logo or not in the luma plane. */
  pgm_structure * half_size_filter; /* Stores a 50% width and 50% height filter image. This is used to tell what pixels are in the logo or not in the chroma planes. */
  /* These 8 variables store the bounding rectangles that the logo resides in. */
  int bounding_rectangle_posx1;
  int bounding_rectangle_posy1;
  int bounding_rectangle_posx2;
  int bounding_rectangle_posy2;
  int bounding_rectangle_half_size_posx1;
  int bounding_rectangle_half_size_posy1;
  int bounding_rectangle_half_size_posx2;
  int bounding_rectangle_half_size_posy2;
} vf_priv_s;

/**
 * \brief Mallocs memory and checks to make sure it succeeded.
 *
 * \param size How many bytes to allocate.
 *
 * \return A pointer to the freshly allocated memory block, or NULL on failutre.
 *
 * Mallocs memory, and checks to make sure it was successfully allocated. Because
 * of how MPlayer works, it cannot safely halt execution, but at least the user
 * will get an error message before the segfault happens.
 */
static void * safe_malloc(int size)
{
  void * answer = malloc(size);
  if (answer == NULL)
    mp_msg(MSGT_VFILTER, MSGL_ERR, "Unable to allocate memory in vf_remove_logo.c\n");

  return answer;
}

/**
 * \brief Calculates the smallest rectangle that will encompass the logo region.
 *
 * \param filter This image contains the logo around which the rectangle will
 *        will be fitted.
 *
 * The bounding rectangle is calculated by testing successive lines (from the four
 * sides of the rectangle) until no more can be removed without removing logo
 * pixels. The results are returned by reference to posx1, posy1, posx2, and
 * posy2.
 */
static void calculate_bounding_rectangle(int * posx1, int * posy1, int * posx2, int * posy2, pgm_structure * filter)
{
  int x; /* Temporary variables to run  */
  int y; /* through each row or column. */
  int start_x;
  int start_y;
  int end_x = filter->width - 1;
  int end_y = filter->height - 1;
  int did_we_find_a_logo_pixel = 0;

  /* Let's find the top bound first. */
  for (start_x = 0; start_x < filter->width && !did_we_find_a_logo_pixel; start_x++)
  {
    for (y = 0; y < filter->height; y++)
    {
      did_we_find_a_logo_pixel |= test_filter(filter, start_x, y);
    }
  }
  start_x--;

  /* Now the bottom bound. */
  did_we_find_a_logo_pixel = 0;
  for (end_x = filter->width - 1; end_x > start_x && !did_we_find_a_logo_pixel; end_x--)
  {
    for (y = 0; y < filter->height; y++)
    {
      did_we_find_a_logo_pixel |= test_filter(filter, end_x, y);
    }
  }
  end_x++;

  /* Left bound. */
  did_we_find_a_logo_pixel = 0;
  for (start_y = 0; start_y < filter->height && !did_we_find_a_logo_pixel; start_y++)
  {
    for (x = 0; x < filter->width; x++)
    {
      did_we_find_a_logo_pixel |= test_filter(filter, x, start_y);
    }
  }
  start_y--;

  /* Right bound. */
  did_we_find_a_logo_pixel = 0;
  for (end_y = filter->height - 1; end_y > start_y && !did_we_find_a_logo_pixel; end_y--)
  {
    for (x = 0; x < filter->width; x++)
    {
      did_we_find_a_logo_pixel |= test_filter(filter, x, end_y);
    }
  }
  end_y++;

  *posx1 = start_x;
  *posy1 = start_y;
  *posx2 = end_x;
  *posy2 = end_y;

  return;
}

/**
 * \brief Free mask memory.
 *
 * \param vf Data structure which stores our persistant data, and is to be freed.
 *
 * We call this function when our filter is done. It will free the memory
 * allocated to the masks and leave the variables in a safe state.
 */
static void destroy_masks(vf_instance_t * vf)
{
  int a, b;

  /* Load values from the vf->priv struct for faster dereferencing. */
  int * * * mask = vf->priv->mask;
  int max_mask_size = vf->priv->max_mask_size;

  if (mask == NULL)
    return; /* Nothing allocated, so return before we segfault. */

  /* Free all allocated memory. */
  for (a = 0; a <= max_mask_size; a++) /* Loop through each mask. */
  {
    for (b = -a; b <= a; b++) /* Loop through each scanline in a mask. */
    {
      free(mask[a][b + a]); /* Free a scanline. */
    }
    free(mask[a]); /* Free a mask. */
  }
  free(mask); /* Free the array of pointers pointing to the masks. */

  /* Set the pointer to NULL, so that any duplicate calls to this function will not cause a crash. */
  vf->priv->mask = NULL;

  return;
}

/**
 * \brief Set up our array of masks.
 *
 * \param vf Where our filter stores persistance data, like these masks.
 *
 * This creates an array of progressively larger masks and calculates their
 * values. The values will not change during program execution once this function
 * is done.
 */
static void initialize_masks(vf_instance_t * vf)
{
  int a, b, c;

  /* Load values from the vf->priv struct for faster dereferencing. */
  int * * * mask = vf->priv->mask;
  int max_mask_size = vf->priv->max_mask_size; /* This tells us how many masks we'll need to generate. */

  /* Create a circular mask for each size up to max_mask_size. When the filter is applied, the mask size is
     determined on a pixel by pixel basis, with pixels nearer the edge of the logo getting smaller mask sizes. */
  mask = (int * * *) safe_malloc(sizeof(int * *) * (max_mask_size + 1));
  for (a = 0; a <= max_mask_size; a++)
  {
    mask[a] = (int * *) safe_malloc(sizeof(int *) * ((a * 2) + 1));
    for (b = -a; b <= a; b++)
    {
      mask[a][b + a] = (int *) safe_malloc(sizeof(int) * ((a * 2) + 1));
      for (c = -a; c <= a; c++)
      {
        if ((b * b) + (c * c) <= (a * a)) /* Circular 0/1 mask. */
          mask[a][b + a][c + a] = 1;
        else
          mask[a][b + a][c + a] = 0;
      }
    }
  }

  /* Store values back to vf->priv so they aren't lost after the function returns. */
  vf->priv->mask = mask;

  return;
}

/**
 * \brief Pre-processes an image to give distance information.
 *
 * \param vf Data structure that holds persistant information. All it is used for
             in this function is to store the calculated max_mask_size variable.
 * \param mask This image will be converted from a greyscale image into a
 *             distance image.
 *
 * This function takes a greyscale image (pgm_structure * mask) and converts it
 * in place into a distance image. A distance image is zero for pixels ourside of
 * the logo and is the manhattan distance (|dx| + |dy|) for pixels inside of the
 * logo. This will overestimate the distance, but that is safe, and is far easier
 * to implement than a proper pythagorean distance since I'm using a modified
 * erosion algorithm to compute the distances.
 */
static void convert_mask_to_strength_mask(vf_instance_t * vf, pgm_structure * mask)
{
  int x, y; /* Used by our for loops to go through every single pixel in the picture one at a time. */
  int has_anything_changed = 1; /* Used by the main while() loop to know if anything changed on the last erosion. */
  int current_pass = 0; /* How many times we've gone through the loop. Used in the in-place erosion algorithm
                           and to get us max_mask_size later on. */
  int max_mask_size; /* This will record how large a mask the pixel that is the furthest from the edge of the logo
                           (and thus the neediest) is. */
  char * current_pixel = mask->pixel; /* This stores the actual pixel data. */

  /* First pass, set all non-zero values to 1. After this loop finishes, the data should be considered numeric
     data for the filter, not color data. */
  for (x = 0; x < mask->height * mask->width; x++, current_pixel++)
    if(*current_pixel) *current_pixel = 1;

  /* Second pass and future passes. For each pass, if a pixel is itself the same value as the current pass,
     and its four neighbors are too, then it is incremented. If no pixels are incremented by the end of the pass,
     then we go again. Edge pixels are counted as always excluded (this should be true anyway for any sane mask,
     but if it isn't this will ensure that we eventually exit). */
  while (has_anything_changed)
  {
    current_pass++;
    current_pixel = mask->pixel;

    has_anything_changed = 0; /* If this doesn't get set by the end of this pass, then we're done. */

    for (y = 1; y < mask->height - 1; y++)
    {
      for (x = 1; x < mask->width - 1; x++)
      {
        /* Apply the in-place erosion transform. It is based on the following two premises: 1 - Any pixel that fails 1 erosion
           will fail all future erosions. 2 - Only pixels having survived all erosions up to the present will be >= to
           current_pass. It doesn't matter if it survived the current pass, failed it, or hasn't been tested yet. */
        if (*current_pixel >= current_pass && /* By using >= instead of ==, we allow the algorithm to work in place. */
            *(current_pixel + 1) >= current_pass &&
            *(current_pixel - 1) >= current_pass &&
            *(current_pixel + mask->width) >= current_pass &&
            *(current_pixel - mask->width) >= current_pass)
         {
           (*current_pixel)++; /* Increment the value since it still has not been eroded, as evidenced by the if statement
                                  that just evaluated to true. */
           has_anything_changed = 1;
         }
        current_pixel++;
      }
    }
  }

  /* Apply the fudge factor, which will increase the size of the mask a little to reduce jitter at the cost of more blur. */
  for (y = 1; y < mask->height - 1; y++)
  {
   for (x = 1; x < mask->width - 1; x++)
    {
      mask->pixel[(y * mask->width) + x] = apply_mask_fudge_factor(mask->pixel[(y * mask->width) + x]);
    }
  }

  max_mask_size = current_pass + 1; /* As a side-effect, we now know the maximum mask size, which we'll use to generate our masks. */
  max_mask_size = apply_mask_fudge_factor(max_mask_size); /* Apply the fudge factor to this number too, since we must
                                                             ensure that enough masks are generated. */
  vf->priv->max_mask_size = max_mask_size; /* Commit the newly calculated max_mask_size to the vf->priv struct. */

  return;
}

/**
 * \brief Our blurring function.
 *
 * \param vf Stores persistant data. In this function we are interested in the
 *           array of masks.
 * \param value_out The properly blurred and delogoed pixel is outputted here.
 * \param logo_mask Tells us which pixels are in the logo and which aren't.
 * \param image The image that is having its logo removed.
 * \param x x-coordinate of the pixel to blur.
 * \param y y-coordinate of the pixel to blur.
 * \param plane 0 = luma, 1 = blue chroma, 2 = red chroma (YUV).
 *
 * This function is the core of the filter. It takes a pixel that is inside the
 * logo and blurs it. It does so by finding the average of all the pixels within
 * the mask and outside of the logo.
 */
static void get_blur(const vf_instance_t * const vf, unsigned int * const value_out, const pgm_structure * const logo_mask,
              const mp_image_t * const image, const int x, const int y, const int plane)
{
  int mask_size; /* Mask size tells how large a circle to use. The radius is about (slightly larger than) mask size. */
  /* Get values from vf->priv for faster dereferencing. */
  int * * * mask = vf->priv->mask;

  int start_posx, start_posy, end_posx, end_posy;
  int i, j;
  unsigned int accumulator = 0, divisor = 0;
  const unsigned char * mask_read_position; /* What pixel we are reading out of the circular blur mask. */
  const unsigned char * logo_mask_read_position; /* What pixel we are reading out of the filter image. */

  /* Prepare our bounding rectangle and clip it if need be. */
  mask_size = test_filter(logo_mask, x, y);
  start_posx = max(0, x - mask_size);
  start_posy = max(0, y - mask_size);
  end_posx = min(image->width - 1, x + mask_size);
  end_posy = min(image->height - 1, y + mask_size);

  mask_read_position = image->planes[plane] + (image->stride[plane] * start_posy) + start_posx;
  logo_mask_read_position = logo_mask->pixel + (start_posy * logo_mask->width) + start_posx;

  for (j = start_posy; j <= end_posy; j++)
  {
    for (i = start_posx; i <= end_posx; i++)
    {
      if (!(*logo_mask_read_position) && mask[mask_size][i - start_posx][j - start_posy])
      { /* Check to see if this pixel is in the logo or not. Only use the pixel if it is not. */
        accumulator += *mask_read_position;
        divisor++;
      }

      mask_read_position++;
      logo_mask_read_position++;
    }

    mask_read_position += (image->stride[plane] - ((end_posx + 1) - start_posx));
    logo_mask_read_position += (logo_mask->width - ((end_posx + 1) - start_posx));
  }

  if (divisor == 0) /* This means that not a single pixel is outside of the logo, so we have no data. */
  { /* We should put some eye catching value here, to indicate the flaw to the user. */
    *value_out = 255;
  }
  else /* Else we need to normalise the data using the divisor. */
  {
    *value_out = (accumulator + (divisor / 2)) / divisor; /* Divide, taking into account average rounding error. */
  }

  return;
}

/**
 * \brief Free a pgm_structure. Undoes load_pgm(...).
 */
static void destroy_pgm(pgm_structure * to_be_destroyed)
{
  if (to_be_destroyed == NULL)
    return; /* Don't do anything if a NULL pointer was passed it. */

  /* Internally allocated memory. */
  if (to_be_destroyed->pixel != NULL)
  {
    free(to_be_destroyed->pixel);
    to_be_destroyed->pixel = NULL;
  }

  /* Free the actual struct instance. This is done here and not by the calling function. */
  free(to_be_destroyed);
}

/** \brief Helper function for load_pgm(...) to skip whitespace. */
static void load_pgm_skip(FILE *f) {
  int c, comment = 0;
  do {
    c = fgetc(f);
    if (c == '#')
      comment = 1;
    if (c == '\n')
      comment = 0;
  } while (c != EOF && (isspace(c) || comment));
  ungetc(c, f);
}

#define REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE(message) {mp_msg(MSGT_VFILTER, MSGL_ERR, message); return NULL;}

/**
 * \brief Loads a raw pgm or ppm file into a newly created pgm_structure object.
 *
 * \param file_name The name of the file to be loaded. So long as the file is a
 *                  valid pgm or ppm file, it will load correctly, even if the
 *                  extension is missing or invalid.
 *
 * \return A pointer to the newly created pgm_structure object. Don't forget to
 *         call destroy_pgm(...) when you're done with this. If an error occurs,
 *         NULL is returned.
 *
 * Can load either raw pgm (P5) or raw ppm (P6) image files as a binary image.
 * While a pgm file will be loaded normally (greyscale), the only thing that is
 * guaranteed with ppm is that all zero (R = 0, G = 0, B = 0) pixels will remain
 * zero, and non-zero pixels will remain non-zero.
 */
static pgm_structure * load_pgm(const char * file_name)
{
  int maximum_greyscale_value;
  FILE * input;
  int pnm_number;
  pgm_structure * new_pgm = (pgm_structure *) safe_malloc (sizeof(pgm_structure));
  char * write_position;
  char * end_position;
  int image_size; /* width * height */

  if((input = fopen(file_name, "rb")) == NULL) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: Unable to open file. File not found or insufficient permissions.\n");

  /* Parse the PGM header. */
  if (fgetc(input) != 'P') REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: File is not a valid PGM or PPM file.\n");
  pnm_number = fgetc(input) - '0';
  if (pnm_number != 5 && pnm_number != 6) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: Invalid PNM file. Only raw PGM (Portable Gray Map) and raw PPM (Portable Pixel Map) subtypes are allowed.\n");
  load_pgm_skip(input);
  if (fscanf(input, "%i", &(new_pgm->width)) != 1) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: Invalid PGM/PPM header.\n");
  load_pgm_skip(input);
  if (fscanf(input, "%i", &(new_pgm->height)) != 1) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: Invalid PGM/PPM header.\n");
  load_pgm_skip(input);
  if (fscanf(input, "%i", &maximum_greyscale_value) != 1) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove-logo: Invalid PGM/PPM header.\n");
  if (maximum_greyscale_value >= 256) REMOVE_LOGO_LOAD_PGM_ERROR_MESSAGE("[vf]remove_logo: Only 1 byte per pixel (pgm) or 1 byte per color value (ppm) are supported.\n");
  load_pgm_skip(input);

  new_pgm->pixel = (unsigned char *) safe_malloc (sizeof(unsigned char) * new_pgm->width * new_pgm->height);

  /* Load the pixels. */
  /* Note: I am aware that fgetc(input) isn't the fastest way of doing things, but it is quite compact and the code only runs once when the filter is initialized.*/
  image_size = new_pgm->width * new_pgm->height;
  end_position = new_pgm->pixel + image_size;
  for (write_position = new_pgm->pixel; write_position < end_position; write_position++)
  {
    *write_position = fgetc(input);
    if (pnm_number == 6) /* This tests to see if the file is a PPM file. */
    { /* If it is, then consider the pixel set if any of the three color channels are set. Since we just care about == 0 or != 0, a bitwise or will do the trick. */
      *write_position |= fgetc(input);
      *write_position |= fgetc(input);
    }
  }

  return new_pgm;
}

/**
 * \brief Generates a scaled down image with half width, height, and intensity.
 *
 * \param vf Our struct for persistant data. In this case, it is used to update
 *           mask_max_size with the larger of the old or new value.
 * \param input_image The image from which the new half-sized one will be based.
 *
 * \return The newly allocated and shrunken image.
 *
 * This function not only scales down an image, but halves the value in each pixel
 * too. The purpose of this is to produce a chroma filter image out of a luma
 * filter image. The pixel values store the distance to the edge of the logo and
 * halving the dimensions halves the distance. This function rounds up, because
 * a downwards rounding error could cause the filter to fail, but an upwards
 * rounding error will only cause a minor amount of excess blur in the chroma
 * planes.
 */
static pgm_structure * generate_half_size_image(vf_instance_t * vf, pgm_structure * input_image)
{
  int x, y;
  pgm_structure * new_pgm = (pgm_structure *) safe_malloc (sizeof(pgm_structure));
  int has_anything_changed = 1;
  int current_pass;
  int max_mask_size;
  char * current_pixel;

  new_pgm->width = input_image->width / 2;
  new_pgm->height = input_image->height / 2;
  new_pgm->pixel = (unsigned char *) safe_malloc (sizeof(unsigned char) * new_pgm->width * new_pgm->height);

  /* Copy over the image data, using the average of 4 pixels for to calculate each downsampled pixel. */
  for (y = 0; y < new_pgm->height; y++)
    for (x = 0; x < new_pgm->width; x++)
    {
      /* Set the pixel if there exists a non-zero value in the source pixels, else clear it. */
      new_pgm->pixel[(y * new_pgm->width) + x] = input_image->pixel[((y << 1) * input_image->width) + (x << 1)] ||
                                                 input_image->pixel[((y << 1) * input_image->width) + (x << 1) + 1] ||
                                                 input_image->pixel[(((y << 1) + 1) * input_image->width) + (x << 1)] ||
                                                 input_image->pixel[(((y << 1) + 1) * input_image->width) + (x << 1) + 1];
      new_pgm->pixel[(y * new_pgm->width) + x] = min(1, new_pgm->pixel[(y * new_pgm->width) + x]);
    }

  /* Now we need to recalculate the numbers for the smaller size. Just using the old_value / 2 can cause subtle
     and fairly rare, but very nasty, bugs. */

  current_pixel = new_pgm->pixel;
  /* First pass, set all non-zero values to 1. */
  for (x = 0; x < new_pgm->height * new_pgm->width; x++, current_pixel++)
    if(*current_pixel) *current_pixel = 1;

  /* Second pass and future passes. For each pass, if a pixel is itself the same value as the current pass,
     and its four neighbors are too, then it is incremented. If no pixels are incremented by the end of the pass,
     then we go again. Edge pixels are counted as always excluded (this should be true anyway for any sane mask,
     but if it isn't this will ensure that we eventually exit). */
  current_pass = 0;
  while (has_anything_changed)
  {
    current_pass++;

    has_anything_changed = 0; /* If this doesn't get set by the end of this pass, then we're done. */

    for (y = 1; y < new_pgm->height - 1; y++)
    {
      for (x = 1; x < new_pgm->width - 1; x++)
      {
        if (new_pgm->pixel[(y * new_pgm->width) + x] >= current_pass && /* By using >= instead of ==, we allow the algorithm to work in place. */
            new_pgm->pixel[(y * new_pgm->width) + (x + 1)] >= current_pass &&
            new_pgm->pixel[(y * new_pgm->width) + (x - 1)] >= current_pass &&
            new_pgm->pixel[((y + 1) * new_pgm->width) + x] >= current_pass &&
            new_pgm->pixel[((y - 1) * new_pgm->width) + x] >= current_pass)
         {
           new_pgm->pixel[(y * new_pgm->width) + x]++; /* Increment the value since it still has not been eroded,
                                                    as evidenced by the if statement that just evaluated to true. */
           has_anything_changed = 1;
         }
      }
    }
  }

  for (y = 1; y < new_pgm->height - 1; y++)
  {
   for (x = 1; x < new_pgm->width - 1; x++)
    {
      new_pgm->pixel[(y * new_pgm->width) + x] = apply_mask_fudge_factor(new_pgm->pixel[(y * new_pgm->width) + x]);
    }
  }

  max_mask_size = current_pass + 1; /* As a side-effect, we now know the maximum mask size, which we'll use to generate our masks. */
  max_mask_size = apply_mask_fudge_factor(max_mask_size);
  /* Commit the newly calculated max_mask_size to the vf->priv struct. */
  vf->priv->max_mask_size = max(max_mask_size, vf->priv->max_mask_size);

  return new_pgm;
}

/**
 * \brief Checks if YV12 is supported by the next filter.
 */
static unsigned int find_best(struct vf_instance *vf){
  int is_format_okay = vf_next_query_format(vf, IMGFMT_YV12);
  if ((is_format_okay & VFCAP_CSP_SUPPORTED_BY_HW) || (is_format_okay & VFCAP_CSP_SUPPORTED))
    return IMGFMT_YV12;
  else
    return 0;
}

//===========================================================================//

/**
 * \brief Configure the filter and call the next filter's config function.
 */
static int config(struct vf_instance *vf, int width, int height, int d_width, int d_height, unsigned int flags, unsigned int outfmt)
{
  if(!(vf->priv->fmt=find_best(vf)))
    return 0;
  else
    return vf_next_config(vf,width,height,d_width,d_height,flags,vf->priv->fmt);
}

/**
 * \brief Removes the logo from a plane (either luma or chroma).
 *
 * \param vf Not needed by this function, but needed by the blur function.
 * \param source The image to have it's logo removed.
 * \param destination Where the output image will be stored.
 * \param source_stride How far apart (in memory) two consecutive lines are.
 * \param destination Same as source_stride, but for the destination image.
 * \param width Width of the image. This is the same for source and destination.
 * \param height Height of the image. This is the same for source and destination.
 * \param is_image_direct If the image is direct, then source and destination are
 *        the same and we can save a lot of time by not copying pixels that
 *        haven't changed.
 * \param filter The image that stores the distance to the edge of the logo for
 *        each pixel.
 * \param logo_start_x Smallest x-coordinate that contains at least 1 logo pixel.
 * \param logo_start_y Smallest y-coordinate that contains at least 1 logo pixel.
 * \param logo_end_x Largest x-coordinate that contains at least 1 logo pixel.
 * \param logo_end_y Largest y-coordinate that contains at least 1 logo pixel.
 *
 * This function processes an entire plane. Pixels outside of the logo are copied
 * to the output without change, and pixels inside the logo have the de-blurring
 * function applied.
 */
static void convert_yv12(const vf_instance_t * const vf, const char * const source, const int source_stride,
                         const mp_image_t * const source_image, const int width, const int height,
                         char * const destination, const int destination_stride, int is_image_direct, pgm_structure * filter,
                         const int plane, const int logo_start_x, const int logo_start_y, const int logo_end_x, const int logo_end_y)
{
  int y;
  int x;

  /* These pointers point to where we are getting our pixel data (inside mpi) and where we are storing it (inside dmpi). */
  const unsigned char * source_line;
  unsigned char * destination_line;

  if (!is_image_direct)
    memcpy_pic(destination, source, width, height, destination_stride, source_stride);

  for (y = logo_start_y; y <= logo_end_y; y++)
  {
    source_line = (const unsigned char *) source + (source_stride * y);
    destination_line = (unsigned char *) destination + (destination_stride * y);

    for (x = logo_start_x; x <= logo_end_x; x++)
    {
      unsigned int output;

      if (filter->pixel[(y * filter->width) + x]) /* Only process if we are in the logo. */
      {
        get_blur(vf, &output, filter, source_image, x, y, plane);
        destination_line[x] = output;
      }
      else /* Else just copy the data. */
        if (!is_image_direct)
          destination_line[x] = source_line[x];
    }
  }
}

/**
 * \brief Process a frame.
 *
 * \param mpi The image sent to use by the previous filter.
 * \param dmpi Where we will store the processed output image.
 * \param vf This is how the filter gets access to it's persistant data.
 *
 * \return The return code of the next filter, or 0 on failure/error.
 *
 * This function processes an entire frame. The frame is sent by the previous
 * filter, has the logo removed by the filter, and is then sent to the next
 * filter.
 */
static int put_image(struct vf_instance *vf, mp_image_t *mpi, double pts){
    mp_image_t *dmpi;

    dmpi=vf_get_image(vf->next,vf->priv->fmt,
        MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
        mpi->w, mpi->h);

    /* Check to make sure that the filter image and the video stream are the same size. */
    if (vf->priv->filter->width != mpi->w || vf->priv->filter->height != mpi->h)
    {
      mp_msg(MSGT_VFILTER,MSGL_ERR, "Filter image and video stream are not of the same size. (Filter: %d x %d, Stream: %d x %d)\n",
             vf->priv->filter->width, vf->priv->filter->height, mpi->w, mpi->h);
      return 0;
    }

    switch(dmpi->imgfmt){
    case IMGFMT_YV12:
          convert_yv12(vf, mpi->planes[0],  mpi->stride[0], mpi, mpi->w, mpi->h,
                          dmpi->planes[0], dmpi->stride[0],
                          mpi->flags & MP_IMGFLAG_DIRECT, vf->priv->filter, 0,
                          vf->priv->bounding_rectangle_posx1, vf->priv->bounding_rectangle_posy1,
                          vf->priv->bounding_rectangle_posx2, vf->priv->bounding_rectangle_posy2);
          convert_yv12(vf, mpi->planes[1],  mpi->stride[1], mpi, mpi->w / 2, mpi->h / 2,
                          dmpi->planes[1], dmpi->stride[1],
                          mpi->flags & MP_IMGFLAG_DIRECT, vf->priv->half_size_filter, 1,
                          vf->priv->bounding_rectangle_half_size_posx1, vf->priv->bounding_rectangle_half_size_posy1,
                          vf->priv->bounding_rectangle_half_size_posx2, vf->priv->bounding_rectangle_half_size_posy2);
          convert_yv12(vf, mpi->planes[2],  mpi->stride[2], mpi, mpi->w / 2, mpi->h / 2,
                          dmpi->planes[2], dmpi->stride[2],
                          mpi->flags & MP_IMGFLAG_DIRECT, vf->priv->half_size_filter, 2,
                          vf->priv->bounding_rectangle_half_size_posx1, vf->priv->bounding_rectangle_half_size_posy1,
                          vf->priv->bounding_rectangle_half_size_posx2, vf->priv->bounding_rectangle_half_size_posy2);
          break;

    default:
        mp_msg(MSGT_VFILTER,MSGL_ERR,"Unhandled format: 0x%X\n",dmpi->imgfmt);
        return 0;
    }

    return vf_next_put_image(vf,dmpi, pts);
}

//===========================================================================//

/**
 * \brief Checks to see if the next filter accepts YV12 images.
 */
static int query_format(struct vf_instance *vf, unsigned int fmt)
{
  if (fmt == IMGFMT_YV12)
    return vf_next_query_format(vf, IMGFMT_YV12);
  else
    return 0;
}

/**
 * \brief Frees memory that our filter allocated.
 *
 * This is called at exit-time.
 */
static void uninit(vf_instance_t *vf)
{
  /* Destroy our masks and images. */
  destroy_pgm(vf->priv->filter);
  destroy_pgm(vf->priv->half_size_filter);
  destroy_masks(vf);

  /* Destroy our private structure that had been used to store those masks and images. */
  free(vf->priv);

  return;
}

/**
 * \brief Initializes our filter.
 *
 * \param args The arguments passed in from the command line go here. This
 *             filter expects only a single argument telling it where the PGM
 *             or PPM file that describes the logo region is.
 *
 * This sets up our instance variables and parses the arguments to the filter.
 */
static int vf_open(vf_instance_t *vf, char *args)
{
  vf->priv = safe_malloc(sizeof(vf_priv_s));
  vf->uninit = uninit;

  /* Load our filter image. */
  if (args)
    vf->priv->filter = load_pgm(args);
  else
  {
    mp_msg(MSGT_VFILTER, MSGL_ERR, "[vf]remove_logo usage: remove_logo=/path/to/filter_image_file.pgm\n");
    free(vf->priv);
    return 0;
  }

  if (vf->priv->filter == NULL)
  {
    /* Error message was displayed by load_pgm(). */
    free(vf->priv);
    return 0;
  }

  /* Create the scaled down filter image for the chroma planes. */
  convert_mask_to_strength_mask(vf, vf->priv->filter);
  vf->priv->half_size_filter = generate_half_size_image(vf, vf->priv->filter);

  /* Now that we know how many masks we need (the info is in vf), we can generate the masks. */
  initialize_masks(vf);

  /* Calculate our bounding rectangles, which determine in what region the logo resides for faster processing. */
  calculate_bounding_rectangle(&vf->priv->bounding_rectangle_posx1, &vf->priv->bounding_rectangle_posy1,
                               &vf->priv->bounding_rectangle_posx2, &vf->priv->bounding_rectangle_posy2,
                                vf->priv->filter);
  calculate_bounding_rectangle(&vf->priv->bounding_rectangle_half_size_posx1,
                               &vf->priv->bounding_rectangle_half_size_posy1,
                               &vf->priv->bounding_rectangle_half_size_posx2,
                               &vf->priv->bounding_rectangle_half_size_posy2,
                                vf->priv->half_size_filter);

  vf->config=config;
  vf->put_image=put_image;
  vf->query_format=query_format;
  return 1;
}

/**
 * \brief Meta data about our filter.
 */
const vf_info_t vf_info_remove_logo = {
    "Removes a tv logo based on a mask image.",
    "remove-logo",
    "Robert Edele",
    "",
    vf_open,
    NULL
};

//===========================================================================//
