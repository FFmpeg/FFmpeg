/*
 * Copyright (c) 2014 Lukasz Marek
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

//TODO: support for more formats
//TODO: support for more systems.
//TODO: implement X11, Windows, Mac OS native default window. SDL 1.2 doesn't allow to render to custom thread.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "config.h"

#if HAVE_WINDOWS_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if HAVE_OPENGL_GL3_H
#include <OpenGL/gl3.h>
#elif HAVE_ES2_GL_H
#include <ES2/gl.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#if HAVE_GLXGETPROCADDRESS
#include <GL/glx.h>
#endif

#if CONFIG_SDL2
#include <SDL.h>
#endif

#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavformat/avformat.h"
#include "libavformat/internal.h"
#include "libavdevice/avdevice.h"
#include "opengl_enc_shaders.h"

#ifndef APIENTRY
#define APIENTRY
#endif

/* FF_GL_RED_COMPONENT is used for planar pixel types.
 * Only red component is sampled in shaders.
 * On some platforms GL_RED is not available and GL_LUMINANCE have to be used,
 * but since OpenGL 3.0 GL_LUMINANCE is deprecated.
 * GL_RED produces RGBA = value, 0, 0, 1.
 * GL_LUMINANCE produces RGBA = value, value, value, 1.
 * Note: GL_INTENSITY may also be used which produce RGBA = value, value, value, value. */
#if defined(GL_RED)
#define FF_GL_RED_COMPONENT GL_RED
#elif defined(GL_LUMINANCE)
#define FF_GL_RED_COMPONENT GL_LUMINANCE
#else
#define FF_GL_RED_COMPONENT 0x1903; //GL_RED
#endif

/* Constants not defined for iOS */
#define FF_GL_UNSIGNED_BYTE_3_3_2 0x8032
#define FF_GL_UNSIGNED_BYTE_2_3_3_REV 0x8362
#define FF_GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#define FF_GL_UNPACK_ROW_LENGTH          0x0CF2

