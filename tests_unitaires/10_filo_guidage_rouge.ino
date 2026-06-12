#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "img_converters.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ===== PINS AI-THINKER =====
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

// ===== IMAGE =====
#define IMG_W 320
#define IMG_H 240

// ===== WIFI =====
#define USE_AP true

const char* WIFI_SSID = "TON_SSID";
const char* WIFI_PASS = "TON_MOTDEPASSE";

const char* AP_SSID = "FILOGUIDAGE";
const char* AP_PASS = "12345678";

// ===== FILTRES DISPONIBLES =====
// 0 = Rouge dominance
// 1 = Rouge ratio
// 2 = Rouge vs max vert/bleu
// 3 = HSV rouge
// 4 = Rouge strict
// 5 = Rouge chroma
// 6 = Bleu dominance
// 7 = Vert dominance
// 8 = Ligne sombre
// 9 = Ligne claire
// 10 = Saturation couleur

// ===== MODES AFFICHAGE =====
// 0 = Camera normale + overlay
// 1 = Masque noir/blanc
// 2 = Heatmap du filtre

// ===== PARAMETRES REGLABLES DEPUIS LE WEB =====
volatile int g_filter_mode    = 0;
volatile int g_view_mode      = 0;

volatile int g_min_contrast   = 35;
volatile int g_thresh_bias    = 0;
volatile int g_preview_thresh = 150;

volatile int g_min_px         = 3;
volatile int g_max_width      = 120;
volatile int g_min_rows       = 4;
volatile int g_max_spread     = 80;
volatile int g_blur_radius    = 1;

volatile int g_red_min        = 80;
volatile int g_red_delta      = 25;

// ===== RESULTAT =====
float line_pos_f = 5.0f;
int   line_pos = 5;
int   confidence = 0;
int   match_count = 0;

int debug_contrast = 0;
int debug_thresh = 0;
int debug_width = 0;

// ===== SERVEUR / CAMERA =====
httpd_handle_t server_http = NULL;
httpd_handle_t stream_http = NULL;
SemaphoreHandle_t cam_mutex = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE =
  "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;

static const char* _STREAM_BOUNDARY =
  "\r\n--" PART_BOUNDARY "\r\n";

static const char* _STREAM_PART =
  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ===== OUTILS =====
static inline int clampi(int v, int mn, int mx) {
  if (v < mn) return mn;
  if (v > mx) return mx;
  return v;
}

