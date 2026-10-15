// One-file Windows release: SuperStrike-Studio-<version>-windows-x64.exe.
//
// GTK needs its DLLs, schemas, icons and loaders next to the exe, so the release exe is a small
// launcher with the whole portable folder appended to it (LZMS, Windows' own Compression API, no
// extra libraries). On start it unpacks the folder once into
//   %LOCALAPPDATA%\SuperStrike-Studio\app\<version>-<hash>
// (later starts reuse it), removes older unpacked versions, then runs superstrike-studio.exe from
// there with the same arguments, standard handles and exit code.
//
// Built twice from this file (CMakeLists.txt, Windows only):
//   ss-onefile.exe  the launcher (GUI subsystem, app icon and version info)
//   ss-pack.exe     the packer:  ss-pack <launcher.exe> <folder> <out.exe>
//
// Layout of the release exe: [launcher][entry]...[entry][trailer]
//   entry   = u32 path_len, u32 0, u64 size, u64 packed_size, path (UTF-8, '\\'), packed bytes
//   trailer = char magic[8] "SSPACK01", u64 offset of the first entry, u64 entries, u64 hash
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <compressapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef SS_VERSION
#define SS_VERSION "0"
#endif
#define WIDE2(s) L##s
#define WIDE(s) WIDE2(s)

static const char MAGIC[8] = {'S', 'S', 'P', 'A', 'C', 'K', '0', '1'};

#pragma pack(push, 1)
typedef struct {
    uint32_t path_len, zero;
    uint64_t size, packed_size;
} Entry;
typedef struct {
    char magic[8];
    uint64_t offset, count, hash;
} Trailer;
#pragma pack(pop)

static int read_all(HANDLE h, void* buf, uint64_t n) {
    char* p = (char*)buf;
    while (n) {
        DWORD chunk = n > 0x40000000 ? 0x40000000 : (DWORD)n, got = 0;
        if (!ReadFile(h, p, chunk, &got, NULL) || got == 0) return 0;
        p += got, n -= got;
    }
    return 1;
}

static int write_all(HANDLE h, const void* buf, uint64_t n) {
    const char* p = (const char*)buf;
    while (n) {
        DWORD chunk = n > 0x40000000 ? 0x40000000 : (DWORD)n, put = 0;
        if (!WriteFile(h, p, chunk, &put, NULL) || put == 0) return 0;
        p += put, n -= put;
    }
    return 1;
}

#ifdef ONEFILE_PACKER
// ================================================================================================
// ss-pack <launcher.exe> <folder> <out.exe>

static uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

static HANDLE out;
static COMPRESSOR_HANDLE comp;
static uint64_t count, hash = 14695981039346656037ull, total_in, total_out;

static void die(const wchar_t* what, const wchar_t* arg) {
    fwprintf(stderr, L"ss-pack: %ls %ls (error %lu)\n", what, arg ? arg : L"", GetLastError());
    exit(1);
}

static void put(const void* data, uint64_t n) {
    if (!write_all(out, data, n)) die(L"cannot write the output", NULL);
    hash = fnv1a(hash, data, (size_t)n);
}

static void pack_file(const wchar_t* full, const wchar_t* rel) {
    HANDLE f = CreateFileW(full, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) die(L"cannot open", full);
    LARGE_INTEGER sz;
    GetFileSizeEx(f, &sz);
    uint64_t size = (uint64_t)sz.QuadPart;
    char* raw = (char*)malloc(size ? size : 1);
    if (!raw || !read_all(f, raw, size)) die(L"cannot read", full);
    CloseHandle(f);

    SIZE_T packed_size = 0;
    char* packed = NULL;
    if (size) {
        Compress(comp, raw, size, NULL, 0, &packed_size);  // asks for the buffer size
        packed = (char*)malloc(packed_size);
        if (!packed || !Compress(comp, raw, size, packed, packed_size, &packed_size)) die(L"cannot compress", full);
    }

    char path[MAX_PATH * 4];
    int path_len = WideCharToMultiByte(CP_UTF8, 0, rel, -1, path, sizeof path, NULL, NULL) - 1;
    if (path_len <= 0) die(L"bad file name", rel);
    Entry e = {(uint32_t)path_len, 0, size, packed_size};
    put(&e, sizeof e);
    put(path, path_len);
    if (packed_size) put(packed, packed_size);
    free(raw);
    free(packed);
    count++, total_in += size, total_out += packed_size;
}

