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
    int metadata_size = num_blocks * BLOCK_SIZE + bitmap_size + sizeof(DiskHeader);
    // Round the metadata size so that the data blocks are BLOCK_SIZE bytes aligned
    metadata_size = ((metadata_size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

    bool is_new_file = false;

    // First try to create the file, if this call fails the file already exists
    int fd = open(filename, O_RDWR | O_CREAT | O_EXCL, 0777);
    if(fd != -1) {
        // New file
        int res = ftruncate(fd, metadata_size);
        ONERROR(res == -1, "[disk driver] Can't resize file");
        is_new_file = true;
    } else {
        fd = open(filename, O_RDWR);
        ONERROR(fd == -1, "[disk driver] Can't open backing file");
    }

    disk->fd = fd;
    disk->header = mmap(NULL, sizeof(DiskHeader), PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
    disk->bitmap_data = mmap(NULL, sizeof(DiskHeader), PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, sizeof(DiskHeader));
    disk->metadata_size = metadata_size;

    if(is_new_file) {
        disk->header->num_blocks = num_blocks;
        disk->header->free_blocks = num_blocks;
        disk->header->bitmap_entries = bitmap_size;
        disk->header->bitmap_blocks = num_blocks;

        bzero(disk->bitmap_data, bitmap_size);
    }
}

int DiskDriver_readBlock(DiskDriver* disk, void* dest, int block_num) {
    BitMap map = {
        .entries = disk->bitmap_data,
        .num_bits = disk->header->num_blocks
    };

    int status = BitMap_get(&map, block_num);
    if(status == 1) {
        
        int to_read = BLOCK_SIZE;
        lseek(disk->fd, disk->metadata_size + block_num * BLOCK_SIZE, SEEK_SET);
        while(to_read > 0) {
            int res = read(disk->fd, dest + to_read, to_read);
            if(res == -1 && errno == EAGAIN) continue;
            if(res == -1) return -1;

            to_read += res;
        }

        return 0;
    }

    return -1;
}

int DiskDriver_writeBlock(DiskDriver* disk, void* src, int block_num) {
    BitMap map = {
        .entries = disk->bitmap_data,
        .num_bits = disk->header->num_blocks
    };

    int status = BitMap_get(&map, block_num);
    if(status == -1) return -1;

    int to_write = BLOCK_SIZE;
    lseek(disk->fd, disk->metadata_size + block_num * BLOCK_SIZE, SEEK_SET);
    while(to_write > 0) {
        int res = write(disk->fd, src + to_write, to_write);
        if(res == -1 && errno == EAGAIN) continue;
        if(res == -1) return -1;

        to_write += res;
    }

    if(status == 0) {
        disk->header->free_blocks--;
    }
    BitMap_set(&map, block_num, 1);
    return 0;
}

int DiskDriver_freeBlock(DiskDriver* disk, int block_num) {
    BitMap map = {
        .entries = disk->bitmap_data,
        .num_bits = disk->header->num_blocks
    };

    int res = BitMap_set(&map, block_num, 0);
    if(res != 1) {
        disk->header->free_blocks++;
    }
    return res;
}

int DiskDriver_getFreeBlock(DiskDriver* disk, int start) {
    BitMap map = {
        .entries = disk->bitmap_data,
        .num_bits = disk->header->num_blocks
    };

    return BitMap_find(&map, start, 0);
}

int DiskDriver_flush(DiskDriver* disk) {
    int res = msync(disk->header, sizeof(DiskHeader), MS_SYNC);
    ONERROR(res == -1, "[disk driver] msync 1 failed");
    res = msync(disk->bitmap_data, disk->header->bitmap_entries, MS_SYNC);
    ONERROR(res == -1, "[disk driver] msync 2 failed");

    return 0;
}
