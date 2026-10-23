#pragma once

/* MacPhoenix: std::filesystem, as `fs`, with what Executor used from
 * boost::filesystem that std lacks or does differently: modification times
 * as time_t (std's file_time_type has no portable C++17 conversion), the
 * path-taking fstreams, and unique_path(). */

#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <fcntl.h>
#include <sys/stat.h>

namespace fs
{
using namespace std::filesystem;

using ifstream = std::ifstream;
using ofstream = std::ofstream;
using fstream = std::fstream;

inline std::time_t last_write_time(const path &p, std::error_code &ec)
{
    struct stat st;
    if(stat(p.c_str(), &st) != 0)
    {
        ec.assign(errno, std::generic_category());
        return (std::time_t)-1;
    }
    ec.clear();
    return st.st_mtime;
}

inline std::time_t last_write_time(const path &p)
{
    std::error_code ec;
    std::time_t t = fs::last_write_time(p, ec);
    if(ec)
        throw filesystem_error("last_write_time", p, ec);
    return t;
}

/* The modification time; the access time is left as it is. */
inline void last_write_time(const path &p, std::time_t t, std::error_code &ec)
{
    struct timespec times[2];
    times[0].tv_sec = 0;
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = t;
    times[1].tv_nsec = 0;
    if(utimensat(AT_FDCWD, p.c_str(), times, 0) != 0)
        ec.assign(errno, std::generic_category());
    else
        ec.clear();
}

inline void last_write_time(const path &p, std::time_t t)
{
    std::error_code ec;
    fs::last_write_time(p, t, ec);
    if(ec)
        throw filesystem_error("last_write_time", p, ec);
}

/* model with each '%' replaced by a random hex digit. */
inline path unique_path(const std::string &model = "%%%%-%%%%-%%%%-%%%%")
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::string s = model;
    for(char &c : s)
        if(c == '%')
            c = "0123456789abcdef"[rng() & 15];
    return s;
}
}
