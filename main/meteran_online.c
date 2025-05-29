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
#define KEY_TOPUP_KWH "topup_kwh"
#define KEY_KWH_MINIMUM "kwh_minimum"
#define KEY_BOT_TOKEN "bot_token"
#define KEY_RECIPIENT_ID "recipient_id"
#define KEY_DAILY_LIMIT "daily_limit"
#define KEY_LAST_WH "last_kwh"
#define KEY_TIME_SAMPLING "time_sampling"
#define KEY_TDL "tdl"
#define KEY_CURRENT_WH_USE "current_wh_use"
/* End Key Configuration */

/* Begin Token Split */
#define MAX_TOKENS 10    // Maksimal jumlah token
#define MAX_TOKEN_LEN 64 // Maksimal panjang tiap token
/* End Token Split */

/* Begin Batas Listrik KVA */
#define KAPASITAS_450VA 400
#define KAPASITAS_900VA 700
#define KAPASITAS_1300VA 1100
#define KAPASITAS_2200VA 1800
#define KAPASITAS_3500VA 2500
/* End Batas Listrik KVA*/

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
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
void wifi_init_sta(void);
void send_telegram_message(const char *message);
void PMonTask(void *pz);

/* End Interface function */

bool receiving = false;
char buffer[20];
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
        .pzem_uart = UART_NUM_2,         /*  <== Specify the UART you want to use, UART_NUM_0, UART_NUM_1, UART_NUM_2 (ESP32 specific) */
        .pzem_rx_pin = GPIO_NUM_16,      /*  <== GPIO for RX */
        .pzem_tx_pin = GPIO_NUM_17,      /*  <== GPIO for TX */
        .pzem_addr = PZ_DEFAULT_ADDRESS, /*  If your module has a different address, specify here or update the variable in pzem004tv3.h */
};

TaskHandle_t PMonTHandle = NULL;
_current_values_t pzValues; /* Measured values */
/* End PZEM sensor */

/* Begin LCD Lock Text */
bool is_info_relay = false;
bool is_reboot = false;
bool is_beep = false;
bool is_saldo_lock = false;
bool is_reset_0_lock = false;
bool is_relay_on = false;
bool is_relay_off = false;
/* End LCD Lock Text */

/* Begin telegram counting next message */
int count_next_message = 0;
const int NEXT_SEND_TELEGRAM = 60; // 60 detik dikirim kembali
bool is_single_message_telegram = false;
/* End telegram counting next message */

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
    xTaskCreate(uart_rx_task, "uart_rx_task", 8192, NULL, 10, NULL);
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
    PzemInit(&pzConf);
    xTaskCreate(PMonTask, "PowerMon", (5120), NULL, tskIDLE_PRIORITY, &PMonTHandle);
    /* END PZEM SENSOR INIT */

    /* Begin INPUT 36 */
    // Konfigurasi GPIO36 sebagai input
    gpio_config_t io_conf_36 = {
        .pin_bit_mask = (1ULL << GPIO_NUM_36),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
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
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf_33);
    /* End GPIO Output 34 relay*/

    /* Begini init Wi-Fi*/
    wifi_init_sta();
    /* End init Wi-Fi */
}

