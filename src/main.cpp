#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

// WiFi credentials
const char* ssid = "LO";        // put your wifi name
const char* password = "12345678"; // put your wifi password

const char* firmwareUrl = "https://github.com/armnuelp/esp32firm/releases/download/esp32firm/firmware.ino.bin";
const char* versionUrl  = "https://raw.githubusercontent.com/armnuelp/esp32firm/refs/heads/main/version.txt";

// Current firmware version
const char* currentFirmwareVersion = "1.0.3";
const unsigned long updateCheckInterval = 5 * 60 * 1000;  // 5 minutes in milliseconds
unsigned long lastUpdateCheck = 0;

// ===============================
// Forward declarations (PROTOTYPE)
// Wajib untuk main.cpp di PlatformIO
// ===============================
void connectToWiFi();
void checkForFirmwareUpdate();
String fetchLatestVersion();
void downloadAndApplyFirmware();
bool startOTAUpdate(WiFiClient* client, int contentLength);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\nStarting ESP32 OTA Update");
  pinMode(2, OUTPUT);

  connectToWiFi();

  Serial.println("Device is ready.");
  Serial.println("Current Firmware Version: " + String(currentFirmwareVersion));

  checkForFirmwareUpdate();

  Serial.println("Current Firmware Version: " + String(currentFirmwareVersion));
}

void loop() {
  digitalWrite(2, HIGH);
  delay(200);
  digitalWrite(2, LOW);
  delay(200);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

void checkForFirmwareUpdate() {
  Serial.println("Checking for firmware update...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return;
  }

  // Step 1: Fetch the latest version from GitHub
  String latestVersion = fetchLatestVersion();
  if (latestVersion == "") {
    Serial.println("Failed to fetch latest version");
    return;
  }

  Serial.println("Current Firmware Version: " + String(currentFirmwareVersion));
  Serial.println("Latest Firmware Version: " + latestVersion);

  // Step 2: Compare versions
  if (latestVersion != currentFirmwareVersion) {
    Serial.println("New firmware available. Starting OTA update...");
    downloadAndApplyFirmware();
  } else {
    Serial.println("Device is up to date.");
  }
}

String fetchLatestVersion() {
  HTTPClient http;
  http.begin(versionUrl);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String latestVersion = http.getString();
    latestVersion.trim();  // Remove any extra whitespace
    http.end();
    return latestVersion;
  } else {
    Serial.printf("Failed to fetch version. HTTP code: %d\n", httpCode);
    http.end();
    return "";
  }
}

void downloadAndApplyFirmware() {
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(firmwareUrl);

  int httpCode = http.GET();
  Serial.printf("HTTP GET code: %d\n", httpCode);

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Firmware size: %d bytes\n", contentLength);

    if (contentLength > 0) {
      WiFiClient* stream = http.getStreamPtr();

      if (startOTAUpdate(stream, contentLength)) {
        Serial.println("OTA update successful, restarting...");
        delay(2000);
        ESP.restart();
      } else {
        Serial.println("OTA update failed");
      }
    } else {
      Serial.println("Invalid firmware size");
    }
  } else {
    Serial.printf("Failed to fetch firmware. HTTP code: %d\n", httpCode);
  }

  http.end();
}

bool startOTAUpdate(WiFiClient* client, int contentLength) {
  Serial.println("Initializing update...");

  if (!Update.begin(contentLength)) {
    Serial.printf("Update begin failed: %s\n", Update.errorString());
    return false;
  }

  Serial.println("Writing firmware...");
  size_t written = 0;
  int progress = 0;
  int lastProgress = 0;

  // Timeout variables
  const unsigned long timeoutDuration = 120UL * 1000UL;  // 120 seconds timeout
  unsigned long lastDataTime = millis();

  while (written < (size_t)contentLength) {
    if (client->available()) {
      uint8_t buffer[128];
      size_t len = client->read(buffer, sizeof(buffer));

      if (len > 0) {
        lastDataTime = millis();  // ✅ update timeout timer saat data masuk

        Update.write(buffer, len);
        written += len;

        // Calculate and print progress
        progress = (int)((written * 100) / (size_t)contentLength);
        if (progress != lastProgress) {
          Serial.printf("Writing Progress: %d%%\n", progress);
          lastProgress = progress;
        }
      }
    }

    // Check for timeout (kalau tidak ada data masuk terlalu lama)
    if (millis() - lastDataTime > timeoutDuration) {
      Serial.println("Timeout: No data received for too long. Aborting update...");
      Update.abort();
      return false;
    }

    yield();
  }

  Serial.println("\nWriting complete");

  if (written != (size_t)contentLength) {
    Serial.printf("Error: Write incomplete. Expected %d but got %u bytes\n",
                  contentLength, (unsigned)written);
    Update.abort();
    return false;
  }

  if (!Update.end()) {
    Serial.printf("Error: Update end failed: %s\n", Update.errorString());
    return false;
  }

  Serial.println("Update successfully completed");
  return true;
}
