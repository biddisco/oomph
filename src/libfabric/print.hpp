/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <array>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
//
#if defined(__linux) || defined(linux) || defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <crt_externs.h>
#include <unistd.h>
#define environ (*_NSGetEnviron())
#else
extern char** environ;
#endif

#include <boost/crc.hpp>

// ------------------------------------------------------------
// This file provides a simple to use printf style debugging
// tool that can be used on a per file basis to enable output.
// It is not intended to be exposed to users, but rather as
// an aid for development.
// ------------------------------------------------------------
// Usage: Instantiate a debug print object at the top of a file
// using a template param of true/false to enable/disable output.
// When the template parameter is false, the optimizer will
// not produce code and so the impact is nil.
//
// static debug::enable_print<true> spq_deb("SUBJECT");
//
// Later in code you may print information using
//
//             spq_deb.debug(str<16>("cleanup_terminated"), "v1"
//                  , "D" , dec<2>(domain_num)
//                  , "Q" , dec<3>(q_index)
//                  , "thread_num", dec<3>(local_num));
//
// various print formatters (dec/hex/str) are supplied to make
// the output regular and aligned for easy parsing/scanning.
//
// In tight loops, huge amounts of debug information might be
// produced, so a simple timer based output is provided
// To instantiate a timed output
//      static auto getnext = spq_deb.make_timer(1
//              , str<16>("get_next_thread"));
// then inside a tight loop
//      spq_deb.timed(getnext, dec<>(thread_num));
// The output will only be produced every N seconds
// ------------------------------------------------------------

#define NS_DEBUG oomph::debug
#define LF_DEB(printer, Expr)                                                                      \
    if constexpr (printer.is_enabled()) { printer.Expr; };

// ------------------------------------------------------------
/// \cond NODETAIL
namespace NS_DEBUG
{

// ------------------------------------------------------------------
// format as zero padded int
// ------------------------------------------------------------------
namespace detail
{

template<int N, typename T>
struct dec
{
    constexpr dec(T const& v)
    : data_(v)
    {
    }

    T const& data_;

    friend std::ostream& operator<<(std::ostream& os, dec<N, T> const& d)
    {
        os << std::right << std::setfill('0') << std::setw(N) << std::noshowbase << std::dec
           << d.data_;
        return os;
    }
};
} // namespace detail

template<int N = 2, typename T>
constexpr detail::dec<N, T>
dec(T const& v)
{
    return detail::dec<N, T>(v);
}

// ------------------------------------------------------------------
// format as pointer
// ------------------------------------------------------------------
struct ptr
{
    ptr(void const* v)
    : data_(v)
    {
    }
    ptr(std::uintptr_t const v)
    : data_(reinterpret_cast<void const*>(v))
    {
    }
    void const*          data_;
    friend std::ostream& operator<<(std::ostream& os, ptr const& d)
    {
        os << std::right << "0x" << std::setfill('0') << std::setw(12) << std::noshowbase
           << std::hex << reinterpret_cast<uintptr_t>(d.data_);
        return os;
    }
};

// ------------------------------------------------------------------
// format as zero padded hex
// ------------------------------------------------------------------
namespace detail
{

template<int N = 4, typename T = int, typename Enable = void>
struct hex;

template<int N, typename T>
struct hex<N, T, typename std::enable_if<!std::is_pointer<T>::value>::type>
{
    constexpr hex(T const& v)
    : data_(v)
    {
    }
    T const&             data_;
    friend std::ostream& operator<<(std::ostream& os, const hex<N, T>& d)
    {
        os << std::right << "0x" << std::setfill('0') << std::setw(N) << std::noshowbase << std::hex
           << d.data_;
        return os;
    }
};

template<int N, typename T>
struct hex<N, T, typename std::enable_if<std::is_pointer<T>::value>::type>
{
    constexpr hex(T const& v)
    : data_(v)
    {
    }
    T const&             data_;
    friend std::ostream& operator<<(std::ostream& os, const hex<N, T>& d)
    {
        os << std::right << std::setw(N) << std::noshowbase << std::hex << d.data_;
        return os;
    }
};
} // namespace detail

template<int N = 4, typename T>
constexpr detail::hex<N, T>
hex(T const& v)
{
    return detail::hex<N, T>(v);
}

// ------------------------------------------------------------------
// format as binary bits
// ------------------------------------------------------------------
namespace detail
{

template<int N = 8, typename T = int>
struct bin
{
    constexpr bin(T const& v)
    : data_(v)
    {
    }
    T const&             data_;
    friend std::ostream& operator<<(std::ostream& os, const bin<N, T>& d)
    {
        os << std::bitset<N>(d.data_);
        return os;
    }
};
} // namespace detail

template<int N = 8, typename T>
constexpr detail::bin<N, T>
bin(T const& v)
{
    return detail::bin<N, T>(v);
}

// ------------------------------------------------------------------
// format as padded string
// ------------------------------------------------------------------
template<int N = 20>
struct str
{
    constexpr str(char const* v)
    : data_(v)
    {
    }

