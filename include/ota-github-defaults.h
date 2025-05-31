#pragma once

// Backwards compatability since naming change
#if defined(OTAGH_OWNER_NAME) && !defined(OTA_OWNER_NAME)
#define OTA_OWNER_NAME OTAGH_OWNER_NAME
#endif

// Backwards compatability since naming change
#if defined(OTAGH_REPO_NAME) && !defined(OTA_REPO_NAME)
#define OTA_REPO_NAME OTAGH_REPO_NAME
#endif

#ifndef OTA_SERVER
#define OTA_SERVER "api.github.com"
#endif

#ifndef OTA_PORT
#define OTA_PORT 443
#endif

#ifndef OTA_CHECK_PATH
const std::string OTA_CHECK_PATH = std::string("/repos/") + OTA_OWNER_NAME + "/" + OTA_REPO_NAME + "/releases/latest"; // TODO:: would this work?
#endif

#ifndef OTA_BIN_PATH
const std::string OTA_BIN_PATH = std::string("/repos/") + OTA_OWNER_NAME + "/" + OTA_REPO_NAME + "/releases/assets/";
#endif