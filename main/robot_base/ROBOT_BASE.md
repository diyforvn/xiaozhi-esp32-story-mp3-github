# Robot Base: nguyên lý và cách hoạt động

Tài liệu này mô tả robot hai bánh trong `main/robot_base`: điều khiển động cơ vi sai, màn hình/đầu servo, cảm biến VL53L0X và BMI160, hành vi cảm xúc, đi lang thang và các khóa an toàn. Các tùy chọn phần cứng được cấu hình khi biên dịch bằng `menuconfig`.

## 1. Tổng quan luồng điều khiển

Robot nhận lệnh từ Xiaozhi qua các MCP tool như `self.robot.move`, `self.robot.perform` và `self.robot.set_wander`. Bộ điều khiển `RobotController` biến lệnh thành tốc độ mục tiêu cho bánh trái/phải và tư thế đầu. Một vòng điều khiển định kỳ kiểm tra trạng thái cảm biến, trạng thái hội thoại, hiệu chuẩn và các giới hạn chuyển động trước khi gửi tốc độ xuống driver motor.

Khi có nhiều yêu cầu đồng thời, bộ điều khiển ưu tiên lệnh di chuyển thủ công, sau đó đến hành vi cảm xúc, rồi mới đến đi lang thang. Hệ thống vẫn có thể từ chối hoặc dừng lệnh nếu cảm biến đang bật nhưng không sẵn sàng, robot đang được phát hiện là bị nhấc/nghiêng, hoặc đang hiệu chuẩn. Servo đầu và chuyển động bánh xe được điều khiển riêng; dừng bánh xe không nhất thiết đưa đầu về giữa.

## 2. Cảm biến và nguyên lý an toàn

### BMI160 (IMU)

BMI160 đo gia tốc và hướng trọng lực. Khi được bật, firmware dùng nó để nhận biết robot bị nhấc, nghiêng hoặc đang được đặt xuống. IMU không đo vị trí tuyệt đối, không biết robot đã quay/dịch chuyển bao xa và cũng không thể xác nhận robot đã trở lại đúng tọa độ ban đầu.

Khi khởi động, robot cần đứng yên trên mặt phẳng và đúng chiều lắp để lấy mốc trọng lực/hướng ban đầu. Bộ phân loại chuyển động dùng độ nghiêng, biến thiên gia tốc, va chạm khi đặt xuống và thời gian ổn định để xác định trạng thái. Nếu VL53L0X cũng được bật, khoảng cách sàn gần với mốc đã học có thể bổ sung bằng chứng robot đã được đặt xuống. Đây vẫn chỉ là kiểm tra độ cao/khoảng cách và trạng thái bề mặt, không phải định vị tọa độ.

Nếu BMI160 được bật nhưng không khởi tạo, không có mẫu mới hoặc chưa hiệu chuẩn hướng, các lệnh di chuyển thông thường bị khóa. Có thể chạy `self.robot.calibrate_imu` khi robot đứng yên, đặt đúng chiều. Nếu BMI160 không có trên phần cứng, tắt nó trong `menuconfig` để firmware bỏ qua cả khởi tạo lẫn kiểm tra liên quan.

### VL53L0X (đo khoảng cách)

VL53L0X đo khoảng cách theo hướng cảm biến đang chĩa. Khi bật, lúc khởi động cảm biến lấy 15 mẫu để ghi nhớ khoảng cách sàn; robot phải đặt trên sàn phẳng trong suốt bước này. Bộ phân loại so sánh các mẫu mới với mốc sàn để nhận biết khoảng cách bất thường, vật cản quá gần hoặc mất phản hồi (được xem là nguy cơ mép/vực). Nhiều mẫu liên tiếp được dùng để lọc nhiễu trước khi đổi trạng thái nguy hiểm.

Có thể chạy `self.robot.calibrate_floor` khi robot đang đặt trên mặt phẳng phù hợp để lấy lại mốc. Nếu hiệu chuẩn thất bại do mẫu nhiễu, không có sàn trong vùng đo hoặc khoảng cách nằm ngoài phạm vi cấu hình, cần kiểm tra vị trí/lắp đặt cảm biến trước khi di chuyển.

VL53L0X chỉ bảo vệ vùng mà cảm biến nhìn thấy. Hướng lắp quyết định nó đang quan sát sàn hay vật cản phía trước; cảm biến nhìn xuống có thể phát hiện mép sàn nhưng không thay thế cảm biến phía sau. Khi bật mà cảm biến lỗi hoặc dữ liệu quá cũ, di chuyển thường bị khóa.

### Bật/tắt cảm biến và trạng thái sẵn sàng

Trong `menuconfig`, từng cảm biến có tùy chọn enable riêng:

- **Enable VL53L0X**: bật khởi tạo, hiệu chuẩn sàn, kiểm tra khoảng cách và hazard.
- **Enable BMI160**: bật khởi tạo, hiệu chuẩn hướng, phát hiện nhấc/nghiêng và kiểm tra tình trạng IMU.

