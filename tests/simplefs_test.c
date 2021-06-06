#include "simplefs.h"
#include <stdio.h>
#include <stdlib.h>

int main(int agc, char** argv) {
    printf("FirstBlock size %ld\n", sizeof(FirstFileBlock));
    printf("DataBlock size %ld\n", sizeof(FileBlock));
    printf("FirstDirectoryBlock size %ld\n", sizeof(FirstDirectoryBlock));
    printf("DirectoryBlock size %ld\n", sizeof(DirectoryBlock));

    DiskDriver disk;
    DiskDriver_init(&disk, "data.fs", 128);

    SimpleFS fs;
    DirectoryHandle *dir = SimpleFS_init(&fs, &disk);

    char name[64];
    for(int i = 0; i < 100; i++) {
        sprintf(name, "test%d.txt", i);
        FileHandle *fh = SimpleFS_createFile(dir, name);
        if(fh && i == 99) FileHandle_print(fh);
        SimpleFS_close(fh);
    }

    DirectoryHandle_print(dir);

    printf("readDir:\n");
    char *names[100];
    int num_names = SimpleFS_readDir(names, dir);
    for(int i = 0; i < num_names; i++) {
        printf("  %s\n", names[i]);
    }
    for(int i = 0; i < num_names; i++) free(names[i]);

    printf("Reopening file\n");
    FileHandle *fh = SimpleFS_openFile(dir, "test98.txt");
    FileHandle_print(fh);
    SimpleFS_close(fh);

    SimpleFS_mkDir(dir, "folder");
    SimpleFS_closeDir(dir);

    DiskDriver_flush(&disk);

    dir = SimpleFS_init(&fs, &disk);
    
    DirectoryHandle_print(dir);
    SimpleFS_closeDir(dir);
}
