// Vòng đeo tay — ESP32-C3 SuperMini (hoặc XIAO ESP32C3 / ESP32C6) + cảm biến PPG MAX30100, MAX30102 hoặc AS7038RB
// (tự nhận).
// Gửi lên MQTT:
//   aiot/<mã>/tay/ppg     2 gói/giây   {"n":12,"t":<ms>,"fs":100,"da":1,"v":[50 số]}  sóng mạch cho mô hình AI;
//                         v đã đảo dấu (giá trị lớn nhất − số đọc thô) để đỉnh mạch hướng lên như dữ liệu huấn luyện
//   aiot/<mã>/tay/chi_so  2 giây/lần   {"t":..,"nhip_tim":72,"spo2":97,"nhiet_do":33.5,"da":1,"cho":0,"cl":100,"pi":0.85}
//                         nhịp tim tính ngay trên vòng (lọc thông dải 0,5–4 Hz, khử gai, bỏ qua lúc cử động);
//                         SpO2 và nhiệt độ da chỉ có với MAX30102/MAX30100; -1 = chưa có;
//                         cl = chất lượng tín hiệu (100 tốt, 50 vừa cử động, 0 chưa bắt được nhịp); pi = chỉ số tưới máu %
// Nghe:
//   aiot/<mã>/canh_bao    platform gửi cảnh báo (Hình 11 của báo cáo): rung/kêu ở chân CHAN_BAO_DONG,
//                         nháy đèn trên board nếu có (XIAO ESP32C6)
// Board: ESP32-C3 SuperMini, khi nạp chọn "Nologo ESP32C3 Super Mini" (gói "esp32" của Espressif, bản 3.x).
//   GY-MAX30100: VIN → 3.3, GND → G, SDA → chân 6, SCL → chân 7 (INT để trống); điện trở kéo lên I2C phải về 3V3.
//   Báo động (tuỳ chọn): chân 10. Đèn xanh trên board (GPIO8) sáng theo báo động. Cấp pin: HUONG_DAN.md.
// XIAO ESP32C3 / ESP32C6 vẫn dùng được: SDA D4, SCL D5, INT D2, ENABLE D3 (AS7038RB), báo động D10.
// ESP32 thường (chọn "ESP32 Dev Module"): SDA GPIO23, SCL GPIO19, báo động GPIO25.
#include "cau_hinh.h"
#include "ket_noi.h"
#include "cam_bien_ppg.h"
#include "nhip_tim.h"

// Chân nối. INT, ENABLE chỉ dùng với AS7038RB (INT chưa dùng: đọc FIFO theo chu kỳ).
#if defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6)
constexpr int CHAN_SDA = SDA, CHAN_SCL = SCL;  // D4, D5
constexpr int CHAN_INT = D2, CHAN_ENABLE = D3, CHAN_BAO_MAC_DINH = D10;
#elif defined(CONFIG_IDF_TARGET_ESP32)
// ESP32 thường (ESP32-WROOM-32, chọn board "ESP32 Dev Module"): SDA GPIO23, SCL GPIO19. Không dùng GPIO6–11 (nối chip
// flash, đụng vào là treo mạch) và các chân khởi động 0, 2, 12, 15. Báo động mặc định GPIO25.
constexpr int CHAN_SDA = 23, CHAN_SCL = 19;
constexpr int CHAN_INT = 18, CHAN_ENABLE = 5, CHAN_BAO_MAC_DINH = 25;
#else
// ESP32-C3 SuperMini: ghi số GPIO, trùng GPIO của XIAO ESP32C3. Không dùng SDA/SCL mặc định của board (GPIO8, GPIO9)
// vì GPIO8 nối đèn trên board, GPIO9 nối nút BOOT.
constexpr int CHAN_SDA = 6, CHAN_SCL = 7;
constexpr int CHAN_INT = 4, CHAN_ENABLE = 5, CHAN_BAO_MAC_DINH = 10;
#endif
constexpr int CHAN_BAO = CHAN_BAO_DONG >= 0 ? CHAN_BAO_DONG : CHAN_BAO_MAC_DINH;
constexpr int MAU_MOI_GOI = 50;  // 0,5 giây

