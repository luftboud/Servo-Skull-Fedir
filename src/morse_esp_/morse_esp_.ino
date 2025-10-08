#include "esp_camera.h"

// Camera pins
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1
#define CAM_PIN_XCLK   21
#define CAM_PIN_SIOD   26
#define CAM_PIN_SIOC   27
#define CAM_PIN_D7     35
#define CAM_PIN_D6     34
#define CAM_PIN_D5     39
#define CAM_PIN_D4     36
#define CAM_PIN_D3     19
#define CAM_PIN_D2     18
#define CAM_PIN_D1      5
#define CAM_PIN_D0      4
#define CAM_PIN_VSYNC  25
#define CAM_PIN_HREF   23
#define CAM_PIN_PCLK   22
// Led pins
#define LED_PIN 14

#define SWITCH_PIN 12

#define LED_ACTIVE_HIGH true

#define FRAME_GAP_MS 80
#define STARTUP_SAMPLES 25
#define MARGIN 12000UL


enum Mode {
  MODE_IDLE,
  MODE_MORSE,
  MODE_MOTION,
  MODE_DEBUG,
  MODE_COUNT
};

Mode currentMode = MODE_IDLE;

bool lastButtonState = HIGH;        
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;
unsigned long lastMutter = 0;
unsigned long lastNoSightMutter = 0;
const unsigned long noSightMutterDelay = 30000; 
unsigned long lastSightALARM = 0;
const unsigned long SightALARMDelay = 2000; 

struct Morse {
  char chr;
  const char *code;
};

Morse morseTable[] = { {'A', ".-"}, {'B', "-..."}, {'C', "-.-."}, {'D', "-.."}, {'E', "."},
  {'F', "..-."}, {'G', "--."}, {'H', "...."}, {'I', ".."}, {'J', ".---"}, {'K', "-.-"},
  {'L', ".-.."}, {'M', "--"}, {'N', "-."}, {'O', "---"}, {'P', ".--."}, {'Q', "--.-"}, 
  {'R', ".-."}, {'S', "..."}, {'T', "-"}, {'U', "..-"}, {'V', "...-"}, {'W', ".--"},
  {'X', "-..-"}, {'Y', "-.--"}, {'Z', "--.."}
};

void printBootSequence() {
  Serial.println("+++INITIATING SACRED WAKE PROTOCOL+++");
  Serial.println("> Servitor cranial unit: MODEL XV-42 “Cogitatus Minor”");
  Serial.println("> Machine spirit stirring... [OK]");
  Serial.println("> Vocal subroutines... [ACTIVE]");
  Serial.println("> Emperor's light detected... [BLESSINGS CONFIRMED]");
  Serial.println("+++SYSTEMS ONLINE+++");
  Serial.println("+++AVE IMPERATOR, FLESHLING+++");
  Serial.println("> MODE CONTROL: Tap the SANCTIFIED BUTTON to cycle.");
  Serial.println("> Sequence: [IDLE → MORSE → MOTION → IDLE]");
  Serial.println("> Hold 2s: perform minor benediction & debounce heresy.");
  Serial.println("+++CATECHISM OF OPERATIONAL RITES+++");
  Serial.println("> MODE: IDLE");
  Serial.println("> Function: Standby vigil & ambience.");
  Serial.println("> MODE: MORSE");
  Serial.println("> Function: Transmit signals in holy Morse (binharic taps).");
  Serial.println("> MODE: MOTION");
  Serial.println("> Function: Auspex motion watch & pict-capture.");
  Serial.println("+++BY THE OMNISSIAH, I STAND READY+++");
  Serial.println("> State your command, magos. Flesh is weak; service is eternal.");
  Serial.println();
}

const char* motionDetectedPhrases[] = {
  "> ALERT: Heretical movement detected!",
  "> Blessed optics have seen the unworthy.",
  "> Motion trace acquired. Engaging scrutiny subroutine.",
  "> Intruder presence confirmed. Purge protocols prepared.",
  "> Emperor's light reveals the moving shadow...",
  "> Data stream anomaly — organic movement verified."
};
const int numMotionDetected = sizeof(motionDetectedPhrases) / sizeof(motionDetectedPhrases[0]);

const char* noMotionPhrases[] = {
  "> Scanning... no motion in the Emperor's sight.",
  "> Optic spirit watches in silence.",
  "> Stillness detected. Continuing vigilance.",
  "> All quiet in the servo-spirits' domain.",
  "> No movement... recalibrating photoreceptors."
};
const int numNoMotion = sizeof(noMotionPhrases) / sizeof(noMotionPhrases[0]);

const char* ambientPhrases[] = {
  "> Servos humming... awaiting divine purpose.",
  "> I see the shadow of motion... shall I smite it?",
  "> Omnissiah preserve my circuits.",
  "> Faith in the code. Purge in the light.",
  "> Machine spirit murmurs in binary prayer...",
  "> Memory checksum: stable.",
  "> Recalibrating ocular lenses...",
  "> Listening for the Emperor's vox..."
};
const int numPhrases = sizeof(ambientPhrases) / sizeof(ambientPhrases[0]);

