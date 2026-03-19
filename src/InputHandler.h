#pragma once

#include <Arduino.h>
#include "BLECamera.h"
#include "BoardConfig.h"

typedef void (*button_callback)(void);

class Input {
public:
    static inline BLECamera *_camera_ref = nullptr;

    static bool Init(BLECamera *newcam);
    static void process(unsigned long now);

    static void registerResetCallback(button_callback cb);

private:
    static inline button_callback _resetCallback = nullptr;

    // debounce states
    static inline bool lastShutter = false;
    static inline bool lastFocus = false;
    static inline bool lastMode = false;
    static inline bool lastPair = false;

    static inline unsigned long lastDebounceShutter = 0;
    static inline unsigned long lastDebounceFocus = 0;
    static inline unsigned long lastDebounceMode = 0;
    static inline unsigned long lastDebouncePair = 0;

    static constexpr unsigned long DEBOUNCE_MS = 20;

    // pairing combo
    static inline bool pairingTriggered = false;
    static inline unsigned long comboStart = 0;

    // long hold to clear bonds (optional)
    static inline bool canReset = true;
    static inline unsigned long shutterHoldStart = 0;

    // helpers
    static bool readButtonActiveLow(uint8_t pin);
};