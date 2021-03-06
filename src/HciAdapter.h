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
// Protocol-level code for the Bluetooth Management API, which is used to configure the Bluetooth adapter
//
// >>
// >>>  DISCUSSION
// >>
//
// This class is intended for use by `Mgmt` (see Mgmt.cpp).
//
// See the discussion at the top of HciAdapter.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <stdint.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <iomanip>

#include "HciSocket.h"
#include "Utils.h"
#include "Logger.h"

namespace ggk {

class HciAdapter
{
public:

	//
	// Constants
	//

	// How long to wait for a response event for commands sent to the adapter
	static const int kMaxEventWaitTimeMS = 1000;

	// A constant referring to a 'non-controller' (for commands that do not require a controller index)
	static const uint16_t kNonController = 0xffff;

	// Command code names
	static const int kMinCommandCode = 0x0001;
	static const int kMaxCommandCode = 0x0043;
	static const char * const kCommandCodeNames[kMaxCommandCode + 1];

	// Event type names
	static const int kMinEventType = 0x0001;
	static const int kMaxEventType = 0x0025;
	static const char * const kEventTypeNames[kMaxEventType + 1];

	static const int kMinStatusCode = 0x00;
	static const int kMaxStatusCode = 0x14;
	static const char * const kStatusCodes[kMaxStatusCode + 1];

	//
	// Types
	//

	// HCI Controller Settings
	//the notes in this struct represent whether we need these parameters to be set when initializing the
	//adapter for ggk gatt coming up and BR/EDR coming down
	enum HciControllerSettings
	{
		EHciPowered = (1<<0),                   //both  //must turn off first before turning on.
		EHciConnectable = (1<<1),               //both
		EHciFastConnectable = (1<<2),           //optional both
		EHciDiscoverable = (1<<3),              //both
		EHciBondable = (1<<4),                  //ggk
		EHciLinkLevelSecurity = (1<<5),         //ggk, possibly a2dp both
		EHciSecureSimplePairing = (1<<6),       //both
		EHciBasicRate_EnhancedDataRate = (1<<7),//a2dp only
		EHciHighSpeed = (1<<8),                 //a2dp only
		EHciLowEnergy = (1<<9),                 //ggk only
		EHciAdvertising = (1<<10),              //both
		EHciSecureConnections = (1<<11),        //ggk
		EHciDebugKeys = (1<<12),                //neither
		EHciPrivacy = (1<<13),                  //neither
		EHciControllerConfiguration = (1<<14),  //unknown
		EHciStaticAddress = (1<<15)             //neither
	};

    // Advertising Features Settings
    enum AdvertisingFeaturesSettings
    {
        EAdvSwitchConnectable = (1<<0),
        EAdvDiscoverable = (1<<1),
        EAdvLimitedDiscoverable = (1<<2),
        EAdvAddFlags = (1<<3),
        EAdvAddTX = (1<<4),
        EAdvAddAppearance = (1<<5),
        EAdvAddLocalName = (1<<6),
        EAdvSecondaryLE1M = (1<<7),
        EAdvSecondaryLE2M = (1<<8),
        EAdvSecondaryLECoded = (1<<9),
    };

	// Major Service Classes from www.bluetooth.com/specifications/assigned-numbers/baseband/
	enum MajorServiceClasses
	{
	    ELimitedDiscoverableModeMSC = (1<<13),
	    EReserved14MSC = (1<<14),
	    EReserved15MSC = (1<<15),
	    EPositioningMSC = (1<<16),
	    ENetworkingMSC = (1<<17),
	    ERenderingMSC = (1<<18),
	    ECapturingMSC = (1<<19),
	    EObjectTransferMSC = (1<<20),
	    EAudioMSC = (1<<21),
	    ETelephonyMSC = (1<<22),
	    EInformationMSC = (1<<23)
	};

	enum MajorDeviceClasses
	{
	    EMiscellaneousMDC = 0x0000,
	    EComputerMDC = 0x0100,
	    EPhoneMDC = 0x0200,
	    ELanNetworkMDC = 0x0300,
	    EAudioVideoMDC = 0x0400,
	    EPeripheralMDC = 0x0500,
	    EImagingMDC = 0x0600,
	    EWearableMDC = 0x0700,
	    EToyMDC = 0x0800,
	    EHealthMDC = 0x0900,
	    EUncategorizedMDC = 0x1F00
	};

	struct HciHeader
	{
		uint16_t code;
		uint16_t controllerId;
		uint16_t dataSize;

		void toNetwork()
		{
			code = Utils::endianToHci(code);
			controllerId = Utils::endianToHci(controllerId);
			dataSize = Utils::endianToHci(dataSize);
		}

