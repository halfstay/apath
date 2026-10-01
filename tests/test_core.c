#include "ap_history.h"
#include "ap_map.h"
#include "ap_nav.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

static uint32_t rng_state = 0x1a2b3c4d;
static uint32_t random32(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static void load_bytes(const unsigned char *bytes, size_t n, ApMap *map,
                       ApResult expected) {
  FILE *f = tmpfile();
  CHECK(f != NULL);
  CHECK(fwrite(bytes, 1, n, f) == n);
  rewind(f);
  CHECK(ap_map_read(f, map) == expected);
  CHECK(fclose(f) == 0);
}

static void test_files(void) {
  ApMap map = {0}, copy = {0};
  unsigned char expected[] = {'A', 'M', 'D', 24, 0, 72, 0, 72, 0, 0x21, 1};
  unsigned char actual[32], bad[32];
  FILE *f;
  size_t i;
  CHECK(ap_map_create(&map, 24, 3, 3) == AP_OK);
  ap_map_set(&map, 0, 0, 1);
  ap_map_set(&map, 2, 1, 1);
  ap_map_set(&map, 2, 2, 1);
  f = tmpfile();
  CHECK(f != NULL);
  CHECK(ap_map_write(f, &map) == AP_OK);
  rewind(f);
  CHECK(fread(actual, 1, sizeof(actual), f) == sizeof(expected));
  CHECK(!memcmp(actual, expected, sizeof(expected)));
  CHECK(fclose(f) == 0);
  load_bytes(expected, sizeof(expected), &copy, AP_OK);
  CHECK(copy.width == 3 && copy.height == 3 && copy.cell_size == 24);
  CHECK(!memcmp(map.bits, copy.bits, ap_map_bytes(&map)));
  for (i = 0; i < sizeof(expected); ++i) {
    load_bytes(expected, i, &copy, AP_INVALID);
    CHECK(copy.width == 3 && copy.bits[0] == 0x21);
  }
  memcpy(bad, expected, sizeof(expected));
  bad[0] = 'Z';
  load_bytes(bad, sizeof(expected), &copy, AP_INVALID);
  memcpy(bad, expected, sizeof(expected));
  bad[3] = 0;
  load_bytes(bad, sizeof(expected), &copy, AP_INVALID);
  memcpy(bad, expected, sizeof(expected));
  bad[5] = 0;
  load_bytes(bad, sizeof(expected), &copy, AP_INVALID);
  memcpy(bad, expected, sizeof(expected));
  bad[7] = 0;
  load_bytes(bad, sizeof(expected), &copy, AP_INVALID);
  memcpy(bad, expected, sizeof(expected));
  bad[10] = 0x81;
  load_bytes(bad, sizeof(expected), &copy, AP_INVALID);
  memcpy(bad, expected, sizeof(expected));
  bad[11] = 0;
  load_bytes(bad, sizeof(expected) + 1, &copy, AP_INVALID);
  CHECK(ap_map_create(&copy, 65535, 65535, 65535) == AP_INVALID);
  CHECK(copy.width == 3);
  CHECK(ap_map_create(&copy, 65535, 65535, 1) == AP_OK);
  CHECK(copy.cell_size == 65535 && copy.width == 65535);
  ap_map_set(&copy, 65534, 0, 1);
  CHECK(ap_map_get(&copy, 65534, 0));
  CHECK(!ap_map_get(&copy, -1, 0) && !ap_map_get(&copy, 65535, 0));
  ap_map_free(&map);
  ap_map_free(&copy);
  for (i = 0; i < 100; ++i) {
    uint32_t j;
    CHECK(ap_map_create_pixels(&map, 1 + random32() % 65535,
                               1 + random32() % 65535,
                               1 + random32() % 65535) == AP_OK);
    for (j = 0; j < ap_map_cells(&map); ++j)
      ap_map_put(&map, j, (int)(random32() & 1));
    f = tmpfile();
    CHECK(f != NULL);
    CHECK(ap_map_write(f, &map) == AP_OK);
    rewind(f);
    CHECK(ap_map_read(f, &copy) == AP_OK);
    fclose(f);
    CHECK(map.cell_size == copy.cell_size && map.width == copy.width &&
          map.height == copy.height);
    CHECK(map.pixel_width == copy.pixel_width &&
          map.pixel_height == copy.pixel_height);
    CHECK(!memcmp(map.bits, copy.bits, ap_map_bytes(&map)));
  }
  ap_map_free(&map);
  ap_map_free(&copy);
}

static void stroke(ApHistory *h, ApMap *m, int x, int value) {
  ap_history_begin(h);
  CHECK(ap_history_set(h, m, x, 0, value) == AP_OK);
  CHECK(ap_history_commit(h, m) == AP_OK);
}

static void test_history(void) {
  ApHistory h;
  ApMap m = {0};
  CHECK(ap_map_create(&m, 8, 10, 10) == AP_OK);
  ap_history_init(&h, 8192);
  CHECK(!ap_history_dirty(&h));
  stroke(&h, &m, 0, 1);
  CHECK(ap_history_dirty(&h));
  ap_history_saved(&h);
  CHECK(!ap_history_dirty(&h));
  stroke(&h, &m, 1, 1);
  CHECK(ap_history_undo(&h, &m));
  CHECK(!ap_history_dirty(&h));
  CHECK(!ap_map_get(&m, 1, 0));
  CHECK(ap_history_redo(&h, &m));
  CHECK(ap_map_get(&m, 1, 0));
  CHECK(ap_history_undo(&h, &m));
  stroke(&h, &m, 2, 1);
  CHECK(!ap_history_redo(&h, &m));
  CHECK(ap_history_dirty(&h));
  CHECK(ap_history_undo(&h, &m));
  CHECK(!ap_history_dirty(&h));
  ap_history_begin(&h);
  CHECK(ap_history_set(&h, &m, 3, 0, 1) == AP_OK);
  CHECK(ap_history_set(&h, &m, 3, 0, 0) == AP_OK);
  CHECK(ap_history_set(&h, &m, 3, 0, 1) == AP_OK);
  ap_history_cancel(&h, &m);
  CHECK(!ap_map_get(&m, 3, 0));
  CHECK(!ap_history_dirty(&h));
  ap_history_begin(&h);
  CHECK(ap_history_set(&h, &m, 3, 0, 1) == AP_OK);
  CHECK(ap_history_set(&h, &m, 3, 0, 0) == AP_OK);
  CHECK(ap_history_commit(&h, &m) == AP_OK);
  CHECK(ap_history_undo(&h, &m));
  CHECK(!ap_map_get(&m, 3, 0));
  CHECK(ap_history_redo(&h, &m));
  CHECK(!ap_map_get(&m, 3, 0));
  ap_history_begin(&h);
  CHECK(ap_history_set(&h, &m, -1, 0, 1) == AP_OK);
  CHECK(ap_history_commit(&h, &m) == AP_OK);
  ap_history_free(&h);
  ap_history_init(&h, 1);
  stroke(&h, &m, 4, 1);
  stroke(&h, &m, 5, 1);
  CHECK(h.count == 1);
  CHECK(ap_history_undo(&h, &m));
  CHECK(!ap_map_get(&m, 5, 0) && ap_map_get(&m, 4, 0));
  CHECK(!ap_history_undo(&h, &m));
  ap_history_free(&h);
  ap_map_free(&m);
}

static double cross(ApPoint a, ApPoint b, ApPoint c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
static int equal_vertex(ApVertex a, ApVertex b) {
  return a.x == b.x && a.y == b.y;
}

static void validate_mesh(const ApMap *map, const ApMesh *m) {
  double total = 0;
  size_t i;
  double walk = 0;
  for (i = 0; i < ap_map_cells(map); ++i)
    if (ap_map_bit(map, (uint32_t)i)) {
      unsigned x = (unsigned)(i % map->width), y = (unsigned)(i / map->width);
      walk += fmin(1, m->extent_x - x) * fmin(1, m->extent_y - y);
    }
  for (i = 0; i < m->triangle_count; ++i) {
    const ApTriangle *t = &m->triangles[i];
    ApPoint a = {t->v[0].x * .5, t->v[0].y * .5};
    CHECK(a.x <= m->extent_x && a.y <= m->extent_y);
    ApPoint b = {t->v[1].x * .5, t->v[1].y * .5};
    ApPoint c = {t->v[2].x * .5, t->v[2].y * .5};
    unsigned j;
    double area = cross(a, b, c) * .5;
    CHECK(area > 0);
    total += area;
    CHECK(ap_map_get(map, (int)floor((a.x + b.x + c.x) / 3),
                     (int)floor((a.y + b.y + c.y) / 3)));
    for (j = 0; j < 3; ++j)
      if (t->neighbor[j] >= 0) {
        const ApTriangle *n;
        unsigned k, matches = 0;
        CHECK((size_t)t->neighbor[j] < m->triangle_count);
        n = &m->triangles[t->neighbor[j]];
        for (k = 0; k < 3; ++k)
          if (n->neighbor[k] == (int32_t)i &&
              equal_vertex(t->v[j], n->v[(k + 1) % 3]) &&
              equal_vertex(t->v[(j + 1) % 3], n->v[k]))
            ++matches;
        CHECK(matches == 1);
        CHECK(n->component == t->component);
      }
  }
  CHECK(fabs(total - (double)walk) < 1e-6);
  for (i = 0; i < ap_map_cells(map); ++i) {
    int x = (int)(i % map->width), y = (int)(i / map->width);
    double xx = x + fmin(1, m->extent_x - x) * .5,
           yy = y + fmin(1, m->extent_y - y) * .5;
    int32_t t = ap_mesh_locate(m, (ApPoint){xx, yy});
    CHECK((t >= 0) == ap_map_get(map, x, y));
    if (t >= 0) {
      int32_t right = ap_mesh_locate(
          m, (ApPoint){x + 1 + fmax(0, fmin(1, m->extent_x - x - 1)) * .5, yy});
      int32_t up = ap_mesh_locate(
          m, (ApPoint){xx, y + 1 + fmax(0, fmin(1, m->extent_y - y - 1)) * .5});
      if (right >= 0)
        CHECK(m->triangles[right].component == m->triangles[t].component);
      if (up >= 0)
        CHECK(m->triangles[up].component == m->triangles[t].component);
    }
  }
}

/* Independent 4-neighbor flood fill reference. */
static int reachable(const ApMap *m, int start, int end) {
  size_t n = ap_map_cells(m), head = 0, tail = 0;
  unsigned char *seen = (unsigned char *)calloc(n, 1);
  int *queue = (int *)malloc(n * sizeof(int));
  int result = 0;
  CHECK(seen && queue);
  queue[tail++] = start;
  seen[start] = 1;
  while (head < tail) {
    int i = queue[head++], x = i % m->width, y = i / m->width;
    int dx[] = {-1, 1, 0, 0}, dy[] = {0, 0, -1, 1}, k;
    if (i == end) {
      result = 1;
      break;
    }
    for (k = 0; k < 4; ++k) {
      int xx = x + dx[k], yy = y + dy[k], index = yy * m->width + xx;
      if (ap_map_get(m, xx, yy) && !seen[index]) {
        seen[index] = 1;
        queue[tail++] = index;
      }
    }
  }
  free(seen);
  free(queue);
  return result;
}

static int slab(double a, double d, double low, double high, double *lo,
                double *hi) {
  double p, q;
  if (fabs(d) < 1e-12)
    return a >= low && a <= high;
  p = (low - a) / d;
  q = (high - a) / d;
  if (p > q) {
    double temp = p;
    p = q;
    q = temp;
  }
  if (p > *lo)
    *lo = p;
  if (q < *hi)
    *hi = q;
  return *lo <= *hi;
}

static void validate_path(const ApMap *map, const ApPath *p, ApPoint start,
                          ApPoint end) {
  size_t i;
  CHECK(p->count > 0);
  CHECK(hypot(p->points[0].x - start.x, p->points[0].y - start.y) < 1e-8);
  CHECK(hypot(p->points[p->count - 1].x - end.x,
              p->points[p->count - 1].y - end.y) < 1e-8);
  for (i = 0; i < p->count; ++i) {
    CHECK(isfinite(p->points[i].x) && isfinite(p->points[i].y));
    CHECK(p->points[i].x >= 0 &&
          p->points[i].x <= map->pixel_width / (double)map->cell_size);
    CHECK(p->points[i].y >= 0 &&
          p->points[i].y <= map->pixel_height / (double)map->cell_size);
  }
  for (i = 1; i < p->count; ++i) {
    ApPoint a = p->points[i - 1], b = p->points[i];
    int x, y;
    for (y = 0; y < map->height; ++y)
      for (x = 0; x < map->width; ++x)
        if (!ap_map_get(map, x, y)) {
          double lo = 0, hi = 1;
          CHECK(!(slab(a.x, b.x - a.x, x + 1e-7, x + 1 - 1e-7, &lo, &hi) &&
                  slab(a.y, b.y - a.y, y + 1e-7, y + 1 - 1e-7, &lo, &hi)));
        }
  }
}

static void test_navigation(void) {
  ApMap map = {0};
  ApMesh mesh = {0};
  ApSearch search = {0};
  ApPath path = {0};
  unsigned x, y, iteration;
  CHECK(ap_map_create(&map, 24, 12, 10) == AP_OK);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  CHECK(mesh.triangle_count == 0);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){.5, .5}) == -1);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.5, .5}, (ApPoint){1.5, 1.5},
                     &path) == AP_INVALID);
  for (y = 0; y < map.height; ++y)
    for (x = 0; x < map.width; ++x)
      ap_map_set(&map, x, y, 1);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  CHECK(mesh.rect_count == 1 && mesh.triangle_count == 4);
  validate_mesh(&map, &mesh);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.5, .5}, (ApPoint){11.5, 9.5},
                     &path) == AP_OK);
  CHECK(path.count == 2);
  validate_path(&map, &path, (ApPoint){.5, .5}, (ApPoint){11.5, 9.5});
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.3, .8}, (ApPoint){9.6, 4.1},
                     &path) == AP_OK);
  CHECK(path.count == 2);
  CHECK(fabs((path.points[1].x - path.points[0].x) -
             (path.points[1].y - path.points[0].y)) > 1);
  validate_path(&map, &path, (ApPoint){.3, .8}, (ApPoint){9.6, 4.1});
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.5, .5}, (ApPoint){.5, .5},
                     &path) == AP_OK);
  CHECK(path.count == 1);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){NAN, 0}) == -1);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){0, INFINITY}) == -1);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){12, 0}) == -1);
  /* An obstacle with a gap forces a turn; exact edge subdivisions must agree.
   */
  for (y = 0; y < 8; ++y)
    ap_map_set(&map, 5, y, 0);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  validate_mesh(&map, &mesh);
  search.generation = UINT32_MAX;
  CHECK(ap_path_find(&mesh, &search, (ApPoint){2.5, 1.5}, (ApPoint){8.5, 1.5},
                     &path) == AP_OK);
  CHECK(path.count >= 3);
  validate_path(&map, &path, (ApPoint){2.5, 1.5}, (ApPoint){8.5, 1.5});
  for (y = 8; y < 10; ++y)
    ap_map_set(&map, 5, y, 0);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  CHECK(mesh.component_count == 2);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){2.5, 1.5}, (ApPoint){8.5, 1.5},
                     &path) == AP_NO_PATH);
  CHECK(
      path.count >=
      3); /* Core failure preserves output; UI explicitly clears stale paths. */
  CHECK(ap_map_create(&map, 1, 2, 2) == AP_OK);
  ap_map_set(&map, 0, 0, 1);
  ap_map_set(&map, 1, 1, 1);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  CHECK(mesh.component_count == 2);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.5, .5}, (ApPoint){1.5, 1.5},
                     &path) == AP_NO_PATH);
  for (iteration = 0; iteration < 240; ++iteration) {
    unsigned q, density = 20 + random32() % 76;
    CHECK(ap_map_create(&map, 16, 8 + random32() % 25, 8 + random32() % 19) ==
          AP_OK);
    for (y = 0; y < map.height; ++y)
      for (x = 0; x < map.width; ++x)
        ap_map_set(&map, x, y, random32() % 100 < density);
    CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
    validate_mesh(&map, &mesh);
    for (q = 0; q < 20; ++q) {
      int a = (int)(random32() % ap_map_cells(&map)),
          b = (int)(random32() % ap_map_cells(&map));
      ApPoint from = {a % map.width + .25 + (random32() % 50) / 100.,
                      a / map.width + .25 + (random32() % 50) / 100.};
      ApPoint to = {b % map.width + .25 + (random32() % 50) / 100.,
                    b / map.width + .25 + (random32() % 50) / 100.};
      ApResult result;
      if (!ap_map_bit(&map, (uint32_t)a) || !ap_map_bit(&map, (uint32_t)b))
        continue;
      result = ap_path_find(&mesh, &search, from, to, &path);
      CHECK(result == (reachable(&map, a, b) ? AP_OK : AP_NO_PATH));
      if (result == AP_OK)
        validate_path(&map, &path, from, to);
    }
  }
  ap_path_free(&path);
  ap_search_free(&search);
  ap_mesh_free(&mesh);
  ap_map_free(&map);
}

