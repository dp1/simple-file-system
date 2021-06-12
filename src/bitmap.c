#include "bitmap.h"
#include <assert.h>
#include <stdio.h>

BitMapEntryKey BitMap_blockToIndex(int num) {
    BitMapEntryKey key;
    key.entry_num = num >> 3;
    key.bit_num = num & 7;
    return key;
}

int BitMap_indexToBlock(int entry, uint8_t bit_num) {
    return (entry << 3) | bit_num;
}

int BitMap_find(BitMap* bmap, int start, int status) {
    if(start < 0 || start >= bmap->num_bits) return -1;

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
    if(pos < 0 || pos >= bmap->num_bits) return -1;
    BitMapEntryKey key = BitMap_blockToIndex(pos);

    if(key.entry_num < bmap->num_bits) {

        if(status == 1) {
            bmap->entries[key.entry_num] |= (1 << key.bit_num);
        } else {
            bmap->entries[key.entry_num] &= ~(1 << key.bit_num);
        }
        return 0;
    }

    return -1;
}

int BitMap_get(BitMap* bmap, int pos) {
    if(pos < 0 || pos >= bmap->num_bits) return -1;
    BitMapEntryKey key = BitMap_blockToIndex(pos);

    if(key.entry_num >= bmap->num_bits) {
        return -1;
    }

    return (bmap->entries[key.entry_num] >> key.bit_num) & 1;
}

void BitMap_print(BitMap *bmap) {
    printf("BitMap with %d bits:\n", bmap->num_bits);
    for(int i = 0; i < bmap->num_bits; i++) {
        int status = BitMap_get(bmap, i);
        
        if(status == -1) putchar('?');
        else if(status == 0) putchar('0');
        else putchar('1');

        if((i+1)%64 == 0 || i == bmap->num_bits-1) putchar('\n');
    }
}