		void toHost()
		{
			code = Utils::endianToHost(code);
			controllerId = Utils::endianToHost(controllerId);
			dataSize = Utils::endianToHost(dataSize);
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> Request header\n";
			text += "  + Command code       : " + Utils::hex(code) + " (" + HciAdapter::kCommandCodeNames[code] + ")\n";
			text += "  + Controller Id      : " + Utils::hex(controllerId) + "\n";
			text += "  + Data size          : " + std::to_string(dataSize) + " bytes";
			return text;
		}
	} __attribute__((packed));

	static std::string printClassOfDevice(uint32_t bitfield) {
	    //bitfield is actually only 24 bits, but is easy to work with as a uint

	    std::string text = "";
        // Format #1
        if( ( bitfield & 0x03 ) == 0x00 ) {
            std::string majSrvClass = "";
            if (bitfield & ELimitedDiscoverableModeMSC) { majSrvClass += "Limited Discoverable Mode, "; }
            if (bitfield & EPositioningMSC) { majSrvClass += "Positioning, "; }
            if (bitfield & ENetworkingMSC) { majSrvClass += "Networking, "; }
            if (bitfield & ERenderingMSC) { majSrvClass += "Rendering, "; }
            if (bitfield & ECapturingMSC) { majSrvClass += "Capturing, "; }
            if (bitfield & EObjectTransferMSC) { majSrvClass += "Object Transfer, "; }
            if (bitfield & EAudioMSC) { majSrvClass += "Audio, "; }
            if (bitfield & ETelephonyMSC) { majSrvClass += "Telephony, "; }
            if (bitfield & EInformationMSC) { majSrvClass += "Information, "; }
            if (majSrvClass.length() != 0)
            {
                majSrvClass = majSrvClass.substr(0, majSrvClass.length() - 2);
            }

            text += "  + CoD Format         : 00 (Format #1)\n";
            text += "  + Major Service Class: " + majSrvClass + "\n";

            // bits 8 through 12 are the major device class
            uint16_t majorDeviceClass = bitfield & 0x1F00;
            // bits 2 through 7. shift it back by 2 to make it easy to work with
            uint8_t minorDeviceClass = (bitfield & 0xFC) >> 2;
            text += "  + Major Device Class : ";
            switch(majorDeviceClass) {
            case EMiscellaneousMDC:
                text += "Miscellaneous\n"; break;
            case EComputerMDC:
                text += "Computer\n";
                text += "  + Minor Device Class : ";
                switch(minorDeviceClass) {
                case 0x00:
                    text += "Uncategorized\n"; break;
                case 0x01:
                    text += "Desktop Workstation\n"; break;
                case 0x02:
                    text += "Server-class computer\n"; break;
                case 0x03:
                    text += "Laptop\n"; break;
                case 0x04:
                    text += "Handheld PC/PDA\n"; break;
                case 0x05:
                    text += "Palm-size PC/PDA\n"; break;
                case 0x06:
                    text += "Wearable computer\n"; break;
                case 0x07:
                    text += "Tablet\n"; break;
                default:
                    text += "Unknown Reserved Value: " + Utils::hex(minorDeviceClass) + "\n";
                }
                break;
            case EPhoneMDC:
                text += "Phone\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case ELanNetworkMDC:
                text += "Lan/Network Access Point\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EAudioVideoMDC:
                text += "Audio/Video\n";
                text += "  + Minor Device Class : ";
                switch(minorDeviceClass) {
                case 0x00:
                    text += "Uncategorized\n"; break;
                case 0x01:
                    text += "Wearable Headset Device\n"; break;
                case 0x02:
                    text += "Hands-free Device\n"; break;
                case 0x03:
                    text += "Reserved (000011)\n"; break;
                case 0x04:
                    text += "Microphone\n"; break;
                case 0x05:
                    text += "Loudspeaker\n"; break;
                case 0x06:
                    text += "Headphones\n"; break;
                case 0x07:
                    text += "Portable Audio\n"; break;
                case 0x08:
                    text += "Car Audio\n"; break;
                case 0x09:
                    text += "Set-top box\n"; break;
                case 0x0A:
                    text += "HiFi Audio Device\n"; break;
                case 0x0B:
                    text += "VCR ... really?\n"; break;
                case 0x0C:
                    text += "Video Camera\n"; break;
                case 0x0D:
                    text += "Camcorder\n"; break;
                case 0x0E:
                    text += "Video Monitor\n"; break;
                case 0x0F:
                    text += "Video Display and Loudspeaker\n"; break;
                case 0x10:
                    text += "Video Conferencing\n"; break;
                case 0x11:
                    text += "Reserved (010001)\n"; break;
                case 0x12:
                    text += "Gaming/Toy\n"; break;
                default:
                    text += "Unknown Reserved Value: " + Utils::hex(minorDeviceClass) + "\n";
                }
                break;
            case EPeripheralMDC:
                text += "Peripheral\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EImagingMDC:
                text += "Imaging\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EWearableMDC:
                text += "Wearable\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EToyMDC:
                text += "Toy\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EHealthMDC:
                text += "Health\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            case EUncategorizedMDC:
                text += "Uncategorized\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
                break;
            default:
                text += "Unknown Reserved Value: " + Utils::hex(majorDeviceClass) + "\n";
                text += "  + Minor Device Class : " + Utils::hex(minorDeviceClass) + "\n";
            }
        } else {
            text += "  + CoD data           : " + Utils::hex(bitfield) + "\n";
        }
        return text;
	}

