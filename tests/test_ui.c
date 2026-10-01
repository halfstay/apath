#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>

static unsigned checks;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

enum {
  ACTION_NONE,
  ACTION_NEW,
  ACTION_NEW_CANCEL,
  ACTION_ABOUT,
  ACTION_SAVE,
  ACTION_OPEN,
  ACTION_DISCARD,
  ACTION_CANCEL,
  ACTION_ASSOC_YES,
  ACTION_ASSOC_NO,
  ACTION_ERROR,
  ACTION_UNEXPECTED
};
static int action, dialog_seen;
static unsigned new_cell = 20, new_width = 805, new_height = 603;
static DWORD dialog_started;
static UINT_PTR dialog_timer;
static wchar_t test_path[AP_PATH_CAP];

static BOOL CALLBACK handle_dialog(HWND window, LPARAM unused) {
  wchar_t title[256], class_name[64];
  int current = action;
  (void)unused;
  GetClassNameW(window, class_name, 64);
  GetWindowTextW(window, title, 256);
  if (wcscmp(class_name, L"#32770") || !IsWindowVisible(window))
    return TRUE;
  if (((current == ACTION_NEW || current == ACTION_NEW_CANCEL) &&
       wcscmp(title, L"New Map")) ||
      (current == ACTION_ABOUT && wcscmp(title, L"About APath")) ||
      (current == ACTION_SAVE && wcscmp(title, L"Save As")) ||
      (current == ACTION_OPEN && wcscmp(title, L"Open")) ||
      ((current == ACTION_DISCARD || current == ACTION_CANCEL ||
        current == ACTION_ERROR) &&
       wcscmp(title, L"APath")) ||
      ((current == ACTION_ASSOC_YES || current == ACTION_ASSOC_NO) &&
       wcscmp(title, L"APath File Association")))
    return TRUE;
  action = ACTION_NONE;
  dialog_seen = 1;
  KillTimer(NULL, dialog_timer);
  switch (current) {
  case ACTION_UNEXPECTED:
    CHECK(0);
    break;
  case ACTION_NEW:
    CHECK(!IsWindowEnabled(app.window));
    SetDlgItemInt(window, IDC_CELL, new_cell, FALSE);
    SetDlgItemInt(window, IDC_WIDTH, new_width, FALSE);
    SetDlgItemInt(window, IDC_HEIGHT, new_height, FALSE);
    PostMessageW(window, WM_COMMAND, IDOK, 0);
    break;
  case ACTION_NEW_CANCEL:
    PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
    break;
  case ACTION_ABOUT: {
    LITEM item;
    RECT icon_rect, text_rect;
    HWND icon = GetDlgItem(window, IDC_ABOUT_ICON),
         text = GetDlgItem(window, IDC_ABOUT_TEXT);
    CHECK(icon && text && IsWindowVisible(icon));
    CHECK(SendMessageW(icon, STM_GETICON, 0, 0) != 0);
    CHECK(GetWindowRect(icon, &icon_rect) && GetWindowRect(text, &text_rect));
    CHECK(icon_rect.right <= text_rect.left && icon_rect.top == text_rect.top);
    ZeroMemory(&item, sizeof(item));
    item.mask = LIF_ITEMINDEX | LIF_URL;
    item.iLink = 0;
    CHECK(GetDlgItem(window, IDC_URL) != NULL);
    CHECK(SendDlgItemMessageW(window, IDC_URL, LM_GETITEM, 0, (LPARAM)&item));
    CHECK(!wcscmp(item.szUrl, L"https://github.com/halfstay/apath"));
    PostMessageW(window, WM_KEYDOWN, VK_ESCAPE, 0);
    break;
  }
  case ACTION_SAVE:
  case ACTION_OPEN: {
    HWND combo = GetDlgItem(window, cmb13);
    HWND edit =
        combo ? (HWND)SendMessageW(combo, CBEM_GETEDITCONTROL, 0, 0) : NULL;
    if (!edit)
      edit = GetDlgItem(window, edt1);
    SendMessageW(window, CDM_SETCONTROLTEXT, edt1, (LPARAM)test_path);
    if (combo)
      SetWindowTextW(combo, test_path);
    CHECK(edit != NULL);
    CHECK(SetWindowTextW(edit, test_path));
    PostMessageW(window, WM_COMMAND, IDOK, 0);
    break;
  }
  case ACTION_DISCARD:
  case ACTION_ASSOC_NO:
    PostMessageW(window, WM_COMMAND, IDNO, 0);
    break;
  case ACTION_CANCEL:
    PostMessageW(window, WM_COMMAND, IDCANCEL, 0);
    break;
  case ACTION_ASSOC_YES:
    PostMessageW(window, WM_COMMAND, IDYES, 0);
    break;
  case ACTION_ERROR:
    PostMessageW(window, WM_COMMAND, IDOK, 0);
    break;
  default:
    break;
  }
  return FALSE;
}

static VOID CALLBACK dialog_tick(HWND hwnd, UINT msg, UINT_PTR timer,
                                 DWORD time) {
  (void)hwnd;
  (void)msg;
  (void)timer;
  (void)time;
  if (GetTickCount() - dialog_started > 15000) {
    fprintf(stderr, "Timed out waiting for dialog action %d\n", action);
    exit(2);
  }
  EnumThreadWindows(GetCurrentThreadId(), handle_dialog, 0);
}

static void expect_dialog(int which) {
  action = which;
  dialog_seen = 0;
  dialog_started = GetTickCount();
  dialog_timer = SetTimer(NULL, 0, 40, dialog_tick);
  CHECK(dialog_timer != 0);
}
static void check_dialog(void) { CHECK(dialog_seen && action == ACTION_NONE); }

static void pump(void) {
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT)
      continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  UpdateWindow(app.window);
}

