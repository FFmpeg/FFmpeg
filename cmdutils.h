#ifndef _CMD_UTILS_H
#define _CMD_UTILS_H

typedef struct {
    const char *name;
    int flags;
#define HAS_ARG    0x0001
#define OPT_BOOL   0x0002
#define OPT_EXPERT 0x0004
#define OPT_STRING 0x0008
#define OPT_VIDEO  0x0010
#define OPT_AUDIO  0x0020
#define OPT_GRAB   0x0040
#define OPT_INT    0x0080
#define OPT_FLOAT  0x0100
#define OPT_SUBTITLE 0x0200
#define OPT_FUNC2  0x0400
     union {
        void (*func_arg)(const char *); //FIXME passing error code as int return would be nicer then exit() in the func
        int *int_arg;
        char **str_arg;
        float *float_arg;
        int (*func2_arg)(const char *, const char *);
    } u;
    const char *help;
    const char *argname;
} OptionDef;

void show_help_options(const OptionDef *options, const char *msg, int mask, int value);
void parse_options(int argc, char **argv, const OptionDef *options);
void parse_arg_file(const char *filename);
void print_error(const char *filename, int err);

#endif /* _CMD_UTILS_H */
