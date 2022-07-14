/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2022, KNS Group LLC (YADRO)
 */

#include "backplane_mcu_driver.hpp"

#include <stdexcept>

/* Backplane MCU request proto version ID */
constexpr uint8_t mcuGetTypeId = 0x00;

std::unique_ptr<BackplaneMCUDriver> backplaneMCU(std::string devPath, int addr)
{
    auto dev = std::make_unique<i2cDev>(devPath, addr);
    if (dev && dev->isOk())
    {
        int res = dev->read_byte_data(mcuGetTypeId);
        if (res == MCUProtoV0::ident())
        {
            return std::make_unique<MCUProtoV0>(std::move(dev));
        }
        if (res == MCUProtoV1::ident())
        {
            return std::make_unique<MCUProtoV1>(std::move(dev));
        }
    }

    throw std::runtime_error("Failed to initialize MCU driver");
}
