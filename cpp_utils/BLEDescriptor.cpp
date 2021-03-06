/*
 * BLEDescriptor.cpp
 *
 *  Created on: Jun 22, 2017
 *      Author: kolban
 */

#include <sstream>
#include <string.h>
#include <iomanip>
#include <stdlib.h>
#include "sdkconfig.h"
#include <esp_log.h>
#include <esp_err.h>
#include "BLEService.h"
#include "BLEDescriptor.h"

static char LOG_TAG[] = "BLEDescriptor";

extern "C" {
	char *espToString(esp_err_t value);
}

BLEDescriptor::BLEDescriptor(BLEUUID uuid) {
	m_bleUUID            = uuid;
	m_value.attr_value   = (uint8_t *)malloc(ESP_GATT_MAX_ATTR_LEN);
	m_value.attr_len     = 0;
	m_value.attr_max_len = ESP_GATT_MAX_ATTR_LEN;
	m_handle             = 0;
	m_pCharacteristic    = nullptr;

} // BLEDescriptor


BLEDescriptor::~BLEDescriptor() {
	free(m_value.attr_value);
} // ~BLEDescriptor


/**
 * @brief Execute the creation of the descriptor with the BLE runtime in ESP.
 * @param [in] pCharacteristic The characteristic to which to register this descriptor.
 */
void BLEDescriptor::executeCreate(BLECharacteristic* pCharacteristic) {
	ESP_LOGD(LOG_TAG, ">> executeCreate(): %s", toString().c_str());

	if (m_handle != 0) {
		ESP_LOGE(LOG_TAG, "Descriptor already has a handle.");
		return;
	}

	m_pCharacteristic = pCharacteristic; // Save the characteristic associated with this service.

	esp_err_t errRc = ::esp_ble_gatts_add_char_descr(
			pCharacteristic->getService()->getHandle(),
			getUUID().getNative(),
			ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
			&m_value,
			nullptr);
	if (errRc != ESP_OK) {
		ESP_LOGE(LOG_TAG, "<< esp_ble_gatts_add_char_descr: rc=%d %s", errRc, espToString(errRc));
		return;
	}
	ESP_LOGD(LOG_TAG, "<< executeCreate");
} // executeCreate


/**
 * @brief Return a string representation of the descriptor.
 * @return A string representation of the descriptor.
 */
std::string BLEDescriptor::toString() {
	std::stringstream stringstream;
	stringstream << std::hex << std::setfill('0');
	stringstream << "UUID: " << m_bleUUID.toString() + ", handle: 0x" << std::setw(2) << m_handle;
	return stringstream.str();
} // toString


BLEUUID BLEDescriptor::getUUID() {
	return m_bleUUID;
}


/**
 * @brief Set the value of the descriptor.
 * @param [in] data The data to set for the descriptor.
 * @param [in] length The length of the data in bytes.
 */
void BLEDescriptor::setValue(uint8_t* data, size_t length) {
	if (length > ESP_GATT_MAX_ATTR_LEN) {
		ESP_LOGE(LOG_TAG, "Size %d too large, must be no bigger than %d", length, ESP_GATT_MAX_ATTR_LEN);
		return;
	}
	m_value.attr_len = length;
	memcpy(m_value.attr_value, data, length);
} // setValue


void BLEDescriptor::setValue(std::string value) {
	setValue((uint8_t *)value.data(), value.length());
}

uint8_t* BLEDescriptor::getValue() {
	return m_value.attr_value;
}

size_t BLEDescriptor::getLength() {
	return m_value.attr_len;
}


/**
 * @brief Set the handle of this descriptor.
 * Set the handle of this descriptor to be the supplied value.
 * @param [in] handle The handle to be associated with this descriptor.
 * @return N/A.
 */
void BLEDescriptor::setHandle(uint16_t handle) {
	ESP_LOGD(LOG_TAG, ">> setHandle(0x%.2x): Setting descriptor handle to be 0x%.2x", handle, handle);
	m_handle = handle;
	ESP_LOGD(LOG_TAG, "<< setHandle()");
} // setHandle

