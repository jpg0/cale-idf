#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_sleep.h"
// - - - - HTTP Client includes:
#include "esp_netif.h"
#include "esp_err.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include <stdlib.h> // Required for atoi
/**
 * Should match your display model. Check repository WiKi: https://github.com/martinberlin/cale-idf/wiki
 * Needs 3 things: 
 * 1. Include the right class (Check Wiki for supported models)
 * 2. Instantiate io class, below an example for Good Display/Waveshare epapers
 * 3. Instantiate the epaper class itself. After this you can call display.METHOD from any part of your program
 */
// 1 channel SPI epaper displays example:
//#include <gdew075T7.h>
//#include "dke/depg1020bn.h"
//#include <gdew042t2.h>
//#include <gdew027w3.h>
//#include <gdeh0213b73.h>
#include "color/dke/dke075z83.h" // DEPGO0750RW
//#include <color/gdew0583z83.h>

EpdSpi io;
//Depg1020bn display(io);
//Gdew075T7 display(io);
Dke075Z83 display(io);
//Gdew0583z83 display(io);
// Plastic Logic test: Check cale-grayscale.cpp

// Enable on HIGH for Laska ESPink

#define GPIO_DISPLAY_POWER_ON GPIO_NUM_16
// Multi-SPI 4 channels EPD only
// Please note that in order to use this big buffer (160 Kb) on this display external memory should be used
// Otherwise you will run out of DRAM very shortly!
/* #include "wave12i48.h" // Only to use with Edp4Spi IO
Epd4Spi io;
Wave12I48 display(io); */

// BMP debug Mode: Turn false for production since it will make things slower and dump Serial debug
bool bmpDebug = false;

// IP is sent per post for logging purpouses. Authentication: Bearer token in the headers
char espIpAddress[16];
char bearerToken[74] = "";
// As default is 512 without setting buffer_size property in esp_http_client_config_t
#define HTTP_RECEIVE_BUFFER_SIZE 1024

extern "C"
{
    void app_main();
}

static const char *TAG = "CALE";

uint16_t countDataEventCalls = 0;
uint32_t countDataBytes = 0;

struct BmpHeader
{
    uint32_t fileSize;
    uint32_t imageOffset;
    uint32_t headerSize;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t depth;
    uint32_t format;
} bmp;

uint16_t read16(uint8_t output_buffer[512], uint8_t startPointer)
{
    // BMP data is stored little-endian
    uint16_t result;
    ((uint8_t *)&result)[0] = output_buffer[startPointer];     // LSB
    ((uint8_t *)&result)[1] = output_buffer[startPointer + 1]; // MSB
    return result;
}

uint32_t read32(uint8_t output_buffer[512], uint8_t startPointer)
{
    //Debug - Leave disabled to avoid Serial output
    //printf("read32: %x %x %x %x\n", output_buffer[startPointer],output_buffer[startPointer+1],output_buffer[startPointer+2],output_buffer[startPointer+3]);
    uint32_t result;
    ((uint8_t *)&result)[0] = output_buffer[startPointer]; // LSB
    ((uint8_t *)&result)[1] = output_buffer[startPointer + 1];
    ((uint8_t *)&result)[2] = output_buffer[startPointer + 2];
    ((uint8_t *)&result)[3] = output_buffer[startPointer + 3]; // MSB
    return result;
}

bool with_color = true; // Candidate for Kconfig
uint32_t rowSize;
uint32_t rowByteCounter;
uint16_t w;
uint16_t h;
uint8_t bitmask = 0xFF;
uint8_t bitshift;
uint16_t red, green, blue;
bool whitish, colored;
uint16_t drawX = 0;
uint16_t drawY = 0;
uint16_t bPointer = 34; // Byte pointer - Attention drawPixel has uint16_t
uint16_t imageBytesRead = 0;
uint32_t dataLenTotal = 0;
uint32_t in_bytes = 0;
uint8_t in_byte = 0; // for depth <= 8
uint8_t in_bits = 0; // for depth <= 8
bool isReadingImage = false;
bool isSupportedBitmap = true;
bool isPaddingAware = false;
uint16_t forCount = 0;

static const uint16_t input_buffer_pixels = 640;      // may affect performance
static const uint16_t max_palette_pixels = 256;       // for depth <= 8
uint8_t mono_palette_buffer[max_palette_pixels / 8];  // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 c/w
uint16_t totalDrawPixels = 0;
int color = EPD_WHITE;
uint64_t startTime = 0;
static int http_max_age_seconds = -1; // Global static for HTTP Cache-Control max-age


