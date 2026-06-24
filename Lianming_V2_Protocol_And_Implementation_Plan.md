# Lianming V2.0 CAN Protocol - Ghi ch? hi?u t?i li?u v? k? ho?ch t?ch h?p

## 1. M?c ti?u c?a t?i li?u n?y

T?i li?u n?y t?ng h?p l?i ? ngh?a th?c t? c?a file PDF `Lianming Power Digital Power Module CAN Communication Protocol V2.0.pdf`, ??i chi?u v?i source code hi?n t?i trong repo, v? bi?n ph?n hi?u bi?t ?? th?nh m?t plan tri?n khai r? r?ng ?? b? sung code v?o project.

Ph?m vi ??c g?m:

- PDF giao th?c Lianming V2.0.
- PDF giao th?c Maxwell V1.50 ?? ??i chi?u.
- Source code trong `charger/`, `firmware/`, v? `pc_app/`.

## 2. K?t lu?n nhanh

1. Giao th?c c?a Lianming V2.0 d?ng CAN 2.0B extended frame, 29-bit ID, baudrate 125 kbit/s.
2. C?ch ??ng g?i frame trong source hi?n t?i ?? ?i ??ng h??ng, ??c bi?t ? nh?nh `charger/App/Src/maxwell_charger.c`.
3. Code hi?n t?i ?? c? khung ?i?u khi?n ?? ?? ch?y m?t module ho?c nhi?u module, nh?ng v?n c?n thi?u c?c ph?n quan tr?ng c?a protocol g?c, nh?t l? c?c l?nh ??c/ghi m? r?ng, chu?n h?a status report, v? ph?n ?nh x? d? li?u cho HMI/PC.
4. Nh?nh `charger/` l? nh?nh g?n v?i lu?ng ch?y th?c t? h?n. Nh?nh `firmware/` tr?ng gi?ng demo/di s?n c?, c? nhi?u API v? gi? ??nh kh?ng kh?p v?i ki?n tr?c hi?n t?i.

## 3. Hi?u protocol Lianming V2.0

### 3.1 Frame CAN

29-bit identifier ???c chia nh? sau:

- `PROTNO`: 9 bit, m?c ??nh `0x060`.
- `PTP`: 1 bit, `1` l? point-to-point, `0` l? broadcast.
- `DSTADDR`: 8 bit, ??a ch? ??ch.
- `SRCADDR`: 8 bit, ??a ch? ngu?n.
- `Group`: 1 bit, nh?m.

Trong t?i li?u Lianming:

- ??a ch? module: `0x00` ??n `0x3F`.
- ??a ch? controller c? ??nh: `0xF0`.
- Broadcast: `0xFF`.
- Broadcast trong nh?m: `0xFE`.
- Extended group broadcast c? d?i ri?ng, nh?ng hi?n t?i project ch?a c?n ngay.

### 3.2 C?u tr?c payload

#### L?nh ghi / set

Format chu?n:

- Byte 0: function code `0x03`
- Byte 1: reserved `0x00`
- Byte 2-3: register number
- Byte 4-7: data

#### L?nh ??c

Format chu?n:

- Byte 0: function code `0x10`
- Byte 1: reserved `0x00`
- Byte 2-3: register number
- Byte 4-7: reserved `0x00`

#### Ki?u d? li?u

- Float 32-bit IEEE754, 4 byte, big-endian tr?n CAN.
- Integer 32-bit, 4 byte, big-endian tr?n CAN.

### 3.3 Quy ??c response

- Byte 0: ki?u d? li?u tr? v?
  - `0x41`: float
  - `0x42`: int
- Byte 1: error code
  - `0xF0`: OK
  - `0xF2`: fail
- Byte 2-3: register number
- Byte 4-7: data

### 3.4 Register quan tr?ng theo t?i li?u

Nh?m register ?ang quan tr?ng nh?t cho project n?y:

