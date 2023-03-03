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
#include <cstdint>
#include <cstring>
#include <ostream>
#include <utility>
//
#include <rdma/fabric.h>
#include <netinet/in.h>
#include <arpa/inet.h>
//
#include "oomph_libfabric_defines.hpp"

// Different providers use different address formats that we must accommodate
// in our locality object.
#ifdef HAVE_LIBFABRIC_GNI
#define HAVE_LIBFABRIC_LOCALITY_SIZE 48
#endif

#ifdef HAVE_LIBFABRIC_CXI
#define HAVE_LIBFABRIC_LOCALITY_SIZE 4
#endif

#ifdef HAVE_LIBFABRIC_EFA
#define HAVE_LIBFABRIC_LOCALITY_SIZE 32
#endif

#if defined(HAVE_LIBFABRIC_VERBS) || defined(HAVE_LIBFABRIC_TCP) ||                                \
    defined(HAVE_LIBFABRIC_SOCKETS) || defined(HAVE_LIBFABRIC_PSM2)
#define HAVE_LIBFABRIC_LOCALITY_SIZE 16
#define HAVE_LIBFABRIC_LOCALITY_SOCKADDR
#endif

namespace oomph
{
// cppcheck-suppress ConfigurationNotChecked
static NS_DEBUG::enable_print<false> loc_deb("LOCALIT");
} // namespace oomph

namespace oomph
{
namespace libfabric
{

struct locality;

// ------------------------------------------------------------------
// format as ip address, port, libfabric address
// ------------------------------------------------------------------
struct iplocality
{
    const locality& data;
    iplocality(const locality& a);
    friend std::ostream& operator<<(std::ostream& os, const iplocality& p);
};

// --------------------------------------------------------------------
// Locality, in this structure we store the information required by
// libfabric to make a connection to another node.
// With libfabric 1.4.x the array contains the fabric ip address stored
// as the second uint32_t in the array. For this reason we use an
// array of uint32_t rather than uint8_t/char so we can easily access
// the ip for debug/validation purposes
// --------------------------------------------------------------------
namespace locality_defs
{
// the number of 32bit ints stored in our array
const uint32_t array_size = HAVE_LIBFABRIC_LOCALITY_SIZE;
const uint32_t array_length = HAVE_LIBFABRIC_LOCALITY_SIZE / 4;
} // namespace locality_defs

struct locality
{
    // array type of our locality data
    typedef std::array<uint32_t, locality_defs::array_length> locality_data;

    static const char* type() { return "libfabric"; }

    explicit locality(const locality_data& in_data)
    {
        std::memcpy(&data_[0], &in_data[0], locality_defs::array_size);
        fi_address_ = 0;
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("expl constructing"), iplocality((*this))));
    }

