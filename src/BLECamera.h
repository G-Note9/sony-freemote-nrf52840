#pragma once

#include "bluefruit_common.h"
#include "BLECharacteristic.h"
#include "BLEClientCharacteristic.h"
#include "BLEClientService.h"
#include <array>
#include <algorithm>
#include "RemoteStatus.h"

// Two-stage shutter sequence:
// half down -> full down -> half up -> full up.
constexpr uint16_t SHUTTER_HALF_UP = 0x0601;
constexpr uint16_t SHUTTER_HALF_DOWN = 0x0701;
constexpr uint16_t SHUTTER_FULL_UP = 0x0801;
constexpr uint16_t SHUTTER_FULL_DOWN = 0x0901;
constexpr uint16_t AFON_UP   = 0x1401;
constexpr uint16_t AFON_DOWN = 0x1501;
constexpr uint16_t C1_UP     = 0x2001;
constexpr uint16_t C1_DOWN   = 0x2101;

//0x2D01 = Sony Camera Corporation Identifer
//0x3000 = This is a camera
//0x64 = Protocol version ?
//0x00 = ??
constexpr std::array<uint8_t, 4> CAMERA_MANUFACTURER_LOOKUP = {0x2D, 0x01, 0x03, 0x00};

//ASCII Model Code E-mount cameras 'E1', A-mount 'A1'.
constexpr std::array<uint8_t, 2> CAMERA_MODEL_CODE  = {0x45, 0x31};

//Indicates if camera is open to pair
//0x22 indicate tag, 0xEF pairing (with bluetooth remote), 0x00 end
constexpr std::array<uint8_t, 3> CAMERA_PAIRING_TAG = {0x22, 0xEF, 0x00};

class BLECamera : public BLEClientService
{
public:
    
    BLECamera(void);

    virtual bool begin(void);
    virtual bool discover(uint16_t conn_handle);

    bool enableNotify(void);
    bool disableNotify(void);

    bool pressTrigger(void);
    bool releaseTrigger(void);
    bool tapTrigger(void);
    bool startTriggerTapAsync(void);
    void serviceAsync(void);
    bool isAsyncActive(void) const;
    void focus(bool f);
    void release(void);
    void afOn(bool press);
    void c1(bool press);

    //Faciliates extracting information from manufacturer data
    bool isCamera(uint8_t* data, uint8_t len);
    bool pairingStatus(uint8_t* data, uint8_t len);
    bool remoteEnabled(uint8_t* data, uint8_t len);

    //bool _ignorantTrigger(void);

protected:
    BLEClientCharacteristic _remoteCommand;
    BLEClientCharacteristic _remoteNotify;

    volatile uint8_t _shutterStatus; //A0
    volatile uint8_t _focusStatus; //3F
    volatile uint8_t _recordingStatus; //D5

    void _handle_camera_notification(uint8_t* data, uint16_t len);

    friend void camera_notify_cb(BLEClientCharacteristic* chr, uint8_t* data, uint16_t len);

private:
    RemoteStatus *rs;
    bool _afOnHeld;

    enum class AsyncTriggerState : uint8_t {
        Idle,
        WaitTapFullDown,
        WaitTapHalfUp,
        WaitTapFullUp
    };

    AsyncTriggerState _asyncTriggerState;
    unsigned long _asyncTriggerDeadlineMs;
};
