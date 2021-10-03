// SCREEN
#define _LCD_TYPE 1
#define SCREEN_W 20
#define SCREEN_H 4

// BUTTONS
#define B_MAIN 6
#define B_UP 4
#define B_DOWN 5

// FLOW SIGNAL
#define SIGNAL 0

// POWER CONTROL
#define POWER 3

// FLOW
#define FLOW_VALUE_DELTA 30

#include <LiquidCrystal_I2C.h>
#include <SPI.h>                             // Подключаем библиотеку SPI
#include <MsTimer2.h>
#include <powerConstants.h>
#include <GyverPower.h>

LiquidCrystal_I2C lcd(0x27, SCREEN_W, SCREEN_H); // Устанавливаем дисплей

bool isSleep = true;  // контроль за состоянием сна устройства
bool isLoad = false;  // для обработки настроек при "просыпании" устройства

double LT = 0.00;   // последнее измерение
double AT = 0.00;   // среднее измерение
int N = 0;        // количество измерений 
double KF = 1.00;
String title = "WAITING START"; // заголовок экрана
String resultStr = "0.00";    // переменная для вывода на экран времени

int buttonPress = 0;      // переменная для определения зажатия кнопок
int delayForButton = 200;   // задержка после срабатывания нажатия, позволяет регулировать скорость срабатывания нажатия кнопки

int tInterupt = 1;        // определяет как часто срабатывает прерывание для считывания нажатия кнопок и состояния датчика воронки
int count = 0;          // счетчик, по которому обновляется вывод времени на экран
int curFlowValue = 0;     // текущее значение сигнала от датчика
int oldFlowValue = 0;     // предыдущее значение сигнала от датчика
long waitstart = 0;       // время (в млс от включения устройства) запуска измерения.  
int waitInterrupt = 60000;    // если с момента запуска измерения прошло столько млс, а порошок не начал сыпаться - сбросит состояние до ожидание старта измерений
long timestart = 0;       // время (в млс от включения устройства), когда начал сыпаться порошок
long timestop = 0;        // время (в млс от включения устройства), когда закончил сыпаться порошок
double result = 0;        // время просыпания порошка

// Какая кнопка нажата сейчас.
enum Button {
  none,
  up,
  down,
  main,
};

// Статус измерения воронки
enum Status {
  no,
  wait,
  proceed,
  finish
};


Button oldButton = none;    // Предыдущая нажатая кнопка
Button curButton = none;    // Текущая нажатая кнопка
Status flow = no;       // Текущее состояние измерения воронки

void setup() {
  
  lcd.init();

  pinMode(POWER, INPUT_PULLUP);
  pinMode(B_MAIN, INPUT_PULLUP);
  pinMode(B_UP, INPUT_PULLUP);
  pinMode(B_DOWN, INPUT_PULLUP);
  
  curFlowValue = analogRead(SIGNAL);
  oldFlowValue = curFlowValue;
  
  MsTimer2::set(tInterupt, timerInterupt); // задаем период прерывания по таймеру 2 мс
  
  // подключаем прерывание сна на пин D3 (Arduino NANO)
  lcd.clear();
  lcd.noBacklight(); // Выключаем подсветку дисплея;
  attachInterrupt(1, isr1, FALLING);
  power.setSleepMode(EXTSTANDBY_SLEEP);
  power.sleep(SLEEP_FOREVER);

}

// обработчик аппаратного прерывания
void isr1() {
  // дёргаем за функцию "проснуться"
  // без неё проснёмся чуть позже (через 0-8 секунд)
  power.wakeUp();
  isSleep = false;
  isLoad = false;
}

void isr2() {
  // дёргаем за функцию "проснуться"  
  isSleep = true;

}

void loop() {
  if (!isSleep) {
    if (!isLoad) {
      attachInterrupt(1, isr2, RISING);
      lcd.backlight(); // Включаем подсветку дисплея;
      lcd.clear();
      lcd_output(2, "Flowmeter v.1", 13);

      for (int i = 0; i < 10; i++) {

        lcd.setCursor(5 + i, 2);
        lcd.print(".");
        delay(200);
      }
      delay(200);
      lcd.clear();
      update_output();
      isLoad = true;
      MsTimer2::start();
    } else {
      if (curButton == up && KF < 9.99 && flow == none && N == 0) {

        KF = KF + 0.01;
        lcd_output(4, "KF=" + String(KF, 2), -7);
        delay(delayForButton);
      } else if (curButton == down && KF > 0.01 && flow == none && N == 0) {
        KF = KF - 0.01;
        lcd_output(4, "KF=" + String(KF, 2), -7);
        delay(delayForButton);
      } else if (curButton == main && buttonPress >= 3000 && flow == none) {
        if (N != 0) {
          title = "WAITING START";
          count = 0;
          result = 0;
          resultStr = String(result / 1000, 2);
          LT = 0;
          AT = 0;
          N = 0;
          flow = no;
          lcd.clear();
          update_output();
        }

      } else if (curButton == none && oldButton == main && flow == no) {
        flow = wait;
        waitstart = millis();
        lcd_row_clear(2);
        title = "WAITING SAMPLE";
        lcd_output(2, title, title.length());

      }

      if (flow == wait) {
        if (millis() - waitstart > waitInterrupt) {
          flow = no;
          lcd_row_clear(2);
          title = "WAITING START";
          lcd_output(2, title, title.length());
          return;
        }
        if (result != 0) {
          result = 0;
          resultStr = String(result / 1000, 2);
          lcd_row_clear(3);
          lcd_output(3, resultStr, resultStr.length());
        }
        check_flow();
        count = 0;
      } else if (flow == proceed) {
        title = "MEASUREMENT";
        check_flow();

        if (count >= 100) {
          count = 0;
          update_timer(3);
//          resultStr = String(result / 1000, 2);
//          lcd_row_clear(3);
//          lcd_output(3, resultStr, resultStr.length());
        }
      } else if (flow == finish) {
        title = "WAITING START";
        LT = result / 1000 * KF;
        AT = (AT * N + LT) / (N + 1);
        N++;

        resultStr = String(result / 1000, 2);
        flow = no;
        lcd.clear();
        update_output();
      }

    }
  } else {
    lcd.clear();
    lcd.noBacklight(); // Включаем подсветку дисплея;
    attachInterrupt(1, isr1, FALLING);
    power.sleep(SLEEP_FOREVER);
  }

}

