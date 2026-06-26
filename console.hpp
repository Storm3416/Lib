#pragma once

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

class Console
{
public:
    enum class Level
    {
        Info,
        Success,
        Warning,
        Error,
        Debug
    };

    struct FlagsType
    {
        static constexpr Level Info = Level::Info;
        static constexpr Level Success = Level::Success;
        static constexpr Level Warning = Level::Warning;
        static constexpr Level Error = Level::Error;
        static constexpr Level Debug = Level::Debug;
    };

    static constexpr FlagsType Flags{};

    bool setup(const char* Title = "Console") noexcept
    {
        if (Attached.exchange(true, std::memory_order_acq_rel))
            return true;

#if defined(_WIN32)
        const bool HasConsole = GetConsoleWindow() != nullptr;
        OwnsConsole = !HasConsole;

        if (!HasConsole && !AllocConsole())
        {
            Attached.store(false, std::memory_order_release);
            return false;
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

    void close() noexcept
    {
        if (!Attached.exchange(false, std::memory_order_acq_rel))
            return;
#if defined(_WIN32)
        if (OwnsConsole) FreeConsole();
        OwnsConsole = false;
#endif
    }

    bool is_attached() const noexcept
    {
        return Attached.load(std::memory_order_acquire);
    }

    void print(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Info, Format, Args);
        va_end(Args);
    }

    void print(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Info, Message.c_str());
    }

