#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>
#include <vector>
#include <memory>
#include <string>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdarg>
#include <initializer_list>
#include <optional>
#include <unordered_map>

using NTSTATUS = LONG;
#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif
#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

static_assert(sizeof(void*) == 8,
    "memory.hpp targets x64 only (PEB offsets and IMAGE_NT_HEADERS64 are hardcoded).");

namespace nt_mem_detail
{
    struct CLIENT_ID
    {
        HANDLE UniqueProcess;
        HANDLE UniqueThread;
    };

    struct OBJECT_ATTRIBUTES
    {
        ULONG  Length;
        HANDLE RootDirectory;
        PVOID  ObjectName;
        ULONG  Attributes;
        PVOID  SecurityDescriptor;
        PVOID  SecurityQualityOfService;
    };

    struct REMOTE_LIST_ENTRY
    {
        std::uintptr_t Flink;
        std::uintptr_t Blink;
    };

    struct REMOTE_LDR_DATA_TABLE_ENTRY
    {
        REMOTE_LIST_ENTRY InLoadOrderLinks;
        REMOTE_LIST_ENTRY InMemoryOrderLinks;
        REMOTE_LIST_ENTRY InInitializationOrderLinks;
        std::uintptr_t    DllBase;
        std::uintptr_t    EntryPoint;
        ULONG             SizeOfImage;
        ULONG             _pad0;
        USHORT            FullDllName_Length;
        USHORT            FullDllName_MaximumLength;
        ULONG             _pad1;
        std::uintptr_t    FullDllName_Buffer;
        USHORT            BaseDllName_Length;
        USHORT            BaseDllName_MaximumLength;
        ULONG             _pad2;
        std::uintptr_t    BaseDllName_Buffer;
    };
}

