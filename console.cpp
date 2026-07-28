#include "console.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace
{
    constexpr std::size_t kMessageMax = 4096;
    constexpr std::size_t kPrefixMax = 256;
    constexpr std::size_t kLineMax = kMessageMax + kPrefixMax;

    struct Color3
    {
        std::uint8_t R, G, B;
    };

    constexpr Color3 StartColor(Console::Level L) noexcept
    {
        switch (L)
        {
        case Console::Level::Info:    return { 130, 200, 255 };
        case Console::Level::Success: return { 130, 255, 170 };
        case Console::Level::Warning: return { 255, 230, 130 };
        case Console::Level::Error:   return { 255, 130, 130 };
        case Console::Level::Debug:   return { 200, 160, 255 };
        }
        return { 255, 255, 255 };
    }

    constexpr Color3 EndColor(Console::Level L) noexcept
    {
        switch (L)
        {
        case Console::Level::Info:    return { 60, 110, 220 };
        case Console::Level::Success: return { 50, 170, 90 };
        case Console::Level::Warning: return { 230, 130, 40 };
        case Console::Level::Error:   return { 200, 30, 60 };
        case Console::Level::Debug:   return { 120, 70, 200 };
        }
        return { 255, 255, 255 };
    }

    constexpr const char* TagFor(Console::Level L) noexcept
    {
        switch (L)
        {
        case Console::Level::Info:    return "[INFO]";
        case Console::Level::Success: return "[SUCCESS]";
        case Console::Level::Warning: return "[WARNING]";
        case Console::Level::Error:   return "[ERROR]";
        case Console::Level::Debug:   return "[DEBUG]";
        }
        return "[?]";
    }

    std::size_t ClampWritten(int Written, std::size_t Cap) noexcept
    {
        if (Written < 0 || Cap == 0) return 0;
        const std::size_t Count = static_cast<std::size_t>(Written);
        return Count >= Cap ? Cap - 1 : Count;
    }

    std::size_t AppendTimestamp(char* Dst, std::size_t Cap) noexcept
    {
        std::time_t Now = std::time(nullptr);
        std::tm Local{};
#if defined(_WIN32)
        localtime_s(&Local, &Now);
#else
        localtime_r(&Now, &Local);
#endif
        return ClampWritten(std::snprintf(Dst, Cap, "\033[38;2;120;120;130m[%02d:%02d:%02d]\033[0m ",
            Local.tm_hour, Local.tm_min, Local.tm_sec), Cap);
    }

    std::size_t AppendGradientTag(char* Dst, std::size_t Cap, const char* Tag, Console::Level Lv) noexcept
    {
        const Color3 Start = StartColor(Lv);
        const Color3 End = EndColor(Lv);
        const std::size_t Length = std::strlen(Tag);
        const float Denom = Length > 1 ? static_cast<float>(Length - 1) : 1.0f;

        std::size_t Pos = 0;
        for (std::size_t Idx = 0; Idx < Length && Pos + 1 < Cap; ++Idx)
        {
            const float T = static_cast<float>(Idx) / Denom;
            const int R = static_cast<int>(Start.R + (static_cast<int>(End.R) - static_cast<int>(Start.R)) * T);
            const int G = static_cast<int>(Start.G + (static_cast<int>(End.G) - static_cast<int>(Start.G)) * T);
            const int B = static_cast<int>(Start.B + (static_cast<int>(End.B) - static_cast<int>(Start.B)) * T);
            Pos += ClampWritten(std::snprintf(Dst + Pos, Cap - Pos, "\033[1;38;2;%d;%d;%dm%c",
                R, G, B, Tag[Idx]), Cap - Pos);
        }

        Pos += ClampWritten(std::snprintf(Dst + Pos, Cap - Pos, "\033[0m"), Cap - Pos);

        constexpr std::size_t Target = 9;
        for (std::size_t Pad = Length; Pad < Target && Pos + 1 < Cap; ++Pad) Dst[Pos++] = ' ';
        if (Pos + 1 < Cap) Dst[Pos++] = ' ';
        if (Pos < Cap) Dst[Pos] = '\0';
        return Pos;
    }

    void FormatInto(char* Dst, std::size_t Cap, const char* Format, va_list Args) noexcept
    {
        if (!Dst || Cap == 0) return;

        const int Written = std::vsnprintf(Dst, Cap, Format, Args);
        if (Written >= 0 && static_cast<std::size_t>(Written) < Cap) return;

        Dst[Cap - 1] = '\0';
        if (Cap >= 4) std::memcpy(Dst + Cap - 4, "...", 4);
    }

    std::uint64_t FnvHash(const char* s) noexcept
    {
        std::uint64_t H = 1469598103934665603ull;
        if (!s) return H ? H : 1;
        while (*s) { H ^= static_cast<unsigned char>(*s++); H *= 1099511628211ull; }
        return H ? H : 1;
    }

    std::uint64_t NowMillis() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::uint64_t>(GetTickCount64());
#else
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
    }

    class OutputGuard
    {
    public:
        explicit OutputGuard(std::atomic_flag& Flag) noexcept : Lock(Flag)
        {
            while (Lock.test_and_set(std::memory_order_acquire))
                Lock.wait(true, std::memory_order_relaxed);
        }

        ~OutputGuard() noexcept
        {
            Lock.clear(std::memory_order_release);
            Lock.notify_one();
        }

        OutputGuard(const OutputGuard&) = delete;
        OutputGuard& operator=(const OutputGuard&) = delete;

    private:
        std::atomic_flag& Lock;
    };
}

