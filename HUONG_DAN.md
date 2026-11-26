# AIoT phát hiện sớm nguy cơ đột quỵ

Hệ thống theo dõi liên tục tim, nhịp thở, SpO₂ và té ngã để phát hiện sớm nguy cơ đột quỵ. Ba thiết bị (vòng đeo tay, vòng đeo ngực, hộp radar đặt cạnh giường) gửi tín hiệu qua Wi-Fi về một máy tính. Nền tảng trên máy tính lọc nhiễu, chạy các mô hình AI và cảnh báo người nhà bằng còi, tin nhắn, cuộc gọi và Telegram; dashboard web xem được trên điện thoại.

Thư mục này gồm sơ đồ mạch, firmware của 3 thiết bị, nền tảng chạy trên máy tính, notebook huấn luyện AI và hộp in 3D.

> Đây là sản phẩm nghiên cứu, không thay thế thiết bị y tế.

## Hệ thống

```
Vòng tay  (PPG, nhịp tim, SpO2) ─┐
Vòng ngực (ECG, PVDF, IMU)      ─┼─ Wi-Fi, MQTT ─▶ Mosquitto ─▶ may_chu.py ai ─┬─▶ aiot/bn01/canh_bao ─┬─▶ vòng ngực: còi, tin nhắn, cuộc gọi
Radar     (sóng thở, sóng tim)  ─┘                 (máy tính)                  │                       └─▶ vòng tay: rung
                                                                               ├─▶ dashboard http://<IP>:8080, Telegram
                                                                               └─▶ du_lieu/lich_su_bn01.db
```

| Thiết bị | Phần cứng chính | Đo và làm gì |
|---|---|---|
| Vòng đeo tay | ESP32-C3 SuperMini, GY-MAX30100 | sóng PPG 100 mẫu/giây, nhịp tim, SpO₂, nhiệt độ da; rung khi có cảnh báo |
| Vòng đeo ngực | ESP32 D1 mini, AD8232, màng PVDF, MPU6050, module 4G A7670C, còi SFM-20B | ECG 500 mẫu/giây, sóng thở 25 mẫu/giây, té ngã, mức vận động; AI nhỏ chạy ngay trên vòng; còi, tin nhắn, cuộc gọi |
| Hộp radar | bộ kit Seeed MR60BHA2 60 GHz (XIAO ESP32C6) | sóng thở, sóng tim, nhịp thở, nhịp tim, khoảng cách tới người lúc ngủ; nút SOS |
| Máy tính | Mosquitto, `may_chu.py` | lọc nhiễu, tính chỉ số, chạy AI, gửi cảnh báo, dashboard, lưu lịch sử |

### Phát hiện và cảnh báo

- **Rối loạn nhịp tim, nhịp thở bất thường**: máy chủ phát hiện bằng các mô hình AI rồi gửi lệnh cảnh báo về vòng ngực (còi, tin nhắn, cuộc gọi) và vòng tay (rung).
- **Nhịp tim không đều kéo dài** (nghi rung nhĩ, nguyên nhân hàng đầu của đột quỵ do cục máu đông): máy chủ phân tích khoảng cách giữa các đỉnh R trong khoảng 1 phút gần nhất, kết hợp với kết quả AI.
- **SpO₂ thấp**: dưới 90 % liên tục khoảng 15 giây thì cảnh báo nhẹ; dưới 85 % thì cảnh báo nguy cơ cao, nhắn tin và gọi.
- **Ngưng thở khi ngủ** (từ SpO₂ vòng tay, chạy cả khi không đeo vòng ngực): cứ 10 giây máy chủ xem SpO₂ có tụt rồi hồi lại kiểu ngưng thở không; từ 10 phút/giờ trở lên thì cảnh báo nhẹ "nghi ngưng thở khi ngủ, nên đi khám" (tối đa 1 lần/giờ). SpO₂ còn dùng để xác nhận cảnh báo nhịp thở của vòng ngực.
- **Té ngã**: vòng ngực tự phát hiện bằng MPU6050, không cần máy chủ. Còi kêu 15 giây; người đeo tự đứng dậy thì huỷ, nếu không vòng ngực nhắn tin và gọi người nhà.
- **Nút SOS** trên hộp radar (giữ 2 giây): vòng ngực nhận trực tiếp qua broker rồi nhắn tin và gọi.
- **Huyết áp**: nhập số đo của máy đo huyết áp trên dashboard; khi đeo cả vòng ngực và vòng tay, nền tảng ước tính xu hướng huyết áp từ thời gian mạch đến (mục "Huyết áp").
- **Telegram** (tuỳ chọn): mọi cảnh báo của nền tảng còn được gửi tới điện thoại người nhà, kèm chỉ số lúc đó và link dashboard; vẫn báo được khi SIM mất sóng.
- **Khi mất máy chủ** (mất Wi-Fi, Mosquitto hoặc `may_chu.py ai` tắt quá 15 giây), vòng ngực tự lo 2 việc và báo qua SIM (mạng di động nên không cần Wi-Fi):
  - **Ngưng thở**: phát hiện bằng màng PVDF ở dây đeo. Ngưng thở từ 20 giây thì còi kêu 15 giây để đánh thức; vẫn chưa thở lại thì nhắn tin và gọi. Đây chỉ là lớp dự phòng: cảm biến ở dây đeo chỉ nhận ra ngưng thở khi lồng ngực không còn cử động.
  - **Nhịp tim nguy cơ cao**: AI nhỏ trên vòng (mục "AI trên vòng ngực (TinyML)") báo "Nguy cơ cao" 6 lần liền (khoảng 30 giây) thì vòng kêu còi và nhắn tin, không gọi vì mô hình nhỏ kém chính xác hơn mô hình trên máy chủ.

## Thư mục dự án

| Đường dẫn | Nội dung |
|---|---|
| `so-do-mach/so-do-mach.html` | Trang tổng hợp (mở bằng trình duyệt): sơ đồ nối dây từng chân của 3 thiết bị (mỗi dây có số và màu gợi ý), bảng nối dây, danh sách linh kiện, lưu ý an toàn; sơ đồ nguyên lý nằm trong mục thu gọn của từng thiết bị |
| `so-do-mach/1_vong_deo_tay.svg` | Hình A, vòng tay: ESP32-C3 SuperMini, GY-MAX30100 (2 điện trở kéo lên 3V3), pin LiPo, mạch sạc TP4056, công tắc, điốt Schottky vào chân 5V, tụ 100 µF, mô-tơ rung tuỳ chọn |
| `so-do-mach/2_vong_deo_nguc.svg` | Hình B, vòng ngực: ESP32 D1 mini, AD8232, màng PVDF với mạch phân cực 1,65 V, MPU6050, module 4G A7670C, còi SFM-20B, pin 18650, TP4056, MT3608 |
| `so-do-mach/3_thiet_bi_mmwave.svg` | Hình C, hộp radar tự lắp: XIAO ESP32C3 và radar MR60BHA1. Bộ kit MR60BHA2 đã nối sẵn nên không cần hình này |
| `so-do-mach/noi-day/` | Từng hình nối dây: A1 vòng tay, A2 thay bằng MAX30102, B0 bản đồ chân ESP32 D1 mini, B1–B7 bảy bước lắp vòng ngực, C1 radar |
| `firmware/vong_deo_tay/` | Vòng tay (ESP32-C3 SuperMini; vẫn chạy trên XIAO ESP32C3/C6): PPG 100 mẫu/giây, tự tính nhịp tim (khử gai, lọc Butterworth 0,5–4 Hz, bỏ qua lúc cử động), chất lượng tín hiệu, chỉ số tưới máu, SpO₂ theo từng nhịp (bỏ nhịp có cử động, hiệu chỉnh được theo máy kẹp ngón), nhiệt độ da; rung hoặc kêu khi nền tảng gửi cảnh báo. Tự nhận MAX30100 (GY-MAX30100), MAX30102 hoặc AS7038RB |
| `firmware/vong_deo_nguc/` | Vòng ngực: ECG 500 mẫu/giây, sóng thở 25 mẫu/giây (màng PVDF, hoặc FSR402), phát hiện té ngã, tự phát hiện ngưng thở khi mất máy chủ, AI chạy ngay trên vòng (`ai_nhip.h`, `mo_hinh_tinyml.h`), nhắn tin và gọi qua module 4G A7670C (`module_sim.h`, vẫn chạy với SIM800L), còi SFM-20B |
| `firmware/thiet_bi_radar/` | Hộp radar: tự nhận bộ kit MR60BHA2 (XIAO ESP32C6) hoặc MR60BHA1; sóng thở, sóng tim, nhịp tim, nhịp thở, khoảng cách tới người, cử động, đếm các lần ngưng cử động thở; nút SOS và đèn báo |
| `may_chu/may_chu.py` | Nền tảng: tính chỉ số, chạy các mô hình AI, gửi cảnh báo, mở dashboard; thêm chế độ xem dữ liệu và chế độ ghi CSV + WAV để huấn luyện lại |
| `may_chu/aiot_infer.py` | Thư viện lọc nhiễu và AI dùng chung với notebook (bản 3): chắn điện lưới 50 Hz, bỏ trôi đường nền bằng lọc trung vị, lọc Hampel, 2 bộ tìm đỉnh R (Pan–Tompkins, Elgendi) ghép lại, chất lượng tín hiệu có bSQI, HRV và hình thái nhịp, luật rung nhĩ COSEn, thời gian mạch đến, mel-spectrogram, trọng số chú ý của AI, ngưng thở khi ngủ từ SpO₂ (`NgungThoSpO2`) |
| `may_chu/nen_tang_web.py`, `may_chu/dashboard/index.html` | Dashboard web: chỉ số trực tiếp, dạng sóng, kết quả AI, cảnh báo, bảng kiểm dấu hiệu đột quỵ F.A.S.T., xu hướng 30 phút đến 7 ngày, xuất CSV, thống kê trong ngày, nhật ký sự kiện, cài đặt số điện thoại người nhà, huyết áp và chế độ ban đêm. Lịch sử lưu trong SQLite (WAL) |
| `may_chu/thong_bao.py` | Gửi cảnh báo qua Telegram cho người nhà, song song với tin nhắn SIM |
| `may_chu/gia_lap_thiet_bi.py` | Giả lập 3 thiết bị bằng ECG thật MIT-BIH, để thử hoặc trình diễn khi chưa có phần cứng |
| `may_chu/*.bat` | Bấm đúp để chạy: `0_cai_thu_vien`, `1_chay_mosquitto`, `2_chay_nen_tang`, `3_gia_lap`; `chay_demo` mở cả 3 rồi mở dashboard |
| `may_chu/mosquitto.conf`, `may_chu/requirements.txt` | Cấu hình broker MQTT, danh sách thư viện Python |
| `may_chu/aiot_models/` | Các mô hình đang dùng (`cnn_gru_fusion*.keras`, `phan_loai_nhip.joblib`, `breath_iforest.joblib`, `ngung_tho_spo2.json`), cấu hình `model_config.json` và các bảng đánh giá `danh_gia_*.csv` |
| `train_aiot_dot_quy.ipynb` | Notebook Google Colab huấn luyện các mô hình: SE-block, attention, residual, Focal Loss, mel-spectrogram, đặc trưng HRV và hình thái nhịp, tăng cường nhiễu NSTDB và PPG thật BIDMC, đa nguồn PhysioNet (MIT-BIH, INCART, AFDB, SVDB, CUDB, PTB-XL, Apnea-ECG); Phần B2: ngưng thở khi ngủ từ SpO₂ vòng tay |
| `hop_in_3d/` | Hộp in 3D và nắp ấn khít (mục "Hộp in 3D") |
| `bao_cao/` | Báo cáo tóm tắt kết quả nghiên cứu (Word) |

File SVG phóng to không vỡ: mở bằng trình duyệt, xem qua `so-do-mach.html`, hoặc chèn vào Word 2019 trở lên.

## Phần cứng

### Linh kiện

| Linh kiện | Thiết bị | Ghi chú |
|---|---|---|
| ESP32-C3 SuperMini | vòng tay | board **Nologo ESP32C3 Super Mini**; I2C ở chân 6, 7; giảm công suất Wi-Fi; cấp pin qua chân 5V |
| Cảm biến nhịp tim và SpO₂ GY-MAX30100 | vòng tay | firmware tự nhận (thanh ghi PART_ID = 0x11): PPG 100 mẫu/giây, 16 bit, SpO₂, nhiệt độ da. **Phải sửa điện trở kéo lên I2C** |
| Pin LiPo 300–500 mAh, TP4056 Type-C, công tắc, điốt Schottky SS14 hoặc 1N5819, tụ 100 µF | vòng tay | nguồn |
| ESP32 D1 mini (ESP32-WROOM-32) | vòng ngực | board **WEMOS D1 MINI ESP32** |
| AD8232 | vòng ngực | ECG 500 mẫu/giây |
| Màng áp điện PVDF | vòng ngực | cử động thở ở dây đeo ngực, qua mạch phân cực 1,65 V (`CAM_BIEN_THO_PVDF 1`) |
| MPU6050 (GY-521) | vòng ngực | té ngã, mức vận động |
| Module 4G LTE Cat.1 A7670C Mini, ăng-ten FPC | vòng ngực | nhắn tin, gọi điện (`module_sim.h`, `SIM_TOC_DO 115200`) |
| Còi SFM-20B 3–24 V (kêu liên tục), transistor NPN | vòng ngực | còi chủ động: firmware chỉ bật/tắt chân 25 theo nhịp |
| Pin 18650, TP4056 Type-C, mạch tăng áp MT3608 (2 A) | vòng ngực | pin → 5 V cho ESP32 D1 mini và còi |
| Bộ kit radar Seeed MR60BHA2 60 GHz (XIAO ESP32C6, đèn RGB, cảm biến ánh sáng BH1750) | hộp radar | đã nối sẵn; board **XIAO_ESP32C6** |
| Board đục lỗ 6×8 cm | lắp mạch | |

Hình A và B (`so-do-mach/1_vong_deo_tay.svg`, `so-do-mach/2_vong_deo_nguc.svg`) vẽ đúng các linh kiện trên. Các hình từng bước trong `so-do-mach.html` vẫn vẽ linh kiện cũ (XIAO ESP32C3, AS7038RB hoặc MAX30102, FSR402, SIM800L): khi lắp, theo Hình A, B và các mục dưới đây. Firmware vẫn chạy được với các linh kiện cũ đó.

