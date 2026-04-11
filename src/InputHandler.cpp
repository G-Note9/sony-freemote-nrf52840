#include "InputHandler.h"
#include "BLEHandler.h"
#include "RemoteStatus.h"

bool Input::readButtonActiveLow(uint8_t pin)
{
    return (digitalRead(pin) == LOW); // кнопка на GND + pullup
}

bool Input::Init(BLECamera *newcam)
{
    _camera_ref = newcam;
    canReset = true;
    currentMode = CaptureMode::NORMAL;
    timerActive = false;
    timerStopIssued = false;
    timerPreset = 0;
    timerStartedAt = 0;
    bulbActive = false;
    bulbStopIssued = false;
    bulbStartedAt = 0;
    bulbStopAt = 0;
    bulbLastElapsed = 0;

    // кнопки
    pinMode(PIN_BTN_SHUTTER, INPUT_PULLUP);
    pinMode(PIN_BTN_FOCUS, INPUT_PULLUP);
    pinMode(PIN_BTN_C1, INPUT_PULLUP);
    pinMode(PIN_BTN_MODE, INPUT_PULLUP);
    pinMode(PIN_BTN_PAIR, INPUT_PULLUP);

    // светодиоды
    pinMode(PIN_LED_STATUS, OUTPUT);
    pinMode(PIN_LED_MODE, OUTPUT);
    pinMode(PIN_LED_PAIR, OUTPUT);

    digitalWrite(PIN_LED_STATUS, LOW);
    digitalWrite(PIN_LED_MODE, LOW);
    digitalWrite(PIN_LED_PAIR, LOW);

    return true;
}

void Input::registerResetCallback(button_callback cb)
{
    _resetCallback = cb;
}

CaptureMode Input::mode()
{
    return currentMode;
}

bool Input::isTimerRunning()
{
    return timerActive;
}

bool Input::isBulbRunning()
{
    return bulbActive && !bulbStopIssued;
}

unsigned long Input::timerPresetMs()
{
    return timerPreset;
}

unsigned long Input::timerRemainingMs(unsigned long now)
{
    if (!timerActive) {
        return timerPreset;
    }

    if (timerStartedAt == 0 || now <= timerStartedAt) {
        return timerPreset;
    }

    const unsigned long elapsed = now - timerStartedAt;
    return (elapsed >= timerPreset) ? 0 : (timerPreset - elapsed);
}

unsigned long Input::bulbElapsedMs(unsigned long now)
{
    if (!bulbActive) {
        return bulbLastElapsed;
    }

    if (bulbStartedAt == 0 || now <= bulbStartedAt) {
        return 0;
    }

    const unsigned long effectiveNow = (bulbStopIssued && bulbStopAt != 0 && now >= bulbStopAt) ? bulbStopAt : now;
    return (effectiveNow > bulbStartedAt) ? (effectiveNow - bulbStartedAt) : 0;
}

bool Input::isConnected()
{
    const Status status = RemoteStatus::access()->get();
    return status == Status::READY ||
           status == Status::FOCUS_ACQUIRED ||
           status == Status::SHUTTER;
}

void Input::cancelActiveCapture()
{
    if ((bulbActive && !bulbStopIssued) || (timerActive && !timerStopIssued)) {
        _camera_ref->tapTrigger();
    }

    bulbActive = false;
    bulbStopIssued = false;
    bulbStartedAt = 0;
    bulbStopAt = 0;
    bulbLastElapsed = 0;
    timerActive = false;
    timerStopIssued = false;
    timerStartedAt = 0;
}

void Input::setMode(CaptureMode newMode)
{
    if (currentMode == newMode) {
        return;
    }

    cancelActiveCapture();
    currentMode = newMode;

    if (currentMode != CaptureMode::TIMER) {
        timerPreset = 0;
    }

    if (currentMode != CaptureMode::BULB) {
        bulbStopIssued = false;
        bulbStartedAt = 0;
        bulbStopAt = 0;
        bulbLastElapsed = 0;
    }
}

void Input::cycleMode()
{
    switch (currentMode) {
        case CaptureMode::NORMAL:
            setMode(CaptureMode::BULB);
            break;
        case CaptureMode::BULB:
            setMode(CaptureMode::TIMER);
            break;
        case CaptureMode::TIMER:
            setMode(CaptureMode::NORMAL);
            break;
    }
}

