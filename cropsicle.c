/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */

/* cropsicle.c - Minimal Growcut implementation
 * 
 * Copyright (C) 2014 Hans Petter Jansson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Hans Petter Jansson <hpj@copyleft.no>
 */

/* Build
 * -----
 *
 * > gcc -g -O3 cropsicle.c $(pkg-config --libs --cflags libpng) -lm -pthread -o cropsicle
 *
 * Run
 * ---
 *
 * This program supports 4-channel 8-bit-per-channel RGBA PNG images only. If
 * you have something else, you must convert it to the proper format first,
 * like this:
 *
 * > convert image.jpg -channel rgba png32:image.png
 *
 * Perform the Growcut operation like this:
 *
 * > cropsicle image.png overlay.png output.png
 *
 * Image is the source image, overlay is an alpha-transparent overlay with
 * a few green pixels spread out over the foreground you want to keep
 * and red pixels over the background. The pixels don't have to be perfect red
 * and green as long as the corresponding red/green channels are dominant and
 * the pixels are not transparent.
 *
 * Enjoy!
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include <png.h>

/* Define if you want a multithreaded implementation */
#define WITH_THREADS

/* Number of threads to use if multithreaded */
#define N_THREADS 4

/* Define if you want to see the preprocessing effects applied to the image buffer */
#undef SHOW_EFFECTS

typedef struct
{
  png_bytep *rows;
  int width, height;
  png_byte color_type;
  png_byte bit_depth;
}
Image;

static void
abort_ (const char *s, ...)
{
  va_list args;

  va_start (args, s);
  vfprintf (stderr, s, args);
  fprintf (stderr, "\n");
  va_end (args);
  abort ();
}

static void
read_png_file (const char *file_name, Image *image)
{
  char header [8];
  png_structp png_ptr;
  png_infop info_ptr;
  int number_of_passes;
  int x, y;
  FILE *fp;

  /* Open file and check file type */

  fp = fopen (file_name, "rb");
  if (!fp)
    abort_ ("File %s could not be opened for reading", file_name);

  fread (header, 1, 8, fp);

  if (png_sig_cmp (header, 0, 8))
    abort_ ("File %s is not a PNG file", file_name);

  /* Initialize */

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
    abort_ ("png_create_read_struct failed");

  info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr)
    abort_ ("png_create_info_struct failed");

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during init_io");

  png_init_io (png_ptr, fp);
  png_set_sig_bytes (png_ptr, 8);

  png_read_info (png_ptr, info_ptr);

  image->width = png_get_image_width (png_ptr, info_ptr);
  image->height = png_get_image_height (png_ptr, info_ptr);
  image->color_type = png_get_color_type (png_ptr, info_ptr);
  image->bit_depth = png_get_bit_depth (png_ptr, info_ptr);

  number_of_passes = png_set_interlace_handling (png_ptr);
  png_read_update_info (png_ptr, info_ptr);

  /* Read file */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during read_image");

  image->rows = (png_bytep*) malloc (sizeof (png_bytep) * image->height);
  for (y = 0; y < image->height; y++)
    image->rows [y] = (png_byte*) malloc (png_get_rowbytes (png_ptr, info_ptr));

  png_read_image (png_ptr, image->rows);
  fclose (fp);

  if (png_get_color_type (png_ptr, info_ptr) == PNG_COLOR_TYPE_RGB)
    abort_ ("Input file is PNG_COLOR_TYPE_RGB but must be PNG_COLOR_TYPE_RGBA "
            "(missing alpha channel)");

  if (png_get_color_type (png_ptr, info_ptr) != PNG_COLOR_TYPE_RGBA)
    abort_ ("Color_type of input file must be PNG_COLOR_TYPE_RGBA (%d) (is %d)",
            PNG_COLOR_TYPE_RGBA, png_get_color_type (png_ptr, info_ptr));
}

