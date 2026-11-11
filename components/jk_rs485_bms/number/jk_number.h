// Updated : 2026.11.11
// Version : 1.2.0
// GitHub  : https://github.com/Sleeper85/esphome-components

// This YAML is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/gpl.html>.

#pragma once

#include "../jk_rs485_bms.h"
#include "esphome/core/component.h"
#include "esphome/components/number/number.h"

namespace esphome {
namespace jk_rs485_bms {

class JkRS485Bms;

class JkRS485BmsNumber : public number::Number, public Component {
 public:
  // Constructor por defecto necesario para crear instancias sin argumentos
  JkRS485BmsNumber() = default;

  explicit JkRS485BmsNumber(bool initial_state);
  virtual ~JkRS485BmsNumber() = default; // Destructor por defecto


  void set_parent(JkRS485Bms *parent) { this->parent_ = parent; };
  void set_register_address(uint16_t register_address) { this->register_address_ = register_address; };
  void set_third_element_of_frame(uint8_t third_element_of_frame) { this->third_element_of_frame_ = third_element_of_frame; };
  void set_data_length(uint8_t data_length) { this->data_length_ = data_length; };
  void set_factor(float factor) { this->factor_ = factor; };
  void set_type(uint8_t type) { this->type_ = type; };
  void dump_config() override;
  void loop() override {}
  float get_setup_priority() const override { return setup_priority::DATA; }


 protected:
  void control(float value) override;

  JkRS485Bms *parent_{nullptr};
  uint16_t register_address_{0};
  uint8_t third_element_of_frame_{0};
  uint8_t data_length_{0};
  float factor_{1000.0f};
  uint8_t type_{0};


};

}  // namespace jk_bms_ble
}  // namespace esphome
