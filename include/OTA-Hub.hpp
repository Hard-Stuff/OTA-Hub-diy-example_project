#pragma once
#define HTTP_MAX_HEADERS 30          // GitHub sends ~28 headers back!

#include <Arduino.h>
SET_LOOP_TASK_STACK_SIZE(16 * 1024); // 16KB, GitHub responses are heavy

// libs
#include <Hard-Stuff-Http.hpp>
#include <Update.h>
#include <ArduinoJson.h>

#ifndef OTA_VERSION
#define OTA_VERSION "local_development"
#endif

#pragma region HelperFunctions
String getMacAddress()
{
    uint8_t baseMac[6];
    // Get MAC address for WiFi station
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char baseMacChr[18] = {0};
    sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    return String(baseMacChr);
}

time_t cvtDate()
{
    char s_month[5];
    int year;
    tmElements_t t;
    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

    sscanf(__DATE__, "%s %hhd %d", s_month, &t.Day, &year);
    sscanf(__TIME__, "%2hhd %*c %2hhd %*c %2hhd", &t.Hour, &t.Minute, &t.Second);

    // Find where is s_month in month_names. Deduce month value.
    t.Month = (strstr(month_names, s_month) - month_names) / 3 + 1;

    // year can be given as '2010' or '10'. It is converted to years since 1970
    if (year > 99)
        t.Year = year - 1970;
    else
        t.Year = year + 30;

    return makeTime(t);
}
#pragma endregion

namespace OTA
{
#pragma region WorkingVariabls
    HardStuffHttpClient *http_ota;
    Client *underlying_client;
    String asset_id;
#pragma endregion

#pragma region UsefulStructs
    /**
     * @brief What is the condition of the update relative to the current installed firmware?
     */
    enum UpdateCondition
    {
        NO_UPDATE,     // The proposed release is the same name and same age as this one (i.e. they're the same)
        OLD_DIFFERENT, // The proposed release is different to what we've got here (but it's older)
        NEW_SAME,      // The proposed release is newer but has the same name as this one (are you versioning correctly?)
        NEW_DIFFERENT  // The proposed update is both newer and has a different name (so is likely to be a legitimate update)
    };

    /**
     * @brief What is the condition of the install?
     */
    enum InstallCondition
    {
        FAILED_TO_DOWNLOAD, // For whatever reason, we failed to download the update
        REDIRECT_REQUIRED,  // We'll need to follow a redirect to download the firmware.bin file. Don't forget to set to the new ca_cert!
        SUCCESS             // Success! You'll likely only see me if you've asked the installer to not restart the ESP32.
    };

    /**
     * @brief Everything necesary related to a firmware release
     */
    struct UpdateObject
    {
        UpdateCondition condition;
        String name;
        time_t published_at;
        String firmware_asset_id;
        String firmware_asset_endpoint;
        String redirect_server;

        void print(Stream *print_stream = &Serial)
        {
            const char *condition_strings[] = {
                "NO_UPDATE",
                "OLD_DIFFERENT",
                "NEW_DIFFERENT",
                "NEW_SAME"};

            // Print condition
            print_stream->println("------------------------");
            print_stream->println("Condition: " + String(condition_strings[condition]));
            print_stream->println("name: " + name);
            print_stream->println("published_at: " + http_ota->formatTimeISO8601(published_at));
            print_stream->println("firmware_asset_id: " + String(firmware_asset_id));
            print_stream->println("firmware_asset_endpoint: " + String(firmware_asset_endpoint));
            print_stream->println("------------------------");
        }
    };
#pragma endregion

#pragma region SupportFunctions
    void confirmConnected()
    {
        if (http_ota->connected())
        {
        }
    }

    void printFirmwareDetails(Stream *print_stream = &Serial)
    {
        print_stream->println("------------------------");
        print_stream->println("Device MAC: " + getMacAddress());
        print_stream->println("Firmware Version: " + String(OTA_VERSION));
        print_stream->println("Firmware Compilation Date: " + String(__DATE__) + ", " + String(__TIME__));
        print_stream->println("------------------------");
    }

    void deinit()
    {
        if (http_ota != nullptr)
        {
            http_ota->stop();
            delete http_ota;
            http_ota = nullptr;
        }
    }

    void reinit(Client &set_underlying_client, const char *server, uint16_t port)
    {
        deinit();
        Serial.print("Server: ");
        Serial.println(server);
        underlying_client = &set_underlying_client;
        http_ota = new HardStuffHttpClient(set_underlying_client, server, port);
    }

    void init(Client &set_underlying_client)
    {
        printFirmwareDetails();
        reinit(set_underlying_client, OTA_SERVER, OTA_PORT);
    }

#pragma endregion

#pragma region CoreFunctions

