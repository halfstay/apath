#ifndef AP_HISTORY_H
#define AP_HISTORY_H

#include "ap_map.h"

typedef struct { uint32_t index; uint8_t before, after; } ApChange;
typedef struct {
    ApChange *changes;
    size_t count, capacity;
    uint64_t before_id, after_id;
} ApStroke;
typedef struct {
    ApStroke *items, active;
    size_t count, cursor, capacity, bytes, budget;
    uint64_t current_id, saved_id, next_id;
    int recording;
} ApHistory;

void ap_history_init(ApHistory *history, size_t byte_budget);
void ap_history_free(ApHistory *history);
void ap_history_begin(ApHistory *history);
ApResult ap_history_set(ApHistory *history, ApMap *map, int x, int y, int value);
ApResult ap_history_commit(ApHistory *history, ApMap *map);
void ap_history_cancel(ApHistory *history, ApMap *map);
int ap_history_undo(ApHistory *history, ApMap *map);
int ap_history_redo(ApHistory *history, ApMap *map);
void ap_history_saved(ApHistory *history);
int ap_history_dirty(const ApHistory *history);

#endif
