#ifndef OOMPH_LIBFABRIC_CONFIG_LIBFABRIC_HPP
#define OOMPH_LIBFABRIC_CONFIG_LIBFABRIC_HPP

// definitions that cmake generates from user options
// clang-format off
@oomph_config_defines@
// clang-format on

// ------------------------------------------------------------------
// This section exists to make interoperabily/sharing of code
// between OOMPH/GHEX and HPX easier - there are some files that do
// the majority of libfabric initialization/setup and polling that
// are basically the same in many apps, these files can be reused provided
// some namespaces for the lib and for debugging are setup correctly

#define NS_LIBFABRIC oomph::libfabric
#define NS_MEMORY    oomph::libfabric
#define NS_DEBUG     oomph::debug

#ifndef LF_DEB
#define LF_DEB(printer, Expr)                                                                      \
    if constexpr (printer.is_enabled()) { printer.Expr; };
#endif

#define LFSOURCE_DIR "@OOMPH_SRC_LIBFABRIC_DIR@"
#define LFPRINT_HPP  "@OOMPH_SRC_LIBFABRIC_DIR@/print.hpp"
#define LFCOUNT_HPP  "@OOMPH_SRC_LIBFABRIC_DIR@/simple_counter.hpp"

// oomph has a debug print helper file in the main source tree
#if __has_include(LFPRINT_HPP)
#include LFPRINT_HPP
#define has_debug 1
#endif

#if __has_include(LFCOUNT_HPP)
#include LFCOUNT_HPP
#endif

#endif
