/************************************************************
 * FEETECH STS: текстовый контроллер по UART1
 * ---------------------------------------------------------
 * Возможности:
 *
 * 1) Найти сервоприводы на шине
 *    - PING <id>
 *    - SCAN_ID [from] [to]
 *
 * 2) Задать / изменить ID
 *    - SET_ID <oldId> <newId>
 *
 * 3) Переключать режимы работы
 *    - SET_SERVO <id>   (позиционный режим)
 *    - SET_WHEEL <id>   (колёсный режим, бесконечное вращение)
 *    - READ_MODE <id>   (0 = SERVO, 1 = WHEEL)
 *
 * 4) Управлять в каждом режиме
 *    Режим SERVO:
 *      - MOVE <id> <pos> <speed> <acc>
 *      - SYNC_MOVE <n> <id1> <pos1> <speed1> <acc1> ...
 *      - READ_POS <id>
 *      - SCAN_POS [from] [to]
 *
 *    Режим WHEEL:
 *      - SPEED <id> <speed>         (signed, -4095..4095)
 *      - SYNC_SPEED <n> <id1> <speed1> ...
 *      - STOP <id>
 *      - STOP_ALL                   (для списка SERVO_IDS[])
 *      - READ_SPEED <id>
 *
 * 5) Управлять усилием и читать параметры
 *    - ENABLE_TORQUE <id>
 *    - DISABLE_TORQUE <id>
 *    - READ_LOAD <id>
 *    - READ_VOLTAGE <id>
 *    - READ_TEMP <id>
 *
 * ---------------------------------------------------------
 * Аппаратные требования (ESP32):
 *  - Шина сервоприводов на UART1 (Serial1)
 *  - Скорость: 1 000 000 бод (SERVO_BAUD)
 *  - Пины: 
 *         GPIO18 = RX1 → RX платы-переходника,
 *         GPIO19 = TX1 → TX платы-переходника
 *    ВАЖНО - Тут нет ошибки! Именно так!
 *
 * Библиотека:
 *  - SCServo (класс SMS_STS)
 *
 * Обмен с ПК:
 *  - USB-Serial (Serial) @ 115200 бод
 *  - Одна команда на строку: <COMMAND> [arg1] [arg2] ...
 *    Пример: "MOVE 100 2048 400 50\n"
 *  - Регистр команд не важен (set_id, Set_Id, SET_ID — одинаково).
 *
 * Внимание:
 *  - В рабочей прошивке лучше использовать бинарный протокол и
 *    избегать класса String из-за динамической памяти.
 ************************************************************/

#include <SCServo.h>

// ---------------------- НАСТРОЙКИ ЖЕЛЕЗА ----------------------

// Пины UART1 (ESP32) для шины сервоприводов
#define S_RXD 18
#define S_TXD 19

// Скорость UART для STS-сервоприводов
#define SERVO_BAUD 1000000

// Диапазон скоростей для wheel-mode (SPEED / SYNC_SPEED)
const int SPEED_MIN = -4095;
const int SPEED_MAX =  4095;

// Пример списка ID для STOP_ALL
const uint8_t SERVO_IDS[] = { 100, 101, 102, 103, 104, 105, 106 };
const int SERVO_COUNT      = sizeof(SERVO_IDS) / sizeof(SERVO_IDS[0]);

// Глобальный объект библиотеки
SMS_STS st;

// ------------------------- ПРОТОТИПЫ --------------------------

void processCommand(String command);

// handlers
void handlePing(const String &args);
void handleScanID(const String &args);
void handleSetID(const String &args);

void handleSetServo(const String &args);
void handleSetWheel(const String &args);
void handleReadMode(const String &args);

void handleMove(const String &args);
void handleSyncMove(const String &args);
void handleReadPos(const String &args);
void handleScanPos(const String &args);

void handleSpeed(const String &args);
void handleSyncSpeed(const String &args);
void handleStop(const String &args);
void handleStopAll();
void handleReadSpeed(const String &args);

void handleEnableTorque(const String &args);
void handleDisableTorque(const String &args);
void handleReadLoad(const String &args);
void handleReadVoltage(const String &args);
void handleReadTemp(const String &args);

