#pragma once

#include <windows.h>
#include <psapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#pragma comment(lib, "psapi.lib")

class Memory
{
public:
    struct Section
    {
        std::uintptr_t Base = 0;
        std::size_t Size = 0;

        explicit operator bool() const noexcept { return Base != 0 && Size != 0; }
    };

    Memory() { Init(static_cast<const wchar_t*>(nullptr)); }
    explicit Memory(const wchar_t* ModuleName) { Init(ModuleName); }
    explicit Memory(const char* ModuleName) { Init(ModuleName); }
    explicit Memory(HMODULE ModuleHandle) { Init(ModuleHandle); }

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    Memory(Memory&&) = delete;
    Memory& operator=(Memory&&) = delete;

    static Memory& Get()
    {
        static Memory Instance;
        return Instance;
    }

    bool Init(const wchar_t* ModuleName = nullptr) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleW(ModuleName) : GetModuleHandleW(nullptr);
        return Init(Handle);
    }

    bool Init(const char* ModuleName) noexcept
    {
        if (!ModuleName) return Init(static_cast<const wchar_t*>(nullptr));
        return Init(GetModuleHandleA(ModuleName));
    }

    bool Init(HMODULE ModuleHandle) noexcept
    {
        if (!ModuleHandle) return false;

        MODULEINFO Info{};
        if (!GetModuleInformation(GetCurrentProcess(), ModuleHandle, &Info, sizeof(Info)))
            return false;

        BaseAddress = reinterpret_cast<std::uintptr_t>(Info.lpBaseOfDll);
        ModuleSize = Info.SizeOfImage;

        const auto Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(BaseAddress);
        if (Dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

        const auto Nt = reinterpret_cast<PIMAGE_NT_HEADERS>(BaseAddress + Dos->e_lfanew);
        if (Nt->Signature != IMAGE_NT_SIGNATURE) return false;

        auto SectionPtr = IMAGE_FIRST_SECTION(Nt);
        for (WORD SectionIdx = 0; SectionIdx < Nt->FileHeader.NumberOfSections; ++SectionIdx, ++SectionPtr)
        {
            std::string_view Name(reinterpret_cast<const char*>(SectionPtr->Name), 8);
            const auto Trim = Name.find('\0');
            if (Trim != std::string_view::npos) Name = Name.substr(0, Trim);

            const std::uintptr_t SectionBase = BaseAddress + SectionPtr->VirtualAddress;
            const std::size_t SectionSize = SectionPtr->Misc.VirtualSize;

            if (Name == ".text") TextSection = { SectionBase, SectionSize };
            else if (Name == ".data") DataSection = { SectionBase, SectionSize };
            else if (Name == ".rdata") RDataSection = { SectionBase, SectionSize };
        }

        Initialized.store(true, std::memory_order_release);
        return true;
    }

    bool IsReady() const noexcept { return Initialized.load(std::memory_order_acquire); }
    std::uintptr_t Base() const noexcept { return BaseAddress; }
    std::size_t Size() const noexcept { return ModuleSize; }
    Section Text() const noexcept { return TextSection; }
    Section Data() const noexcept { return DataSection; }
    Section RData() const noexcept { return RDataSection; }

    static std::uintptr_t GetAddressModule(const char* ModuleName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleA(ModuleName) : GetModuleHandleA(nullptr);
        return reinterpret_cast<std::uintptr_t>(Handle);
    }

    static std::uintptr_t GetAddressModule(const wchar_t* ModuleName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleW(ModuleName) : GetModuleHandleW(nullptr);
        return reinterpret_cast<std::uintptr_t>(Handle);
    }

    static std::size_t GetSizeModule(const char* ModuleName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleA(ModuleName) : GetModuleHandleA(nullptr);
        if (!Handle) return 0;
        MODULEINFO Info{};
        if (!GetModuleInformation(GetCurrentProcess(), Handle, &Info, sizeof(Info))) return 0;
        return Info.SizeOfImage;
    }

    static std::size_t GetSizeModule(const wchar_t* ModuleName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleW(ModuleName) : GetModuleHandleW(nullptr);
        if (!Handle) return 0;
        MODULEINFO Info{};
        if (!GetModuleInformation(GetCurrentProcess(), Handle, &Info, sizeof(Info))) return 0;
        return Info.SizeOfImage;
    }

    static Section GetSectionModule(const char* ModuleName, const char* SectionName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleA(ModuleName) : GetModuleHandleA(nullptr);
        return GetSectionFromHandle(Handle, SectionName);
    }

    static Section GetSectionModule(const wchar_t* ModuleName, const char* SectionName) noexcept
    {
        HMODULE Handle = ModuleName ? GetModuleHandleW(ModuleName) : GetModuleHandleW(nullptr);
        return GetSectionFromHandle(Handle, SectionName);
    }

    std::uintptr_t ScanPatternIDAInModule(const char* ModuleName, std::string_view Signature) const
    {
        const Section Tex = GetSectionModule(ModuleName, ".text");
        if (!Tex) return 0;
        return ScanPatternIDARange(Signature, Tex.Base, Tex.Size);
    }

    std::uintptr_t ScanPatternIDAInModule(const wchar_t* ModuleName, std::string_view Signature) const
    {
        const Section Tex = GetSectionModule(ModuleName, ".text");
        if (!Tex) return 0;
        return ScanPatternIDARange(Signature, Tex.Base, Tex.Size);
    }

    std::vector<std::uintptr_t> FindAllIDAInModule(const char* ModuleName, std::string_view Signature) const
    {
        const Section Tex = GetSectionModule(ModuleName, ".text");
        if (!Tex) return {};
        return FindAllIDARange(Signature, Tex.Base, Tex.Size);
    }

    std::vector<std::uintptr_t> FindAllIDAInModule(const wchar_t* ModuleName, std::string_view Signature) const
    {
        const Section Tex = GetSectionModule(ModuleName, ".text");
        if (!Tex) return {};
        return FindAllIDARange(Signature, Tex.Base, Tex.Size);
    }

    template<typename T>
    T Read(std::uintptr_t Address) const noexcept
    {
        T Value;
        std::memcpy(&Value, reinterpret_cast<const void*>(Address), sizeof(T));
        return Value;
    }

    template<typename T>
    T Read(std::uintptr_t Base, std::ptrdiff_t Offset) const noexcept
    {
        return Read<T>(Base + Offset);
    }

    template<typename T>
    bool TryRead(std::uintptr_t Address, T& OutValue) const noexcept
    {
        if (!IsValidAddress(Address, sizeof(T))) return false;
        std::memcpy(&OutValue, reinterpret_cast<const void*>(Address), sizeof(T));
        return true;
    }

    bool ReadRaw(std::uintptr_t Address, void* Buffer, std::size_t Size) const noexcept
    {
        if (!Address || !Buffer || Size == 0) return false;
        std::memcpy(Buffer, reinterpret_cast<const void*>(Address), Size);
        return true;
    }

    bool TryReadRaw(std::uintptr_t Address, void* Buffer, std::size_t Size) const noexcept
    {
        if (!Buffer || Size == 0) return false;
        if (!IsValidAddress(Address, Size)) return false;
        std::memcpy(Buffer, reinterpret_cast<const void*>(Address), Size);
        return true;
    }

    template<typename T>
    void Write(std::uintptr_t Address, const T& Value) const noexcept
    {
        std::memcpy(reinterpret_cast<void*>(Address), &Value, sizeof(T));
    }

    template<typename T>
    void Write(std::uintptr_t Base, std::ptrdiff_t Offset, const T& Value) const noexcept
    {
        Write<T>(Base + Offset, Value);
    }

    bool WriteRaw(std::uintptr_t Address, const void* Buffer, std::size_t Size) const noexcept
    {
        if (!Address || !Buffer || Size == 0) return false;
        std::memcpy(reinterpret_cast<void*>(Address), Buffer, Size);
        return true;
    }

    bool WriteProtected(std::uintptr_t Address, const void* Buffer, std::size_t Size) const noexcept
    {
        if (!Address || !Buffer || Size == 0) return false;
        DWORD OldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(Address), Size, PAGE_EXECUTE_READWRITE, &OldProtect))
            return false;
        std::memcpy(reinterpret_cast<void*>(Address), Buffer, Size);
        DWORD Restore = 0;
        VirtualProtect(reinterpret_cast<void*>(Address), Size, OldProtect, &Restore);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(Address), Size);
        return true;
    }

    bool PatchBytes(std::uintptr_t Address, std::initializer_list<std::uint8_t> Bytes) const noexcept
    {
        return WriteProtected(Address, std::data(Bytes), Bytes.size());
    }

    bool NopRange(std::uintptr_t Address, std::size_t Size) const
    {
        std::vector<std::uint8_t> Nops(Size, 0x90);
        return WriteProtected(Address, Nops.data(), Nops.size());
    }

    template<typename T>
    T ReadChain(std::uintptr_t Base, std::initializer_list<std::ptrdiff_t> Offsets) const noexcept
    {
        if (Offsets.size() == 0) return Read<T>(Base);
        std::uintptr_t Current = Base;
        const auto* It = Offsets.begin();
        const auto* End = Offsets.end();
        const auto* Last = End - 1;
        while (It != Last)
        {
            Current = Read<std::uintptr_t>(Current + *It);
            if (Current == 0) return T{};
            ++It;
        }
        return Read<T>(Current + *Last);
    }

    template<typename T>
    T direct(std::uintptr_t Address) const noexcept
    {
        return Read<T>(Address);
    }

    bool direct(std::uintptr_t Address, void* Buffer, std::size_t Size) const noexcept
    {
        return ReadRaw(Address, Buffer, Size);
    }

    template<typename T>
    T fast(std::uintptr_t Address) const noexcept
    {
        return ReadCachedLockFree<T, 10'000LL>(Address, FastCacheSlots);
    }

    template<typename T>
    T debit(std::uintptr_t Address) const noexcept
    {
        return ReadCachedLockFree<T, 2'000'000LL>(Address, FastCacheSlots);
    }

    template<typename T>
    T medium(std::uintptr_t Address) const noexcept
    {
        return ReadCachedLockFree<T, 500'000'000LL>(Address, MediumCacheSlots);
    }

    template<typename T>
    T slow(std::uintptr_t Address) const noexcept
    {
        return ReadCachedLockFree<T, 15'000'000'000LL>(Address, SlowCacheSlots);
    }

    void cache_clear() const noexcept
    {
        ClearSlots(FastCacheSlots);
        ClearSlots(MediumCacheSlots);
        ClearSlots(SlowCacheSlots);
    }

    void cache_clear_fast() const noexcept { ClearSlots(FastCacheSlots); }
    void cache_clear_medium() const noexcept { ClearSlots(MediumCacheSlots); }
    void cache_clear_slow() const noexcept { ClearSlots(SlowCacheSlots); }

    std::string ReadCString(std::uintptr_t Address, std::size_t MaxLen = 512) const
    {
        std::string Result;
        if (!Address) return Result;
        Result.reserve(64);
        for (std::size_t Idx = 0; Idx < MaxLen; ++Idx)
        {
            const char C = *reinterpret_cast<const char*>(Address + Idx);
            if (C == 0) break;
            Result.push_back(C);
        }
        return Result;
    }

    std::wstring ReadWString(std::uintptr_t Address, std::size_t MaxLen = 512) const
    {
        std::wstring Result;
        if (!Address) return Result;
        Result.reserve(64);
        for (std::size_t Idx = 0; Idx < MaxLen; ++Idx)
        {
            const wchar_t C = *reinterpret_cast<const wchar_t*>(Address + Idx * sizeof(wchar_t));
            if (C == 0) break;
            Result.push_back(C);
        }
        return Result;
    }

    std::wstring ReadFString(std::uintptr_t Address) const
    {
        if (!Address) return {};
        wchar_t* Data = Read<wchar_t*>(Address);
        const std::int32_t Num = Read<std::int32_t>(Address + sizeof(wchar_t*));
        if (!Data || Num <= 0 || Num > 0x100000) return {};
        const std::size_t Length = static_cast<std::size_t>(Num - 1);
        return std::wstring(Data, Length);
    }

    std::uintptr_t ScanPatternIDA(std::string_view Signature) const
    {
        return ScanPatternIDARange(Signature, TextSection.Base, TextSection.Size);
    }

    std::uintptr_t ScanPatternIDARange(std::string_view Signature, std::uintptr_t RangeBase, std::size_t RangeSize) const
    {
        const auto Compiled = CompileIDA(Signature);
        if (Compiled.Bytes.empty() || RangeBase == 0 || RangeSize < Compiled.Bytes.size()) return 0;
        return ScanCompiled(Compiled, RangeBase, RangeSize);
    }

    std::vector<std::uintptr_t> FindAllIDA(std::string_view Signature) const
    {
        return FindAllIDARange(Signature, TextSection.Base, TextSection.Size);
    }

    std::vector<std::uintptr_t> FindAllIDARange(std::string_view Signature, std::uintptr_t RangeBase, std::size_t RangeSize) const
    {
        const auto Compiled = CompileIDA(Signature);
        if (Compiled.Bytes.empty() || RangeBase == 0 || RangeSize < Compiled.Bytes.size()) return {};
        return ScanAllCompiled(Compiled, RangeBase, RangeSize);
    }

    std::uintptr_t ScanPatternX64(std::span<const std::uint8_t> Pattern, std::string_view Mask) const
    {
        return ScanPatternX64Range(Pattern, Mask, TextSection.Base, TextSection.Size);
    }

    std::uintptr_t ScanPatternX64Range(std::span<const std::uint8_t> Pattern, std::string_view Mask,
                                       std::uintptr_t RangeBase, std::size_t RangeSize) const noexcept
    {
        if (Pattern.size() != Mask.size() || Pattern.empty() || RangeBase == 0 || RangeSize < Pattern.size())
            return 0;

        const auto* Start = reinterpret_cast<const std::uint8_t*>(RangeBase);
        const std::size_t Last = RangeSize - Pattern.size();
        const std::size_t Length = Pattern.size();

        std::size_t FirstFixed = 0;
        while (FirstFixed < Length && Mask[FirstFixed] == '?') ++FirstFixed;
        const std::uint8_t Anchor = FirstFixed < Length ? Pattern[FirstFixed] : 0;
        const bool HasAnchor = FirstFixed < Length;

        for (std::size_t Idx = 0; Idx <= Last; ++Idx)
        {
            if (HasAnchor && Start[Idx + FirstFixed] != Anchor) continue;
            bool Match = true;
            for (std::size_t Inner = 0; Inner < Length; ++Inner)
            {
                if (Mask[Inner] == '?') continue;
                if (Start[Idx + Inner] != Pattern[Inner]) { Match = false; break; }
            }
            if (Match) return RangeBase + Idx;
        }
        return 0;
    }

    std::uintptr_t ScanPatternIDAParallel(std::string_view Signature, unsigned int ThreadCount = 0) const
    {
        return ScanPatternIDAParallelRange(Signature, TextSection.Base, TextSection.Size, ThreadCount);
    }

    std::uintptr_t ScanPatternIDAParallelRange(std::string_view Signature, std::uintptr_t RangeBase, std::size_t RangeSize,
                                                unsigned int ThreadCount = 0) const
    {
        const auto Compiled = CompileIDA(Signature);
        if (Compiled.Bytes.empty() || RangeBase == 0 || RangeSize < Compiled.Bytes.size()) return 0;

        if (ThreadCount == 0)
        {
            const unsigned int HwThreads = std::thread::hardware_concurrency();
            ThreadCount = HwThreads > 2 ? HwThreads - 1 : 2;
        }

        const std::size_t PatternLen = Compiled.Bytes.size();
        const std::size_t Overlap = PatternLen - 1;
        const std::size_t ChunkSize = (RangeSize + ThreadCount - 1) / ThreadCount;

        std::atomic<std::uintptr_t> Found{ 0 };
        std::vector<std::thread> Workers;
        Workers.reserve(ThreadCount);

        for (unsigned int WorkerIdx = 0; WorkerIdx < ThreadCount; ++WorkerIdx)
        {
            const std::size_t ChunkStart = WorkerIdx * ChunkSize;
            if (ChunkStart >= RangeSize) break;
            std::size_t ChunkEnd = ChunkStart + ChunkSize + Overlap;
            if (ChunkEnd > RangeSize) ChunkEnd = RangeSize;
            const std::size_t LocalSize = ChunkEnd - ChunkStart;

            Workers.emplace_back([&Compiled, &Found, RangeBase, ChunkStart, LocalSize]()
            {
                if (Found.load(std::memory_order_acquire) != 0) return;
                const auto Hit = ScanCompiled(Compiled, RangeBase + ChunkStart, LocalSize);
                if (Hit == 0) return;
                std::uintptr_t Expected = 0;
                Found.compare_exchange_strong(Expected, Hit, std::memory_order_acq_rel);
            });
        }
        for (auto& Worker : Workers)
            if (Worker.joinable()) Worker.join();

        return Found.load(std::memory_order_acquire);
    }

    std::uintptr_t ResolveRelative(std::uintptr_t InstructionAddress, std::size_t OperandOffset, std::size_t InstructionSize) const noexcept
    {
        if (InstructionAddress == 0) return 0;
        const auto Relative = Read<std::int32_t>(InstructionAddress + OperandOffset);
        return InstructionAddress + InstructionSize + static_cast<std::intptr_t>(Relative);
    }

    std::uintptr_t ResolveLea(std::uintptr_t InstructionAddress) const noexcept
    {
        return ResolveRelative(InstructionAddress, 3, 7);
    }

    std::uintptr_t ResolveCall(std::uintptr_t CallAddress) const noexcept
    {
        return ResolveRelative(CallAddress, 1, 5);
    }

    bool IsValidAddress(std::uintptr_t Address, std::size_t Size = 1) const noexcept
    {
        if (Address == 0) return false;
        MEMORY_BASIC_INFORMATION Mbi{};
        if (!VirtualQuery(reinterpret_cast<void*>(Address), &Mbi, sizeof(Mbi))) return false;
        if (Mbi.State != MEM_COMMIT) return false;
        constexpr DWORD BadFlags = PAGE_NOACCESS | PAGE_GUARD;
        if (Mbi.Protect & BadFlags) return false;
        const auto RegionEnd = reinterpret_cast<std::uintptr_t>(Mbi.BaseAddress) + Mbi.RegionSize;
        return (Address + Size) <= RegionEnd;
    }

    bool IsInModule(std::uintptr_t Address) const noexcept
    {
        return BaseAddress != 0 && Address >= BaseAddress && Address < (BaseAddress + ModuleSize);
    }

    bool IsInText(std::uintptr_t Address) const noexcept
    {
        return TextSection.Base != 0 && Address >= TextSection.Base && Address < (TextSection.Base + TextSection.Size);
    }

