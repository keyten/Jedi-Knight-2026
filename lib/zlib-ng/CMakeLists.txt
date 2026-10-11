#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================


# zlib-ng (https://github.com/zlib-ng/zlib-ng) in zlib-compatible mode, used in
# place of the bundled zlib 1.2.8 when UseZlibNg is on. Same API and output,
# faster inflate (PNG decoding, pk3 reads). src/ is the zlib-ng 2.3.3 release
# without its test/, doc/ and tools/ folders; this file only sets its options.
#
# Static, position-independent, no tests. Variables set here are only seen by
# zlib-ng's own CMakeLists (CMP0077), not by the rest of the project.

set(ZLIB_COMPAT ON)
set(ZLIB_ALIASES OFF)
set(BUILD_TESTING OFF)
set(BUILD_SHARED_LIBS OFF)
set(WITH_GTEST OFF)
set(SKIP_INSTALL_ALL ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(src)

set_property(TARGET zlib-ng PROPERTY POSITION_INDEPENDENT_CODE True)

# zlib.h and zconf.h are generated into the build folder
set(ZLIB_LIBRARIES zlib-ng PARENT_SCOPE)
set(ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/src PARENT_SCOPE)

mark_as_advanced(ZLIB_LIBRARIES ZLIB_INCLUDE_DIR)