- `0x0001`: ?i?n ?p output.
- `0x0002`: d?ng output.
- `0x0003`: current limit point.
- `0x0004`: nhi?t ?? board DCDC.
- `0x000B`: nhi?t ?? m?i tr??ng.
- `0x0011`: c?ng su?t ??nh m?c.
- `0x0012`: d?ng ??nh m?c.
- `0x0020`: set output power.
- `0x0021`: set output voltage.
- `0x0022`: set current limit.
- `0x0023`: set OVP upper limit.
- `0x0030`: start / shutdown.
- `0x0040`: alarm/status bits.
- `0x0043`: group number + dial address.
- `0x0044`: short circuit reset.
- `0x0046`: input mode.
- `0x0048`: input power.
- `0x004A`: altitude setting.
- `0x004B`: current input working mode.
- `0x0054` - `0x0057`: serial/version registers.

### 3.5 Alarm bits quan tr?ng

C?c bit th?c s? c?n quan t?m cho ?i?u khi?n an to?n:

- `bit 0`: module fault.
- `bit 1`: module protection.
- `bit 3` ho?c `bit 6` t?y b?n t?i li?u c?/m?i: SCI communication failure.
- `bit 7` / `bit 8` t?y quy ??c document: DCDC overvoltage, PFC abnormal.
- `bit 14`: AC undervoltage.
- `bit 16`: CAN communication failure.
- `bit 22`: DCDC on/off status.
- `bit 27`: fan failure.
- `bit 28`: short circuit.
- `bit 30`: over temperature.
- `bit 31`: output overvoltage.

Trong source hi?n t?i, `MXR_ALARM_CRITICAL_MASK` ?? gom ??ng nh?m alarm nghi?m tr?ng ?? emergency stop.

### 3.6 Quy t?c v?n h?nh r?t ra t? t?i li?u

1. Kh?ng ???c ??t t?i tr?c ti?p khi soft start ch?a ?n ??nh.
2. Current setpoint n?n t?ng theo b??c, kh?ng nh?y ??t ng?t.
3. N?u module kh?ng nh?n ???c command monitor trong m?t kho?ng th?i gian d?i th? coi nh? m?t li?n l?c. T?i li?u Lianming ghi kho?ng 2 ph?t, c?n source hi?n t?i ?ang d?ng timeout n?i b? ng?n h?n ?? ph?t hi?n s?m.
4. D? li?u tr? v? c?a voltage/current c?n ???c parse theo big-endian CAN, nh?ng hi?n th? l?n GUI/PC theo format ng??i d?ng hi?u ???c.

## 4. ??i chi?u v?i source code hi?n t?i

### 4.1 Nh?nh `charger/`

#### `charger/App/Inc/maxwell_charger.h` v? `charger/App/Src/maxwell_charger.c`

?i?m ?? l?m ??ng:

- C? helper build 29-bit ID ??ng layout c?a protocol.
- C? encode/decode float v? uint32 theo big-endian.
- C? state machine cho t?ng module.
- C? poll round-robin v? auto recovery khi m?t li?n l?c.
- C? t?ng h?p tr?ng th?i h? th?ng qua `MXR_GetSystemSummary()`.

?i?m c?n thi?u so v?i t?i li?u:

- Ch?a c? ??y ?? register m? r?ng nh? `0x001B`, `0x001E`, `0x001F`, `0x0031`, `0x003E`, `0x0043`, `0x0044`, `0x0046`, `0x004A`, `0x004B`, `0x0054` - `0x0057`.
- Ch?a c? API c?p cao cho group broadcast, address lookup, input mode, altitude, serial/version.
- Keep-alive ?ang l? policy firmware n?i b?, ch?a map tr?c ti?p sang timing khuy?n ngh? trong t?i li?u.

#### `charger/App/Inc/pc_protocol.h` v? `charger/App/Src/pc_protocol.c`

?i?m ?? l?m ??ng:

- C? frame format ri?ng cho PC qua USB CDC.
- C? CRC8 poly `0x07`.
- C? l?nh `SET_VOLTAGE`, `SET_CURRENT`, `START`, `STOP`, `SET_MODULE_ADDR`, `PING`, `EMERGENCY_STOP`.
- C? status report ??nh k?.