static inline uint8_t clamp255(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

static inline int imax3(int a, int b, int c) {
  int m = a;
  if (b > m) m = b;
  if (c > m) m = c;
  return m;
}

static inline int imin3(int a, int b, int c) {
  int m = a;
  if (b < m) m = b;
  if (c < m) m = c;
  return m;
}

static inline uint8_t map_signed_255(int s) {
  int v = (s + 255) * 255 / 510;
  return clamp255(v);
}

// ===== RGB565 -> RGB =====
void rgb565_to_rgb(uint16_t px, uint8_t *r, uint8_t *g, uint8_t *b) {
  px = (px >> 8) | (px << 8);

  uint8_t r5 = (px >> 11) & 0x1F;
  uint8_t g6 = (px >> 5)  & 0x3F;
  uint8_t b5 =  px        & 0x1F;

  *r = (r5 << 3) | (r5 >> 2);
  *g = (g6 << 2) | (g6 >> 4);
  *b = (b5 << 3) | (b5 >> 2);
}

// ===== RGB -> RGB565 BUFFER CAMERA =====
static inline uint16_t mk565(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t v = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  return (uint16_t)((v >> 8) | (v << 8));
}

// ===== SCORE PIXEL SELON FILTRE =====
uint8_t pixel_score(uint16_t px, int mode) {
  uint8_t rr, gg, bb;
  rgb565_to_rgb(px, &rr, &gg, &bb);

  int r = rr;
  int g = gg;
  int b = bb;

  if (mode == 0) {
    // Rouge dominance simple
    int s = r - (g + b) / 2;
    return map_signed_255(s);
  }

  if (mode == 1) {
    // Ratio rouge : pratique si la lumiere change
    int sum = r + g + b;
    if (sum <= 0) return 0;
    return clamp255((r * 255) / sum);
  }

  if (mode == 2) {
    // Rouge contre le plus fort entre vert et bleu
    int m = g;
    if (b > m) m = b;
    int s = r - m;
    return map_signed_255(s);
  }

  if (mode == 3) {
    // HSV rouge approximatif
    int maxv = imax3(r, g, b);
    int minv = imin3(r, g, b);
    int delta = maxv - minv;

    if (maxv < 20) return 0;
    if (delta < 8) return 0;
    if (maxv != r) return 0;

    int hue;
    hue = 60 * (g - b) / delta;
    if (hue < 0) hue += 360;

    int dist = hue;
    if (360 - hue < dist) dist = 360 - hue;

    if (dist > 45) return 0;

    int hue_score = (45 - dist) * 255 / 45;
    int sat = delta * 255 / maxv;

    return clamp255((hue_score * sat) / 255);
  }

  if (mode == 4) {
    // Rouge strict avec seuils reglables
    int red_min = g_red_min;
    int red_delta = g_red_delta;

    if (r >= red_min && r >= g + red_delta && r >= b + red_delta) {
      return 255;
    } else {
      return 0;
    }
  }

  if (mode == 5) {
    // Chroma rouge : 2R - G - B
    int s = 2 * r - g - b;
    int v = (s + 510) * 255 / 1020;
    return clamp255(v);
  }

  if (mode == 6) {
    // Bleu dominance
    int s = b - (r + g) / 2;
    return map_signed_255(s);
  }

  if (mode == 7) {
    // Vert dominance
    int s = g - (r + b) / 2;
    return map_signed_255(s);
  }

  if (mode == 8) {
    // Ligne sombre
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    return clamp255(255 - lum);
  }

  if (mode == 9) {
    // Ligne claire
    int lum = (r * 30 + g * 59 + b * 11) / 100;
    return clamp255(lum);
  }

  if (mode == 10) {
    // Saturation / couleur vive
    int maxv = imax3(r, g, b);
    int minv = imin3(r, g, b);
    return clamp255(maxv - minv);
  }

  return 0;
}

// ===== OTSU =====
uint8_t otsu(uint8_t *data, int n) {
  int hist[64] = {0};

  for (int i = 0; i < n; i++) {
    hist[data[i] >> 2]++;
  }

  float sum_all = 0;
  for (int i = 0; i < 64; i++) {
    sum_all += i * hist[i];
  }

  int best_t = 32;
  float best_var = 0;
  int w0 = 0;
  float sum0 = 0;

  for (int t = 0; t < 63; t++) {
    w0 += hist[t];
    if (w0 == 0) continue;

    int w1 = n - w0;
    if (w1 == 0) break;

    sum0 += t * hist[t];

    float m0 = sum0 / w0;
    float m1 = (sum_all - sum0) / w1;
    float var = (float)w0 * w1 * (m0 - m1) * (m0 - m1);

    if (var > best_var) {
      best_var = var;
      best_t = t;
    }
  }

  return (uint8_t)(best_t << 2);
}

// ===== PETIT FLOU SUR UNE LIGNE DE SCORE =====
void blur_scores(uint8_t *scores, int w, int radius) {
  if (radius <= 0) return;
  if (radius > 4) radius = 4;

  static uint8_t tmp[IMG_W];

  for (int x = 0; x < w; x++) {
    int sum = 0;
    int cnt = 0;

    for (int d = -radius; d <= radius; d++) {
      int xx = x + d;
      if (xx < 0 || xx >= w) continue;
      sum += scores[xx];
      cnt++;
    }

    tmp[x] = sum / cnt;
  }

  for (int x = 0; x < w; x++) {
    scores[x] = tmp[x];
  }
}

// ===== TROUVER LE FIL SUR UNE LIGNE =====
int find_line_x(
  uint8_t *scores,
  int w,
  int *out_start,
  int *out_len,
  int *out_thresh,
  int *out_contrast
) {
  int minv = 255;
  int maxv = 0;
  long sum = 0;

  for (int x = 0; x < w; x++) {
    int v = scores[x];

    if (v < minv) minv = v;
    if (v > maxv) maxv = v;

    sum += v;
  }

  int contrast = maxv - minv;

  if (out_contrast) *out_contrast = contrast;

  if (contrast < g_min_contrast) {
    if (out_start) *out_start = -1;
    if (out_len) *out_len = 0;
    if (out_thresh) *out_thresh = 0;
    return -1;
  }

  int mean = sum / w;
  int thresh = otsu(scores, w);

  int min_thresh = mean + contrast / 4;

  if (thresh < min_thresh) thresh = min_thresh;

  thresh += g_thresh_bias;
  thresh = clampi(thresh, 0, 255);

  if (out_thresh) *out_thresh = thresh;

  int best_start = -1;
  int best_len = 0;

  int cur_start = -1;
  int cur_len = 0;

  for (int x = 0; x < w; x++) {
    if (scores[x] > thresh) {
      if (cur_start < 0) {
        cur_start = x;
        cur_len = 1;
      } else {
        cur_len++;
      }
    } else {
      if (cur_len > best_len) {
        best_len = cur_len;
        best_start = cur_start;
      }

      cur_start = -1;
      cur_len = 0;
    }
  }

  if (cur_len > best_len) {
    best_len = cur_len;
    best_start = cur_start;
  }

  if (out_start) *out_start = best_start;
  if (out_len) *out_len = best_len;

  if (best_start < 0) return -1;
  if (best_len < g_min_px) return -1;
  if (best_len > g_max_width) return -1;

  return best_start + best_len / 2;
}

// ===== DETECTION PRINCIPALE =====
void detect_line(camera_fb_t *fb) {
  if (fb->format != PIXFORMAT_RGB565) return;

  int w = fb->width;
  int h = fb->height;

  uint16_t *pixels = (uint16_t *)fb->buf;

  static uint8_t row_scores[IMG_W];

  long sum_x = 0;
  int valid_rows = 0;

  int xs[16];

  int total_contrast = 0;
  int total_thresh = 0;
  int total_width = 0;
  int total_rows_read = 0;

  int mode = g_filter_mode;
  int blur = g_blur_radius;

  for (int i = 0; i < 8; i++) {
    int y = h * (15 + i * 70 / 7) / 100;

    for (int x = 0; x < w; x++) {
      row_scores[x] = pixel_score(pixels[y * w + x], mode);
    }

    blur_scores(row_scores, w, blur);

    int st = -1;
    int len = 0;
    int th = 0;
    int cont = 0;

    int lx = find_line_x(row_scores, w, &st, &len, &th, &cont);

    total_contrast += cont;
    total_thresh += th;
    total_rows_read++;

    if (lx >= 0) {
      xs[valid_rows] = lx;
      sum_x += lx;
      total_width += len;
      valid_rows++;
    }
  }

  match_count = valid_rows;

  if (total_rows_read > 0) {
    debug_contrast = total_contrast / total_rows_read;
    debug_thresh = total_thresh / total_rows_read;
  }

  if (valid_rows > 0) {
    debug_width = total_width / valid_rows;
  } else {
    debug_width = 0;
  }

  if (valid_rows < g_min_rows) {
    confidence = 0;
    return;
  }

  int min_x = xs[0];
  int max_x = xs[0];

  for (int i = 1; i < valid_rows; i++) {
    if (xs[i] < min_x) min_x = xs[i];
    if (xs[i] > max_x) max_x = xs[i];
  }

  if ((max_x - min_x) > g_max_spread) {
    confidence = 0;
    return;
  }

  confidence = 1;

  float centroid_x = (float)sum_x / valid_rows;
  float new_pos = centroid_x * 10.0f / w;

  if (new_pos < 0) new_pos = 0;
  if (new_pos > 10) new_pos = 10;

  line_pos_f = line_pos_f * 0.70f + new_pos * 0.30f;
  line_pos = (int)(line_pos_f + 0.5f);

  if (line_pos < 0) line_pos = 0;
  if (line_pos > 10) line_pos = 10;
}

// ===== AFFICHAGE MASQUE / HEATMAP =====
void draw_filter_view(camera_fb_t *fb) {
  if (fb->format != PIXFORMAT_RGB565) return;

  int view = g_view_mode;

  if (view == 0) return;

  int mode = g_filter_mode;
  int preview_thresh = g_preview_thresh;

  uint16_t *px = (uint16_t *)fb->buf;
  int w = fb->width;
  int h = fb->height;

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t s = pixel_score(px[y * w + x], mode);

      if (view == 1) {
        if (s > preview_thresh) {
          px[y * w + x] = mk565(255, 255, 255);
        } else {
          px[y * w + x] = mk565(0, 0, 0);
        }
      } else if (view == 2) {
        px[y * w + x] = mk565(s, 0, 255 - s);
      }
    }
  }
}