void uart_rx_task(void *arg)
{
    uint8_t data[BUF_SIZE];

    while (!is_reboot)
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
        // ESP_LOGI(TAG, "Command: %s", buffer);
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
        if (token_count >= 2)
        {
            /* Begin Wifi Save to NVS */
            if (strcmp(tokens[0], "1") == 0)
            {
                save_string_to_nvs(KEY_WIFI_SSID, tokens[1]);
                save_string_to_nvs(KEY_WIFI_PASSWORD, tokens[2]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Wifi Save to NVS */
            /* Begin Data Save to NVS */
            if (strcmp(tokens[0], "2") == 0)
            {
                save_string_to_nvs(KEY_KWH_MINIMUM, tokens[1]);
                save_string_to_nvs(KEY_DAILY_LIMIT, tokens[2]);
                save_string_to_nvs(KEY_TIME_SAMPLING, tokens[3]);
                save_string_to_nvs(KEY_TDL, tokens[4]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Data Save to NVS */
            /* Begin Telegram Token */
            if (strcmp(tokens[0], "3") == 0)
            {
                save_string_to_nvs(KEY_BOT_TOKEN, tokens[1]);
                save_string_to_nvs(KEY_RECIPIENT_ID, tokens[2]);
                ESP_LOGI(TAG, "OK");
            }
            /* End Telegram Token */
            /* Begin Topup KWH */
            if (strcmp(tokens[0], "4") == 0)
            {

                is_saldo_lock = true;
                vTaskDelay(pdMS_TO_TICKS(1000));

                save_string_to_nvs(KEY_TOPUP_KWH, tokens[1]);

                char last_kwh[10];
                read_string_from_nvs(KEY_LAST_WH, last_kwh, sizeof(last_kwh));
                float kwh_float = atof(last_kwh); 
                float topup_kwh_float = atof(tokens[1]);
                float total_wh_float = kwh_float + (topup_kwh_float*1000);

                char buffer_total_wh[10];
                sprintf(buffer_total_wh, "%.2f", total_wh_float);
                save_string_to_nvs(KEY_LAST_WH, buffer_total_wh);

                vTaskDelay(pdMS_TO_TICKS(1000));
                is_saldo_lock = false;
                ESP_LOGI(TAG, "OK");
            }
            /* End Topup KWH */
        }
    }
    else
    {
        /* Begin Relay ON */
        if (strcmp(route, "11") == 0)
        {
            is_relay_on = true;
            gpio_set_level(GPIO_NUM_33, 1); // Set ke HIGH
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Relay ON";
            char buffer_relay[20];
            sprintf(buffer_relay, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_relay);
            ESP_LOGI(TAG, "OK");
        }
        /* End Relay ON */
        /* Begin Relay OFF */
        if (strcmp(route, "12") == 0)
        {
            is_relay_off = true;
            gpio_set_level(GPIO_NUM_33, 0); // Set ke LOW
            // clear LCD
            lcd_clear_row(1);
            // set text
            char *lcd_text = "Relay OFF";
            char buffer_relay[20];
            sprintf(buffer_relay, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_relay);
            ESP_LOGI(TAG, "OK");
        }
        /* End Relay ON */
        /* Begin Send Telegram */
        if (strcmp(route, "13") == 0)
        {
            // clear LCD
            lcd_clear_row(1);
            // set text
            char buffer_telegram[20];
            sprintf(buffer_telegram, "%s", "Send telegram");
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_telegram);
            // do send telegram
            send_telegram_message("OK");
            ESP_LOGI(TAG, "OK");
        }
        /* End Send Telegram */

        /* Begin Send Reboot */
        if (strcmp(route, "14") == 0)
        {
            is_reboot = true; // stop all threads
            // clear LCD
            lcd_clear();
            // set text
            char buffer_telegram[20];
            sprintf(buffer_telegram, "%s", "Rebooting...");
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_telegram);
            vTaskDelay(pdMS_TO_TICKS(2000));
            // reboot
            esp_restart();
        }
        /* End Send Reboot */

        /* Begin Send Normally Relay */
        if (strcmp(route, "15") == 0)
        {
            is_relay_on = false;
            is_relay_off = false;
            // clear LCD
            lcd_clear_row(1);
            // tunggu proses clear selesai
            vTaskDelay(pdMS_TO_TICKS(500));
            char last_kwh[10];
            read_string_from_nvs(KEY_LAST_WH, last_kwh, sizeof(last_kwh));
            float total_last_wh = atof(last_kwh);

            // set text
            char *lcd_text = "Relay OFF";
            if (total_last_wh > 0)
                lcd_text = "Relay ON";

            char buffer_relay[20];
            sprintf(buffer_relay, "%s", lcd_text);
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_relay);
            ESP_LOGI(TAG, "OK");
        }
        /* End Send Normally Relay */

        /* Begin Get Wifi */
        if (strcmp(route, "1") == 0)
        {
            char wifi_ssid[64];
            read_string_from_nvs(KEY_WIFI_SSID, wifi_ssid, sizeof(wifi_ssid));
            char wifi_password[64];
            read_string_from_nvs(KEY_WIFI_PASSWORD, wifi_password, sizeof(wifi_password));
            ESP_LOGI(TAG, "<1,%s,%s>", wifi_ssid, wifi_password);
        }
        /* End Get Wifi */

        /* Begin Pulse KWH */
        if (strcmp(route, "2") == 0)
        {
            char topup_kwh[10];
            read_string_from_nvs(KEY_TOPUP_KWH, topup_kwh, sizeof(topup_kwh));
            char kwh_minimum[10];
            read_string_from_nvs(KEY_KWH_MINIMUM, kwh_minimum, sizeof(kwh_minimum));
            char daily_limit[10];
            read_string_from_nvs(KEY_DAILY_LIMIT, daily_limit, sizeof(daily_limit));
            char last_wh[10];
            read_string_from_nvs(KEY_LAST_WH, last_wh, sizeof(last_wh));
            float pembulatan_kwh = atof(last_wh) / 1000.0;
            float sisa_kwh_rounded = floorf(pembulatan_kwh * 10) / 10;

            char buffer_convert_to_kwh[10];  // pastikan cukup besar
            sprintf(buffer_convert_to_kwh, "%.1f", sisa_kwh_rounded);  // format 2 digit di belakang koma

            char sampling_time[10];
            read_string_from_nvs(KEY_TIME_SAMPLING, sampling_time, sizeof(sampling_time));
            char tdl[10];
            read_string_from_nvs(KEY_TDL, tdl, sizeof(tdl));

            /* Print ESP-LOG */
            ESP_LOGI(TAG, "<2,%s,%s,%s,%s,%s,%s>", topup_kwh, kwh_minimum, daily_limit, buffer_convert_to_kwh, sampling_time, tdl);
        }
        /* End Pulse KWH */

        /* Begin Bot Token */
        if (strcmp(route, "3") == 0)
        {
            char bot_token[64];
            read_string_from_nvs(KEY_BOT_TOKEN, bot_token, sizeof(bot_token));
            char recipient_id[64];
            read_string_from_nvs(KEY_RECIPIENT_ID, recipient_id, sizeof(recipient_id));
            ESP_LOGI(TAG, "<3,%s,%s>", bot_token, recipient_id);
        }
        /* End Bot Token */

        /* Begin Send Reset 0 */
        if (strcmp(route, "20") == 0)
        {
            is_reset_0_lock = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            // clear LCD
            lcd_clear_row(1);
            // set text
            char buffer_reset_kwh[20];
            sprintf(buffer_reset_kwh, "%s", "Reset Kwh");
            lcd_put_cur(1, 0);
            lcd_send_string(buffer_reset_kwh);

            sprintf(buffer_reset_kwh, "%s", "      ");
            lcd_put_cur(0, 4);
            lcd_send_string(buffer_reset_kwh);

            // set last KWH
            save_string_to_nvs(KEY_LAST_WH, "0");
            vTaskDelay(pdMS_TO_TICKS(1000));
            is_reset_0_lock = false;
        }
        /* End Send Reset 0 */
    }
}

