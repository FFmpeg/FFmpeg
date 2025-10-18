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

/**
 * @file
 *
 * drawvg filter, draw vector graphics with cairo.
 *
 * This file contains the parser and the interpreter for VGS, and the
 * AVClass definitions for the drawvg filter.
 */

#include <cairo.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/bswap.h"
#include "libavutil/eval.h"
#include "libavutil/internal.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/sfc64.h"

#include "avfilter.h"
#include "filters.h"
#include "textutils.h"
#include "video.h"

/*
 * == AVExpr Integration ==
 *
 * Definitions to use variables and functions in the expressions from
 * `av_expr_*` functions.
 *
 * For user-variables, created with commands like `setvar` or `defhsla`,
 * the VGS parser updates a copy of the `vgs_default_vars` array. The
 * first user-variable is stored in the slot for `VAR_U0`.
 */

enum {
    VAR_N,          ///< Frame number.
    VAR_T,          ///< Timestamp in seconds.
    VAR_TS,         ///< Time in seconds of the first frame.
    VAR_W,          ///< Frame width.
    VAR_H,          ///< Frame height.
    VAR_DURATION,   ///< Frame duration.
    VAR_CX,         ///< X coordinate for current point.
    VAR_CY,         ///< Y coordinate for current point.
    VAR_I,          ///< Loop counter, to use with `repeat {}`.
    VAR_U0,         ///< User variables.
};

/// Number of user variables that can be created with `setvar`.
///
/// It is possible to allow any number of variables, but this
/// approach simplifies the implementation, and 20 variables
/// is more than enough for the expected use of this filter.
#define USER_VAR_COUNT 20

/// Total number of variables (default- and user-variables).
#define VAR_COUNT (VAR_U0 + USER_VAR_COUNT)

static const char *const vgs_default_vars[] = {
    "n",
    "t",
    "ts",
    "w",
    "h",
    "duration",
    "cx",
    "cy",
    "i",
    NULL, // User variables. Name is assigned by commands like `setvar`.
};

// Functions used in expressions.

static const char *const vgs_func1_names[] = {
    "pathlen",
    "randomg",
    NULL,
};

static double vgs_fn_pathlen(void *, double);
static double vgs_fn_randomg(void *, double);

static double (*const vgs_func1_impls[])(void *, double) = {
    vgs_fn_pathlen,
    vgs_fn_randomg,
    NULL,
};

static const char *const vgs_func2_names[] = {
    "p",
    NULL,
};

static double vgs_fn_p(void *, double, double);

static double (*const vgs_func2_impls[])(void *, double, double) = {
    vgs_fn_p,
    NULL,
};

/*
 * == Command Declarations ==
 *
 * Each command is defined by an opcode (used later by the interpreter), a name,
 * and a set of parameters.
 *
 * Inspired by SVG, some commands can be repeated when the next token after the
 * last parameter is a numeric value (for example, `L 1 2 3 4` is equivalent to
 * `L 1 2 L 3 4`). In these commands, the last parameter is `PARAM_MAY_REPEAT`.
 */

enum VGSCommand {
    CMD_ARC = 1,                ///<  arc (cx cy radius angle1 angle2)
    CMD_ARC_NEG,                ///<  arcn (cx cy radius angle1 angle2)
    CMD_BREAK,                  ///<  break
    CMD_CIRCLE,                 ///<  circle (cx cy radius)
    CMD_CLIP,                   ///<  clip
    CMD_CLIP_EO,                ///<  eoclip
    CMD_CLOSE_PATH,             ///<  Z, z, closepath
    CMD_COLOR_STOP,             ///<  colorstop (offset color)
    CMD_CURVE_TO,               ///<  C, curveto (x1 y1 x2 y2 x y)
    CMD_DEF_HSLA,               ///<  defhsla (varname h s l a)
    CMD_DEF_RGBA,               ///<  defrgba (varname r g b a)
    CMD_CURVE_TO_REL,           ///<  c, rcurveto (dx1 dy1 dx2 dy2 dx dy)
    CMD_ELLIPSE,                ///<  ellipse (cx cy rx ry)
    CMD_FILL,                   ///<  fill
    CMD_FILL_EO,                ///<  eofill
    CMD_GET_METADATA,           ///<  getmetadata varname key
    CMD_HORZ,                   ///<  H (x)
    CMD_HORZ_REL,               ///<  h (dx)
    CMD_IF,                     ///<  if (condition) { subprogram }
    CMD_LINEAR_GRAD,            ///<  lineargrad (x0 y0 x1 y1)
    CMD_LINE_TO,                ///<  L, lineto (x y)
    CMD_LINE_TO_REL,            ///<  l, rlineto (dx dy)
    CMD_MOVE_TO,                ///<  M, moveto (x y)
    CMD_MOVE_TO_REL,            ///<  m, rmoveto (dx dy)
    CMD_NEW_PATH,               ///<  newpath
    CMD_PRESERVE,               ///<  preserve
    CMD_PRINT,                  ///<  print (expr)*
    CMD_PROC_ASSIGN,            ///<  proc name varnames* { subprogram }
    CMD_PROC_CALL,              ///<  call name (expr)*
    CMD_Q_CURVE_TO,             ///<  Q (x1 y1 x y)
    CMD_Q_CURVE_TO_REL,         ///<  q (dx1 dy1 dx dy)
    CMD_RADIAL_GRAD,            ///<  radialgrad (cx0 cy0 radius0 cx1 cy1 radius1)
    CMD_RECT,                   ///<  rect (x y width height)
    CMD_REPEAT,                 ///<  repeat (count) { subprogram }
    CMD_RESET_CLIP,             ///<  resetclip
    CMD_RESET_DASH,             ///<  resetdash
    CMD_RESET_MATRIX,           ///<  resetmatrix
    CMD_RESTORE,                ///<  restore
    CMD_ROTATE,                 ///<  rotate (angle)
    CMD_ROUNDEDRECT,            ///<  roundedrect (x y width height radius)
    CMD_SAVE,                   ///<  save
    CMD_SCALE,                  ///<  scale (s)
    CMD_SCALEXY,                ///<  scalexy (sx sy)
    CMD_SET_COLOR,              ///<  setcolor (color)
    CMD_SET_DASH,               ///<  setdash (length)
    CMD_SET_DASH_OFFSET,        ///<  setdashoffset (offset)
    CMD_SET_HSLA,               ///<  sethsla (h s l a)
    CMD_SET_LINE_CAP,           ///<  setlinecap (cap)
    CMD_SET_LINE_JOIN,          ///<  setlinejoin (join)
    CMD_SET_LINE_WIDTH,         ///<  setlinewidth (width)
    CMD_SET_RGBA,               ///<  setrgba (r g b a)
    CMD_SET_VAR,                ///<  setvar (varname value)
    CMD_STROKE,                 ///<  stroke
    CMD_S_CURVE_TO,             ///<  S (x2 y2 x y)
    CMD_S_CURVE_TO_REL,         ///<  s (dx2 dy2 dx dy)
    CMD_TRANSLATE,              ///<  translate (tx ty)
    CMD_T_CURVE_TO,             ///<  T (x y)
    CMD_T_CURVE_TO_REL,         ///<  t (dx dy)
    CMD_VERT,                   ///<  V (y)
    CMD_VERT_REL,               ///<  v (dy)
};

/// Constants for some commands, like `setlinejoin`.
struct VGSConstant {
    const char* name;
    int value;
};

static const struct VGSConstant vgs_consts_line_cap[] = {
    { "butt", CAIRO_LINE_CAP_BUTT },
    { "round", CAIRO_LINE_CAP_ROUND },
    { "square", CAIRO_LINE_CAP_SQUARE },
    { NULL, 0 },
};

static const struct VGSConstant vgs_consts_line_join[] = {
    { "bevel", CAIRO_LINE_JOIN_BEVEL },
    { "miter", CAIRO_LINE_JOIN_MITER },
    { "round", CAIRO_LINE_JOIN_ROUND },
    { NULL, 0 },
};

struct VGSParameter {
    enum {
        PARAM_COLOR = 1,
        PARAM_CONSTANT,
        PARAM_END,
        PARAM_MAY_REPEAT,
        PARAM_NUMERIC,
        PARAM_NUMERIC_METADATA,
        PARAM_PROC_ARGS,
        PARAM_PROC_NAME,
        PARAM_PROC_PARAMS,
        PARAM_RAW_IDENT,
        PARAM_SUBPROGRAM,
        PARAM_VARIADIC,
        PARAM_VAR_NAME,
    } type;

    const struct VGSConstant *constants; ///< Array for PARAM_CONSTANT.
};

// Max number of parameters for a command.
#define MAX_COMMAND_PARAMS 8

// Max number of arguments when calling a procedure. Subtract 2 to
// `MAX_COMMAND_PARAMS` because the call to `proc` needs 2 arguments
// (the procedure name and its body). The rest can be variable names
// for the arguments.
#define MAX_PROC_ARGS (MAX_COMMAND_PARAMS - 2)

// Definition of each command.

struct VGSCommandSpec {
    const char* name;
    enum VGSCommand cmd;
    const struct VGSParameter *params;
};

// Parameter lists.
#define PARAMS(...) (const struct VGSParameter[]){ __VA_ARGS__ }
#define L(...) PARAMS(__VA_ARGS__, { PARAM_END })
#define R(...) PARAMS(__VA_ARGS__, { PARAM_MAY_REPEAT })
#define NONE   PARAMS({ PARAM_END })