### Vòng tay: ESP32-C3 SuperMini

- Arduino IDE: chọn board **Nologo ESP32C3 Super Mini** (đã bật sẵn USB CDC On Boot nên Serial Monitor chạy qua cổng USB-C). Nếu chọn "ESP32C3 Dev Module" thì phải tự đặt **USB CDC On Boot: Enabled**, không thì Serial Monitor trống.
- Không thấy cổng COM hoặc nạp báo lỗi: giữ nút BOOT, bấm rồi thả RESET (hoặc cắm cáp trong lúc giữ BOOT), thả BOOT rồi bấm Upload. Nạp xong bấm RESET.
- Chân: GY-MAX30100 SDA → chân 6, SCL → chân 7; mô-tơ rung hoặc còi nhỏ (tuỳ chọn) qua transistor ở chân 10; đèn xanh trên board (chân 8) sáng khi có cảnh báo. Không dùng chân 8, 9 cho I2C (chân 8 nối đèn, chân 9 nối nút BOOT). Chân 2, 8, 9 là chân chọn chế độ khởi động: không nối chúng xuống GND.
- Wi-Fi: nhiều mạch SuperMini phát hết công suất thì không vào được Wi-Fi. Firmware đặt sẵn 8,5 dBm (`WIFI_CONG_SUAT` trong `cau_hinh.h`), đủ dùng trong nhà và đỡ tốn pin; router ở xa mà hay rớt mạng thì thử `WIFI_POWER_11dBm`.
- Nguồn: pin LiPo 300–500 mAh → TP4056 (B+, B−) → OUT+ → công tắc → điốt Schottky SS14 hoặc 1N5819 (đầu có vạch quay về phía board) → chân **5V** của SuperMini; OUT− → G. SuperMini không có chân BAT và mạch sạc như XIAO; ổn áp trên board hạ xuống 3,3 V. Điốt chặn điện 5 V từ cáp USB chạy ngược vào pin khi cắm máy tính nạp code; sạc pin bằng cổng Type-C của TP4056. Hàn thêm tụ hoá 100 µF (≥ 6,3 V) giữa chân 3.3 và G cho đỡ sụt áp lúc phát Wi-Fi. Pin xuống dưới khoảng 3,5 V thì Wi-Fi hay rớt: sạc lại.
- Dòng sạc: TP4056 xuất xưởng sạc 1 A (điện trở PROG 1,2 kΩ, ghi "122"), quá mạnh cho pin nhỏ (pin nóng, nhanh hỏng). Thay điện trở PROG bằng 4,7 kΩ (khoảng 250 mA) hoặc 10 kΩ (khoảng 130 mA).

### GY-MAX30100: sửa điện trở kéo lên I2C

**Bắt buộc khi dùng với ESP32.** Trên module GY-MAX30100 màu tím phổ biến, 3 điện trở 4,7 kΩ (ghi "472") kéo SDA, SCL, INT lên nguồn 1,8 V của chip, nên mức cao chỉ 1,8 V. ESP32 cần khoảng 2,5 V trở lên mới hiểu là mức cao, nên thường không đọc được cảm biến (Serial báo "Không thấy … MAX30100"). Cách sửa: gỡ 3 điện trở 4,7 kΩ đó, rồi hàn 2 điện trở 4,7 kΩ từ SDA và SCL lên chân 3,3 V của board (hoặc cắt đường mạch nối 3 điện trở với nguồn 1,8 V và nối chúng sang 3V3). Nếu vẫn lúc đọc được lúc không, đặt `I2C_TOC_DO 100000` trong `firmware/vong_deo_tay/cau_hinh.h`.

- Nối với ESP32-C3 SuperMini: VIN của module → chân 3.3 (module có ổn áp 1,8 V riêng cho chip), GND → G, SDA → chân 6, SCL → chân 7; INT để trống. (XIAO ESP32C3: SDA → D4, SCL → D5, cũng là GPIO6, GPIO7.)
- Cửa sổ đèn áp sát da mặt trong cổ tay, dây đeo vừa chặt, che ánh sáng ngoài. Lần đầu, mở Serial Monitor xem dòng `[PPG] MAX30100 · số đọc trung bình …/65535 (… % thang đo; đỏ … %) · dòng LED …, đỏ … · chạm da …` (30 giây in 1 lần): đeo vào phải ra `chạm da 1`, tháo ra phải ra `chạm da 0`.
- Ngưỡng chạm da là `NGUONG_CHAM_DA_MAX` trong `firmware/vong_deo_tay/cau_hinh.h` (mặc định `0.19f`, tức 19 % thang đo). Lúc đeo, firmware tự chỉnh dòng LED để số đọc không dưới 20 %, nên ngưỡng phải nhỏ hơn 0,20.
  - Đang đeo mà vẫn `chạm da 0` (dòng LED đã lên 15 mà số đọc dưới 19 %): đeo chặt hơn, hoặc hạ ngưỡng xuống thấp hơn % thang đo lúc đeo.
  - Đã tháo mà vẫn `chạm da 1`: chờ khoảng 10 giây cho dòng LED tăng hết mức, xem % thang đo lúc tháo; ngưỡng phải lớn hơn số đó. Nếu số đó đã gần 20 %, ánh sáng LED đang phản xạ trên vỏ hộp vào thẳng cảm biến: dán băng keo đen hoặc mút đen quanh cảm biến, chỉ chừa cửa sổ.
- Firmware tự chỉnh riêng dòng LED đỏ và LED hồng ngoại, giữ cả 2 kênh trong khoảng 20–85 % thang đo. Da hấp thụ ánh sáng đỏ mạnh hơn hồng ngoại; nếu 2 LED dùng chung một mức thì kênh đỏ dễ quá tối (nhiễu) hoặc bão hoà, làm sai tỉ số đỏ/hồng ngoại dùng để tính SpO₂. Mỗi lần LED đỏ đổi mức, bộ tính SpO₂ học lại từ đầu (cần khoảng 4 nhịp), nhịp tim vẫn tính tiếp.
- MAX30100 kém MAX30102: dòng LED chỉ 16 mức, ADC 16 bit (MAX30102 18 bit). SpO₂ dùng đường cong mẫu của Maxim, bù lệch theo máy kẹp ngón ở mục "Hiệu chỉnh SpO2"; mô hình ngưng thở khi ngủ chỉ dùng mức tụt so với nền nên không phụ thuộc sai lệch này.

### Hiệu chỉnh SpO2

SpO₂ tính từ tỉ số R của kênh đỏ và kênh hồng ngoại theo đường cong mẫu của Maxim; mỗi vòng lệch đi vài % tuỳ cách lắp. Cần 1 máy đo SpO₂ kẹp ngón (bán ở hiệu thuốc):

1. Đeo vòng tay, kẹp máy vào một ngón của cùng bàn tay, ngồi yên, thở bình thường khoảng 2 phút cho số ổn định.
2. Mở Serial Monitor: cứ 30 giây vòng in `SpO2 … (R …)`. Ghi 5 lần liền số SpO₂ của vòng và số trên máy kẹp ngón cùng lúc.
3. `SPO2_BU` = trung bình của máy kẹp ngón − trung bình của vòng tay, làm tròn (ví dụ 97 − 94 = 3). Đặt trong `firmware/vong_deo_tay/cau_hinh.h` rồi nạp lại.

Cách này chỉ bù được độ lệch ở mức SpO₂ bình thường (94–99 %), không thay được hiệu chuẩn bằng máy chuẩn; không nín thở hay tự làm giảm SpO₂ để thử. Mô hình ngưng thở khi ngủ dùng mức tụt so với nền nên không phụ thuộc bước này; bước này giúp ngưỡng cảnh báo 90 % và 85 % sát hơn. Firmware chỉ lấy những nhịp không có cử động từ đầu đến cuối nhịp, và xoá số SpO₂ cũ nếu 15 giây không có nhịp hợp lệ.

### Vòng ngực: nguồn và còi

**Nguồn: pin 18650 → TP4056 → công tắc → MT3608 (5 V).**
- Trước khi nối ESP32: cấp pin vào MT3608, vặn biến trở trên MT3608 (thường phải xoay ngược chiều kim đồng hồ nhiều vòng) và đo bằng đồng hồ vạn năng cho đầu ra đúng 5,0 V. MT3608 mới mua thường cho điện áp cao hơn nhiều; nối ngay có thể cháy ESP32.
- 5 V của MT3608 → chân 5V (VIN) của ESP32 D1 mini và cực (+) của còi.
- TP4056 xuất xưởng sạc 1 A (điện trở PROG 1,2 kΩ): hợp với pin 18650.

**Còi SFM-20B** (còi chủ động, có điện là kêu): (+) của còi → 5 V của MT3608; (−) của còi → cực C của transistor NPN (ví dụ S8050, 2N2222); cực E → GND; cực B → GPIO25 qua điện trở 1 kΩ. Firmware bật/tắt chân 25 theo nhịp: 1 tiếng bíp khi khởi động, 2 tiếng ngắn cho cảnh báo mức 1, kêu từng hồi cho mức 2.

### Module 4G A7670C

- Luôn gắn ăng-ten FPC trước khi cấp điện. Thẻ SIM phải là SIM 4G.
- Nguồn: khi phát sóng, module hút dòng đỉnh khoảng 1–2 A. Xem chữ in trên mạch hoặc trang bán hàng: chân nguồn ghi 3,4–4,2 V (VBAT) thì nối thẳng đầu ra của TP4056 (pin 18650); ghi 5 V thì lấy từ đầu ra MT3608 đã chỉnh đúng 5,0 V. Hàn thêm tụ hoá 1000 µF (≥ 10 V) sát chân nguồn module. Không lấy nguồn từ chân 3V3 hay 5V của ESP32. GND của module, ESP32 và pin nối chung.
- Tín hiệu: TX của module → GPIO16, RX của module → GPIO17. Mạch có chân RESET thì nối GPIO4, không có thì để trống. **Không** nối chân PWRKEY/PWR vào GPIO4 (giữ PWRKEY ở mức thấp lâu sẽ tắt module); đa số mạch mini tự bật khi có điện.
- Serial Monitor in `sim 1` là đã đăng ký mạng, `-2` là chưa có sóng, `-3` là thẻ SIM chưa sẵn sàng (chưa cắm, cắm ngược, hoặc thẻ còn mã PIN: lắp thẻ vào điện thoại, tắt khoá SIM bằng mã PIN rồi cắm lại), `-1` là module không trả lời (kiểm tra TX/RX có đảo chéo chưa, nguồn, tốc độ 115200). Dashboard hiện cùng trạng thái này. Module 4G khởi động mất 10–20 giây; firmware chờ thẻ SIM sẵn sàng (`AT+CPIN?`) rồi mới cài chế độ tin nhắn, và kiểm tra lại mỗi phút.
- Thử thật: trên dashboard đánh dấu 1 ô của bảng F.A.S.T. rồi bấm ghi nhận. Nền tảng phát cảnh báo mức 2, vòng ngực nhắn tin và gọi số người nhà (nhớ tắt báo động sau đó). Nhật ký sự kiện chỉ ghi "Đã gọi điện cho người nhà" khi điện thoại người nhà thật sự đổ chuông: sau lệnh gọi, firmware hỏi trạng thái cuộc gọi (`AT+CLCC`) mỗi giây; cuộc gọi bị ngắt trước khi đổ chuông thì nhật ký ghi "Không gọi được điện thoại".
- Gọi điện trên mạng 4G cần VoLTE. Nếu nhắn tin được mà gọi không được, có thể nhà mạng chưa bật VoLTE cho module; cảnh báo vẫn đến người nhà qua tin nhắn và Telegram.

### Màng áp điện PVDF

Màng PVDF chỉ sinh điện khi bị uốn hoặc kéo giãn, điện áp ra có cả phần âm, còn chân ADC của ESP32 chỉ đọc được 0–3,3 V. Vì vậy cần mạch phân cực đưa tín hiệu lên quanh 1,65 V (linh kiện thường, hàn trên board đục lỗ):
- 2 điện trở 1 MΩ nối tiếp từ 3V3 xuống GND; điểm giữa gọi là P (1,65 V). Tụ 10 µF từ P xuống GND.
- Màng PVDF: 1 dây vào P, dây còn lại vào điểm S.
- Điện trở 10 MΩ từ S về P (xả điện tích, giữ S quanh 1,65 V) và tụ 220 nF (gốm hoặc phim) từ S về P (giữ được sóng thở chậm).
- Điện trở 10 kΩ từ S vào GPIO39 (bảo vệ chân ADC khi màng bị đập mạnh).
- Gắn màng vào mặt trong dây đeo ngực, nằm dọc theo dây, dán chặt 2 đầu màng vào dây bằng băng keo vải để khi ngực nở, dây căng làm màng bị uốn. Không gập gãy màng; dây đeo vừa chặt.
- Kiểm tra: đặt `XEM_DO_THI 1` trong `firmware/vong_deo_nguc/cau_hinh.h`, nạp lại, mở Serial Plotter. Đường `tho` phải dao động quanh khoảng 2000, mỗi lần thở một sóng; nín thở 10 giây thì gần như phẳng. Sóng quá nhỏ (chênh cao thấp dưới khoảng 60, vòng sẽ không dám tự báo ngưng thở) thì giảm tụ 220 nF xuống 100 nF; sóng chạm 0 hoặc 4095 thì tăng lên 470 nF.
- Module PVDF bán sẵn kèm mạch (có chân AO, DO) thường chỉ có điện trở 1 MΩ nên chỉ bắt được rung nhanh, gần như không thấy nhịp thở: dùng màng trần 2 dây với mạch trên, hoặc lấy 2 chân của màng trên module nối theo mạch trên.
- Firmware (`CAM_BIEN_THO_PVDF 1`, mặc định) đọc cảm biến thở mỗi 2 ms rồi lấy trung bình 20 lần thành 25 mẫu/giây để giảm nhiễu. Màng PVDF không đo được lực tĩnh của dây đeo, nên vòng ngực coi là "đang đeo" khi điện cực ECG không tuột. Nếu tháo dây đeo mà vẫn để điện cực trên người, lúc mất máy chủ vòng có thể báo nhầm ngưng thở: tháo cả điện cực, hoặc tắt vòng ngực. Dùng FSR402 thay màng PVDF thì đặt `CAM_BIEN_THO_PVDF 0`.
- Máy chủ không cần sửa gì: luồng sóng thở vẫn tên `fsr` trong MQTT và file CSV (để tương thích), dashboard ghi là "thở ngực". Máy chủ hiệu chuẩn 60 giây đầu theo từng người nên biên độ khác FSR không ảnh hưởng.

