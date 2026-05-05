# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/tmp"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/src"
  "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/jafo0/OneDrive - University of Bahrain/Desktop/Senior-Project-Clone/firmware/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