// Common parameter types.
#define N { PARAM_NUMERIC }
#define V { PARAM_VAR_NAME }
#define P { PARAM_SUBPROGRAM }
#define C(c) { PARAM_CONSTANT, .constants = c }

// Declarations table.
//
// The array must be sorted by `name` in ascending order.
static const struct VGSCommandSpec vgs_commands[] = {
    { "C",              CMD_CURVE_TO,         R(N, N, N, N, N, N) },
    { "H",              CMD_HORZ,             R(N) },
    { "L",              CMD_LINE_TO,          R(N, N) },
    { "M",              CMD_MOVE_TO,          R(N, N) },
    { "Q",              CMD_Q_CURVE_TO,       R(N, N, N, N) },
    { "S",              CMD_S_CURVE_TO,       R(N, N, N, N) },
    { "T",              CMD_T_CURVE_TO,       R(N, N) },
    { "V",              CMD_VERT,             R(N) },
    { "Z",              CMD_CLOSE_PATH,       NONE },
    { "arc",            CMD_ARC,              R(N, N, N, N, N) },
    { "arcn",           CMD_ARC_NEG,          R(N, N, N, N, N) },
    { "break",          CMD_BREAK,            NONE },
    { "c",              CMD_CURVE_TO_REL,     R(N, N, N, N, N, N) },
    { "call",           CMD_PROC_CALL,        L({ PARAM_PROC_NAME }, { PARAM_PROC_ARGS }) },
    { "circle",         CMD_CIRCLE,           R(N, N, N) },
    { "clip",           CMD_CLIP,             NONE },
    { "closepath",      CMD_CLOSE_PATH,       NONE },
    { "colorstop",      CMD_COLOR_STOP,       R(N, { PARAM_COLOR }) },
    { "curveto",        CMD_CURVE_TO,         R(N, N, N, N, N, N) },
    { "defhsla",        CMD_DEF_HSLA,         L(V, N, N, N, N) },
    { "defrgba",        CMD_DEF_RGBA,         L(V, N, N, N, N) },
    { "ellipse",        CMD_ELLIPSE,          R(N, N, N, N) },
    { "eoclip",         CMD_CLIP_EO,          NONE },
    { "eofill",         CMD_FILL_EO,          NONE },
    { "fill",           CMD_FILL,             NONE },
    { "getmetadata",    CMD_GET_METADATA,     L(V, { PARAM_RAW_IDENT }) },
    { "h",              CMD_HORZ_REL,         R(N) },
    { "if",             CMD_IF,               L(N, P) },
    { "l",              CMD_LINE_TO_REL,      R(N, N) },
    { "lineargrad",     CMD_LINEAR_GRAD,      L(N, N, N, N) },
    { "lineto",         CMD_LINE_TO,          R(N, N) },
    { "m",              CMD_MOVE_TO_REL,      R(N, N) },
    { "moveto",         CMD_MOVE_TO,          R(N, N) },
    { "newpath",        CMD_NEW_PATH,         NONE },
    { "preserve",       CMD_PRESERVE,         NONE },
    { "print",          CMD_PRINT,            L({ PARAM_NUMERIC_METADATA }, { PARAM_VARIADIC }) },
    { "proc",           CMD_PROC_ASSIGN,      L({ PARAM_PROC_NAME }, { PARAM_PROC_PARAMS }, P) },
    { "q",              CMD_Q_CURVE_TO_REL,   R(N, N, N, N) },
    { "radialgrad",     CMD_RADIAL_GRAD,      L(N, N, N, N, N, N) },
    { "rcurveto",       CMD_CURVE_TO_REL,     R(N, N, N, N, N, N) },
    { "rect",           CMD_RECT,             R(N, N, N, N) },
    { "repeat",         CMD_REPEAT,           L(N, P) },
    { "resetclip",      CMD_RESET_CLIP,       NONE },
    { "resetdash",      CMD_RESET_DASH,       NONE },
    { "resetmatrix",    CMD_RESET_MATRIX,     NONE },
    { "restore",        CMD_RESTORE,          NONE },
    { "rlineto",        CMD_LINE_TO_REL,      R(N, N) },
    { "rmoveto",        CMD_MOVE_TO_REL,      R(N, N) },
    { "rotate",         CMD_ROTATE,           L(N) },
    { "roundedrect",    CMD_ROUNDEDRECT,      R(N, N, N, N, N) },
    { "s",              CMD_S_CURVE_TO_REL,   R(N, N, N, N) },
    { "save",           CMD_SAVE,             NONE },
    { "scale",          CMD_SCALE,            L(N) },
    { "scalexy",        CMD_SCALEXY,          L(N, N) },
    { "setcolor",       CMD_SET_COLOR,        L({ PARAM_COLOR }) },
    { "setdash",        CMD_SET_DASH,         R(N) },
    { "setdashoffset",  CMD_SET_DASH_OFFSET,  R(N) },
    { "sethsla",        CMD_SET_HSLA,         L(N, N, N, N) },
    { "setlinecap",     CMD_SET_LINE_CAP,     L(C(vgs_consts_line_cap)) },
    { "setlinejoin",    CMD_SET_LINE_JOIN,    L(C(vgs_consts_line_join)) },
    { "setlinewidth",   CMD_SET_LINE_WIDTH,   L(N) },
    { "setrgba",        CMD_SET_RGBA,         L(N, N, N, N) },
    { "setvar",         CMD_SET_VAR,          L(V, N) },
    { "stroke",         CMD_STROKE,           NONE },
    { "t",              CMD_T_CURVE_TO_REL,   R(N, N) },
    { "translate",      CMD_TRANSLATE,        L(N, N) },
    { "v",              CMD_VERT_REL,         R(N) },
    { "z",              CMD_CLOSE_PATH,       NONE },
};

#undef C
#undef L
#undef N
#undef NONE
#undef PARAMS
#undef R

/// Comparator for `VGSCommandDecl`, to be used with `bsearch(3)`.
static int vgs_comp_command_spec(const void *cs1, const void *cs2) {
    return strcmp(
        ((const struct VGSCommandSpec*)cs1)->name,
        ((const struct VGSCommandSpec*)cs2)->name
    );
}

/// Return the specs for the given command, or `NULL` if the name is not valid.
///
/// The implementation assumes that `vgs_commands` is sorted by `name`.
static const struct VGSCommandSpec* vgs_get_command(const char *name, size_t length) {
    char bufname[64];
    struct VGSCommandSpec key = { .name = bufname };

    if (length >= sizeof(bufname))
        return NULL;

    memcpy(bufname, name, length);
    bufname[length] = '\0';

    return bsearch(
        &key,
        vgs_commands,
        FF_ARRAY_ELEMS(vgs_commands),
        sizeof(vgs_commands[0]),
        vgs_comp_command_spec
    );
}

/// Return `1` if the command changes the current path in the cairo context.
static int vgs_cmd_change_path(enum VGSCommand cmd) {
    switch (cmd) {
    case CMD_BREAK:
    case CMD_COLOR_STOP:
    case CMD_DEF_HSLA:
    case CMD_DEF_RGBA:
    case CMD_GET_METADATA:
    case CMD_IF:
    case CMD_LINEAR_GRAD:
    case CMD_PRINT:
    case CMD_PROC_ASSIGN:
    case CMD_PROC_CALL:
    case CMD_RADIAL_GRAD:
    case CMD_REPEAT:
    case CMD_RESET_DASH:
    case CMD_RESET_MATRIX:
    case CMD_SET_COLOR:
    case CMD_SET_DASH:
    case CMD_SET_DASH_OFFSET:
    case CMD_SET_HSLA:
    case CMD_SET_LINE_CAP:
    case CMD_SET_LINE_JOIN:
    case CMD_SET_LINE_WIDTH:
    case CMD_SET_RGBA:
    case CMD_SET_VAR:
        return 0;

    default:
        return 1;
    }
}

/*
 * == VGS Parser ==
 *
 * The lexer determines the token kind by reading the first character after a
 * delimiter (any of " \n\t\r,").
 *
 * The output of the parser is an instance of `VGSProgram`. It is a list of
 * statements, and each statement is a command opcode and its arguments. This
 * instance is created on filter initialization, and reused for every frame.
 *
 * User-variables are stored in an array initialized with a copy of
 * `vgs_default_vars`.
 *
 * Blocks (the body for procedures, `if`, and `repeat`) are stored as nested
 * `VGSProgram` instances.
 *
 * The source is assumed to be ASCII. If it contains multibyte chars, each
 * byte is treated as an individual character. This is only relevant when the
 * parser must report the location of a syntax error.
 *
 * There is no error recovery. The first invalid token will stop the parser.
 */

struct VGSParser {
    const char* source;
    size_t cursor;

    const char **proc_names;
    int proc_names_count;

    // Store the variable names for the default ones (from `vgs_default_vars`)
    // and the variables created with `setvar`.
    //
    // The extra slot is needed to store the `NULL` terminator expected by
    // `av_expr_parse`.
    const char *var_names[VAR_COUNT + 1];
};

struct VGSParserToken {
    enum {
        TOKEN_EOF = 1,
        TOKEN_EXPR,
        TOKEN_LEFT_BRACKET,
        TOKEN_LITERAL,
        TOKEN_RIGHT_BRACKET,
        TOKEN_WORD,
    } type;

    const char *lexeme;
    size_t position;
    size_t length;
};

/// Check if `token` is the value of `str`.
static int vgs_token_is_string(const struct VGSParserToken *token, const char *str) {
    return strncmp(str, token->lexeme, token->length) == 0
        && str[token->length] == '\0';
}

