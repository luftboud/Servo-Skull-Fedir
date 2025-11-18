#define CONFIG_ESP_WIFI_SSID      "UCU_Guest"
#define CONFIG_ESP_WIFI_PASSWORD  ""
#define YOUR_WAV_FILE_URL         "https://dl.espressif.com/dl/audio/ff-16b-1c-44100hz.wav"
// #define YOUR_WAV_FILE_URL         ""

#define AUDIO_SAMPLE_RATE         (16000)
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "audio_pipeline.h"
#include "http_stream.h"
#include "wav_decoder.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_spiffs.h"
#include "sdkconfig.h"
#include "raw_stream.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "wav_encoder.h"
#include "wav_decoder.h"
#include "board.h"
#include "filter_resample.h"
#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "driver/dac_continuous.h"

static const char *TAG_MAIN = "APP_MAIN";
static const char *TAG_ADF = "ADF_PRODUCER";
static const char *TAG_DAC = "DAC_CONSUMER";


static audio_element_handle_t g_raw_writer = NULL;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1



static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGE(TAG_MAIN, "connect to the AP fail");
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_MAIN, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_connect(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_MAIN, "wifi_init_sta finished.");

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_MAIN, "connected to ap SSID:%s", CONFIG_ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_MAIN, "Failed to connect to SSID:%s", CONFIG_ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG_MAIN, "UNEXPECTED EVENT");
    }
}


void adf_producer_task(void *pvParameters)
{
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t http_stream_reader, wav_decoder, filter, raw_writer;

    ESP_LOGI(TAG_ADF, "Starting ADF Producer Task");

    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);

    http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
    http_stream_reader = http_stream_init(&http_cfg);
    audio_element_set_uri(http_stream_reader, YOUR_WAV_FILE_URL);

    wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
    wav_decoder = wav_decoder_init(&wav_cfg);

    rsp_filter_cfg_t filter_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    filter_cfg.src_rate = 16000;
    filter_cfg.src_ch = 1;
    filter_cfg.dest_rate = AUDIO_SAMPLE_RATE;
    filter_cfg.dest_bits = 16;
    filter_cfg.dest_ch = 1;
    filter = rsp_filter_init(&filter_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_writer = raw_stream_init(&raw_cfg);

    g_raw_writer = raw_writer;

    audio_pipeline_register(pipeline, http_stream_reader, "http");
    audio_pipeline_register(pipeline, wav_decoder,        "wav");
    audio_pipeline_register(pipeline, filter,             "filter");
    audio_pipeline_register(pipeline, raw_writer,         "raw");

    const char *link_tags[] = {"http", "wav", "filter", "raw"};
    audio_pipeline_link(pipeline, &link_tags[0], 4);

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG_ADF, "Pipeline starting...");
    audio_pipeline_run(pipeline);

    while (1) {
        audio_event_iface_msg_t msg;
        if (audio_event_iface_listen(evt, &msg, portMAX_DELAY) != ESP_OK) {
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
            (int)msg.data == AEL_STATUS_STATE_STOPPED) {
            ESP_LOGW(TAG_ADF, "Pipeline stopped");
            break;
        }
        
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
            msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
            (int)msg.data == AEL_STATUS_STATE_FINISHED) {
            ESP_LOGI(TAG_ADF, "Pipeline finished");
            break;
        }
    }

    ESP_LOGI(TAG_ADF, "Cleaning up ADF pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);
    
    audio_pipeline_unregister(pipeline, http_stream_reader);
    audio_pipeline_unregister(pipeline, wav_decoder);
    audio_pipeline_unregister(pipeline, filter);
    audio_pipeline_unregister(pipeline, raw_writer);
    
    audio_element_deinit(http_stream_reader);
    audio_element_deinit(wav_decoder);
    audio_element_deinit(filter);
    audio_element_deinit(raw_writer);
    
    audio_pipeline_deinit(pipeline);

    ESP_LOGI(TAG_ADF, "ADF Task deleting");
    vTaskDelete(NULL);
}



void dac_consumer_task(void *pvParameters)
{
    dac_continuous_handle_t dac_handle;
    
    dac_continuous_config_t cont_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_ALL,
        .desc_num = 4,
        .buf_size = 2048,
        .freq_hz = AUDIO_SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cont_cfg, &dac_handle));
    ESP_ERROR_CHECK(dac_continuous_enable(dac_handle));

    static int16_t buf[2048];
    static uint8_t new_buf[2048];

    ESP_LOGI(TAG_DAC, "DAC Consumer task started, sample rate %d Hz", AUDIO_SAMPLE_RATE);

    while (1) {
        if (g_raw_writer == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int bytes_read = raw_stream_read(g_raw_writer, (char *)buf, sizeof(buf)) / 2; // Loading data

        for (int i = 0; i < bytes_read; i++) {
            new_buf[i] = (uint8_t)((buf[i] >> 8) + 128);
        }

        if (bytes_read > 0) {
            ESP_ERROR_CHECK(dac_continuous_write(dac_handle, new_buf, bytes_read, NULL, -1));
        } else if (bytes_read == AEL_IO_TIMEOUT) {
            ESP_LOGW(TAG_DAC, "raw_stream_read timeout");
        } else if (bytes_read == AEL_IO_DONE) {
            ESP_LOGI(TAG_DAC, "RAW stream finished");
            break;
        } else if (bytes_read < 0) {
            ESP_LOGE(TAG_DAC, "raw_stream_read error: %d", bytes_read);
            break;
        }
    }
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());

    wifi_connect();

    xTaskCreate(adf_producer_task, "adf_producer", 8 * 1024, NULL, 5, NULL);
    xTaskCreate(dac_consumer_task, "dac_consumer", 4 * 1024, NULL, 5, NULL);
    
    ESP_LOGI(TAG_MAIN, "app_main finished, tasks are running.");
}