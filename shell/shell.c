#define _POSIX_C_SOURCE 1
#define _GNU_SOURCE
#include "simplefs.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_CMD_LEN 4096

DiskDriver disk;
SimpleFS fs;
DirectoryHandle *cwd = NULL;
char cmd[MAX_CMD_LEN]; // current command

void do_help(int argc, char **argv) {
    puts("SimpleFS shell, available commands:");
    puts("  mkdir <dir>           create directory <dir> in the current directory");
    puts("  touch <file>          create empty file <file> in the current directory");
    puts("  cd <dir>              move in directory <dir> from the current directory");
    puts("  ls                    print the contents of the current directory");
    puts("  tree                  recursively print the contents of the current directory");
    puts("  cat <file>            print the contents of file <file>");
    puts("  write <file> <data>   append <data> at the end of <file>, creating it if necessary");
    puts("  rm <file|dir>         remove the specified file or directory");
    puts("  format                format the filesystem");
    puts("  exit                  exit the shell");
    puts("  help                  print this message");
}

typedef void (*handler_fn)(int, char **);
typedef struct {
    char *name;
    handler_fn fn;
} handler_t;

handler_t handlers[] = {
    {"help", do_help}
};

// Parse the string cmd into an argument list, considering quoted strings and escaped characters
// Modifies cmd in place, returns the token array and saves its length in *num_tokens
char **parse_cmd(char *cmd, int *num_tokens) {
    int cmd_len = strlen(cmd);
    
    char **output = (char **) malloc(sizeof(char *));
    int output_size = 0, output_capacity = 1;

    for(int i = 0; i < cmd_len; i++) {
        if(cmd[i] == ' ' || cmd[i] == '\n') {
            cmd[i] = 0;
            continue;
        }

        // Parse the next token
        if(output_size == output_capacity) {
            output_capacity *= 2;
            output = (char **) realloc(output, output_capacity * sizeof(char *));
        }
        output[output_size++] = cmd + i;

        // In caso of quoted strings we will need to move the characters
        // so keep the write pointer for the update
        char *out = cmd + i;
        char quote_char = 0;

        while(i < cmd_len) {
            if(quote_char == 0 && (cmd[i] == '"' || cmd[i] == '\'')) {
                // Open quoted string
                quote_char = cmd[i];
            } else if(quote_char != 0 && cmd[i] == quote_char) {
                // End of quoted string
                quote_char = 0;
            } else {
                // All other cases
                if(quote_char == 0 && (cmd[i] == ' ' || cmd[i] == '\n')) {
                    // End of token
                    cmd[i] = 0;
                    *out = 0;
                    break;
                }

                // Escaped " and '
                if(cmd[i] == '\\') {
                    if(i+1 >= cmd_len) {
                        fprintf(stderr, "Invalid input: incomplete escape sequence \\\n");
                        free(output);
                        return NULL;
                    }
                    i++;
                    if(cmd[i] != '\'' && cmd[i] != '"') {
                        fprintf(stderr, "Invalid input: unknown escape sequence \\%c\n", cmd[i]);
                        free(output);
                        return NULL;
                    }
                }
                *out++ = cmd[i];
            }
            i++;
        }

        // Open quoted string
        if(quote_char != 0) {
            fprintf(stderr, "Invalid input: quote character (%c) never closed\n", quote_char);
            free(output);
            return NULL;
        }
    }

    *num_tokens = output_size;
    return output;
}

int main(int argc, char **argv) {
    
    while(1) {
        bzero(cmd, sizeof(cmd));
        
        if(fgets(cmd, sizeof(cmd), stdin) == 0) {
            perror("fgets failed");
            exit(EXIT_FAILURE);
        }
        int num_tokens;
        char **parsed = parse_cmd(cmd, &num_tokens);
        if(!parsed) {
            continue;
        }

        if(!strcmp(parsed[0], "quit")) {
            free(parsed);
            break;
        }
        for(int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++) {
            if(!strcmp(parsed[0], handlers[i].name)) {
                handlers[i].fn(num_tokens, parsed);
                break;
            }
        }

        free(parsed);
    }
}