static POINT cell_point(double x, double y) {
  RECT r;
  GetClientRect(app.window, &r);
  return (POINT){(LONG)lround(x * app.cell_x - app.scroll_x),
                 (LONG)lround(r.bottom - y * app.cell_y + app.scroll_y)};
}

static void click(int button, double x, double y) {
  POINT p = cell_point(x, y);
  SendMessageW(app.window, button == 1 ? WM_LBUTTONDOWN : WM_RBUTTONDOWN, 0,
               MAKELPARAM(p.x, p.y));
  SendMessageW(app.window, button == 1 ? WM_LBUTTONUP : WM_RBUTTONUP, 0,
               MAKELPARAM(p.x, p.y));
  pump();
}

static void drag(int button, double x0, double y0, double x1, double y1) {
  POINT a = cell_point(x0, y0), b = cell_point(x1, y1);
  SendMessageW(app.window, button == 1 ? WM_LBUTTONDOWN : WM_RBUTTONDOWN, 0,
               MAKELPARAM(a.x, a.y));
  SendMessageW(app.window, WM_MOUSEMOVE, button == 1 ? MK_LBUTTON : MK_RBUTTON,
               MAKELPARAM(b.x, b.y));
  SendMessageW(app.window, button == 1 ? WM_LBUTTONUP : WM_RBUTTONUP, 0,
               MAKELPARAM(b.x, b.y));
  pump();
}

static void command(unsigned id) {
  SendMessageW(app.window, WM_COMMAND, id, 0);
  pump();
}

static void accelerator(WORD key, int shift) {
  HACCEL accelerators =
      LoadAcceleratorsW(app.instance, MAKEINTRESOURCEW(IDR_ACCEL));
  BYTE old[256], keys[256];
  MSG msg;
  GetKeyboardState(old);
  ZeroMemory(keys, sizeof(keys));
  keys[VK_CONTROL] = 0x80;
  if (shift)
    keys[VK_SHIFT] = 0x80;
  CHECK(SetKeyboardState(keys));
  ZeroMemory(&msg, sizeof(msg));
  msg.hwnd = app.window;
  msg.message = WM_KEYDOWN;
  msg.wParam = key;
  CHECK(TranslateAcceleratorW(app.window, accelerators, &msg));
  SetKeyboardState(old);
  pump();
}

static COLORREF pixel_at(double x, double y) {
  POINT p = cell_point(x, y);
  HDC dc;
  COLORREF color;
  UpdateWindow(app.window);
  dc = GetDC(app.window);
  color = GetPixel(dc, p.x, p.y);
  ReleaseDC(app.window, dc);
  return color;
}

static void mouse_at(UINT message, double x, double y) {
  POINT p = cell_point(x, y);
  SendMessageW(app.window, message, 0, MAKELPARAM(p.x, p.y));
}

static void reset_test_map(unsigned pixels, unsigned width, unsigned height,
                           int display) {
  app_end_gesture(1);
  app_leave_mesh();
  CHECK(ap_map_create(&app.map, pixels, width, height) == AP_OK);
  ap_history_free(&app.history);
  ap_history_init(&app.history, AP_HISTORY_BUDGET);
  app_reset_view();
  app.zoom = fmax(1, display / fmin(app.cell_x, app.cell_y));
  app_scrollbars();
  app.scroll_x = app.scroll_y = 0;
  app_scrollbars();
  app_refresh();
  pump();
}

static void check_only_rectangle(int x0, int y0, int x1, int y1) {
  int x, y;
  for (y = 0; y < app.map.height; ++y)
    for (x = 0; x < app.map.width; ++x)
      CHECK(ap_map_get(&app.map, x, y) ==
            (x >= x0 && x <= x1 && y >= y0 && y <= y1));
}

