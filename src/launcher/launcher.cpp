// Launch.exe - one-click launcher (Windows 11 x64, no console, no admin rights, no installation).
//
//   1. starts bin\wb_server.exe hidden (loopback only, free port, --auto-exit)
//   2. reads the chosen port from the server's stdout pipe
//   3. waits until GET /api/health answers
//   4. opens the UI in an Edge "app" window (falls back to the default browser) and exits
//
// The server shuts itself down a few seconds after the window is closed (heartbeat watchdog).
//
// Test hooks (used by tests/windows_smoke.sh under Wine, not needed by users):
//   WB_URL_FILE=<path>  write the UI URL to this file
//   WB_NO_BROWSER=1     do not open a browser
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void fail(const std::wstring& message) {
  MessageBoxW(nullptr, message.c_str(), L"Unified Engineering Workbench", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

std::wstring exeDirectory() {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof buf / sizeof buf[0]));
  std::wstring p(buf, n);
  size_t slash = p.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

bool fileExists(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

long long unixEpochMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return static_cast<long long>(u.QuadPart / 10000ULL) - 11644473600000LL;
}

bool healthOk(int port) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return false;
  DWORD timeoutMs = 1500;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof timeoutMs);
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof timeoutMs);
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bool ok = false;
  if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
    std::string req = "GET /api/health HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nConnection: close\r\n\r\n";
    if (send(s, req.data(), static_cast<int>(req.size()), 0) > 0) {
      // The server may send headers and body in separate segments: read until it closes the connection.
      std::string resp;
      char buf[1024];
      int n;
      while (resp.size() < 8192 && (n = recv(s, buf, sizeof buf, 0)) > 0) resp.append(buf, static_cast<size_t>(n));
      ok = resp.find(" 200 ") != std::string::npos && resp.find("\"status\":\"ok\"") != std::string::npos;
    }
  }
  closesocket(s);
  return ok;
}

std::wstring findEdge() {
  const wchar_t* keys[] = {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
                           L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe"};
  for (const wchar_t* k : keys) {
    HKEY h;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k, 0, KEY_READ, &h) == ERROR_SUCCESS) {
      wchar_t buf[MAX_PATH * 2];
      DWORD sz = sizeof buf, type = 0;
      LONG r = RegQueryValueExW(h, nullptr, &type, nullptr, reinterpret_cast<LPBYTE>(buf), &sz);
      RegCloseKey(h);
      if (r == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        std::wstring p(buf);
        if (fileExists(p)) return p;
      }
    }
  }
  const wchar_t* common[] = {L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
                             L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe"};
  for (const wchar_t* c : common)
    if (fileExists(c)) return c;
  return L"";
}

bool openUi(const std::wstring& url) {
  std::wstring edge = findEdge();
  if (!edge.empty()) {
    std::wstring cmd = L"\"" + edge + L"\" --app=" + url + L" --no-first-run --no-default-browser-check";
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    std::memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    std::memset(&pi, 0, sizeof pi);
    std::wstring mutableCmd = cmd;
    if (CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
      return true;
    }
  }
  return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  const long long launchMs = unixEpochMs();
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fail(L"Windows networking (Winsock) could not be started."); return 1; }

  const std::wstring dir = exeDirectory();
  const std::wstring server = dir + L"\\bin\\wb_server.exe";
  if (!fileExists(server)) {
    fail(L"Cannot find bin\\wb_server.exe next to Launch.exe.\n\nExtract the WHOLE ZIP file to a normal folder (right-click > Extract All) "
         L"and start Launch.exe from there, not from inside the ZIP viewer.");
    return 2;
  }

  // Pipe for the server's stdout (it prints the port once, then stays quiet).
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;
  HANDLE rd = nullptr, wr = nullptr;
  if (!CreatePipe(&rd, &wr, &sa, 0)) { fail(L"Could not create a pipe to the backend process."); return 3; }
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si;
  std::memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdOutput = wr;
  si.hStdError = wr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi;
  std::memset(&pi, 0, sizeof pi);
  std::wstring cmd = L"\"" + server + L"\" --port 0 --auto-exit --launch-epoch-ms " + std::to_wstring(launchMs);
  if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
    CloseHandle(rd);
    CloseHandle(wr);
    fail(L"The backend (bin\\wb_server.exe) could not be started. Error code " + std::to_wstring(GetLastError()) +
         L".\n\nIf your security software blocked it, allow Launch.exe and wb_server.exe and try again.");
    return 4;
  }
  CloseHandle(wr);
  CloseHandle(pi.hThread);

  // Read the port from the pipe (up to 20 s).
  std::string out;
  int port = 0;
  const DWORD startTick = GetTickCount();
  while (port == 0 && GetTickCount() - startTick < 20000) {
    DWORD avail = 0;
    if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
      char buf[512];
      DWORD got = 0;
      if (ReadFile(rd, buf, sizeof buf - 1, &got, nullptr) && got > 0) out.append(buf, got);
      size_t p = out.find("127.0.0.1:");
      if (p != std::string::npos) {
        int v = std::atoi(out.c_str() + p + 10);
        if (v > 0 && v < 65536) port = v;
      }
    } else if (WaitForSingleObject(pi.hProcess, 25) == WAIT_OBJECT_0) {
      DWORD code = 0;
      GetExitCodeProcess(pi.hProcess, &code);
      std::wstring why = L"The backend stopped right after starting (exit code " + std::to_wstring(code) + L").";
      if (code == 3) why += L"\n\nThe 'web' folder is missing. Extract the whole ZIP again.";
      if (code == 4) why += L"\n\nNo local network port could be opened (security software or a network policy may be blocking it).";
      CloseHandle(rd);
      CloseHandle(pi.hProcess);
      fail(why);
      return 5;
    }
  }
  CloseHandle(rd);
  if (port == 0) { fail(L"The backend did not report a port within 20 seconds."); TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); return 6; }

  bool ready = false;
  for (int i = 0; i < 100 && !ready; ++i) {
    ready = healthOk(port);
    if (!ready) Sleep(100);
  }
  if (!ready) { fail(L"The backend started but did not answer on its local port."); TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); return 7; }
  CloseHandle(pi.hProcess);

  std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
  wchar_t urlFile[MAX_PATH * 2];
  if (GetEnvironmentVariableW(L"WB_URL_FILE", urlFile, static_cast<DWORD>(sizeof urlFile / sizeof urlFile[0])) > 0) {
    FILE* f = _wfopen(urlFile, L"wb");
    if (f) { std::string narrow(url.begin(), url.end()); std::fwrite(narrow.data(), 1, narrow.size(), f); std::fclose(f); }
  }
  if (GetEnvironmentVariableW(L"WB_NO_BROWSER", nullptr, 0) == 0) {
    if (!openUi(url)) fail(L"The backend is running but no browser could be opened.\n\nOpen this address manually:\n" + url);
  }
  WSACleanup();
  return 0;
}
