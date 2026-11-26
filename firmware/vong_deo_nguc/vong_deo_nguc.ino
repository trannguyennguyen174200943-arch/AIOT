#include <Preferences.h>
#include "cau_hinh.h"
#include "ket_noi.h"
#include "te_nga.h"
#include "module_sim.h"
#include "ngung_tho.h"
#include "ai_nhip.h"

constexpr int CHAN_ECG = 36, CHAN_LO_DUONG = 34, CHAN_LO_AM = 35;
constexpr int CHAN_THO = 39;  // cảm biến thở ở dây đeo: màng PVDF qua mạch phân cực 1,65 V (hoặc FSR402), HUONG_DAN.md
constexpr int CHAN_SDA = 21, CHAN_SCL = 22;
constexpr int CHAN_SIM_RX = 16, CHAN_SIM_TX = 17, CHAN_SIM_RST = 4;  // A7670C: TX module → 16, RX module ← 17,
                                                                     // RESET (nếu mạch có) ← 4; KHÔNG nối PWRKEY vào 4
constexpr int CHAN_COI = 25;  // còi chủ động SFM-20B (kêu liên tục khi có điện) qua transistor NPN: code chỉ bật/tắt

constexpr int ECG_FS = 500, FSR_FS = 25;
constexpr int ECG_MOI_KHUNG = 100, FSR_MOI_KHUNG = 5;    // 1 khung = 0,2 giây
constexpr uint32_t DEM_NGUOC_TE_NGA_MS = 15000;           // còi kêu 15 s rồi mới báo người nhà; đứng dậy thì huỷ
constexpr uint32_t CACH_NHAU_TIN_NHAN_MS = 5 * 60000;     // cùng 1 lý do không nhắn lặp trong 5 phút
constexpr int NGUNG_THO_BAO_GIAY = 20;                    // ngưng thở từ 20 s: còi kêu đánh thức
constexpr uint32_t DEM_NGUOC_NGUNG_THO_MS = 15000;        // còi 15 s mà chưa thở lại mới báo người nhà
constexpr uint32_t MAT_MAY_CHU_SAU_MS = 15000;            // không thấy máy chủ AI 15 s thì vòng ngực tự lo
const char* const LY_DO_NGUNG_THO = "Nghi ngung tho keo dai (vong nguc tu phat hien khi mat ket noi may chu)";

struct Khung {
  int64_t moc_us;
  uint16_t ecg[ECG_MOI_KHUNG];
  uint16_t fsr[FSR_MOI_KHUNG];
  uint8_t tuot;  // 1 nếu có lúc điện cực ECG bị tuột trong khung
};

struct CanhBao {
  int muc;  // 0 huỷ, 1 nhẹ (kêu ngắn), 2 nguy cơ cao (kêu + nhắn tin)
  bool goi;
  char lyDo[100];
};

struct ImuGiay {
  float vanDong, conQuay;
  int muc;
};

QueueHandle_t hangKhung, hangTeNga, hangImu, hangCanhBao, hangSdt;
te_nga::BoPhatHien boTeNga;
Preferences boNho;
uint32_t soKhung = 0, soKhungBo = 0;
bool dangDemNguoc = false;
uint32_t mocDemNguoc = 0;
ngung_tho::BoPhatHien boNgungTho;
volatile bool aiOnline = false;  // máy chủ AI có bộ phát hiện nhịp thở đang chạy
uint32_t mocThayMayChu = 0;
bool demNguocTho = false;
uint32_t mocDemNguocTho = 0;
int mucVanDong = 0;              // 0 yên, 1 cử động nhẹ, 2 vận động (MPU6050, cập nhật mỗi giây)
// AI trên vòng (TinyML): chạy mỗi 5 giây trên 10 giây ECG gần nhất
ai_nhip::BoTimR boTimR(ECG_FS);
float xsAi[3] = {1, 0, 0};       // xác suất đã làm mượt: bình thường / bất thường nhẹ / nguy cơ cao
int nhipAi = 0, demCaoAi = 0, khungTuot10s = 0;
bool daBaoAi = false;
constexpr int AI_LAN_LIEN_TIEP = 6;          // nguy cơ cao 6 lần liền (~30 s) khi mất máy chủ thì tự báo
const char* const LY_DO_AI = "Nghi nhip tim nguy hiem (AI tren vong nguc tu phat hien khi mat ket noi may chu)";

