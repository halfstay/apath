#ifndef AP_APP_H
#define AP_APP_H
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include "core/ap_map.h"
#include "core/ap_history.h"
#include "core/ap_nav.h"
#include "resource.h"

#define AP_PATH_CAP 32768
#define AP_HISTORY_BUDGET ((size_t)64 * 1024 * 1024)

typedef struct {
    HINSTANCE instance;
    HWND window;
    ApMap map;
    ApHistory history;
    ApMesh mesh;
    ApSearch search;
    ApPath route;
    wchar_t filename[AP_PATH_CAP];
    double cell_x, cell_y, zoom;
    int view_width, view_height, scroll_busy, wheel_remainder;
    int64_t scroll_x, scroll_y;
    int mesh_mode, have_start, have_end;
    ApPoint start, end;
    ApResult route_result;
    int gesture, dragging;
    POINT down_screen;
    ApPoint down_world, last_world;
} App;

extern App app;
int app_create(HINSTANCE instance, int show);
LRESULT CALLBACK app_window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void app_refresh(void);
void app_scrollbars(void);
void app_reset_view(void);
void app_leave_mesh(void);
void app_end_gesture(int cancel);
void app_error(ApResult result);
int app_new(void);
int app_open(const wchar_t *filename);
int app_save(int save_as);
int app_confirm_save(void);
void app_associate(void);
INT_PTR CALLBACK app_about_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

#endif