static void
write_png_file (Image *image, char *file_name)
{
  FILE *fp = fopen (file_name, "wb");
  png_structp png_ptr;
  png_infop info_ptr;
  int x, y;

  if (!fp)
    abort_ ("File %s could not be opened for writing", file_name);

  /* Initialize */

  png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (!png_ptr)
    abort_ ("png_create_write_struct failed");

  info_ptr = png_create_info_struct (png_ptr);
  if (!info_ptr)
    abort_ ("png_create_info_struct failed");

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error during init_io");

#if 0
  png_set_compression_level (png_ptr, 5);
#endif

  png_init_io (png_ptr, fp);
        
  /* Write header */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG header");

  png_set_IHDR (png_ptr, info_ptr, image->width, image->height,
                image->bit_depth, image->color_type, PNG_INTERLACE_NONE,
                PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  png_write_info (png_ptr, info_ptr);

  /* Write data */

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG data");

  png_write_image (png_ptr, image->rows);

  if (setjmp (png_jmpbuf (png_ptr)))
    abort_ ("Error writing PNG data (end of write)");

  png_write_end (png_ptr, NULL);

  /* Cleanup */

  for (y = 0; y < image->height; y++)
    free (image->rows [y]);
  free (image->rows);

  fclose (fp);
}

static void
get_pixel (Image *image, int x, int y, png_byte *out)
{
  png_byte *row;

  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
  {
    out [0] = out [1] = out [2] = 0xff;
    out [4] = 0x00;
    return;
  }

  row = image->rows [y];
  out [0] = row [x * 4];
  out [1] = row [x * 4 + 1];
  out [2] = row [x * 4 + 2];
  out [3] = row [x * 4 + 3];
}

static void
set_pixel (Image *image, int x, int y, png_byte *in)
{
  png_byte *row;

  if (x < 0 || y < 0 || x >= image->width || y >= image->height)
    return;

  row = image->rows [y];
  row [x * 4] = in [0];
  row [x * 4 + 1] = in [1];
  row [x * 4 + 2] = in [2];
  row [x * 4 + 3] = in [3];
}

static void
test_process_file (Image *image, Image *overlay)
{
  int x, y;

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
    {
      png_byte image_pixel [4];
      png_byte overlay_pixel [4];

      get_pixel (image, x, y, image_pixel);
      get_pixel (overlay, x, y, overlay_pixel);

      if (overlay_pixel [3] > 0x80)
      {
        image_pixel [0] = overlay_pixel [0];
        image_pixel [1] = overlay_pixel [1];
        image_pixel [2] = overlay_pixel [2];
      }

      set_pixel (image, x, y, image_pixel);
    }
  }
}

static const int nx8 [8] = { -1,  0,  1, -1, 1, -1, 0, 1 };
static const int ny8 [8] = { -1, -1, -1,  0, 0,  1, 1, 1 };

static void
process_pixel_neighbor_border (const Image *image, int index, const float *overlay_array_in, float *overlay_array_out,
                               const float *g_array, const int *neighbor_index_ofs, int i, int *converged)
{
  int neighbor_index = index + neighbor_index_ofs [i];
  float g = g_array [index * 8 + i];

  if (fabsf (g * overlay_array_in [neighbor_index]) > fabsf (overlay_array_out [index]))
  {
    overlay_array_out [index] = g * overlay_array_in [neighbor_index];
    *converged = 0;
  }
}

static void
process_pixel_border (const Image *image, int x, int y, const float *overlay_array_in, float *overlay_array_out,
                      const float *g_array, const int *neighbor_index_ofs, int *converged)
{
  int index = image->width * y + x;
  int i;

  overlay_array_out [index] = overlay_array_in [index];

  for (i = 0; i < 8; i++)
  {
    if (x + nx8 [i] < 0 || x + nx8 [i] >= image->width ||
        y + ny8 [i] < 0 || y + ny8 [i] >= image->height)
      continue;

    process_pixel_neighbor_border (image, index, overlay_array_in, overlay_array_out, g_array,
                                   neighbor_index_ofs, i, converged);
  }
}

static void
process_pixel_neighbor_internal (int index, const float *overlay_array_in, float *overlay_array_out,
                                 const float *g_array, const int *neighbor_index_ofs, int i,
                                 int *converged)
{
  int neighbor_index = index + neighbor_index_ofs [i];
  float g = g_array [index * 8 + i];

  if (fabsf (g * overlay_array_in [neighbor_index]) > fabsf (overlay_array_out [index]))
  {
    overlay_array_out [index] = g * overlay_array_in [neighbor_index];
    *converged = 0;
  }
}

