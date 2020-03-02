// Copyright 2017-2019 Paul Nettle
// Modifications to implement Doppler GATT, 2018-2019 Brett Lynnes.
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
// This is the money file. This is your server description and complete implementation. If you want to add or remove a Bluetooth
// service, alter its behavior, add or remove characteristics or descriptors (and more), then this is your new home.
//
// >>
// >>>  DISCUSSION
// >>
//
// The use of the term 'server', as it is used here, refers a collection of BlueZ services, characteristics & Descripors (plus
// a little more.)
//
// Our server needs to be described in two ways. Why two? Well, think about it like this: We're communicating with Bluetooth
// clients through BlueZ, and we're communicating with BlueZ through D-Bus. In essence, BlueZ and D-Bus are acting as tunnels, one
// inside the other.
//
// Here are those two descriptions in a bit more detail:
//
// 1. We need to describe ourselves as a citizen on D-Bus: The objects we implement, interfaces we provide, methods we handle, etc.
//
//    To accomplish this, we need to build an XML description (called an 'Introspection' for the curious readers) of our DBus
//    object hierarchy. The code for the XML generation starts in DBusObject.cpp (see `generateIntrospectionXML`) and carries on
//    throughout the other DBus* files (and even a few Gatt* files).
//
// 2. We also need to describe ourselves as a Bluetooth citizen: The services we provide, our characteristics and descriptors.
//
//    To accomplish this, BlueZ requires us to implement a standard D-Bus interface ('org.freedesktop.DBus.ObjectManager'). This
//    interface includes a D-Bus method 'GetManagedObjects', which is just a standardized way for somebody (say... BlueZ) to ask a
//    D-Bus entity (say... this server) to enumerate itself. This is how BlueZ figures out what services we offer. BlueZ will
//    essentially forward this information to Bluetooth clients.
//
// Although these two descriptions work at different levels, the two need to be kept in sync. In addition, we will also need to act
// on the messages we receive from our Bluetooth clients (through BlueZ, through D-Bus.) This means that we'll have yet another
// synchronization issue to resolve, which is to ensure that whatever has been asked of us, makes its way to the correct code in
// our description so we do the right thing.
//
// I don't know about you, but when dealing with data and the concepts "multiple" and "kept in sync" come into play, my spidey
// sense starts to tingle. The best way to ensure sychronization is to remove the need to keep things sychronized.
//
// The large code block below defines a description that includes all the information about our server in a way that can be easily
// used to generate both: (1) the D-Bus object hierarchy and (2) the BlueZ services that occupy that hierarchy. In addition, we'll
// take that a step further by including the implementation right inside the description. Everything in one place.
//
// >>
// >>>  MANAGING SERVER DATA
// >>
//
// The purpose of the server is to serve data. Your application is responsible for providing that data to the server via two data
// accessors (a getter and a setter) that implemented in the form of delegates that are passed into the `ggkStart()` method.
//
// While the server is running, if data is updated via a write operation from the client the setter delegate will be called. If your
// application also generates or updates data periodically, it can push those updates to the server via call to
// `ggkNofifyUpdatedCharacteristic()` or `ggkNofifyUpdatedDescriptor()`.
//
// >>
// >>>  UNDERSTANDING THE UNDERLYING FRAMEWORKS
// >>
//
// The server description below attempts to provide a GATT-based interface in terms of GATT services, characteristics and
// descriptors. Consider the following sample:
//
//     .gattServiceBegin("text", "00000001-1E3C-FAD4-74E2-97A033F1BFAA")
//         .gattCharacteristicBegin("string", "00000002-1E3C-FAD4-74E2-97A033F1BFAA", {"read", "write", "notify"})
//
//             .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//             {
//                 // Abbreviated for simplicity
//                 self.methodReturnValue(pInvocation, myTextString, true);
//             })
//
//             .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//             {
//                 // Abbreviated for simplicity
//                 myTextString = ...
//                 self.methodReturnVariant(pInvocation, NULL);
//             })
//
//             .gattDescriptorBegin("description", "2901", {"read"})
//                 .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
//                 {
//                     self.methodReturnValue(pInvocation, "Returns a test string", true);
//                 })
//
//             .gattDescriptorEnd()
//         .gattCharacteristicEnd()
//     .gattServiceEnd()
//
// The first thing you may notice abpout the sample is that all of the lines begin with a dot. This is because we're chaining
// methods together. Each method returns the appropriate type to provide context. For example, The `gattServiceBegin` method returns
// a reference to a `GattService` object which provides the proper context to create a characteristic within that service.
// Similarly, the `gattCharacteristicBegin` method returns a reference to a `GattCharacteristic` object which provides the proper
// context for responding to requests to read the characterisic value or add descriptors to the characteristic.
//
// For every `*Begin` method, there is a corresponding `*End` method, which returns us to the previous context. Indentation helps us
// keep track of where we are.
//
// Also note the use of the lambda macros, `CHARACTERISTIC_METHOD_CALLBACK_LAMBDA` and `DESCRIPTOR_METHOD_CALLBACK_LAMBDA`. These
// macros simplify the process of including our implementation directly in the description.
//
// The first parameter to each of the `*Begin` methods is a path node name. As we build our hierarchy, we give each node a name,
// which gets appended to it's parent's node (which in turns gets appended to its parent's node, etc.) If our root path was
// "/com/gobbledegook", then our service would have the path "/com/gobbledegook/text" and the characteristic would have the path
// "/com/gobbledegook/text/string", and the descriptor would have the path "/com/gobbledegook/text/string/description". These paths
// are important as they act like an addressing mechanism similar to paths on a filesystem or in a URL.
//
// The second parameter to each of the `*Begin` methods is a UUID as defined by the Bluetooth standard. These UUIDs effectively
// refer to an interface. You will see two different kinds of UUIDs: a short UUID ("2901") and a long UUID
// ("00000002-1E3C-FAD4-74E2-97A033F1BFAA").
//
// For more information on UUDSs, see GattUuid.cpp.
//
// In the example above, our non-standard UUIDs ("00000001-1E3C-FAD4-74E2-97A033F1BFAA") are something we generate ourselves. In the
// case above, we have created a custom service that simply stores a mutable text string. When the client enumerates our services
// they'll see this UUID and, assuming we've documented our interface behind this UUID for client authors, they can use our service
// to read and write a text string maintained on our server.
//
// The third parameter (which only applies to dharacteristics and descriptors) are a set of flags. You will find the current set of
// flags for characteristics and descriptors in the "BlueZ D-Bus GATT API description" at:
//
//     https://git.kernel.org/pub/scm/bluetooth/bluez.git/plain/doc/gatt-api.txt
//
// In addition to these structural methods, there are a small handful of helper methods for performing common operations. These
// helper methods are available within a method (such as `onReadValue`) through the use of a `self` reference. The `self` reference
// refers to the object at which the method is invoked (either a `GattCharacteristic` object or a `GattDescriptor` object.)
//
//     methodReturnValue and methodReturnVariant
//         These methods provide a means for returning values from Characteristics and Descriptors. The `-Value` form accept a set
//         of common types (int, string, etc.) If you need to provide a custom return type, you can do so by building your own
//         GVariant (which is a GLib construct) and using the `-Variant` form of the method.
//
//     sendChangeNotificationValue and sendChangeNotificationVariant
//         These methods provide a means for notifying changes for Characteristics. The `-Value` form accept a set of common types
//         (int, string, etc.) If you need to notify a custom return type, you can do so by building your own GVariant (which is a
//         GLib construct) and using the `-Variant` form of the method.
//
// For information about GVariants (what they are and how to work with them), see the GLib documentation at:
//
//     https://www.freedesktop.org/software/gstreamer-sdk/data/docs/latest/glib/glib-GVariantType.html
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <algorithm>

