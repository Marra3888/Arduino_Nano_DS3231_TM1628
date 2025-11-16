#include <Wire.h>
#include <RTClib.h>
#include "TM1628.h"

// TM1628: DIO=D9, CLK=D8, STB=D7
TM1628 dvdLED(9, 8, 7, 5);
RTC_DS3231 rtc;

// -------- ШРИФТ (ваша раскладка) --------
const uint8_t PROGMEM NUMBER_FONT[] = {
  0b01111110, // 0
  0b01010000, // 1
  0b10111100, // 2
  0b11110100, // 3
  0b11010010, // 4
  0b11100110, // 5
  0b11101110, // 6
  0b01010100, // 7
  0b11111110, // 8
  0b11110110, // 9
  0b11011110, // A
  0b11101010, // b
  0b00101110, // C
  0b11111000, // d
  0b10101110, // E
  0b10001110  // F
};
inline uint8_t segPattern(uint8_t d) { return pgm_read_byte(&NUMBER_FONT[d & 0x0F]); }

// -------- МАСКИ СЕГМЕНТОВ --------
const uint8_t SEG_A = (1 << 2);
const uint8_t SEG_B = (1 << 4);
const uint8_t SEG_C = (1 << 6);
const uint8_t SEG_D = (1 << 5);
const uint8_t SEG_E = (1 << 3);
const uint8_t SEG_F = (1 << 1);
const uint8_t SEG_G = (1 << 7);
inline uint8_t withDP(uint8_t p) { return (uint8_t)(p | 0x01); } // bit0=DP

// Индексы (без конфликтов с avr/io.h)
enum { IDX_A=0, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, IDX_G };
const uint8_t SEG_MASK_BY_IDX[7] = { SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F, SEG_G };

// -------- МАРШРУТЫ ЦИФР (как просили) --------
const uint8_t PROGMEM DIGIT_ROUTE[10][7] = {
  { IDX_A, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, 0xFF },        // 0: a b c d e f
  { IDX_B, IDX_C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },            // 1: b c
  { IDX_A, IDX_B, IDX_G, IDX_E, IDX_D, 0xFF, 0xFF },         // 2: a b g e d
  { IDX_A, IDX_B, IDX_G, IDX_C, IDX_D, 0xFF, 0xFF },         // 3: a b g c d
  { IDX_F, IDX_G, IDX_B, IDX_C, 0xFF, 0xFF, 0xFF },          // 4: f g b c
  { IDX_A, IDX_F, IDX_G, IDX_C, IDX_D, 0xFF, 0xFF },         // 5: a f g c d
  { IDX_A, IDX_F, IDX_G, IDX_C, IDX_D, IDX_E, 0xFF },        // 6: a f g c d e
  { IDX_A, IDX_B, IDX_C, 0xFF, 0xFF, 0xFF, 0xFF },           // 7: a b c
  { IDX_A, IDX_B, IDX_C, IDX_D, IDX_E, IDX_F, IDX_G },       // 8: a b c d e f g
  { IDX_D, IDX_C, IDX_B, IDX_A, IDX_F, IDX_G, 0xFF }         // 9: d c b a f g
};

// -------- УТИЛИТЫ ВЫВОДА --------
void setColon(bool on) { dvdLED.setSegments(on ? (1 << 3) : 0, 4); } // pos4, bit3
void blankAllDigits()  { for (uint8_t p=0; p<4; ++p) dvdLED.setSegments(0, p); }
void drawDigits(uint8_t hh, uint8_t mm) {
  dvdLED.setSegments(segPattern((hh / 10) % 10), 0);
  dvdLED.setSegments(segPattern(hh % 10),        1);
  dvdLED.setSegments(segPattern((mm / 10) % 10), 2);
  dvdLED.setSegments(segPattern(mm % 10),        3);
}

// Прокрутка a..f только на одной позиции (2 цикла; анимация для старта)
void sweepPosition(uint8_t pos, uint8_t cycles=2, uint16_t stepMs=110) {
  static const uint8_t ORDER6[6] = { SEG_A, SEG_B, SEG_C, SEG_D, SEG_E, SEG_F };
  for (uint8_t k=0; k<cycles; ++k)
    for (uint8_t i=0; i<6; ++i) { dvdLED.setSegments(ORDER6[i], pos); delay(stepMs); }
}

// Анимация одной цифры по маршруту
void animateDigitByRoute(uint8_t pos, uint8_t digit, uint16_t stepMs=110) {
  uint8_t finalPat = segPattern(digit), acc = 0;
  for (uint8_t i=0; i<7; ++i) {
    uint8_t idx = pgm_read_byte(&DIGIT_ROUTE[digit][i]);
    if (idx == 0xFF) break;
    uint8_t mask = SEG_MASK_BY_IDX[idx];
    if (finalPat & mask) { acc |= mask; dvdLED.setSegments(acc, pos); delay(stepMs); }
  }
  dvdLED.setSegments(finalPat, pos);
}

// Построение времени по позициям: sweep (2× a..f) + «сборка» каждой цифры
void stagedTimePerPosition(uint8_t hh, uint8_t mm, uint16_t stepMs=110) {
  uint8_t d[4] = { (uint8_t)((hh/10)%10), (uint8_t)(hh%10), (uint8_t)((mm/10)%10), (uint8_t)(mm%10) };
  blankAllDigits();
  for (uint8_t pos=0; pos<4; ++pos) { sweepPosition(pos, 2, stepMs); animateDigitByRoute(pos, d[pos], stepMs); }
}