bool Console::setup(const char* Title) noexcept
{
    if (Attached.exchange(true, std::memory_order_acq_rel))
        return true;

#if defined(_WIN32)
    OwnsConsole = false;
    if (GetConsoleCP() == 0)
    {
        if (AllocConsole())
        {
            OwnsConsole = true;
        }
        else if (GetLastError() != ERROR_ACCESS_DENIED)
        {
            Attached.store(false, std::memory_order_release);
            return false;
        }
    }

    if (Title) SetConsoleTitleA(Title);

    FILE* Stream = nullptr;
    freopen_s(&Stream, "CONOUT$", "w", stdout);
    freopen_s(&Stream, "CONOUT$", "w", stderr);
    freopen_s(&Stream, "CONIN$", "r", stdin);

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    HANDLE OutHandle = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD Mode = 0;
    if (OutHandle != INVALID_HANDLE_VALUE && GetConsoleMode(OutHandle, &Mode))
        SetConsoleMode(OutHandle, Mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    if (OutHandle != INVALID_HANDLE_VALUE) CloseHandle(OutHandle);
#else
    if (Title) std::printf("\033]0;%s\007", Title);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
    return true;
}

void Console::close() noexcept
{
    if (!Attached.exchange(false, std::memory_order_acq_rel))
        return;
#if defined(_WIN32)
    if (OwnsConsole) FreeConsole();
    OwnsConsole = false;
#endif
}

bool Console::is_attached() const noexcept
{
    return Attached.load(std::memory_order_acquire);
}

void Console::print(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Info, Format, Args);
    va_end(Args);
}

void Console::print(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Info, Message.c_str());
}

void Console::print(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Info, Buffer);
}

void Console::print(Level Flag, const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Flag, Format, Args);
    va_end(Args);
}

void Console::print(Level Flag, const std::string& Message) noexcept
{
    PrintRawImpl(Flag, Message.c_str());
}

void Console::print(Level Flag, char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Flag, Buffer);
}

void Console::info(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Info, Format, Args);
    va_end(Args);
}

void Console::info(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Info, Message.c_str());
}

void Console::info(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Info, Buffer);
}

void Console::success(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Success, Format, Args);
    va_end(Args);
}

void Console::success(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Success, Message.c_str());
}

void Console::success(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Success, Buffer);
}

void Console::warning(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Warning, Format, Args);
    va_end(Args);
}

void Console::warning(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Warning, Message.c_str());
}

void Console::warning(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Warning, Buffer);
}

void Console::error(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Error, Format, Args);
    va_end(Args);
}

void Console::error(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Error, Message.c_str());
}

void Console::error(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Error, Buffer);
}

void Console::debug(const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    PrintImpl(Level::Debug, Format, Args);
    va_end(Args);
}

void Console::debug(const std::string& Message) noexcept
{
    PrintRawImpl(Level::Debug, Message.c_str());
}

void Console::debug(char Character) noexcept
{
    const char Buffer[2] = { Character, '\0' };
    PrintRawImpl(Level::Debug, Buffer);
}

void Console::time(std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    TimeImpl(Level::Info, Format, MinIntervalMs, Format, Args);
    va_end(Args);
}

void Console::time(Level Lv, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    TimeImpl(Lv, Format, MinIntervalMs, Format, Args);
    va_end(Args);
}

void Console::time(const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    TimeImpl(Level::Info, Key, MinIntervalMs, Format, Args);
    va_end(Args);
}

void Console::time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
{
    va_list Args;
    va_start(Args, Format);
    TimeImpl(Lv, Key, MinIntervalMs, Format, Args);
    va_end(Args);
}

void Console::time(std::uint32_t MinIntervalMs, const std::string& Message) noexcept
{
    TimeRawImpl(Level::Info, Message.c_str(), MinIntervalMs, Message.c_str());
}

