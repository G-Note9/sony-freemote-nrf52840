#include <Arduino.h>
#include "BLECamera.h"
#include "BLEHandler.h"
#include "RemoteStatus.h"
#include "InputHandler.h"
#include "OLEDDisplay.h"

#if BATTERY_PROBE_TEST

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "OLEDConfig.h"

namespace {
constexpr int kBatteryPinAin0 = PIN_002;
constexpr int kBatteryPinAin2 = PIN_004;
constexpr int kBatteryPinAin4 = PIN_028;
constexpr int kSampleCount = 8;
constexpr int kScreenWidth = 96;
constexpr int kScreenHeight = 16;
constexpr unsigned long kProbeIntervalMs = 1000UL;
constexpr unsigned long kDisplayPageMs = 2500UL;
unsigned long lastProbeMs = 0;
bool oledReady = false;

struct ProbeReadings {
    int rawAin0 = 0;
    int rawAin2 = 0;
    int rawAin4 = 0;
    int rawVdd = 0;
    int rawVddHDiv5 = 0;
    float ain0V = 0.0f;
    float ain2V = 0.0f;
    float ain4V = 0.0f;
    float vddV = 0.0f;
    float vddhDiv5V = 0.0f;
    float estVddhV = 0.0f;
};

ProbeReadings probe;
Adafruit_SSD1306 probeDisplay(kScreenWidth, kScreenHeight, &Wire, -1);

int readAverageAnalog(int pin) {
    long total = 0;
    for (int i = 0; i < kSampleCount; ++i) {
        total += analogRead(pin);
        delay(2);
    }
    return static_cast<int>(total / kSampleCount);
}

float rawToPinVolts(int raw) {
    return (static_cast<float>(raw) * 2.4f) / 4095.0f;
}

float rawToVddVolts(int raw) {
    return (static_cast<float>(raw) * 3.6f) / 4095.0f;
}

const char* vddhSourceLabel(float estVddhV) {
    if (estVddhV > 4.4f) {
        return "USB";
    }

    if (estVddhV > 3.0f && estVddhV < 4.4f) {
        return "BAT";
    }

    return "---";
}

void printProbeToSerial() {
    Serial.print("P0.02 (AIN0): raw=");
    Serial.print(probe.rawAin0);
    Serial.print(" pinV=");
    Serial.println(probe.ain0V, 3);

    Serial.print("P0.04 (AIN2): raw=");
    Serial.print(probe.rawAin2);
    Serial.print(" pinV=");
    Serial.println(probe.ain2V, 3);

    Serial.print("P0.28 (AIN4): raw=");
    Serial.print(probe.rawAin4);
    Serial.print(" pinV=");
    Serial.println(probe.ain4V, 3);

    Serial.print("VDD: raw=");
    Serial.print(probe.rawVdd);
    Serial.print(" vddV=");
    Serial.println(probe.vddV, 3);

#ifdef SAADC_CH_PSELP_PSELP_VDDHDIV5
    Serial.print("VDDHDIV5: raw=");
    Serial.print(probe.rawVddHDiv5);
    Serial.print(" div5V=");
    Serial.print(probe.vddhDiv5V, 3);
    Serial.print(" estVddh=");
    Serial.println(probe.estVddhV, 3);
#endif

    Serial.println("-------------------");
}

void updateProbeReadings() {
    analogReference(AR_INTERNAL_2_4);
    probe.rawAin0 = readAverageAnalog(kBatteryPinAin0);
    probe.rawAin2 = readAverageAnalog(kBatteryPinAin2);
    probe.rawAin4 = readAverageAnalog(kBatteryPinAin4);
    probe.ain0V = rawToPinVolts(probe.rawAin0);
    probe.ain2V = rawToPinVolts(probe.rawAin2);
    probe.ain4V = rawToPinVolts(probe.rawAin4);

    analogReference(AR_DEFAULT);
    probe.rawVdd = analogReadVDD();
    probe.vddV = rawToVddVolts(probe.rawVdd);

#ifdef SAADC_CH_PSELP_PSELP_VDDHDIV5
    probe.rawVddHDiv5 = analogReadVDDHDIV5();
    probe.vddhDiv5V = rawToVddVolts(probe.rawVddHDiv5);
    probe.estVddhV = probe.vddhDiv5V * 5.0f;
#else
    probe.rawVddHDiv5 = 0;
    probe.vddhDiv5V = 0.0f;
    probe.estVddhV = 0.0f;
#endif
}

void drawProbePage() {
    if (!oledReady) {
        return;
    }

    char line1[17];
    char line2[17];
    const unsigned long page = (millis() / kDisplayPageMs) % 2UL;

    probeDisplay.clearDisplay();
    probeDisplay.setTextSize(1);
    probeDisplay.setTextColor(SSD1306_WHITE);
    probeDisplay.setCursor(0, 0);

    if (page == 0UL) {
        snprintf(line1, sizeof(line1), "VH %.2fV %s", probe.estVddhV, vddhSourceLabel(probe.estVddhV));
        snprintf(line2, sizeof(line2), "VDD %.2fV", probe.vddV);
    } else {
        snprintf(line1, sizeof(line1), "A0 %.2f A2 %.2f", probe.ain0V, probe.ain2V);
        snprintf(line2, sizeof(line2), "A4 %.2f", probe.ain4V);
    }

    probeDisplay.println(line1);
    probeDisplay.println(line2);
    probeDisplay.display();
}
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== BATTERY PROBE TEST ===");
    Serial.println("Reading P0.02 (AIN0), P0.04 (AIN2), P0.28 (AIN4), VDD and VDDHDIV5");