static void test_rectangles(void) {
  unsigned direction;
  size_t cursor;
  POINT p, screen, old_cursor;
  RECT r;
  int x1, y1;
  uint8_t before[96]; /* The initial map is 32 x 24 cells. */
  memcpy(before, app.map.bits, sizeof(before));
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  CHECK(GetCapture() == app.window);
  mouse_at(WM_MOUSEMOVE, 10.5, 11.5);
  CHECK(!memcmp(before, app.map.bits, sizeof(before)));
  CHECK(!app.history.count && !app.history.recording &&
        !ap_history_dirty(&app.history));
  CHECK(pixel_at(5.5, 6.5) == RGB(255, 255, 255));
  /* The dotted preview is visible at the rectangle boundary. */
  {
    HDC dc;
    int i, outline = 0;
    p = cell_point(2, 6);
    dc = GetDC(app.window);
    for (i = 0; i < 16; ++i)
      if (GetPixel(dc, p.x, p.y + i) == RGB(0, 110, 45))
        ++outline;
    ReleaseDC(app.window, dc);
    CHECK(outline > 0);
  }
  mouse_at(WM_MOUSEMOVE, 4.5, 5.5); /* Shrinking must discard swept cells. */
  CHECK(!memcmp(before, app.map.bits, sizeof(before)));
  mouse_at(WM_LBUTTONUP, 4.5, 5.5);
  check_only_rectangle(2, 3, 4, 5);
  CHECK(app.history.count == 1);
  accelerator('Z', 0);
  check_only_rectangle(0, 0, -1, -1);
  accelerator('Y', 0);
  check_only_rectangle(2, 3, 4, 5);
  cursor = app.history.cursor;
  drag(1, 2.5, 3.5, 4.5, 5.5);
  CHECK(app.history.cursor == cursor); /* No-op. */
  memcpy(before, app.map.bits, sizeof(before));
  mouse_at(WM_RBUTTONDOWN, 4.5, 5.5);
  CHECK(!memcmp(before, app.map.bits, sizeof(before)));
  mouse_at(WM_MOUSEMOVE, 2.5, 3.5);
  CHECK(!memcmp(before, app.map.bits, sizeof(before)) &&
        app.history.cursor == cursor);
  CHECK(pixel_at(3.5, 4.5) == RGB(72, 183, 93));
  mouse_at(WM_RBUTTONUP, 2.5, 3.5);
  check_only_rectangle(0, 0, -1, -1);
  CHECK(app.history.cursor == cursor + 1);
  accelerator('Z', 0);
  CHECK(!memcmp(before, app.map.bits, sizeof(before)));
  accelerator('Y', 0);
  check_only_rectangle(0, 0, -1, -1);

  /* Every drag direction fills the entire box, including off-diagonal cells. */
  for (direction = 0; direction < 4; ++direction) {
    reset_test_map(24, 32, 24, 24);
    drag(1, (direction & 1) ? 7.5 : 2.5, (direction & 2) ? 8.5 : 3.5,
         (direction & 1) ? 2.5 : 7.5, (direction & 2) ? 3.5 : 8.5);
    check_only_rectangle(2, 3, 7, 8);
    CHECK(app.history.count == 1);
  }
  reset_test_map(24, 32, 24, 24);
  /* A final release without a move message still supplies the complete box. */
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  mouse_at(WM_LBUTTONUP, 7.5, 8.5);
  check_only_rectangle(2, 3, 7, 8);
  accelerator('Z', 0);
  drag(1, 2.5, 3.5, -5, -4);
  check_only_rectangle(0, 0, 2, 3);
  accelerator('Z', 0);
  drag(1, 30.5, 22.5, 38, 29);
  check_only_rectangle(30, 22, 31, 23);
  cursor = app.history.cursor;
  drag(1, 34, 4, 38, 8);
  CHECK(app.history.cursor == cursor);
  check_only_rectangle(30, 22, 31, 23);
  accelerator('Z', 0);
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  mouse_at(WM_MOUSEMOVE, 7.5, 8.5);
  mouse_at(WM_RBUTTONUP, 7.5, 8.5);
  CHECK(app.gesture == 1);
  SendMessageW(app.window, WM_KEYDOWN, VK_ESCAPE, 0);
  CHECK(!app.gesture && GetCapture() != app.window && !app.history.cursor);
  mouse_at(WM_LBUTTONUP, 7.5, 8.5);
  check_only_rectangle(0, 0, -1, -1);
  mouse_at(WM_RBUTTONDOWN, 2.5, 3.5);
  mouse_at(WM_MOUSEMOVE, 7.5, 8.5);
  ReleaseCapture();
  CHECK(!app.gesture && !app.history.cursor);
  mouse_at(WM_RBUTTONUP, 7.5, 8.5);
  check_only_rectangle(0, 0, -1, -1);
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  mouse_at(WM_MOUSEMOVE, 7.5, 8.5);
  SendMessageW(app.window, WM_CANCELMODE, 0, 0);
  CHECK(!app.gesture);
  check_only_rectangle(0, 0, -1, -1);
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  mouse_at(WM_MOUSEMOVE, 7.5, 8.5);
  /* Win32 suppresses menu accelerators while a window captures the mouse. */
  command(IDM_FILL_CLIENT);
  CHECK(!app.gesture && !app.history.cursor);
  check_only_rectangle(0, 0, -1, -1);

  /* Auto-scroll extends the box while retaining its original world cell. */
  reset_test_map(24, 40, 30, 64);
  GetCursorPos(&old_cursor);
  mouse_at(WM_LBUTTONDOWN, 2.5, 3.5);
  GetClientRect(app.window, &r);
  p = (POINT){r.right - 2, 2};
  screen = p;
  ClientToScreen(app.window, &screen);
  CHECK(SetCursorPos(screen.x, screen.y));
  SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(p.x, p.y));
  SendMessageW(app.window, WM_TIMER, 1, 0);
  CHECK(app.scroll_x > 0 && app.scroll_y > 0);
  CHECK(fabs(app.down_world.x - 2.5) <= .51 / app.cell_x &&
        fabs(app.down_world.y - 3.5) <= .51 / app.cell_y && !app.history.count);
  check_only_rectangle(0, 0, -1, -1);
  x1 = (int)((app.scroll_x + p.x) / app.cell_x);
  y1 = (int)((app.scroll_y + r.bottom - p.y) / app.cell_y);
  SendMessageW(app.window, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
  check_only_rectangle(2, 3, x1, y1);
  CHECK(app.history.count == 1);
  SetCursorPos(old_cursor.x, old_cursor.y);
  reset_test_map(24, 32, 24, 24);
}

static void check_baseline(void) {
  RECT r;
  LONG_PTR style = GetWindowLongPtrW(app.window, GWL_STYLE);
  double w, h, base;
  GetClientRect(app.window, &r);
  w = r.right + ((style & WS_VSCROLL) ? GetSystemMetrics(SM_CXVSCROLL) : 0);
  h = r.bottom + ((style & WS_HSCROLL) ? GetSystemMetrics(SM_CYHSCROLL) : 0);
  CHECK(app.zoom >= 1);
  base = fmin(w * app.map.cell_size / app.map.pixel_width,
              h * app.map.cell_size / app.map.pixel_height);
  CHECK(app.cell_x == app.cell_y);
  CHECK(fabs(app.cell_x - base * app.zoom) <= 1e-9 * fmax(1, app.cell_x));
  if (app.zoom == 1) {
    CHECK(!(style & (WS_HSCROLL | WS_VSCROLL)));
    CHECK(!app.scroll_x && !app.scroll_y);
    CHECK(llround(app.map.pixel_width * app.cell_x / app.map.cell_size) <=
          r.right);
    CHECK(llround(app.map.pixel_height * app.cell_y / app.map.cell_size) <=
          r.bottom);
  }
}