static double path_length(const ApPoint *points, size_t count) {
  size_t i;
  double length = 0;
  for (i = 1; i < count; ++i)
    length +=
        hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y);
  return length;
}

static void check_shortest(const ApMap *map, const ApMesh *mesh,
                           ApSearch *search, ApPath *path,
                           const ApPoint *expected, size_t count) {
  unsigned reverse;
  for (reverse = 0; reverse < 2; ++reverse) {
    size_t i;
    ApPoint start = expected[reverse ? count - 1 : 0];
    ApPoint end = expected[reverse ? 0 : count - 1];
    CHECK(ap_path_find(mesh, search, start, end, path) == AP_OK);
    validate_path(map, path, start, end);
    CHECK(path->count == count);
    CHECK(fabs(path_length(path->points, path->count) -
               path_length(expected, count)) < 1e-8);
    for (i = 0; i < count; ++i) {
      ApPoint p = expected[reverse ? count - 1 - i : i];
      CHECK(hypot(path->points[i].x - p.x, path->points[i].y - p.y) < 1e-8);
    }
  }
}

static ApPoint corridor_transform(ApPoint p, unsigned transform) {
  if (transform & 1)
    p.x = 56 - p.x;
  if (transform & 2)
    p.y = 42 - p.y;
  if (transform & 4) {
    double swap = p.x;
    p.x = p.y;
    p.y = swap;
  }
  return p;
}

