// Thiết bị theo dõi khi ngủ — radar 60 GHz của Seeed (không chạm vào người), tự nhận 2 loại:
//   - Bộ kit MR60BHA2 gắn sẵn XIAO ESP32C6 (nhóm đang dùng, chạy đúng trên kit thật ngày 26/11/2026): radar gửi pha
//     thở, pha tim, nhịp thở, nhịp tim, khoảng cách; firmware gom pha thành 5 điểm sóng mỗi giây, chuẩn hoá về 0–255
//     quanh 128 giống MR60BHA1.
//   - MR60BHA1 nối vào XIAO ESP32C3 / ESP32C6 (Hình C): ở chế độ thời gian thực, mỗi giây radar gửi 5 điểm sóng thở
//     và 5 điểm sóng tim (0–255, trục giữa 128).
// Xử lý ngay trên thiết bị (xu_ly_radar.h), cho cả 2 loại radar:
//   - nhịp tim, nhịp thở: trung vị 5 số gần nhất, bỏ số nhảy vọt; kèm chất lượng cl (100 tốt, 50 đang cử động hoặc
//     số chưa ổn định, 0 chưa có) để máy chủ chỉ lấy số đáng tin;
//   - cử động người: MR60BHA2 tự ước lượng từ pha, MR60BHA1 do radar báo;
//   - ngưng cử động thở ≥ 10 giây lúc người nằm yên: đếm số lần trong 1 giờ, từ 20 giây thì gửi sự kiện;
//   - bộ kit có cảm biến ánh sáng: phòng tối thì đèn trạng thái gần như tắt, đèn cảnh báo vẫn sáng rõ.
// Gửi lên MQTT:
//   aiot/<mã>/radar/song     {"n":..,"t":<ms>,"fs":5,"tho":[5 số]}  và  {"n":..,"t":..,"fs":5,"tim":[5 số]}
//   aiot/<mã>/radar/chi_so   3 giây/lần: nhịp tim, nhịp thở, kiểu thở, có người, vận động, mức cử động, khoảng cách,
//                            cl, ngung_giay (khoảng ngưng đang diễn ra), lan_ngung (số lần ngưng ≥ 10 s trong 1 giờ)
//   aiot/<mã>/radar/su_kien  {"loai":"sos"} khi người bệnh giữ nút 2 giây; {"loai":"radar_ngung_tho","chi_tiet":"20"}
//                            khi ngưng cử động thở 20 giây; {"loai":"radar_tho_lai","chi_tiet":"<số giây đã ngưng>"}
// Nghe: aiot/<mã>/canh_bao → đèn xanh lá: bình thường, vàng: cảnh báo nhẹ, đỏ nhấp nháy: nguy cơ cao.
// Máy chủ trừ 128, lọc rồi chuẩn hoá z-score từng cửa sổ. Serial in chẩn đoán 10 giây/lần (3 phút đầu), sau đó
// 1 phút/lần: số byte, số khung từng loại, lỗi tổng kiểm tra, biên độ pha, độ sáng phòng.
// Nối chân: bộ kit MR60BHA2 đã nối sẵn (radar ở D6, D7; đèn RGB ở D1; cảm biến ánh sáng ở D4, D5), chỉ cần cắm USB-C.
//   MR60BHA1 (Hình C): radar 5V, GND, RX←D6, TX→D7; đèn WS2812B DIN←D0 (qua 330 Ω, đặt CHAN_DEN_RGB D0).
//   Nút SOS (tuỳ chọn) giữa D3 và GND.
// Board: XIAO_ESP32C6 (bộ kit) hoặc XIAO_ESP32C3 (gói "esp32" của Espressif, bản 3.x).
#include "cau_hinh.h"
#include "ket_noi.h"
#include "mr60bha1.h"
#include "mr60bha2.h"
#include "xu_ly_radar.h"

constexpr int CHAN_DEN = CHAN_DEN_RGB, CHAN_NUT = D3;
HardwareSerial& radar = Serial1;

