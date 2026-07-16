#include "Arduino.h"
#include "ArduinoJson.h"
#include "Freertos.h"
#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include "base64.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#define PHOTO_TO_CAPTURE 5

constexpr const char* wifiSSID = "LognSteam";
constexpr const char* wifiPassword = "roboticsisfun!1";

constexpr int wifiMaxRetries = 20; // NOTE : Set to 0 for infinite retries

constexpr const char* postURL = "https://script.google.com/macros/s/AKfycbxoq4EkIb7f6GZVewrn-mSUFbtDmdHMZDknfIcomaxGKW3J3ULM_0GvvbNN824VHbABJA/exec";

WiFiClientSecure client;
HTTPClient http;

constexpr const int STMPin = 2;

#define _DEBUG_
#ifdef _DEBUG_
    #define uartBegin(x) Serial.begin(x); \
        while(!Serial) {delay(100);}
    #define printDebug(x) Serial.print(x)
#else
    #define uartBegin(x)
    #define printDebug(x) 
#endif

#include "camera_pins.h"

esp_err_t cameraInit();
esp_err_t sdInit();
esp_err_t wifiInit();
esp_err_t stm32DigitalSignalInit();
void setup();
void loop();
void photo_save(const char* fileName);
void writeFile(fs::FS &fs, const char * path, uint8_t * data, size_t len);
void cameraCapture();
void cameraCaptureTask(void *pvParameters);
String convertPhotoToBase64(int photoNumber);
void wifiPostImage();
void wifiPostImageTask(void* pvParameters);

esp_err_t cameraInit()
{
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size = FRAMESIZE_UXGA;
    config.pixel_format = PIXFORMAT_JPEG; // for streaming
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 12;
    config.fb_count = 1;

    // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
    //                      for larger pre-allocated frame buffer.
    if(config.pixel_format == PIXFORMAT_JPEG)
    {
        if(psramFound())
        {
            config.jpeg_quality = 10;
            config.fb_count = 2;
            config.grab_mode = CAMERA_GRAB_LATEST;
        } 
        else
        {
            // Limit the frame size when PSRAM is not available
            config.frame_size = FRAMESIZE_SVGA;
            config.fb_location = CAMERA_FB_IN_DRAM;
        }
    }
    else
    {
        // Best option for face detection/recognition
        config.frame_size = FRAMESIZE_240X240;
        #if CONFIG_IDF_TARGET_ESP32S3
        config.fb_count = 2;
        #endif
    }

    // camera init
    return esp_camera_init(&config);
}

esp_err_t sdInit()
{
    // Initialize SD card
    if(!SD.begin(21))
    {
        printDebug("Card Mount Failed");
        return ESP_FAIL;
    }
    uint8_t cardType = SD.cardType();

    // Determine if the type of SD card is available
    if(cardType == CARD_NONE)
    {
        printDebug("No SD card attached");
        return ESP_FAIL;
    }

    printDebug("SD Card Type: ");
    if(cardType == CARD_MMC)
    {
        printDebug("MMC");
    } 
    else if(cardType == CARD_SD)
    {
        printDebug("SDSC");
    } 
    else if(cardType == CARD_SDHC)
    {
        printDebug("SDHC");
    } 
    else 
    {
        printDebug("UNKNOWN");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifiInit()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPassword);
    printDebug("Connecting to WiFi");
    int retryCount = 0;
    while(WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        printDebug("Connecting : ");
        printDebug(retryCount++);
        #if wifiMaxRetries > 0
        if(retryCount > wifiMaxRetries)
        {
            printDebug("Failed to connect to WiFi");
            return ESP_FAIL;
        }
        #endif
    }
    printDebug("Connected to WiFi");

    client.setInsecure(); // Disable SSL certificate verification
    return ESP_OK;
}

esp_err_t stm32DigitalSignalInit()
{
    pinMode(STMPin, OUTPUT);
    return ESP_OK;
}

// Save pictures to SD card
void photo_save(const char* fileName) 
{
    camera_fb_t *fb = esp_camera_fb_get();
    while(!fb)
    {
        printDebug("Failed to get camera frame buffer");
        delay(100);
        fb = esp_camera_fb_get();
    }  

    // Save photo to file
    writeFile(SD, fileName, fb->buf, fb->len);
    // Release image buffer
    esp_camera_fb_return(fb);
    printDebug("Photo saved to file");
}