/* MinGW exposes only OpenGL 1.1 API */
#define FF_GL_ARRAY_BUFFER                0x8892
#define FF_GL_ELEMENT_ARRAY_BUFFER        0x8893
#define FF_GL_STATIC_DRAW                 0x88E4
#define FF_GL_FRAGMENT_SHADER             0x8B30
#define FF_GL_VERTEX_SHADER               0x8B31
#define FF_GL_COMPILE_STATUS              0x8B81
#define FF_GL_LINK_STATUS                 0x8B82
#define FF_GL_INFO_LOG_LENGTH             0x8B84
typedef void   (APIENTRY *FF_PFNGLACTIVETEXTUREPROC) (GLenum texture);
typedef void   (APIENTRY *FF_PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void   (APIENTRY *FF_PFNGLDELETEBUFFERSPROC) (GLsizei n, const GLuint *buffers);
typedef void   (APIENTRY *FF_PFNGLBUFFERDATAPROC) (GLenum target, ptrdiff_t size, const GLvoid *data, GLenum usage);
typedef void   (APIENTRY *FF_PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef GLint  (APIENTRY *FF_PFNGLGETATTRIBLOCATIONPROC) (GLuint program, const char *name);
typedef void   (APIENTRY *FF_PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void   (APIENTRY *FF_PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, uintptr_t pointer);
typedef GLint  (APIENTRY *FF_PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const char *name);
typedef void   (APIENTRY *FF_PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void   (APIENTRY *FF_PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void   (APIENTRY *FF_PFNGLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef GLuint (APIENTRY *FF_PFNGLCREATEPROGRAMPROC) (void);
typedef void   (APIENTRY *FF_PFNGLDELETEPROGRAMPROC) (GLuint program);
typedef void   (APIENTRY *FF_PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void   (APIENTRY *FF_PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void   (APIENTRY *FF_PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint *params);
typedef void   (APIENTRY *FF_PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei *length, char *infoLog);
typedef void   (APIENTRY *FF_PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef GLuint (APIENTRY *FF_PFNGLCREATESHADERPROC) (GLenum type);
typedef void   (APIENTRY *FF_PFNGLDELETESHADERPROC) (GLuint shader);
typedef void   (APIENTRY *FF_PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef void   (APIENTRY *FF_PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const char* *string, const GLint *length);
typedef void   (APIENTRY *FF_PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint *params);
typedef void   (APIENTRY *FF_PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog);

typedef struct FFOpenGLFunctions {
    FF_PFNGLACTIVETEXTUREPROC glActiveTexture;                     //Require GL ARB multitexture
    FF_PFNGLGENBUFFERSPROC glGenBuffers;                           //Require GL_ARB_vertex_buffer_object
    FF_PFNGLDELETEBUFFERSPROC glDeleteBuffers;                     //Require GL_ARB_vertex_buffer_object
    FF_PFNGLBUFFERDATAPROC glBufferData;                           //Require GL_ARB_vertex_buffer_object
    FF_PFNGLBINDBUFFERPROC glBindBuffer;                           //Require GL_ARB_vertex_buffer_object
    FF_PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;             //Require GL_ARB_vertex_shader
    FF_PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray; //Require GL_ARB_vertex_shader
    FF_PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;         //Require GL_ARB_vertex_shader
    FF_PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;           //Require GL_ARB_shader_objects
    FF_PFNGLUNIFORM1FPROC glUniform1f;                             //Require GL_ARB_shader_objects
    FF_PFNGLUNIFORM1IPROC glUniform1i;                             //Require GL_ARB_shader_objects
    FF_PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;               //Require GL_ARB_shader_objects
    FF_PFNGLCREATEPROGRAMPROC glCreateProgram;                     //Require GL_ARB_shader_objects
    FF_PFNGLDELETEPROGRAMPROC glDeleteProgram;                     //Require GL_ARB_shader_objects
    FF_PFNGLUSEPROGRAMPROC glUseProgram;                           //Require GL_ARB_shader_objects
    FF_PFNGLLINKPROGRAMPROC glLinkProgram;                         //Require GL_ARB_shader_objects
    FF_PFNGLGETPROGRAMIVPROC glGetProgramiv;                       //Require GL_ARB_shader_objects
    FF_PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;             //Require GL_ARB_shader_objects
    FF_PFNGLATTACHSHADERPROC glAttachShader;                       //Require GL_ARB_shader_objects
    FF_PFNGLCREATESHADERPROC glCreateShader;                       //Require GL_ARB_shader_objects
    FF_PFNGLDELETESHADERPROC glDeleteShader;                       //Require GL_ARB_shader_objects
    FF_PFNGLCOMPILESHADERPROC glCompileShader;                     //Require GL_ARB_shader_objects
    FF_PFNGLSHADERSOURCEPROC glShaderSource;                       //Require GL_ARB_shader_objects
    FF_PFNGLGETSHADERIVPROC glGetShaderiv;                         //Require GL_ARB_shader_objects
    FF_PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;               //Require GL_ARB_shader_objects
} FFOpenGLFunctions;

#define OPENGL_ERROR_CHECK(ctx) \
{\
    GLenum err_code; \
    if ((err_code = glGetError()) != GL_NO_ERROR) { \
        av_log(ctx, AV_LOG_ERROR, "OpenGL error occurred in '%s', line %d: %d\n", __FUNCTION__, __LINE__, err_code); \
        goto fail; \
    } \
}\

typedef struct OpenGLVertexInfo
{
    float x, y, z;    ///<Position
    float s0, t0;     ///<Texture coords
} OpenGLVertexInfo;

/* defines 2 triangles to display */
static const GLushort g_index[6] =
{
    0, 1, 2,
    0, 3, 2,
};

typedef struct OpenGLContext {
    AVClass *class;                    ///< class for private options

#if CONFIG_SDL2
    SDL_Window *window;
    SDL_GLContext glcontext;
#endif
    FFOpenGLFunctions glprocs;

    int inited;                        ///< Set to 1 when write_header was successfully called.
    uint8_t background[4];             ///< Background color
    int no_window;                     ///< 0 for create default window
    char *window_title;                ///< Title of the window

    /* OpenGL implementation limits */
    GLint max_texture_size;            ///< Maximum texture size
    GLint max_viewport_width;          ///< Maximum viewport size
    GLint max_viewport_height;         ///< Maximum viewport size
    int non_pow_2_textures;            ///< 1 when non power of 2 textures are supported
    int unpack_subimage;               ///< 1 when GL_EXT_unpack_subimage is available

    /* Current OpenGL configuration */
    GLuint program;                    ///< Shader program
    GLuint vertex_shader;              ///< Vertex shader
    GLuint fragment_shader;            ///< Fragment shader for current pix_pmt
    GLuint texture_name[4];            ///< Textures' IDs
    GLuint index_buffer;               ///< Index buffer
    GLuint vertex_buffer;              ///< Vertex buffer
    OpenGLVertexInfo vertex[4];        ///< VBO
    GLint projection_matrix_location;  ///< Uniforms' locations
    GLint model_view_matrix_location;
    GLint color_map_location;
    GLint chroma_div_w_location;
    GLint chroma_div_h_location;
    GLint texture_location[4];
    GLint position_attrib;             ///< Attibutes' locations
    GLint texture_coords_attrib;

    GLfloat projection_matrix[16];     ///< Projection matrix
    GLfloat model_view_matrix[16];     ///< Modev view matrix
    GLfloat color_map[16];             ///< RGBA color map matrix
    GLfloat chroma_div_w;              ///< Chroma subsampling w ratio
    GLfloat chroma_div_h;              ///< Chroma subsampling h ratio

    /* Stream information */
    GLenum format;
    GLenum type;
    int width;                         ///< Stream width
    int height;                        ///< Stream height
    enum AVPixelFormat pix_fmt;        ///< Stream pixel format
    int picture_width;                 ///< Rendered width
    int picture_height;                ///< Rendered height
    int window_width;
    int window_height;
} OpenGLContext;

static const struct OpenGLFormatDesc {
    enum AVPixelFormat fixel_format;
    const char * const * fragment_shader;
    GLenum format;
    GLenum type;
} opengl_format_desc[] = {
    { AV_PIX_FMT_YUV420P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV444P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV422P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV410P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV411P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV440P,    &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUV420P16,  &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_YUV422P16,  &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_YUV444P16,  &FF_OPENGL_FRAGMENT_SHADER_YUV_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_YUVA420P,   &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUVA444P,   &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUVA422P,   &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_YUVA420P16, &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_YUVA422P16, &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_YUVA444P16, &FF_OPENGL_FRAGMENT_SHADER_YUVA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_RGB24,      &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_BGR24,      &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_0RGB,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_RGB0,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_0BGR,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_BGR0,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_RGB565,     &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5 },
    { AV_PIX_FMT_BGR565,     &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_SHORT_5_6_5 },
    { AV_PIX_FMT_RGB555,     &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, FF_GL_UNSIGNED_SHORT_1_5_5_5_REV },
    { AV_PIX_FMT_BGR555,     &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGBA, FF_GL_UNSIGNED_SHORT_1_5_5_5_REV },
    { AV_PIX_FMT_RGB8,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, FF_GL_UNSIGNED_BYTE_3_3_2 },
    { AV_PIX_FMT_BGR8,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, FF_GL_UNSIGNED_BYTE_2_3_3_REV },
    { AV_PIX_FMT_RGB48,      &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_BGR48,      &FF_OPENGL_FRAGMENT_SHADER_RGB_PACKET,  GL_RGB, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_ARGB,       &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_RGBA,       &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_ABGR,       &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_BGRA,       &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_RGBA64,     &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_BGRA64,     &FF_OPENGL_FRAGMENT_SHADER_RGBA_PACKET, GL_RGBA, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_GBRP,       &FF_OPENGL_FRAGMENT_SHADER_RGB_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_GBRP16,     &FF_OPENGL_FRAGMENT_SHADER_RGB_PLANAR,  FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_GBRAP,      &FF_OPENGL_FRAGMENT_SHADER_RGBA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_GBRAP16,    &FF_OPENGL_FRAGMENT_SHADER_RGBA_PLANAR, FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_GRAY8,      &FF_OPENGL_FRAGMENT_SHADER_GRAY,        FF_GL_RED_COMPONENT, GL_UNSIGNED_BYTE },
    { AV_PIX_FMT_GRAY16,     &FF_OPENGL_FRAGMENT_SHADER_GRAY,        FF_GL_RED_COMPONENT, GL_UNSIGNED_SHORT },
    { AV_PIX_FMT_NONE,       NULL }
};

static av_cold int opengl_prepare_vertex(AVFormatContext *s);
static int opengl_draw(AVFormatContext *h, void *intput, int repaint, int is_pkt);
static av_cold int opengl_init_context(OpenGLContext *opengl);

static av_cold void opengl_deinit_context(OpenGLContext *opengl)
{
    glDeleteTextures(4, opengl->texture_name);
    opengl->texture_name[0] = opengl->texture_name[1] =
    opengl->texture_name[2] = opengl->texture_name[3] = 0;
    if (opengl->glprocs.glUseProgram)
        opengl->glprocs.glUseProgram(0);
    if (opengl->glprocs.glDeleteProgram) {
        opengl->glprocs.glDeleteProgram(opengl->program);
        opengl->program = 0;
    }
    if (opengl->glprocs.glDeleteShader) {
        opengl->glprocs.glDeleteShader(opengl->vertex_shader);
        opengl->glprocs.glDeleteShader(opengl->fragment_shader);
        opengl->vertex_shader = opengl->fragment_shader = 0;
    }
    if (opengl->glprocs.glBindBuffer) {
        opengl->glprocs.glBindBuffer(FF_GL_ARRAY_BUFFER, 0);
        opengl->glprocs.glBindBuffer(FF_GL_ELEMENT_ARRAY_BUFFER, 0);
    }
    if (opengl->glprocs.glDeleteBuffers) {
        opengl->glprocs.glDeleteBuffers(2, &opengl->index_buffer);
        opengl->vertex_buffer = opengl->index_buffer = 0;
    }
}

static int opengl_resize(AVFormatContext *h, int width, int height)
{
    int ret = 0;
    OpenGLContext *opengl = h->priv_data;
    opengl->window_width = width;
    opengl->window_height = height;
    if (opengl->inited) {
        if (opengl->no_window &&
            (ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_PREPARE_WINDOW_BUFFER, NULL , 0)) < 0) {
            av_log(opengl, AV_LOG_ERROR, "Application failed to prepare window buffer.\n");
            goto end;
        }
        if ((ret = opengl_prepare_vertex(h)) < 0)
            goto end;
        ret = opengl_draw(h, NULL, 1, 0);
    }
  end:
    return ret;
}

static int opengl_control_message(AVFormatContext *h, int type, void *data, size_t data_size)
{
    OpenGLContext *opengl = h->priv_data;
    switch(type) {
    case AV_APP_TO_DEV_WINDOW_SIZE:
        if (data) {
            AVDeviceRect *message = data;
            return opengl_resize(h, message->width, message->height);
        }
        return AVERROR(EINVAL);
    case AV_APP_TO_DEV_WINDOW_REPAINT:
        return opengl_resize(h, opengl->window_width, opengl->window_height);
    }
    return AVERROR(ENOSYS);
}

#if CONFIG_SDL2
static int opengl_sdl_process_events(AVFormatContext *h)
{
    OpenGLContext *opengl = h->priv_data;
    AVDeviceRect message;
    SDL_Event event;
    SDL_PumpEvents();
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) {
        switch (event.type) {
        case SDL_QUIT:
            return AVERROR(EIO);
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_q:
                return AVERROR(EIO);
            }
            return 0;
        case SDL_WINDOWEVENT:
            switch(event.window.event) {
            case SDL_WINDOWEVENT_RESIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                SDL_GL_GetDrawableSize(opengl->window, &message.width, &message.height);
                return opengl_control_message(h, AV_APP_TO_DEV_WINDOW_SIZE, &message, sizeof(AVDeviceRect));
            default:
                break;
            }
        }
    }
    return 0;
}

static int av_cold opengl_sdl_create_window(AVFormatContext *h)
{
    OpenGLContext *opengl = h->priv_data;
    AVDeviceRect message;
    if (SDL_Init(SDL_INIT_VIDEO)) {
        av_log(opengl, AV_LOG_ERROR, "Unable to initialize SDL: %s\n", SDL_GetError());
        return AVERROR_EXTERNAL;
    }
    opengl->window = SDL_CreateWindow(opengl->window_title,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      opengl->window_width, opengl->window_height,
                                      SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!opengl->window) {
        av_log(opengl, AV_LOG_ERROR, "Unable to create default window: %s\n", SDL_GetError());
        return AVERROR_EXTERNAL;
    }
    opengl->glcontext = SDL_GL_CreateContext(opengl->window);
    if (!opengl->glcontext) {
        av_log(opengl, AV_LOG_ERROR, "Unable to create OpenGL context on default window: %s\n", SDL_GetError());
        return AVERROR_EXTERNAL;
    }
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    av_log(opengl, AV_LOG_INFO, "SDL driver: '%s'.\n", SDL_GetCurrentVideoDriver());
    SDL_GL_GetDrawableSize(opengl->window, &message.width, &message.height);
    return opengl_control_message(h, AV_APP_TO_DEV_WINDOW_SIZE, &message, sizeof(AVDeviceRect));
}

static int av_cold opengl_sdl_load_procedures(OpenGLContext *opengl)
{
    FFOpenGLFunctions *procs = &opengl->glprocs;

#define LOAD_OPENGL_FUN(name, type) \
    procs->name = (type)SDL_GL_GetProcAddress(#name); \
    if (!procs->name) { \
        av_log(opengl, AV_LOG_ERROR, "Cannot load OpenGL function: '%s'\n", #name); \
        return AVERROR(ENOSYS); \
    }

    LOAD_OPENGL_FUN(glActiveTexture, FF_PFNGLACTIVETEXTUREPROC)
    LOAD_OPENGL_FUN(glGenBuffers, FF_PFNGLGENBUFFERSPROC)
    LOAD_OPENGL_FUN(glDeleteBuffers, FF_PFNGLDELETEBUFFERSPROC)
    LOAD_OPENGL_FUN(glBufferData, FF_PFNGLBUFFERDATAPROC)
    LOAD_OPENGL_FUN(glBindBuffer, FF_PFNGLBINDBUFFERPROC)
    LOAD_OPENGL_FUN(glGetAttribLocation, FF_PFNGLGETATTRIBLOCATIONPROC)
    LOAD_OPENGL_FUN(glGetUniformLocation, FF_PFNGLGETUNIFORMLOCATIONPROC)
    LOAD_OPENGL_FUN(glUniform1f, FF_PFNGLUNIFORM1FPROC)
    LOAD_OPENGL_FUN(glUniform1i, FF_PFNGLUNIFORM1IPROC)
    LOAD_OPENGL_FUN(glUniformMatrix4fv, FF_PFNGLUNIFORMMATRIX4FVPROC)
    LOAD_OPENGL_FUN(glCreateProgram, FF_PFNGLCREATEPROGRAMPROC)
    LOAD_OPENGL_FUN(glDeleteProgram, FF_PFNGLDELETEPROGRAMPROC)
    LOAD_OPENGL_FUN(glUseProgram, FF_PFNGLUSEPROGRAMPROC)
    LOAD_OPENGL_FUN(glLinkProgram, FF_PFNGLLINKPROGRAMPROC)
    LOAD_OPENGL_FUN(glGetProgramiv, FF_PFNGLGETPROGRAMIVPROC)
    LOAD_OPENGL_FUN(glGetProgramInfoLog, FF_PFNGLGETPROGRAMINFOLOGPROC)
    LOAD_OPENGL_FUN(glAttachShader, FF_PFNGLATTACHSHADERPROC)
    LOAD_OPENGL_FUN(glCreateShader, FF_PFNGLCREATESHADERPROC)
    LOAD_OPENGL_FUN(glDeleteShader, FF_PFNGLDELETESHADERPROC)
    LOAD_OPENGL_FUN(glCompileShader, FF_PFNGLCOMPILESHADERPROC)
    LOAD_OPENGL_FUN(glShaderSource, FF_PFNGLSHADERSOURCEPROC)
    LOAD_OPENGL_FUN(glGetShaderiv, FF_PFNGLGETSHADERIVPROC)
    LOAD_OPENGL_FUN(glGetShaderInfoLog, FF_PFNGLGETSHADERINFOLOGPROC)
    LOAD_OPENGL_FUN(glEnableVertexAttribArray, FF_PFNGLENABLEVERTEXATTRIBARRAYPROC)
    LOAD_OPENGL_FUN(glVertexAttribPointer, FF_PFNGLVERTEXATTRIBPOINTERPROC)

    return 0;

#undef LOAD_OPENGL_FUN
}
#endif /* CONFIG_SDL2 */

#if defined(__APPLE__)
static int av_cold opengl_load_procedures(OpenGLContext *opengl)
{
    FFOpenGLFunctions *procs = &opengl->glprocs;

#if CONFIG_SDL2
    if (!opengl->no_window)
        return opengl_sdl_load_procedures(opengl);
#endif

    procs->glActiveTexture = glActiveTexture;
    procs->glGenBuffers = glGenBuffers;
    procs->glDeleteBuffers = glDeleteBuffers;
    procs->glBufferData = glBufferData;
    procs->glBindBuffer = glBindBuffer;
    procs->glGetAttribLocation = glGetAttribLocation;
    procs->glGetUniformLocation = glGetUniformLocation;
    procs->glUniform1f = glUniform1f;
    procs->glUniform1i = glUniform1i;
    procs->glUniformMatrix4fv = glUniformMatrix4fv;
    procs->glCreateProgram = glCreateProgram;
    procs->glDeleteProgram = glDeleteProgram;
    procs->glUseProgram = glUseProgram;
    procs->glLinkProgram = glLinkProgram;
    procs->glGetProgramiv = glGetProgramiv;
    procs->glGetProgramInfoLog = glGetProgramInfoLog;
    procs->glAttachShader = glAttachShader;
    procs->glCreateShader = glCreateShader;
    procs->glDeleteShader = glDeleteShader;
    procs->glCompileShader = glCompileShader;
    procs->glShaderSource = glShaderSource;
    procs->glGetShaderiv = glGetShaderiv;
    procs->glGetShaderInfoLog = glGetShaderInfoLog;
    procs->glEnableVertexAttribArray = glEnableVertexAttribArray;
    procs->glVertexAttribPointer = (FF_PFNGLVERTEXATTRIBPOINTERPROC) glVertexAttribPointer;
    return 0;
}
#else
static int av_cold opengl_load_procedures(OpenGLContext *opengl)
{
    FFOpenGLFunctions *procs = &opengl->glprocs;

#if HAVE_GLXGETPROCADDRESS
#define SelectedGetProcAddress glXGetProcAddress
#elif HAVE_WGLGETPROCADDRESS
#define SelectedGetProcAddress wglGetProcAddress
#endif

#define LOAD_OPENGL_FUN(name, type) \
    procs->name = (type)SelectedGetProcAddress(#name); \
    if (!procs->name) { \
        av_log(opengl, AV_LOG_ERROR, "Cannot load OpenGL function: '%s'\n", #name); \
        return AVERROR(ENOSYS); \
    }

#if CONFIG_SDL2
    if (!opengl->no_window)
        return opengl_sdl_load_procedures(opengl);
#endif

    LOAD_OPENGL_FUN(glActiveTexture, FF_PFNGLACTIVETEXTUREPROC)
    LOAD_OPENGL_FUN(glGenBuffers, FF_PFNGLGENBUFFERSPROC)
    LOAD_OPENGL_FUN(glDeleteBuffers, FF_PFNGLDELETEBUFFERSPROC)
    LOAD_OPENGL_FUN(glBufferData, FF_PFNGLBUFFERDATAPROC)
    LOAD_OPENGL_FUN(glBindBuffer, FF_PFNGLBINDBUFFERPROC)
    LOAD_OPENGL_FUN(glGetAttribLocation, FF_PFNGLGETATTRIBLOCATIONPROC)
    LOAD_OPENGL_FUN(glGetUniformLocation, FF_PFNGLGETUNIFORMLOCATIONPROC)
    LOAD_OPENGL_FUN(glUniform1f, FF_PFNGLUNIFORM1FPROC)
    LOAD_OPENGL_FUN(glUniform1i, FF_PFNGLUNIFORM1IPROC)
    LOAD_OPENGL_FUN(glUniformMatrix4fv, FF_PFNGLUNIFORMMATRIX4FVPROC)
    LOAD_OPENGL_FUN(glCreateProgram, FF_PFNGLCREATEPROGRAMPROC)
    LOAD_OPENGL_FUN(glDeleteProgram, FF_PFNGLDELETEPROGRAMPROC)
    LOAD_OPENGL_FUN(glUseProgram, FF_PFNGLUSEPROGRAMPROC)
    LOAD_OPENGL_FUN(glLinkProgram, FF_PFNGLLINKPROGRAMPROC)
    LOAD_OPENGL_FUN(glGetProgramiv, FF_PFNGLGETPROGRAMIVPROC)
    LOAD_OPENGL_FUN(glGetProgramInfoLog, FF_PFNGLGETPROGRAMINFOLOGPROC)
    LOAD_OPENGL_FUN(glAttachShader, FF_PFNGLATTACHSHADERPROC)
    LOAD_OPENGL_FUN(glCreateShader, FF_PFNGLCREATESHADERPROC)
    LOAD_OPENGL_FUN(glDeleteShader, FF_PFNGLDELETESHADERPROC)
    LOAD_OPENGL_FUN(glCompileShader, FF_PFNGLCOMPILESHADERPROC)
    LOAD_OPENGL_FUN(glShaderSource, FF_PFNGLSHADERSOURCEPROC)
    LOAD_OPENGL_FUN(glGetShaderiv, FF_PFNGLGETSHADERIVPROC)
    LOAD_OPENGL_FUN(glGetShaderInfoLog, FF_PFNGLGETSHADERINFOLOGPROC)
    LOAD_OPENGL_FUN(glEnableVertexAttribArray, FF_PFNGLENABLEVERTEXATTRIBARRAYPROC)
    LOAD_OPENGL_FUN(glVertexAttribPointer, FF_PFNGLVERTEXATTRIBPOINTERPROC)

    return 0;

#undef SelectedGetProcAddress
#undef LOAD_OPENGL_FUN
}
#endif

static void opengl_make_identity(float matrix[16])
{
    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = matrix[5] = matrix[10] = matrix[15] = 1.0f;
}

static void opengl_make_ortho(float matrix[16], float left, float right,
                              float bottom, float top, float nearZ, float farZ)
{
    float ral = right + left;
    float rsl = right - left;
    float tab = top + bottom;
    float tsb = top - bottom;
    float fan = farZ + nearZ;
    float fsn = farZ - nearZ;

    memset(matrix, 0, 16 * sizeof(float));
    matrix[0] = 2.0f / rsl;
    matrix[5] = 2.0f / tsb;
    matrix[10] = -2.0f / fsn;
    matrix[12] = -ral / rsl;
    matrix[13] = -tab / tsb;
    matrix[14] = -fan / fsn;
    matrix[15] = 1.0f;
}

static av_cold int opengl_read_limits(OpenGLContext *opengl)
{
    static const struct{
        const char *extension;
        int major;
        int minor;
    } required_extensions[] = {
        { "GL_ARB_multitexture",         1, 3 },
        { "GL_ARB_vertex_buffer_object", 1, 5 }, //GLX_ARB_vertex_buffer_object
        { "GL_ARB_vertex_shader",        2, 0 },
        { "GL_ARB_fragment_shader",      2, 0 },
        { "GL_ARB_shader_objects",       2, 0 },
        { NULL,                          0, 0 }
    };
    int i, major, minor;
    const char *extensions, *version;

    version = glGetString(GL_VERSION);
    extensions = glGetString(GL_EXTENSIONS);

    av_log(opengl, AV_LOG_DEBUG, "OpenGL version: %s\n", version);
    sscanf(version, "%d.%d", &major, &minor);

    for (i = 0; required_extensions[i].extension; i++) {
        if (major < required_extensions[i].major &&
            (major == required_extensions[i].major && minor < required_extensions[i].minor) &&
            !strstr(extensions, required_extensions[i].extension)) {
            av_log(opengl, AV_LOG_ERROR, "Required extension %s is not supported.\n",
                   required_extensions[i].extension);
            av_log(opengl, AV_LOG_DEBUG, "Supported extensions are: %s\n", extensions);
            return AVERROR(ENOSYS);
        }
    }
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &opengl->max_texture_size);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, &opengl->max_viewport_width);
    opengl->non_pow_2_textures = major >= 2 || strstr(extensions, "GL_ARB_texture_non_power_of_two");
#if defined(GL_ES_VERSION_2_0)
    opengl->unpack_subimage = !!strstr(extensions, "GL_EXT_unpack_subimage");
#else
    opengl->unpack_subimage = 1;
#endif

    av_log(opengl, AV_LOG_DEBUG, "Non Power of 2 textures support: %s\n", opengl->non_pow_2_textures ? "Yes" : "No");
    av_log(opengl, AV_LOG_DEBUG, "Unpack Subimage extension support: %s\n", opengl->unpack_subimage ? "Yes" : "No");
    av_log(opengl, AV_LOG_DEBUG, "Max texture size: %dx%d\n", opengl->max_texture_size, opengl->max_texture_size);
    av_log(opengl, AV_LOG_DEBUG, "Max viewport size: %dx%d\n",
           opengl->max_viewport_width, opengl->max_viewport_height);

    OPENGL_ERROR_CHECK(opengl);
    return 0;
  fail:
    return AVERROR_EXTERNAL;
}

static const char* opengl_get_fragment_shader_code(enum AVPixelFormat format)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(opengl_format_desc); i++) {
        if (opengl_format_desc[i].fixel_format == format)
            return *opengl_format_desc[i].fragment_shader;
    }
    return NULL;
}

static int opengl_type_size(GLenum type)
{
    switch(type) {
    case GL_UNSIGNED_SHORT:
    case FF_GL_UNSIGNED_SHORT_1_5_5_5_REV:
    case GL_UNSIGNED_SHORT_5_6_5:
        return 2;
    case GL_UNSIGNED_BYTE:
    case FF_GL_UNSIGNED_BYTE_3_3_2:
    case FF_GL_UNSIGNED_BYTE_2_3_3_REV:
    default:
        break;
    }
    return 1;
}

static av_cold void opengl_get_texture_params(OpenGLContext *opengl)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(opengl_format_desc); i++) {
        if (opengl_format_desc[i].fixel_format == opengl->pix_fmt) {
            opengl->format = opengl_format_desc[i].format;
            opengl->type = opengl_format_desc[i].type;
            break;
        }
    }
}

static void opengl_compute_display_area(AVFormatContext *s)
{
    AVRational sar, dar; /* sample and display aspect ratios */
    OpenGLContext *opengl = s->priv_data;
    AVStream *st = s->streams[0];
    AVCodecParameters *par = st->codecpar;

    /* compute overlay width and height from the codec context information */
    sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : (AVRational){ 1, 1 };
    dar = av_mul_q(sar, (AVRational){ par->width, par->height });

    /* we suppose the screen has a 1/1 sample aspect ratio */
    /* fit in the window */
    if (av_cmp_q(dar, (AVRational){ opengl->window_width, opengl->window_height }) > 0) {
        /* fit in width */
        opengl->picture_width = opengl->window_width;
        opengl->picture_height = av_rescale(opengl->picture_width, dar.den, dar.num);
    } else {
        /* fit in height */
        opengl->picture_height = opengl->window_height;
        opengl->picture_width = av_rescale(opengl->picture_height, dar.num, dar.den);
    }
}

static av_cold void opengl_get_texture_size(OpenGLContext *opengl, int in_width, int in_height,
                                            int *out_width, int *out_height)
{
    if (opengl->non_pow_2_textures) {
        *out_width = in_width;
        *out_height = in_height;
    } else {
        int max = FFMIN(FFMAX(in_width, in_height), opengl->max_texture_size);
        unsigned power_of_2 = 1;
        while (power_of_2 < max)
            power_of_2 *= 2;
        *out_height = power_of_2;
        *out_width = power_of_2;
        av_log(opengl, AV_LOG_DEBUG, "Texture size calculated from %dx%d into %dx%d\n",
               in_width, in_height, *out_width, *out_height);
    }
}

static av_cold void opengl_fill_color_map(OpenGLContext *opengl)
{
    const AVPixFmtDescriptor *desc;
    int shift;
    enum AVPixelFormat pix_fmt = opengl->pix_fmt;

    /* We need order of components, not exact position, some minor HACKs here */
    if (pix_fmt == AV_PIX_FMT_RGB565 || pix_fmt == AV_PIX_FMT_BGR555 ||
        pix_fmt == AV_PIX_FMT_BGR8   || pix_fmt == AV_PIX_FMT_RGB8)
        pix_fmt = AV_PIX_FMT_RGB24;
    else if (pix_fmt == AV_PIX_FMT_BGR565 || pix_fmt == AV_PIX_FMT_RGB555)
        pix_fmt = AV_PIX_FMT_BGR24;

    desc = av_pix_fmt_desc_get(pix_fmt);
    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB))
        return;

#define FILL_COMPONENT(i) { \
        shift = (desc->comp[i].depth - 1) >> 3; \
        opengl->color_map[(i << 2) + (desc->comp[i].offset >> shift)] = 1.0; \
    }

    memset(opengl->color_map, 0, sizeof(opengl->color_map));
    FILL_COMPONENT(0);
    FILL_COMPONENT(1);
    FILL_COMPONENT(2);
    if (desc->flags & AV_PIX_FMT_FLAG_ALPHA)
        FILL_COMPONENT(3);

#undef FILL_COMPONENT
}

static av_cold GLuint opengl_load_shader(OpenGLContext *opengl, GLenum type, const char *source)
{
    GLuint shader = opengl->glprocs.glCreateShader(type);
    GLint result;
    if (!shader) {
        av_log(opengl, AV_LOG_ERROR, "glCreateShader() failed\n");
        return 0;
    }
    opengl->glprocs.glShaderSource(shader, 1, &source, NULL);
    opengl->glprocs.glCompileShader(shader);

    opengl->glprocs.glGetShaderiv(shader, FF_GL_COMPILE_STATUS, &result);
    if (!result) {
        char *log;
        opengl->glprocs.glGetShaderiv(shader, FF_GL_INFO_LOG_LENGTH, &result);
        if (result) {
            if ((log = av_malloc(result))) {
                opengl->glprocs.glGetShaderInfoLog(shader, result, NULL, log);
                av_log(opengl, AV_LOG_ERROR, "Compile error: %s\n", log);
                av_free(log);
            }
        }
        goto fail;
    }
    OPENGL_ERROR_CHECK(opengl);
    return shader;
  fail:
    opengl->glprocs.glDeleteShader(shader);
    return 0;
}

static av_cold int opengl_compile_shaders(OpenGLContext *opengl, enum AVPixelFormat pix_fmt)
{
    GLint result;
    const char *fragment_shader_code = opengl_get_fragment_shader_code(pix_fmt);

    if (!fragment_shader_code) {
        av_log(opengl, AV_LOG_ERROR, "Provided pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(pix_fmt));
        return AVERROR(EINVAL);
    }

    opengl->vertex_shader = opengl_load_shader(opengl, FF_GL_VERTEX_SHADER,
                                               FF_OPENGL_VERTEX_SHADER);
    if (!opengl->vertex_shader) {
        av_log(opengl, AV_LOG_ERROR, "Vertex shader loading failed.\n");
        goto fail;
    }
    opengl->fragment_shader = opengl_load_shader(opengl, FF_GL_FRAGMENT_SHADER,
                                                 fragment_shader_code);
    if (!opengl->fragment_shader) {
        av_log(opengl, AV_LOG_ERROR, "Fragment shader loading failed.\n");
        goto fail;
    }

    opengl->program = opengl->glprocs.glCreateProgram();
    if (!opengl->program)
        goto fail;

    opengl->glprocs.glAttachShader(opengl->program, opengl->vertex_shader);
    opengl->glprocs.glAttachShader(opengl->program, opengl->fragment_shader);
    opengl->glprocs.glLinkProgram(opengl->program);

    opengl->glprocs.glGetProgramiv(opengl->program, FF_GL_LINK_STATUS, &result);
    if (!result) {
        char *log;
        opengl->glprocs.glGetProgramiv(opengl->program, FF_GL_INFO_LOG_LENGTH, &result);
        if (result) {
            log = av_malloc(result);
            if (!log)
                goto fail;
            opengl->glprocs.glGetProgramInfoLog(opengl->program, result, NULL, log);
            av_log(opengl, AV_LOG_ERROR, "Link error: %s\n", log);
            av_free(log);
        }
        goto fail;
    }

    opengl->position_attrib = opengl->glprocs.glGetAttribLocation(opengl->program, "a_position");
    opengl->texture_coords_attrib = opengl->glprocs.glGetAttribLocation(opengl->program, "a_textureCoords");
    opengl->projection_matrix_location = opengl->glprocs.glGetUniformLocation(opengl->program, "u_projectionMatrix");
    opengl->model_view_matrix_location = opengl->glprocs.glGetUniformLocation(opengl->program, "u_modelViewMatrix");
    opengl->color_map_location = opengl->glprocs.glGetUniformLocation(opengl->program, "u_colorMap");
    opengl->texture_location[0] = opengl->glprocs.glGetUniformLocation(opengl->program, "u_texture0");
    opengl->texture_location[1] = opengl->glprocs.glGetUniformLocation(opengl->program, "u_texture1");
    opengl->texture_location[2] = opengl->glprocs.glGetUniformLocation(opengl->program, "u_texture2");
    opengl->texture_location[3] = opengl->glprocs.glGetUniformLocation(opengl->program, "u_texture3");
    opengl->chroma_div_w_location = opengl->glprocs.glGetUniformLocation(opengl->program, "u_chroma_div_w");
    opengl->chroma_div_h_location = opengl->glprocs.glGetUniformLocation(opengl->program, "u_chroma_div_h");

    OPENGL_ERROR_CHECK(opengl);
    return 0;
  fail:
    opengl->glprocs.glDeleteShader(opengl->vertex_shader);
    opengl->glprocs.glDeleteShader(opengl->fragment_shader);
    opengl->glprocs.glDeleteProgram(opengl->program);
    opengl->fragment_shader = opengl->vertex_shader = opengl->program = 0;
    return AVERROR_EXTERNAL;
}

static av_cold int opengl_configure_texture(OpenGLContext *opengl, GLuint texture,
                                            GLsizei width, GLsizei height)
{
    if (texture) {
        int new_width, new_height;
        opengl_get_texture_size(opengl, width, height, &new_width, &new_height);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, opengl->format, new_width, new_height, 0,
                     opengl->format, opengl->type, NULL);
        OPENGL_ERROR_CHECK(NULL);
    }
    return 0;
  fail:
    return AVERROR_EXTERNAL;
}

static av_cold int opengl_prepare_vertex(AVFormatContext *s)
{
    OpenGLContext *opengl = s->priv_data;
    int tex_w, tex_h;

    if (opengl->window_width > opengl->max_viewport_width || opengl->window_height > opengl->max_viewport_height) {
        opengl->window_width = FFMIN(opengl->window_width, opengl->max_viewport_width);
        opengl->window_height = FFMIN(opengl->window_height, opengl->max_viewport_height);
        av_log(opengl, AV_LOG_WARNING, "Too big viewport requested, limited to %dx%d", opengl->window_width, opengl->window_height);
    }
    glViewport(0, 0, opengl->window_width, opengl->window_height);
    opengl_make_ortho(opengl->projection_matrix,
                      - (float)opengl->window_width  / 2.0f, (float)opengl->window_width  / 2.0f,
                      - (float)opengl->window_height / 2.0f, (float)opengl->window_height / 2.0f,
                      1.0f, -1.0f);
    opengl_make_identity(opengl->model_view_matrix);

    opengl_compute_display_area(s);

    opengl->vertex[0].z = opengl->vertex[1].z = opengl->vertex[2].z = opengl->vertex[3].z = 0.0f;
    opengl->vertex[0].x = opengl->vertex[1].x = - (float)opengl->picture_width / 2.0f;
    opengl->vertex[2].x = opengl->vertex[3].x =   (float)opengl->picture_width / 2.0f;
    opengl->vertex[1].y = opengl->vertex[2].y = - (float)opengl->picture_height / 2.0f;
    opengl->vertex[0].y = opengl->vertex[3].y =   (float)opengl->picture_height / 2.0f;

    opengl_get_texture_size(opengl, opengl->width, opengl->height, &tex_w, &tex_h);

    opengl->vertex[0].s0 = 0.0f;
    opengl->vertex[0].t0 = 0.0f;
    opengl->vertex[1].s0 = 0.0f;
    opengl->vertex[1].t0 = (float)opengl->height / (float)tex_h;
    opengl->vertex[2].s0 = (float)opengl->width  / (float)tex_w;
    opengl->vertex[2].t0 = (float)opengl->height / (float)tex_h;
    opengl->vertex[3].s0 = (float)opengl->width  / (float)tex_w;
    opengl->vertex[3].t0 = 0.0f;

    opengl->glprocs.glBindBuffer(FF_GL_ARRAY_BUFFER, opengl->vertex_buffer);
    opengl->glprocs.glBufferData(FF_GL_ARRAY_BUFFER, sizeof(opengl->vertex), opengl->vertex, FF_GL_STATIC_DRAW);
    opengl->glprocs.glBindBuffer(FF_GL_ARRAY_BUFFER, 0);
    OPENGL_ERROR_CHECK(opengl);
    return 0;
  fail:
    return AVERROR_EXTERNAL;
}

static int opengl_prepare(OpenGLContext *opengl)
{
    int i;
    opengl->glprocs.glUseProgram(opengl->program);
    opengl->glprocs.glUniformMatrix4fv(opengl->projection_matrix_location, 1, GL_FALSE, opengl->projection_matrix);
    opengl->glprocs.glUniformMatrix4fv(opengl->model_view_matrix_location, 1, GL_FALSE, opengl->model_view_matrix);
    for (i = 0; i < 4; i++)
        if (opengl->texture_location[i] != -1) {
            opengl->glprocs.glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, opengl->texture_name[i]);
            opengl->glprocs.glUniform1i(opengl->texture_location[i], i);
        }
    if (opengl->color_map_location != -1)
        opengl->glprocs.glUniformMatrix4fv(opengl->color_map_location, 1, GL_FALSE, opengl->color_map);
    if (opengl->chroma_div_h_location != -1)
        opengl->glprocs.glUniform1f(opengl->chroma_div_h_location, opengl->chroma_div_h);
    if (opengl->chroma_div_w_location != -1)
        opengl->glprocs.glUniform1f(opengl->chroma_div_w_location, opengl->chroma_div_w);

    OPENGL_ERROR_CHECK(opengl);
    return 0;
  fail:
    return AVERROR_EXTERNAL;
}

static int opengl_create_window(AVFormatContext *h)
{
    OpenGLContext *opengl = h->priv_data;
    int ret;

    if (!opengl->no_window) {
#if CONFIG_SDL2
        if ((ret = opengl_sdl_create_window(h)) < 0) {
            av_log(opengl, AV_LOG_ERROR, "Cannot create default SDL window.\n");
            return ret;
        }
#else
        av_log(opengl, AV_LOG_ERROR, "FFmpeg is compiled without SDL. Cannot create default window.\n");
        return AVERROR(ENOSYS);
#endif
    } else {
        AVDeviceRect message;
        message.x = message.y = 0;
        message.width = opengl->window_width;
        message.height = opengl->window_height;
        if ((ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_CREATE_WINDOW_BUFFER,
                                                       &message , sizeof(message))) < 0) {
            av_log(opengl, AV_LOG_ERROR, "Application failed to create window buffer.\n");
            return ret;
        }
        if ((ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_PREPARE_WINDOW_BUFFER, NULL , 0)) < 0) {
            av_log(opengl, AV_LOG_ERROR, "Application failed to prepare window buffer.\n");
            return ret;
        }
    }
    return 0;
}

