#include <Arduino.h>
#include <BluetoothSerial.h>
#include <ESP32Servo.h>
#include <ESP_FlexyStepper.h>
#include <queue>

// Пины шаговых двигателей (A4988)
#define STEP_1 23 // Пин STEP первого насоса
#define DIR_1  22  // Пин DIR первого насоса
#define STEP_2 18 // Пин STEP второго насоса
#define DIR_2  5   // Пин DIR второго насоса
#define STEP_3 4  // Пин STEP третьего насоса
#define DIR_3  2   // Пин DIR третьего насоса

// Пин сервопривода
#define SERVO_PIN 13 // Пин управления сервоприводом

// Пины датчиков TCRT5000
const int sensorPins[4] = {27, 25, 32, 34}; // Датчики стаканов

// Настройка Bluetooth
BluetoothSerial SerialBT; // Объект для связи по Bluetooth
Servo servo; // Объект сервопривода
std::queue<int> orderQueue; // Очередь заказов
int currentCup = 0; // Текущий стакан

// Углы поворота сервопривода
const int DISPENSER_ANGLE = 0;   // Угол для диспенсера
const int DELIVERY_ANGLE = 180;  // Угол для зоны выдачи
const int CUP_ANGLE = 90;        // Угол между стаканами

// Создание объектов шаговых двигателей
ESP_FlexyStepper stepper1;
ESP_FlexyStepper stepper2;
ESP_FlexyStepper stepper3;

// Константы для дозирования
const int STEPS_PER_10ML = 60; // 3 оборота * 200 шагов на оборот

// Прототипы функций
void setupSteppers();
void runStepper(ESP_FlexyStepper &stepper, int steps);
bool isCupPresent(int sensorPin);
void dispenseDrink(int drinkCode);
void rotateCupHolder(int angle);
void moveCupsToDelivery();
void resetCupHolder();
void clearQueue();
void breakOperation();
void printMenu();
void printQueue();
void processOrder(); // Добавлен прототип функции processOrder

// Функция настройки параметров шаговых двигателей
void setupSteppers() {
    stepper1.connectToPins(STEP_1, DIR_1);
    stepper2.connectToPins(STEP_2, DIR_2);
    stepper3.connectToPins(STEP_3, DIR_3);
    
    stepper1.setSpeedInStepsPerSecond(1000);
    stepper1.setAccelerationInStepsPerSecondPerSecond(2000);
    stepper2.setSpeedInStepsPerSecond(1000);
    stepper2.setAccelerationInStepsPerSecondPerSecond(2000);
    stepper3.setSpeedInStepsPerSecond(1000);
    stepper3.setAccelerationInStepsPerSecondPerSecond(2000);
}

// Функция управления шаговыми моторами
void runStepper(ESP_FlexyStepper &stepper, int steps) {
    stepper.setTargetPositionRelativeInSteps(steps);
    while (!stepper.processMovement()) {
        if (!isCupPresent(sensorPins[currentCup])) {
            Serial.println("Cup removed during dispensing! Stopping...");
            stepper.setTargetPositionToStop();
            break;
        }
    }
}

// Функция проверки наличия стакана
bool isCupPresent(int sensorPin) {
    return digitalRead(sensorPin) == LOW;
}

// Функция выдачи напитка
void dispenseDrink(int drinkCode) {
    switch (drinkCode) {
        case 1: runStepper(stepper1, 5 * STEPS_PER_10ML); break;  // Газ. вода 50 мл
        case 2: runStepper(stepper2, 1 * STEPS_PER_10ML); break;  // Мятный сироп 10 мл
        case 3: runStepper(stepper3, 4 * STEPS_PER_10ML); break;  // Апельсиновый сок 40 мл
        case 4:
            runStepper(stepper1, 8 * STEPS_PER_10ML); // Газ. вода 80 мл
            runStepper(stepper2, 2 * STEPS_PER_10ML); // Мятный сироп 20 мл
            break;
        case 5:
            runStepper(stepper1, 3 * STEPS_PER_10ML);  // Газ. вода 30 мл
            runStepper(stepper3, 5 * STEPS_PER_10ML); // Апельсиновый сок 50 мл
            break;
        case 6:
            runStepper(stepper1, 3.5 * STEPS_PER_10ML); // Газ. вода 35 мл
            runStepper(stepper3, 4.5 * STEPS_PER_10ML); // Апельсиновый сок 45 мл
            runStepper(stepper2, 1 * STEPS_PER_10ML);  // Мятный сироп 10 мл
            break;
    }
}

