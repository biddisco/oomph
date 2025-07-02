/*
 * ghex-org
 *
 * Copyright (c) 2014-2023, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <locality.hpp>

namespace oomph {
namespace libfabric {

// ------------------------------------------------------------------
// format as ip address, port, libfabric address
// ------------------------------------------------------------------
iplocality::iplocality(const locality &l) : data(l) {}

std::ostream &operator<<(std::ostream &os, const iplocality &p) {
  os << std::dec << NS_DEBUG::ipaddr(p.data.fabric_data()) << " - "
     << NS_DEBUG::ipaddr(p.data.ip_address()) << ":"
     << NS_DEBUG::dec<>(p.data.port()) << " ("
     << NS_DEBUG::dec<>(p.data.fi_address()) << ") ";
  return os;
}

} // namespace libfabric
} // namespace oomph
