/*
   esp32 firmware OTA using TinyGsm
   Date: 2022-08-22
   Purpose: Perform an OTA update from a bin located on a webserver, using gsm connection
*/

#include "esp32fota.h"

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Update.h>

#include "ArduinoJson.h"
#include "SSLClient.h"
#include "ca_cert.h"
#include "esp_ota_ops.h"
#include "mbedtls/md.h"
#include "mbedtls/md_internal.h"
#include "mbedtls/pk.h"

esp32FOTA::esp32FOTA(String firmwareType, int firmwareVersion, boolean validate, boolean allow_insecure_https) {
  _firmwareType         = firmwareType;
  _firmwareVersion      = semver_t{firmwareVersion};
  _check_sig            = validate;
  _allow_insecure_https = allow_insecure_https;
  useDeviceID           = false;

  char version_no[256] = {'\0'};  // If we are passed firmwareVersion as an int, we're assuming it's a major version
  semver_render(&_firmwareVersion, version_no);
  log_i("Current firmware version: %s", version_no);
}

esp32FOTA::esp32FOTA(String firmwareType, String firmwareSemanticVersion, boolean validate, boolean allow_insecure_https) {
  if (semver_parse(firmwareSemanticVersion.c_str(), &_firmwareVersion)) {
    log_e("Invalid semver string %s passed to constructor. Defaulting to 0", firmwareSemanticVersion.c_str());
    _firmwareVersion = semver_t{0};
  }

  _firmwareType         = firmwareType;
  _check_sig            = validate;
  _allow_insecure_https = allow_insecure_https;
  useDeviceID           = false;

  char version_no[256] = {'\0'};
  semver_render(&_firmwareVersion, version_no);
  log_i("Current firmware version: %s", version_no);
}

esp32FOTA::~esp32FOTA() {
  semver_free(&_firmwareVersion);
  semver_free(&_payloadVersion);
}

// Check file signature
// https://techtutorialsx.com/2018/05/10/esp32-arduino-mbed-tls-using-the-sha-256-algorithm/
// https://github.com/ARMmbed/mbedtls/blob/development/programs/pkey/rsa_verify.c
bool esp32FOTA::validate_sig(unsigned char *signature, uint32_t firmware_size) {
  int ret = 1;
  mbedtls_pk_context pk;
  mbedtls_md_context_t rsa;

  {  // Open RSA public key:
    File public_key_file = SPIFFS.open("/rsa_key.pub");
    if (!public_key_file) {
      log_e("Failed to open rsa_key.pub for reading");
      return false;
    }
    std::string public_key = "";
    while (public_key_file.available()) {
      public_key.push_back(public_key_file.read());
    }
    public_key_file.close();

    mbedtls_pk_init(&pk);
    if ((ret = mbedtls_pk_parse_public_key(&pk, (unsigned char *)public_key.c_str(), public_key.length() + 1)) != 0) {
      log_e("Reading public key failed\n  ! mbedtls_pk_parse_public_key %d\n\n", ret);
      return false;
    }
  }

  if (!mbedtls_pk_can_do(&pk, MBEDTLS_PK_RSA)) {
    log_e("Public key is not an rsa key -0x%x\n\n", -ret);
    return false;
  }

  const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);

  if (!partition) {
    log_e("Could not find update partition!");
    return false;
  }

  const mbedtls_md_info_t *mdinfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_init(&rsa);
  mbedtls_md_setup(&rsa, mdinfo, 0);
  mbedtls_md_starts(&rsa);

  int bytestoread = SPI_FLASH_SEC_SIZE;
  int bytesread   = 0;
  int size        = firmware_size;

  uint8_t *_buffer = (uint8_t *)malloc(SPI_FLASH_SEC_SIZE);
  if (!_buffer) {
    log_e("malloc failed");
    return false;
  }
  // Serial.printf( "Reading partition (%i sectors, sec_size: %i)\r\n", size, bytestoread );
  while (bytestoread > 0) {
    // Serial.printf( "Left: %i (%i)               \r", size, bytestoread );

    if (ESP.partitionRead(partition, bytesread, (uint32_t *)_buffer, bytestoread)) {
      // Debug output for the purpose of comparing with file
      /*for( int i = 0; i < bytestoread; i++ ) {
        if( ( i % 16 ) == 0 ) {
          Serial.printf( "\r\n0x%08x\t", i + bytesread );
        }
        Serial.printf( "%02x ", (uint8_t*)_buffer[i] );
      }*/

      mbedtls_md_update(&rsa, (uint8_t *)_buffer, bytestoread);

      bytesread = bytesread + bytestoread;
      size      = size - bytestoread;

      if (size <= SPI_FLASH_SEC_SIZE) {
        bytestoread = size;
      }
    } else {
      log_e("partitionRead failed!");
      return false;
    }
  }
  free(_buffer);

  unsigned char *hash = (unsigned char *)malloc(mdinfo->size);
  mbedtls_md_finish(&rsa, hash);

  ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, mdinfo->size, (unsigned char *)signature, 512);

  free(hash);
  mbedtls_md_free(&rsa);
  mbedtls_pk_free(&pk);
  if (ret == 0) {
    return true;
  }

  // overwrite the first few bytes so this partition won't boot!
  ESP.partitionEraseRange(partition, 0, ENCRYPTED_BLOCK_SIZE);

  return false;
}

