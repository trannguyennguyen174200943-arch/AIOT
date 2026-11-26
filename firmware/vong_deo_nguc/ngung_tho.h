// ngung_tho.h — phát hiện ngưng thở ngay trên vòng ngực từ cảm biến thở ở dây đeo (25 mẫu/giây): màng áp điện PVDF
// (CAM_BIEN_THO_PVDF 1 trong cau_hinh.h) hoặc cảm biến lực FSR402.
// Đây là lớp DỰ PHÒNG khi vòng ngực không liên lạc được máy chủ AI (mất Wi-Fi, broker hoặc may_chu.py tắt);
// bình thường mô hình Isolation Forest trên máy chủ mới là bộ phát hiện chính.
//
// Cách làm (nhẹ, chạy được trên ESP32 mà không cần thư viện):
//   1. Lọc thông dải khoảng 0,03–0,5 Hz bằng 2 bộ lọc EMA: bỏ mức nền (FSR: độ chặt của dây đeo; PVDF: điện áp
//      phân cực 1,65 V) và nhiễu nhanh.
//   2. Biên độ thở = (lớn nhất − nhỏ nhất) của tín hiệu đã lọc trong GIAY_CUA_SO giây gần nhất.
//   3. 60 giây đầu sau khi đeo: học biên độ thở bình thường của chính người đeo (giống bước hiệu chuẩn trên máy chủ).
//   4. Biên độ dưới TI_LE_NGUNG × mức bình thường liên tục → đếm giây ngưng thở.
//   Không tính khi: chưa đeo (FSR: lực nền quá nhỏ; PVDF không đo được lực tĩnh nên chỉ dựa vào điện cực ECG),
//   điện cực ECG tuột (vòng có thể đã bị tháo), đang vận động mạnh.
// Giới hạn: cảm biến ở dây đeo đo cử động lồng ngực nên nhận ra ngưng thở trung ương (không còn cử động thở).
// Ngưng thở tắc nghẽn (ngực vẫn gắng thở) cần thêm SpO2 của vòng tay và mô hình trên máy chủ.
#pragma once
#include <Arduino.h>

#ifndef CAM_BIEN_THO_PVDF
#define CAM_BIEN_THO_PVDF 0
#endif

namespace ngung_tho {

constexpr int TAN_SO = 25;
constexpr int GIAY_CUA_SO = 8;            // đủ chứa 1 nhịp thở chậm (7,5 lần/phút)
constexpr float TI_LE_NGUNG = 0.25f;      // dưới 25 % biên độ bình thường coi như không còn cử động thở
constexpr float TI_LE_THO_LAI = 0.5f;     // trên 50 % là đã thở lại
constexpr int GIAY_HOC = 60;              // thời gian học biên độ thở bình thường sau khi đeo
constexpr int LUC_NEN_TOI_THIEU = 150;    // số đọc ADC: thấp hơn là FSR không bị dây ép (chưa đeo). PVDF không dùng
constexpr float BIEN_DO_TOI_THIEU = 60;   // số đọc ADC: biên độ học được nhỏ hơn là dây quá lỏng, nhiễu ADC
                                          // sẽ lấn tín hiệu thở nên không dám báo (học lại từ đầu)

enum class TrangThai : uint8_t { CHUA_DEO, DANG_HOC, BINH_THUONG, NGUNG_THO };
enum class SuKien : uint8_t { KHONG, THO_LAI, THAO_RA };

class BoPhatHien {
 public:
  TrangThai trangThai = TrangThai::CHUA_DEO;
  float bienDo = 0;       // biên độ hiện tại (đơn vị số đọc ADC)
  float binhThuong = 0;   // biên độ thở bình thường đã học

  // Số giây ước tính đã ngưng thở (tính cả phần nằm trong cửa sổ đo biên độ).
  int giayNgung() const { return demNgung > 0 ? demNgung + GIAY_CUA_SO - 2 : 0; }

  void datLai() {
    trangThai = TrangThai::CHUA_DEO;
    khoiDong = true;
    demMau = demGiay = demNgung = demTho = soGiayHoc = 0;
    tongHoc = 0;
    binhThuong = bienDo = 0;
  }

  // Gọi cho từng mẫu cảm biến thở (0–4095).
  void them(uint16_t x) {
    if (khoiDong) {
      nen = loc = x;
      khoiDong = false;
    }
    nen += (x - nen) * (1.0f / (5.0f * TAN_SO));  // lực nền, hằng số thời gian 5 s
    loc += (x - loc) * (1.0f / (0.3f * TAN_SO));  // làm mượt, hằng số thời gian 0,3 s
    float y = loc - nen;
    if (demMau == 0 || y > lonNhat) lonNhat = y;
    if (demMau == 0 || y < nhoNhat) nhoNhat = y;
    demMau++;
  }

