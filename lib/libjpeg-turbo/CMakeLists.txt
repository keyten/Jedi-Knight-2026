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


# libjpeg-turbo 3.2.0 (https://github.com/libjpeg-turbo/libjpeg-turbo), the
# official x64 Visual C++ build: its static jpeg-static.lib and headers. Used in
# place of the bundled libjpeg 9a when UseLibjpegTurbo is on (default off).
#
# SIMD decoding is about twice as fast, but libjpeg 7+ (9a) upsamples chroma by
# DCT scaling and libjpeg-turbo does not, so decoded JPEG pixels differ a
# little (about 6% of bytes, mean difference 2 on the asset sample).
#
# The library is built with the static CRT; the engine and renderers use the
# DLL CRT, so libcmt is excluded from the link.

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message(FATAL_ERROR "UseLibjpegTurbo: lib/libjpeg-turbo only holds the x64 Visual C++ build")
endif()

add_library(bundled_libjpeg_turbo STATIC IMPORTED GLOBAL)
set_target_properties(bundled_libjpeg_turbo PROPERTIES
	IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/jpeg-static.lib
	INTERFACE_LINK_OPTIONS "/NODEFAULTLIB:libcmt.lib")

set(JPEG_LIBRARIES bundled_libjpeg_turbo PARENT_SCOPE)
set(JPEG_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/ PARENT_SCOPE)

mark_as_advanced(JPEG_LIBRARIES JPEG_INCLUDE_DIR)
