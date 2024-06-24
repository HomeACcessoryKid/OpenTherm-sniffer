idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash
                    PRIV_REQUIRES UDPlogger esp_wifi)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DUDPLOG_PRINTF_TO_UDP -DUDPLOG_PRINTF_ALSO_SERIAL")
