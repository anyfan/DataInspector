#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "QuaZip::QuaZip" for configuration "Release"
set_property(TARGET QuaZip::QuaZip APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(QuaZip::QuaZip PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libquazip1-qt5.a"
  )

list(APPEND _cmake_import_check_targets QuaZip::QuaZip )
list(APPEND _cmake_import_check_files_for_QuaZip::QuaZip "${_IMPORT_PREFIX}/lib/libquazip1-qt5.a" )

# Import target "QuaZip::bzip2" for configuration "Release"
set_property(TARGET QuaZip::bzip2 APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(QuaZip::bzip2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "C;CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libbzip2.a"
  )

list(APPEND _cmake_import_check_targets QuaZip::bzip2 )
list(APPEND _cmake_import_check_files_for_QuaZip::bzip2 "${_IMPORT_PREFIX}/lib/libbzip2.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