static void check_sized_window(int tight) {
  MONITORINFO monitor;
  RECT window, client, frame = {0, 0, 0, 0};
  double width, height;
  monitor.cbSize = sizeof(monitor);
  CHECK(GetMonitorInfoW(MonitorFromWindow(app.window, MONITOR_DEFAULTTONEAREST),
                        &monitor));
  CHECK(GetWindowRect(app.window, &window) &&
        GetClientRect(app.window, &client));
  CHECK(window.left >= monitor.rcWork.left && window.top >= monitor.rcWork.top);
  CHECK(window.right <= monitor.rcWork.right &&
        window.bottom <= monitor.rcWork.bottom);
  CHECK(!IsZoomed(app.window) && app.zoom == 1);
  check_baseline();
  width = app.map.pixel_width * app.cell_x / app.map.cell_size;
  height = app.map.pixel_height * app.cell_y / app.map.cell_size;
  if (tight) {
    CHECK(client.right - width < 1.01 && client.right - width >= -1e-6);
    CHECK(client.bottom - height < 1.01 && client.bottom - height >= -1e-6);
  }
  CHECK(AdjustWindowRectEx(
      &frame, (DWORD)GetWindowLongPtrW(app.window, GWL_STYLE), TRUE,
      (DWORD)GetWindowLongPtrW(app.window, GWL_EXSTYLE)));
  if (app.map.pixel_width + frame.right - frame.left <=
          (uint32_t)(monitor.rcWork.right - monitor.rcWork.left) &&
      app.map.pixel_height + frame.bottom - frame.top <=
          (uint32_t)(monitor.rcWork.bottom - monitor.rcWork.top) &&
      app.map.pixel_width + frame.right - frame.left >= 420 &&
      app.map.pixel_height + frame.bottom - frame.top >= 300) {
    CHECK(client.right == (LONG)app.map.pixel_width &&
          client.bottom == (LONG)app.map.pixel_height);
    CHECK(app.cell_x == app.map.cell_size);
  }
}

static void test_new_window(void) {
  RECT before, after;
  uint64_t state;
  uint8_t *bits;
  unsigned i;
  const unsigned sizes[][2] = {
      {805, 603}, {1200, 1600}, {1600, 400}, {45, 27}, {65535, 1}};
  reset_test_map(24, 32, 24, 24);
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    new_width = sizes[i][0];
    new_height = sizes[i][1];
    expect_dialog(ACTION_NEW);
    accelerator('N', 0);
    check_dialog();
    CHECK(app.map.pixel_width == new_width &&
          app.map.pixel_height == new_height);
    CHECK(!app.history.count && !app.filename[0]);
    check_sized_window(i != 4);
  }
  new_width = 805;
  new_height = 603;
  ShowWindow(app.window, SW_MAXIMIZE);
  pump();
  CHECK(IsZoomed(app.window));
  expect_dialog(ACTION_NEW);
  accelerator('N', 0);
  check_dialog();
  check_sized_window(1);
  click(1, .5, .5);
  state = app.history.current_id;
  bits = app.map.bits;
  GetWindowRect(app.window, &before);
  expect_dialog(ACTION_NEW_CANCEL);
  accelerator('N', 0);
  check_dialog();
  GetWindowRect(app.window, &after);
  CHECK(EqualRect(&before, &after));
  CHECK(app.map.bits == bits && app.history.current_id == state);
  CHECK(ap_map_get(&app.map, 0, 0) && ap_history_dirty(&app.history));
  reset_test_map(24, 32, 24, 24);
}

