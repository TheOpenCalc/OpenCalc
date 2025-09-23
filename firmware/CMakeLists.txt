# main/CMakeLists.txt
file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS "headers/*.h")

add_executable(ili9341_test
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(ili9341_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/headers
)

target_link_libraries(ili9341_test
    pico_stdlib
    hardware_spi
    hardware_gpio
)

pico_enable_stdio_usb(ili9341_test 1)

pico_add_extra_outputs(ili9341_test)

