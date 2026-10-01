#include "ap_map.h"

#include <stdlib.h>
#include <string.h>

size_t ap_map_cells(const ApMap *m) { return (size_t)m->width * m->height; }
size_t ap_map_bytes(const ApMap *m) { return (ap_map_cells(m) + 7u) / 8u; }

ApResult ap_map_create(ApMap *m, unsigned cell, unsigned w, unsigned h) {
  ApMap fresh = {0};
  if (!m || !cell || cell > UINT16_MAX || !w || w > UINT16_MAX || !h ||
      h > UINT16_MAX || (uint64_t)w * h > AP_MAX_CELLS)
    return AP_INVALID;
  fresh.cell_size = (uint16_t)cell;
  fresh.width = (uint16_t)w;
  fresh.height = (uint16_t)h;
  fresh.pixel_width = (uint32_t)w * cell;
  fresh.pixel_height = (uint32_t)h * cell;
  fresh.bits = (uint8_t *)calloc(ap_map_bytes(&fresh), 1);
  if (!fresh.bits)
    return AP_MEMORY;
  ap_map_free(m);
  *m = fresh;
  return AP_OK;
}

ApResult ap_map_create_pixels(ApMap *m, unsigned cell, unsigned w, unsigned h) {
  ApResult result;
  if (!cell || cell > UINT16_MAX || !w || w > UINT16_MAX || !h ||
      h > UINT16_MAX)
    return AP_INVALID;
  result = ap_map_create(m, cell, (w + cell - 1) / cell, (h + cell - 1) / cell);
  if (result == AP_OK) {
    m->pixel_width = w;
    m->pixel_height = h;
  }
  return result;
}

void ap_map_free(ApMap *m) {
  if (m) {
    free(m->bits);
    memset(m, 0, sizeof(*m));
  }
}

int ap_map_bit(const ApMap *m, uint32_t i) {
  return m && m->bits && i < ap_map_cells(m)
             ? (m->bits[i >> 3] >> (i & 7u)) & 1u
             : 0;
}

void ap_map_put(ApMap *m, uint32_t i, int value) {
  uint8_t mask;
  if (!m || !m->bits || i >= ap_map_cells(m))
    return;
  mask = (uint8_t)(1u << (i & 7u));
  if (value)
    m->bits[i >> 3] |= mask;
  else
    m->bits[i >> 3] &= (uint8_t)~mask;
}

int ap_map_get(const ApMap *m, int x, int y) {
  if (!m || x < 0 || y < 0 || x >= m->width || y >= m->height)
    return 0;
  return ap_map_bit(m, (uint32_t)y * m->width + (uint32_t)x);
}

void ap_map_set(ApMap *m, unsigned x, unsigned y, int value) {
  if (m && x < m->width && y < m->height)
    ap_map_put(m, (uint32_t)y * m->width + x, value);
}

static unsigned get16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static void put16(uint8_t *p, unsigned n) {
  p[0] = (uint8_t)n;
  p[1] = (uint8_t)(n >> 8);
}

ApResult ap_map_read(FILE *f, ApMap *m) {
  uint8_t header[9];
  ApMap fresh = {0};
  ApResult result;
  size_t n, tail;
  if (!f || !m)
    return AP_INVALID;
  if (fread(header, 1, sizeof(header), f) != sizeof(header))
    return ferror(f) ? AP_IO : AP_INVALID;
  if (memcmp(header, "AMD", 3))
    return AP_INVALID;
  result = ap_map_create_pixels(&fresh, get16(header + 3), get16(header + 5),
                                get16(header + 7));
  if (result != AP_OK)
    return result;
  n = ap_map_bytes(&fresh);
  tail = ap_map_cells(&fresh) & 7u;
  if (fread(fresh.bits, 1, n, f) != n || fgetc(f) != EOF || ferror(f) ||
      (tail && (fresh.bits[n - 1] >> tail))) {
    result = ferror(f) ? AP_IO : AP_INVALID;
    ap_map_free(&fresh);
    return result;
  }
  ap_map_free(m);
  *m = fresh;
  return AP_OK;
}

ApResult ap_map_write(FILE *f, const ApMap *m) {
  uint8_t header[9] = {'A', 'M', 'D', 0, 0, 0, 0, 0, 0};
  size_t n, tail;
  uint8_t last;
  if (!f || !m || !m->bits || !m->cell_size || !m->width || !m->height ||
      ap_map_cells(m) > AP_MAX_CELLS)
    return AP_INVALID;
  if (!m->pixel_width || !m->pixel_height ||
      ((uint64_t)m->pixel_width + m->cell_size - 1) / m->cell_size !=
          m->width ||
      ((uint64_t)m->pixel_height + m->cell_size - 1) / m->cell_size !=
          m->height)
    return AP_INVALID;
  if (m->pixel_width > UINT16_MAX || m->pixel_height > UINT16_MAX)
    return AP_INVALID;
  put16(header + 3, m->cell_size);
  put16(header + 5, m->pixel_width);
  put16(header + 7, m->pixel_height);
  n = ap_map_bytes(m);
  tail = ap_map_cells(m) & 7u;
  last = m->bits[n - 1];
  if (tail)
    last &= (uint8_t)((1u << tail) - 1u);
  if (fwrite(header, 1, 9, f) != 9 ||
      (n > 1 && fwrite(m->bits, 1, n - 1, f) != n - 1) || fputc(last, f) == EOF)
    return AP_IO;
  return AP_OK;
}

const char *ap_result_string(ApResult r) {
  switch (r) {
  case AP_OK:
    return "Success";
  case AP_INVALID:
    return "Invalid map, coordinates, or file format";
  case AP_MEMORY:
    return "Not enough memory";
  case AP_IO:
    return "Unable to read or write the file";
  case AP_NO_PATH:
    return "No route between these points";
  default:
    return "Unknown error";
  }
}
