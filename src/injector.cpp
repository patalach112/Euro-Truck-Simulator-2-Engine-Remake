#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <cwchar>
#include <iostream>
#include <string>

namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~Handle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HANDLE get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ != nullptr && value_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE value_;
};

void print_error(const wchar_t* action, DWORD error) {
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        reinterpret_cast<wchar_t*>(&message), 0, nullptr);

    std::wcout << action << L" failed (Windows error " << error << L")";
    if (length != 0 && message != nullptr) {
        while (*message != L'\0' && (message[std::wcslen(message) - 1] == L'\r' ||
                                     message[std::wcslen(message) - 1] == L'\n')) {
            message[std::wcslen(message) - 1] = L'\0';
        }
        std::wcout << L": " << message;
    }
    std::wcout << '\n';
    if (message != nullptr) {
        LocalFree(message);
    }
}

DWORD find_process(const wchar_t* name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        print_error(L"Taking the process snapshot", GetLastError());
        return 0;
    }

    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32First(snapshot.get(), &entry)) {
        print_error(L"Reading the process list", GetLastError());
        return 0;
    }

    do {
        if (_wcsicmp(entry.szExeFile, name) == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32Next(snapshot.get(), &entry));

    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        print_error(L"Reading the process list", error);
    } else {
        std::wcout << L"Could not find a running process named " << name << L".\n";
    }
    return 0;
}

bool to_wide(const char* text, std::wstring& result) {
    const int length = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (length == 0) {
        return false;
    }
    result.resize(static_cast<std::wstring::size_type>(length));
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text, -1, result.data(), length) == 0) {
        return false;
    }
    result.pop_back();
    return true;
}

bool is_absolute_path(const wchar_t* path) {
    const size_t length = std::wcslen(path);
    const auto is_separator = [](wchar_t c) { return c == L'\\' || c == L'/'; };
    return (length >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
                            (path[0] >= L'a' && path[0] <= L'z')) &&
            path[1] == L':' && is_separator(path[2])) ||
           (length >= 2 && is_separator(path[0]) && is_separator(path[1]));
}

bool inject(HANDLE process, const wchar_t* dll_path) {
    const SIZE_T size = (std::wcslen(dll_path) + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        print_error(L"Allocating memory in the target process", GetLastError());
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, dll_path, size, &written)) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error(L"Writing the DLL path", error);
        return false;
    }
    if (written != size) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        std::wcout << L"Only part of the DLL path was written to the target process.\n";
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32 == nullptr) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error(L"Finding kernel32.dll", error);
        return false;
    }

    FARPROC load_library = GetProcAddress(kernel32, "LoadLibraryW");
    if (load_library == nullptr) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error(L"Finding LoadLibraryW", error);
        return false;
    }

    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr));
    if (!thread.valid()) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error(L"Starting the remote thread", error);
        return false;
    }

    if (WaitForSingleObject(thread.get(), INFINITE) != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        // The thread may still be using the path, so leave its allocation alone.
        print_error(L"Waiting for the remote thread", error);
        return false;
    }

    DWORD result = 0;
    const BOOL got_result = GetExitCodeThread(thread.get(), &result);
    const DWORD result_error = got_result ? ERROR_SUCCESS : GetLastError();
    if (!VirtualFreeEx(process, remote_path, 0, MEM_RELEASE)) {
        print_error(L"Releasing the remote path", GetLastError());
    }
    if (!got_result) {
        print_error(L"Reading the remote thread result", result_error);
        return false;
    }
    if (result == 0) {
        std::wcout << L"LoadLibraryW returned a zero result; the DLL may not have loaded.\n";
        return false;
    }

    std::wcout << L"LoadLibraryW completed successfully. Check the debug output for the DLL message.\n";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::wcout << L"Usage: Injector.exe <process.exe> <absolute-path-to-renderer_hook.dll>\n";
        return 1;
    }
    std::wstring process_name;
    std::wstring dll_path;
    if (!to_wide(argv[1], process_name) || !to_wide(argv[2], dll_path)) {
        print_error(L"Converting the command-line arguments", GetLastError());
        return 1;
    }
    if (!is_absolute_path(dll_path.c_str())) {
        std::wcout << L"Please provide an absolute path to the DLL.\n";
        return 1;
    }

    const DWORD attributes = GetFileAttributesW(dll_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        print_error(L"Checking the DLL path", GetLastError());
        return 1;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        std::wcout << L"The DLL path points to a directory.\n";
        return 1;
    }

    const DWORD pid = find_process(process_name.c_str());
    if (pid == 0) {
        return 1;
    }

    Handle process(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if (!process.valid()) {
        print_error(L"Opening the target process", GetLastError());
        return 1;
    }

    BOOL wow64 = FALSE;
    if (!IsWow64Process(process.get(), &wow64)) {
        print_error(L"Checking the target architecture", GetLastError());
        return 1;
    }
    if (wow64) {
        std::wcout << L"The target is a 32-bit process; this injector requires an x64 target.\n";
        return 1;
    }

    std::wcout << L"Loading the DLL into " << process_name << L" (PID " << pid << L").\n";
    return inject(process.get(), dll_path.c_str()) ? 0 : 1;
}
