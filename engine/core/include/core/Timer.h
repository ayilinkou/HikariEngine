#pragma once

#include <chrono>
#include <string>

#include "Log.h"

class Timer
{
public:
    Timer(const std::string& name)
        : m_Name(name), m_Start(std::chrono::high_resolution_clock::now())
    {
    }

    ~Timer() { EndTimer(); }

    void EndTimer()
    {
        static constexpr LogCategory LogTimer("Timer");

        if (!m_bFinished)
        {
            m_bFinished = true;
            m_End = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> duration = m_End - m_Start; // in seconds

            if (duration.count() < 1.f)
                LogMsg(LogSeverity::Info, LogTimer, "{} took {:.1f} milliseconds.", m_Name,
                       duration.count() * 1000.f);
            else
                LogMsg(LogSeverity::Info, LogTimer, "{} took {:.1f} seconds.", m_Name,
                       duration.count());
        }
    }

    void InvalidateTimer() { m_bFinished = true; }

private:
    std::string m_Name;
    bool m_bFinished = false;

    std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_End;
};