### Hộp radar: bộ kit MR60BHA2

Bộ kit gồm radar MR60BHA2 60 GHz đã nối sẵn với XIAO ESP32C6 (radar ở D6, D7), 1 đèn RGB ở D1 và cảm biến ánh sáng BH1750 (I2C ở D4, D5), nên không cần nối dây.

**Lắp đặt**
- Nạp code: mở `firmware/thiet_bi_radar`, chọn board **XIAO_ESP32C6**. Không cần cài thư viện của Seeed, firmware tự đọc khung dữ liệu của radar. Cắm USB-C 5 V liên tục.
- Mở Serial Monitor: sau vài giây phải có dòng `[Radar] Nhận ra radar MR60BHA2`, rồi cứ 3 giây một dòng nhịp tim, nhịp thở, khoảng cách. Firmware cũng đọc được MR60BHA1 (mạch tự lắp theo Hình C, board XIAO ESP32C3): tự nhận loại radar, không cần sửa gì.
- Đèn báo dùng đèn RGB có sẵn trên kit (`CHAN_DEN_RGB D1` trong `cau_hinh.h`, mặc định). Lắp theo Hình C với đèn WS2812B rời thì đặt `D0`. Nút SOS (tuỳ chọn): nối nút giữa chân D3 và GND. Nếu lúc khởi động chân D3 đã ở mức thấp (không lắp nút, hoặc kit nối chân này vào thứ khác), firmware tắt nút để không gửi SOS giả.
- Đặt hộp cạnh giường, mặt radar hướng vào ngực người nằm, không có vật chắn. Nhịp thở, nhịp tim chỉ đo được khi người nằm yên.
- Kit nhìn giống hệt nhưng mạch ghi **MR60FDA2** là radar phát hiện té ngã, không đo nhịp thở, nhịp tim; khi đó sau 30 giây Serial sẽ nhắc.

**Xử lý ngay trên thiết bị** (`xu_ly_radar.h`, dùng cho cả MR60BHA1 và MR60BHA2)
- Sóng: MR60BHA2 gửi pha thở, pha tim (radian), không có thang cố định như sóng 0–255 của MR60BHA1 và trôi dần khi người xoay mình. Firmware gom thành 5 điểm mỗi giây, bỏ mức nền trôi chậm (khoảng 10 giây), rồi chuẩn hoá về 0–255 quanh 128 theo độ lệch trung bình (khoảng 20 giây). Sau đó gửi lên cùng chủ đề như MR60BHA1, nên dashboard vẽ và máy chủ đọc như nhau. Không có người thì sóng phẳng ở 128.
- Có người: theo khung "có người" nếu firmware radar có gửi. Nếu không, coi là có người khi trong 5 giây qua radar còn báo khoảng cách hợp lệ hoặc nhịp thở, nhịp tim. Nhịp thở, nhịp tim 20 giây không có số mới thì báo "chưa có" (−1), không gửi số cũ.
- Nhịp tim, nhịp thở lấy trung vị 5 số gần nhất radar gửi, bỏ số nhảy vọt. Kèm chất lượng `cl`: 100 tốt; 50 đang cử động hoặc 5 số còn lệch nhau quá 20 %; 0 chưa có. Máy chủ chỉ lấy nhịp từ radar khi `cl` từ 50 trở lên.
- Cử động: MR60BHA2 không tự báo nên firmware ước lượng từ pha. Lúc thở bình thường pha chỉ lệch khỏi nền khoảng 1–1,6 lần độ lệch trung bình; lệch hơn 6 lần là đang cử động. Gửi `van_dong` (0 không có người, 1 nằm yên, 2 cử động) và `cuong_do` (0–100). MR60BHA1 thì dùng số radar tự báo.
- Ngưng cử động thở: so biên độ thở 3 giây gần nhất với mức thở bình thường của chính người đó (học trong 20 giây nằm yên đầu tiên); dưới 25 % lúc người nằm yên là đang ngưng.
  - Từ 10 giây được đếm vào `lan_ngung` (số lần trong 1 giờ qua), dashboard hiện ở dòng Radar.
  - Từ 20 giây radar gửi sự kiện `radar_ngung_tho`; khi thở lại gửi `radar_tho_lai`. Hai sự kiện này chỉ ghi nhật ký, không gửi Telegram.
  - Giống cảm biến ở dây đeo, radar chỉ thấy ngưng cử động thở. Ngưng thở tắc nghẽn (ngực vẫn gắng thở) cần thêm SpO₂ vòng tay.
  - Các ngưỡng 25 %, 10 giây, 20 giây chưa được kiểm chứng với người ngưng thở thật. Kiểm tra nhanh: nín thở 25 giây trước radar thì Serial phải in `radar_ngung_tho`.
- Phòng tối (dưới 3 lux, đo bằng BH1750) thì đèn trạng thái gần như tắt để không làm người ngủ thức giấc; đèn cảnh báo, SOS vẫn sáng rõ.
- CPU chạy 80 MHz cho mát và đỡ tốn điện.

**Chẩn đoán**: trong 3 phút đầu, Serial in 10 giây/lần (sau đó 1 phút/lần) một dòng `[Radar] … byte. MR60BHA2: pha …, nhịp thở …, nhịp tim …, khoảng cách …`, kèm biên độ pha thở, pha tim (radian) và độ sáng phòng.
- "Không nhận được byte nào": radar không gửi gì. Rút USB cắm lại; kiểm tra đã chọn đúng board XIAO_ESP32C6.
- "Có byte nhưng không ghép được khung nào": Serial in kèm 16 byte đầu; chụp lại để kiểm tra loại radar.
- Có khung pha nhưng nhịp thở, nhịp tim luôn −1: chưa có người nằm yên trong tầm radar.

### Hộp in 3D

Thư mục `hop_in_3d/`:
- `hop.step`: hộp vẽ bằng Shapr3D, ngoài 127 × 91 × 38 mm, thành và đáy dày 3 mm, hở miệng trên.
- `nap_hop.step` (mở trong Shapr3D; cùng toạ độ với hộp nên mở cả 2 file là nắp nằm đúng trên miệng hộp) và `nap_hop.stl` (để in): nắp ấn khít, tấm nắp dày 2,4 mm bằng mép ngoài hộp; gờ chèn vào lòng hộp cao 4 mm, thành gờ dày 1,6 mm, khe 0,2 mm mỗi bên, mép dưới gờ vát 0,6 mm cho dễ ấn vào. Khoảng 30 cm³, tức khoảng 37 g nhựa PLA nếu in đặc 100 %.
- In: úp mặt trên của nắp xuống bàn in (gờ hướng lên), không cần support.
- Nắp chặt quá thì tăng `KHE` trong `tao_nap_hop.py` lên 0,25–0,3; lỏng thì giảm còn 0,15. Sau đó chạy `python tao_nap_hop.py` để tạo lại STEP và STL.
- `python ve_xem_truoc.py`: tạo lại nắp theo các số trong `tao_nap_hop.py` rồi vẽ `nap_hop_xem_truoc.png` (nhìn từ trên, nhìn từ dưới, nắp nhấc trên miệng hộp, mặt cắt chỗ gờ gặp thành hộp).

## Cài đặt và chạy

### 1. Nạp firmware (Arduino IDE 2)

1. **Boards Manager** → cài **esp32 by Espressif Systems** bản 3.x (đã dùng 3.3.10). Không cần cài thêm thư viện nào, vì MQTT dùng esp-mqtt có sẵn trong gói board.
2. Mở thư mục từng thiết bị, sửa `cau_hinh.h`:
   - tên và mật khẩu Wi-Fi;
   - `MQTT_MAY_CHU`: IP của máy tính chạy Mosquitto (gõ `ipconfig` trên máy đó, xem IPv4 Address);
   - `MA_NGUOI_DUNG`: giống nhau trên cả 3 thiết bị của cùng một người, ví dụ `bn01`.
3. Riêng vòng tay, trong `cau_hinh.h`:
   - `CHAN_BAO_DONG` (mặc định -1: chân 10 của SuperMini, D10 của XIAO): nối mô-tơ rung hoặc còi nhỏ qua transistor, giống cách nối còi của vòng ngực. Không lắp gì thì cứ để nguyên.
   - `TIET_KIEM_PIN`: đặt 1 thì khi tháo vòng quá 30 giây, firmware giảm dòng LED và ngừng gửi sóng. Chỉ bật sau khi Serial Monitor báo "chạm da" đúng cả lúc đeo lẫn lúc tháo.
   - `NGUONG_CHAM_DA_MAX`: ngưỡng nhận biết đang đeo (mục "GY-MAX30100").
   - `I2C_TOC_DO`: mặc định 400000; GY-MAX30100 đọc lúc được lúc không thì giảm còn 100000.
   - `WIFI_CONG_SUAT`: công suất phát Wi-Fi, mặc định 8,5 dBm (mục "Vòng tay: ESP32-C3 SuperMini").
   - `SPO2_BU`: bù SpO₂ sau khi so với máy kẹp ngón (mục "Hiệu chỉnh SpO2").

   Riêng vòng ngực: `CAM_BIEN_THO_PVDF` (1 = màng PVDF, 0 = FSR402).
4. Chọn board rồi Upload:

   | Thiết bị | Board | Ghi chú |
   |---|---|---|
   | Vòng tay | `Nologo ESP32C3 Super Mini` | Vẫn nạp được cho `XIAO_ESP32C3` hoặc `XIAO_ESP32C6`: code tự đổi chân theo board |
   | Vòng ngực | `WEMOS D1 MINI ESP32` | |
   | Hộp radar | `XIAO_ESP32C6` (bộ kit MR60BHA2) hoặc `XIAO_ESP32C3` (MR60BHA1 tự lắp) | Code dùng tên chân D0–D10, tự nhận loại radar |

5. Mở Serial Monitor ở 115200 để xem trạng thái. Muốn xem dạng sóng thì đặt `XEM_DO_THI` bằng 1 trong `cau_hinh.h`, nạp lại rồi mở **Serial Plotter**: dùng để kiểm tra điện cực ECG, màng PVDF, cảm biến PPG và radar trước khi đo thật. Vòng tay in mỗi 30 giây 1 dòng `[PPG] …`: % thang đo của 2 kênh, dòng LED, chạm da, nhịp tim, SpO₂ kèm tỉ số R, và số tin MQTT đã gửi, mất (lúc chưa kết nối), bỏ (mạng nghẽn).

**Firmware gửi dữ liệu và tiết kiệm pin thế nào**
- Gửi MQTT không chặn, dùng chung cho cả 3 thiết bị (`ket_noi::guiSau` trong `ket_noi.h`, 3 thư mục giống hệt nhau). Gửi thẳng bằng esp-mqtt có thể chặn tới 10 giây khi mạng chậm, nên tin dữ liệu (sóng, chỉ số) vào hàng đợi 8 tin cho một tác vụ riêng gửi; đầy thì bỏ tin mới và cộng vào số `bỏ` (`tin_bo` trong trạng thái thiết bị). Số này tăng đều là sóng Wi-Fi chỗ đeo quá yếu. Tin sự kiện và trạng thái (té ngã, SOS, đã nhắn tin…) vào hộp thư chờ của esp-mqtt và được gửi lại tới khi broker xác nhận, kể cả sau khi mất mạng khoảng 30 giây. Nhờ vậy vòng tay không làm tràn FIFO của MAX30100 (chỉ chứa 16 mẫu, tức 160 ms), vòng ngực không trễ còi và đếm ngược té ngã khi Wi-Fi chậm.
- Vòng tay: vòng lặp chính ngủ giữa 2 lần đọc FIFO (20 ms), đỡ tốn pin và không giành CPU của Wi-Fi. Cảm biến 1 giây không ra mẫu (sụt áp, lỏng dây) thì firmware tự cài lại, Serial báo `cài lại cảm biến`.
- Vòng ngực: CPU chạy 80 MHz, vẫn đủ cho ECG 500 mẫu/giây, AI nhỏ trên vòng và Wi-Fi. MPU6050 không trả lời, hoặc dữ liệu đứng yên 1 giây (sụt áp lúc module 4G phát sóng làm cảm biến khởi động lại ở chế độ ngủ), thì firmware tự cài lại mỗi giây. Lúc bật mà chưa thấy MPU6050, vòng vẫn thử lại; cắm vào là chạy.
- Hộp radar: CPU chạy 80 MHz.

### 2. Chạy nền tảng trên máy tính