// Функция поворота диска со стаканами
void rotateCupHolder(int angle) {
    servo.write(angle); // Повернуть сервопривод на заданный угол
    delay(1000); // Дать время на поворот
}

// Функция перемещения стаканов в зону выдачи
void moveCupsToDelivery() {
    for (int i = 0; i < 4; i++) {
        rotateCupHolder(DELIVERY_ANGLE + i * CUP_ANGLE); // Перемещаем стакан в зону выдачи
        while (isCupPresent(sensorPins[i])) {
            Serial.println("Waiting for cup removal...");
            delay(500);
        }
    }
    resetCupHolder(); // Возвращаем диск в исходное положение
}

// Функция возврата в исходное положение
void resetCupHolder() {
    rotateCupHolder(DISPENSER_ANGLE); // Возвращаем сервопривод в исходное положение
    currentCup = 0; // Сбрасываем текущий стакан
    Serial.println("Cup holder reset to initial position.");
}

// Функция очистки очереди
void clearQueue() {
    while (!orderQueue.empty()) {
        orderQueue.pop();
    }
    SerialBT.println("Queue cleared.");
}

// Функция прерывания работы
void breakOperation() {
    clearQueue();
    resetCupHolder();
    SerialBT.println("Operation interrupted. System reset.");
}

// Функция вывода меню напитков
void printMenu() {
    SerialBT.println("Menu:");
    SerialBT.println("1. Газированная вода (50 мл)");
    SerialBT.println("2. Мятный сироп (10 мл)");
    SerialBT.println("3. Апельсиновый сок (40 мл)");
    SerialBT.println("4. Лимонад 'Мятный' (80 мл воды + 20 мл сиропа)");
    SerialBT.println("5. Лимонад 'Заводной апельсин' (30 мл воды + 50 мл сока)");
    SerialBT.println("6. Лимонад 'Тройной' (35 мл воды + 45 мл сока + 10 мл сиропа)");
}

// Функция вывода текущей очереди
void printQueue() {
    if (orderQueue.empty()) {
        SerialBT.println("Queue is empty.");
    } else {
        SerialBT.println("Current queue:");
        std::queue<int> tempQueue = orderQueue;
        while (!tempQueue.empty()) {
            SerialBT.printf("Drink %d\n", tempQueue.front());
            tempQueue.pop();
        }
    }
}

// Функция обработки заказа
void processOrder() {
    if (!orderQueue.empty()) {
        int drink = orderQueue.front();
        if (isCupPresent(sensorPins[currentCup])) {
            dispenseDrink(drink);
            orderQueue.pop();
            Serial.printf("Drink %d dispensed. Moving to next cup.\n", drink);
            
            rotateCupHolder(DISPENSER_ANGLE + (currentCup + 1) * CUP_ANGLE); // Перемещаем к следующему стакану
            currentCup = (currentCup + 1) % 4; // Обновляем текущий стакан
        } else {
            Serial.println("No cup detected! Waiting...");
        }
    }
}

// Инициализация системы
void setup() {
    Serial.begin(115200);
    SerialBT.begin("LemonadeMachine");
    
    for (int i = 0; i < 4; i++) {
        pinMode(sensorPins[i], INPUT);
    }
    
    servo.attach(SERVO_PIN);
    setupSteppers();
    Serial.println("Machine ready");
}

// Основной цикл работы
void loop() {
    if (SerialBT.available()) {
        char command = SerialBT.read();
        int drink = 0; // Объявляем переменную drink вне switch-case

        switch (command) {
            case '1': case '2': case '3': case '4': case '5': case '6': // Добавление напитка в очередь
                drink = command - '0'; // Присваиваем значение переменной drink
                orderQueue.push(drink);
                SerialBT.printf("Drink %d added to queue.\n", drink);
                break;
            case 'S': // Начало выполнения заказа
                Serial.println("Starting order processing...");
                while (!orderQueue.empty()) {
                    processOrder();
                    delay(5000);
                }
                Serial.println("Order completed. Moving cups to delivery.");
                moveCupsToDelivery();
                break;
            case 'C': // Очистка очереди
                clearQueue();
                break;
            case 'B': // Прерывание работы
                breakOperation();
                break;
            case 'R': // Перезапуск/сброс системы
                clearQueue();
                resetCupHolder();
                SerialBT.println("System reset.");
                break;
            case 'L': // Вывод меню напитков
                printMenu();
                break;
            case 'Q': // Вывод текущей очереди
                printQueue();
                break;
            default:
                SerialBT.println("Unknown command.");
                break;
        }
    }
}