?i?m c?n ch?nh:

- `PC_CMD_READ_REG` ?? khai b?o ? header nh?ng ch?a implement trong `pc_protocol.c`.
- Comment trong header ghi status report 31 bytes, nh?ng layout struct th?c t? l? 29 bytes v? kh?p v?i PC app hi?n t?i.
- Tr??ng `btn_start` v? `btn_stop` ?ang ???c gi? 0, ch?a l?y t? GPIO th?t.

#### `charger/App/Src/app_charger.c`

??y l? lu?ng main app th?c s?.

- `App_Init()` kh?i t?o CAN, init driver, add module m?c ??nh.
- `App_Loop()` g?i `MXR_Process()`, g?i status v? PC, ??c n?t nh?n, v? c?p nh?t LED.
- `App_CAN_RxCallback()` chuy?n frame CAN v?o driver.

Ghi ch? quan tr?ng:

- Code hi?n t?i ?ang l? single-module m?c ??nh, ch?a c? c? ch? c?u h?nh nhi?u module t? PC ho?c HMI.
- Ph?n mock CAN2 ch? l? scaffolding cho test, ch?a ph?i s?n ph?m cu?i.

#### `charger/Core/`

- `main.c` g?i `App_Init()` v? ch?y `App_Loop()`.
- `can.c` c?u h?nh CAN1/CAN2 theo CubeMX hi?n t?i.
- `stm32f4xx_it.c` ??y RX interrupt v? callback app.

L?u ? h? t?ng:

- CAN1 hi?n ?ang ?i tr?n PD0/PD1 trong CubeMX, kh?ng ph?i PB8/PB9 nh? ph?n t?i li?u c? ho?c demo kh?c.
- USB CDC ?? c? ???ng nh?n byte v? b?m v?o `PC_Protocol_FeedByte()`.

### 4.2 Nh?nh `firmware/`

Nh?nh n?y c? c?c file demo v? driver c?, v? d?:

- `firmware/Drivers/Maxwell/maxwell_charger.*`
- `firmware/App/Demo/*`

N? h?u ?ch ?? tham kh?o ? t??ng, nh?ng kh?ng n?n d?ng l?m baseline ch?nh cho ph?n t?ch h?p m?i, v? c? nhi?u API v? gi? ??nh kh?ng c?n kh?p v?i `charger/`.

### 4.3 PC app

`pc_app/charger_demo.py` hi?n parse `PC_StatusReport_t` theo 29 byte, kh?p v?i struct th?c t? c?a nh?nh `charger/`.

?i?u n?y x?c nh?n r?ng:

- payload status ph?i gi? ?n ??nh.
- n?u ??i layout struct th? ph?i ??ng b? c? firmware l?n PC app.

## 5. Gaps c?n b? sung v?o project

### 5.1 Protocol layer

- Ho?n thi?n map register theo t?i li?u Lianming.
- Chu?n h?a helper build frame ID v? helper encode/decode d? li?u.
- B? sung parse/response cho m?i lo?i register c?n d?ng.
- Th?m support cho broadcast/group broadcast n?u ph?n c?ng sau n?y c?n.

### 5.2 Device manager

- Cho ph?p c?u h?nh danh s?ch module r? r?ng thay v? c?ng m?t module m?c ??nh.
- T?ch l?p qu?n l? module kh?i l?p giao ti?p CAN.
- C? tr?ng th?i per-module: online, running, fault, offline, recovering.
- C? counter v? log cho timeout, retry, recovery.

### 5.3 Safety and control policy

- Ramp current theo b??c khi start.
- Ch? cho ph?p start khi voltage ?? ?n ??nh.
- Emergency stop ngay khi alarm critical xu?t hi?n.
- C?u h?nh timeout theo tham s?, kh?ng hardcode m?t gi? tr? duy nh?t.

### 5.4 PC / HMI integration

