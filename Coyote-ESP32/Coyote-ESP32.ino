/** 
 *  ESP32 Coyote Controller with OTA update
 *
 * WARNING: USE AT YOUR OWN RISK
 *
 * The code is provided as-is, with no warranties of any kind. Not suitable for any purpose.
 * Provided as an example and exercise in BLE development only.
 *
 * By default generates a high-frequency wave on port A, and the 'GrainTouch' wave on port B at a power level of 25.
 *
 * Some guardrails have been implemented to limit the maximum power that the Coyote can output, but these can easily be by-passed.
 * The Coyote e-stim power box can generate dangerous power levels under normal usage.
 * See https://www.reddit.com/r/estim/comments/uadthp/dg_lab_coyote_review_by_an_electronics_engineer/ for more details.
 */

#include <vector>

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define CONFIG_BT_NIMBLE_PINNED_TO_CORE
#include <NimBLEDevice.h>


//
// OTA Stuff
//
struct WiFiSSIDandPasswd {
   String ssid;
   String passwd;
};

#include "secrets.h"




static bool hasOTA = false;

bool waitForWiFiConnect(unsigned long timeoutLength = 60000)
{
    unsigned long start = millis();
    while ((!WiFi.status() || WiFi.status() >= WL_DISCONNECTED) && (millis() - start) < timeoutLength) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}


void initOTA()
{
    Serial.println("Starting OTA server");
    WiFi.mode(WIFI_STA);

    int scanResult = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
    if (scanResult == 0) {
        Serial.println("No WiFi networks found. Disbling OTA");
    } else if (scanResult > 0) {
       for (int8_t i = 0; i < scanResult; i++) {
           String ssid;
           int32_t rssi;
           uint8_t encryptionType;
           uint8_t *bssid;
           int32_t channel;
     
           WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, bssid, channel);
           for (auto& it : WifiCredentials) {
               if (ssid == it.ssid) {
                  WiFi.begin(ssid.c_str(), it.passwd.c_str());
                  if (waitForWiFiConnect()) {
                     Serial.printf("Connected to %s. Enabling OTA.", ssid.c_str());
                     hasOTA = true;
                  }
               break;
              }
          }
       }
    }

    if (hasOTA) {
        // Port defaults to 3232
        // ArduinoOTA.setPort(3232);
      
        // Hostname defaults to esp3232-[MAC]
        ArduinoOTA.setHostname("CoyoteESP32");
      
        // No authentication by default
        // ArduinoOTA.setPassword("admin");
      
        // Password can be set with it's md5 value as well
        // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
        // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
      
        ArduinoOTA
          .onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
              type = "sketch";
            else // U_SPIFFS
              type = "filesystem";
      
            // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
            Serial.println("Start updating " + type);
          })
          .onEnd([]() {
            Serial.println("\nEnd");
          })
          .onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
          })
          .onError([](ota_error_t error) {
            Serial.printf("Error[%u]: ", error);
            if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
            else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
            else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
            else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
            else if (error == OTA_END_ERROR) Serial.println("End Failed");
          });
      
        ArduinoOTA.begin();
      
        Serial.println("OTA Ready");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    }
}


//
// Coyote BLE Stuff
//

static NimBLEAdvertisedDevice* myCoyote;
NimBLERemoteCharacteristic* charPower = nullptr;
NimBLERemoteCharacteristic* charWaveA = nullptr;
NimBLERemoteCharacteristic* charWaveB = nullptr;


// Power and waveform encodings, transmitted LSB first 3 bytes only
static struct CFGval {
    uint32_t   step    :  8;
    uint32_t   maxPwr  : 11;
    uint32_t   rsvd    : 13; 
} gCfg = {7, 2000, 0};

static struct PowerVal {
    // Maximum power level allowed to be specified
    static const uint32_t MAXPOWER = 100;

    uint32_t  B    : 11;
    uint32_t  A    : 11;
    uint32_t  rsvd : 10;

    // Power values as number of steps (i.e. what is displayed on the app)
    PowerVal(uint8_t a, uint8_t b)
    : B(((a < 100) ? b : MAXPOWER) * gCfg.step)
    , A(((a < 100) ? a : MAXPOWER) * gCfg.step)
    , rsvd(0)
    {}

