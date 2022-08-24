/**
   esp32 firmware OTA

   Purpose: Perform an OTA update from a bin located on a webserver (HTTPS)

   Setup:
   Step 1 : Set your gsm credentials
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

// ESP32 LilyGO-T-SIM7000G pins definition
#define MODEM_UART_BAUD 9600
#define MODEM_DTR 25
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define LED_PIN 12

// To CHANGE to fit your board
#define SerialMon Serial  // For debug serial
#define SerialAT Serial1  // For GSM module

// Your GPRS credentials, if any
const char apn[]  = "m3-world";  // TO CHANGE
const char user[] = "mms";       // TO CHANGE
const char pass[] = "mms";       // TO CHANGE

TinyGsm modem(SerialAT);
esp32FOTA esp32fota("esp32-fota-http", 1, true, false);

void setup() {
  // Provide spiffs with root_ca.pem to validate server certificate
  SPIFFS.begin(true);

  esp32fota.checkURL = "https://raw.githubusercontent.com/chupachupbum/upload-folder/master/firmware_sig.json";
  Serial.begin(115200);
  esp32fota.setModem(modem, LED_PIN, MODEM_PWRKEY, MODEM_UART_BAUD, MODEM_RX, MODEM_TX);
  esp32fota.readyUpModem(modem, apn, user, pass);
}

void loop() {
  bool updatedNeeded = esp32fota.execHTTPcheck();
  if (updatedNeeded) {
    Serial.println("Confirm OTA");
    esp32fota.execOTA();
  }

  delay(2000);
}