    char const* data_;

    friend std::ostream& operator<<(std::ostream& os, str<N> const& d)
    {
        os << std::left << std::setfill(' ') << std::setw(N) << d.data_;
        return os;
    }
};

// ------------------------------------------------------------------
// format as ip address
// ------------------------------------------------------------------
struct ipaddr
{
    ipaddr(const void* a)
    : data_(reinterpret_cast<const uint8_t*>(a))
    , ipdata_(0)
    {
    }
    ipaddr(const uint32_t a)
    : data_(reinterpret_cast<const uint8_t*>(&ipdata_))
    , ipdata_(a)
    {
    }
    const uint8_t* data_;
    const uint32_t ipdata_;

    friend std::ostream& operator<<(std::ostream& os, ipaddr const& p)
    {
        os << std::dec << int(p.data_[0]) << "." << int(p.data_[1]) << "." << int(p.data_[2]) << "."
           << int(p.data_[3]);
        return os;
    }
};

// ------------------------------------------------------------------
// helper fuction for printing CRC32
// ------------------------------------------------------------------
inline uint32_t
crc32(const void* address, size_t length)
{
    boost::crc_32_type result;
    result.process_bytes(address, length);
    return result.checksum();
}

// ------------------------------------------------------------------
// helper fuction for printing short memory dump and crc32
// useful for debugging corruptions in buffers during
// rma or other transfers
// ------------------------------------------------------------------
struct mem_crc32
{
    mem_crc32(const void* a, std::size_t len, const char* txt)
    : addr_(reinterpret_cast<const std::uint8_t*>(a))
    , len_(len)
    , txt_(txt)
    {
    }
    const std::uint8_t*  addr_;
    const std::size_t    len_;
    const char*          txt_;
    friend std::ostream& operator<<(std::ostream& os, mem_crc32 const& p)
    {
        const std::uint8_t* byte = static_cast<const std::uint8_t*>(p.addr_);
        os << "Memory:";
        os << " address " << ptr(p.addr_) << " length " << hex<6, std::size_t>(p.len_)
           << " CRC32:" << hex<8, std::size_t>(crc32(p.addr_, p.len_)) << "\n";
        size_t i = 0;
        while (i < std::min(size_t(128), p.len_))
        {
            os << "0x";
            for (int j = 7; j >= 0; j--)
            {
                os << std::hex << std::setfill('0') << std::setw(2)
                   << (((i + j) > p.len_) ? (int)0 : (int)byte[i + j]);
            }
            i += 8;
            if (i % 32 == 0) os << std::endl;
            else
                os << " ";
        }
        os << ": " << p.txt_;
        return os;
    }
};

namespace detail
{

template<typename TupleType, std::size_t... I>
void
tuple_print(std::ostream& os, TupleType const& t, std::index_sequence<I...>)
{
    (..., (os << (I == 0 ? "" : " ") << std::get<I>(t)));
}

template<typename... Args>
void
tuple_print(std::ostream& os, const std::tuple<Args...>& t)
{
    tuple_print(os, t, std::make_index_sequence<sizeof...(Args)>());
}
} // namespace detail

namespace detail
{

// ------------------------------------------------------------------
// helper class for printing thread ID
// ------------------------------------------------------------------
struct current_thread_print_helper
{
};

inline std::ostream&
operator<<(std::ostream& os, current_thread_print_helper const&)
{
    os << hex<12, std::thread::id>(std::this_thread::get_id())
#ifdef DEBUGGING_PRINT_LINUX
       << " cpu " << debug::dec<3, int>(sched_getcpu()) << " ";
#else
       << " cpu "
       << "--- ";
#endif
    return os;
}

// ------------------------------------------------------------------
// helper class for printing time since start
// ------------------------------------------------------------------
struct hostname_print_helper
{
    const char* get_hostname() const
    {
        static bool initialized = false;
        static char hostname_[20];
        if (!initialized)
        {
            initialized = true;
            gethostname(hostname_, std::size_t(12));
            std::string temp = "(" + std::to_string(guess_rank()) + ")";
            std::strcat(hostname_, temp.c_str());
        }
        return hostname_;
    }