- Th?m l?nh `READ_REG` ? PC protocol n?u c?n debug s?u.
- ??ng b? status report v?i n?t nh?n th?t, alarm th?t, v? module count th?t.
- Cho ph?p PC set nhi?u module ho?c profile c?u h?nh theo nh?m.

### 5.5 Verification

- C? mock CAN loopback ?? test logic m? kh?ng c?n module th?t.
- C? checklist ?? x?c nh?n t?ng register v?i thi?t b? th?t.
- C? log UART cho frame TX/RX v? state transition.

## 6. Plan chi ti?t ?? b? sung code

### Phase 1 - Ch?t protocol canonical

M?c ti?u:

- Ch?n `Lianming V2.0` l?m ngu?n s? th?t cho register map v? frame layout.
- D?n l?i constant v? enum ?? m?i module d?ng chung m?t chu?n.

Vi?c c?n l?m:

- Gom c?c constant protocol v?o m?t header chu?n.
- Ghi r? mapping register n?o ?ang d?ng th?t, register n?o ?? sau.
- Chu?n h?a status bit definitions theo t?i li?u m?i nh?t.

Deliverable:

- M?t b? constant v? comment duy nh?t cho protocol.
- M?t b?ng register d?ng ???c ngay cho team firmware v? PC.

### Phase 2 - Ho?n thi?n driver Maxwell/Lianming

M?c ti?u:

- Driver c? th? set, read, parse, v? gi?m s?t module theo ??ng t?i li?u.

Vi?c c?n l?m:

- B? sung API cho register c?n thi?u.
- Vi?t helper ??c/ghi integer, float, broadcast, group broadcast.
- B? sung parse cho response status, alarm, input mode, version.
- Chu?n h?a timeout v? retry th?nh tham s? c?u h?nh.

Deliverable:

- `maxwell_charger` tr? th?nh m?t module protocol ??y ??, kh?ng ch? l? demo start/stop.

### Phase 3 - M? r?ng app control

M?c ti?u:

- Cho `App_Loop()` qu?n l? nhi?u module v? tr?ng th?i an to?n t?t h?n.

Vi?c c?n l?m:

- Th?m c?u h?nh module list.
- Th?m profile setpoint theo t?ng module ho?c theo nh?m.
- N?ng c?p LED / button / fault policy theo tr?ng th?i t?ng h?p.
- C?p nh?t status report ?? ph?n ?nh module online, fault, charging, alarm.

Deliverable:

- Firmware ?? d?ng cho bench test v? ch?y module th?t theo profile.

### Phase 4 - Ho?n thi?n PC protocol

M?c ti?u:

- PC app c? th? ?i?u khi?n, ??c tr?ng th?i, v? debug protocol t?t h?n.

Vi?c c?n l?m:

- Implement `READ_REG`.
- Th?m ph?n h?i chi ti?t cho c?c l?i CAN/CRC/l?i tham s?.
- ??ng b? struct status v?i UI v? data logger.
- C?n nh?c th?m command ?? n?p danh s?ch module ho?c group.

Deliverable:

- PC app kh?ng ch? start/stop m? c?n debug ???c t?ng register.

### Phase 5 - Test bench v?i mock v? module th?t

M?c ti?u:

- X?c nh?n m?i frame TX/RX ??ng tr??c khi ch?y tr?n module th?t.

Vi?c c?n l?m:

- B?t l?i mock CAN2 ?? ki?m tra lu?ng kh?ng hardware.
- So kh?p d? li?u g?i ?i v?i v? d? trong PDF Lianming.
- Test t?ng b??c: set voltage, set current limit, start, poll, stop, emergency stop.
- Ki?m tra alarm critical v? timeout recovery.

Deliverable:

- B? test bench t?i thi?u cho bring-up v? regression.

### Phase 6 - D?n s?ch v? ch?t t?i li?u k? thu?t

M?c ti?u:

- Khi code ?? ?n, t?i li?u ph?i ph?n ?nh ??ng code.

Vi?c c?n l?m:

- C?p nh?t README / project document.
- Ch?nh comment sai s? byte c?a status report.
- X?a ho?c ??nh d?u r? c?c file demo c? kh?ng c?n l? path ch?nh.

Deliverable:

- Repo c? m?t ???ng ch?y ch?nh, m?t giao th?c ch?nh, v? m?t t?i li?u ch?nh.

## 7. Th? t? tri?n khai khuy?n ngh?

1. Ch?t canonical protocol v? register map.
2. B? sung driver register m? r?ng.
3. Chu?n h?a status report v? PC protocol.
4. M? r?ng app control cho multi-module v? safety policy.
5. Test v?i mock.
6. Test v?i module th?t.

## 8. Ghi ch? k? thu?t quan tr?ng

- Kh?ng n?n gi? ??ng th?i nhi?u phi?n b?n protocol comment kh?c nhau trong source m? kh?ng ch? r? phi?n b?n n?o ?ang l? chu?n.
- N?u thay ??i layout `PC_StatusReport_t`, ph?i c?p nh?t lu?n `pc_app/charger_demo.py`.
- N?u thay ??i CAN pinout ho?c CubeMX config, ph?i c?p nh?t c? `main.c`, `can.c`, v? comment t?i li?u.
- D? li?u CAN c?a Lianming v? Maxwell r?t g?n nhau, nh?ng v?n c?n ?u ti?n PDF n?o ?ang kh?p ??ng v?i module th?c t? tr?n b?n test.
## 9. Ke hoach trien khai tiep theo (da co nen tang)

> Trang thai hien tai: core driver da tach, Maxwell va Lianming da chay chung qua CHG API, READ_REG da co snapshot debug, va build hien tai dang xanh.
> Phan tiep theo khong lam lai nen tang, ma tap trung vao chuan hoa protocol, hoan thien safety policy, va dong goi thanh mot bo thu vien co the dung cho nhieu module sac.

### 9.1 Sprint A - Chot canonical contract

Muc tieu:

- Chot mot bo quy uoc duy nhat cho CAN frame, register map, va type mapping.
- Loai bo cac ten goi song song giua Maxwell va Lianming o tang chung.

Cong viec:

- Tao mot bang quyen chieu 1-1 giua register Lianming va nhung register/field dang duoc dung trong core.
- Danh dau ro register nao co gia tri doc, register nao chi set, register nao chi dung cho debug.
- Chuan hoa enum state, alarm mask, va type code cho response.
- Ranh gioi ro giua protocol chung va chi tiet vendor adapter.

Output:

- Mot file protocol contract duy nhat co the dung lam tai lieu tham chieu cho firmware, PC app, va test bench.

Tien ich ky thuat:

- Giam nguy co code moi them register nhung khong co ai biet version nao la dung.
- Cho phep thay dong gop tu nhieu backend ma khong pha vo format chung.

### 9.2 Sprint B - Hoan thien driver layer

Muc tieu:

- Tang do day cua driver cho tung vendor, nhung van giu API cap cao o CHG layer.

Cong viec:

- Hoan thien wrapper doc/ghi register cho cac field con thieu cua Lianming.
- Tach ham encode/decode chung thanh util protocol de 2 backend cung dung.
- Lam ro trang thai STARTING / RUNNING / FAULT / RECOVERING trong tung driver.
- Bo sung timeout, retry, va soft-recovery thanh tham so cau hinh thay vi hardcode.
- Bo sung kiem tra input hop le truoc khi gui len bus.

Acceptance criteria:

- Moi driver co the dua ra mot CHG_ModuleView_t day du va nhat quan sau moi chu ky poll.
- Build van xanh voi ca hai backend duoc register.
- Khong con helper trung lap ve build ID va endian conversion.

### 9.3 Sprint C - Mo rong app orchestration

Muc tieu:

- Bien app layer thanh mot bo dieu khien da module co the cau hinh duoc, khong phu thuoc vao mot module mac dinh.

Cong viec:

