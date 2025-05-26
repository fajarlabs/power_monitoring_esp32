/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/i2c.h"
#include "i2c-lcd.h"
#include "string.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "telegram_root_cert.h"
#include "esp_log.h"
#include "pzem004tv3.h"

#define UART_PORT UART_NUM_0      // UART KONFIGURASI
#define UART_PORT_PZEM UART_NUM_2 // UART PZEM
#define PZEMTXD_PIN 17
#define PZEMRXD_PIN 16

#define BUF_SIZE 1024
#define STX '<'
#define ETX '>'

/* Begin Configuration  I2C */
#define I2C_MASTER_SCL_IO GPIO_NUM_22 /*!< GPIO number used for I2C master clock */
#define I2C_MASTER_SDA_IO GPIO_NUM_21 /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM 0              /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ 400000     /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0   /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0   /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS 1000
/* End Configuration I2C */

/* Begin Key Configuration NVS */
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASSWORD "wifi_password"
#define KEY_KWH_PULSE "kwh_pulse"
#define KEY_KWH_MINIMUM "kwh_minimum"
#define KEY_BOT_TOKEN "bot_token"
#define KEY_RECIPIENT_ID "recipient_id"
#define KEY_LAST_KWH "last_kwh"
/* End Key Configuration */

/* Begin Token Split */
#define MAX_TOKENS 10       // Maksimal jumlah token
#define MAX_TOKEN_LEN 64    // Maksimal panjang tiap token
/* End Token Split */

/* Begin Wifi Configuration */
// Ganti dengan Wi-Fi & Bot kamu
// #define WIFI_SSID      "POCOMF"
// #define WIFI_PASS      "Thxtoalloh!1"
// #define BOT_TOKEN      "7130641988:AAEBxaTciSkq1svgg-NtpKT75HXrWWg0UFo"
// #define CHAT_ID        "8097146482"
/* End Wifi Configuration */

/* Begin Interface function */
static const char *TAG = "meteran_online";
void uart_rx_task(void *arg);
void parse_serial(uint8_t byte);
static esp_err_t i2c_master_init(void);
uint16_t modbus_crc(uint8_t *buf, uint8_t len);
void login_main(char *route);
void init_nvs();
void save_string_to_nvs(const char *key, const char *value);
void read_string_from_nvs(const char *key, char *out_value, size_t max_len);
int split_and_store_tokens(char *input, char tokens[][MAX_TOKEN_LEN]);
void read_gpio_task(void *arg);
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
void wifi_init_sta(void);
void send_telegram_message(const char *message);
void PMonTask( void * pz );

/* End Interface function */

bool receiving = false;
char buffer[128];
int buf_index = 0;

/* Begin GPIO INPUT */
static int push_count = 0;
static bool is_fire = false;
const int WAIT_PUSH_IN_SECONDS = 2;
/* End GPIO INPUT */

/* Begin PZEM sensor */
/* @brief Set ESP32  Serial Configuration */
pzem_setup_t pzConf =
{
    .pzem_uart   = UART_NUM_2,              /*  <== Specify the UART you want to use, UART_NUM_0, UART_NUM_1, UART_NUM_2 (ESP32 specific) */
    .pzem_rx_pin = GPIO_NUM_16,             /*  <== GPIO for RX */
    .pzem_tx_pin = GPIO_NUM_17,             /*  <== GPIO for TX */
    .pzem_addr   = PZ_DEFAULT_ADDRESS,      /*  If your module has a different address, specify here or update the variable in pzem004tv3.h */
};

TaskHandle_t PMonTHandle = NULL;
_current_values_t pzValues;            /* Measured values */
/* End PZEM sensor */

void app_main(void)
{
    /* BEGIN INIT NVS*/
    init_nvs();
    /* END INIT NVS */

    /* BEGIN KONFIGURASI DARI UART0 UNTUK TERIMA DATA KONFIGURASI */
    // Konfigurasi UART0
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    // Inisialisasi UART driver
    uart_param_config(UART_PORT, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0); // queue_size = 0, queue = NULL

    // Buat task UART RX
    xTaskCreate(uart_rx_task, "uart_rx_task", 8192 , NULL, 10, NULL);
    /* END KONFIGURASI DARI UART0 UNTUK TERIMA DATA KONFIGURASI */

    /* BEGIN I2C FOR DISPLAY 16x2 */
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C initialized successfully");

    /* init LCD i2C */
    lcd_init();
    lcd_clear();

    //    lcd_put_cur(0, 0);
    //    lcd_send_string("Hello world!");
    //
    //    lcd_put_cur(1, 0);
    //    lcd_send_string("from ESP32");

    char *lcd_text = "Initialize..";
    sprintf(buffer, "%s", lcd_text);
    lcd_put_cur(0, 0);
    lcd_send_string(buffer);
    /* END I2C FOR DISPLAY 16x2 */

    /* BEGIN PZEM SENSOR INIT */
    /* Initialize/Configure UART */
    PzemInit( &pzConf );
    xTaskCreate( PMonTask, "PowerMon", ( 256 * 8 ), NULL, tskIDLE_PRIORITY, &PMonTHandle );
    /* END PZEM SENSOR INIT */

    /* Begin INPUT 36 */
    // Konfigurasi GPIO36 sebagai input
    gpio_config_t io_conf_36 = {
        .pin_bit_mask = (1ULL << GPIO_NUM_36),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_36);

    // Buat task untuk membaca GPIO
    xTaskCreate(read_gpio_task, "read_gpio_task", 2048, NULL, 10, NULL);
    /* End INPUT 36 */

    /* Begin GPIO Output 33 relay */
    gpio_config_t io_conf_33 = {
        .pin_bit_mask = (1ULL << GPIO_NUM_33), // Ganti dengan GPIO yang benar
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_33);
    /* End GPIO Output 34 relay*/

    /* Begini init Wi-Fi*/
    wifi_init_sta();
    /* End init Wi-Fi */
}