Cảm biến bị disable sẽ không được khởi tạo và các kiểm tra riêng của cảm biến đó được bỏ qua. Cảm biến được enable thì phải khởi tạo và cung cấp dữ liệu hợp lệ; lỗi cảm biến sẽ làm `sensors_ready` thành false và khóa chuyển động bình thường. Nếu tắt cả hai, robot bỏ qua các khóa dựa trên hai cảm biến này, nhưng các giới hạn điều khiển khác như trạng thái đang nói, hiệu chuẩn đang chạy, giới hạn lùi và giới hạn PWM vẫn còn hiệu lực.

`self.robot.get_status` trả về các trường như `vl53l0x_enabled`, `vl53l0x_ok`, `imu_enabled`, `imu_present`, `imu_ok`, `imu_calibrating`, `sensors_ready`, `hazard`, `distance_mm`, `floor_mm`, `imu_state`, `pitch_deg` và `roll_deg`. Hãy xem các trường này trước khi chẩn đoán vì sao lệnh di chuyển bị từ chối.

## 3. Chế độ hoạt động

### Lệnh di chuyển thủ công

`self.robot.move` nhận hướng, tốc độ và thời lượng:

- `forward`, `backward`, `spin_left`, `spin_right`, `left`, `right`.
- `speed`: 10–100; `duration_ms`: 100–5000.
- `left` và `right` là đường cong, đặt tốc độ một bánh thấp hơn bánh kia. `spin_*` quay hai bánh ngược chiều.

Lệnh tạo chuyển động trong khoảng thời lượng yêu cầu, sau đó bộ điều khiển trở về chế độ hành vi/nhàn rỗi thích hợp. Lệnh di chuyển thủ công có thể chạy khi đang nghe nếu cấu hình cho phép chuyển động lúc listening; robot tạm dừng bánh xe khi Xiaozhi đang phát lời nói và tiếp tục sau khi trạng thái speaking kết thúc. Hãy kiểm tra `ROBOT_STOP_WHEN_LISTENING` trong cấu hình nếu muốn thay đổi hành vi khi nghe.

`speed` là mức lệnh từ 10 đến 100, không phải duty PWM trực tiếp. Driver ánh xạ mức này vào khoảng `ROBOT_MOTOR_MIN_START_PCT` đến `ROBOT_MOTOR_MAX_PCT` để bù ngưỡng khởi động của motor. Ví dụ cấu hình hiện tại có min-start 75% và max 90%; lệnh speed 70 tương ứng xấp xỉ 85.5% duty. Vì vậy giảm `speed` không nhất thiết làm duty xuống cùng tỷ lệ phần trăm.

### Đi lang thang tự do

`self.robot.set_wander(enabled=true)` bật hành vi ngẫu nhiên khi robot đang idle hoặc listening. Các hành vi có thể tiến ngắn, rẽ, xoay, lùi ngắn hoặc chỉ cử động đầu; giữa các lượt có khoảng chờ ngẫu nhiên. Cấu hình `ROBOT_WANDER_ENABLE` quyết định trạng thái mặc định khi khởi động. Lệnh MCP có thể bật hoặc tắt chế độ này trong lúc chạy.

Đi lang thang vẫn đi qua lớp giới hạn an toàn như lệnh thường. Nó tạm dừng khi robot đang speaking, cảm biến bắt buộc chưa sẵn sàng, IMU đang giữ trạng thái lifted/tilted, hoặc bộ điều khiển đang xử lý phản xạ nguy hiểm. Nếu VL53L0X phát hiện chướng ngại/mép sàn, robot có thể lùi ngắn rồi xoay tránh. Nếu robot mắc kẹt và kích hoạt phản xạ quá nhiều lần, bộ điều khiển tạm dừng chuyển động trong một khoảng thời gian.

### Cảm xúc và hành vi

`self.robot.perform` chạy chuỗi hành vi gắn với cảm xúc như happy, sad, angry, sleepy, laughing, confused, loving, thinking hoặc relaxed. Mỗi hành vi là các bước bánh trái/phải, tư thế đầu, dao động đầu và thời lượng được định nghĩa trong `robot_behaviors.cc`. Các bước vẫn đi qua lớp an toàn của `RobotController`; bảng hành vi không tự bỏ qua kiểm tra cảm biến. `neutral` kết thúc cảm xúc hiện tại và cho phép bộ điều khiển quay về idle/đi lang thang.

### Dừng và hiệu chuẩn

- `self.robot.stop`: dừng bánh xe và hủy cảm xúc/hành vi hiện tại.
- `self.robot.calibrate_floor`: lấy lại mốc sàn; chỉ khả dụng khi VL53L0X được bật.
- `self.robot.calibrate_imu`: lấy lại mốc hướng IMU; chỉ khả dụng khi BMI160 được bật. Robot phải đứng yên trong lúc đo.
- `self.robot.head`: đặt tư thế đầu theo màn hình/servo, độc lập với bánh xe.

