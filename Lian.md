Giao thức Truyền thông CAN Lianming Power Digital Power Module V2.0



1\. Tổng quan và Phạm vi áp dụng



Tài liệu này đặc tả giao thức truyền thông CAN dành cho các dòng module nguồn kỹ thuật số của Lianming Power. Giao thức tuân thủ tiêu chuẩn công nghiệp nhằm đảm bảo khả năng điều khiển chính xác và giám sát trạng thái thời gian thực trong các hệ thống năng lượng lớn.



\* Thông số kỹ thuật lớp dữ liệu:

&#x20; \* Phiên bản CAN: 2.0B.

&#x20; \* Định danh (Identifier): 29-bit (Extended frames).

\* Thiết bị áp dụng:

&#x20; \* Module sạc dòng cao: 10kW, 12kW, 20kW, 30kW.

&#x20; \* Hệ thống nguồn Laser kỹ thuật số.



Lịch sử thay đổi và Phê duyệt



Ngày	Phiên bản	Mô tả thay đổi	Người soạn thảo	Người soát xét	Phê duyệt

2021-04-20	V1.0	Bản thảo đầu tiên	Dai Chang	/	Digital Power Dept

2022-09-19	V2.0	Bổ sung các lưu ý vận hành (Precautions)	Dai Chang	Tang Jianjun	Approved



2\. Giao diện Vật lý (Physical Communication Interface)



Dưới góc độ kỹ thuật hệ thống, giao diện truyền thông được thiết kế để hoạt động ổn định trong môi trường công nghiệp có nhiễu điện từ (EMI) cao:



\* Tốc độ Baud (Baud rate): Cố định 125 kbit/s.

\* Tiêu chuẩn vật lý: Tuân thủ ISO 11898-1:2003.

\* Yêu cầu phần cứng: Bắt buộc sử dụng giao diện CAN cách ly (isolated CAN interface) để bảo vệ các thiết bị điều khiển logic khỏi xung điện áp từ phía động lực.



3\. Cấu trúc Khung truyền (Frame Structure)



Định danh 29-bit được phân bổ để quản lý mã lệnh và địa chỉ vật lý. Cần lưu ý rằng các bit từ 29-31 không được sử dụng trong khung truyền CAN 2.0B mở rộng.



Sơ đồ phân bổ Identifier (29 bits)



Bits	28 - 15	14 - 7	6 - 0

Thành phần	Command ID\_H	Dự phòng / Phân đoạn	Module Address



\* Command ID\_H: Mã định danh lệnh (xác định chức năng bản tin).

\* Module Address (Địa chỉ Module):

&#x20; \* Địa chỉ 0: Lệnh quảng bá (Broadcast). Tất cả module trên bus thực thi nhưng không gửi phản hồi.

&#x20; \* Địa chỉ 1 - 60: Địa chỉ vật lý riêng biệt. Module tương ứng thực thi và bắt buộc gửi phản hồi (Response).



4\. Phân tích chi tiết các mã lệnh (Command Parsing)



4.1. Nhóm mã lệnh 1: Điều khiển và Trạng thái cơ bản



Các lệnh này sử dụng Base ID cơ sở là 0x1907C0xx (trong đó xx là địa chỉ module).



Tên lệnh	Command ID (Gửi)	Module Address	Byte 0 (CMD)	Nội dung Byte 1 - 7

Cài đặt đầu ra	0x1907C0(Addr)	0\~60	0	Byte 1-3: Dòng điện (mA); Byte 4-7: Điện áp (mV)

Đọc thông tin	0x1907C0(Addr)	1\~60	1	Byte 1-7: Dự phòng (điền 0)

Bật/Tắt module	0x1907C0(Addr)	0\~60	2	Byte 6: 0x55 (Bật), 0xAA (Tắt)



\* Lưu ý về dữ liệu: Giá trị cài đặt là số nguyên (Integer) tương ứng với đơn vị mV/mA. Ví dụ: Để cài 100V, gửi giá trị 100,000 (0x186A0).

\* Quy tắc phản hồi: Phản hồi thành công trả về ID 0x1807C0(Addr) với Byte 1 là NZ (Non-Zero), thường là 0xFF. Thất bại trả về 0x00.



4.2. Nhóm mã lệnh 2: Giám sát và Cấu hình hệ thống



Tên lệnh	Command ID	Byte 0	Nội dung phản hồi / Công thức

Đọc áp cài đặt	0x190100(Addr)	/	Giá trị phản hồi = V \* 10

Đọc giới hạn dòng	0x190108(Addr)	/	Giá trị phản hồi = A \* 10

Đọc áp đầu vào	0x1907A0(Addr)	0x31	Byte 2-3: Vab/32; Byte 4-5: Vbc/32; Byte 6-7: Vca/32

Chia tải (Sharing)	0x19C21800	/	Byte 4: 0x55 (Tắt), 0xAA (Bật chia tải tự động)

Tìm địa chỉ	0x19C228(Addr)	/	Byte 3: 0x55 (Nháy đèn xanh trong 10 giây)



5\. Định nghĩa các Bit Trạng thái và Lỗi (Status Flag Bits)



Trạng thái vận hành được trả về trong Byte 7 và Byte 6 của bản tin phản hồi thông tin (CMD=1).



Byte 7: Trạng thái 0 (Status 0)



\* Bit 7: Lỗi áp thấp đầu ra (LED Vàng sáng).