enum class LoaiRadar : uint8_t { CHUA_RO, BHA1, BHA2 };
LoaiRadar loaiRadar = LoaiRadar::CHUA_RO;
mr60::BoDoc boDoc;    // MR60BHA1
mr60b::BoDoc boDoc2;  // MR60BHA2

// Số đo gửi lên máy chủ (chung cho 2 loại radar)
xu_ly::LocNhip locTim, locTho;
xu_ly::BoNgungTho boNgung;
int kieuTho = -1, coNguoi = -1, vanDong = -1, cuongDo = -1, khoangCach = -1;

// MR60BHA2: gom pha thở, pha tim nhận trong mỗi 200 ms thành 1 điểm sóng (5 điểm/giây như MR60BHA1)
xu_ly::ChuanHoa chuanTho, chuanTim;
float tongPhaTho = 0, tongPhaTim = 0, phaThoCuoi = 0, phaTimCuoi = 0, cuongDoTB = 0;
int demPha = 0, soDiem2 = 0, songTho2[5], songTim2[5];
uint32_t mocDiem2 = 0, mocThayNguoi = 0, hetCuDong = 0;
int coNguoiRadar = -1;        // khung "có người" (0x0F09) nếu firmware radar có gửi, -1 = chưa thấy
bool daThayKieuBha2 = false;  // đã thấy khung pha / nhịp / khoảng cách (MR60FDA2 không có các khung này)

uint32_t soGoiTho = 0, soGoiTim = 0;
uint32_t mocSongCuoi = 0, mocLenhCheDo = 0;
volatile int mucCanhBao = 0;
volatile bool laThuCoi = false;
volatile uint32_t mocCanhBao = 0;
bool denBat = true;
bool coNut = true;      // false: chân nút SOS ở mức thấp lúc khởi động (không lắp nút) → bỏ qua nút
bool phongToi = false;  // cảm biến ánh sáng báo phòng tối
float luxCuoi = -1;
uint32_t mocSOS = 0;

// Chẩn đoán
uint32_t soByte = 0, demBha1 = 0, demKieu2[6] = {0};  // pha, nhịp thở, nhịp tim, khoảng cách, có người, khác
uint8_t byteDau[16];
int soByteDau = 0;

int laySo(const char* json, const char* khoa, int macDinh) {
  char mau[24];
  snprintf(mau, sizeof mau, "\"%s\"", khoa);
  const char* p = strstr(json, mau);
  if (!p || !(p = strchr(p + strlen(mau), ':'))) return macDinh;
  return atoi(p + 1);
}

void khiKetNoi() {
  ket_noi::dangKy("canh_bao");
  ket_noi::guiTrangThai();
}

void khiNhanTin(const char* chuDe, const char* s) {
  int muc = laySo(s, "muc", -1);
  if (muc < 0 && isdigit((unsigned char)s[0])) muc = atoi(s);
  if (muc >= 0) {
    mucCanhBao = muc;
    laThuCoi = laySo(s, "thu", 0) == 1;
    mocCanhBao = millis();
  }
}

void guiSong(const char* loai, uint32_t& soGoi, const int* v) {
  char s[160];
  uint64_t bayGio = ket_noi::gioThucMs();
  // 5 điểm trải đều trong 1 giây vừa qua: điểm đầu cách lúc nhận 0,8 s
  snprintf(s, sizeof s, "{\"n\":%lu,\"t\":%llu,\"fs\":5,\"%s\":[%d,%d,%d,%d,%d]}", (unsigned long)soGoi++,
           (unsigned long long)(bayGio ? bayGio - 800 : 0), loai, v[0], v[1], v[2], v[3], v[4]);
  ket_noi::guiSau("song", s);
#if XEM_DO_THI
  for (int i = 0; i < 5; i++) Serial.printf("%s:%d\n", loai, v[i]);
#endif
}