// ---------- còi ----------
namespace coi {
uint16_t msBat = 0, msTat = 0;
uint32_t hetLuc = 0, mocDoi = 0;
bool dangKeu = false;

void dung() {
  hetLuc = 0;
  dangKeu = false;
  digitalWrite(CHAN_COI, LOW);
}

void keu(uint16_t bat, uint16_t tat, uint32_t trongMs) {
  msBat = bat;
  msTat = tat;
  hetLuc = millis() + trongMs;
  mocDoi = millis();
  dangKeu = true;
  digitalWrite(CHAN_COI, HIGH);
}

void capNhat() {
  if (hetLuc == 0) return;
  if ((int32_t)(millis() - hetLuc) >= 0) return dung();
  if (millis() - mocDoi >= (dangKeu ? msBat : msTat)) {
    dangKeu = !dangKeu;
    mocDoi = millis();
    digitalWrite(CHAN_COI, dangKeu ? HIGH : LOW);
  }
}
}  // namespace coi

// ---------- JSON phẳng tối giản cho các tin điều khiển ----------
int laySo(const char* json, const char* khoa, int macDinh) {
  char mau[24];
  snprintf(mau, sizeof mau, "\"%s\"", khoa);
  const char* p = strstr(json, mau);
  if (!p || !(p = strchr(p + strlen(mau), ':'))) return macDinh;
  return atoi(p + 1);
}

bool layChuoi(const char* json, const char* khoa, char* ra, size_t n) {
  char mau[24];
  snprintf(mau, sizeof mau, "\"%s\"", khoa);
  const char* p = strstr(json, mau);
  if (!p || !(p = strchr(p + strlen(mau), ':')) || !(p = strchr(p, '"'))) return false;
  const char* cuoi = strchr(++p, '"');
  if (!cuoi) return false;
  size_t dai = min((size_t)(cuoi - p), n - 1);
  memcpy(ra, p, dai);
  ra[dai] = 0;
  return true;
}

// ---------- tác vụ đọc cảm biến ----------
void tacVuLayMau(void*) {
  Khung k;
  int i = 0;
  uint32_t tongTho = 0;
  TickType_t moc = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&moc, pdMS_TO_TICKS(1000 / ECG_FS));  // đúng 2 ms một mẫu
    if (i == 0) {
      k.moc_us = esp_timer_get_time();
      k.tuot = 0;
    }
    uint32_t tong = 0;
    for (int j = 0; j < 4; j++) tong += analogRead(CHAN_ECG);  // trung bình 4 lần đọc cho bớt nhiễu ADC
    k.ecg[i] = tong / 4;
    if (digitalRead(CHAN_LO_DUONG) || digitalRead(CHAN_LO_AM)) k.tuot = 1;
    // Cảm biến thở: đọc mỗi 2 ms, cộng 20 lần (40 ms) thành 1 mẫu 25 Hz. Lấy trung bình giảm nhiễu ADC khoảng 4 lần
    // (tín hiệu màng PVDF nhỏ) và không để nhiễu nhanh (rung, điện lưới) lọt vào thành sóng chậm giả.
    tongTho += analogRead(CHAN_THO);
    if ((i + 1) % (ECG_FS / FSR_FS) == 0) {
      k.fsr[i / (ECG_FS / FSR_FS)] = tongTho / (ECG_FS / FSR_FS);
      tongTho = 0;
    }
    if (++i == ECG_MOI_KHUNG) {
      if (xQueueSend(hangKhung, &k, 0) != pdTRUE) soKhungBo++;
      i = 0;
    }
  }
}

void tacVuMpu(void*) {
  int loiLienTiep = 0;  // số lần đọc liền bị lỗi hoặc dữ liệu đứng yên
  TickType_t moc = xTaskGetTickCount();
  for (;;) {
    xTaskDelayUntil(&moc, pdMS_TO_TICKS(1000 / te_nga::TAN_SO));
    te_nga::Mau m;
    if (!te_nga::doc(m)) {
      // MPU6050 không trả lời hoặc đang ngủ (sụt áp lúc module 4G phát sóng, lỏng dây, chưa cắm lúc bật): mỗi giây cài
      // lại một lần, không để mất phát hiện té ngã mà không ai biết.
      if (++loiLienTiep % te_nga::TAN_SO == 0) {
        int who = te_nga::batDau();
        int giay = loiLienTiep / te_nga::TAN_SO;
        if (giay == 1 || giay % 60 == 0)
          Serial.printf("[MPU6050] %d giây không có dữ liệu, cài lại cảm biến: %s\n", giay, who < 0 ? "chưa thấy" : "OK");
      }
      continue;
    }
    loiLienTiep = 0;
    te_nga::SuKien sk = boTeNga.them(m, millis());
    if (sk != te_nga::SuKien::KHONG) xQueueSend(hangTeNga, &sk, 0);
    if (boTeNga.coSoLieuMoi) {
      boTeNga.coSoLieuMoi = false;
      ImuGiay g = {boTeNga.vanDong, boTeNga.conQuay, boTeNga.muc};
      xQueueOverwrite(hangImu, &g);
    }
  }
}

