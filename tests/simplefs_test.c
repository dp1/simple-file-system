#include "simplefs.h"
#include <stdio.h>

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
        printf("%d\n", i);
        sprintf(name, "test%d.txt", i);
        FileHandle *fh = SimpleFS_createFile(dir, name);
        if(fh && i == 99) FileHandle_print(fh);
        SimpleFS_close(fh);
    }

    DirectoryHandle_print(dir);

    DiskDriver_flush(&disk);
}