// ------------------------- SETUP / LOOP -----------------------

void setup() {
  // USB-Serial для команд с ПК
  Serial.begin(115200);
  delay(500);

  // UART1 на шину сервоприводов
  Serial1.begin(SERVO_BAUD, SERIAL_8N1, S_RXD, S_TXD);
  st.pSerial = &Serial1;
}

void loop() {
  // Читаем строку из Serial (до '\n')
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      processCommand(cmd);
    }
  }
}

// =============================================================
//                        ПАРСЕР КОМАНД
// =============================================================

void processCommand(String command) {
  // Разделяем на имя команды и аргументы (после первого пробела)
  int firstSpaceIndex = command.indexOf(' ');
  String cmdName = command;
  String args = "";

  if (firstSpaceIndex > 0) {
    cmdName = command.substring(0, firstSpaceIndex);
    args    = command.substring(firstSpaceIndex + 1);
    args.trim();
  }

  cmdName.toUpperCase(); // игнорируем регистр

  // 1. Поиск сервоприводов
  if      (cmdName == "PING")           { handlePing(args); }
  else if (cmdName == "SCAN_ID")        { handleScanID(args); }

  // 2. Настройка ID
  else if (cmdName == "SET_ID")         { handleSetID(args); }

  // 3. Режимы работы
  else if (cmdName == "SET_SERVO")      { handleSetServo(args); }
  else if (cmdName == "SET_WHEEL")      { handleSetWheel(args); }
  else if (cmdName == "READ_MODE")      { handleReadMode(args); }

  // 4a. Управление в позиционном режиме
  else if (cmdName == "MOVE")           { handleMove(args); }
  else if (cmdName == "SYNC_MOVE")      { handleSyncMove(args); }
  else if (cmdName == "READ_POS")       { handleReadPos(args); }
  else if (cmdName == "SCAN_POS")       { handleScanPos(args); }

  // 4b. Управление в wheel-mode
  else if (cmdName == "SPEED")          { handleSpeed(args); }
  else if (cmdName == "SYNC_SPEED")     { handleSyncSpeed(args); }
  else if (cmdName == "STOP")           { handleStop(args); }
  else if (cmdName == "STOP_ALL")       { handleStopAll(); }
  else if (cmdName == "READ_SPEED")     { handleReadSpeed(args); }

  // 5. Усилие и телеметрия
  else if (cmdName == "ENABLE_TORQUE")  { handleEnableTorque(args); }
  else if (cmdName == "DISABLE_TORQUE") { handleDisableTorque(args); }
  else if (cmdName == "READ_LOAD")      { handleReadLoad(args); }
  else if (cmdName == "READ_VOLTAGE")   { handleReadVoltage(args); }
  else if (cmdName == "READ_TEMP")      { handleReadTemp(args); }

  else {
    Serial.println("ERROR: Unknown command");
  }
}

// =============================================================
//      1) Поиск сервоприводов: PING и SCAN_ID
// =============================================================

// PING <id> — одиночная проверка, есть ли серво с таким ID
void handlePing(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: Invalid ID");
    return;
  }

  int pingRes = st.Ping(id);
  Serial.println(pingRes != -1 ? "OK" : "ERROR: Ping fail");
}

// SCAN_ID [from] [to] — поиск всех сервоприводов на шине
//
// Примеры:
//  - SCAN_ID          -> сканировать 0..253
//  - SCAN_ID 100      -> сканировать 100..253
//  - SCAN_ID 90 110   -> сканировать 90..110
//
void handleScanID(const String &args) {
  int from = 0;
  int to   = 253;

  if (args.length()) {
    int sp = args.indexOf(' ');
    if (sp < 0) {
      from = args.toInt();
    } else {
      from = args.substring(0, sp).toInt();
      to   = args.substring(sp + 1).toInt();
    }
  }

  if (from < 0)   from = 0;
  if (to   > 253) to   = 253;
  if (to < from) { int t = from; from = to; to = t; }

  int foundCount = 0;

  Serial.print("SCAN_ID from ");
  Serial.print(from);
  Serial.print(" to ");
  Serial.println(to);

  for (int id = from; id <= to; id++) {
    delayMicroseconds(300);
    int resp = st.Ping(id);
    if (resp != -1) {
      Serial.print("FOUND ID ");
      Serial.println(resp);
      foundCount++;
      delayMicroseconds(200);
    }
  }

  Serial.print("FOUND_TOTAL ");
  Serial.println(foundCount);
}