uint32_t goi[MAU_MOI_GOI];
int soMau = 0;
int64_t mocMauDau_us = 0;   // thời điểm mẫu đầu tiên của gói
uint32_t soGoi = 0;
uint64_t tong1s = 0;        // cộng dồn 1 giây để tự chỉnh dòng LED
uint64_t tongDo1s = 0;      // như trên, cho LED đỏ (chỉnh riêng)
int dem1s = 0;
uint32_t tbCuoi = 0, tbDoCuoi = 0;   // số đọc trung bình của giây vừa rồi (hồng ngoại, đỏ)
bool chamDa = false, dangCho = false;
int giayKhongCham = 0;
float nhietDo = NAN;
nhip_tim::BoDem boNhip;
nhip_tim::BoSpO2 boSpO2;
volatile int lenhBao = -1;  // mức cảnh báo vừa nhận qua MQTT; -1 = không có lệnh mới

// ---------- báo động tại cổ tay ----------
namespace bao {
uint16_t msBat = 0, msTat = 0;
uint32_t hetLuc = 0, mocDoi = 0;
bool dangBat = false;

void xuat(bool bat) {
  dangBat = bat;
  digitalWrite(CHAN_BAO, bat ? HIGH : LOW);
#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, bat ? LOW : HIGH);  // đèn trên board (SuperMini GPIO8, XIAO ESP32C6) sáng ở mức thấp
#endif
}

void dung() {
  hetLuc = 0;
  xuat(false);
}

void chay(uint16_t bat, uint16_t tat, uint32_t trongMs) {
  msBat = bat;
  msTat = tat;
  hetLuc = (millis() + trongMs) | 1;  // khác 0
  mocDoi = millis();
  xuat(true);
}

void capNhat() {
  if (hetLuc == 0) return;
  if ((int32_t)(millis() - hetLuc) >= 0) return dung();
  if (millis() - mocDoi >= (dangBat ? msBat : msTat)) {
    mocDoi = millis();
    xuat(!dangBat);
  }
}
}  // namespace bao

int laySo(const char* json, const char* khoa, int macDinh) {
  char mau[24];
  snprintf(mau, sizeof mau, "\"%s\"", khoa);
  const char* p = strstr(json, mau);
  if (!p || !(p = strchr(p + strlen(mau), ':'))) return macDinh;
  return atoi(p + 1);
}

// ---------- MQTT ----------
char i2cThay[48] = "";  // các địa chỉ I2C trả lời ở lần quét gần nhất (chỉ quét khi chưa thấy cảm biến)
uint32_t tocDoI2C = I2C_TOC_DO;  // tự hạ xuống 100 kHz nếu ở tốc độ đặt sẵn không nhận ra cảm biến

// Quét bus I2C: không địa chỉ nào trả lời là lỗi dây GND/SDA/SCL, nguồn hoặc điện trở kéo lên; có địa chỉ lạ là
// cảm biến khác loại. Địa chỉ 0x57 (MAX3010x) in kèm PART_ID: MAX30100 là 11, MAX30102 là 15; số khác là đọc sai
// dữ liệu (điện trở kéo lên I2C của GY-MAX30100 về 1,8 V, chưa chuyển lên 3,3 V).
void quetI2C() {
  char s[sizeof i2cThay] = "";
  size_t d = 0;
  for (uint8_t a = 0x08; a < 0x78 && d + 16 < sizeof s; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() != 0) continue;
    d += snprintf(s + d, sizeof s - d, "%s%02x", d ? " " : "", a);
    if (a == 0x57) {
      int id = ppg::doc1(0x57, 0xFF);
      d += id < 0 ? snprintf(s + d, sizeof s - d, " (id ?)") : snprintf(s + d, sizeof s - d, " (id %02x)", id);
    }
  }
  memcpy(i2cThay, s, sizeof s);
}

void guiTrangThai() {
  char them[144];
  snprintf(them, sizeof them, ",\"cam_bien\":\"%s\",\"cho\":%d,\"tin_bo\":%lu,\"i2c\":\"%s\",\"i2c_khz\":%lu",
           ppg::ten(), dangCho ? 1 : 0, (unsigned long)ket_noi::soTinBo, i2cThay, (unsigned long)(tocDoI2C / 1000));
  ket_noi::guiTrangThai(them);  // không chặn: gọi được cả trong tác vụ MQTT lúc vừa kết nối
}

