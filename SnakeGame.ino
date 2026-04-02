#include <EEPROM.h>

#define DATA 21
#define CLOCK 19
#define LATCH 18

#define UP_BUTTON 25
#define DOWN_BUTTON 26
#define LEFT_BUTTON 27
#define RIGHT_BUTTON 14

#define LS90_RESET 23
#define LS90_CLOCK 33

#define MODE_BUTTON 22

#define LED_GREEN 4
#define LED_WHITE 16
#define LED_YELLOW 17
#define LED_RED 5

bool gameStarted = false;
bool animationRunning = false;

bool up = digitalRead(UP_BUTTON) == LOW;
bool down = digitalRead(DOWN_BUTTON) == LOW;
bool left = digitalRead(LEFT_BUTTON) == LOW;
bool right = digitalRead(RIGHT_BUTTON) == LOW;

enum Direction { UP,
                 DOWN,
                 LEFT,
                 RIGHT };
Direction dir = RIGHT;
Direction nextDir = RIGHT;

const int MAX_LEN = 64;
int snakeX[MAX_LEN];
int snakeY[MAX_LEN];
int snakeLength = 1;

uint8_t headMask[8];
uint8_t framebuffer[8];

// Food globals
int foodX;
int foodY;
bool foodVisible = true;
unsigned long lastBlink = 0;
const unsigned long blinkInterval = 100;  // 10Hz

// Track the units LS90 value
int unitsCount = 0;

enum GameMode {
  EASY,
  NORMAL,
  HARD,
  LUDICROUS
};

unsigned long lastModePress = 0;

GameMode currentMode = NORMAL;

unsigned long moveInterval = 300;

bool autopilot = false;
bool autopilotAligned = false;
bool firstInputHandled = false;

bool allPressedBefore = false;    // tracks if all 4 buttons were pressed
bool firstButtonPressed = false;  // tracks if any button was pressed before normal start

// -------------- New Game ----------------

void resetGame() {
  snakeLength = 1;
  snakeX[0] = 3;
  snakeY[0] = 3;

  autopilot = false;
  autopilotAligned = false;
  firstInputHandled = false;
  allPressedBefore = false;
  firstButtonPressed = false;  // << reset normal start tracker

  resetCounter();
  pulseCounter();

  spawnFood();
  gameStarted = false;
}

// ---------------- AutoPilot -----------------

Direction autopilotDirection() {

  int x = snakeX[0];
  int y = snakeY[0];

  // ---- Phase 1: align to (1,0) ----
  if (!autopilotAligned) {

    if (y > 0) return UP;
    if (x > 1) return LEFT;
    if (x < 1) return RIGHT;

    autopilotAligned = true;
  }

  // ---- Phase 2: Hamiltonian cycle ----

  // bottom-right entry into column 8
  if (x == 6 && y == 7) return RIGHT;

  // climb column 8
  if (x == 7 && y > 0) return UP;

  // traverse top row
  if (y == 0 && x > 0) return LEFT;

  // re-enter cycle
  if (x == 0 && y == 0) return DOWN;

  // main region (rows 2-8, cols 1-7)
  if (y >= 1 && x <= 6) {

    if (y % 2 == 1) {  // odd row → RIGHT
      if (x < 6) return RIGHT;
      else return DOWN;
    } else {  // even row → LEFT
      if (x > 0) return LEFT;
      else return DOWN;
    }
  }

  return RIGHT;
}

// -------------------- Pulse Units Counter --------------------
void pulseCounter() {
  // Pulse the units LS90
  digitalWrite(LS90_CLOCK, HIGH);
  delayMicroseconds(50);  // ensure clean rising edge
  digitalWrite(LS90_CLOCK, LOW);
  delayMicroseconds(50);

  // Increment software units
  unitsCount++;
  if (unitsCount > 9) {
    unitsCount = 0;  // overflow, tens LS90 is incremented automatically via QD
  }
}