  // Gọi mỗi giây một lần, sau khi đã đưa đủ mẫu của giây đó.
  // dienCucTuot: AD8232 báo điện cực tuột trong giây vừa rồi. vanDongManh: MPU6050 báo đang đi lại/vận động.
  SuKien moiGiay(bool dienCucTuot, bool vanDongManh) {
    if (demMau == 0) return SuKien::KHONG;
    cuaSoMax[viTri] = lonNhat;
    cuaSoMin[viTri] = nhoNhat;
    viTri = (viTri + 1) % GIAY_CUA_SO;
    demMau = 0;
    if (demGiay < GIAY_CUA_SO) demGiay++;
    float mx = cuaSoMax[0], mn = cuaSoMin[0];
    for (int i = 1; i < demGiay; i++) {
      mx = max(mx, cuaSoMax[i]);
      mn = min(mn, cuaSoMin[i]);
    }
    bienDo = mx - mn;

#if CAM_BIEN_THO_PVDF
    bool dangDeo = !dienCucTuot;  // màng PVDF chỉ ra tín hiệu khi bị uốn, không đo được lực tĩnh của dây đeo
#else
    bool dangDeo = nen >= LUC_NEN_TOI_THIEU && !dienCucTuot;
#endif
    if (!dangDeo) {
      bool dangNgung = trangThai == TrangThai::NGUNG_THO;
      if (trangThai != TrangThai::CHUA_DEO) datLaiGiuBoLoc();
      return dangNgung ? SuKien::THAO_RA : SuKien::KHONG;  // tháo vòng thì huỷ đếm, không báo nhầm
    }
    if (trangThai == TrangThai::CHUA_DEO) {
      trangThai = TrangThai::DANG_HOC;
      soGiayHoc = 0;
      tongHoc = 0;
    }
    if (demGiay < GIAY_CUA_SO) return SuKien::KHONG;  // chờ đầy cửa sổ đo biên độ

    switch (trangThai) {
      case TrangThai::DANG_HOC:
        if (vanDongManh) break;  // chỉ học lúc ngồi/nằm yên
        tongHoc += bienDo;
        if (++soGiayHoc >= GIAY_HOC - GIAY_CUA_SO) {
          float tb = tongHoc / soGiayHoc;
          if (tb >= BIEN_DO_TOI_THIEU) {
            binhThuong = tb;
            trangThai = TrangThai::BINH_THUONG;
            demNgung = 0;
          } else {  // dây quá lỏng, học lại từ đầu
            soGiayHoc = 0;
            tongHoc = 0;
          }
        }
        break;
      case TrangThai::BINH_THUONG:
        if (vanDongManh) {  // đang đi lại thì chắc chắn còn tỉnh, FSR lại nhiễu: không đếm
          demNgung = 0;
          break;
        }
        if (bienDo < TI_LE_NGUNG * binhThuong) {
          demNgung++;
        } else {
          demNgung = 0;
          // Cập nhật chậm mức bình thường (dây giãn, đổi tư thế), bỏ qua giá trị quá lớn do cử động
          if (bienDo < 3 * binhThuong) binhThuong += (bienDo - binhThuong) * 0.01f;
        }
        break;
      case TrangThai::NGUNG_THO:
        if (bienDo >= TI_LE_THO_LAI * binhThuong || vanDongManh) {
          if (++demTho >= 3) {  // thở lại đều 3 giây liền
            trangThai = TrangThai::BINH_THUONG;
            demNgung = demTho = 0;
            return SuKien::THO_LAI;
          }
        } else {
          demTho = 0;
          demNgung++;
        }
        break;
      default:
        break;
    }
    return SuKien::KHONG;
  }

  // Đã ngưng thở đủ `giay` giây chưa (gọi sau moiGiay). Trả true một lần khi vừa đủ.
  bool vuaDu(int giay) {
    if (trangThai == TrangThai::BINH_THUONG && giayNgung() >= giay) {
      trangThai = TrangThai::NGUNG_THO;
      demTho = 0;
      return true;
    }
    return false;
  }

 private:
  bool khoiDong = true;
  float nen = 0, loc = 0, lonNhat = 0, nhoNhat = 0, tongHoc = 0;
  float cuaSoMax[GIAY_CUA_SO] = {}, cuaSoMin[GIAY_CUA_SO] = {};
  int viTri = 0, demMau = 0, demGiay = 0, demNgung = 0, demTho = 0, soGiayHoc = 0;

  void datLaiGiuBoLoc() {  // tháo vòng: quên biên độ đã học, đeo lại sẽ học lại
    trangThai = TrangThai::CHUA_DEO;
    demNgung = demTho = soGiayHoc = 0;
    tongHoc = 0;
    binhThuong = 0;
  }
};

}  // namespace ngung_tho