void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void save_string_to_nvs(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
        err = nvs_set_str(handle, key, value);
        if (err == ESP_OK)
        {
            nvs_commit(handle);
        }
        nvs_close(handle);
    }
}

void read_string_from_nvs(const char *key, char *out_value, size_t max_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);
    if (err == ESP_OK)
    {
        size_t required_size;
        err = nvs_get_str(handle, key, NULL, &required_size);
        if (err == ESP_OK && required_size <= max_len)
        {
            nvs_get_str(handle, key, out_value, &required_size);
        }
        nvs_close(handle);
    }
}

int split_and_store_tokens(char *input, char tokens[][MAX_TOKEN_LEN])
{
    int count = 0;
    char *token = strtok(input, ",");

    while (token != NULL && count < MAX_TOKENS)
    {
        strncpy(tokens[count], token, MAX_TOKEN_LEN - 1);
        tokens[count][MAX_TOKEN_LEN - 1] = '\0'; // Null-terminate
        count++;
        token = strtok(NULL, ",");
    }

    return count; // Jumlah token yang disimpan
}

void read_gpio_task(void *arg)
{
    while (!is_reboot)
    {
        int level = gpio_get_level(GPIO_NUM_36);
        if (level == 0)
        {
            if (push_count >= WAIT_PUSH_IN_SECONDS)
            {
                if (!is_fire)
                {
                    is_fire = true;
                    ESP_LOGI(TAG, "FIRE");
                }
            }
            push_count++;
        }
        else
        {
            push_count = 0; // reset
            is_fire = false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // delay 500ms
    }
}

// Wi-Fi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "Wi-Fi Connected");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
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
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", bot_token);

    char post_data[512];
    snprintf(post_data, sizeof(post_data), "chat_id=%s&text=%s", recipient_id, message);

    esp_http_client_config_t config = {
        .url = url,
        .cert_pem = telegram_root_cert,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Telegram sent! Status = %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "Telegram send failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

void PMonTask(void *pz)
{
    char key_tdl[10];
    read_string_from_nvs(KEY_TDL, key_tdl, sizeof(key_tdl));

    char sampling_time[10];
    read_string_from_nvs(KEY_TIME_SAMPLING, sampling_time, sizeof(sampling_time));

    char last_wh[10];
    read_string_from_nvs(KEY_LAST_WH, last_wh, sizeof(last_wh));

    char limit_kwh_char[10];
    read_string_from_nvs(KEY_KWH_MINIMUM, limit_kwh_char, sizeof(limit_kwh_char));
    float limit_kwh = atof(limit_kwh_char);

    ESP_LOGI(TAG, "Key TDL : %s, Sampling Time : %s, last KWH : %s", key_tdl, sampling_time, last_wh);

    float saldo_wh = atof(last_wh) * 1000; // last kwh
    float tarif_per_kwh = atof(key_tdl);
    int pdmsDelay = atoi(sampling_time);
    lcd_clear();
    lcd_put_cur(0, 0);
    lcd_send_string("Kwh:");
    lcd_put_cur(0, 10);
    lcd_send_string("P:");
    lcd_put_cur(1, 15);
    lcd_send_data(0xFF);

    while (!is_reboot)
    {

        /* Begin read Last KWH */
        read_string_from_nvs(KEY_LAST_WH, last_wh, sizeof(last_wh));
        saldo_wh = atof(last_wh); // last kwh
        /* Last read Last KWH */

        // cancel semua aktifitas
        if (is_reboot || is_saldo_lock || is_reset_0_lock)
            continue;

        if (saldo_wh > 0) is_single_message_telegram = false; // reset sekali pesan flag

        // limit untuk kirim notifikasi
        if ((saldo_wh / 1000) < limit_kwh)
        {
            ESP_LOGI(TAG, "Limit Kwh kurang!");
            if (count_next_message < 1)
            {
                char info_pulsa[100];
                if (saldo_wh > 0)
                {
                    float pembulatan_kwh = saldo_wh / 1000.0;
                    float sisa_kwh_rounded = floorf(pembulatan_kwh * 10) / 10;
                    sprintf(info_pulsa, "Pulsa listrik anda akan segera habis, sisa Kwh:%.1f", sisa_kwh_rounded); // save to kwh
                    send_telegram_message(info_pulsa);
                }
                else
                {
                    if (!is_single_message_telegram)
                    {
                        is_single_message_telegram = true;
                        sprintf(info_pulsa, "Pulsa listrik anda telah habis"); // save to kwh
                        send_telegram_message(info_pulsa);
                    }
                }
            }

            count_next_message += 1;
            
            if (count_next_message >= NEXT_SEND_TELEGRAM)
            {
                count_next_message = 0; // reset
            }
        }

        // Ambil data dari PZEM
        PzemGetValues(&pzConf, &pzValues);

        // ============ Begin Rumus yang digunakan ====================
        // Daya (W)=V×I
        // Energi Per Jam (Wh)=Daya (W)×Waktu (jam)
        // Energy Per Detik (wh)=Energy Per Jam / 3600 detik

        float daya = pzValues.voltage * pzValues.current * pzValues.pf;
        float energy_per_jam = daya * 1;
        float energy_per_detik = energy_per_jam / 3600.0;

        // ============ End Rumus yang digunakan ====================

        if (is_reboot || is_saldo_lock || is_reset_0_lock)
            continue; // barrier ke 2

        if (daya <= KAPASITAS_1300VA)
        {

            // Kurangi saldo
            if (saldo_wh > 0) {
                
                char buffer_current_wh_use[10];
                read_string_from_nvs(KEY_CURRENT_WH_USE, buffer_current_wh_use, sizeof(buffer_current_wh_use));
                float current_wh_use = atof(buffer_current_wh_use);
                current_wh_use += energy_per_detik;

                char data_daily_limit[10];
                read_string_from_nvs(KEY_DAILY_LIMIT, data_daily_limit, sizeof(data_daily_limit));
                float daily_limit = atof(data_daily_limit);
                if(current_wh_use >= (daily_limit*1000)){
                    char buffer_batas_harian[20];
                    sprintf(buffer_batas_harian, "%s", "> Batas Harian!");
                    lcd_put_cur(1, 0);
                    lcd_send_string(buffer_batas_harian);
                    is_relay_off = true; // matikan relay
                }

                ESP_LOGI(TAG, "Beban akumulasi : %.2f", current_wh_use);
                sprintf(buffer_current_wh_use, "%.2f", current_wh_use); // save to kwh
                save_string_to_nvs(KEY_CURRENT_WH_USE, buffer_current_wh_use);

                saldo_wh -= energy_per_detik;
            } else
                saldo_wh = 0;

            // update saldo terbaru

            /* Begin save last kwh */
            char buffer_saldo_kwh[10];
            sprintf(buffer_saldo_kwh, "%.2f", saldo_wh); // save to kwh
            save_string_to_nvs(KEY_LAST_WH, buffer_saldo_kwh);
            /* End save last kwh */

            // Hitung sisa pulsa dalam rupiah
            float sisa_rupiah = (saldo_wh / 1000.0) * tarif_per_kwh;

            // Tampilkan info
            ESP_LOGI(TAG, "Vrms: %.1fV - Irms: %.3fA - P: %.1fW - E: %.2fWh", pzValues.voltage, pzValues.current, pzValues.power, pzValues.energy);
            ESP_LOGI(TAG, "Freq: %.1fHz - PF: %.2f", pzValues.frequency, pzValues.pf);
            ESP_LOGI(TAG, "Pemakaian: %.3f Wh | Sisa Pulsa: %.1f Wh (Rp %.2f)", energy_per_detik, saldo_wh, sisa_rupiah);

            if (is_reboot || is_saldo_lock || is_reset_0_lock)
                continue; // barrier ke 3

            /* Begin info KWH */
            float pembulatan_kwh = saldo_wh / 1000.0;
            float sisa_kwh_rounded = floorf(pembulatan_kwh * 10) / 10;
            char buffer_saldo_wh[10];
            lcd_put_cur(0, 4);
            sprintf(buffer_saldo_wh, "%.1f", sisa_kwh_rounded); // wh jadi kwh jadi di bagi 1000

            lcd_send_string(buffer_saldo_wh);

            lcd_put_cur(0, 12);
            sprintf(buffer_saldo_wh, "%.1f", daya);
            lcd_send_string(buffer_saldo_wh);

            // cek apakah ada tegangan ?
            if (pzValues.voltage > 100)
            {
                if (!is_beep)
                {
                    is_beep = true;
                    lcd_put_cur(1, 15);
                    lcd_send_string(" ");
                }
                else
                {
                    is_beep = false;
                    uint8_t love_icon[8] = {
                        0b00000,
                        0b01010,
                        0b11111,
                        0b11111,
                        0b11111,
                        0b01110,
                        0b00100,
                        0b00000
                    };

                    lcd_send_cmd(0x40); // CGRAM address for slot 0
                    for (int i = 0; i < 8; i++) {
                        lcd_send_data(love_icon[i]);
                    }

                    lcd_put_cur(1, 15);  // Baris 1 (indeks 0), kolom paling kanan (indeks 15)
                    lcd_send_data(0);    // Tampilkan karakter CGRAM slot 0
                }
            }
            /* End info KWH */

            // Kontrol relay
            if (saldo_wh > 0)
            {
                /* Begin Info Relay ON */
                if (!is_info_relay)
                {
                    is_info_relay = true;
                    lcd_clear_row(1);
                    char *lcd_text = "Switch ON";
                    sprintf(buffer, "%s", lcd_text);
                    lcd_put_cur(1, 0);
                    lcd_send_string(buffer);
                }
                /* End Info Relay ON */

                /* Begin Relay ON */
                if (is_relay_off || is_relay_on)
                {
                }
                else
                {
                    gpio_set_level(GPIO_NUM_33, 1);
                }
                /* End Relay ON */
            }
            else
            {
                /* Begin Info Relay OFF */
                if (is_info_relay)
                {
                    is_info_relay = false;
                    lcd_clear_row(1);
                    char *lcd_text = "Switch OFF";
                    sprintf(buffer, "%s", lcd_text);
                    lcd_put_cur(1, 0);
                    lcd_send_string(buffer);
                }
                /* End Info Relay OFF */

                /* Begin Relay OFF */
                if (is_relay_off || is_relay_on)
                {
                }
                else
                {
                    gpio_set_level(GPIO_NUM_33, 0);
                }
                /* End Relay OFF */
            }

            ESP_LOGI(TAG, "Stack High Water Mark: %ld Bytes free",
                     (unsigned long int)uxTaskGetStackHighWaterMark(NULL));
        }
        else
        {
            ESP_LOGE(TAG, "Tegangan over / gangguan signal");
        }

        vTaskDelay(pdMS_TO_TICKS(pdmsDelay));
    }

    vTaskDelete(NULL);
}
