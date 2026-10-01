#include "app.h"

#include <commctrl.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <wchar.h>
#include <windowsx.h>

App app;

static int clamp(int value, int lo, int hi) {
  return value < lo ? lo : value > hi ? hi : value;
}
static int max_int(int a, int b) { return a > b ? a : b; }
static int64_t clamp_offset(int64_t value, int64_t hi) {
  return value < 0 ? 0 : value > hi ? hi : value;
}
static int64_t scroll_limit(int64_t extent, int page) {
  return extent > page ? extent - page : 0;
}
/* Keep offscreen GDI coordinates inside its signed 27-bit device range. */
static int device_coord(int64_t value) {
  const int bound = 0x03ffffff;
  return value < -bound ? -bound : value > bound ? bound : (int)value;
}
static RECT client_rect(void) {
  RECT r;
  GetClientRect(app.window, &r);
  return r;
}
static double extent_x(void) {
  return app.map.pixel_width / (double)app.map.cell_size;
}
static double extent_y(void) {
  return app.map.pixel_height / (double)app.map.cell_size;
}
static int64_t display_width(void) {
  return (int64_t)fmax(1, round(extent_x() * app.cell_x));
}
static int64_t display_height(void) {
  return (int64_t)fmax(1, round(extent_y() * app.cell_y));
}
static double edge_x(int x) { return fmin(x, extent_x()); }
static double edge_y(int y) { return fmin(y, extent_y()); }

static ApPoint world_point(int x, int y) {
  RECT r = client_rect();
  return (ApPoint){(app.scroll_x + x) / app.cell_x,
                   (app.scroll_y + r.bottom - y) / app.cell_y};
}

static POINT screen_point(ApPoint p, const RECT *r) {
  POINT q = {
      device_coord(llround(p.x * app.cell_x - app.scroll_x)),
      device_coord(llround(r->bottom - p.y * app.cell_y + app.scroll_y))};
  return q;
}

void app_refresh(void) {
  wchar_t title[1024];
  const wchar_t *name = app.filename[0] ? app.filename : L"Untitled";
  const wchar_t *back = wcsrchr(name, L'\\'), *slash = wcsrchr(name, L'/');
  if (back)
    name = back + 1;
  if (slash && slash >= name)
    name = slash + 1;
  _snwprintf(title, 1024, L"%.*ls%ls - APath | %lu x %lu", 500, name,
             ap_history_dirty(&app.history) ? L" *" : L"",
             (unsigned long)app.map.pixel_width,
             (unsigned long)app.map.pixel_height);
  SetWindowTextW(app.window, title);
  EnableMenuItem(GetMenu(app.window), IDM_UNDO,
                 MF_BYCOMMAND | (app.history.cursor ? MF_ENABLED : MF_GRAYED));
  EnableMenuItem(
      GetMenu(app.window), IDM_REDO,
      MF_BYCOMMAND |
          (app.history.cursor < app.history.count ? MF_ENABLED : MF_GRAYED));
  CheckMenuItem(GetMenu(app.window), IDM_MESH,
                MF_BYCOMMAND | (app.mesh_mode ? MF_CHECKED : MF_UNCHECKED));
  InvalidateRect(app.window, NULL, FALSE);
}