// OTA Logic
void esp32FOTA::execOTA() {
  int contentLength       = 0;
  bool isValidContentType = false;

  TinyGsmClient client;
  client.init(_modem);
  SSLClient secure_client(&client);
  // http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Don't have this in ssl. TODO: Add similar

  log_i("Connecting to: %s...", _firmwareHost.c_str());

  // Include root_ca in SSLClient style, you need to include a ca_cert.h at the top
  if (!_allow_insecure_https) secure_client.setCACert(root_ca);
  if (!secure_client.connect(_firmwareHost.c_str(), _firmwarePort)) log_i("fail! Retrying...\r\n");
  log_i("OK\r\n");

  // Make a HTTP request:
  secure_client.print(String("GET ") + _firmwareBin + " HTTP/1.1\r\n");
  secure_client.print(String("Host: ") + _firmwareHost + "\r\n");
  secure_client.print("Connection: close\r\n\r\n");

  // Check timeout
  long timeout = millis();
  while (secure_client.available() == 0) {
    if (millis() - timeout > 30000L) {
      Serial.println(">>> Client Timeout!");
      secure_client.stop();
    }
  }

  while (secure_client.available()) {
    String line = secure_client.readStringUntil('\n');
    line.trim();
    // log_i("%s", line);  // Uncomment this to show response header
    line.toLowerCase();
    if (line.startsWith("content-length:")) {
      contentLength = line.substring(line.lastIndexOf(':') + 1).toInt();
    }
    if (line.startsWith("content-type:")) {
      String contentType = line.substring(line.lastIndexOf(':') + 1);
      if (contentType == "application/octet-stream") {
        isValidContentType = true;
      }
    }
    if (line.length() == 0) {
      break;
    }
  }

  // Check what is the contentLength and if content type is `application/octet-stream`
  log_i("contentLength : %i, isValidContentType : %s", contentLength, String(isValidContentType));

  // if( contentLength && isValidContentType ) { // Skip check for content-type, uncomment this to enable
  if (_check_sig) {
    // If firmware is signed, extract signature and decrease content-length by 512 bytes for signature
    contentLength = contentLength - 512;
  }
  if (Update.begin(contentLength)) {
    unsigned char signature[512];
    if (_check_sig) {
      client.readBytes(signature, 512);
    }
    Serial.println("Begin OTA. This may take 2 - 5 mins to complete. Things might be quiet for a while.. Patience!");
    // No activity would appear on the Serial monitor
    // So be patient. This may take 2 - 5mins to complete
    // Especially with GSM, it would take 5mins for real.
    size_t written = Update.writeStream(secure_client);

    if (written == contentLength) {
      Serial.println("Written : " + String(written) + " successfully");
    } else {
      Serial.println("Written only : " + String(written) + "/" + String(contentLength) + ". Retry?");
      // retry??
      // execOTA();
    }
    if (Update.end()) {
      if (_check_sig) {
        if (!validate_sig(signature, contentLength)) {
          const esp_partition_t *partition = esp_ota_get_running_partition();
          esp_ota_set_boot_partition(partition);

          log_e("Signature check failed!");
          secure_client.stop();
          // _modem.gprsDisconnect();
          ESP.restart();
          return;
        } else {
          log_i("Signature OK");
        }
      }
      Serial.println("OTA done!");
      if (Update.isFinished()) {
        Serial.println("Restart ESP device!");
        secure_client.stop();
        ESP.restart();
      } else {
        Serial.println("OTA not finished!");
      }
    } else {
      Serial.println("Error occurred #: " + String(Update.getError()));
    }
  } else {
    Serial.println("Not enough space to begin OTA");
    secure_client.stop();
  }
  // } uncomment for content-type checking
  //   else
  //   {
  //       log_e("There was no content in the response");
  //       http.end();
  //   }
}

