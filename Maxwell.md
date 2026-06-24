Giao thức Truyền thông CAN Maxwell MXR Series - Phiên bản V1.50

1\. Tổng quan (Overview)

Tài liệu này quy định chi tiết giao thức truyền thông áp dụng cho các module sạc thuộc dòng Maxwell MXR Series. Giao thức dựa trên chuẩn CAN 2.0B, cho phép thiết bị điều khiển trung tâm giám sát và thiết lập các thông số vận hành của module một cách chính xác.Thông số tốc độ truyền (baud rate) mặc định của hệ thống là:  125 Kbps .

2\. Định nghĩa Định dạng Khung truyền (Definition of Frame Format)

2.1 Định dạng Khung (Frame Format)

Khung truyền là đơn vị cơ bản trong việc trao đổi thông tin. Giao thức sử dụng định dạng khung mở rộng (Extended Frame) của chuẩn CAN 2.0B với cấu trúc như sau:| Mục (Items) | Mã (Code) || ------ | ------ || Start of Frame | sof (1 bit) || Miền Phân xử (Arbitration Domain) | Identifier (11 bits), SRR, IDE, Identifier (18 bits), RTR || Mã Điều khiển (Control Code) | Dự phòng - Reserved (2 bits), Độ dài dữ liệu - Data Len (4 bits) || Miền Dữ liệu (Data Domain) | data (8 bytes) || Mã Kiểm tra (Check Code) | CRC (2 bits) || End of Frame | (7 bits) |

Các phần người dùng có thể can thiệp (Controllable parts):| Identification domain (Miền định danh) | Data Domain (Miền dữ liệu) || ------ | ------ || 29 Bits | 1 - 8 Bytes |

2.2 Định danh Khung - 29 Bits (Frame Identifier)

Định danh khung bao gồm 29 bit, được phân bổ từ bit 28 đến bit 0 để xác định loại gói tin, địa chỉ nguồn và địa chỉ đích:| Bit | 28 - 20 | 19 | 18 - 11 | 10 - 3 | 2 - 0 || ------ | ------ | ------ | ------ | ------ | ------ || Trường | PROTNO | PTP | DSTADDR | SRCADDR | Group |

PROTNO (9 bits):  Mã giao thức. Giá trị mặc định là 0x060.

PTP (1 bit):  Loại hình giao tiếp.

PTP = 1: Giao tiếp điểm-đối-điểm (Point-to-point).

PTP = 0: Giao tiếp quảng bá (Broadcast).

DSTADDR (8 bits):  Địa chỉ đích (Destination Address).

00 \~ 63: Dải địa chỉ của Module sạc.

0xF0: Địa chỉ thiết bị điều khiển (Controller).

0xFF: Quảng bá toàn hệ thống.

0xFE: Quảng bá trong nhóm.

0xFD - Group: Địa chỉ quảng bá cho nhóm mở rộng (từ nhóm index 8 trở lên).

Lưu ý kỹ thuật:  Công thức tính là DSTADDR = 0xFD - Chỉ số nhóm (Group index). Ví dụ: Nhóm index 0 ứng với 0xFD, nhóm index 1 ứng với 0xFC. Hệ thống hỗ trợ tối đa 60 nhóm mở rộng.

SRCADDR (8 bits):  Địa chỉ nguồn (Source Address).

00 \~ 63: Dải địa chỉ module sạc.

0xF0: Địa chỉ thiết bị điều khiển.

Group (3 bits):  Số nhóm module hiện hành, giá trị từ 0 \~ 7.

3\. Miền dữ liệu (Data Domain)

3.1 Cài đặt Tham số Module (Setting module parameters)

Được sử dụng để thiết lập các giá trị vận hành. Định dạng như sau:Định dạng khung truyền (Transmit Frame Data Domain Format):| Byte 0 | Byte 1 | Byte 2 \~ Byte 3 | Byte 4 \~ Byte 7 || ------ | ------ | ------ | ------ || Function code (Mã chức năng) | Reserved (Dự phòng) | Địa chỉ Thanh ghi (Register Number) | Dữ liệu (Data) || 03 | 00 | Xem Bảng 1 | Giá trị thiết lập |

Ví dụ thực tế:

Thiết lập điện áp module (700.0V):  03 00 00 21 44 2F 00 00 (Trong đó 0x442F0000 là giá trị Float IEEE 754 của 700.0).

Thiết lập giới hạn dòng điện:  Giá trị giới hạn được tính theo tỷ lệ phần trăm dựa trên công thức:  $$Giới\\\_hạn\\\_dòng = \\frac{Dòng\\\_điện\\\_yêu\\\_cầu}{Dòng\\\_định\\\_mức\\\_của\\\_module}$$  Ví dụ: Thiết lập giới hạn 50% dòng định mức (giá trị 0.5): 03 00 00 22 3F 00 00 00.

