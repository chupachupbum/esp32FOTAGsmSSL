/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS)

   Setup:
   Step 1 : Set your gsm credentials
   Step 2 : set esp32FotaGsmSSL()
   Step 3 : Provide SPIFFS filesystem with root_ca.pem of your webserver

   Upload:
   Step 1 : Menu > Sketch > Export Compiled Library. The bin file will be saved in the sketch folder (Menu > Sketch > Show Sketch folder)
   Step 2 : Upload it to your webserver
   Step 3 : Update your firmware JSON file ( see firwmareupdate )

*/

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <esp32FotaGsmSSL.h>

// ============== GSM ===============
#define TINY_GSM_MODEM_SIM7000  // Modem is SIM7000
#include <TinyGsmClient.h>

// Define your pins definition
#define MODEM_UART_BAUD 9600
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define LED_PIN 12

// To CHANGE to fit your board
#define SerialMon Serial  // For debug serial
#define SerialAT Serial1  // For GSM module

// Your GPRS credentials, if any
const char apn[]  = "";  // TO CHANGE
const char user[] = "";  // TO CHANGE
const char pass[] = "";  // TO CHANGE

TinyGsm modem(SerialAT);
esp32FotaGsmSSL esp32FotaGsmSSL("esp32-fota-http", 1, true, false);

void setup() {
  // Provide spiffs with root_ca.pem to validate server certificate
  SPIFFS.begin(true);

  esp32FotaGsmSSL.checkURL = "";  // TO CHANGE
  Serial.begin(115200);
  esp32FotaGsmSSL.setModem(modem, LED_PIN, MODEM_PWRKEY, MODEM_UART_BAUD, MODEM_RX, MODEM_TX);
  esp32FotaGsmSSL.readyUpModem(modem, apn, user, pass);
}

void loop() {
  bool updatedNeeded = esp32FotaGsmSSL.execHTTPcheck();
  if (updatedNeeded) {
    Serial.println("Confirm OTA");
    esp32FotaGsmSSL.execOTA();
  }

  delay(2000);
}
