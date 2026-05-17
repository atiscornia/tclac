/**
* Create by Miguel Ángel López on 20/07/19
* and modify by xaxexa
* Refactoring & component making:
* Соловей с паяльником 15.03.2024
**/
#include "esphome.h"
#include "esphome/core/defines.h"
#include "tclac.h"

namespace esphome{
namespace tclac{


ClimateTraits tclacClimate::traits() {
	auto traits = climate::ClimateTraits();
	traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
	
	if (this->supported_modes_.empty()) {
		traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
		traits.add_supported_mode(climate::CLIMATE_MODE_HEAT_COOL);
	} else {
		for (auto mode : this->supported_modes_)
			traits.add_supported_mode(mode);
	}
	// Solo agregamos presets si fueron configurados explicitamente
	if (!this->supported_presets_.empty()) {
		for (auto preset : this->supported_presets_)
			traits.add_supported_preset(preset);
	}
	if (this->supported_fan_modes_.empty()) {
		traits.add_supported_fan_mode(climate::CLIMATE_FAN_AUTO);
	} else {
		for (auto fan_mode : this->supported_fan_modes_)
			traits.add_supported_fan_mode(fan_mode);
	}
	if (this->supported_swing_modes_.empty()) {
		traits.add_supported_swing_mode(climate::CLIMATE_SWING_OFF);
	} else {
		for (auto swing_mode : this->supported_swing_modes_)
			traits.add_supported_swing_mode(swing_mode);
	}

	return traits;
}


void tclacClimate::setup() {

#ifdef CONF_RX_LED
	this->rx_led_pin_->setup();
	this->rx_led_pin_->digital_write(false);
#endif
#ifdef CONF_TX_LED
	this->tx_led_pin_->setup();
	this->tx_led_pin_->digital_write(false);
#endif
}

void tclacClimate::loop()  {
	if (esphome::uart::UARTDevice::available() > 0) {
		dataShow(0, true);
		dataRX[0] = esphome::uart::UARTDevice::read();
		if (dataRX[0] != 0xBB) {
			dataShow(0,0);
			return;
		}
		dataRX[1] = esphome::uart::UARTDevice::read();
		dataRX[2] = esphome::uart::UARTDevice::read();
		dataRX[3] = esphome::uart::UARTDevice::read();
		dataRX[4] = esphome::uart::UARTDevice::read();

		esphome::uart::UARTDevice::read_array(dataRX+5, dataRX[4]+1);

		uint8_t check = getChecksum(dataRX, sizeof(dataRX));
		
		if (check != dataRX[60]) {
			ESP_LOGD("TCL", "Checksum invalido %x", check);
			this->dataShow(0,0);
			return;
		}
		this->dataShow(0,0);
		this->readData();
	}
}

void tclacClimate::update() {
	tclacClimate::dataShow(1,1);
	this->esphome::uart::UARTDevice::write_array(poll, sizeof(poll));
	tclacClimate::dataShow(1,0);
}

void tclacClimate::readData() {
	
	current_temperature = float((( (dataRX[17] << 8) | dataRX[18] ) / 374 - 32)/1.8);
	target_temperature = (dataRX[FAN_SPEED_POS] & SET_TEMP_MASK) + 16;

	if (dataRX[MODE_POS] & ( 1 << 4)) {
		// Aire encendido
		uint8_t modeswitch = MODE_MASK & dataRX[MODE_POS];
		uint8_t fanspeedswitch = FAN_SPEED_MASK & dataRX[FAN_SPEED_POS];
		uint8_t swingmodeswitch = SWING_MODE_MASK & dataRX[SWING_POS];

		switch (modeswitch) {
			case MODE_AUTO:
				// HA bloquea el slider de temperatura en CLIMATE_MODE_AUTO.
				// Mapeamos a HEAT_COOL que si lo permite. Internamente el aire sigue en su AUTO real.
				this->mode = climate::CLIMATE_MODE_HEAT_COOL;
				break;
			case MODE_COOL:
				this->mode = climate::CLIMATE_MODE_COOL;
				break;
			case MODE_DRY:
				this->mode = climate::CLIMATE_MODE_DRY;
				break;
			case MODE_FAN_ONLY:
				this->mode = climate::CLIMATE_MODE_FAN_ONLY;
				break;
			case MODE_HEAT:
				this->mode = climate::CLIMATE_MODE_HEAT;
				break;
			default:
				this->mode = climate::CLIMATE_MODE_HEAT_COOL;
		}

		if ( dataRX[FAN_QUIET_POS] & FAN_QUIET) {
			fan_mode = climate::CLIMATE_FAN_QUIET;
		} else if (dataRX[MODE_POS] & FAN_DIFFUSE){
			fan_mode = climate::CLIMATE_FAN_DIFFUSE;
		} else {
			switch (fanspeedswitch) {
				case FAN_AUTO:
					fan_mode = climate::CLIMATE_FAN_AUTO;
					break;
				case FAN_LOW:
					fan_mode = climate::CLIMATE_FAN_LOW;
					break;
				case FAN_MIDDLE:
					fan_mode = climate::CLIMATE_FAN_MIDDLE;
					break;
				case FAN_MEDIUM:
					fan_mode = climate::CLIMATE_FAN_MEDIUM;
					break;
				case FAN_HIGH:
					fan_mode = climate::CLIMATE_FAN_HIGH;
					break;
				case FAN_FOCUS:
					fan_mode = climate::CLIMATE_FAN_FOCUS;
					break;
				default:
					fan_mode = climate::CLIMATE_FAN_AUTO;
			}
		}

		switch (swingmodeswitch) {
			case SWING_OFF: 
				swing_mode = climate::CLIMATE_SWING_OFF;
				break;
			case SWING_HORIZONTAL:
				swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
				break;
			case SWING_VERTICAL:
				swing_mode = climate::CLIMATE_SWING_VERTICAL;
				break;
			case SWING_BOTH:
				swing_mode = climate::CLIMATE_SWING_BOTH;
				break;
		}
		
		// Procesamiento de presets (solo si hay presets configurados)
		if (!this->supported_presets_.empty()) {
			preset = ClimatePreset::CLIMATE_PRESET_NONE;
			if (dataRX[7] & (1 << 6)){
				preset = ClimatePreset::CLIMATE_PRESET_ECO;
			} else if (dataRX[9] & (1 << 2)){
				preset = ClimatePreset::CLIMATE_PRESET_COMFORT;
			} else if (dataRX[19] & (1 << 0)){
				preset = ClimatePreset::CLIMATE_PRESET_SLEEP;
			}
		}
		
	} else {
		// Aire apagado
		this->mode = climate::CLIMATE_MODE_OFF;
		this->swing_mode = climate::CLIMATE_SWING_OFF;
		if (!this->supported_presets_.empty()) {
			this->preset = ClimatePreset::CLIMATE_PRESET_NONE;
		}
	}
	this->publish_state();
	allow_take_control = true;
}

// Control desde HA
void tclacClimate::control(const climate::ClimateCall &call) {
	
	if (call.get_mode().has_value()) this->mode = *call.get_mode();
	if (call.get_target_temperature().has_value()) this->target_temperature = *call.get_target_temperature();
	if (call.get_fan_mode().has_value()) this->fan_mode = *call.get_fan_mode();
	if (call.get_swing_mode().has_value()) this->swing_mode = *call.get_swing_mode();
	if (call.get_preset().has_value()) this->preset = *call.get_preset();
	
	this->publish_state();
	this->takeControl();
	this->allow_take_control = true;
}


void tclacClimate::takeControl() {
	
	dataTX[7]  = 0b00000000;
	dataTX[8]  = 0b00000000;
	dataTX[9]  = 0b00000000;
	dataTX[10] = 0b00000000;
	dataTX[11] = 0b00000000;
	dataTX[19] = 0b00000000;
	dataTX[32] = 0b00000000;
	dataTX[33] = 0b00000000;
	
	uint8_t target_temperature_set = 31-(int)target_temperature;
	
	// Pitido
	if (beeper_status_){
		dataTX[7] += 0b00100000;
	}
	
	// Display del aire (solo si el aire esta encendido)
	// ATENCION: al apagar el display, el aire pasa forzadamente a modo automatico
	if ((display_status_) && (mode != climate::CLIMATE_MODE_OFF)){
		dataTX[7] += 0b01000000;
	}
		
	// Modo del aire
	switch (this->mode) {
		case climate::CLIMATE_MODE_OFF:
			dataTX[7] += 0b00000000;
			dataTX[8] += 0b00000000;
			break;
		case climate::CLIMATE_MODE_AUTO:
		case climate::CLIMATE_MODE_HEAT_COOL:
			// HEAT_COOL se traduce al modo AUTO del aire (que si acepta setpoint)
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00001000;
			break;
		case climate::CLIMATE_MODE_COOL:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000011;	
			break;
		case climate::CLIMATE_MODE_DRY:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000010;	
			break;
		case climate::CLIMATE_MODE_FAN_ONLY:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000111;	
			break;
		case climate::CLIMATE_MODE_HEAT:
			dataTX[7] += 0b00000100;
			dataTX[8] += 0b00000001;	
			break;
	}

	// Modo del ventilador
	if (this->fan_mode.has_value()) {
		switch(*this->fan_mode) {
			case climate::CLIMATE_FAN_AUTO:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000000;
				break;
			case climate::CLIMATE_FAN_QUIET:
				dataTX[8]	+= 0b10000000;
				dataTX[10]	+= 0b00000000;
				break;
			case climate::CLIMATE_FAN_LOW:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000001;
				break;
			case climate::CLIMATE_FAN_MIDDLE:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000110;
				break;
			case climate::CLIMATE_FAN_MEDIUM:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000011;
				break;
			case climate::CLIMATE_FAN_HIGH:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000111;
				break;
			case climate::CLIMATE_FAN_FOCUS:
				dataTX[8]	+= 0b00000000;
				dataTX[10]	+= 0b00000101;
				break;
			case climate::CLIMATE_FAN_DIFFUSE:
				dataTX[8]	+= 0b01000000;
				dataTX[10]	+= 0b00000000;
				break;
		}
	}
	
	// Modo de barrido de los flaps
	switch(this->swing_mode) {
		case climate::CLIMATE_SWING_OFF:
			dataTX[10]	+= 0b00000000;
			dataTX[11]	+= 0b00000000;
			break;
		case climate::CLIMATE_SWING_VERTICAL:
			dataTX[10]	+= 0b00111000;
			dataTX[11]	+= 0b00000000;
			break;
		case climate::CLIMATE_SWING_HORIZONTAL:
			dataTX[10]	+= 0b00000000;
			dataTX[11]	+= 0b00001000;
			break;
		case climate::CLIMATE_SWING_BOTH:
			dataTX[10]	+= 0b00111000;
			dataTX[11]	+= 0b00001000;  
			break;
	}
	
	// Presets del aire
	if (this->preset.has_value()) {
		switch(*this->preset) {
			case ClimatePreset::CLIMATE_PRESET_NONE:
				break;
			case ClimatePreset::CLIMATE_PRESET_ECO:
				dataTX[7]	+= 0b10000000;
				break;
			case ClimatePreset::CLIMATE_PRESET_SLEEP:
				dataTX[19]	+= 0b00000001;
				break;
			case ClimatePreset::CLIMATE_PRESET_COMFORT:
				dataTX[8]	+= 0b00010000;
				break;
		}
	}

	// Configuracion de los flaps:
	//   Flap vertical:
	//     Barrido vertical [byte 10, mascara 00111000]:
	//       000 - barrido desactivado, flap en ultima posicion o fijo
	//       111 - barrido activado en el modo seleccionado
	//     Modo de barrido vertical [byte 32, mascara 00011000]:
	//       01 - barrido de arriba a abajo (POR DEFECTO)
	//       10 - barrido en la mitad superior
	//       11 - barrido en la mitad inferior
	//     Modo de fijacion del flap [byte 32, mascara 00000111]:
	//       000 - sin fijacion (POR DEFECTO)
	//       001 - fijacion arriba del todo
	//       010 - fijacion entre arriba y centro
	//       011 - fijacion en el centro
	//       100 - fijacion entre centro y abajo
	//       101 - fijacion abajo del todo
	//   Flaps horizontales:
	//     Barrido horizontal [byte 11, mascara 00001000]:
	//       0 - barrido desactivado, flaps en ultima posicion o fijos
	//       1 - barrido activado en el modo seleccionado
	//     Modo de barrido horizontal [byte 33, mascara 00111000]:
	//       001 - barrido de izquierda a derecha (POR DEFECTO)
	//       010 - barrido a la izquierda
	//       011 - barrido en el centro
	//       100 - barrido a la derecha
	//     Modo de fijacion de los flaps horizontales [byte 33, mascara 00000111]:
	//       000 - sin fijacion (POR DEFECTO)
	//       001 - fijacion a la izquierda
	//       010 - fijacion entre izquierda y centro
	//       011 - fijacion en el centro
	//       100 - fijacion entre centro y derecha
	//       101 - fijacion a la derecha

	// Modo del barrido vertical
	switch(vertical_swing_direction_) {
		case VerticalSwingDirection::UP_DOWN:
			dataTX[32]	+= 0b00001000;
			break;
		case VerticalSwingDirection::UPSIDE:
			dataTX[32]	+= 0b00010000;
			break;
		case VerticalSwingDirection::DOWNSIDE:
			dataTX[32]	+= 0b00011000;
			break;
	}
	// Modo del barrido horizontal
	switch(horizontal_swing_direction_) {
		case HorizontalSwingDirection::LEFT_RIGHT:
			dataTX[33]	+= 0b00001000;
			break;
		case HorizontalSwingDirection::LEFTSIDE:
			dataTX[33]	+= 0b00010000;
			break;
		case HorizontalSwingDirection::CENTER:
			dataTX[33]	+= 0b00011000;
			break;
		case HorizontalSwingDirection::RIGHTSIDE:
			dataTX[33]	+= 0b00100000;
			break;
	}
	// Posicion de fijacion del flap vertical
	switch(vertical_direction_) {
		case AirflowVerticalDirection::LAST:
			dataTX[32]	+= 0b00000000;
			break;
		case AirflowVerticalDirection::MAX_UP:
			dataTX[32]	+= 0b00000001;
			break;
		case AirflowVerticalDirection::UP:
			dataTX[32]	+= 0b00000010;
			break;
		case AirflowVerticalDirection::CENTER:
			dataTX[32]	+= 0b00000011;
			break;
		case AirflowVerticalDirection::DOWN:
			dataTX[32]	+= 0b00000100;
			break;
		case AirflowVerticalDirection::MAX_DOWN:
			dataTX[32]	+= 0b00000101;
			break;
	}
	// Posicion de fijacion del flap horizontal
	switch(horizontal_direction_) {
		case AirflowHorizontalDirection::LAST:
			dataTX[33]	+= 0b00000000;
			break;
		case AirflowHorizontalDirection::MAX_LEFT:
			dataTX[33]	+= 0b00000001;
			break;
		case AirflowHorizontalDirection::LEFT:
			dataTX[33]	+= 0b00000010;
			break;
		case AirflowHorizontalDirection::CENTER:
			dataTX[33]	+= 0b00000011;
			break;
		case AirflowHorizontalDirection::RIGHT:
			dataTX[33]	+= 0b00000100;
			break;
		case AirflowHorizontalDirection::MAX_RIGHT:
			dataTX[33]	+= 0b00000101;
			break;
	}

	// Temperatura
	dataTX[9] = target_temperature_set;
		
	// Header y bytes fijos
	dataTX[0] = 0xBB;
	dataTX[1] = 0x00;
	dataTX[2] = 0x01;
	dataTX[3] = 0x03;
	dataTX[4] = 0x20;
	dataTX[5] = 0x03;
	dataTX[6] = 0x01;
	dataTX[12] = 0x00;
	dataTX[13] = 0x01;
	dataTX[14] = 0x00;
	dataTX[15] = 0x00;
	dataTX[16] = 0x00;
	dataTX[17] = 0x00;
	dataTX[18] = 0x00;
	dataTX[20] = 0x00;
	dataTX[21] = 0x00;
	dataTX[22] = 0x00;
	dataTX[23] = 0x00;
	dataTX[24] = 0x00;
	dataTX[25] = 0x00;
	dataTX[26] = 0x00;
	dataTX[27] = 0x00;
	dataTX[28] = 0x00;
	dataTX[30] = 0x00;
	dataTX[31] = 0x00;
	dataTX[34] = 0x00;
	dataTX[35] = 0x00;
	dataTX[36] = 0x00;
	dataTX[37] = 0xFF;
	dataTX[37] = tclacClimate::getChecksum(dataTX, sizeof(dataTX));

	tclacClimate::sendData(dataTX, sizeof(dataTX));
	allow_take_control = false;
	is_call_control = false;
}

// Envio de datos al aire
void tclacClimate::sendData(uint8_t * message, uint8_t size) {
	tclacClimate::dataShow(1,1);
	this->esphome::uart::UARTDevice::write_array(message, size);
	tclacClimate::dataShow(1,0);
}

// Bytes a hex
String tclacClimate::getHex(uint8_t *message, uint8_t size) {
	String raw;
	char buf[4];
	for (int i = 0; i < size; i++) {
		snprintf(buf, sizeof(buf), "%02X ", message[i]);
		raw += buf;
	}
	return raw;
}

// Checksum
uint8_t tclacClimate::getChecksum(const uint8_t * message, size_t size) {
	uint8_t position = size - 1;
	uint8_t crc = 0;
	for (int i = 0; i < position; i++)
		crc ^= message[i];
	return crc;
}

// LEDs de comunicacion
void tclacClimate::dataShow(bool flow, bool shine) {
	if (module_display_status_){
		if (flow == 0){
			if (shine == 1){
#ifdef CONF_RX_LED
				this->rx_led_pin_->digital_write(true);
#endif
			} else {
#ifdef CONF_RX_LED
				this->rx_led_pin_->digital_write(false);
#endif
			}
		}
		if (flow == 1) {
			if (shine == 1){
#ifdef CONF_TX_LED
				this->tx_led_pin_->digital_write(true);
#endif
			} else {
#ifdef CONF_TX_LED
				this->tx_led_pin_->digital_write(false);
#endif
			}
		}
	}
}

void tclacClimate::set_beeper_state(bool state) {
	this->beeper_status_ = state;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_display_state(bool disp_state) {
	this->display_status_ = disp_state;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_force_mode_state(bool f_state) {
	this->force_mode_status_ = f_state;
}
#ifdef CONF_RX_LED
void tclacClimate::set_rx_led_pin(GPIOPin *rx_led_pin) {
	this->rx_led_pin_ = rx_led_pin;
}
#endif
#ifdef CONF_TX_LED
void tclacClimate::set_tx_led_pin(GPIOPin *tx_led_pin) {
	this->tx_led_pin_ = tx_led_pin;
}
#endif
void tclacClimate::set_module_display_state(bool d_state) {
	this->module_display_status_ = d_state;
}
void tclacClimate::set_vertical_airflow(AirflowVerticalDirection v_airflow) {
	this->vertical_direction_ = v_airflow;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_horizontal_airflow(AirflowHorizontalDirection h_airflow) {
	this->horizontal_direction_ = h_airflow;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_vertical_swing_direction(VerticalSwingDirection vs_direction) {
	this->vertical_swing_direction_ = vs_direction;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_supported_modes(climate::ClimateModeMask modes) {
	this->supported_modes_ = modes;
}
void tclacClimate::set_horizontal_swing_direction(HorizontalSwingDirection hs_direction) {
	horizontal_swing_direction_ = hs_direction;
	if (force_mode_status_){
		if (allow_take_control){
			tclacClimate::takeControl();
		}
	}
}
void tclacClimate::set_supported_fan_modes(climate::ClimateFanModeMask fan_modes){
	this->supported_fan_modes_ = fan_modes;
}
void tclacClimate::set_supported_swing_modes(climate::ClimateSwingModeMask swing_modes) {
	this->supported_swing_modes_ = swing_modes;
}
void tclacClimate::set_supported_presets(climate::ClimatePresetMask presets) {
  this->supported_presets_ = presets;
}


}
}
