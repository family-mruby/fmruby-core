# CMake toolchain for the arm64 package (rake native:cross ARCH=arm64),
# inside native/docker's image: Debian's cross compiler and the arm64
# development packages installed beside the amd64 ones (multiarch), found
# through their own pkg-config directory.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar CACHE FILEPATH "")
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP aarch64-linux-gnu-strip CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
