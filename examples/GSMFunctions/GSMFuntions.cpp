#include <Arduino.h>
#include <esp32FotaGsmSSL.h>

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

TinyGsm modem(SerialAT);

void enableGPS(void)
{
    modem.powerONGPS();
    if (modem.waitResponse(10000L) != 1)
    {
        DBG(" SGPIO=0,4,1,1 false ");
    }
    modem.enableGPS();
}

void disableGPS(void)
{
    modem.powerOFFGPS();
    if (modem.waitResponse(10000L) != 1)
    {
        DBG(" SGPIO=0,4,1,0 false ");
    }
    modem.disableGPS();
}

void turnModemOn()
{
    digitalWrite(LED_PIN, LOW);

    pinMode(MODEM_PWRKEY, OUTPUT);
    digitalWrite(MODEM_PWRKEY, LOW);
    delay(1000);
    digitalWrite(MODEM_PWRKEY, HIGH);
}

void turnModemOff()
{
    digitalWrite(MODEM_PWRKEY, LOW);
    delay(1500);
    digitalWrite(MODEM_PWRKEY, HIGH);

    digitalWrite(LED_PIN, LOW);
}

void modemRestart()
{
    turnModemOff();
    delay(1000);
    turnModemOn();
}

void setup()
{
    // Set console baud rate
    SerialMon.begin(115200);

    delay(10);

    // Set LED OFF
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    turnModemOn();

    SerialAT.begin(MODEM_UART_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);

    Serial.println("/**********************************************************/");
    Serial.println("To initialize the network test, please make sure your GPS");
    Serial.println("antenna has been connected to the GPS port on the board.");
    Serial.println("/**********************************************************/\n\n");

    delay(10000);

    if (!modem.testAT())
    {
        Serial.println("Failed to restart modem, attempting to continue without restarting");
        modemRestart();
        return;
    }

    Serial.println("Start positioning . Make sure to locate outdoors.");
    Serial.println("The blue indicator light flashes to indicate positioning.");

    enableGPS();

    float lat, lon, speed, alt;
    int vsat, usat;
    int year, month, day, hour, minute, second;
    int timeout = millis() + 300000;
    while (millis() < timeout)
    {
        if (modem.getGPS(&lat, &lon, &speed, &alt, &vsat, &usat, &year, &month, &day, &hour, &minute, &second))
        {
            Serial.println("The location has been locked, with:");
            Serial.printf("Latitude: %.2f°\n", lat);
            Serial.printf("Longitude: %.2f°\n", lon);
            Serial.printf("Moving speed: %.2f km/h\n", speed);
            Serial.printf("Altitude: %.2f m\n", alt);
            Serial.printf("Satellites in view: %d\n", vsat);
            Serial.printf("Satellites used: %d\n", usat);
            // modem.getGPSTime(&year, &month, &day, &hour, &minute, &second);
            Serial.printf("Time: %d/%d/%d %dh%dm%ds UTC+7\n", day, month, year, hour + 7, minute, second);
            break;
        }
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(2000);
    }
    Serial.println("GPS has been disabled after successfully detected or due to timeout.");
    disableGPS();
}

void loop()
{
}
