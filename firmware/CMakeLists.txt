# main/CMakeLists.txt
file(GLOB CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core/*.c"
)
file(GLOB APP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Apps/*.cpp")
file(GLOB PIKA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-core/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-api/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-lib/PikaStdLib/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-lib/math/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-lib/time/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-lib/random/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-lib/pika_libc/*.c"
)
file(GLOB HEADERS "headers/*.h")

add_executable(ili9341_test
    ${CORE_SOURCES}
    ${APP_SOURCES}
    ${PIKA_SOURCES}
    ${HEADERS}
)

target_include_directories(ili9341_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/headers
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-core
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pikapython/pikascript-api
)

target_compile_definitions(ili9341_test PRIVATE PIKA_CONFIG_ENABLE)
# Recovery build: force-clear the exam flag at boot and return to normal menu.
# Uncomment to clean a device stuck in exam mode; the flag is un-persisted.
# The default production firmware never disables exam mode at boot; leaving exam
# mode is done only by reflashing this recovery build (or the normal FW).
# REMEMBER to re-comment this line for production builds.
# target_compile_definitions(ili9341_test PRIVATE EXAM_RECOVERY)

target_link_libraries(ili9341_test
    pico_stdlib
    pico_multicore
    pico_util
    hardware_spi
    hardware_gpio
    hardware_flash
    hardware_sync
    m
)

pico_enable_stdio_usb(ili9341_test 1)

pico_add_extra_outputs(ili9341_test)
