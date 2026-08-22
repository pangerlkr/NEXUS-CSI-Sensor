# NEXUS CSI Sensor - main component
#
# Web assets live in the repository-level web/ directory and are embedded into
# the firmware binary at build time (served from flash, no filesystem needed).

set(WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../web")

idf_component_register(
    SRCS
        "main.c"
        "utils.c"
        "ringbuffer.c"
        "json.c"
        "storage.c"
        "config.c"
        "logger.c"
        "auth.c"
        "wifi.c"
        "csi.c"
        "motion.c"
        "display.c"
        "ota.c"
        "webserver.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
        esp_timer
        esp_app_format
        app_update
        spi_flash
        driver
        json
        mbedtls
        lwip
        esp_hw_support
    EMBED_FILES
        "${WEB_DIR}/index.html"
        "${WEB_DIR}/login.html"
        "${WEB_DIR}/css/style.css"
        "${WEB_DIR}/js/app.js"
)
