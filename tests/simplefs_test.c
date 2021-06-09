#include "simplefs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    srand(42);

    printf("FirstBlock size %ld\n", sizeof(FirstFileBlock));
    printf("DataBlock size %ld\n", sizeof(FileBlock));
    printf("FirstDirectoryBlock size %ld\n", sizeof(FirstDirectoryBlock));
    printf("DirectoryBlock size %ld\n", sizeof(DirectoryBlock));

    unlink("data.fs");
    DiskDriver disk;
    DiskDriver_init(&disk, "data.fs", 1024);

    SimpleFS fs;
    DirectoryHandle *dir = SimpleFS_init(&fs, &disk);

    char buf[4096];
    char buf2[4096];

    printf("Creating test.txt... ");
    FileHandle *fh = SimpleFS_createFile(dir, "test.txt");
    assert(fh != NULL);
    printf("OK\n");

    printf("Filling test.txt & reading it back... ");
    assert(SimpleFS_write(fh, "lorem ipsum dolor sit amet", 27) == 27);
    assert(SimpleFS_seek(fh, 0) == -27);
    assert(SimpleFS_read(fh, buf, 4096) == 27);
    assert(memcmp("lorem ipsum dolor sit amet", buf, 27) == 0);
    printf("OK\n");

    printf("Filling test.txt with 4k of data & reading it... ");
    for(int i = 0; i < 4096; i++) buf[i] = rand() % 256;
    assert(SimpleFS_seek(fh, 0) == -27);
    assert(SimpleFS_write(fh, buf, 4096) == 4096);
    assert(SimpleFS_seek(fh, 0) == -4096);
    assert(SimpleFS_read(fh, buf2, 4096) == 4096);
    assert(memcmp(buf, buf2, 4096) == 0);
    printf("OK\n");

    printf("Filling test.txt with 4k of data, read/write in multiple calls... ");
    for(int i = 0; i < 4096; i++) buf[i] = rand() % 256;
    assert(SimpleFS_seek(fh, 0) == -4096);
    for(int i = 0; i < 4096; i += 64)
        assert(SimpleFS_write(fh, buf+i, 64) == 64);
    assert(SimpleFS_seek(fh, 0) == -4096);
    for(int i = 0; i < 4096; i += 64)
        assert(SimpleFS_read(fh, buf2+i, 64) == 64);
    assert(memcmp(buf, buf2, 4096) == 0);
    printf("OK\n");

    assert(SimpleFS_close(fh) == 0);
    fh = SimpleFS_openFile(dir, "test.txt");
    assert(fh != NULL);

    printf("Reading at random places from test.txt... ");
    for(int i = 0; i < 100; i++) {
        int pos = rand() % (4096-16);
        SimpleFS_seek(fh, pos);
        assert(SimpleFS_read(fh, buf2, 16) == 16);
        assert(memcmp(buf+pos, buf2, 16) == 0);
    }
    assert(SimpleFS_close(fh) == 0); fh = NULL;
    printf("OK\n");

    printf("Creating /a, /b, /a/c, /a/d, /a/e and testing changeDir... ");
    assert(SimpleFS_mkDir(dir, "a") == 0);
    assert(SimpleFS_mkDir(dir, "b") == 0);
    assert(SimpleFS_changeDir(dir, "a") == 0);
    assert(strcmp("a", dir->dcb->fcb.name) == 0);
    assert(SimpleFS_changeDir(dir, ".") == 0);
    assert(strcmp("a", dir->dcb->fcb.name) == 0);

    assert(SimpleFS_mkDir(dir, "c") == 0);
    assert(SimpleFS_mkDir(dir, "d") == 0);
    assert(SimpleFS_mkDir(dir, "e") == 0);
    assert(SimpleFS_changeDir(dir, "..") == 0);
    assert(strcmp("/", dir->dcb->fcb.name) == 0);
    assert(SimpleFS_changeDir(dir, "..") == -1);
    assert(SimpleFS_changeDir(dir, "invalid-name") == -1);
    printf("OK\n");

    printf("Creating 200 files in /a/c... ");
    SimpleFS_changeDir(dir, "a");
    SimpleFS_changeDir(dir, "c");
    for(int i = 0; i < 200; i++) {
        char name[60];
        sprintf(name, "file%d.txt", i);
        fh = SimpleFS_createFile(dir, name);
        assert(fh != NULL);
        SimpleFS_close(fh);
    }

    char *names[200];
    assert(SimpleFS_readDir(names, dir) == 200);

    for(int i = 0; i < 200; i++) {
        char name[60];
        sprintf(name, "file%d.txt", i);
        assert(strcmp(name, names[i]) == 0);
        free(names[i]);
    }
    
    printf("OK\n");

    printf("Removing /a/c/file0.txt, /a and /test.txt... ");
    int free_blocks = fs.disk->header->free_blocks;
    assert(SimpleFS_remove(dir, "file0.txt") == 0);
    assert(fs.disk->header->free_blocks == free_blocks + 1);
    SimpleFS_changeDir(dir, "/");
    assert(SimpleFS_remove(dir, "test.txt") == 0);
    assert(SimpleFS_remove(dir, "a") == 0);
    assert(fs.disk->header->free_blocks == free_blocks + 214);
    printf("OK\n");

    DiskDriver_flush(&disk);
}
