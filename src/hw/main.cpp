/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "dbus.hpp"
#include "hw_mngr.hpp"

#include <phosphor-logging/log.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

static constexpr uint64_t dbusTimeout = 1 * 1000 * 1000; // Set timeout to 1s

using namespace phosphor::logging;

void createInventoryDelayed(
    boost::asio::io_service& io,
    std::shared_ptr<sdbusplus::asio::connection>& systemBus, HWManager& manager,
    int delay);

bool cpuPresenceUpdate(std::shared_ptr<sdbusplus::asio::connection>& systemBus,
                       HWManager& manager)
{
    SubTreeType objects;
    auto getObjects = systemBus->new_method_call(
        dbus::mapper::busName, dbus::mapper::path, dbus::mapper::interface,
        dbus::mapper::subtree);
    getObjects.append(dbus::inventory::path, 0,
                      std::array<std::string, 1>{dbus::inventory::interface});

    try
    {
        log<level::DEBUG>("Calling GetSubTree for CPU inventory");
        systemBus->call(getObjects, dbusTimeout).read(objects);
        log<level::DEBUG>("GetSubTree call done");
    }
    catch (const sdbusplus::exception::SdBusError& ex)
    {
        log<level::DEBUG>("Error while calling GetSubTree",
                          entry("WHAT=%s", ex.what()));
        return false;
    }

    for (const auto& [path, objDict] : objects)
    {
        static const std::regex cpuRegex(".*/cpu([0-9]+)$", std::regex::icase);
        std::smatch sm;
        if (!std::regex_match(path, sm, cpuRegex) || objDict.empty())
        {
            continue;
        }
        size_t index = 0;
        try
        {
            index = std::stoi(sm[1]);
        }
        catch (std::invalid_argument&)
        {
            log<level::ERR>("Invalid CPU object path",
                            entry("PATH=%s", path.c_str()));
            continue;
        }

        const std::string& owner = objDict.begin()->first;

        DbusProperties data;
        auto getProperties = systemBus->new_method_call(
            owner.c_str(), path.c_str(), dbus::properties::interface,
            dbus::properties::getAll);
        getProperties.append(dbus::inventory::interface);

        try
        {
            log<level::DEBUG>("Calling GetAll for CPU object");
            systemBus->call(getProperties, dbusTimeout).read(data);
            log<level::DEBUG>("GetAll call done");
        }
        catch (const sdbusplus::exception::exception& ex)
        {
            log<level::DEBUG>("Error while calling GetAll",
                              entry("SERVICE=%s", owner.c_str()),
                              entry("PATH=%s", path.c_str()),
                              entry("INTERFACE=%s", dbus::inventory::interface),
                              entry("WHAT=%s", ex.description()));
            return false;
        }

        try
        {
            auto it = data.find(dbus::inventory::properties::Present);
            if (it != data.end())
            {
                manager.config.cpuPresence[index] = std::get<bool>(it->second);
            }
        }
        catch (std::invalid_argument&)
        {
            log<level::ERR>("Error reading property 'Present'",
                            entry("PATH=%s", path.c_str()));
            return false;
        }
    }
    return true;
}

void createInventory(boost::asio::io_service& io,
                     std::shared_ptr<sdbusplus::asio::connection>& systemBus,
                     HWManager& manager)
{
    ManagedObjectType managedObj;
    auto getManagedObjects = systemBus->new_method_call(
        dbus::fru::busName, "/", dbus::objmgr::interface,
        dbus::objmgr::managedObjects);

    try
    {
        log<level::DEBUG>("Calling GetManagedObjects for FruDevice");
        systemBus->call(getManagedObjects, dbusTimeout).read(managedObj);
        log<level::DEBUG>("GetManagedObjects call done");
    }
    catch (const sdbusplus::exception::exception& ex)
    {
        log<level::DEBUG>("Error while calling GetManagedObjects",
                          entry("SERVICE=%s", dbus::fru::busName),
                          entry("PATH=%s", "/"),
                          entry("WHAT=%s", ex.description()));
        createInventoryDelayed(io, systemBus, manager, 30);
        return;
    }

    for (const auto& pathPair : managedObj)
    {

        std::string path = pathPair.first.str;
        if ((path.find("Motherboard") != std::string::npos) ||
            (path.find("Baseboard") != std::string::npos))
        {
            auto findIface = pathPair.second.find(dbus::fru::interface);
            if (findIface == pathPair.second.end())
                continue;

            manager.config.reset();
            for (const auto& [property, value] : findIface->second)
            {
                try
                {
                    if (property == "PRODUCT_PRODUCT_NAME")
                    {
                        manager.setProduct(std::get<std::string>(value));
                    }
                    else if (property == "PRODUCT_PART_NUMBER")
                    {
                        manager.config.chassisPartNumber =
                            std::get<std::string>(value);
                    }
                    else if (property == "PRODUCT_SERIAL_NUMBER")
                    {
                        manager.config.chassisSerial =
                            std::get<std::string>(value);
                    }
                    else if (property.find("PRODUCT_INFO_AM") == 0)
                    {
                        manager.setOption(std::get<std::string>(value));
                    }
                }
                catch (std::exception const& ex)
                {
                    log<level::ERR>("Error while reading FRU data",
                                    entry("WHAT=%s", ex.what()));
                }
            }
            if (manager.config.haveCPUFans)
            {
                if (!cpuPresenceUpdate(systemBus, manager))
                {
                    createInventoryDelayed(io, systemBus, manager, 30);
                    return;
                }
            }
            break;
        }
    }

    manager.publish();
    log<level::DEBUG>("Scan done");
}

void createInventoryDelayed(
    boost::asio::io_service& io,
    std::shared_ptr<sdbusplus::asio::connection>& systemBus, HWManager& manager,
    int delay)
{
    static boost::asio::deadline_timer filterTimer(io);

    // this implicitly cancels the timer
    filterTimer.expires_from_now(boost::posix_time::seconds(delay));
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
        createInventory(io, systemBus, manager);
    });
}

int main()
{
    boost::asio::io_service io;
    auto systemBus = std::make_shared<sdbusplus::asio::connection>(io);
    systemBus->request_name(dbus::hwmgr::busName);
    sdbusplus::asio::object_server objectServer(systemBus);

    HWManager manager(io, static_cast<sdbusplus::bus::bus&>(*systemBus));

    std::function<void(sdbusplus::message::message&)> eventHandler =
        [&](sdbusplus::message::message& message) {
            if (message.is_method_error())
            {
                log<level::ERR>("PropertiesChanged signal error");
                return;
            }

            createInventoryDelayed(io, systemBus, manager, 1);
        };

    auto matchFru = std::make_unique<sdbusplus::bus::match::match>(
        static_cast<sdbusplus::bus::bus&>(*systemBus),
        std::string(
            "type='signal',member='PropertiesChanged',path_namespace='") +
            dbus::fru::path + "',arg0namespace='" + dbus::fru::interface + "'",
        eventHandler);
    auto matchCPU = std::make_unique<sdbusplus::bus::match::match>(
        static_cast<sdbusplus::bus::bus&>(*systemBus),
        std::string(
            "type='signal',member='PropertiesChanged',path_namespace='") +
            dbus::inventory::path + "',arg0namespace='" +
            dbus::inventory::interface + "'",
        eventHandler);

    io.post([&]() { createInventory(io, systemBus, manager); });
    io.run();

    return 0;
}
