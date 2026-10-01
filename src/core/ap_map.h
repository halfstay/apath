#ifndef AP_MAP_H
#define AP_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define AP_MAX_CELLS UINT32_C(16777216)

typedef enum {
    AP_OK = 0, AP_INVALID, AP_MEMORY, AP_IO, AP_NO_PATH
} ApResult;

typedef struct {
    uint16_t cell_size, width, height;
    uint32_t pixel_width, pixel_height;
    uint8_t *bits;
} ApMap;

/* Owning outputs must initially be zeroed. Successful create/read replaces them. */
ApResult ap_map_create(ApMap *map, unsigned cell_size, unsigned width, unsigned height);
/* Pixel extents need not be multiples of cell_size; edge cells are clipped. */
ApResult ap_map_create_pixels(ApMap *map, unsigned cell_size, unsigned width, unsigned height);
void ap_map_free(ApMap *map);
size_t ap_map_cells(const ApMap *map);
size_t ap_map_bytes(const ApMap *map);
int ap_map_get(const ApMap *map, int x, int y);
void ap_map_set(ApMap *map, unsigned x, unsigned y, int walkable);
int ap_map_bit(const ApMap *map, uint32_t index);
void ap_map_put(ApMap *map, uint32_t index, int value);
ApResult ap_map_read(FILE *file, ApMap *map);
ApResult ap_map_write(FILE *file, const ApMap *map);
const char *ap_result_string(ApResult result);

#endif
