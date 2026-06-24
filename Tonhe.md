Giao thức truyền thông CAN giữa Mô-đun sạc và Bộ giám sát - Phiên bản V1.3 (TonHe)



1\. Tổng quan và Phạm vi áp dụng



Tài liệu này quy định chi tiết giao thức truyền thông giữa bo mạch điều khiển chính (bộ giám sát) và các mô-đun sạc thành phần trong hệ thống bộ sạc kép. Giao thức được thiết kế để đảm bảo tính đồng bộ trong việc truyền tải dữ liệu trạng thái, thiết lập thông số vận hành và quản lý lỗi hệ thống một cách tin cậy.



2\. Quy tắc chung và Lớp vật lý



Hệ thống tuân thủ các đặc tính kỹ thuật lớp vật lý và quy tắc truyền dẫn sau:



\* Tiêu chuẩn tuân thủ: ISO 11898-1:2003 (Data link layer and physical signaling) và SAE J1939-11:2006.

\* Tốc độ truyền dữ liệu (Baud rate): 125 kbit/s.

\* Kiểu giao tiếp: Sử dụng giao diện CAN cô lập (isolated CAN interface).

\* Thứ tự truyền dữ liệu: Định dạng byte thấp trước (low post format/little-endian). Đối với các giá trị đa byte, byte có trọng số thấp nhất sẽ được gửi trước trên bus.



3\. Lớp liên kết dữ liệu (Data Link Layer)



Giao thức sử dụng khung dữ liệu mở rộng 29-bit (Extended Frame). Cấu trúc Đơn vị dữ liệu giao thức (PDU) được phân bổ theo chuẩn SAE J1939-21:



Trường dữ liệu	Vị trí Bit	Độ dài (Bit)	Ý nghĩa

Priority (P)	28 - 26	3	Độ ưu tiên bản tin (0: cao nhất, 7: thấp nhất).

Reservation (R)	25	1	Bit dự phòng (mặc định = 0).

Data Page (DP)	24	1	Trang dữ liệu (mặc định = 0).

PDU Format (PF)	23 - 16	8	Xác định định dạng PDU và mã PGN.

Specific PDU (PS)	15 - 8	8	Địa chỉ đích (Target Address) - Định dạng PDU1.

Source Address (SA)	7 - 0	8	Địa chỉ thiết bị gửi.

Data Domain	-	0-64	Vùng dữ liệu (8 bytes).



4\. Định danh địa chỉ mạng (Addressing)



Địa chỉ mạng (IP) được quy định để đảm bảo tính duy nhất của thiết bị trên bus CAN:



Thiết bị	Địa chỉ (Hex)	Ghi chú

Bộ điều khiển chính (Monitor)	0xA0	Cố định.

Mô-đun sạc (Charging Module)	0x01 - 0xF0	Dải địa chỉ 1 - 240.

Địa chỉ quảng bá (Broadcast)	0xFF	Gửi đến tất cả các mô-đun.



5\. Phân loại tin nhắn (Message Classification)



5.1. Bản tin Up-link (Mô-đun -> Bộ điều khiển)



Mã bản tin	Mô tả	PGN (Hex)	Ưu tiên	Độ dài	Chế độ gửi

M\_C\_1	Trạng thái mô-đun sạc	000100H	6	8 Bytes	500ms + Theo sự kiện

M\_C\_2	Xác nhận lệnh cụ thể	000200H	2	8 Bytes	Theo sự kiện (Trigger)

M\_C\_3	Thông tin trao đổi (AC/Temp)	000B00H	6	8 Bytes	500ms

M\_C\_4	Thông tin trạng thái mở rộng	009100H	7	8 Bytes	500ms + Theo sự kiện



5.2. Bản tin Down-link (Bộ điều khiển -> Mô-đun)



Mã bản tin	Mô tả	PGN (Hex)	Ưu tiên	Độ dài	Chế độ gửi

C\_M\_1	Lệnh Start/Stop (Quảng bá)	000300H	2	8 Bytes	Theo sự kiện

C\_M\_2	Cài đặt tham số (Quảng bá)	000400H	4	8 Bytes	Theo sự kiện

C\_M\_3	Lệnh định thời (Timing)	000500H	6	8 Bytes	5000ms

C\_M\_12	Chọn chế độ cài đặt địa chỉ	009000H	7	8 Bytes	Theo sự kiện

C\_M\_23	Cài đặt địa chỉ cụ thể	000900H	6	8 Bytes	Theo sự kiện

C\_M\_24	Lệnh Start/Stop \& Set (Cụ thể)	000600H	2	8 Bytes	Theo sự kiện



6\. Chi tiết định dạng bản tin Up-link



6.1. Trạng thái mô-đun sạc (M\_C\_1)



\* Byte 1: Trạng thái hoạt động (0x00: OFF, 0x01: ON, 0x11: OFF do lỗi).

\* Byte 2-3: Điện áp đầu ra (Tỷ lệ 0.1V/bit, dải 0-1100V).

\* Byte 4-5: Dòng điện đầu ra (Tỷ lệ 0.01A/bit, dải 0-200A).

\* Byte 6-7: Cờ Fault/Warning (Chi tiết tại Mục 7).

\* Byte 8: Lỗi PFC (Chi tiết tại Mục 7).



6.2. Thông tin trao đổi (M\_C\_3)



