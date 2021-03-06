/*
 * BLEService.cpp
 *
 *  Created on: Mar 25, 2017
 *      Author: kolban
 */

// A service is identified by a UUID.  A service is also the container for one or more characteristics.

#include "sdkconfig.h"
#if defined(CONFIG_BT_ENABLED)
#include <esp_log.h>
#include <esp_err.h>
#include <esp_gatts_api.h>
#include <sstream>
#include <iomanip>
#include <string>

#include "BLEService.h"
#include "BLEUtils.h"

extern "C" {
	char *espToString(esp_err_t value);
}

static char LOG_TAG[] = "BLEService";

BLEService::BLEService(BLEUUID uuid) {
	m_uuid     = uuid;
	m_handle   = 0;
	m_gatts_if = 0;
	m_serializeMutex.setName("BLEService");
	m_lastCreatedCharacteristic = nullptr;
}


BLEService::~BLEService() {
}


/**
 * @brief Create the service.
 * Create the service.
 * @param [in] gatts_if The handle of the GATT server interface.
 * @return N/A.
 */
void BLEService::executeCreate(esp_gatt_if_t gatts_if) {
	ESP_LOGD(LOG_TAG, ">> executeCreate() - Creating service (esp_ble_gatts_create_service)");
	m_gatts_if           = gatts_if;
	m_srvc_id.id.inst_id = 0;
	m_srvc_id.id.uuid    = *m_uuid.getNative();

	m_serializeMutex.take("executeCreate"); // Take the mutex and release at event ESP_GATTS_CREATE_EVT
	esp_err_t errRc = ::esp_ble_gatts_create_service(m_gatts_if, &m_srvc_id, 10);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "esp_ble_gatts_create_service: rc=%d %s", errRc, espToString(errRc));
		return;
	}
	ESP_LOGD(LOG_TAG, "<< executeCreate()");
} // executeCreate


/**
 * @brief Dump details of this BLE GATT service.
 * @return N/A.
 */
void BLEService::dump() {
	std::string name = "unknown";
	if (m_srvc_id.id.uuid.len == ESP_UUID_LEN_16) {
		name = BLEUtils::gattServiceToString(m_srvc_id.id.uuid.uuid.uuid16);
	}
	ESP_LOGD(LOG_TAG, "Service: uuid:%s, handle: 0x%.2x",
		m_uuid.toString().c_str(),
		m_handle);
	ESP_LOGD(LOG_TAG, "Characteristics:\n%s", m_characteristicMap.toString().c_str());
} // dump


void BLEService::setService(esp_gatt_srvc_id_t srvc_id) {
	m_srvc_id = srvc_id;
}

esp_gatt_srvc_id_t BLEService::getService() {
	return m_srvc_id;
}


/**
 * @brief Get the UUID of the service.
 * @return the UUID of the service.
 */
BLEUUID BLEService::getUUID() {
	return m_uuid;
} // getUUID


/**
 * @brief Start the service.
 * @return Start the service.
 */
void BLEService::start() {
	ESP_LOGD(LOG_TAG, ">> start(): Starting service (esp_ble_gatts_start_service): %s", toString().c_str());
	esp_err_t errRc = ::esp_ble_gatts_start_service(m_handle);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_start_service: rc=%d %s", errRc, espToString(errRc));
		return;
	}
	BLECharacteristic *pCharacteristic = m_characteristicMap.getFirst();
	while(pCharacteristic != nullptr) {
		m_lastCreatedCharacteristic = pCharacteristic;
		pCharacteristic->executeCreate(this);

		pCharacteristic = m_characteristicMap.getNext();
	}
	// Start each of the characteristics ... these are found in the m_characteristicMap.

	ESP_LOGD(LOG_TAG, "<< start()");
} // start

/**
 * @brief Set the handle associated with this service.
 * @param [in] handle The handle associated with the service.
 */
void BLEService::setHandle(uint16_t handle) {
	ESP_LOGD(LOG_TAG, ">> setHandle(0x%.2x)", handle);
	m_handle = handle;
	ESP_LOGD(LOG_TAG, "<< setHandle()");
} // setHandle


/**
 * @brief Get the handle associated with this service.
 * @return The handle associated with this service.
 */
uint16_t BLEService::getHandle() {
	return m_handle;
}


