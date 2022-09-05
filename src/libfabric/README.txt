To make the same code work in ghex and also hpx,
the 3 files

    memory_region.hpp
    operation_context_base.hpp
    controller_base.hpp

should be synchronized with the equivalent versions in (for exammple)

    /home/biddisco/src/hpx-rma/plugins/parcelport/libfabric/memory_region.hpp
    /home/biddisco/src/hpx-rma/plugins/parcelport/libfabric/operation_context_base.hpp
    /home/biddisco/src/hpx-rma/plugins/parcelport/libfabric/controller_base.hpp

To make changes easier to visualize, the files should be
formatted according to the project clang-format rules before performing diffs.

# ------------------------------------------------------------------------
# if hpx versions have been improved and changes are wanted in oomph
# ------------------------------------------------------------------------
cd ~/src/ghex/extern/oomph
cp ~/src/hpx-rma/.clang-format ./.clang-format

cd ~/src/ghex/extern/oomph/src/libfabric
clang-format -i memory_region.hpp
clang-format -i operation_context_base.hpp
clang-format -i controller_base.hpp

meld memory_region.hpp /home/biddisco/src/hpx-rma/libs/full/parcelport_libfabric/include/hpx/parcelport_libfabric/memory_region.hpp
meld operation_context_base.hpp /home/biddisco/src/hpx-rma/libs/full/parcelport_libfabric/include/hpx/parcelport_libfabric/operation_context_base.hpp
meld controller_base.hpp /home/biddisco/src/hpx-rma/libs/full/parcelport_libfabric/include/hpx/parcelport_libfabric/controller_base.hpp

# ------------------------------------------------------------------------
# if oomph versions have been improved and changes are wanted in hpx
# ------------------------------------------------------------------------
cd /home/biddisco/src/hpx-rma/libs/full/parcelport_libfabric
cp ~/src/ghex/extern/oomph/.clang-format ./.clang-format

clang-format -i ./include/hpx/parcelport_libfabric/memory_region.hpp
clang-format -i ./include/hpx/parcelport_libfabric/operation_context_base.hpp
clang-format -i ./include/hpx/parcelport_libfabric/controller_base.hpp

meld ./include/hpx/parcelport_libfabric/memory_region.hpp ~/src/ghex/extern/oomph/src/libfabric/memory_region.hpp
meld ./include/hpx/parcelport_libfabric/operation_context_base.hpp ~/src/ghex/extern/oomph/src/libfabric/operation_context_base.hpp
meld ./include/hpx/parcelport_libfabric/controller_base.hpp ~/src/ghex/extern/oomph/src/libfabric/controller_base.hpp

cp ~/src/ghex/extern/oomph/src/libfabric/memory_region.hpp          ./include/hpx/parcelport_libfabric/memory_region.hpp
cp ~/src/ghex/extern/oomph/src/libfabric/operation_context_base.hpp ./include/hpx/parcelport_libfabric/operation_context_base.hpp
cp ~/src/ghex/extern/oomph/src/libfabric/controller_base.hpp        ./include/hpx/parcelport_libfabric/controller_base.hpp
cp ~/src/ghex/extern/oomph/src/libfabric/fabric_error.hpp           ./include/hpx/parcelport_libfabric/fabric_error.hpp

clang-format -i include/hpx/parcelport_libfabric/memory_region.hpp
clang-format -i include/hpx/parcelport_libfabric/operation_context_base.hpp
clang-format -i include/hpx/parcelport_libfabric/controller_base.hpp
clang-format -i include/hpx/parcelport_libfabric/fabric_error.hpp

