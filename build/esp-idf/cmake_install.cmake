# Install script for directory: E:/application/Espressif/frameworks/esp-idf-v5.5.3

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files (x86)/proj")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "E:/application/Espressif/tools/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump.exe")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/xtensa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_gpio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_timer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_pm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/mbedtls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/bootloader/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esptool_py/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/partition_table/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_app_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_bootloader_format/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/app_update/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_partition/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/efuse/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/bootloader_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_mm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/spi_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_system/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_common/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_rom/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/hal/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/log/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/heap/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/soc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_security/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_hw_support/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/freertos/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/newlib/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/pthread/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/cxx/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_ringbuf/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_i2c/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_pcnt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_gptimer/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_psram/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_spi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_mcpwm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_ana_cmpr/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_i2s/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_sdmmc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_sdspi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_sdio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_dac/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_bitscrambler/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_rmt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_tsens/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_sdm/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_uart/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_ledc/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_parlio/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_usb_serial_jtag/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_twai/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/driver/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/i2c_bus/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/pca9557/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_lcd/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/lcd_st7789/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/touch_ft6x36/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__esp_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/nvs_flash/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__esp32-camera/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/camera/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/bsp/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/lvgl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_vfs_console/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/vfs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/spiffs/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__cjson/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/storage/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_event/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_phy/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/lwip/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_netif_stack/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_netif/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/wpa_supplicant/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_coex/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_wifi/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/http_parser/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_http_server/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/wifi_provisioning/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp-tls/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/tcp_transport/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/mqtt/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/cloud/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__dl_fft/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__esp_new_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_jpeg/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/esp_driver_ppa/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__esp-dl/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__human_face_detect/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/espressif__human_face_recognition/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/face/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/gui/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("E:/study/share/study_space/esp32/myself/FaceAttend/build/esp-idf/main/cmake_install.cmake")
endif()