static void
process_pixel_internal (int index, const float *overlay_array_in, float *overlay_array_out,
                        const float *g_array, const int *neighbor_index_ofs,
                        int *converged)
{
  int i;

  overlay_array_out [index] = overlay_array_in [index];

  for (i = 0; i < 8; i++)
    process_pixel_neighbor_internal (index, overlay_array_in, overlay_array_out, g_array,
                                     neighbor_index_ofs, i, converged);
}

#ifdef WITH_THREADS

typedef struct
{
  const Image *image;
  const int *neighbor_index_ofs;
  const float *image_array;
  const float *overlay_array_in;
  float *overlay_array_out;
  const float *g_array;
  int index_max;
  int thread_n;
}
ThreadArgs;

static void *
process_iteration_thread (ThreadArgs *args)
{
  int index;
  int converged = 1;

  for (index = args->image->width + args->thread_n * args->image->width + 1;
       index < args->index_max;
       index++)
  {
    int index_line_max = index + args->image->width - 2;

    for ( ; index < index_line_max; index++)
      process_pixel_internal (index, args->overlay_array_in, args->overlay_array_out, args->g_array,
                              args->neighbor_index_ofs, &converged);

    index += (N_THREADS - 1) * args->image->width;
    index++;
  }

  return (void *) converged;
}

static void *
process_iteration_borders_thread (ThreadArgs *args)
{
  int converged = 1;
  int x, y;

  for (x = 0; x < args->image->width; x++)
    process_pixel_border (args->image, x, 0, args->overlay_array_in, args->overlay_array_out, args->g_array,
                          args->neighbor_index_ofs, &converged);

  for (x = 0; x < args->image->width; x++)
    process_pixel_border (args->image, x, args->image->height - 1, args->overlay_array_in, args->overlay_array_out, args->g_array,
                          args->neighbor_index_ofs, &converged);

  for (y = 1; y < args->image->height - 1; y++)
    process_pixel_border (args->image, 0, y, args->overlay_array_in, args->overlay_array_out, args->g_array,
                          args->neighbor_index_ofs, &converged);

  for (y = 1; y < args->image->height - 1; y++)
    process_pixel_border (args->image, args->image->width - 1, y, args->overlay_array_in, args->overlay_array_out, args->g_array,
                          args->neighbor_index_ofs, &converged);

  return (void *) converged;
}

static int
process_iteration (const Image *image, const float *image_array, const float *overlay_array_in, float *overlay_array_out, const float *g_array)
{
  int converged = 1;
  int index;
  int index_max = (image->height - 1) * image->width - 1;
  int neighbor_index_ofs [8];
  ThreadArgs thread_args [N_THREADS + 1];
  pthread_t thread_info [N_THREADS + 1];
  int i;

  for (i = 0; i < 8; i++)
    neighbor_index_ofs [i] = nx8 [i] + ny8 [i] * image->width;

  thread_args [0].image = image;
  thread_args [0].neighbor_index_ofs = neighbor_index_ofs;
  thread_args [0].image_array = image_array;
  thread_args [0].overlay_array_in = overlay_array_in;
  thread_args [0].overlay_array_out = overlay_array_out;
  thread_args [0].g_array = g_array;
  thread_args [0].index_max = index_max;
  thread_args [0].thread_n = 0;

  for (i = 1; i < N_THREADS + 1; i++)
  {
    thread_args [i] = thread_args [0];
    thread_args [i].thread_n = i;
  }

  /* Internal area */

  for (i = 0; i < N_THREADS; i++)
    pthread_create (&thread_info [i], NULL, (void *(*)(void *)) process_iteration_thread, &thread_args [i]);

  /* Borders */

  pthread_create (&thread_info [N_THREADS], NULL, (void *(*)(void *)) process_iteration_borders_thread, &thread_args [N_THREADS]);

  /* Wait for threads and collect results */

  for (i = 0; i < N_THREADS + 1; i++)
  {
    void *ret;

    pthread_join (thread_info [i], &ret);
    converged &= (int) ret;
  }

  return converged;
}

