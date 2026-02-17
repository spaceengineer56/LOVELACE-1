// =============================
//   BC Lovelace-1
//   Version 1.12 (Valid)
// =============================

uint32_t ltime;       // Таймер для периодической отправки SMS
uint32_t soundtime;   // Таймер для переключения частоты звукового сигнала

#include <Wire.h>            // Работа с I2C
#include <Adafruit_BMP280.h> // Барометр BMP280
#include <TinyGPS.h>         // Обработка данных GPS
#include <SoftwareSerial.h>  // Программный UART
#include <SPI.h>             
#include <SD.h>              // Работа с SD-картой

Adafruit_BMP280 bmp;         // Объект барометра

float altitude_total;        // Текущая высота по BMP
const int PIN_CHIP_SELECT PROGMEM = 4; // CS пин SD-карты
float max_alt = -9999;       // Максимально достигнутая высота
int flag_sound = 0;          // Флаг переключения частоты сигнала
int count = 0;

TinyGPS gps;                 
SoftwareSerial ss(7, 8);     // GPS модуль: RX, TX
SoftwareSerial SIM800(3, 2); // SIM800L: RX Arduino, TX Arduino

#pragma pack(push,1)         // Упаковка структуры без выравнивания
struct TData{
  float kadr;  // Номер кадра (счетчик циклов)
  float temp;  // Температура
  float pres;  // Давление
  float att;   // Дополнительный параметр (зарезервировано)
};
TData Data;                  // Экземпляр структуры
#pragma pack(pop)

float flat, flon;            // Широта и долгота

// =============================
//            SETUP
// =============================
void setup() {
  Wire.begin();

  pinMode(9, OUTPUT);   // Пьезоизлучатель
  pinMode(10, OUTPUT);  // Система раскрытия парашюта
  pinMode(4, OUTPUT);   // CS пин SD-карты

  Serial.begin(9600);
  SIM800.begin(9600);
  ss.begin(9600);

  // Инициализация BMP280
  bmp.begin();
  bmp.setSampling(
      Adafruit_BMP280::MODE_NORMAL,
      Adafruit_BMP280::SAMPLING_X2,
      Adafruit_BMP280::SAMPLING_X16,
      Adafruit_BMP280::FILTER_X16,
      Adafruit_BMP280::STANDBY_MS_1
  );

  // Инициализация SD-карты
  SD.begin(4);
}

// =============================
//             LOOP
// =============================
void loop() {

  // Получение текущей высоты по барометру
  float altitude_total = bmp.readAltitude(1013.25);

  // Обновление максимальной высоты
  max_alt = max(altitude_total, max_alt);

  // Если снижение больше 7 метров — считаем, что начался спуск
  if (max_alt - altitude_total > 7) {
    descent();
  }

  bool newData = false;
  unsigned long chars;

  // Чтение GPS-данных (кратковременное окно опроса)
  for (unsigned long start = millis(); millis() - start < 1;) {
    while (ss.available()) {
      char c = ss.read();
      if (gps.encode(c))
        newData = true;
    }
  }

  // Если получены валидные GPS-данные — обновляем координаты
  if (newData) {    
    unsigned long age;
    gps.f_get_position(&flat, &flon, &age);
  }

  // Открытие файла на SD-карте
  File dataFile = SD.open("data.txt", FILE_WRITE);

  // Увеличение номера кадра
  Data.kadr++;

  // Запись телеметрии в CSV-формате:
  // кадр, время, температура, давление,
  // широта, долгота, высота GPS, высота BMP
  dataFile.print(Data.kadr);
  dataFile.print(","); dataFile.print(millis());
  dataFile.print(","); dataFile.print(bmp.readTemperature());
  dataFile.print(","); dataFile.print(bmp.readPressure());
  dataFile.print(","); dataFile.print(flat,6);
  dataFile.print(","); dataFile.print(flon,6);
  dataFile.print(","); dataFile.print(gps.f_altitude(),4);
  dataFile.print(","); dataFile.print(bmp.readAltitude(1013.25),4);
  dataFile.println();

  dataFile.close(); // Закрытие файла
}

// =============================
//    Звуковой сигнал посадки
// =============================
void ls_sygnal(){

  // Переключение частоты каждые ~618 мс
  if (millis() - soundtime >= 618) {
    soundtime = millis();

    if (flag_sound == 0){   
      tone(9, 2080);  // Первая частота
      flag_sound = 1;
    }
    else{
      tone(9, 1780);  // Вторая частота
      flag_sound = 0;
    }
  }
}

// =============================
//     Отправка координат по SMS
// =============================
void send_sms(){

  SIM800.println("AT+CMGF=1");       // Текстовый режим SMS
  delay(100);

  SIM800.println("AT+CMGS=\"+77777777777\""); // Номер заменить перед полетом
  delay(100);

  // Передача координат и максимальной высоты
  SIM800.print(flat, 8);
  SIM800.print(", ");
  SIM800.print(flon, 8);
  SIM800.print(" / ");
  SIM800.print(max_alt);

  delay(100);
  SIM800.write(26); // — отправка сообщения

  Serial.println(1);
}

// =============================
//        Режим спуска
// =============================
void descent(){

  // Активация системы раскрытия парашюта
  digitalWrite(10, HIGH);

  // Отправка SMS каждые 15 секунд
  if (millis() - ltime >= 15000) {
    ltime = millis();
    send_sms();
  }

  // Звуковое оповещение
  ls_sygnal();
}
