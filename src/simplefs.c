#define _GNU_SOURCE
#include "simplefs.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define sizeof_field(structure, field) sizeof(((structure *)0)->field)
#define numelements_field(structure, arrayname) (sizeof_field(structure, arrayname)/sizeof_field(structure, arrayname[0]))

// Number of files in a FirstDirectoryBlock and in a DirectoryBlock
#define FILES_IN_FIRST_DB numelements_field(FirstDirectoryBlock, file_blocks)
#define FILES_IN_DB numelements_field(DirectoryBlock, file_blocks)
#define BYTES_IN_FIRST_FB sizeof_field(FirstFileBlock, data)
#define BYTES_IN_FB sizeof_field(FileBlock, data)

DirectoryHandle cwd; // current directory

typedef struct {
    DirectoryHandle *dir;
    DiskDriver *disk;
    FirstFileBlock ffb; // current file
    DirectoryBlock db;  // current directory block
    int pos;
    int relative_pos;
    int cur_dir_block;
    int next_dir_block;
} FileIterator;

FileIterator *FileIterator_new(DirectoryHandle *dir) {
    FileIterator *it = (FileIterator *) calloc(1, sizeof(FileIterator));
    it->dir = dir;
    it->disk = dir->sfs->disk;
    it->pos = -1;
    it->relative_pos = -1;
    it->cur_dir_block = dir->dcb->fcb.block_in_disk;
    it->next_dir_block = dir->dcb->header.next_block;
    return it;
}

void FileIterator_close(FileIterator *it) {
    free(it);
}

// Returns the index of the next file's control block
int FileIterator_nextidx(FileIterator *it) {
    int file_block;

    ++it->pos;
    if(it->pos == it->dir->dcb->num_entries) {
        return -1; // end of iteration
    }

    if(it->pos < FILES_IN_FIRST_DB) {
        // This is one of the files stored directly in the first block
        file_block = it->dir->dcb->file_blocks[it->pos];
    } else {
        // Otherwise, it's in one of the other blocks. Read the next
        // block if necessary
        if(it->relative_pos == -1 || it->relative_pos == FILES_IN_DB) {
            it->relative_pos = 0;

            DiskDriver_readBlock(it->disk, &it->db, it->next_dir_block);
            it->cur_dir_block = it->next_dir_block;
            it->next_dir_block = it->db.header.next_block;
        }
        file_block = it->db.file_blocks[it->relative_pos];
        it->relative_pos++;
    }
    
    return file_block;
}

FirstFileBlock *FileIterator_next(FileIterator *it) {
    int file_block = FileIterator_nextidx(it);
    if(file_block == -1) return NULL;
    DiskDriver_readBlock(it->disk, &it->ffb, file_block);
    return &it->ffb;
}

int FileIterator_update(FileIterator *it, int new_child_idx) {
    if(it->cur_dir_block == it->dir->dcb->fcb.block_in_disk) {
        it->dir->dcb->file_blocks[it->pos] = new_child_idx;
        DiskDriver_writeBlock(it->disk, it->dir->dcb, it->cur_dir_block);
    } else {
        it->db.file_blocks[it->relative_pos] = new_child_idx;
        DiskDriver_writeBlock(it->disk, &it->db, it->cur_dir_block);
    }
    return 0;
}

