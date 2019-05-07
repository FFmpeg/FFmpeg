
/* gifwrite.c - Functions to write GIFs.
   Copyright (C) 1997-2018 Eddie Kohler, ekohler@gmail.com
   This file is part of the LCDF GIF library.

   The LCDF GIF library is free software. It is distributed under the GNU
   General Public License, version 2; you can copy, distribute, or alter it at
   will, as long as this notice is kept intact and this source code is made
   available. There is no warranty, express or implied. */

#define GIF_MAX_CODE_BITS       12
#define GIF_MAX_CODE            0x1000
#define GIF_MAX_BLOCK           255

#define WRITE_BUFFER_SIZE   255
#define NODES_SIZE      GIF_MAX_CODE
#define LINKS_SIZE      GIF_MAX_CODE

#define TABLE_TYPE      0
#define LINKS_TYPE      1
#define MAX_LINKS_TYPE      5

#define GIF_WRITE_CAREFUL_MIN_CODE_SIZE 1
#define GIF_WRITE_EAGER_CLEAR           2
#define GIF_WRITE_OPTIMIZE              4
#define GIF_WRITE_SHRINK                8

#define GIF_DEBUG(a) printf

#define GIF_WRITE_CAREFUL_MIN_CODE_SIZE 1

#define Gif_New(t)     ((t*) calloc(sizeof(t), 1))
#define Gif_NewArray(t, n)     ((t*) calloc(sizeof(t), (n)))
#define Gif_ReArray(p, t, n)   ((p)=(t*) realloc((void*) (p), sizeof(t) * (n)))
#define Gif_Delete(p)          free((void*)(p))
#define Gif_DeleteArray(p)          free((void*)(p))

typedef uint16_t Gif_Code;

typedef struct Gif_CompressInfo {
    int flags;
    int loss;
} Gif_CompressInfo;

typedef struct Gif_Node {
  Gif_Code code;
  uint8_t type;
  uint8_t suffix;
  struct Gif_Node *sibling;
  union {
    struct Gif_Node *s;
    struct Gif_Node **m;
  } child;
} Gif_Node;


typedef struct Gif_CodeTable {
  Gif_Node *nodes;
  int nodes_pos;
  Gif_Node **links;
  int links_pos;
  int clear_code;
} Gif_CodeTable;

typedef struct Gif_Color {
    uint8_t gfc_red;       /* red component (0-255) */
    uint8_t gfc_green;     /* green component (0-255) */
    uint8_t gfc_blue;      /* blue component (0-255) */
} Gif_Color;

typedef struct Gif_Colormap {
    int ncol;
    Gif_Color *col;
} Gif_Colormap;

typedef struct Gif_Writer {
  uint8_t *v;
  uint32_t pos;
  uint32_t cap;
  Gif_CompressInfo gcinfo;
  int global_size;
  int local_size;
  int cleared;
  Gif_CodeTable code_table;
} Gif_Writer;

typedef struct Gif_Image {
    const uint8_t *image_data;

    uint16_t width;
    uint16_t linesize;
    uint16_t height;
    // uint16_t left;
    // uint16_t top;
    // uint16_t delay;
    // uint8_t disposal;
    // uint8_t interlace;

    short transparent;          /* -1 means no transparent index */
    // Gif_Colormap *local;

    // char* identifier;
    // Gif_Comment* comment;

    // void (*free_image_data)(void *);

    // uint32_t compressed_len;
    // uint8_t* compressed;
    // void (*free_compressed)(void *);

    // void* user_data;
    // void (*free_user_data)(void *);
} Gif_Image;

typedef struct Gif_Stream {
    // Gif_Image **images;
    // int nimages;
    // int imagescap;

    Gif_Colormap *global;
    uint16_t background;        /* 256 means no background */

    uint16_t screen_width;
    uint16_t screen_height;
    long loopcount;             /* -1 means no loop count */
} Gif_Stream;

/* Used to hold accumulated error for the current candidate match */
typedef struct gfc_rgbdiff {signed short r, g, b;} gfc_rgbdiff;