/// Compute the line/column numbers of the given token.
static void vgs_token_span(
    const struct VGSParser *parser,
    const struct VGSParserToken *token,
    size_t *line,
    size_t *column
) {
    const char *source = parser->source;

    *line = 1;

    for (;;) {
        const char *sep = strchr(source, '\n');

        if (sep == NULL || (sep - parser->source) > token->position) {
            *column = token->position - (source - parser->source) + 1;
            break;
        }

        ++*line;
        source = sep + 1;
    }
}

static av_printf_format(4, 5)
void vgs_log_invalid_token(
    void *log_ctx,
    const struct VGSParser *parser,
    const struct VGSParserToken *token,
    const char *extra_fmt,
    ...
) {
    va_list ap;
    char extra[256];
    size_t line, column;

    vgs_token_span(parser, token, &line, &column);

    // Format extra message.
    va_start(ap, extra_fmt);
    vsnprintf(extra, sizeof(extra), extra_fmt, ap);
    va_end(ap);

    av_log(log_ctx, AV_LOG_ERROR,
        "Invalid token '%.*s' at line %zu, column %zu: %s\n",
        (int)token->length, token->lexeme, line, column, extra);
}

/// Return the next token in the source.
///
/// @param[out]  token    Next token.
/// @param[in]   advance  If true, the cursor is updated after finding a token.
///
/// @return `0` on success, and a negative `AVERROR` code on failure.
static int vgs_parser_next_token(
    void *log_ctx,
    struct VGSParser *parser,
    struct VGSParserToken *token,
    int advance
) {

    #define WORD_SEPARATOR " \n\t\r,"

    int level;
    size_t cursor, length;
    const char *source;

next_token:

    source = &parser->source[parser->cursor];

    cursor = strspn(source, WORD_SEPARATOR);
    token->position = parser->cursor + cursor;
    token->lexeme = &source[cursor];

    switch (source[cursor]) {
    case '\0':
        token->type = TOKEN_EOF;
        token->lexeme = "<EOF>";
        token->length = 5;
        return 0;

    case '(':
        // Find matching parenthesis.
        level = 1;
        length = 1;

        while (level > 0) {
            switch (source[cursor + length]) {
            case '\0':
                token->length = 1; // Show only the '(' in the error message.
                vgs_log_invalid_token(log_ctx, parser, token, "Unmatched parenthesis.");
                return AVERROR(EINVAL);

            case '(':
                level++;
                break;

            case ')':
                level--;
                break;
            }

            length++;
        }

        token->type = TOKEN_EXPR;
        token->length = length;
        break;

    case '{':
        token->type = TOKEN_LEFT_BRACKET;
        token->length = 1;
        break;

    case '}':
        token->type = TOKEN_RIGHT_BRACKET;
        token->length = 1;
        break;

    case '+':
    case '-':
    case '.':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        token->type = TOKEN_LITERAL;
        token->length = strcspn(token->lexeme, WORD_SEPARATOR);
        break;

    case '/':
        // If the next character is also '/', ignore the rest of
        // the line.
        //
        // If it is something else, return a `TOKEN_WORD`.
        if (source[cursor + 1] == '/') {
            parser->cursor += cursor + strcspn(token->lexeme, "\n");
            goto next_token;
        }

        /* fallthrough */

    default:
        token->type = TOKEN_WORD;
        token->length = strcspn(token->lexeme, WORD_SEPARATOR);
        break;
    }

    if (advance) {
        parser->cursor += cursor + token->length;
    }

    return 0;
}

/// Command arguments.
struct VGSArgument {
    enum {
        ARG_COLOR = 1,
        ARG_COLOR_VAR,
        ARG_CONST,
        ARG_EXPR,
        ARG_LITERAL,
        ARG_METADATA,
        ARG_PROCEDURE_ID,
        ARG_SUBPROGRAM,
        ARG_VARIABLE,
    } type;

    union {
        uint8_t color[4];
        int constant;
        AVExpr *expr;
        double literal;
        int proc_id;
        struct VGSProgram *subprogram;
        int variable;
    };

    char *metadata;
};

/// Program statements.
struct VGSStatement {
    enum VGSCommand cmd;
    struct VGSArgument *args;
    int args_count;
};

struct VGSProgram {
    struct VGSStatement *statements;
    int statements_count;

    const char **proc_names;
    int proc_names_count;
};

static void vgs_free(struct VGSProgram *program);

static int vgs_parse(
    void *log_ctx,
    struct VGSParser *parser,
    struct VGSProgram *program,
    int subprogram
);

static void vgs_statement_free(struct VGSStatement *stm) {
    if (stm->args == NULL)
        return;

    for (int j = 0; j < stm->args_count; j++) {
        struct VGSArgument *arg = &stm->args[j];

        switch (arg->type) {
        case ARG_EXPR:
            av_expr_free(arg->expr);
            break;

        case ARG_SUBPROGRAM:
            vgs_free(arg->subprogram);
            av_freep(&arg->subprogram);
            break;
        }

        av_freep(&arg->metadata);
    }

    av_freep(&stm->args);
}

/// Release the memory allocated by the program.
static void vgs_free(struct VGSProgram *program) {
    if (program->statements == NULL)
        return;

    for (int i = 0; i < program->statements_count; i++)
        vgs_statement_free(&program->statements[i]);

    av_freep(&program->statements);

    if (program->proc_names != NULL) {
        for (int i = 0; i < program->proc_names_count; i++)
            av_freep(&program->proc_names[i]);

        av_freep(&program->proc_names);
    }
}

/// Consume the next argument as a numeric value, and store it in `arg`.
///
/// Return `0` on success, and a negative `AVERROR` code on failure.
static int vgs_parse_numeric_argument(
    void *log_ctx,
    struct VGSParser *parser,
    struct VGSArgument *arg,
    int metadata
) {
    int ret;
    char stack_buf[64];
    char *lexeme, *endp;
    struct VGSParserToken token;

    ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
    if (ret != 0)
        return ret;

    // Convert the lexeme to a NUL-terminated string. Small lexemes are copied
    // to a buffer on the stack; thus, it avoids allocating memory is most cases.
    if (token.length + 1 < sizeof(stack_buf)) {
        lexeme = stack_buf;
    } else {
        lexeme = av_malloc(token.length + 1);

        if (lexeme == NULL)
            return AVERROR(ENOMEM);
    }

    memcpy(lexeme, token.lexeme, token.length);
    lexeme[token.length] = '\0';

    switch (token.type) {
    case TOKEN_LITERAL:
        arg->type = ARG_LITERAL;
        arg->literal = av_strtod(lexeme, &endp);

        if (*endp != '\0') {
            vgs_log_invalid_token(log_ctx, parser, &token, "Expected valid number.");
            ret = AVERROR(EINVAL);
        }
        break;

    case TOKEN_EXPR:
        arg->type = ARG_EXPR;
        ret = av_expr_parse(
            &arg->expr,
            lexeme,
            parser->var_names,
            vgs_func1_names,
            vgs_func1_impls,
            vgs_func2_names,
            vgs_func2_impls,
            0,
            log_ctx
        );

        if (ret != 0)
            vgs_log_invalid_token(log_ctx, parser, &token, "Invalid expression.");

        break;

    case TOKEN_WORD:
        ret = 1;
        for (int i = 0; i < VAR_COUNT; i++) {
            const char *var = parser->var_names[i];
            if (var == NULL)
                break;

            if (vgs_token_is_string(&token, var)) {
                arg->type = ARG_VARIABLE;
                arg->variable = i;
                ret = 0;
                break;
            }
        }

        if (ret == 0)
            break;

        /* fallthrough */

    default:
        vgs_log_invalid_token(log_ctx, parser, &token, "Expected numeric argument.");
        ret = AVERROR(EINVAL);
    }

    if (ret == 0) {
        if (metadata) {
            size_t line, column;
            vgs_token_span(parser, &token, &line, &column);
            arg->metadata = av_asprintf("[%zu:%zu] %s", line, column, lexeme);
        } else {
            arg->metadata = NULL;
        }
    } else {
        memset(arg, 0, sizeof(*arg));
    }

    if (lexeme != stack_buf)
        av_freep(&lexeme);

    return ret;
}

/// Check if the next token is a numeric value, so the last command must be
/// repeated.
static int vgs_parser_can_repeat_cmd(void *log_ctx, struct VGSParser *parser) {
    struct VGSParserToken token = { 0 };

    const int ret = vgs_parser_next_token(log_ctx, parser, &token, 0);

    if (ret != 0)
        return ret;

    switch (token.type) {
    case TOKEN_EXPR:
    case TOKEN_LITERAL:
        return 0;

    case TOKEN_WORD:
        // If the next token is a word, it will be considered to repeat
        // the command only if it is a variable, and there is not
        // known command with the same name.

        if (vgs_get_command(token.lexeme, token.length) != NULL)
            return 1;

        for (int i = 0; i < VAR_COUNT; i++) {
            const char *var = parser->var_names[i];
            if (var == NULL)
                return 1;

            if (vgs_token_is_string(&token, var))
                return 0;
        }

        return 1;

    default:
        return 1;
    }
}


static int vgs_is_valid_identifier(const struct VGSParserToken *token) {
    // An identifier is valid if:
    //
    //  - It starts with an alphabetic character or an underscore.
    //  - Everything else, alphanumeric or underscore

    for (int i = 0; i < token->length; i++) {
        char c = token->lexeme[i];
        if (c != '_'
            && !(c >= 'a' && c <= 'z')
            && !(c >= 'A' && c <= 'Z')
            && !(i > 0 && c >= '0' && c <= '9')
        ) {
            return 0;
        }
    }

    return 1;
}