static int opengl_release_window(AVFormatContext *h)
{
    int ret;
    OpenGLContext *opengl = h->priv_data;
    if (!opengl->no_window) {
#if CONFIG_SDL2
        SDL_GL_DeleteContext(opengl->glcontext);
        SDL_DestroyWindow(opengl->window);
        SDL_Quit();
#endif
    } else if ((ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_DESTROY_WINDOW_BUFFER, NULL , 0)) < 0) {
        av_log(opengl, AV_LOG_ERROR, "Application failed to release window buffer.\n");
        return ret;
    }
    return 0;
}

static av_cold int opengl_write_trailer(AVFormatContext *h)
{
    OpenGLContext *opengl = h->priv_data;

    if (opengl->no_window &&
        avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_PREPARE_WINDOW_BUFFER, NULL , 0) < 0)
        av_log(opengl, AV_LOG_ERROR, "Application failed to prepare window buffer.\n");

    opengl_deinit_context(opengl);
    opengl_release_window(h);

    return 0;
}

static av_cold int opengl_init_context(OpenGLContext *opengl)
{
    int i, ret;
    const AVPixFmtDescriptor *desc;

    if ((ret = opengl_compile_shaders(opengl, opengl->pix_fmt)) < 0)
        goto fail;

    desc = av_pix_fmt_desc_get(opengl->pix_fmt);
    av_assert0(desc->nb_components > 0 && desc->nb_components <= 4);
    glGenTextures(desc->nb_components, opengl->texture_name);

    opengl->glprocs.glGenBuffers(2, &opengl->index_buffer);
    if (!opengl->index_buffer || !opengl->vertex_buffer) {
        av_log(opengl, AV_LOG_ERROR, "Buffer generation failed.\n");
        ret = AVERROR_EXTERNAL;
        goto fail;
    }

    opengl_configure_texture(opengl, opengl->texture_name[0], opengl->width, opengl->height);
    if (desc->nb_components > 1) {
        int has_alpha = desc->flags & AV_PIX_FMT_FLAG_ALPHA;
        int num_planes = desc->nb_components - (has_alpha ? 1 : 0);
        if (opengl->non_pow_2_textures) {
            opengl->chroma_div_w = 1.0f;
            opengl->chroma_div_h = 1.0f;
        } else {
            opengl->chroma_div_w = 1 << desc->log2_chroma_w;
            opengl->chroma_div_h = 1 << desc->log2_chroma_h;
        }
        for (i = 1; i < num_planes; i++)
            if (opengl->non_pow_2_textures)
                opengl_configure_texture(opengl, opengl->texture_name[i],
                        AV_CEIL_RSHIFT(opengl->width, desc->log2_chroma_w),
                        AV_CEIL_RSHIFT(opengl->height, desc->log2_chroma_h));
            else
                opengl_configure_texture(opengl, opengl->texture_name[i], opengl->width, opengl->height);
        if (has_alpha)
            opengl_configure_texture(opengl, opengl->texture_name[3], opengl->width, opengl->height);
    }

    opengl->glprocs.glBindBuffer(FF_GL_ELEMENT_ARRAY_BUFFER, opengl->index_buffer);
    opengl->glprocs.glBufferData(FF_GL_ELEMENT_ARRAY_BUFFER, sizeof(g_index), g_index, FF_GL_STATIC_DRAW);
    opengl->glprocs.glBindBuffer(FF_GL_ELEMENT_ARRAY_BUFFER, 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor((float)opengl->background[0] / 255.0f, (float)opengl->background[1] / 255.0f,
                 (float)opengl->background[2] / 255.0f, 1.0f);

    ret = AVERROR_EXTERNAL;
    OPENGL_ERROR_CHECK(opengl);

    return 0;
  fail:
    return ret;
}

static av_cold int opengl_write_header(AVFormatContext *h)
{
    OpenGLContext *opengl = h->priv_data;
    AVStream *st;
    int ret;

    if (h->nb_streams != 1 ||
        h->streams[0]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        h->streams[0]->codecpar->codec_id != AV_CODEC_ID_RAWVIDEO) {
        av_log(opengl, AV_LOG_ERROR, "Only a single video stream is supported.\n");
        return AVERROR(EINVAL);
    }
    st = h->streams[0];
    opengl->width = st->codecpar->width;
    opengl->height = st->codecpar->height;
    opengl->pix_fmt = st->codecpar->format;
    if (!opengl->window_width)
        opengl->window_width = opengl->width;
    if (!opengl->window_height)
        opengl->window_height = opengl->height;

    if (!opengl->window_title && !opengl->no_window)
        opengl->window_title = av_strdup(h->url);

    if ((ret = opengl_create_window(h)))
        goto fail;

    if ((ret = opengl_read_limits(opengl)) < 0)
        goto fail;

    if (opengl->width > opengl->max_texture_size || opengl->height > opengl->max_texture_size) {
        av_log(opengl, AV_LOG_ERROR, "Too big picture %dx%d, max supported size is %dx%d\n",
               opengl->width, opengl->height, opengl->max_texture_size, opengl->max_texture_size);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if ((ret = opengl_load_procedures(opengl)) < 0)
        goto fail;

    opengl_fill_color_map(opengl);
    opengl_get_texture_params(opengl);

    if ((ret = opengl_init_context(opengl)) < 0)
        goto fail;

    if ((ret = opengl_prepare_vertex(h)) < 0)
        goto fail;

    glClear(GL_COLOR_BUFFER_BIT);

#if CONFIG_SDL2
    if (!opengl->no_window)
        SDL_GL_SwapWindow(opengl->window);
#endif
    if (opengl->no_window &&
        (ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_DISPLAY_WINDOW_BUFFER, NULL , 0)) < 0) {
        av_log(opengl, AV_LOG_ERROR, "Application failed to display window buffer.\n");
        goto fail;
    }

    ret = AVERROR_EXTERNAL;
    OPENGL_ERROR_CHECK(opengl);

    opengl->inited = 1;
    return 0;

  fail:
    opengl_write_trailer(h);
    return ret;
}

static uint8_t* opengl_get_plane_pointer(OpenGLContext *opengl, AVPacket *pkt, int comp_index,
                                         const AVPixFmtDescriptor *desc)
{
    uint8_t *data = pkt->data;
    int wordsize = opengl_type_size(opengl->type);
    int width_chroma = AV_CEIL_RSHIFT(opengl->width, desc->log2_chroma_w);
    int height_chroma = AV_CEIL_RSHIFT(opengl->height, desc->log2_chroma_h);
    int plane = desc->comp[comp_index].plane;

    switch(plane) {
    case 0:
        break;
    case 1:
        data += opengl->width * opengl->height * wordsize;
        break;
    case 2:
        data += opengl->width * opengl->height * wordsize;
        data += width_chroma * height_chroma * wordsize;
        break;
    case 3:
        data += opengl->width * opengl->height * wordsize;
        data += 2 * width_chroma * height_chroma * wordsize;
        break;
    default:
        return NULL;
    }
    return data;
}

#define LOAD_TEXTURE_DATA(comp_index, sub)                                                  \
{                                                                                           \
    int width = sub ? AV_CEIL_RSHIFT(opengl->width, desc->log2_chroma_w) : opengl->width;   \
    int height = sub ? AV_CEIL_RSHIFT(opengl->height, desc->log2_chroma_h): opengl->height; \
    uint8_t *data;                                                                          \
    int plane = desc->comp[comp_index].plane;                                               \
                                                                                            \
    glBindTexture(GL_TEXTURE_2D, opengl->texture_name[comp_index]);                         \
    if (!is_pkt) {                                                                          \
        GLint length = ((AVFrame *)input)->linesize[plane];                                 \
        int bytes_per_pixel = opengl_type_size(opengl->type);                               \
        if (!(desc->flags & AV_PIX_FMT_FLAG_PLANAR))                                        \
            bytes_per_pixel *= desc->nb_components;                                         \
        data = ((AVFrame *)input)->data[plane];                                             \
        if (!(length % bytes_per_pixel) &&                                                  \
            (opengl->unpack_subimage || ((length / bytes_per_pixel) == width))) {           \
            length /= bytes_per_pixel;                                                      \
            if (length != width)                                                            \
                glPixelStorei(FF_GL_UNPACK_ROW_LENGTH, length);                             \
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,                          \
                            opengl->format, opengl->type, data);                            \
            if (length != width)                                                            \
                glPixelStorei(FF_GL_UNPACK_ROW_LENGTH, 0);                                  \
        } else {                                                                            \
            int h;                                                                          \
            for (h = 0; h < height; h++) {                                                  \
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, h, width, 1,                           \
                                opengl->format, opengl->type, data);                        \
                data += length;                                                             \
            }                                                                               \
        }                                                                                   \
    } else {                                                                                \
        data = opengl_get_plane_pointer(opengl, input, comp_index, desc);                   \
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,                              \
                        opengl->format, opengl->type, data);                                \
    }                                                                                       \
}

