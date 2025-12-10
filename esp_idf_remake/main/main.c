#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_spiffs.h"
#include "sdkconfig.h"

#include "driver/adc.h"
#include "driver/dac_continuous.h"
#include "driver/mcpwm_prelude.h"
#include "driver/gpio.h"

#include "audio_pipeline.h"
#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "raw_stream.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "fatfs_stream.h"
#include "wav_encoder.h"
#include "wav_decoder.h"
#include "filter_resample.h"
#include "board.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_button.h"

#include "mbedtls/base64.h"

#define CONFIG_ESP_WIFI_SSID      "UCU_Guest"
#define CONFIG_ESP_WIFI_PASSWORD  ""

#define AUDIO_SAMPLE_RATE         (16000)

#define MIC_ADC_CHANNEL ADC1_CHANNEL_7  // pin35
#define SAMPLE_RATE 5512


#define MAX_RECORD_SECONDS      6
#define NUM_SAMPLES_MAX         (SAMPLE_RATE * MAX_RECORD_SECONDS)
#define PCM_DATA_SIZE_MAX       (NUM_SAMPLES_MAX * sizeof(int16_t))
#define WAV_TOTAL_SIZE_MAX      (sizeof(wav_header_t) + PCM_DATA_SIZE_MAX)

#define MAX_BUFFER_SIZE 90000

#define SILENCE_THRESHOLD       800
#define SILENCE_BLOCKS_TO_STOP  15

#define BUTTON_PIN 5

#define SERVO_MIN_PULSEWIDTH_US 500
#define SERVO_MAX_PULSEWIDTH_US 2500
#define SERVO_MIN_DEGREE        -90
#define SERVO_MAX_DEGREE        90

#define SERVO_PULSE_GPIO_1             0
#define SERVO_PULSE_GPIO_2             2
#define SERVO_TIMEBASE_RESOLUTION_HZ 1000000
#define SERVO_TIMEBASE_PERIOD        20000

#define SERVO_MOUTH_CLOSED 75
#define SERVO_MOUTH_OPEN 55

typedef struct {
    char riff_tag[4];
    uint32_t riff_length;
    char wave_tag[4]; 
    char fmt_tag[4];
    uint32_t fmt_length;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_tag[4];
    uint32_t data_length;
} wav_header_t;

volatile bool is_audio_playing = false;

typedef struct {
    mcpwm_cmpr_handle_t cmpr1;
    mcpwm_cmpr_handle_t cmpr2;
} servo_handles_t;

static const char *TAG = "APP";
static const char *TAG_MAIN = "APP_MAIN";
static const char *TAG_ADF = "ADF_PRODUCER";
static const char *TAG_DAC = "DAC_CONSUMER";
static const char *TAG_SERVO = "SERVO_MANAGER";

static audio_element_handle_t g_raw_writer = NULL;

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


static const char *IP_PORT = "10.10.241.221:8000";
static char WAV_FILE_URL[64];
static char ASK_LLM_URL[64]; 

static uint8_t wav_buffer[WAV_TOTAL_SIZE_MAX];

static char* base64_code;
static size_t base64_len = 0;

static size_t actual_wav_size = 0;


void adc_init(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(MIC_ADC_CHANNEL, ADC_ATTEN_DB_11);
}


void write_wav_header_to_mem(uint8_t *dest, uint32_t data_len) {
    wav_header_t header;

    memcpy(header.riff_tag, "RIFF", 4);
    header.riff_length = 36 + data_len;

    memcpy(header.wave_tag, "WAVE", 4);
    memcpy(header.fmt_tag, "fmt ", 4);

    header.fmt_length = 16;
    header.audio_format = 1;
    header.num_channels = 1;
    header.sample_rate = SAMPLE_RATE;
    header.bits_per_sample = 16;

    header.byte_rate = SAMPLE_RATE * header.num_channels * header.bits_per_sample / 8;
    header.block_align = header.num_channels * header.bits_per_sample / 8;

    memcpy(header.data_tag, "data", 4);
    header.data_length = data_len;

    memcpy(dest, &header, sizeof(wav_header_t));
}