/// Extract the arguments for a command, and add a new statement
/// to the program.
///
/// On success, return `0`.
static int vgs_parse_statement(
    void *log_ctx,
    struct VGSParser *parser,
    struct VGSProgram *program,
    const struct VGSCommandSpec *decl
) {

    #define FAIL(err) \
        do {                                \
            vgs_statement_free(&statement); \
            return AVERROR(err);            \
        } while(0)

    struct VGSStatement statement = {
        .cmd = decl->cmd,
        .args = NULL,
        .args_count = 0,
    };

    const struct VGSParameter *param = &decl->params[0];

    int proc_args_count = 0;

    for (;;) {
        int ret;
        void *r;

        struct VGSParserToken token = { 0 };
        struct VGSArgument arg = { 0 };

        switch (param->type) {
        case PARAM_VARIADIC:
            // If the next token is numeric, repeat the previous parameter
            // to append it to the current statement.

            if (statement.args_count < MAX_COMMAND_PARAMS
                && vgs_parser_can_repeat_cmd(log_ctx, parser) == 0
            ) {
                param--;
            } else {
                param++;
            }

            continue;

        case PARAM_END:
        case PARAM_MAY_REPEAT:
            // Add the built statement to the program.
            r = av_dynarray2_add(
                (void*)&program->statements,
                &program->statements_count,
                sizeof(statement),
                (void*)&statement
            );

            if (r == NULL)
                FAIL(ENOMEM);

            // May repeat if the next token is numeric.
            if (param->type != PARAM_END
                && vgs_parser_can_repeat_cmd(log_ctx, parser) == 0
            ) {
                param = &decl->params[0];
                statement.args = NULL;
                statement.args_count = 0;
                continue;
            }

            return 0;

        case PARAM_COLOR:
            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            arg.type = ARG_COLOR;

            for (int i = VAR_U0; i < VAR_COUNT; i++) {
                if (parser->var_names[i] == NULL)
                    break;

                if (vgs_token_is_string(&token, parser->var_names[i])) {
                    arg.type = ARG_COLOR_VAR;
                    arg.variable = i;
                    break;
                }
            }

            if (arg.type == ARG_COLOR_VAR)
                break;

            ret = av_parse_color(arg.color, token.lexeme, token.length, log_ctx);
            if (ret != 0) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Expected color.");
                FAIL(EINVAL);
            }

            break;

        case PARAM_CONSTANT: {
            int found = 0;
            char expected_names[64] = { 0 };

            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            for (
                const struct VGSConstant *constant = param->constants;
                constant->name != NULL;
                constant++
            ) {
                if (vgs_token_is_string(&token, constant->name)) {
                    arg.type = ARG_CONST;
                    arg.constant = constant->value;

                    found = 1;
                    break;
                }

                // Collect valid names to include them in the error message, in case
                // the name is not found.
                av_strlcatf(expected_names, sizeof(expected_names), " '%s'", constant->name);
            }

            if (!found) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Expected one of%s.", expected_names);
                FAIL(EINVAL);
            }

            break;
        }

        case PARAM_PROC_ARGS:
            if (vgs_parser_can_repeat_cmd(log_ctx, parser) != 0) {
                // No more arguments. Jump to next parameter.
                param++;
                continue;
            }

            if (proc_args_count++ >= MAX_PROC_ARGS) {
                vgs_log_invalid_token(log_ctx, parser, &token,
                    "Too many arguments. Limit is %d", MAX_PROC_ARGS);
                FAIL(EINVAL);
            }

            /* fallthrough */

        case PARAM_NUMERIC:
        case PARAM_NUMERIC_METADATA:
            ret = vgs_parse_numeric_argument(
                log_ctx,
                parser,
                &arg,
                param->type == PARAM_NUMERIC_METADATA
            );

            if (ret != 0)
                FAIL(EINVAL);

            break;

        case PARAM_PROC_NAME: {
            int proc_id;

            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            if (!vgs_is_valid_identifier(&token)) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Invalid procedure name.");
                FAIL(EINVAL);
            }

            // Use the index in the array as the identifier of the name.

            for (proc_id = 0; proc_id < parser->proc_names_count; proc_id++) {
                if (vgs_token_is_string(&token, parser->proc_names[proc_id]))
                    break;
            }

            if (proc_id == parser->proc_names_count) {
                const char *name = av_strndup(token.lexeme, token.length);

                const char **r = av_dynarray2_add(
                    (void*)&parser->proc_names,
                    &parser->proc_names_count,
                    sizeof(name),
                    (void*)&name
                );

                if (r == NULL) {
                    av_freep(&name);
                    FAIL(ENOMEM);
                }
            }

            arg.type = ARG_PROCEDURE_ID;
            arg.proc_id = proc_id;

            break;
        }

        case PARAM_RAW_IDENT:
            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            switch (token.type) {
            case TOKEN_LITERAL:
            case TOKEN_WORD:
                arg.type = ARG_METADATA;
                arg.metadata = av_strndup(token.lexeme, token.length);
                break;

            default:
                vgs_log_invalid_token(log_ctx, parser, &token, "Expected '{'.");
                FAIL(EINVAL);
            }

            break;

        case PARAM_SUBPROGRAM:
            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            if (token.type != TOKEN_LEFT_BRACKET) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Expected '{'.");
                FAIL(EINVAL);
            }

            arg.type = ARG_SUBPROGRAM;
            arg.subprogram = av_mallocz(sizeof(struct VGSProgram));

            ret = vgs_parse(log_ctx, parser, arg.subprogram, 1);
            if (ret != 0) {
                av_freep(&arg.subprogram);
                FAIL(EINVAL);
            }

            break;

        case PARAM_PROC_PARAMS:
            ret = vgs_parser_next_token(log_ctx, parser, &token, 0);
            if (ret != 0)
                FAIL(EINVAL);

            if (token.type == TOKEN_WORD && proc_args_count++ >= MAX_PROC_ARGS) {
                vgs_log_invalid_token(log_ctx, parser, &token,
                    "Too many parameters. Limit is %d", MAX_PROC_ARGS);
                FAIL(EINVAL);
            }

            if (token.type != TOKEN_WORD) {
                // No more variables. Jump to next parameter.
                param++;
                continue;
            }

            /* fallthrough */

        case PARAM_VAR_NAME: {
            int var_idx = -1;

            ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
            if (ret != 0)
                FAIL(EINVAL);

            // Find the slot where the variable is allocated, or the next
            // available slot if it is a new variable.
            for (int i = 0; i < VAR_COUNT; i++) {
                if (parser->var_names[i] == NULL
                    || vgs_token_is_string(&token, parser->var_names[i])
                ) {
                    var_idx = i;
                    break;
                }
            }

            // No free slots to allocate new variables.
            if (var_idx == -1) {
                vgs_log_invalid_token(log_ctx, parser, &token,
                    "Too many user variables. Can define up to %d variables.", USER_VAR_COUNT);
                FAIL(E2BIG);
            }

            // If the index is before `VAR_U0`, the name is already taken by
            // a default variable.
            if (var_idx < VAR_U0) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Reserved variable name.");
                FAIL(EINVAL);
            }

            // Need to allocate a new variable.
            if (parser->var_names[var_idx] == NULL) {
                if (!vgs_is_valid_identifier(&token)) {
                    vgs_log_invalid_token(log_ctx, parser, &token, "Invalid variable name.");
                    FAIL(EINVAL);
                }

                parser->var_names[var_idx] = av_strndup(token.lexeme, token.length);
            }

            arg.type = ARG_CONST;
            arg.constant = var_idx;
            break;
        }

        default:
            av_assert0(0); /* unreachable */
        }

        r = av_dynarray2_add(
            (void*)&statement.args,
            &statement.args_count,
            sizeof(arg),
            (void*)&arg
        );

        if (r == NULL)
            FAIL(ENOMEM);

        switch (param->type) {
            case PARAM_PROC_ARGS:
            case PARAM_PROC_PARAMS:
                // Don't update params.
                break;

            default:
                param++;
        }
    }

    #undef FAIL
}

static void vgs_parser_init(struct VGSParser *parser, const char *source) {
    parser->source = source;
    parser->cursor = 0;

    parser->proc_names = NULL;
    parser->proc_names_count = 0;

    memset(parser->var_names, 0, sizeof(parser->var_names));
    for (int i = 0; i < VAR_U0; i++)
        parser->var_names[i] = vgs_default_vars[i];
}

static void vgs_parser_free(struct VGSParser *parser) {
    for (int i = VAR_U0; i < VAR_COUNT; i++)
        if (parser->var_names[i] != NULL)
            av_freep(&parser->var_names[i]);

    if (parser->proc_names != NULL) {
        for (int i = 0; i < parser->proc_names_count; i++)
            av_freep(&parser->proc_names[i]);

        av_freep(&parser->proc_names);
    }
}