    Wire.setPins(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.begin();
    Wire.setClock(400000);
    delay(20);

    oledReady = probeDisplay.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (oledReady) {
        probeDisplay.clearDisplay();
        probeDisplay.setTextSize(1);
        probeDisplay.setTextColor(SSD1306_WHITE);
        probeDisplay.setCursor(0, 0);
        probeDisplay.println("BATTERY CHECK");
        probeDisplay.println("OLED READY");
        probeDisplay.display();
    }

    analogReference(AR_INTERNAL_2_4);
    analogReadResolution(12);
}

void loop()
{
    if (millis() - lastProbeMs >= kProbeIntervalMs) {
        lastProbeMs = millis();
        updateProbeReadings();
        printProbeToSerial();
    }

    drawProbePage();
}

#else

BLECamera camera;
BLEHandler handler;

void resetTest(void) {
    Serial.println("Clearing bonds");
    handler.clearBonds();
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== FREEMOTE BOOT ===");
    // Setup the red LED
    //pinMode(LED_BUILTIN, OUTPUT);

    // Configure the Neopixel Status LED
    RemoteStatus *rs = RemoteStatus::access();
    rs->set(Status::BOOT);
    
    // === FIRMWARE MARKER: 8 быстрых вспышек при старте ===
pinMode(PIN_LED_STATUS, OUTPUT);
for (int i = 0; i < 8; i++) {
  digitalWrite(PIN_LED_STATUS, HIGH); delay(80);
  digitalWrite(PIN_LED_STATUS, LOW);  delay(120);
}

    // === GPIO SELF-TEST: LEDs on P0.17 and P0.22 for 1 seconds ===
pinMode(PIN_017, OUTPUT);
pinMode(PIN_022, OUTPUT);
digitalWrite(PIN_017, HIGH);
digitalWrite(PIN_022, HIGH);
delay(1000);
digitalWrite(PIN_017, LOW);
digitalWrite(PIN_022, LOW);

    // Setup button handling
    Input::Init(&camera);

    Input::registerResetCallback(resetTest);

    OLEDDisplay::access()->begin();

// Debug nation bro
    Serial.begin(115200);
#if CFG_DEBUG
    rs->set(Status::WAIT_FOR_SERIAL);
#endif

    // Initialze BLE
    if (!handler.InitBLE(&camera))
    {
        rs->set(Status::ERROR);
    }
}

void loop()
{
    yield();

    RemoteStatus::access()->update();
    Input::process(millis());
}

#endif