// ---------- MQTT ----------
void guiTrangThai() {
  char so[20], them[128];
  sim::laySo(so, sizeof so);
  snprintf(them, sizeof them,
           ",\"sim\":%d,\"song\":%d,\"sdt\":\"...%s\",\"tho_cuc_bo\":%d,\"bd_tho\":%d,\"tin_bo\":%lu",
           sim::trangThai, sim::doSong, strlen(so) > 3 ? so + strlen(so) - 3 : so, (int)boNgungTho.trangThai,
           (int)boNgungTho.binhThuong, (unsigned long)ket_noi::soTinBo);
  ket_noi::guiTrangThai(them);
}

void khiKetNoi() {
  ket_noi::dangKy("canh_bao");
  ket_noi::dangKy("cau_hinh");
  ket_noi::dangKy("radar/su_kien");
  ket_noi::dangKy("ai/trang_thai");
  guiTrangThai();
}

// true khi phải tự phát hiện ngưng thở: mất broker, hoặc máy chủ AI (có bộ phát hiện thở) không chạy.
bool tuLoNgungTho() {
  if (ket_noi::daKetNoi && aiOnline) mocThayMayChu = millis();
  return millis() - mocThayMayChu > MAT_MAY_CHU_SAU_MS;
}

// Chạy trong tác vụ MQTT: chỉ đọc tin rồi chuyển vào hàng đợi cho loop() xử lý.
void khiNhanTin(const char* chuDe, const char* s) {
  if (strstr(chuDe, "/canh_bao")) {
    CanhBao cb = {laySo(s, "muc", -1), laySo(s, "goi", 0) == 1, "Canh bao tu may chu"};
    if (cb.muc < 0 && isdigit((unsigned char)s[0])) cb.muc = atoi(s);  // cho phép gửi tay chỉ 1 chữ số
    layChuoi(s, "ly_do", cb.lyDo, sizeof cb.lyDo);
    if (cb.muc >= 0) xQueueSend(hangCanhBao, &cb, 0);
  } else if (strstr(chuDe, "/cau_hinh")) {
    char so[20];
    if (layChuoi(s, "sdt", so, sizeof so) && strlen(so) >= 9) xQueueSend(hangSdt, so, 0);
  } else if (strstr(chuDe, "/radar/su_kien")) {
    char loai[16] = "";
    if (layChuoi(s, "loai", loai, sizeof loai) && strcmp(loai, "sos") == 0) {
      CanhBao cb = {2, true, "Nguoi benh bam nut SOS tren thiet bi radar"};
      xQueueSend(hangCanhBao, &cb, 0);
    }
  } else if (strstr(chuDe, "/ai/trang_thai")) {
    aiOnline = laySo(s, "online", 0) == 1 && laySo(s, "tho", 1) == 1;
  }
}

void guiKhung(const Khung& k) {
  static char s[900];
  int p = snprintf(s, sizeof s, "{\"n\":%lu,\"t\":%llu,\"lo\":%d,\"ecg\":[", (unsigned long)soKhung++,
                   (unsigned long long)ket_noi::doiGioThuc(k.moc_us), k.tuot);
  for (int i = 0; i < ECG_MOI_KHUNG; i++) p += snprintf(s + p, sizeof s - p, i ? ",%u" : "%u", k.ecg[i]);
  p += snprintf(s + p, sizeof s - p, "],\"fsr\":[");
  for (int i = 0; i < FSR_MOI_KHUNG; i++) p += snprintf(s + p, sizeof s - p, i ? ",%u" : "%u", k.fsr[i]);
  snprintf(s + p, sizeof s - p, "]}");
  ket_noi::guiSau("tin_hieu", s);
}

void guiSuKien(const char* loai, const char* chiTiet) {
  char s[200];
  snprintf(s, sizeof s, "{\"t\":%llu,\"loai\":\"%s\",\"chi_tiet\":\"%s\"}",
           (unsigned long long)ket_noi::gioThucMs(), loai, chiTiet);
  ket_noi::guiSau("su_kien", s, false, true);
  Serial.printf("[Sự kiện] %s: %s\n", loai, chiTiet);
}

