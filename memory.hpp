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
#include <initializer_list>

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

class Memory
{
public:
    std::uintptr_t processId = 0;
    void* processHandle = nullptr;

private:
    NtReadVirtualMemory_t       ntRead       = nullptr;
    NtWriteVirtualMemory_t      ntWrite      = nullptr;
    NtOpenProcess_t             ntOpen       = nullptr;
    NtClose_t                   ntClose      = nullptr;
    NtQuerySystemInformation_t  ntQuerySys   = nullptr;
    NtQueryInformationProcess_t ntQueryProc  = nullptr;

    bool ResolveNtApis() noexcept
    {
        if (ntRead && ntWrite && ntOpen && ntClose && ntQuerySys && ntQueryProc) return true;

        HMODULE ntdll = ::GetModuleHandleA("ntdll.dll");
        if (!ntdll) ntdll = ::LoadLibraryA("ntdll.dll");
        if (!ntdll) return false;

        ntRead      = reinterpret_cast<NtReadVirtualMemory_t>      (::GetProcAddress(ntdll, "NtReadVirtualMemory"));
        ntWrite     = reinterpret_cast<NtWriteVirtualMemory_t>     (::GetProcAddress(ntdll, "NtWriteVirtualMemory"));
        ntOpen      = reinterpret_cast<NtOpenProcess_t>            (::GetProcAddress(ntdll, "NtOpenProcess"));
        ntClose     = reinterpret_cast<NtClose_t>                  (::GetProcAddress(ntdll, "NtClose"));
        ntQuerySys  = reinterpret_cast<NtQuerySystemInformation_t> (::GetProcAddress(ntdll, "NtQuerySystemInformation"));
        ntQueryProc = reinterpret_cast<NtQueryInformationProcess_t>(::GetProcAddress(ntdll, "NtQueryInformationProcess"));

        return ntRead && ntWrite && ntOpen && ntClose && ntQuerySys && ntQueryProc;
    }

    bool NtRead(const std::uintptr_t address, void* buffer, size_t size) const noexcept
    {
        if (!processHandle || !ntRead) return false;
        SIZE_T bytesRead = 0;
        const NTSTATUS st = ntRead(processHandle, reinterpret_cast<PVOID>(address), buffer, size, &bytesRead);
        return NT_SUCCESS(st) && bytesRead == size;
    }

    bool NtWrite(const std::uintptr_t address, const void* buffer, size_t size) const noexcept
    {
        if (!processHandle || !ntWrite) return false;
        SIZE_T bytesWritten = 0;
        const NTSTATUS st = ntWrite(processHandle, reinterpret_cast<PVOID>(address), const_cast<PVOID>(buffer), size, &bytesWritten);
        return NT_SUCCESS(st) && bytesWritten == size;
    }