private:
    static constexpr std::size_t FastCacheSlotCount = 4096;
    static constexpr std::size_t MediumCacheSlotCount = 4096;
    static constexpr std::size_t SlowCacheSlotCount = 2048;
    static constexpr std::size_t CacheSlotDataBytes = 32;

    struct alignas(64) AtomicCacheSlot
    {
        std::atomic<std::uint64_t> Key{ 0 };
        std::atomic<std::uint32_t> Version{ 0 };
        std::atomic<std::uint32_t> DataSize{ 0 };
        std::atomic<std::int64_t> TimestampNs{ 0 };
        alignas(16) std::array<std::uint8_t, CacheSlotDataBytes> Data{};
    };

    static std::int64_t CacheNowNs() noexcept
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    static constexpr std::size_t CacheSlotIndex(std::uintptr_t Address, std::size_t SlotCount) noexcept
    {
        const std::uint64_t Mixed = (static_cast<std::uint64_t>(Address) >> 4) * 0x9E3779B97F4A7C15ULL;
        return static_cast<std::size_t>(Mixed) & (SlotCount - 1);
    }

    template<typename T, std::int64_t MaxAgeNs, std::size_t SlotCount>
    T ReadCachedLockFree(std::uintptr_t Address, std::array<AtomicCacheSlot, SlotCount>& Slots) const noexcept
    {
        static_assert(std::is_trivially_copyable_v<T>, "Cached type must be trivially copyable");
        static_assert(sizeof(T) <= CacheSlotDataBytes, "Cached type exceeds slot data buffer");

        if (Address == 0) return T{};

        const std::size_t Idx = CacheSlotIndex(Address, SlotCount);
        AtomicCacheSlot& Slot = Slots[Idx];

        for (int Attempt = 0; Attempt < 4; ++Attempt)
        {
            const std::uint32_t V1 = Slot.Version.load(std::memory_order_acquire);
            if (V1 & 1u) continue;

            if (Slot.Key.load(std::memory_order_relaxed) != static_cast<std::uint64_t>(Address))
                break;
            if (Slot.DataSize.load(std::memory_order_relaxed) != sizeof(T))
                break;

            const std::int64_t Now = CacheNowNs();
            if ((Now - Slot.TimestampNs.load(std::memory_order_relaxed)) > MaxAgeNs)
                break;

            T Result;
            std::memcpy(&Result, Slot.Data.data(), sizeof(T));

            const std::uint32_t V2 = Slot.Version.load(std::memory_order_acquire);
            if (V1 == V2) return Result;
        }

        T Fresh = Read<T>(Address);
        StoreToSlot(Slot, Address, Fresh);
        return Fresh;
    }

    template<typename T>
    static void StoreToSlot(AtomicCacheSlot& Slot, std::uintptr_t Address, const T& Value) noexcept
    {
        std::uint32_t V = Slot.Version.load(std::memory_order_relaxed);
        if (V & 1u) return;
        if (!Slot.Version.compare_exchange_strong(V, V + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            return;

        Slot.Key.store(static_cast<std::uint64_t>(Address), std::memory_order_relaxed);
        Slot.DataSize.store(sizeof(T), std::memory_order_relaxed);
        std::memcpy(Slot.Data.data(), &Value, sizeof(T));
        Slot.TimestampNs.store(CacheNowNs(), std::memory_order_relaxed);

        Slot.Version.store(V + 2, std::memory_order_release);
    }

    template<std::size_t SlotCount>
    static void ClearSlots(std::array<AtomicCacheSlot, SlotCount>& Slots) noexcept
    {
        for (auto& Slot : Slots)
        {
            std::uint32_t V = Slot.Version.load(std::memory_order_relaxed);
            if (V & 1u) continue;
            if (!Slot.Version.compare_exchange_strong(V, V + 1, std::memory_order_acq_rel, std::memory_order_relaxed))
                continue;
            Slot.Key.store(0, std::memory_order_relaxed);
            Slot.DataSize.store(0, std::memory_order_relaxed);
            Slot.TimestampNs.store(0, std::memory_order_relaxed);
            Slot.Version.store(V + 2, std::memory_order_release);
        }
    }

    static Section GetSectionFromHandle(HMODULE Handle, const char* SectionName) noexcept
    {
        Section Result{};
        if (!Handle || !SectionName) return Result;
        const auto Base = reinterpret_cast<std::uintptr_t>(Handle);
        const auto Dos = reinterpret_cast<PIMAGE_DOS_HEADER>(Base);
        if (Dos->e_magic != IMAGE_DOS_SIGNATURE) return Result;
        const auto Nt = reinterpret_cast<PIMAGE_NT_HEADERS>(Base + Dos->e_lfanew);
        if (Nt->Signature != IMAGE_NT_SIGNATURE) return Result;

        auto SectionPtr = IMAGE_FIRST_SECTION(Nt);
        const std::size_t TargetLen = std::strlen(SectionName);
        for (WORD Idx = 0; Idx < Nt->FileHeader.NumberOfSections; ++Idx, ++SectionPtr)
        {
            const char* Name = reinterpret_cast<const char*>(SectionPtr->Name);
            std::size_t NameLen = 0;
            while (NameLen < 8 && Name[NameLen] != 0) ++NameLen;
            if (NameLen != TargetLen) continue;
            if (std::memcmp(Name, SectionName, TargetLen) != 0) continue;
            Result.Base = Base + SectionPtr->VirtualAddress;
            Result.Size = SectionPtr->Misc.VirtualSize;
            return Result;
        }
        return Result;
    }

    struct CompiledPattern
    {
        std::vector<std::uint8_t> Bytes;
        std::vector<std::uint8_t> IsWildcard;
    };

    static CompiledPattern CompileIDA(std::string_view Signature)
    {
        CompiledPattern Result;
        Result.Bytes.reserve(Signature.size() / 3 + 1);
        Result.IsWildcard.reserve(Signature.size() / 3 + 1);

        auto Hex = [](char C) -> int
        {
            if (C >= '0' && C <= '9') return C - '0';
            if (C >= 'A' && C <= 'F') return C - 'A' + 10;
            if (C >= 'a' && C <= 'f') return C - 'a' + 10;
            return -1;
        };

        const std::size_t Length = Signature.size();
        std::size_t Cursor = 0;
        while (Cursor < Length)
        {
            while (Cursor < Length && (Signature[Cursor] == ' ' || Signature[Cursor] == '\t')) ++Cursor;
            if (Cursor >= Length) break;

            if (Signature[Cursor] == '?')
            {
                Result.Bytes.push_back(0);
                Result.IsWildcard.push_back(1);
                ++Cursor;
                if (Cursor < Length && Signature[Cursor] == '?') ++Cursor;
                continue;
            }

            if (Cursor + 1 >= Length) break;
            const int High = Hex(Signature[Cursor]);
            const int Low = Hex(Signature[Cursor + 1]);
            Cursor += 2;
            if (High < 0 || Low < 0) continue;
            Result.Bytes.push_back(static_cast<std::uint8_t>((High << 4) | Low));
            Result.IsWildcard.push_back(0);
        }
        return Result;
    }

    static std::uintptr_t ScanCompiled(const CompiledPattern& Compiled, std::uintptr_t RangeBase, std::size_t RangeSize) noexcept
    {
        const std::size_t PatternLen = Compiled.Bytes.size();
        if (PatternLen == 0 || RangeSize < PatternLen) return 0;

        const auto* Start = reinterpret_cast<const std::uint8_t*>(RangeBase);
        const std::size_t Last = RangeSize - PatternLen;

        std::size_t FirstFixed = 0;
        while (FirstFixed < PatternLen && Compiled.IsWildcard[FirstFixed]) ++FirstFixed;
        const std::uint8_t Anchor = FirstFixed < PatternLen ? Compiled.Bytes[FirstFixed] : 0;
        const bool HasAnchor = FirstFixed < PatternLen;

        for (std::size_t Idx = 0; Idx <= Last; ++Idx)
        {
            if (HasAnchor && Start[Idx + FirstFixed] != Anchor) continue;
            bool Match = true;
            for (std::size_t Inner = 0; Inner < PatternLen; ++Inner)
            {
                if (Compiled.IsWildcard[Inner]) continue;
                if (Start[Idx + Inner] != Compiled.Bytes[Inner]) { Match = false; break; }
            }
            if (Match) return RangeBase + Idx;
        }
        return 0;
    }

    static std::vector<std::uintptr_t> ScanAllCompiled(const CompiledPattern& Compiled, std::uintptr_t RangeBase, std::size_t RangeSize)
    {
        std::vector<std::uintptr_t> Hits;
        const std::size_t PatternLen = Compiled.Bytes.size();
        if (PatternLen == 0 || RangeSize < PatternLen) return Hits;

        const auto* Start = reinterpret_cast<const std::uint8_t*>(RangeBase);
        const std::size_t Last = RangeSize - PatternLen;

        std::size_t FirstFixed = 0;
        while (FirstFixed < PatternLen && Compiled.IsWildcard[FirstFixed]) ++FirstFixed;
        const std::uint8_t Anchor = FirstFixed < PatternLen ? Compiled.Bytes[FirstFixed] : 0;
        const bool HasAnchor = FirstFixed < PatternLen;

        for (std::size_t Idx = 0; Idx <= Last; ++Idx)
        {
            if (HasAnchor && Start[Idx + FirstFixed] != Anchor) continue;
            bool Match = true;
            for (std::size_t Inner = 0; Inner < PatternLen; ++Inner)
            {
                if (Compiled.IsWildcard[Inner]) continue;
                if (Start[Idx + Inner] != Compiled.Bytes[Inner]) { Match = false; break; }
            }
            if (Match) Hits.push_back(RangeBase + Idx);
        }
        return Hits;
    }

    std::uintptr_t BaseAddress = 0;
    std::size_t ModuleSize = 0;
    Section TextSection{};
    Section DataSection{};
    Section RDataSection{};
    std::atomic<bool> Initialized{ false };

    mutable std::array<AtomicCacheSlot, FastCacheSlotCount> FastCacheSlots{};
    mutable std::array<AtomicCacheSlot, MediumCacheSlotCount> MediumCacheSlots{};
    mutable std::array<AtomicCacheSlot, SlowCacheSlotCount> SlowCacheSlots{};
};

inline Memory& mem = Memory::Get();

