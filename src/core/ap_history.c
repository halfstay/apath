#include "ap_history.h"

#include <stdlib.h>
#include <string.h>

void ap_history_init(ApHistory *h, size_t budget) {
  memset(h, 0, sizeof(*h));
  h->budget = budget;
  h->next_id = 1;
}

void ap_history_free(ApHistory *h) {
  size_t i;
  for (i = 0; i < h->count; ++i)
    free(h->items[i].changes);
  free(h->items);
  free(h->active.changes);
  memset(h, 0, sizeof(*h));
}

void ap_history_begin(ApHistory *h) { h->recording = 1; }

ApResult ap_history_set(ApHistory *h, ApMap *m, int x, int y, int value) {
  uint32_t index;
  int before;
  ApStroke *s = &h->active;
  ApChange *p;
  size_t cap;
  if (!h->recording)
    return AP_INVALID;
  if (x < 0 || y < 0 || x >= m->width || y >= m->height)
    return AP_OK;
  index = (uint32_t)y * m->width + (uint32_t)x;
  before = ap_map_bit(m, index);
  value = !!value;
  if (before == value)
    return AP_OK;
  if (s->count == s->capacity) {
    cap = s->capacity ? s->capacity * 2 : 128;
    if (cap < s->capacity || cap > SIZE_MAX / sizeof(*p))
      return AP_MEMORY;
    p = (ApChange *)realloc(s->changes, cap * sizeof(*p));
    if (!p)
      return AP_MEMORY;
    s->changes = p;
    s->capacity = cap;
  }
  s->changes[s->count++] = (ApChange){index, (uint8_t)before, (uint8_t)value};
  ap_map_put(m, index, value);
  return AP_OK;
}

void ap_history_cancel(ApHistory *h, ApMap *m) {
  size_t i = h->active.count;
  while (i--)
    ap_map_put(m, h->active.changes[i].index, h->active.changes[i].before);
  free(h->active.changes);
  memset(&h->active, 0, sizeof(h->active));
  h->recording = 0;
}

ApResult ap_history_commit(ApHistory *h, ApMap *m) {
  size_t i;
  ApStroke *items;
  if (!h->active.count) {
    ap_history_cancel(h, m);
    return AP_OK;
  }
  if (h->cursor == h->capacity) {
    size_t cap = h->capacity ? h->capacity * 2 : 32;
    if (cap < h->capacity || cap > SIZE_MAX / sizeof(*items)) {
      ap_history_cancel(h, m);
      return AP_MEMORY;
    }
    items = (ApStroke *)realloc(h->items, cap * sizeof(*items));
    if (!items) {
      ap_history_cancel(h, m);
      return AP_MEMORY;
    }
    h->items = items;
    h->capacity = cap;
  }
  for (i = h->cursor; i < h->count; ++i) {
    h->bytes -= h->items[i].capacity * sizeof(ApChange);
    free(h->items[i].changes);
  }
  h->active.before_id = h->current_id;
  h->active.after_id = h->next_id++;
  h->current_id = h->active.after_id;
  h->items[h->cursor++] = h->active;
  h->count = h->cursor;
  h->bytes += h->active.capacity * sizeof(ApChange);
  memset(&h->active, 0, sizeof(h->active));
  h->recording = 0;
  while (h->count > 1 && h->bytes > h->budget) {
    h->bytes -= h->items[0].capacity * sizeof(ApChange);
    free(h->items[0].changes);
    --h->count;
    --h->cursor;
    memmove(h->items, h->items + 1, h->count * sizeof(*h->items));
  }
  return AP_OK;
}

int ap_history_undo(ApHistory *h, ApMap *m) {
  ApStroke *s;
  size_t i;
  if (h->recording || !h->cursor)
    return 0;
  s = &h->items[--h->cursor];
  i = s->count;
  while (i--)
    ap_map_put(m, s->changes[i].index, s->changes[i].before);
  h->current_id = s->before_id;
  return 1;
}

int ap_history_redo(ApHistory *h, ApMap *m) {
  ApStroke *s;
  size_t i;
  if (h->recording || h->cursor == h->count)
    return 0;
  s = &h->items[h->cursor++];
  for (i = 0; i < s->count; ++i)
    ap_map_put(m, s->changes[i].index, s->changes[i].after);
  h->current_id = s->after_id;
  return 1;
}

void ap_history_saved(ApHistory *h) { h->saved_id = h->current_id; }
int ap_history_dirty(const ApHistory *h) {
  return h->current_id != h->saved_id || h->active.count != 0;
}
