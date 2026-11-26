// mr60bha2.h — đọc khung dữ liệu UART của radar Seeed MR60BHA2 (60 GHz, 115200 baud). Radar này bán dạng bộ kit,
// gắn sẵn XIAO ESP32C6 (radar nối D6, D7; đèn RGB WS2812 ở D1).
// Khung: 01 | ID (2 byte) | độ dài dữ liệu (2) | kiểu (2) | tổng kiểm tra phần đầu | dữ liệu | tổng kiểm tra dữ liệu
//   ID, độ dài, kiểu: byte cao trước. Tổng kiểm tra = đảo bit của XOR các byte (phần đầu: 7 byte từ 01 tới hết kiểu).
//   Số trong dữ liệu: float và uint32 byte thấp trước.
// Theo thư viện Seeed-mmWave-library của Seeed Studio (SeeedmmWave.cpp, SEEED_MR60BHA2.cpp).
#pragma once
#include <string.h>

namespace mr60b {

constexpr uint16_t PHA_THO_TIM = 0x0A13;  // 3 float: pha tổng, pha thở, pha tim (radian) — dùng làm sóng thở, sóng tim
constexpr uint16_t NHIP_THO = 0x0A14;     // float: nhịp thở (lần/phút)
constexpr uint16_t NHIP_TIM = 0x0A15;     // float: nhịp tim (lần/phút)
constexpr uint16_t KHOANG_CACH = 0x0A16;  // uint32 cờ có mục tiêu + float khoảng cách tới người (cm)
constexpr uint16_t CO_NGUOI = 0x0F09;     // 1 byte: 0 không có người, 1 có người (firmware radar mới)

struct Khung {
  uint16_t kieu = 0, dai = 0;
  uint8_t dl[16];  // chỉ giữ 16 byte đầu: các kiểu cần dùng đều ngắn hơn; khung đám điểm (dài) chỉ kiểm tra rồi bỏ
};

inline uint8_t tongKiemTra(const uint8_t* p, int n) {
  uint8_t x = 0;
  while (n-- > 0) x ^= *p++;
  return (uint8_t)~x;
}
inline float soThuc(const uint8_t* p) {
  float f;
  memcpy(&f, p, 4);
  return f;
}
inline uint32_t soNguyen(const uint8_t* p) {
  uint32_t u;
  memcpy(&u, p, 4);
  return u;
}

class BoDoc {
 public:
  // Đưa vào từng byte nhận được; trả true khi vừa ghép xong 1 khung đúng cả 2 tổng kiểm tra.
  bool them(uint8_t b) {
    switch (tt) {
      case SOF:
        if (b == 0x01) {
          dau[0] = b;
          i = 1;
          tt = DAU;
        }
        return false;
      case DAU:
        dau[i++] = b;
        if (i < 8) return false;
        if (tongKiemTra(dau, 7) != dau[7] || ((dau[3] << 8) | dau[4]) > DAI_TOI_DA) {
          loiDau++;
          dongLai();  // 01 vừa rồi không phải đầu khung: tìm đầu khung trong các byte đã nhận
          return false;
        }
        k.dai = (dau[3] << 8) | dau[4];
        k.kieu = (dau[5] << 8) | dau[6];
        i = 0;
        xorDl = 0;
        tt = k.dai ? DU_LIEU : TONG;
        return false;
      case DU_LIEU:
        if (i < (int)sizeof k.dl) k.dl[i] = b;
        xorDl ^= b;
        if (++i == k.dai) tt = TONG;
        return false;
      case TONG:
        tt = SOF;
        if ((uint8_t)~xorDl == b) return true;
        loiDuLieu++;
        return false;
    }
    return false;
  }

  const Khung& khung() const { return k; }

  uint32_t loiDau = 0, loiDuLieu = 0;  // số khung sai tổng kiểm tra (phần đầu, dữ liệu), để chẩn đoán

 private:
  static constexpr int DAI_TOI_DA = 1024;
  enum : uint8_t { SOF, DAU, DU_LIEU, TONG } tt = SOF;
  uint8_t dau[8];
  int i = 0;
  uint8_t xorDl = 0;
  Khung k;

  void dongLai() {
    for (int j = 1; j < 8; j++)
      if (dau[j] == 0x01) {
        memmove(dau, dau + j, 8 - j);
        i = 8 - j;
        tt = DAU;
        return;
      }
    tt = SOF;
  }
};

}  // namespace mr60b
