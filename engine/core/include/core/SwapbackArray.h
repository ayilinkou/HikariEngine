#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

// This class is a wrapper for std::vector which keeps all elements in a
// contiguous array. Order is not maintained.
template <typename T>
class SwapbackArray
{
public:
    SwapbackArray() { m_Data = std::vector<T>(); }

    std::vector<T>& Data() { return m_Data; }
    const std::vector<T>& Data() const { return m_Data; }

    T& operator[](size_t index) { return m_Data[index]; }
    const T& operator[](size_t index) const { return m_Data[index]; }

    inline void RemoveAt(size_t index)
    {
        if (index != m_Data.size() - 1)
            m_Data[index] = std::move(m_Data.back());
        m_Data.pop_back();
    }

    inline void RemoveAt(std::vector<T>::iterator it)
    {
        size_t index = it - m_Data.begin();
        RemoveAt(index);
    }

    inline void Erase(T val)
    {
        auto it = std::ranges::find(m_Data, val);
        if (it == m_Data.end())
            throw std::runtime_error("Value to erase not found in vector!");
        RemoveAt(it);
    }

    inline auto begin() { return m_Data.begin(); }
    inline auto end() { return m_Data.end(); }
    inline auto begin() const { return m_Data.begin(); }
    inline auto end() const { return m_Data.end(); }

    inline size_t Size() const { return m_Data.size(); }
    inline bool Empty() const { return m_Data.empty(); }
    inline void Pushback(T& val) { return m_Data.push_back(val); }
    inline void Popback() { return m_Data.pop_back(); }

private:
    std::vector<T> m_Data;
};
