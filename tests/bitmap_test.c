#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main(int agc, char** argv) {
    BitMap bmap;
    bmap.num_bits = 256*8;
    bmap.entries = (char *) calloc(256, 1);

    for(int i = 0; i < 256*8; i++) {
        BitMap_set(&bmap, i, 1);
        assert(BitMap_get(&bmap, i, 1) == i);
    }

    for(int i = 0; i < 128*8; i++) {
        BitMap_set(&bmap, i, 0);
    }
    assert(BitMap_get(&bmap, 0, 1) == 128*8);

    printf("Bitmap tests passed\n");
}