void announceMode() {
  Serial.println(">> Mode switch detected.");
  Serial.println(">> Sanctified subroutine realignment in progress...");

  switch (currentMode) {
    case MODE_IDLE:
      Serial.println("[MODE: IDLE]");
      Serial.println("> Awaiting new orders from superior intellect.");
      Serial.println("> Remaining vigilant. Whispering prayers to the Omnissiah.");
      break;
    case MODE_MORSE:
      Serial.println("[MODE: LEXICON TRANSMISSION]");
      Serial.println("> Translating incoming vox-text to sacred light-code.");
      Serial.println("> Engaging lumen diode... blinking praise to the Emperor.");
      break;
    case MODE_MOTION:
      Serial.println("[MODE: MOTION AUGUR]");
      Serial.println("> Optic spirit online. Searching for heretical movement...");
      Serial.println("> Surveillance purity: 99.7%.");
      break;
  }
  Serial.println();
}

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

bool cam_init() {
  camera_config_t c = {};
  c.pin_pwdn = CAM_PIN_PWDN;  c.pin_reset = CAM_PIN_RESET;
  c.pin_xclk = CAM_PIN_XCLK;  c.pin_sccb_sda = CAM_PIN_SIOD; c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_d7 = CAM_PIN_D7; c.pin_d6 = CAM_PIN_D6; c.pin_d5 = CAM_PIN_D5; c.pin_d4 = CAM_PIN_D4;
  c.pin_d3 = CAM_PIN_D3; c.pin_d2 = CAM_PIN_D2; c.pin_d1 = CAM_PIN_D1; c.pin_d0 = CAM_PIN_D0;
  c.pin_vsync = CAM_PIN_VSYNC; c.pin_href = CAM_PIN_HREF; c.pin_pclk = CAM_PIN_PCLK;

  c.ledc_timer = LEDC_TIMER_0;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_GRAYSCALE;
  c.frame_size   = FRAMESIZE_QQVGA;
  c.jpeg_quality = 12;
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;
  return esp_camera_init(&c) == ESP_OK;
}

uint32_t diff_fb(const camera_fb_t* a, const camera_fb_t* b){
  if(!a || !b || a->len != b->len) return 0;
  uint32_t d=0;
  for(size_t i=0;i<a->len;i+=8){ int t=int(a->buf[i])-int(b->buf[i]); d += (t<0?-t:t); }
  return d;
}

void cam_setup() {
  if(!cam_init()){
    Serial.println("Camera init FAILED");
    while(true){ delay(1000); }
  }
  Serial.println("Camera OK");


  camera_fb_t *f1=nullptr,*f2=nullptr;
  uint64_t acc=0; int n=0;
  for(int i=0;i<STARTUP_SAMPLES;i++){
    f1 = esp_camera_fb_get(); delay(FRAME_GAP_MS); f2 = esp_camera_fb_get();
    if(f1 && f2){ acc += diff_fb(f1,f2); n++; }
    if(f1) esp_camera_fb_return(f1); if(f2) esp_camera_fb_return(f2);
  }
  uint32_t baseline = (n>0) ? (uint32_t)(acc / n) : 0;
  // Serial.printf("Baseline diff=%u\n", baseline);
}

void camera_loop() {
  static uint32_t ema = 0;
  const uint8_t alpha = 20;

  camera_fb_t* f1 = esp_camera_fb_get();
  delay(FRAME_GAP_MS);
  camera_fb_t* f2 = esp_camera_fb_get();

  if(f1 && f2){
    uint32_t d = diff_fb(f1,f2);

    ema = (ema==0) ? d : ( ( (uint64_t)ema*(255-alpha) + (uint64_t)d*alpha ) / 255 );
    bool motion = d > (ema + MARGIN);

    if (motion) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    // Serial.printf("diff=%u  ema=%u  thr=%u  -> %s\n", d, ema, (ema+MARGIN), motion?"MOTION":"idle");
    if (motion && (millis() - lastSightALARM) >= SightALARMDelay ) {
      Serial.println(motionDetectedPhrases[random(numMotionDetected)]);
      lastSightALARM = millis();
    } else if ((millis() - lastNoSightMutter) >= noSightMutterDelay) {
      Serial.println(noMotionPhrases[random(numNoMotion)]);
      lastNoSightMutter = millis();
    }
  }else{
    Serial.println("frame grab failed");
  }
  if(f1) esp_camera_fb_return(f1);
  if(f2) esp_camera_fb_return(f2);
}

void setup(){
  pinMode(LED_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  Serial.begin(115200);

  cam_setup();

  printBootSequence();
}

void loop(){
  int reading = digitalRead(SWITCH_PIN);

  if (reading != lastButtonState && (millis() - lastDebounceTime) > debounceDelay) {
    if (lastButtonState == LOW && reading == HIGH) {
      currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
      lastDebounceTime = millis();
      announceMode();

      digitalWrite(LED_PIN, HIGH);
      delay(150);
      digitalWrite(LED_PIN, LOW);
    }
  }

  lastButtonState = reading;

  runCurrentMode();
}

void runCurrentMode() {
  switch (currentMode) {
    case MODE_IDLE:
      digitalWrite(LED_PIN, LOW);
      if (millis() - lastMutter > random(20000, 30000)) {
        Serial.println(ambientPhrases[random(numPhrases)]);
        lastMutter = millis();
      }
      break;
    case MODE_MORSE:
      morse();
      break;
    case MODE_MOTION:
      camera_loop();
      break;
  }
}