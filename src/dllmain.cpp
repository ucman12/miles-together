// Miles Together - entry point.
#include "common.h"
#include "game.h"
#include "melty.h"
#include "net.h"

namespace {

// Other programs in the game's folder can load this file too; only the game itself gets the mod.
bool InGame() {
  wchar_t path[MAX_PATH];
  const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return false;
  const wchar_t* name = path;
  for (const wchar_t* p = path; *p; ++p) {
    if (*p == L'\\' || *p == L'/') name = p + 1;
  }
  wchar_t expected[64];
  int i = 0;
  for (const char* p = sheets::compatibility::executable; *p && i < 63; ++p) expected[i++] = static_cast<wchar_t>(*p);
  expected[i] = 0;
  return lstrcmpiW(name, expected) == 0;
}

DWORD WINAPI Start(LPVOID) {
  mt::melty::Init();
  if (mt::game::Install()) mt::net::Start();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    if (InGame()) {
      const HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr);
      if (thread) CloseHandle(thread);
    }
  }
  return TRUE;
}
