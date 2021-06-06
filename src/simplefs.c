#define _GNU_SOURCE
#include "simplefs.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

DirectoryHandle *SimpleFS_init(SimpleFS *fs, DiskDriver *disk) {
    fs->disk = disk;
    fs->current_directory_block = 0;

    FirstDirectoryBlock *dcb = (FirstDirectoryBlock *) malloc(sizeof(FirstDirectoryBlock));
    if(DiskDriver_readBlock(disk, dcb, 0) != 0) {
        DBGPRINT("The disk seems to be empty. Formatting...");

        SimpleFS_format(fs);
        DiskDriver_readBlock(disk, dcb, 0);
    }

    DirectoryHandle *root = (DirectoryHandle *) calloc(1, sizeof(DirectoryHandle));
    root->sfs = fs;
    root->dcb = dcb;
    root->directory = NULL;
    root->current_block = &dcb->header;
    root->pos_in_block = 0;
    root->pos_in_dir = 0;
    return root;
}

void SimpleFS_format(SimpleFS *fs) {

    // Deallocate all blocks on disk
    for(int i = 0; i < fs->disk->header->num_blocks; i++) {
        DiskDriver_freeBlock(fs->disk, i);
    }

    FirstDirectoryBlock dcb;
    bzero(&dcb, sizeof(FirstDirectoryBlock));

    dcb.header.previous_block = 0;
    dcb.header.next_block = 0;
    dcb.header.block_in_file = 0;

    dcb.fcb.directory_block = -1;
    dcb.fcb.block_in_disk = 0;
    strcpy(dcb.fcb.name, "/");
    dcb.fcb.size_in_bytes = 0;
    dcb.fcb.size_in_blocks = 1;
    dcb.fcb.is_dir = 1;

    dcb.num_entries = 0;

    DiskDriver_writeBlock(fs->disk, &dcb, 0);
}

int SimpleFS_newDirBlock(DirectoryHandle *d) {
    DiskDriver *disk = d->sfs->disk;
    BlockHeader *cur_block = (BlockHeader *) d->dcb;
    int start_block_num = d->dcb->fcb.block_in_disk;
    int cur_block_num = start_block_num;
    char block[BLOCK_SIZE];

    while(cur_block->next_block != start_block_num) {
        cur_block_num = cur_block->next_block;
        DiskDriver_readBlock(disk, block, cur_block->next_block);
        cur_block = (BlockHeader *)block;
    }

    // Now cur_block is the last block in the list

    DirectoryBlock db;
    bzero(&db, sizeof(db));
    db.header.block_in_file = cur_block->block_in_file + 1;
    db.header.next_block = start_block_num;
    db.header.previous_block = cur_block_num;
    
    int new_pos = DiskDriver_getFreeBlock(disk, 0);
    if(new_pos == -1) {
        return -1;
    }

    DiskDriver_writeBlock(disk, &db, new_pos);

    cur_block->next_block = new_pos;
    DiskDriver_writeBlock(disk, cur_block, cur_block_num);

    d->dcb->header.previous_block = new_pos;
    d->dcb->fcb.size_in_blocks++;
    DiskDriver_writeBlock(disk, d->dcb, start_block_num);

    return new_pos;
}