// =============================================================
//      2) Настройка ID: SET_ID <oldId> <newId>
// =============================================================

void handleSetID(const String &args) {
  int spaceIdx = args.indexOf(' ');
  if (spaceIdx < 0) {
    Serial.println("ERROR: SET_ID usage: SET_ID <oldId> <newId>");
    return;
  }

  int oldId = args.substring(0, spaceIdx).toInt();
  int newId = args.substring(spaceIdx + 1).toInt();

  if (oldId < 0 || oldId > 253 || newId < 0 || newId > 253) {
    Serial.println("ERROR: Invalid ID range");
    return;
  }

  st.unLockEprom((uint8_t)oldId);
  bool writeOk = st.writeByte((uint8_t)oldId, SMS_STS_ID, (uint8_t)newId);
  st.LockEprom((uint8_t)newId);

  Serial.println(writeOk ? "OK" : "ERROR: write ID fail");
}

// =============================================================
//      3) Режимы работы: SET_SERVO / SET_WHEEL / READ_MODE
// =============================================================

// SET_SERVO <id> — позиционный режим (MODE = 0)
void handleSetServo(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int r = st.writeByte((uint8_t)id, SMS_STS_MODE, 0);
  if (r != -1) {
    st.EnableTorque((uint8_t)id, 1);
    Serial.println("OK");
  } else {
    Serial.println("ERROR");
  }
}

// SET_WHEEL <id> — колёсный режим
void handleSetWheel(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int r = st.WheelMode((uint8_t)id);
  Serial.println(r != -1 ? "OK" : "ERROR");
}

// READ_MODE <id> — 0: позиционный, 1: wheel
void handleReadMode(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int m = st.ReadMode((uint8_t)id);
  if (m != -1) {
    Serial.print("MODE ");
    Serial.println(m);
  } else {
    Serial.println("ERROR");
  }
}

// =============================================================
//      4a) Позиционный режим: MOVE / SYNC_MOVE / READ_POS
// =============================================================

// MOVE <id> <position> <speed> <acc>
void handleMove(const String &args) {
  int sp1 = args.indexOf(' ');
  int sp2 = args.indexOf(' ', sp1 + 1);
  int sp3 = args.indexOf(' ', sp2 + 1);

  if (sp1 < 0 || sp2 < 0 || sp3 < 0) {
    Serial.println("ERROR: MOVE usage: MOVE <id> <pos> <speed> <acc>");
    return;
  }

  int id    = args.substring(0, sp1).toInt();
  int pos   = args.substring(sp1 + 1, sp2).toInt();
  int speed = args.substring(sp2 + 1, sp3).toInt();
  int acc   = args.substring(sp3 + 1).toInt();

  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }

  if (pos   < 0)    pos   = 0;
  if (pos   > 4095) pos   = 4095;
  if (speed < 0)    speed = 0;
  if (speed > 4095) speed = 4095;
  if (acc   < 0)    acc   = 0;
  if (acc   > 255)  acc   = 255;

  st.WritePosEx((uint8_t)id, (int16_t)pos, (uint16_t)speed, (uint8_t)acc);
  Serial.println("OK");
}

