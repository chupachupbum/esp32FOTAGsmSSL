# esp32FOTA library for Arduino using GSM

## Purpose

A simple library to add support for Over-The-Air (OTA) updates to your project, using TinyGsm.

- Based on [esp32FOTA](https://github.com/chrisjoyce911/esp32FOTA) and [esp32FOTAGSM](https://github.com/IoTThinks/esp32FOTAGSM)
- Working with both http and https, using [SSLClient](https://github.com/govorox/SSLClient)
- ForceUpdate and withDeviceID are not yet tested, but it should work (I guess).

## Usage

Follow the base project [esp32FOTA](https://github.com/chrisjoyce911/esp32FOTA) to setting up json and rsa_key. Looking at main.cpp in tests folder for example.
Using ca_cert method of SSLClient. You need to add the root_ca of your webserver to ca_cert.h to use https.