    int guess_rank() const
    {
        std::vector<std::string> env_strings{"_RANK=", "_NODEID="};
        for (char** current = environ; *current; current++)
        {
            auto e = std::string(*current);
            for (auto s : env_strings)
            {
                auto pos = e.find(s);
                if (pos != std::string::npos)
                {
                    //std::cout << "Got a rank string : " << e << std::endl;
                    return std::stoi(e.substr(pos + s.size(), 5));
                }
            }
        }
        return -1;
    }
};

inline std::ostream&
operator<<(std::ostream& os, hostname_print_helper const& h)
{
    os << debug::str<13>(h.get_hostname()) << " ";
    return os;
}

// ------------------------------------------------------------------
// helper class for printing time since start
// ------------------------------------------------------------------
struct current_time_print_helper
{
};

inline std::ostream&
operator<<(std::ostream& os, current_time_print_helper const&)
{
    using namespace std::chrono;
    static steady_clock::time_point log_t_start = steady_clock::now();
    //
    auto now = steady_clock::now();
    auto nowt = duration_cast<microseconds>(now - log_t_start).count();
    //
    os << debug::dec<10>(nowt) << " ";
    return os;
}

template<typename... Args>
void
display(char const* prefix, Args const&... args)
{
    // using a temp stream object with a single copy to cout at the end
    // prevents multiple threads from injecting overlapping text
    std::stringstream tempstream;
    tempstream << prefix << detail::current_time_print_helper()
               << detail::current_thread_print_helper() << detail::hostname_print_helper();
    ((tempstream << args << " "), ...);
    tempstream << "\n";
    std::cout << tempstream.str() << std::flush;
}

template<typename... Args>
void
debug(Args const&... args)
{
    display("<DEB> ", args...);
}

template<typename... Args>
void
warning(Args const&... args)
{
    display("<WAR> ", args...);
}

template<typename... Args>
void
error(Args const&... args)
{
    display("<ERR> ", args...);
}

template<typename... Args>
void
scope(Args const&... args)
{
    display("<SCO> ", args...);
}

template<typename... Args>
void
trace(Args const&... args)
{
    display("<TRC> ", args...);
}

template<typename... Args>
void
timed(Args const&... args)
{
    display("<TIM> ", args...);
}
} // namespace detail

template<typename... Args>
struct scoped_var
{
    // capture tuple elements by reference - no temp vars in constructor please
    char const*                      prefix_;
    std::tuple<Args const&...> const message_;
    std::string                      buffered_msg;

    //
    scoped_var(char const* p, Args const&... args)
    : prefix_(p)
    , message_(args...)
    {
        std::stringstream tempstream;
        detail::tuple_print(tempstream, message_);
        buffered_msg = tempstream.str();
        detail::display("<SCO> ", prefix_, debug::str<>(">> enter <<"), tempstream.str());
    }

    ~scoped_var() { detail::display("<SCO> ", prefix_, debug::str<>("<< leave >>"), buffered_msg); }
};

template<typename... Args>
struct timed_var
{
    mutable std::chrono::steady_clock::time_point time_start_;
    double const                                  delay_;
    std::tuple<Args...> const                     message_;
    //
    timed_var(double const& delay, Args const&... args)
    : time_start_(std::chrono::steady_clock::now())
    , delay_(delay)
    , message_(args...)
    {
    }

    bool elapsed(std::chrono::steady_clock::time_point const& now) const
    {
        double elapsed_ =
            std::chrono::duration_cast<std::chrono::duration<double>>(now - time_start_).count();

        if (elapsed_ > delay_)
        {
            time_start_ = now;
            return true;
        }
        return false;
    }

    friend std::ostream& operator<<(std::ostream& os, timed_var<Args...> const& ti)
    {
        detail::tuple_print(os, ti.message_);
        return os;
    }
};

///////////////////////////////////////////////////////////////////////////
template<bool enable>
struct enable_print;

// when false, debug statements should produce no code
template<>
struct enable_print<false>
{
    constexpr enable_print(const char*) {}

    constexpr bool is_enabled() const { return false; }

    template<typename... Args>
    constexpr void debug(Args const&...) const
    {
    }

    template<typename... Args>
    constexpr void warning(Args const&...) const
    {
    }