// ---------- báo động ----------
void baoNguoiThan(const char* lyDo, bool goi) {
  static char lyDoTruoc[100] = "";
  static uint32_t mocTruoc = 0;
  if (mocTruoc && strcmp(lyDo, lyDoTruoc) == 0 && millis() - mocTruoc < CACH_NHAU_TIN_NHAN_MS) return;
  strlcpy(lyDoTruoc, lyDo, sizeof lyDoTruoc);
  mocTruoc = millis();

  char gio[8] = "";
  tm t;
  if (getLocalTime(&t, 0)) snprintf(gio, sizeof gio, " %02d:%02d", t.tm_hour, t.tm_min);
  char tin[161];
  snprintf(tin, sizeof tin, "AIoT CANH BAO [%s]%s: %s. Hay kiem tra nguoi benh ngay!", MA_NGUOI_DUNG, gio, lyDo);
  sim::nhanTin(tin);
  if (goi) sim::goiDien();
  guiSuKien("bao_nguoi_than", lyDo);
}

void xuLyCanhBao(const CanhBao& cb) {
  if (cb.muc == 0) {
    coi::dung();
    boTeNga.datLai();
    dangDemNguoc = false;
    demNguocTho = false;
    guiSuKien("da_huy", cb.lyDo);
  } else if (cb.muc == 1) {
    coi::keu(150, 150, 600);  // 2 tiếng bíp ngắn
    guiSuKien("canh_bao_nhe", cb.lyDo);
  } else {
    coi::keu(800, 400, 30000);
    baoNguoiThan(cb.lyDo, cb.goi);
  }
}

void xuLyTeNga(te_nga::SuKien sk) {
  if (sk == te_nga::SuKien::NGA && !dangDemNguoc) {
    dangDemNguoc = true;
    mocDemNguoc = millis();
    coi::keu(200, 200, DEM_NGUOC_TE_NGA_MS);
    guiSuKien("te_nga", "Phat hien te nga, cho 15 giay truoc khi bao nguoi nha");
  } else if (sk == te_nga::SuKien::DUNG_DAY) {
    if (dangDemNguoc) guiSuKien("huy_te_nga", "Nguoi dung tu dung day trong luc dem nguoc");
    else guiSuKien("da_dung_day", "Nguoi dung da dung day sau khi nga");
    dangDemNguoc = false;
    coi::dung();
  }
}

// Mỗi giây (5 khung): cập nhật bộ phát hiện ngưng thở cục bộ.
void xuLyNgungTho(bool dienCucTuot) {
  ngung_tho::SuKien sk = boNgungTho.moiGiay(dienCucTuot, mucVanDong >= 2);
  if (sk != ngung_tho::SuKien::KHONG) {
    if (demNguocTho) {
      demNguocTho = false;
      coi::dung();
      if (sk == ngung_tho::SuKien::THO_LAI) guiSuKien("tho_lai", "Nguoi deo da tho lai, huy bao ngung tho");
      else guiSuKien("thao_vong", "Vong nguc bi thao ra, huy bao ngung tho");
    }
    return;
  }
  // Chỉ tự báo khi không có máy chủ AI; có máy chủ thì để mô hình trên máy chủ quyết định.
  if (!demNguocTho && tuLoNgungTho() && boNgungTho.vuaDu(NGUNG_THO_BAO_GIAY)) {
    demNguocTho = true;
    mocDemNguocTho = millis();
    coi::keu(300, 300, DEM_NGUOC_NGUNG_THO_MS);
    char s[96];
    snprintf(s, sizeof s, "Nghi ngung tho %d giay, coi keu 15 giay roi bao nguoi nha", boNgungTho.giayNgung());
    guiSuKien("ngung_tho", s);
  }
}