void timerInterupt() {
  if (isLoad) {
    get_button();
    curFlowValue = analogRead(SIGNAL);
    count++;
  }
}

void lcd_output(byte row, String str, int len) {
  int indent = 0;
  if (len < SCREEN_W) {
    indent = (SCREEN_W - len) / 2;
    lcd.setCursor(indent, row - 1);
    lcd.print(str);
  } else {
    indent = 0;
    lcd.setCursor(indent, row - 1);
    lcd.print(str);
  }
}

void lcd_row_clear(byte row) {
  lcd.setCursor(0, row - 1);
  lcd.print("                    ");
}

void get_button() {
  oldButton = curButton;
  if (digitalRead(B_MAIN) == 0) {
    curButton = main;
  } else if (digitalRead(B_UP) == 0) {
    curButton = up;
  } else if (digitalRead(B_DOWN) == 0) {
    curButton = down;
  } else {
    curButton = none;
  }
  if (oldButton == curButton) {
    buttonPress++;
    if (buttonPress < 500) {
      delayForButton = 200;
    } else if (buttonPress < 2000) {
      delayForButton = 20;
    } else if (buttonPress > 2000) {
      delayForButton = 1;
    }
  } else {
    if (buttonPress >= 500) {
      oldButton = none;
    }
    buttonPress = 0;
    delayForButton = 200;
  }
}

void print_button() {
  lcd.clear();
  if (curButton == up) {
    lcd_output(2, "up", 2);
  } else if (curButton == down) {
    lcd_output(2, "down", 4);
  } else if (curButton == main) {
    lcd_output(2, "main", 3);
  } else {
    lcd_output(2, "none", 4);
  }
}

void check_flow() {

  if (flow == wait && curFlowValue - oldFlowValue >= FLOW_VALUE_DELTA) {
    flow = proceed;
    timestart = millis();
    oldFlowValue = curFlowValue;
  }

  if (flow == proceed && oldFlowValue - curFlowValue >= FLOW_VALUE_DELTA) {
    flow = finish;

    if (timestart != 0) {
      timestop = millis();
      result = timestop - timestart;
      timestart = 0;
      timestop = 0;
      oldFlowValue = curFlowValue;
    }

  }

  if (flow == proceed) {

    result = millis() - timestart;
  }

}

void update_output() {
  lcd.clear();
  if (LT > 9.99) {
    lcd_output(1, "LT=" + String(LT, 2) + "        " + " " + "N=" + N, 20);
  } else {
    lcd_output(1, "LT=" + String(LT, 2) + "         " + " " + "N=" + N, 20);
  }

  lcd_output(2, title, title.length());
  lcd_output(3, resultStr, resultStr.length());
  if (AT > 9.99) {
    lcd_output(4, "AT=" + String(AT, 2) + "     " + "KF=" + String(KF, 2), 20);
  } else {
    lcd_output(4, "AT=" + String(AT, 2) + "      " + "KF=" + String(KF, 2), 20);
  }

  isLoad = true;
}

void update_timer(byte row){

  String oldResultStr = resultStr;
  resultStr = String(result / 1000, 2);
  int len = resultStr.length();
  if(len==oldResultStr.length()){
    int indent = (SCREEN_W - len) / 2;
    char newStr[len];
    char oldStr[len];
    resultStr.toCharArray(newStr, len);
    oldResultStr.toCharArray(oldStr, len);
    for(int i=0;i<len;i++){
      if(oldStr[i] != newStr[i]){
        lcd.setCursor(indent+i, row - 1);
        lcd.print(newStr[i]);
      }
    }   
  }
  else{
    lcd_row_clear(3);
    lcd_output(3, resultStr, resultStr.length());
  }
  

  
}
