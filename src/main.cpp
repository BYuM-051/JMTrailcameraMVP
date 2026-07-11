#include "Arduino.h"
#include "Freertos.h"
#include "esp_camera.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#define PHOTO_TO_CAPTURE 5

#define _DEBUG_
#ifdef _DEBUG_
    #define printDebug(x) Serial.print(x)
#else
    #define printDebug(x) 
#endif

#include "camera_pins.h"

esp_err_t cameraInit();
void sdInit();
void photo_save(const char* fileName);
void writeFile(fs::FS &fs, const char * path, uint8_t * data, size_t len);
void wifiPostImageTask();

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

void sdInit()
{
    // Initialize SD card
    if(!SD.begin(21))
    {
        printDebug("Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();

    // Determine if the type of SD card is available
    if(cardType == CARD_NONE)
    {
        printDebug("No SD card attached");
        return;
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
    }

}

// Save pictures to SD card
void photo_save(const char* fileName) 
{
    // Take a photo
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) 
    {
        printDebug("Failed to get camera frame buffer");
        return;
    }

    // Save photo to file
    writeFile(SD, fileName, fb->buf, fb->len);
    // Release image buffer
    esp_camera_fb_return(fb);
    printDebug("Photo saved to file");
}

// SD card write file
void writeFile(fs::FS &fs, const char * path, uint8_t * data, size_t len)
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

void setup() 
{
    #ifdef _DEBUG_
    Serial.begin(115200);
    while(!Serial)
        {delay(100);} // When the serial monitor is turned on, the program starts to execute
    #endif

    printDebug("SETUP METHOD");

    if(cameraInit() != ESP_OK)
    {
        printDebug("Failed to initialize camera");
        return;
    }

    sdInit();

    printDebug("Photos will begin in one minute, please be ready.");

    delay(2000);

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

    xTaskCreate(wifiPostImageTask, "wifiPostImageTask", 4096, NULL, 1, NULL);
}

void wifiPostImageTask()
{
    
    SD.end();
    // TODO : send digital signal to STM32 to indicate that the photos have been taken and saved to SD card
    esp_deep_sleep_start();
}

void loop() 
{
    delay(360000);
}