void BLEDescriptor::handleGATTServerEvent(
		esp_gatts_cb_event_t      event,
		esp_gatt_if_t             gatts_if,
		esp_ble_gatts_cb_param_t *param) {
	switch(event) {
		// ESP_GATTS_ADD_CHAR_DESCR_EVT
		//
		// add_char_descr:
		// - esp_gatt_status_t status
		// - uint16_t attr_handle
		// - uint16_t service_handle
		// - esp_bt_uuid_t char_uuid
		case ESP_GATTS_ADD_CHAR_DESCR_EVT: {
			ESP_LOGD(LOG_TAG, "DEBUG: m_pCharacteristic: %x", (uint32_t)m_pCharacteristic);
			ESP_LOGD(LOG_TAG, "DEBUG: m_bleUUID: %s, add_char_descr.char_uuid: %s, equals: %d",
				m_bleUUID.toString().c_str(),
				BLEUUID(param->add_char_descr.char_uuid).toString().c_str(),
				m_bleUUID.equals(BLEUUID(param->add_char_descr.char_uuid)));
			ESP_LOGD(LOG_TAG, "DEBUG: service->getHandle: %x, add_char_descr.service_handle: %x",
					m_pCharacteristic->getService()->getHandle(), param->add_char_descr.service_handle);
			ESP_LOGD(LOG_TAG, "DEBUG: service->lastCharacteristic: %x",
					(uint32_t)m_pCharacteristic->getService()->getLastCreatedCharacteristic());
			if (m_pCharacteristic != nullptr &&
					m_bleUUID.equals(BLEUUID(param->add_char_descr.char_uuid)) &&
					m_pCharacteristic->getService()->getHandle() == param->add_char_descr.service_handle &&
					m_pCharacteristic == m_pCharacteristic->getService()->getLastCreatedCharacteristic()) {
				setHandle(param->add_char_descr.attr_handle);
			}
			break;
		} // ESP_GATTS_ADD_CHAR_DESCR_EVT

		// ESP_GATTS_WRITE_EVT - A request to write the value of a descriptor has arrived.
		//
		// write:
		// - uint16_t conn_id
		// - uint16_t trans_id
		// - esp_bd_addr_t bda
		// - uint16_t handle
		// - uint16_t offset
		// - bool need_rsp
		// - bool is_prep
		// - uint16_t len
		// - uint8_t *value
		case ESP_GATTS_WRITE_EVT: {
			if (param->write.handle == m_handle) {
				setValue(param->write.value, param->write.len);
				esp_gatt_rsp_t rsp;
				rsp.attr_value.len    = getLength();
				rsp.attr_value.handle = m_handle;
				rsp.attr_value.offset = 0;
				rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
				memcpy(rsp.attr_value.value, getValue(), rsp.attr_value.len);
				esp_err_t errRc = ::esp_ble_gatts_send_response(
						gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, &rsp);
				if (errRc != ESP_OK) {
					ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d %s", errRc, espToString(errRc));
				}
			}
			break;
		} // ESP_GATTS_WRITE_EVT

		// ESP_GATTS_READ_EVT - A request to read the value of a descriptor has arrived.
		//
		// read:
		// - uint16_t conn_id
		// - uint32_t trans_id
		// - esp_bd_addr_t bda
		// - uint16_t handle
		// - uint16_t offset
		// - bool is_long
		// - bool need_rsp
		//
		case ESP_GATTS_READ_EVT: {
			ESP_LOGD(LOG_TAG, "- Testing: Sought handle: 0x%.2x == descriptor handle: 0x%.2x ?", param->read.handle, m_handle);
			if (param->read.handle == m_handle) {
				ESP_LOGD(LOG_TAG, "Sending a response (esp_ble_gatts_send_response)");
				if (param->read.need_rsp) {
					esp_gatt_rsp_t rsp;
					rsp.attr_value.len    = getLength();
					rsp.attr_value.handle = param->read.handle;
					rsp.attr_value.offset = 0;
					rsp.attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
					memcpy(rsp.attr_value.value, getValue(), rsp.attr_value.len);
					esp_err_t errRc = ::esp_ble_gatts_send_response(
							gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
					if (errRc != ESP_OK) {
						ESP_LOGE(LOG_TAG, "esp_ble_gatts_send_response: rc=%d %s", errRc, espToString(errRc));
					}
				}
			} // ESP_GATTS_READ_EVT
			break;
		} // ESP_GATTS_READ_EVT
		default: {
			break;
		}
	}// switch event
}

uint16_t BLEDescriptor::getHandle() {
	return m_handle;
}


