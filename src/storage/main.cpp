/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "com/yadro/Storage/Manager/server.hpp"
#include "dbus.hpp"
#include "inventory.hpp"

#include <boost/algorithm/string.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/server.hpp>

#include <fstream>
#include <streambuf>
#include <string>

using namespace phosphor::logging;

static constexpr const char* storageDataFile = "/var/lib/inventory/storage.csv";

using StorageManagerServer = sdbusplus::server::object_t<
    sdbusplus::com::yadro::Storage::server::Manager>;

class Manager : StorageManagerServer
{
  public:
    Manager(sdbusplus::bus::bus& bus) :
        StorageManagerServer(bus, dbus::stormgr::path), bus(bus)
    {}
    void rescan();

  private:
    sdbusplus::bus::bus& bus;
    std::vector<std::shared_ptr<StorageDrive>> drives;
};

enum Fields
{
    path = 0,
    proto,
    type,
    vendor,
    model,
    serial,
    sizeBytes,
    count
};

/**
 * @brief Find storage drives information and create corresponding dbus objects
 *
 */
void Manager::rescan()
{
    std::ifstream dataFile(storageDataFile);
    std::string line;
    if (!dataFile.is_open())
    {
        log<level::ERR>("failed to open file",
                        entry("VALUE=%s", storageDataFile));
        return;
    }

    size_t index = 1;
    drives.clear();
    while (std::getline(dataFile, line))
    {
        std::vector<std::string> entryFields;
        boost::split(entryFields, line, boost::is_any_of(";"));
        if (entryFields.size() == 1)
        {
            continue;
        }
        else if (entryFields.size() < Fields::count)
        {
            log<level::ERR>("file format error",
                            entry("VALUE=%s", line.c_str()));
            return;
        }
        drives.emplace_back(std::make_shared<StorageDrive>(
            bus, "drive " + std::to_string(index), entryFields[path],
            entryFields[proto], entryFields[type], entryFields[vendor],
            entryFields[model], entryFields[serial], entryFields[sizeBytes]));
        index++;
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
    sdbusplus::server::manager_t objManager(bus, dbus::stormgr::path);

    bus.request_name(dbus::stormgr::busName);
    Manager storageManager(bus);

    storageManager.rescan();
    while (1)
    {
        bus.process_discard();
        bus.wait();
    }

    return 0;
}