// Mỗi 5 giây: AI trên vòng phân tích 10 giây ECG gần nhất.
void xuLyAiVong() {
  if (khungTuot10s > 0) {  // điện cực tuột trong 10 giây qua: kết quả không đáng tin
    demCaoAi = 0;
    return;
  }
  float f[ai_nhip::SO_DT], p[3];
  int so = boTimR.dacTrung(f);
  if (so < 5) return;  // chưa đủ nhịp (vừa đeo, hoặc tín hiệu quá nhiễu)
  ai_nhip::duDoan(f, p);
  for (int i = 0; i < 3; i++) xsAi[i] = 0.5f * xsAi[i] + 0.5f * p[i];
  nhipAi = (int)lroundf(f[0]);
  int lop = 0;
  for (int i = 1; i < 3; i++)
    if (xsAi[i] > xsAi[lop]) lop = i;
  char s[160];
  snprintf(s, sizeof s, "{\"t\":%llu,\"lop\":\"%s\",\"xs\":[%.3f,%.3f,%.3f],\"nhip\":%d}",
           (unsigned long long)ket_noi::gioThucMs(), tinyml::TEN_LOP[lop], xsAi[0], xsAi[1], xsAi[2], nhipAi);
  ket_noi::guiSau("ai", s);
  demCaoAi = xsAi[2] >= 0.7f ? demCaoAi + 1 : 0;
  if (xsAi[2] < 0.5f) daBaoAi = false;  // đã trở lại bình thường: lần sau được báo tiếp
  // Chỉ tự báo khi không có máy chủ AI; có máy chủ thì mô hình lớn trên máy chủ quyết định.
  if (!daBaoAi && demCaoAi >= AI_LAN_LIEN_TIEP && tuLoNgungTho()) {
    daBaoAi = true;
    coi::keu(600, 300, 20000);
    baoNguoiThan(LY_DO_AI, false);  // chỉ nhắn tin, không gọi (AI nhỏ chính xác thấp hơn mô hình trên máy chủ)
  }
}

// ---------- chương trình chính ----------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== AIoT - Vong deo nguc ===");
  setCpuFrequencyMhz(80);  // 80 MHz đủ cho lấy mẫu ECG, AI nhỏ trên vòng và Wi-Fi; đỡ tốn pin hơn 240 MHz mặc định
  pinMode(CHAN_COI, OUTPUT);
  digitalWrite(CHAN_COI, LOW);
  pinMode(CHAN_LO_DUONG, INPUT);
  pinMode(CHAN_LO_AM, INPUT);
  analogReadResolution(12);

  boNho.begin("aiot", false);
  String so = boNho.getString("sdt", SDT_MAC_DINH);
  sim::datSo(so.c_str());
  Serial.printf("[SIM] Số người nhà: %s\n", so.c_str());

  Wire.begin(CHAN_SDA, CHAN_SCL, 400000);
  Wire.setTimeOut(20);  // tránh treo tác vụ MPU khi nhiễu dây I2C
  int who = te_nga::batDau();
  if (who < 0) Serial.println("[MPU6050] Không thấy cảm biến ở 0x68: chưa phát hiện được té ngã, vòng thử lại mỗi giây");
  else Serial.printf("[MPU6050] OK (WHO_AM_I = 0x%02X)\n", who);

  hangKhung = xQueueCreate(40, sizeof(Khung));  // giữ được 8 giây nếu mạng chậm
  hangTeNga = xQueueCreate(4, sizeof(te_nga::SuKien));
  hangImu = xQueueCreate(1, sizeof(ImuGiay));
  hangCanhBao = xQueueCreate(4, sizeof(CanhBao));
  hangSdt = xQueueCreate(2, 20);

  sim::batDau(CHAN_SIM_RX, CHAN_SIM_TX, CHAN_SIM_RST);
  ket_noi::batDau("nguc", khiKetNoi, khiNhanTin);
  xTaskCreatePinnedToCore(tacVuLayMau, "ecg", 4096, nullptr, 5, nullptr, 1);
  xTaskCreatePinnedToCore(tacVuMpu, "mpu", 4096, nullptr, 4, nullptr, 1);  // chạy cả khi chưa thấy MPU6050

  coi::keu(80, 80, 160);  // 1 tiếng bíp: đã khởi động xong
}