static void test_wide_corridors(void) {
  ApMap map = {0};
  ApMesh mesh = {0};
  ApSearch search = {0};
  ApPath path = {0};
  unsigned transform, x, y;
  /* apath.png topology: two upper rooms, narrow legs and a bottom bar.
     The taut route touches obstacle corners, never the legs' fan centers.
     Reflections/axis exchange cover all four portal directions. */
  for (transform = 0; transform < 8; ++transform) {
    ApPoint expected[] = {{6.6, 18.3}, {9, 6}, {40, 6}, {40, 17}, {37, 34}};
    size_t i;
    CHECK(ap_map_create(&map, 1, transform & 4 ? 42 : 56,
                        transform & 4 ? 56 : 42) == AP_OK);
    for (y = 0; y < 42; ++y)
      for (x = 0; x < 56; ++x) {
        int walk = (y < 6 && x >= 5 && x < 45) ||
                   (y >= 6 && y < 13 && x >= 5 && x < 9) ||
                   (y >= 13 && y < 24 && x < 10) ||
                   (y >= 6 && y < 17 && x >= 40 && x < 44) ||
                   (y >= 17 && x >= 36);
        ApPoint p = corridor_transform((ApPoint){x + .5, y + .5}, transform);
        if (walk)
          ap_map_set(&map, (unsigned)p.x, (unsigned)p.y, 1);
      }
    CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
    validate_mesh(&map, &mesh);
    for (i = 0; i < sizeof(expected) / sizeof(*expected); ++i)
      expected[i] = corridor_transform(expected[i], transform);
    check_shortest(&map, &mesh, &search, &path, expected, 5);
    /* Change both endpoints without rebuilding the mesh/workspace. */
    expected[0] = corridor_transform((ApPoint){6.2, 20.5}, transform);
    expected[4] = corridor_transform((ApPoint){38.2, 36.7}, transform);
    check_shortest(&map, &mesh, &search, &path, expected, 5);
  }
  CHECK(ap_map_create(&map, 1, 12, 10) == AP_OK);
  for (y = 0; y < 10; ++y)
    for (x = 0; x < 12; ++x)
      ap_map_set(&map, x, y, y < 3 || y >= 7 || (x >= 2 && x < 10));
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  {
    const ApPoint expected[] = {{4.25, .3}, {7.65, 9.6}};
    CHECK(mesh.rect_count > 1);
    check_shortest(&map, &mesh, &search, &path, expected, 2);
  }
  /* Full-width portals must stop at the fractional last column/row. */
  CHECK(ap_map_create_pixels(&map, 10, 65, 47) == AP_OK);
  for (y = 0; y < map.height; ++y)
    for (x = 0; x < map.width; ++x)
      ap_map_set(&map, x, y, y == 4 || x >= (y < 2 ? 2u : 4u));
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  {
    const ApPoint bent[] = {{2.25, .25}, {4, 2}, {4, 4}, {.25, 4.5}};
    const ApPoint straight[] = {{6.4, .2}, {6.45, 4.6}};
    check_shortest(&map, &mesh, &search, &path, bent, 4);
    check_shortest(&map, &mesh, &search, &path, straight, 2);
  }
  /* These diagonal cells belong to one component via a longer route.
     Smoothing must not cut across their isolated contact at (2,2). */
  CHECK(ap_map_create(&map, 1, 5, 5) == AP_OK);
  for (y = 0; y < 5; ++y)
    for (x = 0; x < 5; ++x)
      ap_map_set(&map, x, y, 1);
  ap_map_set(&map, 2, 1, 0);
  ap_map_set(&map, 1, 2, 0);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  CHECK(mesh.component_count == 1);
  for (transform = 0; transform < 2; ++transform) {
    size_t i;
    ApPoint start = transform ? (ApPoint){2.5, 2.5} : (ApPoint){1.5, 1.5};
    ApPoint end = transform ? (ApPoint){1.5, 1.5} : (ApPoint){2.5, 2.5};
    CHECK(ap_path_find(&mesh, &search, start, end, &path) == AP_OK);
    validate_path(&map, &path, start, end);
    CHECK(path_length(path.points, path.count) >= 2 + sqrt(2) - 1e-8);
    for (i = 1; i < path.count; ++i) {
      ApPoint a = path.points[i - 1], b = path.points[i];
      double cross = (b.x - a.x) * (2 - a.y) - (b.y - a.y) * (2 - a.x);
      double dot = (2 - a.x) * (2 - b.x) + (2 - a.y) * (2 - b.y);
      CHECK(fabs(cross) > 1e-8 || dot > 1e-8);
    }
  }
  ap_path_free(&path);
  ap_search_free(&search);
  ap_mesh_free(&mesh);
  ap_map_free(&map);
}