static void set_scrollbar(int bar, int64_t extent, int page, int64_t offset) {
  SCROLLINFO si;
  int64_t limit = scroll_limit(extent, page);
  int range = extent > INT_MAX ? INT_MAX : (int)extent;
  int maxpos;
  ZeroMemory(&si, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMax = range - 1;
  si.nPage =
      extent > INT_MAX ? (UINT)((int64_t)page * range / extent) : (UINT)page;
  if (page && !si.nPage)
    si.nPage = 1;
  maxpos = max_int(0, range - (int)si.nPage);
  si.nPos = limit ? (int)llround((double)offset / limit * maxpos) : 0;
  SetScrollInfo(app.window, bar, &si, TRUE);
}

void app_scrollbars(void) {
  RECT r;
  LONG_PTR style;
  int full_w, full_h, hbar, vbar, pass;
  int64_t w, h;
  double base, old_x = app.cell_x, old_y = app.cell_y;
  ApPoint center = {0, 0};
  if (app.scroll_busy || !app.window)
    return;
  app.scroll_busy = 1;
  r = client_rect();
  style = GetWindowLongPtrW(app.window, GWL_STYLE);
  full_w =
      r.right + ((style & WS_VSCROLL) ? GetSystemMetrics(SM_CXVSCROLL) : 0);
  full_h =
      r.bottom + ((style & WS_HSCROLL) ? GetSystemMetrics(SM_CYHSCROLL) : 0);
  if (old_x > 0 && old_y > 0)
    center = (ApPoint){(app.scroll_x + app.view_width / 2) / old_x,
                       (app.scroll_y + app.view_height - app.view_height / 2) /
                           old_y};
  base = fmin(max_int(1, full_w) / extent_x(), max_int(1, full_h) / extent_y());
  /* Even a partial cell much larger than the map must remain zoomable. */
  app.zoom = fmax(1, fmin(app.zoom, fmax(256, 65535 / base)));
  app.cell_x = app.cell_y = base * app.zoom;
  w = display_width();
  h = display_height();
  hbar = vbar = 0;
  for (pass = 0; pass < 3; ++pass) {
    hbar = w > full_w - (vbar ? GetSystemMetrics(SM_CXVSCROLL) : 0);
    vbar = h > full_h - (hbar ? GetSystemMetrics(SM_CYHSCROLL) : 0);
  }
  ShowScrollBar(app.window, SB_HORZ, hbar);
  ShowScrollBar(app.window, SB_VERT, vbar);
  r = client_rect();
  if (old_x > 0 && old_y > 0 &&
      (old_x != app.cell_x || old_y != app.cell_y ||
       app.view_width != r.right || app.view_height != r.bottom)) {
    app.scroll_x = llround(center.x * app.cell_x - r.right / 2);
    app.scroll_y = llround(center.y * app.cell_y - (r.bottom - r.bottom / 2));
  }
  app.view_width = r.right;
  app.view_height = r.bottom;
  app.scroll_x = clamp_offset(app.scroll_x, scroll_limit(w, r.right));
  app.scroll_y = clamp_offset(app.scroll_y, scroll_limit(h, r.bottom));
  set_scrollbar(SB_HORZ, w, r.right, app.scroll_x);
  set_scrollbar(SB_VERT, h, r.bottom, scroll_limit(h, r.bottom) - app.scroll_y);
  app.scroll_busy = 0;
}

static void resize_for_map(void) {
  MONITORINFO monitor;
  RECT old, work, frame = {0, 0, 0, 0};
  DWORD style, exstyle;
  int frame_w, frame_h, work_w, work_h, max_w, max_h, min_w, min_h;
  int width, height, x, y;
  double scale, minimum, maximum;
  app.scroll_busy = 1;
  monitor.cbSize = sizeof(monitor);
  if (GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST),
                      &monitor))
    work = monitor.rcWork;
  else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    work = (RECT){0, 0, GetSystemMetrics(SM_CXSCREEN),
                  GetSystemMetrics(SM_CYSCREEN)};
  if (IsZoomed(app.window) || IsIconic(app.window))
    ShowWindow(app.window, SW_RESTORE);
  ShowScrollBar(app.window, SB_BOTH, FALSE);
  GetWindowRect(app.window, &old);
  style = (DWORD)GetWindowLongPtrW(app.window, GWL_STYLE) &
          ~(WS_HSCROLL | WS_VSCROLL);
  exstyle = (DWORD)GetWindowLongPtrW(app.window, GWL_EXSTYLE);
  AdjustWindowRectEx(&frame, style, GetMenu(app.window) != NULL, exstyle);
  frame_w = frame.right - frame.left;
  frame_h = frame.bottom - frame.top;
  work_w = work.right - work.left;
  work_h = work.bottom - work.top;
  max_w = max_int(1, work_w - frame_w);
  max_h = max_int(1, work_h - frame_h);
  min_w = clamp(420 - frame_w, 1, max_w);
  min_h = clamp(300 - frame_h, 1, max_h);
  minimum = fmax(min_w / (double)app.map.pixel_width,
                 min_h / (double)app.map.pixel_height);
  maximum = fmin(max_w / (double)app.map.pixel_width,
                 max_h / (double)app.map.pixel_height);
  scale = fmin(fmax(1, minimum), maximum);
  /* A minimum-sized window may leave a margin for very thin maps. */
  width = clamp((int)ceil(app.map.pixel_width * scale - 1e-9), min_w, max_w) +
          frame_w;
  height = clamp((int)ceil(app.map.pixel_height * scale - 1e-9), min_h, max_h) +
           frame_h;
  x = clamp(old.left + (old.right - old.left - width) / 2, work.left,
            work.right - width);
  y = clamp(old.top + (old.bottom - old.top - height) / 2, work.top,
            work.bottom - height);
  SetWindowPos(app.window, NULL, x, y, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  app.scroll_busy = 0;
}

