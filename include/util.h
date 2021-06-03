#pragma once
#include <stdio.h>
#include <stdlib.h>

#define DEBUG

#define ONERROR(cond, msg) \
    do { \
        if(cond) { \
            fprintf(stderr, "[%s:%d] ERROR: %s\n", __FILE__, __LINE__, msg); \
            exit(EXIT_FAILURE); \
        } \
    } while(0)

#if defined(DEBUG)
# define DBGPRINT(fmt, ...) fprintf(stderr, "[" __FILE__ ":%d] " fmt "\n", __LINE__, ##__VA_ARGS__)
#else
# define DBGPRINT(fmt, ...)
#endif