void khiKetNoi() {
  ket_noi::dangKy("canh_bao");
  guiTrangThai();
}

// Chạy trong tác vụ MQTT: chỉ ghi nhận lệnh, loop() mới bật/tắt chân báo động.
void khiNhanTin(const char* chuDe, const char* s) {
  int muc = laySo(s, "muc", -1);
  if (muc < 0 && isdigit((unsigned char)s[0])) muc = atoi(s);
  if (muc >= 0) lenhBao = muc;
}

void xuLyLenhBao() {
  int muc = lenhBao;
  if (muc < 0) return;
  lenhBao = -1;
  if (muc == 0) bao::dung();                    // người nhà đã tắt báo động trên platform
  else if (muc == 1) bao::chay(150, 150, 600);  // cảnh báo nhẹ: 2 nhịp ngắn
  else bao::chay(500, 300, 20000);              // nguy cơ cao: từng hồi trong 20 giây
}

void guiGoi() {
  static char s[700];
  int p = snprintf(s, sizeof s, "{\"n\":%lu,\"t\":%llu,\"fs\":%d,\"da\":%d,\"v\":[", (unsigned long)soGoi++,
                   (unsigned long long)ket_noi::doiGioThuc(mocMauDau_us), ppg::TAN_SO, chamDa ? 1 : 0);
  for (int i = 0; i < MAU_MOI_GOI; i++)
    p += snprintf(s + p, sizeof s - p, i ? ",%lu" : "%lu", (unsigned long)(ppg::giaTriMax - goi[i]));
  snprintf(s + p, sizeof s - p, "]}");
  ket_noi::guiSau("ppg", s);
}

void guiChiSo() {
  char s[220];
  uint32_t bay = millis();
  int hr = chamDa ? boNhip.nhipTim(bay) : -1;
  int spo2 = (chamDa && ppg::coKenhDo()) ? boSpO2.phanTram() : -1;
  float nhiet = (chamDa && !isnan(nhietDo)) ? nhietDo : -1.0f;
  int cl = chamDa ? boNhip.chatLuong(bay) : 0;
  float pi = chamDa ? boNhip.chiSoTuoiMau() : 0;
  snprintf(s, sizeof s,
           "{\"t\":%llu,\"nhip_tim\":%d,\"spo2\":%d,\"nhiet_do\":%.1f,\"da\":%d,\"cho\":%d,\"cl\":%d,\"pi\":%.2f}",
           (unsigned long long)ket_noi::gioThucMs(), hr, spo2, nhiet, chamDa ? 1 : 0, dangCho ? 1 : 0, cl, pi);
  ket_noi::guiSau("chi_so", s);
}

// Nhiệt kế của MAX30102: 10 giây đo 1 lần, ra lệnh rồi 100 ms sau mới đọc kết quả.
void capNhatNhiet(uint32_t bay) {
  static uint32_t mocDo = 0;
  static bool dangDo = false;
  if (!ppg::coNhietKe()) return;
  if (!dangDo && (mocDo == 0 || bay - mocDo > 10000)) {
    ppg::batDauDoNhiet();
    mocDo = bay | 1;
    dangDo = true;
  } else if (dangDo && bay - mocDo > 100) {
    float t = ppg::docNhiet();
    if (!isnan(t) && t > 15 && t < 50) nhietDo = t;  // bỏ số vô lý (vd. ngay sau khi cảm biến được cài lại)
    dangDo = false;
  }
}

