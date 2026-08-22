#pragma once

#include <chrono>
#include <string>
#include <utility>

#include "Log.h"

// Measures an interval, in either of two shapes.
//
// Named, it is a scope timer, and the name is the whole of the call site: it
// logs "<name> took X" when it goes out of scope.
//
//     Timer timer("LoadScene()");
//
// Nameless, it is a stopwatch. It never logs; the caller reads the number and
// words its own message. That is what is wanted when the measurement belongs in
// a sentence of its own, under a different log category, or at a precision the
// automatic message does not carry — a sub-millisecond result reads as "0.0" in
// the message below and as something useful at two decimal places.
//
//     Timer timer;
//     LogMsg(LogSeverity::Info, LogRhi, "Created {} in {:.2f} ms", name, timer.ElapsedMs());
class Timer
{
public:
    // steady_clock rather than high_resolution_clock, which libstdc++ makes an
    // alias for system_clock — a clock the standard does not require to move
    // only forwards. An NTP correction landing mid-measurement then produces a
    // nonsense duration, and can produce a negative one. Measuring an interval
    // is the whole of what this class does, so it uses the clock that is
    // specified to be monotonic.
    using Clock = std::chrono::steady_clock;

    Timer() = default;

    explicit Timer(std::string name) : m_Name(std::move(name)) {}

    ~Timer() { EndTimer(); }

    // Copying one would log its message twice.
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    // Time since construction, or the final interval once the timer has ended.
    // Readable in every state and any number of times.
    float ElapsedMs() const
    {
        return std::chrono::duration<float, std::milli>(MeasuredEnd() - m_Start).count();
    }

    float ElapsedSeconds() const
    {
        return std::chrono::duration<float>(MeasuredEnd() - m_Start).count();
    }

    // Stops the clock, and logs now rather than at the end of the scope. Does
    // nothing on a second call, on a nameless timer, or after InvalidateTimer().
    void EndTimer()
    {
        static constexpr LogCategory LogTimer("Timer");

        if (m_bStopped)
            return;

        m_End = Clock::now();
        m_bStopped = true;

        if (m_bSuppressed || m_Name.empty())
            return;

        const float seconds = ElapsedSeconds();
        if (seconds < 1.f)
            LogMsg(LogSeverity::Info, LogTimer, "{} took {:.1f} milliseconds.", m_Name,
                   seconds * 1000.f);
        else
            LogMsg(LogSeverity::Info, LogTimer, "{} took {:.1f} seconds.", m_Name, seconds);
    }

    // Suppresses the message for a scope that turned out to have nothing worth
    // reporting. The clock keeps running and the elapsed time stays readable.
    void InvalidateTimer() { m_bSuppressed = true; }

private:
    // The end of the interval: the moment the timer stopped, or now if it is
    // still running.
    Clock::time_point MeasuredEnd() const { return m_bStopped ? m_End : Clock::now(); }

    std::string m_Name;

    Clock::time_point m_Start = Clock::now();
    Clock::time_point m_End{};

    bool m_bStopped = false;
    bool m_bSuppressed = false;
};
