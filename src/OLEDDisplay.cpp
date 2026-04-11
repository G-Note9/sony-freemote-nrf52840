#include "OLEDDisplay.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "OLEDConfig.h"

namespace {
constexpr int SCREEN_WIDTH = 96;
constexpr int SCREEN_HEIGHT = 16;
constexpr unsigned long BOOT_SPLASH_MS = 2000UL;
constexpr unsigned long BOOT_BATTERY_DURATION_MS = 2000UL;
constexpr unsigned long BATTERY_REFRESH_MS = 5000UL;
constexpr unsigned long PAIRING_SCREEN_TIMEOUT_MS = 15000UL;
constexpr unsigned long DISCONNECT_SCREEN_MS = 2000UL;
constexpr unsigned long MODE_OVERLAY_MS = 2000UL;
constexpr unsigned long CONNECT_WORD_MS = 1000UL;
constexpr int BATTERY_SAMPLE_COUNT = 16;
constexpr int SEP_X = 63;
constexpr int TIME_Y_OFFSET = 2;
constexpr int MODE_Y_OFFSET = -1;
constexpr int RIGHT_BLOCK_X_OFFSET = 2;
constexpr int SYSTEM_TEXT_Y_OFFSET = 2;
constexpr int SYSTEM_TEXT_X_OFFSET = 0;
constexpr float USB_DETECT_THRESHOLD_V = 4.4f;
constexpr float BATTERY_MIN_VALID_V = 3.0f;
constexpr float BATTERY_MAX_VALID_V = 4.5f;
constexpr float BATTERY_FILTER_ALPHA = 0.2f;

float rawToDefaultRangeVolts(int raw)
{
    return (static_cast<float>(raw) * 3.6f) / 4095.0f;
}

int clampBatteryPercent(float batteryVolts)
{
    const float mvolts = batteryVolts * 1000.0f;

    if (mvolts <= 3300.0f) {
        return 0;
    }

    if (mvolts < 3600.0f) {
        return static_cast<int>((mvolts - 3300.0f) / 30.0f);
    }

    const int percent = 10 + static_cast<int>((mvolts - 3600.0f) * 0.15f);
    if (percent < 0) {
        return 0;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}
}

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static OLEDDisplay* instance = nullptr;

OLEDDisplay* OLEDDisplay::access()
{
    if (!instance) {
        instance = new OLEDDisplay();
    }
    return instance;
}

bool OLEDDisplay::begin()
{
    Wire.setPins(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.begin();
    Wire.setClock(400000);
    delay(20);

    _initialized = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
    if (!_initialized) {
        Serial.println("OLED: SSD1306 init failed");
        return false;
    }

    _bootMs = millis();
    refreshBatteryState(_bootMs, true);
    _statusSinceMs = _bootMs;
    _currentStatus = Status::BOOT;
    _previousStatus = Status::NONE;
    ensureDisplayPower(true);
    drawCenteredText("G_NOTE9", 2);
    return true;
}

void OLEDDisplay::setStatus(Status status)
{
    _previousStatus = _currentStatus;
    _currentStatus = status;
    _statusSinceMs = millis();

    if (!_initialized) {
        return;
    }

    if (status == Status::CONNECTION_LOST) {
        _connectSequenceActive = false;
        _connectSequenceShown = false;
        _modeOverlayUntilMs = 0;
        _disconnectScreenActive = _hadReadyConnection;
        _hadReadyConnection = false;
    }

    if (status == Status::READY && !_connectSequenceShown) {
        _hadReadyConnection = true;
        _disconnectScreenActive = false;
        _connectSequenceActive = true;
        _connectSequenceStartMs = _statusSinceMs;
    }
}

bool OLEDDisplay::isConnectedStatus(Status status) const
{
    return status == Status::READY ||
           status == Status::FOCUS_ACQUIRED ||
           status == Status::SHUTTER;
}

bool OLEDDisplay::isIdleWelcomeState() const
{
    return !isConnectedStatus(_currentStatus) &&
           _currentStatus != Status::PAIRING &&
           _currentStatus != Status::ERROR;
}

void OLEDDisplay::ensureDisplayPower(bool on)
{
    if (_displayOn == on) {
        return;
    }

    display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    _displayOn = on;
}

void OLEDDisplay::refreshBatteryState(unsigned long now, bool force)
{
    if (!force && (now - _lastBatteryReadMs) < BATTERY_REFRESH_MS) {
        return;
    }

    _lastBatteryReadMs = now;

#ifdef SAADC_CH_PSELP_PSELP_VDDHDIV5
    analogReference(AR_DEFAULT);
    analogReadResolution(12);

    long total = 0;
    for (int i = 0; i < BATTERY_SAMPLE_COUNT; ++i) {
        total += analogReadVDDHDIV5();
        delay(2);
    }

    analogReadResolution(10);

    const int rawAverage = static_cast<int>(total / BATTERY_SAMPLE_COUNT);
    const float vddhVolts = rawToDefaultRangeVolts(rawAverage) * 5.0f;

    _usbPowerDetected = vddhVolts > USB_DETECT_THRESHOLD_V;
    if (!_usbPowerDetected && vddhVolts > BATTERY_MIN_VALID_V && vddhVolts < BATTERY_MAX_VALID_V) {
        if (!_hasBatteryEstimate) {
            _filteredBatteryVolts = vddhVolts;
            _hasBatteryEstimate = true;
        } else {
            _filteredBatteryVolts = (_filteredBatteryVolts * (1.0f - BATTERY_FILTER_ALPHA)) + (vddhVolts * BATTERY_FILTER_ALPHA);
        }

        _batteryPercent = clampBatteryPercent(_filteredBatteryVolts);
    }
#else
    (void)now;
    _hasBatteryEstimate = false;
    _usbPowerDetected = false;
    _batteryPercent = 0;
#endif
}

void OLEDDisplay::update()
{
    if (!_initialized) {
        return;
    }

    const unsigned long now = millis();
    refreshBatteryState(now);
    const CaptureMode mode = Input::mode();
    const unsigned long timerPreset = Input::timerPresetMs();
    const unsigned long bootElapsed = now - _bootMs;

    if (mode != _lastMode || timerPreset != _lastTimerPresetMs) {
        _lastMode = mode;
        _lastTimerPresetMs = timerPreset;
        if (isConnectedStatus(_currentStatus)) {
            _modeOverlayUntilMs = now + MODE_OVERLAY_MS;
        }
    }

    ensureDisplayPower(true);

    if (bootElapsed < BOOT_SPLASH_MS) {
        drawCenteredText("G_NOTE9", 2);
        return;
    }

    if (bootElapsed < (BOOT_SPLASH_MS + BOOT_BATTERY_DURATION_MS)) {
        drawBatterySplash(now);
        return;
    }

    if (!_bootBatteryShown) {
        _bootBatteryShown = true;
    }

    if (_connectSequenceActive) {
        if (_connectSequenceStartMs < (_bootMs + BOOT_SPLASH_MS + BOOT_BATTERY_DURATION_MS)) {
            _connectSequenceStartMs = now;
        }
        drawConnectSequence(now);
        return;
    }

    if (_currentStatus == Status::PAIRING && (now - _statusSinceMs) < PAIRING_SCREEN_TIMEOUT_MS) {
        drawCenteredText("PAIRING", 2);
        return;
    }

    if (_currentStatus == Status::ERROR) {
        drawCenteredText("ERROR", 2);
        return;
    }

    if (_disconnectScreenActive && _currentStatus == Status::CONNECTION_LOST && (now - _statusSinceMs) < DISCONNECT_SCREEN_MS) {
        drawCenteredText("- VIBE", 2);
        return;
    }

    if (isConnectedStatus(_currentStatus)) {
        if (Input::isBulbRunning()) {
            drawMainScreen(Input::bulbElapsedMs(now), 'B');
            return;
        }

        if (Input::isTimerRunning()) {
            drawMainScreen(Input::timerRemainingMs(now), 'T');
            return;
        }

        if (mode == CaptureMode::TIMER) {
            drawMainScreen(Input::timerPresetMs(), 'T');
            return;
        }

        if (mode == CaptureMode::BULB) {
            drawMainScreen(Input::bulbElapsedMs(now), 'B');
            return;
        }

        if (_modeOverlayUntilMs > now) {
            drawModeScreen(mode, now);
            return;
        }

        ensureDisplayPower(false);
        return;
    }

    if (isIdleWelcomeState() || _currentStatus == Status::CONNECTION_LOST || _currentStatus == Status::CONNECTING) {
        drawWelcomeScreen();
        return;
    }

    drawWelcomeScreen();
}

void OLEDDisplay::formatTimeMs(unsigned long totalMs, char* out, size_t outSize) const
{
    const unsigned long totalSeconds = totalMs / 1000UL;
    const unsigned long minutes = (totalSeconds / 60UL) % 100UL;
    const unsigned long seconds = totalSeconds % 60UL;
    snprintf(out, outSize, "%02lu:%02lu", minutes, seconds);
}

void OLEDDisplay::drawUsbBolt(int x, int y)
{
    display.drawLine(x + 2, y, x + 1, y + 2, SSD1306_WHITE);
    display.drawLine(x + 1, y + 2, x + 3, y + 2, SSD1306_WHITE);
    display.drawLine(x + 3, y + 2, x + 1, y + 6, SSD1306_WHITE);
    display.drawLine(x + 1, y + 6, x + 4, y + 3, SSD1306_WHITE);
}

void OLEDDisplay::drawBootUsbBolt(int x, int y)
{
    display.drawLine(x + 5, y, x + 1, y + 6, SSD1306_WHITE);
    display.drawLine(x + 1, y + 6, x + 6, y + 6, SSD1306_WHITE);
    display.drawLine(x + 6, y + 6, x + 4, y + 12, SSD1306_WHITE);
    display.drawLine(x + 4, y + 12, x + 9, y + 6, SSD1306_WHITE);
    display.drawLine(x + 9, y + 6, x + 6, y + 6, SSD1306_WHITE);
}

void OLEDDisplay::drawBatteryIcon(int x, int y, int percent, bool usbPowered)
{
    const int bodyW = 10;
    const int bodyH = 5;
    const int tipW = 2;
    const int tipH = 3;

    display.drawRect(x, y, bodyW, bodyH, SSD1306_WHITE);
    display.fillRect(x + bodyW, y + 1, tipW, tipH, SSD1306_WHITE);

    int fillW = map(percent, 0, 100, 0, bodyW - 2);
    if (fillW < 0) fillW = 0;
    if (fillW > (bodyW - 2)) fillW = bodyW - 2;

    if (fillW > 0) {
        display.fillRect(x + 1, y + 1, fillW, bodyH - 2, SSD1306_WHITE);
    }

    if (usbPowered) {
        drawUsbBolt(x + bodyW + tipW + 1, y - 1);
    }
}

void OLEDDisplay::drawBootBatteryIcon(int x, int y, int percent)
{
    const int bodyW = 15;
    const int bodyH = 9;
    const int tipW = 3;
    const int tipH = 4;

    display.drawRect(x, y, bodyW, bodyH, SSD1306_WHITE);
    display.fillRect(x + bodyW, y + 2, tipW, tipH, SSD1306_WHITE);

    int fillW = map(percent, 0, 100, 0, bodyW - 2);
    if (fillW < 0) fillW = 0;
    if (fillW > (bodyW - 2)) fillW = bodyW - 2;

    if (fillW > 0) {
        display.fillRect(x + 1, y + 1, fillW, bodyH - 2, SSD1306_WHITE);
    }
}

void OLEDDisplay::drawCenteredText(const char* text, uint8_t textSize)
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(textSize);

    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

    const int textX = (SCREEN_WIDTH - static_cast<int>(w)) / 2 - x1 + SYSTEM_TEXT_X_OFFSET;
    const int textY = (SCREEN_HEIGHT - static_cast<int>(h)) / 2 - y1 + SYSTEM_TEXT_Y_OFFSET;

    display.setCursor(textX, textY);
    display.print(text);
    display.display();
}

void OLEDDisplay::drawWelcomeScreen()
{
    drawCenteredText("SONY", 2);
}

void OLEDDisplay::drawBatterySplash(unsigned long now)
{
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (_usbPowerDetected) {
        const unsigned long batteryPhaseMs = now - (_bootMs + BOOT_SPLASH_MS);
        const unsigned long cycleMs = BOOT_BATTERY_DURATION_MS / 2UL;
        const unsigned long cyclePosMs = cycleMs == 0 ? 0 : (batteryPhaseMs % cycleMs);
        const int animatedPercent = cycleMs == 0 ? 100 : static_cast<int>((cyclePosMs * 100UL) / cycleMs);

        const int iconX = 28;
        const int iconY = 4;
        drawBootBatteryIcon(iconX, iconY, animatedPercent);
        drawBootUsbBolt(iconX + 23, iconY);
    } else {
        display.setTextSize(2);

        char batteryBuf[8];
        snprintf(batteryBuf, sizeof(batteryBuf), "%d%%", _batteryPercent);

        int16_t x1;
        int16_t y1;
        uint16_t w;
        uint16_t h;
        display.getTextBounds(batteryBuf, 0, 0, &x1, &y1, &w, &h);

        const int iconX = 0;
        const int iconY = 4;
        const int gap = 4;
        const int totalBlockW = 18 + gap + static_cast<int>(w);
        const int blockStartX = (SCREEN_WIDTH - totalBlockW) / 2;
        const int textX = blockStartX + 18 + gap;
        const int textY = (SCREEN_HEIGHT - static_cast<int>(h)) / 2 - y1 + SYSTEM_TEXT_Y_OFFSET;

        drawBootBatteryIcon(blockStartX + iconX, iconY, _batteryPercent);
        display.setCursor(textX, textY);
        display.print(batteryBuf);
    }

    display.display();
}

void OLEDDisplay::drawConnectSequence(unsigned long now)
{
    const unsigned long elapsed = now - _connectSequenceStartMs;

    ensureDisplayPower(true);

    if (elapsed < CONNECT_WORD_MS) {
        drawCenteredText("CONNECT", 2);
    } else if (elapsed < (CONNECT_WORD_MS * 2UL)) {
        drawCenteredText("SONY", 2);
    } else if (elapsed < (CONNECT_WORD_MS * 3UL)) {
        drawCenteredText("CAMERA", 2);
    } else {
        _connectSequenceActive = false;
        _connectSequenceShown = true;
        ensureDisplayPower(false);
    }
}

void OLEDDisplay::drawModeScreen(CaptureMode mode, unsigned long now)
{
    switch (mode) {
        case CaptureMode::NORMAL:
            drawCenteredText("PHOTO", 2);
            break;
        case CaptureMode::BULB:
            drawMainScreen(Input::bulbElapsedMs(now), 'B');
            break;
        case CaptureMode::TIMER:
            drawMainScreen(Input::isTimerRunning() ? Input::timerRemainingMs(now) : Input::timerPresetMs(), 'T');
            break;
    }
}

void OLEDDisplay::drawMainScreen(unsigned long timeMs, char modeChar)
{
    char timeBuf[6];
    formatTimeMs(timeMs, timeBuf, sizeof(timeBuf));

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    int16_t x1;
    int16_t y1;
    uint16_t w;
    uint16_t h;

    display.setTextSize(2);
    display.getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);