void guiSuKien(const char* loai, int giay) {
  char s[112];
  snprintf(s, sizeof s, "{\"t\":%llu,\"loai\":\"%s\",\"chi_tiet\":\"%d\"}", (unsigned long long)ket_noi::gioThucMs(),
           loai, giay);
  ket_noi::guiSau("su_kien", s, false, true);
  Serial.printf("[Radar] %s: %d giây\n", loai, giay);
}

void datLoai(LoaiRadar l) {
  if (loaiRadar == l) return;
  loaiRadar = l;
  Serial.printf("[Radar] Nhận ra radar %s\n", l == LoaiRadar::BHA2 ? "MR60BHA2 (bộ kit có XIAO ESP32C6)" : "MR60BHA1");
}

// 1 điểm sóng thở (đơn vị tuỳ loại radar) cho bộ phát hiện ngưng cử động thở.
void choBoNgung(float x, uint32_t ms) {
  xu_ly::BoNgungTho::SuKien sk = boNgung.them(x, coNguoi == 1, vanDong == 2, ms);
  if (sk == xu_ly::BoNgungTho::BAO) guiSuKien("radar_ngung_tho", boNgung.giayNgung);
  else if (sk == xu_ly::BoNgungTho::THO_LAI) guiSuKien("radar_tho_lai", boNgung.giayVua);
}

// ---------- MR60BHA1 ----------
void xuLyKhung(const mr60::Khung& k) {
  uint32_t bay = millis();
  if (k.dai == 5 && k.lenh == 0x05 && (k.dk == 0x81 || k.dk == 0x85)) {
    int v[5];
    for (int i = 0; i < 5; i++) v[i] = k.dl[i];
    if (k.dk == 0x81) {
      guiSong("tho", soGoiTho, v);
      for (int i = 0; i < 5; i++) choBoNgung(v[i] - 128.0f, bay - (4 - i) * 200);
    } else {
      guiSong("tim", soGoiTim, v);
    }
    mocSongCuoi = bay;
    return;
  }
  if (k.dai == 0) return;
  switch ((k.dk << 8) | k.lenh) {
    case 0x8502: locTim.them(k.dl[0], bay); break;  // nhịp tim (lần/phút), 3 s/lần
    case 0x8102: locTho.them(k.dl[0], bay); break;  // nhịp thở (lần/phút)
    case 0x8101: kieuTho = k.dl[0]; break;          // 1 bình thường, 2 thở nhanh, 3 thở chậm, 4 không đo được
    case 0x8001:
      coNguoi = k.dl[0];                            // 0 không có người, 1 có người
      if (coNguoi == 0) {
        locTim.datLai();
        locTho.datLai();
        kieuTho = -1;
        cuongDo = 0;
      }
      break;
    case 0x8002: vanDong = k.dl[0]; break;          // 0 không, 1 nằm yên, 2 đang cử động
    case 0x8003: cuongDo = k.dl[0]; break;          // mức cử động cơ thể 0–100
    case 0x8004:
      if (k.dai >= 2) khoangCach = (k.dl[0] << 8) | k.dl[1];  // cm
      break;
  }
}

