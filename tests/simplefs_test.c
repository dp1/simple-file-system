#include "simplefs.h"
#include <stdio.h>
#include <stdlib.h>

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

    char name[64];
    for(int i = 0; i < 1; i++) {
        sprintf(name, "test%d.txt", i);
        FileHandle *fh = SimpleFS_createFile(dir, name);
        if(fh && i == 99) FileHandle_print(fh);
        SimpleFS_close(fh);
    }
    
    SimpleFS_mkDir(dir, "subdir");
    SimpleFS_changeDir(dir, "subdir");
    SimpleFS_mkDir(dir, "sub2");
    SimpleFS_changeDir(dir, "sub2");

    for(int i = 0; i < 100; i++) {
        sprintf(name, "inner%d.txt", i);
        SimpleFS_close(SimpleFS_createFile(dir, name));
    }


    DirectoryHandle_print(dir);
    readdir(dir);

    SimpleFS_changeDir(dir, "..");
    SimpleFS_changeDir(dir, "..");
    SimpleFS_remove(dir, "subdir");

    DirectoryHandle_print(dir);
    readdir(dir);

    SimpleFS_closeDir(dir);

    DiskDriver_flush(&disk);
}
