# Robot base cho xiaozhi (2 bánh + laser VL53L0X + servo đầu)

Chạy với cả bản **OLED (mặt Mochi)** và bản **TFT LCD (mắt robot)**. Phần chuyển động bám theo chuỗi cảm xúc mà xiaozhi đã gửi cho màn hình, nên không phải sửa lớp hiển thị.

## 1. Đấu nối (mặc định, đổi được trong menuconfig)

| Linh kiện | Chân | Ghi chú |
|---|---|---|
| DRV8833 bánh trái IN1 / IN2 | GPIO39 / GPIO40 | đảo chiều bánh bằng `ROBOT_MOTOR_L_INVERT` |
| DRV8833 bánh phải IN1 / IN2 | GPIO41 / GPIO42 | đảo chiều bánh bằng `ROBOT_MOTOR_R_INVERT` |
| Servo SG90 (tín hiệu) | GPIO47 | nguồn 5V riêng, **nối chung GND** với ESP32 |
| VL53L0X SDA / SCL | bản OLED: chân OLED đang dùng; bản LCD: GPIO1 / GPIO2 | bản OLED dùng chung bus OLED (không có mục cấu hình I2C); bản LCD tự tạo bus, đổi chân trong menuconfig |
| VL53L0X INT (GPIO1 của module) | GPIO38 | ngắt "có mẫu đo mới" |
| BMI160 SDA / SCL | dùng chung bus I2C với VL53L0X | địa chỉ mặc định `0x68`, chọn `0x69` trong menuconfig nếu chân SDO/SA0 đặt địa chỉ đó |

Lưu ý phần cứng:
- DRV8833: chân **nSLEEP phải nối lên mức cao** (3.3V), nếu không motor không chạy.
- Thêm điện trở kéo xuống 10k cho 4 chân IN để motor không giật khi ESP32 khởi động.
- Pin 2S 8.4V nằm trong giới hạn DRV8833 (tối đa 10.8V). Giới hạn duty tối đa mặc định 70% để bảo vệ motor; chỉnh theo điện áp định mức của motor.
- Cấp nguồn servo và motor tách khỏi nguồn ESP32/mic nếu có thể (sụt áp gây reset và nhiễu mic).
- Bản OLED: `TOUCH_BUTTON_GPIO` (GPIO47) bị tắt vì trùng chân servo. Bản LCD: không còn đèn `LAMP` trên GPIO38 vì trùng chân INT.

## 2. Bật tính năng

```
idf.py menuconfig
  Xiaozhi Assistant -> Robot Base (2 wheels + laser guard + head servo) -> Enable robot base
idf.py build flash monitor
```

## 3. Lần chạy đầu (làm đúng thứ tự)

1. **Nhấc robot khỏi mặt bàn**, nạp firmware, xem log `RobotMotor`, `RobotHead`, `RobotRange` và `RobotIMU`. BMI160 hiệu chuẩn tư thế tự động khi khởi động; đặt robot yên trên mặt phẳng trong thời gian này.
2. Gọi tool `self.robot.head` thử servo; nếu đầu "ngẩng" lại thành cúi thì bật `ROBOT_HEAD_INVERT`. Chỉnh `ROBOT_HEAD_CENTER_DEG` / `ROBOT_HEAD_RANGE_DEG` cho đúng cơ khí.
3. Gọi `self.robot.move` (bánh chạm đất, **trên sàn nhà**, không phải bàn): nếu bánh quay ngược thì bật `ROBOT_MOTOR_*_INVERT`.
4. Đặt robot trên **sàn phẳng**, bật nguồn: cảm biến tự đo khoảng cách sàn (log `floor calibrated: ... mm`). Đo lại bất cứ lúc nào bằng tool `self.robot.calibrate_floor`.
5. Thử vật cản và mép bàn trên sàn (dùng một cạnh bậc thấp) trước khi cho chạy trên bàn.

## 4. Hành vi

- **Vui**: xoay tròn đến khi đổi cảm xúc, đầu lắc lư; thỉnh thoảng nhảy tới/lùi ngắn.
- **Buồn**: lùi chậm (có giới hạn), đầu gập xuống, dừng "thở dài".
- **Giận**: tiến từ từ, đầu ngẩng thách thức, dừng nhìn trừng, gằm đầu, quay dậm chân.
- **Buồn ngủ**: đi qua lại chậm, đầu gật gù.
- Bổ sung: laughing, silly/funny, crying, surprised/shocked, confused, embarrassed, loving/kissy, thinking, cool/confident, relaxed, winking, delicious. Sửa bảng ở `main/robot_base/robot_behaviors.cc`.
- Cường độ hành vi tự chọn theo cảm xúc; MCP `self.robot.perform` nhận thêm `intensity` từ 25–100 để chỉnh độ mạnh bánh xe và góc đầu.
- **Rảnh (idle)**: đi tới, rẽ, xoay tròn ngẫu nhiên, lắc lư lùi tiến, nhìn quanh, xen kẽ nghỉ 1.5–4.5 giây.
- Khi `neutral` (xiaozhi về idle) thì kết thúc cảm xúc và quay lại đi lang thang. Mỗi cảm xúc tự về neutral sau `ROBOT_EMOTION_MAX_SEC` giây.
- Các cảm xúc diễn ra một lượt tự kết thúc và trả đầu về giữa; cảm xúc nối vòng vẫn kết thúc khi đổi cảm xúc hoặc hết thời gian.
- Khi thiết bị đang **listening/speaking**, bánh xe dừng ngay để giảm nhiễu mic/loa; đầu gật theo lúc nói và làm cử chỉ chào/tạm biệt khi bắt đầu/kết thúc hội thoại.
- Từ trạng thái idle sang listening (gọi wake word hoặc nhấn nút) robot chào bằng đầu; khi hội thoại về idle robot cúi chào.
- BMI160: nghiêng/nhấc lên tạo cảm xúc ngạc nhiên; khi được giữ ổn định robot chuyển sang tò mò/suy nghĩ. Motor bị khóa ngay khi có sự kiện nhấc, và chỉ mở sau khi nhận diện đặt xuống. Nghiêng trước/sau/trái/phải có thể xem qua pitch/roll trong `self.robot.get_status`.