    const int leftAreaWidth = SEP_X - 2;
    const int timeX = (leftAreaWidth - static_cast<int>(w)) / 2 - x1;
    const int timeY = (SCREEN_HEIGHT - static_cast<int>(h)) / 2 - y1 + TIME_Y_OFFSET;

    display.setCursor(timeX, timeY);
    display.print(timeBuf);
    display.drawLine(SEP_X, 0, SEP_X, SCREEN_HEIGHT - 1, SSD1306_WHITE);

    const int rightX = SEP_X + 2;
    const int rightW = SCREEN_WIDTH - rightX;

    display.setTextSize(1);
    const int iconX = rightX + 1 + RIGHT_BLOCK_X_OFFSET;
    const int iconY = 1;
    drawBatteryIcon(iconX, iconY, _batteryPercent, false);

    if (_usbPowerDetected) {
        drawUsbBolt(iconX + 15, 0);
    } else if (_hasBatteryEstimate) {
        char batteryBuf[4];
        snprintf(batteryBuf, sizeof(batteryBuf), "%d", _batteryPercent);
        display.getTextBounds(batteryBuf, 0, 0, &x1, &y1, &w, &h);

        const int numberX = iconX + 15;
        const int numberY = 1 - y1;

        display.setCursor(numberX, numberY);
        display.print(batteryBuf);
    }

    char modeBuf[2] = {modeChar, '\0'};
    display.getTextBounds(modeBuf, 0, 0, &x1, &y1, &w, &h);

    const int modeX = rightX + (rightW - static_cast<int>(w)) / 2 - x1 + RIGHT_BLOCK_X_OFFSET;
    const int modeY = 10 - y1 + MODE_Y_OFFSET;

    display.setCursor(modeX, modeY);
    display.print(modeBuf);
    display.display();
}
