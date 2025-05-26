idf_component_register(SRCS "pzem004tv3.c" "i2c-lcd.c" "meteran_online.c" 
                    PRIV_REQUIRES esp_timer spi_flash driver nvs_flash esp_wifi esp_event esp_http_client    
                    INCLUDE_DIRS ".")