void app_reset_view(void) {
  app.cell_x = app.cell_y = 0;
  app.zoom = 1;
  app.scroll_x = app.scroll_y = app.wheel_remainder = 0;
  resize_for_map();
  app_scrollbars();
  app_refresh();
}

void app_leave_mesh(void) {
  ap_mesh_free(&app.mesh);
  ap_search_free(&app.search);
  ap_path_free(&app.route);
  app.mesh_mode = app.have_start = app.have_end = 0;
  app.route_result = AP_OK;
}

static void toggle_mesh(void) {
  ApResult result;
  app_end_gesture(1);
  if (app.mesh_mode)
    app_leave_mesh();
  else {
    app_leave_mesh();
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    result = ap_mesh_build(&app.map, &app.mesh);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    if (result != AP_OK) {
      app_error(result);
      return;
    }
    app.mesh_mode = 1;
  }
  app_refresh();
}

static void set_endpoint(int button, int x, int y) {
  ApPoint p = world_point(x, y);
  ApPoint old = button == 1 ? app.start : app.end;
  int had = button == 1 ? app.have_start : app.have_end;
  if (ap_mesh_locate(&app.mesh, p) < 0)
    return;
  if (had && old.x == p.x && old.y == p.y)
    return;
  if (button == 1) {
    app.start = p;
    app.have_start = 1;
  } else {
    app.end = p;
    app.have_end = 1;
  }
  ap_path_free(&app.route);
  if (app.have_start && app.have_end) {
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    app.route_result =
        ap_path_find(&app.mesh, &app.search, app.start, app.end, &app.route);
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
    if (app.route_result != AP_OK && app.route_result != AP_NO_PATH)
      app_error(app.route_result);
  }
  app_refresh();
}

/* Cell bounds are half-open, but include both endpoint cells. */
static int selection_bounds(RECT *cells) {
  if (fmax(app.down_world.x, app.last_world.x) < 0 ||
      fmax(app.down_world.y, app.last_world.y) < 0 ||
      fmin(app.down_world.x, app.last_world.x) >= extent_x() ||
      fmin(app.down_world.y, app.last_world.y) >= extent_y())
    return 0;
  int ax = (int)floor(app.down_world.x), ay = (int)floor(app.down_world.y);
  int bx = (int)floor(app.last_world.x), by = (int)floor(app.last_world.y);
  cells->left = clamp(ax < bx ? ax : bx, 0, app.map.width);
  cells->bottom = clamp((ay > by ? ay : by) + 1, 0, app.map.height);
  cells->right = clamp((ax > bx ? ax : bx) + 1, 0, app.map.width);
  cells->top = clamp(ay < by ? ay : by, 0, app.map.height);
  return cells->left < cells->right && cells->top < cells->bottom;
}

void app_end_gesture(int cancel) {
  ApResult result = AP_OK;
  int button = app.gesture, dragging = app.dragging;
  if (!button)
    return;
  app.gesture = app.dragging = 0;
  KillTimer(app.window, 1);
  if (GetCapture() == app.window)
    ReleaseCapture();
  if (!cancel) {
    ap_history_begin(&app.history);
    if (dragging) {
      RECT cells;
      if (selection_bounds(&cells)) {
        int x, y;
        for (y = cells.top; y < cells.bottom && result == AP_OK; ++y)
          for (x = cells.left; x < cells.right && result == AP_OK; ++x)
            result = ap_history_set(&app.history, &app.map, x, y, button == 1);
      }
    } else if (app.down_world.x >= 0 && app.down_world.y >= 0 &&
               app.down_world.x < extent_x() && app.down_world.y < extent_y()) {
      int x = (int)floor(app.down_world.x), y = (int)floor(app.down_world.y);
      result = ap_history_set(&app.history, &app.map, x, y,
                              button == 1 ? !ap_map_get(&app.map, x, y) : 0);
    }
    if (result != AP_OK)
      ap_history_cancel(&app.history, &app.map);
    else
      result = ap_history_commit(&app.history, &app.map);
  }
  app_refresh();
  if (result != AP_OK)
    app_error(result);
}