bool esp32FOTA::checkJSONManifest(JsonVariant JSONDocument) {
  if (strcmp(JSONDocument["type"].as<const char *>(), _firmwareType.c_str()) != 0) {
    log_i("Payload type in manifest %s doesn't match current firmware %s", JSONDocument["type"].as<const char *>(), _firmwareType.c_str());
    log_i("Doesn't match type: %s", _firmwareType.c_str());
    return false;  // Move to the next entry in the manifest
  }
  log_i("Payload type in manifest %s matches current firmware %s", JSONDocument["type"].as<const char *>(), _firmwareType.c_str());

  semver_free(&_payloadVersion);
  if (JSONDocument["version"].is<uint16_t>()) {
    log_i("JSON version: %d (int)", JSONDocument["version"].as<uint16_t>());
    _payloadVersion = semver_t{JSONDocument["version"].as<uint16_t>()};
  } else if (JSONDocument["version"].is<const char *>()) {
    log_i("JSON version: %s (semver)", JSONDocument["version"].as<const char *>());
    if (semver_parse(JSONDocument["version"].as<const char *>(), &_payloadVersion)) {
      log_e("Invalid semver string received in manifest. Defaulting to 0");
      _payloadVersion = semver_t{0};
    }
  } else {
    log_e("Invalid semver format received in manifest. Defaulting to 0");
    _payloadVersion = semver_t{0};
  }

  char version_no[256] = {'\0'};
  semver_render(&_payloadVersion, version_no);
  log_i("Payload firmware version: %s", version_no);

  if (JSONDocument["url"].is<String>()) {
    // We were provided a complete URL in the JSON manifest - use it
    String url = JSONDocument["url"].as<String>();
    String urlRaw;
    if (url.substring(0, 5) == "https") {
      urlRaw        = url.substring(8);
      _firmwarePort = 443;
    } else {
      urlRaw        = url.substring(7);
      _firmwarePort = 80;
    }
    _firmwareHost = urlRaw.substring(0, urlRaw.indexOf('/'));
    _firmwareBin  = urlRaw.substring(urlRaw.indexOf('/'));

    if (JSONDocument["host"].is<String>())  // If the manifest provides both, warn the user
      log_w("Manifest provides both url and host - Using URL");
  } else if (JSONDocument["host"].is<String>() && JSONDocument["port"].is<uint16_t>() && JSONDocument["bin"].is<String>()) {
    _firmwareHost = JSONDocument["host"].as<String>();
    _firmwarePort = JSONDocument["port"].as<uint16_t>();
    _firmwareBin  = JSONDocument["bin"].as<String>();

  } else {
    // JSON was malformed - no firmware target was provided
    log_e("JSON manifest was missing both 'url' and 'host'/'port'/'bin' keys");
    return false;
  }

  if (semver_compare(_payloadVersion, _firmwareVersion) == 1) {
    return true;
  }
  return false;
}

