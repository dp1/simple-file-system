#define _POSIX_C_SOURCE 1
#define _GNU_SOURCE
#include "simplefs.h"
#include "util.h"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#define BOLDBLUE "\e[1;34m"
#define ENDCOLOR "\e[0m"
#define MAX_CMD_LEN 4096

DiskDriver disk;
SimpleFS fs;
DirectoryHandle *cwd = NULL;
char cmd[MAX_CMD_LEN]; // current command
char *cwd_path = NULL; // full path of the current directory
int cwd_path_cap = 0;

void do_format(int argc, char **argv) {
    printf("This will erase all data, continue? [y/N] ");
    
    char ch;
    scanf("%c", &ch);
    while(getchar() != '\n');
    if(ch != 'y') {
        puts("Aborted");
        return;
    }

    SimpleFS_format(&fs);
    cwd = SimpleFS_init(&fs, &disk);

    if(cwd_path) free(cwd_path);
    cwd_path_cap = 64;
    cwd_path = (char *) calloc(sizeof(char), cwd_path_cap);
    ONERROR(cwd_path == NULL, "calloc failed");
    strncpy(cwd_path, cwd->dcb->fcb.name, cwd_path_cap);

    puts("Done");
}

void do_mkdir(int argc, char **argv) {
    if(SimpleFS_mkDir(cwd, argv[1]) == -1) {
        fprintf(stderr, "Operation failed\n");
    }
}

void do_touch(int argc, char **argv) {
    FileHandle *fh = SimpleFS_createFile(cwd, argv[1]);

    if(!fh) {
        fprintf(stderr, "Operation failed\n");
        return;
    }

    SimpleFS_close(fh);
}

void do_cd(int argc, char **argv) {
    if(SimpleFS_changeDir(cwd, argv[1]) == -1) {
        fprintf(stderr, "%s: not found\n", argv[1]);
    } else {

        // update the current path
        if(!strcmp("/", argv[1])) {
            strcpy(cwd_path, "/");
        } else if(!strcmp("..", argv[1])) {
            char *pos = strrchr(cwd_path, '/');
            if(pos) {
                if(pos == cwd_path) *(pos+1) = 0;
                else *pos = 0;
            }
        } else if(!strcmp(".", argv[1])) {
            // do nothing
        } else {
            if(strlen(cwd_path) + strlen(argv[1]) + 2 > cwd_path_cap) {
                cwd_path_cap = max(cwd_path_cap * 2, strlen(cwd_path) + strlen(argv[1]) + 2);
                cwd_path = (char *) realloc(cwd_path, cwd_path_cap * sizeof(char));
                ONERROR(cwd_path == NULL, "realloc failed");
            }
            if(strcmp(cwd_path, "/") != 0) {
                strcat(cwd_path, "/");
            }
            strcat(cwd_path, argv[1]);
        }
    }
}

typedef struct {
    char *name;
    bool is_dir;
    int size;
} ls_item;

int ls_compare(const void *_a, const void *_b) {
    ls_item *a = (ls_item *)_a;
    ls_item *b = (ls_item *)_b;

    return strcmp(a->name, b->name);
}

void do_ls(int argc, char **argv) {
    int num_entries = cwd->dcb->num_entries;
    ls_item *entries = (ls_item *) malloc(num_entries * sizeof(ls_item));
    char **names = (char **) malloc(num_entries * sizeof(char *));
    assert(entries != NULL && names != NULL);

    if(SimpleFS_readDir(names, cwd) == -1) {
        fprintf(stderr, "Operation failed\n");
        return;
    }

    for(int i = 0; i < num_entries; i++) {
        entries[i].name = names[i];

        FileHandle *fh = SimpleFS_openFile(cwd, names[i]);
        if(fh) {
            entries[i].size = fh->fcb->fcb.size_in_bytes;
            entries[i].is_dir = false;
            SimpleFS_close(fh);
        } else {
            entries[i].size = 0;
            entries[i].is_dir = true;
        }
    }

    qsort(entries, num_entries, sizeof(ls_item), ls_compare);

    printf("%s:\n", cwd->dcb->fcb.name);
    for(int i = 0; i < num_entries; i++) {
        if(entries[i].is_dir) {
            printf("  %s/\n", entries[i].name);
        } else {
            printf("  %d %s\n", entries[i].size, entries[i].name);
        }
    }

    for(int i = 0; i < num_entries; i++) {
        free(names[i]);
    }
    free(entries);
    free(names);
}

