#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <winver.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

std::string hex(const unsigned char* bytes, DWORD size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(static_cast<std::size_t>(size) * 2);
    for (DWORD i = 0; i < size; ++i) {
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 0x0f]);
    }
    return result;
}

bool sha256_file(const std::wstring& path, std::string& digest) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0, returned = 0, digest_size = 0;
    std::vector<unsigned char> object;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size), sizeof(digest_size), &returned, 0) >= 0;
    if (ok && digest_size == 32) {
        object.resize(object_size);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0;
    } else ok = false;
    unsigned char buffer[64 * 1024];
    while (ok) {
        DWORD read = 0;
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { ok = false; break; }
        if (read == 0) break;
        ok = BCryptHashData(hash, buffer, read, 0) >= 0;
    }
    unsigned char output[32]{};
    if (ok) ok = BCryptFinishHash(hash, output, sizeof(output), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (ok) digest = hex(output, sizeof(output));
    return ok;
}

bool sha256_remote(HANDLE process, const unsigned char* address, DWORD size, std::string& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0, returned = 0, digest_size = 0;
    std::vector<unsigned char> object;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &returned, 0) >= 0 &&
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digest_size), sizeof(digest_size), &returned, 0) >= 0;
    if (ok && digest_size == 32) {
        object.resize(object_size);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0;
    } else ok = false;
    unsigned char buffer[64 * 1024];
    DWORD offset = 0;
    while (ok && offset < size) {
        const DWORD request = std::min<DWORD>(sizeof(buffer), size - offset);
        SIZE_T read = 0;
        if (!ReadProcessMemory(process, address + offset, buffer, request, &read) || read != request) { ok = false; break; }
        ok = BCryptHashData(hash, buffer, request, 0) >= 0;
        offset += request;
    }
    unsigned char output[32]{};
    if (ok) ok = BCryptFinishHash(hash, output, sizeof(output), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (ok) digest = hex(output, sizeof(output));
    return ok;
}

bool is_i386_image(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    bool valid = ReadFile(file, &dos, sizeof(dos), &read, nullptr) && read == sizeof(dos) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 16 * 1024 * 1024;
    if (valid && SetFilePointer(file, dos.e_lfanew, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
        DWORD signature = 0;
        IMAGE_FILE_HEADER header{};
        valid = ReadFile(file, &signature, sizeof(signature), &read, nullptr) && read == sizeof(signature) && signature == IMAGE_NT_SIGNATURE &&
            ReadFile(file, &header, sizeof(header), &read, nullptr) && read == sizeof(header) && header.Machine == IMAGE_FILE_MACHINE_I386;
    } else valid = false;
    CloseHandle(file);
    return valid;
}

std::wstring version_string(DWORD ms, DWORD ls) {
    return std::to_wstring(HIWORD(ms)) + L"." + std::to_wstring(LOWORD(ms)) + L"." +
        std::to_wstring(HIWORD(ls)) + L"." + std::to_wstring(LOWORD(ls));
}

bool get_file_version(const std::wstring& path, std::wstring& product_name,
    std::wstring& file_version_raw, std::wstring& product_version_raw,
    std::wstring& file_version_string, std::wstring& product_version_string) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (size == 0 || size > 4 * 1024 * 1024) return false;
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return false;

    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixed_size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixed_size) || !fixed ||
        fixed_size < sizeof(*fixed) || fixed->dwSignature != VS_FFI_SIGNATURE) return false;
    file_version_raw = version_string(fixed->dwFileVersionMS, fixed->dwFileVersionLS);
    product_version_raw = version_string(fixed->dwProductVersionMS, fixed->dwProductVersionLS);

    struct Translation { WORD language; WORD code_page; };
    Translation* translations = nullptr;
    UINT translation_size = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &translation_size) &&
        translations && translation_size >= sizeof(Translation)) {
        wchar_t query[96]{};
        swprintf_s(query, _countof(query), L"\\StringFileInfo\\%04x%04x\\ProductName", translations[0].language, translations[0].code_page);
        wchar_t* value = nullptr;
        UINT value_size = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_size) && value && value_size > 1)
            product_name.assign(value, value_size - 1);
        swprintf_s(query, _countof(query), L"\\StringFileInfo\\%04x%04x\\FileVersion", translations[0].language, translations[0].code_page);
        value = nullptr;
        value_size = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_size) && value && value_size > 1)
            file_version_string.assign(value, value_size - 1);
        swprintf_s(query, _countof(query), L"\\StringFileInfo\\%04x%04x\\ProductVersion", translations[0].language, translations[0].code_page);
        value = nullptr;
        value_size = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_size) && value && value_size > 1)
            product_version_string.assign(value, value_size - 1);
    }
    return !product_name.empty() && !file_version_raw.empty() && !product_version_raw.empty() &&
        !file_version_string.empty() && !product_version_string.empty();
}