// ===== OVERLAY =====
void draw_overlay(camera_fb_t *fb) {
  if (fb->format != PIXFORMAT_RGB565) return;

  uint16_t *px = (uint16_t *)fb->buf;
  int w = fb->width;
  int h = fb->height;

  uint16_t grey = mk565(90, 90, 90);
  uint16_t blue = mk565(0, 120, 255);
  uint16_t green = mk565(0, 255, 0);
  uint16_t red = mk565(255, 60, 60);

  int xc = w / 2;

  for (int y = 0; y < h; y++) {
    px[y * w + xc] = grey;
  }

  for (int i = 0; i < 8; i++) {
    int y = h * (15 + i * 70 / 7) / 100;

    for (int x = 0; x < w; x += 8) {
      px[y * w + x] = blue;
    }
  }

  if (confidence) {
    int xl = (int)(line_pos_f * w / 10.0f);

    for (int d = -1; d <= 1; d++) {
      int xx = xl + d;

      if (xx < 0 || xx >= w) continue;

      for (int y = 0; y < h; y++) {
        px[y * w + xx] = green;
      }
    }
  } else {
    // Petite croix rouge au centre si perdu
    for (int d = -12; d <= 12; d++) {
      int x1 = xc + d;
      int y1 = h / 2 + d;
      int y2 = h / 2 - d;

      if (x1 >= 0 && x1 < w && y1 >= 0 && y1 < h) {
        px[y1 * w + x1] = red;
      }

      if (x1 >= 0 && x1 < w && y2 >= 0 && y2 < h) {
        px[y2 * w + x1] = red;
      }
    }
  }
}