// Used to keep the prefix for the current line in tree
static char *tree_prefix = NULL;
static int tree_prefix_len, tree_prefix_cap;

void tree_aux(int depth) {
    int num_entries = cwd->dcb->num_entries;
    ls_item *entries = (ls_item *) malloc(num_entries * sizeof(ls_item));
    char **names = (char **) malloc(num_entries * sizeof(char *));
    assert(entries != NULL && names != NULL);

    if(SimpleFS_readDir(names, cwd) == -1) {
        fprintf(stderr, "Operation failed\n");
        return;
    }

    for(int i = 0; i < num_entries; i++) {
        entries[i].name = names[i];

        FileHandle *fh = SimpleFS_openFile(cwd, names[i]);
        if(fh) {
            entries[i].size = fh->fcb->fcb.size_in_bytes;
            entries[i].is_dir = false;
            SimpleFS_close(fh);
        } else {
            entries[i].size = 0;
            entries[i].is_dir = true;
        }
    }

    qsort(entries, num_entries, sizeof(ls_item), ls_compare);

    // "│   " is 7 bytes long, make sure there's enough space to append it to the prefix
    if(tree_prefix_cap < tree_prefix_len + 8) {
        tree_prefix_cap = max(tree_prefix_cap * 2, tree_prefix_cap + 8);
        tree_prefix = (char *) realloc(tree_prefix, tree_prefix_cap * sizeof(char));
        ONERROR(tree_prefix == NULL, "realloc failed");
    }

    for(int i = 0; i < num_entries; i++) {
        
        printf("%s", tree_prefix);
        if(i < num_entries - 1) printf("├── ");
        else printf("└── ");

        if(entries[i].is_dir) {
            printf("%s\n", entries[i].name);
            SimpleFS_changeDir(cwd, entries[i].name);

            int prefix_item_len = 0;
            if(i < num_entries - 1) {
                memcpy(tree_prefix + tree_prefix_len, "│   ", sizeof("│   "));
                prefix_item_len = sizeof("│   ") - 1;
            } else {
                memcpy(tree_prefix + tree_prefix_len, "    ", 5);
                prefix_item_len = 4;
            }
            tree_prefix_len += prefix_item_len;

            tree_aux(depth + 1);
            
            tree_prefix_len -= prefix_item_len;
            tree_prefix[tree_prefix_len] = 0;

            SimpleFS_changeDir(cwd, "..");
        } else {
            printf("%s\n", entries[i].name);
        }
    }

    for(int i = 0; i < num_entries; i++) {
        free(names[i]);
    }
    free(entries);
    free(names);
}

void do_tree(int argc, char **argv) {
    tree_prefix_cap = 64;
    tree_prefix = (char *) calloc(1, tree_prefix_cap * sizeof(char));
    ONERROR(tree_prefix == NULL, "calloc failed");

    printf("%s\n", cwd->dcb->fcb.name);
    tree_aux(0);

    free(tree_prefix);
    tree_prefix = NULL;
}

void do_cat(int argc, char **argv) {
    char buf[512];
    FileHandle *fh = SimpleFS_openFile(cwd, argv[1]);
    if(!fh) {
        fprintf(stderr, "%s: not found\n", argv[1]);
        return;
    }

    while(1) {
        int sz = SimpleFS_read(fh, buf, sizeof(buf));
        if(sz == -1) {
            fprintf(stderr, "read: operation failed\n");
            break;
        }

        if(sz == 0) break;
        for(int i = 0; i < sz; i++) putchar(buf[i]);
    }
    putchar('\n');

    SimpleFS_close(fh);
}

void do_write(int argc, char **argv) {
    FileHandle *fh = SimpleFS_openFile(cwd, argv[1]);
    if(!fh) {

        // File not found, try to create it
        fh = SimpleFS_createFile(cwd, argv[1]);

        if(!fh) {
            fprintf(stderr, "%s: file creation failed\n", argv[1]);
            return;
        }
    }

    if(SimpleFS_write(fh, argv[2], strlen(argv[2])) == -1) {
        fprintf(stderr, "write: operation failed\n");
    }

    SimpleFS_close(fh);
}

