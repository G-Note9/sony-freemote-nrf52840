#pragma once

#include <Arduino.h>
#include "BLECamera.h"
#include "BoardConfig.h"

typedef void (*button_callback)(void);

enum class CaptureMode {
    NORMAL,
    TIMER,
    BULB
};

class Input {
public:
    static inline BLECamera *_camera_ref = nullptr;

    static bool Init(BLECamera *newcam);
    static void process(unsigned long now);

    static void registerResetCallback(button_callback cb);

    static CaptureMode mode();
    static bool isTimerRunning();
    static bool isBulbRunning();
    static unsigned long timerPresetMs();
    static unsigned long timerRemainingMs(unsigned long now);
    static unsigned long bulbElapsedMs(unsigned long now);

private:
    static inline button_callback _resetCallback = nullptr;

    static inline bool lastShutter = false;
    static inline bool lastFocus = false;
    static inline bool lastC1 = false;
    static inline bool lastMode = false;
    static inline bool lastPair = false;

    static inline unsigned long lastDebounceShutter = 0;
    static inline unsigned long lastDebounceFocus = 0;
    static inline unsigned long lastDebounceC1 = 0;
    static inline unsigned long lastDebounceMode = 0;
    static inline unsigned long lastDebouncePair = 0;

    static inline CaptureMode currentMode = CaptureMode::NORMAL;

    static inline bool pairingTriggered = false;
    static inline unsigned long pairHoldStart = 0;

    static inline bool canReset = true;
    static inline unsigned long shutterHoldStart = 0;

    static inline bool timerActive = false;
    static inline bool timerStopIssued = false;
    static inline unsigned long timerPreset = 0;
    static inline unsigned long timerStartedAt = 0;

    static inline bool bulbActive = false;
    static inline bool bulbStopIssued = false;
    static inline unsigned long bulbStartedAt = 0;
    static inline unsigned long bulbStopAt = 0;
    static inline unsigned long bulbLastElapsed = 0;

    static inline unsigned long modePressStart = 0;
    static inline bool modeHoldHandled = false;
    static inline unsigned long modeLastRepeatAt = 0;
    static inline uint8_t modeClickCount = 0;
    static inline unsigned long modeClickWindowStart = 0;

    static constexpr unsigned long DEBOUNCE_MS = 20;
    static constexpr unsigned long PAIR_HOLD_MS = 3000;
    static constexpr unsigned long RESET_HOLD_MS = 10000;
    static constexpr unsigned long RESET_WINDOW_MS = 25000;
    static constexpr unsigned long MODE_HOLD_MS = 600;
    static constexpr unsigned long MODE_REPEAT_MS = 1000;
    static constexpr unsigned long MODE_CLICK_WINDOW_MS = 650;
    static constexpr unsigned long TIMER_STEP_MS = 5000;
    static constexpr unsigned long TIMER_START_DELAY_MS = 1000;
    static constexpr unsigned long TIMER_STOP_EARLY_MS = 1000;
    static constexpr unsigned long BULB_STOP_DELAY_MS = 1000;

    static bool readButtonActiveLow(uint8_t pin);
    static bool isConnected();
    static void cycleMode();
    static void setMode(CaptureMode newMode);
    static void cancelActiveCapture();
    static void handleModeClickAction();
    static void startBulb(unsigned long now);
    static void stopBulb();
    static void startTimer(unsigned long now);
    static void stopTimer();
};
