/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#pragma once

#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/FilePath/server.hpp>
#include <xyz/openbmc_project/Software/Activation/server.hpp>
#include <xyz/openbmc_project/Software/ExtendedVersion/server.hpp>
#include <xyz/openbmc_project/Software/Version/server.hpp>

#include <filesystem>
#include <string>

using ActivationServer = sdbusplus::server::object::object<
    sdbusplus::xyz::openbmc_project::Software::server::Activation,
    sdbusplus::xyz::openbmc_project::Software::server::ExtendedVersion,
    sdbusplus::xyz::openbmc_project::Software::server::Version,
    sdbusplus::xyz::openbmc_project::Common::server::FilePath,
    sdbusplus::xyz::openbmc_project::Association::server::Definitions>;

class FirmwareUpdateble;

/**
 * @class SoftwareObject
 *
 * This class implements software update interface
 */
class SoftwareObject :
    ActivationServer,
    public std::enable_shared_from_this<SoftwareObject>
{
  public:
    SoftwareObject(sdbusplus::bus::bus& bus, std::string objPath,
                   std::string filePath, std::string fwVersion,
                   std::string type, VersionPurpose versionPurpose,
                   std::shared_ptr<FirmwareUpdateble> targetDev);

    /** @brief Overloaded Activation property setter function
     *
     * @param[in] value - One of Activation::Activations
     *
     * @return Success or exception thrown
     */
    Activations activation(Activations value) override;

    /** @brief Overloaded requestedActivation property setter function
     *
     * @param[in] value - One of Activation::RequestedActivations
     *
     * @return Success or exception thrown
     */
    RequestedActivations
        requestedActivation(RequestedActivations value) override;

    std::string getVersion()
    {
        return version();
    };

  private:
    std::shared_ptr<FirmwareUpdateble> target;
    std::string objectPath;
};

class FirmwareUpdateble
{
  public:
    virtual std::string getType() = 0;
    virtual bool updateImage(std::filesystem::path imagePath,
                             std::string imageVersion, std::string dbusObject,
                             std::shared_ptr<SoftwareObject> updater) = 0;
};