## 4. Motor và kiểm tra chẩn đoán

Mặc định GPIO điều khiển driver hai bánh là GPIO39/40 cho bánh trái và GPIO41/42 cho bánh phải. GPIO47 dùng cho tín hiệu servo đầu. Chân I2C và chân INT của VL53L0X có thể thay đổi theo board/Kconfig; kiểm tra lựa chọn board và cấu hình chân trong `menuconfig` trước khi đấu nối. Các chân IN của driver cần đúng mức logic, chân nSLEEP phải được kéo mức hoạt động theo yêu cầu của module, và nguồn motor phải đủ dòng. Nguồn motor/servo nên phù hợp tải và nối chung GND với ESP32.

`self.robot.motor_test` là công cụ chẩn đoán dùng duty PWM trực tiếp, tối đa 1 giây. Nó bỏ qua giới hạn hazard và một số khóa chuyển động thường, nhưng vẫn yêu cầu các cảm biến đang enable phải khỏe; đồng thời nó vượt giới hạn PWM đã cấu hình. Chỉ dùng khi bánh được nhấc khỏi mặt đất hoặc robot được cố định chắc chắn. Đây không phải chế độ lái bình thường.

## 5. Cấu hình menuconfig

Mở cấu hình bằng:

```text
idf.py menuconfig
```

Đi tới **Xiaozhi Assistant → Robot Base**. Các mục chính gồm:

- Bật/tắt Robot Base.
- GPIO/chiều quay của từng motor, PWM min-start và giới hạn duty tối đa.
- Bật/tắt riêng VL53L0X và BMI160; địa chỉ I2C, hướng trục IMU và ngưỡng nhấc/đặt xuống.
- Ngưỡng khoảng cách cliff/vật cản và cấu hình hành vi đi lang thang.
- Có dừng khi listening hay cho phép bánh xe hoạt động khi listening.
- Servo đầu và giới hạn góc.

Giá trị đang có trong `sdkconfig` của workspace này: VL53L0X đang tắt, BMI160 đang bật, đi lang thang bật, dừng khi listening bật, min-start PWM 75% và max PWM 90%. Đây là cấu hình build hiện tại; các giá trị mặc định và các lựa chọn khác có thể xem trực tiếp trong `Kconfig.projbuild`.

## 6. Khởi động và chẩn đoán nhanh

1. Đặt robot đứng yên, đúng chiều trên mặt phẳng nếu BMI160 hoặc VL53L0X được bật.
2. Kiểm tra log khởi tạo motor, BMI160 và VL53L0X. Với VL53L0X, chờ log `floor calibrated`; với BMI160, chờ hiệu chuẩn orientation hoàn tất.
3. Gọi `self.robot.get_status` để xác nhận `sensors_ready=true` và trạng thái từng cảm biến đúng với phần cứng/cấu hình.
4. Thử `self.robot.move` trong thời gian ngắn ở tốc độ phù hợp, robot đặt trên sàn phẳng và có người giám sát.
5. Nếu motor test chạy nhưng move không chạy, kiểm tra `sensors_ready`, `imu_state`, `hazard`, `speaking`, trạng thái hiệu chuẩn và log `RobotBase`. Lệnh thường có nhiều khóa bảo vệ hơn phép thử PWM trực tiếp.
6. Nếu bánh quay ngược hoặc rẽ sai, đổi cấu hình đảo chiều từng motor. Nếu motor chỉ khởi động ở mức duty cao, điều chỉnh min-start và max PWM tương thích với motor/driver/nguồn.

Không thử phát hiện mép bàn trên mặt bàn cao. VL53L0X nhìn xuống cần được kiểm chứng ở độ cao thấp, có kiểm soát và không để robot tự chạy gần mép nguy hiểm.

## 7. Giải thích và cách chỉnh tham số cảm biến

Mở `idf.py menuconfig`, vào **Xiaozhi Assistant → Robot Base → Laser guard** hoặc **BMI160 motion sensor**. Sau khi đổi tham số, lưu cấu hình, build và nạp lại firmware; thay đổi trong menuconfig không áp dụng ngay trên firmware đang chạy.

### VL53L0X: ngưỡng sàn, vật cản và mép vực

VL53L0X đo khoảng cách theo hướng cố định của module. Khi khởi động, robot lấy 15 mẫu để tạo `floor_mm` (mốc khoảng cách sàn). Đặt robot đúng tư thế trên mặt phẳng, để yên và bảo đảm cảm biến nhìn thấy sàn trong lúc hiệu chuẩn. Có thể hiệu chuẩn lại bằng `self.robot.calibrate_floor`.

