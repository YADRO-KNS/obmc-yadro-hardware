/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#pragma once

#include <phosphor-logging/log.hpp>

#include <fstream>
#include <string>

using namespace phosphor::logging;

/**
 * @brief Lookup the pciids database to retrieve the corresponding Vendor
 *        and Model by VID/DID
 *
 * @note VID and DID should be in 4 charters hex representation. They may be
 * with or without '0x' prefix but both should be in same form.
 *
 * @param[in] vendorId                                  - vendor ID
 * @param[in] deviceId                                  - device ID (optional)
 *
 * @return const std::pair<std::string, std::string>    - Vendor and Model
 */
static const std::pair<std::string, std::string>
    pciLookup(const std::string& vendorId,
              const std::string& deviceId = std::string())
{
    constexpr const char* pciidsPath = "/usr/share/misc/pci.ids";

    static const size_t vidPrefixLen = 0;
    /* The line of DID has one tabulation to indent */
    static const size_t didPrefixLen = 1;
    static const size_t wordLen = 4;
    static const size_t sepLen = 2;
    /* The 0x prefix of the VID: `0x____` */
    static const std::string hexPrefix = "0x";
    size_t prefixLen = false;
    if (vendorId.compare(0, hexPrefix.length(), hexPrefix) == 0)
    {
        prefixLen = hexPrefix.length();
    }

    std::string line;
    std::string vendorName;
    std::string modelName;
    std::ifstream idfile(pciidsPath);
    if (!idfile.is_open())
    {
        log<level::ERR>("failed to open pid.ids file");
        return std::pair<std::string, std::string>();
    }

    while (std::getline(idfile, line))
    {
        if (vendorName.empty() &&
            (line.compare(vidPrefixLen, wordLen, vendorId, prefixLen) == 0))
        {
            vendorName = line.substr(vidPrefixLen + wordLen + sepLen);
            if (line.compare(vidPrefixLen + wordLen, sepLen, "  ") != 0)
            {
                log<level::ERR>("pid.ids: wrong vendor line format",
                                entry("VALUE=%s", line.c_str()));
            }
        }
        else if (modelName.empty() && !deviceId.empty() &&
                 line.size() > didPrefixLen)
        {
            if (0 ==
                line.compare(didPrefixLen, wordLen, deviceId, prefixLen))
            {
                modelName = line.substr(didPrefixLen + wordLen + sepLen);
                if (line.compare(didPrefixLen + wordLen, sepLen, "  ") != 0)
                {
                    log<level::ERR>("pid.ids: wrong device line format",
                                    entry("VALUE=%s", line.c_str()));
                }
            }
        }

        if (!vendorName.empty() && (!modelName.empty() || deviceId.empty()))
        {
            break;
        }
    }
    return std::pair<std::string, std::string>(vendorName, modelName);
}
