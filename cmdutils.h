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
    union {
        void (*func_arg)(const char *);
        int *int_arg;
        char **str_arg;
    } u;
    const char *help;
    const char *argname;
} OptionDef;

void show_help_options(const OptionDef *options, const char *msg, int mask, int value);
void parse_options(int argc, char **argv, const OptionDef *options);
void parse_arg_file(const char *filename);
void print_error(const char *filename, int err);

#endif /* _CMD_UTILS_H */
