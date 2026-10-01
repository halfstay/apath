#include "ap_nav.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  int32_t line, pos;
} Cut;
typedef struct {
  ApVertex a, b;
  int32_t triangle;
  unsigned side;
} Edge;
typedef struct {
  ApPoint left, right;
} Portal;

static int reserve(void **p, size_t *capacity, size_t need, size_t element) {
  size_t cap = *capacity ? *capacity : 64;
  void *q;
  if (need <= *capacity)
    return 1;
  while (cap < need) {
    if (cap > SIZE_MAX / 2) {
      cap = need;
      break;
    }
    cap *= 2;
  }
  if (cap > SIZE_MAX / element)
    return 0;
  q = realloc(*p, cap * element);
  if (!q)
    return 0;
  *p = q;
  *capacity = cap;
  return 1;
}

static int vertex_cmp(ApVertex a, ApVertex b) {
  if (a.x != b.x)
    return a.x < b.x ? -1 : 1;
  return a.y < b.y ? -1 : a.y > b.y;
}

static int cut_cmp(const void *a, const void *b) {
  const Cut *x = (const Cut *)a, *y = (const Cut *)b;
  if (x->line != y->line)
    return x->line < y->line ? -1 : 1;
  return x->pos < y->pos ? -1 : x->pos > y->pos;
}

