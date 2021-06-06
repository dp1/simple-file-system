#define _GNU_SOURCE
#include "disk_driver.h"
#include "util.h"
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

void DiskDriver_init(DiskDriver* disk, const char* filename, int num_blocks) {

    int bitmap_size = (num_blocks + 7) / 8; // round up
    int metadata_size = bitmap_size + sizeof(DiskHeader);
    // Round the metadata size so that the data blocks are BLOCK_SIZE bytes aligned
    metadata_size = ((metadata_size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;
    int total_size = metadata_size + num_blocks * BLOCK_SIZE;

    bool is_new_file = false;

    // First try to create the file, if this call fails the file already exists
    int fd = open(filename, O_RDWR | O_CREAT | O_EXCL, 0777);
    if(fd != -1) {
        // New file
        DBGPRINT("creating new file");
        int res = ftruncate(fd, total_size);
        ONERROR(res == -1, "Can't resize file");
        is_new_file = true;
    } else {
        DBGPRINT("opening existing file");
        fd = open(filename, O_RDWR);
        ONERROR(fd == -1, "Can't open backing file");
    }

    char *metadata = mmap(NULL, total_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    ONERROR(metadata == MAP_FAILED, "can't mmap header and bitmap");

    disk->fd = fd;
    disk->header = (DiskHeader *) metadata;
    disk->bitmap.entries = metadata + sizeof(DiskHeader);
    disk->bitmap.num_bits = num_blocks;
    disk->metadata_size = metadata_size;

    if(is_new_file) {
        disk->header->num_blocks = num_blocks;
        disk->header->free_blocks = num_blocks;
        disk->header->bitmap_entries = bitmap_size;
        disk->header->bitmap_blocks = num_blocks;

        bzero(disk->bitmap.entries, bitmap_size);
    } else {
        // Some sanity checks when opening an existing file
        ONERROR(disk->header->num_blocks != num_blocks, "file has %d blocks (not %d)",
            disk->header->num_blocks, num_blocks);
        ONERROR(disk->header->free_blocks > num_blocks, "file has more free blocks (%d) than total blocks (%d)",
            disk->header->free_blocks, num_blocks);
        ONERROR(disk->header->bitmap_blocks != num_blocks, "bitmap size (%d) doesn't match total number of blocks (%d)",
            disk->header->bitmap_blocks, num_blocks);
    }
}

int DiskDriver_readBlock(DiskDriver* disk, void* dest, int block_num) {

    int status = BitMap_get(&disk->bitmap, block_num);
    if(status == 1) {
        
        int to_read = BLOCK_SIZE;
        lseek(disk->fd, disk->metadata_size + block_num * BLOCK_SIZE, SEEK_SET);
        while(to_read > 0) {
            int res = read(disk->fd, dest, to_read);
            if(res == -1 && errno == EAGAIN) continue;
            if(res == -1) return -1;

            to_read -= res;
            dest += res;
        }

        return 0;
    }

    return -1;
}

int DiskDriver_writeBlock(DiskDriver* disk, void* src, int block_num) {

    int status = BitMap_get(&disk->bitmap, block_num);
    if(status == -1) return -1;

    int to_write = BLOCK_SIZE;
    lseek(disk->fd, disk->metadata_size + block_num * BLOCK_SIZE, SEEK_SET);
    while(to_write > 0) {
        int res = write(disk->fd, src, to_write);
        if(res == -1 && errno == EAGAIN) continue;
        if(res == -1) return -1;

        to_write -= res;
        src += res;
    }

    if(status == 0) {
        disk->header->free_blocks--;
    }
    BitMap_set(&disk->bitmap, block_num, 1);
    return 0;
}

int DiskDriver_freeBlock(DiskDriver* disk, int block_num) {

    int prev = BitMap_get(&disk->bitmap, block_num);
    int res = BitMap_set(&disk->bitmap, block_num, 0);
    if(res != -1 && prev == 1) {
        disk->header->free_blocks++;
    }
    return res;
}

int DiskDriver_getFreeBlock(DiskDriver* disk, int start) {

    return BitMap_find(&disk->bitmap, start, 0);
}

int DiskDriver_flush(DiskDriver* disk) {
    int res = msync(disk->header, disk->metadata_size, MS_SYNC);
    ONERROR(res == -1, "msync failed");

    return 0;
}

void DiskDriver_print(DiskDriver *disk) {
    printf("Diskdriver(\n");
    printf("  metadata_size = %d,\n", disk->metadata_size);
    printf("  num_blocks = %d,\n", disk->header->num_blocks);
    printf("  bitmap_blocks = %d,\n", disk->header->bitmap_blocks);
    printf("  bitmap_entries = %d,\n", disk->header->bitmap_entries);
    printf("  free_blocks = %d\n", disk->header->free_blocks);
    printf(")\n");
}