    // Values are sent in little-endian order
    operator uint8_t*() const
      {
          return (uint8_t*) this;
      }
} gPower(0, 0);

struct WaveVal {
    uint32_t   x    :  5;
    uint32_t   y    : 10;
    uint32_t   z    :  5;
    uint32_t   rsvd : 12; 

    WaveVal(uint8_t X, uint16_t Y, uint8_t Z)
    : x(X)
    , y(Y)
    , z(Z)
    , rsvd(0)
    {}

    // Construct a wave from observed transmitted values, in transmit order
    WaveVal(std::vector<uint8_t> bytes)
    {
        // Make sure there are 3 bytes
        while (bytes.size() < 3) bytes.push_back(0x00);

        auto b = (uint8_t*) this;
        for (unsigned int i = 0; i < 3; i++) b[i] = bytes[i];
    }

    // Values are sent in little-endian order
    operator uint8_t*() const
      {
          return (uint8_t*) this;
      }
};


static enum {DISCONNECTED, CONNECT, CONNECTED} connState = DISCONNECTED;


void endScanCB(NimBLEScanResults res)
{
}
    

class ClientCallbacks : public NimBLEClientCallbacks
{
    void onConnect(NimBLEClient* pClient)
    {
        Serial.println("Connected");
    }

    void onDisconnect(NimBLEClient* pClient)
    {
        Serial.print(pClient->getPeerAddress().toString().c_str());
        Serial.println(" Disconnected - Restarting scan");

        connState = DISCONNECTED;

        NimBLEDevice::getScan()->start(0 /* forever */, endScanCB);
    }

    /** Called when the peripheral requests a change to the connection parameters.
     *  Return true to accept and apply them or false to reject and keep
     *  the currently used parameters. Default will return true.
     */
    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params)
    {
        return true;
    }
} clientCB;


/** Define a class to handle the callbacks when advertisments are received */
class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks
{
    void onResult(NimBLEAdvertisedDevice* advertisedDevice)
    {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice->toString().c_str());  

        // The Coyote does not advertise its services, so connect by name and MAC address 
        if (advertisedDevice->getName() == "D-LAB ESTIM01" && advertisedDevice->getAddress().equals(MAC_ADDRESS))
        {
            myCoyote = advertisedDevice;

            /** stop scan before connecting */
            NimBLEDevice::getScan()->stop();
            connState = CONNECT;
        }
    }
};


void notifyBattery(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
    uint8_t level = pData[0];
    Serial.printf("Battery Level = %d%%\n", level);
}

void notifyPower(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
    gPower = *((PowerVal*) pData);
    Serial.printf("Power Setting  A:%3d  B:%3d\n", gPower.A/gCfg.step, gPower.B/gCfg.step);
}

void notifyUnidentified(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify)
{
    Serial.printf("Notification from unidentified attribute: L=%ld", length);
    for (unsigned int i = 0; i < length; i++) Serial.printf(" %02x", pData[i]);
    Serial.printf("\n");    
}