    template<typename... Args>
    constexpr void trace(Args const&...) const
    {
    }

    template<typename... Args>
    constexpr void error(Args const&...) const
    {
    }

    template<typename... Args>
    constexpr void timed(Args const&...) const
    {
    }

    template<typename T>
    constexpr void array(std::string const&, std::vector<T> const&) const
    {
    }

    template<typename T, std::size_t N>
    constexpr void array(std::string const&, std::array<T, N> const&) const
    {
    }

    template<typename Iter>
    constexpr void array(std::string const&, Iter, Iter) const
    {
    }

    template<typename... Args>
    constexpr bool scope(Args const&...)
    {
        return true;
    }

    template<typename T, typename... Args>
    constexpr bool declare_variable(Args const&...) const
    {
        return true;
    }

    template<typename T, typename V>
    constexpr void set(T&, V const&)
    {
    }

    // @todo, return void so that timers have zero footprint when disabled
    template<typename... Args>
    constexpr int make_timer(const double, Args const&...) const
    {
        return 0;
    }

    template<typename Expr>
    constexpr bool eval(Expr const&)
    {
        return true;
    }
};

// when true, debug statements produce valid output
template<>
struct enable_print<true>
{
  private:
    char const* prefix_;

  public:
    constexpr enable_print()
    : prefix_("")
    {
    }

    constexpr enable_print(const char* p)
    : prefix_(p)
    {
    }

    constexpr bool is_enabled() const { return true; }

    template<typename... Args>
    constexpr void debug(Args const&... args) const
    {
        detail::debug(prefix_, args...);
    }

    template<typename... Args>
    constexpr void warning(Args const&... args) const
    {
        detail::warning(prefix_, args...);
    }

    template<typename... Args>
    constexpr void trace(Args const&... args) const
    {
        detail::trace(prefix_, args...);
    }

    template<typename... Args>
    constexpr void error(Args const&... args) const
    {
        detail::error(prefix_, args...);
    }

    template<typename... Args>
    scoped_var<Args...> scope(Args const&... args)
    {
        return scoped_var<Args...>(prefix_, args...);
    }

    template<typename... T, typename... Args>
    void timed(timed_var<T...> const& init, Args const&... args) const
    {
        auto now = std::chrono::steady_clock::now();
        if (init.elapsed(now)) { detail::timed(prefix_, init, args...); }
    }

    template<typename T>
    void array(std::string const& name, std::vector<T> const& v) const
    {
        std::cout << str<20>(name.c_str()) << ": {" << debug::dec<4>(v.size()) << "} : ";
        std::copy(std::begin(v), std::end(v), std::ostream_iterator<T>(std::cout, ", "));
        std::cout << "\n";
    }

    template<typename T, std::size_t N>
    void array(std::string const& name, const std::array<T, N>& v) const
    {
        std::cout << str<20>(name.c_str()) << ": {" << debug::dec<4>(v.size()) << "} : ";
        std::copy(std::begin(v), std::end(v), std::ostream_iterator<T>(std::cout, ", "));
        std::cout << "\n";
    }

    template<typename Iter>
    void array(std::string const& name, Iter begin, Iter end) const
    {
        std::cout << str<20>(name.c_str()) << ": {" << debug::dec<4>(std::distance(begin, end))
                  << "} : ";
        std::copy(begin, end,
            std::ostream_iterator<typename std::iterator_traits<Iter>::value_type>(std::cout,
                ", "));
        std::cout << std::endl;
    }

    template<typename T, typename... Args>
    T declare_variable(Args const&... args) const
    {
        return T(args...);
    }

    template<typename T, typename V>
    void set(T& var, V const& val)
    {
        var = val;
    }

    template<typename... Args>
    timed_var<Args...> make_timer(const double delay, const Args... args) const
    {
        return timed_var<Args...>(delay, args...);
    }

    template<typename Expr>
    auto eval(Expr const& e)
    {
        return e();
    }
};

// ------------------------------------------------------------------
// helper for N>M true/false
// ------------------------------------------------------------------
template <int Level, int Threshold>
struct check_level : std::integral_constant<bool, Level <= Threshold>
{
};

template <int Level, int Threshold>
struct print_threshold : enable_print<check_level<Level, Threshold>::value>
{
    using base_type = enable_print<check_level<Level, Threshold>::value>;
    // inherit constructor
    using base_type::base_type;
};

} // namespace NS_DEBUG
/// \endcond
