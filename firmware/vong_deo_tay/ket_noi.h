// ket_noi.h — Wi-Fi, đồng hồ NTP và MQTT dùng chung cho cả 3 thiết bị.
// Ba thư mục firmware có bản giống hệt nhau; sửa ở đâu thì chép sang 2 thư mục kia.
// MQTT dùng thư viện esp-mqtt có sẵn trong gói board "esp32" 3.x, không cần cài thêm.
// Gửi tin bằng guiSau() (không chặn). gui() gửi thẳng, có thể chặn tới 10 giây khi mạng chậm: chỉ tác vụ gửi dùng.
#pragma once
#include <WiFi.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <mqtt_client.h>
#include "cau_hinh.h"

#ifndef TIN_DAI_NHAT
#define TIN_DAI_NHAT 768  // tin dữ liệu dài nhất qua hàng đợi: 1 khung ECG vòng ngực ≈ 600 ký tự, 1 gói PPG ≈ 400
#endif
#ifndef SO_TIN_CHO
#define SO_TIN_CHO 8      // số tin dữ liệu chờ gửi khi mạng chậm: vòng tay ≈ 3 giây sóng PPG, vòng ngực ≈ 1,5 giây ECG
#endif

namespace ket_noi {

// Chạy trong tác vụ MQTT: chỉ chép dữ liệu và đặt cờ, không làm việc lâu ở đây.
using HamNhanTin = void (*)(const char* chuDe, const char* noiDung);
// Chạy mỗi lần (kết nối lại) tới broker: đăng ký các chủ đề cần nghe ở đây.
using HamKhiKetNoi = void (*)();

inline esp_mqtt_client_handle_t mqtt = nullptr;
inline volatile bool daKetNoi = false;
inline char goc[40];        // "aiot/bn01"
inline char thietBi[12];    // "tay", "nguc" hoặc "radar"
inline HamNhanTin khiNhanTin = nullptr;
inline HamKhiKetNoi khiKetNoi = nullptr;
inline uint32_t soTinGui = 0, soTinMat = 0;  // chỉ để thống kê
inline uint32_t soTinBo = 0;                 // tin dữ liệu bị bỏ vì hàng đợi gửi đầy (mạng nghẽn lâu)

// Giờ thực (ms tính từ 1/1/1970) sau khi đồng bộ NTP; trả 0 nếu chưa đồng bộ được
// (Wi-Fi không ra Internet). Khi đó máy chủ tự lấy giờ nhận tin thay thế.
inline uint64_t gioThucMs() {
  timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000) return 0;
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000;
}

// Đổi mốc esp_timer_get_time() (µs kể từ lúc bật nguồn) sang giờ thực.
inline uint64_t doiGioThuc(int64_t moc_us) {
  uint64_t bayGio = gioThucMs();
  if (bayGio == 0) return 0;
  return bayGio - (uint64_t)((esp_timer_get_time() - moc_us) / 1000);
}

inline void taoChuDe(char* chuDe, size_t n, const char* kenh) { snprintf(chuDe, n, "%s/%s/%s", goc, thietBi, kenh); }

// Gửi thẳng lên chủ đề "aiot/<mã>/<thiết bị>/<kênh>", chặn tới khi gửi xong.
// giuLai: broker giữ tin cuối cho người đến sau. quanTrong: QoS 1, broker xác nhận đã nhận.
inline bool gui(const char* kenh, const char* noiDung, bool giuLai = false, bool quanTrong = false) {
  if (!daKetNoi) {
    soTinMat++;
    return false;
  }
  char chuDe[64];
  taoChuDe(chuDe, sizeof chuDe, kenh);
  bool ok = esp_mqtt_client_publish(mqtt, chuDe, noiDung, 0, (giuLai || quanTrong) ? 1 : 0, giuLai) >= 0;
  if (ok) soTinGui++;
  else soTinMat++;
  return ok;
}

// ---- Gửi không chặn ----
struct TinCho {
  char kenh[12];
  char noiDung[TIN_DAI_NHAT];
};
inline QueueHandle_t hangGui = nullptr;