static void begin_gesture(int button, int x, int y) {
  if (app.gesture)
    app_end_gesture(1);
  SetFocus(app.window);
  if (app.mesh_mode) {
    set_endpoint(button, x, y);
    return;
  }
  app.gesture = button;
  app.dragging = 0;
  app.down_screen = (POINT){x, y};
  app.down_world = app.last_world = world_point(x, y);
  SetCapture(app.window);
  SetTimer(app.window, 1, 30, NULL);
}

static void move_gesture(int x, int y) {
  if (!app.gesture)
    return;
  if (!app.dragging &&
      (abs(x - app.down_screen.x) >= GetSystemMetrics(SM_CXDRAG) ||
       abs(y - app.down_screen.y) >= GetSystemMetrics(SM_CYDRAG)))
    app.dragging = 1;
  if (app.dragging) {
    app.last_world = world_point(x, y);
    InvalidateRect(app.window, NULL, FALSE);
  }
}

static void end_button(int button, int x, int y) {
  if (app.gesture != button)
    return;
  move_gesture(x, y);
  app_end_gesture(0);
}

static void auto_scroll(void) {
  RECT r;
  int64_t old_x, old_y;
  POINT p;
  if (!app.gesture || !app.dragging)
    return;
  GetCursorPos(&p);
  ScreenToClient(app.window, &p);
  r = client_rect();
  old_x = app.scroll_x;
  old_y = app.scroll_y;
  if (p.x < 16)
    app.scroll_x -= 16;
  else if (p.x >= r.right - 16)
    app.scroll_x += 16;
  if (p.y < 16)
    app.scroll_y += 16;
  else if (p.y >= r.bottom - 16)
    app.scroll_y -= 16;
  app_scrollbars();
  if (old_x != app.scroll_x || old_y != app.scroll_y)
    move_gesture(p.x, p.y);
}