// SYNC_MOVE <n> <id pos speed acc> ...
// Пример:
//   SYNC_MOVE 2  100 2048 400 50  101 1024 400 50
//
void handleSyncMove(const String &args) {
  int sp1 = args.indexOf(' ');
  if (sp1 < 0) {
    Serial.println("ERROR: SYNC_MOVE usage");
    return;
  }

  int n = args.substring(0, sp1).toInt();
  if (n < 1 || n > 12) {
    Serial.println("ERROR: n out of range");
    return;
  }

  uint8_t IDs[12];
  int16_t Positions[12];
  uint16_t Speeds[12];
  uint8_t Accs[12];

  String remain = args.substring(sp1 + 1);
  remain.trim();

  for (int i = 0; i < n; i++) {
    int spA = remain.indexOf(' ');
    int spB = remain.indexOf(' ', spA + 1);
    int spC = remain.indexOf(' ', spB + 1);

    if (spA < 0 || spB < 0 || spC < 0) {
      Serial.println("ERROR: not enough arguments for SYNC_MOVE");
      return;
    }

    String idStr  = remain.substring(0, spA);
    String posStr = remain.substring(spA + 1, spB);
    String spdStr = remain.substring(spB + 1, spC);

    int spNext = remain.indexOf(' ', spC + 1);
    String accStr;
    if (spNext >= 0) {
      accStr = remain.substring(spC + 1, spNext);
      remain = remain.substring(spNext + 1);
    } else {
      accStr = remain.substring(spC + 1);
      remain = "";
    }
    remain.trim();

    int id    = idStr.toInt();
    int pos   = posStr.toInt();
    int speed = spdStr.toInt();
    int acc   = accStr.toInt();

    if (pos   < 0)    pos   = 0;
    if (pos   > 4095) pos   = 4095;
    if (speed < 0)    speed = 0;
    if (speed > 4095) speed = 4095;
    if (acc   < 0)    acc   = 0;
    if (acc   > 255)  acc   = 255;

    IDs[i]       = (uint8_t)id;
    Positions[i] = (int16_t)pos;
    Speeds[i]    = (uint16_t)speed;
    Accs[i]      = (uint8_t)acc;
  }

  st.SyncWritePosEx(IDs, n, Positions, Speeds, Accs);
  Serial.println("OK");
}

// READ_POS <id>
void handleReadPos(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int pos = st.ReadPos(id);
  if (pos != -1) {
    Serial.print("POS ");
    Serial.println(pos);
  } else {
    Serial.println("ERROR: read pos fail");
  }
}

// SCAN_POS [from] [to] — читаем позиции всех ответивших в диапазоне
void handleScanPos(const String &args) {
  int from = 0;
  int to   = 253;

  if (args.length()) {
    int sp = args.indexOf(' ');
    if (sp < 0) {
      from = args.toInt();
    } else {
      from = args.substring(0, sp).toInt();
      to   = args.substring(sp + 1).toInt();
    }
  }

  if (from < 0)   from = 0;
  if (to   > 253) to   = 253;
  if (to < from) { int t = from; from = to; to = t; }

  for (int id = from; id <= to; id++) {
    int pos = st.ReadPos(id);
    if (pos >= 0) {
      Serial.print("ID ");
      Serial.print(id);
      Serial.print(" POS ");
      Serial.println(pos);
    }
    delayMicroseconds(300);
  }
  Serial.println("DONE");
}

// =============================================================
//      4b) Wheel-mode: SPEED / SYNC_SPEED / STOP / READ_SPEED
// =============================================================

// SPEED <id> <speed> — signed скорость для wheel-mode
void handleSpeed(const String &args) {
  int sp = args.indexOf(' ');
  if (sp < 0) {
    Serial.println("ERROR: SPEED usage: SPEED <id> <speed>");
    return;
  }

  int id    = args.substring(0, sp).toInt();
  int speed = args.substring(sp + 1).toInt();

  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }

  if (speed < SPEED_MIN) speed = SPEED_MIN;
  if (speed > SPEED_MAX) speed = SPEED_MAX;

  st.EnableTorque((uint8_t)id, 1);
  delay(2);

  int r = st.WriteSpe((uint8_t)id, (int16_t)speed);
  Serial.println(r != -1 ? "OK" : "ERROR");
}

