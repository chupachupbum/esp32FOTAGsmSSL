/*
   esp32 firmware OTA using TinyGsm
   Date: 2022-08-22
   Purpose: Perform an OTA update from a bin located on a webserver, using gsm connection
*/

#ifndef esp32FotaGsmSSL_h
#define esp32FotaGsmSSL_h

#include <Arduino.h>
#include <ArduinoJson.h>

#define TINY_GSM_MODEM_SIM7000
#include <TinyGsmClient.h>

#include "semver/semver.h"

class esp32FotaGsmSSL {
 public:
  esp32FotaGsmSSL(String firwmareType, int firwmareVersion, boolean validate = false, boolean allow_insecure_https = false);
  esp32FotaGsmSSL(String firwmareType, String firmwareSemanticVersion, boolean validate = false, boolean allow_insecure_https = false);
  ~esp32FotaGsmSSL();
  void forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, boolean validate);
  void forceUpdate(String firmwareURL, boolean validate);
  void forceUpdate(boolean validate);
  void execOTA();
  bool execHTTPcheck();
  int getPayloadVersion();
  void getPayloadVersion(char* version_string);
  bool useDeviceID;
  String checkURL;
  bool validate_sig(unsigned char* signature, uint32_t firmware_size);
  void modemRestart();
  void readyUpModem(TinyGsm& modem, const char* apn, const char* user, const char* pass);
  void setModem(TinyGsm& modem, int led, int pwr, int baud, int rx, int tx);

 private:
  String getDeviceID();
  String _firmwareType;
  semver_t _firmwareVersion = {0};
  semver_t _payloadVersion  = {0};
  String _firmwareHost;
  String _firmwareBin;
  int _firmwarePort;
  boolean _check_sig;
  boolean _allow_insecure_https;
  bool checkJSONManifest(JsonVariant JSONDocument);
  void turnModemOn();
  void turnModemOff();
  int _ledPin, _pwrPin, _modemBaud, _modemRX, _modemTX;
  TinyGsm* _modem;
};

#endif
