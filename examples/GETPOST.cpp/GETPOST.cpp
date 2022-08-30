#include <Arduino.h>
#include <esp32FotaGsmSSL.h>

#include "SSLClient.h"

// ============== GSM ===============
#define TINY_GSM_MODEM_SIM7000 // Modem is SIM7000
#include <TinyGsmClient.h>

// Define your pins definition
#define MODEM_UART_BAUD 9600
#define MODEM_TX 27
#define MODEM_RX 26
#define MODEM_PWRKEY 4
#define LED_PIN 12

// To CHANGE to fit your board
#define SerialMon Serial // For debug serial
#define SerialAT Serial1 // For GSM module

// Your GPRS credentials, if any
const char apn[] = "";  // TO CHANGE
const char user[] = ""; // TO CHANGE
const char pass[] = ""; // TO CHANGE

const char hostname[] = "api.eosflare.io";
const char path[] = "/v1/chain/get_block";
int port = 443;

TinyGsm modem(SerialAT);
TinyGsmClient gsm_transport_layer(modem);
SSLClient http_client(&gsm_transport_layer);
esp32FotaGsmSSL esp32FotaGsmSSL("esp32-fota-http", 1, true, false);

void setup()
{
    Serial.begin(115200);
    esp32FotaGsmSSL.setModem(modem, LED_PIN, MODEM_PWRKEY, MODEM_UART_BAUD, MODEM_RX, MODEM_TX);
    esp32FotaGsmSSL.readyUpModem(modem, apn, user, pass);
}

void loop()
{
    Serial.printf("Connecting to %s...", hostname);

    // if you get a connection, report back via serial:
    if (!http_client.connect(hostname, port))
    {
        Serial.println(" fail");
        delay(10000);
        return;
    }
    Serial.println(" OK");

    // HTTP Test
    if (modem.isGprsConnected())
    {
        Serial.println("");
        Serial.println("Making eos info POST request");
        String postData = "";
        http_client.print("POST /v1/chain/get_info HTTP/1.1\n");
        http_client.print(String("Host: ") + hostname + "\n");
        http_client.print("Accept: */*\n");
        // http_client.println("Content-Type: application/x-www-form-urlencoded");
        http_client.print("Content-Length: 0\n\n");
        http_client.print(postData);

        long timeout = millis();
        uint32_t contentLength = 1024;
        uint32_t readLength = 0;

        while (readLength < contentLength && http_client.connected() && millis() - timeout < 30000L)
        { // Timeout should be at least 20s for TTGO SIM7000G
            while (http_client.available())
            {
                // read file data to spiffs
                char c = http_client.read();
                Serial.print((char)c); // Uncomment this to show data
                readLength++;
                timeout = millis();
            }
        }

        http_client.stop();
        Serial.println("Stopped");
    }
    else
    {
        Serial.println("...not connected");
    }

    delay(15000);
}
