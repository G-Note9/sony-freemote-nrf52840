#include "RemoteStatus.h"
#include "OLEDDisplay.h"

static RemoteStatus* instance = nullptr;

RemoteStatus* RemoteStatus::access() {
    if (!instance) instance = new RemoteStatus();
    return instance;
}

RemoteStatus::RemoteStatus() {
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);

    currentStatus = Status::NONE;
    blinking = false;
    blinkInterval = 0;
    lastToggle = 0;
    ledState = LOW;
}

void RemoteStatus::update() {
    OLEDDisplay::access()->update();

    if (!blinking) {
        bool shouldBeOn = (currentStatus != Status::NONE && currentStatus != Status::DO_NOT_USE);
        digitalWrite(PIN_LED_STATUS, shouldBeOn ? HIGH : LOW);
        return;
    }

    if (millis() - lastToggle >= blinkInterval) {
        lastToggle = millis();
        ledState = !ledState;
        digitalWrite(PIN_LED_STATUS, ledState ? HIGH : LOW);
    }
}

void RemoteStatus::set(Status s) {
    currentStatus = s;
    resolveBlinkPattern(s);
    OLEDDisplay::access()->setStatus(s);
}

Status RemoteStatus::get() const {
    return currentStatus;
}

void RemoteStatus::resolveBlinkPattern(Status s) {
    blinking = false;
    blinkInterval = 0;

    switch (s) {
        case Status::NONE:
        case Status::DO_NOT_USE:
            digitalWrite(PIN_LED_STATUS, LOW);
            break;

        case Status::BOOT:
        case Status::READY:
        case Status::CONNECTED:
            digitalWrite(PIN_LED_STATUS, HIGH);
            break;

        case Status::PAIRING:
            blinking = true;
            blinkInterval = 150;
            break;

        case Status::ERROR:
            blinking = true;
            blinkInterval = 100;
            break;

        case Status::CONNECTING:
            blinking = true;
            blinkInterval = 500;
            break;

        case Status::CONNECTION_LOST:
            blinking = true;
            blinkInterval = 200;
            break;

        case Status::FOCUS_ACQUIRED:
            blinking = true;
            blinkInterval = 300;
            break;

        case Status::SHUTTER:
            blinking = true;
            blinkInterval = 80;   // короткое "дрожание" при спуске
            break;

        case Status::WAIT_FOR_SERIAL:
            blinking = true;
            blinkInterval = 1000;
            break;

        default:
            digitalWrite(PIN_LED_STATUS, LOW);
            break;
    }
}