// ---------- MR60BHA2 ----------
// Pha (radian) → sóng; nhịp thở, nhịp tim là số thực; khoảng cách (cm) kèm cờ có mục tiêu.
void xuLyKhung2(const mr60b::Khung& k) {
  const uint8_t* d = k.dl;
  uint32_t bay = millis();
  switch (k.kieu) {
    case mr60b::PHA_THO_TIM:
      demKieu2[0]++;
      daThayKieuBha2 = true;
      if (k.dai < 12) return;
      tongPhaTho += mr60b::soThuc(d + 4);
      tongPhaTim += mr60b::soThuc(d + 8);
      demPha++;
      mocSongCuoi = bay;
      break;
    case mr60b::NHIP_THO:
    case mr60b::NHIP_TIM: {
      bool laTho = k.kieu == mr60b::NHIP_THO;
      demKieu2[laTho ? 1 : 2]++;
      daThayKieuBha2 = true;
      if (k.dai < 4) return;
      float f = mr60b::soThuc(d);
      if (f > 0 && f < (laTho ? 80 : 220)) {  // 0 hoặc số vô lý = chưa đo được
        (laTho ? locTho : locTim).them((int)lroundf(f), bay);
        mocThayNguoi = bay;
      }
      break;
    }
    case mr60b::KHOANG_CACH: {
      demKieu2[3]++;
      daThayKieuBha2 = true;
      if (k.dai < 8) return;
      float cm = mr60b::soThuc(d + 4);
      bool co = mr60b::soNguyen(d) != 0 && cm > 0 && cm < 1000;
      khoangCach = co ? (int)lroundf(cm) : -1;
      if (co) mocThayNguoi = bay;
      break;
    }
    case mr60b::CO_NGUOI:
      demKieu2[4]++;
      if (k.dai < 1) return;
      coNguoiRadar = d[0] ? 1 : 0;
      if (d[0]) mocThayNguoi = bay;
      break;
    default:
      demKieu2[5]++;  // đám điểm, phiên bản firmware...: không dùng
      break;
  }
}

// Có người: theo khung "có người" nếu firmware radar có gửi; nếu không, coi là có người khi trong 5 giây qua còn
// thấy khoảng cách hợp lệ hoặc nhịp thở, nhịp tim. Một khung lẻ báo không có mục tiêu không xoá số đang đo.
void capNhatCoNguoi2() {
  uint32_t bay = millis();
  if (coNguoiRadar >= 0) coNguoi = coNguoiRadar;
  else coNguoi = (mocThayNguoi && bay - mocThayNguoi < 5000) ? 1 : 0;
  if (coNguoi == 0) {
    locTim.datLai();
    locTho.datLai();
    khoangCach = -1;
  }
}

// Mỗi 200 ms lấy trung bình các pha vừa nhận thành 1 điểm; đủ 5 điểm (1 giây) thì gửi như sóng của MR60BHA1.
// Cử động: pha lệch khỏi nền gấp nhiều lần độ lệch thường ngày (thở chỉ lệch khoảng 1–1,6 lần) → giữ cờ 3 giây.
void laySongBha2() {
  uint32_t bay = millis();
  if (bay - mocDiem2 < 200) return;
  mocDiem2 = (bay - mocDiem2 > 1000) ? bay : mocDiem2 + 200;
  capNhatCoNguoi2();
  if (bay - mocSongCuoi > 2000) {  // hơn 2 giây không có pha: ngừng gửi sóng, bỏ điểm dở
    soDiem2 = demPha = 0;
    tongPhaTho = tongPhaTim = 0;
    return;
  }
  if (demPha) {
    phaThoCuoi = tongPhaTho / demPha;
    phaTimCuoi = tongPhaTim / demPha;
  }
  tongPhaTho = tongPhaTim = 0;
  demPha = 0;
  if (coNguoi != 0) {
    bool dangCuDong = hetCuDong && (int32_t)(hetCuDong - bay) > 0;
    songTho2[soDiem2] = chuanTho.ra(phaThoCuoi, dangCuDong);
    songTim2[soDiem2] = chuanTim.ra(phaTimCuoi, dangCuDong);
    if (chuanTho.tiLe > 6 || chuanTim.tiLe > 10) hetCuDong = bay + 3000;
    float muc = (fmaxf(chuanTho.tiLe, chuanTim.tiLe * 0.6f) - 1.5f) * 15.0f;  // ước lượng mức cử động 0–100
    cuongDoTB += 0.2f * (fminf(fmaxf(muc, 0), 100) - cuongDoTB);
    vanDong = (hetCuDong && (int32_t)(hetCuDong - bay) > 0) ? 2 : 1;
    cuongDo = (int)lroundf(cuongDoTB);
  } else {  // không có người: sóng phẳng ở 128 (máy chủ cũng bỏ qua radar), bộ chuẩn hoá đứng yên chờ người quay lại
    songTho2[soDiem2] = songTim2[soDiem2] = 128;
    vanDong = cuongDo = 0;
    cuongDoTB = 0;
  }
  choBoNgung(phaThoCuoi, bay);
  if (++soDiem2 == 5) {
    soDiem2 = 0;
    guiSong("tho", soGoiTho, songTho2);
    guiSong("tim", soGoiTim, songTim2);
  }
}

