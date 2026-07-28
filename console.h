#pragma once

#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
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

    bool setup(const char* Title = "Console") noexcept;
    void close() noexcept;
    bool is_attached() const noexcept;

    void print(const char* Format, ...) noexcept;
    void print(const std::string& Message) noexcept;
    void print(char Character) noexcept;
    void print(Level Flag, const char* Format, ...) noexcept;
    void print(Level Flag, const std::string& Message) noexcept;
    void print(Level Flag, char Character) noexcept;

    void info(const char* Format, ...) noexcept;
    void info(const std::string& Message) noexcept;
    void info(char Character) noexcept;

    void success(const char* Format, ...) noexcept;
    void success(const std::string& Message) noexcept;
    void success(char Character) noexcept;

    void warning(const char* Format, ...) noexcept;
    void warning(const std::string& Message) noexcept;
    void warning(char Character) noexcept;

    void error(const char* Format, ...) noexcept;
    void error(const std::string& Message) noexcept;
    void error(char Character) noexcept;

    void debug(const char* Format, ...) noexcept;
    void debug(const std::string& Message) noexcept;
    void debug(char Character) noexcept;

    void time(std::uint32_t MinIntervalMs, const char* Format, ...) noexcept;
    void time(Level Lv, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept;
    void time(const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept;
    void time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Format, ...) noexcept;
    void time(std::uint32_t MinIntervalMs, const std::string& Message) noexcept;
    void time(Level Lv, std::uint32_t MinIntervalMs, const std::string& Message) noexcept;
    void time(const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept;
    void time(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const std::string& Message) noexcept;

    void newline() noexcept;
    void clear() noexcept;

    void enable_timestamp(bool On) noexcept;
    void enable_tag(bool On) noexcept;

private:
    void PrintImpl(Level Lv, const char* Format, va_list Args) noexcept;
    void PrintRawImpl(Level Lv, const char* Text) noexcept;
    void TimeRawImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Text) noexcept;
    void TimeImpl(Level Lv, const char* Key, std::uint32_t MinIntervalMs, const char* Format, va_list Args) noexcept;

    void Emit(Level Lv, const char* Text) noexcept;
    bool ShouldEmit(const char* Key, std::uint32_t MinIntervalMs) noexcept;

    struct TimeSlot
    {
        std::atomic<std::uint64_t> KeyHash{ 0 };
        std::atomic<std::uint64_t> LastMs{ 0 };
    };

    static constexpr std::size_t kTimeSlots = 256;
    static constexpr std::size_t kProbeLimit = 8;
    TimeSlot TimeSlots[kTimeSlots]{};

    std::atomic_flag OutLock{};
    std::atomic<bool> Attached{ false };
    std::atomic<bool> ShowTimestamp{ true };
    std::atomic<bool> ShowTag{ true };
#if defined(_WIN32)
    bool OwnsConsole{ false };
#endif
};

extern Console console;