void Input::handleModeClickAction()
{
    if (currentMode == CaptureMode::TIMER && modeClickCount == 3) {
        timerPreset = (timerPreset >= TIMER_STEP_MS) ? (timerPreset - TIMER_STEP_MS) : 0;
    } else if (modeClickCount == 1) {
        cycleMode();
    }

    modeClickCount = 0;
    modeClickWindowStart = 0;
}

void Input::startBulb(unsigned long now)
{
    if (!isConnected() || bulbActive) {
        return;
    }

    if (_camera_ref->tapTrigger()) {
        bulbActive = true;
        bulbStopIssued = false;
        bulbStartedAt = now;
        bulbStopAt = 0;
        bulbLastElapsed = 0;
    }
}

void Input::stopBulb()
{
    if (!bulbActive || bulbStopIssued) {
        return;
    }

    const unsigned long now = millis();

    if (_camera_ref->tapTrigger()) {
        bulbStopIssued = true;
        bulbStopAt = now + BULB_STOP_DELAY_MS;
    }
}

void Input::startTimer(unsigned long now)
{
    if (!isConnected() || timerActive || timerPreset == 0) {
        return;
    }

    if (_camera_ref->tapTrigger()) {
        timerActive = true;
        timerStopIssued = false;
        timerStartedAt = now + TIMER_START_DELAY_MS;
    }
}

void Input::stopTimer()
{
    if (!timerActive || timerStopIssued) {
        return;
    }

    if (_camera_ref->tapTrigger()) {
        timerStopIssued = true;
    }
}