bool verify_launcher_parent(DWORD pid, std::wstring& reason) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { reason = L"Could not inspect the process parent."; return false; }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD parent_pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) { parent_pid = entry.th32ParentProcessID; break; }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (parent_pid == 0) { reason = L"Could not identify the game's launcher parent."; return false; }

    HANDLE parent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent_pid);
    if (!parent) { reason = L"Could not query the game's launcher parent."; return false; }
    std::vector<wchar_t> path_buffer(32768);
    DWORD path_size = static_cast<DWORD>(path_buffer.size());
    const bool got_path = QueryFullProcessImageNameW(parent, 0, path_buffer.data(), &path_size) != FALSE;
    CloseHandle(parent);
    if (!got_path) { reason = L"Could not read the launcher's executable path."; return false; }
    const std::wstring expected_path = L"E:\\SteamLibrary\\steamapps\\common\\Plants Vs Zombies\\PlantsVsZombies.exe";
    std::wstring actual_path(path_buffer.data(), path_size);
    if (lower(actual_path) != lower(expected_path)) { reason = L"The game was not started by the verified Steam game executable."; return false; }

    std::wstring product_name, file_raw, product_raw, file_string, product_string;
    std::string digest;
    if (!get_file_version(actual_path, product_name, file_raw, product_raw, file_string, product_string) ||
        file_raw != L"1.2.0.1095" || product_raw != L"1.2.0.1095" ||
        file_string != L"1.2.0.1096" || product_string != L"1.2.0.1096" ||
        !sha256_file(actual_path, digest) || digest != "868f8e2bab0d6a7ef8afc4c5960c608eccef82bd086bd6e0c0e2670199a5ca45") {
        reason = L"Launcher version or SHA-256 does not match the verified Steam executable.";
        return false;
    }
    return true;
}

bool read_process_exact(HANDLE process, const void* remote, void* local, SIZE_T size) {
    SIZE_T read = 0;
    return ReadProcessMemory(process, remote, local, size, &read) && read == size;
}

bool is_supported_process(DWORD pid, std::wstring& image_path, std::wstring& reason, bool& already_attached) {
    already_attached = false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) { reason = L"Could not read target process identity."; return false; }
    std::vector<wchar_t> path(32768);
    DWORD path_size = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &path_size)) {
        CloseHandle(process); reason = L"Could not read target executable path."; return false;
    }
    image_path.assign(path.data(), path_size);
    const std::wstring expected_path = L"C:\\ProgramData\\PopCap Games\\PlantsVsZombies\\popcapgame1.exe";
    if (lower(image_path) != lower(expected_path)) {
        CloseHandle(process); reason = L"Target executable path is not the verified runtime image."; return false;
    }
    if (!verify_launcher_parent(pid, reason)) { CloseHandle(process); return false; }

    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modules == INVALID_HANDLE_VALUE) { CloseHandle(process); reason = L"Could not inspect the target's loaded executable image."; return false; }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    const bool got_module = Module32FirstW(modules, &module) != FALSE;
    CloseHandle(modules);
    if (!got_module || reinterpret_cast<std::uintptr_t>(module.modBaseAddr) != 0x400000) {
        CloseHandle(process); reason = L"Target main-module base does not match the verified x86 image."; return false;
    }
    const auto base = reinterpret_cast<const unsigned char*>(module.modBaseAddr);
    IMAGE_DOS_HEADER dos{};
    if (!read_process_exact(process, base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 1024 * 1024) {
        CloseHandle(process); reason = L"Target has no valid in-memory PE header."; return false;
    }
    IMAGE_NT_HEADERS32 nt{};
    const auto* nt_address = base + dos.e_lfanew;
    if (!read_process_exact(process, nt_address, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt.FileHeader.TimeDateStamp != 0x4d02b058 ||
        nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt.OptionalHeader.SizeOfImage != 0x41e000) {
        CloseHandle(process); reason = L"Target in-memory PE identity does not match the verified runtime."; return false;
    }
    if (nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 32) {
        CloseHandle(process); reason = L"Target PE section count is outside the supported range."; return false;
    }
    std::vector<IMAGE_SECTION_HEADER> sections(nt.FileHeader.NumberOfSections);
    const auto* remote_sections = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    if (!read_process_exact(process, remote_sections, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER))) {
        CloseHandle(process); reason = L"Could not read target PE section headers."; return false;
    }
    bool found_text = false;
    DWORD text_rva = 0, text_size = 0;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        if (std::strncmp(reinterpret_cast<const char*>(sections[i].Name), ".text", 8) == 0) {
            found_text = true;
            text_rva = sections[i].VirtualAddress;
            text_size = sections[i].Misc.VirtualSize;
            break;
        }
    }
    if (!found_text || text_rva != 0x1000 || text_size != 0x2d3e9c ||
        text_rva > nt.OptionalHeader.SizeOfImage || text_size > nt.OptionalHeader.SizeOfImage - text_rva) {
        CloseHandle(process); reason = L"Target code section layout does not match the verified runtime."; return false;
    }

    wchar_t pipe_name[96]{};
    swprintf_s(pipe_name, _countof(pipe_name), L"\\\\.\\pipe\\pvz-ai-%lu", pid);
    SetLastError(ERROR_SUCCESS);
    const BOOL pipe_available = WaitNamedPipeW(pipe_name, 0);
    const DWORD pipe_error = pipe_available ? ERROR_SUCCESS : GetLastError();
    if (pipe_available || pipe_error == ERROR_PIPE_BUSY || pipe_error == ERROR_SEM_TIMEOUT) {
        already_attached = true;
        CloseHandle(process);
        return true;
    }
    std::string digest;
    if (!sha256_remote(process, base + text_rva, text_size, digest) || digest != "420db8be5f6d704d10ea1fb7c7c815623807b7a2cebb55c80fd66ce70447ff64") {
        CloseHandle(process); reason = L"Target .text SHA-256 does not match the verified runtime."; return false;
    }
    CloseHandle(process);
    return true;
}