    /**
     * @brief Check your server (or GitHub) to see if an update is available
     *
     * @return UpdateObject that bundles all the info we'll need.
     */
    UpdateObject isUpdateAvailable()
    {
        UpdateObject return_object;
        return_object.condition = NO_UPDATE;

        // Get the response from the server
        HardStuffHttpRequest request;
        if (String(OTA_SERVER).indexOf("github") > 0) {
            request.addHeader("Accept", "application/vnd.github+json");
        } else {
            request.addHeader("accept", "application/json");
        }
#ifdef OTA_BEARER
        request.addHeader("authorization", "Bearer " + String(OTA_BEARER)); // Used only in private repos. See the docs.
#endif

        HardStuffHttpResponse response = http_ota->getFromHTTPServer(OTA_CHECK_PATH.c_str(), &request);

        if (response.success())
        {
            // Compile into a JSON doc
#if ARDUINOJSON_VERSION_MAJOR >= 7
            JsonDocument release_response;
#else
            DynamicJsonDocument release_response(8129);
#endif
            deserializeJson(release_response, response.body);
            if (
#if ARDUINOJSON_VERSION_MAJOR >= 7
                release_response["name"].isNull() ||
                release_response["published_at"].isNull() ||
                release_response["assets"].isNull()
#else
                !release_response.containsKey("name") ||
                !release_response.containsKey("published_at") ||
                !release_response.containsKey("assets")
#endif
            )
            {
                Serial.println("The latest release contains no assets and/or metadata. We can't continue...");
                return return_object;
            }

            return_object.name = release_response["name"].as<String>();
            return_object.published_at = http_ota->formatTimeFromISO8601(release_response["published_at"].as<String>());

            // Evaluate comparison based on metadata
            bool update_is_different = release_response["name"].as<String>().compareTo(OTA_VERSION) != 0;
            bool update_is_newer = release_response["published_at"].as<time_t>() > cvtDate();

            JsonArray asset_array = release_response["assets"].as<JsonArray>();
            for (JsonVariant v : asset_array)
            {
                if (v["name"].as<String>().compareTo("firmware.bin") == 0)
                {
                    return_object.firmware_asset_id = v["id"].as<String>();
                    return_object.condition = update_is_different ? (update_is_newer ? NEW_DIFFERENT : OLD_DIFFERENT) : (update_is_newer ? NEW_SAME : NO_UPDATE);
                    return_object.firmware_asset_endpoint = String(OTA_BIN_PATH.c_str()) + return_object.firmware_asset_id;
                    return return_object;
                }
            }
            Serial.println("The latest release contains no firmware asset. We can't continue...");
#if ARDUINOJSON_VERSION_MAJOR < 7
            release_response.clear();
#endif
            return return_object;
        }

        Serial.println("Failed to connect to GitHub. Check your OTA_... #defines.");
        return return_object;
    }

    /**
     * @brief Download and perform an update based on the details provided in the UpdateObject file.
     *
     * @param details You'll get this from `isUpdateAvailable`
     * @param restart You can stop the updater from automatically restarting the board, say if you need to wind things down a bit...
     * @return InstallCondition Was it a success?
     */
    InstallCondition performUpdate(UpdateObject *details, bool restart = true)
    {
        Serial.println("Fetching update from: " + (details->redirect_server.isEmpty() ? String(OTA_SERVER) : details->redirect_server) + details->firmware_asset_endpoint);

        HardStuffHttpRequest request;
        // Headers
        request.addHeader("Accept", "application/octet-stream");
#ifdef OTA_BEARER
        request.addHeader("Authorization", "Bearer " + String(OTA_BEARER));
#endif

        // On GitHub this will likely return a 302 with a "location" header:
        HardStuffHttpResponse response = http_ota->getFromHTTPServer(details->firmware_asset_endpoint, &request, true);

        if (response.status_code == 302)
        {
            // Do redirect logic
            // Extract URL from "Location"
            String URL = "";
            for (int i = 0; i < response.header_count; i++)
            {
                if (response.headers[i].key.compareTo("Location") == 0)
                {
                    URL = response.headers[i].value;
                    break;
                }
            }
            if (URL.isEmpty())
            {
                Serial.println("Redirection URL extraction error...");
                Serial.println("We can't continue.");
                return FAILED_TO_DOWNLOAD;
            }
            // Get .com/ (or other part);
            URL.replace("https://", "");
            URL.replace("http://", "");
            int slash_index = URL.indexOf("/");

            http_ota->stop();
            delay(1000);
            details->redirect_server = URL.substring(0, slash_index);
            details->firmware_asset_endpoint = URL.substring(slash_index);
            return REDIRECT_REQUIRED;
        }

        if (response.status_code >= 200 && response.status_code < 300)
        {
            // we can download as normal
            Serial.println("firmware.bin found. Checking validity.");
            int contentLength;
            bool isValidContentType;

            for (int i_header = 0; i_header < response.header_count; i_header++)
            {
                if (response.headers[i_header].key.compareTo("Content-Length") == 0)
                {
                    contentLength = response.headers[i_header].value.toInt();
                }
                if (response.headers[i_header].key.compareTo("Content-Type") == 0)
                {
                    String contentType = response.headers[i_header].value;
                    isValidContentType =
                        contentType == "application/octet-stream" || contentType == "application/macbinary";
                }
            }

            if (contentLength && isValidContentType)
            {
                Serial.println("firmware.bin is good. Beginning the OTA update, this may take a while...");
                if (Update.begin(contentLength))
                {
                    Update.writeStream(*http_ota);
                    if (Update.end())
                    {
                        if (Update.isFinished())
                        {
                            Serial.println("OTA done!");
                            if (restart)
                            {
                                Serial.println("Reboot...");
                                ESP.restart();
                            }
                            http_ota->stop();
                            return SUCCESS;
                        }
                    }
                }

                Serial.println("------------------------------ERROR------------------------------");
                Serial.printf("    ERROR CODE: %d", Update.getError());
                Serial.println("-----------------------------------------------------------------");
            }
            else
            {
                Serial.println("Not enough space available.");
            }
        }
        else
        {
            Serial.println("There was no content in the response");
        }
        http_ota->stop();
        return FAILED_TO_DOWNLOAD;
    }

    /**
     * @brief Continue with an update (likely modified) following a 302 redirect.
     * Behaves similar to performUpdate, but is used after defining new SSL certs as needed.
     * @return InstallCondition
     */
    InstallCondition continueRedirect(UpdateObject *details, bool restart = true)
    {
        reinit(*underlying_client, details->redirect_server.c_str(), OTA_PORT);
        return performUpdate(details, restart);
    }
#pragma endregion
}