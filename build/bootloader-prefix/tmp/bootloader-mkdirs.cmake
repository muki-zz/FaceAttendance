# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "E:/application/Espressif/frameworks/esp-idf-v5.5.3/components/bootloader/subproject")
  file(MAKE_DIRECTORY "E:/application/Espressif/frameworks/esp-idf-v5.5.3/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader"
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix"
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/tmp"
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/src/bootloader-stamp"
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/src"
  "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "E:/study/share/study_space/esp32/myself/FaceAttend/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