/// Build a program by parsing a script.
///
/// `subprogram` must be true when the function is called to parse the body of
/// a block (like `if` or `proc` commands).
///
/// Return `0` on success, and a negative `AVERROR` code on failure.
static int vgs_parse(
    void *log_ctx,
    struct VGSParser *parser,
    struct VGSProgram *program,
    int subprogram
) {
    struct VGSParserToken token;

    memset(program, 0, sizeof(*program));

    for (;;) {
        int ret;
        const struct VGSCommandSpec *cmd;

        ret = vgs_parser_next_token(log_ctx, parser, &token, 1);
        if (ret != 0)
            goto fail;

        switch (token.type) {
        case TOKEN_EOF:
            if (subprogram) {
                vgs_log_invalid_token(log_ctx, parser, &token, "Expected '}'.");
                goto fail;
            } else {
                // Move the proc names to the main program.
                FFSWAP(const char **, program->proc_names, parser->proc_names);
                FFSWAP(int, program->proc_names_count, parser->proc_names_count);
            }

            return 0;

        case TOKEN_WORD:
            // The token must be a valid command.
            cmd = vgs_get_command(token.lexeme, token.length);
            if (cmd == NULL)
                goto invalid_token;

            ret = vgs_parse_statement(log_ctx, parser, program, cmd);
            if (ret != 0)
                goto fail;

            break;

        case TOKEN_RIGHT_BRACKET:
            if (!subprogram)
                goto invalid_token;

            return 0;

        default:
            goto invalid_token;
        }
    }

    return AVERROR_BUG; /* unreachable */

invalid_token:
    vgs_log_invalid_token(log_ctx, parser, &token, "Expected command.");

fail:
    vgs_free(program);
    return AVERROR(EINVAL);
}

/*
 * == Interpreter ==
 *
 * The interpreter takes the `VGSProgram` built by the parser, and translate the
 * statements to calls to cairo.
 *
 * `VGSEvalState` tracks the state needed to execute such commands.
 */

/// Number of different states for the `randomg` function.
#define RANDOM_STATES 4

/// Block assigned to a procedure by a call to the `proc` command.
struct VGSProcedure {
    const struct VGSProgram *program;

    /// Number of expected arguments.
    int proc_args_count;

    /// Variable ids where each argument is stored.
    int args[MAX_PROC_ARGS];
};

struct VGSEvalState {
    void *log_ctx;

    /// Current frame.
    AVFrame *frame;

    /// Cairo context for drawing operations.
    cairo_t *cairo_ctx;

    /// Pattern being built by commands like `colorstop`.
    cairo_pattern_t *pattern_builder;

    /// Register if `break` was called in a subprogram.
    int interrupted;

    /// Next call to `[eo]fill`, `[eo]clip`, or `stroke`, should use
    /// the `_preserve` function.
    int preserve_path;

    /// Subprograms associated to each procedure identifier.
    struct VGSProcedure *procedures;

    /// Reference to the procedure names in the `VGSProgram`.
    const char *const *proc_names;

    /// Values for the variables in expressions.
    ///
    /// Some variables (like `cx` or `cy`) are written before
    /// executing each statement.
    double vars[VAR_COUNT];

    /// State for each index available for the `randomg` function.
    FFSFC64 random_state[RANDOM_STATES];

    /// Frame metadata, if any.
    AVDictionary *metadata;

    // Reflected Control Points. Used in T and S commands.
    //
    // See https://www.w3.org/TR/SVG/paths.html#ReflectedControlPoints
    struct {
        enum { RCP_NONE, RCP_VALID, RCP_UPDATED } status;

        double cubic_x;
        double cubic_y;
        double quad_x;
        double quad_y;
    } rcp;
};

/// Function `pathlen(n)` for `av_expr_eval`.
///
/// Compute the length of the current path in the cairo context. If `n > 0`, it
/// is the maximum number of segments to be added to the length.
static double vgs_fn_pathlen(void *data, double arg) {
    if (!isfinite(arg))
        return NAN;

    const struct VGSEvalState *state = (struct VGSEvalState *)data;

    int max_segments = (int)arg;

    double lmx = NAN, lmy = NAN; // last move point
    double cx = NAN, cy = NAN;   // current point.

    double length = 0;
    cairo_path_t *path = cairo_copy_path_flat(state->cairo_ctx);

    for (int i = 0; i < path->num_data; i += path->data[i].header.length) {
        double x, y;
        cairo_path_data_t *data = &path->data[i];

        switch (data[0].header.type) {
        case CAIRO_PATH_MOVE_TO:
            cx = lmx = data[1].point.x;
            cy = lmy = data[1].point.y;

            // Don't update `length`.
            continue;

        case CAIRO_PATH_LINE_TO:
            x = data[1].point.x;
            y = data[1].point.y;
            break;

        case CAIRO_PATH_CLOSE_PATH:
            x = lmx;
            y = lmy;
            break;

        default:
            continue;
        }

        length += hypot(cx - x, cy - y);

        cx = x;
        cy = y;

        // If the function argument is `> 0`, use it as a limit for how
        // many segments are added up.
        if (--max_segments == 0)
            break;
    }

    cairo_path_destroy(path);

    return length;
}

/// Function `randomg(n)` for `av_expr_eval`.
///
/// Compute a random value between 0 and 1. Similar to `random()`, but the
/// state is global to the VGS program.
///
/// The last 2 bits of the integer representation of the argument are used
/// as the state index. If the state is not initialized, the argument is
/// the seed for that state.
static double vgs_fn_randomg(void *data, double arg) {
    if (!isfinite(arg))
        return arg;

    struct VGSEvalState *state = (struct VGSEvalState *)data;

    const uint64_t iarg = (uint64_t)arg;
    const int rng_idx = iarg % FF_ARRAY_ELEMS(state->random_state);

    FFSFC64 *rng = &state->random_state[rng_idx];

    if (rng->counter == 0)
        ff_sfc64_init(rng, iarg, iarg, iarg, 12);

    return ff_sfc64_get(rng) * (1.0 / UINT64_MAX);
}

/// Function `p(x, y)` for `av_expr_eval`.
///
/// Return the pixel color in 0xRRGGBBAA format.
///
/// The transformation matrix is applied to the given coordinates.
///
/// If the coordinates are outside the frame, return NAN.
static double vgs_fn_p(void* data, double x0, double y0) {
    const struct VGSEvalState *state = (struct VGSEvalState *)data;
    const AVFrame *frame = state->frame;

    if (frame == NULL || !isfinite(x0) || !isfinite(y0))
        return NAN;

    cairo_user_to_device(state->cairo_ctx, &x0, &y0);

    const int x = (int)x0;
    const int y = (int)y0;

    if (x < 0 || y < 0 || x >= frame->width || y >= frame->height)
        return NAN;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frame->format);

    uint32_t color[4] = { 0, 0, 0, 255 };

    for (int c = 0; c < desc->nb_components; c++) {
        uint32_t pixel;
        const int depth = desc->comp[c].depth;

        av_read_image_line2(
            &pixel,
            (void*)frame->data,
            frame->linesize,
            desc,
            x, y,
            c,
            1, // width
            0, // read_pal_component
            4  // dst_element_size
        );

        if (depth != 8) {
            pixel = pixel * 255 / ((1 << depth) - 1);
        }

        color[c] = pixel;
    }

    return color[0] << 24 | color[1] << 16 | color[2] << 8 | color[3];
}

static int vgs_eval_state_init(
    struct VGSEvalState *state,
    const struct VGSProgram *program,
    void *log_ctx,
    AVFrame *frame
) {
    memset(state, 0, sizeof(*state));

    state->log_ctx = log_ctx;
    state->frame = frame;
    state->rcp.status = RCP_NONE;

    if (program->proc_names != NULL) {
        state->procedures = av_calloc(sizeof(struct VGSProcedure), program->proc_names_count);
        state->proc_names = program->proc_names;

        if (state->procedures == NULL)
            return AVERROR(ENOMEM);
    }

    for (int i = 0; i < VAR_COUNT; i++)
        state->vars[i] = NAN;

    return 0;
}

static void vgs_eval_state_free(struct VGSEvalState *state) {
    if (state->pattern_builder != NULL)
        cairo_pattern_destroy(state->pattern_builder);

    if (state->procedures != NULL)
        av_free(state->procedures);

    memset(state, 0, sizeof(*state));
}

/// Draw an ellipse. `x`/`y` specifies the center, and `rx`/`ry` the radius of
/// the ellipse on the x/y axis.
///
/// Cairo does not provide a native way to create an ellipse, but it can be done
/// by scaling the Y axis with the transformation matrix.
static void draw_ellipse(cairo_t *c, double x, double y, double rx, double ry) {
    cairo_save(c);
    cairo_translate(c, x, y);

    if (rx != ry)
        cairo_scale(c, 1, ry / rx);

    cairo_new_sub_path(c);
    cairo_arc(c, 0, 0, rx, 0, 2 * M_PI);
    cairo_close_path(c);
    cairo_new_sub_path(c);

    cairo_restore(c);
}

/// Draw a quadratic bezier from the current point to `x, y`, The control point
/// is specified by `x1, y1`.
///
/// If the control point is NAN, use the reflected point.
///
/// cairo only supports cubic cuvers, so control points must be adjusted to
/// simulate the behaviour in SVG.
static void draw_quad_curve_to(
    struct VGSEvalState *state,
    int relative,
    double x1,
    double y1,
    double x,
    double y
) {
    double x0 = 0, y0 = 0;  // Current point.
    double xa, ya, xb, yb;  // Control points for the cubic curve.

    const int use_reflected = isnan(x1);

    cairo_get_current_point(state->cairo_ctx, &x0, &y0);

    if (relative) {
        if (!use_reflected) {
            x1 += x0;
            y1 += y0;
        }

        x += x0;
        y += y0;
    }

    if (use_reflected) {
        if (state->rcp.status != RCP_NONE) {
            x1 = state->rcp.quad_x;
            y1 = state->rcp.quad_y;
        } else {
            x1 = x0;
            y1 = y0;
        }
    }

    xa = (x0 + 2 * x1) / 3;
    ya = (y0 + 2 * y1) / 3;
    xb = (x + 2 * x1) / 3;
    yb = (y + 2 * y1) / 3;
    cairo_curve_to(state->cairo_ctx, xa, ya, xb, yb, x, y);

    state->rcp.status = RCP_UPDATED;
    state->rcp.cubic_x = x1;
    state->rcp.cubic_y = y1;
    state->rcp.quad_x = 2 * x - x1;
    state->rcp.quad_y = 2 * y - y1;
}

