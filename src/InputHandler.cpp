#include "InputHandler.h"
#include "BLEHandler.h"

bool Input::readButtonActiveLow(uint8_t pin)
{
    return (digitalRead(pin) == LOW); // кнопка на GND + pullup
}

bool Input::Init(BLECamera *newcam)
{
    _camera_ref = newcam;
    canReset = true;

    // кнопки
    pinMode(PIN_BTN_SHUTTER, INPUT_PULLUP);
    pinMode(PIN_BTN_FOCUS, INPUT_PULLUP);
    pinMode(PIN_BTN_MODE, INPUT_PULLUP);

    // светодиоды
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_LED_MODE, OUTPUT);
    pinMode(PIN_LED_PAIR, OUTPUT);

    digitalWrite(PIN_LED_STATUS, LOW);
    digitalWrite(PIN_LED_MODE, LOW);
    digitalWrite(PIN_LED_PAIR, LOW);

    // стартовый режим
    manualMode = false;
    _camera_ref->setMode(AUTO_FOCUS);

    return true;
}

void Input::registerResetCallback(button_callback cb)
{
    _resetCallback = cb;
}

void Input::process(unsigned long now)
{
    // читаем сырье
    bool sRaw = readButtonActiveLow(PIN_BTN_SHUTTER);
    bool fRaw = readButtonActiveLow(PIN_BTN_FOCUS);
    bool mRaw = readButtonActiveLow(PIN_BTN_MODE);

    // debounce shutter
    static bool sStable = false;
    if (sRaw != lastShutter) { lastDebounceShutter = now; lastShutter = sRaw; }
    if ((now - lastDebounceShutter) > DEBOUNCE_MS) sStable = sRaw;

    // debounce focus
    static bool fStable = false;
    if (fRaw != lastFocus) { lastDebounceFocus = now; lastFocus = fRaw; }
    if ((now - lastDebounceFocus) > DEBOUNCE_MS) fStable = fRaw;

    // debounce mode
    static bool mStable = false;
    if (mRaw != lastMode) { lastDebounceMode = now; lastMode = mRaw; }
    if ((now - lastDebounceMode) > DEBOUNCE_MS) mStable = mRaw;

    // --- Pairing combo: SHUTTER + FOCUS hold 3s ---
    if (sStable && fStable)
    {
        if (comboStart == 0) comboStart = now;

        if (!pairingTriggered && (now - comboStart) > 3000)
        {
            pairingTriggered = true;

            // включаем pairing mode + очищаем bonds, чтобы камера точно спросила "подключить?"
            BLEHandler::setPairingMode(true, true);

            digitalWrite(PIN_LED_PAIR, HIGH);
        }
    }
    else
    {
        comboStart = 0;
        pairingTriggered = false;
        digitalWrite(PIN_LED_PAIR, LOW);
    }

    // --- Mode button: toggle AF/MF on press edge ---
    static bool prevM = false;
    if (mStable && !prevM)
    {
        manualMode = !manualMode;
        if (manualMode)
        {
            _camera_ref->setMode(MANUAL_FOCUS);
            digitalWrite(PIN_LED_MODE, HIGH);
        }
        else
        {
            _camera_ref->setMode(AUTO_FOCUS);
            digitalWrite(PIN_LED_MODE, LOW);
        }
    }
    prevM = mStable;

    // --- Focus (hold) ---
    static bool prevF = false;
    if (fStable && !prevF) _camera_ref->focus(true);
    if (!fStable && prevF) _camera_ref->focus(false);
    prevF = fStable;

    // --- Shutter (press/release) ---
    static bool prevS = false;
    if (sStable && !prevS)
    {
        _camera_ref->pressTrigger();
        shutterHoldStart = now;
    }
    if (!sStable && prevS)
    {
        _camera_ref->releaseTrigger();
        shutterHoldStart = 0;
        canReset = true;
    }
    prevS = sStable;

    // --- Optional: hold SHUTTER 10s early after boot to clear bonds ---
    // (поведение как "сброс пульта")
    if (sStable && shutterHoldStart != 0 && canReset)
    {
        if ((now - shutterHoldStart) > 10000 && now < 25000)
        {
            if (_resetCallback) _resetCallback();
            canReset = false;
        }
    }
}