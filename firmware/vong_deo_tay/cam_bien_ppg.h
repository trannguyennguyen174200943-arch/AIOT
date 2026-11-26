// cam_bien_ppg.h — đọc sóng mạch PPG 100 mẫu/giây.
// Tự nhận cảm biến: AS7038RB (I2C 0x30), MAX30102 hoặc MAX30100 (cùng I2C 0x57, phân biệt bằng thanh ghi PART_ID).
// Các loại nối chân giống nhau. MAX30102/MAX30100 cho thêm kênh LED đỏ (để tính SpO2) và nhiệt độ chip
// (xấp xỉ nhiệt độ da cổ tay). Module GY-MAX30100 màu tím: xem lưu ý điện trở kéo lên I2C trong HUONG_DAN.md.
#pragma once
#include <Wire.h>
#include <math.h>

namespace ppg {

constexpr int TAN_SO = 100;  // mẫu/giây

struct Mau {
  uint32_t chinh;   // hồng ngoại (MAX30102/MAX30100) hoặc kênh duy nhất (AS7038RB). Số đọc thô: phản xạ nhiều thì lớn
  uint32_t kenhDo;  // LED đỏ của MAX30102/MAX30100; 0 nếu cảm biến không có
};

// ---- I2C ----
inline bool ghi(uint8_t diaChi, uint8_t thanhGhi, uint8_t giaTri) {
  Wire.beginTransmission(diaChi);
  Wire.write(thanhGhi);
  Wire.write(giaTri);
  return Wire.endTransmission() == 0;
}

inline int doc(uint8_t diaChi, uint8_t thanhGhi, uint8_t* ra, size_t n) {
  Wire.beginTransmission(diaChi);
  Wire.write(thanhGhi);
  if (Wire.endTransmission(false) != 0) return -1;
  size_t nhan = Wire.requestFrom((uint16_t)diaChi, n, true);
  for (size_t i = 0; i < nhan; i++) ra[i] = Wire.read();
  return (int)nhan;
}

inline int doc1(uint8_t diaChi, uint8_t thanhGhi) {
  uint8_t v;
  return doc(diaChi, thanhGhi, &v, 1) == 1 ? v : -1;
}

// ---- Dùng chung cho MAX30102 và MAX30100 (cùng hãng, cùng cách đánh dấu FIFO và đọc nhiệt độ) ----
namespace maxim {
// Số mẫu đang chờ trong FIFO sâu `sau` mẫu (16 hoặc 32). FIFO đầy đúng `sau` mẫu thì con trỏ ghi trùng con trỏ đọc,
// nên phải xem thêm bộ đếm tràn.
inline int soMauFifo(uint8_t dc, uint8_t thanhGhiGhi, uint8_t thanhGhiDoc, uint8_t thanhGhiTran, int sau) {
  int wr = doc1(dc, thanhGhiGhi), rd = doc1(dc, thanhGhiDoc);
  if (wr < 0 || rd < 0) return 0;
  int n = (wr - rd) & (sau - 1);
  if (n == 0 && doc1(dc, thanhGhiTran) > 0) n = sau;
  return n;
}

// Nhiệt độ chip: phần nguyên (có dấu) + phần lẻ 4 bit × 0,0625 °C.
inline float nhietDo(uint8_t dc, uint8_t thanhGhiNguyen, uint8_t thanhGhiLe) {
  int nguyen = doc1(dc, thanhGhiNguyen), le = doc1(dc, thanhGhiLe);
  if (nguyen < 0 || le < 0) return NAN;
  return (int8_t)nguyen + (le & 0x0F) * 0.0625f;
}
}  // namespace maxim

// ---- AS7038RB (ams OSRAM, datasheet DS000726) ----
// Cấu hình: sequencer bật LED1 trong 160 µs mỗi 10 ms, ADC 14 bit đọc thẳng ngõ ra bộ khuếch đại TIA
// lúc LED đang sáng. Chưa thử trên phần cứng thật; nếu số đọc đứng yên hãy xem mục xử lý lỗi trong hướng dẫn.
namespace as7038 {
constexpr uint8_t DC = 0x30;
enum : uint8_t {
  CONTROL = 0x00, LED_CFG = 0x10, LED1_CURRL = 0x12, LED1_CURRH = 0x13, PD_CFG = 0x1A,
  PD_AMPRCCFG = 0x1D, PD_AMPCFG = 0x1E, LED12_MODE = 0x2C, LED34_MODE = 0x2D, MAN_SEQ_CFG = 0x2E,
  SEQ_CNT = 0x30, SEQ_DIV = 0x31, SEQ_START = 0x32, SEQ_PER = 0x33, SEQ_LED_STA = 0x34, SEQ_LED_STO = 0x35,
  SEQ_ITG_STA = 0x38, SEQ_ITG_STO = 0x39, SEQ_ADC = 0x42, OFE_CFGA = 0x50, FIFO_CNTRL = 0x79,
  ADC_CFGA = 0x88, ADC_CFGB = 0x89, ADC_CFGC = 0x8A, ADC_CHANNEL_MASK_L = 0x8B, ADC_CHANNEL_MASK_H = 0x8C,
  ID = 0x92, FIFOSTATUS = 0xA4, FIFOLEVEL = 0xA6, FIFOL = 0xFE,
};

// Dòng LED1 = 0,786 mA + 0,097 mA × ma (ma = 0…1023).
inline void datDongLed(int ma) {
  ghi(DC, LED1_CURRL, (ma & 3) << 6);  // bit cs1_boost = 0: tối đa 100 mA
  ghi(DC, LED1_CURRH, ma >> 2);
}

inline bool batDau(int chanEnable) {
  pinMode(chanEnable, OUTPUT);
  digitalWrite(chanEnable, HIGH);  // ENABLE mức cao mới truy cập được thanh ghi
  delay(10);
  int id = doc1(DC, ID);
  if (id < 0 || (id >> 2) != 0x15) return false;  // 0x15 = họ AS703x

  ghi(DC, CONTROL, 0x03);             // bật LDO bên trong + bộ dao động
  delay(5);
  ghi(DC, LED_CFG, 0x81);             // bật điện áp tham chiếu + LED1
  datDongLed(146);                    // ≈ 15 mA, vòng lặp chính tự chỉnh sau
  ghi(DC, LED12_MODE, 0x02);          // LED1 do sequencer bật/tắt
  ghi(DC, LED34_MODE, 0x00);
  ghi(DC, PD_CFG, 0x3C);              // nối cả 4 photodiode PD1–PD4
  ghi(DC, PD_AMPRCCFG, (1 << 5) | 13);       // TIA: 1 MΩ, 1,3 pF → 1 V/µA (bảng Figure 33)
  ghi(DC, PD_AMPCFG, 0x80 | (15 << 2) | 1);  // bật TIA, pd_ampvo = 15, pd_ampcomp = 1
  ghi(DC, OFE_CFGA, 0x20);            // en_bias_ofe, bắt buộc khi bật TIA
  ghi(DC, ADC_CFGA, 0x00);            // mỗi lần kích 1 mẫu
  ghi(DC, ADC_CFGC, 0x09);            // xả tụ trước khi lấy mẫu, chờ ổn định 4 µs
  ghi(DC, ADC_CHANNEL_MASK_L, 0x01);  // ADC chỉ đọc kênh TIA
  ghi(DC, ADC_CHANNEL_MASK_H, 0x00);
  ghi(DC, ADC_CFGB, 0x01);            // xung ADC 1 MHz, bật ADC

  // Sequencer: 1 nhịp = (39 + 1) µs = 40 µs; chu kỳ 250 nhịp = 10 ms → 100 mẫu/giây.
  ghi(DC, SEQ_DIV, 39);
  ghi(DC, SEQ_PER, 250);
  ghi(DC, SEQ_CNT, 0);                // chạy liên tục
  ghi(DC, SEQ_LED_STA, 1);            // LED sáng từ nhịp 1 (40 µs)…
  ghi(DC, SEQ_LED_STO, 5);            // …đến nhịp 5 (200 µs)
  ghi(DC, SEQ_ITG_STA, 1);            // TIA dùng điện trở: để bộ tích phân luôn chạy
  ghi(DC, SEQ_ITG_STO, 0);
  ghi(DC, SEQ_ADC, 4);                // lấy mẫu ở nhịp 4 (160 µs), lúc LED đang sáng
  ghi(DC, FIFO_CNTRL, 0x01);          // xoá FIFO
  ghi(DC, MAN_SEQ_CFG, 0x01);         // chế độ sequencer
  ghi(DC, SEQ_START, 0x01);
  return true;
}

inline int docFifo(Mau* ra, int toiDa) {
  int st = doc1(DC, FIFOSTATUS);
  if (st > 0 && (st & 1)) {           // tràn FIFO (vòng lặp bị chậm quá lâu): xoá, đọc lại từ đầu
    ghi(DC, FIFO_CNTRL, 0x01);
    return 0;
  }
  int n = min(doc1(DC, FIFOLEVEL), toiDa);
  for (int i = 0; i < n; i++) {
    uint8_t b[2];
    if (doc(DC, FIFOL, b, 2) != 2) return i;
    ra[i].chinh = ((uint32_t)b[1] << 6) | (b[0] >> 2);  // 14 bit = fifoh[7:0] : fifol[7:2]
    ra[i].kenhDo = 0;
  }
  return max(n, 0);
}
}  // namespace as7038

// ---- MAX30102 (Analog Devices / Maxim) ----
namespace max3010x {
constexpr uint8_t DC = 0x57;
enum : uint8_t {
  FIFO_WR = 0x04, OVF = 0x05, FIFO_RD = 0x06, FIFO_DATA = 0x07, FIFO_CFG = 0x08,
  MODE = 0x09, SPO2_CFG = 0x0A, LED_DO = 0x0C, LED_HONG_NGOAI = 0x0D,
  NHIET_NGUYEN = 0x1F, NHIET_LE = 0x20, NHIET_CFG = 0x21, PART_ID = 0xFF,
};

inline void datDongLed(int hongNgoai, int doLed) {  // mã × 0,2 mA, mỗi LED một thanh ghi
  ghi(DC, LED_HONG_NGOAI, hongNgoai);
  ghi(DC, LED_DO, doLed);
}

inline bool batDau() {
  if (doc1(DC, PART_ID) != 0x15) return false;
  ghi(DC, MODE, 0x40);                          // khởi động lại chip
  delay(10);
  ghi(DC, FIFO_CFG, (2 << 5) | 0x10);           // trung bình 4 mẫu, FIFO quay vòng
  ghi(DC, SPO2_CFG, (1 << 5) | (3 << 2) | 3);   // dải 4096 nA, 400 mẫu/s, xung 411 µs (18 bit)
  datDongLed(0x40, 0x40);                       // ≈ 12,8 mA
  ghi(DC, FIFO_WR, 0);
  ghi(DC, OVF, 0);
  ghi(DC, FIFO_RD, 0);
  ghi(DC, MODE, 0x03);                          // chế độ SpO2: LED đỏ + hồng ngoại → 400/4 = 100 mẫu/s
  return true;
}

inline int docFifo(Mau* ra, int toiDa) {
  int n = min(maxim::soMauFifo(DC, FIFO_WR, FIFO_RD, OVF, 32), toiDa);
  int daDoc = 0;
  uint8_t b[126];
  while (daDoc < n) {
    int lo = min(n - daDoc, 21);                // 21 mẫu × 6 byte vừa bộ đệm 128 byte của Wire
    if (doc(DC, FIFO_DATA, b, lo * 6) != lo * 6) break;
    for (int i = 0; i < lo; i++) {
      const uint8_t* p = b + i * 6;             // mỗi mẫu: 3 byte LED đỏ rồi 3 byte hồng ngoại
      ra[daDoc].kenhDo = (((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]) & 0x3FFFF;
      ra[daDoc].chinh = (((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5]) & 0x3FFFF;
      daDoc++;
    }
  }
  return daDoc;
}

// Nhiệt kế trong chip: ra lệnh đo, khoảng 30 ms sau mới có kết quả.
inline void batDauDoNhiet() { ghi(DC, NHIET_CFG, 0x01); }

inline float docNhiet() {  // NAN nếu chưa đo xong
  int cfg = doc1(DC, NHIET_CFG);
  if (cfg < 0 || (cfg & 1)) return NAN;
  return maxim::nhietDo(DC, NHIET_NGUYEN, NHIET_LE);
}
}  // namespace max3010x

// ---- MAX30100 (Maxim, module GY-MAX30100) ----
// Cùng địa chỉ 0x57 với MAX30102 nhưng khác bảng thanh ghi; PART_ID = 0x11 (MAX30102 là 0x15).
// FIFO chỉ 16 mẫu (160 ms ở 100 mẫu/s; vòng lặp đọc 20 ms/lần nên vẫn kịp), mỗi mẫu 4 byte: hồng ngoại 16 bit rồi
// LED đỏ 16 bit. Đặt 100 mẫu/s, xung 1600 µs, độ phân giải cao → ADC 16 bit. Dòng LED chỉ có 16 mức (mã 0–15,
// 0–50 mA, mỗi mức khoảng 3,1 mA), đỏ và hồng ngoại chung một thanh ghi.
namespace max30100 {
constexpr uint8_t DC = 0x57;
enum : uint8_t {
  FIFO_WR = 0x02, OVF = 0x03, FIFO_RD = 0x04, FIFO_DATA = 0x05, MODE = 0x06, SPO2_CFG = 0x07, LED_CFG = 0x09,
  NHIET_NGUYEN = 0x16, NHIET_LE = 0x17, PART_ID = 0xFF,
};
constexpr uint8_t CHE_DO_SPO2 = 0x03;   // LED đỏ + hồng ngoại
constexpr uint8_t BIT_DO_NHIET = 0x08;  // TEMP_EN: đo nhiệt độ 1 lần, tự xoá khi xong

inline void datDongLed(int hongNgoai, int doLed) {  // mã 0–15 mỗi LED: đỏ ở 4 bit cao, hồng ngoại ở 4 bit thấp
  ghi(DC, LED_CFG, (uint8_t)((constrain(doLed, 0, 15) << 4) | constrain(hongNgoai, 0, 15)));
}

inline bool batDau() {
  if (doc1(DC, PART_ID) != 0x11) return false;
  ghi(DC, MODE, 0x40);                          // khởi động lại chip
  delay(10);
  ghi(DC, SPO2_CFG, (1 << 6) | (1 << 2) | 3);   // độ phân giải cao, 100 mẫu/s, xung 1600 µs (16 bit)
  datDongLed(8, 8);                             // ≈ 27 mA, vòng lặp chính tự chỉnh sau
  ghi(DC, FIFO_WR, 0);
  ghi(DC, OVF, 0);
  ghi(DC, FIFO_RD, 0);
  ghi(DC, MODE, CHE_DO_SPO2);
  return true;
}

inline int docFifo(Mau* ra, int toiDa) {
  int n = min(maxim::soMauFifo(DC, FIFO_WR, FIFO_RD, OVF, 16), toiDa);
  if (n <= 0) return 0;
  uint8_t b[64];                                // 16 mẫu × 4 byte, vừa bộ đệm 128 byte của Wire
  if (doc(DC, FIFO_DATA, b, n * 4) != n * 4) return 0;
  for (int i = 0; i < n; i++) {
    const uint8_t* p = b + i * 4;               // mỗi mẫu: 2 byte hồng ngoại rồi 2 byte LED đỏ
    ra[i].chinh = ((uint32_t)p[0] << 8) | p[1];
    ra[i].kenhDo = ((uint32_t)p[2] << 8) | p[3];
  }
  return n;
}

// Nhiệt kế trong chip: bật TEMP_EN (giữ nguyên chế độ SpO2), khoảng 29 ms sau bit tự xoá thì đọc được.
inline void batDauDoNhiet() { ghi(DC, MODE, CHE_DO_SPO2 | BIT_DO_NHIET); }

inline float docNhiet() {  // NAN nếu chưa đo xong
  int m = doc1(DC, MODE);
  if (m < 0 || (m & BIT_DO_NHIET)) return NAN;
  return maxim::nhietDo(DC, NHIET_NGUYEN, NHIET_LE);
}
}  // namespace max30100

// ---- Lớp chung ----
enum class Loai { KHONG_CO, AS7038RB, MAX30102, MAX30100 };
inline Loai loai = Loai::KHONG_CO;
inline uint32_t giaTriMax = 1;    // giá trị lớn nhất của ADC
inline int dongLed = 0, dongLedDo = 0, dongLedMax = 0, dongLedDau = 0;   // dongLedDo: LED đỏ của MAX3010x

inline const char* ten() {
  switch (loai) {
    case Loai::AS7038RB: return "AS7038RB";
    case Loai::MAX30102: return "MAX30102";
    case Loai::MAX30100: return "MAX30100";
    default: return "khong_co";
  }
}

inline bool laMax() { return loai == Loai::MAX30102 || loai == Loai::MAX30100; }  // có LED đỏ + nhiệt kế

inline Loai batDau(int chanEnable) {
  if (as7038::batDau(chanEnable)) {
    loai = Loai::AS7038RB;
    giaTriMax = 16383;
    dongLedDau = 146;
    dongLedMax = 404;   // ≈ 40 mA
  } else if (max3010x::batDau()) {  // chỉ đọc PART_ID rồi dừng nếu không phải MAX30102
    loai = Loai::MAX30102;
    giaTriMax = 262143;
    dongLedDau = 0x40;
    dongLedMax = 0xA0;  // ≈ 32 mA
  } else if (max30100::batDau()) {
    loai = Loai::MAX30100;
    giaTriMax = 65535;
    dongLedDau = 8;     // ≈ 27 mA
    dongLedMax = 15;    // 50 mA
  } else {
    loai = Loai::KHONG_CO;
  }
  dongLed = dongLedDo = dongLedDau;
  return loai;
}

// Đọc các mẫu mới trong FIFO của cảm biến.
inline int doc(Mau* ra, int toiDa) {
  if (loai == Loai::AS7038RB) return as7038::docFifo(ra, toiDa);
  if (loai == Loai::MAX30102) return max3010x::docFifo(ra, toiDa);
  if (loai == Loai::MAX30100) return max30100::docFifo(ra, toiDa);
  return 0;
}

inline void napDongLed() {  // ghi dòng LED hiện tại xuống cảm biến
  if (loai == Loai::AS7038RB) as7038::datDongLed(dongLed);
  if (loai == Loai::MAX30102) max3010x::datDongLed(dongLed, dongLedDo);
  if (loai == Loai::MAX30100) max30100::datDongLed(dongLed, dongLedDo);
}

inline void datDongLed(int ma) {  // LED chính: hồng ngoại (MAX3010x) hoặc LED duy nhất (AS7038RB)
  dongLed = constrain(ma, 1, dongLedMax);
  napDongLed();
}

// LED đỏ chỉnh riêng: da hấp thụ ánh sáng đỏ mạnh hơn hồng ngoại, nếu dùng chung một mức thì kênh đỏ dễ quá tối
// (nhiễu lớn) hoặc bão hoà, SpO2 tính từ tỉ số đỏ / hồng ngoại sẽ sai.
inline void datDongLedDo(int ma) {
  dongLedDo = constrain(ma, 1, dongLedMax);
  napDongLed();
}

// Giữ số đọc trung bình ở khoảng 20–85% thang đo: quá sáng thì giảm dòng LED, quá tối thì tăng.
inline int mucMoi(int ma, uint32_t trungBinh) {
  if (trungBinh > giaTriMax * 0.85) return ma * 7 / 10;
  if (trungBinh < giaTriMax * 0.20 && ma < dongLedMax) return ma * 14 / 10 + 1;
  return ma;
}
inline void tuChinhDongLed(uint32_t trungBinh) {
  int m = mucMoi(dongLed, trungBinh);
  if (m != dongLed) datDongLed(m);
}
inline void tuChinhDongLedDo(uint32_t trungBinhDo) {
  int m = mucMoi(dongLedDo, trungBinhDo);
  if (m != dongLedDo) datDongLedDo(m);
}

// Da có áp vào cảm biến không: ánh sáng phản xạ đủ lớn. Ngưỡng của MAX3010x chỉnh được bằng NGUONG_CHAM_DA_MAX trong
// cau_hinh.h; phải nhỏ hơn 0,20 vì lúc đeo, tuChinhDongLed giữ số đọc từ 20 % trở lên (trừ khi LED đã hết mức).
#ifndef NGUONG_CHAM_DA_MAX
#define NGUONG_CHAM_DA_MAX 0.19f
#endif
inline bool coChamDa(uint32_t trungBinh) {
  return trungBinh > giaTriMax * (laMax() ? NGUONG_CHAM_DA_MAX : 0.05f);
}

inline bool coKenhDo() { return laMax(); }    // có kênh LED đỏ → tính được SpO2
inline bool coNhietKe() { return laMax(); }
inline void batDauDoNhiet() {
  if (loai == Loai::MAX30102) max3010x::batDauDoNhiet();
  if (loai == Loai::MAX30100) max30100::batDauDoNhiet();
}
inline float docNhiet() {
  if (loai == Loai::MAX30102) return max3010x::docNhiet();
  if (loai == Loai::MAX30100) return max30100::docNhiet();
  return NAN;
}

// Chế độ chờ khi tháo vòng: đèn LED chạy dòng thấp, chỉ để nhận biết lúc đeo lại.
inline bool choDuoc() { return laMax(); }
inline void vaoCho() {  // ≈ 7,6 mA (MAX30100), 4,8 mA (MAX30102) cho cả 2 LED
  dongLed = dongLedDo = constrain(loai == Loai::MAX30100 ? 2 : 0x18, 1, dongLedMax);
  napDongLed();
}
inline void raCho() {
  dongLed = dongLedDo = dongLedDau;
  napDongLed();
}
inline bool coChamDaKhiCho(uint32_t trungBinh) { return trungBinh > giaTriMax * 0.07; }

}  // namespace ppg