/// Similar to quad_curve_to, but for cubic curves.
static void draw_cubic_curve_to(
    struct VGSEvalState *state,
    int relative,
    double x1,
    double y1,
    double x2,
    double y2,
    double x,
    double y
) {
    double x0 = 0, y0 = 0; // Current point.

    const int use_reflected = isnan(x1);

    cairo_get_current_point(state->cairo_ctx, &x0, &y0);

    if (relative) {
        if (!use_reflected) {
            x1 += x0;
            y1 += y0;
        }

        x += x0;
        y += y0;
        x2 += x0;
        y2 += y0;
    }

    if (use_reflected) {
        if (state->rcp.status != RCP_NONE) {
            x1 = state->rcp.cubic_x;
            y1 = state->rcp.cubic_y;
        } else {
            x1 = x0;
            y1 = y0;
        }
    }

    cairo_curve_to(state->cairo_ctx, x1, y1, x2, y2, x, y);

    state->rcp.status = RCP_UPDATED;
    state->rcp.cubic_x = 2 * x - x2;
    state->rcp.cubic_y = 2 * y - y2;
    state->rcp.quad_x = x2;
    state->rcp.quad_y = y2;
}

static void draw_rounded_rect(
    cairo_t *c,
    double x,
    double y,
    double width,
    double height,
    double radius
) {
    radius = av_clipd(radius, 0, FFMIN(height / 2, width / 2));

    cairo_new_sub_path(c);
    cairo_arc(c, x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cairo_arc(c, x + width - radius, y + radius, radius, 3 * M_PI / 2, 2 * M_PI);
    cairo_arc(c, x + width - radius, y + height - radius, radius, 0, M_PI / 2);
    cairo_arc(c, x + radius, y + height - radius, radius, M_PI / 2, M_PI);
    cairo_close_path(c);
}

static void hsl2rgb(
    double h,
    double s,
    double l,
    double *pr,
    double *pg,
    double *pb
) {
    // https://en.wikipedia.org/wiki/HSL_and_HSV#HSL_to_RGB

    double r, g, b, chroma, x, h1;

    if (h < 0 || h >= 360)
        h = fmod(FFMAX(h, 0), 360);

    s = av_clipd(s, 0, 1);
    l = av_clipd(l, 0, 1);

    chroma = (1 - fabs(2 * l - 1)) * s;
    h1 = h / 60;
    x = chroma * (1 - fabs(fmod(h1, 2) - 1));

    switch ((int)floor(h1)) {
    case 0:
        r = chroma;
        g = x;
        b = 0;
        break;

    case 1:
        r = x;
        g = chroma;
        b = 0;
        break;

    case 2:
        r = 0;
        g = chroma;
        b = x;
        break;

    case 3:
        r = 0;
        g = x;
        b = chroma;
        break;

    case 4:
        r = x;
        g = 0;
        b = chroma;
        break;

    default:
        r = chroma;
        g = 0;
        b = x;
        break;

    }

    x = l - chroma / 2;

    *pr = r + x;
    *pg = g + x;
    *pb = b + x;
}

/// Interpreter for `VGSProgram`.
///
/// Its implementation is a simple switch-based dispatch.
///
/// To evaluate blocks (like `if` or `call`), it makes a recursive call with
/// the subprogram allocated to the block.
static int vgs_eval(
    struct VGSEvalState *state,
    const struct VGSProgram *program
) {

    #define ASSERT_ARGS(n) av_assert0(statement->args_count == n)

    // When `preserve` is used, the next call to `clip`, `fill`, or `stroke`
    // uses the `cairo_..._preserve` function.
    #define MAY_PRESERVE(funcname) \
        do {                                           \
            if (state->preserve_path) {                \
                state->preserve_path = 0;              \
                funcname##_preserve(state->cairo_ctx); \
            } else {                                   \
                funcname(state->cairo_ctx);            \
            }                                          \
        } while(0)

    double numerics[MAX_COMMAND_PARAMS];
    double colors[MAX_COMMAND_PARAMS][4];

    double cx, cy; // Current point.

    int relative;

    for (int st_number = 0; st_number < program->statements_count; st_number++) {
        const struct VGSStatement *statement = &program->statements[st_number];

        if (statement->args_count > FF_ARRAY_ELEMS(numerics)) {
            av_log(state->log_ctx, AV_LOG_ERROR, "Too many arguments (%d).\n", statement->args_count);
            return AVERROR_BUG;
        }

        if (cairo_has_current_point(state->cairo_ctx)) {
            cairo_get_current_point(state->cairo_ctx, &cx, &cy);
        } else {
            cx = NAN;
            cy = NAN;
        }

        state->vars[VAR_CX] = cx;
        state->vars[VAR_CY] = cy;

        // Compute arguments.
        for (int arg = 0; arg < statement->args_count; arg++) {
            uint8_t color[4];

            const struct VGSArgument *a = &statement->args[arg];

            switch (a->type) {
            case ARG_COLOR:
            case ARG_COLOR_VAR:
                if (a->type == ARG_COLOR) {
                    memcpy(color, a->color, sizeof(color));
                } else {
                    uint32_t c = av_be2ne32((uint32_t)state->vars[a->variable]);
                    memcpy(color, &c, sizeof(color));
                }

                colors[arg][0] = (double)(color[0]) / 255.0,
                colors[arg][1] = (double)(color[1]) / 255.0,
                colors[arg][2] = (double)(color[2]) / 255.0,
                colors[arg][3] = (double)(color[3]) / 255.0;
                break;

            case ARG_EXPR:
                numerics[arg] = av_expr_eval(a->expr, state->vars, state);
                break;

            case ARG_LITERAL:
                numerics[arg] = a->literal;
                break;

            case ARG_VARIABLE:
                av_assert0(a->variable < VAR_COUNT);
                numerics[arg] = state->vars[a->variable];
                break;

            default:
                numerics[arg] = NAN;
                break;
            }
        }

        // If the command uses a pending pattern (like a solid color
        // or a gradient), set it to the cairo context before executing
        // stroke/fill commands.
        if (state->pattern_builder != NULL) {
            switch (statement->cmd) {
            case CMD_FILL:
            case CMD_FILL_EO:
            case CMD_RESTORE:
            case CMD_SAVE:
            case CMD_STROKE:
                cairo_set_source(state->cairo_ctx, state->pattern_builder);
                cairo_pattern_destroy(state->pattern_builder);
                state->pattern_builder = NULL;
            }
        }

        // Execute the command.
        switch (statement->cmd) {
        case CMD_ARC:
            ASSERT_ARGS(5);
            cairo_arc(
                state->cairo_ctx,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3],
                numerics[4]
            );
            break;

        case CMD_ARC_NEG:
            ASSERT_ARGS(5);
            cairo_arc_negative(
                state->cairo_ctx,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3],
                numerics[4]
            );
            break;

        case CMD_CIRCLE:
            ASSERT_ARGS(3);
            draw_ellipse(state->cairo_ctx, numerics[0], numerics[1], numerics[2], numerics[2]);
            break;

        case CMD_CLIP:
        case CMD_CLIP_EO:
            ASSERT_ARGS(0);
            cairo_set_fill_rule(
                state->cairo_ctx,
                statement->cmd == CMD_CLIP ?
                    CAIRO_FILL_RULE_WINDING :
                    CAIRO_FILL_RULE_EVEN_ODD
            );

            MAY_PRESERVE(cairo_clip);
            break;

        case CMD_CLOSE_PATH:
            ASSERT_ARGS(0);
            cairo_close_path(state->cairo_ctx);
            break;

        case CMD_COLOR_STOP:
            if (state->pattern_builder == NULL) {
                av_log(state->log_ctx, AV_LOG_ERROR, "colorstop with no active gradient.\n");
                break;
            }

            ASSERT_ARGS(2);
            cairo_pattern_add_color_stop_rgba(
                state->pattern_builder,
                numerics[0],
                colors[1][0],
                colors[1][1],
                colors[1][2],
                colors[1][3]
            );
            break;

        case CMD_CURVE_TO:
        case CMD_CURVE_TO_REL:
            ASSERT_ARGS(6);
            draw_cubic_curve_to(
                state,
                statement->cmd == CMD_CURVE_TO_REL,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3],
                numerics[4],
                numerics[5]
            );
            break;

        case CMD_DEF_HSLA:
        case CMD_DEF_RGBA: {
            double r, g, b;

            ASSERT_ARGS(5);

            const int user_var = statement->args[0].variable;
            av_assert0(user_var >= VAR_U0 && user_var < (VAR_U0 + USER_VAR_COUNT));

            if (statement->cmd == CMD_DEF_HSLA) {
                hsl2rgb(numerics[1], numerics[2], numerics[3], &r, &g, &b);
            } else {
                r = numerics[1];
                g = numerics[2];
                b = numerics[3];
            }

            #define C(v, o) ((uint32_t)(av_clipd(v, 0, 1) * 255) << o)

            state->vars[user_var] = (double)(
                C(r, 24)
                | C(g, 16)
                | C(b, 8)
                | C(numerics[4], 0)
            );

            #undef C

            break;
        }

        case CMD_ELLIPSE:
            ASSERT_ARGS(4);
            draw_ellipse(state->cairo_ctx, numerics[0], numerics[1], numerics[2], numerics[3]);
            break;

        case CMD_FILL:
        case CMD_FILL_EO:
            ASSERT_ARGS(0);

            cairo_set_fill_rule(
                state->cairo_ctx,
                statement->cmd == CMD_FILL ?
                    CAIRO_FILL_RULE_WINDING :
                    CAIRO_FILL_RULE_EVEN_ODD
            );

            MAY_PRESERVE(cairo_fill);
            break;

        case CMD_GET_METADATA: {
            ASSERT_ARGS(2);

            double value = NAN;

            const int user_var = statement->args[0].constant;
            const char *key = statement->args[1].metadata;

            av_assert0(user_var >= VAR_U0 && user_var < (VAR_U0 + USER_VAR_COUNT));

            if (state->metadata != NULL && key != NULL) {
                char *endp;
                AVDictionaryEntry *entry = av_dict_get(state->metadata, key, NULL, 0);

                if (entry != NULL) {
                    value = av_strtod(entry->value, &endp);

                    if (*endp != '\0')
                        value = NAN;
                }
            }

            state->vars[user_var] = value;
            break;
        }

        case CMD_BREAK:
            state->interrupted = 1;
            return 0;

        case CMD_IF:
            ASSERT_ARGS(2);

            if (isfinite(numerics[0]) && numerics[0] != 0.0) {
                int ret = vgs_eval(state, statement->args[1].subprogram);
                if (ret != 0 || state->interrupted != 0)
                    return ret;
            }

            break;

        case CMD_LINEAR_GRAD:
            ASSERT_ARGS(4);

            if (state->pattern_builder != NULL)
                cairo_pattern_destroy(state->pattern_builder);

            state->pattern_builder = cairo_pattern_create_linear(
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3]
            );
            break;

        case CMD_LINE_TO:
            ASSERT_ARGS(2);
            cairo_line_to(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_LINE_TO_REL:
            ASSERT_ARGS(2);
            cairo_rel_line_to(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_MOVE_TO:
            ASSERT_ARGS(2);
            cairo_move_to(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_MOVE_TO_REL:
            ASSERT_ARGS(2);
            cairo_rel_move_to(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_NEW_PATH:
            ASSERT_ARGS(0);
            cairo_new_sub_path(state->cairo_ctx);
            break;

        case CMD_PRESERVE:
            ASSERT_ARGS(0);
            state->preserve_path = 1;
            break;

        case CMD_PRINT: {
            char msg[256];
            int len = 0;

            for (int i = 0; i < statement->args_count; i++) {
                int written;
                int capacity = sizeof(msg) - len;

                written = snprintf(
                    msg + len,
                    capacity,
                    "%s%s = %f",
                    i > 0 ? " | " : "",
                    statement->args[i].metadata,
                    numerics[i]
                );

                // If buffer is too small, discard the latest arguments.
                if (written >= capacity)
                    break;

                len += written;
            }

            av_log(state->log_ctx, AV_LOG_INFO, "%.*s\n", len, msg);
            break;
        }

        case CMD_PROC_ASSIGN: {
            struct VGSProcedure *proc;

            const int proc_args = statement->args_count - 2;
            av_assert0(proc_args >= 0 && proc_args <= MAX_PROC_ARGS);

            proc = &state->procedures[statement->args[0].proc_id];
            proc->program = statement->args[proc_args + 1].subprogram;
            proc->proc_args_count = proc_args;

            for (int i = 0; i < MAX_PROC_ARGS; i++)
                proc->args[i] = i < proc_args ? statement->args[i + 1].constant : -1;

            break;
        }

        case CMD_PROC_CALL: {
            const int proc_args = statement->args_count - 1;
            av_assert0(proc_args >= 0 && proc_args <= MAX_PROC_ARGS);

            const int proc_id = statement->args[0].proc_id;

            const struct VGSProcedure *proc = &state->procedures[proc_id];

            if (proc->proc_args_count != proc_args) {
                av_log(
                    state->log_ctx,
                    AV_LOG_ERROR,
                    "Procedure expects %d arguments, but received %d.",
                    proc->proc_args_count,
                    proc_args
                );

                break;
            }

            if (proc->program == NULL) {
                const char *proc_name = state->proc_names[proc_id];
                av_log(state->log_ctx, AV_LOG_ERROR,
                    "Missing body for procedure '%s'\n", proc_name);
            } else {
                int ret;
                double current_vars[MAX_PROC_ARGS] = { 0 };

                // Set variables for the procedure arguments
                for (int i = 0; i < proc_args; i++) {
                    const int var = proc->args[i];
                    if (var != -1) {
                        current_vars[i] = state->vars[var];
                        state->vars[var] = numerics[i + 1];
                    }
                }

                ret = vgs_eval(state, proc->program);

                // Restore variable values.
                for (int i = 0; i < proc_args; i++) {
                    const int var = proc->args[i];
                    if (var != -1) {
                        state->vars[var] = current_vars[i];
                    }
                }

                if (ret != 0)
                    return ret;

                // `break` interrupts the procedure, but don't stop the program.
                if (state->interrupted) {
                    state->interrupted = 0;
                    break;
                }
            }

            break;
        }

        case CMD_Q_CURVE_TO:
        case CMD_Q_CURVE_TO_REL:
            ASSERT_ARGS(4);
            relative = statement->cmd == CMD_Q_CURVE_TO_REL;
            draw_quad_curve_to(
                state,
                relative,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3]
            );
            break;

        case CMD_RADIAL_GRAD:
            ASSERT_ARGS(6);

            if (state->pattern_builder != NULL)
                cairo_pattern_destroy(state->pattern_builder);

            state->pattern_builder = cairo_pattern_create_radial(
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3],
                numerics[4],
                numerics[5]
            );
            break;

        case CMD_RESET_CLIP:
            cairo_reset_clip(state->cairo_ctx);
            break;

        case CMD_RESET_DASH:
            cairo_set_dash(state->cairo_ctx, NULL, 0, 0);
            break;

        case CMD_RESET_MATRIX:
            cairo_identity_matrix(state->cairo_ctx);
            break;

        case CMD_RECT:
            ASSERT_ARGS(4);
            cairo_rectangle(state->cairo_ctx, numerics[0], numerics[1], numerics[2], numerics[3]);
            break;

        case CMD_REPEAT: {
            double var_i = state->vars[VAR_I];

            ASSERT_ARGS(2);

            if (!isfinite(numerics[0]))
                break;

            for (int i = 0, count = (int)numerics[0]; i < count; i++) {
                state->vars[VAR_I] = i;

                const int ret = vgs_eval(state, statement->args[1].subprogram);
                if (ret != 0)
                    return ret;

                // `break` interrupts the loop, but don't stop the program.
                if (state->interrupted) {
                    state->interrupted = 0;
                    break;
                }
            }

            state->vars[VAR_I] = var_i;
            break;
        }

        case CMD_RESTORE:
            ASSERT_ARGS(0);
            cairo_restore(state->cairo_ctx);
            break;

        case CMD_ROTATE:
            ASSERT_ARGS(1);
            cairo_rotate(state->cairo_ctx, numerics[0]);
            break;

        case CMD_ROUNDEDRECT:
            ASSERT_ARGS(5);
            draw_rounded_rect(
                state->cairo_ctx,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3],
                numerics[4]
            );
            break;

        case CMD_SAVE:
            ASSERT_ARGS(0);
            cairo_save(state->cairo_ctx);
            break;

        case CMD_SCALE:
            ASSERT_ARGS(1);
            cairo_scale(state->cairo_ctx, numerics[0], numerics[0]);
            break;

        case CMD_SCALEXY:
            ASSERT_ARGS(2);
            cairo_scale(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_SET_COLOR:
            ASSERT_ARGS(1);

            if (state->pattern_builder != NULL)
                cairo_pattern_destroy(state->pattern_builder);

            state->pattern_builder = cairo_pattern_create_rgba(
                colors[0][0],
                colors[0][1],
                colors[0][2],
                colors[0][3]
            );
            break;

        case CMD_SET_LINE_CAP:
            ASSERT_ARGS(1);
            cairo_set_line_cap(state->cairo_ctx, statement->args[0].constant);
            break;

        case CMD_SET_LINE_JOIN:
            ASSERT_ARGS(1);
            cairo_set_line_join(state->cairo_ctx, statement->args[0].constant);
            break;

        case CMD_SET_LINE_WIDTH:
            ASSERT_ARGS(1);
            cairo_set_line_width(state->cairo_ctx, numerics[0]);
            break;

        case CMD_SET_DASH:
        case CMD_SET_DASH_OFFSET: {
            int num;
            double *dashes, offset, stack_buf[16];

            ASSERT_ARGS(1);

            num = cairo_get_dash_count(state->cairo_ctx);

            if (num + 1 < FF_ARRAY_ELEMS(stack_buf)) {
                dashes = stack_buf;
            } else {
                dashes = av_calloc(num + 1, sizeof(double));

                if (dashes == NULL)
                    return AVERROR(ENOMEM);
            }

            cairo_get_dash(state->cairo_ctx, dashes, &offset);

            if (statement->cmd == CMD_SET_DASH) {
                dashes[num] = numerics[0];
                num++;
            } else {
                offset = numerics[0];
            }

            cairo_set_dash(state->cairo_ctx, dashes, num, offset);

            if (dashes != stack_buf)
                av_freep(&dashes);

            break;
        }

        case CMD_SET_HSLA:
        case CMD_SET_RGBA: {
            double r, g, b;

            ASSERT_ARGS(4);

            if (state->pattern_builder != NULL)
                cairo_pattern_destroy(state->pattern_builder);

            if (statement->cmd == CMD_SET_HSLA) {
                hsl2rgb(numerics[0], numerics[1], numerics[2], &r, &g, &b);
            } else {
                r = numerics[0];
                g = numerics[1];
                b = numerics[2];
            }

            state->pattern_builder = cairo_pattern_create_rgba(r, g, b, numerics[3]);
            break;
        }

        case CMD_SET_VAR: {
            ASSERT_ARGS(2);

            const int user_var = statement->args[0].constant;

            av_assert0(user_var >= VAR_U0 && user_var < (VAR_U0 + USER_VAR_COUNT));
            state->vars[user_var] = numerics[1];
            break;
        }

        case CMD_STROKE:
            ASSERT_ARGS(0);
            MAY_PRESERVE(cairo_stroke);
            break;

        case CMD_S_CURVE_TO:
        case CMD_S_CURVE_TO_REL:
            ASSERT_ARGS(4);
            draw_cubic_curve_to(
                state,
                statement->cmd == CMD_S_CURVE_TO_REL,
                NAN,
                NAN,
                numerics[0],
                numerics[1],
                numerics[2],
                numerics[3]
            );
            break;

        case CMD_TRANSLATE:
            ASSERT_ARGS(2);
            cairo_translate(state->cairo_ctx, numerics[0], numerics[1]);
            break;

        case CMD_T_CURVE_TO:
        case CMD_T_CURVE_TO_REL:
            ASSERT_ARGS(2);
            relative = statement->cmd == CMD_T_CURVE_TO_REL;
            draw_quad_curve_to(state, relative, NAN, NAN, numerics[0], numerics[1]);
            break;

        case CMD_HORZ:
        case CMD_HORZ_REL:
        case CMD_VERT:
        case CMD_VERT_REL:
            ASSERT_ARGS(1);

            if (cairo_has_current_point(state->cairo_ctx)) {
                double d = numerics[0];

                switch (statement->cmd) {
                    case CMD_HORZ:     cx  = d; break;
                    case CMD_VERT:     cy  = d; break;
                    case CMD_HORZ_REL: cx += d; break;
                    case CMD_VERT_REL: cy += d; break;
                }

                cairo_line_to(state->cairo_ctx, cx, cy);
            }

            break;
        }

        // Reflected control points will be discarded if the executed
        // command did not update them, and it is a commands to
        // modify the path.
        if (state->rcp.status == RCP_UPDATED) {
            state->rcp.status = RCP_VALID;
        } else if (vgs_cmd_change_path(statement->cmd)) {
            state->rcp.status = RCP_NONE;
        }

        // Check for errors in cairo.
        if (cairo_status(state->cairo_ctx) != CAIRO_STATUS_SUCCESS) {
            av_log(
                state->log_ctx,
                AV_LOG_ERROR,
                "Error in cairo context: %s\n",
                cairo_status_to_string(cairo_status(state->cairo_ctx))
            );

            return AVERROR(EINVAL);
        }
    }

    return 0;
}

