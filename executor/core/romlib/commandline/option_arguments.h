#pragma once

#include <stdint.h>
#include <iostream>

struct MemorySize
{
    static const int32_t min = 0, max = 0x7FFFFFFF;
    int32_t size;

    friend std::istream& operator>>(std::istream& in, MemorySize& out)
    {
        double tmp;
        in >> tmp;
        if (!in.eof())
        {
            char c;
            in >> std::ws >> c;
            if (!in.eof())
            {
                if (c == 'K' || c == 'k')
                    tmp *= 1024;
                else if (c == 'M' || c == 'm')
                    tmp *= 1024 * 1024;
                else if (c == 'G' || c == 'g')
                    tmp *= 1024 * 1024 * 1024;
                else
                    tmp = -1;
            }
        }
        if (tmp >= (double)min && tmp <= (double)max)
        {
            out.size = static_cast<int32_t>(tmp + 0.5);
        }
        else
            in.setstate(std::ios::failbit);

        return in;
    }

    template<typename T>
    operator T() { return static_cast<T>(size); }
};

struct Ratio
{
    int numer, denom = 1;

    friend std::istream& operator>>(std::istream& in, Ratio& out)
    {
        in >> out.numer;
        if (in.eof())
            return in;
        char c;
        in >> c;
        if (in.eof() || c != '/')
        {
            in.unget();
            return in;
        }

        in >> out.denom;
        return in;
    }
};

struct Size2D
{
    unsigned width, height;

    friend std::istream& operator>>(std::istream& in, Size2D& out)
    {
        in >> out.width;
        char c;
        in >> c;
        if (in.eof() || c != 'x')
            in.setstate(std::ios::failbit);
        in >> out.height;
        return in;
    }
};

struct VersionNumber
{
    unsigned major = 0, minor = 0, patch = 0;

    friend std::istream& operator>>(std::istream& in, VersionNumber& out)
    {
        out = {};
        in >> out.major;
        if (in.eof())
            return in;
        char c;
        in >> c;
        if (in.eof() || c != '.')
        {
            in.unget();
            return in;
        }
        in >> out.minor;
        if (in.eof())
            return in;
        in >> c;
        if (in.eof() || c != '.')
        {
            in.unget();
            return in;
        }
        in >> out.patch;
        return in;
    }
};