// SYNC_SPEED <n> <id1> <speed1> ...
// Для простоты здесь пишем по одному (SyncWriteSpe в либе нет)
void handleSyncSpeed(const String &args) {
  int sp1 = args.indexOf(' ');
  if (sp1 < 0) {
    Serial.println("ERROR: SYNC_SPEED usage");
    return;
  }

  int n = args.substring(0, sp1).toInt();
  if (n < 1 || n > 12) {
    Serial.println("ERROR: n out of range");
    return;
  }

  String remain = args.substring(sp1 + 1);
  remain.trim();

  for (int i = 0; i < n; i++) {
    int spA = remain.indexOf(' ');
    if (spA < 0) {
      Serial.println("ERROR: not enough args");
      return;
    }

    String idStr = remain.substring(0, spA);
    remain = remain.substring(spA + 1);
    remain.trim();

    int spB = remain.indexOf(' ');
    String speedStr;
    if (spB >= 0 && i < n - 1) {
      speedStr = remain.substring(0, spB);
      remain   = remain.substring(spB + 1);
      remain.trim();
    } else {
      speedStr = remain;
      remain   = "";
    }

    int id    = idStr.toInt();
    int speed = speedStr.toInt();

    if (id < 0 || id > 253) {
      Serial.println("ERROR: invalid ID");
      return;
    }
    if (speed < SPEED_MIN) speed = SPEED_MIN;
    if (speed > SPEED_MAX) speed = SPEED_MAX;

    st.EnableTorque((uint8_t)id, 1);
    delay(2);
    if (st.WriteSpe((uint8_t)id, (int16_t)speed) == -1) {
      Serial.println("ERROR");
      return;
    }
    delayMicroseconds(300);
  }

  Serial.println("OK");
}

// STOP <id> — скорость = 0
void handleStop(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  st.EnableTorque((uint8_t)id, 1);
  delay(2);
  int r = st.WriteSpe((uint8_t)id, 0);
  Serial.println(r != -1 ? "OK" : "ERROR");
}

// STOP_ALL — скорость = 0 для всех из SERVO_IDS[]
void handleStopAll() {
  for (int i = 0; i < SERVO_COUNT; i++) {
    uint8_t id = SERVO_IDS[i];
    st.EnableTorque(id, 1);
    delay(2);
    if (st.WriteSpe(id, 0) == -1) {
      Serial.println("ERROR");
      return;
    }
    delayMicroseconds(300);
  }
  Serial.println("OK");
}

// READ_SPEED <id>
void handleReadSpeed(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int v = st.ReadSpeed((uint8_t)id);
  if (v != -1) {
    Serial.print("SPEED ");
    Serial.println(v);
  } else {
    Serial.println("ERROR");
  }
}

// =============================================================
//      5) Усилие и телеметрия: torque, load, voltage, temp
// =============================================================

// ENABLE_TORQUE <id>
void handleEnableTorque(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: Invalid ID");
    return;
  }
  int res = st.EnableTorque((uint8_t)id, 1);
  Serial.println(res != -1 ? "OK" : "ERROR: enable torque fail");
}

// DISABLE_TORQUE <id>
void handleDisableTorque(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: Invalid ID");
    return;
  }
  int res = st.EnableTorque((uint8_t)id, 0);
  Serial.println(res != -1 ? "OK" : "ERROR: disable torque fail");
}

// READ_LOAD <id>
void handleReadLoad(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int load = st.ReadLoad(id);
  if (load != -1) {
    Serial.print("LOAD ");
    Serial.println(load);
  } else {
    Serial.println("ERROR: read load fail");
  }
}

// READ_VOLTAGE <id>
void handleReadVoltage(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int volt = st.ReadVoltage(id);
  if (volt != -1) {
    Serial.print("VOLTAGE ");
    Serial.println(volt);
  } else {
    Serial.println("ERROR: read voltage fail");
  }
}

// READ_TEMP <id>
void handleReadTemp(const String &args) {
  int id = args.toInt();
  if (id < 0 || id > 253) {
    Serial.println("ERROR: invalid ID");
    return;
  }
  int temp = st.ReadTemper(id);
  if (temp != -1) {
    Serial.print("TEMP ");
    Serial.println(temp);
  } else {
    Serial.println("ERROR: read temperature fail");
  }
}