/*
 * == AVClass for drawvg ==
 *
 * Source is parsed on the `init` function.
 *
 * Cairo supports a few pixel formats, but only RGB. All compatible formats are
 * listed in the `drawvg_pix_fmts` array.
 */

typedef struct DrawVGContext {
    const AVClass *class;

    /// Equivalent to AVPixelFormat.
    cairo_format_t cairo_format;

    /// Time in seconds of the first frame.
    double time_start;

    /// Inline source.
    uint8_t *script_text;

    /// File path to load the source.
    uint8_t *script_file;

    struct VGSProgram program;
} DrawVGContext;

#define OPT(name, field, help)          \
    {                                   \
        name,                           \
        help,                           \
        offsetof(DrawVGContext, field), \
        AV_OPT_TYPE_STRING,             \
        { .str = NULL },                \
        0, 0,                           \
        AV_OPT_FLAG_FILTERING_PARAM     \
           | AV_OPT_FLAG_VIDEO_PARAM    \
    }

static const AVOption drawvg_options[]= {
    OPT("script", script_text, "script source to draw the graphics"),
    OPT("s",      script_text, "script source to draw the graphics"),
    OPT("file",   script_file, "file to load the script source"),
    { NULL }
};

#undef OPT


AVFILTER_DEFINE_CLASS(drawvg);