// ---------- chỉ số, chẩn đoán ----------
// Chất lượng: 0 không có người hoặc chưa có số; 50 đang cử động hoặc số còn nhảy (lệch nhau > 20 %); 100 tốt.
int chatLuong(uint32_t ms) {
  int tim = locTim.giaTri(ms), tho = locTho.giaTri(ms);
  if (coNguoi != 1 || (tim < 0 && tho < 0)) return 0;
  bool onDinh = (tim < 0 || locTim.onDinh(ms)) && (tho < 0 || locTho.onDinh(ms));
  return (vanDong == 2 || !onDinh) ? 50 : 100;
}

void guiChiSo() {
  uint32_t bay = millis();
  int nhipTim = locTim.giaTri(bay), nhipTho = locTho.giaTri(bay), cl = chatLuong(bay);
  int lanNgung = boNgung.lanTrong1Gio(bay);
  char s[256];
  snprintf(s, sizeof s,
           "{\"t\":%llu,\"nhip_tim\":%d,\"nhip_tho\":%d,\"kieu_tho\":%d,\"co_nguoi\":%d,\"van_dong\":%d,"
           "\"cuong_do\":%d,\"khoang_cach\":%d,\"cl\":%d,\"ngung_giay\":%d,\"lan_ngung\":%d}",
           (unsigned long long)ket_noi::gioThucMs(), nhipTim, nhipTho, kieuTho, coNguoi, vanDong, cuongDo,
           khoangCach, cl, boNgung.giayNgung, lanNgung);
  ket_noi::guiSau("chi_so", s);
#if !XEM_DO_THI
  Serial.printf("[Radar] %s · có người %d · nhịp tim %d · nhịp thở %d · cl %d · cử động %d · cách %d cm · "
                "ngưng %d s, %d lần/giờ\n",
                loaiRadar == LoaiRadar::BHA2 ? "MR60BHA2" : "MR60BHA1", coNguoi, nhipTim, nhipTho, cl, cuongDo,
                khoangCach, boNgung.giayNgung, lanNgung);
#endif
}