void do_rm(int argc, char **argv) {
    if(SimpleFS_remove(cwd, argv[1]) == -1) {
        fprintf(stderr, "Operation failed\n");
    }
}

void do_help(int argc, char **argv);

typedef void (*handler_fn)(int, char **);
typedef struct {
    char *name;
    handler_fn fn;
    int num_arguments;
    char *argument_names;
    char *description;
} handler_t;

handler_t handlers[] = {
    {"mkdir",  do_mkdir, 1, "<dir>", "create directory <dir> in the current directory"},
    {"touch",  do_touch, 1, "<file>", "create empty file <file> in the current directory"},
    {"cd",     do_cd, 1, "<dir>", "move in directory <dir> from the current directory"},
    {"ls",     do_ls, 0, "", "print the contents of the current directory"},
    {"tree",   do_tree, 0, "", "recursively print the contents of the current directory"},
    {"cat",    do_cat, 1, "<file>", "print the contents of file <file>"},
    {"write",  do_write, 2, "<file> <data>", "append <data> at the end of <file>, creating it if necessary"},
    {"rm",     do_rm, 1, "<file|dir>", "remove the specified file or directory"},
    {"format", do_format, 0, "", "format the filesystem"},
    {"help",   do_help, 0, "", "print this message"},
    {"exit",   NULL, 0, "", "exit the shell"}
};

void do_help(int argc, char **argv) {
    puts("SimpleFS shell, available commands:");
    
    int col_size = 0;
    for(int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++) {
        col_size = max(col_size, strlen(handlers[i].name) + strlen(handlers[i].argument_names) + 4);
    }
    
    assert(col_size < 63);
    char col[64] = {0};

    for(int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++) {
        bzero(col, sizeof(col));
        sprintf(col, "%s %s   ", handlers[i].name, handlers[i].argument_names);
        printf(" %-*s%s\n", col_size, col, handlers[i].description);
    }
}


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

                // Escaped \, ' and "
                if(cmd[i] == '\\') {
                    if(i+1 >= cmd_len) {
                        fprintf(stderr, "Invalid input: incomplete escape sequence \\\n");
                        free(output);
                        return NULL;
                    }
                    i++;
                    if(!strchr("\\\'\"", cmd[i])) {
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

    DiskDriver_init(&disk, "simple.fs", 1024);
    cwd = SimpleFS_init(&fs, &disk);
    if(!cwd) {
        fprintf(stderr, "Error opening filesystem\n");
        exit(EXIT_FAILURE);
    }

    cwd_path_cap = 64;
    cwd_path = (char *) calloc(sizeof(char), cwd_path_cap);
    ONERROR(cwd_path == NULL, "calloc failed");
    strncpy(cwd_path, cwd->dcb->fcb.name, cwd_path_cap);
    
    while(1) {
        bzero(cmd, sizeof(cmd));

        printf(BOLDBLUE "%s" ENDCOLOR "$ ", cwd_path);
        if(fgets(cmd, sizeof(cmd), stdin) == 0) {
            perror("fgets failed");
            exit(EXIT_FAILURE);
        }

        int num_tokens = 0;
        char **parsed = parse_cmd(cmd, &num_tokens);
        if(!parsed || num_tokens == 0) {
            if(parsed) free(parsed);
            continue;
        }

        if(!strcmp(parsed[0], "exit")) {
            free(parsed);
            break;
        }

        bool found = false;
        for(int i = 0; i < sizeof(handlers)/sizeof(handlers[0]); i++) {
            if(!strcmp(parsed[0], handlers[i].name)) {
                if(!handlers[i].fn) {
                    puts("Unimplemented");
                } else {

                    // Right number of arguments?
                    if(num_tokens != handlers[i].num_arguments + 1) {
                        fprintf(stderr, "Usage: %s %s\n", handlers[i].name, handlers[i].argument_names);
                    } else {
                        handlers[i].fn(num_tokens, parsed);
                    }
                }

                found = true;
                break;
            }
        }
        if(!found) {
            printf("%s: command not found\n", parsed[0]);
        }

        free(parsed);
    }

    free(cwd_path);
    DiskDriver_flush(&disk);
}
