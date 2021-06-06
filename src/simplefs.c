#define _GNU_SOURCE
#include "simplefs.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    DirectoryHandle *dir;
    DiskDriver *disk;
    FirstFileBlock ffb; // current file
    DirectoryBlock db;  // current directory block
    int pos;
    int relative_pos;
    int next_dir_block;
} FileIterator;

FileIterator *FileIterator_new(DirectoryHandle *dir) {
    FileIterator *it = (FileIterator *) calloc(1, sizeof(FileIterator));
    it->dir = dir;
    it->disk = dir->sfs->disk;
    it->pos = -1;
    it->relative_pos = -1;
    it->next_dir_block = dir->dcb->header.next_block;
    return it;
}

void FileIterator_close(FileIterator *it) {
    free(it);
}

FirstFileBlock *FileIterator_next(FileIterator *it) {
    int file_block;

    ++it->pos;
    if(it->pos == it->dir->dcb->num_entries) {
        return NULL; // end of iteration
    }

    if(it->pos < sizeof(it->dir->dcb->file_blocks)/sizeof(it->dir->dcb->file_blocks[0])) {
        // This is one of the files stored directly in the first block
        file_block = it->dir->dcb->file_blocks[it->pos];
    } else {
        // Otherwise, it's in one of the other blocks. Read the next
        // block if necessary
        if(it->relative_pos == -1 || it->relative_pos == sizeof(it->db.file_blocks)/sizeof(it->db.file_blocks[0])) {
            it->relative_pos = 0;

            DiskDriver_readBlock(it->disk, &it->db, it->next_dir_block);
            it->next_dir_block = it->db.header.next_block;
        }
        file_block = it->db.file_blocks[it->relative_pos];
        it->relative_pos++;
    }
    
    DiskDriver_readBlock(it->disk, &it->ffb, file_block);
    return &it->ffb;
}



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

// Add the given block as a children of the directory d
int SimpleFS_addToDirectory(DirectoryHandle *d, int child_pos) {
    DiskDriver *disk = d->sfs->disk;
    int first_block_entries = sizeof(d->dcb->file_blocks)/sizeof(d->dcb->file_blocks[0]);
    DirectoryBlock db;
    char block[BLOCK_SIZE];

    if(d->dcb->num_entries < first_block_entries) {
        d->dcb->file_blocks[d->dcb->num_entries] = child_pos;
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

        cur_db->file_blocks[relative_pos] = child_pos;
        DiskDriver_writeBlock(disk, cur_db, cur_block_num);
    }
    
    d->dcb->num_entries++;
    DiskDriver_writeBlock(disk, d->dcb, d->dcb->fcb.block_in_disk);

    return 0;
}

FileHandle *SimpleFS_createFile(DirectoryHandle *d, const char *filename) {
    {
        FileIterator *it = FileIterator_new(d);
        FirstFileBlock *ffb;
        while((ffb = FileIterator_next(it))) {
            if(!strcmp(ffb->fcb.name, filename)) {
                DBGPRINT("found duplicate filename");
                FileIterator_close(it);
                return NULL; // File exists
            }
        }
        FileIterator_close(it);
    }

    // There's no duplicate. Let's create the file
    
    DiskDriver *disk = d->sfs->disk;

    int pos;
    if((pos = DiskDriver_getFreeBlock(disk, 0)) == -1) {
        return NULL; // No space left on disk
    }

    if(strlen(filename) >= MAX_FILENAME_LEN) return NULL;

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
    SimpleFS_addToDirectory(d, pos);

    FileHandle *fh = (FileHandle *) calloc(1, sizeof(FileHandle));
    fh->sfs = d->sfs;
    fh->fcb = ffb;
    fh->directory = d->dcb;
    fh->current_block = &ffb->header;
    fh->pos_in_file = 0;
    return fh;
}

int SimpleFS_readDir(char **names, DirectoryHandle *d) {
    int names_len = 0;
    
    FileIterator *it = FileIterator_new(d);
    FirstFileBlock *ffb;
    while((ffb = FileIterator_next(it))) {
        names[names_len++] = strdup(ffb->fcb.name);
    }
    FileIterator_close(it);

    return names_len;
}

FileHandle *SimpleFS_openFile(DirectoryHandle *d, const char *filename) {
    FileIterator *it = FileIterator_new(d);
    FirstFileBlock *ffb;
    while((ffb = FileIterator_next(it))) {
        if(!strcmp(ffb->fcb.name, filename)) {
            
            // Copy so that we can free the iterator
            FirstFileBlock *ffb_copy = (FirstFileBlock *) calloc(1, sizeof(FirstFileBlock));
            memcpy(ffb_copy, ffb, sizeof(FirstFileBlock));
            
            FileHandle *fh = (FileHandle *) calloc(1, sizeof(FileHandle));
            fh->sfs = d->sfs;
            fh->fcb = ffb_copy;
            fh->directory = d->dcb;
            fh->current_block = &ffb_copy->header;
            fh->pos_in_file = 0;

            FileIterator_close(it);
            return fh;
        }
    }
    FileIterator_close(it);

    // Not found
    return NULL;
}

int SimpleFS_close(FileHandle* f) {
    if(f) {
        free(f->fcb);
        free(f);
    }
    return 0;
}

int SimpleFS_closeDir(DirectoryHandle *d) {
    if(d) {
        free(d->dcb);
        free(d);
    }
    return 0;
}

int SimpleFS_mkDir(DirectoryHandle *d, char *dirname) {
    {
        FileIterator *it = FileIterator_new(d);
        FirstFileBlock *ffb;
        while((ffb = FileIterator_next(it))) {
            if(!strcmp(ffb->fcb.name, dirname)) {
                DBGPRINT("found duplicate filename");
                FileIterator_close(it);
                return -1; // File exists
            }
        }
        FileIterator_close(it);
    }

    // There's no duplicate. Let's create the directory
    
    DiskDriver *disk = d->sfs->disk;

    int pos;
    if((pos = DiskDriver_getFreeBlock(disk, 0)) == -1) {
        return -1; // No space left on disk
    }

    if(strlen(dirname) >= MAX_FILENAME_LEN) return -1;

    FirstDirectoryBlock *ffb = (FirstDirectoryBlock *) calloc(1, sizeof(FirstDirectoryBlock));
    ffb->header.block_in_file = pos;
    ffb->header.next_block = pos;
    ffb->header.previous_block = pos;
    ffb->fcb.directory_block = d->dcb->fcb.block_in_disk;
    ffb->fcb.block_in_disk = pos;
    strcpy(ffb->fcb.name, dirname);
    ffb->fcb.size_in_bytes = 0;
    ffb->fcb.size_in_blocks = 1;
    ffb->fcb.is_dir = 1;

    DiskDriver_writeBlock(disk, ffb, pos);
    SimpleFS_addToDirectory(d, pos);
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