// -------------------- Reset Both Counters --------------------
void resetCounter() {
  // Reset both LS90 chips
  digitalWrite(LS90_RESET, HIGH);
  delayMicroseconds(50);
  digitalWrite(LS90_RESET, LOW);
  unitsCount = 0;
}

void responsiveDelay(int totalMs) {
  unsigned long start = millis();

  while (millis() - start < totalMs) {
    readModeButton();
    delay(5);
  }
}

void flashAll(int times, int delayMs) {

  animationRunning = true;

  for (int t = 0; t < times; t++) {

    shift16((0x00 << 8) | 0xFF);
    responsiveDelay(delayMs);

    shift16((0xFF << 8) | 0x00);
    responsiveDelay(delayMs);
  }

  animationRunning = false;
}

void spiralWin() {
  int left = 0, right = 7, top = 0, bottom = 7;

  while (left <= right && top <= bottom) {

    for (int x = left; x <= right; x++) {
      shift16(((uint16_t) ~(1 << top) << 8) | (1 << x));
      delay(60);
    }
    top++;

    for (int y = top; y <= bottom; y++) {
      shift16(((uint16_t) ~(1 << y) << 8) | (1 << right));
      delay(60);
    }
    right--;

    if (top <= bottom) {
      for (int x = right; x >= left; x--) {
        shift16(((uint16_t) ~(1 << bottom) << 8) | (1 << x));
        delay(60);
      }
      bottom--;
    }

    if (left <= right) {
      for (int y = bottom; y >= top; y--) {
        shift16(((uint16_t) ~(1 << y) << 8) | (1 << left));
        delay(60);
      }
      left++;
    }
  }
}

// -------------------- Occupied map --------------------
bool occupied[8][8];

void updateOccupiedMap() {
  // Clear map
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      occupied[y][x] = false;

  // Mark all snake segments
  for (int i = 0; i < snakeLength; i++) {
    occupied[snakeY[i]][snakeX[i]] = true;
  }
}

// -------------------- Spawn food --------------------
void spawnFood() {
  if (snakeLength >= MAX_LEN) return;

  updateOccupiedMap();

  do {
    foodX = random(8);
    foodY = random(8);
  } while (occupied[foodY][foodX]);

  foodVisible = true;
}

// -------------------- Buttons --------------------

void readButtons() {
  bool up = digitalRead(UP_BUTTON) == LOW;
  bool down = digitalRead(DOWN_BUTTON) == LOW;
  bool left = digitalRead(LEFT_BUTTON) == LOW;
  bool right = digitalRead(RIGHT_BUTTON) == LOW;

  // ----------------- 1. Autopilot termination -----------------
  if (autopilot) {
    if (up && dir != DOWN) {
      autopilot = false;
      nextDir = UP;
      return;
    }
    if (down && dir != UP) {
      autopilot = false;
      nextDir = DOWN;
      return;
    }
    if (left && dir != RIGHT) {
      autopilot = false;
      nextDir = LEFT;
      return;
    }
    if (right && dir != LEFT) {
      autopilot = false;
      nextDir = RIGHT;
      return;
    }
  }

  // ----------------- 2. Game not started -----------------
  if (!gameStarted) {

    // --- Autopilot activation: detect all 4 buttons pressed ---
    if (!autopilot && up && down && left && right) {
      allPressedBefore = true;
      return;  // wait for release
    }

    // --- Normal start: detect release of any button after at least one press ---
    if (!up && !down && !left && !right) {
      if (allPressedBefore) {
        // all buttons were pressed, now released => start autopilot
        autopilot = true;
        firstInputHandled = true;
        allPressedBefore = false;
        gameStarted = true;
        return;
      }
      if (firstButtonPressed) {
        // any single button was pressed before => normal game start
        gameStarted = true;
        return;
      }
    }

    // --- Record first button pressed for normal start ---
    if (!allPressedBefore && !firstButtonPressed) {
      if (up || down || left || right) {
        firstButtonPressed = true;

        // Set initial direction for normal start
        if (up) {
          dir = UP;
          nextDir = UP;
        }
        if (down) {
          dir = DOWN;
          nextDir = DOWN;
        }
        if (left) {
          dir = LEFT;
          nextDir = LEFT;
        }
        if (right) {
          dir = RIGHT;
          nextDir = RIGHT;
        }
      }
    }

    return;  // skip game started logic
  }

  // ----------------- 3. Game started: normal directional control -----------------
  if (up && dir != DOWN) nextDir = UP;
  else if (down && dir != UP) nextDir = DOWN;
  else if (left && dir != RIGHT) nextDir = LEFT;
  else if (right && dir != LEFT) nextDir = RIGHT;
}

