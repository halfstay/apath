#ifndef AP_NAV_H
#define AP_NAV_H

#include "ap_map.h"

typedef struct { double x, y; } ApPoint;
/* Doubled cell coordinates; fractional at clipped pixel boundaries/centers. */
typedef struct { double x, y; } ApVertex;
typedef struct {
    ApVertex v[3];
    int32_t neighbor[3]; /* Across v[i] -> v[(i+1)%3], -1 at boundary. */
    uint32_t component;
} ApTriangle;
typedef struct {
    uint32_t x0, y0, x1, y1;
    uint32_t first, count;
} ApRect;
typedef struct { uint32_t x0, x1, rect; } ApSpan;
typedef struct {
    uint16_t width, height;
    double extent_x, extent_y;
    ApRect *rects;
    ApSpan *spans;
    uint32_t *rows; /* height+1 offsets into spans */
    ApTriangle *triangles;
    size_t rect_count, span_count, triangle_count;
    uint32_t component_count;
} ApMesh;

typedef struct { ApPoint *points; size_t count; } ApPath;
typedef struct {
    double *cost;
    int32_t *parent, *position, *heap;
    uint32_t *stamp;
    size_t capacity, heap_count;
    uint32_t generation;
} ApSearch;

/* Zero-initialize owning structs. Build is transactional. Mesh is immutable. */
ApResult ap_mesh_build(const ApMap *map, ApMesh *mesh);
void ap_mesh_free(ApMesh *mesh);
/* Outside points, blocked cells and non-finite values return -1. */
int32_t ap_mesh_locate(const ApMesh *mesh, ApPoint point);
/* Triangle A*, then funnel through full convex-rectangle portals. Shortest
   within that corridor, not necessarily globally. Each concurrent query needs
   its own reusable search workspace. */
ApResult ap_path_find(const ApMesh *mesh, ApSearch *search,
                      ApPoint start, ApPoint end, ApPath *path);
void ap_search_free(ApSearch *search);
void ap_path_free(ApPath *path);

#endif