static void scroll_window(int bar, unsigned command) {
  SCROLLINFO si;
  RECT r = client_rect();
  int page = bar == SB_HORZ ? r.right : r.bottom;
  int64_t extent = bar == SB_HORZ ? display_width() : display_height();
  int64_t maxpos = scroll_limit(extent, page);
  int64_t pos = bar == SB_HORZ ? app.scroll_x : maxpos - app.scroll_y;
  double cell = bar == SB_HORZ ? app.cell_x : app.cell_y;
  app_end_gesture(1);
  ZeroMemory(&si, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_ALL;
  GetScrollInfo(app.window, bar, &si);
  switch (command) {
  case SB_LINEUP:
    pos -= (int64_t)fmax(16, cell);
    break;
  case SB_LINEDOWN:
    pos += (int64_t)fmax(16, cell);
    break;
  case SB_PAGEUP:
    pos -= page;
    break;
  case SB_PAGEDOWN:
    pos += page;
    break;
  case SB_THUMBPOSITION:
  case SB_THUMBTRACK: {
    int thumb_max = max_int(0, si.nMax - (int)si.nPage + 1);
    pos = thumb_max ? llround((double)si.nTrackPos / thumb_max * maxpos) : 0;
    break;
  }
  case SB_TOP:
    pos = 0;
    break;
  case SB_BOTTOM:
    pos = maxpos;
    break;
  default:
    return;
  }
  pos = clamp_offset(pos, maxpos);
  if (bar == SB_HORZ)
    app.scroll_x = pos;
  else
    app.scroll_y = maxpos - pos;
  app_scrollbars();
  InvalidateRect(app.window, NULL, FALSE);
}

static void set_zoom(double factor, POINT anchor_pixel, int center) {
  ApPoint anchor = world_point(anchor_pixel.x, anchor_pixel.y);
  RECT r;
  app.zoom = factor;
  app_scrollbars();
  r = client_rect();
  if (center)
    anchor_pixel = (POINT){r.right / 2, r.bottom / 2};
  app.scroll_x = llround(anchor.x * app.cell_x - anchor_pixel.x);
  app.scroll_y = llround(anchor.y * app.cell_y - (r.bottom - anchor_pixel.y));
  app_scrollbars();
  app_refresh();
}

static void fill_client(void) {
  RECT r = client_rect();
  app.wheel_remainder = 0;
  set_zoom(1, (POINT){r.right / 2, r.bottom / 2}, 1);
}

static void zoom(int delta, POINT cursor) {
  double factor = app.zoom;
  double maximum = fmax(256, 65535 / (fmin(app.cell_x, app.cell_y) / app.zoom));
  app_end_gesture(1);
  ScreenToClient(app.window, &cursor);
  app.wheel_remainder += delta;
  while (app.wheel_remainder >= WHEEL_DELTA) {
    factor = fmin(factor * 1.2, maximum);
    app.wheel_remainder -= WHEEL_DELTA;
  }
  while (app.wheel_remainder <= -WHEEL_DELTA) {
    factor = fmax(1, factor / 1.2);
    app.wheel_remainder += WHEEL_DELTA;
  }
  set_zoom(factor, cursor, 0);
}

static void line(HDC dc, int x0, int y0, int x1, int y1) {
  MoveToEx(dc, x0, y0, NULL);
  LineTo(dc, x1, y1);
}

static int clip_axis(double p, double q, double *lo, double *hi) {
  double t;
  if (p == 0)
    return q >= 0;
  t = q / p;
  if (p < 0) {
    if (t > *hi)
      return 0;
    if (t > *lo)
      *lo = t;
  } else {
    if (t < *lo)
      return 0;
    if (t < *hi)
      *hi = t;
  }
  return 1;
}

/* Clip before conversion to GDI coordinates, even for maps over INT_MAX pixels.
 */
static void world_line(HDC dc, ApPoint a, ApPoint b, const RECT *client) {
  double x = a.x * app.cell_x - app.scroll_x,
         y = client->bottom - a.y * app.cell_y + app.scroll_y;
  double dx = (b.x - a.x) * app.cell_x, dy = (a.y - b.y) * app.cell_y;
  double lo = 0, hi = 1;
  if (!clip_axis(-dx, x + 4, &lo, &hi) ||
      !clip_axis(dx, client->right + 4 - x, &lo, &hi) ||
      !clip_axis(-dy, y + 4, &lo, &hi) ||
      !clip_axis(dy, client->bottom + 4 - y, &lo, &hi))
    return;
  line(dc, (int)lround(x + lo * dx), (int)lround(y + lo * dy),
       (int)lround(x + hi * dx), (int)lround(y + hi * dy));
}

static void render_selection(HDC dc, const RECT *client) {
  RECT cells;
  HPEN pen;
  HGDIOBJ old_pen, old_brush;
  POINT a, b;
  if (!app.gesture || !app.dragging || !selection_bounds(&cells))
    return;
  a = screen_point((ApPoint){edge_x(cells.left), edge_y(cells.bottom)}, client);
  b = screen_point((ApPoint){edge_x(cells.right), edge_y(cells.top)}, client);
  pen = CreatePen(PS_DOT, 1,
                  app.gesture == 1 ? RGB(0, 110, 45) : RGB(200, 45, 45));
  old_pen = SelectObject(dc, pen);
  old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  SetBkMode(dc, TRANSPARENT);
  Rectangle(dc, a.x, a.y, b.x, b.y);
  SetBkMode(dc, OPAQUE);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(pen);
}

static void render(HDC dc, const RECT *client) {
  int x0 = clamp(app.scroll_x / app.cell_x, 0, app.map.width);
  int y0 = clamp(app.scroll_y / app.cell_y, 0, app.map.height);
  int x1 =
      clamp((app.scroll_x + client->right) / app.cell_x + 1, 0, app.map.width);
  int y1 = clamp((app.scroll_y + client->bottom) / app.cell_y + 1, 0,
                 app.map.height);
  int x, y, step_x = max_int(1, (int)ceil(4 / app.cell_x));
  int step_y = max_int(1, (int)ceil(4 / app.cell_y));
  HBRUSH green = CreateSolidBrush(RGB(72, 183, 93));
  HPEN grid = CreatePen(PS_SOLID, 1, RGB(215, 222, 218));
  HPEN edge = CreatePen(PS_SOLID, 1, RGB(0, 145, 55));
  HPEN route = CreatePen(PS_SOLID, 3, RGB(35, 91, 214));
  HPEN red = CreatePen(PS_SOLID, 1, RGB(180, 20, 25));
  HBRUSH dot = CreateSolidBrush(RGB(230, 40, 45));
  HGDIOBJ old_pen = SelectObject(dc, grid), old_brush = SelectObject(dc, dot);
  FillRect(dc, client, (HBRUSH)GetStockObject(WHITE_BRUSH));
  if (!app.mesh_mode) {
    for (y = y0; y < y1; ++y)
      for (x = x0; x < x1;) {
        int begin;
        RECT r;
        if (!ap_map_get(&app.map, x, y)) {
          ++x;
          continue;
        }
        begin = x;
        while (x < x1 && ap_map_get(&app.map, x, y))
          ++x;
        POINT a = screen_point((ApPoint){edge_x(begin), edge_y(y + 1)}, client);
        POINT b = screen_point((ApPoint){edge_x(x), edge_y(y)}, client);
        r = (RECT){a.x, a.y, b.x, b.y};
        FillRect(dc, &r, green);
      }
  }
  for (x = x0 / step_x * step_x; x <= x1; x += step_x)
    world_line(dc, (ApPoint){edge_x(x), edge_y(y0)},
               (ApPoint){edge_x(x), edge_y(y1)}, client);
  for (y = y0 / step_y * step_y; y <= y1; y += step_y)
    world_line(dc, (ApPoint){edge_x(x0), edge_y(y)},
               (ApPoint){edge_x(x1), edge_y(y)}, client);
  world_line(dc, (ApPoint){extent_x(), 0}, (ApPoint){extent_x(), extent_y()},
             client);
  world_line(dc, (ApPoint){0, extent_y()}, (ApPoint){extent_x(), extent_y()},
             client);
  if (app.mesh_mode) {
    SelectObject(dc, edge);
    for (y = y0; y < y1; ++y) {
      uint32_t s;
      for (s = app.mesh.rows[y]; s < app.mesh.rows[y + 1]; ++s) {
        const ApSpan *span = &app.mesh.spans[s];
        const ApRect *r;
        uint32_t i;
        if (span->x0 >= (uint32_t)x1)
          break;
        if (span->x1 <= (uint32_t)x0)
          continue;
        r = &app.mesh.rects[span->rect];
        if (r->y0 != (uint32_t)y && y != y0)
          continue;
        for (i = r->first; i < r->first + r->count; ++i) {
          const ApTriangle *t = &app.mesh.triangles[i];
          unsigned k;
          for (k = 0; k < 3; ++k) {
            if (t->neighbor[k] >= 0 && t->neighbor[k] < (int32_t)i)
              continue;
            world_line(
                dc, (ApPoint){t->v[k].x * .5, t->v[k].y * .5},
                (ApPoint){t->v[(k + 1) % 3].x * .5, t->v[(k + 1) % 3].y * .5},
                client);
          }
        }
      }
    }
    if (app.route.count) {
      size_t i;
      SelectObject(dc, route);
      for (i = 1; i < app.route.count; ++i) {
        world_line(dc, app.route.points[i - 1], app.route.points[i], client);
      }
    }
    SelectObject(dc, red);
    if (app.have_start) {
      POINT p = screen_point(app.start, client);
      POINT vertices[3] = {
          {p.x, p.y - 7}, {p.x + 7, p.y + 6}, {p.x - 7, p.y + 6}};
      Polygon(dc, vertices, 3);
    }
    if (app.have_end) {
      POINT p = screen_point(app.end, client);
      Rectangle(dc, p.x - 6, p.y - 6, p.x + 7, p.y + 7);
    }
  }
  if (!app.mesh_mode)
    render_selection(dc, client);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_brush);
  DeleteObject(green);
  DeleteObject(grid);
  DeleteObject(edge);
  DeleteObject(route);
  DeleteObject(red);
  DeleteObject(dot);
}