static void test_fill_view(void) {
  POINT p;
  RECT r, window;
  double center_x, center_y, base;
  uint64_t state;
  int i;
  reset_test_map(20, 100, 80, 40);
  click(1, 2.5, 3.5);
  state = app.history.current_id;
  app.scroll_x = 600;
  app.scroll_y = 500;
  app_scrollbars();
  GetClientRect(app.window, &r);
  p = (POINT){r.right / 2, r.bottom / 2};
  ClientToScreen(app.window, &p);
  SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA / 2),
               MAKELPARAM(p.x, p.y));
  CHECK(app.wheel_remainder == WHEEL_DELTA / 2);
  accelerator('H', 0);
  base = app.cell_x;
  check_baseline();
  CHECK(app.zoom == 1 && app.map.cell_size == 20 && app.wheel_remainder == 0);
  CHECK(app.history.current_id == state && ap_map_get(&app.map, 2, 3));
  for (i = 0; i < 40; ++i)
    SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA),
                 MAKELPARAM(p.x, p.y));
  CHECK(app.zoom == 1 && app.cell_x == base);
  check_baseline();
  GetClientRect(app.window, &r);
  center_x = (app.scroll_x + r.right / 2) / app.cell_x;
  center_y = (app.scroll_y + r.bottom - r.bottom / 2) / app.cell_y;
  p = (POINT){r.right / 2, r.bottom / 2};
  ClientToScreen(app.window, &p);
  SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
               MAKELPARAM(p.x, p.y));
  ScreenToClient(app.window, &p);
  GetClientRect(app.window, &r);
  CHECK(fabs((app.scroll_x + p.x) / app.cell_x - center_x) <= .51 / app.cell_x);
  CHECK(fabs((app.scroll_y + r.bottom - p.y) / app.cell_y - center_y) <=
        .51 / app.cell_y);
  CHECK((GetWindowLongPtrW(app.window, GWL_STYLE) &
         (WS_HSCROLL | WS_VSCROLL)) == (WS_HSCROLL | WS_VSCROLL));
  /* Returning to the complete map resets offsets on both axes. */
  reset_test_map(10, 200, 20, 80);
  app.scroll_x = 2400;
  app.scroll_y = 250;
  app_scrollbars();
  GetClientRect(app.window, &r);
  center_x = (app.scroll_x + r.right / 2) / app.cell_x;
  accelerator('H', 0);
  GetClientRect(app.window, &r);
  CHECK(app.zoom == 1 && !app.scroll_x && !app.scroll_y);
  check_baseline();
  reset_test_map(20, 100, 80, 40);
  GetWindowRect(app.window, &window);
  app.zoom = 2;
  app_scrollbars();
  GetClientRect(app.window, &r);
  app.scroll_x = llround((100 * app.cell_x - r.right) / 2);
  app.scroll_y = llround((80 * app.cell_y - r.bottom) / 2);
  app_scrollbars();
  center_x = (app.scroll_x + r.right / 2) / app.cell_x;
  center_y = (app.scroll_y + r.bottom - r.bottom / 2) / app.cell_y;
  SetWindowPos(app.window, NULL, 0, 0, 900, 650, SWP_NOMOVE | SWP_NOZORDER);
  pump();
  check_baseline();
  CHECK(app.zoom == 2);
  GetClientRect(app.window, &r);
  CHECK(fabs((app.scroll_x + r.right / 2) / app.cell_x - center_x) <=
        .51 / app.cell_x);
  CHECK(fabs((app.scroll_y + r.bottom - r.bottom / 2) / app.cell_y -
             center_y) <= .51 / app.cell_y);
  accelerator('H', 0);
  SetWindowPos(app.window, NULL, 0, 0, window.right - window.left,
               window.bottom - window.top, SWP_NOMOVE | SWP_NOZORDER);
  pump();
  check_baseline();
  CHECK(app.zoom == 1);
  /* A tall map, a wide map and a fractional boundary all fit completely. */
  reset_test_map(20, 60, 80, 0);
  check_baseline();
  CHECK(app.cell_x == app.cell_y);
  click(1, 59.5, 79.5);
  CHECK(ap_map_get(&app.map, 59, 79));
  CHECK(pixel_at(59.5, 79.5) == RGB(72, 183, 93));
  reset_test_map(20, 80, 20, 0);
  check_baseline();
  click(1, 79.5, 19.5);
  CHECK(ap_map_get(&app.map, 79, 19));
  CHECK(pixel_at(79.5, 19.5) == RGB(72, 183, 93));
  reset_test_map(1, 5000, 3000, 0);
  CHECK(app.cell_x < 1);
  check_baseline();
  /* Extreme zoom still maps a native scrollbar to offsets beyond INT_MAX. */
  reset_test_map(1, 65535, 2, 65535);
  SendMessageW(app.window, WM_HSCROLL, SB_RIGHT, 0);
  GetClientRect(app.window, &r);
  CHECK(app.scroll_x == llround(65535 * app.cell_x) - r.right &&
        app.scroll_x > INT_MAX);
  SendMessageW(app.window, WM_VSCROLL, SB_BOTTOM, 0);
  SendMessageW(app.window, WM_LBUTTONDOWN, 0,
               MAKELPARAM(r.right - 10, r.bottom - 10));
  SendMessageW(app.window, WM_LBUTTONUP, 0,
               MAKELPARAM(r.right - 10, r.bottom - 10));
  CHECK(ap_map_get(&app.map, 65534, 0));
  pump();
  reset_test_map(1, 2, 65535, 65535);
  SendMessageW(app.window, WM_VSCROLL, SB_TOP, 0);
  GetClientRect(app.window, &r);
  CHECK(app.scroll_y == llround(65535 * app.cell_y) - r.bottom &&
        app.scroll_y > INT_MAX);
  SendMessageW(app.window, WM_HSCROLL, SB_LEFT, 0);
  SendMessageW(app.window, WM_LBUTTONDOWN, 0, MAKELPARAM(10, 10));
  SendMessageW(app.window, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
  CHECK(ap_map_get(&app.map, 0, 65534));
  reset_test_map(24, 32, 24, 24);
}

static void test_editor(void) {
  unsigned i;
  POINT p;
  double old_cell;
  RECT r;
  size_t history;
  CHECK(GetMenuItemCount(GetMenu(app.window)) == 3);
  CHECK(GetClassLongPtrW(app.window, GCLP_HICON) != 0);
  CHECK(GetClassLongPtrW(app.window, GCLP_HICONSM) != 0);
  click(1, .5, .5);
  CHECK(ap_map_get(&app.map, 0, 0));
  CHECK(app.history.count == 1);
  CHECK(pixel_at(.5, .5) == RGB(72, 183, 93));
  CHECK(pixel_at(1.5, .5) == RGB(255, 255, 255));
  click(1, .5, .5);
  CHECK(!ap_map_get(&app.map, 0, 0));
  drag(1, 2.5, 2.5, 12.5, 2.5);
  for (i = 2; i <= 12; ++i)
    CHECK(ap_map_get(&app.map, (int)i, 2));
  history = app.history.count;
  accelerator('Z', 0);
  for (i = 2; i <= 12; ++i)
    CHECK(!ap_map_get(&app.map, (int)i, 2));
  accelerator('Y', 0);
  CHECK(app.history.count == history);
  drag(2, 4.5, 2.5, 8.5, 2.5);
  for (i = 4; i <= 8; ++i)
    CHECK(!ap_map_get(&app.map, (int)i, 2));
  accelerator('Z', 0);
  p = cell_point(1.5, 5.5);
  SendMessageW(app.window, WM_LBUTTONDOWN, 0, MAKELPARAM(p.x, p.y));
  p = cell_point(9.5, 5.5);
  SendMessageW(app.window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(p.x, p.y));
  SendMessageW(app.window, WM_KEYDOWN, VK_ESCAPE, 0);
  for (i = 1; i <= 9; ++i)
    CHECK(!ap_map_get(&app.map, (int)i, 5));
  CHECK(GetCapture() != app.window);
  drag(1, 1.5, 7.5, 8.5, 14.5);
  for (i = 0; i <= 7; ++i)
    CHECK(ap_map_get(&app.map, (int)(1 + i), (int)(7 + i)));
  /* Zoom in enough to reveal both scrollbars, without changing stored pixels.
   */
  GetClientRect(app.window, &r);
  p = (POINT){r.right / 2, r.bottom / 2};
  ClientToScreen(app.window, &p);
  old_cell = app.cell_x;
  for (i = 0; i < 5; ++i)
    SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                 MAKELPARAM(p.x, p.y));
  CHECK(app.cell_x > old_cell && app.map.cell_size == 24);
  CHECK((GetWindowLongPtrW(app.window, GWL_STYLE) &
         (WS_HSCROLL | WS_VSCROLL)) == (WS_HSCROLL | WS_VSCROLL));
  SendMessageW(app.window, WM_HSCROLL, SB_RIGHT, 0);
  CHECK(app.scroll_x > 0);
  SendMessageW(app.window, WM_VSCROLL, SB_TOP, 0);
  CHECK(app.scroll_y > 0);
  for (i = 0; i < 20; ++i)
    SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA),
                 MAKELPARAM(p.x, p.y));
  CHECK(app.zoom == 1);
  check_baseline();
  accelerator('H', 0);
  CHECK(app.zoom == 1);
  expect_dialog(ACTION_CANCEL);
  CHECK(!app_confirm_save());
  check_dialog();
  ap_history_saved(&app.history);
  expect_dialog(ACTION_NEW);
  accelerator('N', 0);
  check_dialog();
  CHECK(app.map.cell_size == 20 && app.map.width == 41 &&
        app.map.height == 31 && app.map.pixel_width == 805 &&
        app.map.pixel_height == 603 && app.zoom == 1);
  CHECK(!app.history.count && !app.filename[0]);
  check_sized_window(1);
}

