/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once

enum class OptionType
{
    macAddr = 0x01,
    cpuCooling = 0x02,
    chassisFans = 0x03,
    pidZoneMinSpeed = 0x04,
    pcieBifurcation = 0x05,
    none = 0x00
};
