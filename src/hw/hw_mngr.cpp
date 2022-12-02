/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#include "hw_mngr.hpp"

#include "dbus.hpp"
#include "objects.hpp"
#include "product_registry.hpp"

#include <nlohmann/json.hpp>
#include <phosphor-logging/log.hpp>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

using namespace nlohmann;

namespace fs = std::filesystem;
using namespace phosphor::logging;

namespace fans
{
constexpr int maxErrorAttempts = 20;
const std::string emptyString = "";
const std::string sysHwmonPath = "/sys/class/hwmon/";
const std::string sysHwmonFile = "name";
const std::string sysHwmonContent = "aspeed_pwm_tacho";
const std::string sysFanPrefixName = "Sys_Fan";
const std::string sysFanDataFile = "/tmp/fan_features.json";

const FanModuleInfo&
    getFanInfoByRPM(const std::vector<FanModuleInfo>& detectionFanTable,
                    uint32_t inletRpm, uint32_t outletRpm)
{
    static const FanModuleInfo unknownFanModule = {
        .type = FanPerformanceType::UNKNOWN};

    if (inletRpm && outletRpm && inletRpm < outletRpm)
    {
        std::swap(inletRpm, outletRpm);
    }

    for (const auto& item : detectionFanTable)
    {
        bool inletRpmMatched =
            (inletRpm >= item.inletRangeMin && inletRpm <= item.inletRangeMax);
        bool outletRpmMatched = (outletRpm >= item.outletRangeMin &&
                                 outletRpm <= item.outletRangeMax);

        if (inletRpmMatched && outletRpmMatched)
        {
            return item;
        }
    }

    return unknownFanModule;
}

std::string getFansControlPath()
{
    for (const auto& entry : fs::directory_iterator(sysHwmonPath))
    {
        auto dirname = entry.path().string() + "/";
        auto pathname = dirname + sysHwmonFile;

        if (!std::filesystem::exists(pathname))
        {
            continue;
        }

        try
        {
            std::ifstream inFile(pathname);
            std::string content;
            inFile >> content;
            content.erase(content.find_last_not_of(" \n\r\t") + 1);
            if (content == sysHwmonContent)
            {
                return dirname;
            }
        }
        catch (...)
        {
            std::stringstream ssLog;
            ssLog << "Fail processing of file: " << pathname;
            log<level::ERR>(ssLog.str().c_str());
        }
    }

    return emptyString;
}

void setHwmonValue(const std::string& pathname, uint32_t pwmValue)
{
    std::stringstream ssLog;
    if (!std::filesystem::exists(pathname))
    {
        ssLog << "File is not exist: " << pathname;
        log<level::ERR>(ssLog.str().c_str());
        return;
    }

    try
    {
        std::ofstream outFile(pathname);
        outFile << pwmValue;
    }
    catch (...)
    {
        ssLog << "Fail writing to file: " << pathname;
        log<level::ERR>(ssLog.str().c_str());
    }
}

uint32_t getHwmonValue(const std::string& pathname)
{
    std::stringstream ssLog;
    if (!std::filesystem::exists(pathname))
    {
        ssLog << "File is not exist: " << pathname;
        log<level::ERR>(ssLog.str().c_str());
        return 0;
    }

    try
    {
        std::ifstream inFile(pathname);

        std::stringstream ss;
        ss << inFile.rdbuf();
        return std::stoi(ss.str());
    }
    catch (...)
    {
        ssLog << "Fail reading of file: " << pathname;
        log<level::ERR>(ssLog.str().c_str());
    }
    return 0;
}

}; // namespace fans

using namespace fans;

HWManager::HWManager(boost::asio::io_service& io, sdbusplus::bus::bus& bus) :
    io(io), bus(bus), powerState(bus)
{
    loadSystemFanFeatures();

    powerState.addCallback("manager", std::bind(&HWManager::onHostPowerChanged,
                                                this, std::placeholders::_1));
}

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

FanState HWManager::getDetectFanState()
{
    return detectFansState;
}

