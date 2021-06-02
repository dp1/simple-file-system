#include "bitmap.h"
#include <assert.h>

BitMapEntryKey BitMap_blockToIndex(int num) {
    BitMapEntryKey key;
    key.entry_num = num >> 3;
    key.bit_num = num & 7;
    return key;
}

int BitMap_indexToBlock(int entry, uint8_t bit_num) {
    return (entry << 3) | bit_num;
}

int BitMap_get(BitMap* bmap, int start, int status) {
    int pos = start;
    while(pos < bmap->num_bits) {
        
        BitMapEntryKey key = BitMap_blockToIndex(pos);
        if(((bmap->entries[key.entry_num] >> key.bit_num) & 1) == status) {
            return pos;
        }

        pos++;
    }

    return -1;
}

int BitMap_set(BitMap* bmap, int pos, int status) {
    BitMapEntryKey key = BitMap_blockToIndex(pos);

    if(key.entry_num < bmap->num_bits) {

        if(status == 1) {
            bmap->entries[key.entry_num] |= (1 << key.bit_num);
        } else {
            bmap->entries[key.entry_num] &= ~(1 << key.bit_num);
        }
    }

    return 0;
}