void loop() {
  Khung k;
  while (xQueueReceive(hangKhung, &k, 0) == pdTRUE) {
    guiKhung(k);
    static int khungTrongGiay = 0;
    static bool tuotTrongGiay = false;
    for (int i = 0; i < FSR_MOI_KHUNG; i++) boNgungTho.them(k.fsr[i]);
    tuotTrongGiay |= k.tuot;
    static int khungAi = 0;
    if (k.tuot) {
      boTimR.datLai();    // điện cực tuột: đặt lại bộ lọc để học ngưỡng 2 s đầu khi gắn lại
      xsAi[0] = 1;
      xsAi[1] = xsAi[2] = 0;
      daBaoAi = false;
      khungTuot10s = 50;  // 50 khung = 10 giây
    } else {
      for (int i = 0; i < ECG_MOI_KHUNG; i++) boTimR.them(k.ecg[i]);
      if (khungTuot10s > 0) khungTuot10s--;
    }
    if (++khungAi == 25) {          // 25 khung = 5 giây
      khungAi = 0;
      xuLyAiVong();
    }
    if (++khungTrongGiay == FSR_FS / FSR_MOI_KHUNG) {
      xuLyNgungTho(tuotTrongGiay);
      khungTrongGiay = 0;
      tuotTrongGiay = false;
    }
    static bool tuotTruoc = false;
    if (k.tuot != tuotTruoc) {
      tuotTruoc = k.tuot;
      Serial.println(k.tuot ? "[ECG] Điện cực bị tuột!" : "[ECG] Điện cực đã gắn lại");
    }
#if XEM_DO_THI
    for (int i = 0; i < ECG_MOI_KHUNG; i += 5)
      Serial.printf("ecg:%u tho:%u\n", k.ecg[i], k.fsr[i / (ECG_FS / FSR_FS)]);
#endif
  }

  ImuGiay g;
  if (xQueueReceive(hangImu, &g, 0) == pdTRUE) {
    mucVanDong = g.muc;
    char s[120];
    snprintf(s, sizeof s, "{\"t\":%llu,\"muc\":%d,\"vd\":%.3f,\"cq\":%.1f}",
             (unsigned long long)ket_noi::gioThucMs(), g.muc, g.vanDong, g.conQuay);
    ket_noi::guiSau("imu", s);
  }

  te_nga::SuKien sk;
  while (xQueueReceive(hangTeNga, &sk, 0) == pdTRUE) xuLyTeNga(sk);
  if (dangDemNguoc && millis() - mocDemNguoc > DEM_NGUOC_TE_NGA_MS) {
    dangDemNguoc = false;
    coi::keu(800, 400, 60000);
    baoNguoiThan("Phat hien te nga, nguoi benh chua dung day", true);
  }
  if (demNguocTho && millis() - mocDemNguocTho > DEM_NGUOC_NGUNG_THO_MS) {
    demNguocTho = false;
    coi::keu(800, 400, 60000);
    baoNguoiThan(LY_DO_NGUNG_THO, true);
  }

  CanhBao cb;
  while (xQueueReceive(hangCanhBao, &cb, 0) == pdTRUE) xuLyCanhBao(cb);

  char so[20];
  if (xQueueReceive(hangSdt, so, 0) == pdTRUE) {
    sim::datSo(so);
    boNho.putString("sdt", so);
    Serial.printf("[SIM] Đã đổi số người nhà thành %s\n", so);
  }

  sim::KetQua kq;
  while (xQueueReceive(sim::hangKetQua, &kq, 0) == pdTRUE)
    guiSuKien(kq.viec == sim::Viec::NHAN_TIN ? (kq.ok ? "da_nhan_tin" : "loi_nhan_tin")
                                             : (kq.ok ? "da_goi_dien" : "loi_goi_dien"),
              kq.ok ? "SIM da gui" : "SIM khong gui duoc, xem lai song/nguon cua module");

  coi::capNhat();

  static uint32_t mocTrangThai = 0;
  if (millis() - mocTrangThai > 30000) {
    mocTrangThai = millis();
    guiTrangThai();
#if !XEM_DO_THI
    static const char* const TEN_THO[] = {"chưa đeo", "đang học", "bình thường", "NGƯNG THỞ"};
    Serial.printf("[Trạng thái] MQTT %s, máy chủ AI %s, đã gửi %lu khung, bỏ %lu, mạng nghẽn bỏ %lu tin, SIM %d (sóng %d), "
                  "thở cục bộ: %s (biên độ %.0f / bình thường %.0f), AI trên vòng: %.0f%% BT / %.0f%% nhẹ / %.0f%% cao, "
                  "nhịp %d\n",
                  ket_noi::daKetNoi ? "OK" : "chưa", tuLoNgungTho() ? "không thấy (vòng tự phát hiện ngưng thở)" : "OK",
                  (unsigned long)soKhung, (unsigned long)soKhungBo, (unsigned long)ket_noi::soTinBo, sim::trangThai,
                  sim::doSong,
                  TEN_THO[(int)boNgungTho.trangThai], boNgungTho.bienDo, boNgungTho.binhThuong, xsAi[0] * 100,
                  xsAi[1] * 100, xsAi[2] * 100, nhipAi);
#endif
  }
  delay(2);
}
