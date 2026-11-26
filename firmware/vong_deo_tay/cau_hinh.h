// cau_hinh.h — sửa các dòng dưới đây trước khi nạp code.
#pragma once

// ===== Mạng (giống nhau trên cả 3 thiết bị) =====
#define WIFI_TEN        "GIAOTHONG"
#define WIFI_MAT_KHAU   "12345678"
#define MQTT_MAY_CHU    "172.17.180.109"  // IP máy tính chạy Mosquitto (gõ ipconfig trên máy đó để xem)
#define MQTT_CONG       1883
#define MQTT_TAI_KHOAN  ""              // để trống nếu broker không đặt mật khẩu
#define MQTT_MAT_KHAU   ""
#define MA_NGUOI_DUNG   "bn01"          // mã người đeo, giống nhau trên 3 thiết bị của cùng một người
#define MUI_GIO_GIAY    (7 * 3600)      // giờ Việt Nam (UTC+7)

// ===== Riêng vòng đeo tay (ESP32-C3 SuperMini: khi nạp chọn board "Nologo ESP32C3 Super Mini") =====
#define XEM_DO_THI      0               // 1 = in sóng PPG ra Serial Plotter để kiểm tra cảm biến
#define CHAN_BAO_DONG   -1              // chân báo động: mô-tơ rung hoặc còi nhỏ, nối qua transistor như còi vòng ngực.
                                        // -1 = chân mặc định: chân "10" của SuperMini (GPIO10), D10 của XIAO.
                                        // Không lắp gì vào chân này thì vẫn để nguyên
#define WIFI_CONG_SUAT  WIFI_POWER_8_5dBm  // công suất phát Wi-Fi. Nhiều mạch ESP32-C3 SuperMini phát hết công suất
                                        // (19,5 dBm) thì không vào được Wi-Fi; giảm còn 8,5 dBm là cách sửa phổ biến, lại
                                        // đỡ tốn pin. Router ở xa, kết nối chập chờn thì thử WIFI_POWER_11dBm
#define TIET_KIEM_PIN   0               // 1 = tháo vòng quá 30 giây thì giảm đèn LED và ngừng gửi sóng (MAX30102, MAX30100).
                                        // Chỉ bật sau khi Serial Monitor báo "chạm da" đúng cả lúc đeo lẫn lúc tháo
#define I2C_TOC_DO      400000          // tốc độ I2C (Hz). Module GY-MAX30100 mà đọc lúc được lúc không thì giảm còn 100000
#define NGUONG_CHAM_DA_MAX 0.19f        // MAX30100/MAX30102: số đọc trung bình trên 19 % thang đo là đang áp vào da.
                                        // Phải nhỏ hơn 0.20 (lúc đeo, firmware tự chỉnh LED cho số đọc từ 20 % trở lên).
                                        // Cách chỉnh theo "% thang đo" trên Serial Monitor: HUONG_DAN.md, mục GY-MAX30100
#define SPO2_BU         0               // bù SpO2 (%) sau khi so với máy kẹp ngón lúc ngồi yên: số của máy kẹp ngón trừ
                                        // số của vòng tay, vd. máy 97, vòng 94 → đặt 3 (HUONG_DAN.md, "Hiệu chỉnh SpO2")