FileHandle *SimpleFS_createFile(DirectoryHandle *d, const char *filename) {
    DiskDriver *disk = d->sfs->disk;
    char block[BLOCK_SIZE];

    DirectoryBlock db;
    int next_dir_block = d->dcb->header.next_block;

    for(int pos = 0, relative_pos = -1; pos < d->dcb->num_entries; pos++) {
        
        int file_block;
        
        if(pos < sizeof(d->dcb->file_blocks)/sizeof(d->dcb->file_blocks[0])) {
            // This is one of the files stored directly in the first block
            file_block = d->dcb->file_blocks[pos];
        } else {
            // Otherwise, it's in one of the other blocks. Read the next
            // block if necessary
            if(relative_pos == -1 || relative_pos == sizeof(db.file_blocks)/sizeof(db.file_blocks[0])) {
                relative_pos = 0;

                DiskDriver_readBlock(disk, &db, next_dir_block);
                next_dir_block = db.header.next_block;
            }
            file_block = db.file_blocks[relative_pos];
            relative_pos++;
        }
        
        DiskDriver_readBlock(disk, block, file_block);
        FirstFileBlock *ffb = (FirstFileBlock *)block;
        
        if(!strcmp(ffb->fcb.name, filename)) {
            DBGPRINT("found duplicate filename");
            return NULL; // File exists
        }
    }

    // There's no duplicate. Let's create the file

    int pos;
    if((pos = DiskDriver_getFreeBlock(disk, 0)) == -1) {
        return NULL; // No space left on disk
    }

    if(strlen(filename) >= 128) return NULL;

    FirstFileBlock *ffb = (FirstFileBlock *) calloc(1, sizeof(FirstFileBlock));
    ffb->header.block_in_file = pos;
    ffb->header.next_block = pos;
    ffb->header.previous_block = pos;
    ffb->fcb.directory_block = d->dcb->fcb.block_in_disk;
    ffb->fcb.block_in_disk = pos;
    strcpy(ffb->fcb.name, filename);
    ffb->fcb.size_in_bytes = 0;
    ffb->fcb.size_in_blocks = 1;
    ffb->fcb.is_dir = 0;

    DiskDriver_writeBlock(disk, ffb, pos);

    // Now add the file to the current directory

    int first_block_entries = sizeof(d->dcb->file_blocks)/sizeof(d->dcb->file_blocks[0]);

    if(d->dcb->num_entries < first_block_entries) {
        d->dcb->file_blocks[d->dcb->num_entries] = pos;
    } else {

        int cur_block_num = d->dcb->header.next_block;
        int relative_pos = d->dcb->num_entries - first_block_entries;
        int other_block_entries = sizeof(db.file_blocks)/sizeof(db.file_blocks[0]);

        // Allocate the first DirectoryBlock if it isn't there already
        if(cur_block_num == d->dcb->fcb.block_in_disk) {
            cur_block_num = SimpleFS_newDirBlock(d);
        }

        DiskDriver_readBlock(disk, block, cur_block_num);
        DirectoryBlock *cur_db = (DirectoryBlock *)block;

        while(relative_pos >= other_block_entries) {
            
            // Is this the end of the list? Allocate a new block if so
            if(cur_db->header.next_block == d->dcb->fcb.block_in_disk) {
                cur_block_num = SimpleFS_newDirBlock(d);
            } else {
                cur_block_num = cur_db->header.next_block;
            }

            DiskDriver_readBlock(disk, block, cur_block_num);
            
            relative_pos -= other_block_entries;
        }

        cur_db->file_blocks[relative_pos] = pos;
        DiskDriver_writeBlock(disk, cur_db, cur_block_num);
    }
    
    d->dcb->num_entries++;
    DiskDriver_writeBlock(disk, d->dcb, d->dcb->fcb.block_in_disk);

    // All good, nothing to see here

    FileHandle *fh = (FileHandle *) calloc(1, sizeof(FileHandle));
    fh->sfs = d->sfs;
    fh->fcb = ffb;
    fh->directory = d->dcb;
    fh->current_block = &ffb->header;
    fh->pos_in_file = 0;
    return fh;
}

int SimpleFS_close(FileHandle* f) {
    if(f) {
        free(f->fcb);
        free(f);
    }
    return 0;
}




void BlockHeader_print(BlockHeader *b, int spaces) {
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("BlockHeader(prev=%d, next=%d, block_in_file=%d)",
        b->next_block, b->previous_block, b->block_in_file);
}

void FileControlBlock_print(FileControlBlock *f, int spaces) {
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("FileControlBlock(\n");
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("  name=\"%s\", directory_block=%d,\n", f->name, f->directory_block);
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("  block_in_disk=%d, is_dir=%d,\n", f->block_in_disk, f->is_dir);
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("  size_in_bytes=%d, size_in_blocks=%d\n", f->size_in_bytes, f->size_in_blocks);
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf(")");
}

void FirstFileBlock_print(FirstFileBlock *f) {
    printf("FirstFileBlock(\n");
    BlockHeader_print(&f->header, 2);
    printf(",\n");
    FileControlBlock_print(&f->fcb, 2);
    printf(")");
}

void FileBlock_print(FileBlock *f) {
    printf("FileBlock(\n");
    BlockHeader_print(&f->header, 2);
    printf("\n)");
}

void FirstDirectoryBlock_print(FirstDirectoryBlock *f) {
    printf("FirstDirectoryBlock(\n");
    BlockHeader_print(&f->header, 2);
    printf(",\n");
    FileControlBlock_print(&f->fcb, 2);
    printf(",\n");
    printf("  num_entries=%d\n", f->num_entries);
    printf(")");
}

void DirectoryBlock_print(DirectoryBlock *f) {
    printf("DirectoryBlock(\n");
    BlockHeader_print(&f->header, 2);
    printf("\n)");
}

void DirectoryHandle_print(DirectoryHandle *h) {
    BlockHeader *bh = &h->dcb->header;
    int start_idx = h->dcb->fcb.block_in_disk;
    char block[BLOCK_SIZE];
    
    FirstDirectoryBlock_print((FirstDirectoryBlock *)bh);

    while(bh->next_block != start_idx) {
        DiskDriver_readBlock(h->sfs->disk, block, bh->next_block);
        bh = (BlockHeader *)block;

        printf(",\n");
        DirectoryBlock_print((DirectoryBlock *)block);
    }
    printf("\n");
}

void FileHandle_print(FileHandle *h) {
    BlockHeader *bh = &h->fcb->header;
    int start_idx = h->fcb->fcb.block_in_disk;
    char block[BLOCK_SIZE];
    
    FirstFileBlock_print((FirstFileBlock *)bh);

    while(bh->next_block != start_idx) {
        DiskDriver_readBlock(h->sfs->disk, block, bh->next_block);
        bh = (BlockHeader *)block;

        printf(",\n");
        FileBlock_print((FileBlock *)block);
    }
    printf("\n");
}
