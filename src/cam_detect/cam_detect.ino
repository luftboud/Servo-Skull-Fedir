#include "esp_camera.h"


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
// --------------------------------------------------------------------

#define LED_PIN 14



#define LED_ACTIVE_HIGH true


#define FRAME_GAP_MS 80
#define STARTUP_SAMPLES 25
#define MARGIN 12000UL

inline void setLed(bool on){
  digitalWrite(LED_PIN, (LED_ACTIVE_HIGH ? (on?HIGH:LOW) : (on?LOW:HIGH)));
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

void setup(){
  pinMode(LED_PIN, OUTPUT);
  setLed(false);
  Serial.begin(115200);

  if(!cam_init()){
    Serial.println("Camera init FAILED (шлейф? пін-мап?)");
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
  Serial.printf("Baseline diff=%u\n", baseline);


  setLed(true); delay(150); setLed(false);
}

void loop(){
  static uint32_t ema = 0;
  const uint8_t alpha = 20;

  camera_fb_t* f1 = esp_camera_fb_get();
  delay(FRAME_GAP_MS);
  camera_fb_t* f2 = esp_camera_fb_get();

  if(f1 && f2){
    uint32_t d = diff_fb(f1,f2);

    ema = (ema==0) ? d : ( ( (uint64_t)ema*(255-alpha) + (uint64_t)d*alpha ) / 255 );
    bool motion = d > (ema + MARGIN);

    setLed(motion);
    Serial.printf("diff=%u  ema=%u  thr=%u  -> %s\n", d, ema, (ema+MARGIN), motion?"MOTION":"idle");
  }else{
    Serial.println("frame grab failed");
  }
  if(f1) esp_camera_fb_return(f1);
  if(f2) esp_camera_fb_return(f2);
}
