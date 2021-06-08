#include "simplefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void readdir(DirectoryHandle *dir) {
    printf("readDir:\n");
    char *names[100];
    int num_names = SimpleFS_readDir(names, dir);
    for(int i = 0; i < num_names; i++) {
        printf("  %s\n", names[i]);
    }
    for(int i = 0; i < num_names; i++) free(names[i]);
}

int main(int agc, char** argv) {
    printf("FirstBlock size %ld\n", sizeof(FirstFileBlock));
    printf("DataBlock size %ld\n", sizeof(FileBlock));
    printf("FirstDirectoryBlock size %ld\n", sizeof(FirstDirectoryBlock));
    printf("DirectoryBlock size %ld\n", sizeof(DirectoryBlock));

    DiskDriver disk;
    DiskDriver_init(&disk, "data.fs", 128);

    SimpleFS fs;
    DirectoryHandle *dir = SimpleFS_init(&fs, &disk);

    FileHandle *fh = SimpleFS_createFile(dir, "text.txt");
    if(!fh) fh = SimpleFS_openFile(dir, "text.txt");

    char buf[1024] = {0};
    for(int i = 0; i < 1024; i += 16) {
        memcpy(buf + i, "0123456789abcdef", 16);
    }

    SimpleFS_write(fh, buf, 1024);

    FileHandle_print(fh);
    SimpleFS_close(fh);

    fh = SimpleFS_openFile(dir, "text.txt");
    SimpleFS_seek(fh, 1000);
    for(int i = 0; i < 10; i++) {
        char ch;
        int r = SimpleFS_read(fh, &ch, 1);
        printf("%c ", ch);
    }
    printf("\n");

    SimpleFS_closeDir(dir);

    DiskDriver_flush(&disk);
}