// Mỗi giây: xét da có áp vào cảm biến không, chỉnh dòng LED, vào/ra chế độ chờ.
void xuLyMoiGiay(uint32_t trungBinh, uint32_t trungBinhDo) {
  tbCuoi = trungBinh;
  tbDoCuoi = trungBinhDo;
  bool cham = dangCho ? ppg::coChamDaKhiCho(trungBinh) : ppg::coChamDa(trungBinh);
  if (cham != chamDa) {
    chamDa = cham;
    boNhip.datLai();
    boSpO2.datLai();
  }
  giayKhongCham = chamDa ? 0 : giayKhongCham + 1;
#if TIET_KIEM_PIN
  if (ppg::choDuoc()) {
    if (!dangCho && giayKhongCham >= 30) {
      dangCho = true;
      soMau = 0;
      ppg::vaoCho();
      Serial.println("[PPG] Không thấy da 30 giây: vào chế độ chờ, ngừng gửi sóng");
    } else if (dangCho && chamDa) {
      dangCho = false;
      ppg::raCho();
      Serial.println("[PPG] Đã đeo lại: gửi sóng bình thường");
    }
  }
#endif
  if (!dangCho) {
    int dongTruoc = ppg::dongLed, doTruoc = ppg::dongLedDo;
    ppg::tuChinhDongLed(trungBinh);
    if (ppg::coKenhDo()) ppg::tuChinhDongLedDo(trungBinhDo);
    if (ppg::dongLed != dongTruoc) {  // đổi dòng LED làm mức tín hiệu nhảy bậc: cho bộ lọc học lại từ đầu
      boNhip.datLai();
      boSpO2.datLai();
    } else if (ppg::dongLedDo != doTruoc) {
      boSpO2.datLai();               // chỉ kênh đỏ đổi mức: SpO2 học lại, nhịp tim vẫn đếm tiếp
    }
  }
}

// Cảm biến 1 giây không ra mẫu (sụt áp làm chip tự khởi động lại ở chế độ tắt, lỏng dây I2C): cài lại từ đầu thay
// vì để vòng tay im lặng mãi. Lỗi kéo dài thì cứ mỗi giây thử lại.
void caiLaiCamBien() {
  static uint32_t soLan = 0;
  Wire.end();
  Wire.begin(CHAN_SDA, CHAN_SCL, tocDoI2C);
  Wire.setTimeOut(20);
  if (ppg::batDau(CHAN_ENABLE) != ppg::Loai::KHONG_CO && dangCho) ppg::vaoCho();
  chamDa = false;
  boNhip.datLai();
  boSpO2.datLai();
  soMau = dem1s = 0;
  tong1s = tongDo1s = 0;
  if (++soLan == 1 || soLan % 60 == 0)
    Serial.printf("[PPG] 1 giây không có mẫu, cài lại cảm biến (lần %lu): %s\n", (unsigned long)soLan, ppg::ten());
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // Serial qua cổng USB của chip: không mở Serial Monitor thì bỏ dòng in, không chờ
#endif
  delay(500);
  Serial.println("\n=== AIoT - Vong deo tay ===");
  setCpuFrequencyMhz(80);  // 80 MHz vẫn đủ cho Wi-Fi, đỡ tốn pin hơn 160 MHz
  pinMode(CHAN_INT, INPUT_PULLUP);
  pinMode(CHAN_BAO, OUTPUT);
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
#endif
  bao::dung();
  Wire.begin(CHAN_SDA, CHAN_SCL, tocDoI2C);
  Wire.setTimeOut(20);  // tránh treo I2C khi dây cảm biến lỏng

  // Vào mạng trước rồi mới tìm cảm biến: chưa thấy cảm biến thì vòng tay vẫn hiện trên dashboard ("chưa thấy cảm
  // biến PPG", các địa chỉ I2C trả lời nằm trong trạng thái), biết ngay lỗi ở cảm biến hay ở Wi-Fi.
  ket_noi::batDau("tay", khiKetNoi, khiNhanTin);
  while (ppg::batDau(CHAN_ENABLE) == ppg::Loai::KHONG_CO) {
    quetI2C();
    Serial.printf("[PPG] Không thấy AS7038RB (0x30), MAX30102 hay MAX30100 (0x57) ở SDA %d, SCL %d, I2C %lu kHz; bus "
                  "I2C trả lời: %s. Kiểm tra dây GND/SDA/SCL/nguồn; GY-MAX30100 thì kiểm tra điện trở kéo lên I2C "
                  "(HUONG_DAN.md)\n", CHAN_SDA, CHAN_SCL, (unsigned long)(tocDoI2C / 1000),
                  i2cThay[0] ? i2cThay : "không địa chỉ nào");
    guiTrangThai();
    if (tocDoI2C > 100000) {  // điện trở kéo lên yếu (GY-MAX30100 kéo lên 1,8 V): ở 100 kHz thường đọc đúng hơn
      tocDoI2C = 100000;
      Wire.setClock(tocDoI2C);
      continue;
    }
    delay(2000);
  }
  Serial.printf("[PPG] Cảm biến: %s (I2C %lu kHz)\n", ppg::ten(), (unsigned long)(tocDoI2C / 1000));
  guiTrangThai();          // báo dashboard đã thấy cảm biến
  bao::chay(80, 80, 160);  // 1 nhịp ngắn: đã khởi động xong
}

