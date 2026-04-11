#ifndef REMOTESTATUS_H
#define REMOTESTATUS_H

#include <Arduino.h>
#include "BoardConfig.h"

enum class Status {
    NONE,
    BOOT,
    ERROR,
    PAIRING,
    CONNECTING,
    CONNECTED,
    CONNECTION_LOST,
    READY,
    FOCUS_ACQUIRED,
    SHUTTER,
    WAIT_FOR_SERIAL,
    DO_NOT_USE
};

class RemoteStatus {
public:
    static RemoteStatus* access();
    void set(Status s);
    Status get() const;
    void update();

private:
    RemoteStatus();

    Status currentStatus;
    bool blinking;
    unsigned long blinkInterval;
    unsigned long lastToggle;
    bool ledState;

    void resolveBlinkPattern(Status s);
};

#endif