// ===== PAGE WEB =====
static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Filoguidage - Test filtres</title>

<style>
 body {
   margin: 0;
   background: #0d1117;
   color: #e6edf3;
   font-family: system-ui, sans-serif;
   text-align: center;
 }

 h1 {
   font-size: 17px;
   padding: 12px;
   margin: 0;
   background: #161b22;
   border-bottom: 1px solid #30363d;
 }

 img {
   max-width: 100%;
   background: #000;
   display: block;
   margin: 12px auto;
   border: 1px solid #30363d;
   border-radius: 8px;
 }

 .panel {
   max-width: 760px;
   margin: 10px auto;
   padding: 10px;
   background: #161b22;
   border: 1px solid #30363d;
   border-radius: 10px;
   text-align: left;
 }

 .row {
   display: grid;
   grid-template-columns: 150px 1fr 48px;
   gap: 8px;
   align-items: center;
   margin: 8px 0;
 }

 select, input {
   width: 100%;
 }

 label {
   font-size: 13px;
   color: #c9d1d9;
 }

 .val {
   font-size: 13px;
   color: #8b949e;
   text-align: right;
 }

 #bar {
   display: flex;
   justify-content: center;
   gap: 3px;
   margin: 10px;
   flex-wrap: wrap;
 }

 .cell {
   width: 26px;
   height: 26px;
   border-radius: 5px;
   background: #21262d;
   display: flex;
   align-items: center;
   justify-content: center;
   font-size: 12px;
   color: #8b949e;
 }

 .cell.center {
   outline: 1px solid #6e7681;
 }

 .cell.on {
   background: #2ea043;
   color: #fff;
   font-weight: bold;
 }

 #info {
   font-size: 14px;
   color: #8b949e;
   margin: 8px;
 }

 #info.lost {
   color: #f85149;
   font-weight: bold;
 }

 .small {
   color: #8b949e;
   font-size: 12px;
   line-height: 1.35;
 }
