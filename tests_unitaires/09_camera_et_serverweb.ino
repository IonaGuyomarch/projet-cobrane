#include "esp_camera.h"
#include "WiFi.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
// AI Thinker pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;
// Page HTML avec contrôles
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial; text-align: center; background: #1a1a2e; color: #eee; margin: 0; padding: 10px; }
    h1 { color: #e94560; }
    img { width: 100%%; max-width: 800px; border-radius: 8px; margin: 10px 0; }
    .controls { max-width: 800px; margin: 0 auto; text-align: left; padding: 10px; }
    .ctrl { display: flex; align-items: center; margin: 8px 0; }
    .ctrl label { width: 140px; font-size: 14px; }
    .ctrl input[type=range] { flex: 1; }
    .ctrl span { width: 40px; text-align: center; font-size: 14px; }
    .ctrl select { flex: 1; padding: 4px; }
    .btn { background: #e94560; color: white; border: none; padding: 8px 20px; border-radius: 5px; cursor: pointer; margin: 5px; font-size: 14px; }
    .btn:hover { background: #c81e45; }
  </style>
</head>
<body>
  <h1>ESP32-CAM</h1>
  <img id="stream" src="">
  <div>
    <button class="btn" onclick="startStream()">Start Stream</button>
    <button class="btn" onclick="stopStream()">Stop Stream</button>
  </div>
  <div class="controls">
    <h3>Resolution</h3>
    <div class="ctrl">
      <label>Taille</label>
      <select id="framesize" onchange="setSetting('framesize', this.value)">
        <option value="3">HQVGA 240x176</option>
        <option value="5">QVGA 320x240</option>
        <option value="6">CIF 400x296</option>
        <option value="8" selected>VGA 640x480</option>
        <option value="9">SVGA 800x600</option>
        <option value="10">XGA 1024x768</option>
        <option value="12">SXGA 1280x1024</option>
        <option value="13">UXGA 1600x1200</option>
      </select>
    </div>
    <div class="ctrl">
      <label>Qualite JPEG</label>
      <input type="range" id="quality" min="4" max="63" value="12" onchange="setSetting('quality', this.value)">
      <span id="quality_val">12</span>
    </div>
    <h3>Image</h3>
    <div class="ctrl">
      <label>Luminosite</label>
      <input type="range" id="brightness" min="-2" max="2" value="0" onchange="setSetting('brightness', this.value)">
      <span id="brightness_val">0</span>
    </div>
    <div class="ctrl">
      <label>Contraste</label>
      <input type="range" id="contrast" min="-2" max="2" value="0" onchange="setSetting('contrast', this.value)">
      <span id="contrast_val">0</span>
    </div>
    <div class="ctrl">
      <label>Saturation</label>
      <input type="range" id="saturation" min="-2" max="2" value="0" onchange="setSetting('saturation', this.value)">
      <span id="saturation_val">0</span>
    </div>
    <div class="ctrl">
      <label>Nettete</label>
      <input type="range" id="sharpness" min="-2" max="2" value="0" onchange="setSetting('sharpness', this.value)">
      <span id="sharpness_val">0</span>
    </div>
    <h3>Exposition</h3>
    <div class="ctrl">
      <label>AEC (auto)</label>
      <select id="aec" onchange="setSetting('aec', this.value)">
        <option value="1" selected>ON</option>
        <option value="0">OFF</option>
      </select>
    </div>
    <div class="ctrl">
      <label>Exposure</label>
      <input type="range" id="aec_value" min="0" max="1200" value="300" onchange="setSetting('aec_value', this.value)">
      <span id="aec_value_val">300</span>
    </div>
    <div class="ctrl">
      <label>Gain (AGC)</label>
      <select id="agc" onchange="setSetting('agc', this.value)">
        <option value="1" selected>ON</option>
        <option value="0">OFF</option>
      </select>
    </div>
    <div class="ctrl">
      <label>Gain manual</label>
      <input type="range" id="agc_gain" min="0" max="30" value="0" onchange="setSetting('agc_gain', this.value)">
      <span id="agc_gain_val">0</span>
    </div>
    <h3>Effets</h3>
    <div class="ctrl">
      <label>Effet</label>
      <select id="special_effect" onchange="setSetting('special_effect', this.value)">
        <option value="0" selected>Normal</option>
        <option value="1">Negatif</option>
        <option value="2">Noir & Blanc</option>
        <option value="3">Rouge</option>
        <option value="4">Vert</option>
        <option value="5">Bleu</option>
        <option value="6">Sepia</option>
      </select>
    </div>
    <div class="ctrl">
      <label>H-Mirror</label>
      <select id="hmirror" onchange="setSetting('hmirror', this.value)">
        <option value="0">OFF</option>
        <option value="1" selected>ON</option>
      </select>
    </div>
    <div class="ctrl">
      <label>V-Flip</label>
      <select id="vflip" onchange="setSetting('vflip', this.value)">
        <option value="0" selected>OFF</option>
        <option value="1">ON</option>
      </select>
    </div>
    <div class="ctrl">
      <label>AWB (blanc)</label>
      <select id="awb" onchange="setSetting('awb', this.value)">
        <option value="1" selected>ON</option>
        <option value="0">OFF</option>
      </select>
    </div>
  </div>
  <script>
    function startStream() {
      document.getElementById('stream').src = 'http://' + location.hostname + ':81/stream';
    }
    function stopStream() {
      document.getElementById('stream').src = '';
    }
    function setSetting(key, val) {
      var s = document.getElementById(key + '_val');
      if (s) s.innerHTML = val;
      fetch('/control?var=' + key + '&val=' + val);
    }
    window.onload = startStream;
  </script>
</body>
</html>
)rawliteral";
// Handler page HTML
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}
// Handler contrôles caméra
static esp_err_t control_handler(httpd_req_t *req) {
  char buf[100];
  char variable[32] = {0};
  char value[32] = {0};
  int buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1 && buf_len <= 100) {
    httpd_req_get_url_query_str(req, buf, buf_len);
    httpd_query_key_value(buf, "var", variable, sizeof(variable));
    httpd_query_key_value(buf, "val", value, sizeof(value));
  }
  int val = atoi(value);
  sensor_t *s = esp_camera_sensor_get();
  if (!strcmp(variable, "framesize"))     s->set_framesize(s, (framesize_t)val);
  else if (!strcmp(variable, "quality"))  s->set_quality(s, val);
  else if (!strcmp(variable, "brightness")) s->set_brightness(s, val);
  else if (!strcmp(variable, "contrast")) s->set_contrast(s, val);
  else if (!strcmp(variable, "saturation")) s->set_saturation(s, val);
  else if (!strcmp(variable, "sharpness")) s->set_sharpness(s, val);
  else if (!strcmp(variable, "aec"))      s->set_exposure_ctrl(s, val);
  else if (!strcmp(variable, "aec_value")) s->set_aec_value(s, val);
  else if (!strcmp(variable, "agc"))      s->set_gain_ctrl(s, val);
  else if (!strcmp(variable, "agc_gain")) s->set_agc_gain(s, val);
  else if (!strcmp(variable, "special_effect")) s->set_special_effect(s, val);
  else if (!strcmp(variable, "hmirror"))  s->set_hmirror(s, val);
  else if (!strcmp(variable, "vflip"))    s->set_vflip(s, val);
  else if (!strcmp(variable, "awb"))      s->set_whitebal(s, val);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}
// Handler stream
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }
    size_t hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK)
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
  }
  return res;
}
void startServers() {
  // Serveur page web (port 80)
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t control_uri = { .uri = "/control", .method = HTTP_GET, .handler = control_handler, .user_ctx = NULL };
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &control_uri);
    Serial.println("Web server OK (port 80)");
  }
  // Serveur stream (port 81)
  config.server_port = 81;
  config.ctrl_port = 32769;
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Stream server OK (port 81)");
  }
}
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(3000);
  Serial.println("START");
  Serial.flush();
  pinMode(32, OUTPUT);
  digitalWrite(32, LOW);
  delay(500);
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 8000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 2;
  esp_err_t err = esp_camera_init(&config);
  Serial.printf("Camera init: 0x%x\n", err);
  Serial.flush();
  if (err != ESP_OK) return;
  WiFi.softAP("ESP32-CAM", "12345678");
  delay(2000);
  Serial.print("Page: http://");
  Serial.println(WiFi.softAPIP());
  Serial.flush();
  delay(500);
  startServers();
}
void loop() { delay(1000); }