void free_cwd(void) {
    if(cwd.dcb) {
        free(cwd.dcb);
    }
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

    cwd.sfs = fs;
    cwd.dcb = dcb;
    cwd.directory = NULL;
    cwd.current_block = &dcb->header;
    cwd.pos_in_block = 0;
    cwd.pos_in_dir = 0;

    atexit(free_cwd);

    return &cwd;
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
    char block[BLOCK_SIZE];

    if(d->dcb->num_entries < FILES_IN_FIRST_DB) {
        d->dcb->file_blocks[d->dcb->num_entries] = child_pos;
    } else {

        int cur_block_num = d->dcb->header.next_block;
        int relative_pos = d->dcb->num_entries - FILES_IN_FIRST_DB;

        // Allocate the first DirectoryBlock if it isn't there already
        if(cur_block_num == d->dcb->fcb.block_in_disk) {
            cur_block_num = SimpleFS_newDirBlock(d);
        }

        DiskDriver_readBlock(disk, block, cur_block_num);
        DirectoryBlock *cur_db = (DirectoryBlock *)block;

        while(relative_pos >= FILES_IN_DB) {
            
            // Is this the end of the list? Allocate a new block if so
            if(cur_db->header.next_block == d->dcb->fcb.block_in_disk) {
                cur_block_num = SimpleFS_newDirBlock(d);
            } else {
                cur_block_num = cur_db->header.next_block;
            }

            DiskDriver_readBlock(disk, block, cur_block_num);
            
            relative_pos -= FILES_IN_DB;
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
    ffb->header.block_in_file = 0;
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
    fh->current_block_pos = ffb->fcb.block_in_disk;
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
            fh->current_block_pos = ffb_copy->fcb.block_in_disk;
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
        if(f->current_block != (BlockHeader *) f->fcb) free(f->current_block);
        free(f->fcb);
        free(f);
    }
    return 0;
}

// pos_in_file points to the next position to read/write in the file
// current_block is the last block written to. If pos_in_file is just
// after a block boundary, a block allocation may be needed if current_block
// doesn't have a successor

int SimpleFS_write(FileHandle *f, void *data, int size) {
    DiskDriver *disk = f->sfs->disk;
    int bytes_written = size;

    while(size > 0) {

        // Fill up the current block
        if(f->pos_in_file < BYTES_IN_FIRST_FB) {
            int bytes_to_write = min(BYTES_IN_FIRST_FB - f->pos_in_file, size);
            memcpy(f->fcb->data + f->pos_in_file, data, bytes_to_write);
            f->fcb->fcb.size_in_bytes = max(
                f->fcb->fcb.size_in_bytes,
                f->pos_in_file + bytes_to_write
            );

            size -= bytes_to_write;
            data += bytes_to_write;
            f->pos_in_file += bytes_to_write;
            
        } else {
            int pos_in_block = (f->pos_in_file - BYTES_IN_FIRST_FB) % BYTES_IN_FB;
            int bytes_to_write = min(size, BYTES_IN_FB - pos_in_block);

            // Allocate a new block if needed
            if(pos_in_block == 0 && f->current_block->next_block == f->fcb->fcb.block_in_disk) {
                int cur_block_pos = f->fcb->header.previous_block;
                
                FileBlock *fb = (FileBlock *) calloc(1, sizeof(FileBlock));
                fb->header.block_in_file = f->current_block->block_in_file + 1;
                fb->header.next_block = f->current_block->next_block;
                fb->header.previous_block = f->fcb->header.previous_block;
                
                int fb_pos = DiskDriver_getFreeBlock(disk, 0);
                if(fb_pos == -1) {
                    return -1; // no space left
                }
                f->current_block->next_block = fb_pos;
                f->fcb->header.previous_block = fb_pos;

                f->fcb->fcb.size_in_blocks++;

                DiskDriver_writeBlock(disk, f->current_block, cur_block_pos);
                DiskDriver_writeBlock(disk, fb, fb_pos);
                
                if(f->current_block != (BlockHeader *) f->fcb) {
                    free(f->current_block);
                }
                f->current_block = (BlockHeader *) fb;
                f->current_block_pos = fb_pos;

            } else if(pos_in_block == 0) {
                // Move to the next block
                int next_block = f->current_block->next_block;
                FileBlock *fb = (FileBlock *) calloc(1, sizeof(FileBlock));
                DiskDriver_readBlock(disk, fb, next_block);
                if(f->current_block != (BlockHeader *) f->fcb) {
                    free(f->current_block);
                }
                f->current_block_pos = next_block;
                f->current_block = (BlockHeader *) fb;
            }

            memcpy(((FileBlock *)f->current_block)->data + pos_in_block, data, bytes_to_write);
            DiskDriver_writeBlock(disk, f->current_block, f->current_block_pos);
            
            f->fcb->fcb.size_in_bytes = max(
                f->fcb->fcb.size_in_bytes,
                f->pos_in_file + bytes_to_write
            );

            size -= bytes_to_write;
            data += bytes_to_write;
            f->pos_in_file += bytes_to_write;
        }
    }

    DiskDriver_writeBlock(f->sfs->disk, f->fcb, f->fcb->fcb.block_in_disk);
    return bytes_written;
}

int SimpleFS_read(FileHandle *f, void *data, int size) {
    DiskDriver *disk = f->sfs->disk;

    // If we don't have that many bytes, truncate the request
    if(f->pos_in_file + size > f->fcb->fcb.size_in_bytes) {
        size = f->fcb->fcb.size_in_bytes - f->pos_in_file;
    }
    int bytes_read = size;

    while(size > 0) {

        if(f->pos_in_file < BYTES_IN_FIRST_FB) {
            int bytes_to_read = min(BYTES_IN_FIRST_FB - f->pos_in_file, size);
            memcpy(data, f->fcb->data + f->pos_in_file, bytes_to_read);

            size -= bytes_to_read;
            data += bytes_to_read;
            f->pos_in_file += bytes_to_read;
            
        } else {
            int pos_in_block = (f->pos_in_file - BYTES_IN_FIRST_FB) % BYTES_IN_FB;
            int bytes_to_read = min(size, BYTES_IN_FB - pos_in_block);

            // Load the next block
            if(pos_in_block == 0) {

                // We should never reach the end of the file with more data to read, as we truncated the request before
                ONERROR(f->current_block->next_block == f->fcb->fcb.block_in_disk,
                    "read: end of file reached while reading data");
                
                int next_block = f->current_block->next_block;
                FileBlock *fb = (FileBlock *) calloc(1, sizeof(FileBlock));
                DiskDriver_readBlock(disk, fb, next_block);
                if(f->current_block != (BlockHeader *) f->fcb) {
                    free(f->current_block);
                }
                f->current_block_pos = next_block;
                f->current_block = (BlockHeader *) fb;
            }

            memcpy(data, ((FileBlock *)f->current_block)->data + pos_in_block, bytes_to_read);
            
            size -= bytes_to_read;
            data += bytes_to_read;
            f->pos_in_file += bytes_to_read;
        }
    }

    return bytes_read;
}

int SimpleFS_seek(FileHandle *f, int pos) {
    DiskDriver *disk = f->sfs->disk;

    // If we don't have that many bytes, truncate the request
    if(pos > f->fcb->fcb.size_in_bytes || pos < 0) {
        return -1;
    }
    int moved_by = pos - f->pos_in_file;

    // If we need to rewind, go back
    if(pos < f->pos_in_file) {
        DBGPRINT("seeking to 0, then %d", pos);
        if(f->current_block == (BlockHeader *) f->fcb) {
            f->pos_in_file = 0;
        } else {
            free(f->current_block);
            f->current_block = &f->fcb->header;
            f->current_block_pos = f->fcb->fcb.block_in_disk;
            f->pos_in_file = 0;
        }
    } else {
        DBGPRINT("Seeking normally to %d, offset %d", pos, pos - f->pos_in_file);
        pos -= f->pos_in_file;
    }


    while(pos > 0) {

        if(f->pos_in_file < BYTES_IN_FIRST_FB) {
            int bytes_to_read = min(BYTES_IN_FIRST_FB - f->pos_in_file, pos);

            pos -= bytes_to_read;
            f->pos_in_file += bytes_to_read;
            
        } else {
            int pos_in_block = (f->pos_in_file - BYTES_IN_FIRST_FB) % BYTES_IN_FB;
            int bytes_to_read = min(pos, BYTES_IN_FB - pos_in_block);

            // Load the next block
            if(pos_in_block == 0) {

                // We should never reach the end of the file with more data to read, as we truncated the request before
                ONERROR(f->current_block->next_block == f->fcb->fcb.block_in_disk,
                    "seek: end of file reached while moving");
                
                FileBlock *fb = (FileBlock *) calloc(1, sizeof(FileBlock));
                DiskDriver_readBlock(disk, fb, f->current_block->next_block);
                if(f->current_block != (BlockHeader *) f->fcb) {
                    free(f->current_block);
                }
                f->current_block_pos = f->current_block->next_block;
                f->current_block = (BlockHeader *) fb;
            }

            pos -= bytes_to_read;
            f->pos_in_file += bytes_to_read;
        }
    }

    return moved_by;
}

int SimpleFS_changeDir(DirectoryHandle *d, char *dirname) {

    if(!strcmp(".", dirname)) {
        return 0;
    }

    if(!strcmp("..", dirname)) {
        if(d->directory != NULL) {
            
            free(d->dcb);
            d->dcb = d->directory;
            d->current_block = &d->directory->header;
            d->pos_in_dir = 0;
            d->pos_in_block = 0;

            FirstDirectoryBlock *fdb = (FirstDirectoryBlock *) calloc(1, sizeof(FirstDirectoryBlock));
            DiskDriver_readBlock(d->sfs->disk, fdb, d->dcb->fcb.directory_block);
            d->directory = fdb;
            return 0;

        } else return -1;
    }

    if(!strcmp("/", dirname)) {
        free(d->dcb);
        if(d->directory) free(d->directory);
        d->pos_in_dir = 0;
        d->pos_in_block = 0;

        FirstDirectoryBlock *fdb = (FirstDirectoryBlock *) calloc(1, sizeof(FirstDirectoryBlock));
        DiskDriver_readBlock(d->sfs->disk, fdb, 0);
        d->dcb = fdb;
        d->current_block = &fdb->header;
        return 0;
    }

    FileIterator *it = FileIterator_new(d);
    FirstFileBlock *ffb;
    while((ffb = FileIterator_next(it))) {
        if(ffb->fcb.is_dir && !strcmp(ffb->fcb.name, dirname)) {
            
            // Copy so that we can free the iterator
            // Here we use the fact that the layout for directory/file first
            // blocks is identical up to the fcb, so we can safely read
            // is_dir/name and cast to a directory block
            FirstDirectoryBlock *fdb_copy = (FirstDirectoryBlock *) calloc(1, sizeof(FirstDirectoryBlock));
            memcpy(fdb_copy, ffb, sizeof(FirstDirectoryBlock));
            
            free(d->directory);
            d->directory = d->dcb;
            d->dcb = fdb_copy;
            d->current_block = &fdb_copy->header;
            d->pos_in_dir = 0;
            d->pos_in_block = 0;

            FileIterator_close(it);
            return 0;
        }
    }
    FileIterator_close(it);

    return -1; // not found
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

    FirstDirectoryBlock ffb = {0};
    ffb.header.block_in_file = 0;
    ffb.header.next_block = pos;
    ffb.header.previous_block = pos;
    ffb.fcb.directory_block = d->dcb->fcb.block_in_disk;
    ffb.fcb.block_in_disk = pos;
    strcpy(ffb.fcb.name, dirname);
    ffb.fcb.size_in_bytes = 0;
    ffb.fcb.size_in_blocks = 1;
    ffb.fcb.is_dir = 1;

    DiskDriver_writeBlock(disk, &ffb, pos);
    SimpleFS_addToDirectory(d, pos);
    return 0;
}

// Free the linked list of blocks starting with the given header
static int SimpleFS_removeblocks(DiskDriver *disk, BlockHeader *b, int first_block) {
    int cur_block = first_block;
    char block[BLOCK_SIZE];

    DiskDriver_freeBlock(disk, cur_block);
    cur_block = b->next_block;

    while(cur_block != first_block) {
        DiskDriver_readBlock(disk, block, cur_block);
        DiskDriver_freeBlock(disk, cur_block);
        b = (BlockHeader *)block;
        cur_block = b->next_block;
    }

    return 0;
}

// Remove all the contents of the given folder. The folder is not removed, and is not updated to reflect the missing files
int SimpleFS_removecontents(DiskDriver *disk, FirstDirectoryBlock *fdb) {
    BlockHeader *h = &fdb->header;
    int first_block = fdb->fcb.block_in_disk;
    FirstFileBlock ffb;
    DirectoryBlock db;
    int entries = fdb->num_entries;

    for(int i = 0; i < entries && i < FILES_IN_FIRST_DB; i++) {
        DiskDriver_readBlock(disk, &ffb, fdb->file_blocks[i]);
        if(ffb.fcb.is_dir) {
            SimpleFS_removecontents(disk, (FirstDirectoryBlock *)&ffb);
        }
        SimpleFS_removeblocks(disk, &ffb.header, fdb->file_blocks[i]);
    }
    entries -= FILES_IN_FIRST_DB;

    for(; h->next_block != first_block; entries -= FILES_IN_DB) {
        DiskDriver_readBlock(disk, &db, h->next_block);
        h = &db.header;
        for(int j = 0; j < FILES_IN_DB && j < entries; j++) {
            DiskDriver_readBlock(disk, &ffb, db.file_blocks[j]);
            if(ffb.fcb.is_dir) {
                SimpleFS_removecontents(disk, (FirstDirectoryBlock *)&ffb);
            }
            SimpleFS_removeblocks(disk, &ffb.header, db.file_blocks[j]);
        }
    }

    return 0;
}

int SimpleFS_remove(DirectoryHandle *d, char *filename) {
    FileIterator *it = FileIterator_new(d);
    FirstFileBlock *ffb;
    while((ffb = FileIterator_next(it))) {
        if(!strcmp(ffb->fcb.name, filename)) {

            if(ffb->fcb.is_dir) {
                SimpleFS_removecontents(d->sfs->disk, (FirstDirectoryBlock *) ffb);
            }

            SimpleFS_removeblocks(d->sfs->disk, &ffb->header, ffb->fcb.block_in_disk);

            // Replace this file in the directory with the last one
            int last_idx = -1, idx = -1;
            FileIterator *it2 = FileIterator_new(d);
            while((idx = FileIterator_nextidx(it2)) != -1) last_idx = idx;
            FileIterator_close(it2);

            if(last_idx != ffb->fcb.block_in_disk) {
                FileIterator_update(it, last_idx);
            }
            FileIterator_close(it);

            // Shorten the directory by one element. If the element is the only entry in the last DirectoryBlock, free the block
            if(d->dcb->num_entries > FILES_IN_FIRST_DB) {
                int relative_pos = (d->dcb->num_entries - 1 - FILES_IN_FIRST_DB) % FILES_IN_DB; // position in the last block
                DBGPRINT("removing, relative %d", relative_pos);
                
                if(relative_pos == 0) {
                    DirectoryBlock last = {0}, second_to_last = {0};
                    int last_idx = d->dcb->header.previous_block;

                    DiskDriver_readBlock(d->sfs->disk, &last, last_idx);

                    if(last.header.previous_block == d->dcb->fcb.block_in_disk) {
                        d->dcb->header.next_block = d->dcb->fcb.block_in_disk;
                    } else {
                        DiskDriver_readBlock(d->sfs->disk, &second_to_last, last.header.previous_block);
                        second_to_last.header.next_block = d->dcb->fcb.block_in_disk;
                        DiskDriver_writeBlock(d->sfs->disk, &second_to_last, last.header.previous_block);
                    }
                    d->dcb->header.previous_block = last.header.previous_block;
                    DiskDriver_freeBlock(d->sfs->disk, last_idx);
                    d->dcb->fcb.size_in_blocks--;
                }
            }
            d->dcb->num_entries--;
            DiskDriver_writeBlock(d->sfs->disk, d->dcb, d->dcb->fcb.block_in_disk);

            return 0;
        }
    }
    FileIterator_close(it);
    return -1;
}




void BlockHeader_print(BlockHeader *b, int spaces) {
    for(int i = 0; i < spaces; i++) putchar(' ');
    printf("BlockHeader(prev=%d, next=%d, block_in_file=%d)",
        b->previous_block, b->next_block, b->block_in_file);
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