| Tham số menuconfig | Mặc định / khoảng | Ý nghĩa và cách chọn |
|---|---:|---|
| `ROBOT_CLIFF_MM` — Cliff threshold | 150 mm / 60–1000 mm | Một số đo lớn hơn ngưỡng này được xem là `cliff`; không có phản hồi cũng được xem là cliff. Sàn dùng để hiệu chuẩn phải thấp hơn ngưỡng ít nhất khoảng 15 mm. Tăng ngưỡng nếu khoảng cách sàn bình thường của cách lắp cảm biến lớn hơn; ngưỡng quá cao có thể làm phát hiện mép chậm hơn. |
| `ROBOT_OBSTACLE_MARGIN_MM` — Obstacle margin | 30 mm / 10–100 mm | Vật cản được nhận biết khi số đo ngắn hơn mốc sàn ít nhất margin: `reading + margin < floor_mm`. Ví dụ mốc sàn 100 mm, margin 30 mm thì số đo dưới 70 mm là vật cản. Margin nhỏ nhạy hơn với thay đổi nhỏ nhưng dễ phản ứng với nhiễu; margin lớn chỉ nhận vật cản khi nó gần hơn. |

Ngoài hai tham số trên, code xem khoảng cách dưới 30 mm là vật cản/quá gần. Cần phân biệt ngưỡng này với margin: 30 mm là giới hạn cố định trong logic, còn `ROBOT_OBSTACLE_MARGIN_MM` là độ chênh so với mốc sàn.

Hazard cần nhiều mẫu liên tiếp để lọc nhiễu: thường 2 mẫu để vào trạng thái nguy hiểm và 3 mẫu sạch để xóa. Mốc sàn không hợp lệ nếu 15 mẫu quá nhiễu (p90-p10 lớn hơn 15 mm), trung vị dưới 30 mm, hoặc trung vị không thấp hơn ngưỡng cliff đủ khoảng cách. Nếu hiệu chuẩn thất bại, trước tiên kiểm tra hướng cảm biến và khoảng cách thực tế; sau đó mới cân nhắc tăng `ROBOT_CLIFF_MM` vừa đủ.

**Cách chỉnh:** đặt robot trên sàn nơi sẽ chạy, đọc `floor_mm` trong `self.robot.get_status` hoặc log `floor calibrated`. Chọn `ROBOT_CLIFF_MM` lớn hơn mốc này ít nhất 15 mm, nhưng không cao hơn mức cần thiết. Chọn margin dựa trên chênh lệch giữa số đo sàn và vật cản mà bạn muốn nhận biết; thử ở tốc độ thấp, có người giữ robot. Margin 30 mm không có nghĩa cảm biến sẽ phát hiện mọi vật cản ở phía trước: kết quả phụ thuộc góc lắp, bề mặt và vùng nhìn của cảm biến.

### BMI160: địa chỉ, hướng lắp và phát hiện nhấc/đặt xuống

BMI160 chỉ đo gia tốc và hướng trọng lực; nó không cung cấp vị trí tuyệt đối. Firmware đọc mẫu mỗi 20 ms. Khi bật IMU, robot phải hiệu chuẩn được hướng ban đầu và nhận mẫu mới; nếu không, chuyển động thường bị khóa.

| Tham số menuconfig | Mặc định / lựa chọn | Ý nghĩa và cách chọn |
|---|---:|---|
| `ROBOT_BMI160_ADDRESS` | 0x68 hoặc 0x69; mặc định 0x68 | Địa chỉ I2C do chân SA0/SDO trên module quyết định. Firmware thử địa chỉ đã chọn trước rồi thử địa chỉ còn lại, nên sai lựa chọn chưa chắc làm cảm biến thất bại; vẫn kiểm tra log `RobotIMU` để biết địa chỉ thực tế. |
| `ROBOT_BMI160_FORWARD_AXIS` | X mặc định; X hoặc Y | Chọn trục cảm biến được lắp gần song song với hướng tiến của robot. Nếu board được xoay và hướng tiến nằm theo trục Y của BMI160, chọn Y. Cần đặt robot đúng chiều khi hiệu chuẩn. |
| `ROBOT_BMI160_FORWARD_INVERT` | Tắt mặc định | Đảo chiều trục tiến đã chọn. Bật nếu nghiêng robot về trước nhưng `pitch_deg`/hướng nghiêng được báo là lùi, sau khi đã xác nhận robot được đặt đúng chiều trong lúc hiệu chuẩn. |
| `ROBOT_BMI160_LIFT_TILT_DEG` | 30° / 20–60° | Góc nghiêng lớn nhất giữa pitch/roll để nhận biết robot đang bị nhấc/nghiêng; code cần góc vượt ngưỡng khoảng 300 ms để dùng điều kiện này. Giảm ngưỡng để nhạy hơn nhưng dễ khóa nhầm khi robot chạy trên mặt nghiêng/rung; tăng ngưỡng để chỉ phản ứng với góc nghiêng lớn. Cú nhấc có gia tốc mạnh vẫn có thể được phát hiện bằng các điều kiện gia tốc độc lập với góc này. |
| `ROBOT_BMI160_RELEASE_IMPACT_MG` | 1200 mg / 1000–2500 mg | Ngưỡng độ lớn gia tốc để ghi nhận va chạm khi đặt robot xuống. 1000 mg = 1.0 g; mặc định 1200 mg = 1.2 g. Giảm để nhận cú đặt nhẹ hơn nhưng có thể nhầm rung/lắc khi đang cầm; tăng để ít nhạy hơn nhưng đặt nhẹ có thể không tạo đủ dấu hiệu giải phóng. |
| `ROBOT_BMI160_RELEASE_DELTA_MG` | 350 mg / 100–1000 mg | Ngưỡng thay đổi gia tốc giữa hai mẫu liên tiếp để ghi nhận va chạm; mẫu cách nhau 20 ms, đây là mức thay đổi mỗi mẫu, không phải mg/giây. Giảm ngưỡng để bắt cú đặt nhẹ hơn; tăng nếu robot thường bị nhận nhầm là đã đặt xuống khi vẫn còn cầm. |