#include "Server.h"
#include "ServerUtils.h"
#include "Utils.h"
#include "Globals.h"
#include "DBusObject.h"
#include "DBusInterface.h"
#include "GattProperty.h"
#include "GattService.h"
#include "GattUuid.h"
#include "GattCharacteristic.h"
#include "GattDescriptor.h"
#include "Logger.h"

#include "string.h"

// TODO: remove these headers that are being used for debugging
#include <iostream>
using namespace std;

namespace ggk {

// There's a good chance there will be a bunch of unused parameters from the lambda macros
#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------------------------------------------------------------

// Our one and only server. It's global.
std::shared_ptr<Server> TheServer = nullptr;

// Setting these as globals for easy retreival inside the lamdas
static std::string gSerialNum = "";
static std::string gFirmwareRev = "";
static std::string gHardwareRev = "";
static std::string gSoftwareRev = "";

// ---------------------------------------------------------------------------------------------------------------------------------
// Object implementation
// ---------------------------------------------------------------------------------------------------------------------------------

// Our constructor builds our entire server description
//
// serviceName: The name of our server (collection of services)
//
//     This is used to build the path for our Bluetooth services. It also provides the base for the D-Bus owned name (see
//     getOwnedName.)
//
//     This value will be stored as lower-case only.
//
//     Retrieve this value using the `getName()` method.
//
// advertisingName: The name for this controller, as advertised over LE
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingName()` method.
//
// advertisingShortName: The short name for this controller, as advertised over LE
//
//     According to the spec, the short name is used in case the full name doesn't fit within Extended Inquiry Response (EIR) or
//     Advertising Data (AD).
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingShortName()` method.
//
Server::Server(const std::map<const std::string, const std::string> &dataMap,
	GGKServerDataGetter getter, GGKServerDataSetter setter)
{
	

	// Save our names
	this->serviceName = dataMap.at("serviceName");
	std::transform(this->serviceName.begin(), this->serviceName.end(), this->serviceName.begin(), ::tolower);
	this->advertisingName = dataMap.at("advertisingName");
	this->advertisingShortName = dataMap.at("advertisingShortName");
	gSerialNum = dataMap.at("serialNumber");
	gFirmwareRev = dataMap.at("firmwareRevision");
	gHardwareRev = dataMap.at("hardwareRevision");
	gSoftwareRev = dataMap.at("softwareRevision");


	// Register getter & setter for server data
	dataGetter = getter;
	dataSetter = setter;

	// Adapter configuration flags - set these flags based on how you want the adapter configured
	enableBREDR = dataMap.at("enableBREDR") == "true";
	enableSecureConnection = dataMap.at("enableSecureConnection") == "true";
	enableLinkLayerSecurity = dataMap.at("enableLinkLayerSecurity") == "true";
	enableConnectable = dataMap.at("enableConnectable") == "true";
	enableDiscoverable = dataMap.at("enableDiscoverable") == "true";
	enableAdvertising = dataMap.at("enableAdvertising") == "true";
	enableBondable = dataMap.at("enableBondable") == "true";
	enableSecureSimplePairing = dataMap.at("enableSecureSimplePairing") == "true";
	enableHighspeedConnect = dataMap.at("enableHighspeedConnect") == "true";
	enableFastConnect = dataMap.at("enableFastConnect") == "true";
	
	const char *READ_SECURITY_SETTING=dataMap.at("readSecuritySetting").c_str();
	const char *WRITE_SECURITY_SETTING=dataMap.at("writeSecuritySetting").c_str();
	
	//
	// Define the server
	//

	// Create the root D-Bus object and push it into the list
	objects.push_back(DBusObject(DBusObjectPath() + "com" + getServiceName()));

	// We're going to build off of this object, so we need to get a reference to the instance of the object as it resides in the
	// list (and not the object that would be added to the list.)
	objects.back()

    // Service: Battery Service (0x180F)
    //
    // This is included because iOS devices like to ping this.
    .gattServiceBegin("battery_service", "180F")
        .gattCharacteristicBegin("battery_level", "2A19", {"read"})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            uint8_t maybeGetTheRealBatteryValue = 100;
                self.methodReturnValue(pInvocation, maybeGetTheRealBatteryValue, true);
            })

        .gattCharacteristicEnd()
    .gattServiceEnd()

    // Service: Generic Attribute Service (0x1801)
    //
    // This is included because it should cause devices not to try and cache our services
