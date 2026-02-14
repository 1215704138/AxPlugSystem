#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "opencv_core" for configuration "Release"
set_property(TARGET opencv_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_core PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/opencv_core480.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/opencv_core480.dll"
  )

list(APPEND _cmake_import_check_targets opencv_core )
list(APPEND _cmake_import_check_files_for_opencv_core "${_IMPORT_PREFIX}/lib/opencv_core480.lib" "${_IMPORT_PREFIX}/bin/opencv_core480.dll" )

# Import target "opencv_imgproc" for configuration "Release"
set_property(TARGET opencv_imgproc APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_imgproc PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/opencv_imgproc480.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/opencv_imgproc480.dll"
  )

list(APPEND _cmake_import_check_targets opencv_imgproc )
list(APPEND _cmake_import_check_files_for_opencv_imgproc "${_IMPORT_PREFIX}/lib/opencv_imgproc480.lib" "${_IMPORT_PREFIX}/bin/opencv_imgproc480.dll" )

# Import target "opencv_imgcodecs" for configuration "Release"
set_property(TARGET opencv_imgcodecs APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(opencv_imgcodecs PROPERTIES
  IMPORTED_IMPLIB_RELEASE "${_IMPORT_PREFIX}/lib/opencv_imgcodecs480.lib"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/opencv_imgcodecs480.dll"
  )

list(APPEND _cmake_import_check_targets opencv_imgcodecs )
list(APPEND _cmake_import_check_files_for_opencv_imgcodecs "${_IMPORT_PREFIX}/lib/opencv_imgcodecs480.lib" "${_IMPORT_PREFIX}/bin/opencv_imgcodecs480.dll" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