static void test_pixel_edges(void) {
  ApMap map = {0}, copy = {0};
  ApMesh mesh = {0};
  ApSearch search = {0};
  ApPath path = {0};
  unsigned char expected[] = {'A', 'M', 'D', 20, 0, 45, 0, 27, 0, 0x24};
  unsigned char bytes[32];
  FILE *f;
  unsigned x, y, iteration;
  CHECK(ap_map_create_pixels(&map, 20, 45, 27) == AP_OK);
  CHECK(map.width == 3 && map.height == 2 && ap_map_bytes(&map) == 1);
  ap_map_set(&map, 2, 0, 1);
  ap_map_set(&map, 2, 1, 1);
  f = tmpfile();
  CHECK(f);
  CHECK(ap_map_write(f, &map) == AP_OK);
  rewind(f);
  CHECK(fread(bytes, 1, sizeof(bytes), f) == sizeof(expected));
  CHECK(!memcmp(bytes, expected, sizeof(expected)));
  rewind(f);
  CHECK(ap_map_read(f, &copy) == AP_OK);
  fclose(f);
  CHECK(copy.pixel_width == 45 && copy.pixel_height == 27 &&
        copy.bits[0] == 0x24);
  CHECK(ap_map_create_pixels(&copy, 0, 20, 20) == AP_INVALID);
  CHECK(ap_map_create_pixels(&copy, 1, 65535, 65535) == AP_INVALID);
  CHECK(copy.pixel_width == 45 && copy.bits[0] == 0x24);
  for (x = 0; x < 3; ++x)
    for (y = 0; y < 2; ++y)
      ap_map_set(&map, x, y, 1);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  validate_mesh(&map, &mesh);
  CHECK(fabs(mesh.extent_x - 2.25) < 1e-12 &&
        fabs(mesh.extent_y - 1.35) < 1e-12);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){2.249, 1.349}) >= 0);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){2.25, 1}) == -1);
  CHECK(ap_mesh_locate(&mesh, (ApPoint){2.1, 1.35}) == -1);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.2, .3}, (ApPoint){2.2, 1.3},
                     &path) == AP_OK);
  CHECK(path.count == 2);
  validate_path(&map, &path, (ApPoint){.2, .3}, (ApPoint){2.2, 1.3});
  CHECK(ap_map_create_pixels(&map, 65535, 1, 1) == AP_OK);
  CHECK(map.width == 1 && map.height == 1);
  ap_map_set(&map, 0, 0, 1);
  CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
  validate_mesh(&map, &mesh);
  CHECK(ap_path_find(&mesh, &search, (ApPoint){.1 / 65535, .2 / 65535},
                     (ApPoint){.8 / 65535, .7 / 65535}, &path) == AP_OK);
  CHECK(path.count == 2);
  /* Fractional last rows/columns across many split boundaries and obstacles. */
  for (iteration = 0; iteration < 80; ++iteration) {
    unsigned q;
    CHECK(ap_map_create_pixels(&map, 17, 100 + random32() % 270,
                               100 + random32() % 230) == AP_OK);
    for (y = 0; y < map.height; ++y)
      for (x = 0; x < map.width; ++x)
        ap_map_set(&map, x, y, random32() % 100 < 75);
    CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
    validate_mesh(&map, &mesh);
    for (q = 0; q < 20; ++q) {
      int a = (int)(random32() % ap_map_cells(&map)),
          b = (int)(random32() % ap_map_cells(&map));
      int ax = a % map.width, ay = a / map.width, bx = b % map.width,
          by = b / map.width;
      ApPoint from = {ax + fmin(1, mesh.extent_x - ax) * .5,
                      ay + fmin(1, mesh.extent_y - ay) * .5};
      ApPoint to = {bx + fmin(1, mesh.extent_x - bx) * .5,
                    by + fmin(1, mesh.extent_y - by) * .5};
      ApResult result;
      if (!ap_map_bit(&map, (uint32_t)a) || !ap_map_bit(&map, (uint32_t)b))
        continue;
      result = ap_path_find(&mesh, &search, from, to, &path);
      CHECK(result == (reachable(&map, a, b) ? AP_OK : AP_NO_PATH));
      if (result == AP_OK)
        validate_path(&map, &path, from, to);
    }
  }
  ap_path_free(&path);
  ap_search_free(&search);
  ap_mesh_free(&mesh);
  ap_map_free(&map);
  ap_map_free(&copy);
}