**Trình tự hiệu chuẩn và nhận trạng thái:** khi khởi động hoặc gọi `self.robot.calibrate_imu`, firmware thu 40 mẫu trong khoảng 0,8 giây. Giữ robot đứng yên, nằm đúng tư thế sử dụng bình thường và trên mặt phẳng trong suốt thời gian này. Bộ hiệu chuẩn yêu cầu gia tốc gần mức trọng lực; rung hoặc di chuyển có thể làm hiệu chuẩn thất bại. Tool hiệu chuẩn thủ công chỉ chạy khi thiết bị idle.

Sau khi hiệu chuẩn, cú nhấc được nhận biết từ xung gia tốc, chuyển động kéo dài hoặc nghiêng đủ lâu. Khi đã nhấc, robot ghi nhận dấu hiệu va chạm/đặt xuống, sau đó yêu cầu tư thế không nghiêng và gia tốc ổn định liên tục trước khi trở lại trạng thái resting. Vì thế hạ robot thật nhẹ có thể cần chỉnh giảm một trong hai ngưỡng release; nếu robot tự thoát trạng thái held khi đang cầm, hãy tăng ngưỡng tương ứng. Chỉ chỉnh từng tham số một và thử lại.

Khi VL53L0X cũng bật, firmware có thêm dấu hiệu robot rời mốc sàn rồi trở lại gần mốc đó (sai khác khoảng cách tối đa 20 mm) để xác nhận đã đặt xuống. Đây là kiểm tra độ cao/khoảng cách, không phải xác nhận vị trí XY ban đầu. Khi VL53L0X tắt, BMI160 phải dựa chủ yếu vào gia tốc và độ ổn định nên việc nhận biết đặt xuống kém chắc chắn hơn.

**Quy trình tinh chỉnh an toàn:**

1. Bật đúng cảm biến có trên robot; xem `self.robot.get_status` để xác nhận `imu_ok`, `imu_state`, `pitch_deg`, `roll_deg`, `vl53l0x_ok` và `floor_mm`.
2. Hiệu chuẩn trên mặt phẳng, không cầm hoặc rung robot. Xác nhận trạng thái ban đầu là resting và cảm biến khỏe.
3. Thử nhấc và đặt xuống nhẹ nhàng khi robot đứng yên, bánh không chạm khu vực nguy hiểm. Xem `imu_state` đổi giữa resting, picked_up, held và resting.
4. Nếu không phát hiện cú nhấc nghiêng, giảm `ROBOT_BMI160_LIFT_TILT_DEG` từng bước. Nếu vẫn không phát hiện, kiểm tra trục X/Y, chiều đảo và vị trí lắp.
5. Nếu đặt xuống nhưng vẫn held, giảm nhẹ ngưỡng impact hoặc delta. Nếu robot báo đã đặt xuống trong lúc vẫn đang cầm, tăng ngưỡng gây nhận nhầm. Tránh giảm cả hai cùng lúc vì sẽ khó biết nguyên nhân và tăng nguy cơ báo release giả.
6. Đặt `ROBOT_CLIFF_MM`/margin theo số đo thực tế của VL53L0X; đừng thử cliff trên bàn cao. Thử nghiệm cảm biến khoảng cách ở vị trí thấp, có người giữ robot.


## 8. Tham chiếu đầy đủ mọi mục trong menuconfig Robot Base

Các mục Robot Base chỉ xuất hiện trên những board được Kconfig hỗ trợ. Phần này giải thích tất cả lựa chọn nằm trong menu **Xiaozhi Assistant → Robot Base**. Giá trị ghi là mặc định trong Kconfig; `sdkconfig` của từng workspace có thể ghi đè chúng. Nhớ build và nạp firmware sau khi thay đổi.

