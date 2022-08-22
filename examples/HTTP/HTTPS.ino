/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS)

   Setup:
   Step 1 : Set your WiFi (ssid & password)
   Step 2 : set esp32fota()
   Step 3 : Provide SPIFFS filesystem with root_ca.pem of your webserver

   Upload:
   Step 1 : Menu > Sketch > Export Compiled Library. The bin file will be saved in the sketch folder (Menu > Sketch > Show Sketch folder)
   Step 2 : Upload it to your webserver
   Step 3 : Update your firmware JSON file ( see firwmareupdate )

*/

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp32fota.h>

// ============== GSM ===============
#define TINY_GSM_MODEM_SIM7000  // Modem is SIM7000
#include <TinyGsmClient.h>

// To CHANGE to fit your board
#define SerialMon Serial  // For debug serial
#define SerialAT Serial1  // For GSM module

// Your GPRS credentials, if any
const char apn[]    = "m3-world";  // TO CHANGE
const char user[]   = "mms";       // TO CHANGE
const char pass[]   = "mms";       // TO CHANGE
bool gprs_connected = false;
int _ledPin         = 12;
int _pwrPin         = 4;

// GSM variables
TinyGsm modem(SerialAT);

// esp32fota esp32fota("<Type of Firme for this device>", <this version>, <validate signature>);
esp32FOTA esp32fota("esp32-fota-http", 1, false, false);

void turnModemOn() {
  digitalWrite(_ledPin, LOW);
  digitalWrite(_pwrPin, LOW);
  delay(1000);  // Datasheet Ton minutes = 1S
  digitalWrite(_pwrPin, HIGH);
}

void turnModemOff() {
  digitalWrite(_pwrPin, LOW);
  delay(1500);  // Datasheet Ton minutes = 1.2S
  digitalWrite(_pwrPin, HIGH);
  digitalWrite(_ledPin, LOW);
}

void modemRestart() {
  turnModemOff();
  delay(1000);
  turnModemOn();
  delay(5000);
}

void readyUpModem(const char* apn, const char* user, const char* pass) {
  SerialAT.begin(9600, SERIAL_8N1, 26, 27);
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

  // Set modes. Only use 2 and 13
  modem.setNetworkMode(2);
  delay(3000);

  // Choose IoT mode. Only use if the SIM provider supports
  // _sim_modem.setPreferredMode(3);
  // delay(3000);

  // Wait for network availability
  Serial.print("Waiting for network...");
  if (!modem.waitForNetwork()) {
    Serial.println(" fail");
    delay(10000);
    return;
  }
  Serial.println(" OK");

  // Connect to the GPRS network
  Serial.print("Connecting to network...");
  if (!modem.isNetworkConnected()) {
    Serial.println(" fail");
    delay(10000);
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

void setup() {
  // Provide spiffs with root_ca.pem to validate server certificate
  SPIFFS.begin(true);

  esp32fota.checkURL = "https://raw.githubusercontent.com/chupachupbum/upload-folder/master/firmware.json";
  Serial.begin(115200);
  readyUpModem(apn, user, pass);
  esp32fota.setModem(modem);
}

void loop() {
  // bool updatedNeeded = true;
  bool updatedNeeded = esp32fota.execHTTPcheck();
  if (updatedNeeded) {
    Serial.println("Confirm OTA");
    esp32fota.execOTA();
  }

  delay(2000);
}