// In chẩn đoán: 10 giây/lần trong 3 phút đầu, sau đó 1 phút/lần.
void inChanDoan() {
  static uint32_t moc = 0;
  uint32_t chuKy = millis() < 180000 ? 10000 : 60000;
  if (millis() - moc < chuKy) return;
  moc = millis();
  uint32_t khung2 = 0;
  for (uint32_t d : demKieu2) khung2 += d;
  if (soByte == 0) {
    Serial.println("[Radar] Không nhận được byte nào từ radar. Bộ kit MR60BHA2: chọn đúng board XIAO_ESP32C6, rút "
                   "USB rồi cắm lại. MR60BHA1 tự lắp: kiểm tra nguồn 5 V, dây TX radar → D7, RX radar → D6.");
  } else {
    Serial.printf("[Radar] %lu s qua: %lu byte. MR60BHA2: pha %lu, nhịp thở %lu, nhịp tim %lu, khoảng cách %lu, có "
                  "người %lu, khung khác %lu, sai tổng kiểm tra %lu/%lu. MR60BHA1: %lu khung\n",
                  (unsigned long)(chuKy / 1000), (unsigned long)soByte, (unsigned long)demKieu2[0],
                  (unsigned long)demKieu2[1], (unsigned long)demKieu2[2], (unsigned long)demKieu2[3],
                  (unsigned long)demKieu2[4], (unsigned long)demKieu2[5], (unsigned long)boDoc2.loiDau,
                  (unsigned long)boDoc2.loiDuLieu, (unsigned long)demBha1);
    if (loaiRadar == LoaiRadar::BHA2)
      Serial.printf("[Radar] Biên độ pha: thở %.4f rad, tim %.4f rad\n", chuanTho.bienDo, chuanTim.bienDo);
    if (khung2 == 0 && demBha1 == 0) {
      Serial.print("[Radar] Có byte nhưng không ghép được khung nào (sai tốc độ UART hoặc loại radar khác). 16 byte đầu:");
      for (int i = 0; i < soByteDau; i++) Serial.printf(" %02X", byteDau[i]);
      Serial.println();
    }
  }
  if (anh_sang::dc) Serial.printf("[Ánh sáng] %.0f lux%s\n", luxCuoi, phongToi ? " (phòng tối: giảm đèn)" : "");
  if (loaiRadar == LoaiRadar::BHA2 && !daThayKieuBha2 && millis() > 30000) {
    static bool daNhac = false;
    if (!daNhac) {
      daNhac = true;
      Serial.println("[Radar] Radar gửi khung kiểu MR60xxA2 nhưng không có khung pha, nhịp, khoảng cách: nếu mạch "
                     "ghi MR60FDA2 (phát hiện té ngã) thì không đo được nhịp thở, nhịp tim.");
    }
  }
  soByte = demBha1 = 0;
  memset(demKieu2, 0, sizeof demKieu2);
  soByteDau = 0;
  boDoc2.loiDau = boDoc2.loiDuLieu = 0;
}

// ---------- đèn, nút, ánh sáng ----------
void datDen(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t mau = 0xFFFFFFFF;
  uint32_t moi = ((uint32_t)r << 16) | (g << 8) | b;
  if (moi == mau) return;
  mau = moi;
  rgbLedWrite(CHAN_DEN, r, g, b);
}

// Phòng tối (dưới 3 lux, sáng lại khi trên 8 lux): đèn trạng thái gần như tắt để không làm người ngủ thức giấc.
void docAnhSang() {
  static uint32_t moc = 0;
  if (!anh_sang::dc || millis() - moc < 2000) return;
  moc = millis();
  luxCuoi = anh_sang::docLux();
  if (luxCuoi < 0) return;
  if (!phongToi && luxCuoi < 3) phongToi = true;
  else if (phongToi && luxCuoi > 8) phongToi = false;
}

void capNhatDen() {
  const uint8_t S = DO_SANG_DEN;                    // cảnh báo, SOS: luôn sáng rõ
  const uint8_t T = phongToi ? 2 : DO_SANG_DEN;     // trạng thái thường: phòng tối thì gần như tắt
  bool nhay = (millis() / 300) % 2;
  uint32_t tuoi = millis() - mocCanhBao;
  if (mocSOS && millis() - mocSOS < 5000) datDen(nhay ? S : 0, 0, 0);                 // vừa bấm SOS
  else if (mucCanhBao >= 2 && tuoi < 300000) datDen(nhay ? S : 0, 0, 0);              // nguy cơ cao: đỏ nhấp nháy
  else if (mucCanhBao == 1 && tuoi < (laThuCoi ? 3000U : 60000U)) datDen(S, S * 6 / 10, 0);  // cảnh báo nhẹ: vàng
  else if (!denBat) datDen(0, 0, 0);
  else if (!ket_noi::daKetNoi) datDen(0, 0, T);                                       // đang kết nối: xanh dương
  else datDen(0, T > 2 ? T / 2 : 1, 0);                                               // bình thường: xanh lá
}

