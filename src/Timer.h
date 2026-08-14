#pragma once

#include <chrono>
#include <string>

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
        if (!m_bFinished)
        {
            m_bFinished = true;
            m_End = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> duration = m_End - m_Start;

            printf("%s took %.1f milliseconds.\n", m_Name.c_str(), duration.count() * 1000.f);
        }
    }

    void InvalidateTimer() { m_bFinished = true; }

private:
    std::string m_Name;
    bool m_bFinished = false;

    std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_End;
};
