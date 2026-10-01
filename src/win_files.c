#include "app.h"

#include <commctrl.h>
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

static const wchar_t filter[] =
    L"apath map data (*.amd)\0*.amd\0All files (*.*)\0*.*\0\0";

void app_error(ApResult result) {
  wchar_t text[256];
  MultiByteToWideChar(CP_UTF8, 0, ap_result_string(result), -1, text, 256);
  MessageBoxW(app.window, text, L"APath", MB_OK | MB_ICONERROR);
}

static void center_dialog(HWND dialog) {
  RECT owner, r;
  GetWindowRect(GetParent(dialog), &owner);
  GetWindowRect(dialog, &r);
  SetWindowPos(dialog, NULL,
               owner.left + (owner.right - owner.left - r.right + r.left) / 2,
               owner.top + (owner.bottom - owner.top - r.bottom + r.top) / 2, 0,
               0, SWP_NOSIZE | SWP_NOZORDER);
}

static INT_PTR CALLBACK new_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    SetWindowLongPtrW(hwnd, DWLP_USER, lp);
    SetDlgItemInt(hwnd, IDC_CELL, app.map.cell_size, FALSE);
    SetDlgItemInt(hwnd, IDC_WIDTH, app.map.pixel_width, FALSE);
    SetDlgItemInt(hwnd, IDC_HEIGHT, app.map.pixel_height, FALSE);
    SendDlgItemMessageW(hwnd, IDC_CELL, EM_SETLIMITTEXT, 5, 0);
    SendDlgItemMessageW(hwnd, IDC_WIDTH, EM_SETLIMITTEXT, 5, 0);
    SendDlgItemMessageW(hwnd, IDC_HEIGHT, EM_SETLIMITTEXT, 5, 0);
    center_dialog(hwnd);
    return TRUE;
  }
  if (msg == WM_COMMAND) {
    if (LOWORD(wp) == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    if (LOWORD(wp) == IDOK) {
      BOOL a, b, c;
      unsigned cell = GetDlgItemInt(hwnd, IDC_CELL, &a, FALSE);
      unsigned width = GetDlgItemInt(hwnd, IDC_WIDTH, &b, FALSE);
      unsigned height = GetDlgItemInt(hwnd, IDC_HEIGHT, &c, FALSE);
      ApMap *map = (ApMap *)GetWindowLongPtrW(hwnd, DWLP_USER);
      ApResult result;
      if (!a || !b || !c || !cell || !width || !height || cell > 65535 ||
          width > 65535 || height > 65535 ||
          (uint64_t)((width + cell - 1) / cell) * ((height + cell - 1) / cell) >
              AP_MAX_CELLS) {
        MessageBoxW(
            hwnd,
            L"Enter pixel values from 1 to 65535.\nThe map may contain at most "
            L"16,777,216 cells, including partial edge cells.\nThe bitmap uses "
            L"up to 2 MiB; navigation and undo need additional memory.",
            L"New Map", MB_OK | MB_ICONWARNING);
        return TRUE;
      }
      result = ap_map_create_pixels(map, cell, width, height);
      if (result != AP_OK) {
        app_error(result);
        return TRUE;
      }
      EndDialog(hwnd, IDOK);
      return TRUE;
    }
  }
  return FALSE;
}

INT_PTR CALLBACK app_about_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    center_dialog(hwnd);
    return TRUE;
  }
  if (msg == WM_COMMAND && (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL)) {
    EndDialog(hwnd, LOWORD(wp));
    return TRUE;
  }
  if (msg == WM_NOTIFY) {
    NMHDR *notice = (NMHDR *)lp;
    if (notice->idFrom == IDC_URL &&
        (notice->code == NM_CLICK || notice->code == NM_RETURN)) {
      if ((INT_PTR)ShellExecuteW(hwnd, L"open",
                                 L"https://github.com/halfstay/apath", NULL,
                                 NULL, SW_SHOWNORMAL) <= 32)
        MessageBoxW(hwnd, L"Unable to open the default browser.", L"APath",
                    MB_OK | MB_ICONERROR);
      return TRUE;
    }
  }
  return FALSE;
}