</style>
</head>

<body>
<h1>FILOGUIDAGE - TEST FILTRES</h1>

<img id="cam" src="">

<div id="bar"></div>
<div id="info">connexion...</div>

<div class="panel">
  <div class="row">
    <label>Filtre</label>
    <select id="filter">
      <option value="0">0 - Rouge dominance</option>
      <option value="1">1 - Rouge ratio</option>
      <option value="2">2 - Rouge vs max vert/bleu</option>
      <option value="3">3 - HSV rouge</option>
      <option value="4">4 - Rouge strict</option>
      <option value="5">5 - Rouge chroma</option>
      <option value="6">6 - Bleu dominance</option>
      <option value="7">7 - Vert dominance</option>
      <option value="8">8 - Ligne sombre</option>
      <option value="9">9 - Ligne claire</option>
      <option value="10">10 - Saturation couleur</option>
    </select>
    <div></div>
  </div>

  <div class="row">
    <label>Affichage</label>
    <select id="view">
      <option value="0">Camera + overlay</option>
      <option value="1">Masque noir/blanc</option>
      <option value="2">Heatmap du filtre</option>
    </select>
    <div></div>
  </div>

  <div class="row">
    <label>Contraste min</label>
    <input id="minContrast" type="range" min="0" max="120" value="35">
    <span class="val" id="minContrast_v">35</span>
  </div>

  <div class="row">
    <label>Correction seuil</label>
    <input id="threshBias" type="range" min="-80" max="80" value="0">
    <span class="val" id="threshBias_v">0</span>
  </div>

  <div class="row">
    <label>Seuil masque</label>
    <input id="previewThresh" type="range" min="0" max="255" value="150">
    <span class="val" id="previewThresh_v">150</span>
  </div>

  <div class="row">
    <label>Pixels min</label>
    <input id="minPx" type="range" min="1" max="40" value="3">
    <span class="val" id="minPx_v">3</span>
  </div>

  <div class="row">
    <label>Largeur max</label>
    <input id="maxWidth" type="range" min="5" max="220" value="120">
    <span class="val" id="maxWidth_v">120</span>
  </div>

  <div class="row">
    <label>Lignes min</label>
    <input id="minRows" type="range" min="1" max="8" value="4">
    <span class="val" id="minRows_v">4</span>
  </div>

  <div class="row">
    <label>Dispersion max</label>
    <input id="maxSpread" type="range" min="10" max="320" value="80">
    <span class="val" id="maxSpread_v">80</span>
  </div>

  <div class="row">
    <label>Flou score</label>
    <input id="blur" type="range" min="0" max="4" value="1">
    <span class="val" id="blur_v">1</span>
  </div>

  <div class="row">
    <label>Rouge min</label>
    <input id="redMin" type="range" min="0" max="255" value="80">
    <span class="val" id="redMin_v">80</span>
  </div>

  <div class="row">
    <label>Rouge delta</label>
    <input id="redDelta" type="range" min="0" max="120" value="25">
    <span class="val" id="redDelta_v">25</span>
  </div>

  <p class="small">
    Conseil : commence avec l'affichage <b>Heatmap</b>, puis teste les filtres rouges 0, 2, 3, 4 et 5.
    Ensuite passe en <b>Masque</b> pour regler le seuil. Si le fil est perdu trop vite, baisse "Contraste min".
    Si le robot detecte encore un fil quand il n'y en a pas, augmente "Contraste min" ou "Correction seuil".
  </p>
