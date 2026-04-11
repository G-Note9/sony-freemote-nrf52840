#pragma once

#include <Arduino.h>
#include "RemoteStatus.h"
#include "InputHandler.h"

class OLEDDisplay {
public:
    static OLEDDisplay* access();

    bool begin();
    void update();
    void setStatus(Status status);

private:
    OLEDDisplay() = default;

    bool _initialized = false;
    bool _displayOn = false;
    bool _bootBatteryShown = false;
    bool _hadReadyConnection = false;
    bool _disconnectScreenActive = false;
    bool _connectSequenceActive = false;
    bool _connectSequenceShown = false;
    bool _hasBatteryEstimate = false;
    bool _usbPowerDetected = false;
    unsigned long _connectSequenceStartMs = 0;
    unsigned long _bootMs = 0;
    unsigned long _lastBatteryReadMs = 0;
    unsigned long _statusSinceMs = 0;
    unsigned long _modeOverlayUntilMs = 0;
    Status _currentStatus = Status::NONE;
    Status _previousStatus = Status::NONE;
    CaptureMode _lastMode = CaptureMode::NORMAL;
    unsigned long _lastTimerPresetMs = 0;
    int _batteryPercent = 0;
    float _filteredBatteryVolts = 0.0f;

    bool isConnectedStatus(Status status) const;
    bool isIdleWelcomeState() const;
    void refreshBatteryState(unsigned long now, bool force = false);
    void ensureDisplayPower(bool on);
    void drawWelcomeScreen();
    void drawBatterySplash(unsigned long now);
    void drawConnectSequence(unsigned long now);
    void drawModeScreen(CaptureMode mode, unsigned long now);
    void drawMainScreen(unsigned long timeMs, char modeChar);
    void drawBatteryIcon(int x, int y, int percent, bool usbPowered);
    void drawUsbBolt(int x, int y);
    void drawBootBatteryIcon(int x, int y, int percent);
    void drawBootUsbBolt(int x, int y);
    void drawCenteredText(const char* text, uint8_t textSize);
    void formatTimeMs(unsigned long totalMs, char* out, size_t outSize) const;
};
