// Miles Together - Melty's files (sheet rows systems/melty_files and systems/test_mode).
//
// Contract (sheet melty_contract):
//   %LOCALAPPDATA%\MilesTogether\melty.properties   join=<address> when joining, empty when hosting
//   %LOCALAPPDATA%\MilesTogether\melty.log          "Melty join address: ", "Melty joined: ", "Melty join failed: "
//   %LOCALAPPDATA%\Melty\test\<slug>.json           present during a Melty test run; names the checks file
#include "melty.h"

#include <mutex>
#include <set>
#include <vector>

namespace mt {
namespace {

std::mutex g_log_mutex;
std::wstring g_folder;  // ...\MilesTogether\ with a trailing backslash
LARGE_INTEGER g_start = {};
LARGE_INTEGER g_frequency = {};

std::wstring Widen(const char* text) {
  std::wstring out;
  for (const char* p = text; *p; ++p) out.push_back(static_cast<wchar_t>(*p == '/' ? '\\' : *p));
  return out;
}

bool ReadWholeFile(const std::wstring& path, std::string* out) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  out->clear();
  char buffer[4096];
  DWORD got = 0;
  while (ReadFile(file, buffer, sizeof(buffer), &got, nullptr) && got > 0 && out->size() < (1u << 20)) out->append(buffer, got);
  CloseHandle(file);
  return true;
}

// Opens, appends one line and closes, so Melty always sees a complete file.
bool AppendLine(const std::wstring& path, const std::string& line) {
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const std::string text = line + "\r\n";
  DWORD written = 0;
  const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  CloseHandle(file);
  return ok && written == text.size();
}

std::string Trim(const std::string& text) {
  std::size_t begin = 0, end = text.size();
  while (begin < end && static_cast<unsigned char>(text[begin]) <= ' ') ++begin;
  while (end > begin && static_cast<unsigned char>(text[end - 1]) <= ' ') --end;
  return text.substr(begin, end - begin);
}

}  // namespace

std::uint32_t NowMs() {
  if (g_frequency.QuadPart == 0) {
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_start);
  }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<std::uint32_t>((now.QuadPart - g_start.QuadPart) * 1000 / g_frequency.QuadPart);
}

std::wstring LocalAppData() {
  wchar_t buffer[MAX_PATH];
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return L"";
  std::wstring path(buffer, length);
  if (path.back() != L'\\') path.push_back(L'\\');
  return path;
}

void Log(const char* format, ...) {
  char message[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  char line[1200];
  std::snprintf(line, sizeof(line), "[%lu %8u] %s", GetCurrentProcessId(), NowMs(), message);
  std::lock_guard<std::mutex> lock(g_log_mutex);
  if (g_folder.empty()) return;
  AppendLine(g_folder + L"MilesTogether.log", line);
}

namespace melty {
namespace {

std::string g_join;
bool g_test = false;
std::wstring g_checks_path;
std::set<std::string> g_reported;
std::mutex g_report_mutex;

std::wstring MeltyLog() { return g_folder + Widen(sheets::melty::log_file); }

// The value of a string member of a flat JSON object, with \\ and \/ unescaped.
std::string JsonString(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  std::size_t at = json.find(needle);
  if (at == std::string::npos) return "";
  at = json.find(':', at + needle.size());
  if (at == std::string::npos) return "";
  at = json.find('"', at);
  if (at == std::string::npos) return "";
  std::string out;
  for (std::size_t i = at + 1; i < json.size(); ++i) {
    const char c = json[i];
    if (c == '"') return out;
    if (c == '\\' && i + 1 < json.size()) {
      const char next = json[++i];
      out.push_back(next == 'n' ? '\n' : next == 't' ? '\t' : next);
      continue;
    }
    out.push_back(c);
  }
  return "";
}

std::wstring Utf8ToWide(const std::string& text) {
  if (text.empty()) return L"";
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

void Report(const char* check, const std::string& line) {
  if (!g_test) return;
  {
    std::lock_guard<std::mutex> lock(g_report_mutex);
    if (!g_reported.insert(check).second) return;
  }
  AppendLine(g_checks_path, line);
  Log("test check: %s", line.c_str());
}

}  // namespace

void Init() {
  const std::wstring root = LocalAppData();
  if (root.empty()) return;
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_folder = root + Widen(sheets::melty::folder) + L"\\";
    CreateDirectoryW(g_folder.c_str(), nullptr);
  }
  // Keep the diagnostic log small: start over once it passes 1 MB.
  WIN32_FILE_ATTRIBUTE_DATA info;
  const std::wstring own_log = g_folder + L"MilesTogether.log";
  if (GetFileAttributesExW(own_log.c_str(), GetFileExInfoStandard, &info) && (info.nFileSizeHigh != 0 || info.nFileSizeLow > (1u << 20))) {
    DeleteFileW(own_log.c_str());
  }

  // Empty melty.log: lines from an earlier session must not be read as this one's.
  HANDLE log = CreateFileW(MeltyLog().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (log != INVALID_HANDLE_VALUE) CloseHandle(log);

  std::string properties;
  if (ReadWholeFile(g_folder + Widen(sheets::melty::join_file), &properties)) {
    const std::string key = std::string(sheets::melty::join_key) + "=";
    std::size_t line_start = 0;
    while (line_start <= properties.size()) {
      std::size_t line_end = properties.find('\n', line_start);
      if (line_end == std::string::npos) line_end = properties.size();
      const std::string line = Trim(properties.substr(line_start, line_end - line_start));
      if (line.compare(0, key.size(), key) == 0) g_join = Trim(line.substr(key.size()));
      line_start = line_end + 1;
    }
  }

  std::string test_json;
  const std::wstring test_file = root + Widen(sheets::melty::test_folder) + L"\\" + Widen(sheets::melty::slug) + L".json";
  if (ReadWholeFile(test_file, &test_json)) {
    const std::string checks = JsonString(test_json, "checks");
    if (!checks.empty()) {
      g_checks_path = Utf8ToWide(checks);
      g_test = true;
    }
  }
  Log("%s started: %s%s%s", sheets::design::title, g_join.empty() ? "hosting" : "joining ", g_join.c_str(), g_test ? " (Melty test run)" : "");
}

const std::string& JoinAddress() { return g_join; }

void WriteHosting(const std::string& address) { AppendLine(MeltyLog(), std::string(sheets::melty::host_line) + address); }
void WriteJoined(const std::string& address) { AppendLine(MeltyLog(), std::string(sheets::melty::joined_line) + address); }
void WriteJoinFailed(const std::string& reason) { AppendLine(MeltyLog(), std::string(sheets::melty::failed_line) + reason); }

bool TestMode() { return g_test; }

bool MirrorRequested() { return GetFileAttributesW((g_folder + L"mirror.txt").c_str()) != INVALID_FILE_ATTRIBUTES; }

void ReportPass(const char* check) { Report(check, std::string("pass ") + check); }
void ReportFail(const char* check, const char* why) { Report(check, std::string("fail ") + check + ": " + why); }

}  // namespace melty
}  // namespace mt