</div>

<script>
 var img = document.getElementById('cam');
 img.src = location.protocol + '//' + location.hostname + ':81/stream';

 var bar = document.getElementById('bar');

 for (var i = 0; i <= 10; i++) {
   var c = document.createElement('div');
   c.className = 'cell' + (i == 5 ? ' center' : '');
   c.id = 'c' + i;
   c.textContent = i;
   bar.appendChild(c);
 }

 var ids = [
   'filter',
   'view',
   'minContrast',
   'threshBias',
   'previewThresh',
   'minPx',
   'maxWidth',
   'minRows',
   'maxSpread',
   'blur',
   'redMin',
   'redDelta'
 ];

 function setVal(id, val) {
   var e = document.getElementById(id);
   if (!e) return;
   e.value = val;

   var v = document.getElementById(id + '_v');
   if (v) v.textContent = val;
 }

 function bind(id) {
   var e = document.getElementById(id);
   if (!e) return;

   e.addEventListener('input', function() {
     var v = document.getElementById(id + '_v');
     if (v) v.textContent = e.value;
   });

   e.addEventListener('change', sendSettings);
 }

 for (var i = 0; i < ids.length; i++) {
   bind(ids[i]);
 }

 async function sendSettings() {
   var q = [];

   for (var i = 0; i < ids.length; i++) {
     var id = ids[i];
     var e = document.getElementById(id);
     q.push(id + '=' + encodeURIComponent(e.value));
   }

   try {
     await fetch('/set?' + q.join('&'));
   } catch(e) {}
 }

 var initialized = false;

 async function poll() {
   try {
     var j = await (await fetch('/status')).json();

     if (!initialized) {
       setVal('filter', j.filter);
       setVal('view', j.view);
       setVal('minContrast', j.minContrast);
       setVal('threshBias', j.threshBias);
       setVal('previewThresh', j.previewThresh);
       setVal('minPx', j.minPx);
       setVal('maxWidth', j.maxWidth);
       setVal('minRows', j.minRows);
       setVal('maxSpread', j.maxSpread);
       setVal('blur', j.blur);
       setVal('redMin', j.redMin);
       setVal('redDelta', j.redDelta);
       initialized = true;
     }

     for (var i = 0; i <= 10; i++) {
       document.getElementById('c' + i).classList.remove('on');
     }

     var info = document.getElementById('info');

     if (j.conf) {
       document.getElementById('c' + j.pos).classList.add('on');

       var dir = j.pos < 5 ? 'GAUCHE' : (j.pos > 5 ? 'DROITE' : 'CENTRE');

       info.classList.remove('lost');
       info.textContent =
         'Position ' + j.pos + '/10 - ' + dir +
         ' | lignes ' + j.rows + '/' + j.total +
         ' | contraste ' + j.dbgContrast +
         ' | seuil ' + j.dbgThresh +
         ' | largeur ' + j.dbgWidth;
     } else {
       info.classList.add('lost');
       info.textContent =
         'FIL PERDU' +
         ' | lignes ' + j.rows + '/' + j.total +
         ' | contraste ' + j.dbgContrast +
         ' | seuil ' + j.dbgThresh;
     }
   } catch(e) {}

   setTimeout(poll, 250);
 }

 poll();
</script>
</body>
</html>
)HTML";