void loop() {
  // Ngủ tới lượt đọc FIFO kế tiếp, 20 ms một lần (FIFO giữ 160 ms với MAX30100, 320 ms với MAX30102), thay vì quay vòng
  // liên tục: CPU rảnh thì nghỉ chờ, đỡ tốn pin, tác vụ gửi MQTT và Wi-Fi có thời gian chạy. Gửi MQTT không chặn
  // (ket_noi::guiSau) nên mạng chậm không làm lỡ lượt đọc.
  static TickType_t mocDanhThuc = xTaskGetTickCount();
  xTaskDelayUntil(&mocDanhThuc, pdMS_TO_TICKS(20));
  xuLyLenhBao();
  bao::capNhat();
  uint32_t bay = millis();

  ppg::Mau moi[32];
  int n = ppg::doc(moi, 32);
  static uint32_t mocCoMau = millis();
  if (n > 0) {
    mocCoMau = bay;
  } else if (bay - mocCoMau > 1000) {
    caiLaiCamBien();
    mocCoMau = bay;
  }
  int64_t bayGio = esp_timer_get_time();
  for (int i = 0; i < n; i++) {
    uint32_t msMau = bay - (uint32_t)(n - 1 - i) * (1000 / ppg::TAN_SO);
    if (chamDa) {
      bool laNhip = boNhip.them(moi[i].chinh, msMau);
      if (ppg::coKenhDo()) boSpO2.them(moi[i].chinh, moi[i].kenhDo, laNhip, !boNhip.dangCuDong(msMau));
    }
    tong1s += moi[i].chinh;
    tongDo1s += moi[i].kenhDo;
    if (++dem1s == ppg::TAN_SO) {
      uint32_t tb = tong1s / dem1s, tbDo = tongDo1s / dem1s;
      tong1s = tongDo1s = 0;
      dem1s = 0;
      xuLyMoiGiay(tb, tbDo);
    }
    if (!dangCho) {
      if (soMau == 0) mocMauDau_us = bayGio - (int64_t)(n - 1 - i) * 1000000 / ppg::TAN_SO;
      goi[soMau++] = moi[i].chinh;
      if (soMau == MAU_MOI_GOI) {
        guiGoi();
        soMau = 0;
      }
    }
#if XEM_DO_THI
    Serial.printf("ppg:%lu\n", (unsigned long)(ppg::giaTriMax - moi[i].chinh));
#endif
  }
  capNhatNhiet(bay);

  static uint32_t mocChiSo = 0, mocTrangThai = 0;
  if (bay - mocChiSo > 2000) {
    mocChiSo = bay;
    guiChiSo();
  }
  if (bay - mocTrangThai > 30000) {
    mocTrangThai = bay;
    guiTrangThai();
#if !XEM_DO_THI
    Serial.printf("[PPG] %s · số đọc trung bình %lu/%lu (%.0f %% thang đo; đỏ %.0f %%) · dòng LED %d, đỏ %d · "
                  "chạm da %d · nhịp tim %d · SpO2 %d (R %.3f) · đã gửi %lu tin, mất %lu, bỏ %lu\n",
                  ppg::ten(), (unsigned long)tbCuoi, (unsigned long)ppg::giaTriMax, 100.0 * tbCuoi / ppg::giaTriMax,
                  100.0 * tbDoCuoi / ppg::giaTriMax, ppg::dongLed, ppg::dongLedDo, chamDa ? 1 : 0,
                  boNhip.nhipTim(millis()), boSpO2.phanTram(), boSpO2.tiSoR(), (unsigned long)ket_noi::soTinGui,
                  (unsigned long)ket_noi::soTinMat, (unsigned long)ket_noi::soTinBo);
#endif
  }
}