### Enable robot base

| Tham số | Mặc định | Chức năng |
|---|---:|---|
| `ROBOT_BASE_ENABLE` | Tắt | Bật toàn bộ robot base: driver bánh, servo, logic robot và nhóm MCP `self.robot.*`. Nếu tắt, board không khởi tạo robot và không đăng ký các MCP tool này. Chỉ dùng trên các board được khai báo hỗ trợ trong Kconfig. |

### Pins

Các chân phải là GPIO thật sự được board/ESP32 hỗ trợ, chưa bị màn hình, I2C, flash, PSRAM hoặc thiết bị khác sử dụng. Không chọn cùng một GPIO cho hai chức năng. Đổi chân trong firmware không thay thế việc đấu lại dây phần cứng.

| Tham số | Mặc định | Chức năng / lưu ý |
|---|---:|---|
| `ROBOT_PIN_MOTOR_L_IN1` | GPIO39 | Tín hiệu IN1 của driver bánh trái. |
| `ROBOT_PIN_MOTOR_L_IN2` | GPIO40 | Tín hiệu IN2 của driver bánh trái. |
| `ROBOT_PIN_MOTOR_R_IN1` | GPIO41 | Tín hiệu IN1 của driver bánh phải. |
| `ROBOT_PIN_MOTOR_R_IN2` | GPIO42 | Tín hiệu IN2 của driver bánh phải. |
| `ROBOT_PIN_SERVO` | GPIO47 | Tín hiệu PWM servo đầu. Servo cần nguồn phù hợp; nối chung GND với ESP32. |
| `ROBOT_PIN_TOF_INT` | GPIO38 | Chân ngắt/data-ready GPIO1 của VL53L0X, không phải chân INT của BMI160. Nếu không nối dây INT, firmware vẫn polling I2C khi không nhận được ngắt. Chân này không được dùng làm SDA/SCL. |
| `ROBOT_I2C_SDA` | GPIO1 | SDA của bus cảm biến trên các cấu hình LCD được chỉ định trong Kconfig. |
| `ROBOT_I2C_SCL` | GPIO2 | SCL của bus cảm biến trên các cấu hình LCD được chỉ định trong Kconfig. |

Ở cấu hình OLED, robot dùng lại bus I2C do màn hình tạo và truyền vào lúc khởi tạo; hai mục SDA/SCL riêng chỉ xuất hiện với một số cấu hình LCD. BMI160 và VL53L0X dùng chung bus I2C này. Hãy kiểm tra sơ đồ board trước khi đổi chân. Kconfig cho phép nhập GPIO trong khoảng 0–48 nhưng không có nghĩa mọi GPIO trong khoảng đó đều phù hợp với board của bạn.

### Wheels

| Tham số | Mặc định / phạm vi | Chức năng / cách chỉnh |
|---|---:|---|
| `ROBOT_MOTOR_L_INVERT` | Tắt | Đảo chiều logic bánh trái. Bật nếu lệnh tiến làm riêng bánh trái quay lùi. |
| `ROBOT_MOTOR_R_INVERT` | Tắt | Đảo chiều logic bánh phải. Bật nếu lệnh tiến làm riêng bánh phải quay lùi. |
| `ROBOT_MOTOR_MIN_START_PCT` | 75% / 0–90% | Duty tối thiểu áp dụng cho lệnh khác 0, nhằm vượt ma sát tĩnh để motor bắt đầu quay. Tăng nếu bánh không khởi động ở tốc độ thấp; giảm nếu robot giật hoặc chạy nhanh quá ở lệnh nhỏ. |
| `ROBOT_MOTOR_MAX_PCT` | 70% / 20–100% | Giới hạn duty cho lệnh di chuyển bình thường để hạn chế tốc độ/dòng/tải motor. Không vượt giới hạn an toàn của motor, driver và pin. |

`self.robot.move` đưa ra mức tốc độ điều khiển chứ không phải duty thô: code ánh xạ độ lớn lệnh 1–100 vào khoảng min-start đến max duty. Nếu max duty nhỏ hơn min-start, driver nâng max lên bằng min-start, nên cần đặt hai giá trị nhất quán. `self.robot.motor_test` dùng PWM thô và có thể vượt giới hạn này; đây chỉ là phép thử ngắn, cần cố định hoặc nhấc bánh khỏi mặt đất.

### Laser guard — VL53L0X