static int opengl_draw(AVFormatContext *h, void *input, int repaint, int is_pkt)
{
    OpenGLContext *opengl = h->priv_data;
    enum AVPixelFormat pix_fmt = h->streams[0]->codecpar->format;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);
    int ret;

#if CONFIG_SDL2
    if (!opengl->no_window && (ret = opengl_sdl_process_events(h)) < 0)
        goto fail;
#endif
    if (opengl->no_window &&
        (ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_PREPARE_WINDOW_BUFFER, NULL , 0)) < 0) {
        av_log(opengl, AV_LOG_ERROR, "Application failed to prepare window buffer.\n");
        goto fail;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    if (!repaint) {
        if (is_pkt)
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        LOAD_TEXTURE_DATA(0, 0)
        if (desc->flags & AV_PIX_FMT_FLAG_PLANAR) {
            LOAD_TEXTURE_DATA(1, 1)
            LOAD_TEXTURE_DATA(2, 1)
            if (desc->flags & AV_PIX_FMT_FLAG_ALPHA)
                LOAD_TEXTURE_DATA(3, 0)
        }
    }
    ret = AVERROR_EXTERNAL;
    OPENGL_ERROR_CHECK(opengl);

    if ((ret = opengl_prepare(opengl)) < 0)
        goto fail;

    opengl->glprocs.glBindBuffer(FF_GL_ARRAY_BUFFER, opengl->vertex_buffer);
    opengl->glprocs.glBindBuffer(FF_GL_ELEMENT_ARRAY_BUFFER, opengl->index_buffer);
    opengl->glprocs.glVertexAttribPointer(opengl->position_attrib, 3, GL_FLOAT, GL_FALSE, sizeof(OpenGLVertexInfo), 0);
    opengl->glprocs.glEnableVertexAttribArray(opengl->position_attrib);
    opengl->glprocs.glVertexAttribPointer(opengl->texture_coords_attrib, 2, GL_FLOAT, GL_FALSE, sizeof(OpenGLVertexInfo), 12);
    opengl->glprocs.glEnableVertexAttribArray(opengl->texture_coords_attrib);

    glDrawElements(GL_TRIANGLES, FF_ARRAY_ELEMS(g_index), GL_UNSIGNED_SHORT, 0);

    ret = AVERROR_EXTERNAL;
    OPENGL_ERROR_CHECK(opengl);

#if CONFIG_SDL2
    if (!opengl->no_window)
        SDL_GL_SwapWindow(opengl->window);
#endif
    if (opengl->no_window &&
        (ret = avdevice_dev_to_app_control_message(h, AV_DEV_TO_APP_DISPLAY_WINDOW_BUFFER, NULL , 0)) < 0) {
        av_log(opengl, AV_LOG_ERROR, "Application failed to display window buffer.\n");
        goto fail;
    }

    return 0;
  fail:
    return ret;
}