bool esp32FOTA::execHTTPcheck() {
  String useURL;

  if (useDeviceID) {
    // String deviceID = getDeviceID() ;
    useURL = checkURL + "?id=" + getDeviceID();
  } else {
    useURL = checkURL;
  }

  log_i("Getting HTTP: %s", useURL.c_str());
  log_i("------");

  TinyGsmClient client;
  client.init(_modem);
  SSLClient secure_client(&client);

  String urlRaw;
  int urlPort;
  if (useURL.substring(0, 5) == "https") {
    urlRaw  = useURL.substring(8);
    urlPort = 443;
    if (!_allow_insecure_https) secure_client.setCACert(root_ca);
  } else {
    urlRaw  = useURL.substring(7);
    urlPort = 80;
  }
  // SSLClient connect to the host so we need to separate the host and the path
  String urlHost = urlRaw.substring(0, urlRaw.indexOf('/'));
  String urlPath = urlRaw.substring(urlRaw.indexOf('/'));
  if (!secure_client.connect(urlHost.c_str(), urlPort)) log_i("fail! Retrying...\r\n");
  log_i("OK\r\n");

  // Make a HTTP request:
  secure_client.print(String("GET ") + urlPath + " HTTP/1.1\r\n");
  secure_client.print(String("Host: ") + urlHost + "\r\n");
  secure_client.print("Connection: close\r\n\r\n");

  // TODO: Add check http code
  while (secure_client.connected()) {
    String line = secure_client.readStringUntil('\n');
    if (line == "\r") {
      break;
    }  // if line only contain '\r', it's the end of headers
  }

  long timeout = millis();
  String payload;

  while (secure_client.connected() && millis() - timeout < 30000L) {  // Timeout should be at least 20s for TTGO SIM7000G
    while (secure_client.available()) {
      char c = secure_client.read();
      payload.concat(c);
      timeout = millis();
    }
  }

  int str_len = payload.length() + 1;
  char JSONMessage[str_len];
  payload.toCharArray(JSONMessage, str_len);

  DynamicJsonDocument JSONResult(2048);
  DeserializationError err = deserializeJson(JSONResult, JSONMessage);

  // We're done with HTTP - free the resources
  secure_client.stop();
  client.stop();

  if (err) {  // Check for errors in parsing
    log_e("Parsing failed");
    return false;
  }

  if (JSONResult.is<JsonArray>()) {
    // We already received an array of multiple firmware types
    JsonArray arr = JSONResult.as<JsonArray>();
    for (JsonVariant JSONDocument : arr) {
      if (checkJSONManifest(JSONDocument)) {
        return true;
      }
    }
  } else if (JSONResult.is<JsonObject>()) {
    if (checkJSONManifest(JSONResult.as<JsonVariant>())) return true;
  }

  return false;  // We didn't get a hit against the above, return false
}

String esp32FOTA::getDeviceID() {
  char deviceid[21];
  uint64_t chipid;
  chipid = ESP.getEfuseMac();
  sprintf(deviceid, "%" PRIu64, chipid);
  String thisID(deviceid);
  return thisID;
}

// Force a firmware update regardless on current version
void esp32FOTA::forceUpdate(String firmwareURL, boolean validate) {
  String urlRaw;
  if (firmwareURL.substring(0, 5) == "https") {
    urlRaw        = firmwareURL.substring(8);
    _firmwarePort = 443;
  } else {
    urlRaw        = firmwareURL.substring(7);
    _firmwarePort = 80;
  }
  _firmwareHost = urlRaw.substring(0, urlRaw.indexOf('/'));
  _firmwareBin  = urlRaw.substring(urlRaw.indexOf('/'));
  execOTA();
}

void esp32FOTA::forceUpdate(String firmwareHost, uint16_t firmwarePort, String firmwarePath, boolean validate) {
  _firmwareHost = firmwareHost;
  _firmwareBin  = firmwarePath;
  _firmwarePort = firmwarePort;
  _check_sig    = validate;
  execOTA();
}

void esp32FOTA::forceUpdate(boolean validate) {
  // Forces an update from a manifest, ignoring the version check
  if (!execHTTPcheck()) {
    if (!_firmwareHost) {
      // execHTTPcheck returns false if either the manifest is malformed or if the version isn't
      // an upgrade. If _firmwareHost isn't set, however, we can't force an upgrade.
      log_e("forceUpdate called, but unable to get _firmwareHost from manifest via execHTTPcheck.");
      return;
    }
  }
  _check_sig = validate;
  execOTA();
}