static int measure_dc_mid(void) {
    const int N = 2000;
    int64_t sum = 0;
    for (int i = 0; i < N; ++i) {
        sum += adc1_get_raw(MIC_ADC_CHANNEL);
        esp_rom_delay_us(1000000 / SAMPLE_RATE);
    }
    int mid = (int)(sum / N);
    return mid;
}

void record_task(void) {
    ESP_LOGI(TAG, "Start recording...");

    int dc_mid = measure_dc_mid();

    int16_t *pcm = (int16_t *)(wav_buffer + sizeof(wav_header_t));
    uint32_t total_samples = 0;

    const int period_us = 1000000 / SAMPLE_RATE;
    int64_t next_time = esp_timer_get_time();

    while (total_samples < NUM_SAMPLES_MAX) {
        int raw = adc1_get_raw(MIC_ADC_CHANNEL);

        int32_t centered = (int32_t)raw - dc_mid;
        int32_t scaled   = centered << 2;

        if (scaled > 32767)  scaled = 32767;
        if (scaled < -32768) scaled = -32768;

        int16_t sample = (int16_t)scaled;
        pcm[total_samples++] = sample;

        next_time += period_us;
        int64_t now = esp_timer_get_time();
        int64_t delay = next_time - now;
        if (delay > 0) {
            esp_rom_delay_us((uint32_t)delay);
        } else {
            next_time = now;
        }

        if ((total_samples % 1000) == 0) {
            vTaskDelay(1);
        }
    }

    write_wav_header_to_mem(wav_buffer, total_samples * sizeof(int16_t));
    actual_wav_size = sizeof(wav_header_t) + total_samples * sizeof(int16_t);

    // ESP_LOGI(TAG, "Recording finished. Samples: %u", total_samples);
}

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
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));


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
    audio_element_set_uri(http_stream_reader, WAV_FILE_URL);

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
            is_audio_playing = true;
        } else if (bytes_read == AEL_IO_TIMEOUT) {
            ESP_LOGW(TAG_DAC, "raw_stream_read timeout");
        } else if (bytes_read == AEL_IO_DONE) {
            ESP_LOGI(TAG_DAC, "RAW stream finished");
            is_audio_playing = false;
            break;
        } else if (bytes_read < 0) {
            ESP_LOGE(TAG_DAC, "raw_stream_read error: %d", bytes_read);
            is_audio_playing = false;
            break;
        }
    }
}


void create_base64() {
    size_t src_len = actual_wav_size;

    mbedtls_base64_encode(
        (unsigned char *)base64_code, MAX_BUFFER_SIZE, &base64_len, 
        (const unsigned char *)wav_buffer, src_len
    );

    if (base64_len < MAX_BUFFER_SIZE) {
        base64_code[base64_len] = '\0';
    } else base64_code[MAX_BUFFER_SIZE - 1] = '\0';
}


void get_wav_response(esp_http_client_handle_t client) {
    create_base64();
    size_t json_size = base64_len + 32;

    char *json = malloc(json_size);
    int actual_size = snprintf(json, json_size, "{\"user_prompt\": \"%s\"}", base64_code);

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, actual_size);

    esp_err_t err = esp_http_client_open(client, actual_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "Cannot open");
        esp_http_client_close(client);
        return;
    }

    int wr = esp_http_client_write(client, json, actual_size);
    if (wr < 0) {
        ESP_LOGE(TAG_MAIN, "Writing failed");
        esp_http_client_close(client);
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGE(TAG_MAIN, "STATUS: %d", status);
    if (status != 200) { ESP_LOGE(TAG_MAIN, "Response was not successful."); }
    else {
        ESP_LOGE(TAG_MAIN, "RESPONSE: SUCCESS");
    }

    free(json);
    esp_http_client_close(client);
}

