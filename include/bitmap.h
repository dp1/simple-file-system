#pragma once
#include <stdint.h>
typedef struct{
  int num_bits;
  char* entries;
} BitMap;

typedef struct {
  int entry_num;
  char bit_num;
} BitMapEntryKey;

// converts a block index to an index in the array,
// and a char that indicates the offset of the bit inside the array
BitMapEntryKey BitMap_blockToIndex(int num);

// converts a bit to a linear index
int BitMap_indexToBlock(int entry, uint8_t bit_num);

// returns the index of the first bit having status "status"
// in the bitmap bmap, and starts looking from position start
// returns -1 if no block is found
int BitMap_find(BitMap* bmap, int start, int status);

// sets the bit at index pos in bmap to status
// returns -1 if the block isn't in the bitmap
int BitMap_set(BitMap* bmap, int pos, int status);

// returns the status of the block at index pos
// returns -1 if the block isn't in the bitmap
int BitMap_get(BitMap *bmap, int pos);

// Print the contents of the bitmap to stdout
void BitMap_print(BitMap *bmap);