// -------------------- Framebuffer --------------------
void updateFramebuffer() {
  // Clear framebuffer
  for (int i = 0; i < 8; i++) {
    framebuffer[i] = 0;
    headMask[i] = 0;
  }

  // Draw body first
  for (int i = snakeLength - 1; i >= 1; i--) {  // start from tail
    framebuffer[snakeY[i]] |= (1 << snakeX[i]);
  }

  // Draw head
  headMask[snakeY[0]] |= (1 << snakeX[0]);
}

// -------------------- Movement --------------------
void moveSnake() {

  if (autopilot) {
    dir = autopilotDirection();
    nextDir = dir;  // keep them synced
  } else {
    dir = nextDir;  // normal manual control
  }

  int newX = snakeX[0];
  int newY = snakeY[0];

  // Move head according to direction
  switch (dir) {
    case UP: newY--; break;
    case DOWN: newY++; break;
    case LEFT: newX--; break;
    case RIGHT: newX++; break;
  }

  if (newX < 0 || newX > 7 || newY < 0 || newY > 7) {
    flashAll(10, 200);
    gameStarted = false;
    resetGame();

    return;
  }

  // WIN CONDITION
  if (snakeLength >= MAX_LEN - 1) {
    if (snakeLength == 63) {
      pulseCounter();
    }
    flashAll(50, 50);
    spiralWin();
    gameStarted = false;
    resetGame();
    return;
  }

  bool grow = (newX == foodX && newY == foodY);

  // Check collision with self
  int checkLength = grow ? snakeLength : snakeLength - 1;
  for (int i = 0; i < checkLength; i++) {
    if (snakeX[i] == newX && snakeY[i] == newY) {
      flashAll(10, 200);
      gameStarted = false;
      resetGame();
      return;
    }
  }

  // Save old tail
  int oldTailX = snakeX[snakeLength - 1];
  int oldTailY = snakeY[snakeLength - 1];

  // Shift body
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }

  // Move head
  snakeX[0] = newX;
  snakeY[0] = newY;

  if (grow) {
    snakeLength++;
    pulseCounter();

    // Set new tail explicitly
    snakeX[snakeLength - 1] = oldTailX;
    snakeY[snakeLength - 1] = oldTailY;

    updateOccupiedMap();
    spawnFood();
  }
}

// -------------------- Matrix Scan --------------------
void scanMatrix() {
  const int pwmLevels = 10;
  const int bodyOnSteps = 3;

  for (int r = 0; r < 8; r++) {
    uint8_t rowByte = 0xFF;
    rowByte &= ~(1 << r);

    for (int pwm = 0; pwm < pwmLevels; pwm++) {
      uint8_t cols = headMask[r];

      if (pwm < bodyOnSteps) cols |= framebuffer[r];

      // Food fully ON
      if (foodVisible && foodY == r) cols |= (1 << foodX);

      uint16_t frame = ((uint16_t)rowByte << 8) | cols;
      shift16(frame);

      delayMicroseconds(200);
    }
  }
}

// -------------------- Shift --------------------
void shift16(uint16_t value) {
  digitalWrite(LATCH, LOW);
  for (int i = 15; i >= 0; i--) {
    digitalWrite(CLOCK, LOW);
    digitalWrite(DATA, (value >> i) & 1);
    digitalWrite(CLOCK, HIGH);
  }
  digitalWrite(LATCH, HIGH);
}

// ------------ Gamemode Switching -------------

