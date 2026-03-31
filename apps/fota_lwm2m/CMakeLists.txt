cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
<<<<<<< Updated upstream
project(fota_lwm2m)
=======
project(fota_http)
>>>>>>> Stashed changes

target_include_directories(app PRIVATE include)

target_sources(app PRIVATE src/main.c src/firmware_update.c)
