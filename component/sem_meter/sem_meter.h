#pragma once

#include "esphome.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>

namespace esphome {
namespace sem_meter {

class SEMMeter : public PollingComponent, public uart::UARTDevice {
public:
SEMMeter() : PollingComponent(30000) {}

void setup() override;
void update() override;
void dump_config() override;

void set_address(uint16_t address) { address_ = address; }
void set_password(const std::string &password) { password_ = password; }

  // Метод для добавления сенсоров (будет вызываться из YAML)
void add_sensor(sensor::Sensor *sensor, const std::string &command);

private:
struct MeterSensor {
    sensor::Sensor *sensor;
    std::string command;
};

uint16_t address_{1};
std::string password_{"00000"};
std::vector<MeterSensor> sensors_;

std::string build_command(const std::string &cmd_code);
std::string calculate_crc(const std::string &data);
bool send_command(const std::string &command);
std::string receive_response(uint32_t timeout = 1000);
float parse_response(const std::string &response, const std::string &command);
};

}  // namespace sem_meter
}  // namespace esphome