static void test_mesh(void) {
  drag(1, .5, .5, 10.5, 5.5);
  accelerator('M', 0);
  CHECK(app.mesh_mode && app.mesh.triangle_count == 4);
  CHECK(!app.have_start && !app.have_end && !app.route.count);
  click(1, .5, .5);
  CHECK(app.have_start && !app.have_end);
  CHECK(pixel_at(.5, .5) == RGB(230, 40, 45));
  {
    POINT p = cell_point(app.start.x, app.start.y);
    HDC dc = GetDC(app.window);
    CHECK(GetPixel(dc, p.x, p.y - 4) == RGB(230, 40, 45));
    CHECK(GetPixel(dc, p.x - 5, p.y - 4) != RGB(230, 40, 45));
    CHECK(GetPixel(dc, p.x - 4, p.y + 4) == RGB(230, 40, 45));
    ReleaseDC(app.window, dc);
  }
  click(2, 9.5, 4.5);
  CHECK(app.have_end && app.route.count == 2 && app.route_result == AP_OK);
  {
    POINT p = cell_point(app.end.x, app.end.y);
    HDC dc = GetDC(app.window);
    CHECK(GetPixel(dc, p.x - 4, p.y - 4) == RGB(230, 40, 45));
    CHECK(GetPixel(dc, p.x + 4, p.y + 4) == RGB(230, 40, 45));
    ReleaseDC(app.window, dc);
  }
  click(1, 20.5, 20.5);
  CHECK(fabs(app.start.x - .5) <= .51 / app.cell_x &&
        fabs(app.start.y - .5) <= .51 / app.cell_y);
  click(2, 8.5, 3.5);
  CHECK(fabs(app.end.x - 8.5) <= .51 / app.cell_x &&
        app.route.points[app.route.count - 1].x == app.end.x);
  {
    ApPoint *route = app.route.points;
    ApTriangle *triangles = app.mesh.triangles;
    POINT p = cell_point(4, 3);
    ClientToScreen(app.window, &p);
    SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                 MAKELPARAM(p.x, p.y));
    CHECK(app.zoom > 1);
    accelerator('H', 0);
    CHECK(app.zoom == 1 && app.mesh_mode && app.have_start && app.have_end);
    CHECK(app.mesh.triangles == triangles && app.route.points == route &&
          app.route.count == 2);
    CHECK(fabs(app.start.x - .5) <= .51 / app.cell_x &&
          fabs(app.end.x - 8.5) <= .51 / app.cell_x &&
          app.route_result == AP_OK);
  }
  accelerator('M', 0);
  CHECK(!app.mesh_mode && !app.have_start && !app.have_end && !app.route.count);
  drag(2, 5.5, .5, 5.5, 5.5);
  accelerator('M', 0);
  CHECK(app.mesh.component_count == 2);
  click(1, .5, .5);
  click(2, 9.5, 4.5);
  CHECK(app.route_result == AP_NO_PATH && app.route.count == 0);
  accelerator('Z', 0);
  CHECK(!app.mesh_mode && ap_map_get(&app.map, 5, 0));
  accelerator('M', 0);
  CHECK(app.mesh.component_count == 1 && !app.have_start);
  accelerator('M', 0);
}