Lệnh Khởi động (Start-up):  03 00 00 30 00 00 00 00

Lệnh Tắt máy (Shut-down):  03 00 00 30 00 01 00 00

3.2 Đọc dữ liệu Module (Read module data)

Khung gửi yêu cầu (Transmit Frame):  10 00 Địa chỉ Thanh ghi 00 00 00 00Khung phản hồi từ Module (Response Frame):  | Byte 0 | Byte 1 | Byte 2 \~ Byte 3 | Byte 4 \~ Byte 7 | | :--- | :--- | :--- | :--- | | Loại dữ liệu | Mã lỗi | Địa chỉ Thanh ghi | Dữ liệu trả về |

Byte 0 (Data Type):  41 (Số dấu phẩy động), 42 (Số nguyên).

Byte 1 (Error Code):  F0 (Bình thường/Thành công). Nếu nhận mã khác, gói tin bị coi là lỗi và phải loại bỏ.

4\. Bảng tra cứu loại dữ liệu - Bảng 1 (Data Type Analysis Table 1)

Địa chỉ Thanh ghi,Mô tả dữ liệu,Định dạng,Ghi chú / Đơn vị

0x0001,Đọc điện áp module,Số dấu phẩy động,V

0x0002,Đọc dòng điện module,Số dấu phẩy động,A

0x0003,Đọc điểm giới hạn dòng điện,Số dấu phẩy động,A

0x0004,Đọc nhiệt độ board DC,Số dấu phẩy động,°C

0x0005,Đọc điện áp pha đầu vào (hoặc DC),Số dấu phẩy động,V

0x0008,Đọc điện áp PFC0 (bus dương),Số dấu phẩy động,V

0x000A,Đọc điện áp PFC1 (bus âm),Số dấu phẩy động,V

0x000B,Đọc nhiệt độ môi trường,Số dấu phẩy động,°C

0x000C,Đọc điện áp AC pha A,Số dấu phẩy động,V

0x000D,Đọc điện áp AC pha B,Số dấu phẩy động,V

0x000E,Đọc điện áp AC pha C,Số dấu phẩy động,V

0x0010,Đọc nhiệt độ board PFC,Số dấu phẩy động,°C

0x0011,Đọc công suất đầu ra định mức,Số dấu phẩy động,W

0x0012,Đọc dòng điện đầu ra định mức,Số dấu phẩy động,A

0x0017,Cài đặt độ cao hoạt động,Số nguyên,mét (1000\~5000)

0x001B,Cài đặt giá trị dòng điện đầu ra,Số nguyên,Giá trị = Dòng điện \* 1024

0x001E,Cài đặt số nhóm module,Số nguyên,"Byte 7, 6 bit thấp (0\~60)"

0x001F,Thiết lập phương thức cấp địa chỉ,Số nguyên,0: Tự động; 0x00010000: Công tắc DIP

0x0020,Cài đặt công suất đầu ra,Số dấu phẩy động,Tỷ lệ 0.1\~1.0 so với định mức

0x0021,Cài đặt điện áp đầu ra,Số dấu phẩy động,V

0x0022,Cài đặt giới hạn dòng điện,Số dấu phẩy động,Tỷ lệ (Ví dụ: 0.5 = 50%)

0x0023,Cài đặt ngưỡng bảo vệ quá áp,Số dấu phẩy động,V (Chỉ dùng khi cần thiết)

0x0030,Lệnh Tắt máy / Khởi động,Số nguyên,0x00010000: Tắt; 0x00000000: Bật

0x0031,Reset lỗi quá áp module,Số nguyên,0x00010000: Reset; 0x00000000: Disable

0x003E,Quyền bảo vệ quá áp đầu ra,Số nguyên,0x00000000: Enable; 0x00010000: Disable

0x0040,Đọc trạng thái cảnh báo hiện tại,Số nguyên,Chi tiết tại Bảng 2

0x0043,Đọc số nhóm và địa chỉ Dial-up,Số nguyên,High 16: Nhóm; Low 16: Địa chỉ

0x0044,Reset lỗi ngắn mạch,Số nguyên,0x00010000: Reset

0x0046,Cài đặt chế độ đầu vào,Số nguyên,1: AC (mặc định); 2: DC

0x0048,Đọc công suất đầu vào,Số nguyên,W

0x004A,Đọc giá trị độ cao đã cài đặt,Số nguyên,mét

