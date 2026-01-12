#include "sem_meter.h"
#include "esphome/core/log.h"

namespace esphome {
namespace sem_meter {

static const char *TAG = "sem_meter";

void SEMMeter::setup() {
  ESP_LOGI(TAG, "Инициализация счетчика СЭБ/МАЯК...");
  ESP_LOGI(TAG, "Адрес: %d, Пароль: %s", address_, password_.c_str());
  ESP_LOGI(TAG, "Количество сенсоров: %d", sensors_.size());
}

void SEMMeter::update() {
  if (sensors_.empty()) {
    ESP_LOGW(TAG, "Нет сенсоров для опроса");
    return;
  }
  
  // Опрашиваем каждый сенсор
  for (auto &meter_sensor : sensors_) {
    ESP_LOGD(TAG, "Опрос сенсора: %s, команда: %s", 
            meter_sensor.sensor->get_name().c_str(),
            meter_sensor.command.c_str());
    
    std::string command = build_command(meter_sensor.command);
    
    ESP_LOGD(TAG, "Отправка: %s", 
            format_hex_pretty(std::vector<uint8_t>(command.begin(), command.end())).c_str());
    
    if (!send_command(command)) {
      ESP_LOGW(TAG, "Ошибка отправки команды");
      meter_sensor.sensor->publish_state(NAN);
      continue;
    }
    
    std::string response = receive_response();
    if (response.empty()) {
      ESP_LOGW(TAG, "Нет ответа от счетчика");
      meter_sensor.sensor->publish_state(NAN);
      continue;
    }
    
    ESP_LOGD(TAG, "Получен ответ: %s", 
            format_hex_pretty(std::vector<uint8_t>(response.begin(), response.end())).c_str());
    
    float value = parse_response(response, meter_sensor.command);
    if (!isnan(value)) {
      meter_sensor.sensor->publish_state(value);
      ESP_LOGD(TAG, "%s: %.3f", meter_sensor.sensor->get_name().c_str(), value);
    } else {
      ESP_LOGW(TAG, "Ошибка парсинга ответа");
      meter_sensor.sensor->publish_state(NAN);
    }
    
    // Пауза между командами
    delay(100);
  }
}

void SEMMeter::add_sensor(sensor::Sensor *sensor, const std::string &command) {
  MeterSensor meter_sensor;
  meter_sensor.sensor = sensor;
  meter_sensor.command = command;
  sensors_.push_back(meter_sensor);
  
  ESP_LOGI(TAG, "Добавлен сенсор: %s, команда: %s", 
          sensor->get_name().c_str(), command.c_str());
}

std::string SEMMeter::build_command(const std::string &cmd_code) {
  char addr_str[4];
  snprintf(addr_str, sizeof(addr_str), "%03d", address_);
  
  std::string command = "#";
  command += addr_str;
  command += password_;
  command += cmd_code;
  
  std::string crc = calculate_crc(command);
  command += crc;
  command += "\r";
  
  return command;
}

std::string SEMMeter::calculate_crc(const std::string &data) {
  uint8_t crc = 0;
  for (char c : data) {
    crc += static_cast<uint8_t>(c);
  }
  
  char crc_str[3];
  snprintf(crc_str, sizeof(crc_str), "%02X", crc);
  return std::string(crc_str);
}

bool SEMMeter::send_command(const std::string &command) {
  // Отправляем команду
  this->write_array(std::vector<uint8_t>(command.begin(), command.end()));
  this->flush();
  
  delay(50);
  return true;
}

std::string SEMMeter::receive_response(uint32_t timeout) {
  std::string response;
  uint32_t start_time = millis();
  uint8_t byte;
  
  // Ждем начало ответа '~'
  while (millis() - start_time < timeout) {
    if (this->available() > 0) {
      if (this->read_byte(&byte)) {
        if (byte == '~') {
          response += static_cast<char>(byte);
          break;
        }
      }
    }
    delay(1);
  }
  
  if (response.empty()) {
    ESP_LOGD(TAG, "Таймаут ожидания '~'");
    return "";
  }
  
  // Читаем остаток
  start_time = millis();
  while (millis() - start_time < timeout) {
    if (this->available() > 0) {
      if (this->read_byte(&byte)) {
        response += static_cast<char>(byte);
        
        if (byte == '\r') {
          ESP_LOGD(TAG, "Полный ответ получен, длина: %d", response.length());
          return response;
        }
        
        if (response.length() > 100) {
          ESP_LOGW(TAG, "Слишком длинный ответ");
          return "";
        }
      }
    }
    delay(1);
  }
  
  ESP_LOGW(TAG, "Таймаут чтения ответа, получено: %d байт", response.length());
  return response;
}

float SEMMeter::parse_response(const std::string &response, const std::string &command) {
 /* if (response.length() < 8) {
    ESP_LOGD(TAG, "Слишком короткий ответ: %d байт", response.length());
    return NAN;
  }*/
  
  // Проверяем, что ответ начинается с '~'
  if (response[0] != '~') {
    ESP_LOGD(TAG, "Ответ не начинается с '~'");
    return NAN;
  }
  
  // Проверяем адрес (позиции 1-3)
  std::string response_addr = response.substr(1, 3);
  char addr_str[4];
  snprintf(addr_str, sizeof(addr_str), "%03d", address_);
  if (response_addr != addr_str) {
    ESP_LOGD(TAG, "Неверный адрес в ответе: %s, ожидался: %s", 
            response_addr.c_str(), addr_str);
    return NAN;
  }
  
  // Проверяем команду (позиция 4)
  if (response.length() > 4 && response[4] != command[0]) {
    ESP_LOGD(TAG, "Неверная команда в ответе: %c, ожидалась: %c", 
            response[4], command[0]);
    return NAN;
  }
  
  // Парсим в зависимости от команды
  if (command == "E" || command == "W" || command == "V" || command == "U") {
    // Энергия T1-T4 - формат: ~001E12345678CRC
    if (response.length() >= 13) {
      std::string energy_str = response.substr(5, 8);
      char *endptr;
      long value = strtol(energy_str.c_str(), &endptr, 10);
      if (endptr == energy_str.c_str() || *endptr != '\0') {
        return NAN;
      }
      return value / 1000.0f; // Вт·ч → кВт·ч
    }
  } else if (command == "=M") {
    // Мощность - формат: ~001M1234CRC
    if (response.length() >= 9) {
      std::string power_str = response.substr(5, 4);
      char *endptr;
      long value = strtol(power_str.c_str(), &endptr, 10);
      if (endptr == power_str.c_str() || *endptr != '\0') {
        return NAN;
      }
      return value * 10.0f; // десятки Вт → Вт
    }
  } else if (command == "D") {
      std::string datetime_str = response.substr(5, 13);
    ESP_LOGD(TAG, "Строка даты/времени: %s", datetime_str.c_str());
    // 7. Проверяем длину
    if (datetime_str.length() != 13) {
        ESP_LOGD(TAG, "Неверная длина даты/времени: %d", datetime_str.length());
        return NAN;
    }
    // 8. Проверяем что все символы - цифры
    for (char c : datetime_str) {
        if (c < '0' || c > '9') {
            ESP_LOGD(TAG, "Неверный символ в данных времени: '%c'", c);
            return NAN;
        }
    }
    
    // 9. Парсим компоненты
    char day_week_char = datetime_str[0];
    std::string time_str = datetime_str.substr(1, 6);  // HHMMSS
    std::string date_str = datetime_str.substr(7, 6);  // DDMMYY
    
    int day_week = day_week_char - '0'; // 0-воскресенье, 1-понедельник...
    int hour = std::stoi(time_str.substr(0, 2));
    int minute = std::stoi(time_str.substr(2, 2));
    int second = std::stoi(time_str.substr(4, 2));
    int day = std::stoi(date_str.substr(0, 2));
    int month = std::stoi(date_str.substr(2, 2));
    int year = 2000 + std::stoi(date_str.substr(4, 2)); // 20XX
    // 10. Валидация значений
    if (day_week < 0 || day_week > 6 ||
        hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 ||
        second < 0 || second > 59 ||
        day < 1 || day > 31 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2099) {
        ESP_LOGW(TAG, "Некорректные значения даты/времени");
        return NAN;
    }
    // 11. Логируем результат
    const char* days_of_week[] = {"воскресенье", "понедельник", "вторник", 
                                  "среда", "четверг", "пятница", "суббота"};
    
    ESP_LOGI(TAG, "✓ ВРЕМЯ СЧЕТЧИКА:");
    ESP_LOGI(TAG, "  День недели: %d (%s)", day_week, 
            day_week < 7 ? days_of_week[day_week] : "неизвестно");
    ESP_LOGI(TAG, "  Время: %02d:%02d:%02d", hour, minute, second);
    ESP_LOGI(TAG, "  Дата: %02d.%02d.%04d", day, month, year);
    /* Время - возвращаем 0 как признак успеха
    return 0.0f;*/
    return hour;
  }
  
  return NAN;
}

void SEMMeter::dump_config() {
  ESP_LOGI(TAG, "Счетчик СЭБ/МАЯК:");
  ESP_LOGI(TAG, "  Адрес: %d", address_);
  ESP_LOGI(TAG, "  Пароль: %s", password_.c_str());
  ESP_LOGI(TAG, "  Количество сенсоров: %d", sensors_.size());
}

}  // namespace sem_meter
}  // namespace esphome