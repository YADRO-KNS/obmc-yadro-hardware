/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "common.hpp"

#include "dbus.hpp"

#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/State/Host/server.hpp>

#include <filesystem>

using Host = sdbusplus::xyz::openbmc_project::State::server::Host;
using HostState =
    sdbusplus::xyz::openbmc_project::State::server::Host::HostState;

using namespace phosphor::logging;
namespace fs = std::filesystem;

static bool hostStateToBool(const HostState powerState)
{
    if (powerState == HostState::Running || powerState == HostState::Quiesced ||
        powerState == HostState::DiagnosticMode)
    {
        return true;
    }
    return false;
}

PowerState::PowerState(sdbusplus::bus::bus& aBus, StateChangeFunc callback) :
    bus(aBus)
{
    addCallback("default", std::move(callback));
}

PowerState::PowerState(sdbusplus::bus::bus& aBus) : bus(aBus)
{}

void PowerState::addCallback(const std::string& name, StateChangeFunc callback)
{
    callbacks.emplace(name, std::move(callback));
    if (!match.has_value())
    {
        match.emplace(bus,
                      sdbusplus::bus::match::rules::propertiesChanged(
                          dbus::power::path, dbus::power::interface),
                      [this](auto& msg) { this->hostStateChanged(msg); });
        readHostState();
    }
}

void PowerState::deleteCallback(const std::string& name)
{
    callbacks.erase(name);
}

bool PowerState::isPowerOn() const
{
    return (powerState == systemPowerState::On);
}

void PowerState::setPowerState(bool state)
{
    systemPowerState newState =
        state ? systemPowerState::On : systemPowerState::Off;
    if (newState != powerState)
    {
        powerState = newState;
        for (const auto& [name, callback] : callbacks)
        {
            callback(state);
        }
    }
}

void PowerState::hostStateChanged(sdbusplus::message::message& msg)
{
    Interface interface;
    DbusProperties properties;

    msg.read(interface, properties);

    auto hostStateProp = properties.find(dbus::power::properties::state);
    if (hostStateProp != properties.end())
    {
        auto currentHostState = Host::convertHostStateFromString(
            std::get<std::string>(hostStateProp->second));
        const bool powerState = hostStateToBool(currentHostState);
        setPowerState(powerState);
    }
}

void PowerState::readHostState()
{
    DbusPropVariant data;
    auto getProperty =
        bus.new_method_call(dbus::power::busname, dbus::power::path,
                            dbus::properties::interface, dbus::properties::get);
    getProperty.append(dbus::power::interface, dbus::power::properties::state);

    try
    {
        log<level::DEBUG>("Calling Get for Host State object");
        bus.call(getProperty).read(data);
        log<level::DEBUG>("Get call done");
    }
    catch (const sdbusplus::exception::exception& ex)
    {
        log<level::ERR>("Error while calling Get",
                        entry("SERVICE=%s", dbus::power::busname),
                        entry("PATH=%s", dbus::power::path),
                        entry("INTERFACE=%s", dbus::pid::interface),
                        entry("WHAT=%s", ex.description()));
        return;
    }

    auto currentHostState =
        Host::convertHostStateFromString(std::get<std::string>(data));
    const bool powerState = hostStateToBool(currentHostState);
    setPowerState(powerState);
}

static constexpr const char* muxSymlinkDirPath = "/dev/i2c-mux";
static constexpr int symlinkDepth = 1;

std::string getBusByChanName(const std::string& chanName)
{
    fs::path muxSymlinkDir(muxSymlinkDirPath);
    std::error_code ec;
    if (!fs::exists(muxSymlinkDir, ec))
    {
        log<level::ERR>("I2C mux directory does not exists",
                        entry("PATH=%s", muxSymlinkDirPath),
                        entry("ERROR=%s", ec.message().c_str()));
        return std::string();
    }

    std::string busDevice;
    for (auto p = fs::recursive_directory_iterator(
             muxSymlinkDir, fs::directory_options::follow_directory_symlink);
         p != fs::recursive_directory_iterator(); ++p)
    {
        fs::path path = p->path();
        if (!is_directory(*p))
        {
            if (path.filename() == chanName)
            {
                const std::string busFile = fs::read_symlink(*p, ec);
                if (ec)
                {
                    log<level::ERR>("Can't read link destination",
                                    entry("PATH=%s", path.c_str()),
                                    entry("ERROR=%s", ec.message().c_str()));
                }
                return busFile;
            }
        }
        if (p.depth() >= symlinkDepth)
        {
            p.disable_recursion_pending();
        }
    }
    return std::string();
}

void rtrim(std::string& str, const std::string& chars)
{
    static const std::regex isPrintableRegex("^[[:print:]]+$",
                                             std::regex::optimize);
    static const std::regex notPrintableRegex("[^[:print:]]",
                                              std::regex::optimize);
    str.erase(str.find_last_not_of(chars) + 1);
    str.resize(strlen(str.c_str()));
    if (!std::regex_match(str, isPrintableRegex))
    {
        log<level::INFO>("String contains non-printable characters",
                         entry("VALUE=%s", str.c_str()));
        str = std::regex_replace(str, notPrintableRegex, "_");
    }
}
