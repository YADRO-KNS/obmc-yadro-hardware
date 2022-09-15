/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021, KNS Group LLC (YADRO)
 */

#include "com/yadro/Inventory/Manager/server.hpp"
#include "dbus.hpp"
#include "adapter.hpp"

#include <phosphor-logging/log.hpp>
#include <sdbusplus/server.hpp>

#include <fstream>
#include <streambuf>
#include <string>

#include <nlohmann/json.hpp>

using namespace phosphor::logging;
using namespace nlohmann;

static constexpr const char* adaptersDataFile = "/var/lib/inventory/mac.json";

using NetworkAdapterManagerServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::Inventory::server::Manager>;

class Manager : NetworkAdapterManagerServer
{
  public:
    Manager(sdbusplus::bus::bus& bus) :
        NetworkAdapterManagerServer(bus, dbus::netadpmgr::path), bus(bus)
    {}
    void rescan();

  private:
    sdbusplus::bus::bus& bus;
    std::vector<std::shared_ptr<NetworkAdapter>> adapters;
};

/**
 * @brief Find network adapters information and create corresponding dbus objects
 *
 */
void Manager::rescan()
{
    std::ifstream dataFile(adaptersDataFile);
    json jsonData;
    if (!dataFile.is_open())
    {
        log<level::ERR>("failed to open file",
                        entry("VALUE=%s", adaptersDataFile));
        return;
    }

    try
    {
        dataFile >> jsonData;
        if (jsonData.is_discarded())
        {
            log<level::ERR>("failed to read network adapters file",
                            entry("VALUE=%s", adaptersDataFile));
            return;
        }

        adapters.clear();
        for (const auto& [adapterName, adapter] : jsonData.items())
        {
            adapters.emplace_back(std::make_shared<NetworkAdapter>(
                bus, adapterName, adapter["Vendor"], adapter["Device"],
                adapter["Mac"]));
        }
    }
    catch (const std::exception& ex)
    {
        log<level::ERR>("failed to read network adapters file",
                        entry("VALUE=%s", adaptersDataFile),
                        entry("ERROR=%s", ex.what()));
    }
}

/**
 * @brief Application entry point
 *
 * @return exit status
 */
int main()
{
    auto bus = sdbusplus::bus::new_default();
    sdbusplus::server::manager_t objManager(bus, "/");

    bus.request_name(dbus::netadpmgr::busName);
    Manager networkAdapterManager(bus);

    networkAdapterManager.rescan();
    while (1)
    {
        bus.process_discard();
        bus.wait();
    }

    return 0;
}
