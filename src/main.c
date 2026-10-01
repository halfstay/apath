#include "app.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command,
                    int show) {
  MSG message;
  HACCEL accelerators;
  wchar_t **arguments;
  int count, status;
  (void)previous;
  (void)command;
  if (!app_create(instance, show)) {
    MessageBoxW(NULL, L"Unable to initialize APath.", L"APath",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  app_associate();
  arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (arguments) {
    if (count == 2)
      app_open(arguments[1]);
    else if (count > 2)
      MessageBoxW(app.window, L"Open one .amd file at a time.", L"APath",
                  MB_OK | MB_ICONINFORMATION);
    LocalFree(arguments);
  }
  accelerators = LoadAcceleratorsW(instance, MAKEINTRESOURCEW(IDR_ACCEL));
  while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
    if (!TranslateAcceleratorW(app.window, accelerators, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  return status < 0 ? 1 : (int)message.wParam;
}