/**
 * This function return the new version of new firmware
 */
int esp32FOTA::getPayloadVersion() {
  log_w("int esp32FOTA::getPayloadVersion() only returns the major version from semantic version strings. Use void esp32FOTA::getPayloadVersion(char * version_string) instead!");
  return _payloadVersion.major;
}

void esp32FOTA::getPayloadVersion(char *version_string) { semver_render(&_payloadVersion, version_string); }

void esp32FOTA::setModem(TinyGsm &modem, int led, int pwr, int baud, int rx, int tx) {
  _modem     = &modem;
  _ledPin    = led;
  _pwrPin    = pwr;
  _modemBaud = baud;
  _modemRX   = rx;
  _modemTX   = tx;
}

void esp32FOTA::turnModemOn() {
  unsigned long current = millis();
  digitalWrite(_ledPin, LOW);
  digitalWrite(_pwrPin, LOW);
  while (millis() - current < 1000) {
    ;
  }  // Datasheet Ton minutes = 1S
  digitalWrite(_pwrPin, HIGH);
}

void esp32FOTA::turnModemOff() {
  unsigned long current = millis();
  digitalWrite(_pwrPin, LOW);
  while (millis() - current < 1500) {
    ;
  }  // Datasheet Ton minutes = 1.2S
  digitalWrite(_pwrPin, HIGH);
  digitalWrite(_ledPin, LOW);
}

void esp32FOTA::modemRestart() {
  unsigned long current = millis();
  turnModemOff();
  while (millis() - current < 1000) {
    ;
  }
  current = millis();
  turnModemOn();
  while (millis() - current < 5000) {
    ;
  }
}

void esp32FOTA::readyUpModem(TinyGsm &modem, const char *apn, const char *user, const char *pass) {
  Serial1.begin(_modemBaud, SERIAL_8N1, _modemRX, _modemTX);
  Serial.print("Initializing modem...");
  if (!modem.init()) {
    Serial.print(" fail... restarting modem...");
    modemRestart();
    if (!modem.restart()) {
      Serial.println(" fail... even after restart");
      return;
    }
  }
  Serial.println(" OK");

  if (!modem.testAT()) {
    Serial.println("Failed to restart modem, attempting to continue without restarting");
    modemRestart();
    return;
  }

  // General information
  Serial.println("Modem Name: " + modem.getModemName());
  Serial.println("Modem Info: " + modem.getModemInfo());

  unsigned long current = millis();
  // Set modes. Only use 2 and 13
  modem.setNetworkMode(2);
  while (millis() - current < 3000) {
    ;
  }

  // Choose IoT mode. Only use if the SIM provider supports
  // current = millis()
  // _sim_modem.setPreferredMode(3);
  // while (millis() - current < 3000) {
  //   ;
  // }

  // Wait for network availability
  Serial.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    current = millis();
    Serial.println(" fail");
    while (millis() - current < 10000) {
      ;
    }
    return;
  }
  Serial.println(" OK");

  // Connect to the GPRS network
  Serial.print("Connecting to network...");
  if (!modem.isNetworkConnected()) {
    current = millis();
    Serial.println(" fail");
    while (millis() - current < 10000) {
      ;
    }
    return;
  }
  Serial.println(" OK");

  // Connect to APN
  Serial.print("Connecting to APN: ");
  Serial.print(apn);
  if (!modem.gprsConnect(apn, user, pass)) {
    Serial.println(" fail");
    return;
  }
  digitalWrite(_ledPin, HIGH);
  Serial.println(" OK");

  // More info..
  Serial.println("");
  Serial.println("CCID: " + modem.getSimCCID());
  Serial.println("IMEI: " + modem.getIMEI());
  Serial.println("Operator: " + modem.getOperator());
  Serial.println("Local IP: " + String(modem.localIP()));
  Serial.println("Signal quality: " + String(modem.getSignalQuality()));
}