void Input::process(unsigned long now)
{
    if (bulbActive && bulbStopIssued && bulbStopAt != 0 && now >= bulbStopAt) {
        bulbLastElapsed = (bulbStartedAt != 0 && bulbStopAt > bulbStartedAt) ? (bulbStopAt - bulbStartedAt) : 0;
        bulbActive = false;
        bulbStopIssued = false;
        bulbStartedAt = 0;
        bulbStopAt = 0;
    }

    if (timerActive && !timerStopIssued && timerRemainingMs(now) <= TIMER_STOP_EARLY_MS) {
        stopTimer();
    }

    if (timerActive && timerStopIssued && timerRemainingMs(now) == 0) {
        timerActive = false;
        timerStopIssued = false;
        timerStartedAt = 0;
    }

    if (!isConnected()) {
        bulbActive = false;
        bulbStopIssued = false;
        bulbStartedAt = 0;
        bulbStopAt = 0;
        bulbLastElapsed = 0;
        timerActive = false;
        timerStopIssued = false;
        timerStartedAt = 0;
    }

    // читаем сырье
    const bool sRaw = readButtonActiveLow(PIN_BTN_SHUTTER);
    const bool fRaw = readButtonActiveLow(PIN_BTN_FOCUS);
    const bool c1Raw = readButtonActiveLow(PIN_BTN_C1);
    const bool mRaw = readButtonActiveLow(PIN_BTN_MODE);
    const bool pRaw = readButtonActiveLow(PIN_BTN_PAIR);

    // debounce shutter
    static bool sStable = false;
    if (sRaw != lastShutter) { lastDebounceShutter = now; lastShutter = sRaw; }
    if ((now - lastDebounceShutter) > DEBOUNCE_MS) sStable = sRaw;

    // debounce focus
    static bool fStable = false;
    if (fRaw != lastFocus) { lastDebounceFocus = now; lastFocus = fRaw; }
    if ((now - lastDebounceFocus) > DEBOUNCE_MS) fStable = fRaw;

    // debounce C1
    static bool c1Stable = false;
    if (c1Raw != lastC1) { lastDebounceC1 = now; lastC1 = c1Raw; }
    if ((now - lastDebounceC1) > DEBOUNCE_MS) c1Stable = c1Raw;

    // debounce mode
    static bool mStable = false;
    if (mRaw != lastMode) { lastDebounceMode = now; lastMode = mRaw; }
    if ((now - lastDebounceMode) > DEBOUNCE_MS) mStable = mRaw;

    // debounce pair
    static bool pStable = false;
    if (pRaw != lastPair) { lastDebouncePair = now; lastPair = pRaw; }
    if ((now - lastDebouncePair) > DEBOUNCE_MS) pStable = pRaw;

    // --- Pair button: hold 3s ---
    static bool prevP = false;

    if (pStable)
    {
        if (!prevP)
        {
            pairHoldStart = now;
            pairingTriggered = false;
#if CFG_DEBUG
            Serial.println("PAIR BUTTON DOWN");
#endif
        }

        if (!pairingTriggered && (now - pairHoldStart) > PAIR_HOLD_MS)
        {
            pairingTriggered = true;
            Serial.println("PAIR BUTTON HOLD -> pairing_mode=ON");

            BLEHandler::setPairingMode(true, true);
            digitalWrite(PIN_LED_PAIR, HIGH);
        }
    }
    else
    {
        if (prevP)
        {
#if CFG_DEBUG
            Serial.println("PAIR BUTTON UP");
#endif
        }

        pairHoldStart = 0;
        pairingTriggered = false;
        digitalWrite(PIN_LED_PAIR, LOW);
    }

    prevP = pStable;

    static bool prevC1 = false;
    if (c1Stable && !prevC1 && isConnected())
    {
        _camera_ref->c1(true);
        digitalWrite(PIN_LED_MODE, HIGH);
    }
    if (!c1Stable && prevC1 && isConnected())
    {
        _camera_ref->c1(false);
        digitalWrite(PIN_LED_MODE, LOW);
    }
    prevC1 = c1Stable;

    // --- Focus (hold) ---
    static bool prevF = false;
    if (fStable && !prevF && isConnected()) _camera_ref->focus(true);
    if (!fStable && prevF && isConnected()) _camera_ref->focus(false);
    prevF = fStable;

    static bool prevM = false;
    if (mStable && !prevM)
    {
        modePressStart = now;
        modeHoldHandled = false;
        modeLastRepeatAt = now;
    }
    if (mStable && currentMode == CaptureMode::TIMER && (now - modePressStart) > MODE_HOLD_MS)
    {
        if (!modeHoldHandled || (now - modeLastRepeatAt) >= MODE_REPEAT_MS)
        {
            timerPreset += TIMER_STEP_MS;
            modeHoldHandled = true;
            modeClickCount = 0;
            modeClickWindowStart = 0;
            modeLastRepeatAt = now;
        }
    }
    if (!mStable && prevM)
    {
        if (!modeHoldHandled)
        {
            modeClickCount++;
            modeClickWindowStart = now;
        }

        modePressStart = 0;
        modeHoldHandled = false;
        modeLastRepeatAt = 0;
    }
    if (!mStable && modeClickCount > 0 && (now - modeClickWindowStart) > MODE_CLICK_WINDOW_MS)
    {
        handleModeClickAction();
    }
    prevM = mStable;

    // --- Shutter (press/release) ---
    static bool prevS = false;
    if (sStable && !prevS)
    {
        shutterHoldStart = now;

        switch (currentMode)
        {
            case CaptureMode::NORMAL:
                if (isConnected())
                {
                    _camera_ref->pressTrigger();
                }
                break;

            case CaptureMode::BULB:
                if (bulbActive)
                {
                    stopBulb();
                }
                else
                {
                    startBulb(now);
                }
                break;

            case CaptureMode::TIMER:
                if (!timerActive)
                {
                    startTimer(now);
                }
                break;
        }
    }
    if (!sStable && prevS)
    {
        if (currentMode == CaptureMode::NORMAL && isConnected())
        {
            _camera_ref->releaseTrigger();
        }
        shutterHoldStart = 0;
        canReset = true;
    }
    prevS = sStable;

    // --- Optional: hold SHUTTER 10s early after boot to clear bonds ---
    // (поведение как "сброс пульта")
    if (sStable && shutterHoldStart != 0 && canReset && currentMode == CaptureMode::NORMAL)
    {
        if ((now - shutterHoldStart) > RESET_HOLD_MS && now < RESET_WINDOW_MS)
        {
            if (_resetCallback) _resetCallback();
            canReset = false;
        }
    }
}