void uart_rx_task(void *arg)
{
    uint8_t data[BUF_SIZE];

    while (1)
    {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE - 1, pdMS_TO_TICKS(10));
        for (int i = 0; i < len; i++)
        {
            parse_serial(data[i]);
        }
    }
}

void parse_serial(uint8_t byte)
{
    if (byte == STX)
    {
        receiving = true;
        buf_index = 0;
        return;
    }

    if (byte == ETX && receiving)
    {
        buffer[buf_index] = '\0'; // Null-terminate
        receiving = false;
        //ESP_LOGI(TAG, "Command: %s", buffer);
        login_main(buffer);
        return;
    }

    if (receiving)
    {
        if (buf_index < sizeof(buffer) - 1)
        {
            buffer[buf_index++] = byte;
        }
        else
        {
            // Buffer overflow, reset reception
            receiving = false;
            buf_index = 0;
            ESP_LOGW(TAG, "Buffer overflow. Frame dropped.");
        }
    }
}

/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init(void)
{
    int i2c_master_port = I2C_MASTER_NUM;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    i2c_param_config(i2c_master_port, &conf);

    return i2c_driver_install(i2c_master_port, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
}

uint16_t modbus_crc(uint8_t *buf, uint8_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (int i = 0; i < 8; i++)
        {
            if ((crc & 0x0001) != 0)
            {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

void login_main(char *route)
{
    if (strchr(route, ','))
    {
        char tokens[MAX_TOKENS][MAX_TOKEN_LEN];
        int token_count = split_and_store_tokens(route, tokens);
        // for (int i = 0; i < token_count; i++) {
        //     ESP_LOGI(TAG, "Token %d: %s\n", i, tokens[i]);
        // }
        if(token_count >= 3){
            /* Begin Wifi Save to NVS */
            if(strcmp(tokens[0], "1") == 0){
                save_string_to_nvs(KEY_WIFI_SSID, tokens[1]);
                save_string_to_nvs(KEY_WIFI_PASSWORD, tokens[2]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Wifi Save to NVS */
            /* Begin Wifi Save to NVS */
            if(strcmp(tokens[0], "2") == 0){
                save_string_to_nvs(KEY_KWH_PULSE, tokens[1]);
                save_string_to_nvs(KEY_KWH_MINIMUM, tokens[2]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Wifi Save to NVS */
            /* Begin Telegram Token */
            if(strcmp(tokens[0], "3") == 0){
                save_string_to_nvs(KEY_BOT_TOKEN, tokens[1]);
                save_string_to_nvs(KEY_RECIPIENT_ID, tokens[2]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Telegram Token */
        }
    }
    else
    {
        /* End Wifi Save to NVS */
        /* Begin Relay ON */
        if(strcmp(route, "11") == 0){
            gpio_set_level(GPIO_NUM_33, 1); // Set ke HIGH
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Relay ON";
            sprintf(buffer, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer);
            ESP_LOGI(TAG, "OK");
        }
        /* End Relay ON */
        /* Begin Relay OFF */
        if(strcmp(route, "12") == 0){
            gpio_set_level(GPIO_NUM_33, 0); // Set ke LOW
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Relay OFF";
            sprintf(buffer, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer);
            ESP_LOGI(TAG, "OK");
        }
        /* End Relay ON */
        /* Begin Send Telegram */
        if(strcmp(route, "13") == 0){
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Send telegram";
            sprintf(buffer, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer);
            // do send telegram
            send_telegram_message("OK");
        }
        /* End Send Telegram */

        /* Begin Send Reboot */
        if(strcmp(route, "14") == 0){
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Rebooting...";
            sprintf(buffer, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer);
            vTaskDelay(pdMS_TO_TICKS(2000));
            // reboot
            esp_restart();
        }
        /* End Send Reboot */

        /* Begin Get Wifi */
        if(strcmp(route, "1") == 0){
            char wifi_ssid[64];
            read_string_from_nvs(KEY_WIFI_SSID, wifi_ssid, sizeof(wifi_ssid));
            char wifi_password[64];
            read_string_from_nvs(KEY_WIFI_PASSWORD, wifi_password, sizeof(wifi_password));
            ESP_LOGI(TAG,"<1,%s,%s>", wifi_ssid, wifi_password);
        }
        /* End Get Wifi */

        /* Begin Pulse KWH */
        if(strcmp(route, "2") == 0){
            char pulse_kwh[64];
            read_string_from_nvs(KEY_KWH_PULSE, pulse_kwh, sizeof(pulse_kwh));
            char kwh_minimum[64];
            read_string_from_nvs(KEY_KWH_MINIMUM, kwh_minimum, sizeof(kwh_minimum));
            ESP_LOGI(TAG,"<2,%s,%s>", pulse_kwh, kwh_minimum);
        }
        /* End Pulse KWH */

        /* Begin Bot Token */
        if(strcmp(route, "3") == 0){
            char bot_token[64];
            read_string_from_nvs(KEY_BOT_TOKEN, bot_token, sizeof(bot_token));
            char recipient_id[64];
            read_string_from_nvs(KEY_RECIPIENT_ID, recipient_id, sizeof(recipient_id));
            ESP_LOGI(TAG,"<3,%s,%s>", bot_token, recipient_id);
        }
        /* End Bot Token */
    }
}

void init_nvs() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void save_string_to_nvs(const char *key, const char *value) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, key, value);
        if (err == ESP_OK) {
            nvs_commit(handle);
        }
        nvs_close(handle);
    }
}

void read_string_from_nvs(const char *key, char *out_value, size_t max_len) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t required_size;
        err = nvs_get_str(handle, key, NULL, &required_size);
        if (err == ESP_OK && required_size <= max_len) {
            nvs_get_str(handle, key, out_value, &required_size);
        }
        nvs_close(handle);
    }
}

int split_and_store_tokens(char *input, char tokens[][MAX_TOKEN_LEN]) {
    int count = 0;
    char *token = strtok(input, ",");

    while (token != NULL && count < MAX_TOKENS) {
        strncpy(tokens[count], token, MAX_TOKEN_LEN - 1);
        tokens[count][MAX_TOKEN_LEN - 1] = '\0'; // Null-terminate
        count++;
        token = strtok(NULL, ",");
    }

    return count; // Jumlah token yang disimpan
}

void read_gpio_task(void *arg)
{
    while (1) {
        int level = gpio_get_level(GPIO_NUM_36);
        if(level == 0){
            if(push_count >= WAIT_PUSH_IN_SECONDS){
                if(!is_fire){
                    is_fire = true;
                    ESP_LOGI(TAG, "FIRE");
                }
            }
            push_count++;
        } else {
            push_count = 0; // reset
            is_fire = false;
        }        
        vTaskDelay(pdMS_TO_TICKS(1000)); // delay 500ms
    }
}

// Wi-Fi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi Connected");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Wi-Fi Disconnected, retrying...");
        esp_wifi_connect();
    }
}

void wifi_init_sta(void)
{
    // Buffer untuk baca dari NVS
    char wifi_ssid[64];
    read_string_from_nvs(KEY_WIFI_SSID, wifi_ssid, sizeof(wifi_ssid));

    char wifi_password[64];
    read_string_from_nvs(KEY_WIFI_PASSWORD, wifi_password, sizeof(wifi_password));

    // Inisialisasi WiFi
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);

    // Inisialisasi struct wifi_config dan masukkan SSID + Password
    wifi_config_t wifi_config = {0};

    strncpy((char *)wifi_config.sta.ssid, wifi_ssid, sizeof(wifi_config.sta.ssid));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    strncpy((char *)wifi_config.sta.password, wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Set konfigurasi dan mulai koneksi
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
}

// Kirim pesan ke Telegram
void send_telegram_message(const char *message)
{
    char bot_token[64];
    read_string_from_nvs(KEY_BOT_TOKEN, bot_token, sizeof(bot_token));
    char recipient_id[64];
    read_string_from_nvs(KEY_RECIPIENT_ID, recipient_id, sizeof(recipient_id));

    char url[256];
    snprintf(url, sizeof(url),"https://api.telegram.org/bot%s/sendMessage", bot_token);

    char post_data[512];
    snprintf(post_data, sizeof(post_data),"chat_id=%s&text=%s", recipient_id, message);

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = telegram_root_cert,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Telegram sent! Status = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "Telegram send failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void PMonTask( void * pz )
{
    for( ;; )
    {
        PzemGetValues( &pzConf, &pzValues );
        printf( "Vrms: %.1fV - Irms: %.3fA - P: %.1fW - E: %.2fWh\n", pzValues.voltage, pzValues.current, pzValues.power, pzValues.energy );
        printf( "Freq: %.1fHz - PF: %.2f\n", pzValues.frequency, pzValues.pf );

        ESP_LOGI( TAG, "Stack High Water Mark: %ld Bytes free", ( unsigned long int ) uxTaskGetStackHighWaterMark( NULL ) );     /* Show's what's left of the specified stacksize */

        vTaskDelay( pdMS_TO_TICKS( 2500 ) );
    }

    vTaskDelete( NULL );
}