// Nhấn nhanh: bật/tắt đèn (ban đêm). Giữ 2 giây: gọi trợ giúp (SOS).
void docNut() {
  static bool nhanTruoc = false, daSOS = false;
  static uint32_t mocNhan = 0;
  bool nhan = digitalRead(CHAN_NUT) == LOW;
  if (nhan && !nhanTruoc) {
    mocNhan = millis();
    daSOS = false;
  }
  if (nhan && !daSOS && millis() - mocNhan > 2000) {
    daSOS = true;
    mocSOS = millis();
    char s[80];
    snprintf(s, sizeof s, "{\"t\":%llu,\"loai\":\"sos\"}", (unsigned long long)ket_noi::gioThucMs());
    ket_noi::guiSau("su_kien", s, false, true);
    Serial.println("[Nút] Đã gửi SOS");
  }
  if (!nhan && nhanTruoc && !daSOS && millis() - mocNhan > 30) denBat = !denBat;
  nhanTruoc = nhan;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== AIoT - Thiet bi radar mmWave ===");
  setCpuFrequencyMhz(80);  // 80 MHz đủ cho đọc radar, xử lý và Wi-Fi; mát và đỡ tốn điện hơn mặc định
  pinMode(CHAN_NUT, INPUT_PULLUP);
  delay(5);
  if (digitalRead(CHAN_NUT) == LOW) {  // chưa ai bấm mà đã ở mức thấp: chân đang nối thứ khác → không coi là nút
    coNut = false;
    Serial.println("[Nút] Chân D3 ở mức thấp lúc khởi động (không lắp nút hoặc đang nối thứ khác): tắt nút SOS");
  }
  datDen(0, 0, DO_SANG_DEN);
  if (anh_sang::batDau())
    Serial.printf("[Ánh sáng] Thấy cảm biến BH1750 (0x%02X): phòng tối thì giảm đèn trạng thái\n", anh_sang::dc);
  radar.setRxBufferSize(4096);               // MR60BHA2 gửi nhiều khung (cả đám điểm): bộ đệm lớn để không mất byte
  radar.begin(115200, SERIAL_8N1, D7, D6);  // RX = D7 (nối TX radar), TX = D6 (nối RX radar)
  ket_noi::batDau("radar", khiKetNoi, khiNhanTin);
  mocSongCuoi = millis();
}

void loop() {
  while (radar.available()) {
    uint8_t b = radar.read();
    soByte++;
    if (soByteDau < (int)sizeof byteDau) byteDau[soByteDau++] = b;
    if (boDoc.them(b)) {
      demBha1++;
      datLoai(LoaiRadar::BHA1);
      xuLyKhung(boDoc.khung());
    }
    if (boDoc2.them(b)) {
      datLoai(LoaiRadar::BHA2);
      xuLyKhung2(boDoc2.khung());
    }
  }
  if (loaiRadar == LoaiRadar::BHA2) laySongBha2();

  // MR60BHA1 chưa gửi sóng: gửi lại lệnh chế độ thời gian thực (sau 3 s đầu, rồi 20 s/lần nếu vẫn chưa có).
  // MR60BHA2 tự gửi sóng, không cần lệnh này.
  bool chuaLanNao = mocLenhCheDo == 0;
  if (loaiRadar != LoaiRadar::BHA2 &&
      ((chuaLanNao && millis() > 3000) ||
       (!chuaLanNao && millis() - mocSongCuoi > 20000 && millis() - mocLenhCheDo > 20000))) {
    mr60::cheDoThoiGianThuc(radar);
    mocLenhCheDo = millis();
    if (!chuaLanNao)
      Serial.println("[Radar] Chưa nhận được sóng thở/tim. Kiểm tra dây TX/RX; nếu vẫn không có, "
                     "rút điện radar rồi cắm lại (đổi chế độ cần khởi động lại cảm biến).");
  }

  static uint32_t mocChiSo = 0, mocTrangThai = 0;
  if (millis() - mocChiSo > 3000) {
    mocChiSo = millis();
    guiChiSo();
  }
  if (millis() - mocTrangThai > 30000) {
    mocTrangThai = millis();
    ket_noi::guiTrangThai();
  }
  if (coNut) docNut();
  docAnhSang();
  capNhatDen();
  inChanDoan();
  delay(2);
}