// ===== STATUS JSON =====
esp_err_t send_status_json(httpd_req_t *req) {
  char buf[512];

  int n = snprintf(
    buf,
    sizeof(buf),
    "{"
      "\"pos\":%d,"
      "\"conf\":%d,"
      "\"rows\":%d,"
      "\"total\":8,"
      "\"filter\":%d,"
      "\"view\":%d,"
      "\"minContrast\":%d,"
      "\"threshBias\":%d,"
      "\"previewThresh\":%d,"
      "\"minPx\":%d,"
      "\"maxWidth\":%d,"
      "\"minRows\":%d,"
      "\"maxSpread\":%d,"
      "\"blur\":%d,"
      "\"redMin\":%d,"
      "\"redDelta\":%d,"
      "\"dbgContrast\":%d,"
      "\"dbgThresh\":%d,"
      "\"dbgWidth\":%d"
    "}",
    line_pos,
    confidence,
    match_count,
    g_filter_mode,
    g_view_mode,
    g_min_contrast,
    g_thresh_bias,
    g_preview_thresh,
    g_min_px,
    g_max_width,
    g_min_rows,
    g_max_spread,
    g_blur_radius,
    g_red_min,
    g_red_delta,
    debug_contrast,
    debug_thresh,
    debug_width
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  return httpd_resp_send(req, buf, n);
}

void query_set_int(const char *query, const char *key, volatile int *dst, int mn, int mx) {
  char val[24];

  if (httpd_query_key_value(query, key, val, sizeof(val)) == ESP_OK) {
    int v = atoi(val);
    v = clampi(v, mn, mx);
    *dst = v;
  }
}

// ===== HANDLERS HTTP =====
esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t status_handler(httpd_req_t *req) {
  return send_status_json(req);
}

esp_err_t set_handler(httpd_req_t *req) {
  int len = httpd_req_get_url_query_len(req) + 1;

  if (len > 1 && len < 512) {
    char query[512];

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
      query_set_int(query, "filter",        &g_filter_mode,    0, 10);
      query_set_int(query, "view",          &g_view_mode,      0, 2);
      query_set_int(query, "minContrast",   &g_min_contrast,   0, 120);
      query_set_int(query, "threshBias",    &g_thresh_bias,   -80, 80);
      query_set_int(query, "previewThresh", &g_preview_thresh, 0, 255);
      query_set_int(query, "minPx",         &g_min_px,         1, 40);
      query_set_int(query, "maxWidth",      &g_max_width,      5, 220);
      query_set_int(query, "minRows",       &g_min_rows,       1, 8);
      query_set_int(query, "maxSpread",     &g_max_spread,     10, 320);
      query_set_int(query, "blur",          &g_blur_radius,    0, 4);
      query_set_int(query, "redMin",        &g_red_min,        0, 255);
      query_set_int(query, "redDelta",      &g_red_delta,      0, 120);
    }
  }

  return send_status_json(req);
}

esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);

  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part_buf[64];

  while (true) {
    uint8_t *jpg_buf = NULL;
    size_t jpg_len = 0;
    bool ok = false;

    if (xSemaphoreTake(cam_mutex, portMAX_DELAY) == pdTRUE) {
      camera_fb_t *fb = esp_camera_fb_get();

      if (fb) {
        detect_line(fb);
        draw_filter_view(fb);
        draw_overlay(fb);

        ok = frame2jpg(fb, 80, &jpg_buf, &jpg_len);

        esp_camera_fb_return(fb);
      }

      xSemaphoreGive(cam_mutex);
    }

    if (!ok) {
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));

    if (res == ESP_OK) {
      int hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, (unsigned)jpg_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }

    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)jpg_buf, jpg_len);
    }

    free(jpg_buf);

    if (res != ESP_OK) break;
  }

  return res;
}

