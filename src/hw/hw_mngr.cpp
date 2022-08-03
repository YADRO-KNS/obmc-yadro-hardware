/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#include "hw_mngr.hpp"

#include "dbus.hpp"
#include "objects.hpp"
#include "product_registry.hpp"

#include <phosphor-logging/log.hpp>

#include <charconv>
#include <filesystem>

namespace fs = std::filesystem;
using namespace phosphor::logging;

void HWManager::setProduct(const std::string& pname)
{
    config.chassisModel = pname;
    config.desc = nullptr;
    for (const auto& desc : productRegistry)
    {
        if (std::regex_match(pname, desc.pnameRegex))
        {
            config.desc = &desc;
            break;
        }
    }
}

static std::string getZoneName(int index)
{
    if (index < 0 || index >= 0xFF)
    {
        return std::string();
    }
    if ((index & 0xF0) == 0x10)
    {
        return "Chassis" + std::to_string(index & 0x0F);
    }
    switch (index)
    {
        case 1:
            return "Main";
            break;
        case 2:
            return "CPU";
            break;
        case 3:
            return "PSU";
            break;
    }

    return std::string();
}

bool HWManager::setOption(const OptionType& optType, const int& instance,
                          const std::string& value)
{
    switch (optType)
    {
        case OptionType::cpuCooling:
            if (value == "00") // Passive PCU cooling
            {
                config.haveCPUFans = false;
            }
            else if (value == "01") // Active CPU cooling
            {
                config.haveCPUFans = true;
            }
            break;
        case OptionType::chassisFans:
        {
            std::string zoneName = getZoneName(instance);
            int cnt = 0;
            int fan_no = 0;
            if (value.size() < 2)
            {
                log<level::ERR>("Invalid chassisFans option value format",
                                entry("VALUE=%s", value.c_str()));
                return false;
            }
            auto res = std::from_chars(value.data(), value.data() + 2, cnt, 16);
            if (res.ec != std::errc() || value.size() < (cnt + 1) * 2)
            {
                log<level::ERR>("Invalid chassisFans option value format",
                                entry("VALUE=%s", value.c_str()));
                return false;
            }

            config.chassisFans[zoneName].fanConnector.clear();
            for (int i = 0; i < cnt; i++)
            {
                res = std::from_chars(res.ptr, res.ptr + 2, fan_no, 16);
                if (res.ec != std::errc())
                {
                    log<level::ERR>("Invalid chassisFans option value format",
                                    entry("VALUE=%s", value.c_str()));
                    return false;
                }
                config.chassisFans[zoneName].fanConnector.emplace_back(fan_no);
            }
            break;
        }
        case OptionType::pidZoneMinSpeed:
        {
            std::string zoneName = getZoneName(instance);
            int speed = 0;
            if (value.size() != 2)
            {
                log<level::ERR>("Invalid pidZoneMinSpeed option value format",
                                entry("VALUE=%s", value.c_str()));
                return false;
            }
            std::from_chars(value.data(), value.data() + 2, speed, 16);
            if ((speed >= 5) && (speed <= 100))
            {
                config.chassisFans[zoneName].fanMinSpeed = speed;
            }
            break;
        }
        default:
            return false;
    }
    return true;
}

void HWManager::publish()
{
    if (!config.desc || configActive == config)
    {
        return;
    }
    clear();
    log<level::INFO>("Exposing hardware information",
                     entry("SYSTEM=%s", config.desc->productName.c_str()));
    chassis.emplace_back(std::make_shared<Chassis>(
        bus, config.desc->productName, config.chassisModel,
        config.chassisPartNumber, config.chassisSerial));
    auto sysFanMod = config.desc->productName + " System Fan";
    auto cpuFanMod = config.desc->productName + " CPU Fan";
    auto chsFanMod = config.desc->productName + " Chassis Fan";
    auto sysFanPN = config.desc->sysFanPN;
    auto cpuFanPN = config.desc->cpuFanPN;
    auto chsFanPN = "CHSFAN000001A";
    for (const auto& [conIndex, conDescr] : config.desc->fans)
    {
        auto fanIndexStr = std::to_string(conDescr.fanIndex);
        if (conDescr.type == ConnectorType::SYSTEM)
        {
            fans.emplace_back(std::make_shared<Fan>(
                bus, "Sys_Fan" + fanIndexStr, "System Fan " + fanIndexStr,
                sysFanMod, sysFanPN, conDescr.zone, conDescr.connector,
                conDescr.tachIndexA, conDescr.tachIndexB, conDescr.pwmIndex,
                conDescr.pwmLimitMax));
        }
        else if ((conDescr.type == ConnectorType::CPU) && config.haveCPUFans)
        {

            auto it = config.cpuPresence.find(conDescr.fanIndex);
            if (it != config.cpuPresence.end() && it->second)
            {
                fans.emplace_back(std::make_shared<Fan>(
                    bus, "CPU" + fanIndexStr + "_Fan",
                    "CPU" + fanIndexStr + " Fan", cpuFanMod, cpuFanPN,
                    conDescr.zone, conDescr.connector, conDescr.tachIndexA,
                    conDescr.tachIndexB, conDescr.pwmIndex,
                    conDescr.pwmLimitMax));
            }
        }
    }

    size_t chassisFanIndex = 1;
    for (const auto& [zoneName, zoneDesc] : config.chassisFans)
    {
        for (const auto& connector : zoneDesc.fanConnector)
        {
            auto it = config.desc->fans.find(connector);
            if (it == config.desc->fans.end())
            {
                log<level::ERR>(
                    "Fan connector index not defined for the platform",
                    entry("VALUE=%d", connector));
                continue;
            }
            if (it->second.type == ConnectorType::SYSTEM)
            {
                log<level::ERR>("Can't redefine system fan",
                                entry("VALUE=%d", connector));
                continue;
            }
            if ((it->second.type == ConnectorType::CPU) && (config.haveCPUFans))
            {
                log<level::ERR>("Can't redefine CPU fan, active CPU "
                                "cooling enabled",
                                entry("VALUE=%d", connector));
                continue;
            }
            auto fanIndexStr = std::to_string(chassisFanIndex);
            fans.emplace_back(std::make_shared<Fan>(
                bus, "Cha_Fan" + fanIndexStr, "Chassis Fan " + fanIndexStr,
                chsFanMod, chsFanPN, zoneName, it->second.connector,
                it->second.tachIndexA, it->second.tachIndexB,
                it->second.pwmIndex, it->second.pwmLimitMax));
            chassisFanIndex++;
        }
    }

    configActive = config;

    auto matchPIDZone = std::make_unique<sdbusplus::bus::match::match>(
        bus,
        std::string("type='signal',member='PropertiesChanged',path_"
                    "namespace='") +
            dbus::pid::path + "',arg0namespace='" + dbus::pid::interface + "'",
        [&](sdbusplus::message::message& message) {
            if (message.is_method_error())
            {
                log<level::ERR>("PropertiesChanged signal error");
                return;
            }

            setFanSpeedDelayed();
        });
    matches.emplace_back(std::move(matchPIDZone));

    setFanSpeedDelayed();
}

