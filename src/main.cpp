#include <Arduino.h>
#include "BLECamera.h"
#include "BLEHandler.h"
#include "RemoteStatus.h"
#include "InputHandler.h"

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
    
    // === FIRMWARE MARKER: 3 быстрых вспышки при старте ===
pinMode(PIN_LED_STATUS, OUTPUT);
for (int i = 0; i < 8; i++) {
  digitalWrite(PIN_LED_STATUS, HIGH); delay(80);
  digitalWrite(PIN_LED_STATUS, LOW);  delay(120);
}

    // === GPIO SELF-TEST: LEDs on P0.17 and P0.22 for 4 seconds ===
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