    locality()
    {
        std::memset(&data_[0], 0x00, locality_defs::array_size);
        fi_address_ = 0;
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("default construct"), iplocality((*this))));
    }

    locality(const locality& other)
    : data_(other.data_)
    , fi_address_(other.fi_address_)
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("copy construct"), iplocality((*this))));
    }

    locality(const locality& other, fi_addr_t addr)
    : data_(other.data_)
    , fi_address_(addr)
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("copy fi construct"), iplocality((*this))));
    }

    locality(locality&& other)
    : data_(std::move(other.data_))
    , fi_address_(other.fi_address_)
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("move construct"), iplocality((*this))));
    }

    // provided to support sockets mode bootstrap
    explicit locality(const std::string& address, const std::string& portnum)
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("explicit construct"), address, ":", portnum));
        //
        struct sockaddr_in socket_data;
        memset(&socket_data, 0, sizeof(socket_data));
        socket_data.sin_family = AF_INET;
        socket_data.sin_port = htons(std::stol(portnum));
        inet_pton(AF_INET, address.c_str(), &(socket_data.sin_addr));
        //
        std::memcpy(&data_[0], &socket_data, locality_defs::array_size);
        fi_address_ = 0;
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("string constructing"), iplocality((*this))));
    }

    // some condition marking this locality as valid
    explicit inline operator bool() const
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("bool operator"), iplocality((*this))));
        return (ip_address() != 0);
    }

    inline bool valid() const
    {
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("valid operator"), iplocality((*this))));
        return (ip_address() != 0);
    }

    locality& operator=(const locality& other)
    {
        data_ = other.data_;
        fi_address_ = other.fi_address_;
        LF_DEB(loc_deb,
            trace(NS_DEBUG::str<>("copy operator"), iplocality(*this), iplocality(other)));
        return *this;
    }

    bool operator==(const locality& other)
    {
        LF_DEB(loc_deb,
            trace(NS_DEBUG::str<>("equality operator"), iplocality(*this), iplocality(other)));
        return std::memcmp(&data_, &other.data_, locality_defs::array_size) == 0;
    }

    bool less_than(const locality& other)
    {
        LF_DEB(loc_deb,
            trace(NS_DEBUG::str<>("less operator"), iplocality(*this), iplocality(other)));
        if (ip_address() < other.ip_address()) return true;
        if (ip_address() == other.ip_address()) return port() < other.port();
        return false;
    }

    const uint32_t& ip_address() const
    {
#if defined(HAVE_LIBFABRIC_LOCALITY_SOCKADDR)
        return reinterpret_cast<const struct sockaddr_in*>(data_.data())->sin_addr.s_addr;
#elif defined(HAVE_LIBFABRIC_GNI)
        return data_[0];
#elif defined(HAVE_LIBFABRIC_CXI)
        return data_[0];
#elif defined(HAVE_LIBFABRIC_EFA)
        return data_[0];
#else
        throw fabric_error(0, "unsupported fabric provider, please fix ASAP");
#endif
    }

    static const uint32_t& ip_address(const locality_data& data)
    {
#if defined(HAVE_LIBFABRIC_LOCALITY_SOCKADDR)
        return reinterpret_cast<const struct sockaddr_in*>(&data)->sin_addr.s_addr;
#elif defined(HAVE_LIBFABRIC_GNI)
        return data[0];
#elif defined(HAVE_LIBFABRIC_CXI)
        return data[0];
#elif defined(HAVE_LIBFABRIC_EFA)
        return data[0];
#else
        throw fabric_error(0, "unsupported fabric provider, please fix ASAP");
#endif
    }

    inline const fi_addr_t& fi_address() const { return fi_address_; }

    inline void set_fi_address(fi_addr_t fi_addr) { fi_address_ = fi_addr; }

    inline uint16_t port() const
    {
        uint16_t port = 256 * reinterpret_cast<const uint8_t*>(data_.data())[2] +
                        reinterpret_cast<const uint8_t*>(data_.data())[3];
        return port;
    }

    inline const void* fabric_data() const { return data_.data(); }

    inline char* fabric_data_writable() { return reinterpret_cast<char*>(data_.data()); }

  private:
    friend bool operator==(locality const& lhs, locality const& rhs)
    {
        LF_DEB(loc_deb,
            trace(NS_DEBUG::str<>("equality friend"), iplocality(lhs), iplocality(rhs)));
        return ((lhs.data_ == rhs.data_) && (lhs.fi_address_ == rhs.fi_address_));
    }

    friend bool operator<(locality const& lhs, locality const& rhs)
    {
        const uint32_t&  a1 = lhs.ip_address();
        const uint32_t&  a2 = rhs.ip_address();
        const fi_addr_t& f1 = lhs.fi_address();
        const fi_addr_t& f2 = rhs.fi_address();
        LF_DEB(loc_deb, trace(NS_DEBUG::str<>("less friend"), iplocality(lhs), iplocality(rhs)));
        return (a1 < a2) || (a1 == a2 && f1 < f2);
    }

    friend std::ostream& operator<<(std::ostream& os, locality const& loc)
    {
        for (uint32_t i = 0; i < locality_defs::array_length; ++i) { os << loc.data_[i]; }
        return os;
    }

  private:
    locality_data data_;
    fi_addr_t     fi_address_;
};

} // namespace libfabric
} // namespace oomph