    std::uintptr_t NtFindPidByName(const wchar_t* processName) const noexcept
    {
        if (!ntQuerySys || !processName) return 0;

        ULONG cb = 0x40000;
        std::vector<BYTE> buf;
        NTSTATUS st = STATUS_INFO_LENGTH_MISMATCH;
        for (int tries = 0; tries < 6 && st == STATUS_INFO_LENGTH_MISMATCH; ++tries) {
            buf.assign(cb, 0);
            ULONG need = 0;
            st = ntQuerySys(5, buf.data(), cb, &need);
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

public:

    bool Attach(const wchar_t* processName) noexcept
    {
        if (!ResolveNtApis()) {
            std::printf("[memory] ResolveNtApis failed\n");
            std::fflush(stdout);
            return false;
        }

        processId = NtFindPidByName(processName);
        if (!processId) {
            return false;
        }

        nt_mem_detail::OBJECT_ATTRIBUTES oa{};
        oa.Length = sizeof(oa);
        nt_mem_detail::CLIENT_ID cid{};
        cid.UniqueProcess = reinterpret_cast<HANDLE>(processId);
        cid.UniqueThread  = nullptr;

        const ACCESS_MASK mask = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_LIMITED_INFORMATION;

        HANDLE h = nullptr;
        const NTSTATUS st = ntOpen(&h, mask, &oa, &cid);
        if (!NT_SUCCESS(st) || !h) {
            std::printf("[memory] NtOpenProcess failed status=0x%08lX pid=%lu\n", static_cast<long>(st), static_cast<unsigned long>(processId));
            std::fflush(stdout);
            processId = 0;
            return false;
        }
        processHandle = h;

        std::printf("[memory] Attach OK  pid=%lu  mask=0x%lX  (NtQuerySystemInformation + NtOpenProcess)\n", static_cast<unsigned long>(processId), static_cast<unsigned long>(mask));
        std::fflush(stdout);
        return true;
    }

    void Detach() noexcept
    {
        if (processHandle)
        {
            if (ntClose) ntClose(processHandle);
            else         ::CloseHandle(processHandle);
            processHandle = nullptr;
            processId = 0;
        }
    }

    struct ModuleInfo {
        std::uintptr_t base = 0;
        std::size_t    size = 0;
        bool valid() const noexcept { return base != 0 && size != 0; }
    };

private:
    ModuleInfo PebFindModule(const wchar_t* moduleName) const noexcept
    {
        ModuleInfo out{};
        if (!processHandle || !ntQueryProc || !moduleName) return out;

        PROCESS_BASIC_INFORMATION pbi{};
        ULONG ret = 0;
        if (!NT_SUCCESS(ntQueryProc(processHandle, 0,
                                    &pbi, sizeof(pbi), &ret)) || !pbi.PebBaseAddress) {
            return out;
        }

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

public:
    std::uintptr_t GetModuleAddress(const wchar_t* moduleName) const noexcept
    {
        return PebFindModule(moduleName).base;
    }

    ModuleInfo GetModuleInfo(const wchar_t* moduleName) const noexcept
    {
        return PebFindModule(moduleName);
    }

    struct SectionInfo {
        std::uintptr_t base = 0;
        std::size_t    size = 0;
        bool valid() const noexcept { return base != 0 && size != 0; }
    };

    SectionInfo GetSection(const wchar_t* ModuleName, const char* SectionName) const noexcept
    {
        SectionInfo Out{};
        const ModuleInfo Mod = GetModuleInfo(ModuleName);
        if (!Mod.valid() || !SectionName) return Out;

        IMAGE_DOS_HEADER Dos{};
        if (!NtRead(Mod.base, &Dos, sizeof(Dos)) || Dos.e_magic != IMAGE_DOS_SIGNATURE) return Out;

        IMAGE_NT_HEADERS64 Nt{};
        if (!NtRead(Mod.base + Dos.e_lfanew, &Nt, sizeof(Nt)) || Nt.Signature != IMAGE_NT_SIGNATURE) return Out;

        const std::uintptr_t SectionTable = Mod.base + Dos.e_lfanew
            + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + Nt.FileHeader.SizeOfOptionalHeader;

        IMAGE_SECTION_HEADER Hdr{};
        for (WORD I = 0; I < Nt.FileHeader.NumberOfSections; ++I) {
            if (!NtRead(SectionTable + I * sizeof(IMAGE_SECTION_HEADER), &Hdr, sizeof(Hdr))) break;
            if (std::strncmp(reinterpret_cast<const char*>(Hdr.Name), SectionName, IMAGE_SIZEOF_SHORT_NAME) == 0) {
                Out.base = Mod.base + Hdr.VirtualAddress;
                Out.size = Hdr.Misc.VirtualSize ? Hdr.Misc.VirtualSize : Hdr.SizeOfRawData;
                return Out;
            }
        }
        return Out;
    }

private:
    bool ReadChunkedFallback(std::uintptr_t Addr, void* Out, std::size_t Size) const noexcept
    {
        if (NtRead(Addr, Out, Size)) return true;
        constexpr std::size_t KChunk = 0x100000;
        bool Any = false;
        std::size_t Off = 0;
        while (Off < Size) {
            const std::size_t N = (Size - Off < KChunk) ? (Size - Off) : KChunk;
            if (NtRead(Addr + Off, static_cast<std::uint8_t*>(Out) + Off, N)) Any = true;
            Off += N;
        }
        return Any;
    }

public:
    std::uintptr_t FindStringA(const wchar_t* ModuleName, const char* Needle) const noexcept
    {
        if (!Needle) return 0;
        const SectionInfo Rdata = GetSection(ModuleName, ".rdata");
        if (!Rdata.valid()) return 0;

        const std::size_t Nlen = std::strlen(Needle);
        if (!Nlen || Nlen > Rdata.size) return 0;

        std::vector<std::uint8_t> Buf(Rdata.size);
        if (!ReadChunkedFallback(Rdata.base, Buf.data(), Buf.size())) return 0;

        const std::uint8_t* P = Buf.data();
        const std::size_t   Last = Buf.size() - Nlen;
        const std::uint8_t  First = static_cast<std::uint8_t>(Needle[0]);
        for (std::size_t I = 0; I <= Last; ++I) {
            if (P[I] != First) continue;
            if (std::memcmp(P + I, Needle, Nlen) != 0) continue;
            if (I + Nlen < Buf.size() && P[I + Nlen] != 0) continue;
            return Rdata.base + I;
        }
        return 0;
    }

    std::uintptr_t FindRipXrefTo(const wchar_t* ModuleName, std::uintptr_t TargetVa) const noexcept
    {
        if (!TargetVa) return 0;
        const SectionInfo Text = GetSection(ModuleName, ".text");
        if (!Text.valid() || Text.size < 7) return 0;

        std::vector<std::uint8_t> Buf(Text.size);
        if (!ReadChunkedFallback(Text.base, Buf.data(), Buf.size())) return 0;

        const std::uint8_t* P = Buf.data();
        const std::size_t   Last = Text.size - 7;

        for (std::size_t I = 0; I <= Last; ++I) {
            const std::uint8_t B0 = P[I];
            const std::uint8_t B1 = P[I + 1];
            const std::uint8_t B2 = P[I + 2];

            const bool         Rex   = (B0 & 0xF0) == 0x40;
            const std::uint8_t Opc   = Rex ? B1 : B0;
            const std::uint8_t Modrm = Rex ? B2 : B1;
            const std::size_t  Off   = Rex ? 3  : 2;

            if (Opc != 0x8D && Opc != 0x8B) continue;
            if ((Modrm & 0xC7) != 0x05)     continue;
            if (I + Off + 4 > Text.size)    continue;

            std::int32_t Disp;
            std::memcpy(&Disp, P + I + Off, sizeof(Disp));
            const std::size_t    InstrLen = Off + 4;
            const std::uintptr_t Resolved = Text.base + I + InstrLen + Disp;
            if (Resolved == TargetVa) return Text.base + I;
        }
        return 0;
    }

    std::uintptr_t WalkBackToFunctionStart(const wchar_t* ModuleName, std::uintptr_t XrefVa, std::size_t MaxBack = 0x2000) const noexcept
    {
        const SectionInfo Text = GetSection(ModuleName, ".text");
        if (!Text.valid()) return 0;
        if (XrefVa < Text.base || XrefVa >= Text.base + Text.size) return 0;

        const std::size_t Off  = XrefVa - Text.base;
        const std::size_t Look = (Off < MaxBack) ? Off : MaxBack;
        if (Look < 2) return 0;

        std::vector<std::uint8_t> Buf(Look);
        if (!NtRead(XrefVa - Look, Buf.data(), Look)) return 0;

        for (std::size_t I = Look; I >= 2; --I) {
            if (Buf[I - 1] == 0xCC && Buf[I - 2] == 0xCC) {
                return (XrefVa - Look) + I;
            }
        }
        return 0;
    }

    std::uintptr_t FindByStringXref(const wchar_t* ModuleName, const char* Needle, bool WalkBackToFnStart = true) const noexcept
    {
        const std::uintptr_t StrVa = FindStringA(ModuleName, Needle);
        if (!StrVa) {
            std::printf("[xref] string=\"%s\" status=not_found\n", Needle);
            return 0;
        }

        const std::uintptr_t Xref = FindRipXrefTo(ModuleName, StrVa);
        if (!Xref) {
            std::printf("[xref] string=\"%s\" strva=%p status=no_xref\n", Needle, reinterpret_cast<void*>(StrVa));
            return 0;
        }

        if (!WalkBackToFnStart) {
            std::printf("[xref] string=\"%s\" strva=%p xref=%p\n", Needle, reinterpret_cast<void*>(StrVa), reinterpret_cast<void*>(Xref));
            return Xref;
        }

        const std::uintptr_t Fn = WalkBackToFunctionStart(ModuleName, Xref);
        if (!Fn) {
            std::printf("[xref] string=\"%s\" xref=%p fn=not_found\n", Needle, reinterpret_cast<void*>(Xref));
            return Xref;
        }
        std::printf("[xref] string=\"%s\" strva=%p xref=%p fn=%p\n", Needle, reinterpret_cast<void*>(StrVa), reinterpret_cast<void*>(Xref), reinterpret_cast<void*>(Fn));
        return Fn;
    }

    static bool ParseIdaPattern(const std::string& pattern, std::vector<std::uint8_t>& bytes, std::vector<std::uint8_t>& mask) noexcept
    {
        bytes.clear();
        mask.clear();

        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return -1;
        };

        const char* p = pattern.c_str();
        const char* end = p + pattern.size();
        while (p < end) {
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            if (p >= end) break;

            if (*p == '?') {
                while (p < end && *p == '?') ++p;
                bytes.push_back(0);
                mask.push_back(0);
                continue;
            }

            const int hi = hex_val(*p++);
            if (hi < 0) return false;
            std::uint8_t b = static_cast<std::uint8_t>(hi);
            if (p < end && *p != ' ' && *p != '\t') {
                const int lo = hex_val(*p++);
                if (lo < 0) return false;
                b = static_cast<std::uint8_t>((hi << 4) | lo);
            }
            bytes.push_back(b);
            mask.push_back(1);
        }
        return !bytes.empty();
    }

    std::uintptr_t PatternScan(const wchar_t* moduleName, const std::string& ida_pattern) const noexcept
    {
        const ModuleInfo mod = GetModuleInfo(moduleName);
        if (!mod.valid()) return 0;

        std::vector<std::uint8_t> bytes, mask;
        if (!ParseIdaPattern(ida_pattern, bytes, mask)) return 0;
        if (bytes.size() > mod.size) return 0;

        std::vector<std::uint8_t> buf(mod.size);
        if (!NtRead(mod.base, buf.data(), buf.size())) {
            constexpr std::size_t kChunk = 0x100000;
            std::size_t off = 0;
            while (off < mod.size) {
                const std::size_t n = (mod.size - off < kChunk) ? (mod.size - off) : kChunk;
                NtRead(mod.base + off, buf.data() + off, n);
                off += n;
            }
        }

        const std::size_t pat_len = bytes.size();
        const std::size_t last    = buf.size() - pat_len;
        const std::uint8_t* b = buf.data();
        const std::uint8_t* p = bytes.data();
        const std::uint8_t* m = mask.data();

        for (std::size_t i = 0; i <= last; ++i) {
            std::size_t j = 0;
            for (; j < pat_len; ++j) {
                if (m[j] && b[i + j] != p[j]) break;
            }
            if (j == pat_len) return mod.base + i;
        }
        return 0;
    }

    std::uintptr_t PatternScan(const std::string& moduleName, const std::string& ida_pattern) const noexcept
    {
        const std::wstring w(moduleName.begin(), moduleName.end());
        return PatternScan(w.c_str(), ida_pattern);
    }

    std::uintptr_t ResolveRel32(std::uintptr_t instr_addr, int disp_offset = 3, int instr_len = 7) const noexcept
    {
        if (!instr_addr) return 0;
        const std::int32_t disp = Read<std::int32_t>(instr_addr + disp_offset);
        return instr_addr + instr_len + disp;
    }

    std::uintptr_t PatternScanRel32(const wchar_t* moduleName, const std::string& ida_pattern,
                                     int disp_offset = 3, int instr_len = 7) const noexcept
    {
        std::uintptr_t addr = PatternScan(moduleName, ida_pattern);
        if (!addr) return 0;
        return ResolveRel32(addr, disp_offset, instr_len);
    }

    template <typename T>
    T Read(const std::uintptr_t address) const noexcept
    {
        T value = { };
        NtRead(address, &value, sizeof(T));
        return value;
    }

    bool ReadRaw(const std::uintptr_t address, const void* buffer, size_t size) const noexcept
    {
        return NtRead(address, const_cast<void*>(buffer), size);
    }

    std::string ReadString(std::uintptr_t address, size_t size = 32) const
    {
        std::vector<char> buffer(size, '\0');
        NtRead(address, buffer.data(), size);
        return std::string(buffer.data());
    }

    template <typename T>
    bool Write(const std::uintptr_t address, const T& value) const noexcept
    {
        return NtWrite(address, &value, sizeof(T));
    }

    bool WriteRaw(const std::uintptr_t address, const void* buffer, size_t size) const noexcept
    {
        return NtWrite(address, buffer, size);
    }


    static std::string BytesToIda(const std::vector<std::uint8_t>& bytes, const std::string& mask) noexcept
    {
        std::string out;
        char tmp[8];
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
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
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            if (bytes[i] == 0x00) out += "? ";
            else { sprintf_s(tmp, sizeof(tmp), "%02X ", bytes[i]); out += tmp; }
        }
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }


    std::uintptr_t FindSig(const wchar_t* moduleName, const std::string& ida_pattern) const noexcept
    {
        return PatternScan(moduleName, ida_pattern);
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

    std::uintptr_t CreateSigIDA(std::uintptr_t address,
                                const wchar_t* moduleName,
                                std::size_t MaxLen = 64) const noexcept
    {
        if (!address) { printf("[sig] null address\n"); return 0; }

        auto build_pattern = [&](std::size_t Len) -> std::string
        {
            std::vector<std::uint8_t> Buf(Len);
            if (!ReadRaw(address, Buf.data(), Len)) return {};

            std::vector<bool> Wild(Len, false);

            auto wildcard_disp32 = [&](std::size_t Start)
            {
                for (std::size_t k = 0; k < 4 && Start + k < Len; ++k) Wild[Start + k] = true;
            };

            auto is_modrm_rip = [](std::uint8_t B) { return (B & 0xC7) == 0x05; };
            auto is_rex       = [](std::uint8_t B) { return (B & 0xF0) == 0x40;  };
            auto is_sse_pfx   = [](std::uint8_t B) { return B == 0xF2 || B == 0xF3 || B == 0x66; };

            for (std::size_t i = 0; i < Len; ++i)
            {
                std::size_t p = i;
                if (p < Len && is_sse_pfx(Buf[p])) ++p;
                if (p < Len && is_rex(Buf[p]))     ++p;

                bool two_byte = false;
                if (p < Len && Buf[p] == 0x0F) { two_byte = true; ++p; }

                if (p + 1 < Len)
                {
                    const std::uint8_t Opc   = Buf[p];
                    const std::uint8_t Modrm = Buf[p + 1];

                    bool good_opc = false;
                    if (two_byte)
                    {
                        if (Opc >= 0x10 && Opc <= 0x17) good_opc = true;
                        if (Opc == 0x28 || Opc == 0x29) good_opc = true;
                        if (Opc == 0x6E || Opc == 0x6F) good_opc = true;
                        if (Opc == 0x7E || Opc == 0x7F) good_opc = true;
                    }
                    else
                    {
                        switch (Opc)
                        {
                        case 0x8B: case 0x8D: case 0x89:
                        case 0x39: case 0x3B:
                        case 0xC7: case 0xFF:
                            good_opc = true; break;
                        default: break;
                        }
                    }

                    if (good_opc && is_modrm_rip(Modrm))
                    {
                        wildcard_disp32(p + 2);
                        i = p + 1;
                    }
                }

                if (i + 5 < Len && Buf[i] == 0x0F && (Buf[i + 1] & 0xF0) == 0x80)
                    for (std::size_t k = 2; k <= 5; ++k) Wild[i + k] = true;

                if (i + 4 < Len && (Buf[i] == 0xE8 || Buf[i] == 0xE9))
                    for (std::size_t k = 1; k <= 4; ++k) Wild[i + k] = true;
            }

            std::string Out;
            char Tmp[8];
            for (std::size_t i = 0; i < Len; ++i)
            {
                if (Wild[i]) Out += "? ";
                else { sprintf_s(Tmp, sizeof(Tmp), "%02X ", Buf[i]); Out += Tmp; }
            }
            if (!Out.empty() && Out.back() == ' ') Out.pop_back();
            return Out;
        };

        std::string Best;
        std::uintptr_t Match = 0;

        for (std::size_t Len = 12; Len <= MaxLen; Len += 4)
        {
            std::string Sig = build_pattern(Len);
            if (Sig.empty()) continue;

            if (PatternScan(moduleName, Sig) == address)
            {
                Best  = Sig;
                Match = address;
                break;
            }
        }

        if (Match)
            printf("[sig] %p  ->  \"%s\"\n", (void*)address, Best.c_str());
        else
            printf("[sig] %p  no unique sig within %zu bytes\n", (void*)address, MaxLen);

        return Match;
    }
};

inline Memory memory;