void deepsleep(int max_age_sec){
    if (max_age_sec >= 0) {
        uint64_t sleep_duration_us = (uint64_t)max_age_sec * 1000000LL;
        ESP_LOGI(TAG, "Using max-age for deep sleep: %d seconds", max_age_sec);
        esp_deep_sleep(sleep_duration_us);
    } else {
        ESP_LOGI(TAG, "Using default deep sleep duration: %d minutes", CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
        esp_deep_sleep(1000000LL * 60 * CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
    }
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    uint8_t output_buffer[HTTP_RECEIVE_BUFFER_SIZE]; // Buffer to store HTTP response

    switch (evt->event_id)
    {
    default:
        break;
    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        if (strcmp(evt->header_key, "Cache-Control") == 0) {
            const char *max_age_str = strstr(evt->header_value, "max-age=");
            if (max_age_str != NULL) {
                max_age_str += strlen("max-age="); // Move pointer past "max-age="
                int parsed_max_age = atoi(max_age_str);
                if (parsed_max_age > 0) { // atoi returns 0 on error, ensure it's a valid positive number
                    http_max_age_seconds = parsed_max_age;
                    ESP_LOGI(TAG, "max-age found: %d", http_max_age_seconds);
                } else {
                    // Check if the value was actually "0" or if atoi failed
                    bool is_zero = true;
                    const char* p = max_age_str;
                    while(*p) {
                        if (isdigit(*p)) {
                            if (*p != '0') {
                                is_zero = false;
                                break;
                            }
                        } else if (!isspace(*p)) { // allow whitespace around the number
                            is_zero = false; // Non-digit, non-whitespace means error or other directives
                            break;
                        }
                        p++;
                    }
                    if (is_zero && strchr(max_age_str, '0') != NULL) { // It was "0"
                         http_max_age_seconds = 0;
                         ESP_LOGI(TAG, "max-age found: %d", http_max_age_seconds);
                    } else {
                         ESP_LOGW(TAG, "Malformed Cache-Control header: %s. Could not parse max-age value or value is not a positive integer.", evt->header_value);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Malformed Cache-Control header: %s. 'max-age=' not found.", evt->header_value);
            }
        }
        break;
    case HTTP_EVENT_ON_DATA:
        ++countDataEventCalls;
        if (countDataEventCalls%10==0) {
        ESP_LOGI(TAG, "%d len:%d\n", (int)countDataEventCalls, (int)evt->data_len); }
        dataLenTotal += evt->data_len;
        // Unless bmp.imageOffset initial skip we start reading stream always on byte pointer 0:
        bPointer = 0;
        // Copy the response into the buffer
        memcpy(output_buffer, evt->data, evt->data_len);

        if (countDataEventCalls == 1)
        {
            startTime = esp_timer_get_time();
            // Read BMP header -In total 34 bytes header
            bmp.fileSize = read32(output_buffer, 2);
            bmp.imageOffset = read32(output_buffer, 10);
            bmp.headerSize = read32(output_buffer, 14);
            bmp.width = read32(output_buffer, 18);
            bmp.height = read32(output_buffer, 22);
            bmp.planes = read16(output_buffer, 26);
            bmp.depth = read16(output_buffer, 28);
            bmp.format = read32(output_buffer, 30);

            drawY = bmp.height;
            ESP_LOGI(TAG, "BMP HEADERS\nfilesize:%d\noffset:%d\nW:%d\nH:%d\nplanes:%d\ndepth:%d\nformat:%d\n",
                     (int)bmp.fileSize, (int)bmp.imageOffset, (int)bmp.width, (int)bmp.height, (int)bmp.planes, (int)bmp.depth, (int)bmp.format);

            if (bmp.depth == 1)
            {
                isPaddingAware = true;
                ESP_LOGI(TAG, "BMP isPaddingAware:  1 bit depth are 4 bit padded. Wikipedia gave me a lesson.");
            }
            if (((bmp.planes == 1) && ((bmp.format == 0) || (bmp.format == 3))) == false)
            { // uncompressed is handled
                isSupportedBitmap = false;
                ESP_LOGE(TAG, "BMP NOT SUPPORTED: Compressed formats not handled.\nBMP NOT SUPPORTED: Only planes==1, format 0 or 3\n");
            }
            if (bmp.depth > 8)
            {
                isSupportedBitmap = false;
                ESP_LOGE(TAG, "BMP DEPTH %d: Only 1, 4, and 8 bits depth are supported.\n", (int)bmp.depth);
            }

            rowSize = ((bmp.width * bmp.depth + 8 - bmp.depth) / 8 + 3) & ~3;

            w = bmp.width;
            h = bmp.height;
            if ((w - 1) >= display.width())
                w = display.width();
            if ((h - 1) >= display.height())
                h = display.height();

            bitshift = 8 - bmp.depth;

            if (bmp.depth == 1)
                with_color = false;

            if (bmp.depth <= 8)
            {
                if (bmp.depth < 8)
                    bitmask >>= bmp.depth;
                // Color-palette location:
                bPointer = bmp.imageOffset - (4 << bmp.depth);
                if (bmpDebug)
                    printf("Palette location: %d\n\n", bPointer);

                for (uint16_t pn = 0; pn < (1 << bmp.depth); pn++)
                {
                    blue = output_buffer[bPointer++];
                    green = output_buffer[bPointer++];
                    red = output_buffer[bPointer++];
                    bPointer++;

                    whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                    colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));                                                  // reddish or yellowish?
                    if (0 == pn % 8) {
                        mono_palette_buffer[pn / 8] = 0;
                        color_palette_buffer[pn / 8] = 0;
                    }
                        
                    mono_palette_buffer[pn / 8]  |= whitish << pn % 8;                       
                    color_palette_buffer[pn / 8] |= colored << pn % 8;

                    // DEBUG Colors - TODO: Double check Palette!!
                    if (bmpDebug)
                        printf("0x00%x%x%x : %x, %x\n", red, green, blue, whitish, colored);
                }
            }
            imageBytesRead += evt->data_len;
        }
        if (!isSupportedBitmap)
            return ESP_FAIL;

        if (bmpDebug)
        {
            printf("\n--> bPointer %d\n_inX: %d _inY: %d DATALEN TOTAL:%d bytesRead so far:%d\n",
                   (int)bPointer, (int)drawX, (int)drawY, (int)dataLenTotal, (int)imageBytesRead);
        }

        // Didn't arrived to imageOffset YET, it will in next calls of HTTP_EVENT_ON_DATA:
        if (dataLenTotal < bmp.imageOffset)
        {
            imageBytesRead = dataLenTotal;
            if (bmpDebug)
                printf("IF read<offset UPDATE bytesRead:%d\n", imageBytesRead);
            return ESP_OK;
        }
        else
        {
            // Only move pointer once to set right offset
            if (countDataEventCalls == 1 && bmp.imageOffset < evt->data_len)
            {
                bPointer = bmp.imageOffset;
                isReadingImage = true;
                printf("Offset comes in first DATA callback. bPointer: %d == bmp.imageOffset\n", bPointer);
            }
            if (!isReadingImage)
            {
                bPointer = bmp.imageOffset - imageBytesRead;
                imageBytesRead += bPointer;
                isReadingImage = true;
                printf("Start reading image. bPointer: %d\n", bPointer);
            }
        }
        forCount = 0;
        // LOOP all the received Buffer but start on ImageOffset if first call
        for (uint32_t byteIndex = bPointer; byteIndex < evt->data_len; ++byteIndex)
        {
            in_byte = output_buffer[byteIndex];
            // Dump only the first calls
            if (countDataEventCalls < 2 && bmpDebug)
            {
                printf("L%d: BrsF:%d %x\n", (int)byteIndex, (int)imageBytesRead, (int)in_byte);
            }
            in_bits = 8;

            switch (bmp.depth)
            {
            case 1:
            case 4:
            case 8:
            {
                while (in_bits != 0)
                {

                    uint16_t pn = (in_byte >> bitshift) & bitmask;
                    whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                    colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                    in_byte <<= bmp.depth;
                    in_bits -= bmp.depth;

                    if (whitish)
                    {
                        color = EPD_WHITE;
                    }
                    
                    #ifdef IS_COLOR_EPD
                    else if (colored && with_color)
                    {
                        color = EPD_RED;
                    }
                    #endif

                    else
                    {
                        color = EPD_BLACK;
                    }

                    // bmp.width reached? Then go one line up (Is readed from bottom to top)
                    if (isPaddingAware)
                    { // 1 bit images are 4-bit padded (Filled usually with 0's)
                        if (drawX + 1 > rowSize * 8)
                        {
                            drawX = 0;
                            rowByteCounter = 0;
                            --drawY;
                        }
                    }
                    else
                    {
                        // if (drawX + 1 > bmp.width) -> Updated but not tested with 12.48 inch epaper
                        if (drawX + 1 > rowSize *2)
                        {
                            drawX = 0;
                            rowByteCounter = 0;
                            --drawY;
                        }
                    }
                    // The ultimate mission: Send the X / Y pixel to the GFX Buffer
                    display.drawPixel(drawX, drawY, color);
                    if (drawY == 0) break;

                    totalDrawPixels++;
                    ++drawX;
                }
            }
            break;
            }

            rowByteCounter++;
            imageBytesRead++;
            forCount++;
        }

        if (bmpDebug)
            printf("Total drawPixel calls: %d\noutX: %d outY: %d\n", totalDrawPixels, drawX, drawY);

        // Hexa dump:
        //ESP_LOG_BUFFER_HEX(TAG, output_buffer, evt->data_len);
        break;

    case HTTP_EVENT_ON_FINISH:
        countDataEventCalls=0;
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH\nDownload took: %llu ms\nRefresh and go to sleep %d minutes\n", (esp_timer_get_time()-startTime)/1000, CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
        display.update();
        if (bmpDebug) 
            printf("Free heap after display render: %d\n", (int)xPortGetFreeHeapSize());
        // Go to deepsleep after rendering
        vTaskDelay(14000 / portTICK_PERIOD_MS);
        deepsleep(http_max_age_seconds);
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        break;
    }
    return ESP_OK;
}

static void http_post(void)
{
    /**
     * NOTE: All the configuration parameters for http_client must be spefied either in URL or as host and path parameters.
     * If host and path parameters are not set, query parameter will be ignored. In such cases,
     * query parameter should be specified in URL.
     *
     * If URL as well as host and path parameters are specified, values of host and path will be considered. TESTs:
       http://img.cale.es/bmp/fasani/5e8cc4cf03d81  -> 4 bit 2.7 tests
       http://cale.es/img/test/1.bmp                -> vertical line
       http://cale.es/img/test/circle.bmp           -> Circle test
       timeout_ms set to 9 seconds since for large displays a dynamic BMP can take some seconds to be generated
     */
    
    http_max_age_seconds = -1; // Reset before each new HTTP request cycle

    // POST Send the IP for logging purpouses
    char post_data[22];
    uint8_t postsize = sizeof(post_data);
    strlcpy(post_data, "ip=", postsize);
    strlcat(post_data, espIpAddress, postsize);

    esp_http_client_config_t config = {
        .url = CONFIG_CALE_SCREEN_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 9000,
        .event_handler = _http_event_handler,
        .buffer_size = HTTP_RECEIVE_BUFFER_SIZE
        };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Authentication: Bearer    
    strlcpy(bearerToken, "Bearer: ", sizeof(bearerToken));
    strlcat(bearerToken, CONFIG_CALE_BEARER_TOKEN, sizeof(bearerToken));
    
    printf("POST data: %s\n%s\n", post_data, bearerToken);

    esp_http_client_set_header(client, "Authorization", bearerToken);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "\nIMAGE URL: %s\n\nHTTP GET Status = %d, content_length = %d\n",
                 CONFIG_CALE_SCREEN_URL,
                 (int) esp_http_client_get_status_code(client),
                 (int) esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "\nHTTP GET request failed: %s", esp_err_to_name(err));
    }
    //ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
}

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG, "Connect to the AP failed %d times. Going to deepsleep %d minutes", (int)CONFIG_ESP_MAXIMUM_RETRY, (int)CONFIG_DEEPSLEEP_MINUTES_AFTER_RENDER);
            // Wi-Fi failure means HTTP request likely didn't happen or finish, pass -1 for default sleep.
            deepsleep(-1);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        sprintf(espIpAddress,  IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "got ip: %s\n", espIpAddress);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    sprintf(reinterpret_cast<char *>(wifi_config.sta.ssid), CONFIG_ESP_WIFI_SSID);
    sprintf(reinterpret_cast<char *>(wifi_config.sta.password), CONFIG_ESP_WIFI_PASSWORD);
    wifi_config.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void app_main(void)
{
    printf("CalEPD version: %s\n", CALEPD_VERSION);
    
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // WiFi log level
    esp_log_level_set("wifi", ESP_LOG_ERROR);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    
    gpio_set_direction(GPIO_DISPLAY_POWER_ON, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DISPLAY_POWER_ON, 1);
    //  On  init(true) activates debug (And makes SPI communication slower too)
    display.init();
    display.setRotation(CONFIG_DISPLAY_ROTATION);
    display.update();
    
    // Show available Dynamic Random Access Memory available after display.init() - Both report same number
    printf("Free heap: %d (After epaper instantiation)\n", (int) xPortGetFreeHeapSize());

    http_post();
}
