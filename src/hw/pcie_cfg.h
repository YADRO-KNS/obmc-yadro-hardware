/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#pragma once
#include <sdbusplus/bus.hpp>

class pcieCfg
{
  public:
    pcieCfg(sdbusplus::bus::bus& bus) : bus(bus)
    {}
    bool addBifurcationConfig(const int& socket, const std::string& optValue);
    ~pcieCfg();

  private:
    std::map<uint16_t, uint8_t> bifurcationConfig;
    sdbusplus::bus::bus& bus;
};
