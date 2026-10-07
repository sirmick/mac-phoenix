#pragma once

#include <MemoryMgr.h>
#include <type_traits>
#include <utility>
#include <string.h>

namespace Executor
{

template<typename T, typename TypedHandle = GUEST<GUEST<T>*>*, size_t offset = 0, bool tight = false>
class handle_vector
{
    static_assert(std::is_trivially_copyable_v<T>);
    Handle handle_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    bool owned_ = false;

    static constexpr size_t elem_size = sizeof(GUEST<T>);
public:
    handle_vector() noexcept = default;
    explicit handle_vector(TypedHandle h) noexcept
        : handle_((Handle) h)
        , size_((GetHandleSize(handle_) - offset) / elem_size)
        , capacity_(size_)
    {
    }
    ~handle_vector()
    {
        if(owned_ && handle_)
            DisposeHandle(handle_);
    }

    friend void swap(handle_vector& a, handle_vector& b) noexcept
    {
        std::swap(a.handle_, b.handle_);
        std::swap(a.size_, b.size_);
        std::swap(a.capacity_, b.capacity_);
    }

    handle_vector(const handle_vector& other)
    {
        if(other.handle_)
        {
            handle_ = NewHandle(offset + elem_size * other.size_);
            owned_ = true;
            size_ = other.size_;
            capacity_ = other.size_;
            memcpy(*handle_, *other.handle_, offset + other.size_ * elem_size);
        }
    }

    handle_vector(handle_vector&& other) noexcept
    {
        swap(*this, other);
    }

    handle_vector& operator=(const handle_vector& other)
    {
        return (*this = handle_vector(other));
    }
    handle_vector& operator=(handle_vector&& other)
    {
        handle_vector moved(std::move(other));
        swap(*this, moved);
        return *this;
    }

    TypedHandle get() const noexcept
    {
        return (TypedHandle)handle_;
    }

    TypedHandle release() noexcept
    {
        if(!handle_)
            return (TypedHandle) NewHandleClear(offset);
        
        if(capacity_ != size_)
            SetHandleSize(handle_, offset + size_ * elem_size);
        TypedHandle h = (TypedHandle) handle_;
        handle_ = nullptr;
        owned_ = false;
        size_ = capacity_ = 0;
        return h;
    }

    size_t size() const noexcept { return size_; }
    size_t capacity() const noexcept { return capacity_; }
    
    void reserve(size_t cap)
    {
        if(cap > capacity_)
        {
            capacity_ = cap;
            if(handle_)
                SetHandleSize(handle_, offset + capacity_ * elem_size);
            else
            {
                handle_ = NewHandle(offset + capacity_ * elem_size);
                owned_ = true;
                if(offset)
                    memset(*handle_, 0, offset);
            }
        }
    }

    void shrink_to_fit()
    {
        if(size_ != capacity_)
        {
            capacity_ = size_;
            SetHandleSize(handle_, offset + capacity_ * elem_size);
        }
    }

    using pointer = GUEST<T>*;
    using const_pointer = const GUEST<T>*;
    using iterator = pointer;
    using const_iterator = const_pointer;

    pointer data() noexcept { return handle_ ? reinterpret_cast<pointer>(*handle_ + offset) : nullptr; }
    iterator begin() noexcept { return data(); }
    iterator end() noexcept { return data() + size_; }
    GUEST<T>& operator[] (size_t idx) noexcept { return *begin()[idx]; }

    const_pointer data() const noexcept { return handle_ ? reinterpret_cast<pointer>(*handle_ + offset) : nullptr; }
    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + size_; }
    const GUEST<T>& operator[] (size_t idx) const noexcept { return *begin()[idx]; }


    void push_back(T x)
    {
        if(size_ == capacity_)
            reserve(tight ? capacity_ + 1 : capacity_ + 32 + capacity_/10);
        new(data() + size_) GUEST<T>(x);
        ++size_;
    }

    iterator erase(iterator first, iterator last)
    {
        std::copy(last, end(), first);
        size_ -= last - first;
        if(tight)
        {
            size_t newOffset = first - begin();
            shrink_to_fit();
            return begin() + newOffset;
        }
        else
            return first;
    }

    iterator erase(iterator pos)
    {
        return erase(pos, pos+1);
    }
};

}
