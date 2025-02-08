#include <TFT_eSPI.h>  // TFT display library
#include <SPIFFS.h>    // ESP32 Flash Storage
#include <PNGdec.h>    // PNG decoder library
#include <FS.h>        // File system
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

TFT_eSPI tft = TFT_eSPI();  
PNG png;  // PNG decoder object

#define TFT_BL    22  // Backlight

const int totalFrames = 64; // Updated to match noon=0, midnight=31
const int smallWidth = 16;
const int smallHeight = 16;
const int upscaleFactor = 15; // 16x15 = 240 pixels

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -8 * 3600, 60000); // Adjust for PST (-8 hours)
unsigned long lastSerialPrint = 0;

void pngDraw(PNGDRAW *pDraw) {
    static uint16_t lastRow[smallWidth] = {0}; // Store previous row for edge detection
    uint16_t lineBuffer[smallWidth]; // Buffer for one line
    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_LITTLE_ENDIAN, 0xFFFF);

    for (int x = 0; x < smallWidth; x++) {
        uint16_t color = lineBuffer[x];

        // Check if the pixel is in the outer 3-pixel boundary
        bool isExpandedEdge = (x < 3 || x >= smallWidth - 3 || pDraw->y < 3 || pDraw->y >= smallHeight - 3);

        // If the color is transparent and within the 3-pixel boundary, convert to black
        if (isExpandedEdge && color == 0xFFE0) { // Assuming 0xFFE0 represents transparency
            color = TFT_BLACK;
        }

        lastRow[x] = color;

        // Draw the upscaled pixel
        tft.fillRect(x * upscaleFactor, pDraw->y * upscaleFactor, upscaleFactor, upscaleFactor, color);
    }
}

bool loadPNGfromSPIFFS(const char *filename) {
    Serial.print("Loading PNG: "); Serial.println(filename);
    int rc = png.open(filename, [](const char *fn, int32_t *size) -> void* {
        fs::File *file = new fs::File(SPIFFS.open(fn));
        if (!file || !file->available()) {
            delete file;
            return nullptr;
        }
        *size = file->size();
        return file;
    }, [](void *handle) {
        fs::File *file = static_cast<fs::File *>(handle);
        if (file) {
            file->close();
            delete file;
        }
    }, [](PNGFILE *handle, uint8_t *buffer, int32_t length) -> int32_t {
        fs::File *file = static_cast<fs::File *>(handle->fHandle);
        return file->read(buffer, length);
    }, [](PNGFILE *handle, int32_t position) -> int32_t {
        fs::File *file = static_cast<fs::File *>(handle->fHandle);
        return file->seek(position);
    }, pngDraw);

    if (rc != PNG_SUCCESS) {
        Serial.println("❌ PNG Decode Failed!");
        return false;
    }
    png.decode(NULL, 0);
    png.close();
    return true;
}

void splashScreen() {
    for (int i = 0; i < totalFrames; i++) {
        char spiffsFile[32];
        snprintf(spiffsFile, sizeof(spiffsFile), "/frames/frame_%03d.png", i);
        if (!loadPNGfromSPIFFS(spiffsFile)) {
            Serial.println("❌ Error loading frame.");
        }
        delay(100);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    WiFi.begin("SSID", "PASSKEY");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected");
    timeClient.begin();
    
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS Mount Failed!");
        return;
    }
    Serial.println("✅ SPIFFS Initialized Successfully!");
    
    tft.fillScreen(TFT_BLACK); // Clear screen before splash
    splashScreen(); // Show all frames at startup
    tft.fillScreen(TFT_BLACK); // Clear screen after splash
}

void loop() {
    timeClient.update();
    int currentHour = timeClient.getHours();
    int currentMinute = timeClient.getMinutes();
    int currentSecond = timeClient.getSeconds();
    
    // Correct frame mapping to match 12 PM = 0 and 12 AM = 31
    int totalMinutes = (currentHour * 60) + currentMinute;
    int frameIndex = ((totalMinutes + 720) % 1440) * 64 / 1440;
    
    int secondsUntilNextFrame = 60 - currentSecond;
    
    if (millis() - lastSerialPrint >= 15000) {
        Serial.printf("Current Time: %02d:%02d:%02d, Frame: %d, Next Frame in: %d sec\n", currentHour, currentMinute, currentSecond, frameIndex, secondsUntilNextFrame);
        lastSerialPrint = millis();
    }
    
    char spiffsFile[32];
    snprintf(spiffsFile, sizeof(spiffsFile), "/frames/frame_%03d.png", frameIndex);
    tft.fillScreen(TFT_BLACK);
    if (!loadPNGfromSPIFFS(spiffsFile)) {
        Serial.println("❌ Error loading frame.");
    }
    delay(60000); // Update every minute
}
