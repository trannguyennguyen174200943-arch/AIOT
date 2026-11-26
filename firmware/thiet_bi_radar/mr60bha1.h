// mr60bha1.h — đọc khung dữ liệu UART của radar Seeed MR60BHA1 (60 GHz, 115200 baud).
// Khung: 53 59 | điều khiển | lệnh | độ dài (2 byte) | dữ liệu | tổng kiểm tra | 54 43
// Tổng kiểm tra = byte thấp của tổng mọi byte từ 53 tới hết dữ liệu.
#pragma once

namespace mr60 {

struct Khung {
  uint8_t dk, lenh;
  uint16_t dai;
  uint8_t dl[32];
};

class BoDoc {
 public:
  // Đưa vào từng byte nhận được; trả true khi vừa ghép xong 1 khung đúng tổng kiểm tra.
  bool them(uint8_t b) {
    switch (tt) {
      case DAU1:
        if (b == 0x53) {
          tong = b;
          tt = DAU2;
        }
        break;
      case DAU2:
        if (b == 0x59) {
          tong += b;
          tt = DK;
        } else {
          tt = (b == 0x53) ? DAU2 : DAU1;
          tong = b;
        }
        break;
      case DK:
        k.dk = b;
        tong += b;
        tt = LENH;
        break;
      case LENH:
        k.lenh = b;
        tong += b;
        tt = DAI1;
        break;
      case DAI1:
        k.dai = b << 8;
        tong += b;
        tt = DAI2;
        break;
      case DAI2:
        k.dai |= b;
        tong += b;
        i = 0;
        tt = k.dai > sizeof k.dl ? DAU1 : (k.dai ? DU_LIEU : TONG);
        break;
      case DU_LIEU:
        k.dl[i++] = b;
        tong += b;
        if (i == k.dai) tt = TONG;
        break;
      case TONG:
        tt = (b == tong) ? CUOI1 : DAU1;
        break;
      case CUOI1:
        tt = (b == 0x54) ? CUOI2 : DAU1;
        break;
      case CUOI2:
        tt = DAU1;
        return b == 0x43;
    }
    return false;
  }

  const Khung& khung() const { return k; }

 private:
  enum TrangThai : uint8_t { DAU1, DAU2, DK, LENH, DAI1, DAI2, DU_LIEU, TONG, CUOI1, CUOI2 } tt = DAU1;
  Khung k;
  uint16_t i = 0;
  uint8_t tong = 0;
};

inline void guiLenh(Stream& cong, uint8_t dk, uint8_t lenh, uint8_t duLieu) {
  uint8_t f[10] = {0x53, 0x59, dk, lenh, 0x00, 0x01, duLieu, 0, 0x54, 0x43};
  for (int j = 0; j < 7; j++) f[7] += f[j];
  cong.write(f, sizeof f);
  cong.flush();
}

// Chế độ thời gian thực: radar mới gửi dạng sóng thở/sóng tim (chế độ ngủ thì không).
inline void cheDoThoiGianThuc(Stream& cong) { guiLenh(cong, 0x84, 0x0F, 0x00); }

}  // namespace mr60