using NtReadVirtualMemory_t       = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
using NtWriteVirtualMemory_t      = NTSTATUS(NTAPI*)(HANDLE, PVOID, LPCVOID, SIZE_T, PSIZE_T);
using NtOpenProcess_t             = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, nt_mem_detail::OBJECT_ATTRIBUTES*, nt_mem_detail::CLIENT_ID*);
using NtClose_t                   = NTSTATUS(NTAPI*)(HANDLE);
using NtQuerySystemInformation_t  = NTSTATUS(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
using NtQueryInformationProcess_t = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

enum class AttachResult
{
    Ok,
    NtApiUnresolved,
    ProcessNotFound,
    OpenFailed,
};

enum class ReadResult
{
    Empty,
    Partial,
    Full,
};

class Memory
{
public:
    using LogFn = void(*)(const char*);
    inline static LogFn s_logger = nullptr;

    struct ModuleInfo
    {
        std::uintptr_t base = 0;
        std::size_t    size = 0;
        bool valid() const noexcept { return base != 0 && size != 0; }
    };

    struct SectionInfo
    {
        std::uintptr_t base = 0;
        std::size_t    size = 0;
        bool valid() const noexcept { return base != 0 && size != 0; }
    };

    static void SetLogger(LogFn fn) noexcept { s_logger = fn; }

    std::uintptr_t Pid()        const noexcept { return processId_; }
    void*          Handle()     const noexcept { return processHandle_; }
    bool           IsAttached() const noexcept { return processHandle_ != nullptr; }

    AttachResult AttachEx(const wchar_t* processName) noexcept
    {
        if (!ResolveNtApis()) {
            Log("[memory] ResolveNtApis failed\n");
            return AttachResult::NtApiUnresolved;
        }

        const std::uintptr_t newPid = NtFindPidByName(processName);
        if (!newPid) {
            Log("[memory] pid not found for target process\n");
            return AttachResult::ProcessNotFound;
        }

        nt_mem_detail::OBJECT_ATTRIBUTES oa{};
        oa.Length = sizeof(oa);
        nt_mem_detail::CLIENT_ID cid{};
        cid.UniqueProcess = reinterpret_cast<HANDLE>(newPid);
        cid.UniqueThread  = nullptr;

        const ACCESS_MASK mask =
            PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION;

        HANDLE newHandle = nullptr;
        const NTSTATUS st = ntOpen_(&newHandle, mask, &oa, &cid);
        if (!NT_SUCCESS(st) || !newHandle) {
            Log("[memory] NtOpenProcess failed status=0x%08lX pid=%lu\n",
                static_cast<long>(st), static_cast<unsigned long>(newPid));
            return AttachResult::OpenFailed;
        }

        Detach();
        processId_     = newPid;
        processHandle_ = newHandle;

        Log("[memory] Attach OK pid=%lu mask=0x%lX\n",
            static_cast<unsigned long>(processId_), static_cast<unsigned long>(mask));
        return AttachResult::Ok;
    }

    bool Attach(const wchar_t* processName) noexcept
    {
        return AttachEx(processName) == AttachResult::Ok;
    }

    void Detach() noexcept
    {
        if (processHandle_) {
            if (ntClose_) ntClose_(processHandle_);
            else          ::CloseHandle(processHandle_);
            processHandle_ = nullptr;
            processId_     = 0;
            scanCache_.clear();
        }
    }

    std::uintptr_t GetModuleAddress(const wchar_t* moduleName) const noexcept
    {
        return PebFindModule(moduleName).base;
    }

    ModuleInfo GetModuleInfo(const wchar_t* moduleName) const noexcept
    {
        return PebFindModule(moduleName);
    }

    SectionInfo GetSection(const wchar_t* moduleName, const char* sectionName) const noexcept
    {
        SectionInfo out{};
        const ModuleInfo mod = GetModuleInfo(moduleName);
        if (!mod.valid() || !sectionName) return out;

        IMAGE_DOS_HEADER dos{};
        if (!NtRead(mod.base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return out;

        IMAGE_NT_HEADERS64 nt{};
        if (!NtRead(mod.base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return out;

        const std::uintptr_t sectionTable = mod.base + dos.e_lfanew
            + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;

        IMAGE_SECTION_HEADER hdr{};
        for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            if (!NtRead(sectionTable + i * sizeof(IMAGE_SECTION_HEADER), &hdr, sizeof(hdr))) break;
            if (std::strncmp(reinterpret_cast<const char*>(hdr.Name), sectionName, IMAGE_SIZEOF_SHORT_NAME) == 0) {
                out.base = mod.base + hdr.VirtualAddress;
                out.size = hdr.Misc.VirtualSize ? hdr.Misc.VirtualSize : hdr.SizeOfRawData;
                return out;
            }
        }
        return out;
    }

    template <typename T>
    T Read(std::uintptr_t address) const noexcept
    {
        T value = {};
        NtRead(address, &value, sizeof(T));
        return value;
    }

    template <typename T>
    std::optional<T> TryRead(std::uintptr_t address) const noexcept
    {
        T value = {};
        if (!NtRead(address, &value, sizeof(T))) return std::nullopt;
        return value;
    }

    bool ReadRaw(std::uintptr_t address, void* buffer, size_t size) const noexcept
    {
        return NtRead(address, buffer, size);
    }

    std::string ReadString(std::uintptr_t address, size_t size = 32) const
    {
        if (!size) return {};
        std::vector<char> buffer(size + 1, '\0');
        NtRead(address, buffer.data(), size);
        return std::string(buffer.data(), strnlen(buffer.data(), size));
    }

    template <typename T>
    bool Write(std::uintptr_t address, const T& value) const noexcept
    {
        return NtWrite(address, &value, sizeof(T));
    }

    bool WriteRaw(std::uintptr_t address, const void* buffer, size_t size) const noexcept
    {
        return NtWrite(address, buffer, size);
    }

    std::uintptr_t FindStringA(const wchar_t* moduleName, const char* needle) const noexcept
    {
        if (!needle) return 0;
        const SectionInfo rdata = GetSection(moduleName, ".rdata");
        if (!rdata.valid()) return 0;

        const std::size_t nlen = std::strlen(needle);
        if (!nlen || nlen > rdata.size) return 0;

        ReadResult coverage = ReadResult::Empty;
        const auto* bufPtr = GetOrLoadRange(rdata.base, rdata.size, &coverage);
        if (!bufPtr || coverage == ReadResult::Empty) return 0;
        const auto& buf = *bufPtr;

        const std::uint8_t* p     = buf.data();
        const std::size_t   last  = buf.size() - nlen;
        const std::uint8_t  first = static_cast<std::uint8_t>(needle[0]);
        for (std::size_t i = 0; i <= last; ++i) {
            if (p[i] != first) continue;
            if (std::memcmp(p + i, needle, nlen) != 0) continue;
            if (i + nlen < buf.size() && p[i + nlen] != 0) continue;
            return rdata.base + i;
        }
        return 0;
    }

    std::uintptr_t FindRipXrefTo(const wchar_t* moduleName, std::uintptr_t targetVa) const noexcept
    {
        if (!targetVa) return 0;
        const SectionInfo text = GetSection(moduleName, ".text");
        if (!text.valid() || text.size < 7) return 0;

        ReadResult coverage = ReadResult::Empty;
        const auto* bufPtr = GetOrLoadRange(text.base, text.size, &coverage);
        if (!bufPtr || coverage == ReadResult::Empty) return 0;
        const auto& buf = *bufPtr;

        const std::uint8_t* p    = buf.data();
        const std::size_t   last = text.size - 7;

        for (std::size_t i = 0; i <= last; ++i) {
            const std::uint8_t b0 = p[i];
            const std::uint8_t b1 = p[i + 1];
            const std::uint8_t b2 = p[i + 2];

            const bool         rex   = (b0 & 0xF0) == 0x40;
            const std::uint8_t opc   = rex ? b1 : b0;
            const std::uint8_t modrm = rex ? b2 : b1;
            const std::size_t  off   = rex ? 3  : 2;

            if (opc != 0x8D && opc != 0x8B) continue;
            if ((modrm & 0xC7) != 0x05)     continue;
            if (i + off + 4 > text.size)    continue;

            std::int32_t disp;
            std::memcpy(&disp, p + i + off, sizeof(disp));
            const std::size_t    instrLen = off + 4;
            const std::uintptr_t resolved = text.base + i + instrLen + disp;
            if (resolved == targetVa) return text.base + i;
        }
        return 0;
    }

    std::uintptr_t WalkBackToFunctionStart(const wchar_t* moduleName, std::uintptr_t xrefVa,
                                          std::size_t maxBack = 0x2000) const noexcept
    {
        const SectionInfo text = GetSection(moduleName, ".text");
        if (!text.valid()) return 0;
        if (xrefVa < text.base || xrefVa >= text.base + text.size) return 0;

        const std::size_t off  = xrefVa - text.base;
        const std::size_t look = (off < maxBack) ? off : maxBack;
        if (look < 2) return 0;

        std::vector<std::uint8_t> buf(look);
        if (!NtRead(xrefVa - look, buf.data(), look)) return 0;

        for (std::size_t i = look; i >= 2; --i) {
            if (buf[i - 1] == 0xCC && buf[i - 2] == 0xCC) {
                return (xrefVa - look) + i;
            }
        }
        return 0;
    }

    std::uintptr_t FindByStringXref(const wchar_t* moduleName, const char* needle,
                                    bool walkBackToFnStart = true) const noexcept
    {
        const std::uintptr_t strVa = FindStringA(moduleName, needle);
        if (!strVa) {
            Log("[xref] string=\"%s\" status=not_found\n", needle);
            return 0;
        }

        const std::uintptr_t xref = FindRipXrefTo(moduleName, strVa);
        if (!xref) {
            Log("[xref] string=\"%s\" strva=%p status=no_xref\n",
                needle, reinterpret_cast<void*>(strVa));
            return 0;
        }

        if (!walkBackToFnStart) {
            Log("[xref] string=\"%s\" strva=%p xref=%p\n",
                needle, reinterpret_cast<void*>(strVa), reinterpret_cast<void*>(xref));
            return xref;
        }

        const std::uintptr_t fn = WalkBackToFunctionStart(moduleName, xref);
        if (!fn) {
            Log("[xref] string=\"%s\" xref=%p fn=not_found\n",
                needle, reinterpret_cast<void*>(xref));
            return xref;
        }
        Log("[xref] string=\"%s\" strva=%p xref=%p fn=%p\n",
            needle, reinterpret_cast<void*>(strVa), reinterpret_cast<void*>(xref),
            reinterpret_cast<void*>(fn));
        return fn;
    }

    static bool ParseIdaPattern(const std::string& pattern,
                                std::vector<std::uint8_t>& bytes,
                                std::vector<std::uint8_t>& mask) noexcept
    {
        bytes.clear();
        mask.clear();

        auto hexVal = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return -1;
        };

        const char* p   = pattern.c_str();
        const char* end = p + pattern.size();
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (p >= end) break;

            if (*p == '?') {
                ++p;
                if (p < end && *p == '?') ++p;
                if (p < end && *p == '?') return false;
                bytes.push_back(0);
                mask.push_back(0);
                continue;
            }

            const int hi = hexVal(*p++);
            if (hi < 0) return false;
            std::uint8_t b = static_cast<std::uint8_t>(hi);
            if (p < end && *p != ' ' && *p != '\t') {
                const int lo = hexVal(*p++);
                if (lo < 0) return false;
                b = static_cast<std::uint8_t>((hi << 4) | lo);
            }
            bytes.push_back(b);
            mask.push_back(1);
        }
        return !bytes.empty();
    }

    std::uintptr_t PatternScan(const wchar_t* moduleName, const std::string& idaPattern) const noexcept
    {
        const ModuleInfo mod = GetModuleInfo(moduleName);
        if (!mod.valid()) return 0;

        std::vector<std::uint8_t> bytes, mask;
        if (!ParseIdaPattern(idaPattern, bytes, mask)) return 0;
        if (bytes.size() > mod.size) return 0;

        ReadResult coverage = ReadResult::Empty;
        const auto* bufPtr = GetOrLoadRange(mod.base, mod.size, &coverage);
        if (!bufPtr || coverage == ReadResult::Empty) return 0;
        const auto& buf = *bufPtr;

        const std::size_t   patLen = bytes.size();
        const std::size_t   last   = buf.size() - patLen;
        const std::uint8_t* b = buf.data();
        const std::uint8_t* p = bytes.data();
        const std::uint8_t* m = mask.data();

        for (std::size_t i = 0; i <= last; ++i) {
            std::size_t j = 0;
            for (; j < patLen; ++j) {
                if (m[j] && b[i + j] != p[j]) break;
            }
            if (j == patLen) return mod.base + i;
        }
        return 0;
    }

    std::uintptr_t PatternScan(const std::string& moduleName, const std::string& idaPattern) const noexcept
    {
        const std::wstring w(moduleName.begin(), moduleName.end());
        return PatternScan(w.c_str(), idaPattern);
    }

    std::uintptr_t ResolveRel32(std::uintptr_t instrAddr, int dispOffset = 3, int instrLen = 7) const noexcept
    {
        if (!instrAddr) return 0;
        const auto disp = TryRead<std::int32_t>(instrAddr + dispOffset);
        if (!disp) return 0;
        return instrAddr + instrLen + *disp;
    }

    std::uintptr_t PatternScanRel32(const wchar_t* moduleName, const std::string& idaPattern,
                                    int dispOffset = 3, int instrLen = 7) const noexcept
    {
        const std::uintptr_t addr = PatternScan(moduleName, idaPattern);
        if (!addr) return 0;
        return ResolveRel32(addr, dispOffset, instrLen);
    }

    static std::string BytesToIda(const std::vector<std::uint8_t>& bytes, const std::string& mask) noexcept
    {
        std::string out;
        char tmp[8];
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const bool wild = (i < mask.size()) && (mask[i] == '?' || mask[i] == '0');
            if (wild) out += "? ";
            else { sprintf_s(tmp, sizeof(tmp), "%02X ", bytes[i]); out += tmp; }
        }
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    static std::string BytesToIda(const std::vector<std::uint8_t>& bytes) noexcept
    {
        std::string out;
        char tmp[8];
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            sprintf_s(tmp, sizeof(tmp), "%02X ", bytes[i]);
            out += tmp;
        }
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }

    std::uintptr_t FindSig(const wchar_t* moduleName, const std::string& idaPattern) const noexcept
    {
        return PatternScan(moduleName, idaPattern);
    }

    std::uintptr_t FindSig(const wchar_t* moduleName,
                           std::initializer_list<std::uint8_t> bytes,
                           const std::string& mask) const noexcept
    {
        return PatternScan(moduleName, BytesToIda(std::vector<std::uint8_t>(bytes), mask));
    }

    std::uintptr_t FindSig(const wchar_t* moduleName,
                           std::initializer_list<std::uint8_t> bytes) const noexcept
    {
        return PatternScan(moduleName, BytesToIda(std::vector<std::uint8_t>(bytes)));
    }

    std::uintptr_t CreateSigIda(std::uintptr_t address,
                                const wchar_t* moduleName,
                                std::size_t maxLen = 64) const noexcept
    {
        if (!address) { Log("[sig] null address\n"); return 0; }

        auto buildPattern = [&](std::size_t len) -> std::string
        {
            std::vector<std::uint8_t> buf(len);
            if (!ReadRaw(address, buf.data(), len)) return {};

            std::vector<bool> wild(len, false);

            auto wildcardDisp32 = [&](std::size_t start) {
                for (std::size_t k = 0; k < 4 && start + k < len; ++k) wild[start + k] = true;
            };
            auto isModrmRip = [](std::uint8_t b) { return (b & 0xC7) == 0x05; };
            auto isRex      = [](std::uint8_t b) { return (b & 0xF0) == 0x40; };
            auto isSsePfx   = [](std::uint8_t b) { return b == 0xF2 || b == 0xF3 || b == 0x66; };

            for (std::size_t i = 0; i < len; ++i) {
                std::size_t p = i;
                if (p < len && isSsePfx(buf[p])) ++p;
                if (p < len && isRex(buf[p]))    ++p;

                bool twoByte = false;
                if (p < len && buf[p] == 0x0F) { twoByte = true; ++p; }

                if (p + 1 < len) {
                    const std::uint8_t opc   = buf[p];
                    const std::uint8_t modrm = buf[p + 1];

                    bool goodOpc = false;
                    if (twoByte) {
                        if (opc >= 0x10 && opc <= 0x17) goodOpc = true;
                        if (opc == 0x28 || opc == 0x29) goodOpc = true;
                        if (opc == 0x6E || opc == 0x6F) goodOpc = true;
                        if (opc == 0x7E || opc == 0x7F) goodOpc = true;
                    } else {
                        switch (opc) {
                        case 0x8B: case 0x8D: case 0x89:
                        case 0x39: case 0x3B:
                        case 0xC7: case 0xFF:
                            goodOpc = true; break;
                        default: break;
                        }
                    }

                    if (goodOpc && isModrmRip(modrm)) {
                        wildcardDisp32(p + 2);
                        i = p + 1;
                    }
                }

                if (i + 5 < len && buf[i] == 0x0F && (buf[i + 1] & 0xF0) == 0x80)
                    for (std::size_t k = 2; k <= 5; ++k) wild[i + k] = true;

                if (i + 4 < len && (buf[i] == 0xE8 || buf[i] == 0xE9))
                    for (std::size_t k = 1; k <= 4; ++k) wild[i + k] = true;
            }

            std::string out;
            char tmp[8];
            for (std::size_t i = 0; i < len; ++i) {
                if (wild[i]) out += "? ";
                else { sprintf_s(tmp, sizeof(tmp), "%02X ", buf[i]); out += tmp; }
            }
            if (!out.empty() && out.back() == ' ') out.pop_back();
            return out;
        };

        std::string    best;
        std::uintptr_t match = 0;
        for (std::size_t len = 12; len <= maxLen; len += 4) {
            std::string sig = buildPattern(len);
            if (sig.empty()) continue;
            if (PatternScan(moduleName, sig) == address) {
                best  = sig;
                match = address;
                break;
            }
        }

        if (match) Log("[sig] %p  ->  \"%s\"\n", reinterpret_cast<void*>(address), best.c_str());
        else       Log("[sig] %p  no unique sig within %zu bytes\n", reinterpret_cast<void*>(address), maxLen);

        return match;
    }

    std::uintptr_t CreateSigIDA(std::uintptr_t address,
                                const wchar_t* moduleName,
                                std::size_t maxLen = 64) const noexcept
    {
        return CreateSigIda(address, moduleName, maxLen);
    }

    void ClearScanCache() const noexcept { scanCache_.clear(); }

private:
    std::uintptr_t processId_     = 0;
    void*          processHandle_ = nullptr;

    NtReadVirtualMemory_t       ntRead_       = nullptr;
    NtWriteVirtualMemory_t      ntWrite_      = nullptr;
    NtOpenProcess_t             ntOpen_       = nullptr;
    NtClose_t                   ntClose_      = nullptr;
    NtQuerySystemInformation_t  ntQuerySys_   = nullptr;
    NtQueryInformationProcess_t ntQueryProc_  = nullptr;

    struct ScanCacheEntry
    {
        std::size_t               size = 0;
        std::vector<std::uint8_t> data;
        ReadResult                coverage = ReadResult::Empty;
    };
    mutable std::unordered_map<std::uintptr_t, ScanCacheEntry> scanCache_;

    static void Log(const char* fmt, ...) noexcept
    {
        if (!s_logger) return;
        char buf[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        s_logger(buf);
    }

    bool ResolveNtApis() noexcept
    {
        if (ntRead_ && ntWrite_ && ntOpen_ && ntClose_ && ntQuerySys_ && ntQueryProc_) return true;

        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll) ntdll = LoadLibraryA("ntdll.dll");
        if (!ntdll) return false;

        ntRead_      = reinterpret_cast<NtReadVirtualMemory_t>      (GetProcAddress(ntdll, "NtReadVirtualMemory"));
        ntWrite_     = reinterpret_cast<NtWriteVirtualMemory_t>     (GetProcAddress(ntdll, "NtWriteVirtualMemory"));
        ntOpen_      = reinterpret_cast<NtOpenProcess_t>            (GetProcAddress(ntdll, "NtOpenProcess"));
        ntClose_     = reinterpret_cast<NtClose_t>                  (GetProcAddress(ntdll, "NtClose"));
        ntQuerySys_  = reinterpret_cast<NtQuerySystemInformation_t> (GetProcAddress(ntdll, "NtQuerySystemInformation"));
        ntQueryProc_ = reinterpret_cast<NtQueryInformationProcess_t>(GetProcAddress(ntdll, "NtQueryInformationProcess"));

        return ntRead_ && ntWrite_ && ntOpen_ && ntClose_ && ntQuerySys_ && ntQueryProc_;
    }

    bool NtRead(std::uintptr_t address, void* buffer, size_t size) const noexcept
    {
        if (!processHandle_ || !ntRead_) return false;
        SIZE_T bytesRead = 0;
        const NTSTATUS st = ntRead_(processHandle_, reinterpret_cast<PVOID>(address), buffer, size, &bytesRead);
        return NT_SUCCESS(st) && bytesRead == size;
    }

    bool NtWrite(std::uintptr_t address, const void* buffer, size_t size) const noexcept
    {
        if (!processHandle_ || !ntWrite_) return false;
        SIZE_T bytesWritten = 0;
        const NTSTATUS st = ntWrite_(processHandle_, reinterpret_cast<PVOID>(address), const_cast<PVOID>(buffer), size, &bytesWritten);
        return NT_SUCCESS(st) && bytesWritten == size;
    }

    std::uintptr_t NtFindPidByName(const wchar_t* processName) const noexcept
    {
        if (!ntQuerySys_ || !processName) return 0;

        ULONG cb = 0x40000;
        std::vector<BYTE> buf;
        NTSTATUS st = STATUS_INFO_LENGTH_MISMATCH;
        for (int tries = 0; tries < 6 && st == STATUS_INFO_LENGTH_MISMATCH; ++tries) {
            buf.assign(cb, 0);
            ULONG need = 0;
            st = ntQuerySys_(5, buf.data(), cb, &need);
            if (st == STATUS_INFO_LENGTH_MISMATCH) cb = (need ? need : cb * 2) + 0x4000;
        }
        if (!NT_SUCCESS(st)) return 0;

        const size_t nameLen = std::wcslen(processName);
        const BYTE* p = buf.data();
        for (;;) {
            const auto* info = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(p);
            if (info->ImageName.Buffer && info->ImageName.Length) {
                const USHORT chars = info->ImageName.Length / sizeof(wchar_t);
                if (chars == nameLen && _wcsnicmp(processName, info->ImageName.Buffer, chars) == 0) {
                    return reinterpret_cast<std::uintptr_t>(info->UniqueProcessId);
                }
            }
            if (!info->NextEntryOffset) break;
            p += info->NextEntryOffset;
        }
        return 0;
    }

    ModuleInfo PebFindModule(const wchar_t* moduleName) const noexcept
    {
        ModuleInfo out{};
        if (!processHandle_ || !ntQueryProc_ || !moduleName) return out;

        PROCESS_BASIC_INFORMATION pbi{};
        ULONG ret = 0;
        if (!NT_SUCCESS(ntQueryProc_(processHandle_, 0, &pbi, sizeof(pbi), &ret)) || !pbi.PebBaseAddress) return out;

        std::uintptr_t ldrAddr = 0;
        if (!NtRead(reinterpret_cast<std::uintptr_t>(pbi.PebBaseAddress) + 0x18, &ldrAddr, sizeof(ldrAddr)) || !ldrAddr) return out;

        const std::uintptr_t headAddr = ldrAddr + 0x10;
        nt_mem_detail::REMOTE_LIST_ENTRY head{};
        if (!NtRead(headAddr, &head, sizeof(head)) || !head.Flink) return out;

        const size_t modLen = std::wcslen(moduleName);
        wchar_t nameBuf[260];

        std::uintptr_t cur = head.Flink;
        for (int i = 0; cur && cur != headAddr && i < 1024; ++i) {
            nt_mem_detail::REMOTE_LDR_DATA_TABLE_ENTRY e{};
            if (!NtRead(cur, &e, sizeof(e))) break;

            if (e.DllBase && e.BaseDllName_Length && e.BaseDllName_Buffer) {
                const USHORT bytes = e.BaseDllName_Length;
                if (bytes < sizeof(nameBuf) - sizeof(wchar_t)) {
                    if (NtRead(e.BaseDllName_Buffer, nameBuf, bytes)) {
                        const USHORT chars = bytes / sizeof(wchar_t);
                        nameBuf[chars] = L'\0';
                        if (chars == modLen && _wcsnicmp(nameBuf, moduleName, chars) == 0) {
                            out.base = e.DllBase;
                            out.size = e.SizeOfImage;
                            return out;
                        }
                    }
                }
            }

            cur = e.InLoadOrderLinks.Flink;
        }
        return out;
    }

    ReadResult ReadChunkedFallback(std::uintptr_t addr, void* out, std::size_t size) const noexcept
    {
        if (!size) return ReadResult::Empty;
        if (NtRead(addr, out, size)) return ReadResult::Full;

        constexpr std::size_t kChunk = 0x100000;
        std::size_t okChunks    = 0;
        std::size_t totalChunks = 0;
        std::size_t off = 0;
        auto* dst = static_cast<std::uint8_t*>(out);
        while (off < size) {
            const std::size_t n = (size - off < kChunk) ? (size - off) : kChunk;
            ++totalChunks;
            if (NtRead(addr + off, dst + off, n)) {
                ++okChunks;
            } else {
                std::memset(dst + off, 0xCC, n);
            }
            off += n;
        }
        if (okChunks == 0)               return ReadResult::Empty;
        if (okChunks == totalChunks)     return ReadResult::Full;
        return ReadResult::Partial;
    }

    const std::vector<std::uint8_t>* GetOrLoadRange(std::uintptr_t base, std::size_t size,
                                                    ReadResult* outCoverage = nullptr) const noexcept
    {
        if (!base || !size) return nullptr;

        if (auto it = scanCache_.find(base); it != scanCache_.end() && it->second.size == size) {
            if (outCoverage) *outCoverage = it->second.coverage;
            return &it->second.data;
        }

        ScanCacheEntry entry;
        entry.size = size;
        entry.data.resize(size);
        entry.coverage = ReadChunkedFallback(base, entry.data.data(), size);
        if (entry.coverage == ReadResult::Empty) {
            if (outCoverage) *outCoverage = ReadResult::Empty;
            return nullptr;
        }

        auto [it, inserted] = scanCache_.insert_or_assign(base, std::move(entry));
        (void)inserted;
        if (outCoverage) *outCoverage = it->second.coverage;
        return &it->second.data;
    }
};

inline Memory memory;
