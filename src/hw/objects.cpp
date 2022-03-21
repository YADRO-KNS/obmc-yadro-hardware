/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include "objects.hpp"

#include "dbus.hpp"

Chassis::Chassis(sdbusplus::bus::bus& bus, const std::string& aName,
                 const std::string& aModel, const std::string& aPartNumber,
                 const std::string& aSerial) :
    HWManagerChassisServer(
        bus, dbusEscape(std::string(dbus::hwmgr::path) + "/chassis/" + aName)
                 .c_str())
{
    name(aName);
    model(aModel);
    partNumber(aPartNumber);
    serial(aSerial);
}

Fan::Fan(sdbusplus::bus::bus& bus, const std::string& aName,
         const std::string& aPrettyName, const std::string& aModel,
         const std::string& aPartNumber, const std::string& aZone,
         const std::string& aConnector, const uint32_t& aTachIndexA,
         const uint32_t& aTachIndexB, const uint32_t& aPwmIndex,
         const uint32_t& aPwmLimitMax) :
    HWManagerFanServer(
        bus,
        dbusEscape(std::string(dbus::hwmgr::path) + "/fan/" + aName).c_str())
{
    name(aName);
    prettyName(aPrettyName);
    model(aModel);
    partNumber(aPartNumber);
    zone(aZone);
    connector(aConnector);
    // FIXME: we need to hardcode tachometer indexes since
    // FANSensor too stupid to take them from Connector. We need to refactor
    // FANSensor to make it possible to use 'Tachs' parameter.
    tachIndexA(aTachIndexA);
    tachIndexB(aTachIndexB);
    // FIXME: we need to hardcode PWM indexes since EntityManager
    // bind connector functionality is broken: it cant't bind in case Fan
    // object created early then Chassis object. There is "FOUND" Match
    // condition that should help to fight the race but it doesn't work.
    pwmIndex(aPwmIndex);
    if (aPwmLimitMax >= 30 && aPwmLimitMax <= 100)
    {
        pwmLimitMax(aPwmLimitMax);
    }
}