\* Byte 1-2 / 3-4 / 5-6: Điện áp pha A / B / C (Tỷ lệ 0.1V/bit).

\* Byte 7-8: Nhiệt độ môi trường. Định dạng số nguyên 16-bit (Signed Integer). Ví dụ: 18 00 = 24°C.



7\. Bảng mã lỗi và Cảnh báo chi tiết



7.1. Lỗi và Cảnh báo (Byte 6-7 của M\_C\_1)



Bit	Mô tả lỗi	Bit	Mô tả lỗi

0	Điện áp đầu vào thấp	8	Bất thường đường Bus (Bus exception)

1	Mất pha đầu vào	9	Lỗi giao tiếp SCI

2	Quá áp đầu vào	10	Lỗi mạch xả (Discharge fault)

3	Quá áp đầu ra	11	PFC tắt do bất thường

4	Quá dòng đầu ra	12	Cảnh báo điện áp đầu ra thấp

5	Nhiệt độ mô-đun cao	13	Cảnh báo quá áp đầu ra

6	Lỗi quạt	14	Giới hạn công suất do nhiệt độ cao

7	Lỗi phần cứng mô-đun	15	Lỗi ngắn mạch



Quy tắc logic lan truyền lỗi:



1\. Nếu bất kỳ lỗi nào từ Bit 8 đến Bit 13 xảy ra, Bit 7 (Lỗi phần cứng) sẽ tự động được thiết lập lên 1.

2\. Nếu Bit 2 hoặc Bit 7 của lỗi PFC (xem bên dưới) xảy ra, Bit 7 của bản tin trạng thái chung này cũng sẽ được thiết lập lên 1.



7.2. Lỗi PFC (Byte 8 của M\_C\_1)



Bit	Mô tả lỗi PFC	Bit	Mô tả lỗi PFC

0	Quá dòng đầu vào	4	Xung đột địa chỉ

1	Lỗi tần số lưới	5	Lệch áp Bus (Bus bias)

2	Mất cân bằng lưới	6	Lỗi pha bất thường

3	Lỗi DCTz	7	Quá áp đường Bus



8\. Chi tiết định dạng bản tin Down-link



8.1. Điều khiển vận hành (C\_M\_1, C\_M\_2, C\_M\_24)



\* Lệnh Start/Stop: 55H (Stop), AAH (Start).

\* Cài đặt tham số: Điện áp (0.1V/bit), Dòng điện (0.01A/bit).

\* Ràng buộc vận hành (Clamping Logic): Nếu giá trị cài đặt từ bộ giám sát vượt quá dải giới hạn vật lý của mô-đun, mô-đun sẽ tự động giới hạn (clamp) và hoạt động tại giá trị Max hoặc Min cho phép.



8.2. Quản lý địa chỉ (Addressing Logic)



\* Chọn chế độ (C\_M\_12): 0: Tự động (Automatic), 1: Thủ công (Manual).

&#x20; \* Lưu ý: Mô-đun chỉ chấp nhận lệnh thay đổi chế độ địa chỉ khi đang ở trạng thái Power-off (Standby).

\* Cài đặt thủ công (C\_M\_23): Dữ liệu byte 1 chứa địa chỉ mới (1-240). Giá trị được lưu vào EEPROM.

\* Cờ xử lý bản tin (Message Processing Flag):

&#x20; \* Sử dụng "Address Multiple" (Bội số địa chỉ) tại Byte 5 để quản lý các nhóm 24 mô-đun.

&#x20; \* Thuật toán: Nếu địa chỉ thực tế là n, bội số là m, mô-đun kiểm tra bit tại vị trí (n - m\*24 - 1) trong trường Flag (Byte 1-3).

&#x20; \* Ví dụ: Address multiple = 0 quản lý địa chỉ 1-24; Multiple = 1 quản lý 25-48.



9\. Ví dụ bản tin cụ thể (Use Cases)



\* Ví dụ 1: Mô-đun 1 báo cáo trạng thái (400V, 100A, không lỗi)

&#x20; \* Mã Hex: 1801A001 01 A0 0F 10 27 00 00 00

&#x20; \* Phân tích: 1801A001 (ID: Priority 6, PGN 000100H, Đích A0, Nguồn 01). Dữ liệu: 01 (ON), A0 0F (400.0V), 10 27 (100.00A).

\* Ví dụ 2: Bộ điều khiển lệnh Start cho các mô-đun 1, 2 và 3

&#x20; \* Mã Hex: 0803FFA0 07 00 00 AA 00 00 00 00

&#x20; \* Phân tích: 0803FFA0 (Lệnh quảng bá từ A0). Byte 1 = 07 (Binary 00000111, kích hoạt bit cho địa chỉ 1, 2, 3). Byte 4 = AA (Start).

\* Ví dụ 3: Cài đặt tham số (400V, 100A) cho mô-đun 1-3

&#x20; \* Mã Hex: 1004FFA0 07 00 00 00 A0 0F 10 27

&#x20; \* Phân tích: Byte 1 = 07 (Chọn mô-đun 1-3). Byte 5-6 = A0 0F (400.0V). Byte 7-8 = 10 27 (100.00A).

\* Ví dụ 4: Mô-đun 1 xác nhận đã nhận lệnh Start

&#x20; \* Mã Hex: 0802A001 01 00 00 00 00 00 00 00

&#x20; \* Phân tích: Byte 1 = 01 (Xác nhận đã nhận lệnh thành công).



