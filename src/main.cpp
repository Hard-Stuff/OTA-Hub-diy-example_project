#include <Arduino.h>

// OTA Hub via GitHub
#define OTAGH_OWNER_NAME "Hard-Stuff"
#define OTAGH_REPO_NAME "OTA-Hub-diy-example_project"
#include <OTA-Hub.hpp>

// Networking
static const char *WIFI_SSID = "Hard-Stuff.com";
static const char *WIFI_PASS = "password";

#include <WiFiClientSecure.h>
WiFiClientSecure wifi_client;

void setup()
{
    // Initialise our board
    Serial.begin(115200);
    Serial.println("Started...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.println("WiFi failure");
        ESP.restart();
    }

    // Initialise OTA
    wifi_client.setCACert(OTAGH_CA_CERT); // Set the api.github.com SSL cert on the WiFiSecure modem
    OTA::init(wifi_client);

    // Check OTA for updates
    OTA::UpdateObject details = OTA::isUpdateAvailable();
    details.print();
    if (OTA::NO_UPDATE != details.condition)
    {
        Serial.println("An update is available!");
        // Perform OTA update
        if (OTA::performUpdate(&details) == OTA::SUCCESS)
        {
            // .. success! It'll restart by default, or you can do other things here...
        }
    }
    else
    {
        Serial.println("No new update available. Continuing...");
    }
    Serial.print("Loop");
}

void loop()
{
    delay(5000);
    Serial.print("edy loop");
}