	struct CommandCompleteEvent
	{
		HciHeader header;
		uint16_t commandCode;
		uint8_t status;

		CommandCompleteEvent(const std::vector<uint8_t> &data)
		{
			*this = *reinterpret_cast<const CommandCompleteEvent *>(data.data());
			toHost();

			// Log it
			Logger::info(debugText());
		}

		void toNetwork()
		{
			header.toNetwork();
			commandCode = Utils::endianToHci(commandCode);
		}

		void toHost()
		{
			header.toHost();
			commandCode = Utils::endianToHost(commandCode);
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> Command complete event\n";
			text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
			text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
			text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
			text += "  + Command code       : " + Utils::hex(commandCode) + " (" + HciAdapter::kCommandCodeNames[commandCode] + ")\n";
			text += "  + Status             : " + Utils::hex(status);
			return text;
		}
	} __attribute__((packed));

	struct CommandStatusEvent
	{
		HciHeader header;
		uint16_t commandCode;
		uint8_t status;

		CommandStatusEvent(const std::vector<uint8_t> &data)
		{
			*this = *reinterpret_cast<const CommandStatusEvent *>(data.data());
			toHost();

			// Log it
			if( status ) {
			    Logger::error(debugText());
			} else {
			    Logger::info(debugText());
			}
		}

		void toNetwork()
		{
			header.toNetwork();
			commandCode = Utils::endianToHci(commandCode);
		}

		void toHost()
		{
			header.toHost();
			commandCode = Utils::endianToHost(commandCode);
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> Command status event\n";
			text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
			text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
			text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
			text += "  + Command code       : " + Utils::hex(commandCode) + " (" + HciAdapter::kCommandCodeNames[commandCode] + ")\n";
			text += "  + Status             : " + Utils::hex(status) + " (" + HciAdapter::kStatusCodes[status] + ")";
			return text;
		}
	} __attribute__((packed));

	struct DeviceConnectedEvent
	{
		HciHeader header;
		uint8_t address[6];
		uint8_t addressType;
		uint32_t flags;
		uint16_t eirDataLength;

		DeviceConnectedEvent(const std::vector<uint8_t> &data)
		{
			*this = *reinterpret_cast<const DeviceConnectedEvent *>(data.data());
			toHost();

			// Log it
			Logger::info(debugText());
		}

		void toNetwork()
		{
			header.toNetwork();
			flags = Utils::endianToHci(flags);
			eirDataLength = Utils::endianToHci(eirDataLength);
		}

		void toHost()
		{
			header.toHost();
			flags = Utils::endianToHost(flags);
			eirDataLength = Utils::endianToHost(eirDataLength);
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> DeviceConnected event\n";
			text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
			text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
			text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
			text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
			text += "  + Address type       : " + Utils::hex(addressType) + "\n";
			text += "  + Flags              : " + Utils::hex(flags) + "\n";
			text += "  + EIR Data Length    : " + Utils::hex(eirDataLength);
			if (eirDataLength > 0)
			{
				text += "\n";
				text += "  + EIR Data           : " + Utils::hex(reinterpret_cast<uint8_t *>(&eirDataLength) + 2, eirDataLength);
			}
			return text;
		}
	} __attribute__((packed));

	struct DeviceDisconnectedEvent
	{
		HciHeader header;
		uint8_t address[6];
		uint8_t addressType;
		uint8_t reason;

		DeviceDisconnectedEvent(const std::vector<uint8_t> &data)
		{
			*this = *reinterpret_cast<const DeviceDisconnectedEvent *>(data.data());
			toHost();

			// Log it
			Logger::info(debugText());
		}

		void toNetwork()
		{
			header.toNetwork();
		}

		void toHost()
		{
			header.toHost();
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> DeviceDisconnected event\n";
			text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
			text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
			text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
			text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
			text += "  + Address type       : " + Utils::hex(addressType) + "\n";
			text += "  + Reason             : " + Utils::hex(reason);
			return text;
		}
	} __attribute__((packed));

    struct AuthenticationFailedEvent
    {
        HciHeader header;
        uint8_t address[6];
        uint8_t addressType;
        uint8_t reason;