## 5. An toàn (laser)

- Đọc > 150mm (hoặc không có phản xạ) = **mép bàn/hố sụt**. Đọc ngắn hơn (khoảng cách sàn đã đo − 30mm) = **vật cản**.
- Cần 2 mẫu liên tiếp mới vào trạng thái nguy hiểm (khoảng 45ms), 3 mẫu sạch mới thoát.
- Ngắt phần cứng ngay trong tác vụ cảm biến: cắt thành phần tiến của bánh xe, không chờ vòng điều khiển.
- Gặp nguy hiểm khi đang tiến thì lùi tối đa ~0.4s rồi quay hướng ngẫu nhiên, cảm xúc nào cũng vậy. Lặp 6 lần trong 20s thì "kẹt": đứng yên 15s, mặt bối rối.
- Cảm biến hỏng hoặc không có: **bánh xe bị khóa** (đầu vẫn chạy). Chỉ bật `ROBOT_ALLOW_NO_SENSOR` khi thử trên bàn làm việc.
- Chưa hiệu chuẩn sàn thì chỉ có phát hiện mép (tuyệt đối 150mm), chưa có phát hiện vật cản.
- Hiệu chuẩn sàn khóa bánh ngay lập tức; chỉ thay ngưỡng khi phép đo mới hợp lệ, lỗi hiệu chuẩn giữ nguyên ngưỡng cũ.
- BMI160 dùng riêng tác vụ đọc, không chặn tác vụ VL53L0X. Nếu không tìm thấy chip hoặc I2C lỗi, log báo rõ, phần phản ứng nhấc/nghiêng tắt nhưng khóa laser và các chức năng khác vẫn hoạt động.
- Chọn địa chỉ BMI160, trục X/Y song song hướng trước và đảo dấu trục trong menuconfig nếu cần. Nếu hướng lắp đổi, đặt robot đúng tư thế sử dụng rồi gọi `self.robot.calibrate_imu`.
- Chỉ dùng gia tốc kế không thể phân biệt robot đang được cầm yên với robot được đặt xuống thật nhẹ ở đúng tư thế ban đầu. Vì an toàn, trạng thái cầm chỉ tự kết thúc khi cảm biến laser xác nhận khoảng cách sàn đã hiệu chuẩn hoặc BMI160 nhận được xung đặt xuống; nếu không, motor giữ khóa. Đặt robot trên sàn phẳng để laser xác nhận, hoặc gọi `self.robot.calibrate_imu` khi robot đã được đặt xuống.

**Giới hạn đã biết**
- **Không có cảm biến phía sau**: lùi là "mù". Có giới hạn 0.7s liên tục và ngân sách lùi mỗi cảm xúc, nhưng vẫn có thể rơi khỏi bàn khi lùi. Cân nhắc thêm cảm biến sau hoặc công tắc, hoặc tắt `ROBOT_BLIND_REVERSE` khi chạy trên bàn.
- Mặt bàn đen, bóng, trong suốt làm VL53L0X đo sai; bàn đen có thể bị hiểu nhầm là hố sụt (robot sẽ dừng, an toàn nhưng khó chịu).
- Vật rất thấp (vài mm) không bị phát hiện.
- Bản OLED: bus I2C dùng chung, một lần đẩy khung hình OLED có thể làm trễ một mẫu laser khoảng 25ms.

## 6. Công cụ MCP cho LLM

`self.robot.get_status`, `self.robot.move`, `self.robot.stop`, `self.robot.head`, `self.robot.perform` (with optional `intensity` 25–100), `self.robot.set_wander`, `self.robot.calibrate_floor`, `self.robot.calibrate_imu`.

## 7. Kiểm thử

`tests/test_robot.cc` kiểm tra logic (phân loại nguy hiểm, bảng hành vi, an toàn) trên máy tính, không cần ESP32:

```
g++ -std=c++17 -Imain/robot_base tests/test_robot.cc main/robot_base/robot_logic.cc main/robot_base/robot_behaviors.cc main/robot_base/robot_controller.cc -o t && ./t
```

Giấy phép: `vl53l0x.cc/.h` là bản port thư viện Pololu VL53L0X (MIT), xem `main/robot_base/VL53L0X_LICENSE.txt`.
