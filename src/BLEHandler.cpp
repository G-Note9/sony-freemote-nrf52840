#include "BLEHandler.h"

RemoteStatus *rs = RemoteStatus::access();

static void print_addr(const ble_gap_evt_adv_report_t *report)
{
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
    _reconnect_block_until = 0;
    _camera_ref = newcam;

    Bluefruit.begin(0, 1);
    Bluefruit.setName("FREEMOTE");

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
    if (millis() < _reconnect_block_until)
    {
        Bluefruit.Scanner.resume();
        return;
    }

    std::array<uint8_t, 64> data;
    uint8_t bufferSize = Bluefruit.Scanner.parseReportByType(report, 0xff, data.data(), data.size());

    // Нет manufacturer data -> не наш кандидат
    if (bufferSize == 0)
    {
        Bluefruit.Scanner.resume();
        return;
    }

    // Слишком короткий manufacturer data -> не наш кандидат
    if (bufferSize < CAMERA_MANUFACTURER_LOOKUP.size())
    {
        Bluefruit.Scanner.resume();
        return;
    }

    // Проверяем Sony camera по manufacturer data
    if (!_camera_ref->isCamera(data.data(), bufferSize))
    {
        Bluefruit.Scanner.resume();
        return;
    }

    bool pairingOpen = _camera_ref->pairingStatus(data.data(), bufferSize);
    bool remoteOn    = _camera_ref->remoteEnabled(data.data(), bufferSize);

    bool ok_to_connect = false;

    if (_pairing_mode)
    {
        // pairing mode -> только если камера реально в pairing screen
        ok_to_connect = pairingOpen;
        _attempt_pairing = pairingOpen;
    }
    else
    {
        // normal mode -> только если камера НЕ в pairing screen
        ok_to_connect = !pairingOpen;
        _attempt_pairing = false;
    }

    Serial.print("SCAN: addr=");
    print_addr(report);
    Serial.print(" sony_by_mfg=YES pairingOpen=");
    Serial.print(pairingOpen ? "YES" : "NO");
    Serial.print(" remoteOn=");
    Serial.print(remoteOn ? "YES" : "NO");
    Serial.print(" pairing_mode=");
    Serial.print(_pairing_mode ? "YES" : "NO");
    Serial.print(" ok_to_connect=");
    Serial.println(ok_to_connect ? "YES" : "NO");

    if (ok_to_connect)
    {
        Serial.println("SCAN: connecting...");
        Bluefruit.Central.connect(report);
        return;
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

    // В pairing mode явно инициируем pairing
    if (_pairing_mode || _attempt_pairing)
    {
        Serial.println("CONN: requestPairing()");
        conn->requestPairing();
        return;
    }

    // В normal mode не держим небондированное соединение
    if (!conn->bonded())
    {
        Serial.println("CONN: not bonded and not pairing_mode -> disconnect");
        conn->disconnect();
        return;
    }
}

void BLEHandler::_disconnect_callback(uint16_t conn_handle, uint8_t reason)
{
    (void)conn_handle;

    Serial.print("DISC: reason=0x");
    Serial.println(reason, HEX);

    _reconnect_block_until = millis() + 3000;

    rs->set(Status::CONNECTION_LOST);
}

void BLEHandler::_connection_secured_callback(uint16_t conn_handle)
{
    BLEConnection *conn = Bluefruit.Connection(conn_handle);

    Serial.print("SEC: secured=");
    Serial.println(conn->secured() ? "YES" : "NO");

    if (!conn->secured())
    {
        if (_pairing_mode || _attempt_pairing)
        {
            Serial.println("SEC: not secure in pairing mode -> requestPairing()");
            conn->requestPairing();
        }
        else
        {
            Serial.println("SEC: not secure in normal mode -> disconnect");
            conn->disconnect();
        }
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

        _pairing_mode = false;
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

    _reconnect_block_until = 0;
    rs->set(enabled ? Status::PAIRING : Status::CONNECTING);
    Bluefruit.Scanner.start(0);
}