    void print(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Info, Buffer);
    }

    void print(Level Flag, const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Flag, Format, Args);
        va_end(Args);
    }

    void print(Level Flag, const std::string& Message) noexcept
    {
        PrintRawImpl(Flag, Message.c_str());
    }

    void print(Level Flag, char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Flag, Buffer);
    }

    void info(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Info, Format, Args);
        va_end(Args);
    }

    void info(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Info, Message.c_str());
    }

    void info(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Info, Buffer);
    }

    void success(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Success, Format, Args);
        va_end(Args);
    }

    void success(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Success, Message.c_str());
    }

    void success(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Success, Buffer);
    }

    void warning(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Warning, Format, Args);
        va_end(Args);
    }

    void warning(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Warning, Message.c_str());
    }

    void warning(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Warning, Buffer);
    }

    void error(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Error, Format, Args);
        va_end(Args);
    }

    void error(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Error, Message.c_str());
    }

    void error(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Error, Buffer);
    }

    void debug(const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        PrintImpl(Level::Debug, Format, Args);
        va_end(Args);
    }

    void debug(const std::string& Message) noexcept
    {
        PrintRawImpl(Level::Debug, Message.c_str());
    }

    void debug(char Character) noexcept
    {
        const char Buffer[2] = { Character, '\0' };
        PrintRawImpl(Level::Debug, Buffer);
    }

    void time(std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        TimeImpl(Level::Info, Format, MinIntervalMs, Format, Args);
        va_end(Args);
    }

    void time(Level Lv, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        TimeImpl(Lv, Format, MinIntervalMs, Format, Args);
        va_end(Args);
    }

    void time(const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        TimeImpl(Level::Info, Key, MinIntervalMs, Format, Args);
        va_end(Args);
    }

    void time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept
    {
        va_list Args;
        va_start(Args, Format);
        TimeImpl(Lv, Key, MinIntervalMs, Format, Args);
        va_end(Args);
    }

    void time(std::uint32_t MinIntervalMs, const std::string& Message) noexcept
    {
        TimeRawImpl(Level::Info, Message.c_str(), MinIntervalMs, Message.c_str());
    }

    void time(Level Lv, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
    {
        TimeRawImpl(Lv, Message.c_str(), MinIntervalMs, Message.c_str());
    }

    void time(const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
    {
        TimeRawImpl(Level::Info, Key, MinIntervalMs, Message.c_str());
    }

    void time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept
    {
        TimeRawImpl(Lv, Key, MinIntervalMs, Message.c_str());
    }

    void newline() noexcept
    {
        std::fputc('\n', stdout);
    }

    void clear() noexcept
    {
        std::fputs("\033[2J\033[H", stdout);
    }

    void enable_timestamp(bool On) noexcept { ShowTimestamp.store(On, std::memory_order_release); }
    void enable_tag(bool On) noexcept { ShowTag.store(On, std::memory_order_release); }

private:
    struct Color3
    {
        std::uint8_t R, G, B;
    };

    static constexpr Color3 StartColor(Level L) noexcept
    {
        switch (L)
        {
        case Level::Info:    return { 130, 200, 255 };
        case Level::Success: return { 130, 255, 170 };
        case Level::Warning: return { 255, 230, 130 };
        case Level::Error:   return { 255, 130, 130 };
        case Level::Debug:   return { 200, 160, 255 };
        }
        return { 255, 255, 255 };
    }

    static constexpr Color3 EndColor(Level L) noexcept
    {
        switch (L)
        {
        case Level::Info:    return { 60, 110, 220 };
        case Level::Success: return { 50, 170, 90 };
        case Level::Warning: return { 230, 130, 40 };
        case Level::Error:   return { 200, 30, 60 };
        case Level::Debug:   return { 120, 70, 200 };
        }
        return { 255, 255, 255 };
    }

    static constexpr const char* TagFor(Level L) noexcept
    {
        switch (L)
        {
        case Level::Info:    return "[INFO]";
        case Level::Success: return "[SUCCESS]";
        case Level::Warning: return "[WARNING]";
        case Level::Error:   return "[ERROR]";
        case Level::Debug:   return "[DEBUG]";
        }
        return "[?]";
    }

    static void PrintTimestamp() noexcept
    {
        std::time_t Now = std::time(nullptr);
        std::tm Local{};
#if defined(_WIN32)
        localtime_s(&Local, &Now);
#else
        localtime_r(&Now, &Local);
#endif
        std::fprintf(stdout, "\033[38;2;120;120;130m[%02d:%02d:%02d]\033[0m ", Local.tm_hour, Local.tm_min, Local.tm_sec);
    }

    static void PrintGradientTag(const char* Tag, Level Lv) noexcept
    {
        const Color3 Start = StartColor(Lv);
        const Color3 End = EndColor(Lv);
        const std::size_t Length = std::strlen(Tag);
        const float Denom = Length > 1 ? static_cast<float>(Length - 1) : 1.0f;

        for (std::size_t Idx = 0; Idx < Length; ++Idx)
        {
            const float T = static_cast<float>(Idx) / Denom;
            const int R = static_cast<int>(Start.R + (static_cast<int>(End.R) - static_cast<int>(Start.R)) * T);
            const int G = static_cast<int>(Start.G + (static_cast<int>(End.G) - static_cast<int>(Start.G)) * T);
            const int B = static_cast<int>(Start.B + (static_cast<int>(End.B) - static_cast<int>(Start.B)) * T);
            std::fprintf(stdout, "\033[1;38;2;%d;%d;%dm%c", R, G, B, Tag[Idx]);
        }

        std::fputs("\033[0m", stdout);
        constexpr std::size_t Target = 9;
        for (std::size_t Pad = Length; Pad < Target; ++Pad)
            std::fputc(' ', stdout);
        std::fputc(' ', stdout);
    }

    void PrintImpl(Level Lv, const char* Format, va_list Args) noexcept
    {
        if (!Attached.load(std::memory_order_acquire)) return;

        if (ShowTimestamp.load(std::memory_order_relaxed)) PrintTimestamp();
        if (ShowTag.load(std::memory_order_relaxed)) PrintGradientTag(TagFor(Lv), Lv);

        char Buffer[4096];
        std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
        Buffer[sizeof(Buffer) - 1] = '\0';

        std::fputs(Buffer, stdout);
        std::fputc('\n', stdout);
    }

    void PrintRawImpl(Level Lv, const char* Text) noexcept
    {
        if (!Attached.load(std::memory_order_acquire)) return;
        if (!Text) return;

        if (ShowTimestamp.load(std::memory_order_relaxed)) PrintTimestamp();
        if (ShowTag.load(std::memory_order_relaxed)) PrintGradientTag(TagFor(Lv), Lv);

        std::fputs(Text, stdout);
        std::fputc('\n', stdout);
    }

    void TimeRawImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Text) noexcept
    {
        if (!Attached.load(std::memory_order_acquire)) return;
        if (!Key) Key = "";
        if (!Text) return;

        const std::uint64_t Hash = FnvHash(Text);
        const std::uint64_t KeyHash = FnvHash(Key);
        const std::uint64_t Slot = KeyHash % kTimeSlots;
        const std::uint64_t NowMs = NowMillis();

        TimeSlot& S = TimeSlots[Slot];
        const std::uint64_t LastMs = S.LastMs.load(std::memory_order_acquire);
        const std::uint64_t LastHash = S.LastHash.load(std::memory_order_acquire);
        const std::uint64_t StoredKey = S.KeyHash.load(std::memory_order_acquire);

        const bool DifferentKey = (StoredKey != KeyHash);
        const bool DifferentMsg = (LastHash != Hash);
        const bool TimedOut = (NowMs - LastMs) >= MinIntervalMs;

        if (!(DifferentKey || DifferentMsg || TimedOut)) return;

        S.KeyHash.store(KeyHash, std::memory_order_release);
        S.LastHash.store(Hash, std::memory_order_release);
        S.LastMs.store(NowMs, std::memory_order_release);

        if (ShowTimestamp.load(std::memory_order_relaxed)) PrintTimestamp();
        if (ShowTag.load(std::memory_order_relaxed)) PrintGradientTag(TagFor(Lv), Lv);
        std::fputs(Text, stdout);
        std::fputc('\n', stdout);
    }

    void TimeImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs,
        const char* Format, va_list Args) noexcept
    {
        if (!Attached.load(std::memory_order_acquire)) return;
        if (!Key) Key = "";

        char Buffer[4096];
        std::vsnprintf(Buffer, sizeof(Buffer), Format, Args);
        Buffer[sizeof(Buffer) - 1] = '\0';

        const std::uint64_t Hash = FnvHash(Buffer);
        const std::uint64_t KeyHash = FnvHash(Key);
        const std::uint64_t Slot = KeyHash % kTimeSlots;
        const std::uint64_t NowMs = NowMillis();

        TimeSlot& S = TimeSlots[Slot];
        const std::uint64_t LastMs = S.LastMs.load(std::memory_order_acquire);
        const std::uint64_t LastHash = S.LastHash.load(std::memory_order_acquire);
        const std::uint64_t StoredKey = S.KeyHash.load(std::memory_order_acquire);

        const bool DifferentKey = (StoredKey != KeyHash);
        const bool DifferentMsg = (LastHash != Hash);
        const bool TimedOut = (NowMs - LastMs) >= MinIntervalMs;

        if (!(DifferentKey || DifferentMsg || TimedOut)) return;

        S.KeyHash.store(KeyHash, std::memory_order_release);
        S.LastHash.store(Hash, std::memory_order_release);
        S.LastMs.store(NowMs, std::memory_order_release);

        if (ShowTimestamp.load(std::memory_order_relaxed)) PrintTimestamp();
        if (ShowTag.load(std::memory_order_relaxed)) PrintGradientTag(TagFor(Lv), Lv);
        std::fputs(Buffer, stdout);
        std::fputc('\n', stdout);
    }

    static std::uint64_t FnvHash(const char* s) noexcept
    {
        std::uint64_t H = 1469598103934665603ull;
        if (!s) return H ? H : 1;
        while (*s) { H ^= static_cast<unsigned char>(*s++); H *= 1099511628211ull; }
        return H ? H : 1;
    }

    static std::uint64_t NowMillis() noexcept
    {
#if defined(_WIN32)
        return static_cast<std::uint64_t>(GetTickCount64());
#else
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
#endif
    }

    struct TimeSlot
    {
        std::atomic<std::uint64_t> KeyHash{ 0 };
        std::atomic<std::uint64_t> LastHash{ 0 };
        std::atomic<std::uint64_t> LastMs{ 0 };
    };

    static constexpr std::size_t kTimeSlots = 256;
    mutable TimeSlot TimeSlots[kTimeSlots]{};

    std::atomic<bool> Attached{ false };
    std::atomic<bool> ShowTimestamp{ true };
    std::atomic<bool> ShowTag{ true };
#if defined(_WIN32)
    bool OwnsConsole{ false };
#endif
};

inline Console console;