void applyMode() {

  switch (currentMode) {

    case EASY:
      moveInterval = 500;  // 2Hz
      break;

    case NORMAL:
      moveInterval = 300;  // ~3Hz
      break;

    case HARD:
      moveInterval = 200;  // 5Hz
      break;

    case LUDICROUS:
      moveInterval = 100;  // 10Hz
      break;
  }
}

// ------- Gamemode LED Indicators ----------------

void updateModeLEDs() {

  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_WHITE, HIGH);
  digitalWrite(LED_YELLOW, HIGH);
  digitalWrite(LED_RED, HIGH);

  if (currentMode >= EASY) digitalWrite(LED_GREEN, LOW);
  if (currentMode >= NORMAL) digitalWrite(LED_WHITE, LOW);
  if (currentMode >= HARD) digitalWrite(LED_YELLOW, LOW);
  if (currentMode >= LUDICROUS) digitalWrite(LED_RED, LOW);
}

// --------------- Gamemode Cycling & EEPROM Saving -------------

void cycleMode() {

  currentMode = (GameMode)((currentMode + 1) % 4);

  applyMode();
  updateModeLEDs();

  EEPROM.write(0, currentMode);
  EEPROM.commit();
}

// -------------- Reading Gamemode Switching Button -------------------

void readModeButton() {

  if (gameStarted && !animationRunning) return;

  if (digitalRead(MODE_BUTTON) == LOW) {

    if (millis() - lastModePress > 300) {

      lastModePress = millis();
      cycleMode();
    }
  }
}

// -------------------- Setup --------------------
void setup() {
  pinMode(DATA, OUTPUT);
  pinMode(CLOCK, OUTPUT);
  pinMode(LATCH, OUTPUT);

  pinMode(UP_BUTTON, INPUT_PULLUP);
  pinMode(DOWN_BUTTON, INPUT_PULLUP);
  pinMode(LEFT_BUTTON, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON, INPUT_PULLUP);

  pinMode(LS90_CLOCK, OUTPUT);
  pinMode(LS90_RESET, OUTPUT);
  digitalWrite(LS90_CLOCK, LOW);
  digitalWrite(LS90_RESET, LOW);

  pinMode(MODE_BUTTON, INPUT_PULLUP);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_WHITE, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  EEPROM.begin(8);

  uint8_t savedMode = EEPROM.read(0);

  if (savedMode <= LUDICROUS) {
    currentMode = (GameMode)savedMode;
  } else {
    currentMode = NORMAL;
  }

  applyMode();
  updateModeLEDs();

  randomSeed(analogRead(34));

  snakeX[0] = 3;
  snakeY[0] = 3;
  snakeLength = 1;
  autopilot = false;
  autopilotAligned = false;
  firstInputHandled = false;
  pulseCounter();

  spawnFood();
}

// -------------------- Loop --------------------
unsigned long lastMove = 0;

void loop() {
  readButtons();
  readModeButton();

  // Blink food
  if (millis() - lastBlink > blinkInterval) {
    lastBlink = millis();
    foodVisible = !foodVisible;
  }

  if (gameStarted && millis() - lastMove > moveInterval) {
    lastMove = millis();
    moveSnake();
  }

  updateFramebuffer();
  scanMatrix();
}



// C1R1,C2R1,C3R1,C4R1,C5R1,C6R1,C7R1,C8R1
// C1R2,C2R2,C3R2,C4R2,C5R2,C6R2,C7R2,C8R2
// C1R3,C2R3,C3R3,C4R3,C5R3,C6R3,C7R3,C8R3
// C1R4,C2R4,C3R4,C4R4,C5R4,C6R4,C7R4,C8R4
// C1R5,C2R5,C3R5,C4R5,C5R5,C6R5,C7R5,C8R5
// C1R6,C2R6,C3R6,C4R6,C5R6,C6R6,C7R6,C8R6
// C1R7,C2R7,C3R7,C4R7,C5R7,C6R7,C7R7,C8R7
// C1R8,C2R8,C3R8,C4R8,C5R8,C6R8,C7R8,C8R8