1. Cài Mosquitto (https://mosquitto.org/download) và Python 3.11 hoặc 3.12 (bản quá mới có thể chưa có TensorFlow). Lúc cài Python nhớ tick **Add python.exe to PATH**.
2. Bấm đúp `may_chu/0_cai_thu_vien.bat` (chạy 1 lần), hoặc gõ `pip install -r requirements.txt`.
3. Mô hình đang dùng có sẵn trong `may_chu/aiot_models`. Sau mỗi lần huấn luyện lại, giải nén `aiot_models.zip` tải từ Colab đè vào thư mục này.
4. Bấm đúp `1_chay_mosquitto.bat`, rồi `2_chay_nen_tang.bat`. Lệnh tương đương:
   ```
   "C:\Program Files\mosquitto\mosquitto.exe" -c mosquitto.conf -v
   python may_chu.py ai --ma bn01 --mo-hinh aiot_models
   ```
   Chưa có `aiot_models` thì `2_chay_nen_tang.bat` tự chạy `python may_chu.py web --ma bn01`: vẫn có dashboard và chỉ số, chưa có AI. Máy chỉ được chạy 1 Mosquitto: nếu bộ cài đã tạo dịch vụ Windows "mosquitto", dừng dịch vụ đó và đặt Manual (mục "Khi gặp lỗi").
5. Cửa sổ nền tảng in ra địa chỉ dạng `Dashboard: http://192.168.1.10:8080`. Mở địa chỉ đó trên điện thoại hoặc máy tính cùng mạng Wi-Fi. Lần đầu Windows hỏi tường lửa cho Mosquitto và Python thì chọn **cho phép trên mạng Private**.
6. Trên dashboard, mục **Cài đặt** → nhập số điện thoại người nhà. Vòng ngực lưu lại, mất điện vẫn nhớ. Không dùng dashboard thì gửi bằng lệnh:
   ```
   "C:\Program Files\mosquitto\mosquitto_pub.exe" -t aiot/bn01/cau_hinh -r -m "{\"sdt\":\"+84912345678\"}"
   ```
7. Đem ra mạng đông người (hội thi) thì đặt `PIN` trong `2_chay_nen_tang.bat` (hoặc thêm `--pin 1234`). Khi đó phải nhập PIN mới đổi được số điện thoại hay tắt báo động.

Kiểm tra dữ liệu từ thiết bị thật: `python may_chu.py xem --ma bn01`. Thấy khoảng `ECG 500/s · thở ngực 25/s · PPG 100/s · radar thở 5/s · radar tim 5/s` là đủ.

### 3. Thử khi chưa có phần cứng

Bấm đúp `chay_demo.bat`: file này mở Mosquitto, nền tảng, bộ giả lập (hỏi chọn kịch bản), rồi mở dashboard ở http://localhost:8080. Hoặc mở 3 cửa sổ:
```
"C:\Program Files\mosquitto\mosquitto.exe" -c mosquitto.conf -v
python may_chu.py ai --ma bn01 --mo-hinh aiot_models
python gia_lap_thiet_bi.py --ma bn01 --kich-ban rung_nhi
```
Có các kịch bản `binh_thuong`, `ngoai_tam_thu`, `rung_nhi`, `ngung_tho`, `te_nga`, `sos`. ECG lấy từ MIT-BIH: bản ghi 103 (nhịp xoang), 232 (ngoại tâm thu nhĩ) và 221 (rung nhĩ), đều thuộc tập kiểm tra DS2 nên mô hình chưa thấy khi huấn luyện. Lần đầu chạy cần Internet để tải khoảng 2 MB. PPG và radar được mô phỏng theo từng nhịp tim, giống cách notebook huấn luyện. Bộ giả lập cũng gửi chỉ số vòng tay (nhịp tim, SpO₂, nhiệt độ da, chỉ số tưới máu), kết quả AI trên vòng ngực và phản hồi sự kiện SIM khi nhận lệnh cảnh báo; cửa sổ bộ giả lập in ra lệnh cảnh báo mà vòng ngực sẽ nhận.

Kết quả mong đợi:
- `binh_thuong`: "Bình thường", không cảnh báo;
- `ngoai_tam_thu`: "Bất thường nhẹ", cảnh báo mức 1;
- `rung_nhi`: "Nguy cơ cao", cảnh báo sau khoảng 15 giây; sau khoảng 40 giây dashboard hiện thêm "Nhịp KHÔNG đều";
- `ngung_tho`: cảnh báo nhịp thở sau đợt ngưng thở đầu tiên (bắt đầu ở giây thứ 90); bộ giả lập cho SpO₂ tụt khoảng 6 % mỗi đợt nên ô SpO₂ hiện "vừa tụt …% so với nền" và cảnh báo vẫn ở mức 2;
- `te_nga`, `sos`: ở giây thứ 30, dashboard hiện cảnh báo té ngã (đếm ngược 15 giây rồi báo người nhà) hoặc SOS.

### Gửi cảnh báo qua Telegram

Làm 1 lần:
1. Trên Telegram, nhắn cho **@BotFather** lệnh `/newbot`, đặt tên cho bot, nhận **mã bot** (dạng `123456789:AA...`).
2. Mỗi người nhà mở bot vừa tạo, bấm **Start** (hoặc nhắn 1 câu bất kỳ).
3. Trong thư mục `may_chu`, chạy lệnh dưới (thay mã bot) để xem **mã chat** của từng người:
   ```
   python may_chu.py telegram --telegram-token 123456789:AA...
   ```
4. Mở `2_chay_nen_tang.bat` bằng Notepad, điền `TELEGRAM_TOKEN` (mã bot) và `TELEGRAM_CHAT` (các mã chat, cách nhau dấu phẩy), lưu lại.
5. Chạy `2_chay_nen_tang.bat`, mở dashboard → **Cài đặt** → **Thử gửi Telegram**.

Nền tảng gửi mọi cảnh báo mức 1 và 2 (tim, nhịp thở, SpO₂, té ngã, SOS, huyết áp rất cao, vòng ngực báo người nhà, SIM không gửi được tin) và tin "đã tắt báo động". Cùng một nội dung không gửi lặp trong 2 phút. Không gửi tin mất/kết nối lại thiết bị để tránh làm phiền. Đã điền mã bot thì đừng chia sẻ file `.bat` lên mạng.

## Nền tảng xử lý thế nào

- **Lọc nhiễu** (`aiot_infer.py`, `may_chu.py`): ECG chắn điện lưới 50/100 Hz, bỏ trôi đường nền bằng 2 bộ lọc trung vị 200 ms và 600 ms, rồi lọc 0,5–40 Hz; đỉnh R do 2 bộ tìm độc lập ghép lại, đoạn mà 2 bộ không khớp nhau bị coi là nhiễu (mục "Lọc nhiễu và thuật toán bản 3"). PPG bỏ gai nhọn (lọc Hampel) rồi lọc 0,5–8 Hz. Mọi bộ lọc chạy 2 chiều để không làm lệch thời điểm đỉnh sóng; hệ số bộ lọc được lưu lại, không tính lại mỗi lần. Kết quả lọc ECG, PPG dùng chung cho nhịp tim và thời gian mạch đến.
- **Chất lượng tín hiệu** (0–100 %, hiện ở góc mỗi đồ thị sóng): ECG chấm theo độ giống nhau giữa các nhịp, độ nhọn của sóng và tỉ lệ nhịp mà 2 bộ tìm đỉnh R cùng thấy; PPG theo độ giống nhau giữa các sóng mạch. ECG dưới 60 % (PPG dưới 50 %) thì đoạn đó bị coi là nhiễu: bỏ qua trước khi chạy AI và không đưa vào bộ theo dõi từng nhịp, vừa đỡ tốn CPU vừa tránh hỏng mẫu nhịp chuẩn của người đeo. Vòng tay tháo ra, chất lượng PPG quá kém (< 25 %) hoặc radar không thấy người thì máy chủ đưa kênh đó về mức nền để mô hình chạy như khi thiếu thiết bị, trường hợp đã được huấn luyện.
- **Chỉ số, 5 giây một lần**: nhịp tim lấy theo thứ tự ECG → PPG → vòng tay → radar (nguồn nào đang có và đủ chất lượng thì dùng; radar chỉ khi chất lượng `cl` từ 50 trở lên), đỉnh R tìm theo Pan–Tompkins; nhịp thở lấy từ sóng thở ngực (nội suy phổ parabol 3 điểm), chỉ dùng radar khi dây ngực không có sóng thở (không lấy số radar lúc đang đeo dây ngực, điện cực không tuột, mà sóng thở phẳng vì ngưng thở); SpO₂ và nhiệt độ da từ vòng tay; mức vận động từ MPU6050.
- **Biến thiên nhịp tim và nhịp không đều**: từ các đỉnh R trong khoảng 1 phút gần nhất (≥ 40 nhịp), máy chủ tính SDNN, RMSSD và COSEn của chuỗi khoảng RR (Lake, Moorman 2011). Nghi rung nhĩ khi COSEn > −1,3 (khoảng RR ngẫu nhiên, khó đoán) và RMSSD/RR trung bình > 0,1. Ngoại tâm thu lẻ tẻ được loại trước khi tính; bộ đếm đỉnh R tự đặt lại khi thiết bị khởi động lại hoặc lệch đồng hồ.
- **AI**:
  - cứ 5 giây, các mô hình tim (mục "Mô hình tim") chạy trên 12 giây ECG + PPG + radar gần nhất; xác suất được hiệu chỉnh và làm mượt theo độ đồng thuận giữa các mô hình (bất đồng cao thì cập nhật chậm lại để tránh cảnh báo nhầm);
  - cứ 10 giây, Isolation Forest chạy trên 30 giây sóng thở ngực gần nhất. 60 giây đầu dùng để hiệu chuẩn theo từng người (tự kiểm tra dây đeo áp sát và biên độ thở đủ lớn trước khi chốt mốc chuẩn); máy chủ bỏ qua lúc người đang vận động;
  - cũng cứ 10 giây, mô hình ngưng thở khi ngủ (`ngung_tho_spo2.json`) chấm 80 giây SpO₂ gần nhất của vòng tay so với mức nền của chính người đó và đếm số phút/giờ có tụt SpO₂ kiểu ngưng thở. Ô SpO₂ trên dashboard ghi "vừa tụt …% so với nền" hoặc "ổn định quanh mức nền", kèm số phút/giờ khi đã đo đủ 30 phút.
- **Kết hợp mô hình**: lấy trung bình 3 mô hình học sâu (`cnn_gru_fusion.keras`, `cnn_gru_fusion_2.keras`, `cnn_gru_fusion_3.keras`), rồi ghép với mô hình 2 tầng theo từng nhịp (`phan_loai_nhip.joblib`, trọng số 0,15). Mô hình 2 tầng theo dõi từng nhịp tim của người đeo, tự học mẫu nhịp riêng của người đó, phân loại mỗi nhịp là bình thường, ngoại tâm thu nhĩ hay ngoại tâm thu thất. Nếu thư mục mô hình có `dac_trung_hgb.joblib`, máy chủ ghép thêm mô hình cây trên đặc trưng nhịp.
- **Độ tin cậy của AI**: mỗi lần phân tích có "độ tin cậy" (100 % là chắc chắn, 0 % là phân vân đều các lớp, tính từ entropy của xác suất) và mức các mô hình bất đồng với nhau. Lần nào các mô hình bất đồng mạnh thì không được tính vào bộ đếm cảnh báo.
- **AI giải thích được** (thẻ "AI giải thích" dưới các đồ thị sóng): dashboard vẽ lại đoạn ECG 10 giây vừa phân tích, tô vàng đậm chỗ mô hình chú ý nhiều nhất (trọng số của lớp attention), kèm số ngoại tâm thu trong đoạn, độ tin cậy và mức bất đồng. Người xem thấy AI dựa vào đâu để kết luận, không phải "hộp đen".
- **Lịch sử và xuất CSV**: chỉ số và sự kiện lưu vào `du_lieu/lich_su_bn01.db` (SQLite, chế độ WAL), tự dọn chỉ số cũ hơn 30 ngày lúc khởi động. Dashboard xem lại xu hướng 30 phút, 6 giờ, 24 giờ, 7 ngày, thống kê từ 0 giờ hôm nay, và có nút **Tải CSV** để xuất ra file bảng tính.
- **Thời gian nhịp không đều trong ngày** (ô "Nhịp không đều" ở phần thống kê): tổng số phút luật rung nhĩ báo nhịp không đều và tỉ lệ so với thời gian đo được nhịp. Bác sĩ dùng "gánh nặng rung nhĩ" này để đánh giá nguy cơ đột quỵ.
- **Bảng kiểm F.A.S.T.** trên dashboard: người nhà kiểm tra 4 dấu hiệu đột quỵ (F: méo miệng, A: yếu hoặc tê tay chân, S: nói đớ, khó nói, T: triệu chứng cấp) và phát báo động khẩn cấp kèm ghi nhật ký ngay.
- **Thiếu thiết bị**: thiếu vòng tay hoặc radar thì vẫn chạy, nhưng máy chủ báo "thiếu ppg/radar" vì độ chính xác giảm. Điện cực ECG tuột thì bỏ qua lần đó.
- Máy chủ giữ trên broker chủ đề `aiot/bn01/ai/trang_thai`, nhờ vậy vòng ngực biết máy chủ còn chạy hay không để bật chế độ tự phát hiện ngưng thở.

### Cảnh báo

**Mức 2**: vòng ngực kêu còi 30 giây, nhắn tin và gọi; vòng tay rung từng hồi trong 20 giây; dashboard hiện biểu ngữ đỏ có nút **Đã kiểm tra · Tắt báo động** và nút **Gọi cấp cứu 115** (bấm được trên điện thoại). Phát khi:
- xác suất "Nguy cơ cao" (đã làm mượt) từ 0,6 trở lên 2 lần liên tiếp;
- nhịp không đều kéo dài kèm xác suất "Nguy cơ cao" từ 0,35;
- ≥ 20 giây không có nhịp thở nào, hoặc 2 lần liên tiếp thở bất thường (khi vòng tay đang đo được SpO₂: chỉ khi SpO₂ cũng tụt từ 3 % trở lên so với nền);
- thở bất thường kèm SpO₂ ≤ 92 % (có SpO₂ vòng tay thì phải tụt so với nền);
- SpO₂ dưới 85 % kéo dài;
- nhịp tim quá chậm (< 40 lần/phút) hoặc quá nhanh khi nghỉ (> 150 lần/phút) kéo dài, đo từ ECG hoặc PPG đạt chuẩn;
- người nhà ghi nhận dấu hiệu đột quỵ qua bảng kiểm F.A.S.T.

**Mức 1**: vòng ngực kêu 2 tiếng ngắn, vòng tay rung 2 nhịp, dashboard hiện biểu ngữ vàng. Phát khi:
- xác suất bất thường từ 0,6 trở lên 3 lần liên tiếp;
- nhịp không đều kéo dài;
- SpO₂ dưới 90 % kéo dài;
- thở bất thường hoặc ≥ 20 giây không thấy nhịp thở ở ngực mà SpO₂ vòng tay tụt dưới 3 % so với nền (thường do dây đeo ngực lỏng; lời báo nhắc kiểm tra dây đeo);
- nghi ngưng thở khi ngủ (≥ 10 phút/giờ có tụt SpO₂ kiểu ngưng thở, tối đa 1 lần/giờ).

Cùng một loại cảnh báo thì không báo lặp trong 5 phút (mức 2) hoặc 10 phút (mức 1). Lý do và số liệu của bước xác nhận bằng SpO₂: mục "Ngưng thở khi ngủ từ SpO2 vòng tay".

### Chế độ ban đêm

Dành cho người đã biết mình ngưng thở khi ngủ: các cơn tụt SpO₂ rồi hồi lại lặp đi lặp lại cả đêm là tình trạng đã biết; gọi người nhà 5 phút một lần không giúp được gì mà làm cả nhà mất ngủ và tắt hệ thống đi.

- **Bật**: dashboard → **Cài đặt** → tích **Chế độ ban đêm**, chọn giờ bắt đầu và kết thúc (mặc định 22:00–06:00, được phép qua nửa đêm) → **Lưu chế độ ban đêm**. Cài đặt lưu trong `du_lieu/lich_su_bn01.db` (bảng `cai_dat`), khởi động lại nền tảng vẫn nhớ. Dòng trạng thái cho biết đang trong giờ ban đêm hay không, đã gọi lúc nào và đã ghi lại mấy cảnh báo.
- **Trong giờ ban đêm**, khi vòng tay đang đo được SpO₂ (còn mạch): cảnh báo mức 2 đầu tiên của nhóm hô hấp trong đêm vẫn gửi như thường (còi, tin nhắn, cuộc gọi, Telegram); các cảnh báo sau của nhóm này (cả mức 1) chỉ ghi vào nhật ký với chữ "Ban đêm, chỉ ghi lại". Nhóm hô hấp gồm: nhịp thở bất thường và luật ≥ 20 giây (`tho`), thở bất thường kèm SpO₂ ≤ 92 % (`tho_thieu_oxy`), SpO₂ thấp kéo dài (`spo2`) và nghi ngưng thở khi ngủ (`ngung_tho_ngu`).
- **Vẫn luôn gọi**, dù đã gọi trước đó:
  - SpO₂ dưới 85 % liên tục từ 2 phút (`spo2_keo_dai`, tối đa 10 phút 1 lần);
  - không thấy nhịp thở khoảng 1 phút (luật ≥ 20 giây nổ 5 lần liền) kèm SpO₂ tụt từ 3 % (`tho_keo_dai`);
  - vòng tay không đo được SpO₂ hoặc mất mạch: không chặn gì, mọi cảnh báo như khi tắt chế độ ban đêm;
  - mọi cảnh báo về tim, té ngã, SOS và cảnh báo vòng ngực tự phát khi mất máy chủ.
- **Hết giờ ban đêm** (hoặc khi tắt chế độ), nền tảng ghi vào nhật ký và gửi Telegram bản tổng kết: số giờ đo được SpO₂, số phút mỗi giờ có tụt SpO₂ kiểu ngưng thở, SpO₂ thấp nhất, số phút dưới 90 %, lúc đã gọi, số cảnh báo chỉ ghi lại theo từng loại, số lần vẫn gọi; từ 10 phút/giờ thì thêm "Nên đi khám chuyên khoa giấc ngủ".

Kết quả mô phỏng cả đêm: mục "Ngưng thở khi ngủ từ SpO2 vòng tay".

### Huyết áp

Thiết bị không có vòng bít đo huyết áp, nên nền tảng làm 2 việc:

1. **Lưu số đo của máy đo huyết áp**: dashboard → **Cài đặt** → nhập tâm thu, tâm trương, mạch. Từ 180/120 trở lên dashboard hiện cảnh báo kèm hướng dẫn đo lại và dấu hiệu cần gọi 115; dưới 90/60 hiện cảnh báo huyết áp thấp.
2. **Ước tính xu hướng** từ thời gian mạch đến (PAT): khoảng thời gian từ đỉnh R trên ECG (ngực) tới sườn lên của sóng mạch ở cổ tay. Huyết áp tăng thì mạch căng, sóng đi nhanh hơn nên thời gian này ngắn lại. Nền tảng dùng mô hình `tâm thu = a − b·ln(thời gian mạch đến)` với hồi quy Ridge neo về `b = 90` để không lệch quá mức khi mới có ít lần đo. Mỗi lần nhập số đo **trong lúc đang đeo cả vòng ngực và vòng tay**, ước tính càng sát thực tế; từ 2 lần đo có thời gian mạch đến chênh nhau ≥ 10 ms thì bật hiệu chuẩn nhiều điểm. Số ước tính hiện trên dashboard có dấu "≈".

Điều kiện: cả 2 vòng đồng bộ giờ qua Internet (NTP), tín hiệu ECG và PPG đều tốt (chất lượng ≥ 60 %), không vận động mạnh, không rối loạn nhịp. Hiệu chuẩn chỉ dùng số đo trong 7 ngày gần nhất. Đây là số tham khảo xu hướng, không thay máy đo huyết áp và không dùng để cảnh báo.

## Thu dữ liệu và huấn luyện AI

### Thu dữ liệu của nhóm

```
python may_chu.py ghi --ma bn01 --nhan binh_thuong --lan lan1 --tinh-trang nghi_ngoi --phut 5
```
- `--tinh-trang` ghi điều kiện lúc đo vào tên file (nghỉ ngơi, vận động, căng thẳng; với radar: `ngu`, `tho_gap`, `nam_nghieng`, `ngoi_day`), ví dụ `bn01_lan1_nghi_ngoi.csv`.
- 60 giây đầu: ngồi yên, thở đều, để hiệu chuẩn nhịp thở.
- Nhấn **Enter** khi bắt đầu và khi kết thúc một đoạn thở bất thường (nín thở, thở gấp) để đánh dấu.
- Kết quả:
  - `du_lieu/my_data/<nhãn>/bn01_lan1.csv`: cột `t, ecg, ppg, mmw_breath, mmw_heart`, chỉ giữ đoạn cả 4 luồng cùng có dữ liệu;
  - `du_lieu/my_fsr/<binh_thuong|bat_thuong>/bn01_lan1.csv`: cột `t, fsr`, thêm `label` nếu có đánh dấu;
  - `du_lieu/wav/bn01_lan1_<luồng>.wav`: mỗi cảm biến 1 file WAV 16 bit đúng tần số lấy mẫu, mở bằng Audacity để xem dạng sóng.
- Máy chủ tự bù lệch đồng hồ của từng cảm biến: cảm biến thật chạy 98 thay vì 100 mẫu/giây thì thời gian vẫn khớp.
- Nén 2 thư mục `my_data`, `my_fsr`, tải lên `/content/` trên Colab, rồi đặt `DATA_MODE = "csv"`.
- Nhãn `bat_thuong_nhe` và `nguy_co_cao` chỉ dùng cho người đã được bác sĩ chẩn đoán và đồng ý tham gia. Học sinh khỏe mạnh chỉ cho dữ liệu `binh_thuong`; nín thở chỉ mô phỏng được nhịp thở bất thường.

### Huấn luyện trên Google Colab

1. Vào https://colab.research.google.com → **File → Upload notebook** → chọn `train_aiot_dot_quy.ipynb`.
2. **Runtime → Change runtime type → T4 GPU** → Save.
3. **Runtime → Run all**. Chế độ mặc định **"chính xác nhất"** mất khoảng 60–90 phút: tải ECG từ PhysioNet (MIT-BIH, INCART, AFDB, nhiễu NSTDB, tổng khoảng 1,5 GB), huấn luyện 3 mô hình 1D-CNN-GRU Fusion và mô hình 2 tầng theo từng nhịp, rồi Isolation Forest cho nhịp thở. Để tab mở trong lúc chạy. Muốn nhanh (khoảng 20 phút) thì đặt `CHE_DO = "nhanh"` ở ô Cấu hình.
4. Cuối notebook tự tải về `aiot_models.zip` gồm các mô hình, `aiot_infer.py` (máy chủ dùng file này để tiền xử lý đúng như lúc huấn luyện), `mo_hinh_tinyml.h` cho vòng ngực, bảng đánh giá khi thiếu thiết bị và các hình kết quả. Giải nén đè vào `may_chu/aiot_models/`.

Đổi cấu hình ở ô **1. Cấu hình**:
- `DATA_MODE = "synthetic"`: chạy nhanh bằng dữ liệu mô phỏng, không cần mạng.
- `DATA_MODE = "csv"`: dùng dữ liệu nhóm thu bằng `may_chu.py ghi`.
- `BINARY = True`: chỉ 2 lớp Bình thường / Bất thường.
- Muốn so với mô hình gốc: đặt các cờ nâng cấp ở bảng của mục "Mô hình tim" bằng `False` và `MODALITY_DROPOUT = 0`, chạy lại rồi so độ nhạy, độ đặc hiệu, AUC.

### Tinh chỉnh bằng dữ liệu thật của nhóm (học chuyển giao)

Khi nhóm đã thu dữ liệu đồng thời từ 3 thiết bị bằng `python may_chu.py ghi` (mục "Thu dữ liệu của nhóm"):
1. Tải lên Colab `aiot_models.zip` đang dùng, giải nén thành `/content/aiot_models_cu/`; tải và giải nén `my_data.zip`, `my_fsr.zip`.
2. Ở ô **1. Cấu hình** đặt `DATA_MODE = "csv"` và `FINE_TUNE_FROM = "/content/aiot_models_cu/cnn_gru_fusion.keras"`, rồi **Run all**.
3. Mô hình học tiếp từ kiến thức đã có thay vì học lại từ đầu: giữ nguyên các khối CNN (`FREEZE_CNN`), học với tốc độ nhỏ hơn 10 lần, trộn thêm MIT-BIH DS1 (`TRON_MITBIH`) để không "quên". Tập kiểm tra chỉ gồm người của nhóm; notebook in bảng trước / sau tinh chỉnh (`tinh_chinh.csv`). Đây là cách để có mô hình chính xác với cảm biến thật khi dữ liệu của nhóm còn ít.

## Mô hình AI và kết quả

Mọi kết quả dưới đây đo trên người chưa có trong dữ liệu huấn luyện (chia theo người, không chia ngẫu nhiên từng đoạn), nên phản ánh đúng khả năng dùng cho người mới.

### Mô hình tim

Mô hình đang dùng (`may_chu/aiot_models/`, chế độ "chính xác nhất"):
- 3 lớp: Bình thường, Bất thường nhẹ, Nguy cơ cao. Đầu vào: cửa sổ 10 giây ECG (250 Hz), PPG (50 Hz) và sóng radar (10 Hz).
- Học trên MIT-BIH DS1, INCART, AFDB, SVDB, CUDB và PTB-XL, trộn nhiễu điện tim thật NSTDB; lọc nhiễu bản 3.
- 3 mô hình học sâu 1D-CNN-GRU Fusion khác hạt giống, lấy trung bình, ghép với mô hình 2 tầng theo từng nhịp (trọng số 0,15); hiệu chỉnh xác suất T = 0,55. Trọng số ghép và T đều chọn trên tập kiểm định, không nhìn tập kiểm tra.

| Thành phần | Cờ trong ô Cấu hình | Tác dụng |
|---|---|---|
| Thêm INCART (32 người, nhiều ngoại tâm thu), AFDB (23 người rung nhĩ), SVDB (Holter, nhiều ngoại tâm thu nhĩ), CUDB (nhịp nhanh thất, rung thất) | `CHE_DO = "chinh_xac_nhat"` | MIT-BIH DS1 chỉ có 22 người, trong đó 2 người rung nhĩ: mô hình gặp thêm nhiều kiểu nhịp của người khác |
| Thêm PTB-XL (ECG có chẩn đoán của bác sĩ) | `USE_PTBXL` | thêm nhồi máu cơ tim, rối loạn dẫn truyền, rung nhĩ... |
| Khối CNN có kết nối tắt (residual) | `USE_RESIDUAL` | học sâu hơn mà không mất tín hiệu gốc |
| Khối Squeeze-and-Excitation | `USE_SE` | mô hình tự cân trọng số giữa các kênh đặc trưng |
| GRU 2 chiều + cơ chế chú ý (attention) | `USE_ATTENTION` | mô hình tự chú ý vào đúng đoạn có nhịp bất thường; dashboard vẽ được chỗ AI chú ý |
| Nhánh mel-spectrogram + 2D-CNN | `USE_MEL` | chuyển tín hiệu sang miền tần số, CNN trích đặc trưng như ảnh |
| Nhánh đặc trưng HRV và 6 đặc trưng hình thái nhịp (RR đến sớm, nghỉ bù, độ rộng QRS, nhịp khác hình dạng) | `USE_HRV`, `HRV_SET = "v3"` | đưa thông tin "nhịp đều hay loạn" và hình dạng nhịp rõ ràng, nhắm vào lớp "Bất thường nhẹ" |
| Hàm mất mát Focal (γ = 1,5) | `FOCAL_GAMMA` | tập trung học các mẫu khó |
| Tăng cường dữ liệu: nhiễu cơ, trôi đường nền, cử động cổ tay; nhiễu điện tim thật NSTDB | `AUGMENT`, `NOISE_AUG` | chịu được nhiễu khi đo ngoài bệnh viện |
| Huấn luyện cho trường hợp thiếu thiết bị | `MODALITY_DROPOUT` (0,15) | quên đeo vòng tay hay radar mất kết nối vẫn đoán được |
| Mô hình 2 tầng: phân loại từng nhịp (N/S/V) bằng khoảng RR và hình dạng so với mẫu nhịp riêng của người đó, rồi gộp thành kết luận cho cửa sổ | tự động | cách của các nghiên cứu phân loại nhịp cho người mới (de Chazal 2004); bắt ngoại tâm thu thất tốt |
| 3 mô hình học sâu khác hạt giống, lấy trung bình | tự động | giảm sai lệch ngẫu nhiên của 1 lần huấn luyện |
| Hiệu chỉnh xác suất (temperature scaling) | tự động | xác suất khớp thực tế hơn; máy chủ dùng để làm mượt và đặt ngưỡng |
| Trọng số ghép với mô hình 2 tầng và mô hình cây | `ENSEMBLE_WEIGHT = "auto"` | chọn trên tập kiểm định; nếu không giúp thì tự tắt |

**Kết quả trên tập kiểm tra** (21 người MIT-BIH DS2 và những người được tách riêng của INCART, AFDB, SVDB, CUDB):

| Nguồn dữ liệu kiểm tra | Số cửa sổ | Độ chính xác 3 lớp | Độ nhạy | Độ đặc hiệu | AUC |
|---|---|---|---|---|---|
| MIT-BIH DS2 | 7 560 | 0,814 | 0,828 | 0,973 | 0,954 |
| INCART | 8 257 | 0,931 | 0,989 | 0,974 | 0,998 |
| AFDB (rung nhĩ) | 976 | 0,853 | 0,983 | 0,835 | 0,955 |
| SVDB (Holter 128 Hz) | 5 166 | 0,838 | 0,918 | 0,911 | 0,967 |
| CUDB (nhịp nhanh thất, rung thất) | 339 | 0,673 | 0,989 | 0,551 | 0,902 |
| **Cả 5 nguồn** | **22 298** | **0,862** | **0,913** | **0,940** | **0,968** |

Độ nhạy từng mức trên cả 5 nguồn: bình thường 0,940; bất thường nhẹ 0,793 (10,6 % bị coi là bình thường); nguy cơ cao 0,820 (5,4 % bị coi là bình thường).

| Độ bền (cả 5 nguồn) | Kết quả |
|---|---|
| Thiếu vòng tay / thiếu radar / chỉ có ECG (AUC) | 0,957 / 0,966 / 0,957 |
| ECG nhiễu thật SNR 12 / 6 / 0 dB (độ chính xác) | 0,858 / 0,840 / 0,807 |
| Thời gian 1 cửa sổ trên Colab (3 mô hình) | 598 ms |

- CUDB nhiều đoạn nhiễu và rung thất nên độ đặc hiệu thấp nhất (0,551).
- Mô hình vẫn học thuộc một phần: độ chính xác huấn luyện khoảng 0,94, kiểm định khoảng 0,80; sai số kiểm định thấp nhất ở epoch thứ 5.
- Chạy qua đúng đường xử lý của máy chủ trên máy tính (Keras 3.12, backend torch, CPU), ECG thật của người chưa thấy: MIT-BIH 103 (nhịp xoang) ra "Bình thường 0,94", không cảnh báo; 221 (rung nhĩ) ra "Nguy cơ cao 0,75" kèm "nhịp không đều", cảnh báo mức 2; 233 (nhiều ngoại tâm thu thất) ra "Bất thường nhẹ 0,71" (đếm 3 nhịp thất), cảnh báo mức 1; 232 (ngoại tâm thu nhĩ) ra "Nguy cơ cao 0,52". Mỗi lần phân tích mất 2,5–3,2 giây (tối đa khoảng 4 giây); AI chạy 5 giây một lần nên vẫn kịp.

### Lọc nhiễu và thuật toán bản 3

Máy chủ dùng `aiot_infer.py` bản 3 để tính nhịp tim, chất lượng tín hiệu và luật rung nhĩ; mô hình đang dùng được huấn luyện với bản 3 (`"preprocess_version": 3` trong `model_config.json`). Mô hình huấn luyện với lọc nhiễu bản 2 (`"preprocess_version": 2`) vẫn được lọc đúng như bản 2.

| Thay đổi so với bản 2 | Cách làm | Tài liệu |
|---|---|---|
| Bỏ trôi đường nền ECG | 2 bộ lọc trung vị nối tiếp: 200 ms xoá QRS và sóng P, 600 ms xoá sóng T; phần còn lại là đường nền, trừ đi trước khi lọc 0,5–40 Hz. Theo kịp cả bước nhảy đột ngột của đường nền mà không làm méo đoạn ST | de Chazal và cộng sự 2004 |
| Bộ tìm đỉnh R thứ hai | lọc 8–20 Hz, bình phương, so trung bình trượt 97 ms (cỡ 1 QRS) với 611 ms (cỡ 1 nhịp) | M. Elgendi, PLoS ONE, vol. 8, no. 9, e73557, 2013, doi: 10.1371/journal.pone.0073557 |
| Ghép 2 bộ tìm | 2 bộ khớp ≥ 90 % nhịp (ECG sạch): dùng Elgendi; có nhiễu: giữ nhịp cả 2 cùng thấy, thêm nhịp chỉ 1 bộ thấy nếu cách nhịp đã có ≥ 0,75 lần RR trung vị | |
| Chất lượng tín hiệu ECG | nhân thêm hệ số bSQI (tỉ lệ nhịp 2 bộ cùng thấy: ≥ 0,9 giữ nguyên, ≤ 0,5 về 0); nhịp lạ nhưng giống nhau (ngoại tâm thu cùng ổ) được so với mẫu nhịp thứ 2 nên không bị coi là nhiễu | Li, Mark, Clifford 2008 |
| Luật nhịp không đều (rung nhĩ) | COSEn của chuỗi RR > −1,3 và RMSSD/RR trung bình > 0,1, thay luật Dash | D. E. Lake, J. R. Moorman, Am. J. Physiol. Heart Circ. Physiol., vol. 300, no. 1, pp. H319–H325, 2011, doi: 10.1152/ajpheart.00561.2010 |

Kết quả đo trên máy tính. Tìm đỉnh R: 44 bản ghi MIT-BIH × 5 phút ở 500 Hz như vòng ngực, sạch và trộn nhiễu thật NSTDB; đoạn "tìm sai" là đoạn 10 giây sai từ 2 nhịp trở lên. Notebook (ô "Đánh giá lọc nhiễu và thuật toán") cho số gần như trùng, lưu trong `may_chu/aiot_models/danh_gia_loc_nhieu.csv` và `danh_gia_rung_nhi.csv`.

| Điều kiện | Tìm đỉnh R bản 2 (Se / +P) | Tìm đỉnh R bản 3 (Se / +P) | Đoạn tìm sai vẫn lọt qua cổng chất lượng: bản 2 → bản 3 | Đoạn được nhận: bản 2 → bản 3 |
|---|---|---|---|---|
| ECG sạch | 98,83 / 98,15 % | 99,39 / 99,16 % | 3,2 → 0,4 % | 99,2 → 97,6 % |
| Nhiễu điện cực 6 dB | 98,89 / 97,24 % | 99,22 / 98,10 % | 3,6 → 1,7 % | 97,5 → 94,5 % |
| Nhiễu điện cực 0 dB | 98,19 / 89,44 % | 98,56 / 92,99 % | 16,2 → 7,6 % | 85,8 → 74,0 % |
| Nhiễu cơ 6 dB | 98,88 / 98,00 % | 99,19 / 98,49 % | 3,3 → 0,5 % | 98,9 → 95,8 % |
| Nhiễu cơ 0 dB | 98,76 / 95,79 % | 98,76 / 96,11 % | 8,3 → 5,9 % | 96,4 → 89,2 % |
| Trôi đường nền 6 dB | 98,79 / 98,12 % | 99,34 / 99,12 % | 3,2 → 0,4 % | 99,2 → 97,5 % |
| Trôi đường nền 0 dB | 98,90 / 98,08 % | 99,37 / 99,09 % | 2,9 → 0,6 % | 99,1 → 97,4 % |

| Luật rung nhĩ (đoạn 90 giây, mốc nhịp chuẩn) | Bản 2: luật Dash (độ nhạy / độ đặc hiệu) | Bản 3: COSEn |
|---|---|---|
| AFDB (23 người, 9 138 đoạn) | 88,5 / 93,6 % | 93,9 / 98,8 % |
| MIT-BIH (44 bản ghi, nhiều ngoại tâm thu) | 96,7 / 86,3 % | 98,9 / 88,7 % |
| Tất cả (10 017 đoạn, 3 819 đoạn rung nhĩ) | 88,7 / 92,7 % | 94,0 / 97,5 % |

- Bản 3 tìm đỉnh R tốt hơn ở cả 7 điều kiện. Cổng chất lượng chặt hơn: số đoạn tìm sai mà vẫn lọt qua giảm 1,4–8 lần, đổi lại bỏ thêm khoảng 1,6 % đoạn ECG sạch (một số bản ghi MIT-BIH vốn có nhiễu sẵn).
- Thời gian lọc, tìm đỉnh R và chấm chất lượng cho 1 đoạn 12 giây ở 500 Hz: 6,7 ms (bản 2), 9,4 ms (bản 3).
- Mức bSQI (0,5–0,9) và ngưỡng COSEn (−1,3) được chọn trên chính dữ liệu này nên số có thể hơi lạc quan; với mọi ngưỡng COSEn từ −1,3 đến −1,2, luật COSEn đều tốt hơn luật Dash ở cả 2 bộ dữ liệu.

### Nhịp thở: ngưỡng riêng từng người

Isolation Forest chấm 30 giây sóng thở ngực gần nhất bằng 3 đặc trưng cơ bản và 4 đặc trưng phổ, cộng thêm luật "≥ 20 giây không thở". Sóng thở thật kém đều hơn sóng mô phỏng nhiều, nên không có một ngưỡng chung nào vừa bắt được thở bất thường vừa không báo nhầm sóng thở thật. Vì vậy mô hình dùng ngưỡng riêng từng người:
- Khi mới đeo, máy chủ tính điểm bất thường của các đoạn trong 60 giây hiệu chuẩn (người đó đang thở bình thường), lấy điểm lớn nhất làm **điểm nền**. Ngưỡng của người đó = max(ngưỡng chung, điểm nền + biên độ). Người vốn thở kém đều thì ngưỡng tự nâng lên.
- Ngưỡng chung và biên độ chọn bằng kiểm định chéo 4 phần theo người trên tập học (không nhìn tập kiểm tra): lớn nhất độ nhạy + độ đặc hiệu trên tín hiệu mô phỏng, với điều kiện độ đặc hiệu trên sóng thở thật ≥ `BR_DAC_HIEU_THAT_MIN`. Không cặp ngưỡng nào đạt điều kiện thì notebook lấy cặp báo nhầm ít nhất trên sóng thở thật và in cảnh báo.
- File mô hình không có khoá `margin` thì máy chủ dùng ngưỡng chung.

Kết quả của `breath_iforest.joblib` đang dùng (ngưỡng chung 0,335, biên độ 0,130; học trên sóng thở mô phỏng và sóng thở thật Apnea-ECG), trên 14 người chưa thấy (551 đoạn mô phỏng, 248 đoạn sóng thở thật của 2 người Apnea-ECG), kèm luật ≥ 20 giây không thở:

| Cách đặt ngưỡng | Mô phỏng: độ nhạy / độ đặc hiệu | Sóng thở thật: độ nhạy / độ đặc hiệu |
|---|---|---|
| Ngưỡng chung thấp (0,564) | 0,782 / 0,980 | 1,000 / 0,031 |
| Ngưỡng chung cao (0,594) | 0,061 / 1,000 | 0,159 / 0,925 |
| **Ngưỡng riêng (đang dùng)** | **0,619 / 1,000** | **0,068 / 0,988** |
| Ngưỡng riêng, không ràng buộc sóng thở thật (`BR_DAC_HIEU_THAT_MIN = 0`) | 0,837 / 0,975 | 0,432 / 0,788 |

Ngưỡng riêng giảm báo nhầm trên sóng thở thật từ 97 % đoạn xuống 1,2 %, nhưng chỉ bắt được 6,8 % đoạn bất thường của 2 người Apnea-ECG trong tập kiểm tra. Ngưng thở trong Apnea-ECG phần lớn do **tắc nghẽn**: đường thở bị chặn nhưng lồng ngực vẫn cố thở, nên tín hiệu ở ngực thay đổi ít hơn ngưng thở trung ương. Ngưng thở tắc nghẽn được phát hiện bằng SpO₂ vòng tay (mục dưới).

Notebook hiện nạp thêm 53 bản ghi BIDMC: sóng thở thật của bệnh nhân ICU (đều bình thường) cho phần nhịp thở, và dao động PPG thật cộng vào 25 % mẫu học của mô hình tim. Mặc định `BR_DAC_HIEU_THAT_MIN` là 0,80. File `breath_iforest.joblib` đang cài được tạo trước khi có BIDMC; lần chạy Colab tới sẽ cho file mới. Mức 0,80 được chọn sau khi xem kết quả trên tập kiểm tra, nên số kiểm tra của lần chạy đó sẽ hơi lạc quan; độ đặc hiệu khi kiểm định chéo do notebook in ra trung thực hơn.

### Ngưng thở khi ngủ từ SpO2 vòng tay

**Vì sao.** Ngưng thở khi ngủ làm tăng nguy cơ đột quỵ (Yaggi và cộng sự, N. Engl. J. Med. 353: 2034–2041, 2005). Isolation Forest trên sóng thở ngực, thử trên cả đêm của 8 người Apnea-ECG có SpO₂ (3 911 phút, 1 609 phút có ngưng hoặc giảm thở theo chuyên gia), chỉ bắt được 50,9 % số phút ngưng thở và báo nhầm 25,6 % số phút bình thường. Kết quả rất khác nhau giữa các người: người khỏe c01 bị báo gần như mọi phút (59,5 phút/giờ), còn người ngưng thở nặng a01 gần như không được báo (0,1 phút/giờ, thực tế 58,3).

**Cách làm** (`aiot_infer.py`: `spo2_lam_sach`, `spo2_dac_trung`, `NgungThoSpO2`; notebook Phần B2; máy chủ `chay_spo2_ngu`):
- Vòng tay gửi SpO₂ 2 giây/lần. Máy chủ bỏ giá trị ngoài 50–100 % hoặc đổi nhanh hơn 4 %/giây (cử động, cảm biến tuột).
- Cứ 10 giây, lấy 80 giây gần nhất so với **mức nền của chính người đó** (phân vị 90 của 210 giây gần nhất), tính 3 đặc trưng: mức tụt (nền − thấp nhất), biên độ dao động (cao nhất − thấp nhất), độ lệch trung bình (nền − trung bình). Không dùng giá trị SpO₂ tuyệt đối vì SpO₂ cổ tay chưa hiệu chuẩn. Tụt SpO₂ đến chậm hơn cơn ngưng thở vài chục giây, nên đoạn 80 giây này ứng với nhịp thở của khoảng 30–90 giây trước.
- **Hồi quy logistic** trên 3 đặc trưng: 3 hệ số và 1 hệ số tự do trong `ngung_tho_spo2.json` (mở bằng Notepad xem được), học trên 8 đêm Apnea-ECG với nhãn từng phút của chuyên gia.

**Kết quả** theo từng phút, kiểm định chéo bỏ-ra-1-người (học trên 7 người, chấm người thứ 8, lặp đủ 8 lần; số trong `may_chu/aiot_models/danh_gia_ngung_tho_spo2.csv`):

| Cách phát hiện | Số phút | Độ nhạy | Độ đặc hiệu | AUC |
|---|---|---|---|---|
| Isolation Forest (sóng thở ngực, ngưỡng riêng) | 3 911 | 0,509 | 0,744 | – |
| Isolation Forest bất thường và SpO₂ ≤ 92 % | 3 911 | 0,479 | 0,960 | – |
| Tụt SpO₂ ≥ 3 % so với nền | 3 911 | 0,968 | 0,779 | – |
| Hồi quy logistic, chỉ sóng thở ngực | 3 911 | 0,833 | 0,518 | 0,649 |
| **Hồi quy logistic, chỉ SpO₂ (đang dùng)** | 3 845 | **0,961** | **0,964** | **0,988** |
| Hồi quy logistic, sóng thở ngực + SpO₂ | 3 845 | 0,971 | 0,769 | 0,849 |

| Bản ghi | Phút ngưng/giảm thở mỗi giờ: chuyên gia | Mô hình SpO₂ | Isolation Forest |
|---|---|---|---|
| a01 | 58,3 | 58,3 | 0,1 |
| a02 | 48,2 | 51,1 | 14,7 |
| a03 | 28,7 | 31,1 | 32,6 |
| a04 | 55,4 | 53,7 | 58,3 |
| b01 | 2,4 | 0,8 | 4,2 |
| c01 | 0,0 | 0,0 | 59,5 |
| c02 | 0,1 | 0,4 | 2,3 |
| c03 | 0,0 | 0,0 | 0,0 |

Ghép thêm đặc trưng sóng thở ngực làm độ đặc hiệu giảm còn 0,769 (người b01 bị báo gần như mọi phút): sóng thở ngực mỗi người một kiểu, 7 người không đủ để học quy luật chung. Vì vậy mô hình chỉ dùng SpO₂; sóng thở ngực vẫn lo luật "≥ 20 giây không thở" (ngưng thở trung ương, ngừng thở) và Isolation Forest (thở gấp, thở không đều).

**Độ bền** khi SpO₂ của người được chấm bị làm xấu giống đo ở cổ tay (mô hình vẫn học trên dữ liệu gốc):

| SpO₂ của người được chấm | Độ nhạy | Độ đặc hiệu |
|---|---|---|
| Như bản ghi | 0,961 | 0,964 |
| Cộng nhiễu ngẫu nhiên độ lệch chuẩn 1 % (làm tròn) | 0,973 | 0,947 |
| Cộng nhiễu ngẫu nhiên độ lệch chuẩn 2 % | 0,984 | 0,879 |
| Chậm thêm (trung bình trượt mũ hệ số 0,4) | 0,943 | 0,977 |
| Mất ngẫu nhiên 20 % số giá trị | 0,960 | 0,966 |
| Cả 3: nhiễu 2 %, chậm thêm, mất 20 % | 0,975 | 0,910 |

**Máy chủ dùng thế nào**
- Đếm số phút/giờ mô hình báo "kiểu ngưng thở" (cần ≥ 30 phút đo); từ `NGUONG_NGUNG_THO_GIO = 10` phút/giờ thì báo mức 1 "nghi ngưng thở khi ngủ, nên đi khám", tối đa 1 lần/giờ. Mức 10 phút/giờ do nhóm tự đặt, chưa có chuẩn y khoa cho chỉ số tính theo phút này.
- Xác nhận cảnh báo nhịp thở bằng SpO₂, cho cả Isolation Forest báo 2 lần liên tiếp và luật "≥ 20 giây không thở": nếu SpO₂ tụt dưới `SPO2_TUT_XAC_NHAN = 3` % so với nền thì chỉ báo mức 1 (không gọi); với luật 20 giây, lời báo nhắc kiểm tra dây đeo ngực. Ngưng thở thật kéo dài thì SpO₂ tụt và lần xét sau (10 giây) lên mức 2. Luật "thở bất thường kèm SpO₂ ≤ 92 %" cũng cần SpO₂ tụt từ 3 % so với nền. Không có vòng tay, vòng tay mất mạch, hoặc không có `ngung_tho_spo2.json` thì cảnh báo nhịp thở giữ mức 2.
- Bước xác nhận dùng mức tụt 3 % (mức AASM dùng để chấm giảm thở, Berry và cộng sự, J. Clin. Sleep Med. 8: 597–619, 2012) mà không dùng xác suất của mô hình: mô hình học từ các cơn tụt sâu 10–20 % của Apnea-ECG nên chỉ báo khi tụt khoảng 7–8 % trở lên (ví dụ SpO₂ dao động tụt 6 % cho xác suất 0,145). Dùng xác suất để hạ cảnh báo sẽ hạ nhầm cơn giảm thở nhẹ. Trên 8 đêm, 96,8 % số phút có ngưng thở đều tụt từ 3 % trở lên.
- Chỉ dùng SpO₂ khi vòng tay đang bắt được mạch. Khi mất mạch (`nhip_tim` = -1, `cl` = 0), firmware vẫn gửi giá trị SpO₂ cũ; máy chủ coi giá trị đó là không đo được, nên SpO₂ cũ không làm hạ cảnh báo.
- Đánh đổi: ngưng thở trung ương ngắn mà SpO₂ chưa kịp tụt 3 % chỉ được báo mức 1 trong vài chục giây đầu; ngưng thở kéo dài thì SpO₂ tụt và cảnh báo lên mức 2. Vòng tay mất mạch hoặc không đeo thì luật ≥ 20 giây vẫn báo mức 2 ngay.

**Mô phỏng cả đêm.** Sóng thở ngực Apnea-ECG (đổi sang đơn vị ADC như cảm biến ngực) và SpO₂ (2 giây/lần) chạy qua đúng luật cảnh báo của máy chủ: nhịp thở 10 giây/lần, SpO₂ 5 giây/lần, các cảnh báo nhịp thở dùng chung 5 phút chờ như máy chủ. Mô hình SpO₂ của mỗi người là mô hình học trên 7 người còn lại. Chế độ ban đêm bật suốt đêm.

| Bản ghi (giờ đo) | Cuộc gọi mức 2 mỗi đêm: không xác nhận SpO₂ → xác nhận 3 % → thêm chế độ ban đêm | Chế độ ban đêm: chỉ ghi lại / vẫn gọi vì nguy hiểm | Báo "nghi ngưng thở khi ngủ" |
|---|---|---|---|
| a01 (8,2) | 78 → 78 → **1** | 131 / 0 | 8 lần (lần đầu sau 31 phút) |
| a02 (8,8) | 169 → 169 → **7** | 222 / 6 | 9 lần (sau 31 phút) |
| a03 (8,7) | 161 → 160 → **1** | 210 / 0 | 9 lần (sau 31 phút) |
| a04 (8,3) | 259 → 259 → **30** | 285 / 6 | 8 lần (sau 53 phút) |
| b01 (8,1) | 19 → 13 → 3 | 20 / 0 | không |
| c01 (8,0) | 99 → 25 → 23 | 55 / 18 | không |
| c02 (8,3) | 12 → 8 → 2 | 12 / 1 | không |
| c03 (7,5) | 0 → 0 → 0 | 0 / 0 | không |

- Báo "nghi ngưng thở khi ngủ" đúng cả 4 người ngưng thở nặng, không báo nhầm người khỏe nào, và không báo b01 (ngưng thở nhẹ, 2,4 phút/giờ).
- Người khỏe c01 có sóng thở ngực gần như phẳng cả đêm (giống dây đeo lỏng). Bước xác nhận 3 % bớt 3/4 số cuộc gọi nhầm (99 → 25); phần bị hạ thành 47 lần báo mức 1 nhắc kiểm tra dây đeo, 21 cuộc còn lại do luật ≥ 20 giây rơi vào lúc SpO₂ của c01 thật sự tụt từ 3 %. Thêm chế độ ban đêm còn 23 cuộc, trong đó 18 lần "khoảng 1 phút không thấy nhịp thở kèm SpO₂ tụt" vì SpO₂ có lúc dao động từ 3 %. Ngưỡng này được giữ để an toàn; gặp trường hợp này thì đeo lại dây ngực.
- 4 người ngưng thở nặng giữ nguyên số cuộc gọi do nhịp thở khi thêm bước xác nhận: lúc luật ≥ 20 giây nổ trong cơn ngưng thở thật, SpO₂ đã tụt từ 3 % ở 97,5–100 % số lần (a03: 348/349 lần luật nổ rơi vào phút ngưng thở; a04: 41/41). Phần lớn cuộc gọi của họ do SpO₂ < 85 % kéo dài và "thở bất thường kèm SpO₂ ≤ 92 %" (mỗi loại 5 phút 1 lần); chế độ ban đêm đưa a01, a03 về 1 cuộc mỗi đêm. a02 thêm 6 cuộc vì SpO₂ dưới 85 % liên tục từ 2 phút: thiếu oxy kéo dài thật, vẫn cần gọi.
- a04 còn 30 cuộc: 6 cuộc vì SpO₂ thấp kéo dài, phần lớn số còn lại rơi vào lúc không đo được SpO₂ (bản ghi này mất SpO₂ ở khoảng 23 % số phút), khi đó chế độ ban đêm cố ý không chặn. Vòng tay đeo sát, ít mất tín hiệu thì sẽ ít cuộc gọi hơn.
- Ngưỡng "vẫn luôn gọi": các cơn tụt rồi hồi lại của người ngưng thở nặng không chạm mức "SpO₂ dưới 85 % liên tục từ 2 phút" (a01 dài nhất 64 giây, a03 36 giây), còn a02 có 11 đoạn và a04 có 3 đoạn từ 2 phút (a02 dài nhất 8,4 phút). Luật ≥ 20 giây nổ liền nhau tối đa 3 lần ở người ngưng thở, dưới mức 5 lần liền của ngưỡng "khoảng 1 phút không thấy nhịp thở".

**Hạn chế**: chỉ 8 người, SpO₂ đo bằng máy kẹp ngón, phần lớn là ngưng thở nặng; mô hình đánh giá thấp người ngưng thở nhẹ (b01: 2,4 → 0,8 phút/giờ). Cần thử lại với SpO₂ cổ tay của nhóm, tốt nhất là đo cùng lúc với máy kẹp ngón.

### AI trên vòng ngực (TinyML)

Vòng ngực tự tìm đỉnh R theo Pan–Tompkins từng mẫu, tính 11 đặc trưng nhịp của 10 giây gần nhất và chạy một mạng nơ-ron nhỏ (11 → 16 → 16 → 3, 515 tham số, khoảng 2 KB) 5 giây/lần. Kết quả gửi lên `nguc/ai` và hiện ở dòng "Vòng đeo ngực" trên dashboard. Khi mất máy chủ mà mạng báo "Nguy cơ cao" 6 lần liền (khoảng 30 giây), vòng ngực tự kêu còi và nhắn tin cho người nhà (không gọi).

Kết quả của `mo_hinh_tinyml.h` đang nạp, trên 21 người chưa thấy (MIT-BIH DS2, 7 560 cửa sổ), dùng đúng bộ tìm đỉnh R chạy từng mẫu như firmware:

| AI trên vòng ngực | Độ chính xác 3 lớp | Độ nhạy | Độ đặc hiệu | Nhạy với nguy cơ cao | AUC |
|---|---|---|---|---|---|
| `mo_hinh_tinyml.h` đang nạp | 0,711 | 0,720 | 0,903 | 0,741 | 0,858 |

Bộ tìm đỉnh R từng mẫu trên 10 bản ghi MIT-BIH khó: tìm đúng 99,6 % nhịp, 97,7 % đỉnh tìm được là nhịp thật. Mô hình nhỏ này kém mô hình trên máy chủ vì chỉ dùng khoảng cách giữa các nhịp, nên chỉ là lớp dự phòng khi mất máy chủ.

Sau mỗi lần huấn luyện, notebook ghi `mo_hinh_tinyml.h` mới vào `aiot_models.zip`. Chỉ chép file này đè vào `firmware/vong_deo_nguc/` (rồi nạp lại vòng ngực) khi nó tốt hơn bản đang nạp, so bằng bộ tìm đỉnh R chạy từng mẫu như firmware: bắt nguy cơ cao tốt hơn mà không báo nhầm nhiều hơn. Số ghi ở đầu file (ví dụ độ đặc hiệu) tính bằng đỉnh R tìm trên cả đoạn nên cao hơn khi chạy thật trên vòng.

## Chủ đề MQTT

| Chủ đề | Ai gửi | Nội dung |
|---|---|---|
| `aiot/bn01/tay/ppg` | vòng tay | 2 gói/giây `{"n","t","fs":100,"da","v":[50 số]}`; `v` đã đảo dấu nên đỉnh mạch hướng lên |
| `aiot/bn01/tay/chi_so` | vòng tay | 2 giây/lần `{"t","nhip_tim","spo2","nhiet_do","da","cho","cl","pi"}`; -1 là chưa có; `cl` chất lượng tín hiệu (100 tốt, 50 vừa cử động, 0 chưa bắt được nhịp), `pi` chỉ số tưới máu (%) |
| `aiot/bn01/nguc/tin_hieu` | vòng ngực | 5 gói/giây `{"n","t","lo","ecg":[100 số],"fsr":[5 số]}`; `lo` = 1 khi điện cực tuột |
| `aiot/bn01/nguc/imu` | vòng ngực | 1 gói/giây, mức vận động `muc` 0 (yên) / 1 (nhẹ) / 2 (đi lại) |
| `aiot/bn01/nguc/ai` | vòng ngực | 5 giây/lần, kết quả AI chạy trên vòng: `{"lop","xs":[3 xác suất],"nhip"}` |
| `aiot/bn01/radar/song` | radar | 1 gói/giây cho mỗi loại: `{"tho":[5 số]}` hoặc `{"tim":[5 số]}`, quanh 128 |
| `aiot/bn01/radar/chi_so` | radar | 3 giây/lần: nhịp tim, nhịp thở (trung vị 5 số), có người, cử động, mức cử động, khoảng cách, chất lượng `cl`, khoảng ngưng thở đang diễn ra `ngung_giay`, số lần ngưng thở ≥ 10 giây trong 1 giờ `lan_ngung` |
| `aiot/bn01/<thiết bị>/su_kien` | vòng ngực, radar | té ngã, nghi ngưng thở khi mất máy chủ, đã nhắn tin, SOS... |
| `aiot/bn01/<thiết bị>/trang_thai` | cả 3 | online/offline (broker tự báo khi mất kết nối), IP, sóng Wi-Fi; vòng tay thêm số tin bị bỏ khi mạng nghẽn (`tin_bo`), vòng ngực thêm trạng thái module SIM (`sim`) |
| `aiot/bn01/canh_bao` | máy chủ AI, dashboard | `{"muc":0/1/2,"ly_do":"...","goi":0/1,"ly_do_vi":"..."}`; 0 = huỷ báo động; `"thu":1` là lệnh thử còi từ dashboard |
| `aiot/bn01/cau_hinh` | dashboard | `{"sdt":"+84..."}` số người nhà (gửi kèm cờ retain) |
| `aiot/bn01/ket_qua` | máy chủ AI | kết quả từng lần chạy mô hình: xác suất đã làm mượt và tức thời, chất lượng tín hiệu, thời gian xử lý (ms), nhịp không đều, lý do báo nhịp thở; khi có SpO₂ vòng tay thêm `spo2_ngu` (xác suất tụt kiểu ngưng thở), `tut_spo2`, `nen_spo2`, `phut_ngung_gio` |
| `aiot/bn01/ai/trang_thai` | máy chủ AI | `{"online":1,"tho":1}` (retain); broker tự đổi thành `{"online":0}` khi máy chủ mất kết nối |

`t` là giờ thực (ms) của mẫu đầu tiên, lấy qua NTP. Wi-Fi không ra Internet thì `t` = 0 và máy chủ dùng giờ nhận tin thay thế.

## Khi gặp lỗi

| Hiện tượng | Cách xử lý |
|---|---|
| Serial báo `[MQTT] Mất kết nối` liên tục | Sai IP trong `cau_hinh.h`, tường lửa chặn cổng 1883, hoặc Mosquitto chạy thiếu `-c mosquitto.conf` |
| Điện thoại không mở được dashboard | Điện thoại và máy tính phải cùng mạng Wi-Fi; cho phép Python qua tường lửa (mạng Private); gõ đúng IP được in ra. Cổng 8080 bận thì thêm `--cong-web 8081` |
| Dashboard báo chưa kết nối broker | Mosquitto chưa chạy, hoặc chạy trên máy khác mà quên thêm `--broker <IP>` |
| Thiết bị đang chạy mà dashboard không hiện (radar, vòng tay, vòng ngực) | 1. `MQTT_MAY_CHU` trong `cau_hinh.h` phải là IP hiện tại của máy tính (gõ `ipconfig`, xem IPv4 của Wi-Fi), nạp lại firmware sau khi sửa. Máy lấy IP tự động nên đổi mạng hoặc khởi động lại router thì IP có thể đổi. 2. Chỉ được có 1 Mosquitto: bộ cài Mosquitto tự tạo dịch vụ Windows "mosquitto" chỉ nghe trong máy, khiến nền tảng nối vào broker đó còn thiết bị nối vào broker của `1_chay_mosquitto.bat`. Dừng dịch vụ đó và đặt Manual (services.msc → Mosquitto Broker), chỉ dùng `1_chay_mosquitto.bat`. 3. Máy tính và thiết bị cùng một Wi-Fi 2,4 GHz. Wi-Fi trường học, quán cà phê thường chặn các máy trong mạng nói chuyện với nhau: khi đó phát Wi-Fi từ điện thoại cho cả máy tính và thiết bị. 4. Serial của thiết bị phải in `[MQTT] Đã kết nối broker` |
| `Không thấy AS7038RB (0x30), MAX30102 hay MAX30100 (0x57)` | Kiểm tra dây SDA (chân 6), SCL (chân 7) và nguồn 3,3 V của GY-MAX30100. Phải chuyển điện trở kéo lên I2C sang 3V3 (mục "GY-MAX30100"); thử `I2C_TOC_DO 100000` |
| Vòng tay đang đeo mà Serial báo `chạm da 0`, hoặc đã tháo mà vẫn `chạm da 1` | Chỉnh `NGUONG_CHAM_DA_MAX` trong `firmware/vong_deo_tay/cau_hinh.h` (mục "GY-MAX30100") |
| SpO₂ và nhiệt độ da luôn hiện "--" | Đeo sát hơn, giữ yên tay: SpO₂ cần ít nhất 4 nhịp không cử động. (Nếu lắp AS7038RB thay MAX30100: cảm biến này chỉ dùng 1 LED nên không có SpO₂ và nhiệt độ da; driver AS7038RB chưa thử trên chip thật.) |
| Serial vòng tay báo `cài lại cảm biến` nhiều lần | Dây SDA, SCL lỏng; chưa chuyển điện trở kéo lên của GY-MAX30100 sang 3,3 V; hoặc nguồn sụt khi phát Wi-Fi (thêm tụ 100 µF ở chân 3.3, sạc pin) |
| Dòng `[PPG]` (vòng tay) hoặc `[Trạng thái]` (vòng ngực) trên Serial có số `bỏ` tăng dần | Wi-Fi nghẽn hoặc yếu, tác vụ gửi không kịp nên bỏ bớt tin sóng mới. Đặt router gần hơn; xem `rssi` trong trạng thái thiết bị, dưới −80 dBm là yếu |
| ESP32-C3 SuperMini không vào được Wi-Fi (Serial in dấu chấm mãi) | Kiểm tra tên, mật khẩu Wi-Fi (chỉ băng tần 2,4 GHz). Giữ `WIFI_CONG_SUAT WIFI_POWER_8_5dBm`; vẫn không được thì thử `WIFI_POWER_5dBm` và đặt vòng gần router. Pin yếu cũng làm Wi-Fi rớt |
| Nạp xong mà Serial Monitor của vòng tay trống | Chọn đúng board **Nologo ESP32C3 Super Mini**, hoặc bật **USB CDC On Boot: Enabled** nếu chọn ESP32C3 Dev Module; mở Serial Monitor rồi bấm RESET |
| Sóng thở ngực (PVDF) phẳng, quá nhỏ hoặc chạm 0/4095 | Đo điện áp GPIO39 lúc nằm yên phải khoảng 1,65 V (mạch phân cực). Chỉnh tụ song song với màng và dán chặt 2 đầu màng vào dây đeo (mục "Màng áp điện PVDF") |
| ECG nhiễu hoặc R hướng xuống | Dán chặt điện cực, tránh cắm sạc khi đo. Máy chủ tự đảo dấu nếu R hướng xuống |
| Dashboard ghi "chất lượng tín hiệu" ECG thấp, nhịp tim lấy từ nguồn khác | Điện cực khô hoặc lỏng: lau da, thay miếng dán, đeo dây ngực sát hơn; tránh cắm sạc lúc đo. Dưới 60 % máy chủ bỏ qua đoạn đó để AI không báo nhầm |
| Serial vòng ngực báo `[MPU6050] … cài lại cảm biến` | Kiểm tra dây SDA (GPIO21), SCL (GPIO22), nguồn 3V3 của GY-521. Báo đúng lúc module 4G gọi hoặc nhắn tin là nguồn bị sụt: thêm tụ 1000 µF sát module 4G |
| Báo té ngã nhầm hoặc bỏ sót | Thử ngã lên đệm dày rồi chỉnh các ngưỡng ở đầu `te_nga.h` |
| Mất máy chủ thì vòng ngực báo ngưng thở nhầm, hoặc không bao giờ báo | Xem dòng `[Trạng thái]` trên Serial: "thở cục bộ" phải là "bình thường", và biên độ bình thường phải từ khoảng 60 trở lên. Thấp hơn là dây quá lỏng, cảm biến thở không nằm trên lồng ngực, hoặc tụ của mạch PVDF quá lớn (mục "Màng áp điện PVDF"). Các ngưỡng nằm ở đầu `ngung_tho.h` |
| Dashboard hoặc Telegram báo "Không thấy nhịp thở ở ngực … nhưng SpO2 vòng tay vẫn bình thường" | Dây đeo ngực lỏng hoặc cảm biến thở lệch khỏi lồng ngực. Đeo lại sát người; nếu sóng thở vẫn yếu thì tắt vòng ngực hơn 30 giây rồi bật lại (hoặc khởi động lại `may_chu.py`) để máy chủ hiệu chuẩn lại trong 60 giây đầu, lúc đó ngồi yên thở đều |
| Module SIM không có sóng (`"sim":-2`) hoặc không trả lời (`-1`) | A7670C: gắn ăng-ten, SIM 4G, nguồn đủ dòng và tụ 1000 µF, TX/RX đảo chéo, `SIM_TOC_DO 115200`; chờ 10–20 giây sau khi bật. SIM800L chỉ có 2G, nhà mạng Việt Nam đã ngừng phục vụ: dùng module 4G |
| Dashboard báo "thẻ SIM chưa sẵn sàng" (`"sim":-3`) | Thẻ chưa cắm hoặc cắm ngược, hoặc thẻ còn mã PIN. Lắp thẻ vào điện thoại, tắt khoá SIM bằng mã PIN, rồi cắm lại vào module và bật lại vòng ngực |
| Nhắn tin được nhưng nhật ký báo "Không gọi được điện thoại" (module 4G) | Máy người nhà không đổ chuông. Gọi trên 4G cần nhà mạng bật VoLTE cho module (gọi tổng đài nhà mạng, hoặc dùng SIM đã bật VoLTE); kiểm tra thêm thẻ còn tiền và số người nhà đúng dạng +84. Cảnh báo vẫn đến người nhà qua tin nhắn và Telegram |
| Radar không có sóng thở/tim, hoặc sóng phẳng, lệch khung | Bộ kit MR60BHA2: Serial phải có dòng `Nhận ra radar MR60BHA2`; người phải nằm yên trước mặt radar. Xem dòng chẩn đoán `[Radar] … byte …` (mục "Hộp radar: bộ kit MR60BHA2"); không nhận được byte nào thì rút USB rồi cắm lại. MR60BHA1 tự lắp: kiểm tra TX radar → D7, RX radar → D6; firmware đã gửi lệnh chuyển chế độ thời gian thực, nếu vẫn không có sóng thì rút điện radar rồi cắm lại |
| Nạp mô hình báo `Unrecognized keyword arguments ... quantization_config` | Colab lưu mô hình bằng Keras mới hơn bản cài được trên máy (Python 3.10 chỉ có tới Keras 3.12). `aiot_infer.py` tự xử lý: bỏ tham số lạ rồi nạp lại trọng số. Nếu vẫn lỗi, chạy nền tảng bằng Python 3.12 (`py -3.12 -m pip install -r requirements.txt`, rồi `py -3.12 may_chu.py ai ...`) |
| Nạp mô hình báo `PCG64 ... is not a known BitGenerator` | File `.joblib` được lưu bằng numpy 2 (Colab) mà máy còn numpy 1. `aiot_infer.py` tự đọc được (hàm `load_joblib`); nếu vẫn lỗi, chép `may_chu/aiot_infer.py` đè vào `may_chu/aiot_models/` |
| Cửa sổ nền tảng báo AI chạy chậm (mỗi lần trên 4 giây), máy tính nóng | Chế độ chính xác nhất chạy 3 mô hình học sâu; trên máy tính thường mất khoảng 2,5–3,2 giây mỗi lần, vẫn kịp vì AI chạy 5 giây/lần. Máy yếu thì mở `may_chu/aiot_models/model_config.json`, xoá tên các file trong `"extra_models"` (để `[]`): máy chủ chỉ dùng 1 mô hình, nhanh gấp 3 |
| Bấm "Thử gửi Telegram" báo lỗi 401 hoặc 400 | Sai mã bot (401), hoặc người nhận chưa bấm Start với bot / sai mã chat (400). Chạy lại `python may_chu.py telegram --telegram-token ...` để lấy đúng mã chat |
| Ô Huyết áp chỉ có số đo tay, không có "≈" ước tính | Cần đeo cả vòng ngực và vòng tay, cả 2 có Internet để đồng bộ giờ, rồi nhập 1 số đo huyết áp lúc đang đeo để hiệu chuẩn |
| Bật chế độ ban đêm mà ban đêm chỉ nhận 1 cuộc gọi | Đúng thiết kế: các cảnh báo hô hấp sau đó được ghi ở **Nhật ký sự kiện** (chữ "Ban đêm, chỉ ghi lại") và trong bản tổng kết buổi sáng. SpO₂ dưới 85 % liên tục từ 2 phút, ngưng thở khoảng 1 phút kèm tụt SpO₂, cảnh báo tim, té ngã, SOS vẫn gọi. Không muốn thì bỏ tích **Chế độ ban đêm** |

## Tình trạng và hạn chế

- Firmware cả 3 thiết bị biên dịch không lỗi, không cảnh báo với gói esp32 3.3.10.
- Hộp radar (bộ kit MR60BHA2) đã chạy trên phần cứng thật: gửi nhịp tim, nhịp thở, sóng thở, sóng tim và các trường `cl`, `van_dong`, `lan_ngung` lên dashboard. Các ngưỡng đếm ngưng cử động thở (25 %, 10 giây, 20 giây) chưa được kiểm chứng.
- Vòng tay và vòng ngực chưa chạy thử trên phần cứng; chưa đo dòng điện tiêu thụ (lắp xong nên đo bằng đồng hồ vạn năng nối tiếp ở dây pin). Nền tảng (lọc nhiễu, AI, cảnh báo, dashboard) đã chạy thử trên máy tính với bộ giả lập.
- Lúc huấn luyện, ECG là dữ liệu thật (PhysioNet), còn PPG và radar là mô phỏng theo đúng thời điểm từng nhịp tim. Với cảm biến thật phải thu dữ liệu của nhóm và huấn luyện lại (mục "Tinh chỉnh bằng dữ liệu thật của nhóm") trước khi tin kết quả.
- SpO₂ tính theo đường cong mẫu của Maxim, chỉ bù lệch theo máy kẹp ngón, chưa hiệu chuẩn với máy đo SpO₂ y tế: chỉ là số tham khảo.
- Mô hình ngưng thở khi ngủ học từ 8 đêm Apnea-ECG đo SpO₂ bằng máy kẹp ngón, chưa thử với SpO₂ đo ở cổ tay; mức 10 phút/giờ để báo "nghi ngưng thở khi ngủ" do nhóm tự đặt.
- Cảm biến thở ở dây đeo ngực và radar chỉ thấy ngưng cử động thở; ngưng thở tắc nghẽn (ngực vẫn gắng thở) phải dựa vào SpO₂.
- Huyết áp ước tính từ thời gian mạch đến chỉ là xu hướng tham khảo, phụ thuộc hiệu chuẩn, không dùng để cảnh báo.
- Hệ thống là sản phẩm nghiên cứu, không thay thế thiết bị y tế.
