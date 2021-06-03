#pragma once
#include <stdio.h>
#include <stdlib.h>

#define ONERROR(cond, msg) \
    do { \
        if(cond) { \
            fprintf(stderr, "ERROR %s\n", msg); \
            exit(EXIT_FAILURE); \
        } \
    } while(0)