// SD card write file
void writeFile(fs::FS &fs, const char *path, uint8_t *data, size_t len)
{
    printDebug("Writing file: ");
    printDebug(path);
    printDebug("\n");

    File file = fs.open(path, FILE_WRITE);
    if(!file)
    {
        printDebug("Failed to open file for writing");
        return;
    }
    if(file.write(data, len) == len)
    {
        printDebug("File written");
    } 
    else 
    {
        printDebug("Write failed");
    }
    file.close();
}

String convertPhotoToBase64(int photoNumber)
{
    String filePath = "/Photo" + String(photoNumber) + ".jpg";
    File file = SD.open(filePath);
    if(!file)
    {
        printDebug("Failed to open file for reading");
        return String();
    }
    size_t fileSize = file.size();
    uint8_t* buffer = new uint8_t[fileSize];
    file.read(buffer, fileSize);
    file.close();

    String base64String = base64::encode(buffer, fileSize);
    delete[] buffer;
    return base64String;
}

void setup() 
{
    uartBegin(115200);
    printDebug("SETUP METHOD");

    if(cameraInit() != ESP_OK)
    {
        printDebug("Failed to initialize camera");
        return;
    }
    if(sdInit() != ESP_OK)
    {
        printDebug("Failed to initialize SD card");
        return;
    }
    if(stm32DigitalSignalInit() != ESP_OK)
    {
        printDebug("Failed to initialize STM32 digital signal");
        return;
    }
    cameraCapture(); // Capture photos and save to SD card
    // xTaskCreate(cameraCaptureTask, "cameraCaptureTask", 4096, NULL, 1, NULL);

    if(wifiInit() != ESP_OK)
    {
        printDebug("Failed to initialize WiFi");
        return;
    }
    wifiPostImage(); // Post images to server
    // xTaskCreate(wifiPostImageTask, "wifiPostImageTask", 4096, NULL, 1, NULL);
}

void cameraCaptureTask(void *pvParameters)
{
    cameraCapture();
    vTaskDelete(NULL); // Delete the task after completion
}
void cameraCapture()
{
    printDebug("Camera Capture Task Started");
    // Capture photos and save to SD card
    for(int i = 0 ; i <= PHOTO_TO_CAPTURE ; i++)
    {
        // Get the current time
        unsigned long now = millis();
        int photoCount = 0;
        char fileName[32] = "/Photo0.jpg";
        while(SD.exists(fileName))
        {
            photoCount++;
            sprintf(fileName,"/Photo%d.jpg",photoCount);
        }
        photo_save(fileName);
        delay(10);
    }
    // vTaskDelete(NULL); // NOTE : Do not uncomment this. We don't use this function as RTOS Task 
}

void wifiPostImageTask(void *pvParameters)
{
    wifiPostImage();
    vTaskDelete(NULL); // Delete the task after completion
}
void wifiPostImage()
{
    printDebug("WiFi Post Image Task Started");

    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    http.begin(client, postURL);
    http.addHeader("Content-Type", "application/json");
    
    for(int photoCount = 0 ; photoCount <= PHOTO_TO_CAPTURE ; photoCount++)
    {
        String base64Image = convertPhotoToBase64(photoCount);
        if(base64Image == "")
        {
            printDebug("No image to post for photoCount: ");
            printDebug(photoCount);
            continue;
        }

        String jsonPayload = "{\"image\":\"" + base64Image + "\"}";

        int httpResponseCode = http.POST(jsonPayload);
        if(httpResponseCode > 0)
        {
            printDebug("HTTP Response code: ");
            printDebug(httpResponseCode);
        }
        else
        {
            printDebug("Error on sending POST: ");
            printDebug(httpResponseCode);
        }


    }
    printDebug("All images posted to server");
    client.stop();
    http.end();

    SD.end();
    digitalWrite(STMPin, HIGH);
    esp_deep_sleep_start();
}

void loop() 
{
    vTaskDelete(NULL); // Delete the loop task to prevent it from running indefinitely
}