void HWManager::clear()
{
    matches.clear();
    chassis.clear();
    fans.clear();
}

void HWManager::setFanSpeed()
{
    SubTreeType objects;
    auto getObjects =
        bus.new_method_call(dbus::mapper::busName, dbus::mapper::path,
                            dbus::mapper::interface, dbus::mapper::subtree);
    getObjects.append(dbus::pid::path, 0,
                      std::array<std::string, 1>{dbus::pid::interface});

    try
    {
        log<level::DEBUG>("Calling GetSubTree for PID Zones");
        bus.call(getObjects).read(objects);
        log<level::DEBUG>("GetSubTree call done");
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        log<level::ERR>("Error while calling GetSubTree",
                        entry("WHAT=%s", ex.what()));
        return;
    }

    for (const auto& [path, objDict] : objects)
    {
        fs::path zonePath = path;
        std::string zoneName = zonePath.filename();
        const std::string& owner = objDict.begin()->first;

        auto it = config.chassisFans.find(zoneName);
        if (it == config.chassisFans.end() || !it->second.fanMinSpeed)
        {
            continue;
        }
        double fanMinSpeed = it->second.fanMinSpeed;

        DbusPropVariant data;
        auto getProperty = bus.new_method_call(owner.c_str(), path.c_str(),
                                               dbus::properties::interface,
                                               dbus::properties::get);
        getProperty.append(dbus::pid::interface,
                           dbus::pid::properties::MinThermalOutput);

        try
        {
            log<level::DEBUG>("Calling Get for PID Zone object");
            bus.call(getProperty).read(data);
            log<level::DEBUG>("Get call done");
        }
        catch (const sdbusplus::exception::exception& ex)
        {
            log<level::ERR>("Error while calling Get",
                            entry("SERVICE=%s", owner.c_str()),
                            entry("PATH=%s", path.c_str()),
                            entry("INTERFACE=%s", dbus::pid::interface),
                            entry("WHAT=%s", ex.description()));
            return;
        }

        double curValue;
        try
        {
            curValue = std::get<double>(data);
        }
        catch (std::invalid_argument&)
        {
            log<level::ERR>("Error reading property 'MinThermalOutput'",
                            entry("PATH=%s", path.c_str()));
            return;
        }
        if (curValue >= fanMinSpeed)
        {
            continue;
        }

        data = fanMinSpeed;
        auto setProperty = bus.new_method_call(owner.c_str(), path.c_str(),
                                               dbus::properties::interface,
                                               dbus::properties::set);
        setProperty.append(dbus::pid::interface,
                           dbus::pid::properties::MinThermalOutput, data);

        try
        {
            log<level::DEBUG>("Calling Set for PID Zone object");
            bus.call(setProperty);
            log<level::DEBUG>("Set call done");
        }
        catch (const sdbusplus::exception::exception& ex)
        {
            log<level::ERR>("Error while calling Set",
                            entry("SERVICE=%s", owner.c_str()),
                            entry("PATH=%s", path.c_str()),
                            entry("INTERFACE=%s", dbus::pid::interface),
                            entry("WHAT=%s", ex.description()));
            return;
        }
    }
}

void HWManager::setFanSpeedDelayed()
{
    static boost::asio::deadline_timer filterTimer(io);

    // this implicitly cancels the timer
    filterTimer.expires_from_now(boost::posix_time::seconds(5));
    filterTimer.async_wait([&](const boost::system::error_code& err) {
        if (err == boost::asio::error::operation_aborted)
        {
            /* we were canceled*/
            return;
        }
        else if (err)
        {
            log<level::ERR>("Timer error",
                            entry("WHAT=%s", err.message().c_str()));
            return;
        }
        setFanSpeed();
    });
}