static void benchmark(void) {
  ApMap map = {0};
  ApMesh mesh = {0};
  ApSearch search = {0};
  ApPath path = {0};
  unsigned scenario;
  for (scenario = 0; scenario < 3; ++scenario) {
    unsigned x, y, i, size = scenario == 0 ? 2048 : 512;
    clock_t before, built, searched;
    CHECK(ap_map_create(&map, 16, size, size) == AP_OK);
    for (y = 0; y < size; ++y)
      for (x = 0; x < size; ++x)
        ap_map_set(&map, x, y,
                   scenario == 0 ||
                       (scenario == 1 ? (x % 16 != 8 || y % 64 > 48)
                                      : ((x + y) % 2 == 0)));
    before = clock();
    CHECK(ap_mesh_build(&map, &mesh) == AP_OK);
    built = clock();
    for (i = 0; i < 100; ++i)
      (void)ap_path_find(&mesh, &search, (ApPoint){.5, .5},
                         (ApPoint){size - .5, size - .5}, &path);
    searched = clock();
    printf("benchmark %s %ux%u: %lu rectangles, %lu triangles; build %.3f s; "
           "100 queries %.3f s\n",
           scenario == 0   ? "solid"
           : scenario == 1 ? "obstacles"
                           : "checkerboard",
           size, size, (unsigned long)mesh.rect_count,
           (unsigned long)mesh.triangle_count,
           (double)(built - before) / CLOCKS_PER_SEC,
           (double)(searched - built) / CLOCKS_PER_SEC);
  }
  ap_path_free(&path);
  ap_search_free(&search);
  ap_mesh_free(&mesh);
  ap_map_free(&map);
}

int main(int argc, char **argv) {
  test_files();
  test_history();
  test_navigation();
  test_pixel_edges();
  test_wide_corridors();
  printf("PASS: %u core checks (including 320 randomized maps)\n", checks);
  if (argc > 1 && !strcmp(argv[1], "--benchmark"))
    benchmark();
  return 0;
}