static const enum AVPixelFormat drawvg_pix_fmts[] = {
    AV_PIX_FMT_RGB32,
    AV_PIX_FMT_0RGB32,
    AV_PIX_FMT_RGB565,
    AV_PIX_FMT_X2RGB10,
    AV_PIX_FMT_NONE
};

// Return the cairo equivalent to AVPixelFormat.
static cairo_format_t cairo_format_from_pix_fmt(
    DrawVGContext* ctx,
    enum AVPixelFormat format
) {
    // This array must have the same order of `drawvg_pix_fmts`.
    const cairo_format_t format_map[] = {
        CAIRO_FORMAT_ARGB32, // cairo expects pre-multiplied alpha.
        CAIRO_FORMAT_RGB24,
        CAIRO_FORMAT_RGB16_565,
        CAIRO_FORMAT_RGB30,
        CAIRO_FORMAT_INVALID,
    };

    for (int i = 0; i < FF_ARRAY_ELEMS(drawvg_pix_fmts); i++) {
        if (drawvg_pix_fmts[i] == format)
            return format_map[i];
    }

    const char* name = av_get_pix_fmt_name(format);
    av_log(ctx, AV_LOG_ERROR, "Invalid pix_fmt: %s\n", name);

    return CAIRO_FORMAT_INVALID;
}

static int drawvg_filter_frame(AVFilterLink *inlink, AVFrame *frame) {
    int ret;
    double var_t;
    cairo_surface_t* surface;

    FilterLink *inl = ff_filter_link(inlink);
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterContext *filter_ctx = inlink->dst;
    DrawVGContext *drawvg_ctx = filter_ctx->priv;

    struct VGSEvalState eval_state;
    ret = vgs_eval_state_init(&eval_state, &drawvg_ctx->program, drawvg_ctx, frame);

    if (ret != 0)
        return ret;

    // Draw directly on the frame data.
    surface = cairo_image_surface_create_for_data(
        frame->data[0],
        drawvg_ctx->cairo_format,
        frame->width,
        frame->height,
        frame->linesize[0]
    );

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        av_log(drawvg_ctx, AV_LOG_ERROR, "Failed to create cairo surface.\n");
        return AVERROR_EXTERNAL;
    }

    eval_state.cairo_ctx = cairo_create(surface);

    var_t = TS2T(frame->pts, inlink->time_base);

    if (isnan(drawvg_ctx->time_start))
        drawvg_ctx->time_start = var_t;

    eval_state.vars[VAR_N] = inl->frame_count_out;
    eval_state.vars[VAR_T] = var_t;
    eval_state.vars[VAR_TS] = drawvg_ctx->time_start;
    eval_state.vars[VAR_W] = inlink->w;
    eval_state.vars[VAR_H] = inlink->h;
    eval_state.vars[VAR_DURATION] = frame->duration * av_q2d(inlink->time_base);

    eval_state.metadata = frame->metadata;

    ret = vgs_eval(&eval_state, &drawvg_ctx->program);

    cairo_destroy(eval_state.cairo_ctx);
    cairo_surface_destroy(surface);

    vgs_eval_state_free(&eval_state);

    if (ret != 0)
        return ret;

    return ff_filter_frame(outlink, frame);
}

static int drawvg_config_props(AVFilterLink *inlink) {
    AVFilterContext *filter_ctx = inlink->dst;
    DrawVGContext *drawvg_ctx = filter_ctx->priv;

    // Find the cairo format equivalent to the format of the frame,
    // so cairo can draw directly on the memory already allocated.

    drawvg_ctx->cairo_format = cairo_format_from_pix_fmt(drawvg_ctx, inlink->format);
    if (drawvg_ctx->cairo_format == CAIRO_FORMAT_INVALID)
        return AVERROR(EINVAL);

    return 0;
}

static av_cold int drawvg_init(AVFilterContext *ctx) {
    int ret;
    struct VGSParser parser;
    DrawVGContext *drawvg = ctx->priv;

    drawvg->time_start = NAN;

    if ((drawvg->script_text == NULL) == (drawvg->script_file == NULL)) {
        av_log(ctx, AV_LOG_ERROR,
            "Either 'source' or 'file' must be provided\n");

        return AVERROR(EINVAL);
    }

    if (drawvg->script_file != NULL) {
        ret = ff_load_textfile(
            ctx,
            (const char *)drawvg->script_file,
            &drawvg->script_text,
            NULL
        );

        if (ret != 0)
            return ret;
    }

    vgs_parser_init(&parser, drawvg->script_text);

    ret = vgs_parse(drawvg, &parser, &drawvg->program, 0);

    vgs_parser_free(&parser);

    return ret;
}

static av_cold void drawvg_uninit(AVFilterContext *ctx) {
    DrawVGContext *drawvg = ctx->priv;
    vgs_free(&drawvg->program);
}

static const AVFilterPad drawvg_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .flags        = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame = drawvg_filter_frame,
        .config_props = drawvg_config_props,
    },
};

const FFFilter ff_vf_drawvg = {
    .p.name        = "drawvg",
    .p.description = NULL_IF_CONFIG_SMALL("Draw vector graphics on top of video frames."),
    .p.priv_class  = &drawvg_class,
    .p.flags       = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size     = sizeof(DrawVGContext),
    .init          = drawvg_init,
    .uninit        = drawvg_uninit,
    FILTER_INPUTS(drawvg_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(drawvg_pix_fmts),
};
