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

#include "jk_number.h"
#include "esphome/core/log.h"
#include <cmath>

namespace esphome {
namespace jk_rs485_bms {

static const char *const TAG = "jk_rs485_bms.number";

void JkRS485BmsNumber::dump_config() { LOG_NUMBER("", "JkRS485Bms Number", this); }
void JkRS485BmsNumber::control(float value) {
  if (this->parent_ == nullptr) {
      ESP_LOGE(TAG, "Parent is null");
      return;
  }

  this->publish_state(value);
  //this->parent_->write_register(this->register_address_, payload, sizeof(payload))
  if (this->type_==0){
    //UINT
    if (this->data_length_==1){
      //uint8_t     
    } else {
      if (this->data_length_==2){
        //uint16_t
        if (this->register_address_==0x0104){
          this->parent_->trigger_bms2sniffer_number16_event(this->register_address_,this->third_element_of_frame_); 
        }         
      } else {
        if (this->data_length_==4){
          //uint32_t
          uint32_t value_to_send= (uint32_t) std::lround(value * std::pow(10, this->factor_));
          this->parent_->trigger_bms2sniffer_switch_or_number_uint32_event(this->register_address_,this->third_element_of_frame_, value_to_send);
        }
      }
    }

  } else {
      //INT
      if (this->type_==1){
        //int32_t
        int32_t value_to_send= (int32_t) std::lround(value * std::pow(10, this->factor_));
        this->parent_->trigger_bms2sniffer_switch_or_number_int32_event(this->register_address_,this->third_element_of_frame_, value_to_send);
      }    
  }
  


}

}  // namespace jk_bms_ble
}  // namespace esphome