void HWManager::publishSystemFans()
{
    if (!config.desc)
    {
        return;
    }

    log<level::DEBUG>("HWManager::publishSystemFans()");

    //  remove existing system fans before re-publish
    for (auto it = fans.begin(); it != fans.end();)
    {
        const auto& fan = *it;

        size_t pos = fan->name().find(sysFanPrefixName);
        if (pos != std::string::npos)
        {
            it = fans.erase(it);
            continue;
        }

        it++;
    }

    auto sysFanMod = config.desc->productName + " System Fan";
    auto sysFanPN = config.desc->sysFanPN;

    std::set<FanPerformanceType> foundFanModuleTypes;

    for (const auto& [conIndex, conDescr] : config.desc->fans)
    {
        auto fanIndexStr = std::to_string(conDescr.fanIndex);
        if (conDescr.type == ConnectorType::SYSTEM)
        {
            auto& fan = fanFeatures[conIndex];
            std::string prettyName = "System Fan " + fanIndexStr;
            std::string partNumber = sysFanPN;

            if (!fan.prettyName.empty())
            {
                prettyName = fan.prettyName;
            }
            if (!fan.partNumber.empty())
            {
                partNumber = fan.partNumber;
            }

            foundFanModuleTypes.insert(fan.type);

            fans.emplace_back(std::make_shared<Fan>(
                bus, sysFanPrefixName + fanIndexStr, prettyName, sysFanMod,
                partNumber, conDescr.zone, conDescr.connector,
                conDescr.tachIndexA, conDescr.tachIndexB, conDescr.pwmIndex,
                conDescr.pwmLimitMax));

            std::stringstream ssLog;
            ssLog << "FAN: P/N: " << partNumber << " ('" << prettyName << "')";
            log<level::DEBUG>(ssLog.str().c_str());
        }
    }

    if (foundFanModuleTypes.size() > 1)
    {
        log<level::WARNING>("Mixed type of FAN modules used");
    }

    saveSystemFanFeatures();
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
    auto cpuFanMod = config.desc->productName + " CPU Fan";
    auto chsFanMod = config.desc->productName + " Chassis Fan";
    auto cpuFanPN = config.desc->cpuFanPN;
    auto chsFanPN = "CHSFAN000001A";

    for (const auto& [conIndex, conDescr] : config.desc->fans)
    {
        auto fanIndexStr = std::to_string(conDescr.fanIndex);
        if ((conDescr.type == ConnectorType::CPU) && config.haveCPUFans)
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

    //  remove all FANs except System FANs
    for (auto it = fans.begin(); it != fans.end();)
    {
        const auto& fan = *it;

        size_t pos = fan->name().find(sysFanPrefixName);
        if (pos == std::string::npos)
        {
            it = fans.erase(it);
            continue;
        }

        it++;
    }
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

        sd_journal_send(
            "MESSAGE=%s", "Fan PWM minimum changed due to hardware policy",
            "PRIORITY=%i", LOG_INFO, "ZONE_NAME=%s", zoneName.c_str(),
            "CUR_VALUE=%0.0f", curValue, "NEW_VALUE=%0.0f", fanMinSpeed,
            "REDFISH_MESSAGE_ID=%s", "OpenBMC.0.1.FanMinPwmRestricted",
            "REDFISH_MESSAGE_ARGS=%s,%0.0f,%0.0f", zoneName.c_str(), curValue,
            fanMinSpeed, NULL);

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

void HWManager::saveSystemFanFeatures()
{
    log<level::DEBUG>("HWManager::saveSystemFanFeatures()");

    std::ofstream ofs(sysFanDataFile);
    json jsonDoc;

    for (const auto& [fanIndex, fanFeature] : fanFeatures)
    {
        json jsonItem;
        jsonItem["fanIndex"] = fanIndex;
        jsonItem["partNumber"] = fanFeature.partNumber;
        jsonItem["prettyName"] = fanFeature.prettyName;
        jsonItem["type"] = fanFeature.type;
        jsonDoc.push_back(jsonItem);
    }

    ofs << std::setw(4) << jsonDoc << std::endl;
}

void HWManager::loadSystemFanFeatures()
{
    log<level::DEBUG>("HWManager::loadSystemFanFeatures()");
    fanFeatures.clear();

    std::ifstream dataFile(sysFanDataFile);
    if (!dataFile.is_open())
    {
        log<level::DEBUG>("Cannot to open file",
                          entry("VALUE=%s", sysFanDataFile.c_str()));
        return;
    }

    try
    {
        json jsonData;
        dataFile >> jsonData;
        if (!jsonData.is_array())
        {
            log<level::ERR>("Json file has invalid format.",
                            entry("VALUE=%s", sysFanDataFile.c_str()));
            return;
        }

        for (const auto& [itemIndex, jsonItem] : jsonData.items())
        {
            int fanIndex = jsonItem["fanIndex"];
            FanFeature& fan = fanFeatures[fanIndex];

            fan.partNumber = jsonItem["partNumber"];
            fan.prettyName = jsonItem["prettyName"];
            fan.type = static_cast<FanPerformanceType>(jsonItem["type"]);

            std::string strLog;
            strLog = "FAN-" + std::to_string(fanIndex) +
                     ", type: " + std::to_string(static_cast<int>(fan.type)) +
                     ", partNumber: " + fan.partNumber +
                     ", prettyName: " + fan.prettyName;
            log<level::DEBUG>(strLog.c_str());
        }
    }
    catch (const std::exception& ex)
    {
        log<level::ERR>("Failed to read json file",
                        entry("VALUE=%s", sysFanDataFile.c_str()),
                        entry("ERROR=%s", ex.what()));
    }
}

void HWManager::detectFansDelayed(uint32_t delaySecs)
{
    static boost::asio::deadline_timer tmr(io);

    //  if we has expired attempts, then stop fan detection
    //  and work in normal state
    if (++numErrorAttempts > maxErrorAttempts)
    {
        log<level::ERR>("Reached maxErrorAttempts",
                        entry("STATE=%d", int(detectFansState)));
        detectFansState = FanState::normal;
        return;
    }

    // this implicitly cancels the timer
    tmr.expires_from_now(boost::posix_time::seconds(delaySecs));
    tmr.async_wait([&](const boost::system::error_code& err) {
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

        processDetectState();
    });
}

void HWManager::onHostPowerChanged(bool powered)
{
    if (powered)
    {
        log<level::DEBUG>("Host power is ON");
        numErrorAttempts = 0;
    }
    else
    {
        log<level::DEBUG>("Host power is OFF");
    }
}

void HWManager::runDetectFans()
{
    if (detectFansState == FanState::uninit)
    {
        if (powerState.isPowerOn())
        {
            detectFansState = FanState::init;
        }

        processDetectState();
    }
}

void HWManager::processDetectState()
{
    if (detectFansState == FanState::normal)
    {
        return;
    }

    //  wait until createInventory() complete a work
    if (!config.desc)
    {
        detectFansDelayed(5);
        return;
    }

    if (detectFansState != FanState::uninit)
    {
        if (!processSystemFans())
        {
            detectFansState = FanState::normal;
        }
    }

    switch (detectFansState)
    {
        case FanState::uninit:
            if (fanFeatures.size() > 0)
            {
                detectFansState = FanState::normal;
            }
            break;
        case FanState::init:
            detectFansState = FanState::detect;
            detectFansDelayed(5);
            break;
        case FanState::detect:
            updateSystemFanFeatures();
            detectFansState = FanState::normal;
            break;
        default:
            break;
    }

    if (detectFansState == FanState::normal)
    {
        publishSystemFans();
    }
}

bool HWManager::processSystemFans()
{
    if (config.desc->detectionFanTable.empty())
    {
        return false;
    }

    std::string path = getFansControlPath();
    if (path.empty())
    {
        return false;
    }

    std::stringstream ssLog;

    for (const auto& [conIndex, conDescr] : config.desc->fans)
    {
        if (conDescr.type != ConnectorType::SYSTEM)
        {
            continue;
        }

        FanFeature& fan = fanFeatures[conIndex];
        auto pwmPath = path + "pwm" + std::to_string(conDescr.pwmIndex + 1);
        auto inletPath =
            path + "fan" + std::to_string(conDescr.tachIndexA + 1) + "_input";
        auto outletPath =
            path + "fan" + std::to_string(conDescr.tachIndexB + 1) + "_input";

        switch (detectFansState)
        {
            case FanState::init:
                fan.initialPwm = getHwmonValue(pwmPath);
                fan.maxInletRpm = getHwmonValue(inletPath);
                fan.maxOutletRpm = getHwmonValue(outletPath);
                ssLog << std::to_string(fan.initialPwm) << "; ";
                if (fan.maxInletRpm > 0 && fan.maxOutletRpm > 0)
                {
                    setHwmonValue(pwmPath, 255);
                }
                break;

            case FanState::detect:
                if (fan.maxInletRpm > 0 && fan.maxOutletRpm > 0)
                {
                    fan.maxInletRpm = getHwmonValue(inletPath);
                    fan.maxOutletRpm = getHwmonValue(outletPath);
                    setHwmonValue(pwmPath, fan.initialPwm);
                }
                ssLog << std::to_string(fan.maxInletRpm) << "/"
                      << std::to_string(fan.maxOutletRpm) << "; ";
                break;

            default:
                break;
        }
    }

    std::string strLog;
    if (detectFansState == FanState::init)
    {
        strLog = std::string("FAN PWMs: ") + ssLog.str();
    }
    else if (detectFansState == FanState::detect)
    {
        strLog = std::string("FAN RPMs: ") + ssLog.str();
    }
    if (!strLog.empty())
    {
        log<level::DEBUG>(strLog.c_str());
    }
    return true;
}

void HWManager::updateSystemFanFeatures()
{
    for (const auto& [conIndex, conDescr] : config.desc->fans)
    {
        if (conDescr.type != ConnectorType::SYSTEM)
        {
            continue;
        }

        FanFeature& fan = fanFeatures[conIndex];

        const FanModuleInfo& fanInfo = getFanInfoByRPM(
            config.desc->detectionFanTable, fan.maxInletRpm, fan.maxOutletRpm);

        if (fanInfo.type != FanPerformanceType::UNKNOWN &&
            !fanInfo.partNumber.empty() && !fanInfo.prettyName.empty())
        {
            std::stringstream ssLog;
            ssLog << "FAN: updated P/N: " << fanInfo.partNumber << " ("
                  << fanInfo.prettyName << ")";
            log<level::DEBUG>(ssLog.str().c_str());

            fan.partNumber = fanInfo.partNumber;
            fan.prettyName = fanInfo.prettyName;
            fan.type = fanInfo.type;
        }
        else
        {
            auto fanIndexStr = std::to_string(conDescr.fanIndex);

            fan.partNumber = config.desc->sysFanPN;
            fan.prettyName = "System Fan " + fanIndexStr;
        }
    }
}