static size_t cut_lower(const Cut *cuts, size_t n, int32_t line, int32_t pos) {
  size_t lo = 0, hi = n;
  Cut key = {line, pos};
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cut_cmp(&cuts[mid], &key) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

static int edge_cmp(const void *a, const void *b) {
  const Edge *x = (const Edge *)a, *y = (const Edge *)b;
  int c = vertex_cmp(x->a, y->a);
  return c ? c : vertex_cmp(x->b, y->b);
}

void ap_mesh_free(ApMesh *m) {
  if (!m)
    return;
  free(m->rects);
  free(m->spans);
  free(m->rows);
  free(m->triangles);
  memset(m, 0, sizeof(*m));
}

static int build_rects(const ApMap *map, ApMesh *m) {
  size_t rect_cap = 0, span_cap = 0;
  uint32_t y;
  m->rows = (uint32_t *)calloc((size_t)map->height + 1, sizeof(*m->rows));
  if (!m->rows)
    return 0;
  for (y = 0; y < map->height; ++y) {
    uint32_t x = 0, prev = y ? m->rows[y - 1] : 0;
    uint32_t prev_end = m->rows[y];
    while (x < map->width) {
      uint32_t x0, rect;
      while (x < map->width && !ap_map_get(map, (int)x, (int)y))
        ++x;
      if (x == map->width)
        break;
      x0 = x;
      while (x < map->width && ap_map_get(map, (int)x, (int)y))
        ++x;
      while (prev < prev_end && m->spans[prev].x0 < x0)
        ++prev;
      if (prev < prev_end && m->spans[prev].x0 == x0 &&
          m->spans[prev].x1 == x) {
        rect = m->spans[prev].rect;
        m->rects[rect].y1 = y + 1;
      } else {
        if (!reserve((void **)&m->rects, &rect_cap, m->rect_count + 1,
                     sizeof(*m->rects)))
          return 0;
        rect = (uint32_t)m->rect_count++;
        m->rects[rect] = (ApRect){x0, y, x, y + 1, 0, 0};
      }
      if (!reserve((void **)&m->spans, &span_cap, m->span_count + 1,
                   sizeof(*m->spans)))
        return 0;
      m->spans[m->span_count++] = (ApSpan){x0, x, rect};
    }
    m->rows[y + 1] = (uint32_t)m->span_count;
  }
  return 1;
}

static int add_boundary(ApVertex **v, size_t *count, size_t *cap,
                        const Cut *cuts, size_t n, int32_t line, int32_t lo,
                        int32_t hi, int vertical, int reverse) {
  size_t begin = cut_lower(cuts, n, line, lo);
  size_t end = cut_lower(cuts, n, line, hi);
  size_t i;
  /* Forward: [lo,hi). Reverse: (lo,hi], starting at the corner. */
  if (reverse) {
    ++begin;
    ++end;
  }
  if (!reserve((void **)v, cap, *count + end - begin, sizeof(**v)))
    return 0;
  for (i = begin; i < end; ++i) {
    int32_t pos = cuts[reverse ? end - 1 - (i - begin) : i].pos;
    (*v)[(*count)++] = vertical ? (ApVertex){line * 2, pos * 2}
                                : (ApVertex){pos * 2, line * 2};
  }
  return 1;
}

static ApResult triangulate(ApMesh *m) {
  Cut *horizontal = NULL, *vertical = NULL;
  ApVertex *boundary = NULL;
  size_t i, n = m->rect_count * 4, unique = 0, vcap = 0, tcap = 0;
  ApResult result = AP_MEMORY;
  if (!n)
    return AP_OK;
  if (n > SIZE_MAX / sizeof(Cut))
    return AP_MEMORY;
  horizontal = (Cut *)malloc(n * sizeof(Cut));
  vertical = (Cut *)malloc(n * sizeof(Cut));
  if (!horizontal || !vertical)
    goto done;
  for (i = 0; i < m->rect_count; ++i) {
    ApRect r = m->rects[i];
    horizontal[4 * i] = (Cut){(int32_t)r.y0, (int32_t)r.x0};
    horizontal[4 * i + 1] = (Cut){(int32_t)r.y0, (int32_t)r.x1};
    horizontal[4 * i + 2] = (Cut){(int32_t)r.y1, (int32_t)r.x0};
    horizontal[4 * i + 3] = (Cut){(int32_t)r.y1, (int32_t)r.x1};
  }
  for (i = 0; i < n; ++i)
    vertical[i] = (Cut){horizontal[i].pos, horizontal[i].line};
  qsort(horizontal, n, sizeof(Cut), cut_cmp);
  qsort(vertical, n, sizeof(Cut), cut_cmp);
  for (i = 0; i < n; ++i)
    if (!i || cut_cmp(&horizontal[i], &horizontal[unique - 1]))
      horizontal[unique++] = horizontal[i];
  n = unique;
  unique = 0;
  /* Both tables represent the same unique corner set. */
  for (i = 0; i < m->rect_count * 4; ++i)
    if (!i || cut_cmp(&vertical[i], &vertical[unique - 1]))
      vertical[unique++] = vertical[i];
  for (i = 0; i < m->rect_count; ++i) {
    ApRect *r = &m->rects[i];
    size_t count = 0, j;
    ApVertex center = {r->x0 + fmin(r->x1, m->extent_x),
                       r->y0 + fmin(r->y1, m->extent_y)};
    if (!add_boundary(&boundary, &count, &vcap, horizontal, n, (int32_t)r->y0,
                      (int32_t)r->x0, (int32_t)r->x1, 0, 0) ||
        !add_boundary(&boundary, &count, &vcap, vertical, unique,
                      (int32_t)r->x1, (int32_t)r->y0, (int32_t)r->y1, 1, 0) ||
        !add_boundary(&boundary, &count, &vcap, horizontal, n, (int32_t)r->y1,
                      (int32_t)r->x0, (int32_t)r->x1, 0, 1) ||
        !add_boundary(&boundary, &count, &vcap, vertical, unique,
                      (int32_t)r->x0, (int32_t)r->y0, (int32_t)r->y1, 1, 1))
      goto done;
    if (count > (size_t)INT32_MAX - m->triangle_count ||
        !reserve((void **)&m->triangles, &tcap, m->triangle_count + count,
                 sizeof(*m->triangles)))
      goto done;
    r->first = (uint32_t)m->triangle_count;
    r->count = (uint32_t)count;
    for (j = 0; j < count; ++j) {
      boundary[j].x = fmin(boundary[j].x, m->extent_x * 2);
      boundary[j].y = fmin(boundary[j].y, m->extent_y * 2);
    }
    for (j = 0; j < count; ++j) {
      ApTriangle *t = &m->triangles[m->triangle_count++];
      t->v[0] = center;
      t->v[1] = boundary[j];
      t->v[2] = boundary[(j + 1) % count];
      t->neighbor[0] = t->neighbor[1] = t->neighbor[2] = -1;
      t->component = UINT32_MAX;
    }
  }
  result = AP_OK;
done:
  free(horizontal);
  free(vertical);
  free(boundary);
  return result;
}

static ApResult connect_mesh(ApMesh *m) {
  Edge *edges;
  int32_t *stack;
  size_t i, n = m->triangle_count;
  if (!n)
    return AP_OK;
  if (n > SIZE_MAX / (3 * sizeof(Edge)))
    return AP_MEMORY;
  edges = (Edge *)malloc(n * 3 * sizeof(*edges));
  if (!edges)
    return AP_MEMORY;
  for (i = 0; i < n; ++i) {
    unsigned j;
    for (j = 0; j < 3; ++j) {
      Edge *e = &edges[i * 3 + j];
      e->a = m->triangles[i].v[j];
      e->b = m->triangles[i].v[(j + 1) % 3];
      if (vertex_cmp(e->a, e->b) > 0) {
        ApVertex t = e->a;
        e->a = e->b;
        e->b = t;
      }
      e->triangle = (int32_t)i;
      e->side = j;
    }
  }
  qsort(edges, n * 3, sizeof(*edges), edge_cmp);
  for (i = 0; i + 1 < n * 3; ++i) {
    if (!edge_cmp(&edges[i], &edges[i + 1])) {
      Edge a = edges[i], b = edges[++i];
      m->triangles[a.triangle].neighbor[a.side] = b.triangle;
      m->triangles[b.triangle].neighbor[b.side] = a.triangle;
    }
  }
  free(edges);
  stack = (int32_t *)malloc(n * sizeof(*stack));
  if (!stack)
    return AP_MEMORY;
  for (i = 0; i < n; ++i)
    if (m->triangles[i].component == UINT32_MAX) {
      size_t count = 0;
      uint32_t component = m->component_count++;
      stack[count++] = (int32_t)i;
      m->triangles[i].component = component;
      while (count) {
        ApTriangle *t = &m->triangles[stack[--count]];
        unsigned j;
        for (j = 0; j < 3; ++j) {
          int32_t k = t->neighbor[j];
          if (k >= 0 && m->triangles[k].component == UINT32_MAX) {
            m->triangles[k].component = component;
            stack[count++] = k;
          }
        }
      }
    }
  free(stack);
  return AP_OK;
}

ApResult ap_mesh_build(const ApMap *map, ApMesh *out) {
  ApMesh m = {0};
  ApResult result;
  if (!map || !out || !map->bits || !map->cell_size || !map->width ||
      !map->height || ap_map_cells(map) > AP_MAX_CELLS)
    return AP_INVALID;
  m.width = map->width;
  m.height = map->height;
  m.extent_x = map->pixel_width / (double)map->cell_size;
  m.extent_y = map->pixel_height / (double)map->cell_size;
  if (m.extent_x <= m.width - 1 || m.extent_x > m.width ||
      m.extent_y <= m.height - 1 || m.extent_y > m.height)
    return AP_INVALID;
  if (!build_rects(map, &m)) {
    ap_mesh_free(&m);
    return AP_MEMORY;
  }
  result = triangulate(&m);
  if (result == AP_OK)
    result = connect_mesh(&m);
  if (result != AP_OK) {
    ap_mesh_free(&m);
    return result;
  }
  ap_mesh_free(out);
  *out = m;
  return AP_OK;
}

static ApPoint point(ApVertex v) { return (ApPoint){v.x * 0.5, v.y * 0.5}; }
static double area(ApPoint a, ApPoint b, ApPoint c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
static int same(ApPoint a, ApPoint b) {
  return fabs(a.x - b.x) < 1e-10 && fabs(a.y - b.y) < 1e-10;
}
static double distance(ApPoint a, ApPoint b) {
  return hypot(a.x - b.x, a.y - b.y);
}
static ApPoint centroid(const ApMesh *m, int32_t i) {
  const ApTriangle *t = &m->triangles[i];
  return (ApPoint){(t->v[0].x + t->v[1].x + t->v[2].x) / 6.0,
                   (t->v[0].y + t->v[1].y + t->v[2].y) / 6.0};
}

int32_t ap_mesh_locate(const ApMesh *m, ApPoint p) {
  uint32_t y, lo, hi, i;
  const ApRect *r;
  if (!m || !m->rows || !isfinite(p.x) || !isfinite(p.y) || p.x < 0 ||
      p.y < 0 || p.x >= m->extent_x || p.y >= m->extent_y)
    return -1;
  y = (uint32_t)p.y;
  lo = m->rows[y];
  hi = m->rows[y + 1];
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (m->spans[mid].x1 <= p.x)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == m->rows[y + 1] || m->spans[lo].x0 > p.x)
    return -1;
  r = &m->rects[m->spans[lo].rect];
  for (i = r->first; i < r->first + r->count; ++i) {
    const ApTriangle *t = &m->triangles[i];
    if (area(point(t->v[0]), point(t->v[1]), p) >= -1e-9 &&
        area(point(t->v[1]), point(t->v[2]), p) >= -1e-9 &&
        area(point(t->v[2]), point(t->v[0]), p) >= -1e-9)
      return (int32_t)i;
  }
  return -1;
}

void ap_search_free(ApSearch *s) {
  if (!s)
    return;
  free(s->cost);
  free(s->parent);
  free(s->position);
  free(s->heap);
  free(s->stamp);
  memset(s, 0, sizeof(*s));
}
void ap_path_free(ApPath *p) {
  if (p) {
    free(p->points);
    memset(p, 0, sizeof(*p));
  }
}

static int prepare_search(ApSearch *s, size_t n) {
  if (s->capacity < n) {
    ApSearch fresh = {0};
    if (n > SIZE_MAX / sizeof(double))
      return 0;
    fresh.cost = (double *)malloc(n * sizeof(double));
    fresh.parent = (int32_t *)malloc(n * sizeof(int32_t));
    fresh.position = (int32_t *)malloc(n * sizeof(int32_t));
    fresh.heap = (int32_t *)malloc(n * sizeof(int32_t));
    fresh.stamp = (uint32_t *)calloc(n, sizeof(uint32_t));
    if (!fresh.cost || !fresh.parent || !fresh.position || !fresh.heap ||
        !fresh.stamp) {
      ap_search_free(&fresh);
      return 0;
    }
    fresh.capacity = n;
    ap_search_free(s);
    *s = fresh;
  }
  if (++s->generation == 0) {
    memset(s->stamp, 0, s->capacity * sizeof(*s->stamp));
    s->generation = 1;
  }
  s->heap_count = 0;
  return 1;
}

static double score(const ApMesh *m, const ApSearch *s, int32_t i,
                    ApPoint goal) {
  return s->cost[i] + distance(centroid(m, i), goal);
}
static void heap_up(const ApMesh *m, ApSearch *s, size_t pos, ApPoint goal) {
  int32_t node = s->heap[pos];
  double value = score(m, s, node, goal);
  while (pos) {
    size_t parent = (pos - 1) / 2;
    if (score(m, s, s->heap[parent], goal) <= value)
      break;
    s->heap[pos] = s->heap[parent];
    s->position[s->heap[pos]] = (int32_t)pos;
    pos = parent;
  }
  s->heap[pos] = node;
  s->position[node] = (int32_t)pos;
}
static int32_t heap_pop(const ApMesh *m, ApSearch *s, ApPoint goal) {
  int32_t root = s->heap[0], node = s->heap[--s->heap_count];
  size_t pos = 0;
  if (s->heap_count) {
    while (pos * 2 + 1 < s->heap_count) {
      size_t child = pos * 2 + 1;
      if (child + 1 < s->heap_count && score(m, s, s->heap[child + 1], goal) <
                                           score(m, s, s->heap[child], goal))
        ++child;
      if (score(m, s, node, goal) <= score(m, s, s->heap[child], goal))
        break;
      s->heap[pos] = s->heap[child];
      s->position[s->heap[pos]] = (int32_t)pos;
      pos = child;
    }
    s->heap[pos] = node;
    s->position[node] = (int32_t)pos;
  }
  s->position[root] = -1;
  return root;
}

static void path_append(ApPath *p, ApPoint v) {
  if (p->count && same(p->points[p->count - 1], v))
    return;
  /* A portal apex may lie exactly on the final segment: do not expose an
     unnecessary waypoint (including at a rectangle's fan center). */
  while (p->count >= 2) {
    ApPoint a = p->points[p->count - 2], b = p->points[p->count - 1];
    if (fabs(area(a, b, v)) > 1e-10 ||
        (b.x - a.x) * (v.x - b.x) + (b.y - a.y) * (v.y - b.y) < 0)
      break;
    --p->count;
  }
  p->points[p->count++] = v;
}

static void funnel(const Portal *portals, size_t n, ApPath *path) {
  ApPoint apex = portals[0].left, left = apex, right = apex;
  size_t apex_index = 0, left_index = 0, right_index = 0, i;
  path_append(path, apex);
  for (i = 1; i < n; ++i) {
    ApPoint new_left = portals[i].left, new_right = portals[i].right;
    if (area(apex, right, new_right) >= 0) {
      if (same(apex, right) || area(apex, left, new_right) < 0) {
        right = new_right;
        right_index = i;
      } else {
        path_append(path, left);
        apex = left;
        apex_index = left_index;
        left = right = apex;
        left_index = right_index = apex_index;
        i = apex_index;
        continue;
      }
    }
    if (area(apex, left, new_left) <= 0) {
      if (same(apex, left) || area(apex, right, new_left) > 0) {
        left = new_left;
        left_index = i;
      } else {
        path_append(path, right);
        apex = right;
        apex_index = right_index;
        left = right = apex;
        left_index = right_index = apex_index;
        i = apex_index;
      }
    }
  }
  path_append(path, portals[n - 1].left);
}

static size_t triangle_rect(const ApMesh *m, int32_t triangle) {
  size_t lo = 0, hi = m->rect_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (m->rects[mid].first + m->rects[mid].count <= (uint32_t)triangle)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

/* Widen an A* corridor to its convex rectangles. A fan edge is not an obstacle:
   retaining it in the funnel can force a detour through a rectangle center.
   The search is finished, so position can now index rectangles (R <= T). */
static size_t rectangle_corridor(const ApMesh *m, ApSearch *s,
                                 int32_t *corridor, size_t count) {
  size_t i, used = 0;
  for (i = 0; i < count; ++i) {
    corridor[i] = (int32_t)triangle_rect(m, corridor[i]);
    s->position[corridor[i]] = -1;
  }
  for (i = 0; i < count; ++i) {
    int32_t rect = corridor[i], previous = s->position[rect];
    if (previous >= 0) {
      /* Re-entry into a convex rectangle needs no excursion outside it. */
      while (used > (size_t)previous + 1)
        s->position[corridor[--used]] = -1;
    } else {
      s->position[rect] = (int32_t)used;
      corridor[used++] = rect;
    }
  }
  return used;
}

static int rectangle_portal(const ApMesh *m, int32_t from, int32_t to,
                            Portal *p) {
  const ApRect *a = &m->rects[from], *b = &m->rects[to];
  double low, high, line;
  if (a->y1 == b->y0 || a->y0 == b->y1) {
    low = fmax(a->x0, b->x0);
    high = fmin(fmin(a->x1, b->x1), m->extent_x);
    line = a->y1 == b->y0 ? b->y0 : a->y0;
    if (high <= low)
      return 0;
    if (a->y1 == b->y0)
      *p = (Portal){{low, line}, {high, line}}; /* Up: west is left. */
    else
      *p = (Portal){{high, line}, {low, line}};
  } else if (a->x1 == b->x0 || a->x0 == b->x1) {
    low = fmax(a->y0, b->y0);
    high = fmin(fmin(a->y1, b->y1), m->extent_y);
    line = a->x1 == b->x0 ? b->x0 : a->x0;
    if (high <= low)
      return 0;
    if (a->x1 == b->x0)
      *p = (Portal){{line, high}, {line, low}}; /* Right: north is left. */
    else
      *p = (Portal){{line, low}, {line, high}};
  } else
    return 0;
  return 1;
}

ApResult ap_path_find(const ApMesh *m, ApSearch *s, ApPoint start, ApPoint end,
                      ApPath *out) {
  int32_t from, to, current, *corridor = NULL;
  ApPoint goal;
  Portal *portals = NULL;
  ApPath path = {0};
  size_t count = 0, i;
  if (!m || !s || !out)
    return AP_INVALID;
  from = ap_mesh_locate(m, start);
  to = ap_mesh_locate(m, end);
  if (from < 0 || to < 0)
    return AP_INVALID;
  if (m->triangles[from].component != m->triangles[to].component)
    return AP_NO_PATH;
  /* A merged rectangle is convex. A direct segment is exact and avoids a
     centroid-selected fan corridor unnecessarily visiting its center. */
  if (triangle_rect(m, from) == triangle_rect(m, to)) {
    path.points = (ApPoint *)malloc(2 * sizeof(*path.points));
    if (!path.points)
      return AP_MEMORY;
    path_append(&path, start);
    path_append(&path, end);
    ap_path_free(out);
    *out = path;
    return AP_OK;
  }
  if (!prepare_search(s, m->triangle_count))
    return AP_MEMORY;
  goal = centroid(m, to);
  s->stamp[from] = s->generation;
  s->cost[from] = 0;
  s->parent[from] = -1;
  s->heap[s->heap_count++] = from;
  s->position[from] = 0;
  while (s->heap_count) {
    unsigned side;
    current = heap_pop(m, s, goal);
    if (current == to)
      break;
    for (side = 0; side < 3; ++side) {
      int32_t next = m->triangles[current].neighbor[side];
      double cost;
      if (next < 0)
        continue;
      cost =
          s->cost[current] + distance(centroid(m, current), centroid(m, next));
      if (s->stamp[next] != s->generation) {
        s->stamp[next] = s->generation;
        s->cost[next] = cost;
        s->parent[next] = current;
        s->position[next] = (int32_t)s->heap_count;
        s->heap[s->heap_count++] = next;
        heap_up(m, s, (size_t)s->position[next], goal);
      } else if (s->position[next] >= 0 && cost < s->cost[next]) {
        s->cost[next] = cost;
        s->parent[next] = current;
        heap_up(m, s, (size_t)s->position[next], goal);
      }
    }
  }
  if (s->stamp[to] != s->generation)
    return AP_NO_PATH;
  for (current = to; current >= 0; current = s->parent[current])
    ++count;
  if (count > SIZE_MAX / sizeof(Portal) - 1)
    return AP_MEMORY;
  corridor = (int32_t *)malloc(count * sizeof(*corridor));
  portals = (Portal *)malloc((count + 1) * sizeof(*portals));
  path.points = (ApPoint *)malloc((count + 1) * sizeof(*path.points));
  if (!corridor || !portals || !path.points) {
    free(corridor);
    free(portals);
    ap_path_free(&path);
    return AP_MEMORY;
  }
  current = to;
  for (i = count; i-- > 0;) {
    corridor[i] = current;
    current = s->parent[current];
  }
  count = rectangle_corridor(m, s, corridor, count);
  portals[0] = (Portal){start, start};
  portals[count] = (Portal){end, end};
  for (i = 0; i + 1 < count; ++i) {
    if (!rectangle_portal(m, corridor[i], corridor[i + 1], &portals[i + 1])) {
      free(corridor);
      free(portals);
      ap_path_free(&path);
      return AP_INVALID;
    }
  }
  funnel(portals, count + 1, &path);
  free(corridor);
  free(portals);
  ap_path_free(out);
  *out = path;
  return AP_OK;
}