static int opengl_write_packet(AVFormatContext *h, AVPacket *pkt)
{
    return opengl_draw(h, pkt, 0, 1);
}

static int opengl_write_frame(AVFormatContext *h, int stream_index,
                              AVFrame **frame, unsigned flags)
{
    if ((flags & AV_WRITE_UNCODED_FRAME_QUERY))
        return 0;
    return opengl_draw(h, *frame, 0, 0);
}

#define OFFSET(x) offsetof(OpenGLContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "background",   "set background color",   OFFSET(background),   AV_OPT_TYPE_COLOR,  {.str = "black"}, CHAR_MIN, CHAR_MAX, ENC },
    { "no_window",    "disable default window", OFFSET(no_window),    AV_OPT_TYPE_INT,    {.i64 = 0}, INT_MIN, INT_MAX, ENC },
    { "window_title", "set window title",       OFFSET(window_title), AV_OPT_TYPE_STRING, {.str = NULL}, 0, 0, ENC },
    { "window_size",  "set window size",        OFFSET(window_width), AV_OPT_TYPE_IMAGE_SIZE, {.str = NULL}, 0, 0, ENC },
    { NULL }
};

static const AVClass opengl_class = {
    .class_name = "opengl outdev",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEVICE_VIDEO_OUTPUT,
};

AVOutputFormat ff_opengl_muxer = {
    .name           = "opengl",
    .long_name      = NULL_IF_CONFIG_SMALL("OpenGL output"),
    .priv_data_size = sizeof(OpenGLContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_RAWVIDEO,
    .write_header   = opengl_write_header,
    .write_packet   = opengl_write_packet,
    .write_uncoded_frame = opengl_write_frame,
    .write_trailer  = opengl_write_trailer,
    .control_message = opengl_control_message,
    .flags          = AVFMT_NOFILE | AVFMT_VARIABLE_FPS | AVFMT_NOTIMESTAMPS,
    .priv_class     = &opengl_class,
};