static void test_files_and_dialogs(void) {
  wchar_t temp[MAX_PATH];
  FILE *file;
  ApMap saved = {0};
  CHECK(GetTempPathW(MAX_PATH, temp) > 0);
  _snwprintf(test_path, AP_PATH_CAP,
             L"%lsapath integration \x5730\x56fe %lu.amd", temp,
             (unsigned long)GetCurrentProcessId());
  DeleteFileW(test_path);
  expect_dialog(ACTION_SAVE);
  accelerator('S', 1);
  check_dialog();
  CHECK(!wcscmp(app.filename, test_path) && !ap_history_dirty(&app.history));
  file = _wfopen(test_path, L"rb");
  CHECK(file != NULL);
  {
    unsigned char bytes[9], expected[] = {'A', 'M', 'D', 20, 0, 37, 3, 91, 2};
    CHECK(fread(bytes, 1, 9, file) == 9 && !memcmp(bytes, expected, 9));
    rewind(file);
  }
  CHECK(ap_map_read(file, &saved) == AP_OK);
  fclose(file);
  CHECK(saved.width == 41 && saved.pixel_width == 805 &&
        saved.pixel_height == 603 && saved.cell_size == 20 &&
        ap_map_get(&saved, 0, 0));
  ap_map_free(&saved);
  click(1, .5, .5);
  CHECK(ap_history_dirty(&app.history));
  accelerator('S', 0);
  CHECK(!ap_history_dirty(&app.history));
  click(1, .5, .5);
  expect_dialog(ACTION_DISCARD);
  CHECK(app_open(test_path));
  check_dialog();
  CHECK(!ap_map_get(&app.map, 0, 0) && !ap_history_dirty(&app.history));
  app.zoom = 2;
  app_scrollbars();
  expect_dialog(ACTION_OPEN);
  accelerator('O', 0);
  check_dialog();
  CHECK(app.map.width == 41);
  CHECK(app.zoom == 1);
  check_baseline();
  expect_dialog(ACTION_ABOUT);
  command(IDM_ABOUT);
  check_dialog();
  CHECK(IsWindowEnabled(app.window));
  /* Failed parsing cannot replace a valid active map. */
  file = _wfopen(test_path, L"wb");
  CHECK(file != NULL);
  fputs("bad", file);
  fclose(file);
  expect_dialog(ACTION_ERROR);
  CHECK(!app_open(test_path));
  check_dialog();
  CHECK(app.map.width == 41);
  CHECK(DeleteFileW(test_path));
}

static void registry_value(const wchar_t *path, const wchar_t *name,
                           const wchar_t *value) {
  HKEY key;
  CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &key, NULL) == ERROR_SUCCESS);
  CHECK(RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value,
                       (DWORD)((wcslen(value) + 1) * sizeof(wchar_t))) ==
        ERROR_SUCCESS);
  RegCloseKey(key);
}

static void silent_association(void) {
  expect_dialog(ACTION_UNEXPECTED);
  app_associate();
  KillTimer(NULL, dialog_timer);
  CHECK(!dialog_seen);
  action = ACTION_NONE;
}

static void test_association(void) {
  wchar_t value[AP_PATH_CAP], expected[AP_PATH_CAP], before[AP_PATH_CAP];
  const wchar_t *command_key =
      L"Software\\Classes\\APath.Map\\shell\\open\\command";
  const wchar_t *choice_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\E"
                              L"xplorer\\FileExts\\.amd\\UserChoice";
  DWORD bytes;
  HKEY key;
  /* Isolated test account only: begin without any per-user test registration.
   */
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_ALL_ACCESS,
                    &key) == ERROR_SUCCESS) {
    RegDeleteTreeW(key, L".amd");
    RegDeleteTreeW(key, L"APath.Map");
    RegCloseKey(key);
  }
  silent_association();
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\.amd", NULL,
                     RRF_RT_REG_SZ, NULL, value, &bytes) == ERROR_SUCCESS);
  CHECK(!wcscmp(value, L"APath.Map"));
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, L"Software\\Classes\\APath.Map", NULL,
                     RRF_RT_REG_SZ, NULL, value, &bytes) == ERROR_SUCCESS);
  CHECK(!wcscmp(value, L"apath map data"));
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, command_key, NULL, RRF_RT_REG_SZ, NULL,
                     value, &bytes) == ERROR_SUCCESS);
  GetModuleFileNameW(NULL, expected, AP_PATH_CAP);
  CHECK(value[0] == '"' && wcsstr(value, expected) && wcsstr(value, L"\"%1\""));
  lstrcpyW(before, value);
  silent_association();
  /* Identity is the executable, including when its argument template differs.
   */
  _snwprintf(value, AP_PATH_CAP, L"\"%ls\" \"%%L\"", expected);
  registry_value(command_key, NULL, value);
  silent_association();
  /* A protected choice pointing to this executable also needs no prompt. */
  registry_value(choice_key, L"ProgId", L"APath.Map");
  silent_association();
  registry_value(command_key, NULL, L"\"C:\\Other APath\\apath.exe\" \"%1\"");
  expect_dialog(ACTION_ASSOC_NO);
  app_associate();
  check_dialog();
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, command_key, NULL, RRF_RT_REG_SZ, NULL,
                     value, &bytes) == ERROR_SUCCESS);
  CHECK(wcsstr(value, L"Other APath") != NULL);
  expect_dialog(ACTION_ASSOC_YES);
  app_associate();
  check_dialog();
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, command_key, NULL, RRF_RT_REG_SZ, NULL,
                     value, &bytes) == ERROR_SUCCESS);
  CHECK(!wcscmp(before, value));
  silent_association();
  /* Different protected default wins over our ordinary extension entry. */
  registry_value(choice_key, L"ProgId", L"Another.Application");
  expect_dialog(ACTION_ASSOC_NO);
  app_associate();
  check_dialog();
  bytes = sizeof(value);
  CHECK(RegGetValueW(HKEY_CURRENT_USER, choice_key, L"ProgId", RRF_RT_REG_SZ,
                     NULL, value, &bytes) == ERROR_SUCCESS);
  CHECK(!wcscmp(value, L"Another.Application"));
  if (RegOpenKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts",
          0, KEY_ALL_ACCESS, &key) == ERROR_SUCCESS) {
    RegDeleteTreeW(key, L".amd");
    RegCloseKey(key);
  }
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0, KEY_ALL_ACCESS,
                    &key) == ERROR_SUCCESS) {
    RegDeleteTreeW(key, L".amd");
    RegDeleteTreeW(key, L"APath.Map");
    RegCloseKey(key);
  }
}