// ===== SERVEURS =====
void start_servers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.server_port = 80;
  config.ctrl_port = 32768;

  httpd_uri_t index_uri = {};
  index_uri.uri = "/";
  index_uri.method = HTTP_GET;
  index_uri.handler = index_handler;
  index_uri.user_ctx = NULL;

  httpd_uri_t status_uri = {};
  status_uri.uri = "/status";
  status_uri.method = HTTP_GET;
  status_uri.handler = status_handler;
  status_uri.user_ctx = NULL;

  httpd_uri_t set_uri = {};
  set_uri.uri = "/set";
  set_uri.method = HTTP_GET;
  set_uri.handler = set_handler;
  set_uri.user_ctx = NULL;

  if (httpd_start(&server_http, &config) == ESP_OK) {
    httpd_register_uri_handler(server_http, &index_uri);
    httpd_register_uri_handler(server_http, &status_uri);
    httpd_register_uri_handler(server_http, &set_uri);
  }

  config.server_port = 81;
  config.ctrl_port = 32769;

  httpd_uri_t stream_uri = {};
  stream_uri.uri = "/stream";
  stream_uri.method = HTTP_GET;
  stream_uri.handler = stream_handler;
  stream_uri.user_ctx = NULL;

  if (httpd_start(&stream_http, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_http, &stream_uri);
  }
}

// ===== WIFI =====
void start_wifi() {
#if USE_AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.print("AP \"");
  Serial.print(AP_SSID);
  Serial.print("\" -> ouvre http://");
  Serial.println(WiFi.softAPIP());
#else
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connexion WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.print("\nConnecte -> ouvre http://");
  Serial.println(WiFi.localIP());
#endif
}

// ===== SETUP =====
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  Serial.println("=== FILOGUIDAGE - TEST FILTRES WEB ===");

  cam_mutex = xSemaphoreCreateMutex();

  pinMode(PWDN_GPIO_NUM, OUTPUT);
  digitalWrite(PWDN_GPIO_NUM, LOW);
  delay(300);

  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 8000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  config.fb_count = 2;

  esp_err_t err = esp_camera_init(&config);

  Serial.printf("Camera init: 0x%x\n", err);

  if (err != ESP_OK) {
    Serial.println("ERREUR CAMERA !");
    return;
  }

  sensor_t *s = esp_camera_sensor_get();

  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_gain_ctrl(s, 1);
  s->set_exposure_ctrl(s, 1);

  // Optionnel : si les couleurs changent trop, tu peux essayer :
  // s->set_awb_gain(s, 1);
  // s->set_saturation(s, 1);
  // s->set_brightness(s, 0);
  // s->set_contrast(s, 1);

  Serial.println("Stabilisation camera...");

  for (int i = 0; i < 30; i++) {
    camera_fb_t *fb = esp_camera_fb_get();

    if (fb) {
      esp_camera_fb_return(fb);
    }

    delay(100);
  }

  start_wifi();
  start_servers();

  Serial.println();
  Serial.println("Page web : http://192.168.4.1 en mode AP");
  Serial.println("Flux MJPEG : port 81");
  Serial.println("------------------------------");
}

// ===== LOOP =====
void loop() {
  // Le stream fait deja detect_line().
  // Cette boucle garde juste un affichage serie.

  static unsigned long last_print = 0;

  if (millis() - last_print > 500) {
    last_print = millis();

    if (confidence == 0) {
      Serial.printf(
        "PERDU | lignes:%d/8 | filtre:%d | contraste:%d | seuil:%d\n",
        match_count,
        g_filter_mode,
        debug_contrast,
        debug_thresh
      );
    } else {
      char bar[12];

      memset(bar, '.', 11);
      bar[11] = '\0';
      bar[5] = '|';

      if (line_pos >= 0 && line_pos <= 10) {
        bar[line_pos] = '#';
      }

      const char* dir;

      if (line_pos < 4)       dir = "<<< GAUCHE";
      else if (line_pos < 5)  dir = "<  GAUCHE";
      else if (line_pos == 5) dir = "  CENTRE";
      else if (line_pos < 7)  dir = "  DROITE >";
      else                    dir = "  DROITE >>>";

      Serial.printf(
        "%d [%s] lignes:%d/8 filtre:%d contraste:%d seuil:%d largeur:%d %s\n",
        line_pos,
        bar,
        match_count,
        g_filter_mode,
        debug_contrast,
        debug_thresh,
        debug_width,
        dir
      );
    }
  }

  delay(30);
}