        AuthenticationFailedEvent(const std::vector<uint8_t> &data)
        {
            *this = *reinterpret_cast<const AuthenticationFailedEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork()
        {
            header.toNetwork();
        }

        void toHost()
        {
            header.toHost();
        }

        std::string debugText()
        {
            std::string text = "";
            text += "> DeviceDisconnected event\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
            text += "  + Address type       : " + Utils::hex(addressType) + "\n";
            text += "  + Reason             : " + Utils::hex(reason);
            return text;
        }
    } __attribute__((packed));

    struct ClassOfDeviceChangedEvent
    {
        HciHeader header;
        uint8_t classOfDevice[3];

        ClassOfDeviceChangedEvent(const std::vector<uint8_t> &data)
        {
            *this = *reinterpret_cast<const ClassOfDeviceChangedEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork()
        {
            header.toNetwork();
        }

        void toHost()
        {
            header.toHost();
        }

        std::string debugText()
        {
            uint32_t bitfield = (((uint32_t)classOfDevice[0]) << 16) + (((uint32_t)classOfDevice[1]) << 8) + (uint32_t)classOfDevice[2];
            std::string text = "";
            text += "> Class of Device Changed event\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " (" + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += printClassOfDevice(bitfield);
            return text;
        }
    } __attribute__((packed));

    struct NewLinkKeyEvent {
            HciHeader header;
            uint8_t store_hint;
            uint8_t key_address[6];
            uint8_t key_addressType;
            uint8_t key_type;
            uint8_t key_data[16];
            uint8_t key_pinLength;

            NewLinkKeyEvent(const std::vector<uint8_t> &data) {
                *this = *reinterpret_cast<const NewLinkKeyEvent *>(data.data());
                toHost();

                // Log it
                Logger::info(debugText());
            }

            void toNetwork() {
                header.toNetwork();
            }

            void toHost() {
                header.toHost();
            }

            std::string debugText() {
                std::string text = "";
                std::string addieType;
                std::string keyTypeString;
                switch( key_addressType ) {
                    case 0x00:
                        addieType = "BR/EDR";
                        break;
                    case 0x01:
                        addieType = "Reserved (not in use)";
                        break;
                    case 0x02:
                        addieType = "Reserved (not in use)";
                        break;
                    default:
                        addieType = "Unsupported value";
                }
                switch(key_type) {
                case 0x00:
                    keyTypeString = "Combination key"; break;
                case 0x01:
                    keyTypeString = "Local Unit key"; break;
                case 0x02:
                    keyTypeString = "Remote Unit key"; break;
                case 0x03:
                    keyTypeString = "Debug Combination key"; break;
                case 0x04:
                    keyTypeString = "Unauthenticated Combination key from P-192"; break;
                case 0x05:
                    keyTypeString = "Authenticated Combination key from P-192"; break;
                case 0x06:
                    keyTypeString = "Changed Combination key"; break;
                case 0x07:
                    keyTypeString = "Unauthenticated Combination key from P-256"; break;
                case 0x08:
                    keyTypeString = "Authenticated Combination key from P-256"; break;
                default:
                    keyTypeString = "Unsupported value";
                }
                text += "> New Link Key event\n";
                text += "  + Event code         : " + Utils::hex(header.code) + " ("
                        + HciAdapter::kEventTypeNames[header.code] + ")\n";
                text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
                text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
                text += "  + Store Hint         : " + Utils::hex(store_hint) + "\n";
                text += "  - Key                :\n";
                text += "    + Address          : " + Utils::bluetoothAddressString(key_address) + "\n";
                text += "    + Address type     : " + addieType + "\n";
                text += "    + Type             : " + keyTypeString + "\n";
                text += "    + Data             : " + Utils::hex(key_data, 16) + "\n";
                text += "    + Pin Length       : " + Utils::hex(key_pinLength) + "\n";
                return text;
            }
        }__attribute__((packed));

    struct PasskeyNotifyEvent {
            HciHeader header;
            uint8_t address[6];
            uint8_t addressType;
            uint32_t passkey;
            uint8_t entered;

            PasskeyNotifyEvent(const std::vector<uint8_t> &data) {
                *this = *reinterpret_cast<const PasskeyNotifyEvent *>(data.data());
                toHost();

                // Log it
                Logger::info(debugText());
            }

            void toNetwork() {
                header.toNetwork();
                passkey = Utils::endianToHci(passkey);
            }

            void toHost() {
                header.toHost();
                passkey = Utils::endianToHost(passkey);
            }

            std::string debugText() {
                std::string text = "";
                std::string addieType;
                std::ostringstream ss;
                ss << std::setw( 6 ) << std::setfill( '0' ) << passkey;
                std::string passkeyString = ss.str();
                switch( addressType ) {
                    case 0x00:
                        addieType = "BR/EDR";
                        break;
                    case 0x01:
                        addieType = "LE Public";
                        break;
                    case 0x02:
                        addieType = "LE Random";
                        break;
                    default:
                        addieType = "Unsupported value";
                }
                text += "> New Passkey Notify event\n";
                text += "  + Event code         : " + Utils::hex(header.code) + " ("
                        + HciAdapter::kEventTypeNames[header.code] + ")\n";
                text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
                text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
                text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
                text += "  + Address type       : " + addieType + "\n";
                text += "  + Key                : " + passkeyString + "\n";
                text += "  + Digits Entered     : " + std::to_string(entered) + "\n";
                return text;
            }
        }__attribute__((packed));

    struct UserConfirmationRequestEvent {
        HciHeader header;
        uint8_t address[6];
        uint8_t addressType;
        uint8_t confirm_hint;
        uint32_t passkey;

        UserConfirmationRequestEvent(const std::vector<uint8_t> &data) {
            *this = *reinterpret_cast<const UserConfirmationRequestEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork() {
            header.toNetwork();
            passkey = Utils::endianToHci(passkey);
        }

        void toHost() {
            header.toHost();
            passkey = Utils::endianToHost(passkey);
        }

        std::string debugText() {
            std::string text = "";
            std::string addieType;
            std::ostringstream ss;
            ss << std::setw( 6 ) << std::setfill( '0' ) << passkey;
            std::string passkeyString = ss.str();
            switch( addressType ) {
                case 0x00:
                    addieType = "BR/EDR";
                    break;
                case 0x01:
                    addieType = "LE Public";
                    break;
                case 0x02:
                    addieType = "LE Random";
                    break;
                default:
                    addieType = "Unsupported value";
            }
            text += "> New User Confirmation Request event\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " ("
                    + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
            text += "  + Address type       : " + addieType + "\n";
            text += "  + Confirm Hint       : " + Utils::hex(confirm_hint) + "\n";
            text += "  + Key                : " + passkeyString + "\n";
            return text;
        }
    }__attribute__((packed));

    struct NewIdenityResolvingKeyEvent {
        HciHeader header;
        uint8_t store_hint;
        uint8_t random_address[6];
        uint8_t key_address[6];
        uint8_t key_addressType;
        uint8_t key_data[16];

        NewIdenityResolvingKeyEvent(const std::vector<uint8_t> &data) {
            *this = *reinterpret_cast<const NewIdenityResolvingKeyEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork() {
            header.toNetwork();
        }

        void toHost() {
            header.toHost();
        }

        std::string debugText() {
            std::string text = "";
            std::string addieType;
            switch( key_addressType ) {
                case 0x00:
                    addieType = "BR/EDR";
                    break;
                case 0x01:
                    addieType = "LE Public";
                    break;
                case 0x02:
                    addieType = "LE Random";
                    break;
                default:
                    addieType = "Unsupported value";
            }
            text += "> New Identity Resolving Key event\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " ("
                    + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += "  + Store Hint         : " + Utils::hex(store_hint) + "\n";
            text += "  + Random Address     : " + Utils::bluetoothAddressString(random_address) + "\n";
            text += "  - Key                :\n";
            text += "    + Address          : " + Utils::bluetoothAddressString(key_address) + "\n";
            text += "    + Address type     : " + addieType + "\n";
            text += "    + Data             : " + Utils::hex(key_data, 16) + "\n";
            return text;
        }
    }__attribute__((packed));

    struct NewSignatureResolvingKeyEvent {
        HciHeader header;
        uint8_t store_hint;
        uint8_t key_address[6];
        uint8_t key_addressType;
        uint8_t key_type;
        uint8_t key_data[16];

        NewSignatureResolvingKeyEvent(const std::vector<uint8_t> &data) {
            *this = *reinterpret_cast<const NewSignatureResolvingKeyEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork() {
            header.toNetwork();
        }

        void toHost() {
            header.toHost();
        }

        std::string debugText() {
            std::string text = "";
            std::string addieType;
            std::string keyTypeString;
            switch(key_addressType) {
            case 0x00:
                addieType = "BR/EDR"; break;
            case 0x01:
                addieType = "LE Public"; break;
            case 0x02:
                addieType = "LE Random"; break;
            default:
                addieType = "Unsupported value";
            }
            switch(key_type) {
            case 0x00:
                keyTypeString = "Unauthenticated local CSRK"; break;
            case 0x01:
                keyTypeString = "Unauthenticated remote CSRK"; break;
            case 0x02:
                keyTypeString = "Authenticated local CSRK"; break;
            case 0x03:
                keyTypeString = "Authenticated remote CSRK"; break;
            default:
                keyTypeString = "Unsupported value";
            }
            text += "> New Signature Resolving Key event\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " ("
                    + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += "  + Store Hint         : " + Utils::hex(store_hint) + "\n";
            text += "  - Key                :\n";
            text += "    + Address          : " + Utils::bluetoothAddressString(key_address) + "\n";
            text += "    + Address type     : " + addieType + "\n";
            text += "    + Type             : " + keyTypeString + "\n";
            text += "    + Data             : " + Utils::hex(key_data, 16) + "\n";
            return text;
        }
    }__attribute__((packed));

    struct NewLongTermKeyEvent {
        HciHeader header;
        uint8_t store_hint;
        uint8_t key_address[6];
        uint8_t key_addressType;
        uint8_t key_type;
        uint8_t key_master;
        uint8_t key_encryptionSize;
        uint16_t key_encryptedDiversifier;
        uint8_t key_randomID[8];
        uint8_t key_data[16];

        NewLongTermKeyEvent(const std::vector<uint8_t> &data) {
            *this = *reinterpret_cast<const NewLongTermKeyEvent *>(data.data());
            toHost();

            // Log it
            Logger::info(debugText());
        }

        void toNetwork() {
            header.toNetwork();
            key_encryptedDiversifier = Utils::endianToHci(key_encryptedDiversifier);
        }

        void toHost() {
            header.toHost();
            key_encryptedDiversifier = Utils::endianToHost(key_encryptedDiversifier);
        }

        std::string debugText() {
            std::string text = "";
            std::string addieType;
            std::string keyTypeString;
            std::string masterString = "Yes";
            if(key_master==0) {
                masterString = "No";
            }
            switch(key_addressType) {
            case 0x00:
                addieType = "BR/EDR"; break;
            case 0x01:
                addieType = "LE Public"; break;
            case 0x02:
                addieType = "LE Random"; break;
            default:
                addieType = "Unsupported value";
            }
            switch(key_type) {
            case 0x00:
                keyTypeString = "Unauthenticated legacy key"; break;
            case 0x01:
                keyTypeString = "Authenticated legacy key"; break;
            case 0x02:
                keyTypeString = "Unauthenticated key from P-256"; break;
            case 0x03:
                keyTypeString = "Authenticated key from P-256"; break;
            case 0x04:
                keyTypeString = "Debug key from P-256"; break;
            default:
                keyTypeString = "Unsupported value";
            }
            text += "> New Long Term Key event (Pairing/Bonding complete)\n";
            text += "  + Event code         : " + Utils::hex(header.code) + " ("
                    + HciAdapter::kEventTypeNames[header.code] + ")\n";
            text += "  + Controller Id      : " + Utils::hex(header.controllerId) + "\n";
            text += "  + Data size          : " + std::to_string(header.dataSize) + " bytes\n";
            text += "  + Store Hint         : " + Utils::hex(store_hint) + "\n";
            text += "  - Key                :\n";
            text += "    + Address          : " + Utils::bluetoothAddressString(key_address) + "\n";
            text += "    + Address type     : " + addieType + "\n";
            text += "    + Type             : " + keyTypeString + "\n";
            text += "    + Master           : " + masterString + "\n";
            text += "    + Encryption Size  : " + Utils::hex(key_encryptionSize) + "\n";
            text += "    + Enc. Diversifier : " + Utils::hex(key_encryptedDiversifier) + "\n";
            text += "    + Random ID        : " + Utils::hex(key_randomID, 8) + "\n";
            text += "    + Data             : " + Utils::hex(key_data, 16) + "\n";
            return text;
        }
    }__attribute__((packed));

	struct AdapterSettings
	{
		uint32_t masks;

		void toHost()
		{
			masks = Utils::endianToHost(masks);
		}

		bool isSet(HciControllerSettings mask)
		{
			return (masks & mask) != 0;
		}

		// Returns a human-readable string of flags and settings
		std::string toString()
		{
			std::string text = "";
			if (isSet(EHciPowered)) { text += "Powered, "; }
			if (isSet(EHciConnectable)) { text += "Connectable, "; }
			if (isSet(EHciFastConnectable)) { text += "FC, "; }
			if (isSet(EHciDiscoverable)) { text += "Discov, "; }
			if (isSet(EHciBondable)) { text += "Bondable, "; }
			if (isSet(EHciLinkLevelSecurity)) { text += "LLS, "; }
			if (isSet(EHciSecureSimplePairing)) { text += "SSP, "; }
			if (isSet(EHciBasicRate_EnhancedDataRate)) { text += "BR/EDR, "; }
			if (isSet(EHciHighSpeed)) { text += "HS, "; }
			if (isSet(EHciLowEnergy)) { text += "LE, "; }
			if (isSet(EHciAdvertising)) { text += "Adv, "; }
			if (isSet(EHciSecureConnections)) { text += "SC, "; }
			if (isSet(EHciDebugKeys)) { text += "DebugKeys, "; }
			if (isSet(EHciPrivacy)) { text += "Privacy, "; }
			if (isSet(EHciControllerConfiguration)) { text += "ControllerConfig, "; }
			if (isSet(EHciStaticAddress)) { text += "StaticAddr, "; }

			if (text.length() != 0)
			{
				text = text.substr(0, text.length() - 2);
			}

			return text;
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> Adapter settings\n";
			text += "  + " + toString();
			return text;
		}
	} __attribute__((packed));

	struct AdvertisingSettings
	{
        uint32_t masks;

        void toNetwork() {
            masks = Utils::endianToHci(masks);
        }

        void toHost() {
            masks = Utils::endianToHost(masks);
        }

        bool isSet(AdvertisingFeaturesSettings mask) {
            return (masks & mask) != 0;
        }

        // Returns a human-readable string of flags and settings
        std::string toString() {
            std::string text = "";
            if(isSet(EAdvSwitchConnectable)) {
                text += "Switch into Connectable, ";
            }
            if(isSet(EAdvDiscoverable)) {
                text += "Disc, ";
            }
            if(isSet(EAdvLimitedDiscoverable)) {
                text += "Limited Disc, ";
            }
            if(isSet(EAdvAddFlags)) {
                text += "Add Flags, ";
            }
            if(isSet(EAdvAddTX)) {
                text += "Add TX, ";
            }
            if(isSet(EAdvAddAppearance)) {
                text += "Add Appearance, ";
            }
            if(isSet(EAdvAddLocalName)) {
                text += "Add Local Name, ";
            }
            if(isSet(EAdvSecondaryLE1M)) {
                text += "LE1M, ";
            }
            if(isSet(EAdvSecondaryLE2M)) {
                text += "LE2M, ";
            }
            if(isSet(EAdvSecondaryLECoded)) {
                text += "LECoded, ";
            }

            if(text.length() != 0) {
                text = text.substr(0, text.length() - 2);
            }

            return text;
        }

        std::string debugText() {
            std::string text = "";
            text += "> Adapter settings\n";
            text += "  + " + toString();
            return text;
        }
    }__attribute__((packed));

	// The comments documenting these fields are very high level. There is a lot of detailed information not present, for example
	// some values are not available at all times. This is fully documented in:
	//
	//     https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/mgmt-api.txt
	struct ControllerInformation
	{
		uint8_t address[6];         // The Bluetooth address
		uint8_t bluetoothVersion;   // Bluetooth version
		uint16_t manufacturer;      // The manufacturer
		AdapterSettings supportedSettings; // Bits for various supported settings (see HciControllerSettings)
		AdapterSettings currentSettings;   // Bits for various currently configured settings (see HciControllerSettings)
		uint8_t classOfDevice[3];   // Um, yeah. That.
		char name[249];             // Null terminated name
		char shortName[11];         // Null terminated short name

		void toHost()
		{
			manufacturer = Utils::endianToHost(manufacturer);
			supportedSettings.toHost();
			currentSettings.toHost();
		}

		std::string debugText()
		{
		    uint32_t bitfield = (((uint32_t)classOfDevice[0]) << 16) + (((uint32_t)classOfDevice[1]) << 8) + (uint32_t)classOfDevice[2];
			std::string text = "";
			text += "> Controller information\n";
			text += "  + Current settings   : " + Utils::hex(currentSettings.masks) + "\n";
			text += "  + Address            : " + Utils::bluetoothAddressString(address) + "\n";
			text += "  + BT Version         : " + std::to_string(static_cast<int>(bluetoothVersion)) + "\n";
			text += "  + Manufacturer       : " + Utils::hex(manufacturer) + "\n";
			text += "  + Supported settings : " + supportedSettings.toString() + "\n";
			text += "  + Current settings   : " + currentSettings.toString() + "\n";
			text += printClassOfDevice(bitfield);
			text += "  + Name               : " + std::string(name) + "\n";
			text += "  + Short name         : " + std::string(shortName);
			return text;
		}
	} __attribute__((packed));

	struct VersionInformation
	{
		uint8_t version;
		uint16_t revision;

		void toHost()
		{
			revision = Utils::endianToHost(revision);
		}

		std::string debugText()
		{
			std::string text = "";
			text += "> Version information\n";
			text += "  + Version  : " + std::to_string(static_cast<int>(version)) + "\n";
			text += "  + Revision : " + std::to_string(revision);
			return text;
		}
	} __attribute__((packed));

	struct AdvertisingFeatures
	{
	    AdvertisingSettings supportedFlags;
        uint8_t maxAdv;
        uint8_t maxScanRsp;
        uint8_t maxInstances;
        uint8_t numInstances;
        // TODO: i couldnt figure out how to pack this as a pointer, current max instances is "5"
        uint8_t instanceRef[5];

        void toNetwork() {
            supportedFlags.toNetwork();
        }

        void toHost() {
            supportedFlags.toHost();
        }

        std::string debugText() {
            std::string text = "";
            text += "> Advertising Features\n";
            text += "  + Settings (hex)   : " + Utils::hex(supportedFlags.masks) + "\n";
            text += "  + Settings         : " + supportedFlags.toString() + "\n";
            text += "  + Max Adv Data Len : " + std::to_string(maxAdv) + "\n";
            text += "  + Max Scan Resp Len: " + std::to_string(maxScanRsp) + "\n";
            text += "  + Max Instances    : " + std::to_string(maxInstances) + "\n";
            text += "  + Num Instances    : " + std::to_string(numInstances) + "\n";
            for(int i=0; i<numInstances; i++) {
                text += "  + Instances[" + std::to_string(i) + "] : " + std::to_string(instanceRef[i]) + "\n";
            }
            return text;
        }
    }__attribute__((packed));

	struct LocalName
	{
		char name[249];
		char shortName[11];

		std::string debugText()
		{
			std::string text = "";
			text += "> Local name information\n";
			text += "  + Name       : '" + std::string(name) + "'\n";
			text += "  + Short name : '" + std::string(shortName) + "'";
			return text;
		}
	} __attribute__((packed));

	//
	// Accessors
	//

	// Returns the instance to this singleton class
	static HciAdapter &getInstance()
	{
		static HciAdapter instance;
		return instance;
	}

	inline AdapterSettings getAdapterSettings() { return adapterSettings; }
	inline ControllerInformation getControllerInformation() { return controllerInformation; }
	inline VersionInformation getVersionInformation() { return versionInformation; }
	inline AdvertisingFeatures getAdvertisingFeatures() { return advertisingFeatures; }
	inline LocalName getLocalName() { return localName; }
	inline int getActiveConnectionCount() { return activeConnections; }

	//
	// Disallow copies of our singleton (c++11)
	//

	HciAdapter(HciAdapter const&) = delete;
	void operator=(HciAdapter const&) = delete;

	// Reads current values from the controller
	//
	// This effectively requests data from the controller but that data may not be available instantly, but within a few
	// milliseconds. Therefore, it is not recommended attempt to retrieve the results from their accessors immediately.
	void sync(uint16_t controllerIndex);

	// Connects the HCI socket if a connection does not already exist and starts the run thread
	//
	// If a connection already exists, this method will fail
	//
	// Note that it shouldn't be necessary to connect manually; any action requiring a connection will automatically connect
	//
	// Returns true if the HCI socket is connected (either via a new connection or an existing one), otherwise false
	bool start();

	// Waits for the HciAdapter run thread to join
	//
	// This method will block until the thread joins
	void stop();

	// Sends a command over the HCI socket
	//
	// If the HCI socket is not connected, it will auto-connect prior to sending the command. In the case of a failed auto-connect,
	// a failure is returned.
	//
	// Returns true on success, otherwise false
	bool sendCommand(HciHeader &request);

	// Event processor, responsible for receiving events from the HCI socket
	//
	// This mehtod should not be called directly. Rather, it runs continuously on a thread until the server shuts down
	void runEventThread();

	// TODO: this should register a callback (and deregister during cleanup), for now just give it a way of notifying
	bool registerEventListener(GGKServerDataSetter const &hack) {hackCallback = hack; return true;}

private:
	// Private constructor for our Singleton
	HciAdapter() : commandResponseLock(commandResponseMutex), conditionalValue(-1), activeConnections(0) {}

	// Uses a std::condition_variable to wait for a response event for the given `commandCode` or `timeoutMS` milliseconds.
	//
	// Returns true if the response event was received for `commandCode` or false if the timeout expired.
	//
	// Command responses are set via `setCommandResponse()`
	bool waitForCommandResponse(uint16_t commandCode, int timeoutMS);

	// Sets the command response and notifies the waiting std::condition_variable (see `waitForCommandResponse`)
	void setCommandResponse(uint16_t commandCode);

	// Our HCI Socket, which allows us to talk directly to the kernel
	HciSocket hciSocket;

	// Our event thread listens for events coming from the adapter and deals with them appropriately
	static std::thread eventThread;

	// Our adapter information
	AdapterSettings adapterSettings;
	ControllerInformation controllerInformation;
	VersionInformation versionInformation;
	AdvertisingFeatures advertisingFeatures;
	LocalName localName;

	std::condition_variable cvCommandResponse;
	std::mutex commandResponseMutex;
	std::unique_lock<std::mutex> commandResponseLock;
	int conditionalValue;

	GGKServerDataSetter hackCallback = NULL;

	// Our active connection count
	int activeConnections;
};

}; // namespace ggk