- Bo sung co che khai bao danh sach module theo profile.
- Cho phep khoi tao theo preset: mot module, nhieu module, hoac nhom module.
- Tach logic nut nhan, LED, va fault policy thanh cac ham don nhiem.
- Bao ve start/stop theo trang thai hien tai cua tung module.
- Thiet lap quy tac khi module fault thi module khac khong bi reset vo can cu.

Acceptance criteria:

- App co the khoi dong voi 1 cau hinh nho va 1 cau hinh nhieu module ma khong can sua code logic.
- LED va status report phan anh dung trang thai tong hop.

### 9.4 Sprint D - Hoan thien PC/HMI contract

Muc tieu:

- Khac phuc tinh trang PC chi start/stop, de debug register va cau hinh module ro rang hon.

Cong viec:

- Hoan thien PC_CMD_READ_REG thanh response co type, module index, register, va payload.
- Bo sung error code ro rang cho bad param, unsupported reg, va driver mismatch.
- Dong bo pc_app/charger_demo.py voi format moi.
- Neu can, them lenh xem danh sach module va driver dang active.

Acceptance criteria:

- PC app doc duoc status cua tung module va co the debug mot register cu the.
- Khong vo layout status 29 byte hien tai.

### 9.5 Sprint E - Safety policy va recovery

Muc tieu:

- Khong chi chay duoc, ma chay theo chinh sach an toan co the giai thich duoc.

Cong viec:

- Chot dieu kien vao start: voltage on dinh, alarm critical = false, bus online.
- Them ramp current theo buoc, co gioi han toc do tang.
- Nhung alarm critical phai goi emergency stop ngay lap tuc.
- Phan loai offline do mat lien lac va fault do loi thiet bi.
- Tien hanh recovery co giay do, retry limit, va co log.

Acceptance criteria:

- Khong con start truc tiep khi module chua san sang.
- Moi transition an toan deu co log va co the truy vet.

### 9.6 Sprint F - Verification va regression

Muc tieu:

- Co bo test bench va regression toi thieu truoc khi dua ra module that.

Cong viec:

- Tao mock CAN loopback cho ca Maxwell va Lianming.
- Tao test cases cho set voltage, set current, start, poll, stop, emergency stop, timeout, recovery.
- So sanh frame TX/RX voi document PDF.
- Kiem tra lai status report o PC app sau moi lan thay doi struct.
- Chot mot checklist bring-up cho hardware that.

Acceptance criteria:

- Co it nhat mot duong chay test khong can module that.
- Co checklist hardware de team bench test lam theo.

### 9.7 Sprint G - Tai lieu va dong goi

Muc tieu:

- Dong bo source, comment, README, va tai lieu de project duoc tiep nhan lai duoc trong tuong lai.

Cong viec:

- Cap nhat README va project document theo kien truc moi.
- Danh dau ro phan demo cu, phan chinh thuc, va phan de thu nghiem.
- Ghi lai version protocol dang dung cho firmware va PC app.
- Neu co thay doi layout, chot luon cach phoi hop voi code sinh giao dien.

Acceptance criteria:

- Nguoi moi vao project co the doc tai lieu va biet phai sua file nao truoc.
- Khong con comment moi file noi mot kieu.

## 10. Thu tu uu tien de lam tiep

1. Chot canonical contract va danh sach register that su dung.
2. Hoan thien driver layer cho Lianming va Maxwell tren cung chung contract.
3. Mo rong app orchestration cho multi-module va safety policy.
4. Hoan thien PC/HMI contract cho debug va van hanh.
5. Tao test bench, mock, va checklist cho hardware.
6. Dong bo tai lieu, README, va goi ten file/ham.

## 11. Dinh nghia xong viec

Mot phase chi duoc coi la xong khi:

- Build xanh.
- Khong con API mo treo hoac comment chua dong bo voi code.
- Co log/trace toi thieu cho cac state transition quan trong.
- Co it nhat mot bai test hoac kich ban bench phu hop.
- PC app va firmware khong con lech layout du lieu chinh.
