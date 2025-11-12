#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040)
void setup() {}
void loop() {}

#else
#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include "AudioFileSourceICYStream.h"
// #include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
// #include "AudioGeneratorAAC.h"
#include "AudioOutputI2S.h"

// To run, set your ESP8266 build to 160MHz, update the SSID info, and upload.

// Enter your WiFi setup here:
#ifndef STASSID
#define STASSID "UCU_Guest"
#define STAPSK  "your-password"
#endif

#define SD_PIN 5

const char* ssid = STASSID;
const char* password = STAPSK;

// Randomly picked URL
const char *URL = "http://kvbstreams.dyndns.org:8000/wkvi-am";

AudioGeneratorMP3 *mp3;
AudioFileSourceICYStream *file;
// AudioFileSourceHTTPStream *file;
AudioFileSourceBuffer *buff;
AudioOutputI2S *out;

// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2) - 1] = 0;
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string) {
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1) - 1] = 0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

void stopStream() {
  if (mp3) {
    mp3->stop();
    delete mp3;
    mp3 = NULL;
  }
  if (buff) {
    buff->close();
    delete buff;
    buff = NULL;
  }
  if (file) {
    file->close();
    delete file;
    file = NULL;
  }
}

void startStream() {
  stopStream(); // Stop and delete any old stream first
  Serial.println("Connecting to audio stream...");

  file = new AudioFileSourceICYStream(URL);

  file->RegisterMetadataCB(MDCallback, (void*)"ICY");

  buff = new AudioFileSourceBuffer(file, 65536); // 64KB buffer
  buff->RegisterStatusCB(StatusCallback, (void*)"buffer");

  mp3 = new AudioGeneratorMP3();
  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");
  
  if (!mp3->begin(buff, out)) {
    Serial.println("Error starting MP3 stream!");
    // Clean up on failure
    stopStream();
  } else {
    Serial.println("Stream started.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Connecting to WiFi");

  digitalWrite(SD_PIN, LOW);

  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid);

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("...Connecting to WiFi");
    delay(1000);
  }
  Serial.println("Connected");

  if (!psramFound()) {
    Serial.println("PSRAM not found! Buffer will be small and popping is likely.");
  } else {
    Serial.printf("PSRAM found! Free: %u bytes\n", ESP.getFreePsram());
  }

  audioLogger = &Serial;

  out = new AudioOutputI2S();
  out->SetPinout(26, 25, 27);
  out->SetLsbJustified(false);
  
  startStream();
}


void loop() {
  static int lastms = 0;

  if (mp3->isRunning()) {
    if (millis() - lastms > 1000) {
      lastms = millis();
      Serial.printf("Running for %d ms...\n", lastms);
      Serial.flush();
    }
    if (!mp3->loop()) {
      mp3->stop();
    }
  } else {
    Serial.println("Stream is not running. Retrying in 3s...");
    delay(3000);
    startStream();
  }
}
#endif