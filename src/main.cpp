#include <Arduino.h>
#include "BLECamera.h"
#include "BLEHandler.h"
#include "RemoteStatus.h"
#include "InputHandler.h"
#include "OLEDDisplay.h"

BLECamera camera;
BLEHandler handler;

namespace {
constexpr int kBootBlinkCount = 8;
constexpr unsigned long kBootBlinkOnMs = 80UL;
constexpr unsigned long kBootBlinkOffMs = 120UL;
constexpr unsigned long kStartupSelfTestMs = 1000UL;
}

void resetTest(void) {
    handler.clearBonds();
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== FREEMOTE BOOT ===");
    RemoteStatus* rs = RemoteStatus::access();
    rs->set(Status::BOOT);
    
    // === FIRMWARE MARKER: 8 быстрых вспышек при старте ===
    pinMode(PIN_LED_STATUS, OUTPUT);
    for (int i = 0; i < kBootBlinkCount; ++i) {
        digitalWrite(PIN_LED_STATUS, HIGH);
        delay(kBootBlinkOnMs);
        digitalWrite(PIN_LED_STATUS, LOW);
        delay(kBootBlinkOffMs);
    }

    pinMode(PIN_017, OUTPUT);
    pinMode(PIN_022, OUTPUT);
    digitalWrite(PIN_017, HIGH);
    digitalWrite(PIN_022, HIGH);
    delay(kStartupSelfTestMs);
    digitalWrite(PIN_017, LOW);
    digitalWrite(PIN_022, LOW);

    Input::Init(&camera);

    Input::registerResetCallback(resetTest);

    OLEDDisplay::access()->begin();
#if CFG_DEBUG
    rs->set(Status::WAIT_FOR_SERIAL);
#endif

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
