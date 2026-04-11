#include "BLECamera.h"
#include "bluefruit.h"

#if CFG_DEBUG
#define CAM_TRACE_PRINT(...) Serial.print(__VA_ARGS__)
#define CAM_TRACE_PRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define CAM_TRACE_PRINT(...) do {} while (0)
#define CAM_TRACE_PRINTLN(...) do {} while (0)
#endif

BLECamera::BLECamera(void)
    : BLEClientService("8000FF00-FF00-FFFF-FFFF-FFFFFFFFFFFF"),
      _remoteCommand(0xFF01),
      _remoteNotify(0xFF02),
      _shutterStatus(0),
      _focusStatus(0),
      _recordingStatus(0),
      _afOnHeld(false),
      _asyncTriggerState(AsyncTriggerState::Idle),
      _asyncTriggerDeadlineMs(0)
{
    rs = RemoteStatus::access();
}

void camera_notify_cb(BLEClientCharacteristic *chr, uint8_t *data, uint16_t len)
{
    BLECamera &svc = (BLECamera &)chr->parentService();
    svc._handle_camera_notification(data, len);
}

bool BLECamera::begin(void)
{
    // Invoke base class begin()
    VERIFY(BLEClientService::begin());

    _remoteCommand.begin(this);

    _remoteNotify.setNotifyCallback(camera_notify_cb);
    _remoteNotify.begin(this);

    return true;
}

bool BLECamera::discover(uint16_t conn_handle)
{
    // Call Base class discover
    VERIFY(BLEClientService::discover(conn_handle));

    _conn_hdl = BLE_CONN_HANDLE_INVALID; // make as invalid until we found all chars

    // // Discover all characteristics
    Bluefruit.Discovery.discoverCharacteristic(conn_handle, _remoteCommand, _remoteNotify);

    VERIFY(_remoteCommand.discovered() && _remoteNotify.discovered());

    _conn_hdl = conn_handle;

    return true;
}

