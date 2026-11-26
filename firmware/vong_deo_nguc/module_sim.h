// module_sim.h — nhắn tin và gọi điện cho người nhà qua module SIM.
// Đang dùng module 4G A7670C (SIMCom, LTE Cat.1, UART 115200). Vẫn chạy được với SIM800L hay A7680C vì cùng
// lệnh AT (SIM800L: đặt SIM_TOC_DO = 9600 trong cau_hinh.h).
// Lệnh AT chạy trong tác vụ riêng nên vòng lặp chính và việc lấy mẫu ECG không bị chặn.
// Mạng 4G: kiểm tra đăng ký cả AT+CREG? (thoại) lẫn AT+CEREG? (4G). Gọi điện trên 4G cần nhà mạng bật VoLTE
// cho module; nếu gọi không được thì tin nhắn và Telegram vẫn hoạt động.
#pragma once
#include "cau_hinh.h"

namespace sim {

enum class Viec : uint8_t { NHAN_TIN, GOI_DIEN };
struct YeuCau {
  Viec viec;
  char noiDung[161];  // tin nhắn không dấu, tối đa 160 ký tự
};
struct KetQua {
  Viec viec;
  bool ok;
};

inline HardwareSerial& cong = Serial2;
inline QueueHandle_t hangViec = nullptr, hangKetQua = nullptr;
inline int chanReset = -1;
inline volatile int trangThai = 0;  // 0 đang khởi động, 1 sẵn sàng, -1 module không trả lời, -2 chưa có sóng,
                                    // -3 thẻ SIM chưa sẵn sàng (chưa cắm, còn mã PIN, tiếp xúc kém)
inline volatile int doSong = 99;    // AT+CSQ: 0–31, 99 = không rõ
inline char soDienThoai[20] = SDT_MAC_DINH;
inline portMUX_TYPE khoaSo = portMUX_INITIALIZER_UNLOCKED;

inline void datSo(const char* so) {
  portENTER_CRITICAL(&khoaSo);
  strlcpy(soDienThoai, so, sizeof soDienThoai);
  portEXIT_CRITICAL(&khoaSo);
}

inline void laySo(char* ra, size_t n) {
  portENTER_CRITICAL(&khoaSo);
  strlcpy(ra, soDienThoai, n);
  portEXIT_CRITICAL(&khoaSo);
}

// Gửi 1 lệnh AT (lenh = nullptr: chỉ chờ) tới khi thấy chuỗi "doi", gặp "ERROR" hoặc hết giờ.
inline bool at(const char* lenh, const char* doi = "OK", uint32_t choMs = 2000, String* phanHoi = nullptr) {
  if (lenh) {
    while (cong.available()) cong.read();
    cong.print(lenh);
    cong.print("\r");
  }
  String s;
  uint32_t t0 = millis();
  bool thay = false;
  while (millis() - t0 < choMs) {
    while (cong.available()) {
      char c = (char)cong.read();
      if (s.length() < 512) s += c;
    }
    if (s.indexOf(doi) >= 0) {
      thay = true;
      break;
    }
    if (s.indexOf("ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (phanHoi) *phanHoi = s;
  return thay;
}

inline bool daDangKyMang(const char* lenh) {
  String r;
  if (!at(lenh, "OK", 2000, &r)) return false;
  int i = r.indexOf(':');
  int phay = r.indexOf(',', i);
  if (i < 0 || phay < 0) return false;
  int st = r.substring(phay + 1).toInt();
  return st == 1 || st == 5;  // 1: mạng nhà, 5: chuyển vùng
}

inline bool kiemTraSong() {
  String r;
  if (at("AT+CSQ", "OK", 2000, &r)) {
    int i = r.indexOf("+CSQ:");
    if (i >= 0) doSong = r.substring(i + 5).toInt();
  }
  return daDangKyMang("AT+CREG?") || daDangKyMang("AT+CEREG?");
}

// Thẻ SIM đã đọc xong chưa (AT+CPIN? trả READY). Module 4G cần thêm vài giây sau khi trả lời AT mới đọc xong thẻ.
inline bool theSanSang() { return at("AT+CPIN?", "READY", 1000); }

// Tin nhắn dạng chữ, bảng mã GSM. Gọi lại trước mỗi tin: module có thể tự khởi động lại (sụt nguồn lúc phát sóng)
// và quay về chế độ PDU mặc định, khi đó AT+CMGS="số" sẽ báo lỗi.
inline bool caiTinNhan() { return at("AT+CMGF=1") && at("AT+CSCS=\"GSM\""); }

inline bool khoiDong() {
  if (chanReset >= 0) {
    digitalWrite(chanReset, LOW);  // RESET mức thấp để khởi động lại module (A7670C ~2,5 s, SIM800L 0,2 s)
    vTaskDelay(pdMS_TO_TICKS(SIM_XUNG_RESET_MS));
    digitalWrite(chanReset, HIGH);
  }
  vTaskDelay(pdMS_TO_TICKS(3000));
  bool coTraLoi = false;
  for (int i = 0; i < 20 && !coTraLoi; i++) coTraLoi = at("AT", "OK", 1000);  // module 4G khởi động 10–20 s
  if (!coTraLoi) {
    trangThai = -1;
    return false;
  }
  at("ATE0");                // tắt lặp lại lệnh
  // Chờ đọc xong thẻ SIM rồi mới cài tin nhắn: gửi AT+CMGF trước lúc đó, module 4G báo lỗi (SIM busy) và vẫn ở
  // chế độ PDU, tin nhắn cảnh báo sẽ không gửi được.
  bool coThe = false;
  for (int i = 0; i < 15 && !(coThe = theSanSang()); i++) vTaskDelay(pdMS_TO_TICKS(1000));
  if (!coThe) {
    trangThai = -3;
    return true;
  }
  for (int i = 0; i < 5 && !caiTinNhan(); i++) vTaskDelay(pdMS_TO_TICKS(1000));  // phần tin nhắn còn đang khởi tạo
  at("AT+CNMI=0,0,0,0,0");   // không đẩy tin nhắn đến ra cổng UART
  trangThai = kiemTraSong() ? 1 : -2;
  return true;
}

inline bool nhanTinNgay(const char* noiDung) {
  char so[20], lenh[40];
  laySo(so, sizeof so);
  if (strlen(so) < 4) return false;
  caiTinNhan();
  snprintf(lenh, sizeof lenh, "AT+CMGS=\"%s\"", so);
  if (!at(lenh, ">", 5000)) {
    cong.write(0x1B);  // ESC: huỷ lệnh đang dở
    return false;
  }
  cong.print(noiDung);
  cong.write(0x1A);    // Ctrl+Z: gửi
  return at(nullptr, "+CMGS", 60000);
}

inline bool cuocGoiDaKetThuc(const String& r) {
  return r.indexOf("NO CARRIER") >= 0 || r.indexOf("BUSY") >= 0 || r.indexOf("NO ANSWER") >= 0;
}

// Gọi cho người nhà, để đổ chuông tối đa doDaiMs rồi tự cúp (module không gắn micro/loa). Chỉ coi là gọi được khi
// AT+CLCC cho thấy máy người nhà đã đổ chuông (trạng thái 3) hoặc đã nghe máy (0): trên mạng 4G không có VoLTE cho
// module, ATD vẫn trả OK nhưng cuộc gọi tắt ngay, khi đó phải báo "không gọi được" chứ không phải "đã gọi".
inline bool goiNgay(uint32_t doDaiMs = 25000) {
  char so[20], lenh[40];
  laySo(so, sizeof so);
  if (strlen(so) < 4) return false;
  snprintf(lenh, sizeof lenh, "ATD%s;", so);
  if (!at(lenh, "OK", 10000)) return false;
  bool doChuong = false;
  uint32_t t0 = millis();
  while (millis() - t0 < doDaiMs) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    String r;
    while (cong.available()) r += (char)cong.read();   // tin tự báo đến giữa 2 lần hỏi (at() sẽ xoá bộ đệm)
    if (cuocGoiDaKetThuc(r)) break;
    String q;
    at("AT+CLCC", "OK", 2000, &q);
    if (cuocGoiDaKetThuc(q)) break;
    int i = q.indexOf("+CLCC:");
    if (i < 0) break;                                   // không còn cuộc gọi nào
    int p1 = q.indexOf(',', i), p2 = p1 < 0 ? -1 : q.indexOf(',', p1 + 1);
    int trangThaiGoi = p2 < 0 ? -1 : q.substring(p2 + 1).toInt();   // +CLCC: id,chiều,trạng thái,...
    if (trangThaiGoi == 0 || trangThaiGoi == 3) doChuong = true;
  }
  at("ATH", "OK", 5000);
  return doChuong;
}

inline void tacVu(void*) {
  khoiDong();
  uint32_t mocKiemTra = millis();
  for (;;) {
    YeuCau yc;
    if (xQueueReceive(hangViec, &yc, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (trangThai == -1) khoiDong();
      bool ok = trangThai != -1 && (yc.viec == Viec::NHAN_TIN ? nhanTinNgay(yc.noiDung) : goiNgay());
      KetQua kq = {yc.viec, ok};
      xQueueSend(hangKetQua, &kq, 0);
    }
    if (millis() - mocKiemTra > 60000) {  // mỗi phút kiểm tra thẻ SIM và sóng
      mocKiemTra = millis();
      if (at("AT", "OK", 1000)) trangThai = !theSanSang() ? -3 : (kiemTraSong() ? 1 : -2);
      else khoiDong();
    }
  }
}

inline void batDau(int rx, int tx, int rst) {
  chanReset = rst;
  pinMode(rst, OUTPUT);
  digitalWrite(rst, HIGH);
  cong.begin(SIM_TOC_DO, SERIAL_8N1, rx, tx);
  hangViec = xQueueCreate(4, sizeof(YeuCau));
  hangKetQua = xQueueCreate(4, sizeof(KetQua));
  xTaskCreatePinnedToCore(tacVu, "sim", 6144, nullptr, 2, nullptr, 1);
}

inline bool nhanTin(const char* noiDung) {
  YeuCau yc = {Viec::NHAN_TIN, {}};
  strlcpy(yc.noiDung, noiDung, sizeof yc.noiDung);
  return xQueueSend(hangViec, &yc, 0) == pdTRUE;
}

inline bool goiDien() {
  YeuCau yc = {Viec::GOI_DIEN, {}};
  return xQueueSend(hangViec, &yc, 0) == pdTRUE;
}

}  // namespace sim