bool canonical_dll_path(const std::wstring& input, std::wstring& output) {
    if (input.empty() || input.find(L'"') != std::wstring::npos) return false;
    const bool drive_absolute = input.size() >= 3 && std::iswalpha(input[0]) && input[1] == L':' && (input[2] == L'\\' || input[2] == L'/');
    const bool unc_absolute = input.size() >= 2 && (input[0] == L'\\' || input[0] == L'/') && input[1] == input[0];
    if (!drive_absolute && !unc_absolute) return false;
    const DWORD needed = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (needed == 0 || needed > 32768) return false;
    std::vector<wchar_t> buffer(needed);
    const DWORD written = GetFullPathNameW(input.c_str(), needed, buffer.data(), nullptr);
    if (written == 0 || written >= needed) return false;
    output.assign(buffer.data(), written);
    const DWORD attributes = GetFileAttributesW(output.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
        lower(output.substr(output.find_last_of(L'.') == std::wstring::npos ? output.size() : output.find_last_of(L'.'))) != L".dll") return false;

    return is_i386_image(output);
}

bool inject(DWORD pid, const std::wstring& dll_path, std::wstring& reason) {
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!process) { reason = L"Could not open target with the required DLL-load permissions."; return false; }

    const SIZE_T bytes = (dll_path.size() + 1) * sizeof(wchar_t);
    void* remote_path = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!remote_path) { CloseHandle(process); reason = L"Could not allocate target memory for the DLL path."; return false; }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote_path, dll_path.c_str(), bytes, &written) || written != bytes) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        reason = L"Could not write the DLL path into the target.";
        return false;
    }

    auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = load_library ? CreateRemoteThread(process, nullptr, 0, load_library, remote_path, 0, nullptr) : nullptr;
    if (!thread) {
        VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
        CloseHandle(process);
        reason = L"Could not start the target's LoadLibraryW call.";
        return false;
    }
    const DWORD wait = WaitForSingleObject(thread, 15000);
    DWORD module_handle = 0;
    const bool loaded = wait == WAIT_OBJECT_0 && GetExitCodeThread(thread, &module_handle) && module_handle != 0;
    CloseHandle(thread);
    if (wait == WAIT_OBJECT_0) VirtualFreeEx(process, remote_path, 0, MEM_RELEASE);
    CloseHandle(process);
    if (!loaded) {
        reason = wait == WAIT_TIMEOUT ? L"DLL loading did not finish within 15 seconds; target memory was left allocated for safety." :
            L"LoadLibraryW failed in the target process.";
        return false;
    }
    return true;
}

bool parse_pid(const wchar_t* text, DWORD& pid) {
    if (!text || !*text) return false;
    wchar_t* end = nullptr;
    const unsigned long value = wcstoul(text, &end, 10);
    if (!end || *end || value == 0 || value > MAXDWORD) return false;
    pid = static_cast<DWORD>(value);
    return true;
}

void usage() { std::wcerr << L"Usage: pvz_injector.exe --pid PID --dll ABSOLUTE_PATH\n"; }
}

int wmain(int argc, wchar_t** argv) {
    DWORD pid = 0;
    std::wstring dll_argument;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--pid" && i + 1 < argc) {
            if (!parse_pid(argv[++i], pid)) { usage(); return 2; }
        } else if (std::wstring(argv[i]) == L"--dll" && i + 1 < argc) {
            dll_argument = argv[++i];
        } else { usage(); return 2; }
    }
    if (pid == 0 || dll_argument.empty()) { usage(); return 2; }
    std::wstring dll_path;
    if (!canonical_dll_path(dll_argument, dll_path)) {
        std::wcerr << L"DLL path must name an existing regular .dll file.\n";
        return 2;
    }
    std::wstring image_path, reason;
    bool already_attached = false;
    if (!is_supported_process(pid, image_path, reason, already_attached)) {
        std::wcerr << L"Refused: " << reason << L"\n";
        return 3;
    }
    if (already_attached) {
        std::wcout << L"A bridge pipe already exists for verified process " << pid << L"; no second injection was attempted.\n";
        return 0;
    }
    if (!inject(pid, dll_path, reason)) {
        std::wcerr << L"Injection failed: " << reason << L"\n";
        return 4;
    }
    std::wcout << L"Bridge DLL loaded into verified process " << pid << L".\n";
    return 0;
}