static void pack_dir(const wchar_t* dir, const wchar_t* rel) {
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) die(L"cannot list", dir);
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t full[MAX_PATH], sub[MAX_PATH];
        swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        swprintf(sub, MAX_PATH, rel[0] ? L"%ls\\%ls" : L"%ls%ls", rel, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) pack_dir(full, sub);
        else pack_file(full, sub);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 4) {
        fwprintf(stderr, L"usage: ss-pack <launcher.exe> <folder> <out.exe>\n");
        return 2;
    }
    if (!CopyFileW(argv[1], argv[3], FALSE)) die(L"cannot copy the launcher to", argv[3]);
    out = CreateFileW(argv[3], GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (out == INVALID_HANDLE_VALUE) die(L"cannot open", argv[3]);
    LARGE_INTEGER end, offset;
    end.QuadPart = 0;
    SetFilePointerEx(out, end, &offset, FILE_END);
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, NULL, &comp)) die(L"no LZMS compressor", NULL);

    pack_dir(argv[2], L"");

    Trailer t;
    memcpy(t.magic, MAGIC, 8);
    t.offset = (uint64_t)offset.QuadPart, t.count = count, t.hash = hash;
    if (!write_all(out, &t, sizeof t)) die(L"cannot write the output", NULL);
    CloseHandle(out);
    CloseCompressor(comp);
    wprintf(L"packed %llu files: %llu MB -> %llu MB\n", (unsigned long long)count,
            (unsigned long long)(total_in >> 20), (unsigned long long)(total_out >> 20));
    return 0;
}

#else
// ================================================================================================
// The launcher

static void fail(const wchar_t* what, const wchar_t* arg) {
    wchar_t msg[1024];
    swprintf(msg, 1024, L"SuperStrike Studio could not start.\n\n%ls\n%ls\n\nWindows error %lu.", what,
             arg ? arg : L"", GetLastError());
    MessageBoxW(NULL, msg, L"SuperStrike Studio", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

static int exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

// Creates every folder of a path (the last component too).
static void make_dirs(const wchar_t* path) {
    wchar_t buf[MAX_PATH];
    wcsncpy(buf, path, MAX_PATH - 1);
    buf[MAX_PATH - 1] = 0;
    for (wchar_t* p = buf + 3; *p; p++)  // after "C:\"
        if (*p == L'\\') {
            *p = 0;
            CreateDirectoryW(buf, NULL);
            *p = L'\\';
        }
    CreateDirectoryW(buf, NULL);
}

static void remove_tree(const wchar_t* dir) {
    wchar_t pattern[MAX_PATH];
    swprintf(pattern, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t full[MAX_PATH];
            swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(full);
            else DeleteFileW(full);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    RemoveDirectoryW(dir);
}

// A relative path that stays inside the folder (no drive, no leading "\\", no ".." component).
static int safe_path(const wchar_t* p) {
    if (!*p || *p == L'\\' || wcschr(p, L':') || wcschr(p, L'/')) return 0;
    for (const wchar_t* c = p; c; c = wcschr(c, L'\\')) {
        if (*c == L'\\') c++;
        if (c[0] == L'.' && c[1] == L'.' && (c[2] == L'\\' || c[2] == 0)) return 0;
    }
    return 1;
}

static void extract(HANDLE self, const Trailer* t, const wchar_t* dest) {
    DECOMPRESSOR_HANDLE dec;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, NULL, &dec)) fail(L"This Windows has no LZMS decompressor.", NULL);
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)t->offset;
    SetFilePointerEx(self, pos, NULL, FILE_BEGIN);
    for (uint64_t i = 0; i < t->count; i++) {
        Entry e;
        char path[MAX_PATH * 4];
        if (!read_all(self, &e, sizeof e) || e.path_len >= sizeof path || !read_all(self, path, e.path_len))
            fail(L"The program file is damaged. Download it again.", NULL);
        path[e.path_len] = 0;
        wchar_t rel[MAX_PATH], full[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, rel, MAX_PATH) || !safe_path(rel))
            fail(L"The program file is damaged. Download it again.", NULL);
        swprintf(full, MAX_PATH, L"%ls\\%ls", dest, rel);

        wchar_t* slash = wcsrchr(full, L'\\');
        *slash = 0;
        make_dirs(full);
        *slash = L'\\';

        char* packed = (char*)malloc(e.packed_size ? e.packed_size : 1);
        char* raw = (char*)malloc(e.size ? e.size : 1);
        if (!packed || !raw) fail(L"Out of memory.", NULL);
        if (!read_all(self, packed, e.packed_size)) fail(L"The program file is damaged. Download it again.", NULL);
        SIZE_T got = 0;
        if (e.size && (!Decompress(dec, packed, e.packed_size, raw, e.size, &got) || got != e.size))
            fail(L"The program file is damaged. Download it again.", rel);
        HANDLE f = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE || !write_all(f, raw, e.size)) fail(L"Cannot write", full);
        CloseHandle(f);
        free(packed);
        free(raw);
    }
    CloseDecompressor(dec);
}

