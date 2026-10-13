// Miles Together - loads as winmm.dll beside the game (sheet row systems/loader_proxy).
//
// The game imports winmm.dll, and Windows looks in the game's folder first, so this file
// is loaded with the game. Every winmm export is passed straight on to Windows' own
// winmm.dll in the system folder. The list of exports lives in proxy_exports.inc.
#include "common.h"

namespace {

struct Export {
  const char* name;      // null when the export has no name
  std::uint16_t ordinal; // used when it has no name
};

#define MT_PROXY_EXPORT(index, name) {#name, 0},
#define MT_PROXY_ORDINAL(index, symbol, ordinal) {nullptr, ordinal},
constexpr Export kExports[] = {
#include "proxy_exports.inc"
};
#undef MT_PROXY_EXPORT
#undef MT_PROXY_ORDINAL
static_assert(sizeof(kExports) / sizeof(kExports[0]) == MT_PROXY_EXPORT_COUNT, "export list and count disagree");

INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

}  // namespace

extern "C" {

// Filled once with Windows' own functions; read by the stubs below.
void* mt_proxy_targets[MT_PROXY_EXPORT_COUNT] = {};

BOOL CALLBACK mt_proxy_load(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH + 16];
  const UINT length = GetSystemDirectoryW(path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return TRUE;
  lstrcatW(path, L"\\winmm.dll");
  const HMODULE real = LoadLibraryW(path);
  if (!real) return TRUE;
  for (int i = 0; i < MT_PROXY_EXPORT_COUNT; ++i) {
    const char* key = kExports[i].name ? kExports[i].name : reinterpret_cast<const char*>(static_cast<std::uintptr_t>(kExports[i].ordinal));
    mt_proxy_targets[i] = reinterpret_cast<void*>(GetProcAddress(real, key));
  }
  return TRUE;
}

void mt_proxy_resolve() { InitOnceExecuteOnce(&g_once, mt_proxy_load, nullptr, nullptr); }

}  // extern "C"

// One stub per export: on first use find Windows' own function, keeping the caller's
// arguments untouched, then jump to it. Returns 0 if Windows does not have that function.
#define MT_PROXY_STUB(index, symbol)                             \
  __asm__(".text\n"                                              \
          ".globl " #symbol "\n"                                 \
          ".p2align 4\n"                                         \
          #symbol ":\n"                                          \
          "  movq mt_proxy_targets+8*" #index "(%rip), %rax\n"   \
          "  testq %rax, %rax\n"                                 \
          "  jnz 1f\n"                                           \
          "  pushq %rcx\n"                                       \
          "  pushq %rdx\n"                                       \
          "  pushq %r8\n"                                        \
          "  pushq %r9\n"                                        \
          "  subq $0x28, %rsp\n"                                 \
          "  call mt_proxy_resolve\n"                            \
          "  addq $0x28, %rsp\n"                                 \
          "  popq %r9\n"                                         \
          "  popq %r8\n"                                         \
          "  popq %rdx\n"                                        \
          "  popq %rcx\n"                                        \
          "  movq mt_proxy_targets+8*" #index "(%rip), %rax\n"   \
          "  testq %rax, %rax\n"                                 \
          "  jnz 1f\n"                                           \
          "  xorl %eax, %eax\n"                                  \
          "  ret\n"                                              \
          "1:\n"                                                 \
          "  jmp *%rax\n");

#define MT_PROXY_EXPORT(index, name) MT_PROXY_STUB(index, name)
#define MT_PROXY_ORDINAL(index, symbol, ordinal) MT_PROXY_STUB(index, symbol)
#include "proxy_exports.inc"