#else

static int
process_iteration (const Image *image, const float *image_array, const float *overlay_array_in, float *overlay_array_out, const float *g_array)
{
  int converged = 1;
  int index;
  int index_max = (image->height - 1) * image->width - 1;
  int neighbor_index_ofs [8];
  int x, y;
  int i;

  for (i = 0; i < 8; i++)
    neighbor_index_ofs [i] = nx8 [i] + ny8 [i] * image->width;

  /* Internal area */

  for (index = image->width + 1; index < index_max; index++)
  {
    int index_line_max = index + image->width - 2;

    for ( ; index < index_line_max; index++)
      process_pixel_internal (index, overlay_array_in, overlay_array_out, g_array,
                              neighbor_index_ofs, &converged);

    index++;
  }

  /* Borders */

  for (x = 0; x < image->width; x++)
    process_pixel_border (image, x, 0, overlay_array_in, overlay_array_out, g_array,
                          neighbor_index_ofs, &converged);

  for (x = 0; x < image->width; x++)
    process_pixel_border (image, x, image->height - 1, overlay_array_in, overlay_array_out, g_array,
                          neighbor_index_ofs, &converged);

  for (y = 1; y < image->height - 1; y++)
    process_pixel_border (image, 0, y, overlay_array_in, overlay_array_out, g_array,
                          neighbor_index_ofs, &converged);

  for (y = 1; y < image->height - 1; y++)
    process_pixel_border (image, image->width - 1, y, overlay_array_in, overlay_array_out, g_array,
                          neighbor_index_ofs, &converged);

  return converged;
}

#endif

static void
blur_image_array (Image *image, float *array)
{
  const int nx9 [9] = { 0, -1,  0,  1, -1, 1, -1, 0, 1 };
  const int ny9 [9] = { 0, -1, -1, -1,  0, 0,  1, 1, 1 };
  float *temp_array;
  int x, y;
  int i;

  temp_array = malloc (image->width * image->height * 3 * sizeof (float));
  memset (temp_array, 0, image->width * image->height * 3 * sizeof (float));

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
    {
      int index = (x + (y * image->width)) * 3;
      int n_pixels = 0;

      for (i = 0; i < 9; i++)
      {
        int neighbor_index;

        if (x + nx9 [i] < 0 || x + nx9 [i] >= image->width ||
            y + ny9 [i] < 0 || y + ny9 [i] >= image->height)
          continue;

        neighbor_index = (x + nx9 [i] + ((y + ny9 [i]) * image->width)) * 3;

        temp_array [index] += array [neighbor_index];
        temp_array [index + 1] += array [neighbor_index + 1];
        temp_array [index + 2] += array [neighbor_index + 2];
        n_pixels++;
      }

      temp_array [index] /= (float) n_pixels;
      temp_array [index + 1] /= (float) n_pixels;
      temp_array [index + 2] /= (float) n_pixels;
    }
  }

  memcpy (array, temp_array, image->width * image->height * 3 * sizeof (float));
  free (temp_array);
}

#if 0

/* TODO: A further refinement would be to process in HSV color space, so we
 * can apply different weights to hue, saturation and value. Typically, hue
 * would have a higher weight. */

static void
image_array_to_hsv (Image *image, float *array)
{
  int x, y;
  int i;

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
    {
      int index = (x + (y * image->width)) * 3;
      int n_pixels = 0;

      temp_array [index] /= (float) n_pixels;
      temp_array [index + 1] /= (float) n_pixels;
      temp_array [index + 2] /= (float) n_pixels;
    }
  }

  memcpy (array, temp_array, image->width * image->height * 3 * sizeof (float));
  free (temp_array);
}
#endif

