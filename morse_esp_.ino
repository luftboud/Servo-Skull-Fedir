#define LED_PIN 14

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(9600);
}

struct Morse {
  char chr;
  const char *code;
};

Morse morseTable[] = { {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
  {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"},
  {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, 
  {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"},
  {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."} };

void blink_char(const char *code) {
  int k = 0;
  while (code[k] != '\0') {
    if (code[k] == '-') {
      digitalWrite(LED_PIN, HIGH);
      delay(400);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    } else if (code[k] == '.') {
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
    ++k;
  }
}

void morse() {
  String message = Serial.readStringUntil('\n');
  int len = message.length();

  for (int i = 0; i < len; ++i) {
    char c = message[i];
    c = toupper(c);
    for (int j = 0; j < 26; ++j) {
      if (c == morseTable[j].chr){
        const char *code = morseTable[j].code;
        blink_char(code);
        delay(500);
      }
    }
  }
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  
}
