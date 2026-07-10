#include <WiFi.h>
#include <HTTPClient.h>
#include <SD_MMC.h>
#include <esp_camera.h>

// WiFi credentials
const char* kWiFiSsid = "LognSteam";
const char* kWiFiPassword = "roboticsisfun!";

// REST endpoint for posting JSON payloads
const char* kPostUrl = "https://script.google.com/macros/s/AKfycbxoq4EkIb7f6GZVewrn-mSUFbtDmdHMZDknfIcomaxGKW3J3ULM_0GvvbNN824VHbABJA/exec";

// Save directory on SD card
const char* kPhotoDirectory = "/photos";

// Camera pin definitions for ESP32-S3 boards; adjust for your hardware
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      8
#define SIOD_GPIO_NUM     11
#define SIOC_GPIO_NUM     12
#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       18
#define Y7_GPIO_NUM       17
#define Y6_GPIO_NUM       16
#define Y5_GPIO_NUM       15
#define Y4_GPIO_NUM       14
#define Y3_GPIO_NUM       13
#define Y2_GPIO_NUM       20
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     21
#define PCLK_GPIO_NUM     23

bool hasCapturedPhoto = false;

String base64Encode(const uint8_t* data, size_t length) {
  static const char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String encoded;
  encoded.reserve(((length + 2) / 3) * 4);

  for (size_t i = 0; i < length; i += 3) {
    uint32_t value = 0;
    int bytes = 0;

    for (int j = 0; j < 3; ++j) {
      value <<= 8;
      if (i + j < length) {
        value |= data[i + j];
        ++bytes;
      }
    }

    encoded += kBase64Alphabet[(value >> 18) & 0x3F];
    encoded += kBase64Alphabet[(value >> 12) & 0x3F];
    encoded += (bytes > 1) ? kBase64Alphabet[(value >> 6) & 0x3F] : '=';
    encoded += (bytes > 2) ? kBase64Alphabet[value & 0x3F] : '=';
  }

  return encoded;
}

bool initWiFi() {
  Serial.printf("Connecting to WiFi '%s'...\n", kWiFiSsid);
  WiFi.begin(kWiFiSsid, kWiFiPassword);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("Failed to connect to WiFi");
  return false;
}

bool initSDCard() {
  Serial.println("Initializing SD card...");
  if (!SD_MMC.begin()) {
    Serial.println("SD_MMC.begin() failed");
    return false;
  }

  if (!SD_MMC.exists(kPhotoDirectory)) {
    Serial.printf("Creating directory %s\n", kPhotoDirectory);
    if (!SD_MMC.mkdir(kPhotoDirectory)) {
      Serial.println("Failed to create photo directory");
      return false;
    }
  }

  uint64_t cardSize = SD_MMC.cardSize() >> 20;
  Serial.printf("SD card mounted, size: %llu MB\n", cardSize);
  return true;
}

bool initCamera() {
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_SVGA);
  }

  Serial.println("Camera initialized successfully");
  return true;
}

String makePhotoPath() {
  unsigned long timestamp = millis();
  return String(kPhotoDirectory) + "/photo_" + String(timestamp) + ".jpg";
}

bool captureAndSavePhoto() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }

  String path = makePhotoPath();
  File file = SD_MMC.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open file for writing: %s\n", path.c_str());
    esp_camera_fb_return(fb);
    return false;
  }

  size_t written = file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);

  if (written != fb->len) {
    Serial.printf("Failed to write complete file. expected=%u written=%u\n", fb->len, written);
    SD_MMC.remove(path.c_str());
    return false;
  }

  Serial.printf("Photo saved to SD: %s (%u bytes)\n", path.c_str(), fb->len);
  return true;
}

String findNewPhotoFile() {
  File dir = SD_MMC.open(kPhotoDirectory);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return "";
  }

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = String(entry.name());
      if (name.endsWith(".jpg") || name.endsWith(".jpeg")) {
        entry.close();
        dir.close();
        if (name.startsWith("/")) {
          return name;
        }
        return String(kPhotoDirectory) + "/" + name;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }

  dir.close();
  return "";
}

bool postFileToServer(const String& filePath) {
  File file = SD_MMC.open(filePath.c_str(), FILE_READ);
  if (!file) {
    Serial.printf("Failed to open file for POST: %s\n", filePath.c_str());
    return false;
  }

  size_t fileSize = file.size();
  if (fileSize == 0) {
    Serial.println("File is empty, skipping POST");
    file.close();
    return false;
  }

  uint8_t* buffer = new uint8_t[fileSize];
  size_t readBytes = file.read(buffer, fileSize);
  file.close();

  if (readBytes != fileSize) {
    Serial.println("Failed to read image data from file");
    delete[] buffer;
    return false;
  }

  String payload = "{\"filename\":\"" + filePath + "\",\"image\":\"";
  payload += base64Encode(buffer, fileSize);
  payload += "\"}";
  delete[] buffer;

  HTTPClient http;
  http.begin(kPostUrl);
  http.addHeader("Content-Type", "application/json");

  Serial.printf("Posting %u bytes to %s\n", payload.length(), kPostUrl);
  int httpCode = http.POST(payload);
  String response;
  if (httpCode > 0) {
    response = http.getString();
    Serial.printf("HTTP %d response: %s\n", httpCode, response.c_str());
  } else {
    Serial.printf("HTTP POST failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();

  if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED || httpCode == HTTP_CODE_ACCEPTED) {
    Serial.println("POST successful, deleting local file");
    return SD_MMC.remove(filePath.c_str());
  }

  Serial.println("POST failed, keeping file on SD for retry");
  return false;
}

void setup() {
  Serial.begin(115200);
  while(!Serial)
    {delay(10);}

  if (!initCamera()) {
    Serial.println("Camera initialization failed. Halting.");
    while (true) {
      delay(1000);
    }
  }

  if (!initSDCard()) {
    Serial.println("SD card initialization failed. Halting.");
    while (true) {
      delay(1000);
    }
  }

  if (!initWiFi()) {
    Serial.println("WiFi not connected. Will continue running and retry on loop.");
  }

  if (captureAndSavePhoto()) {
    hasCapturedPhoto = true;
  }
}

void loop() {
  if (!hasCapturedPhoto) {
    if (captureAndSavePhoto()) {
      hasCapturedPhoto = true;
    } else {
      delay(5000);
      return;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    initWiFi();
  }

  if (WiFi.status() == WL_CONNECTED) {
    String photoFile = findNewPhotoFile();
    if (photoFile.length() > 0) {
      Serial.printf("Found new photo file: %s\n", photoFile.c_str());
      if (postFileToServer(photoFile)) {
        Serial.printf("Deleted %s after successful upload\n", photoFile.c_str());
      }
    }
  }

  delay(10000);
}