\* Bit 6: Quá áp đầu ra (LED Đỏ sáng).

\* Bit 5: Áp thấp đầu vào (LED Đỏ sáng).

\* Bit 4: Quá áp đầu vào (LED Đỏ sáng).

\* Bit 3: Lỗi quạt (LED Đỏ nháy).

\* Bit 2: Chế độ dòng hằng CC (LED Vàng sáng).

\* Bit 1: Lỗi hệ thống Module (LED Đỏ sáng).

\* Bit 0: Module đang ở trạng thái Off.



Byte 6: Trạng thái 1 (Status 1)



\* Bit 7: Đã nhận lệnh tắt máy.

\* Bit 6: Bảo vệ quá nhiệt (LED Đỏ sáng).

\* Bit 5: Bảo vệ quá dòng (LED Đỏ sáng).

\* Bit 4: Đèn LED Xanh đang nháy (Chế độ Lookup).



QUY TẮC AN TOÀN: Nếu đèn chỉ báo chuyển sang Màu Đỏ Sáng liên tục, module sẽ kích hoạt logic tự động tắt máy để bảo vệ. Các cảnh báo khác (LED Vàng hoặc Nháy) cho phép module tiếp tục hoạt động nhưng cần kiểm tra.



6\. Các ví dụ bản tin cụ thể (Message Examples)



1\. Bật quảng bá (Tất cả module):

&#x20; \* Gửi: ID 0x1907C080, Data: 02 00 00 00 00 00 00 55

&#x20; \* Phản hồi: Không có.

2\. Bật module số 1:

&#x20; \* Gửi: ID 0x1907C081, Data: 02 00 00 00 00 00 00 55

&#x20; \* Nhận: ID 0x1807C081, Data: 02 FF 00 00 00 00 00 00 (Thành công).

3\. Cài đặt Module 3 đầu ra 100V, 90A:

&#x20; \* Gửi: ID 0x1907C083, Data: 00 01 5F 90 00 01 86 A0

&#x20; \* Logic: 0x186A0 = 100,000mV; 0x15F90 = 90,000mA.

&#x20; \* Nhận (Thành công): ID 0x1807C083, Data: 00 FF 00 00 00 00 00 00.

4\. Đọc trạng thái Module 2:

&#x20; \* Gửi: ID 0x1907C082, Data: 01 00 00 00 00 00 00 00

&#x20; \* Nhận: ID 0x1807C082, Data: 01 00 01 17 03 E8 00 00

&#x20; \* Phân tích: Áp = 0x03E8 (1000/10 = 100V); Dòng = 0x0117 (279/10 = 27.9A).

5\. Đọc áp cài đặt Module 1:

&#x20; \* Gửi: ID 0x19010081, Data: (Trống)

&#x20; \* Nhận: ID 0x18010081, Data: 01 00 00 00 00 00 03 84 (900/10 = 90V).

6\. Đọc giới hạn dòng Module 5:

&#x20; \* Gửi: ID 0x19010885, Data: (Trống)

&#x20; \* Nhận: ID 0x18010885, Data: 01 3D 00 00 00 00 00 00 (317/10 = 31.7A).

7\. Đọc áp đầu vào Module 1:

&#x20; \* Gửi: ID 0x1907A081, Data: 31 00 00 00 00 00 00 00

&#x20; \* Nhận: ID 0x1807A081, Data: 31 01 2F 1E 2F 2C 2F 26 (Vab = 0x2F1E/32 = 376.9V).

8\. Đọc nhiệt độ môi trường Module 1:

&#x20; \* Gửi: ID 0x19008081, Data: (Trống)

&#x20; \* Nhận: ID 0x18008081, Data: 00 00 00 00 01 52 00 00 (338/10 = 33.8°C).

9\. Tắt chia tải tự động:

&#x20; \* Gửi: ID 0x19C21880, Data: 00 00 00 00 55 00 00 00 (Byte 4 = 0x55).

10\. Tìm địa chỉ Module 2:

&#x20; \* Gửi: ID 0x19C22882, Data: 00 00 00 55 00 00 00 00 (Module 2 nháy đèn xanh).



7\. Các lưu ý quan trọng (Precautions)



\* Khởi động mềm (Soft Start):

&#x20; \* Theo Hình 6-1, trong giai đoạn khởi động (soft-start phase), đường đặc tính áp sẽ tăng dần. Tuyệt đối không mang tải trong giai đoạn này.

&#x20; \* Chỉ mang tải khi áp đạt trạng thái xác lập (steady-state) - tương ứng với đoạn plateau (bình nguyên) trên dạng sóng. Kiểm tra qua Bảng 6-1: các bit lỗi áp thấp/cao tại Status 0 phải bằng 0.

\* Cài đặt dòng điện:

&#x20; \* Phải tăng dòng theo từng bước (stepwise). Khuyến nghị mỗi bước tăng 10% giá trị tải mục tiêu (ví dụ: 10A -> 20A -> ... -> 100A).

\* Thời gian đáp ứng:

&#x20; \* Thời gian tăng dòng (ramp-up time) tối thiểu phải đặt là 1 giây để đảm bảo ổn định vòng lặp phản hồi.

\* Mất kết nối:

&#x20; \* Nếu trong vòng 2 phút module không nhận được lệnh giám sát, hệ thống sẽ tự động xác định là lỗi ngắt quãng truyền thông.



