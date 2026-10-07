#pragma once

#include <array>
#include <stdexcept>

namespace Executor
{

template<typename T, int maxSize>
class static_vector
{
    std::array<T, maxSize> items_;
    size_t size_ = 0;
public:

    static_vector() = default;

    auto data() { return items_.data(); }
    auto data() const { return items_.data(); }

    auto begin() { return items_.begin(); }
    auto begin() const { return items_.begin(); }

    auto end() { return items_.begin() + size_; }
    auto end() const { return items_.begin() + size_; }

    size_t size() const { return size_; }
    constexpr size_t capacity() const { return maxSize; }

    bool empty() const { return size_ == 0; }

    T& operator[](size_t idx) { return items_[idx]; }
    const T& operator[](size_t idx) const { return items_[idx]; }

    T& front() { return items_.front(); }
    const T& front() const { return items_.front(); }

    T& back() { return items_[size_-1]; }
    const T& back() const { return items_[size_-1]; }

    void resize(size_t sz)
    {
        if(sz > maxSize)
            throw std::logic_error("static_vector out of space");
        size_ = sz;
    }

    void push_back(const T& item)
    {
        resize(size_ + 1);
        items_[size_ - 1] = item;
    }

    void pop_back()
    {
        resize(size_-1);
    }

    void clear()
    {
        resize(0);
    }
};

}
