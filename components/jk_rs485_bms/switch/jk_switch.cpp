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

#include "jk_switch.h"

namespace esphome {
namespace jk_rs485_bms {

//JkRS485BmsSwitch::JkRS485BmsSwitch(bool some_param) {
//  // Constructor implementation...
//}

static const char *const TAG = "jk_rs485_bms.switch";


JkRS485BmsSwitch::JkRS485BmsSwitch(bool initial_state) {
  this->write_state(initial_state);
}

void JkRS485BmsSwitch::dump_config() { LOG_SWITCH("", "JkRS485BmsSwitch DUMP CONFIG", this); }

void JkRS485BmsSwitch::write_state(bool state) {
    if (this->parent_ == nullptr) {
        ESP_LOGE(TAG, "Parent is null");
        return;
    }

    if (this->data_length_>0){
      this->publish_state(state);
      if (this->register_address_==0x0014){
        this->parent_->trigger_bms2sniffer_switch16_event(this->register_address_,this->third_element_of_frame_); 
      } else {
        std::uint64_t value_to_send = (state) ? 0x00000001 : 0x00000000;      
        this->parent_->trigger_bms2sniffer_switch_or_number_uint32_event(this->register_address_, this->third_element_of_frame_, value_to_send);
      }
    }
    

    
}


}  // namespace jk_rs485_bms
}  // namespace esphome