static void
calc_g (const Image *image, const float *image_array, float *g_array, int x, int y)
{
  int pixel_index;
  int pixel_offset;
  int neighbor_offset;
  const float maxC = 1.732050808;
  int i;

  pixel_index = x + (y * image->width);
  pixel_offset = pixel_index * 3;

  for (i = 0; i < 8; i++)
  {
    float C;

    if (x + nx8 [i] < 0 || x + nx8 [i] >= image->width ||
        y + ny8 [i] < 0 || y + ny8 [i] >= image->height)
      continue;

    neighbor_offset = (x + nx8 [i] + ((y + ny8 [i]) * image->width)) * 3;

    C = sqrtf ((image_array [pixel_offset] - image_array [neighbor_offset]) * (image_array [pixel_offset] - image_array [neighbor_offset]) +
               (image_array [pixel_offset + 1] - image_array [neighbor_offset + 1]) * (image_array [pixel_offset + 1] - image_array [neighbor_offset + 1]) +
               (image_array [pixel_offset + 2] - image_array [neighbor_offset + 2]) * (image_array [pixel_offset + 2] - image_array [neighbor_offset + 2]));
    g_array [pixel_index * 8 + i] = 1.0 - (C / maxC);
  }
}

static void
process_file (Image *image, Image *overlay)
{
  float *image_array, *overlay_array_a, *overlay_array_b, *g_array;
  const int max_iter = 2000;
  int iter = 0;
  int x, y;

  image_array = malloc (image->width * image->height * 3 * sizeof (float));
  overlay_array_a = malloc (image->width * image->height * sizeof (float));
  overlay_array_b = malloc (image->width * image->height * sizeof (float));
  g_array = malloc (image->width * image->height * 8 * sizeof (float));

  memset (image_array, 0, image->width * image->height * 3 * sizeof (float));
  memset (overlay_array_a, 0, image->width * image->height * sizeof (float));
  memset (overlay_array_b, 0, image->width * image->height * sizeof (float));
#if 0
  memset (g_array, 0, image->width * image->height * 8 * sizeof (float));
#endif

  /* Init arrays */

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
    {
      png_byte image_pixel [4];
      png_byte overlay_pixel [4];

      get_pixel (image, x, y, image_pixel);
      get_pixel (overlay, x, y, overlay_pixel);

      image_array [(x + y * image->width) * 3]     = (float) image_pixel [0] / 255.0;
      image_array [(x + y * image->width) * 3 + 1] = (float) image_pixel [1] / 255.0;
      image_array [(x + y * image->width) * 3 + 2] = (float) image_pixel [2] / 255.0;

      if (overlay_pixel [3] > 0x80)
      {
        if ((int) overlay_pixel [0] > (int) overlay_pixel [1] + 128)
        {
          /* Red, background */
          overlay_array_a [x + y * image->width] = -1.0;
        }
        else
        {
          /* Green, foreground */
          overlay_array_a [x + y * image->width] = 1.0;
        }
      }
    }
  }

  blur_image_array (image, image_array);

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
      calc_g (image, image_array, g_array, x, y);
  }

  /* Process */

  while (!process_iteration (image, image_array, overlay_array_a, overlay_array_b, g_array) &&
         (++iter < max_iter))
  {
    float *tmp_array;

    tmp_array = overlay_array_a;
    overlay_array_a = overlay_array_b;
    overlay_array_b = tmp_array;
  }

  /* Generate alpha from arrays */

  for (y = 0; y < image->height; y++)
  {
    for (x = 0; x < image->width; x++)
    {
      png_byte image_pixel [4];

      get_pixel (image, x, y, image_pixel);
      image_pixel [3] = overlay_array_b [x + (y * image->width)] > 0.0 ? 0xff : 0x00;

#ifdef SHOW_EFFECTS
      image_pixel [0] = image_array [(x + (y * image->width)) * 3] * 255.0;
      image_pixel [1] = image_array [(x + (y * image->width)) * 3 + 1] * 255.0;
      image_pixel [2] = image_array [(x + (y * image->width)) * 3 + 2] * 255.0;
#endif

      set_pixel (image, x, y, image_pixel);
    }
  }
}

int
main (int argc, char **argv)
{
  Image image;
  Image overlay;

  if (argc != 4)
    abort_ ("Usage: %s <image_in> <overlay_in> <image_out>", argv [0]);

  read_png_file (argv [1], &image);
  read_png_file (argv [2], &overlay);

  process_file (&image, &overlay);

  write_png_file (&image, argv [3]);

  return 0;
}