//    .gattServiceBegin("gattService", "1801")
//        .gattCharacteristicBegin("serviceChanged", "2A05", {"notify"})
//            // Standard characteristic "ReadValue" method call
//            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//            {
//                //two 16bit handles representing the range of the change
//	        uint32_t nothingChanges = 0;
//                self.methodReturnValue(pInvocation, nothingChanges, true);
//            })
//
//        .gattCharacteristicEnd()
//    .gattServiceEnd()

	// Service: Device Information (0x180A)
	//
	// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.service.device_information.xml
	.gattServiceBegin("device", "180A")

		// Characteristic: Manufacturer Name String (0x2A29)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.manufacturer_name_string.xml
		.gattCharacteristicBegin("mfgr_name", "2A29", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, "Palo Alto Innovation", true);
			})

		.gattCharacteristicEnd()

		// Characteristic: Model Number String (0x2A24)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.model_number_string.xml
		.gattCharacteristicBegin("model_num", "2A24", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, "Sandman", true);
			})

		.gattCharacteristicEnd()

		// Characteristic: Serial Number String (0x2A25)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.serial_number_string.xml
		.gattCharacteristicBegin("serial_num", "2A25", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, gSerialNum, true);
			})

		.gattCharacteristicEnd()

		// Characteristic: Firmware Revision String (0x2A26)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.firmware_revision_string.xml
		.gattCharacteristicBegin("firmware", "2A26", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, gFirmwareRev, true);
			})

		.gattCharacteristicEnd()

		// Characteristic: Hardware Revision String (0x2A27)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.hardware_revision_string.xml
		.gattCharacteristicBegin("hardware", "2A27", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, gHardwareRev, true);
			})

		.gattCharacteristicEnd()

		// Characteristic: Software Revision String (0x2A28)
		//
		// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.software_revision_string.xml
		.gattCharacteristicBegin("software", "2A28", {"read"})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
				self.methodReturnValue(pInvocation, gSoftwareRev, true);
			})

		.gattCharacteristicEnd()

	.gattServiceEnd()

	// Custom Doppler Hardware service
	//
	// This service will get and set various things related to the hardware on the Doppler

	// Service: Doppler Hardware (custom: 8e7934bdf06d48f6860483c94e0ec8f9)
	.gattServiceBegin("hardware", "8e7934bdf06d48f6860483c94e0ec8f9")

		// Characteristic: R,G,B color values (custom: 57edcf379f674c64a9076efaa28e1712)
		.gattCharacteristicBegin("displaycolor", "57edcf379f674c64a9076efaa28e1712", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
	            const uint8_t *pColorData = self.getDataPointer<const uint8_t *>("hardware/displaycolor", nullptr);
	            /* normally would call
	             * self.methodReturnVariant(pInvocation, pColorData, true);
	             * but that converts as an empty char string if the first data is 0, so do it the long way.
	             */
	            // this is just what that internal call would do anyway, forcing a conversion as a byte array
	            GVariant *pVariant = Utils::gvariantFromByteArray(pColorData, 3);
	            self.methodReturnVariant(pInvocation, pVariant, true);
			})

			// Standard characteristic "WriteValue" method call
			.onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(g_variant_get_child_value(pParameters, 0)), &size, 1);

				if(size == 3) {
	                self.setDataPointer("hardware/displaycolor", pPtr);
	
	                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
	                // Characteristic interface (which just so happens to be the same interface passed into our self
	                // parameter) we can that parameter to call our own onUpdatedValue method
	                self.callOnUpdatedValue(pConnection, pUserData);
                } else {
                	Logger::error(SSTR << "Failed updating display color: invalid array size " << size);
                }

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);

			})

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *pColorData = self.getDataPointer<const char *>("hardware/displaycolor", "");
                self.sendChangeNotificationValue(pConnection, pColorData);
                return true;
            })

			// GATT Descriptor: Characteristic User Description (0x2901)
			// 
			// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
			.gattDescriptorBegin("description", "2901", {"read"})

				// Standard descriptor "ReadValue" method call
				.onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
				{
					const char *pDescription = "Gets and sets the color on the Doppler display in R,G,B format (1 byte each)";
					self.methodReturnValue(pInvocation, pDescription, true);
				})

			.gattDescriptorEnd()

		.gattCharacteristicEnd()


        // Characteristic: R,G,B color values (custom: 101caed5c43e4822bce1ed29a457f01b)
        .gattCharacteristicBegin("buttoncolor", "101caed5c43e4822bce1ed29a457f01b", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t *pColorData = self.getDataPointer<const uint8_t *>("hardware/buttoncolor", nullptr);
                /* normally would call
                 * self.methodReturnVariant(pInvocation, pColorData, true);
                 * but that converts as an empty char string if the first data is 0, so do it the long way.
                 */
                // this is just what that internal call would do anyway, forcing a conversion as a byte array
                GVariant *pVariant = Utils::gvariantFromByteArray(pColorData, 3);
                self.methodReturnVariant(pInvocation, pVariant, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(g_variant_get_child_value(pParameters, 0)), &size, 1);

				if(size == 3) {
	                self.setDataPointer("hardware/buttoncolor", pPtr);
	
	                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
	                // Characteristic interface (which just so happens to be the same interface passed into our self
	                // parameter) we can that parameter to call our own onUpdatedValue method
	                self.callOnUpdatedValue(pConnection, pUserData); 
                } else {
                	Logger::error(SSTR << "Failed updating button color: invalid array size " << size);
                }

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *pColorData = self.getDataPointer<const char *>("hardware/buttoncolor", "");
                self.sendChangeNotificationValue(pConnection, pColorData);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Gets and sets the color on the Doppler buttons in R,G,B format (1 byte each)";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()


        // Characteristic: Brightness percent (custom: a6848d4c81ea44cebc5381404e8e4969)
        .gattCharacteristicBegin("brightness", "a6848d4c81ea44cebc5381404e8e4969", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t brightness = self.getDataValue<const uint8_t>("hardware/brightness", 0);
                self.methodReturnValue(pInvocation, brightness, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
	            gsize size;
	            gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
	            // TODO: check size == 1
                uint8_t brightness = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("hardware/brightness", brightness);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &brightness);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
	            const uint8_t brightness = self.getDataValue<const uint8_t>("hardware/brightness", 0);
                self.sendChangeNotificationValue(pConnection, brightness);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Brightness to set the display and button LEDs as a percent.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Auto-Brightness toggle (custom: 25d2042ee4a24aa880bf949ce65cd7c0)
        .gattCharacteristicBegin("autobright", "25d2042ee4a24aa880bf949ce65cd7c0", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t abright = self.getDataValue<const uint8_t>("hardware/autobright", 0);
                self.methodReturnValue(pInvocation, abright, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
	            gsize size;
	            gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
	            // TODO: check size == 1
                uint8_t abright = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("hardware/autobright", abright);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &abright);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
	        const uint8_t abright = self.getDataValue<const uint8_t>("hardware/autobright", 0);
                self.sendChangeNotificationValue(pConnection, abright);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Toggle the ability for automatic brightness";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()
        
        // Characteristic: Disconnect Bluetooth (custom: 72fecd2579d44b85929c8222de83eabd)
        .gattCharacteristicBegin("disconnect", "72fecd2579d44b85929c8222de83eabd", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            self.setDataPointer("hardware/disconnect", "");

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Causes the Server(Peripheral) to disconnect the current connection";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Volume percent (custom: 5f00e8c711b34e66962d96ef45aae66c)
        .gattCharacteristicBegin("volume", "5f00e8c711b34e66962d96ef45aae66c", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t vol = self.getDataValue<const uint8_t>("hardware/volume", 0);
                self.methodReturnValue(pInvocation, vol, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size
                uint8_t vol = *static_cast<const uint8_t *>(pPtr);
                self.setDataValue("hardware/volume", vol);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &vol);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint8_t vol = self.getDataValue<const uint8_t>("hardware/volume", 0);
                self.sendChangeNotificationValue(pConnection, vol);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Volume to set the system to as a percent.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Update System Software (custom: 030249f40ded40ec8832a4dda5963f7f)
        .gattCharacteristicBegin("update", "030249f40ded40ec8832a4dda5963f7f", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING, "notify"})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *updateString = self.getDataPointer<const char *>("hardware/update", "");
                uint16_t offset = ServerUtils::getOffsetFromParameters(pParameters, strlen(updateString));
                updateString += offset;
                self.methodReturnValue(pInvocation, updateString, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("hardware/update", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *updateString = self.getDataPointer<const char *>("hardware/update", "");
                self.sendChangeNotificationValue(pConnection, updateString);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Notifies when a system update is available to apply. Write a value to accept.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()
    .gattServiceEnd()

    // Custom Wifi Settings Service (custom: 5f4615cc1cb44da9a8409d5266d65d0e)
    //
    // This service allows the client to see the list of SSIDs the Doppler is
	// currently seeing, and then start a connection with one of those SSIDs.
	// It also lists the current connection status of the doppler.
    .gattServiceBegin("wifi", "5f4615cc1cb44da9a8409d5266d65d0e")

        // Characteristic: SSID list (custom: 8fb508b822a548aab5402602e26016db)
        .gattCharacteristicBegin("ssid_list", "8fb508b822a548aab5402602e26016db", {WRITE_SECURITY_SETTING,"notify"})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value to trigger the callback
	        // HACK: put a dummy value on the data (should actually be a vector<uint8_t>
                self.setDataPointer("wifi/ssid_list", "");
                 
                // normally you would call onUpdatedValue, but we are waiting for a process to complete
                // in the background, so have that process call the update instead.
                // self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
	        vector<guint8> val;
                val = self.getDataValue<const vector<guint8>>("wifi/ssid_list", val);
                self.sendChangeNotificationValue(pConnection, val);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 encoded json containing the field \"SSIDs\" which is an array of objects containing the fields \"SSID\", \"str\", and \"enc\"﻿";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Wifi Status (custom: 6fcbf07c93f34fef866a7d9c8926596a)
        .gattCharacteristicBegin("wifi_status", "6fcbf07c93f34fef866a7d9c8926596a", {READ_SECURITY_SETTING,"notify"})


            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                vector<guint8> val;
                val = self.getDataValue<const vector<guint8>>("wifi/wifi_status", val);
                self.methodReturnValue(pInvocation, val, true);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                vector<guint8> val;
                val = self.getDataValue<const vector<guint8>>("wifi/wifi_status", val);
                self.sendChangeNotificationValue(pConnection, val);
                return true;
            })


            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "byte array of at least length 1. Byte 1 is the status, remaining bytes are a string of the SSID";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Connect to SSID (custom: 4fdaabaab9ec4624a1a76febcf9e6901)
        .gattCharacteristicBegin("connect", "4fdaabaab9ec4624a1a76febcf9e6901", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("wifi/connect", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 encoded json containing the fields \"SSID\" and \"Pass\"";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()
        
        // Characteristic: Read a Random Number (custom: eaf092540c1743eabacdd3a57d6c74af)
//        .gattCharacteristicBegin("readRandom", "eaf092540c1743eabacdd3a57d6c74af", {READ_SECURITY_SETTING})
//
//            // Standard characteristic "ReadValue" method call
//            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//            {
//                vector<guint32> val;
//                val = self.getDataValue<const vector<guint8>>("wifi/readRandom", val);
//                self.methodReturnValue(pInvocation, val, true);
//            })
//
//            // GATT Descriptor: Characteristic User Description (0x2901)
//            //
//            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
//            .gattDescriptorBegin("description", "2901", {"read"})
//
//                // Standard descriptor "ReadValue" method call
//                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
//                {
//                    const char *pDescription = "A random number from Doppler";
//                    self.methodReturnValue(pInvocation, pDescription, true);
//                })
//
//            .gattDescriptorEnd()
//
//        .gattCharacteristicEnd()
//        
//        // Characteristic: write a random int (custom: 8757daf9b3e040d78a2bcbcef013c0f6)
//        .gattCharacteristicBegin("writeRandom", "8757daf9b3e040d78a2bcbcef013c0f6", {WRITE_SECURITY_SETTING})
//
//            // Standard characteristic "WriteValue" method call
//            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//            {
//                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
//                gsize size;
//                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
//                // TODO: check size == 4
//                uint32_t nonce = *static_cast<const uint32_t *>(pPtr);
//
//                self.setDataValue("wifi/writeRandom", nonce);
//
//                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
//                // Characteristic interface (which just so happens to be the same interface passed into our self
//                // parameter) we can that parameter to call our own onUpdatedValue method
//                self.callOnUpdatedValue(pConnection, &nonce);
//
//                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
//                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
//                // Only "write-without-response" works without this
//                self.methodReturnVariant(pInvocation, NULL);
//            })
//
//            // GATT Descriptor: Characteristic User Description (0x2901)
//            //
//            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
//            .gattDescriptorBegin("description", "2901", {"read"})
//
//                // Standard descriptor "ReadValue" method call
//                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
//                {
//                    const char *pDescription = "utf-8 encoded json containing the fields \"SSID\" and \"Pass\"";
//                    self.methodReturnValue(pInvocation, pDescription, true);
//                })
//
//            .gattDescriptorEnd()
//
//        .gattCharacteristicEnd()
    .gattServiceEnd()

    // Custom Alarm Settings service for Doppler (custom: 447b7a3534ce419a94c18134f94b7889)
    //
    .gattServiceBegin("alarm", "447b7a3534ce419a94c18134f94b7889")

        // Characteristic: Alarm List (custom: 3de058344cab4d658d042463a5e9248f)
        .gattCharacteristicBegin("alarm_list", "3de058344cab4d658d042463a5e9248f", {WRITE_SECURITY_SETTING,"notify"})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value to trigger the callback
                self.setDataPointer("alarm/alarm_list", "");

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                vector<guint8> val;
                val = self.getDataValue<const vector<guint8>>("alarm/alarm_list", val);
                self.sendChangeNotificationValue(pConnection, val);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 encoded json containing the alarm objects.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Change Notification CRC (custom: d387d13edfbc475493855fa0c192fcb9)
        .gattCharacteristicBegin("crc", "d387d13edfbc475493855fa0c192fcb9", {READ_SECURITY_SETTING,"notify"})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint32_t crc = self.getDataValue<const uint32_t>("alarm/crc", 0);
                self.methodReturnValue(pInvocation, crc, true);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint32_t crc = self.getDataValue<const uint32_t>("alarm/crc", 0);
                self.sendChangeNotificationValue(pConnection, crc);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "When the alarm_list changes, this CRC updates. Subscribe to this notification for changes";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Add an Alarm (custom: d25448326eeb4900a7cc7174ea67e0df)
        .gattCharacteristicBegin("add_alarm", "d25448326eeb4900a7cc7174ea67e0df", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("alarm/add_alarm", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Write an utf-8 encoded json containing the alarm object to set a new alarm.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Delete an Alarm (custom: d4593d59f1f9493baf97f459b256d118)
        .gattCharacteristicBegin("del_alarm", "d4593d59f1f9493baf97f459b256d118", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 1
                int32_t alarm_id = *static_cast<const int32_t *>(pPtr);

                self.setDataValue("alarm/del_alarm", alarm_id);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &alarm_id);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Write the int32_t id of the alarm to delete";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Edit an Alarm (custom: c61385db89bb452886b1f7b1dff6aa97)
        .gattCharacteristicBegin("edit_alarm", "c61385db89bb452886b1f7b1dff6aa97", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("alarm/edit_alarm", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Write an utf-8 encoded json containing the alarm object with alarm id.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Alarm Sound List (custom: ada4d25b255e441582d5ec6de21771c2)
        .gattCharacteristicBegin("sounds", "ada4d25b255e441582d5ec6de21771c2", {READ_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *sounds = self.getDataPointer<const char *>("alarm/sounds", "");
                uint16_t offset = ServerUtils::getOffsetFromParameters(pParameters, strlen(sounds));
                sounds += offset;
                self.methodReturnValue(pInvocation, sounds, true);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *sounds = self.getDataPointer<const char *>("alarm/sounds", "");
                self.sendChangeNotificationValue(pConnection, sounds);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "List of sound filenames in json format";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Play a Test Sound (custom: e4c042eabbb84547bcc7ea79cc8940bb)
        .gattCharacteristicBegin("test_sound", "e4c042eabbb84547bcc7ea79cc8940bb", {WRITE_SECURITY_SETTING})

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("alarm/test_sound", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Write an UTF-8 json formatted string of the sound file to play and volume percent. Example {\"sound\":\"foo.mp3\", \"vol\":70}";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

    .gattServiceEnd()

    // Service: Doppler Software (custom: e0339a93c7694f8fb39d8bc94feb183c)
    //
    // This service contains anything not directly manipulating hardware or not covered in another service
    // that still needs to be communicated to/from the Doppler software
    .gattServiceBegin("software", "e0339a93c7694f8fb39d8bc94feb183c")

        // Characteristic: Time Mode(custom: f307c52b14af4162bad4d56c4df9e28a)
        .gattCharacteristicBegin("time_mode", "f307c52b14af4162bad4d56c4df9e28a", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t mode = self.getDataValue<const uint8_t>("software/time_mode", 0);
                self.methodReturnValue(pInvocation, mode, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 1
                uint8_t mode = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("software/time_mode", mode);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &mode);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint8_t mode = self.getDataValue<const uint8_t>("software/time_mode", 0);
                self.sendChangeNotificationValue(pConnection, mode);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Sets the time display mode between 12hr or 24hr mode (uint8_t '12' or '24')";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Doppler Status (custom: af1664900d964f0c9596aed9fb717b78)
        .gattCharacteristicBegin("status", "af1664900d964f0c9596aed9fb717b78", {READ_SECURITY_SETTING,"notify"})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	        const uint32_t status = self.getDataValue<const uint32_t>("software/status", 0);
                self.methodReturnValue(pInvocation, status, true);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint32_t status = self.getDataValue<const uint32_t>("software/status", 0);
                self.sendChangeNotificationValue(pConnection, status);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "uint32_t with data on each byte. First byte is the Doppler Status, second byte is the Alexa Status";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Day of the Week (custom: d99cd3de563f4c5491a380d6cabedb1f)
        .gattCharacteristicBegin("dotw", "d99cd3de563f4c5491a380d6cabedb1f", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint32_t dotw = self.getDataValue<const uint32_t>("software/dotw", 0);
                self.methodReturnValue(pInvocation, dotw, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 4
                uint32_t dotw = *static_cast<const uint32_t *>(pPtr);

                self.setDataValue("software/dotw", dotw);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &dotw);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint32_t mode = self.getDataValue<const uint32_t>("software/dotw", 0);
                self.sendChangeNotificationValue(pConnection, mode);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "First byte represents the state of the DOTW LEDs (0=off). R,G,B bytes values for the other 3";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Weather (custom: 0xdcadae6819034eea8fc4cc7435b12c4a)
        .gattCharacteristicBegin("weather", "dcadae6819034eea8fc4cc7435b12c4a", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint32_t weather = self.getDataValue<const uint32_t>("software/weather", 0);
                self.methodReturnValue(pInvocation, weather, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 4
                uint32_t weather = *static_cast<const uint32_t *>(pPtr);

                self.setDataValue("software/weather", weather);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &weather);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint32_t weather = self.getDataValue<const uint32_t>("software/weather", 0);
                self.sendChangeNotificationValue(pConnection, weather);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "First byte represents the state of the weather LEDs (0=off). Remaining 3 are postal code";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Temperature Digits (custom: 0xe76f7eec8f3c4c0bb26d0e0371f9b3f0)
        .gattCharacteristicBegin("temp", "e76f7eec8f3c4c0bb26d0e0371f9b3f0", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint32_t temp = self.getDataValue<const uint32_t>("software/temp", 0);
                self.methodReturnValue(pInvocation, temp, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 4
                uint32_t temp = *static_cast<const uint32_t *>(pPtr);

                self.setDataValue("software/temp", temp);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &temp);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint32_t temp = self.getDataValue<const uint32_t>("software/temp", 0);
                self.sendChangeNotificationValue(pConnection, temp);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "First byte represents the state of the temp LEDs (0b11=Faren./on). Remaining 3 are postal code";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Traffic Indicator Bar (custom: 0xf0c5985d197546a09f250ffbd460bd0e)
        .gattCharacteristicBegin("traffic", "f0c5985d197546a09f250ffbd460bd0e", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *traffic = self.getDataPointer<const char *>("software/traffic", "");
                self.methodReturnValue(pInvocation, traffic, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("software/traffic", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *traffic = self.getDataPointer<const char *>("software/traffic", "");
                self.sendChangeNotificationValue(pConnection, traffic);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 json formatted string containing the traffic bar state, and info on src->dest";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Light Bar Mode (custom: 0x93a9a17141e04274acaf7ae7f7873fd4)
        .gattCharacteristicBegin("light_bar", "93a9a17141e04274acaf7ae7f7873fd4", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *lights = self.getDataPointer<const char *>("software/light_bar", "");
                self.methodReturnValue(pInvocation, lights, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("software/light_bar", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *lights = self.getDataPointer<const char *>("software/light_bar", "");
                self.sendChangeNotificationValue(pConnection, lights);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 json formatted string with the light bar state";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: IFTTT Button 1(custom: 0xdb736f32e0114d69b11795353ea92ef6)
        .gattCharacteristicBegin("IFTTT1", "db736f32e0114d69b11795353ea92ef6", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *ifttt = self.getDataPointer<const char *>("software/ifttt1", "");
                self.methodReturnValue(pInvocation, ifttt, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("software/ifttt1", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *ifttt = self.getDataPointer<const char *>("software/ifttt1", "");
                self.sendChangeNotificationValue(pConnection, ifttt);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 json formatted string with the IFTTT URI for button 1";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: IFTTT Button 2(custom: 0x0adc78cfd69c495893a730aed2140f74)
        .gattCharacteristicBegin("IFTTT2", "0adc78cfd69c495893a730aed2140f74", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *ifttt = self.getDataPointer<const char *>("software/ifttt2", "");
                self.methodReturnValue(pInvocation, ifttt, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("software/ifttt2", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *ifttt = self.getDataPointer<const char *>("software/ifttt2", "");
                self.sendChangeNotificationValue(pConnection, ifttt);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 json formatted string with the IFTTT URI for button 2";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()
    .gattServiceEnd()

	// Service: Alexa Setup (custom: 0xfc0acbe67b664a439d30b39cd3e7f4b0)
	.gattServiceBegin("alexa", "fc0acbe67b664a439d30b39cd3e7f4b0")

		// Characteristic: Request Challenge (custom: 0x0e8c74b16b984f40af47513af053c50f)
		.gattCharacteristicBegin("generate", "0e8c74b16b984f40af47513af053c50f", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING,"notify"})
            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t isSet = self.getDataValue<const uint8_t>("alexa/generate", 0);
                self.methodReturnValue(pInvocation, isSet, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
                // TODO: check size == 1
                uint8_t isSet = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("alexa/generate", isSet);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &isSet);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const uint8_t isSet = self.getDataValue<const uint8_t>("alexa/generate", 0);
                self.sendChangeNotificationValue(pConnection, isSet);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Set this to '1' to have Doppler generate a challenge. Doppler will set to '0' after challenge generation.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()
        .gattCharacteristicEnd()

        // Characteristic: Challenge (custom: 0x9c2ba4af872249b19b2deec923ace9c8)
        .gattCharacteristicBegin("challenge", "9c2ba4af872249b19b2deec923ace9c8", {READ_SECURITY_SETTING,"notify"})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *challenge = self.getDataPointer<const char *>("alexa/challenge", "");
                self.methodReturnValue(pInvocation, challenge, true);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *challenge = self.getDataPointer<const char *>("alexa/challenge", "");
                self.sendChangeNotificationValue(pConnection, challenge);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "64bit encoded challenge required for signon. empty string when not in the correct state";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Alexa Signon Key (custom: 0x683517267b7e4d569b8097fafd36e0a0)
        .gattCharacteristicBegin("key", "683517267b7e4d569b8097fafd36e0a0", {WRITE_SECURITY_SETTING})
            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("alexa/key", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })


            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "utf-8 encoded json of the authorization code, redirect URI, and Client ID that Amazon provides";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        .gattServiceEnd()

	// Custom Doppler Time service
	//
	// This service will get and set the time and timezone

	// Service: Doppler Time (custom: 3eda5f6eb32f48c48475dbf1de865d04)
	.gattServiceBegin("doptime", "3eda5f6eb32f48c48475dbf1de865d04")

		// Characteristic: The UTC time currently set on doppler (custom: 83a20a54cc854e208cdc619a05cee43b)
		.gattCharacteristicBegin("utctime", "83a20a54cc854e208cdc619a05cee43b", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

			// Standard characteristic "ReadValue" method call
			.onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
	            const uint8_t *pTimeData = self.getDataPointer<const uint8_t *>("doptime/utctime", nullptr);
	            /* normally would call
	             * self.methodReturnVariant(pInvocation, pColorData, true);
	             * but that converts as an empty char string if the first data is 0, so do it the long way.
	             */
	            // this is just what that internal call would do anyway, forcing a conversion as a byte array
	            // TODO: check that pTimeData has 2 bytes
	            GVariant *pVariant = Utils::gvariantFromByteArray(pTimeData, 2);
	            self.methodReturnVariant(pInvocation, pVariant, true);
			})

			// Standard characteristic "WriteValue" method call
			.onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
			{
                gsize size;
                gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(g_variant_get_child_value(pParameters, 0)), &size, 1);

                self.setDataPointer("doptime/utctime", pPtr);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
			})

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *pTimeData = self.getDataPointer<const char *>("doptime/utctime", "");
                self.sendChangeNotificationValue(pConnection, pTimeData);
                return true;
            })

			// GATT Descriptor: Characteristic User Description (0x2901)
			// 
			// See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
			.gattDescriptorBegin("description", "2901", {"read"})

				// Standard descriptor "ReadValue" method call
				.onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
				{
					const char *pDescription = "Get and set UTC time. 2 byte array: 1st byte is hour, 2nd byte is minute.";
					self.methodReturnValue(pInvocation, pDescription, true);
				})

			.gattDescriptorEnd()

		.gattCharacteristicEnd()


        // Characteristic: Extra time offset minutes (custom: 64f9476b044c479e994fa6fd18a9f9df)
        .gattCharacteristicBegin("offset", "64f9476b044c479e994fa6fd18a9f9df", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t offset = self.getDataValue<const uint8_t>("doptime/offset", 0);
                self.methodReturnValue(pInvocation, offset, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
	            gsize size;
	            gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
	            // TODO: check size == 1
                uint8_t offset = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("doptime/offset", offset);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &offset);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
	            const uint8_t offset = self.getDataValue<const uint8_t>("doptime/offset", 0);
                self.sendChangeNotificationValue(pConnection, offset);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Minutes of offset to add to the final time. Data is a single signed byte";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()


        // Characteristic: Timezone (custom: 60a562e87ed44a32a7960b342a784139)
        .gattCharacteristicBegin("timezone", "60a562e87ed44a32a7960b342a784139", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const char *tz = self.getDataPointer<const char *>("doptime/timezone", "");
                self.methodReturnValue(pInvocation, tz, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                // Update the text string value
                GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
                self.setDataPointer("doptime/timezone", Utils::stringFromGVariantByteArray(pAyBuffer).c_str());

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, pUserData);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
                const char *tz = self.getDataPointer<const char *>("doptime/timezone", "");
                self.sendChangeNotificationValue(pConnection, tz);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "String of a timezone in the IANA 2018e database the Doppler is using.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

        // Characteristic: Use NTP time toggle (custom: 3e3be9b1b5b54d10846001585028deb5)
        .gattCharacteristicBegin("ntp", "3e3be9b1b5b54d10846001585028deb5", {READ_SECURITY_SETTING, WRITE_SECURITY_SETTING})

            // Standard characteristic "ReadValue" method call
            .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
                const uint8_t ntp = self.getDataValue<const uint8_t>("doptime/ntp", 0);
                self.methodReturnValue(pInvocation, ntp, true);
            })

            // Standard characteristic "WriteValue" method call
            .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
            {
	            GVariant *pAyBuffer = g_variant_get_child_value(pParameters, 0);
	            gsize size;
	            gconstpointer pPtr = g_variant_get_fixed_array(const_cast<GVariant *>(pAyBuffer), &size, 1);
	            // TODO: check size == 1
                uint8_t ntp = *static_cast<const uint8_t *>(pPtr);

                self.setDataValue("doptime/ntp", ntp);

                // Since all of these methods (onReadValue, onWriteValue, onUpdateValue) are all part of the same
                // Characteristic interface (which just so happens to be the same interface passed into our self
                // parameter) we can that parameter to call our own onUpdatedValue method
                self.callOnUpdatedValue(pConnection, &ntp);

                // Note: Even though the WriteValue method returns void, it's important to return like this, so that a
                // dbus "method_return" is sent, otherwise the client gets an error (ATT error code 0x0e"unlikely").
                // Only "write-without-response" works without this
                self.methodReturnVariant(pInvocation, NULL);
            })

            // Here we use the onUpdatedValue to set a callback that isn't exposed to BlueZ, but rather allows us to manage
            // updates to our value. These updates may have come from our own server or some other source.
            //
            // We can handle updates in any way we wish, but the most common use is to send a change notification.
            .onUpdatedValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
            {
	            const uint8_t ntp = self.getDataValue<const uint8_t>("doptime/ntp", 0);
                self.sendChangeNotificationValue(pConnection, ntp);
                return true;
            })

            // GATT Descriptor: Characteristic User Description (0x2901)
            //
            // See: https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.characteristic_user_description.xml
            .gattDescriptorBegin("description", "2901", {"read"})

                // Standard descriptor "ReadValue" method call
                .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
                {
                    const char *pDescription = "Toggle use of NTP time. 0 means NTP is not used, manually set time.";
                    self.methodReturnValue(pInvocation, pDescription, true);
                })

            .gattDescriptorEnd()

        .gattCharacteristicEnd()

	.gattServiceEnd(); // << -- NOTE THE SEMICOLON

	//  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
	//                                                ____ _____ ___  _____
	//                                               / ___|_   _/ _ \|  _  |
	//                                               \___ \ | || | | | |_) |
	//                                                ___) || || |_| |  __/
	//                                               |____/ |_| \___/|_|
	//
	// You probably shouldn't mess with stuff beyond this point. It is required to meet BlueZ's requirements for a GATT Service.
	//
	// >>
	// >>  WHAT IT IS
	// >>
	//
	// From the BlueZ D-Bus GATT API description (https://git.kernel.org/pub/scm/bluetooth/bluez.git/plain/doc/gatt-api.txt):
	//
	//     "To make service registration simple, BlueZ requires that all objects that belong to a GATT service be grouped under a
	//     D-Bus Object Manager that solely manages the objects of that service. Hence, the standard DBus.ObjectManager interface
	//     must be available on the root service path."
	//
	// The code below does exactly that. Notice that we're doing much of the same work that our Server description does except that
	// instead of defining our own interfaces, we're following a pre-defined standard.
	//
	// The object types and method names used in the code below may look unfamiliar compared to what you're used to seeing in the
	// Server desecription. That's because the server description uses higher level types that define a more GATT-oriented framework
	// to build your GATT services. That higher level functionality was built using a set of lower-level D-Bus-oriented framework,
	// which is used in the code below.
	//  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -

	// Create the root object and push it into the list. We're going to build off of this object, so we need to get a reference
	// to the instance of the object as it resides in the list (and not the object that would be added to the list.)
	//
	// This is a non-published object (as specified by the 'false' parameter in the DBusObject constructor.) This way, we can
	// include this within our server hieararchy (i.e., within the `objects` list) but it won't be exposed by BlueZ as a Bluetooth
	// service to clietns.
	objects.push_back(DBusObject(DBusObjectPath(), false));

	// Get a reference to the new object as it resides in the list
	DBusObject &objectManager = objects.back();

	// Create an interface of the standard type 'org.freedesktop.DBus.ObjectManager'
	//
	// See: https://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces-objectmanager
	auto omInterface = std::make_shared<DBusInterface>(objectManager, "org.freedesktop.DBus.ObjectManager");

	// Add the interface to the object manager
	objectManager.addInterface(omInterface);

	// Finally, we setup the interface. We do this by adding the `GetManagedObjects` method as specified by D-Bus for the
	// 'org.freedesktop.DBus.ObjectManager' interface.
	const char *pInArgs[] = { nullptr };
	const char *pOutArgs = "a{oa{sa{sv}}}";
	omInterface->addMethod("GetManagedObjects", pInArgs, pOutArgs, INTERFACE_METHOD_CALLBACK_LAMBDA
	{
		ServerUtils::getManagedObjects(pInvocation);
	});
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Utilitarian
// ---------------------------------------------------------------------------------------------------------------------------------

// Find a D-Bus interface within the given D-Bus object
//
// If the interface was found, it is returned, otherwise nullptr is returned
std::shared_ptr<const DBusInterface> Server::findInterface(const DBusObjectPath &objectPath, const std::string &interfaceName) const
{
	for (const DBusObject &object : objects)
	{
		std::shared_ptr<const DBusInterface> pInterface = object.findInterface(objectPath, interfaceName);
		if (pInterface != nullptr)
		{
			return pInterface;
		}
	}

	return nullptr;
}

// Find and call a D-Bus method within the given D-Bus object on the given D-Bus interface
//
// If the method was called, this method returns true, otherwise false. There is no result from the method call itself.
bool Server::callMethod(const DBusObjectPath &objectPath, const std::string &interfaceName, const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const
{
	for (const DBusObject &object : objects)
	{
		if (object.callMethod(objectPath, interfaceName, methodName, pConnection, pParameters, pInvocation, pUserData))
		{
			return true;
		}
	}

	return false;
}

// Find a GATT Property within the given D-Bus object on the given D-Bus interface
//
// If the property was found, it is returned, otherwise nullptr is returned
const GattProperty *Server::findProperty(const DBusObjectPath &objectPath, const std::string &interfaceName, const std::string &propertyName) const
{
	std::shared_ptr<const DBusInterface> pInterface = findInterface(objectPath, interfaceName);

	// Try each of the GattInterface types that support properties?
	if (std::shared_ptr<const GattInterface> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattInterface))
	{
		return pGattInterface->findProperty(propertyName);
	}
	else if (std::shared_ptr<const GattService> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattService))
	{
		return pGattInterface->findProperty(propertyName);
	}
	else if (std::shared_ptr<const GattCharacteristic> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattCharacteristic))
	{
		return pGattInterface->findProperty(propertyName);
	}

	return nullptr;
}

}; // namespace ggk
