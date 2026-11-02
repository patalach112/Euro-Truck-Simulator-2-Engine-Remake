#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include <cstring>
#include <iostream>

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

void print_error(const char* action, DWORD error) {
    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<char*>(&message), 0, nullptr);

    std::cout << action << " failed (Windows error " << error << ")";
    if (length != 0 && message != nullptr) {
        while (*message != '\0' && (message[std::strlen(message) - 1] == '\r' ||
                                     message[std::strlen(message) - 1] == '\n')) {
            message[std::strlen(message) - 1] = '\0';
        }
        std::cout << ": " << message;
    }
    std::cout << '\n';
    if (message != nullptr) {
        LocalFree(message);
    }
}

DWORD find_process(const char* name) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) {
        print_error("Taking the process snapshot", GetLastError());
        return 0;
    }

    PROCESSENTRY32A entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstA(snapshot.get(), &entry)) {
        print_error("Reading the process list", GetLastError());
        return 0;
    }

    do {
        if (_stricmp(entry.szExeFile, name) == 0) {
            return entry.th32ProcessID;
        }
    } while (Process32NextA(snapshot.get(), &entry));

    const DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_FILES) {
        print_error("Reading the process list", error);
    } else {
        std::cout << "Could not find a running process named " << name << ".\n";
    }
    return 0;
}

bool is_absolute_path(const char* path) {
    const size_t length = std::strlen(path);
    const auto is_separator = [](char c) { return c == '\\' || c == '/'; };
    return (length >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') ||
                            (path[0] >= 'a' && path[0] <= 'z')) &&
            path[1] == ':' && is_separator(path[2])) ||
           (length >= 2 && is_separator(path[0]) && is_separator(path[1]));
}

bool inject(HANDLE process, const char* dll_path) {
    const SIZE_T size = std::strlen(dll_path) + 1;
    void* remote_path = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote_path == nullptr) {
        print_error("Allocating memory in the target process", GetLastError());
        return false;
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, dll_path, size, &written)) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error("Writing the DLL path", error);
        return false;
    }
    if (written != size) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        std::cout << "Only part of the DLL path was written to the target process.\n";
        return false;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error("Finding kernel32.dll", error);
        return false;
    }

    FARPROC load_library = GetProcAddress(kernel32, "LoadLibraryA");
    if (load_library == nullptr) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error("Finding LoadLibraryA", error);
        return false;
    }

    Handle thread(CreateRemoteThread(process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(load_library), remote_path, 0, nullptr));
    if (!thread.valid()) {
        const DWORD error = GetLastError();
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        print_error("Starting the remote thread", error);
        return false;
    }

    if (WaitForSingleObject(thread.get(), INFINITE) != WAIT_OBJECT_0) {
        const DWORD error = GetLastError();
        // The thread may still be using the path, so leave its allocation alone.
        print_error("Waiting for the remote thread", error);
        return false;
    }

    DWORD result = 0;
    const BOOL got_result = GetExitCodeThread(thread.get(), &result);
    const DWORD result_error = got_result ? ERROR_SUCCESS : GetLastError();
    if (!VirtualFreeEx(process, remote_path, 0, MEM_RELEASE)) {
        print_error("Releasing the remote path", GetLastError());
    }
    if (!got_result) {
        print_error("Reading the remote thread result", result_error);
        return false;
    }
    if (result == 0) {
        std::cout << "LoadLibraryA returned a zero result; the DLL may not have loaded.\n";
        return false;
    }

    std::cout << "LoadLibraryA completed successfully. Check the debug output for the DLL message.\n";
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "Usage: Injector.exe <process.exe> <absolute-path-to-renderer_hook.dll>\n";
        return 1;
    }
    if (!is_absolute_path(argv[2])) {
        std::cout << "Please provide an absolute path to the DLL.\n";
        return 1;
    }

    const DWORD attributes = GetFileAttributesA(argv[2]);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        print_error("Checking the DLL path", GetLastError());
        return 1;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        std::cout << "The DLL path points to a directory.\n";
        return 1;
    }

    const DWORD pid = find_process(argv[1]);
    if (pid == 0) {
        return 1;
    }

    Handle process(OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid));
    if (!process.valid()) {
        print_error("Opening the target process", GetLastError());
        return 1;
    }

    BOOL wow64 = FALSE;
    if (!IsWow64Process(process.get(), &wow64)) {
        print_error("Checking the target architecture", GetLastError());
        return 1;
    }
    if (wow64) {
        std::cout << "The target is a 32-bit process; this injector requires an x64 target.\n";
        return 1;
    }

    std::cout << "Loading the DLL into " << argv[1] << " (PID " << pid << ").\n";
    return inject(process.get(), argv[2]) ? 0 : 1;
}