void BLECamera::_handle_camera_notification(uint8_t *data, uint16_t len)
{

#if CFG_DEBUG
    Serial.print("RX: ");
    for (int i = 0; i < len; i++)
    {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
#endif

    if (len == 3)
    {
        if (data[0] == 0x02)
        {
            switch (data[1])
            {
            case 0x3F:
                _focusStatus = data[2];

                if (_focusStatus == 0x20)
                {
                    rs->set(Status::FOCUS_ACQUIRED);
                }
                else
                {
                    rs->set(Status::READY);
                }

                break;

            case 0xA0:
                _shutterStatus = data[2];

                if (_shutterStatus == 0x20)
                {
                    rs->set(Status::SHUTTER);
                }
                else
                {
                    rs->set(Status::READY);
                }

                break;

            case 0xD5:
                _recordingStatus = data[2];
                break;
            }
        }
    }
}

bool BLECamera::enableNotify(void)
{
    return _remoteNotify.enableNotify();
}

bool BLECamera::disableNotify(void)
{
    return _remoteNotify.disableNotify();
}

bool BLECamera::pressTrigger(void)
{
    uint32_t startTime = millis();

    if (!_afOnHeld)
    {
        _focusStatus = 0x00;

        CAM_TRACE_PRINTLN("TRIGGER: SHUTTER HALF DOWN");
        _remoteCommand.write16_resp(SHUTTER_HALF_DOWN);

        while (_focusStatus != 0x20)
        {
            yield();

            if ((millis() - startTime) >= 1000)
            {
                Serial.println("TRIGGER: focus wait timeout");
                break;
            }
        }
    }
    else
    {
        CAM_TRACE_PRINTLN("TRIGGER: AF already held, skip focus phase");
    }

    _shutterStatus = 0x00;

    CAM_TRACE_PRINTLN("TRIGGER: SHUTTER FULL DOWN");
    _remoteCommand.write16_resp(SHUTTER_FULL_DOWN);

    while (_shutterStatus != 0x20)
    {
        yield();

        if ((millis() - startTime) >= 1500)
        {
            Serial.println("TRIGGER: shutter wait timeout");
            break;
        }
    }

    return true;
}

bool BLECamera::releaseTrigger(void)
{
    CAM_TRACE_PRINTLN("TRIGGER: SHUTTER HALF UP");
    _remoteCommand.write16_resp(SHUTTER_HALF_UP);

    delay(10);

    CAM_TRACE_PRINTLN("TRIGGER: SHUTTER FULL UP");
    _remoteCommand.write16_resp(SHUTTER_FULL_UP);

    return true;
}

bool BLECamera::tapTrigger(void)
{
    if (_asyncTriggerState != AsyncTriggerState::Idle) {
        return false;
    }

    if (!pressTrigger()) {
        return false;
    }

    delay(50);
    return releaseTrigger();
}

bool BLECamera::startTriggerTapAsync(void)
{
    if (_asyncTriggerState != AsyncTriggerState::Idle) {
        return false;
    }

    CAM_TRACE_PRINTLN("TRIGGER ASYNC: SHUTTER HALF DOWN");
    _remoteCommand.write16_resp(SHUTTER_HALF_DOWN);
    _asyncTriggerState = AsyncTriggerState::WaitTapFullDown;
    _asyncTriggerDeadlineMs = millis() + 20UL;
    return true;
}

void BLECamera::serviceAsync(void)
{
    if (_asyncTriggerState == AsyncTriggerState::Idle) {
        return;
    }

    const unsigned long now = millis();

    switch (_asyncTriggerState)
    {
        case AsyncTriggerState::WaitTapFullDown:
            if (now >= _asyncTriggerDeadlineMs)
            {
                CAM_TRACE_PRINTLN("TRIGGER ASYNC: SHUTTER FULL DOWN");
                _remoteCommand.write16_resp(SHUTTER_FULL_DOWN);
                _asyncTriggerState = AsyncTriggerState::WaitTapHalfUp;
                _asyncTriggerDeadlineMs = now + 50UL;
            }
            break;

        case AsyncTriggerState::WaitTapHalfUp:
            if (now >= _asyncTriggerDeadlineMs)
            {
                CAM_TRACE_PRINTLN("TRIGGER ASYNC: SHUTTER HALF UP");
                _remoteCommand.write16_resp(SHUTTER_HALF_UP);
                _asyncTriggerState = AsyncTriggerState::WaitTapFullUp;
                _asyncTriggerDeadlineMs = now + 10UL;
            }
            break;

        case AsyncTriggerState::WaitTapFullUp:
            if (now >= _asyncTriggerDeadlineMs)
            {
                CAM_TRACE_PRINTLN("TRIGGER ASYNC: SHUTTER FULL UP");
                _remoteCommand.write16_resp(SHUTTER_FULL_UP);
                _asyncTriggerState = AsyncTriggerState::Idle;
            }
            break;

        case AsyncTriggerState::Idle:
        default:
            break;
    }
}

bool BLECamera::isAsyncActive(void) const
{
    return _asyncTriggerState != AsyncTriggerState::Idle;
}

void BLECamera::afOn(bool press)
{
    _afOnHeld = press;

    uint16_t cmd = press ? AFON_DOWN : AFON_UP;

    CAM_TRACE_PRINT("CMD AF-ON: 0x");
    CAM_TRACE_PRINTLN(cmd, HEX);

    _remoteCommand.write16_resp(cmd);
}

void BLECamera::c1(bool press)
{
    uint16_t cmd = press ? C1_DOWN : C1_UP;

    CAM_TRACE_PRINT("CMD C1: 0x");
    CAM_TRACE_PRINTLN(cmd, HEX);

    _remoteCommand.write16_resp(cmd);
}

void BLECamera::focus(bool f)
{
    if (f)
    {
        CAM_TRACE_PRINTLN("FOCUS BTN -> AF-ON DOWN");
        afOn(true);
    }
    else
    {
        CAM_TRACE_PRINTLN("FOCUS BTN -> AF-ON UP");
        afOn(false);
    }
}

void BLECamera::release(void)
{
    _remoteCommand.write16_resp(SHUTTER_HALF_UP);

    delay(10);

    _remoteCommand.write16_resp(SHUTTER_FULL_UP);
}

// is_camera returns true if this is a sony cam
bool BLECamera::isCamera(uint8_t* data, uint8_t len)
{
    if (len < CAMERA_MANUFACTURER_LOOKUP.size())
        return false;

    return std::equal(
        CAMERA_MANUFACTURER_LOOKUP.begin(),
        CAMERA_MANUFACTURER_LOOKUP.end(),
        data
    );
}

// pairing_status returns true if camera is open for pairing, false otherwise
bool BLECamera::pairingStatus(uint8_t* data, uint8_t len)
{
    for (int i = 0; i < len - 1; i++)
    {
        if (data[i] == 0x22)
        {
            uint8_t flags = data[i + 1];

            // Pairing screen = bit 0x40 set
            return (flags & 0x40);
        }
    }
    return false;
}

bool BLECamera::remoteEnabled(uint8_t* data, uint8_t len)
{
    for (int i = 0; i < len - 1; i++)
    {
        if (data[i] == 0x22)
        {
            return true;  // если тег есть — remote включен
        }
    }
    return false;
}

// This just sends the commands in order to test, doesn't work if the camera struggles to focus.
// bool BLECamera::_ignorantTrigger(void)
// {
//
//     // Half down
//     _remoteCommand.write16_resp(0x0701);
//
//     // Full down
//     _remoteCommand.write16_resp(0x0901);
//
//     // Release full press, keep half press
//     _remoteCommand.write16_resp(0x0601);
//
//     // Release half press
//     _remoteCommand.write16_resp(0x0801);
//
//     return true;
// }