static void paint_window(HWND hwnd) {
  PAINTSTRUCT ps;
  RECT r = client_rect();
  HDC dc = BeginPaint(hwnd, &ps), memory = CreateCompatibleDC(dc);
  HBITMAP bitmap =
      CreateCompatibleBitmap(dc, max_int(1, r.right), max_int(1, r.bottom));
  if (memory && bitmap) {
    HGDIOBJ old = SelectObject(memory, bitmap);
    render(memory, &r);
    BitBlt(dc, 0, 0, r.right, r.bottom, memory, 0, 0, SRCCOPY);
    SelectObject(memory, old);
  } else
    render(dc, &r);
  if (bitmap)
    DeleteObject(bitmap);
  if (memory)
    DeleteDC(memory);
  EndPaint(hwnd, &ps);
}

LRESULT CALLBACK app_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_CREATE:
    app.window = hwnd;
    return 0;
  case WM_SIZE:
    if (!app.scroll_busy && wp != SIZE_MINIMIZED) {
      app_end_gesture(1);
      app_scrollbars();
      app_refresh();
    }
    return 0;
  case WM_GETMINMAXINFO:
    ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){420, 300};
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_PAINT:
    paint_window(hwnd);
    return 0;
  case WM_COMMAND:
    app_end_gesture(1);
    switch (LOWORD(wp)) {
    case IDM_NEW:
      app_new();
      break;
    case IDM_OPEN:
      app_open(NULL);
      break;
    case IDM_SAVE:
      app_save(0);
      break;
    case IDM_SAVE_AS:
      app_save(1);
      break;
    case IDM_EXIT:
      SendMessageW(hwnd, WM_CLOSE, 0, 0);
      break;
    case IDM_UNDO:
      if (app.history.cursor) {
        app_leave_mesh();
        ap_history_undo(&app.history, &app.map);
        app_refresh();
      }
      break;
    case IDM_REDO:
      if (app.history.cursor < app.history.count) {
        app_leave_mesh();
        ap_history_redo(&app.history, &app.map);
        app_refresh();
      }
      break;
    case IDM_MESH:
      toggle_mesh();
      break;
    case IDM_FILL_CLIENT:
      fill_client();
      break;
    case IDM_ABOUT:
      DialogBoxParamW(app.instance, MAKEINTRESOURCEW(IDD_ABOUT), hwnd,
                      app_about_proc, 0);
      break;
    default:
      break;
    }
    return 0;
  case WM_LBUTTONDOWN:
    begin_gesture(1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_RBUTTONDOWN:
    begin_gesture(2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_MOUSEMOVE:
    move_gesture(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_LBUTTONUP:
    end_button(1, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_RBUTTONUP:
    end_button(2, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
    return 0;
  case WM_CAPTURECHANGED:
    if (app.gesture)
      app_end_gesture(1);
    return 0;
  case WM_CANCELMODE:
    app_end_gesture(1);
    return 0;
  case WM_KEYDOWN:
    if (wp == VK_ESCAPE) {
      app_end_gesture(1);
      return 0;
    }
    break;
  case WM_TIMER:
    if (wp == 1)
      auto_scroll();
    return 0;
  case WM_MOUSEWHEEL:
    zoom(GET_WHEEL_DELTA_WPARAM(wp),
         (POINT){GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
    return 0;
  case WM_HSCROLL:
    scroll_window(SB_HORZ, LOWORD(wp));
    return 0;
  case WM_VSCROLL:
    scroll_window(SB_VERT, LOWORD(wp));
    return 0;
  case WM_CLOSE:
    if (app_confirm_save())
      DestroyWindow(hwnd);
    return 0;
  case WM_QUERYENDSESSION:
    return app_confirm_save();
  case WM_DESTROY:
    app_end_gesture(1);
    app_leave_mesh();
    ap_map_free(&app.map);
    ap_history_free(&app.history);
    PostQuitMessage(0);
    return 0;
  default:
    break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

int app_create(HINSTANCE instance, int show) {
  WNDCLASSEXW wc;
  INITCOMMONCONTROLSEX controls = {sizeof(controls),
                                   ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
  ZeroMemory(&app, sizeof(app));
  app.instance = instance;
  app.zoom = 1;
  InitCommonControlsEx(&controls);
  if (ap_map_create(&app.map, 24, 32, 24) != AP_OK)
    return 0;
  ap_history_init(&app.history, AP_HISTORY_BUDGET);
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = app_window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                               GetSystemMetrics(SM_CXICON),
                               GetSystemMetrics(SM_CYICON), LR_SHARED);
  wc.hIconSm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                 IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON), LR_SHARED);
  wc.lpszClassName = L"APath.Editor";
  wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU);
  if (!RegisterClassExW(&wc)) {
    ap_map_free(&app.map);
    return 0;
  }
  app.window = CreateWindowExW(0, wc.lpszClassName, L"APath",
                               WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                               CW_USEDEFAULT, CW_USEDEFAULT, 1050, 760, NULL,
                               NULL, instance, NULL);
  if (!app.window) {
    ap_map_free(&app.map);
    ap_history_free(&app.history);
    return 0;
  }
  app_reset_view();
  ShowWindow(app.window, show);
  UpdateWindow(app.window);
  return 1;
}
