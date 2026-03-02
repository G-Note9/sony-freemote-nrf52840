#include "BLEHandler.h"
#include <cstring>

RemoteStatus *rs = RemoteStatus::access();

// Known camera address (your a6400): D0:40:EF:3B:F6:CA
// In Nordic reports addr is little-endian in peer_addr.addr
static const uint8_t CAM_ADDR[6] = { 0xCA, 0xF6, 0x3B, 0xEF, 0x40, 0xD0 };

static bool addr_is_camera(const ble_gap_evt_adv_report_t *report)
{
    return (memcmp(report->peer_addr.addr, CAM_ADDR, 6) == 0);
}

static void print_addr(const ble_gap_evt_adv_report_t *report)
{
    // print as normal MAC (big-endian)
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             report->peer_addr.addr[5], report->peer_addr.addr[4], report->peer_addr.addr[3],
             report->peer_addr.addr[2], report->peer_addr.addr[1], report->peer_addr.addr[0]);
    Serial.print(buf);
}

bool BLEHandler::InitBLE(BLECamera *newcam)
{
    _attempt_pairing = false;
    _pairing_mode = false;
    _camera_ref = newcam;

    Bluefruit.begin(0, 1);
    Bluefruit.setName("FREEMOTE");

    // Callbacks
    Bluefruit.Scanner.setRxCallback(_scan_callback);
    Bluefruit.Central.setConnectCallback(_connect_callback);
    Bluefruit.Central.setDisconnectCallback(_disconnect_callback);
    Bluefruit.Security.setSecuredCallback(_connection_secured_callback);

    VERIFY(_camera_ref->begin());

    Bluefruit.autoConnLed(false);

    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.setInterval(160, 80);
    Bluefruit.Scanner.useActiveScan(false);
    Bluefruit.Scanner.start(0);

    Serial.println("BLE: scanner started");
    rs->set(Status::CONNECTING);
    return true;
}

void BLEHandler::_scan_callback(ble_gap_evt_adv_report_t *report)
{
    // Manufacturer data buffer
    std::array<uint8_t, 64> data;   // enlarged to avoid missing tag 0x22
    uint8_t bufferSize = 0;

    const bool isKnownCamAddr = addr_is_camera(report);

    // Parse manufacturer data (AD type 0xFF)
    bufferSize = Bluefruit.Scanner.parseReportByType(report, 0xff, data.data(), data.size());

    // If we don't even have manufacturer bytes, still can fallback by MAC (TEMP)
    if (bufferSize == 0)
    {
        if (isKnownCamAddr)
        {
            Serial.print("SCAN: known CAM addr seen (no mfg data). pairing_mode=");
            Serial.println(_pairing_mode ? "YES" : "NO");

            // TEMP POLICY:
            // - connect ONLY in pairing mode, so we behave like JJC
            if (_pairing_mode)
            {
                RemoteStatus::access()->set(Status::FOCUS_ACQUIRED);
                Bluefruit.Central.connect(report);
            }
        }

        Bluefruit.Scanner.resume();
        return;
    }

    // Quick debug: only print for our known camera address to avoid spam
    if (isKnownCamAddr)
    {
        Serial.print("SCAN: addr=");
        print_addr(report);
        Serial.print(" mfg_len=");
        Serial.print(bufferSize);
        Serial.print(" pairing_mode=");
        Serial.println(_pairing_mode ? "YES" : "NO");
    }

    // If manufacturer data is too short for signature, use MAC fallback (TEMP)
    if (bufferSize < CAMERA_MANUFACTURER_LOOKUP.size())
    {
        if (isKnownCamAddr && _pairing_mode)
        {
            Serial.println("SCAN: mfg too short, but known addr -> connect (pairing mode)");
            RemoteStatus::access()->set(Status::FOCUS_ACQUIRED);
            Bluefruit.Central.connect(report);
        }
        Bluefruit.Scanner.resume();
        return;
    }

    // Check Sony camera signature (by manufacturer bytes)
    bool isSonyByMfg = _camera_ref->isCamera(data.data(), bufferSize);

    // If signature doesn't match but address matches, allow as TEMP fallback in pairing mode
    if (!isSonyByMfg)
    {
        if (isKnownCamAddr && _pairing_mode)
        {
            Serial.println("SCAN: mfg signature mismatch, but known addr -> connect (pairing mode)");
            RemoteStatus::access()->set(Status::FOCUS_ACQUIRED);
            Bluefruit.Central.connect(report);
        }

        Bluefruit.Scanner.resume();
        return;
    }

    // We see Sony camera
    RemoteStatus::access()->set(Status::FOCUS_ACQUIRED);

    bool pairingOpen = _camera_ref->pairingStatus(data.data(), bufferSize);
    bool remoteOn = _camera_ref->remoteEnabled(data.data(), bufferSize);

    bool ok_to_connect = false;

    // 1️⃣ Если включён pairing mode на пульте
    if (_pairing_mode)
    {
    // подключаемся ТОЛЬКО если камера реально в pairing screen
        ok_to_connect = pairingOpen;
        _attempt_pairing = pairingOpen;
    }
    else
    {
    // 2️⃣ Нормальный режим

    // подключаемся только если камера НЕ в pairing screen
    // (то есть обычный режим после успешного bond)
        ok_to_connect = !pairingOpen;

        _attempt_pairing = false;
    }

    if (isKnownCamAddr)
    {
        Serial.print("SCAN: sony_by_mfg=YES pairingOpen=");
        Serial.print(pairingOpen ? "YES" : "NO");
        Serial.print(" remoteOn=");
        Serial.print(remoteOn ? "YES" : "NO");
        Serial.print(" ok_to_connect=");
        Serial.println(ok_to_connect ? "YES" : "NO");
    }

    if (ok_to_connect)
    {
        Bluefruit.Central.connect(report);
        return; // <-- важно: не resume после connect
    }

    Bluefruit.Scanner.resume();
}