/**
 * @brief Add a characteristic to the service.
 * @param [in] pCharacteristic A pointer to the characteristic to be added.
 */
void BLEService::addCharacteristic(BLECharacteristic* pCharacteristic) {
// We maintain a mapping of characteristics owned by this service.  These are managed by the
// BLECharacteristicMap class instance found in m_characteristicMap.  We add the characteristic
// to the map and then ask the service to add the characteristic at the BLE level (ESP-IDF).
//
	ESP_LOGD(LOG_TAG, ">> addCharacteristic()");
	ESP_LOGD(LOG_TAG, "Adding characteristic (esp_ble_gatts_add_char): uuid=%s to service: %s",
		pCharacteristic->getUUID().toString().c_str(),
		toString().c_str());

	// Check that we don't add the same characteristic twice.
	if (m_characteristicMap.getByUUID(pCharacteristic->getUUID()) != nullptr) {
		ESP_LOGE(LOG_TAG, "<< Attempt to add a characteristic but we already have one with this UUID");
		return;
	}

	// Remember this characteristic in our map of characteristics.  At this point, we can lookup by UUID
	// but not by handle.  The handle is allocated to us on the ESP_GATTS_ADD_CHAR_EVT.
	m_characteristicMap.setByUUID(pCharacteristic->getUUID(), pCharacteristic);

	ESP_LOGD(LOG_TAG, "<< addCharacteristic()");
} // addCharacteristic


void BLEService::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t *param) {


	switch(event) {
	  // ESP_GATTS_ADD_CHAR_EVT - Indicate that a characteristic was added to the service.
		// add_char:
		// - esp_gatt_status_t status
		// - uint16_t attr_handle
		// - uint16_t service_handle
		// - esp_bt_uuid_t char_uuid

		// If we have reached the correct service, then locate the characteristic and remember the handle
		// for that characteristic.
		case ESP_GATTS_ADD_CHAR_EVT: {
			if (m_handle == param->add_char.service_handle) {
				BLECharacteristic *pCharacteristic = getCharacteristic(BLEUUID(param->add_char.char_uuid));
				if (pCharacteristic == nullptr) {
					ESP_LOGE(LOG_TAG, "Expected to find characteristic with UUID: %s, but didnt!",
							BLEUUID(param->add_char.char_uuid).toString().c_str());
					dump();
					m_serializeMutex.give();
					break;
				}
				pCharacteristic->setHandle(param->add_char.attr_handle);
				m_characteristicMap.setByHandle(param->add_char.attr_handle, pCharacteristic);
				//ESP_LOGD(tag, "Characteristic map: %s", m_characteristicMap.toString().c_str());
				m_serializeMutex.give();
				break;
			} // Reached the correct service.
		} // ESP_GATTS_ADD_CHAR_EVT

		// ESP_GATTS_CREATE_EVT
		// Called when a new service is registered as having been created.
		//
		// create:
		// * esp_gatt_status_t status
		// * uint16_t service_handle
		// * esp_gatt_srvc_id_t service_id
		// * - esp_gatt_id id
		// *   - esp_bt_uuid uuid
		// *   - uint8_t inst_id
		// * - bool is_primary
		//
		case ESP_GATTS_CREATE_EVT: {
			if (getUUID().equals(BLEUUID(param->create.service_id.id.uuid))) {
				setHandle(param->create.service_handle);
				m_serializeMutex.give();
			}
			break;
		}

		default: {
			break;
		} // Default
	} // Switch

	m_characteristicMap.handleGATTServerEvent(event, gatts_if, param);
} // handleGATTServerEvent


BLECharacteristic* BLEService::getCharacteristic(BLEUUID uuid) {
	return m_characteristicMap.getByUUID(uuid);
}

/**
 * @brief Return a string representation of this service.
 * A service is defined by:
 * * Its UUID
 * * Its handle
 * @return A string representation of this service.
 */
std::string BLEService::toString() {
	std::stringstream stringStream;
	stringStream << "UUID: " << getUUID().toString() <<
		", handle: 0x" << std::hex << std::setfill('0') << std::setw(2) << getHandle();
	return stringStream.str();
} // toString

BLECharacteristic* BLEService::getLastCreatedCharacteristic() {
	return m_lastCreatedCharacteristic;
}

#endif // CONFIG_BT_ENABLED
