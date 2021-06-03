#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "disk_driver.h"


int main(int argc, char **argv) {
    DiskDriver disk;
    DiskDriver_init(&disk, "data.fs", 128);
    
    char block[BLOCK_SIZE];
    memset(block, 'a', BLOCK_SIZE);
    
    printf("%d\n", DiskDriver_getFreeBlock(&disk, 0));
    assert(DiskDriver_writeBlock(&disk, block, 1) == 0);

    printf("%d\n", DiskDriver_getFreeBlock(&disk, 0));
    DiskDriver_writeBlock(&disk, block, 0);
    printf("%d\n", DiskDriver_getFreeBlock(&disk, 0));

    DiskDriver_flush(&disk);

    char block2[BLOCK_SIZE];
    memset(block2, 0, BLOCK_SIZE);
    assert(DiskDriver_readBlock(&disk, block2, 1) == 0);
    assert(memcmp(block, block2, BLOCK_SIZE) == 0);
    assert(DiskDriver_readBlock(&disk, block2, 2) == -1);

    printf("Disk driver tests OK\n");
}