// The command line after the program name (as typed, quotes kept).
static const wchar_t* arguments(void) {
    const wchar_t* p = GetCommandLineW();
    if (*p == L'"') {
        for (p++; *p && *p != L'"'; p++) {}
        if (*p) p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') p++;
    }
    while (*p == L' ' || *p == L'\t') p++;
    return p;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)inst, (void)prev, (void)cmd, (void)show;
    wchar_t self_path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, self_path, MAX_PATH)) fail(L"Cannot find the program file.", NULL);
    HANDLE self = CreateFileW(self_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (self == INVALID_HANDLE_VALUE) fail(L"Cannot open", self_path);
    Trailer t;
    LARGE_INTEGER back;
    back.QuadPart = -(LONGLONG)sizeof t;
    if (!SetFilePointerEx(self, back, NULL, FILE_END) || !read_all(self, &t, sizeof t) || memcmp(t.magic, MAGIC, 8))
        fail(L"The program file is incomplete. Download it again.", self_path);

    // %LOCALAPPDATA%\SuperStrike-Studio\app\<version>-<hash>
    wchar_t base[MAX_PATH], name[64], dest[MAX_PATH], done[MAX_PATH], exe[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) n = GetTempPathW(MAX_PATH, base), base[n ? n - 1 : 0] = 0;
    wcsncat(base, L"\\SuperStrike-Studio\\app", MAX_PATH - wcslen(base) - 1);
    swprintf(name, 64, L"%ls-%016llx", WIDE(SS_VERSION), (unsigned long long)t.hash);
    swprintf(dest, MAX_PATH, L"%ls\\%ls", base, name);
    swprintf(done, MAX_PATH, L"%ls\\.complete", dest);
    swprintf(exe, MAX_PATH, L"%ls\\superstrike-studio.exe", dest);

    if (!exists(done)) {
        // Unpack next to the target and rename, so a half-written folder is never used.
        wchar_t tmp[MAX_PATH], tmp_done[MAX_PATH];
        swprintf(tmp, MAX_PATH, L"%ls.tmp-%lu", dest, GetCurrentProcessId());
        make_dirs(tmp);
        extract(self, &t, tmp);
        swprintf(tmp_done, MAX_PATH, L"%ls\\.complete", tmp);
        HANDLE f = CreateFileW(tmp_done, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) fail(L"Cannot write", tmp_done);
        CloseHandle(f);
        if (exists(dest) && !exists(done)) remove_tree(dest);  // left over from an interrupted run
        if (!MoveFileExW(tmp, dest, 0)) {
            if (!exists(done)) fail(L"Cannot create", dest);
            remove_tree(tmp);  // another start unpacked it at the same time
        }

        // Older versions (folders of a running copy stay: their files are in use)
        wchar_t pattern[MAX_PATH];
        swprintf(pattern, MAX_PATH, L"%ls\\*", base);
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW(pattern, &fd);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.' ||
                    !wcscmp(fd.cFileName, name) || wcsstr(fd.cFileName, L".tmp-"))
                    continue;
                wchar_t old[MAX_PATH];
                swprintf(old, MAX_PATH, L"%ls\\%ls", base, fd.cFileName);
                remove_tree(old);
            } while (FindNextFileW(find, &fd));
            FindClose(find);
        }
    }
    CloseHandle(self);

    // Run it; closing this launcher (or killing it) also ends the app.
    size_t len = wcslen(exe) + wcslen(arguments()) + 4;
    wchar_t* line = (wchar_t*)malloc(len * sizeof(wchar_t));
    swprintf(line, len, L"\"%ls\" %ls", exe, arguments());
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi;
    HANDLE job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;
    memset(&lim, 0, sizeof lim);
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job) SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim);
    if (!CreateProcessW(exe, line, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) fail(L"Cannot start", exe);
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
#endif