static inline uint32_t example_angle_to_compare(int angle)
{
    return (angle - SERVO_MIN_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (SERVO_MAX_DEGREE - SERVO_MIN_DEGREE) + SERVO_MIN_PULSEWIDTH_US;
}

void init_servos(servo_handles_t *out_handles)
{
    ESP_LOGI(TAG_SERVO, "Create timer and operator");
    mcpwm_timer_handle_t timer = NULL;
    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SERVO_TIMEBASE_RESOLUTION_HZ,
        .period_ticks = SERVO_TIMEBASE_PERIOD,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

    mcpwm_oper_handle_t oper = NULL;
    mcpwm_operator_config_t operator_config = {
        .group_id = 0,
    };
    ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

    ESP_LOGI(TAG_SERVO, "Connect timer and operator");
    ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

    ESP_LOGI(TAG_SERVO, "Create comparator and generator from the operator");
    mcpwm_comparator_config_t comparator_config = {
        .flags.update_cmp_on_tez = true,
    };

    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &out_handles->cmpr1));
    ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &out_handles->cmpr2));

    mcpwm_gen_handle_t generator1 = NULL;
    mcpwm_generator_config_t generator_config1 = {
        .gen_gpio_num = SERVO_PULSE_GPIO_1,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config1, &generator1));

    mcpwm_gen_handle_t generator2 = NULL;
    mcpwm_generator_config_t generator_config2 = {
        .gen_gpio_num = SERVO_PULSE_GPIO_2,
    };
    ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config2, &generator2));

    ESP_LOGI(TAG_SERVO, "Set generator action on timer and compare event");

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator1,
                                                              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator1,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, out_handles->cmpr1, MCPWM_GEN_ACTION_HIGH)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator2,
                                                                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_LOW)));

    ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator2,
                                                                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, out_handles->cmpr2, MCPWM_GEN_ACTION_HIGH)));

    ESP_LOGI(TAG_SERVO, "Enable and start timer");
    ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
    ESP_ERROR_CHECK(mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP));
}

void move_servo(servo_handles_t *handlers, int angle)
{
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(handlers->cmpr1, example_angle_to_compare(angle)));
    ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(handlers->cmpr2, example_angle_to_compare(-angle)));
}

void app_main(void)
{
    base64_code = malloc(MAX_BUFFER_SIZE);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());

    snprintf(WAV_FILE_URL, sizeof(WAV_FILE_URL), "http://%s/audio/sound.wav", IP_PORT);
    snprintf(ASK_LLM_URL, sizeof(ASK_LLM_URL), "http://%s/ask_llm_mp3", IP_PORT);

    gpio_config_t btn_config = {
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_config);

    esp_http_client_config_t config = {
        .url = ASK_LLM_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000
    };

    wifi_connect();
    adc_init();

    servo_handles_t servos;
    init_servos(&servos);
    int angle = SERVO_MOUTH_CLOSED;
    int direction = -1;
    while (1) {
        if (gpio_get_level(BUTTON_PIN)) {
            record_task();

            esp_http_client_handle_t client = esp_http_client_init(&config);
            get_wav_response(client);

            xTaskCreate(adf_producer_task, "adf_producer", 8 * 1024, NULL, 5, NULL);
            xTaskCreate(dac_consumer_task, "dac_consumer", 4 * 1024, NULL, 5, NULL);
            
            ESP_LOGI(TAG_MAIN, "app_main finished, tasks are running.");
            esp_http_client_cleanup(client);
        }
        if (is_audio_playing) {
            move_servo(&servos, angle);
            vTaskDelay(pdMS_TO_TICKS(50));
            angle += direction;
            if (angle <= SERVO_MOUTH_OPEN || angle >= SERVO_MOUTH_CLOSED) {
                direction *= -1;
            }
        } else if (angle != SERVO_MOUTH_CLOSED) {
            angle = SERVO_MOUTH_CLOSED;
            move_servo(&servos, angle);
        }
    }
}