static void test_partial_cells(void) {
  ApPoint start;
  app_leave_mesh();
  ap_history_free(&app.history);
  ap_history_init(&app.history, AP_HISTORY_BUDGET);
  CHECK(ap_map_create_pixels(&app.map, 20, 45, 27) == AP_OK);
  app_reset_view();
  pump();
  CHECK(app.map.width == 3 && app.map.height == 2);
  check_baseline();
  drag(1, .5, .5, 2.2, 1.3);
  CHECK(app.history.count == 1);
  check_only_rectangle(0, 0, 2, 1);
  click(1, 2.3, 1.1);
  CHECK(ap_map_get(&app.map, 2, 1)); /* Beyond the partial column. */
  click(2, 2.1, 1.4);
  CHECK(ap_map_get(&app.map, 2, 1)); /* Beyond the partial row. */
  accelerator('M', 0);
  CHECK(app.mesh_mode);
  click(1, 2.2, .5);
  click(2, 2.2, 1.3);
  CHECK(app.have_start && app.have_end && app.route.count == 2);
  CHECK(fabs(app.mesh.extent_x - 2.25) < 1e-12 &&
        fabs(app.mesh.extent_y - 1.35) < 1e-12);
  start = app.start;
  click(1, 2.3, 1.1);
  CHECK(app.start.x == start.x && app.start.y == start.y);
  app_leave_mesh();
  CHECK(ap_map_create_pixels(&app.map, 65535, 1, 1) == AP_OK);
  app_reset_view();
  pump();
  CHECK(app.zoom == 1 && app.map.width == 1 && app.map.height == 1);
  check_baseline();
  click(1, .25 / 65535, .25 / 65535);
  CHECK(ap_map_get(&app.map, 0, 0));
  accelerator('M', 0);
  click(1, .25 / 65535, .25 / 65535);
  click(2, .5 / 65535, .5 / 65535);
  CHECK(app.have_start && app.have_end && app.route_result == AP_OK);
  {
    POINT p = cell_point(.4 / 65535, .4 / 65535);
    ClientToScreen(app.window, &p);
    SendMessageW(app.window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                 MAKELPARAM(p.x, p.y));
    CHECK(app.zoom > 1 && app.have_start && app.have_end &&
          app.route_result == AP_OK);
    accelerator('H', 0);
    CHECK(app.zoom == 1);
    check_baseline();
  }
  app_leave_mesh();
}

static void check_corridor_route(int reverse) {
  const ApPoint corners[] = {{9, 6}, {40, 6}, {40, 17}};
  size_t i;
  double actual = 0, expected = 0;
  ApPoint previous = app.start;
  CHECK(app.route_result == AP_OK && app.route.count == 5);
  for (i = 0; i < 3; ++i) {
    ApPoint corner = corners[reverse ? 2 - i : i];
    CHECK(hypot(app.route.points[i + 1].x - corner.x,
                app.route.points[i + 1].y - corner.y) < 1e-8);
    expected += hypot(corner.x - previous.x, corner.y - previous.y);
    previous = corner;
  }
  expected += hypot(app.end.x - previous.x, app.end.y - previous.y);
  for (i = 1; i < app.route.count; ++i)
    actual += hypot(app.route.points[i].x - app.route.points[i - 1].x,
                    app.route.points[i].y - app.route.points[i - 1].y);
  CHECK(fabs(actual - expected) < 1e-8);
}

static void test_corridor_route(void) {
  unsigned x, y;
  app_leave_mesh();
  ap_history_free(&app.history);
  ap_history_init(&app.history, AP_HISTORY_BUDGET);
  CHECK(ap_map_create(&app.map, 10, 56, 42) == AP_OK);
  app_reset_view();
  pump();
  for (y = 0; y < 42; ++y)
    for (x = 0; x < 56; ++x)
      ap_map_set(&app.map, x, y,
                 (y < 6 && x >= 5 && x < 45) ||
                     (y >= 6 && y < 13 && x >= 5 && x < 9) ||
                     (y >= 13 && y < 24 && x < 10) ||
                     (y >= 6 && y < 17 && x >= 40 && x < 44) ||
                     (y >= 17 && x >= 36));
  accelerator('M', 0);
  CHECK(app.mesh_mode);
  click(1, 6.6, 18.3);
  click(2, 37, 34);
  check_corridor_route(0);
  click(2, 38.2, 36.7);
  check_corridor_route(0);
  click(1, 38.2, 36.7);
  click(2, 6.6, 18.3);
  check_corridor_route(1);
  accelerator('M', 0);
  CHECK(!app.mesh_mode && !app.route.count);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  puts("Starting Windows UI integration checks");
  CHECK(app_create(GetModuleHandleW(NULL), SW_SHOWNORMAL));
  pump();
  check_baseline();
  CHECK(app.zoom == 1);
  test_rectangles();
  puts("PASS: rectangle previews, deferred fill/erase, cancellation, undo/redo "
       "and auto-scroll");
  test_fill_view();
  puts("PASS: fill baseline, minimum zoom, resize, Ctrl+H and large scroll "
       "offsets");
  test_new_window();
  puts("PASS: New window sizing, square cells, work-area fit, restore and "
       "cancellation");
  test_editor();
  puts("PASS: editing, undo/redo, zoom, scrollbars and New");
  test_mesh();
  puts("PASS: mesh, endpoints, route updates and mode invalidation");
  test_files_and_dialogs();
  puts("PASS: Unicode Open/Save/Save As, dirty prompts and About");
  test_corridor_route();
  puts("PASS: U corridor shortest length, endpoint changes and reverse route");
  test_partial_cells();
  puts("PASS: partial edge cells, exact bounds, tiny maps and routes");
  test_association();
  puts("PASS: association registration, quoting and refusal");
  ap_history_saved(&app.history);
  SendMessageW(app.window, WM_CLOSE, 0, 0);
  CHECK(!IsWindow(app.window));
  printf("PASS: %u Windows UI checks\n", checks);
  return 0;
}