void BLEHandler::_connect_callback(uint16_t conn_handle)
{
    BLEConnection *conn = Bluefruit.Connection(conn_handle);

    RemoteStatus::access()->set(Status::CONNECTED);

    Serial.print("CONN: connected handle=");
    Serial.println(conn_handle);

    Serial.print("CONN: pairing_mode=");
    Serial.print(_pairing_mode ? "YES" : "NO");
    Serial.print(" attempt_pairing=");
    Serial.println(_attempt_pairing ? "YES" : "NO");

    // In pairing mode ALWAYS request pairing
    if (_pairing_mode || _attempt_pairing)
    {
        Serial.println("CONN: requestPairing()");
        conn->requestPairing();
    }

    // если НЕ pairing_mode и соединение НЕ bonded -> сразу рвём
    if (!_pairing_mode)
    {
    // (проверь компиляцией: есть ли bonded() в твоей версии)
    if (!conn->bonded())
    {
        Serial.println("CONN: not bonded and not pairing_mode -> disconnect");
        conn->disconnect();
        return;
    }
    }
}

void BLEHandler::_disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;

    Serial.print("DISC: reason=0x");
    Serial.println(reason, HEX);

    rs->set(Status::CONNECTION_LOST);
}

void BLEHandler::_connection_secured_callback(uint16_t conn_handle)
{
    BLEConnection *conn = Bluefruit.Connection(conn_handle);

    Serial.print("SEC: secured=");
    Serial.println(conn->secured() ? "YES" : "NO");

    if (!conn->secured())
    {
        Serial.println("SEC: not secure -> requestPairing()");
        conn->requestPairing();
        return;
    }

    Serial.println("SEC: OK -> discover services");

    if (!_camera_ref->discover(conn_handle))
    {
        Serial.println("GATT: discover FAILED");
        rs->set(Status::ERROR);
        return;
    }

    Serial.println("GATT: discover OK -> enable notify");

    if (_camera_ref->enableNotify())
    {
        Serial.println("GATT: notify enabled -> READY");

        _pairing_mode   = false;
        _attempt_pairing = false;
        digitalWrite(PIN_LED_PAIR, LOW);

        Serial.println("MODE: pairing complete -> pairing_mode=OFF");

        rs->set(Status::READY);
    }
    else
    {
        Serial.println("GATT: notify FAILED");
        rs->set(Status::ERROR);
    }
}

void BLEHandler::clearBonds(void)
{
    Serial.println("BOND: clearBonds()");
    Bluefruit.Central.clearBonds();
}

void BLEHandler::setPairingMode(bool enabled, bool clear_bonds)
{
    _pairing_mode = enabled;
    _attempt_pairing = false;

    Serial.print("MODE: pairing_mode=");
    Serial.println(_pairing_mode ? "ON" : "OFF");

    if (clear_bonds)
    {
        clearBonds();
    }

    Bluefruit.Scanner.start(0);
}