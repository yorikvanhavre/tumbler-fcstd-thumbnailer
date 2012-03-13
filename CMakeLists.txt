cmake_minimum_required ( VERSION 2.6 )
project ( tumbler-fcstd-thumbnailer )

INCLUDE( FindPkgConfig )
FIND_PACKAGE( GTK2 REQUIRED )
ADD_DEFINITIONS("`pkg-config --cflags --libs gtk+-2.0`")
ADD_DEFINITIONS("`pkg-config --cflags --libs tumbler-1`")
pkg_check_modules(PKG REQUIRED libgsf-1)

include_directories( ${PKG_INCLUDE_DIRS} ${PROJECT_SOURCE_DIR} )
link_directories( ${PKG_LIBRARY_DIRS} )
message ("found libs: ${PKG_INCLUDE_DIRS}")

add_library( tumbler-fcstd-thumbnailer SHARED fcstd-thumbnailer.c fcstd-thumbnailer-provider.c fcstd-thumbnailer-plugin.c )

set_target_properties(tumbler-fcstd-thumbnailer PROPERTIES PREFIX "")