/** Handles the provisioning of clients and connects / interfaces with the server */
bool connectToServer() {
    NimBLEClient* pClient = nullptr;

    /** Check if we have a client we should reuse first **/
    if (NimBLEDevice::getClientListSize()) {
        /** Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(myCoyote->getAddress());
        if(pClient){
            if(!pClient->connect(myCoyote, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        }
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
        else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    /** No client to reuse? Create a new one. */
    if (!pClient) {
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();

        Serial.println("New client created");

        pClient->setClientCallbacks(&clientCB, false);
        /** Set initial connection parameters: These settings are 15ms interval, 0 latency, 120ms timout.
         *  These settings are safe for 3 clients to connect reliably, can go faster if you have less
         *  connections. Timeout should be a multiple of the interval, minimum is 100ms.
         *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0 latency, 51 * 10ms = 510ms timeout
         */
        pClient->setConnectionParams(12,12,0,51);
        /** Set how long we are willing to wait for the connection to complete (seconds), default is 30. */
        pClient->setConnectTimeout(30);


        if (!pClient->connect(myCoyote)) {
            /** Created a client but failed to connect, don't need to keep it as it has no data */
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected()) {
        if (!pClient->connect(myCoyote)) {
            Serial.println("Failed to connect");
            return false;
        }
    }

    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());

    pClient->discoverAttributes();    

    /** Now we can read/write/subscribe the charateristics of the services we are interested in */
    
    //
    // Reported services and characteristics
    //
    // Service: '0x1800'
    //     Characteristic: '0x2a00'
    //     Characteristic: '0x2a01'
    //     Characteristic: '0x2a04'
    //     Characteristic: '0x2aa6'
    // Service: '0x1801'
    // Service: '955a180a-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1500-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1501-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1502-0fe2-f5aa-a094-84b8d4f3e8ad'
    // Service: '955a180b-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1503-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1504-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1505-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1506-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1507-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1508-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a1509-0fe2-f5aa-a094-84b8d4f3e8ad'
    // Service: '955a180c-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a150a-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a150b-0fe2-f5aa-a094-84b8d4f3e8ad'
    //     Characteristic: '955a150c-0fe2-f5aa-a094-84b8d4f3e8ad'
    // Service: '8e400001-f315-4f60-9fb8-838830daea50'
    //     Characteristic: '8e400001-f315-4f60-9fb8-838830daea50'
    //

#ifdef REPORT_ALL_SERVICES_AND_ATTRIBUTES
    for (auto it : *(pClient->getServices(true))) {
       Serial.printf(".   Service: '%s'\n", it->getUUID().toString().c_str());
       for (auto ch : *(it->getCharacteristics(true))) {
          Serial.printf(".     Characteristic: '%s'\n", ch->getUUID().toString().c_str()); 
       }
    }
#endif

    NimBLERemoteService *pSvc = nullptr;

    pSvc = pClient->getService("955A180A-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (pSvc == NULL) {
      Serial.printf("Cannot find Battery service.\n");
      return false;
    }
    
    auto firmware = pSvc->getCharacteristic("955A1501-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (firmware != NULL) {
        auto fw = firmware->readValue().data();
        Serial.printf("Firmware Version %02x.%02x\n", fw[0], fw[1]);
    }

    auto battery  = pSvc->getCharacteristic("955A1500-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (battery != NULL) battery->subscribe(true, notifyBattery, false);
    
    pSvc = pClient->getService("955A180B-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (pSvc == NULL) {
      Serial.printf("Cannot find power service.\n");
      return false;
    }
  
    auto cfg = pSvc->getCharacteristic("955A1507-0FE2-F5AA-A094-84B8D4F3E8AD");
    charPower = pSvc->getCharacteristic("955A1504-0FE2-F5AA-A094-84B8D4F3E8AD");
    charWaveA = pSvc->getCharacteristic("955A1506-0FE2-F5AA-A094-84B8D4F3E8AD");
    charWaveB = pSvc->getCharacteristic("955A1505-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (cfg == NULL || charPower == NULL || charWaveA == NULL || charWaveB == NULL) {
        Serial.printf("Cannot find all power generator characteristics.\n");
        return false;
    }

    gCfg = *((CFGval*) cfg->readValue().data());
    Serial.printf("Max Power: %d/%d = %d\n", gCfg.maxPwr, gCfg.step, gCfg.maxPwr/gCfg.step);

    charPower->subscribe(true, notifyPower, false);
    
    uint8_t zeroes[3] = {0x00, 0x00, 0x00};
    charPower->writeValue(zeroes, 3);
    charWaveA->writeValue(zeroes, 3);
    charWaveB->writeValue(zeroes, 3);

    pSvc = pClient->getService("955A180C-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (pSvc == NULL) {
      Serial.printf("Cannot find Unidentified service.\n");
      return false;
    }
    
    auto unidentified = pSvc->getCharacteristic("955A150B-0FE2-F5AA-A094-84B8D4F3E8AD");
    if (unidentified != NULL) unidentified->subscribe(true, notifyUnidentified, false);
    else {
      Serial.printf("Cannot find Unidentified attribute.\n");
    }
    

    Serial.println("Connected to device!");
    
    return true;
}


void initCoyote()
{
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::init("");

    /** Set the IO capabilities of the device, each option will trigger a different pairing method.
     *  BLE_HS_IO_KEYBOARD_ONLY    - Passkey pairing
     *  BLE_HS_IO_DISPLAY_YESNO   - Numeric comparison pairing
     *  BLE_HS_IO_NO_INPUT_OUTPUT - DEFAULT setting - just works pairing
     */
    //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY); // use passkey
    //NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO); //use numeric comparison

    /** 2 different ways to set security - both calls achieve the same result.
     *  no bonding, no man in the middle protection, secure connections.
     *
     *  These are the default values, only shown here for demonstration.
     */
    //NimBLEDevice::setSecurityAuth(false, false, true);
    NimBLEDevice::setSecurityAuth(/*BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM |*/ BLE_SM_PAIR_AUTHREQ_SC);

    /** Optional: set the transmit power, default is 3db */
#ifdef ESP_PLATFORM
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
#else
    NimBLEDevice::setPower(9); /** +9db */
#endif

    /** create new scan */
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());

    /** Set scan interval (how often) and window (how long) in milliseconds */
    pScan->setInterval(45);
    pScan->setWindow(15);

    /** Active scan will gather scan response data from advertisers
     *  but will use more energy from both devices
     */
    pScan->setActiveScan(true);
    
    /** Start scanning for advertisers for the scan time specified (in seconds) 0 = forever
     *  Optional callback for when scanning stops.
     */
    pScan->start(0 /* forever */, endScanCB);
}


void setPower(uint8_t powA, uint8_t powB)
{
    const bool noResponse = false;

    charPower->writeValue(PowerVal(powA, powB), 3, noResponse);
}


void setWave(NimBLERemoteCharacteristic* waveChar, struct WaveVal &val)
{
    const bool noResponse = false;
    waveChar->writeValue(val, 3, noResponse);
}


void setup() {
    Serial.begin(115200);

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);

    initOTA();
    initCoyote();
}