inline void tacVuGui(void*) {
  static TinCho t;
  for (;;)
    if (xQueueReceive(hangGui, &t, portMAX_DELAY) == pdTRUE) gui(t.kenh, t.noiDung);
}

// Gửi không chặn, gọi được trong loop() lẫn trong tác vụ MQTT. esp_mqtt_client_publish có thể chặn tới 10 giây khi
// mạng chậm; nếu gọi thẳng trong loop() thì vòng tay tràn FIFO cảm biến (MAX30100 chỉ giữ 160 ms), vòng ngực trễ còi
// và đếm ngược té ngã.
//   - Tin QoS 1 (giuLai hoặc quanTrong: sự kiện, trạng thái) vào hộp thư chờ của esp-mqtt. Tác vụ MQTT gửi đi và tự
//     gửi lại tới khi broker xác nhận, kể cả sau khi mất kết nối một lúc (tối đa khoảng 30 giây).
//   - Tin dữ liệu QoS 0 (sóng, chỉ số) vào hàng đợi SO_TIN_CHO tin, tác vụ "gui" gửi đi. Hàng đợi đầy thì bỏ tin mới
//     (cộng soTinBo); đang mất kết nối thì bỏ luôn, vì dữ liệu cũ không còn giá trị khi kết nối lại.
inline void guiSau(const char* kenh, const char* noiDung, bool giuLai = false, bool quanTrong = false) {
  if (giuLai || quanTrong) {
    char chuDe[64];
    taoChuDe(chuDe, sizeof chuDe, kenh);
    if (mqtt && esp_mqtt_client_enqueue(mqtt, chuDe, noiDung, 0, 1, giuLai, true) >= 0) soTinGui++;
    else soTinMat++;
    return;
  }
  if (!daKetNoi) {
    soTinMat++;
    return;
  }
  size_t dai = strlen(noiDung);
  if (!hangGui || dai >= sizeof(TinCho::noiDung)) {  // không có hàng đợi (thiếu RAM) hoặc tin quá dài: gửi thẳng
    gui(kenh, noiDung);
    return;
  }
  TinCho t;
  strlcpy(t.kenh, kenh, sizeof t.kenh);
  memcpy(t.noiDung, noiDung, dai + 1);
  if (xQueueSend(hangGui, &t, 0) != pdTRUE) soTinBo++;
}

// Nghe 1 chủ đề con của "aiot/<mã>", vd. dangKy("canh_bao") → "aiot/bn01/canh_bao".
inline void dangKy(const char* duoi) {
  char chuDe[64];
  snprintf(chuDe, sizeof chuDe, "%s/%s", goc, duoi);
  esp_mqtt_client_subscribe_single(mqtt, chuDe, 1);
}

// Trạng thái thiết bị dạng JSON. themJson: các trường riêng, vd. ",\"cam_bien\":\"MAX30102\"".
inline void trangThaiJson(char* s, size_t n, const char* themJson = "") {
  snprintf(s, n, "{\"online\":1,\"ip\":\"%s\",\"rssi\":%d,\"chay_giay\":%lu,\"tin_gui\":%lu,\"tin_mat\":%lu%s}",
           WiFi.localIP().toString().c_str(), (int)WiFi.RSSI(), (unsigned long)(millis() / 1000),
           (unsigned long)soTinGui, (unsigned long)soTinMat, themJson);
}

// Gửi trạng thái thiết bị (broker giữ lại), không chặn. Mất kết nối thì thôi: lúc kết nối lại khiKetNoi gửi bản mới.
inline void guiTrangThai(const char* themJson = "") {
  if (!daKetNoi) return;
  char s[256];
  trangThaiJson(s, sizeof s, themJson);
  guiSau("trang_thai", s, true);
}