0x004B,Đọc chế độ làm việc đầu vào thực tế,Số nguyên,1: 1-ph AC; 2: DC; 3: 3-ph AC

0x0054,Đọc Serial Number (Low bits),Số nguyên,

0x0055,Đọc Serial Number (High bits),Số nguyên,

0x0056,Đọc phiên bản phần mềm DCDC,Số nguyên,Low 16 bits (Hệ thập phân)

0x0057,Đọc phiên bản phần mềm PFC,Số nguyên,Low 16 bits (Hệ thập phân)

5\. Bảng trạng thái cảnh báo Module - Bảng 2 (Module Alarm Status Table)

Quy ước: 0 là Không khả dụng (Invalid), 1 là Khả dụng (Valid).| Bit | Diễn giải (Illustration) | Bit | Diễn giải (Illustration) || ------ | ------ | ------ | ------ || 0 | Lỗi module (Đèn đỏ sáng) | 16 | Lỗi truyền thông CAN || 1 | Bảo vệ module (Đèn vàng sáng) | 17 | Mất cân bằng dòng điện || 2 | Dự phòng (Reserved) | 18-21 | Dự phòng (Reserved) || 3 | Lỗi giao tiếp SCI nội bộ | 22 | Trạng thái DCDC (0: Bật, 1: Tắt) || 4 | Lỗi phát hiện chế độ đầu vào | 23 | Giới hạn công suất module || 5 | Chế độ đầu vào không khớp | 24 | Giảm tải theo nhiệt độ || 6 | Dự phòng (Reserved) | 25 | Giới hạn công suất AC || 7 | Quá áp DCDC | 26 | Dự phòng (Reserved) || 8 | Điện áp PFC bất thường | 27 | Lỗi quạt || 9 | Quá áp AC | 28 | Ngắn mạch DCDC || 10-13 | Dự phòng (Reserved) | 29 | Dự phòng (Reserved) || 14 | Thấp áp AC | 30 | Quá nhiệt DCDC || 15 | Dự phòng (Reserved) | 31 | Quá áp đầu ra DCDC |

6\. Lệnh thiết lập Số nhóm và Địa chỉ (Commands Setting)

Hệ thống hỗ trợ hai phương thức cấp phát:

Chế độ Cấp phát tự động (Automatic):  Module sử dụng vị trí công tắc DIP để xác định số nhóm. Lệnh thiết lập qua CAN sẽ bị vô hiệu hóa và trả về mã lỗi F2.

Chế độ Thiết lập bằng tay (Dial setting):  Cho phép ghi số nhóm vào bộ nhớ EEPROM của module thông qua lệnh CAN.Ví dụ thiết lập:

Thiết lập số nhóm module (ví dụ nhóm 5):  03 00 00 1E 00 00 00 05

Thiết lập sang chế độ Dial mode:  03 00 00 1F 00 01 00 00

7\. Hướng dẫn Dữ liệu dấu phẩy động (IEEE 754)

Dữ liệu dấu phẩy động được truyền dưới dạng HEX-ASCII theo tiêu chuẩn IEEE 754 32-bit.Cấu trúc 32 bit:

D31:  Bit dấu (S) - 0: Dương, 1: Âm.

D30 - D23:  Phần mũ (Exponent - E).

D22 - D0:  Phần định trị (Mantissa - M).Công thức tính:   $$Giá\\\_trị = \\pm(1 + M \\times 2^{-23}) \\times 2^{(E - 127)}$$Ví dụ chuyển đổi:

5.0:  Hex 40 A0 00 00. Chuyển sang nhị phân, ta có  $E = 129$ ,  $S = 0$ ,  $M = 0.25$ . Tính:  $(1 + 0.25) \\times 2^{(129-127)} = 1.25 \\times 4 = 5.0$ .

60.0:  Hex 42 70 00 00.

1.2:  Hex 3F 99 99 9A.

8\. Hướng dẫn Giảm tải theo Độ cao (Altitude Derating)

Khi vận hành ở độ cao lớn, mật độ không khí loãng làm giảm hiệu quả tản nhiệt. Module cần được thiết lập độ cao thực tế để tự động điều chỉnh ngưỡng bảo vệ.

Dải hiệu dụng:  1000m đến 5000m.

Khuyến nghị:  Cần thiết lập giá trị thực tế khi module hoạt động trên 1000m để đảm bảo độ bền linh kiện. Dưới 1000m, module chạy ở công suất định mức mặc định.Ví dụ thiết lập độ cao hoạt động (3000m):  Lệnh gửi: 03 00 00 17 00 00 0B B8 (Với 0x0BB8 = 3000).





