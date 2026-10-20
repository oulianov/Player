#.rst:
# FindFFmpeg
# ----------
#
# Find the FFmpeg libraries the movie player uses: libavformat, libavcodec,
# libswscale, libswresample and libavutil (through pkg-config).
#
# Imported Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines the following :prop_tgt:`IMPORTED` targets:
#
# ``FFmpeg::FFmpeg``
#   The FFmpeg libraries, if found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module will set the following variables in your project:
#
# ``FFMPEG_FOUND``
#   true if the FFmpeg libraries were found.
# ``FFMPEG_VERSION``
#   the version of libavcodec.

find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
	pkg_check_modules(PC_FFMPEG QUIET IMPORTED_TARGET
		libavformat libavcodec libswscale libswresample libavutil)
endif()

set(FFMPEG_VERSION ${PC_FFMPEG_libavcodec_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
	REQUIRED_VARS PC_FFMPEG_LINK_LIBRARIES
	VERSION_VAR FFMPEG_VERSION)

if(FFMPEG_FOUND AND NOT TARGET FFmpeg::FFmpeg)
	add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
	set_target_properties(FFmpeg::FFmpeg PROPERTIES
		INTERFACE_LINK_LIBRARIES PkgConfig::PC_FFMPEG)
endif()
