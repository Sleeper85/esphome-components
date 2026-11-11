// Updated : 2026.11.11
// Version : 1.2.2
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

#include "jk_rs485_sniffer.h"

#include <cstring>


namespace esphome {
namespace jk_rs485_sniffer {

static const char *const TAG = "jk_rs485_sniffer";

static const uint16_t JKPB_RS485_MASTER_SHORT_REQUEST_SIZE = 8;
static const uint16_t JKPB_RS485_MASTER_REQUEST_SIZE = 11;
static const uint16_t JKPB_RS485_RESPONSE_SIZE = 308;

// Cap on rx_buffer_ growth : std::vector::max_size() is the allocator limit and is never reached
static const size_t RX_BUFFER_MAX_SIZE = 4 * JKPB_RS485_RESPONSE_SIZE;

static const uint16_t JKPB_RS485_NUMBER_OF_ELEMENTS_TO_COMPUTE_CHECKSUM = 299;
static const uint16_t JKPB_RS485_FRAME_TYPE_ADDRESS = 4;
static const uint16_t JKPB_RS485_FRAME_TYPE_ADDRESS_FOR_FRAME_TYPE_x01 = 264;
static const uint16_t JKPB_RS485_CHECKSUM_INDEX = 299;
static const uint16_t JKPB_RS485_ADDRESS_OF_RS485_ADDRESS = 300;

static const uint16_t MIN_SILENCE_MILLISECONDS = 150; // MIN TIME THAT MEANS THAT THERE IS A SILENCE
static const uint16_t MIN_SILENCE_NEEDED_BEFORE_SPEAKING_MILLISECONDS = 250;

static const uint32_t TIME_BETWEEN_CELL_INFO_REQUESTS_MILLISECONDS       =  5000;
static const uint32_t TIME_BETWEEN_DEVICE_SETTINGS_REQUESTS_MILLISECONDS = 10000; //5000
static const uint32_t TIME_BETWEEN_DEVICE_INFO_REQUESTS_MILLISECONDS     = 3600000; //3600000

static const uint16_t SILENCE_BEFORE_ACTING_AS_MASTER = 2000;
static const uint16_t SILENCE_BEFORE_REUSING_NETWORK_ACTING_AS_MASTER=400;
static const uint16_t TIME_BEFORE_NEXT_POOLING_MILLISENCONDS=750;
static const uint16_t TIME_BETWEEN_CONSECUTIVE_REQUEST_SENDINGS_TO_SAME_SLAVE=2500;


static const uint16_t TIME_BETWEEN_NETWORK_SCAN_MILLISECONDS=500;  // to improve
static const uint16_t NO_MESSAGE_RECEIVED_TIME_SET_AS_UNAVAILABLE_MILLISECONDS = 10000;
// Mode 2 : a node is set unavailable after this number of consecutive polls without answer.
// A time limit does not work there : with many BMSs, each one is only polled every few seconds
static const uint8_t MAX_MISSED_REQUESTS = 3;
// Writes waiting for a bus silence : 15 BMSs in broadcast mode take 15 entries per change
static const size_t WRITE_QUEUE_MAX_SIZE = 32;

// Result of one manage_rx_buffer_() pass
enum RxParseResult : uint8_t {
  RX_NO_DATA = 0,
  RX_PARSED_SHORT = 1,
  RX_PARSED_REQUEST = 2,
  RX_PARSED_RESPONSE = 3,
  RX_NEED_MORE = 4,
  RX_RESYNC = 5,
  RX_CHECKSUM_FAIL = 6,
};

static const char *rx_result_to_string(uint8_t value) {
  switch (value) {
    case RX_NO_DATA:
      return "no_data";
    case RX_PARSED_SHORT:
      return "parsed_short";
    case RX_PARSED_REQUEST:
      return "parsed_request";
    case RX_PARSED_RESPONSE:
      return "parsed_response";
    case RX_NEED_MORE:
      return "need_more";
    case RX_RESYNC:
      return "resync";
    case RX_CHECKSUM_FAIL:
      return "checksum_fail";
    default:
      return "unknown";
  }
}

std::vector<unsigned char> pattern_response_header = {0x55, 0xAA, 0xEB, 0x90};


uint16_t crc16_c(const uint8_t data[], const uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x1) == 1) {
                crc = (crc >> 1) ^ 40961;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return (((crc & 0x00FF)<<8) | ((crc & 0xFF00))>>8);
}

uint16_t chksum(const uint8_t data[], const uint16_t len) {
  uint16_t checksum = 0;
  for (uint16_t i = 0; i < len; i++) {
    checksum = checksum + data[i];
  }
  return checksum;
}


void JkRS485Sniffer::set_broadcast_changes_to_all_bms(bool state) {
    this->broadcast_changes_to_all_bms_ = state;
    if (state) {
      ESP_LOGD(TAG, "Broadcast to All BMSs ACTIVATED");
      // Code to run on activation can be added here
    } else {
      ESP_LOGD(TAG, "Broadcast to All BMSs DEACTIVATED");
      // Code to run on deactivation can be added here
    }
}


bool JkRS485Sniffer::get_broadcast_changes_to_all_bms() const {
    return this->broadcast_changes_to_all_bms_;
}


void JkRS485Sniffer::handle_bms2sniffer_event(std::uint8_t slave_address, std::string event, std::uint8_t frame_type)
{
  // Handle the event here. For example, you can print the event:
  ESP_LOGD(TAG,"Received Event from BMS.. [address:0x%02X] @ %d -->  %s", slave_address, frame_type, event.c_str());
  const uint32_t now=millis();

  // rs485_network_node[] has 16 slots (addresses 0x00 to 0x0F)
  if (slave_address > 15) {
    ESP_LOGE(TAG, "slave_address 0x%02X out of range (max 0x0F), ignoring", slave_address);
    return;
  }

  if (frame_type==1)
  {
    this->rs485_network_node[slave_address].last_device_settings_request_received_OK=now;  
    this->rs485_network_node[slave_address].counter_device_settings_received++;
    ESP_LOGD(TAG, "updated last_device_settings_request_received_OK");
  }
  else if (frame_type==2)
  {
    this->rs485_network_node[slave_address].last_cell_info_request_received_OK=now;  
    this->rs485_network_node[slave_address].counter_cell_info_received++;
    ESP_LOGD(TAG, "updated last_cell_info_request_received_OK");
  }
  else if (frame_type==3)
  {
    this->rs485_network_node[slave_address].last_device_info_request_received_OK=now;  
    this->rs485_network_node[slave_address].counter_device_info_received++;
    ESP_LOGD(TAG, "updated last_device_info_request_received_OK");
  }

  this->last_jk_rs485_network_activity_=now;
  if (this->act_as_master==true)
  {
    this->last_message_received_acting_as_master=now;
  }  
}

void JkRS485Sniffer::handle_bms2sniffer_switch_or_number_uint32_event(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::uint32_t value) {

  // 02.10.10.78.00.02.04.00.00.00.00.37.A9
  // 02.10.10.78.00.02.04.00.00.00.01.F6.69.

  // 02.10.10.78.00.02.04.00.00.00.00.37.A9

  // 02.10.10.00.00.02.04.00.00.0D.AC.35.C6 (13)

  // rs485_network_node[] has 16 slots (addresses 0x00 to 0x0F)
  if (slave_address > 15) {
    ESP_LOGE(TAG, "slave_address 0x%02X out of range (max 0x0F), ignoring", slave_address);
    return;
  }

  if (rs485_network_node[slave_address].available) {
    send_command_switch_or_number_to_slave_uint32(slave_address,third_element_of_frame,register_address,value);
  }

  if (this->broadcast_changes_to_all_bms_==true){
    for (uint8_t j = 1; j < 16; ++j) {
        if (rs485_network_node[j].available && slave_address!=j) {
            send_command_switch_or_number_to_slave_uint32(j,third_element_of_frame,register_address,value);
        }
    }
  }


  

}


void JkRS485Sniffer::handle_bms2sniffer_switch_or_number_int32_event(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::int32_t value) {

  // 02.10.10.78.00.02.04.00.00.00.00.37.A9
  // 02.10.10.78.00.02.04.00.00.00.01.F6.69.

  // 02.10.10.78.00.02.04.00.00.00.00.37.A9

  // rs485_network_node[] has 16 slots (addresses 0x00 to 0x0F)
  if (slave_address > 15) {
    ESP_LOGE(TAG, "slave_address 0x%02X out of range (max 0x0F), ignoring", slave_address);
    return;
  }

  if (rs485_network_node[slave_address].available) {
    send_command_switch_or_number_to_slave_int32(slave_address,third_element_of_frame,register_address,value);
  }

  if (this->broadcast_changes_to_all_bms_==true){
    for (uint8_t j = 1; j < 16; ++j) {
        if (rs485_network_node[j].available && slave_address!=j) {
            send_command_switch_or_number_to_slave_int32(j,third_element_of_frame,register_address,value);
        }
    }
  }


  

}

void JkRS485Sniffer::handle_bms2sniffer_switch_or_number_uint16_event(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::uint16_t value) {

  // rs485_network_node[] has 16 slots (addresses 0x00 to 0x0F)
  if (slave_address > 15) {
    ESP_LOGE(TAG, "slave_address 0x%02X out of range (max 0x0F), ignoring", slave_address);
    return;
  }

  if (rs485_network_node[slave_address].available) {
    send_command_switch_or_number_to_slave_uint16(slave_address,third_element_of_frame,register_address,value);
    rs485_network_node[slave_address].last_device_info_request_received_OK=0;
  }

  if (this->broadcast_changes_to_all_bms_==true){
    for (uint8_t j = 1; j < 16; ++j) {
        if (rs485_network_node[j].available && slave_address!=j) {
            send_command_switch_or_number_to_slave_uint16(j,third_element_of_frame,register_address,value);
            rs485_network_node[j].last_device_info_request_received_OK=0;
        }
    }
  }


  

}

// Sends a frame on the bus, driving the talk pin of the RS485 transceiver when there is one
void JkRS485Sniffer::send_frame_(const uint8_t *frame, size_t size, uint8_t address) {
  if (this->talk_pin_ != nullptr) { this->talk_pin_->digital_write(true); }
  delayMicroseconds(50);
  this->write_array(frame, size);
  this->flush();
  if (this->talk_pin_ != nullptr) { this->talk_pin_->digital_write(false); }
  delayMicroseconds(50);

  const uint32_t now=millis();
  this->rs485_network_node[address].last_request_sent=now;
  this->last_jk_rs485_network_activity_=now;
}

// Queues a write : it is sent by loop() once the bus is silent, instead of right away
// from the number or switch control() call, which could collide with the bus traffic
void JkRS485Sniffer::enqueue_write_(const uint8_t *frame, size_t size, uint8_t address) {
  if (size > sizeof(PendingWrite::frame) || address > 15) {
    ESP_LOGE(TAG, "Invalid write (address 0x%02X, %u bytes), ignoring", address, (unsigned) size);
    return;
  }
  if (this->write_queue_.size() >= WRITE_QUEUE_MAX_SIZE) {
    ESP_LOGW(TAG, "Write queue full (%u), write to address 0x%02X dropped", (unsigned) WRITE_QUEUE_MAX_SIZE, address);
    return;
  }

  PendingWrite pending;
  memcpy(pending.frame, frame, size);
  pending.size = size;
  pending.address = address;
  this->write_queue_.push_back(pending);
  ESP_LOGD(TAG, "Write to address 0x%02X queued (%u pending)", address, (unsigned) this->write_queue_.size());
}

// Sends the oldest pending pending. Returns true when a frame was sent
bool JkRS485Sniffer::send_next_write_(void) {
  while (!this->write_queue_.empty()) {
    const PendingWrite pending = this->write_queue_.front();
    this->write_queue_.pop_front();

    if (!this->rs485_network_node[pending.address].available) {
      ESP_LOGW(TAG, "Address 0x%02X no longer available, queued write dropped", pending.address);
      continue;
    }

    ESP_LOGD(TAG, "Sending queued write to address 0x%02X (%u left)", pending.address,
             (unsigned) this->write_queue_.size());
    this->send_frame_(pending.frame, pending.size, pending.address);
    return true;
  }
  return false;
}

void JkRS485Sniffer::send_command_switch_or_number_to_slave_uint32(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::uint32_t value) {
  uint8_t frame[13];
  uint8_t size=0;

  //[0x0000, 0x10,   0x04,  3,  0],
      // 02.10.10.78.00.02.04.00.00.00.00.37.A9
      // 02.10.10.78.00.02.04.00.00.00.01.F6.69.
      // 02.10.10.78.00.02.04.00.00.00.00.37.A9


  frame[0]  = slave_address ;                   // Slave Address
  frame[1]  = 0x10;                             // 
  frame[2]  = third_element_of_frame;           // 
  frame[3]  = (register_address & 0x00FF) >> 0;                 // Register address
  frame[4]  = 0x00;                             // 
  frame[5]  = 0x02;                             // 
  frame[6]  = 4;                                // Length of data in number of Bytes
  frame[7]  = (value & 0xFF000000) >> 24;       // Data Byte 1
  frame[8]  = (value & 0x00FF0000) >> 16;       // Data Byte 2
  frame[9]  = (value & 0x0000FF00) >> 8;        // Data Byte 3
  frame[10] = (value & 0x000000FF) >> 0;        // Data Byte 4
  size=13;

  uint16_t computed_checksum = crc16_c(frame, size-2);
  frame[11] = ((computed_checksum & 0xFF00)>>8);
  frame[12] = ((computed_checksum & 0x00FF)>>0);


  ESP_LOGD(TAG, "MESSAGE REQUEST TO SEND switch >>: %s",format_hex_pretty(frame, size).c_str());
  this->enqueue_write_(frame, size, slave_address);
}

void JkRS485Sniffer::send_command_switch_or_number_to_slave_uint16(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::uint16_t value) {
  uint8_t frame[13];
  uint8_t size=0;

  //02.10.11.14.00.01.   02.  02.10.   B0.19

  frame[0]  = slave_address ;                   // Slave Address
  frame[1]  = 0x10;                             // 
  frame[2]  = third_element_of_frame;                      // 
  frame[3]  = register_address;                 // Register address
  frame[4]  = 0x00;                             // 
  frame[5]  = 0x01;                             // 
  frame[6]  = 2;                                // Length of data in number of Bytes
  frame[7]  = (value & 0xFF00) >> 8;            // Data Byte 1
  frame[8]  = (value & 0x00FF) >> 0;            // Data Byte 2
  size=11; 

  uint16_t computed_checksum = crc16_c(frame, size-2);
  frame[9] = ((computed_checksum & 0xFF00)>>8);
  frame[10] = ((computed_checksum & 0x00FF)>>0);
     


  ESP_LOGD(TAG, "MESSAGE REQUEST TO SEND switch or number >>: %s",format_hex_pretty(frame, size).c_str());
  this->enqueue_write_(frame, size, slave_address);
}

void JkRS485Sniffer::send_command_switch_or_number_to_slave_int32(std::uint8_t slave_address, std::uint8_t third_element_of_frame, std::uint16_t register_address, std::int32_t value) {
  uint8_t frame[13];
  uint8_t size=0;

  frame[0]  = slave_address ;                   // Slave Address
  frame[1]  = 0x10;                             // 
  frame[2]  = third_element_of_frame;           // 
  frame[3]  = register_address;                 // Register address
  frame[4]  = 0x00;                             // 
  frame[5]  = 0x02;                             // 
  frame[6]  = 4;                                // Length of data in number of Bytes
  frame[7]  = (value & 0xFF000000) >> 24;       // Data Byte 1
  frame[8]  = (value & 0x00FF0000) >> 16;       // Data Byte 2
  frame[9]  = (value & 0x0000FF00) >> 8;        // Data Byte 3
  frame[10] = (value & 0x000000FF) >> 0;        // Data Byte 4
  size=13;

  uint16_t computed_checksum = crc16_c(frame, size-2);
  frame[11] = ((computed_checksum & 0xFF00)>>8);
  frame[12] = ((computed_checksum & 0x00FF)>>0);


  ESP_LOGD(TAG, "MESSAGE REQUEST TO SEND switch or number >>: %s",format_hex_pretty(frame, size).c_str());
  this->enqueue_write_(frame, size, slave_address);
}


void JkRS485Sniffer::send_request_to_slave(uint8_t address, uint8_t frame_type)
{
    uint8_t frame[11];
    frame[0] = address ;
    frame[1] = 0x10;
    frame[2] = 0x16;
    
    // frame[3]
    if (frame_type==1) frame[3] = 0x1E;
    else if (frame_type==2) frame[3] = 0x20;
    else if (frame_type==3) frame[3] = 0x1C;
    else return;

    frame[4] = 0x00; // holding register
    frame[5] = 0x01; // size of the value in byte
    frame[6] = 0x02; // command word: 0x01 (activation), 0x02 (write), 0x03 (read), 0x05 (password), 0x06 (read all)
    frame[7] = 0x00;
    frame[8] = 0x00;
    
    uint16_t computed_checksum = crc16_c(frame, 9);
    frame[9] = ((computed_checksum & 0xFF00)>>8);
    frame[10] = ((computed_checksum & 0x00FF)>>0);

    // Mode 2 : count the polls of an available node that got no frame since the previous poll.
    // The scan of unavailable nodes and the requests sent in Mode 1 are not counted
    if (this->act_as_master && address <= 15 && this->rs485_network_node[address].available) {
      struct_rs485_network_node &node = this->rs485_network_node[address];
      if (node.last_poll_sent != 0 && (int32_t) (node.last_message_received - node.last_poll_sent) < 0) {
        node.missed_requests++;
        if (node.missed_requests >= MAX_MISSED_REQUESTS) {
          ESP_LOGW(TAG, "No answer from address 0x%02X to %u consecutive requests, set as unavailable", address,
                   (unsigned) node.missed_requests);
          node.missed_requests = 0;
          this->set_node_availability(address, 0);
        }
      }
      node.last_poll_sent = millis();
    }

    ESP_LOGV(TAG, "MESSAGE REQUEST TO SEND>>: %s",format_hex_pretty(frame, 11).c_str());
    this->send_frame_(frame, sizeof(frame), address);
}

bool JkRS485Sniffer::calculate_next_pooling(void)
{
  //NORMAL POOLING MODE: SAME NODE
  bool found=false;

  const uint32_t now=millis();

  // PENDING INFO FROM ACTUAL ADDRESS NODE ?
  if (this->rs485_network_node[this->pooling_index.node_address].available && (now-this->rs485_network_node[this->pooling_index.node_address].last_request_sent)>TIME_BETWEEN_CONSECUTIVE_REQUEST_SENDINGS_TO_SAME_SLAVE)
  {
    if (now-this->rs485_network_node[pooling_index.node_address].last_device_info_request_received_OK>TIME_BETWEEN_DEVICE_INFO_REQUESTS_MILLISECONDS || this->rs485_network_node[pooling_index.node_address].last_device_info_request_received_OK==0)
    {
      this->pooling_index.frame_type=3; // DEVICE INFO
      found=true;
    }
    else
    {
      if (now-this->rs485_network_node[pooling_index.node_address].last_device_settings_request_received_OK>TIME_BETWEEN_DEVICE_SETTINGS_REQUESTS_MILLISECONDS || this->rs485_network_node[pooling_index.node_address].last_device_settings_request_received_OK==0)
      {
        this->pooling_index.frame_type=1; // DEVICE SETTINGS
        found=true;
      }
      else
      {
        if (now-this->rs485_network_node[pooling_index.node_address].last_cell_info_request_received_OK>TIME_BETWEEN_CELL_INFO_REQUESTS_MILLISECONDS ||this->rs485_network_node[pooling_index.node_address].last_cell_info_request_received_OK==0)
        {
          this->pooling_index.frame_type=2; //CELL INFO
          found=true;
        }
      }
    }
  }

  if (found==false)
  {
    // Try other address
    uint8_t found_index=0;
    for (uint8_t j = this->pooling_index.node_address+1; j < 16; ++j)
    {
      if (rs485_network_node[j].available && (now-this->rs485_network_node[j].last_request_sent)>TIME_BETWEEN_CONSECUTIVE_REQUEST_SENDINGS_TO_SAME_SLAVE)
      {
        if (now-this->rs485_network_node[j].last_device_info_request_received_OK>TIME_BETWEEN_DEVICE_INFO_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_device_info_request_received_OK==0)
        {
          this->pooling_index.frame_type=3; //DEVICE INFO
          found=true;
        }
        else
        {
          if (now-this->rs485_network_node[j].last_device_settings_request_received_OK>TIME_BETWEEN_DEVICE_SETTINGS_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_device_settings_request_received_OK==0)
          {
            this->pooling_index.frame_type=1; // DEVICE SETTINGS
            found=true;
          }
          else
          {
            if (now-this->rs485_network_node[j].last_cell_info_request_received_OK>TIME_BETWEEN_CELL_INFO_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_cell_info_request_received_OK==0)
            {
              this->pooling_index.frame_type=2; //CELL INFO
              found=true;
            }      
          }
        }
      }
      if (found==true)
      {
        found_index=j;
        break;
      }      
    }

    if (found==false)
    {
      for (uint8_t j = 1; j <= this->pooling_index.node_address; ++j)
      {
        if (rs485_network_node[j].available && (now-this->rs485_network_node[j].last_request_sent)>TIME_BETWEEN_CONSECUTIVE_REQUEST_SENDINGS_TO_SAME_SLAVE)
        {
          if (now-this->rs485_network_node[j].last_device_info_request_received_OK>TIME_BETWEEN_DEVICE_INFO_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_device_info_request_received_OK==0)
          {
            this->pooling_index.frame_type=3; // DEVICE INFO
            found=true;
          }
          else
          {
            if (now-this->rs485_network_node[j].last_device_settings_request_received_OK>TIME_BETWEEN_DEVICE_SETTINGS_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_device_settings_request_received_OK==0)
            {
              this->pooling_index.frame_type=1; // DEVICE SETTINGS
              found=true;
            }
            else
            {
              if (now-this->rs485_network_node[j].last_cell_info_request_received_OK>TIME_BETWEEN_CELL_INFO_REQUESTS_MILLISECONDS || this->rs485_network_node[j].last_cell_info_request_received_OK==0)
              {
                this->pooling_index.frame_type=2; // CELL INFO
                found=true;
              }    
            }
          }
        }
        if (found==true)
        {
          found_index=j;
          break;
        }
      }
    }

    if (found==true) this->pooling_index.node_address=found_index;
  }

  if (found==true)
  {
    const uint32_t now=millis();
    ESP_LOGI(TAG, "POOLING NEXT AVAILABLE... [address:0x%02X] @ %d [%lu,%lu,%lu]",
                  this->pooling_index.node_address,this->pooling_index.frame_type,
                  now-this->rs485_network_node[this->pooling_index.node_address].last_device_settings_request_received_OK,
                  now-this->rs485_network_node[this->pooling_index.node_address].last_cell_info_request_received_OK,
                  now-this->rs485_network_node[this->pooling_index.node_address].last_device_info_request_received_OK);
  }

  return(found);
}

int JkRS485Sniffer::found_next_node_to_discover(void)
{
  int found_index=-1;
  for (uint8_t j = this->pooling_index.scan_address+1; j < 16; ++j)
  {
      if (!rs485_network_node[j].available)
      {
          found_index=j;
          break;
      }
  }
  if (found_index==-1)
  {
    for (uint8_t j = 1; j <= this->pooling_index.scan_address; ++j)
    {
        if (!rs485_network_node[j].available)
        {
            found_index=j;
            break;
        }
    }
  }
  return(found_index);
}

void JkRS485Sniffer::loop() {
  uint32_t now = millis();

  if (this->rx_buffer_.size()>=RX_BUFFER_MAX_SIZE){
    ESP_LOGW(TAG, "### Buffer cleared buffer size: %d",this->rx_buffer_.size());
    this->rx_buffer_.clear();
  }

  if (this->available()){
    if ((now-this->last_jk_rs485_network_activity_)>MIN_SILENCE_MILLISECONDS){
      if (this->act_as_master==false){
        ESP_LOGD(TAG, "SILENCE: %f ms",(float)(now-this->last_jk_rs485_network_activity_));
      } else {
        ESP_LOGI(TAG, "SILENCE: %f ms",(float)(now-this->last_jk_rs485_network_activity_));
      }
    
    }

    //bulk to Received data to "rx_buffer_"
    uint8_t byte;
    while (this->available() && (this->rx_buffer_.size()<RX_BUFFER_MAX_SIZE)) {
      this->read_byte(&byte);
      this->rx_buffer_.push_back(byte);
    }
    now = millis();
    this->last_jk_rs485_network_activity_ = now; 

    //manage buffer
    uint8_t response=0;
    uint16_t original_buffer_size=rx_buffer_.size();
    uint8_t cont_manage=0;
    bool changed=true;
    ESP_LOGD(TAG, "..........................................");

    do {
        cont_manage++;
        ESP_LOGV(TAG, "Buffer before number %d:    %s",cont_manage,format_hex_pretty(&this->rx_buffer_.front(), this->rx_buffer_.size()).c_str());  
        response=this->manage_rx_buffer_();
        ESP_LOGV(TAG, "Response:            %d (%s)",response, rx_result_to_string(response));
        if (original_buffer_size==rx_buffer_.size()){
           changed=false;
        } else {
           changed=true;
           original_buffer_size=this->rx_buffer_.size();   
        }
    } while (cont_manage<5 && changed==true && original_buffer_size>=JKPB_RS485_MASTER_SHORT_REQUEST_SIZE);
    
    if (original_buffer_size==0){
      ESP_LOGV(TAG,     "Buffer empty");
    }
    
  } else {
    //NO RX DATA
    if ((now-this->last_jk_rs485_network_activity_)>MIN_SILENCE_NEEDED_BEFORE_SPEAKING_MILLISECONDS){
      //CAN TX REQUEST IF NEEDED

      // Pending writes go first, in both modes : one frame per silence, the polling
      // resumes once the queue is empty
      if (this->send_next_write_()) {
        return;
      }

      if (now-last_master_activity>SILENCE_BEFORE_ACTING_AS_MASTER){
        if (this->act_as_master==false){
          //NO MASTER HAS BEEN DETECTED IN THE NETWORK --> ESP WILL ACT AS MASTER
          this->act_as_master=true;
          this->set_node_availability(0,0);
          ESP_LOGI(TAG, "NO JK MASTER DETECTED IN THE NETWORK. ESP WILL ACT AS MASTER");
        }
      }
      
      if (this->act_as_master) {
        if (now-last_message_received_acting_as_master>SILENCE_BEFORE_REUSING_NETWORK_ACTING_AS_MASTER){
          this->last_message_received_acting_as_master=now;      
          
          bool scan_sent=false;
          //SCAN NEXT UNAVAILABLE NODE
          if (now-this->last_network_scan>TIME_BETWEEN_NETWORK_SCAN_MILLISECONDS){
            int found_index=-1;
            found_index=this->found_next_node_to_discover();

            if (found_index==-1){
              //all nodes are available now
              ESP_LOGD(TAG, "SCANNING TO DISCOVER...ALL NODES ARE AVAILABLE");
            } else {
              ESP_LOGD(TAG, "SCANNING TO DISCOVER...0x%02X [%s]",found_index,this->nodes_available_to_string().c_str());
              this->pooling_index.scan_address=found_index;
              this->send_request_to_slave(found_index,2);

              this->last_network_scan=now;
              scan_sent=true;
            }
          }
          
          if (scan_sent==false){
            if (this->nodes_available_number>0 && now-this->last_jk_rs485_pooling_trial_>TIME_BEFORE_NEXT_POOLING_MILLISENCONDS){
              this->last_jk_rs485_pooling_trial_=now;
              //NORMAL POOLING LOOP AS MASTER
              if (this->calculate_next_pooling()==true){
                this->send_request_to_slave(this->pooling_index.node_address,this->pooling_index.frame_type);

              }
            }
          }
        }

      } else {
        //SPEAK WHEN A MASTER IS IN THE NETWORK
        for (uint8_t cont=0;cont<16;cont++){
          if (this->rs485_network_node[cont].available==true){
            //repeat device info request
            if (now-rs485_network_node[cont].last_device_info_request_received_OK>TIME_BETWEEN_DEVICE_INFO_REQUESTS_MILLISECONDS){

              send_request_to_slave(cont,03);


              break;
            }

          }
        }

        //decide if node is available (if none info recieved during a time from that address)
        for (uint8_t cont=0;cont<16;cont++){
          if (now-this->rs485_network_node[cont].last_message_received>NO_MESSAGE_RECEIVED_TIME_SET_AS_UNAVAILABLE_MILLISECONDS){
            this->set_node_availability(cont,0);
          }

          //periodically test !!!!!!
          if (this->rs485_network_node[cont].available && cont>0){
            if (this->rs485_network_node[cont].counter_device_info_received==0){
              this->rs485_network_node[cont].last_device_info_request_received_OK=0;
            }                
          }
        }
      }
    }
  }
}

std::string JkRS485Sniffer::nodes_available_to_string() {
    std::string bufferHex;
    bufferHex.reserve(17); // Reserve space for the 16 characters
    uint8_t number_of_nodes_available=0;
    for (uint8_t cont = 0; cont < 16; cont++) {
        // Dump the buffer content into the string
        if (this->rs485_network_node[cont].available) {
            bufferHex.push_back('1');
            number_of_nodes_available++;
        } else {
            bufferHex.push_back('0');
        }
    }
    
    this->nodes_available_number=number_of_nodes_available;
    return bufferHex;
}

void JkRS485Sniffer::set_node_availability(uint8_t address,bool value){
  // rs485_network_node[] has 16 slots (addresses 0x00 to 0x0F)
  if (address > 15) {
    ESP_LOGE(TAG, "address 0x%02X out of range (max 0x0F), ignoring", address);
    return;
  }

  if (this->rs485_network_node[address].available==value){
    //no changes
  } else {
    uint8_t previous_value=this->rs485_network_node[address].available;
    this->rs485_network_node[address].available=value;

    std::string previous=this->nodes_available;
    this->nodes_available=this->nodes_available_to_string();
    ESP_LOGI(TAG, "NODES AVAILABLE CHANGED: address 0x%02X (%d->%d) [%s] --> [%s]",address,previous_value,value,previous.c_str(),this->nodes_available.c_str());
  }
}


void JkRS485Sniffer::detected_master_activity_now(void){
  const uint32_t now = millis();

  if (this->act_as_master){
    this->act_as_master=false;
    ESP_LOGI(TAG, "JK MASTER DETECTED IN THE NETWORK");
  }
  this->last_master_activity=now;
}

uint8_t JkRS485Sniffer::manage_rx_buffer_(void) {

  uint8_t address = 0;

  const uint32_t now = millis();
  const size_t response_size = JKPB_RS485_RESPONSE_SIZE;
  const size_t header_size = pattern_response_header.size();
  const size_t short_size = JKPB_RS485_MASTER_SHORT_REQUEST_SIZE;
  const size_t request_size = JKPB_RS485_MASTER_REQUEST_SIZE;
  uint8_t result = RX_NEED_MORE;

  if (this->rx_buffer_.empty()) {
    return(RX_NO_DATA);
  }

  auto erase_prefix = [&](size_t count) {
    if (count == 0) {
      return;
    }
    if (count >= this->rx_buffer_.size()) {
      this->rx_buffer_.clear();
      return;
    }
    this->rx_buffer_.erase(this->rx_buffer_.begin(), this->rx_buffer_.begin() + count);
  };

  // The three try_parse_* lambdas below only modify rx_buffer_ when they return true,
  // and manage_rx_buffer_() then returns at once : one preamble search is enough for all of them
  auto preamble_it = std::search(this->rx_buffer_.begin(), this->rx_buffer_.end(),
                                 pattern_response_header.begin(), pattern_response_header.end());
  const bool preamble_found = (preamble_it != this->rx_buffer_.end());
  const size_t preamble_index =
      preamble_found ? std::distance(this->rx_buffer_.begin(), preamble_it) : this->rx_buffer_.size();

  // Short Modbus frame (8 bytes : address, function, register, quantity, CRC), e.g. the
  // answer of a BMS to a write. It can be followed by a response in the same buffer
  auto try_parse_short_request = [&]() -> bool {
    if (this->rx_buffer_.size() < short_size) {
      return false;
    }
    if (preamble_found && preamble_index < short_size) {
      return false;
    }

    const uint8_t *raw = this->rx_buffer_.data();
    uint16_t computed_checksum = crc16_c(raw, 6);
    uint16_t remote_checksum = ((uint16_t(raw[6]) << 8) | (uint16_t(raw[7]) << 0));
    if (computed_checksum != remote_checksum) {
      this->rx_short_checksum_fail_++;
      ESP_LOGV(TAG, "CHECKSUM failed! 0x%04X != 0x%04X (short_fail=%u)", computed_checksum,
               remote_checksum, (unsigned) this->rx_short_checksum_fail_);
      result = RX_CHECKSUM_FAIL;
      return false;
    }

    address = raw[0];
    ESP_LOGD(TAG, "Answer received for MASTER (type: SHORT REQUEST for address %02X, %u bytes)", address,
             (unsigned) short_size);
    erase_prefix(short_size);
    result = RX_PARSED_SHORT;
    return true;
  };

  // Request of a real master (11 bytes, no preamble)
  auto try_parse_request = [&]() -> bool {
    if (this->rx_buffer_.size() < request_size) {
      return false;
    }
    const bool try_with_master_request_size = !preamble_found || (preamble_index >= request_size);

    if (!try_with_master_request_size) {
      return false;
    }

    const uint8_t *raw = this->rx_buffer_.data();
    uint16_t computed_checksum = crc16_c(raw, 9);
    uint16_t remote_checksum = ((uint16_t(raw[9]) << 8) | (uint16_t(raw[10]) << 0));
    if (computed_checksum != remote_checksum) {
      this->rx_request_checksum_fail_++;
      ESP_LOGV(TAG, "CHECKSUM failed! 0x%04X != 0x%04X (req_fail=%u)", computed_checksum,
               remote_checksum, (unsigned) this->rx_request_checksum_fail_);
      result = RX_CHECKSUM_FAIL;
      return false;
    }

    address = raw[0];
    ESP_LOGI(TAG, "REAL master is speaking to address 0x%02X (request)", address);
    this->rs485_network_node[0].last_message_received = now;
    this->detected_master_activity_now();
    this->set_node_availability(0, 1);
    erase_prefix(request_size);
    result = RX_PARSED_REQUEST;
    return true;
  };

  // Response of a slave (308 bytes, starting with the 0x55AAEB90 preamble)
  auto try_parse_response = [&]() -> bool {
    if (this->rx_buffer_.size() < response_size) {
      return false;
    }

    if (preamble_found) {
      if (preamble_index > 0) {
        erase_prefix(preamble_index);
        this->rx_preamble_drop_++;
        result = RX_RESYNC;
      }
      if (this->rx_buffer_.size() < response_size) {
        return true;
      }
    } else {
      // A full frame worth of bytes without any preamble : drop them, but keep the last
      // (header_size - 1) bytes in case they are the start of a preamble split across UART reads
      size_t keep = (header_size > 0) ? header_size - 1 : 0;
      if (this->rx_buffer_.size() > keep) {
        erase_prefix(this->rx_buffer_.size() - keep);
      }
      this->rx_preamble_drop_++;
      ESP_LOGV(TAG, "No preamble found, dropping buffer (preamble_drop=%u)", (unsigned) this->rx_preamble_drop_);
      result = RX_RESYNC;
      return true;
    }

    const uint8_t *raw = this->rx_buffer_.data();
    uint8_t computed_checksum = chksum(raw, JKPB_RS485_NUMBER_OF_ELEMENTS_TO_COMPUTE_CHECKSUM);
    uint8_t remote_checksum = raw[JKPB_RS485_CHECKSUM_INDEX];

    if (raw[JKPB_RS485_FRAME_TYPE_ADDRESS]==1) {
      address = raw[JKPB_RS485_FRAME_TYPE_ADDRESS_FOR_FRAME_TYPE_x01 + 6];
    } else {
      address = raw[JKPB_RS485_ADDRESS_OF_RS485_ADDRESS];
    }

    if (computed_checksum != remote_checksum) {
      this->rx_response_checksum_fail_++;
      ESP_LOGW(TAG, "CHECKSUM failed! 0x%02X != 0x%02X (resp_fail=%u)", computed_checksum,
               remote_checksum, (unsigned) this->rx_response_checksum_fail_);
      // The search starts at begin()+1, so index_next is never 0. Without a next preamble it equals
      // size(), and erase_prefix() then clears the buffer
      auto it_next = std::search(this->rx_buffer_.begin() + 1, this->rx_buffer_.end(),
                                 pattern_response_header.begin(), pattern_response_header.end());
      size_t index_next = std::distance(this->rx_buffer_.begin(), it_next);
      erase_prefix(index_next);
      result = RX_CHECKSUM_FAIL;
      return true;
    }

    // The address is a raw byte from the wire, only range-checked below
    if (address <= 15) {
      this->rs485_network_node[address].last_message_received = now;
      this->rs485_network_node[address].missed_requests = 0;
    }
    if (address == 0) {
      last_master_activity = now;
    } else if (address > 15) {
      ESP_LOGV(TAG, "(this->rx_buffer_.size():%03u) [address 0x%02X] Frame Type 0x%02X | CHECKSUM is correct",
               (unsigned) this->rx_buffer_.size(), address, raw[JKPB_RS485_FRAME_TYPE_ADDRESS]);
      this->rx_buffer_.clear();
      result = RX_PARSED_RESPONSE;
      return true;
    } else {
      this->set_node_availability(address, 1);
    }

    std::vector<uint8_t> data(this->rx_buffer_.begin(), this->rx_buffer_.begin() + response_size);
    this->rx_frames_ok_++;
    ESP_LOGD(TAG, "Frame received from SLAVE (type: 0x%02X, %u bytes) %02X address", raw[4], (unsigned) data.size(), address);
    ESP_LOGVV(TAG, "  %s", format_hex_pretty(&data.front(), data.size()).c_str());

    bool found = false;
    for (auto *device : this->devices_) {
      device->on_jk_rs485_sniffer_data(address, raw[JKPB_RS485_FRAME_TYPE_ADDRESS], data, this->nodes_available);
      found = true;
    }

    if (!found) {
      ESP_LOGW(TAG, "Got JkRS485 but no recipients to send [frame type:0x%02X] 0x%02X!",
               raw[JKPB_RS485_FRAME_TYPE_ADDRESS], address);
    }

    erase_prefix(response_size);
    result = RX_PARSED_RESPONSE;
    return true;
  };

  if (try_parse_short_request()) {
    return(result);
  }
  if (try_parse_request()) {
    return(result);
  }
  if (try_parse_response()) {
    return(result);
  }

  return(result);
}

void JkRS485Sniffer::dump_config()
{
  ESP_LOGCONFIG(TAG, "JkRS485Sniffer:");
  ESP_LOGCONFIG(TAG, "  RX timeout: %d ms", this->rx_timeout_);
  ESP_LOGCONFIG(TAG, "  RX frames OK: %u", (unsigned) this->rx_frames_ok_);
  ESP_LOGCONFIG(TAG, "  RX preamble drops: %u", (unsigned) this->rx_preamble_drop_);
  ESP_LOGCONFIG(TAG, "  RX checksum failures: short=%u request=%u response=%u",
                (unsigned) this->rx_short_checksum_fail_, (unsigned) this->rx_request_checksum_fail_,
                (unsigned) this->rx_response_checksum_fail_);
}
float JkRS485Sniffer::get_setup_priority() const {
  // After UART bus
  return setup_priority::BUS - 1.0f;
}

}  // namespace jk_rs485_sniffer
}  // namespace esphome