void setLED(int to = -1 /*Toggle*/)
{
   static bool ledState = true;

   if (to < 0) ledState = !ledState;
   else ledState = to;
   digitalWrite(LED_BUILTIN, ledState);
}

std::vector<WaveVal> waveA = {WaveVal(1, 9, 16)};
std::vector<WaveVal> waveB =  // "GrainTouch" pre-defined waveform
                             {WaveVal(std::vector<uint8_t>({0xE1, 0x03, 0x00})),
                              WaveVal(std::vector<uint8_t>({0xE1, 0x03, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0xA1, 0x04, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0xC1, 0x05, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0x01, 0x07, 0x00})),
                              WaveVal(std::vector<uint8_t>({0x21, 0x01, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0x61, 0x01, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0xA1, 0x01, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0x01, 0x02, 0x00})),
                              WaveVal(std::vector<uint8_t>({0x01, 0x02, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0x81, 0x02, 0x0A})),
                              WaveVal(std::vector<uint8_t>({0x21, 0x03, 0x0A}))};

auto waveAIter = waveA.begin();
auto waveBIter = waveB.begin();

unsigned long stamp = 0;

void loop()
{ 
    auto now = millis();

    switch (connState) {
      case DISCONNECTED:
        // OTA updates can only happen when we are disconnected
        if (hasOTA) ArduinoOTA.handle();

        // Flash the on-board LED when we are disconnected
        if (now - stamp >= 500) {
            setLED();
            stamp = now;
        }
        break;

      case CONNECT:
        setLED(1);
        if (connectToServer()) {
            connState = CONNECTED;
            setLED(0);
            setPower(25, 25);
        } else {
            Serial.println("Failed to connect, re-starting scan");
            connState = DISCONNECTED;
            NimBLEDevice::getScan()->start(0 /* forever */, endScanCB);
        }
        stamp     = now;
        break;

      case CONNECTED:
        if (now - stamp >= 99) {

           setWave(charWaveA, *waveAIter++);
           if (waveAIter == waveA.end()) waveAIter = waveA.begin();                

           setWave(charWaveB, *waveBIter++);
           if (waveBIter == waveB.end()) waveBIter = waveB.begin();

           stamp = now;
        }  
        break;
    }
}