void Console::time(Level Lv, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
{
    TimeRawImpl(Lv, Message.c_str(), MinIntervalMs, Message.c_str());
}

void Console::time(const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
{
    TimeRawImpl(Level::Info, Key, MinIntervalMs, Message.c_str());
}

void Console::time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
{
    TimeRawImpl(Lv, Key, MinIntervalMs, Message.c_str());
}

void Console::newline() noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;

    OutputGuard Guard(OutLock);
    std::fputc('\n', stdout);
}

void Console::clear() noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;

    static constexpr char Sequence[] = "\033[2J\033[H";
    OutputGuard Guard(OutLock);
    std::fwrite(Sequence, 1, sizeof(Sequence) - 1, stdout);
}

void Console::enable_timestamp(bool On) noexcept
{
    ShowTimestamp.store(On, std::memory_order_release);
}

void Console::enable_tag(bool On) noexcept
{
    ShowTag.store(On, std::memory_order_release);
}

void Console::Emit(Level Lv, const char* Text) noexcept
{
    char Line[kLineMax];
    std::size_t Pos = 0;

    if (ShowTimestamp.load(std::memory_order_relaxed))
        Pos += AppendTimestamp(Line + Pos, sizeof(Line) - Pos);
    if (ShowTag.load(std::memory_order_relaxed))
        Pos += AppendGradientTag(Line + Pos, sizeof(Line) - Pos, TagFor(Lv), Lv);

    for (const char* Cursor = Text; *Cursor && Pos + 1 < sizeof(Line); ++Cursor)
        Line[Pos++] = *Cursor;
    Line[Pos++] = '\n';

    OutputGuard Guard(OutLock);
    std::fwrite(Line, 1, Pos, stdout);
}

bool Console::ShouldEmit(const char* Key, std::uint32_t MinIntervalMs) noexcept
{
    const std::uint64_t KeyHash = FnvHash(Key);
    const std::size_t Base = static_cast<std::size_t>(KeyHash % kTimeSlots);
    const std::uint64_t NowMs = NowMillis();

    TimeSlot* Slot = nullptr;
    TimeSlot* Victim = nullptr;
    std::uint64_t VictimMs = 0;
    bool Fresh = false;

    for (std::size_t Probe = 0; Probe < kProbeLimit; ++Probe)
    {
        TimeSlot& Candidate = TimeSlots[(Base + Probe) % kTimeSlots];
        const std::uint64_t Stored = Candidate.KeyHash.load(std::memory_order_acquire);

        if (Stored == KeyHash)
        {
            Slot = &Candidate;
            break;
        }

        if (Stored == 0)
        {
            std::uint64_t Expected = 0;
            if (Candidate.KeyHash.compare_exchange_strong(Expected, KeyHash,
                std::memory_order_acq_rel, std::memory_order_acquire))
            {
                Slot = &Candidate;
                Fresh = true;
                break;
            }
            if (Expected == KeyHash)
            {
                Slot = &Candidate;
                break;
            }
        }

        const std::uint64_t CandidateMs = Candidate.LastMs.load(std::memory_order_acquire);
        if (!Victim || CandidateMs < VictimMs)
        {
            Victim = &Candidate;
            VictimMs = CandidateMs;
        }
    }

    if (!Slot)
    {
        Slot = Victim ? Victim : &TimeSlots[Base];
        Slot->KeyHash.store(KeyHash, std::memory_order_release);
        Fresh = true;
    }

    if (!Fresh)
    {
        const std::uint64_t LastMs = Slot->LastMs.load(std::memory_order_acquire);
        if (NowMs - LastMs < MinIntervalMs) return false;
    }

    Slot->LastMs.store(NowMs, std::memory_order_release);
    return true;
}

void Console::PrintImpl(Level Lv, const char* Format, va_list Args) noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;
    if (!Format) return;

    char Message[kMessageMax];
    FormatInto(Message, sizeof(Message), Format, Args);
    Emit(Lv, Message);
}

void Console::PrintRawImpl(Level Lv, const char* Text) noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;
    if (!Text) return;

    Emit(Lv, Text);
}

void Console::TimeRawImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Text) noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;
    if (!Text) return;
    if (!ShouldEmit(Key ? Key : "", MinIntervalMs)) return;

    Emit(Lv, Text);
}

void Console::TimeImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Format, va_list Args) noexcept
{
    if (!Attached.load(std::memory_order_acquire)) return;
    if (!Format) return;
    if (!ShouldEmit(Key ? Key : "", MinIntervalMs)) return;

    char Message[kMessageMax];
    FormatInto(Message, sizeof(Message), Format, Args);
    Emit(Lv, Message);
}

Console console;