| Tham số | Mặc định / phạm vi | Chức năng |
|---|---:|---|
| `ROBOT_VL53L0X_ENABLE` | Bật | Bật khởi tạo, đọc khoảng cách, hiệu chuẩn sàn và khóa an toàn do hazard/lỗi cảm biến. Nếu bật mà sensor lỗi hoặc không có mẫu mới, chuyển động bình thường bị khóa. Nếu tắt, firmware bỏ qua VL53L0X và các kiểm tra cliff/vật cản từ nó. |
| `ROBOT_CLIFF_MM` | 150 mm / 60–1000 mm | Ngưỡng khoảng cách lớn được xem là cliff; xem giải thích và ví dụ ở mục 7. |
| `ROBOT_OBSTACLE_MARGIN_MM` | 30 mm / 10–100 mm | Độ chênh thấp hơn mốc sàn để nhận biết vật cản; xem mục 7. |

Nếu tắt VL53L0X, hai ngưỡng vẫn có giá trị lưu trong cấu hình nhưng không được dùng. Khoảng cách đo không tự xác định hướng; phải lắp module đúng góc để vùng tia phù hợp với kiểu bảo vệ mong muốn.

### BMI160 motion sensor

| Tham số | Mặc định / phạm vi | Chức năng |
|---|---:|---|
| `ROBOT_BMI160_ENABLE` | Bật | Bật khởi tạo/hiệu chuẩn IMU và kiểm tra dữ liệu, trạng thái nhấc/nghiêng. Nếu bật nhưng BMI160 lỗi hoặc chưa hiệu chuẩn, chuyển động bình thường bị khóa. Tắt nếu board không có BMI160. |
| `ROBOT_BMI160_ADDRESS` | 0x68 | Chọn địa chỉ I2C 0x68 hoặc 0x69 theo chân SA0/SDO của module. Firmware thử địa chỉ còn lại nếu địa chỉ chọn trước không phản hồi đúng chip ID. |
| `ROBOT_BMI160_FORWARD_AXIS` | X | Chọn trục X hoặc Y của cảm biến gần song song với chiều tiến của robot. |
| `ROBOT_BMI160_FORWARD_INVERT` | Tắt | Đảo chiều của trục tiến đã chọn nếu hướng nghiêng trước/sau bị báo ngược. |
| `ROBOT_BMI160_LIFT_TILT_DEG` | 30° / 20–60° | Ngưỡng góc pitch/roll để phát hiện nghiêng; code yêu cầu nghiêng duy trì khoảng 300 ms. Ngưỡng thấp nhạy hơn, ngưỡng cao cần nghiêng nhiều hơn. |
| `ROBOT_BMI160_RELEASE_IMPACT_MG` | 1200 mg / 1000–2500 mg | Ngưỡng độ lớn gia tốc để nhận biết cú đặt xuống; giảm để nhận đặt nhẹ, tăng để tránh nhầm rung/lắc lúc đang cầm. |
| `ROBOT_BMI160_RELEASE_DELTA_MG` | 350 mg / 100–1000 mg | Ngưỡng thay đổi gia tốc giữa các mẫu 20 ms để nhận biết cú đặt xuống; giảm nhạy hơn nhưng tăng nguy cơ nhận nhầm. |

Các giá trị trục và ngưỡng BMI160 được giải thích chi tiết kèm trình tự thử tại mục 7. Các mục con của BMI160 chỉ hiện khi `ROBOT_BMI160_ENABLE` được bật. Địa chỉ sai có cơ chế thử địa chỉ thay thế, nhưng trục/hướng lắp sai vẫn làm pitch/roll không đúng mong đợi.

Trong Kconfig, hai lựa chọn địa chỉ được khai báo riêng là `ROBOT_BMI160_ADDRESS_68` và `ROBOT_BMI160_ADDRESS_69`; hai lựa chọn trục là `ROBOT_BMI160_FORWARD_X` và `ROBOT_BMI160_FORWARD_Y`. Mỗi nhóm là lựa chọn loại trừ lẫn nhau trong menu, không phải các công tắc có thể bật đồng thời.

### Head servo

Servo chạy ở 50 Hz. Tư thế lệnh (`pose`) từ -100 đến +100 được ánh xạ thành góc quanh tâm theo công thức: `góc = tâm + chiều * (range × pose / 100)`.

| Tham số | Mặc định / phạm vi | Chức năng / cách chỉnh |
|---|---:|---|
| `ROBOT_HEAD_CENTER_DEG` | 90° / 0–180° | Góc servo tương ứng đầu ở vị trí cân bằng/ngang. Chỉnh để cơ khí nằm đúng giữa khi pose=0. |
| `ROBOT_HEAD_RANGE_DEG` | 35° / 5–90° | Biên độ tính từ tâm tới pose ±100. Giảm nếu cơ cấu chạm giới hạn, kẹt hoặc servo kêu; chỉ tăng sau khi kiểm tra hành trình cơ khí. |
| `ROBOT_HEAD_INVERT` | Tắt | Đảo chiều servo nếu lệnh ngẩng làm đầu cúi hoặc ngược lại. |
| `ROBOT_SERVO_MIN_US` | 500 µs / 400–1000 µs | Độ rộng xung tại góc logic 0°. |
| `ROBOT_SERVO_MAX_US` | 2400 µs / 2000–2600 µs | Độ rộng xung tại góc logic 180°. |