// -------- ПОКАЗ ДАТЫ И ТЕМПЕРАТУРЫ --------
void showDate(uint8_t day, uint8_t mon) {
  // Лидирующие нули показываем (DD:MM)
  setColon(true); // как разделитель даты
  dvdLED.setSegments(segPattern((day/10)%10), 0);
  dvdLED.setSegments(segPattern(day%10),      1);
  dvdLED.setSegments(segPattern((mon/10)%10), 2);
  dvdLED.setSegments(segPattern(mon%10),      3);
}

void showTempC(float tC) {
  setColon(false);
  // Режимы отображения:
  //  >=0:
  //    intPart >= 10  -> "xx.x"
  //    intPart <  10  -> " x.x"
  //  <0:
  //    |t| < 10       -> "-x.x"
  //    |t| >= 10      -> "-xx " (без десятичной, чтобы влез знак)
  int t10 = (int)roundf(tC * 10.0f);
  bool neg = (t10 < 0);
  if (neg) t10 = -t10;
  int intPart = t10 / 10;
  int frac    = t10 % 10;

  if (!neg) {
    if (intPart >= 10) {
      // xx.x
      dvdLED.setSegments(segPattern((intPart/10)%10), 0);
      dvdLED.setSegments(withDP(segPattern(intPart%10)), 1); // DP на позиции 1
      dvdLED.setSegments(segPattern(frac), 2);
      dvdLED.setSegments(0, 3);
    } else {
      //  x.x
      dvdLED.setSegments(0, 0);
      dvdLED.setSegments(withDP(segPattern(intPart)), 1);
      dvdLED.setSegments(segPattern(frac), 2);
      dvdLED.setSegments(0, 3);
    }
  } else {
    if (intPart < 10) {
      // -x.x
      dvdLED.setSegments(SEG_G, 0); // минус
      dvdLED.setSegments(withDP(segPattern(intPart)), 1);
      dvdLED.setSegments(segPattern(frac), 2);
      dvdLED.setSegments(0, 3);
    } else {
      // -xx (без десятичной)
      dvdLED.setSegments(SEG_G, 0); // минус
      dvdLED.setSegments(segPattern((intPart/10)%10), 1);
      dvdLED.setSegments(segPattern(intPart%10),      2);
      dvdLED.setSegments(0, 3);
    }
  }
}

// --------- РОТАЦИЯ: ВРЕМЯ → ДАТА → ТЕМП ---------
enum Mode : uint8_t { MODE_TIME=0, MODE_DATE=1, MODE_TEMP=2 };
Mode mode = MODE_TIME;

const uint32_t T_TIME_MS = 10000; // 10 c
const uint32_t T_DATE_MS = 4000;  // 4 c
const uint32_t T_TEMP_MS = 4000;  // 4 c
uint32_t modeT0 = 0;

void nextMode() {
  mode = (Mode)((mode + 1) % 3);
  modeT0 = millis();
  // Мгновенный вывод при переключении
  DateTime now = rtc.now();
  switch (mode) {
    case MODE_TIME: drawDigits(now.hour(), now.minute()); break;
    case MODE_DATE: showDate(now.day(), now.month());     break;
    case MODE_TEMP: showTempC(rtc.getTemperature());      break;
  }
}

#define ADJUST_AT_BOOT 0

void setup() {
  Wire.begin();

  setColon(false);
  blankAllDigits();

  if (!rtc.begin()) { drawDigits(88,88); setColon(true); while(1){} }
#if ADJUST_AT_BOOT
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
#endif

  // Стартовая анимация: по позициям
  DateTime t = rtc.now();
  stagedTimePerPosition(t.hour(), t.minute(), 110);

  mode = MODE_TIME;
  modeT0 = millis();
}

void loop() {
  static uint32_t t250 = 0;      // 250 мс тик
  static bool colon = false;
  static uint8_t prevH = 255, prevM = 255;

  uint32_t ms = millis();
  // Переключение режимов по таймерам
  uint32_t dwell =
    (mode==MODE_TIME) ? T_TIME_MS :
    (mode==MODE_DATE) ? T_DATE_MS : T_TEMP_MS;

  if (ms - modeT0 >= dwell) {
    nextMode();
  }

  // Периодический тик
  if (ms - t250 >= 250) {
    t250 += 250;

    DateTime now = rtc.now();

    // Время/Дата/Температура — обновление содержимого
    switch (mode) {
      case MODE_TIME:
        // двоеточие 2 Гц
        colon = !colon; setColon(colon);
        // перерисовываем цифры только при смене hh/mm
        if (now.hour()!=prevH || now.minute()!=prevM) {
          prevH=now.hour(); prevM=now.minute();
          drawDigits(prevH, prevM);
        }
        break;

      case MODE_DATE:
        // двоеточие как разделитель даты — постоянно включено
        setColon(true);
        showDate(now.day(), now.month());
        break;

      case MODE_TEMP:
        // двоеточие выключено
        setColon(false);
        showTempC(rtc.getTemperature());
        break;
    }
  }
}