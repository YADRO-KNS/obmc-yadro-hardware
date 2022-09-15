/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "common_swupd.hpp"

#include "dbus.hpp"

#include <phosphor-logging/log.hpp>

namespace softwareServer = sdbusplus::xyz::openbmc_project::Software::server;
using namespace phosphor::logging;
namespace fs = std::filesystem;

SoftwareObject::SoftwareObject(sdbusplus::bus::bus& bus, std::string objPath,
                               std::string filePath, std::string fwVersion,
                               std::string type, VersionPurpose versionPurpose,
                               std::shared_ptr<FirmwareUpdateble> targetDev) :
    objectPath(objPath),
    ActivationServer(bus, objPath.c_str()), target(targetDev)
{
    std::vector<Association> assoc;
    assoc.emplace_back("inventory", "activation", target->getInventory());
    associations(assoc);
    path(filePath);
    activation(Activations::Ready);
    requestedActivation(RequestedActivations::None);
    extendedVersion(type);
    version(fwVersion);
    purpose(versionPurpose);
}

auto SoftwareObject::activation(Activations value) -> Activations
{
    if (value == softwareServer::Activation::Activations::Activating)
    {
#ifdef WANT_SIGNATURE_VERIFY
        fs::path uploadDir(IMG_UPLOAD_DIR);
        if (!verifySignature(uploadDir / versionId, SIGNED_IMAGE_CONF_PATH))
        {
            onVerifyFailed();
            // Stop the activation process, if fieldMode is enabled.
            if (parent.control::FieldMode::fieldModeEnabled())
            {
                return softwareServer::Activation::activation(
                    softwareServer::Activation::Activations::Failed);
            }
        }
#endif
        fs::path firmwareDir(path());
        std::string imageFilename = extendedVersion() + ".bin";
        if (!target->updateImage(firmwareDir / imageFilename, version(),
                                 objectPath, shared_from_this()))
        {
            return softwareServer::Activation::activation(
                softwareServer::Activation::Activations::Failed);
        }
    }
    return softwareServer::Activation::activation(value);
}

auto SoftwareObject::requestedActivation(RequestedActivations value)
    -> RequestedActivations
{
    if ((value == softwareServer::Activation::RequestedActivations::Active) &&
        ((softwareServer::Activation::activation() ==
          softwareServer::Activation::Activations::Ready) ||
         (softwareServer::Activation::activation() ==
          softwareServer::Activation::Activations::Failed)))
    {
        activation(softwareServer::Activation::Activations::Activating);
    }
    return softwareServer::Activation::requestedActivation(value);
}