static int choose_file(wchar_t *filename, int save) {
  OPENFILENAMEW ofn;
  ZeroMemory(&ofn, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = app.window;
  ofn.lpstrFilter = filter;
  ofn.nFilterIndex = 1;
  ofn.lpstrFile = filename;
  ofn.nMaxFile = AP_PATH_CAP;
  ofn.lpstrDefExt = L"amd";
  ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
              (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if (save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))
    return 1;
  if (CommDlgExtendedError())
    MessageBoxW(app.window, L"Unable to display the file dialog.", L"APath",
                MB_OK | MB_ICONERROR);
  return 0;
}

static void replace_document(ApMap *fresh, const wchar_t *filename) {
  app_leave_mesh();
  ap_map_free(&app.map);
  app.map = *fresh;
  ZeroMemory(fresh, sizeof(*fresh));
  ap_history_free(&app.history);
  ap_history_init(&app.history, AP_HISTORY_BUDGET);
  lstrcpynW(app.filename, filename ? filename : L"", AP_PATH_CAP);
  app_reset_view();
}

int app_new(void) {
  ApMap fresh = {0};
  if (DialogBoxParamW(app.instance, MAKEINTRESOURCEW(IDD_NEW), app.window,
                      new_proc, (LPARAM)&fresh) != IDOK)
    return 0;
  if (!app_confirm_save()) {
    ap_map_free(&fresh);
    return 0;
  }
  replace_document(&fresh, NULL);
  return 1;
}

int app_open(const wchar_t *filename) {
  wchar_t path[AP_PATH_CAP] = L"";
  ApMap fresh = {0};
  ApResult result;
  FILE *file;
  if (filename)
    lstrcpynW(path, filename, AP_PATH_CAP);
  else if (!choose_file(path, 0))
    return 0;
  file = _wfopen(path, L"rb");
  if (!file) {
    app_error(AP_IO);
    return 0;
  }
  result = ap_map_read(file, &fresh);
  if (fclose(file) && result == AP_OK)
    result = AP_IO;
  if (result != AP_OK) {
    ap_map_free(&fresh);
    app_error(result);
    return 0;
  }
  if (!app_confirm_save()) {
    ap_map_free(&fresh);
    return 0;
  }
  replace_document(&fresh, path);
  return 1;
}

static ApResult save_atomic(const wchar_t *filename) {
  wchar_t full[AP_PATH_CAP], temp[AP_PATH_CAP];
  HANDLE handle = INVALID_HANDLE_VALUE;
  FILE *file;
  int descriptor;
  unsigned attempt;
  DWORD length = GetFullPathNameW(filename, AP_PATH_CAP, full, NULL);
  ApResult result;
  if (!length || length >= AP_PATH_CAP - 64)
    return AP_IO;
  for (attempt = 0; attempt < 100; ++attempt) {
    _snwprintf(temp, AP_PATH_CAP, L"%ls.%lu.%lu.%u.tmp", full,
               (unsigned long)GetCurrentProcessId(),
               (unsigned long)GetTickCount(), attempt);
    handle = CreateFileW(temp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (handle != INVALID_HANDLE_VALUE)
      break;
    if (GetLastError() != ERROR_FILE_EXISTS &&
        GetLastError() != ERROR_ALREADY_EXISTS)
      return AP_IO;
  }
  if (handle == INVALID_HANDLE_VALUE)
    return AP_IO;
  descriptor = _open_osfhandle((intptr_t)handle, _O_WRONLY | _O_BINARY);
  if (descriptor < 0) {
    CloseHandle(handle);
    DeleteFileW(temp);
    return AP_IO;
  }
  file = _fdopen(descriptor, "wb");
  if (!file) {
    _close(descriptor);
    DeleteFileW(temp);
    return AP_IO;
  }
  result = ap_map_write(file, &app.map);
  if (fflush(file) || _commit(descriptor))
    result = AP_IO;
  if (fclose(file))
    result = AP_IO;
  if (result == AP_OK &&
      !MoveFileExW(temp, full,
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    result = AP_IO;
  if (result != AP_OK)
    DeleteFileW(temp);
  return result;
}

int app_save(int save_as) {
  wchar_t path[AP_PATH_CAP];
  ApResult result;
  app_end_gesture(1);
  lstrcpynW(path, app.filename, AP_PATH_CAP);
  if ((save_as || !path[0]) && !choose_file(path, 1))
    return 0;
  result = save_atomic(path);
  if (result != AP_OK) {
    app_error(result);
    return 0;
  }
  lstrcpynW(app.filename, path, AP_PATH_CAP);
  ap_history_saved(&app.history);
  app_refresh();
  return 1;
}

int app_confirm_save(void) {
  int choice;
  app_end_gesture(1);
  if (!ap_history_dirty(&app.history))
    return 1;
  choice = MessageBoxW(app.window, L"Save changes to the current map?",
                       L"APath", MB_YESNOCANCEL | MB_ICONQUESTION);
  if (choice == IDCANCEL)
    return 0;
  return choice != IDYES || app_save(0);
}

static int read_registry(HKEY root, const wchar_t *key, const wchar_t *name,
                         wchar_t *value, DWORD capacity) {
  DWORD bytes = capacity * sizeof(wchar_t);
  value[0] = 0;
  return RegGetValueW(root, key, name, RRF_RT_REG_SZ, NULL, value, &bytes) ==
             ERROR_SUCCESS &&
         value[0];
}

static int write_registry(const wchar_t *name, const wchar_t *value) {
  HKEY key;
  LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, name, 0, NULL, 0,
                                   KEY_SET_VALUE, NULL, &key, NULL);
  if (status != ERROR_SUCCESS)
    return 0;
  status = RegSetValueExW(key, NULL, 0, REG_SZ, (const BYTE *)value,
                          (DWORD)((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return status == ERROR_SUCCESS;
}

static int command_is_self(const wchar_t *command, const wchar_t *exe) {
  int count, same = 0;
  wchar_t **args = CommandLineToArgvW(command, &count);
  wchar_t full[AP_PATH_CAP];
  if (args) {
    if (count) {
      DWORD length = GetFullPathNameW(args[0], AP_PATH_CAP, full, NULL);
      same = length && length < AP_PATH_CAP && !lstrcmpiW(full, exe);
    }
    LocalFree(args);
  }
  return same;
}

void app_associate(void) {
  wchar_t existing[512], choice[512], exe[AP_PATH_CAP],
      command[AP_PATH_CAP + 16], icon[AP_PATH_CAP + 8];
  wchar_t key[640], registered[AP_PATH_CAP + 16];
  int protected_choice =
      read_registry(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\F"
                    L"ileExts\\.amd\\UserChoice",
                    L"ProgId", choice, 512);
  int exists = protected_choice ||
               read_registry(HKEY_CURRENT_USER, L"Software\\Classes\\.amd",
                             NULL, existing, 512) ||
               read_registry(HKEY_CLASSES_ROOT, L".amd", NULL, existing, 512);
  DWORD length;
  length = GetModuleFileNameW(NULL, exe, AP_PATH_CAP);
  if (!length || length >= AP_PATH_CAP) {
    app_error(AP_IO);
    return;
  }
  _snwprintf(command, AP_PATH_CAP + 16, L"\"%ls\" \"%%1\"", exe);
  if (exists) {
    const wchar_t *handler = protected_choice ? choice : existing;
    int found;
    _snwprintf(key, 640, L"Software\\Classes\\%ls\\shell\\open\\command",
               handler);
    found = read_registry(HKEY_CURRENT_USER, key, NULL, registered,
                          AP_PATH_CAP + 16);
    if (!found) {
      _snwprintf(key, 640, L"%ls\\shell\\open\\command", handler);
      found = read_registry(HKEY_CLASSES_ROOT, key, NULL, registered,
                            AP_PATH_CAP + 16);
    }
    if (found && command_is_self(registered, exe))
      return;
    if (MessageBoxW(app.window,
                    L"The .amd extension already has a file "
                    L"association.\nRegister APath as its handler?",
                    L"APath File Association",
                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
      return;
  }
  _snwprintf(icon, AP_PATH_CAP + 8, L"\"%ls\",0", exe);
  if (!write_registry(L"Software\\Classes\\APath.Map", L"apath map data") ||
      !write_registry(L"Software\\Classes\\APath.Map\\shell\\open\\command",
                      command) ||
      !write_registry(L"Software\\Classes\\APath.Map\\DefaultIcon", icon) ||
      !write_registry(L"Software\\Classes\\.amd", L"APath.Map")) {
    MessageBoxW(app.window,
                L"Unable to register the .amd file association for this user.",
                L"APath", MB_OK | MB_ICONWARNING);
    return;
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
  if (protected_choice && lstrcmpiW(choice, L"APath.Map")) {
    if (MessageBoxW(
            app.window,
            L"APath is registered. Windows protects your existing default "
            L"application.\nOpen Default Apps to choose APath for .amd files?",
            L"APath File Association", MB_YESNO | MB_ICONINFORMATION) == IDYES)
      ShellExecuteW(app.window, L"open", L"control.exe",
                    L"/name Microsoft.DefaultPrograms", NULL, SW_SHOWNORMAL);
  }
}
