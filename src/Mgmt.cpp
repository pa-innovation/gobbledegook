// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This file contains various functions for interacting with Bluetooth Management interface, which provides adapter configuration.
//
// >>
// >>>  DISCUSSION
// >>
//
// We only cover the basics here. If there are configuration features you need that aren't supported (such as configuring BR/EDR),
// then this would be a good place for them.
//
// Note that this class relies on the `HciAdapter`, which is a very primitive implementation. Use with caution.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <string.h>

#include "Mgmt.h"
#include "Logger.h"
#include "Utils.h"

namespace ggk {

// Construct the Mgmt device
//
// Set `controllerIndex` to the zero-based index of the device as recognized by the OS. If this parameter is omitted, the index
// of the first device (0) will be used.
Mgmt::Mgmt(uint16_t controllerIndex)
: controllerIndex(controllerIndex)
{
	HciAdapter::getInstance().sync(controllerIndex);
}

// Set the adapter name and short name
//
// The inputs `name` and `shortName` may be truncated prior to setting them on the adapter. To ensure that `name` and
// `shortName` conform to length specifications prior to calling this method, see the constants `kMaxAdvertisingNameLength` and
// `kMaxAdvertisingShortNameLength`. In addition, the static methods `truncateName()` and `truncateShortName()` may be helpful.
//
// Returns true on success, otherwise false
bool Mgmt::setName(std::string name, std::string shortName)
{
	// Ensure their lengths are okay
	name = truncateName(name);
	shortName = truncateShortName(shortName);

	struct SRequest : HciAdapter::HciHeader
	{
		char name[249];
		char shortName[11];
	} __attribute__((packed));

	SRequest request;
	request.code = Mgmt::ESetLocalNameCommand;
	request.controllerId = controllerIndex;
	request.dataSize = sizeof(SRequest) - sizeof(HciAdapter::HciHeader);

	memset(request.name, 0, sizeof(request.name));
	snprintf(request.name, sizeof(request.name), "%s", name.c_str());

	memset(request.shortName, 0, sizeof(request.shortName));
	snprintf(request.shortName, sizeof(request.shortName), "%s", shortName.c_str());

	if (!HciAdapter::getInstance().sendCommand(request))
	{
		Logger::warn(SSTR << "  + Failed to set name");
		return false;
	}

	return true;
}

// Sets discoverable mode
// 0x00 disables discoverable
// 0x01 enables general discoverable
// 0x02 enables limited discoverable
// Timeout is the time in seconds. For 0x02, the timeout value is required.
bool Mgmt::setDiscoverable(uint8_t disc, uint16_t timeout)
{
	struct SRequest : HciAdapter::HciHeader
	{
		uint8_t disc;
		uint16_t timeout;
	} __attribute__((packed));

	SRequest request;
	request.code = Mgmt::ESetDiscoverableCommand;
	request.controllerId = controllerIndex;
	request.dataSize = sizeof(SRequest) - sizeof(HciAdapter::HciHeader);
	request.disc = disc;
	request.timeout = timeout;

	if (!HciAdapter::getInstance().sendCommand(request))
	{
		Logger::warn(SSTR << "  + Failed to set discoverable");
		return false;
	}

	return true;
}

// Set a setting state to 'newState'
//
// Many settings are set the same way, this is just a convenience routine to handle them all
//
// Returns true on success, otherwise false
bool Mgmt::setState(uint16_t commandCode, uint16_t controllerId, uint8_t newState)
{
	struct SRequest : HciAdapter::HciHeader
	{
		uint8_t state;
	} __attribute__((packed));

	SRequest request;
	request.code = commandCode;
	request.controllerId = controllerId;
	request.dataSize = sizeof(SRequest) - sizeof(HciAdapter::HciHeader);
	request.state = newState;

	if (!HciAdapter::getInstance().sendCommand(request))
	{
		Logger::warn(SSTR << "  + Failed to set " << HciAdapter::kCommandCodeNames[commandCode] << " state to: " << static_cast<int>(newState));
		return false;
	}

	return true;
}

// Set the powered state to `newState` (true = powered on, false = powered off)
//
// Returns true on success, otherwise false
bool Mgmt::setPowered(bool newState)
{
	return setState(Mgmt::ESetPoweredCommand, controllerIndex, newState ? 1 : 0);
}

// Set the BR/EDR state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setBredr(bool newState)
{
	return setState(Mgmt::ESetBREDRCommand, controllerIndex, newState ? 1 : 0);
}

// Set the Secure Connection state (0 = disabled, 1 = enabled, 2 = secure connections only mode)
//
// Returns true on success, otherwise false
bool Mgmt::setSecureConnections(uint8_t newState)
{
	return setState(Mgmt::ESetSecureConnectionsCommand, controllerIndex, newState);
}

// Set the Link Layer Security state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setLLS(bool newState)
{
    return setState(Mgmt::ESetLinkSecurityCommand, controllerIndex, newState ? 1 : 0);
}

// Set the bondable state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setBondable(bool newState)
{
	return setState(Mgmt::ESetBondableCommand, controllerIndex, newState ? 1 : 0);
}

// Set the connectable state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setConnectable(bool newState)
{
	return setState(Mgmt::ESetConnectableCommand, controllerIndex, newState ? 1 : 0);
}

// Set the LE state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setLE(bool newState)
{
	return setState(Mgmt::ESetLowEnergyCommand, controllerIndex, newState ? 1 : 0);
}

// Set the SSP state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setSSP(bool newState)
{
    return setState(Mgmt::ESetSecureSimplePairingCommand, controllerIndex, newState ? 1 : 0);
}

// Set the HC state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setHC(bool newState)
{
    return setState(Mgmt::ESetHighSpeedCommand, controllerIndex, newState ? 1 : 0);
}

// Set the FC state to `newState` (true = enabled, false = disabled)
//
// Returns true on success, otherwise false
bool Mgmt::setFC(bool newState)
{
    return setState(Mgmt::ESetFastConnectableCommand, controllerIndex, newState ? 1 : 0);
}

// Sets a custom advertising using addAdvertising if newState > 0. otherwise calls delAdvertising and setAdvertising(0)
//
// Returns true on success, otherwise false
bool Mgmt::setAdvertising(bool newState, std::string name, std::string shortName)
{
    // addAdvertising only works if setAdvertising (the automatic advertiser) is turned off
    if(!setState(Mgmt::ESetAdvertisingCommand, controllerIndex, 0)) {
        Logger::error(SSTR << "Failed to setAdvertising to 0");
        return false;
    }

    // Get the Advertising Features to see what we can do.
    HciAdapter::HciHeader readAdvCmd;
    readAdvCmd.code = Mgmt::EReadAdvertisingFeaturesCommand;
    readAdvCmd.controllerId = controllerIndex;
    readAdvCmd.dataSize = 0;
    if(!HciAdapter::getInstance().sendCommand(readAdvCmd)) {
        Logger::error(SSTR << "Failed to send ReadAdvertisingFeaturesCommand");
        return false;
    }

    // TODO: this was the return from the previous command (not sure if this is synchronous here, i may have created an issue)
    HciAdapter::AdvertisingFeatures availableFeatures = HciAdapter::getInstance().getAdvertisingFeatures();
    Logger::warn(SSTR << "FEATURES FLAGS ARE " << Utils::hex(availableFeatures.supportedFlags.masks));

    // if there were any previous addAdvertising pages, remove them
    if( availableFeatures.numInstances != 0 ) {
        struct RemRequest : HciAdapter::HciHeader
        {
            uint8_t instance;
        } __attribute__((packed));

        int numToRemove = availableFeatures.numInstances;
        std::vector<uint8_t> instanceNums;
        for(int i = 0; i < numToRemove; i++) {
            instanceNums.push_back(availableFeatures.instanceRef[i]);
        }
        for(uint8_t instanceNum : instanceNums) {
            RemRequest removeAdvCmd;
            removeAdvCmd.code = Mgmt::ERemoveAdvertisingCommand;
            removeAdvCmd.controllerId = controllerIndex;
            removeAdvCmd.dataSize = 1;
            removeAdvCmd.instance = instanceNum;
            if(!HciAdapter::getInstance().sendCommand(removeAdvCmd)) {
                Logger::error(SSTR << "Failed to send RemoveAdvertisingCommand");
                return false;
            }
        }
    }

    // if any advertising was wanted, add custom attributes
    if( newState ) {
        HciAdapter::AdvertisingSettings wantedFeatures;
        wantedFeatures.masks = HciAdapter::EAdvSwitchConnectable | HciAdapter::EAdvDiscoverable |
               HciAdapter::EAdvAddFlags | HciAdapter::EAdvAddTX;
        // Dont use HciAdapter::EAdvAddLocalName (automatic name adding) or this will fail below (we are manually putting names in)
        // Dont use HciAdapter::EAdvAddAppearance (automatic appearance/CoD) or this will fail (we are manually adding CoD)

        // 0x06 incomplete 128bit service list
        // 0x09 full name
        // 0x08 short name
        // 0x0D class of device (3 bytes) (possibly sent in GAP appearance?)

        // only turn on the features that are available
        wantedFeatures.masks &= availableFeatures.supportedFlags.masks;
        Logger::warn(SSTR << "ACTIVATED FEATURES FLAGS ARE " << Utils::hex(wantedFeatures.masks));
        wantedFeatures.toNetwork();
        // 18 bytes for full name "Doppler-12345678"
        // 5 bytes for CoD
        const int ADV_DATA_LEN = 23; //31 bytes max

        // 9 bytes for short name "Doppler"
        // 18 bytes for service list
        const int SCAN_RESP_LEN = 27; //27 bytes max (4 of them were used for flags and tx)

        struct SRequest : HciAdapter::HciHeader
        {
            uint8_t instance;
            HciAdapter::AdvertisingSettings flags;
            uint16_t duration;
            uint16_t timeout;
            uint8_t advDataLen;
            uint8_t scanRespLen;
            uint8_t advData[ADV_DATA_LEN];
            uint8_t scanResp[SCAN_RESP_LEN];
        } __attribute__((packed));

        SRequest request;
        request.code = Mgmt::EAddAdvertisingCommand;
        request.controllerId = controllerIndex;
        request.dataSize = sizeof(SRequest) - sizeof(HciAdapter::HciHeader);
        // USE 1 for now, assume no other instances exist
        request.instance = 1;
        request.flags = wantedFeatures;
        request.duration = 0;
        request.timeout = 0;
        request.advDataLen = ADV_DATA_LEN;
        request.scanRespLen = SCAN_RESP_LEN;

        request.advData[0] = 17;
        request.advData[1] = 0x09; // full name
        memcpy(request.advData + 2, name.c_str(), 16);

        // TODO: this doesnt seem to work
        request.advData[18] = 4;
        request.advData[19] = 0x0D; // CoD
        request.advData[20] = 0x20;
        request.advData[21] = 0x04;
        request.advData[22] = 0x14;

        request.scanResp[0] = 17;
        request.scanResp[1] = 0x06; // incomplete service list
        // 0x8e7934bdf06d48f6860483c94e0ec8f9
        request.scanResp[2] = 0x8e;
        request.scanResp[3] = 0x79;
        request.scanResp[4] = 0x34;
        request.scanResp[5] = 0xbd;
        request.scanResp[6] = 0xf0;
        request.scanResp[7] = 0x6d;
        request.scanResp[8] = 0x48;
        request.scanResp[9] = 0xf6;
        request.scanResp[10] = 0x86;
        request.scanResp[11] = 0x04;
        request.scanResp[12] = 0x83;
        request.scanResp[13] = 0xc9;
        request.scanResp[14] = 0x4e;
        request.scanResp[15] = 0x0e;
        request.scanResp[16] = 0xc8;
        request.scanResp[17] = 0xf9;

        request.scanResp[18] = 8;
        request.scanResp[19] = 0x08; // short name
        // note: the shortName string is not used because it must match EXACTLY the
        //  first characters of the long name or it will be rejected
        memcpy(request.scanResp + 20, name.c_str(), 7);

        return(HciAdapter::getInstance().sendCommand(request));
    }

    return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Utilitarian
// ---------------------------------------------------------------------------------------------------------------------------------

// Truncates the string `name` to the maximum allowed length for an adapter name. If `name` needs no truncation, a copy of
// `name` is returned.
std::string Mgmt::truncateName(const std::string &name)
{
	if (name.length() <= kMaxAdvertisingNameLength)
	{
		return name;
	}

	return name.substr(0, kMaxAdvertisingNameLength);
}

// Truncates the string `name` to the maximum allowed length for an adapter short-name. If `name` needs no truncation, a copy
// of `name` is returned.
std::string Mgmt::truncateShortName(const std::string &name)
{
	if (name.length() <= kMaxAdvertisingShortNameLength)
	{
		return name;
	}

	return name.substr(0, kMaxAdvertisingShortNameLength);
}

}; // namespace ggk