Min/max pulse là hiệu chuẩn của chính servo, khác với `center` và `range`: trước tiên dùng xung phù hợp thông số servo, sau đó chỉnh tâm và biên độ. Hai đầu xung quá rộng có thể làm servo ép vào chặn cơ khí, nóng hoặc hỏng. Nguồn servo sụt áp cũng có thể làm ESP32 reset.

### Behavior

| Tham số | Mặc định / phạm vi | Chức năng / lưu ý |
|---|---:|---|
| `ROBOT_WANDER_ENABLE` | Bật | Bật đi lang thang ngẫu nhiên lúc idle/listening khi khởi động. Đây là trạng thái mặc định; MCP `self.robot.set_wander` vẫn có thể đổi trạng thái lúc chạy. Khi bật, robot vẫn chịu các khóa cảm biến và trạng thái hội thoại. |
| `ROBOT_STOP_WHEN_LISTENING` | Bật | Dừng các bước bánh xe từ hành vi cảm xúc khi đang nghe để giảm tiếng motor lọt vào microphone. Lệnh `self.robot.move` và wander được phép chạy trong listening; motor vẫn dừng khi speaking hoặc khi có khóa an toàn khác. Tắt mục này cho phép cả hành vi cảm xúc chuyển động khi listening, có thể ảnh hưởng nhận dạng giọng nói. |
| `ROBOT_BLIND_REVERSE` | Bật | Cho phép lùi dù không có cảm biến phía sau. Tắt để chặn các lệnh lùi từ cảm xúc/phản xạ; robot sẽ không có khả năng tự lùi tránh vật cản. Mục này không tạo bảo vệ phía sau. |
| `ROBOT_REVERSE_MAX_MS` | 700 ms / 200–3000 ms | Thời gian lùi liên tục tối đa. Khi vượt giới hạn, lùi bị chặn tạm thời rồi bộ đếm được làm mới. Giảm khi robot chạy gần mép phía sau; giới hạn này áp dụng cho lùi nói chung. |
| `ROBOT_EMOTION_MAX_SEC` | 90 s / 5–600 s | Thời gian tối đa một hành vi cảm xúc duy trì trước khi controller kết thúc hành vi và về neutral. Dùng giá trị nhỏ hơn nếu muốn hành vi bánh/đầu không kéo dài; cảm xúc mới hoặc `neutral` cũng có thể thay thế/kết thúc hành vi. |
| `ROBOT_FACE_REACTION` | Bật | Cho phép bộ điều khiển yêu cầu biểu cảm surprised/shocked khi phản xạ laser guard chạy. Tắt chỉ bỏ phản ứng khuôn mặt; không tắt phát hiện hazard hay hành vi tránh. |

Wander và emotion là các chuỗi bước được định nghĩa trong `robot_behaviors.cc`. Bảng này quyết định hành vi cụ thể; các tùy chọn menuconfig ở trên điều khiển lúc chạy, giới hạn thời gian, lùi và phản ứng khuôn mặt. Cờ `ROBOT_STOP_WHEN_LISTENING` chỉ liên quan chuyển động từ emotion khi listening; nó không phải công tắc tắt toàn bộ motor trong lúc nghe.

### Giá trị mặc định và cấu hình đang build

Giá trị `default` ở bảng Kconfig chỉ được dùng khi tạo cấu hình mới hoặc khi người dùng chưa lưu giá trị khác; `sdkconfig` hiện có sẽ ghi đè mặc định. Trong workspace tại thời điểm bổ sung tài liệu, `ROBOT_BASE_ENABLE` bật, VL53L0X tắt, BMI160 bật, min-start PWM 75%, max PWM 90%, đi lang thang bật, stop emotion khi listening bật, lùi bật với giới hạn 700 ms, emotion timeout 90 giây và phản ứng khuôn mặt bật. `ROBOT_PIN_TOF_INT` đang được lưu là GPIO21 trong `sdkconfig`; GPIO38 là mặc định Kconfig, không phải giá trị đang build. Khi cần biết chính xác firmware sắp nạp sẽ dùng gì, đối chiếu `sdkconfig` và log khởi tạo sau khi build.



### CÁC DỰ ĐỊNH NÂNG CẤP TIẾP THEO CHO XIAOZHI ROBOT ####
 Từ những logic nền cơ bản như trên, dự kiến sẽ có những mốc nâng cấp xiaozhi robot lên những bước sau:

 - Điều khiển từ xa bằng RF, wifi, bluetooh hoặc tay game PS.
 - Thêm servo giả lập chân tay cho xiaozhi ra lệnh qua giọng nói
 - Tương tác giữa 2 robot Xiaozhi với nhau.
 ... Các ý tưởng sẽ cập nhật tiếp theo cho project,...