inline void suKien(void*, esp_event_base_t, int32_t id, void* duLieu) {
  auto e = static_cast<esp_mqtt_event_handle_t>(duLieu);
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      daKetNoi = true;
      Serial.println("[MQTT] Đã kết nối broker");
      if (khiKetNoi) khiKetNoi();
      break;
    case MQTT_EVENT_DISCONNECTED:
      if (daKetNoi) Serial.println("[MQTT] Mất kết nối, đang thử lại...");
      daKetNoi = false;
      break;
    case MQTT_EVENT_DATA: {
      // Tin điều khiển đều ngắn; tin bị chia nhiều mảnh thì bỏ qua.
      if (!khiNhanTin || e->current_data_offset != 0 || e->data_len != e->total_data_len) break;
      char chuDe[64], noiDung[256];
      int n1 = min(e->topic_len, (int)sizeof chuDe - 1), n2 = min(e->data_len, (int)sizeof noiDung - 1);
      memcpy(chuDe, e->topic, n1);
      chuDe[n1] = 0;
      memcpy(noiDung, e->data, n2);
      noiDung[n2] = 0;
      khiNhanTin(chuDe, noiDung);
      break;
    }
    default:
      break;
  }
}

inline void batDau(const char* tenThietBi, HamKhiKetNoi hamKetNoi = nullptr, HamNhanTin hamNhanTin = nullptr) {
  snprintf(goc, sizeof goc, "aiot/%s", MA_NGUOI_DUNG);
  snprintf(thietBi, sizeof thietBi, "%s", tenThietBi);
  khiKetNoi = hamKetNoi;
  khiNhanTin = hamNhanTin;

  hangGui = xQueueCreate(SO_TIN_CHO, sizeof(TinCho));
  if (hangGui && xTaskCreate(tacVuGui, "gui", 5120, nullptr, 1, nullptr) != pdPASS) {
    vQueueDelete(hangGui);
    hangGui = nullptr;
  }
  if (!hangGui) Serial.println("[MQTT] Thiếu RAM, không tạo được tác vụ gửi: gửi thẳng như bản cũ");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_TEN, WIFI_MAT_KHAU);
#ifdef WIFI_CONG_SUAT
  WiFi.setTxPower(WIFI_CONG_SUAT);  // đặt sau WiFi.begin (xem cau_hinh.h của vòng tay: ESP32-C3 SuperMini)
#endif
  Serial.printf("[WiFi] Đang kết nối \"%s\"", WIFI_TEN);
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf(" OK, IP %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println(" chưa được, thiết bị sẽ tự thử lại");
  configTime(MUI_GIO_GIAY, 0, "pool.ntp.org", "time.google.com");

  static char uri[80], chuDeTrangThai[64], tenMay[48];
  snprintf(uri, sizeof uri, "mqtt://%s:%d", MQTT_MAY_CHU, MQTT_CONG);
  snprintf(chuDeTrangThai, sizeof chuDeTrangThai, "%s/%s/trang_thai", goc, thietBi);
  snprintf(tenMay, sizeof tenMay, "aiot-%s-%s-%06lx", MA_NGUOI_DUNG, thietBi,
           (unsigned long)((ESP.getEfuseMac() >> 24) & 0xFFFFFF));

  esp_mqtt_client_config_t c = {};
  c.broker.address.uri = uri;
  c.credentials.client_id = tenMay;
  if (strlen(MQTT_TAI_KHOAN) > 0) {
    c.credentials.username = MQTT_TAI_KHOAN;
    c.credentials.authentication.password = MQTT_MAT_KHAU;
  }
  c.session.last_will.topic = chuDeTrangThai;  // broker tự báo "offline" khi thiết bị mất kết nối
  c.session.last_will.msg = "{\"online\":0}";
  c.session.last_will.qos = 1;
  c.session.last_will.retain = 1;
  c.session.keepalive = 15;
  c.network.reconnect_timeout_ms = 3000;
  c.buffer.size = 2048;
  c.outbox.limit = 16 * 1024;  // hộp thư chờ tin QoS 1 tối đa 16 KB khi mất kết nối lâu
  mqtt = esp_mqtt_client_init(&c);
  esp_mqtt_client_register_event(mqtt, MQTT_EVENT_ANY, suKien, nullptr);
  esp_mqtt_client_start(mqtt);
}

}  // namespace ket_noi
