# The test app reuses the production sources for the pure-logic modules
# directly from ../../main, so the code under test is byte-for-byte the code
# that ships in the firmware.

set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../main")

idf_component_register(
    SRCS
        "test_main.c"
        "${SRC_DIR}/ringbuffer.c"
        "${SRC_DIR}/json.c"
        "${SRC_DIR}/utils.c"
    INCLUDE_DIRS
        "."
        "${SRC_DIR}"
    REQUIRES
        unity
        json
        mbedtls
        esp_timer
        esp_hw_support
)
